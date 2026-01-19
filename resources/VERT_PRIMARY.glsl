#version 460 core

// INPUT:
// We read directly from the SSBO buffer.
// uvec2 = 8 bytes = sizeof(PackedVertex) in C++.
// binding = 0 matches glBindBufferRange index.
layout (std430, binding = 0) readonly buffer VoxelData {
    uvec2 packedVertices[];
};

uniform mat4 u_ViewProjection;
uniform vec3 u_ChunkOffset; 

out vec3 v_Normal;
out vec2 v_TexCoord;
out float v_TexID;
out vec3 v_Color;

// Lookup table for normal vectors based on Face Index
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
    // 1. Fetch raw 64-bit data (split into 2x 32-bit uints)
    uvec2 rawData = packedVertices[gl_VertexID];
    
    // 2. UNPACK WORD 0 (Positions + Normal Index)
    // Matches PackedVertex::data1 logic
    float x = float(bitfieldExtract(rawData.x, 0,  6)); // Bits 0-5
    float y = float(bitfieldExtract(rawData.x, 6,  6)); // Bits 6-11
    float z = float(bitfieldExtract(rawData.x, 12, 6)); // Bits 12-17
    int normIndex = int(bitfieldExtract(rawData.x, 18, 3)); // Bits 18-20
    
    // 3. UNPACK WORD 1 (Texture ID)
    // Matches PackedVertex::data2 logic
    int texID = int(bitfieldExtract(rawData.y, 0, 16)); // Bits 0-15

    vec3 localPos = vec3(x, y, z);
    vec3 worldPos = localPos + u_ChunkOffset;

    v_Normal = getCubeNormal(normIndex);
    v_TexID = float(texID);
    
    // Tri-planar UV mapping
    // Generates UVs based on world position to avoid storing them.
    if (abs(v_Normal.x) > 0.5) v_TexCoord = localPos.yz;
    else if (abs(v_Normal.y) > 0.5) v_TexCoord = localPos.xz;
    else v_TexCoord = localPos.xy;

    // Tint color based on Normal (Cheap lighting)
    v_Color = v_Normal * 0.5 + 0.5; 

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}
