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
uniform float u_Time; // Time is now required for wind!

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
    
    // --- FLAG EXTRACTION UPDATE ---
    uint rawTexID = bitfieldExtract(data, 23, 9);
    
    bool isAnimated = (rawTexID & 0x100u) != 0u; // Check Bit 8 (Value 256)
    int texID = int(rawTexID & 0xFFu);           // Strip Bit 8 to get real ID

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

    // --- REALISTIC WIND ANIMATION ---
    // Only apply to blocks flagged as animated (Leaves/Water)
    // We assume water is handled in a separate shader pass or by specific ID check if needed,
    // but this logic works specifically well for vegetation.
    if (isAnimated) {
        // 1. Determine Intensity (Anchoring)
        // We want the TOP of the block to move, but the BOTTOM to stay fixed (attached to tree).
        // Since we disabled greedy meshing for leaves, every leaf is a 1x1x1 cube (6 quads).
        // We can infer "Top" vertices using the vertex index pattern and normal direction.
        
        bool isTopVertex = false;
        int vertIdx = gl_VertexID % 6; // 6 vertices per face
        
        // Case A: Top Face (Normal +Y) -> Entire face moves
        if (normal.y > 0.5) {
            isTopVertex = true;
        }
        // Case B: Bottom Face (Normal -Y) -> Entire face is anchored (No move)
        else if (normal.y < -0.5) {
            isTopVertex = false;
        }
        // Case C: Side Faces (Normal X or Z) -> Only the top 2 vertices move
        else {
            // Based on greedy meshing strip order:
            // For +X and -Z faces, indices 1, 4, 5 are the top vertices.
            // For -X and +Z faces, indices 2, 4, 5 are the top vertices.
            bool useSetA = (normal.x > 0.5) || (normal.z < -0.5);
            
            if (useSetA) isTopVertex = (vertIdx == 1 || vertIdx == 4 || vertIdx == 5);
            else         isTopVertex = (vertIdx == 2 || vertIdx == 4 || vertIdx == 5);
        }

        // 2. Apply Wind if it is a Top Vertex
        if (isTopVertex) {
            float time = u_Time;
            
            // Phase shift based on world position (prevents unison movement)
            float phase = trueWorldPos.x * 0.5 + trueWorldPos.z * 0.5;
            
            // Primary Sway (Large, Slow)
            float swayX = sin(time * 1.5 + phase) * 0.1;
            float swayZ = cos(time * 1.2 + phase * 0.8) * 0.1;
            
            // Secondary Rustle (Small, Fast) - Adds the "shivering" effect
            float rustle = sin(time * 5.0 + trueWorldPos.y) * 0.02;

            // Apply deformation
            // We apply to X and Z for shear.
            // We also add slight Y bobbing for "breathing" effect.
            renderPos.x += swayX + rustle;
            renderPos.z += swayZ + rustle;
            renderPos.y += sin(time * 2.0 + phase) * 0.03; // Slight breathing
        }
    }

    // outputs
    v_Normal = normal;
    v_TexID = texID; 
    v_FragPos = trueWorldPos; 
    v_AO = float(aoVal) / 3.0; 

    // --- TEXTURE MAPPING FIX ---
    // auto-uv mapping based on world position
    if (abs(normal.x) > 0.5) {
        v_TexCoord = trueWorldPos.zy; 
    } 
    else if (abs(normal.y) > 0.5) {
        v_TexCoord = trueWorldPos.xz;
    } 
    else {
        v_TexCoord = trueWorldPos.xy;
    }

    v_Color = vec3(1.0); 

    gl_Position = u_ViewProjection * vec4(renderPos, 1.0);
}