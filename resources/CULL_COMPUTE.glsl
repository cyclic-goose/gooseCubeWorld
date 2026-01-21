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
bool IsOccluded(vec3 minAABB, vec3 maxAABB) {
    // 1. Calculate AABB Center and Extents
    vec3 center = (minAABB + maxAABB) * 0.5;
    vec3 extent = (maxAABB - minAABB) * 0.5;

    // 2. Project Center to Clip Space
    vec4 clipPos = u_ViewProjection * vec4(center, 1.0);
    // Don't cull if behind camera (W < epsilon)
    if (clipPos.w <= 0.001) return false; 
    
    // 3. To Screen Space (NDC -> 0..1)
    vec3 ndc = clipPos.xyz / clipPos.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    
    // 4. Calculate Screen-Space Bounding Box Size
    // We approximate the projected size of the AABB.
    // Width in Clip Space = (2 * WorldRadius * P00) / LinearDepth
    // This is an approximation treating the box as a sphere/cube.
    // For a more robust fit, we'd project all 8 corners, but that's expensive.
    float maxExtent = max(extent.x, max(extent.y, extent.z));
    
    // Linear Depth approximation (View Space Z)
    // We can roughly use clipPos.w as linear depth
    float boxSizePixels = max(
        (2.0 * maxExtent * u_P00 * u_PyramidSize.x) / clipPos.w,
        (2.0 * maxExtent * u_P11 * u_PyramidSize.y) / clipPos.w
    );

    // 5. Select Mip Level
    // We want a mip where 1 texel covers the entire AABB.
    // log2(size) gives us that level.
    float lod = floor(log2(boxSizePixels));

    // 6. Sample Depth Buffer
    // textureLod does the sampling. We rely on the conservative reduction (MIN) 
    // done in the Hi-Z generation shader.
    // Note: GL_LINEAR filtering on MIN-reduced maps can be incorrect (averaging).
    // Ensure the sampler is set to GL_NEAREST_MIPMAP_NEAREST.
    float occluderDepth = textureLod(u_DepthPyramid, uv, lod).r;

    // 7. Compare
    // Reverse-Z: Near = 1.0, Far = 0.0
    // Visible Logic: Object Closest Z >= Occluder Depth
    // Occluded Logic: Object Closest Z < Occluder Depth
    // "Closest Z" of the object is the NDC z coordinate + some safety margin.
    
    // Since we used 'center' for Z, we need to push it to the 'closest' face.
    // Conservative Approach: Use the raw NDC Z of the center, then nudge it towards camera.
    // Ideally, we'd project the nearest point of the AABB.
    float objectDepth = ndc.z; // This is center depth.
    
    // Simple heuristic: Assume object has radius in Z. Convert that radius to depth buffer delta?
    // Hard to do accurately in non-linear depth. 
    // Instead, we just use the center depth. If the center is occluded, the box is likely occluded.
    // To be safe, we add a bias.
    // Since Closer = Higher Value, we subtract bias to push it "back" (harder to cull).
    // Actually, we want to ensure we don't falsely cull.
    // If we use Center Z, and the front face is closer, we might cull a visible front face.
    // So we should use the CLOSEST point (Highest Z).
    // This assumes the box is un-rotated roughly.
    
    // Let's rely on a bias instead of exact corner projection for speed.
    // Since it's voxels, boxes are fairly uniform.
    
    // REVERSE Z: Closest point has HIGHER Z. 
    // We want to verify: Is (Object.Closest) < Occluder?
    // If we use Object.Center, Object.Closest is ~ Object.Center + RadiusDepth.
    // We'll just use Object.Center and a small tolerance.
    
    return objectDepth < (occluderDepth - 0.0005);
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