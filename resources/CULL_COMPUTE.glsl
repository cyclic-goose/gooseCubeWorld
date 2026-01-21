#version 460 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// --- CONFIGURATION ---
// #define ENABLE_OCCLUSION 
// #define ZERO_TO_ONE_DEPTH // Use this for glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)

// --- INPUTS ---

struct ChunkGpuData {
    vec4 minAABB_scale; 
    vec4 maxAABB_pad;   
    uint firstVertex;
    uint vertexCount;   
    uint pad1;
    uint pad2;      
};

// Binding 0: Persistent global buffer
layout(std430, binding = 0) readonly buffer GlobalBuffer {
    ChunkGpuData allChunks[];
};

uniform mat4 u_ViewProjection;
uniform uint u_MaxChunks;

#ifdef ENABLE_OCCLUSION
uniform sampler2D u_DepthPyramid;
#endif

// --- OUTPUTS ---

struct DrawCommand {
    uint count;
    uint instanceCount;
    uint first;
    uint baseInstance;
};

// Binding 1: Draw Commands
layout(std430, binding = 1) writeonly buffer OutputDrawCommands {
    DrawCommand outCommands[];
};

// Binding 2: Offsets for Vertex Shader
layout(std430, binding = 2) writeonly buffer OutputChunkOffsets {
    vec4 outChunkOffsets[];
};

// Binding 0: Atomic Counter
layout(binding = 0, offset = 0) uniform atomic_uint u_VisibleCount;

// --- FRUSTUM LOGIC ---

bool IsVisible(vec3 minPos, vec3 maxPos) {
    // Extract frustum planes. 
    // Gribb-Hartmann extraction.
    // Rows: 0=Left/Right, 1=Top/Bottom, 2=Near/Far, 3=W
    mat4 M = transpose(u_ViewProjection);
    vec4 planes[6];

    planes[0] = M[3] + M[0]; // Left
    planes[1] = M[3] - M[0]; // Right
    planes[2] = M[3] + M[1]; // Bottom
    planes[3] = M[3] - M[1]; // Top

#ifdef ZERO_TO_ONE_DEPTH
    // GL_ZERO_TO_ONE: Near is z >= 0, Far is z <= w
    // Row 2 is Z. 
    planes[4] = M[2];        // Near (0 <= z)
    planes[5] = M[3] - M[2]; // Far  (z <= w)
#else
    // Standard GL: Near is z >= -w, Far is z <= w
    planes[4] = M[3] + M[2]; // Near
    planes[5] = M[3] - M[2]; // Far
#endif

    for(int i = 0; i < 6; i++) {
        vec3 normal = planes[i].xyz;
        // Positive vertex (p-vertex)
        vec3 p = minPos;
        if(normal.x >= 0) p.x = maxPos.x;
        if(normal.y >= 0) p.y = maxPos.y;
        if(normal.z >= 0) p.z = maxPos.z;

        if(dot(vec4(p, 1.0), planes[i]) < 0.0) {
            return false;
        }
    }
    return true;
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_MaxChunks) return;

    // Direct access to persistent buffer
    ChunkGpuData chunk = allChunks[idx];

    // 1. Check if slot is ACTIVE
    // We use vertexCount == 0 to mark empty/unloaded slots
    if (chunk.vertexCount == 0) return;

    // 2. Frustum Culling
    if (IsVisible(chunk.minAABB_scale.xyz, chunk.maxAABB_pad.xyz)) {
        
        // 3. (Optional) Occlusion Culling
        #ifdef ENABLE_OCCLUSION
            // Reproject AABB center to screen UV, sample depth pyramid, compare.
            // (Skipped for brevity/stability unless depth pyramid is bound)
        #endif

        // Append to Indirect Buffer
        uint outIndex = atomicCounterIncrement(u_VisibleCount);

        DrawCommand cmd;
        cmd.count = chunk.vertexCount;
        cmd.instanceCount = 1;
        cmd.first = chunk.firstVertex;
        cmd.baseInstance = outIndex; // Maps to index in OutputChunkOffsets
        outCommands[outIndex] = cmd;

        // Output data required by Vertex Shader
        outChunkOffsets[outIndex] = vec4(chunk.minAABB_scale.xyz, chunk.minAABB_scale.w);
    }
}