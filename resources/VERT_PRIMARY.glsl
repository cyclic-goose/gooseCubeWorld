#version 460 core

// We read 2 uints per vertex now (PackedVertex is 8 bytes)
// .x = data1 (coords + normal), .y = data2 (texture)
layout (std430, binding = 0) readonly buffer VoxelData {
    uvec2 packedVertices[];
};

uniform mat4 u_ViewProjection;
uniform vec3 u_ChunkOffset; // Changed to vec3 to match C++ glUniform3f

// outputs to fragment shader
out vec3 v_Normal;
out vec2 v_TexCoord;
out float v_TexID;
out vec3 v_Color; // Debug color

// lookup table for normals (matching C++ face order: +X, -X, +Y, -Y, +Z, -Z)
vec3 getCubeNormal(int i) {
    const vec3 normals[6] = vec3[](
        vec3( 1, 0, 0), vec3(-1, 0, 0), // 0: +X, 1: -X
        vec3( 0, 1, 0), vec3( 0,-1, 0), // 2: +Y, 3: -Y
        vec3( 0, 0, 1), vec3( 0, 0,-1)  // 4: +Z, 5: -Z
    );
    
    if (i < 0 || i > 5) return vec3(0, 1, 0); // Safety default
    return normals[i];
}

void main() {
    // 1. Fetch the 64-bit vertex data (as two 32-bit uints)
    uvec2 rawData = packedVertices[gl_VertexID];
    
    // 2. Unpack Word 0 (Coords + Normal)
    // Layout: | Unused(11) | Axis(3) | Z(6) | Y(6) | X(6) |
    float x = float(bitfieldExtract(rawData.x, 0,  6));
    float y = float(bitfieldExtract(rawData.x, 6,  6));
    float z = float(bitfieldExtract(rawData.x, 12, 6));
    int normIndex = int(bitfieldExtract(rawData.x, 18, 3));
    
    // 3. Unpack Word 1 (Texture + Unused)
    // Layout: | Unused(16) | Texture(16) |
    int texID = int(bitfieldExtract(rawData.y, 0, 16));

    // 4. Transform
    vec3 localPos = vec3(x, y, z);
    vec3 worldPos = localPos + u_ChunkOffset;

    // 5. Outputs
    v_Normal = getCubeNormal(normIndex);
    v_TexID = float(texID);
    
    // Simple Tri-planar UV generation based on normal
    // If normal is pointing X, use YZ coords. If Y, use XZ. If Z, use XY.
    if (abs(v_Normal.x) > 0.5) v_TexCoord = localPos.yz;
    else if (abs(v_Normal.y) > 0.5) v_TexCoord = localPos.xz;
    else v_TexCoord = localPos.xy;

    // Debug color based on normal so you can see faces clearly
    v_Color = v_Normal * 0.5 + 0.5; 

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}
