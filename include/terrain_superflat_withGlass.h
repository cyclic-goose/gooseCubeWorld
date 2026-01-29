#pragma once
#include "terrain_system.h"
#include <algorithm>

// Ensure ImGui is available
#ifndef IMGUI_VERSION
// #include "imgui.h"
#endif

// ================================================================================================
// SUPERFLAT GENERATOR (TESTING MODE)
// ================================================================================================
class SuperflatGenerator : public ITerrainGenerator {
public:
    struct FlatSettings {
        int groundLevel = 10;
        int dirtLayerDepth = 3;
        bool bedrockFloor = true;
        
        // IDs
        uint8_t grassId = 1;
        uint8_t dirtId = 2;
        uint8_t stoneId = 3;
        uint8_t bedrockId = 4;
        
        // TEST: Specific ID defined as 'Transparent' in mesher.h (usually 6 or 7)
        uint8_t glassId = 4; 
        bool enableGlassLayer = true; 
    };

    SuperflatGenerator() = default;
    SuperflatGenerator(FlatSettings settings) : m_settings(settings) {}

    void Init() override {}

    int GetHeight(float x, float z) const override {
        // Return height of glass layer if enabled, otherwise ground
        return m_settings.enableGlassLayer ? m_settings.groundLevel + 10 : m_settings.groundLevel;
    }

    uint8_t GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const override {
        int iY = static_cast<int>(y);

        // TEST LAYER: Glass floating 2 blocks above ground
        if (m_settings.enableGlassLayer && iY == m_settings.groundLevel + 10) {
            return m_settings.glassId; 
        }

        if (iY > m_settings.groundLevel) return 0; // Air
        
        if (iY == m_settings.groundLevel) return m_settings.grassId;
        
        if (iY > m_settings.groundLevel - m_settings.dirtLayerDepth) return m_settings.dirtId;
        
        if (m_settings.bedrockFloor && iY == 0) return m_settings.bedrockId;
        
        return m_settings.stoneId;
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        // Adjust bounds to include the floating glass
        minH = m_settings.groundLevel;
        maxH = m_settings.enableGlassLayer ? m_settings.groundLevel + 10 : m_settings.groundLevel;
    }

    std::vector<std::string> GetTexturePaths() const override {
        return {
        "resources/textures/dirt1.jpg",    // ID 1: Mountain Base
        "resources/textures/dirt1.jpg",    // ID 2: Hills Top
        "resources/textures/dirt1.jpg",     // ID 3: Underground/Filler
        "resources/textures/dirt1.jpg",     // ID 4: Mountain Peaks
        "resources/textures/dirt1.jpg"      // ID 5: LOD/Fallback
    };
    }

#ifdef IMGUI_VERSION
    void OnImGui() override {
        if (ImGui::CollapsingHeader("Superflat Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool changed = false;
            changed |= ImGui::SliderInt("Ground Level", &m_settings.groundLevel, 1, 250);
            changed |= ImGui::Checkbox("Enable Secondary Mesh", &m_settings.enableGlassLayer);
            if (changed) m_dirty = true;
        }
    }
#endif

private:
    FlatSettings m_settings;
};