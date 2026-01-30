#version 460 core

// --- UNIFORMS ---
uniform sampler2DArray u_Textures;
uniform int u_DebugMode;           // 0=Default, 1=Normals, 2=AO, 3=UVs

// --- INPUTS (Must match Vertex Shader) ---
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_Color; // Tint from vertex shader
in float v_AO;   // 0.0 (Occluded/Corner) -> 1.0 (Open/Face)
flat in int v_TexID;

out vec4 FragColor;

// --- FALLBACK COLORS ---
vec3 GetFallbackColor(int id) {
    switch (id) {
        // --- Basic Biomes ---
        case 1:  return vec3(0.13, 0.55, 0.13); // Grass (Standard Green)
        case 2:  return vec3(0.47, 0.33, 0.23); // Dirt (Brown)
        case 3:  return vec3(0.55, 0.55, 0.55); // Stone (Grey)
        case 4:  return vec3(0.95, 0.95, 0.98); // Snow (Near White)
        case 5:  return vec3(0.86, 0.78, 0.55); // Sand (Tan)

        // --- Required IDs ---
        case 6:  return vec3(0.20, 0.45, 0.90); // Water (Deep Blue)
        case 7:  return vec3(0.85, 0.95, 0.98); // Glass (Pale Cyan Tint)

        // --- Earthy Variations ---
        case 8:  return vec3(0.62, 0.65, 0.70); // Clay (Blue-Grey)
        case 9:  return vec3(0.50, 0.48, 0.48); // Gravel (Noise Grey)
        case 10: return vec3(0.35, 0.25, 0.20); // Mud (Dark Brown)
        case 11: return vec3(0.85, 0.65, 0.40); // Sandstone
        case 12: return vec3(0.60, 0.80, 0.95); // Ice (Light Blue)

        // --- Vegetation ---
        case 13: return vec3(0.36, 0.25, 0.15); // Wood (Oak Log)
        case 14: return vec3(0.15, 0.45, 0.10); // Leaves (Oak)
        case 15: return vec3(0.26, 0.18, 0.12); // Wood (Spruce/Dark Log)
        case 16: return vec3(0.15, 0.30, 0.20); // Leaves (Pine/Spruce)
        case 17: return vec3(0.45, 0.55, 0.20); // Marsh/Swamp Grass
        case 18: return vec3(0.75, 0.70, 0.35); // Dry Grass/Savanna

        // --- Volcanic & Deep ---
        case 19: return vec3(0.10, 0.10, 0.12); // Bedrock/Obsidian
        case 20: return vec3(0.90, 0.30, 0.05); // Lava (Bright Orange)
        case 21: return vec3(0.45, 0.05, 0.05); // Red Stone/Scorched Earth

        // --- Ores ---
        case 22: return vec3(1.00, 0.84, 0.00); // Gold
        case 23: return vec3(0.10, 0.80, 0.75); // Gem/Diamond
        case 24: return vec3(0.70, 0.35, 0.15); // Copper/Rust

        // --- Error/Fallback ---
        default: return vec3(1.0, 0.0, 1.0);    // Hot Pink (Error)
    }
}

void main()
{
    // --- DEBUG MODES ---
    if (u_DebugMode == 1) { FragColor = vec4(v_Normal * 0.5 + 0.5, 1.0); return; }
    if (u_DebugMode == 2) { FragColor = vec4(vec3(v_AO), 1.0); return; }
    if (u_DebugMode == 3) { FragColor = vec4(fract(v_TexCoord), 0.0, 1.0); return; }

    // --- 1. MATERIAL COLOR ---
    vec4 albedo;

    if (u_DebugMode == 0) {
        // ID 0 is Air, so Texture Layer 0 corresponds to Block ID 1
        float layer = float(max(0, v_TexID - 1)); 
        albedo = texture(u_Textures, vec3(v_TexCoord, layer));
        
        // PERFORMANCE FIX: 
        // 'discard' disables Early-Z culling. 
        // For opaque terrain (Dirt, Stone, etc.), NEVER use discard.
        // if (albedo.a < 0.1) discard; 
    } 
    else {
        albedo = vec4(GetFallbackColor(v_TexID), 1.0);

        // Procedural Edge Highlight (Lego Effect) for non-textured debug mode
        vec2 grid = fract(v_TexCoord);
        vec2 dist = min(grid, 1.0 - grid);
        float edgeFactor = min(dist.x, dist.y);
        if (edgeFactor < 0.02) albedo.rgb *= 0.85;
    }

    // --- 2. LIGHTING MODEL ---
    
    // A. Sun Direction
    vec3 sunDir = normalize(vec3(0.5, 0.7, 0.5)); 
    vec3 sunColor = vec3(1.0, 0.98, 0.9);
    
    // Diffuse term
    float diff = max(dot(v_Normal, sunDir), 0.0);

    // B. Hemispheric Ambient
    vec3 skyColor = vec3(0.65, 0.8, 0.95);   
    vec3 groundColor = vec3(0.25, 0.25, 0.3); 
    
    float hemiMix = v_Normal.y * 0.5 + 0.5; // Map -1..1 to 0..1
    vec3 ambient = mix(groundColor, skyColor, hemiMix) * 0.7; 

    // C. Ambient Occlusion
    // Remap AO (0.0-1.0) to (0.3-1.0) to prevent pitch black corners
    float aoFactor = mix(0.3, 1.0, v_AO);

    // D. Combine
    vec3 totalLight = (ambient * aoFactor) + (sunColor * diff * 1.1 * aoFactor);

    // --- 3. FINAL COMPOSITION ---
    vec3 finalColor = albedo.rgb * v_Color * totalLight;

    // Gamma Correction
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    FragColor = vec4(finalColor, 1.0);
}