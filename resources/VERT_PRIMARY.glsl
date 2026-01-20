#version 460 core

// Binding 0: Giant Geometry Heap (The VRAM Manager)
layout (std430, binding = 0) readonly buffer VoxelData {
    uvec2 packedVertices[];
};

// Binding 1: Chunk Position Lookup (Per Draw Command)
layout (std430, binding = 1) readonly buffer ChunkOffsets {
    vec4 chunkPositions[]; // .w is padding/unused
};

uniform mat4 u_ViewProjection;

out vec3 v_Normal;
out vec2 v_TexCoord;
out float v_TexID;
out vec3 v_Color;

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
    uvec2 rawData = packedVertices[gl_VertexID];
    
    // Unpack Position (0-63)
    float x = float(bitfieldExtract(rawData.x, 0,  6));
    float y = float(bitfieldExtract(rawData.x, 6,  6));
    float z = float(bitfieldExtract(rawData.x, 12, 6));
    
    // Unpack Normals & Texture
    int normIndex = int(bitfieldExtract(rawData.x, 18, 3));
    int texID = int(bitfieldExtract(rawData.y, 0, 16));

    vec3 localPos = vec3(x, y, z);
    
    // MDI MAGIC:
    // gl_BaseInstance is set by the Indirect Command Buffer in C++.
    // It points to the index in 'chunkPositions' for this specific chunk.
    vec3 chunkOffset = chunkPositions[gl_BaseInstance].xyz;
    vec3 worldPos = localPos + chunkOffset;

    v_Normal = getCubeNormal(normIndex);
    v_TexID = float(texID);
    
    // Simple Tri-planar UVs
    if (abs(v_Normal.x) > 0.5) v_TexCoord = localPos.yz;
    else if (abs(v_Normal.y) > 0.5) v_TexCoord = localPos.xz;
    else v_TexCoord = localPos.xy;

    v_Color = v_Normal * 0.5 + 0.5; 

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}