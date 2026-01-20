#version 460 core

// we read directly from this array, no "in" vec required 
layout (std430, binding = 0) readonly buffer VoxelData {
    uint packedVertices[];
};

uniform mat4 u_ViewProjection;
uniform ivec3 u_ChunkOffset; // the world position of the Chunk

// outputs to fragment shader
out vec3 v_Normal;
out vec2 v_TexCoord;
out float v_TexID;

// lookup table for normals (up, down, left, right, front, back)
vec3 getCubeNormal(int i) {
    const vec3 normals[6] = vec3[](
        vec3( 0, 1, 0), vec3(0, -1, 0),
        vec3( -1, 0, 0), vec3( 1, 0, 0),
        vec3( 0, 0, 1), vec3( 0, 0, 1)
    );
    return normals[i];
}

void main() {
    //  PULLING LOGIC
    // gl_VertexID is provided by the system. It corresponds to the index 
    // in the draw call (0 to count-1).
    uint rawData = packedVertices[gl_VertexID];

    //  UNPACKING
    // bitfieldExtract(value, offset, bits)
    float x = float(bitfieldExtract(rawData, 0, 6));
    float y = float(bitfieldExtract(rawData, 6, 6));
    float z = float(bitfieldExtract(rawData, 12, 6));
    
    int normIndex = int(bitfieldExtract(rawData, 18, 3));
    int texID     = int(bitfieldExtract(rawData, 21, 11));

    //  TRANSFORM
    // Calculate local position
    vec3 localPos = vec3(x, y, z);
    
    // Calculate world position
    vec3 worldPos = localPos + vec3(u_ChunkOffset);

    //  OUTPUT
    v_Normal = getCubeNormal(normIndex);
    v_TexID = float(texID);
    
    // Simple planar UVs based on position (optional)
    v_TexCoord = localPos.xz; 

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}
