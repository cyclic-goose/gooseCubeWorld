#version 460 core

layout (std430, binding = 0) readonly buffer VoxelData {
    uvec2 packedVertices[];
};

layout (std430, binding = 1) readonly buffer ChunkOffsets {
    vec4 chunkPositions[]; 
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
    
    // Unpack with 128 offset
    float x = float(bitfieldExtract(rawData.x, 0,  8)) - 128.0;
    float y = float(bitfieldExtract(rawData.x, 8,  8)) - 128.0;
    float z = float(bitfieldExtract(rawData.x, 16, 8)) - 128.0;
    
    int normIndex = int(bitfieldExtract(rawData.x, 24, 3));
    int texID = int(bitfieldExtract(rawData.y, 0, 16));

    vec3 localPos = vec3(x, y, z);
    
    vec3 chunkOffset = chunkPositions[gl_BaseInstance].xyz;
    float scale = chunkPositions[gl_BaseInstance].w;

    vec3 worldPos = (localPos * scale) + chunkOffset;

    // SINKING LOGIC:
    // If we are drawing a lower-detail chunk, it means the high-detail one is missing.
    // We sink it slightly to prevent z-fighting if they partially overlap during transition.
    // However, if strict culling is working, overlap should be minimal.
    // We sink by a larger amount (scale * 2.0) to be safe.
    if (scale > 1.0) {
        worldPos.y -= (scale * 2.0); 
    }

    v_Normal = getCubeNormal(normIndex);
    v_TexID = float(texID);
    
    if (abs(v_Normal.x) > 0.5) v_TexCoord = worldPos.yz * 0.5; 
    else if (abs(v_Normal.y) > 0.5) v_TexCoord = worldPos.xz * 0.5;
    else v_TexCoord = worldPos.xy * 0.5;

    v_Color = v_Normal * 0.5 + 0.5; 

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}