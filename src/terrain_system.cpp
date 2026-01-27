#include "terrain_system.h"
#include <imgui.h> 
#include <algorithm>
#include <iostream>

// ================================================================================================
// STANDARD GENERATOR IMPLEMENTATION
// ================================================================================================

// Default Constructor
StandardGenerator::StandardGenerator() 
    : m_settings(TerrainSettings()) 
{
    Init();
}

// Seed Constructor (Matches your main.cpp usage)
StandardGenerator::StandardGenerator(int seed) {
    m_settings = TerrainSettings();
    m_settings.seed = seed;
    Init();
}

// Settings Constructor
StandardGenerator::StandardGenerator(TerrainSettings settings) 
    : m_settings(settings) 
{
    Init();
}

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

// Asset Definition
std::vector<std::string> StandardGenerator::GetTexturePaths() const {
    // These paths correspond to the Block IDs returned in GetBlock.
    // Assuming: 
    // ID 1 -> Index 0
    // ID 2 -> Index 1
    // ID 3 -> Index 2 ... etc
    return {
        "resources/textures/dirt1.jpg",    // ID 1: Mountain Base
        "resources/textures/dirt1.jpg",    // ID 2: Hills Top
        "resources/textures/dirt1.jpg",     // ID 3: Underground/Filler
        "resources/textures/dirt1.jpg",     // ID 4: Mountain Peaks
        "resources/textures/dirt1.jpg"      // ID 5: LOD/Fallback
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
    
    return m_settings.seaLevel + (int)(hillHeight + mountainHeight);
}

uint8_t StandardGenerator::GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const {
    int wy = (int)y;
    
    if (lodScale == 1) { 
            float val = m_caveNoise->GenSingle3D(x * 0.02f, y * 0.04f, z * 0.02f, m_settings.seed);
            if (val > m_settings.caveThreshold) return 0; 
    }

    if (wy > heightAtXZ) return 0; 

    if (wy == heightAtXZ) {
        if (wy > 550) return 4; 
        if (wy > 350) return 1; 
        return 2;              
    } 
    else if (wy > heightAtXZ - (4 * lodScale)) {
        if (wy > 550) return 4; 
        if (wy > 350) return 2; 
        return 5;              
    }
    
    return 3; 
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

    minH = std::min({h1, h2, h3, h4, h5});
    maxH = std::max({h1, h2, h3, h4, h5});
}

// ================================================================================================
// DYNAMIC UI IMPLEMENTATION
// ================================================================================================
void StandardGenerator::OnImGui() {
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Standard Noise Gen");
    
    bool valChanged = false;

    // Seed
    if(ImGui::DragInt("Seed", &m_settings.seed)) valChanged = true;
    
    // Hills
    if(ImGui::SliderFloat("Noise Scale", &m_settings.scale, 0.001f, 0.1f)) valChanged = true;
    if(ImGui::SliderFloat("Hill Amp", &m_settings.hillAmplitude, 0.0f, 500.0f)) valChanged = true;
    if(ImGui::SliderFloat("Hill Freq", &m_settings.hillFrequency, 0.05f, 10.0f)) valChanged = true;
    
    // Mountains
    if(ImGui::SliderFloat("Mnt Amp", &m_settings.mountainAmplitude, 0.0f, 8000.0f)) valChanged = true;
    if(ImGui::SliderFloat("Mnt Freq", &m_settings.mountainFrequency, 0.01f, 0.2f)) valChanged = true;
    
    // General
    if(ImGui::SliderInt("Sea Level", &m_settings.seaLevel, 0, 500)) valChanged = true;
    if(ImGui::SliderFloat("Cave Threshold", &m_settings.caveThreshold, 0.0f, 1.0f)) valChanged = true;

    if (valChanged) {
        m_dirty = true;
        Init(); // Re-seed noise if needed
    }
}