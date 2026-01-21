#version 460 core

// 0 = Standard, 1 = Normals
uniform int u_DebugMode;

in vec3 v_Normal;
in vec2 v_TexCoord;
in float v_TexID;
in vec3 v_Color; // Now carries Tint from Vertex Shader
in float v_AO; 

out vec4 FragColor;

// Helper to get a debug color for missing textures
vec3 GetColorFromID(float id) {
    int i = int(id);
    if (i == 1) return vec3(0.2, 0.8, 0.2); // Green (Grass)
    if (i == 2) return vec3(0.6, 0.4, 0.2); // Brown (Dirt)
    if (i == 3) return vec3(0.5, 0.5, 0.5); // Grey (Stone)
    if (i == 4) return vec3(0.9, 0.9, 0.9); // White (Snow)
    if (i == 5) return vec3(0.8, 0.7, 0.5); // Sand
    return vec3(1.0, 0.0, 1.0);             // Magenta (Error)
}

void main()
{
    // --- DEBUG: NORMALS ---
    if (u_DebugMode == 1) {
        FragColor = vec4(v_Normal * 0.5 + 0.5, 1.0);
        return;
    }

    // --- STANDARD RENDER ---
    
    // 1. Base Color (Texture Placeholder)
    // Eventually replace GetColorFromID with: texture(u_TextureArray, vec3(v_TexCoord, v_TexID))
    vec3 albedo = GetColorFromID(v_TexID);
    
    // Add the grid pattern back for depth perception
    vec2 uv = v_TexCoord * 2.0; // Scale up grid
    bool pattern = (mod(floor(uv.x) + floor(uv.y), 2.0) == 0.0);
    if (!pattern) albedo *= 0.9; // Slight checkerboard

    // 2. Lighting (Simple Lambert)
    vec3 sunDir = normalize(vec3(0.2, 1.0, 0.3));
    float diff = max(dot(v_Normal, sunDir), 0.3); // 0.3 ambient

    // 3. Ambient Occlusion
    // Remap AO (0.0 - 1.0) to (0.4 - 1.0) so corners aren't pitch black
    float aoIntensity = 0.4 + (v_AO * 0.6);

    vec3 finalColor = albedo * diff * aoIntensity;

    FragColor = vec4(finalColor, 1.0);
}