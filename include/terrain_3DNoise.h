#pragma once
#include "terrain_system.h"
#include <algorithm>
//#include "profiler.h"

#ifndef IMGUI_VERSION
// forward declaration or include your imgui header here if needed
// #include "imgui.h"
#endif


// ================================================================================================
// 4. OVERHANG GENERATOR (True 3D)
// ================================================================================================
class OverhangGenerator : public ITerrainGenerator {
public:
    struct OverhangSettings {
        int seed = 5678;
        
        // Noise Shape
        float noiseScale = 0.094f;    // General zoom
        float yStretch = 1.5f;        // Vertical stretch (higher = taller/thinner features)
        int octaves = 5;
        float gain = 0.8f;
        float lacunarity = 2.0f;

        // Density Logic
        float threshold = 0.0f;       // Surface cut-off (Density > this is solid)
        float gradientCenter = 64.0f; // Height where density is purely noise (0 bias)
        float gradientFalloff = 64.0f;// Range over which density fades to air
        
        // Limits
        int hardFloor = 10;           // Bedrock level
        int maxTerrainHeight = 256;   // Optimisation cap
    };

    OverhangGenerator();
    void Init() override;
    
    // In 3D, this is a density check. > 0 is solid, < 0 is air.
    float GetDensity(float x, float y, float z) const;
    
    uint8_t GetBlock(float x, float y, float z, int lodScale) const override;
    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override;
    
    std::vector<std::string> GetTexturePaths() const override;
    void OnImGui() override;

private:
    OverhangSettings m_settings;
    FastNoise::SmartNode<> m_noise3D;
};



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
        "resources/textures/dirt1.jpg",  // 1
        "resources/textures/dirt1.jpg",   // 2
        "resources/textures/dirt1.jpg",  // 3
        "resources/textures/dirt1.jpg"   // 4 (Example: Overhang bottom)
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
    //Engine::Profiler::ScopedTimer timer("3D Noise World:: Get BLOCK");
    if (y < m_settings.hardFloor) return 3; // Bedrock/Stone floor
    if (y > m_settings.maxTerrainHeight) return 0; // Hard cap

    float density = GetDensity(x, y, z);

    if (density > m_settings.threshold) {
        // It is solid. Now determine WHAT block.
        
        // FIX: The surface check must be LOD-invariant. 
        // We always check exactly 1 unit (1 block) above in world space, 
        // regardless of the current LOD scale. 
        // Old faulty logic: y + (1.0f * lodScale) -> Caused seams between LODs.
        
        float densityAbove = GetDensity(x, y + 1.0f, z);

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



#ifdef IMGUI_VERSION
void OverhangGenerator::OnImGui() {
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Overhang 3D Gen");
    
    bool changed = false;

    // Seed Input
    if(ImGui::DragInt("Seed", &m_settings.seed)) changed = true;
    
    ImGui::Separator();
    ImGui::Text("Noise Shape");
    if(ImGui::SliderFloat("Scale", &m_settings.noiseScale, 0.008f, 0.2f)) changed = true;
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

#endif