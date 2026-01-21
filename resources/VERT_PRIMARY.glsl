#version 460 core

// Binding 0: Packed Voxel Data (1x uint32 per vertex)
layout (std430, binding = 0) readonly buffer VoxelData {
    uint packedVertices[];
};

// Binding 1: Per-Chunk transform/scale data
layout (std430, binding = 1) readonly buffer ChunkOffsets {
    vec4 chunkPositions[]; 
};

uniform mat4 u_ViewProjection;

// OUTPUTS
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_Color;
out float v_AO; 

// CRITICAL: 'flat' ensures the integer ID is not interpolated. 
// Without this, ID 1.0 might become 0.999 across a face, casting to 0.
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
    // We add 0.5 to move from grid-aligned integers to pixel centers if needed, 
    // but here we just restore the float offsets.
    float x = float(bitfieldExtract(data, 0,  6)) - 8.0;
    float y = float(bitfieldExtract(data, 6,  6)) - 8.0;
    float z = float(bitfieldExtract(data, 12, 6)) - 8.0;
    
    // 3. Unpack Attributes
    int normIndex = int(bitfieldExtract(data, 18, 3));
    int aoVal     = int(bitfieldExtract(data, 21, 2));
    int texID     = int(bitfieldExtract(data, 23, 9));

    vec3 localPos = vec3(x, y, z);
    vec3 normal = getCubeNormal(normIndex);

    // 4. World Position Calculation
    // chunkPositions[gl_BaseInstance] is set by MultiDrawIndirect
    vec3 chunkOffset = chunkPositions[gl_BaseInstance].xyz;
    float scale = chunkPositions[gl_BaseInstance].w;

    vec3 trueWorldPos = (localPos * scale) + chunkOffset;
    
    // Sinking Logic for LOD blending (optional based on your engine logic)
    vec3 renderPos = trueWorldPos;
    if (scale > 1.0) {
        renderPos.y -= (scale * 2.0); 
    }

    // 5. Outputs
    v_Normal = normal;
    v_TexID = texID; 
    
    // AO: 0..3 maps to 0.0..1.0
    v_AO = float(aoVal) / 3.0; 
    
    // UV GENERATION
    // We use world coordinates for consistent tiling across chunks.
    // We assume 1.0 world unit = 1 texture repeat.
    // If your texture looks too small/big, multiply trueWorldPos by a factor here.
    if (abs(normal.x) > 0.5) {
        v_TexCoord = trueWorldPos.yz; 
    } else if (abs(normal.y) > 0.5) {
        v_TexCoord = trueWorldPos.xz;
    } else {
        v_TexCoord = trueWorldPos.xy;
    }

    // Pass White tint (can be modified for damage flashes, selection, etc.)
    v_Color = vec3(1.0); 

    gl_Position = u_ViewProjection * vec4(renderPos, 1.0);
}