#pragma once
#include "terrain_system.h"
#include <algorithm>

// Ensure ImGui is available, or stub this out if you handle includes differently
#ifndef IMGUI_VERSION
// forward declaration or include your imgui header here if needed
// #include "imgui.h"
#endif

// ================================================================================================
// SUPERFLAT GENERATOR
// ================================================================================================
class SuperflatGenerator : public ITerrainGenerator {
public:
    struct FlatSettings {
        int groundLevel = 0;
        int dirtLayerDepth = 3;
        bool bedrockFloor = true;
        // Block IDs (customizable mapping)
        uint8_t grassId = 1;
        uint8_t dirtId = 2;
        uint8_t stoneId = 3;
        uint8_t bedrockId = 4;
    };

    SuperflatGenerator() = default;
    SuperflatGenerator(FlatSettings settings) : m_settings(settings) {}

    // No initialization needed for flat logic
    void Init() override {}

    // Constant height across the board
    int GetHeight(float x, float z) const override {
        return m_settings.groundLevel;
    }

    // Simple layer logic
    uint8_t GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const override {
        int iY = static_cast<int>(y);

        if (iY > m_settings.groundLevel) return 0; // Air
        
        if (iY == m_settings.groundLevel) return m_settings.grassId;
        
        if (iY > m_settings.groundLevel - m_settings.dirtLayerDepth) return m_settings.dirtId;
        
        if (m_settings.bedrockFloor && iY == 0) return m_settings.bedrockId;
        
        return m_settings.stoneId;
    }

    // Optimization: The bounds are exactly the ground level
    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        minH = m_settings.groundLevel;
        maxH = m_settings.groundLevel;
    }

    // Define texture assets required for this generator
    std::vector<std::string> GetTexturePaths() const override {
        return {
            "textures/grass.png",   // ID 1
            "textures/dirt.png",    // ID 2
            "textures/stone.png",   // ID 3
            "textures/bedrock.png"  // ID 4
        };
    }

#ifdef IMGUI_VERSION
    void OnImGui() override {
        if (ImGui::CollapsingHeader("Superflat Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            
            bool changed = false;
            
            changed |= ImGui::SliderInt("Ground Level", &m_settings.groundLevel, 1, 255);
            changed |= ImGui::SliderInt("Dirt Depth", &m_settings.dirtLayerDepth, 1, 10);
            changed |= ImGui::Checkbox("Bedrock Floor", &m_settings.bedrockFloor);

            if (changed) {
                m_dirty = true;
            }
        }
    }
#endif

private:
    FlatSettings m_settings;
};