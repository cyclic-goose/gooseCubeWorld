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
        "resources/textures/dirt1.jpg",    
        "resources/textures/dirt1.jpg",     
        "resources/textures/dirt1.jpg",    
        "resources/textures/dirt1.jpg"   
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


