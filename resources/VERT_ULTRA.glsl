#version 460 core

// --- INPUTS ---
layout (std430, binding = 0) readonly buffer VoxelData {
    uint packedVertices[];
};

layout (std430, binding = 2) readonly buffer ChunkOffsets {
    vec4 chunkPositions[]; 
};

// --- UNIFORMS ---
uniform mat4 u_ViewProjection;
uniform float u_Time;
uniform float u_WindStrength;

// --- OUTPUTS ---
out vec3 v_Normal;
out vec2 v_TexCoord;
out vec3 v_Color;
out float v_AO;
out vec3 v_WorldPos; // Renamed from v_FragPos for clarity
flat out int v_TexID;

// --- CONSTANTS ---
const vec3 NORMALS[6] = vec3[](
    vec3( 1, 0, 0), vec3(-1, 0, 0),
    vec3( 0, 1, 0), vec3( 0,-1, 0),
    vec3( 0, 0, 1), vec3( 0, 0,-1) 
);

void main() {
    uint data = packedVertices[gl_VertexID];

    // 1. Unpack
    vec3 localPos = vec3(
        float(bitfieldExtract(data, 0,  6)),
        float(bitfieldExtract(data, 6,  6)),
        float(bitfieldExtract(data, 12, 6))
    );
    
    int normIndex = int(bitfieldExtract(data, 18, 3));
    int aoVal     = int(bitfieldExtract(data, 21, 2));
    uint rawTexID = bitfieldExtract(data, 23, 9);
    
    bool isAnimated = (rawTexID & 0x100u) != 0u; 
    int texID = int(rawTexID & 0xFFu);           

    vec3 normal = (normIndex >= 0 && normIndex <= 5) ? NORMALS[normIndex] : vec3(0,1,0);

    // 2. World Position Calculation
    vec3 chunkOffset = chunkPositions[gl_BaseInstance].xyz;
    float scale = chunkPositions[gl_BaseInstance].w;
    vec3 worldPos = (localPos * scale) + chunkOffset;
    
    // LOD Sinking fix
    if (scale > 1.0) worldPos.y -= (scale * 1.1);

    // 3. Wind Animation (Vegetation/Water)
    if (isAnimated) {
        // Only move top vertices of plants
        bool isTop = false;
        // Simple heuristic: if y is not 0 (relative to block bottom), it's a top vertex
        if (localPos.y > 0.1) isTop = true;

        if (isTop) {
            float t = u_Time * 1.5;
            float sway = sin(t + worldPos.x + worldPos.z) * 0.1 * u_WindStrength;
            float gust = sin(t * 3.0 + worldPos.y) * 0.05 * u_WindStrength;
            worldPos.x += sway + gust;
            worldPos.z += sway - gust;
        }
    }

    // 4. Tri-Planar / Auto UVs
    // (Preserving your existing logic which is good)
    if (abs(normal.x) > 0.5)      v_TexCoord = worldPos.zy; 
    else if (abs(normal.y) > 0.5) v_TexCoord = worldPos.xz; 
    else                          v_TexCoord = worldPos.xy;

    // 5. Outputs
    v_Normal = normal;
    v_TexID = texID;
    v_WorldPos = worldPos;
    v_AO = float(aoVal) / 3.0; // Normalized 0.0 - 1.0
    v_Color = vec3(1.0);       // Base vertex color

    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
}