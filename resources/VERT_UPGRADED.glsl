#version 460 core

// Binding 0: Packed Voxel Data (1x uint32 per vertex)
layout (std430, binding = 0) readonly buffer VoxelData {
    uint packedVertices[];
};

// Binding 2: Per-Chunk transform/scale data
layout (std430, binding = 2) readonly buffer ChunkOffsets {
    vec4 chunkPositions[]; 
};

uniform mat4 u_ViewProjection;

// --- OUTPUTS ---
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_Color;
out float v_AO; 
out vec3 v_FragPos; // REQUIRED: Passed to Frag shader for Fog/Sun logic

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

    // Unpack Geometry
    float x = float(bitfieldExtract(data, 0,  6));
    float y = float(bitfieldExtract(data, 6,  6));
    float z = float(bitfieldExtract(data, 12, 6));
    
    // Unpack Attributes
    int normIndex = int(bitfieldExtract(data, 18, 3));
    int aoVal     = int(bitfieldExtract(data, 21, 2));
    int texID     = int(bitfieldExtract(data, 23, 9));

    vec3 localPos = vec3(x, y, z);
    vec3 normal = getCubeNormal(normIndex);

    // World Position Calculation
    vec3 chunkOffset = chunkPositions[gl_BaseInstance].xyz;
    float scale = chunkPositions[gl_BaseInstance].w;
    vec3 trueWorldPos = (localPos * scale) + chunkOffset;
    
    // Sinking Logic for LOD
    vec3 renderPos = trueWorldPos;
    if (scale > 1.0) {
        renderPos.y -= (scale * 1.1);
    }

    // Outputs
    v_Normal = normal;
    v_TexID = texID; 
    v_FragPos = trueWorldPos; // <--- This is the new vital piece
    v_AO = float(aoVal) / 3.0; 

    // Auto-UV mapping
    if (abs(normal.x) > 0.5) {
        v_TexCoord = trueWorldPos.yz; 
    } else if (abs(normal.y) > 0.5) {
        v_TexCoord = trueWorldPos.xz;
    } else {
        v_TexCoord = trueWorldPos.xy;
    }

    v_Color = vec3(1.0); 

    gl_Position = u_ViewProjection * vec4(renderPos, 1.0);
}