#version 460 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// --- INPUTS ---

// Structure matching the C++ ChunkCandidate struct
struct ChunkCandidate {
    vec4 minAABB_scale; // xyz = min bounds, w = scale
    vec4 maxAABB_pad;   // xyz = max bounds, w = padding
    uvec2 vertexData;   // x = firstVertex, y = vertexCount
    uvec2 padding;      // Alignment padding
};

// The read-only buffer containing ALL chunks currently in memory
layout(std430, binding = 0) readonly buffer CandidateBuffer {
    ChunkCandidate candidates[];
};

// Uniforms
uniform mat4 u_ViewProjection;
uniform uint u_Count; // Total number of candidates

// --- OUTPUTS ---

// 1. Indirect Draw Command Structure
struct DrawCommand {
    uint count;
    uint instanceCount;
    uint first;
    uint baseInstance;
};

layout(std430, binding = 1) writeonly buffer OutputDrawCommands {
    DrawCommand outCommands[];
};

// 2. Chunk Positions/Scale for the Vertex Shader
// Matches VERT_PRIMARY.glsl: layout (std430, binding = 1) readonly buffer ChunkOffsets
layout(std430, binding = 2) writeonly buffer OutputChunkOffsets {
    vec4 outChunkOffsets[];
};

// 3. Atomic Counter for visible count (Used as Parameter Buffer)
layout(binding = 0, offset = 0) uniform atomic_uint u_VisibleCount;

// --- LOGIC ---

// Standard AABB-Frustum Intersection
bool IsVisible(vec3 minPos, vec3 maxPos) {
    // Extract frustum planes from ViewProjection matrix
    // Gribb-Hartmann method
    vec4 planes[6];
    mat4 M = transpose(u_ViewProjection);
    planes[0] = M[3] + M[0]; // Left
    planes[1] = M[3] - M[0]; // Right
    planes[2] = M[3] + M[1]; // Bottom
    planes[3] = M[3] - M[1]; // Top
    planes[4] = M[3] + M[2]; // Near
    planes[5] = M[3] - M[2]; // Far

    for(int i = 0; i < 6; i++) {
        // Find the p-vertex (positive vertex) in the direction of the normal
        vec3 normal = planes[i].xyz;
        vec3 p = minPos;
        if(normal.x >= 0) p.x = maxPos.x;
        if(normal.y >= 0) p.y = maxPos.y;
        if(normal.z >= 0) p.z = maxPos.z;

        // If p is behind the plane, the box is outside
        if(dot(vec4(p, 1.0), planes[i]) < 0.0) {
            return false;
        }
    }
    return true;
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_Count) return;

    ChunkCandidate chunk = candidates[idx];

    // Culling Check
    if (IsVisible(chunk.minAABB_scale.xyz, chunk.maxAABB_pad.xyz)) {
        // Atomic Add to get the unique index for this visible chunk
        uint outIndex = atomicCounterIncrement(u_VisibleCount);

        // 1. Write Draw Command
        // We set instanceCount to 1, letting the GPU draw this mesh
        DrawCommand cmd;
        cmd.count = chunk.vertexData.y;
        cmd.instanceCount = 1;
        cmd.first = chunk.vertexData.x;
        cmd.baseInstance = outIndex; // Important: Maps to the correct offset in OutputChunkOffsets
        outCommands[outIndex] = cmd;

        // 2. Write Chunk Data for Vertex Shader
        // VERT_PRIMARY needs vec4(pos.x, pos.y, pos.z, scale)
        outChunkOffsets[outIndex] = vec4(chunk.minAABB_scale.xyz, chunk.minAABB_scale.w);
    }
}