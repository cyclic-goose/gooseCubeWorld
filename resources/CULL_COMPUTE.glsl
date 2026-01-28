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
bool IsOccluded(vec3 minAABB, vec3 maxAABB) {
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
    vec2 minUV = vec2(1.0);
    vec2 maxUV = vec2(0.0);
    float maxZ = 0.0; // Closest Z (Reverse-Z: 1.0 is near)
    
    bool intersectsNearPlane = false;
    bool anyVisible = false;

    for(int i = 0; i < 8; i++) {
        vec4 clipPos = u_PrevViewProjection * vec4(corners[i], 1.0);
        
        if (clipPos.w < 0.001) {
            intersectsNearPlane = true; 
            break;
        }

        vec3 ndc = clipPos.xyz / clipPos.w;
        
        if (ndc.z > 1.0) {
            intersectsNearPlane = true;
            break;
        }

        vec2 uv = ndc.xy * 0.5 + 0.5;
        minUV = min(minUV, uv);
        maxUV = max(maxUV, uv);
        maxZ = max(maxZ, ndc.z);
        anyVisible = true;
    }
    
    if (intersectsNearPlane) return false;
    if (!anyVisible) return false; 

    minUV = clamp(minUV, 0.0, 1.0);
    maxUV = clamp(maxUV, 0.0, 1.0);

    // 3. Calculate Dimensions & LOD
    vec2 dims = (maxUV - minUV) * u_PyramidSize;
    float maxDim = max(dims.x, dims.y);
    
    // [TWEAK HERE] LOD Bias
    // -1.0 is standard. -2.0 is Aggressive.
    // -2.0 forces the shader to sample a texture that is 2x larger (higher res).
    // This allows it to see "thinner" occluders (like distant mountains) without
    // them getting blurred into the sky.
    float lod = clamp(log2(maxDim) - 3.0, 0.0, 10.0);

    // 4. Sample Hi-Z (5 Taps: Corners + Center)
    float d1 = textureLod(u_DepthPyramid, vec2(minUV.x, minUV.y), lod).r;
    float d2 = textureLod(u_DepthPyramid, vec2(maxUV.x, minUV.y), lod).r;
    float d3 = textureLod(u_DepthPyramid, vec2(minUV.x, maxUV.y), lod).r;
    float d4 = textureLod(u_DepthPyramid, vec2(maxUV.x, maxUV.y), lod).r;
    float d5 = textureLod(u_DepthPyramid, (minUV + maxUV) * 0.5, lod).r;
    
    float furthestOccluder = min(d5, min(min(d1, d2), min(d3, d4)));
    
    // 5. Compare
    // high value can cause potentially more flicker, but be less Aggressive??? // actually 0.00001 was far more aggresive than 0.002
    return maxZ < (furthestOccluder - 0.00001);
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_MaxChunks) return;

    ChunkGpuData chunk = allChunks[idx];
    if (chunk.vertexCount == 0) return;

    if (IsFrustumVisible(chunk.minAABB_scale.xyz, chunk.maxAABB_pad.xyz)) {
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