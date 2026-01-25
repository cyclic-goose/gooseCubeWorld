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
        case 1: return vec3(0.2, 0.7, 0.2); // Grass
        case 2: return vec3(0.45, 0.3, 0.2); // Dirt
        case 3: return vec3(0.6, 0.6, 0.6); // Stone
        case 4: return vec3(0.95, 0.95, 0.95); // Snow
        case 5: return vec3(0.8, 0.7, 0.5); // Sand
        default: return vec3(1.0, 0.0, 1.0); // Magenta
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