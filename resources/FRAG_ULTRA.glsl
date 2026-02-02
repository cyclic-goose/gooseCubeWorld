#version 460 core

// --- UNIFORMS (Set by ShaderParams) ---
uniform sampler2DArray u_Textures;
uniform int u_DebugMode;
uniform vec3 u_CameraPos;

uniform vec3 u_SunDir;
uniform vec3 u_SunColor;
uniform float u_SunIntensity;
uniform vec3 u_SkyColorTop;
uniform vec3 u_SkyColorHorizon;

uniform float u_FogDensity;
uniform float u_CloudCoverage;
uniform float u_CloudSpeed;
uniform float u_CloudSoftness;

uniform float u_Exposure;
uniform float u_Saturation;
uniform float u_Gamma;
uniform float u_Time;

// --- INPUTS ---
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_Color;
in float v_AO;
in vec3 v_WorldPos;
flat in int v_TexID;

out vec4 FragColor;

// =========================================================
//                  NOISE FUNCTIONS (For Clouds)
// =========================================================
float hash(float n) { return fract(sin(n) * 43758.5453); }
float noise(vec3 x) {
    vec3 p = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float n = p.x + p.y * 57.0 + 113.0 * p.z;
    return mix(mix(mix(hash(n + 0.0), hash(n + 1.0), f.x),
                   mix(hash(n + 57.0), hash(n + 58.0), f.x), f.y),
               mix(mix(hash(n + 113.0), hash(n + 114.0), f.x),
                   mix(hash(n + 170.0), hash(n + 171.0), f.x), f.y), f.z);
}

float fbm(vec3 p) {
    float f = 0.0;
    f += 0.5000 * noise(p); p *= 2.02;
    f += 0.2500 * noise(p); p *= 2.03;
    f += 0.1250 * noise(p);
    return f;
}

// =========================================================
//                  PROCEDURAL SKY
// =========================================================
vec3 GetSkyColor(vec3 rayDir) {
    // Horizon: Fade from Horizon Color to Top Color based on height
    // We assume rayDir is normalized.
    float horizon = smoothstep(-0.1, 0.3, rayDir.y);
    vec3 baseSky = mix(u_SkyColorHorizon, u_SkyColorTop, pow(horizon, 0.8));

    // Sun Disk
    // We only draw the sun if the ray is actually looking at it
    float sunDot = max(dot(rayDir, u_SunDir), 0.0);
    
    // Strict cutoff for disk, smoother for bloom
    float sunDisk = smoothstep(0.9995, 0.9999, sunDot); 
    float sunBloom = pow(sunDot, 128.0) * 0.5; 
    
    vec3 sunFinal = u_SunColor * u_SunIntensity * (sunDisk + sunBloom);

    // Clouds (Projected onto a plane)
    // Only draw clouds if looking somewhat UP (> 0.0) to avoid clouds on the floor
    if (rayDir.y > 0.01) {
        float cloudTime = u_Time * u_CloudSpeed;
        
        // Project ray to a "sky plane" at height 1000 or similar concept
        // UVs become (x/y, z/y)
        vec2 cloudUV = (rayDir.xz / rayDir.y) * 1.5 + cloudTime;
        
        float cloudVal = fbm(vec3(cloudUV, cloudTime * 0.1));
        
        float density = smoothstep(1.0 - u_CloudCoverage, (1.0 - u_CloudCoverage) + u_CloudSoftness, cloudVal);
        
        // Darken clouds near the bottom/horizon
        vec3 cloudColor = vec3(0.95) * mix(0.7, 1.0, density);
        
        // Fade clouds out at horizon so they don't hard clip
        float horizonFade = smoothstep(0.0, 0.2, rayDir.y);
        
        baseSky = mix(baseSky, cloudColor, density * 0.8 * horizonFade);
    }

    return baseSky + sunFinal;
}

// =========================================================
//                  COLOR GRADING
// =========================================================
vec3 ACESFilm(vec3 x) {
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    // 1. Base Color
    float layer = float(max(0, v_TexID - 1));
    vec4 texColor = texture(u_Textures, vec3(v_TexCoord, layer));
    if (texColor.a < 0.1) discard;

    // --- MATERIAL PROPERTIES ---
    float roughness = 0.9;
    float metallic = 0.0;
    float emission = 0.0;

    if (v_TexID == 6 || v_TexID == 8 || v_TexID == 20 || v_TexID == 21) {
        roughness = 0.1;
    }
    if (v_TexID == 20) { // Lava
        emission = 2.0; 
        roughness = 1.0;
    }
    if (v_TexID == 22 || v_TexID == 23) { // Ores
        metallic = 0.8;
        roughness = 0.3;
    }

    vec3 albedo = texColor.rgb * v_Color;

    // 2. Lighting Vectors
    // toCamera: Surface -> Camera (Used for Specular/Reflection)
    vec3 toCamera = normalize(u_CameraPos - v_WorldPos);
    
    // rayDir: Camera -> Surface (Used for Sky/Fog projection)
    // IMPORTANT: This was the fix. Looking "down" is negative Y, which avoids the sun.
    vec3 rayDir = -toCamera; 

    // Ambient (Hemisphere)
    float upDot = v_Normal.y * 0.5 + 0.5;
    vec3 ambient = mix(u_SkyColorHorizon * 0.3, u_SkyColorTop * 0.5, upDot) * v_AO;

    // Diffuse
    float NdotL = max(dot(v_Normal, u_SunDir), 0.0);
    vec3 diffuse = u_SunColor * u_SunIntensity * NdotL * v_AO;

    // Specular (Blinn-Phong)
    vec3 halfDir = normalize(u_SunDir + toCamera);
    float NdotH = max(dot(v_Normal, halfDir), 0.0);
    float specFactor = pow(NdotH, (1.0 - roughness) * 128.0);
    vec3 specular = u_SunColor * specFactor * mix(0.1, 1.0, metallic);

    // Combine
    vec3 lighting = ambient + diffuse;
    vec3 finalColor = (albedo * lighting) + specular;
    
    finalColor += albedo * emission;

    // 3. Fog & Atmosphere
    float dist = length(u_CameraPos - v_WorldPos);
    
    // Get sky color for the direction we are looking
    vec3 fogColor = GetSkyColor(rayDir);
    
    float fogFactor = 1.0 - exp(-dist * u_FogDensity);
    
    // Optional: Vertical fog density (less fog high up)
    // float heightFactor = smoothstep(50.0, 0.0, v_WorldPos.y);
    // fogFactor = mix(fogFactor, 1.0, heightFactor * 0.2);

    finalColor = mix(finalColor, fogColor, clamp(fogFactor, 0.0, 1.0));

    // 4. Post-Processing
    finalColor *= u_Exposure;
    float luminance = dot(finalColor, vec3(0.2126, 0.7152, 0.0722));
    finalColor = mix(vec3(luminance), finalColor, u_Saturation);
    finalColor = ACESFilm(finalColor);
    finalColor = pow(finalColor, vec3(1.0 / u_Gamma));

    FragColor = vec4(finalColor, texColor.a);
}