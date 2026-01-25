#version 460 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// --- CONFIGURATION ---
#define ENABLE_OCCLUSION 
#define ZERO_TO_ONE_DEPTH // Matches C++ glClipControl

// --- INPUTS ---
struct ChunkGpuData {
    vec4 minAABB_scale; 
    vec4 maxAABB_pad;   
    uint firstVertex;
    uint vertexCount;   
    uint pad1;
    uint pad2;      
};

layout(std430, binding = 0) readonly buffer GlobalBuffer {
    ChunkGpuData allChunks[];
};

uniform mat4 u_ViewProjection;
uniform uint u_MaxChunks;

// Helper uniforms for Occlusion
uniform sampler2D u_DepthPyramid;
uniform vec2 u_PyramidSize; // Size of Mip 0
uniform float u_P00;        // Projection[0][0]
uniform float u_P11;        // Projection[1][1]
uniform float u_zNear;      // Camera Z Near
uniform float u_zFar;       // Camera Z Far
uniform bool u_OcclusionEnabled; 

// --- OUTPUTS ---
struct DrawCommand {
    uint count;
    uint instanceCount;
    uint first;
    uint baseInstance;
};

layout(std430, binding = 1) writeonly buffer OutputDrawCommands {
    DrawCommand outCommands[];
};

layout(std430, binding = 2) writeonly buffer OutputChunkOffsets {
    vec4 outChunkOffsets[];
};

layout(binding = 0, offset = 0) uniform atomic_uint u_VisibleCount;

// --- FRUSTUM LOGIC ---
bool IsFrustumVisible(vec3 minPos, vec3 maxPos) {
    mat4 M = transpose(u_ViewProjection);
    vec4 planes[6];
    planes[0] = M[3] + M[0]; // Left
    planes[1] = M[3] - M[0]; // Right
    planes[2] = M[3] + M[1]; // Bottom
    planes[3] = M[3] - M[1]; // Top
#ifdef ZERO_TO_ONE_DEPTH
    planes[4] = M[2];        // Near (0 <= z)
    planes[5] = M[3] - M[2]; // Far  (z <= w)
#else
    planes[4] = M[3] + M[2]; 
    planes[5] = M[3] - M[2]; 
#endif

    for(int i = 0; i < 6; i++) {
        vec3 p = minPos;
        if(planes[i].x >= 0) p.x = maxPos.x;
        if(planes[i].y >= 0) p.y = maxPos.y;
        if(planes[i].z >= 0) p.z = maxPos.z;
        if(dot(vec4(p, 1.0), planes[i]) < 0.0) return false;
    }
    return true;
}

// --- OCCLUSION LOGIC ---
// Robust 8-corner projection
bool IsOccluded(vec3 minAABB, vec3 maxAABB) {
    // 1. Define the 8 corners of the box
    vec3 corners[8];
    corners[0] = vec3(minAABB.x, minAABB.y, minAABB.z);
    corners[1] = vec3(maxAABB.x, minAABB.y, minAABB.z);
    corners[2] = vec3(minAABB.x, maxAABB.y, minAABB.z);
    corners[3] = vec3(maxAABB.x, maxAABB.y, minAABB.z);
    corners[4] = vec3(minAABB.x, minAABB.y, maxAABB.z);
    corners[5] = vec3(maxAABB.x, minAABB.y, maxAABB.z);
    corners[6] = vec3(minAABB.x, maxAABB.y, maxAABB.z);
    corners[7] = vec3(maxAABB.x, maxAABB.y, maxAABB.z);

    // 2. Project to Screen Space
    vec2 minUV = vec2(1.0);
    vec2 maxUV = vec2(0.0);
    float maxZ = 0.0; // Closest Z (Reverse-Z: 1.0 is near)

    // Check if any point is behind camera or too close. 
    // In our projection, W < epsilon means behind/clipping.
    // If the box clips the near plane, we simply assume it's visible.
    // It's cheaper than doing precise near-plane clipping on the GPU.
    bool clipping = false;

    for(int i = 0; i < 8; i++) {
        vec4 clipPos = u_ViewProjection * vec4(corners[i], 1.0);
        
        // If a point is behind the camera (w <= 0), the projection is invalid.
        // The object is likely intersecting the near plane -> Visible.
        if (clipPos.w <= 0.001) {
            clipping = true;
            break;
        }

        vec3 ndc = clipPos.xyz / clipPos.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;

        minUV = min(minUV, uv);
        maxUV = max(maxUV, uv);
        
        // Reverse-Z: The point closest to camera has the LARGEST Z value.
        // We want the Max Z of the object to compare against the buffer.
        maxZ = max(maxZ, ndc.z);
    }

    if (clipping) return false; // Visible (intersects near plane)

    // Clamp UVs to screen
    minUV = clamp(minUV, 0.0, 1.0);
    maxUV = clamp(maxUV, 0.0, 1.0);

    // 3. Calculate Dimensions & LOD
    // Convert UV size to Pixel size
    vec2 dims = (maxUV - minUV) * u_PyramidSize;
    float maxDim = max(dims.x, dims.y);
    
    // Select LOD
    // We want a mip level where our box is covered by roughly 2-4 texels.
    // floor(log2(maxDim)) ensures that 1 texel at LOD is smaller than or equal to maxDim.
    float lod = floor(log2(maxDim));

    // 4. Sample Hi-Z (4 samples for conservation)
    // We sample the corners of the UV bounding box at the chosen LOD.
    // This approximates the "Minimum Depth" (furthest distance) in the screen area covered by the object.
    
    float d1 = textureLod(u_DepthPyramid, vec2(minUV.x, minUV.y), lod).r;
    float d2 = textureLod(u_DepthPyramid, vec2(maxUV.x, minUV.y), lod).r;
    float d3 = textureLod(u_DepthPyramid, vec2(minUV.x, maxUV.y), lod).r;
    float d4 = textureLod(u_DepthPyramid, vec2(maxUV.x, maxUV.y), lod).r;
    
    // Conservative Reduction (Reverse-Z):
    // We want the FURTHEST occluder depth in the region.
    // In Reverse-Z (Near=1, Far=0), Furthest = Minimum Value.
    // If ANY pixel in the background is "Far" (e.g. 0.1), and our object is "Near" (e.g. 0.5),
    // then min(depths) will be 0.1.
    // 0.5 < 0.1 is False -> Object is Visible. (Correct)
    // We only cull if the object is BEHIND the furthest thing.
    float furthestOccluder = min(min(d1, d2), min(d3, d4));
    
    // 5. Compare
    // Is the object BEHIND the furthest occluder?
    // In Reverse-Z, "Behind" means "Smaller Z Value".
    // If (Object.ClosestZ < Occluder.FurthestZ), then the object is completely obscured.
    
    return maxZ < furthestOccluder;
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_MaxChunks) return;

    ChunkGpuData chunk = allChunks[idx];
    if (chunk.vertexCount == 0) return;

    // 1. Frustum Culling (Always do this first, it's cheap)
    if (IsFrustumVisible(chunk.minAABB_scale.xyz, chunk.maxAABB_pad.xyz)) {
        
        // 2. Occlusion Culling
        bool visible = true;
        if (u_OcclusionEnabled) {
             if (IsOccluded(chunk.minAABB_scale.xyz, chunk.maxAABB_pad.xyz)) {
                 visible = false;
             }
        }

        if (visible) {
            uint outIndex = atomicCounterIncrement(u_VisibleCount);

            DrawCommand cmd;
            cmd.count = chunk.vertexCount;
            cmd.instanceCount = 1;
            cmd.first = chunk.firstVertex;
            cmd.baseInstance = outIndex;
            outCommands[outIndex] = cmd;

            outChunkOffsets[outIndex] = vec4(chunk.minAABB_scale.xyz, chunk.minAABB_scale.w);
        }
    }
}