#version 460 core

// --- INPUTS (SSBO) ---
layout (std430, binding = 0) readonly buffer VoxelData {
    uint compressedVertexData[];
};

layout (std430, binding = 2) readonly buffer ChunkOffsets {
    vec4 chunkInstanceData[]; // xyz = position, w = scale
};

// --- GLOBAL UNIFORMS ---
uniform mat4 u_ViewProjection;
uniform float u_Time;

// --- NEW UNIFORM: Height Offset ---
uniform float u_WaterHeightOffset; 

// --- EXPOSED WAVE CONTROLS ---
// Format: vec4(DirectionX, DirectionY, Steepness, Wavelength)
uniform vec4 u_WaveLayer1; 
uniform vec4 u_WaveLayer2; 
uniform vec4 u_WaveLayer3; 

// Storm Settings
uniform float u_StormFrequency; 
uniform float u_StormSpeed;     

// --- OUTPUTS TO FRAGMENT SHADER ---
out vec3 v_WorldNormal;
out vec3 v_WorldPosition;
out float v_WaveHeightOffset;    
out float v_LinearDepthFromCamera; 
out vec4 v_ClipSpacePosition;

const float PI = 3.14159;

// --- HELPERS ---
vec3 decodeCubeNormal(int faceIndex) {
    const vec3 normals[6] = vec3[](
        vec3( 1, 0, 0), vec3(-1, 0, 0),
        vec3( 0, 1, 0), vec3( 0,-1, 0),
        vec3( 0, 0, 1), vec3( 0, 0,-1) 
    );
    return (faceIndex >= 0 && faceIndex <= 5) ? normals[faceIndex] : vec3(0, 1, 0);
}

float calculateStormIntensity(vec2 position) {
    return 0.5 + 0.5 * sin(position.x * u_StormFrequency + sin(position.y * u_StormFrequency) + u_Time * u_StormSpeed); 
}

void applyGerstnerWaves(inout vec3 position, inout vec3 normal, float stormIntensity) {
    vec3 tangent = vec3(1, 0, 0);
    vec3 binormal = vec3(0, 0, 1);
    vec3 originalPos = position;
    float cumulativeHeight = 0.0;
    
    vec4 waveLayers[3] = vec4[](u_WaveLayer1, u_WaveLayer2, u_WaveLayer3);
    
    for(int i = 0; i < 3; i++) {
        vec2 direction = normalize(waveLayers[i].xy);
        float steepness = waveLayers[i].z;
        float wavelength = waveLayers[i].w;
        
        float currentAmplitude = steepness / wavelength;
        if (i == 0) {
            currentAmplitude *= (0.8 + stormIntensity * 0.5); 
        }
        
        float wavenumber = 2.0 * PI / wavelength;
        float phaseSpeed = sqrt(9.8 / wavenumber); 
        
        float phase = wavenumber * (dot(direction, originalPos.xz) - phaseSpeed * 0.38 * u_Time);
        float sinPhase = sin(phase);
        float cosPhase = cos(phase);
        
        float displacement = currentAmplitude * sinPhase;
        position.y += displacement;
        cumulativeHeight += displacement;

        float waveDerivative = wavenumber * currentAmplitude; 
        float derivativeX = waveDerivative * direction.x * cosPhase;
        float derivativeZ = waveDerivative * direction.y * cosPhase;
        
        tangent.y += derivativeX;
        binormal.y += derivativeZ;
    }
    
    normal = normalize(cross(binormal, tangent));
    v_WaveHeightOffset = cumulativeHeight;
}

void main() {
    uint vertexData = compressedVertexData[gl_VertexID];

    // 1. Bitwise Unpacking
    float localX = float(bitfieldExtract(vertexData, 0,  6));
    float localY = float(bitfieldExtract(vertexData, 6,  6));
    float localZ = float(bitfieldExtract(vertexData, 12, 6));
    int faceIndex = int(bitfieldExtract(vertexData, 18, 3));

    vec3 localPosition = vec3(localX, localY, localZ);
    
    // 2. Instance Transform
    vec3 chunkOffset = chunkInstanceData[gl_BaseInstance].xyz;
    float chunkScale = chunkInstanceData[gl_BaseInstance].w;
    
    vec3 worldPos = (localPosition * chunkScale) + chunkOffset;
    
    // --- APPLY OFFSET HERE ---
    // Move the base water level down before calculating waves
    worldPos.y += u_WaterHeightOffset;

    vec3 worldNormal = decodeCubeNormal(faceIndex);

    // 3. Apply Wave Physics
    if (faceIndex == 2) {
        float stormIntensity = calculateStormIntensity(worldPos.xz);
        applyGerstnerWaves(worldPos, worldNormal, stormIntensity);
    } else {
        worldPos.y += sin(worldPos.x * 0.5 + u_Time) * 0.05;
    }

    // 4. Artifact Prevention
    if (chunkScale > 1.0) {
        worldPos.y -= (chunkScale * 0.1);
    }

    // 5. Final Outputs
    gl_Position = u_ViewProjection * vec4(worldPos, 1.0);
    
    v_WorldNormal = worldNormal;
    v_WorldPosition = worldPos;
    v_ClipSpacePosition = gl_Position;
    v_LinearDepthFromCamera = gl_Position.w; 
}