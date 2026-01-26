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

uniform mat4 u_ViewProjection;     // CURRENT Frame (For Frustum Culling)
uniform mat4 u_PrevViewProjection; // PREVIOUS Frame (For Occlusion Reprojection)
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

    // 2. Project to Screen Space using PREVIOUS Matrix
    // We use Previous Matrix because the Hi-Z buffer contains the depth of the Previous Frame.
    // If we use Current Matrix, the AABB screen coords won't align with the depth buffer.
    vec2 minUV = vec2(1.0);
    vec2 maxUV = vec2(0.0);
    float maxZ = 0.0; // Closest Z (Reverse-Z: 1.0 is near)

    bool visibleInPrevFrame = false;

    for(int i = 0; i < 8; i++) {
        vec4 clipPos = u_PrevViewProjection * vec4(corners[i], 1.0);
        
        // If a point is behind the camera (w <= epsilon), the projection is invalid.
        if (clipPos.w > 0.001) {
            vec3 ndc = clipPos.xyz / clipPos.w;
            vec2 uv = ndc.xy * 0.5 + 0.5;
            
            // Check if point was on screen in previous frame
            // We use a slight margin (0.0 - 1.0)
            if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
                visibleInPrevFrame = true;
            }

            minUV = min(minUV, uv);
            maxUV = max(maxUV, uv);
            
            // Reverse-Z: The point closest to camera has the LARGEST Z value.
            maxZ = max(maxZ, ndc.z);
        }
    }
    
    // SAFETY: If the object was totally off-screen or behind the camera in the last frame,
    // we have no depth data for it. We must ASSUME VISIBLE (return false) to avoid popping.
    if (!visibleInPrevFrame) return false; 

    // Clamp UVs to screen
    minUV = clamp(minUV, 0.0, 1.0);
    maxUV = clamp(maxUV, 0.0, 1.0);

    // 3. Calculate Dimensions & LOD
    // Convert UV size to Pixel size
    vec2 dims = (maxUV - minUV) * u_PyramidSize;
    float maxDim = max(dims.x, dims.y);
    
    // Select LOD
    // We subtract 1.0 to pick a higher-resolution mip level (better accuracy for distant objects).
    float lod = clamp(floor(log2(maxDim) - 1.0), 0.0, 10.0);

    // 4. Sample Hi-Z
    float d1 = textureLod(u_DepthPyramid, vec2(minUV.x, minUV.y), lod).r;
    float d2 = textureLod(u_DepthPyramid, vec2(maxUV.x, minUV.y), lod).r;
    float d3 = textureLod(u_DepthPyramid, vec2(minUV.x, maxUV.y), lod).r;
    float d4 = textureLod(u_DepthPyramid, vec2(maxUV.x, maxUV.y), lod).r;
    
    // Conservative Reduction (Reverse-Z): Furthest = Minimum Value.
    float furthestOccluder = min(min(d1, d2), min(d3, d4));
    
    // 5. Compare
    // Is the object BEHIND the furthest occluder?
    // We add a tiny epsilon to maxZ to prevent Z-fighting artifacts.
    return maxZ < furthestOccluder;
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_MaxChunks) return;

    ChunkGpuData chunk = allChunks[idx];
    if (chunk.vertexCount == 0) return;

    // 1. Frustum Culling using CURRENT Matrix
    // We only care if the chunk is in the CURRENT frustum.
    if (IsFrustumVisible(chunk.minAABB_scale.xyz, chunk.maxAABB_pad.xyz)) {
        
        // 2. Occlusion Culling using PREVIOUS Matrix
        // We check if it was visible in the LAST frame.
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