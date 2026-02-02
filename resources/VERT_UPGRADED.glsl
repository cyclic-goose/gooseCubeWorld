#version 460 core

// binding 0: packed voxel data (1x uint32 per vertex)
layout (std430, binding = 0) readonly buffer VoxelData {
    uint packedVertices[];
};

// binding 2: per-chunk transform/scale data
layout (std430, binding = 2) readonly buffer ChunkOffsets {
    vec4 chunkPositions[]; 
};

uniform mat4 u_ViewProjection;

// --- outputs ---
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_Color;
out float v_AO; 
out vec3 v_FragPos; 

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
    uint data = packedVertices[gl_VertexID];

    // unpack geometry
    float x = float(bitfieldExtract(data, 0,  6));
    float y = float(bitfieldExtract(data, 6,  6));
    float z = float(bitfieldExtract(data, 12, 6));
    
    // unpack attributes
    int normIndex = int(bitfieldExtract(data, 18, 3));
    int aoVal     = int(bitfieldExtract(data, 21, 2));
    int texID     = int(bitfieldExtract(data, 23, 9));

    vec3 localPos = vec3(x, y, z);
    vec3 normal = getCubeNormal(normIndex);

    // world position calculation
    vec3 chunkOffset = chunkPositions[gl_BaseInstance].xyz;
    float scale = chunkPositions[gl_BaseInstance].w;
    vec3 trueWorldPos = (localPos * scale) + chunkOffset;
    
    // sinking logic for lod
    vec3 renderPos = trueWorldPos;
    if (scale > 1.0) {
        renderPos.y -= (scale * 1.1);
    }

    // outputs
    v_Normal = normal;
    v_TexID = texID; 
    v_FragPos = trueWorldPos; 
    v_AO = float(aoVal) / 3.0; 

    // --- TEXTURE MAPPING FIX ---
    // auto-uv mapping based on world position
    
    // case 1: side faces (x-axis)
    if (abs(normal.x) > 0.5) {
        // ROTATION FIX: changed .yz to .zy
        // normally, u=horizontal, v=vertical.
        // in world space, z is horizontal, y is vertical.
        // so we need u=z, v=y. 
        v_TexCoord = trueWorldPos.zy; 
    } 
    // case 2: top/bottom faces (y-axis)
    else if (abs(normal.y) > 0.5) {
        v_TexCoord = trueWorldPos.xz;
    } 
    // case 3: front/back faces (z-axis)
    else {
        v_TexCoord = trueWorldPos.xy;
    }

    v_Color = vec3(1.0); 

    gl_Position = u_ViewProjection * vec4(renderPos, 1.0);
}