#include "terrain_system.h"
#include <imgui.h> 
#include <algorithm>
#include <iostream>

// ================================================================================================
// STANDARD GENERATOR IMPLEMENTATION
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
    // Matches ID 1-4
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
    
    // PRECISION FIX: Explicit floor to prevent chunk boundary mismatches
    return m_settings.seaLevel + (int)std::floor(hillHeight + mountainHeight);
}

uint8_t StandardGenerator::GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const {
    int wy = (int)y;
    
    if (lodScale == 1) { 
            float val = m_caveNoise->GenSingle3D(x * 0.02f, y * 0.04f, z * 0.02f, m_settings.seed);
            if (val > m_settings.caveThreshold) return 0; 
    }

    if (wy > heightAtXZ) return 0; 

    if (wy == heightAtXZ) {
        if (wy > 180) return 4; // Snow/Bedrock texture for peaks
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

    int h1 = GetHeight((float)worldX, (float)worldZ);
    int h2 = GetHeight((float)(worldX + size), (float)worldZ);
    int h3 = GetHeight((float)worldX, (float)(worldZ + size));
    int h4 = GetHeight((float)(worldX + size), (float)(worldZ + size));
    int h5 = GetHeight((float)(worldX + size/2), (float)(worldZ + size/2));

    int min = std::min({h1, h2, h3, h4, h5});
    int max = std::max({h1, h2, h3, h4, h5});

    // Add padding to handle noise variance
    minH = min - (8 * scale);
    maxH = max + (8 * scale);
}

void StandardGenerator::OnImGui() {
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Standard Noise Gen");
    bool valChanged = false;

    if(ImGui::DragInt("Seed", &m_settings.seed)) valChanged = true;
    if(ImGui::SliderFloat("Scale", &m_settings.scale, 0.001f, 0.1f)) valChanged = true;
    if(ImGui::SliderFloat("Hill Amp", &m_settings.hillAmplitude, 0.0f, 500.0f)) valChanged = true;
    if(ImGui::SliderFloat("Mnt Amp", &m_settings.mountainAmplitude, 0.0f, 8000.0f)) valChanged = true;
    
    if (valChanged) {
        m_dirty = true;
        Init(); 
    }
}