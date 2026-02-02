#version 460 core

// --- INPUTS (From Vertex Shader) ---
in vec3 v_WorldNormal;
in vec3 v_WorldPosition;
in float v_WaveHeightOffset;
in float v_LinearDepthFromCamera; 
in vec4 v_ClipSpacePosition;

// --- GLOBAL UNIFORMS ---
uniform vec3 u_CameraPosition;
uniform vec3 u_SunDirection;
uniform float u_Time;
uniform vec2 u_ScreenSize; 
uniform float u_CameraNearPlane; 

// --- EXPOSED CONTROLS (User Settings) ---
// Suggested Default: (0.0, 0.4, 0.85) -> Bright Cartoon Blue
uniform vec3 u_DeepWaterColor; 

// Suggested Default: (0.2, 0.9, 1.0) -> Cyan/Turquoise
uniform vec3 u_ShallowWaterColor;

// Suggested Default: (1.0, 1.0, 1.0) -> Pure White
uniform vec3 u_FoamColor;

// Suggested Default: 0.5 -> Harder edge for cartoon look (was 1.5)
uniform float u_ShoreSoftness; 

// Suggested Default: 15.0 -> Controls how fast color fades to deep (Higher = Clearer)
uniform float u_WaterClarity;

// Suggested Default: 0.6 -> Height at which waves turn to white caps
uniform float u_WaveFoamHeight; 

// --- TEXTURES ---
layout(binding = 1) uniform sampler2D u_SceneDepthTexture; 

out vec4 FragColor;

// --- HELPERS ---

// Helper for Reverse-Z (Infinite Projection) calculations
float linearizeDepth(float rawDepthBufferValue) {
    // In Reverse-Z: 0 is far, 1 is near
    if (rawDepthBufferValue < 0.00001) return 100000.0; // Sky is "infinite"
    return u_CameraNearPlane / rawDepthBufferValue;
}

// Simple pseudo-random generator
float randomNoise(vec2 seed){
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    // 1. Calculate Screen Coordinates (UVs) for reading background depth
    vec2 screenUV = (v_ClipSpacePosition.xy / v_ClipSpacePosition.w) * 0.5 + 0.5;

    // 2. Calculate Depth of the solid world behind the water
    float rawBackDepth = texture(u_SceneDepthTexture, screenUV).r;
    float linearBackDepth = linearizeDepth(rawBackDepth);
    
    // 3. Calculate Depth of the water surface itself
    float linearWaterSurfaceDepth = v_LinearDepthFromCamera;

    // 4. Calculate actual water depth at this pixel (Distance from surface to floor)
    float waterDepth = linearBackDepth - linearWaterSurfaceDepth;
    
    // Clipping safety (prevent rendering if water is technically behind the land due to wave movement)
    if (waterDepth < -0.1) discard; 
    waterDepth = max(0.0, waterDepth);

    // 5. Water Color Mixing (Beer's Law approximation)
    float depthFactor = clamp(waterDepth / u_WaterClarity, 0.0, 1.0);
    vec3 baseWaterColor = mix(u_ShallowWaterColor, u_DeepWaterColor, depthFactor);

    // 6. Foam Calculation
    // Shore Foam: Foam generated where water meets land
    float shoreFoamIntensity = 1.0 - smoothstep(0.0, u_ShoreSoftness, waterDepth);
    
    // Wave Cap Foam: Foam generated on high wave peaks
    float noiseValue = randomNoise(v_WorldPosition.xz * 0.1); 
    // Sharper smoothstep for cartoon look
    float waveCapIntensity = smoothstep(u_WaveFoamHeight, u_WaveFoamHeight + 0.1, v_WaveHeightOffset + noiseValue * 0.2);
    
    // Combine foam types
    float totalFoam = clamp(shoreFoamIntensity + waveCapIntensity, 0.0, 1.0);

    // 7. Lighting Calculation
    vec3 normal = normalize(v_WorldNormal);
    vec3 viewDirection = normalize(u_CameraPosition - v_WorldPosition);
    vec3 lightDirection = normalize(u_SunDirection); 
    
    // Specular Highlight (Sun reflection)
    vec3 halfVector = normalize(lightDirection + viewDirection);
    // Increased power (256.0) for sharper, smaller cartoon highlight
    float specularStrength = pow(max(dot(normal, halfVector), 0.0), 256.0);
    
    // Fresnel Effect (Reflectivity at angles)
    float fresnelFactor = 0.02 + 0.98 * pow(1.0 - max(dot(viewDirection, normal), 0.0), 5.0);

    // 8. Final Color Composition
    vec3 albedo = mix(baseWaterColor, u_FoamColor, totalFoam);
    
    // Diffuse lighting (Sunlight)
    float diffuseLight = max(dot(normal, lightDirection), 0.0);
    
    // Add ambient (0.4) + diffuse + intense specular white
    vec3 finalRGB = albedo * (vec3(0.4) + diffuseLight) + (vec3(1.0) * specularStrength);

    // 9. Alpha / Transparency Logic
    // Start fairly opaque (0.8) and become fully opaque at grazing angles (Fresnel)
    float alpha = mix(0.8, 1.0, fresnelFactor);
    
    // Foam is always opaque
    alpha = mix(alpha, 1.0, totalFoam);
    
    // Soften the edge where water meets shore
    alpha *= clamp(waterDepth / (u_ShoreSoftness * 0.5), 0.0, 1.0);

    FragColor = vec4(finalRGB, alpha);
}