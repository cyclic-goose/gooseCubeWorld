#version 460 core

layout (std430, binding = 0) readonly buffer VoxelData {
    uvec2 packedVertices[];
};

uniform mat4 u_ViewProjection;
uniform vec3 u_ChunkOffset; 

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
    
    float x = float(bitfieldExtract(rawData.x, 0,  6));
    float y = float(bitfieldExtract(rawData.x, 6,  6));
    float z = float(bitfieldExtract(rawData.x, 12, 6));
    int normIndex = int(bitfieldExtract(rawData.x, 18, 3));
    int texID = int(bitfieldExtract(rawData.y, 0, 16));

    vec3 localPos = vec3(x, y, z);
    vec3 worldPos = localPos + u_ChunkOffset;

    v_Normal = getCubeNormal(normIndex);
    v_TexID = float(texID);
    
    if (abs(v_Normal.x) > 0.5) v_TexCoord = localPos.yz;
    else if (abs(v_Normal.y) > 0.5) v_TexCoord = localPos.xz;
    else v_TexCoord = localPos.xy;

    v_Color = v_Normal; // Pass raw normal for debug tinting

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}
