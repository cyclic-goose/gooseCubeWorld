#pragma once

#include "terrain_system.h"
#include <FastNoise/FastNoise.h>
#include <imgui.h>
#include <algorithm>
#include <vector>
#include <string>

class AlienGenerator : public ITerrainGenerator {
public:
    struct AlienSettings {
        int seed = 42;
        float cellularJitter = 1.0f;     // Makes it look more like organic cells vs perfect grid
        float spikeHeight = 400.0f;      // Height of the crystal towers
        float platformFreq = 0.02f;      // Frequency of floating platforms
        float warpStrength = 40.0f;      // How much to twist the terrain
        int lavaLevel = 40;              // Base floor level
        float floatingIslandThreshold = 0.4f; 
    } m_settings;

private:
    FastNoise::SmartNode<> m_heightNoise;
    // m_warpNoise removed as DomainWarpGradient handles its own noise generation
    FastNoise::SmartNode<> m_islandNoise;

public:
    AlienGenerator(int seed = 42) {
        m_settings.seed = seed;
        Init();
    }

    void Init() override {
        // 1. Cellular Noise for "Crystal Towers" look
        auto fnCellular = FastNoise::New<FastNoise::CellularValue>();
        fnCellular->SetGridJitter(m_settings.cellularJitter); // Fixed: SetJitterModifier
        
        // 2. Domain Warp to twist the towers
        // Note: DomainWarpGradient generates its own internal gradient noise for the warp.
        // It does not expose a frequency setter; frequency is handled by input coordinate scaling.
        auto fnDomainWarp = FastNoise::New<FastNoise::DomainWarpGradient>();
        fnDomainWarp->SetSource(fnCellular);
        fnDomainWarp->SetWarpAmplitude(m_settings.warpStrength);
        // Removed SetWarpFrequency as it is not a member of DomainWarpGradient
        
        m_heightNoise = fnDomainWarp;

        // 3. 3D Perlin for Floating Islands
        auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
        m_islandNoise = fnPerlin;
    }

    int GetHeight(float x, float z) const override {
        // Base alien terrain: Cellular noise returns -1 to 1. 
        // We sharpen it to make spikes.
        float val = m_heightNoise->GenSingle2D(x * 0.01f, z * 0.01f, m_settings.seed);
        
        // Sharpen the peaks (make valleys wider, peaks pointier)
        val = std::abs(val); 
        val = 1.0f - val; // Invert so cell centers are peaks
        val = std::pow(val, 4.0f); // Power curve for extreme spikes

        int h = m_settings.lavaLevel + (int)(val * m_settings.spikeHeight);
        
        // Hack: For floating islands, we need to tell the engine to render UP TO the sky
        // so the chunk filler actually checks for blocks there.
        // We add a constant buffer to height to allow floating islands to generate above terrain.
        return h + 200; 
    }

    uint8_t GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const override {
        // heightAtXZ includes our +200 buffer, so we recalculate true terrain height
        float val = m_heightNoise->GenSingle2D(x * 0.01f, z * 0.01f, m_settings.seed);
        val = std::abs(val);
        val = 1.0f - val;
        val = std::pow(val, 4.0f);
        int terrainH = m_settings.lavaLevel + (int)(val * m_settings.spikeHeight);

        int wy = (int)y;

        // 1. Bedrock / Lava Floor
        if (wy < 5) return 1; // Core

        // 2. Main Terrain (Spikes)
        if (wy <= terrainH) {
            if (wy > terrainH - 4) return 2; // Top Soil (Alien Grass)
            return 3; // Filler (Alien Dirt)
        }

        // 3. Floating Islands (3D Noise Check)
        // We only generate these above the terrain
        if (wy > terrainH && wy < terrainH + 200) {
            // Warped coordinates for islands
            float ix = x * m_settings.platformFreq;
            float iy = y * (m_settings.platformFreq * 2.0f); // Flattened discs
            float iz = z * m_settings.platformFreq;
            
            float islandVal = m_islandNoise->GenSingle3D(ix, iy, iz, m_settings.seed + 99);
            
            if (islandVal > m_settings.floatingIslandThreshold) {
                return 4; // Island Stone
            }
        }

        return 0; // Air
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        // Simple bounding. Since we added a +200 buffer for islands, we can just grab corners
        // and add the buffer.
        int worldX = cx * 32 * scale;
        int worldZ = cz * 32 * scale;
        
        // Sample just one point for speed, expand bounds generously for the weird noise
        int h = GetHeight((float)worldX, (float)worldZ);
        
        minH = 0; // Safe bottom
        maxH = h + 100; // Buffer
        
        if (maxH > 1000) maxH = 1000;
    }

    std::vector<std::string> GetTexturePaths() const override {
        return {
            "resources/textures/dirt1.jpg",    // ID 1: Core
            "resources/textures/dirt1.png", // ID 2: Top
            "resources/textures/dirt1.jpg",  // ID 3: Filler
            "resources/textures/dirt1.jpg",   // ID 4: Floating Islands
            "resources/textures/dirt1.jpg" // ID 5: Extra
        };
    }

    void OnImGui() override {
        ImGui::TextColored(ImVec4(1, 0, 1, 1), "XENON GENERATOR");
        bool changed = false;

        changed |= ImGui::DragInt("Seed", &m_settings.seed);
        changed |= ImGui::SliderFloat("Warp Strength", &m_settings.warpStrength, 0.0f, 100.0f);
        changed |= ImGui::SliderFloat("Spike Height", &m_settings.spikeHeight, 100.0f, 1000.0f);
        changed |= ImGui::SliderFloat("Island Threshold", &m_settings.floatingIslandThreshold, 0.1f, 0.9f);
        
        if (changed) {
            m_dirty = true;
            Init();
        }
    }
};