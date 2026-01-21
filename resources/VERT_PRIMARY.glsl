#version 460 core

// Binding 0 is now an array of uints (4 bytes), not uvec2 (8 bytes)
layout (std430, binding = 0) readonly buffer VoxelData {
    uint packedVertices[];
};

layout (std430, binding = 1) readonly buffer ChunkOffsets {
    vec4 chunkPositions[]; 
};

uniform mat4 u_ViewProjection;

out vec3 v_Normal;
out vec2 v_TexCoord;
out float v_TexID;
out vec3 v_Color;
out float v_AO; 

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
    // 1. Fetch 32-bit Data
    uint data = packedVertices[gl_VertexID];

    // 2. Unpack
    float x = float(bitfieldExtract(data, 0,  6)) - 8.0;
    float y = float(bitfieldExtract(data, 6,  6)) - 8.0;
    float z = float(bitfieldExtract(data, 12, 6)) - 8.0;
    
    int normIndex = int(bitfieldExtract(data, 18, 3));
    int aoVal     = int(bitfieldExtract(data, 21, 2));
    int texID     = int(bitfieldExtract(data, 23, 9));

    vec3 localPos = vec3(x, y, z);
    
    // 3. Chunk Processing
    vec3 chunkOffset = chunkPositions[gl_BaseInstance].xyz;
    float scale = chunkPositions[gl_BaseInstance].w;

    vec3 trueWorldPos = (localPos * scale) + chunkOffset;
    vec3 renderPos = trueWorldPos;

    // Sinking Logic
    if (scale > 1.0) {
        renderPos.y -= (scale * 2.0); 
    }

    v_Normal = getCubeNormal(normIndex);
    v_TexID = float(texID);
    
    // Unpack AO: 0..3 -> 0.0..1.0
    // 0 = Darkest (Corner), 3 = Brightest (Face)
    v_AO = float(aoVal) / 3.0; 
    
    // UV Logic
    if (abs(v_Normal.x) > 0.5) v_TexCoord = trueWorldPos.yz; 
    else if (abs(v_Normal.y) > 0.5) v_TexCoord = trueWorldPos.xz;
    else v_TexCoord = trueWorldPos.xy;

    // Default tint (White). We apply lighting/AO in Fragment shader now.
    v_Color = vec3(1.0); 

    gl_Position = u_ViewProjection * vec4(renderPos, 1.0);
}