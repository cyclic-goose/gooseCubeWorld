#version 460 core

// Binding 0: Packed Voxel Data (1x uint32 per vertex)
layout (std430, binding = 0) readonly buffer VoxelData {
    uint packedVertices[];
};

// Binding 2: Per-Chunk transform/scale data
// This buffer contains PACKED vec4s (16 bytes per chunk).
// Do NOT use a struct here, or the stride will mismatch the C++ buffer.
layout (std430, binding = 2) readonly buffer ChunkOffsets {
    vec4 chunkPositions[]; 
};

uniform mat4 u_ViewProjection;

// OUTPUTS
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_Color;
out float v_AO; 

flat out int v_TexID; 

vec3 getCubeNormal(int i) {
    const vec3 normals[6] = vec3[](
        vec3( 1, 0, 0), vec3(-1, 0, 0),
        vec3( 0, 1, 0), vec3( 0,-1, 0),
        vec3( 0, 0, 1), vec3( 0, 0,-1) 
    );
    if (i < 0 || i > 5) return vec3(0, 1, 0); 
    return normals[i];
}

void main() {
    // 1. Fetch Data
    uint data = packedVertices[gl_VertexID];

    // 2. Unpack Geometry
    float x = float(bitfieldExtract(data, 0,  6));
    float y = float(bitfieldExtract(data, 6,  6));
    float z = float(bitfieldExtract(data, 12, 6));
    
    // 3. Unpack Attributes
    int normIndex = int(bitfieldExtract(data, 18, 3));
    int aoVal     = int(bitfieldExtract(data, 21, 2));
    int texID     = int(bitfieldExtract(data, 23, 9));

    vec3 localPos = vec3(x, y, z);
    vec3 normal = getCubeNormal(normIndex);

    // 4. World Position Calculation
    // We access the vec4 directly using swizzles (.xyz, .w)
    vec4 chunkData = chunkPositions[gl_BaseInstance];
    
    vec3 chunkOffset = chunkData.xyz;
    float scale = chunkData.w;

    vec3 trueWorldPos = (localPos * scale) + chunkOffset;
    
    // 5. Outputs
    v_Normal = normal;
    v_TexID = texID; 
    v_AO = float(aoVal) / 3.0; 
    
    // UV GENERATION
    if (abs(normal.x) > 0.5) {
        v_TexCoord = trueWorldPos.yz; 
    } else if (abs(normal.y) > 0.5) {
        v_TexCoord = trueWorldPos.xz;
    } else {
        v_TexCoord = trueWorldPos.xy;
    }

    v_Color = vec3(1.0); 

    gl_Position = u_ViewProjection * vec4(trueWorldPos, 1.0);
}