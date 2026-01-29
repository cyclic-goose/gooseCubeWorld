#include "terrain_system.h"
#include <imgui.h> 
#include <algorithm>
#include <iostream>

// ================================================================================================
// STANDARD GENERATOR (2.5D Heightmap Logic)
// ================================================================================================

StandardGenerator::StandardGenerator() : m_settings(TerrainSettings()) { Init(); }
StandardGenerator::StandardGenerator(int seed) { m_settings = TerrainSettings(); m_settings.seed = seed; Init(); }
StandardGenerator::StandardGenerator(TerrainSettings settings) : m_settings(settings) { Init(); }

void StandardGenerator::Init() {
    auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
    auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();
    fnFractal->SetSource(fnPerlin);
    fnFractal->SetOctaveCount(4);
    m_baseNoise = fnFractal;

    auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
    auto fnFractal2 = FastNoise::New<FastNoise::FractalFBm>();
    fnFractal2->SetSource(fnSimplex);
    fnFractal2->SetOctaveCount(3);
    m_mountainNoise = fnFractal2;

    m_caveNoise = FastNoise::New<FastNoise::Perlin>();
}

std::vector<std::string> StandardGenerator::GetTexturePaths() const {
    return {
        "resources/textures/grass.jpg",    
        "resources/textures/dirt.jpg",     
        "resources/textures/stone.jpg",    
        "resources/textures/bedrock.jpg"   
    };
}

int StandardGenerator::GetHeight(float x, float z) const {
    float nx = x * m_settings.scale;
    float nz = z * m_settings.scale;
    
    float baseVal = m_baseNoise->GenSingle2D(nx * m_settings.hillFrequency, nz * m_settings.hillFrequency, m_settings.seed);
    float hillHeight = baseVal * m_settings.hillAmplitude;
    
    float mountainVal = m_mountainNoise->GenSingle2D(nx * m_settings.mountainFrequency, nz * m_settings.mountainFrequency, m_settings.seed + 1);
    mountainVal = std::abs(mountainVal); 
    mountainVal = std::pow(mountainVal, 2.0f); 
    float mountainHeight = mountainVal * m_settings.mountainAmplitude;
    
    return m_settings.seaLevel + (int)std::floor(hillHeight + mountainHeight);
}

// ADAPTED FOR COMPATIBILITY
uint8_t StandardGenerator::GetBlock(float x, float y, float z, int lodScale) const {
    // We calculate height locally here since the World no longer passes it in
    int heightAtXZ = GetHeight(x, z);
    
    int wy = (int)y;
    
    // 3D Cave Check (Only cut holes if we are below surface)
    if (wy < heightAtXZ && lodScale == 1) { 
        float val = m_caveNoise->GenSingle3D(x * 0.02f, y * 0.04f, z * 0.02f, m_settings.seed);
        if (val > m_settings.caveThreshold) return 0; // Air
    }

    if (wy > heightAtXZ) return 0; 

    if (wy == heightAtXZ) {
        if (wy > 180) return 4; // Snow/Bedrock
        return 1; // Grass
    } 
    else if (wy > heightAtXZ - (4 * lodScale)) {
        return 2; // Dirt
    }
    
    if (wy == 0) return 4; // Bedrock floor

    return 3; // Stone
}

void StandardGenerator::GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) {
    const int CHUNK_SIZE = 32; 
    int worldX = cx * CHUNK_SIZE * scale;
    int worldZ = cz * CHUNK_SIZE * scale;
    int size = CHUNK_SIZE * scale;

    // Sampling corners and center is usually enough for heightmap bounds
    int h1 = GetHeight((float)worldX, (float)worldZ);
    int h2 = GetHeight((float)(worldX + size), (float)worldZ);
    int h3 = GetHeight((float)worldX, (float)(worldZ + size));
    int h4 = GetHeight((float)(worldX + size), (float)(worldZ + size));
    int h5 = GetHeight((float)(worldX + size/2), (float)(worldZ + size/2));

    int min = std::min({h1, h2, h3, h4, h5});
    int max = std::max({h1, h2, h3, h4, h5});

    minH = min - (16 * scale); // padding for caves
    maxH = max + (4 * scale);
}

void StandardGenerator::OnImGui() {
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Standard Noise Gen");
    if(ImGui::DragInt("Seed", &m_settings.seed)) { m_dirty = true; Init(); }
    if(ImGui::SliderFloat("Scale", &m_settings.scale, 0.001f, 0.1f)) { m_dirty = true; Init(); }
}


// ================================================================================================
// OVERHANG GENERATOR (3D Volumetric)
// ================================================================================================

OverhangGenerator::OverhangGenerator() { Init(); }

void OverhangGenerator::Init() {
    // We use a Simplex noise for organic 3D shapes
    auto noise = FastNoise::New<FastNoise::Simplex>();
    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    fractal->SetSource(noise);
    fractal->SetOctaveCount(m_settings.octaves);
    fractal->SetGain(m_settings.gain);
    fractal->SetLacunarity(m_settings.lacunarity);
    m_noise3D = fractal;
}

std::vector<std::string> OverhangGenerator::GetTexturePaths() const {
    return {
        "resources/textures/grass.jpg",  // 1
        "resources/textures/dirt.jpg",   // 2
        "resources/textures/stone.jpg",  // 3
        "resources/textures/mossy.jpg"   // 4 (Example: Overhang bottom)
    };
}

float OverhangGenerator::GetDensity(float x, float y, float z) const {
    // 1. Get raw 3D noise (-1.0 to 1.0)
    // Stretch noise vertically by using a higher frequency for Y input (multiply Y by yStretch)
    float n = m_noise3D->GenSingle3D(x * m_settings.noiseScale, 
                                     y * m_settings.noiseScale * m_settings.yStretch, 
                                     z * m_settings.noiseScale, 
                                     m_settings.seed);
    
    // 2. Apply Height Gradient
    // Center: Where the bias is 0.
    // Falloff: How quickly density drops as Y increases.
    float heightGradient = -((y - m_settings.gradientCenter) / m_settings.gradientFalloff); 
    
    // 3. Combine
    return n + heightGradient;
}

void OverhangGenerator::GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) {
    // In a pure 3D noise world, exact bounds are expensive to calculate perfectly.
    // We return safe estimates.
    minH = 0; 
    maxH = m_settings.maxTerrainHeight;
}

uint8_t OverhangGenerator::GetBlock(float x, float y, float z, int lodScale) const {
    if (y < m_settings.hardFloor) return 3; // Bedrock/Stone floor
    if (y > m_settings.maxTerrainHeight) return 0; // Hard cap

    float density = GetDensity(x, y, z);

    if (density > m_settings.threshold) {
        // It is solid. Now determine WHAT block.
        // We check density above to see if it's a surface
        
        float densityAbove = GetDensity(x, y + (1.0f * lodScale), z);

        if (densityAbove <= m_settings.threshold) {
            return 1; // Grass (Surface)
        } else if (densityAbove < m_settings.threshold + 0.2f) {
            return 2; // Dirt (Just below surface)
        } else {
            return 3; // Stone (Deep)
        }
    }

    return 0; // Air
}

void OverhangGenerator::OnImGui() {
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Overhang 3D Gen");
    
    bool changed = false;

    // Seed Input
    if(ImGui::DragInt("Seed", &m_settings.seed)) changed = true;
    
    ImGui::Separator();
    ImGui::Text("Noise Shape");
    if(ImGui::SliderFloat("Scale", &m_settings.noiseScale, 0.001f, 0.1f)) changed = true;
    if(ImGui::SliderFloat("Y Stretch", &m_settings.yStretch, 0.1f, 5.0f)) changed = true;
    if(ImGui::SliderInt("Octaves", &m_settings.octaves, 1, 8)) changed = true;
    if(ImGui::SliderFloat("Gain", &m_settings.gain, 0.0f, 1.0f)) changed = true;
    if(ImGui::SliderFloat("Lacunarity", &m_settings.lacunarity, 1.0f, 4.0f)) changed = true;

    ImGui::Separator();
    ImGui::Text("Density & Height");
    if(ImGui::SliderFloat("Threshold", &m_settings.threshold, -1.0f, 1.0f)) changed = true;
    if(ImGui::DragFloat("Gradient Center", &m_settings.gradientCenter, 1.0f, 0.0f, 256.0f)) changed = true;
    if(ImGui::DragFloat("Gradient Falloff", &m_settings.gradientFalloff, 1.0f, 10.0f, 200.0f)) changed = true;
    
    ImGui::Separator();
    if(ImGui::DragInt("Hard Floor", &m_settings.hardFloor, 1, 0, 64)) changed = true;
    if(ImGui::DragInt("Max Height", &m_settings.maxTerrainHeight, 1, 128, 512)) changed = true;

    if (changed) {
        m_dirty = true; 
        Init(); 
    }
}