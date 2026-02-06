#version 460 core

// --- UNIFORMS ---
uniform sampler2DArray u_Textures;
uniform int u_DebugMode;           // 0=Default, 1=Normals, 2=AO, 3=UVs

// NOTE: If you don't have this uniform yet, the fog will look static (won't move with player).
// You can temp replace this with vec3(0.0) inside main() if needed.
uniform vec3 u_CameraPos; 

// --- INPUTS ---
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_Color;
in float v_AO;
in vec3 v_FragPos;
flat in int v_TexID;

out vec4 FragColor;

// =========================================================
//                  TWEAKABLE VISUALS
// =========================================================

// 1. THE SUN
// Direction: (X, Y, Z). Y is height. normalize(vec3(0,1,0)) is noon.
const vec3 SUN_DIR = normalize(vec3(0.4, 0.6, 0.3)); 
// Color: Sunlight color. (1.0, 0.95, 0.8) is warm sunlight.
const vec3 SUN_COLOR = vec3(1.0, 0.95, 0.85);

// 2. SHADOWS / AMBIENT
// Sky Ambient: Color of shadows facing UP. Blue-ish looks best.
const vec3 AMBIENT_SKY = vec3(0.6, 0.75, 0.9);
// Ground Ambient: Color of shadows facing DOWN. Dark grey/brown.
const vec3 AMBIENT_GROUND = vec3(0.2, 0.2, 0.25);

// 3. FOG & ATMOSPHERE
// Fog Sky: The fog color when looking AWAY from the sun.
const vec3 FOG_COLOR_SKY = vec3(0.5, 0.7, 0.9);
// Fog Sun: The fog color when looking TOWARD the sun (glare).
const vec3 FOG_COLOR_SUN = vec3(1.0, 0.8, 0.6);
// Density: Higher = Thicker fog. 0.005 is clear, 0.02 is misty.
const float FOG_DENSITY = 0.00005; 

// =========================================================

// --- TONE MAPPING (ACES) ---
// Makes brights brighter and prevents colors from looking "flat"
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

// --- FALLBACK COLORS (Your existing palette) ---
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

    // --- 1. MATERIAL & TEXTURE ---
    vec4 albedo;
    if (u_DebugMode == 0) {
        float layer = float(max(0, v_TexID - 1)); 
        albedo = texture(u_Textures, vec3(v_TexCoord, layer));
        
        if (albedo.a < 0.1) discard; 
    } else {
        albedo = vec4(GetFallbackColor(v_TexID), 1.0);
        vec2 grid = fract(v_TexCoord);
        if (min(grid.x, grid.y) < 0.02) albedo.rgb *= 0.85;
    }

    // --- 2. LIGHTING CALCULATION ---
    
    // A. Shadows (Diffuse)
    // Standard dot product lighting
    float diff = max(dot(v_Normal, SUN_DIR), 0.0);
    // mix(0.3, 1.0) prevents shadows from being pitch black
    float lightIntensity = mix(0.1, 1.0, diff); 

    // B. Ambient Occlusion (AO)
    // pow(v_AO, 1.5) makes the corners slightly darker than the faces
    float ao = pow(v_AO, 1.5); 

    // C. Hemispheric Ambient
    // Split ambient light into Sky (Top) and Ground (Bottom)
    float upMap = v_Normal.y * 0.5 + 0.5;
    vec3 ambient = mix(AMBIENT_GROUND, AMBIENT_SKY, upMap);

    // D. Combine
    // (Ambient + Sun) * Material * AO
    vec3 lighting = (ambient * ao) + (SUN_COLOR * lightIntensity * 1.1 * ao);
    
    // Special Case: Lava (ID 20) is emissive, ignore shadows
    if (v_TexID == 20) lighting = vec3(1.5); 

    vec3 finalColor = albedo.rgb * v_Color * lighting;

    // --- 3. ATMOSPHERIC FOG ---
    
    float dist = length(v_FragPos - u_CameraPos);
    vec3 viewDir = normalize(v_FragPos - u_CameraPos);

    // "Sun Scatter": 1.0 if looking at sun, -1.0 if looking away
    float sunDot = max(dot(viewDir, SUN_DIR), 0.0);
    
    // Mix fog color based on sun angle (Blue <-> Orange)
    // pow(sunDot, 4.0) makes the orange glow concentrated around the sun
    vec3 fogColor = mix(FOG_COLOR_SKY, FOG_COLOR_SUN, pow(sunDot, 4.0));
    
    // Calculate Density (Exponential Squared)
    float fogFactor = 1.0 - exp(-pow(dist * FOG_DENSITY, 2.0));
    
    // --- 4. SUN DISK ---
    // If looking DIRECTLY at sun (angle > 0.995), draw a bright circle
    if (sunDot > 0.995) {
        vec3 sunDisk = SUN_COLOR * 3.0; // Very bright
        fogColor = mix(fogColor, sunDisk, sunDot * sunDot);
    }

    // Apply Fog
    finalColor = mix(finalColor, fogColor, fogFactor);

    // --- 5. TONE MAPPING & GAMMA ---
    // This gives it the "Cinematic" look
    finalColor = ACESFilm(finalColor);
    
    // Gamma correction (linear -> sRGB)
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    FragColor = vec4(finalColor, albedo.a);
}