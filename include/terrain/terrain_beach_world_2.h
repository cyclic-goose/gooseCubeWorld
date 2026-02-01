#pragma once
#include "terrain_system.h"
#include "chunk.h"
#include <FastNoise/FastNoise.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

// ================================================================================================
// COMPLEX BIOME GENERATOR
// Features:
// - Macro "Cellular" Features: Volcanos, Craters, Mega Mountains
// - Climate System: Temperature & Humidity maps
// - 3D Noise: Caves and Overhangs
// - Material System: Obsidian, Ice, Gravel, Snow, Lava
// ================================================================================================
class ComplexBiomeGenerator : public ITerrainGenerator {
public:
    enum class FeatureType {
        None,
        Volcano,
        Crater,
        MegaMountain
    };

    struct Settings {
        int seed = 4242;
        
        // Global Scales
        float biomeScale = 0.002f;      // Low freq for Temp/Humid
        float featureScale = 0.004f;    // Frequency of Volcanos/Craters
        
        // Feature Probabilities (0.0 - 1.0)
        float volcanoChance = 0.15f;    
        float craterChance = 0.15f;
        float megaMntChance = 0.20f;
        
        // Feature Shapes
        float volcanoHeight = 160.0f;
        float volcanoRadius = 0.7f;     // Relative to cell size
        float craterDepth = 40.0f;
        float craterRadius = 0.6f;
        float megaMntHeight = 250.0f;
        
        // Base Terrain
        float baseHeight = 30.0f;
        float hillAmp = 40.0f;
        int seaLevel = 50;
        
        // 3D Noise (Caves/Overhangs)
        bool enable3D = false;
        float noise3DScale = 0.06f;
        float noise3DThreshold = 0.3f; // > this = solid (for overhangs)
    };

    ComplexBiomeGenerator() : m_settings(Settings()) { Init(); }
    ComplexBiomeGenerator(int seed) { m_settings = Settings(); m_settings.seed = seed; Init(); }

    void Init() override {
        // 1. Feature Map (Cellular / Voronoi)
        // We use CellularDistance to find the center of the nearest "Zone"
        auto fnCell = FastNoise::New<FastNoise::CellularDistance>();
        fnCell->SetDistanceFunction(FastNoise::DistanceFunction::Euclidean);
        // Default ReturnType is distance to nearest point, which is what we want.
        // Removing explicit SetReturnType to avoid enum member errors across versions.
        m_featureNoise = fnCell;

        // 2. Feature ID (Cellular Lookup)
        m_randomNoise = FastNoise::New<FastNoise::Simplex>();

        // 3. Climate Maps
        m_tempNoise = FastNoise::New<FastNoise::Perlin>();
        m_humidNoise = FastNoise::New<FastNoise::Perlin>();

        // 4. Base Terrain Detail
        auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnFractal->SetSource(FastNoise::New<FastNoise::Perlin>());
        fnFractal->SetOctaveCount(4);
        fnFractal->SetGain(0.5f);
        m_detailNoise = fnFractal;
        
        // 5. Mega Mountain Texture
        auto fnRidged = FastNoise::New<FastNoise::FractalRidged>();
        fnRidged->SetSource(FastNoise::New<FastNoise::Simplex>());
        fnRidged->SetOctaveCount(5);
        m_megaMntNoise = fnRidged;

        // 6. 3D Noise (Overhangs/Caves)
        auto fn3D = FastNoise::New<FastNoise::FractalFBm>();
        fn3D->SetSource(FastNoise::New<FastNoise::Simplex>());
        fn3D->SetOctaveCount(3);
        m_noise3D = fn3D;
    }

    std::vector<std::string> GetTexturePaths() const override {
        // Matches fallback_texture_id_table.txt
        return {
            "resources/textures/grass.jpg",       // 1
            "resources/textures/dirt.jpg",        // 2
            "resources/textures/stone.jpg",       // 3
            "resources/textures/snow.jpg",        // 4
            "resources/textures/sand.jpg",        // 5
            "resources/textures/water.jpg",       // 6
            "resources/textures/glass.jpg",       // 7
            "resources/textures/clay.jpg",        // 8
            "resources/textures/gravel.jpg",      // 9
            "resources/textures/mud.jpg",         // 10
            "resources/textures/sandstone.jpg",   // 11
            "resources/textures/ice.jpg",         // 12
            "resources/textures/wood_oak.jpg",    // 13
            "resources/textures/leaves_oak.jpg",  // 14
            "resources/textures/wood_spruce.jpg", // 15
            "resources/textures/leaves_spruce.jpg",// 16
            "resources/textures/grass_swamp.jpg", // 17
            "resources/textures/grass_savanna.jpg",// 18
            "resources/textures/obsidian.jpg",    // 19
            "resources/textures/lava.jpg",        // 20
            "resources/textures/redstone.jpg",    // 21
            "resources/textures/gold_ore.jpg",    // 22
            "resources/textures/diamond_ore.jpg", // 23
            "resources/textures/copper_ore.jpg",  // 24
        };
    }

    // --------------------------------------------------------------------------------------------
    // LOGIC HELPERS
    // --------------------------------------------------------------------------------------------
    
    // Hash a cell coordinate to determine what feature is there
    FeatureType GetFeatureAtCell(int cellX, int cellZ) const {
        // Simple hash
        int h = m_settings.seed + cellX * 374761393 + cellZ * 668265263;
        h = (h ^ (h >> 13)) * 1274126177;
        float r = (float)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f; // 0.0 - 1.0

        if (r < m_settings.volcanoChance) return FeatureType::Volcano;
        if (r < m_settings.volcanoChance + m_settings.craterChance) return FeatureType::Crater;
        if (r < m_settings.volcanoChance + m_settings.craterChance + m_settings.megaMntChance) return FeatureType::MegaMountain;
        
        return FeatureType::None;
    }

    // --------------------------------------------------------------------------------------------
    // BATCH GENERATION
    // --------------------------------------------------------------------------------------------
    void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int lodScale) override {
        static thread_local std::vector<float> bufDetail;
        static thread_local std::vector<float> bufMega;
        static thread_local std::vector<float> bufTemp;
        static thread_local std::vector<float> bufHumid;

        int P = CHUNK_SIZE_PADDED;
        int size2D = P * P;
        
        if (bufDetail.size() != size2D) {
            bufDetail.resize(size2D); bufMega.resize(size2D); 
            bufTemp.resize(size2D); bufHumid.resize(size2D);
        }

        // 1. Coords
        float scale = m_settings.featureScale;
        float startX = (cx * CHUNK_SIZE - 1) * lodScale;
        float startZ = (cz * CHUNK_SIZE - 1) * lodScale;
        float step = lodScale;

        // 2. Generate Base Noises
        m_detailNoise->GenUniformGrid2D(bufDetail.data(), startX * 0.02f, startZ * 0.02f, P, P, step * 0.02f, step * 0.02f, m_settings.seed);
        m_megaMntNoise->GenUniformGrid2D(bufMega.data(), startX * 0.01f, startZ * 0.01f, P, P, step * 0.01f, step * 0.01f, m_settings.seed + 1);
        m_tempNoise->GenUniformGrid2D(bufTemp.data(), startX * m_settings.biomeScale, startZ * m_settings.biomeScale, P, P, step * m_settings.biomeScale, step * m_settings.biomeScale, m_settings.seed + 2);
        m_humidNoise->GenUniformGrid2D(bufHumid.data(), startX * m_settings.biomeScale, startZ * m_settings.biomeScale, P, P, step * m_settings.biomeScale, step * m_settings.biomeScale, m_settings.seed + 3);

        uint8_t* voxels = chunk->voxels;
        int worldYBase = (cy * CHUNK_SIZE * lodScale) - (1 * lodScale);

        // 3. Loop 2D (Columns)
        for (int z = 0; z < P; z++) {
            for (int x = 0; x < P; x++) {
                int idx2D = x + (z * P);
                float wx = startX + (x * step);
                float wz = startZ + (z * step);

                // --- FEATURE LOGIC (Manual Cellular) ---
                float fx = wx * scale;
                float fz = wz * scale;
                int ix = (int)floor(fx);
                int iz = (int)floor(fz);
                
                float minDist = 999.0f;
                int featureX = 0;
                int featureZ = 0;
                
                // 3x3 neighbor search for closest feature center
                for (int nx = -1; nx <= 1; nx++) {
                    for (int nz = -1; nz <= 1; nz++) {
                        int cx = ix + nx;
                        int cz = iz + nz;
                        int h = m_settings.seed + cx * 43758 + cz * 65487;
                        float ox = (float)((h & 1023) / 1023.0f) * 0.8f + 0.1f;
                        float oz = (float)(((h >> 10) & 1023) / 1023.0f) * 0.8f + 0.1f;
                        
                        float dx = (cx + ox) - fx;
                        float dz = (cz + oz) - fz;
                        float d = sqrt(dx*dx + dz*dz);
                        
                        if (d < minDist) {
                            minDist = d;
                            featureX = cx;
                            featureZ = cz;
                        }
                    }
                }
                
                FeatureType feature = GetFeatureAtCell(featureX, featureZ);
                
                // --- HEIGHT CALCULATION ---
                float baseH = m_settings.baseHeight + (bufDetail[idx2D] * m_settings.hillAmp);
                float finalH = baseH;
                float distNorm = minDist * 2.0f; 

                // Feature blending
                if (feature == FeatureType::Volcano) {
                    if (distNorm < m_settings.volcanoRadius) {
                        float vShape = (1.0f - (distNorm / m_settings.volcanoRadius));
                        vShape = vShape * vShape; 
                        finalH += vShape * m_settings.volcanoHeight;
                        if (distNorm < 0.15f) { // Caldera
                            float dip = (0.15f - distNorm) / 0.15f;
                            finalH -= dip * m_settings.volcanoHeight * 1.2f; 
                        }
                    }
                }
                else if (feature == FeatureType::Crater) {
                    if (distNorm < m_settings.craterRadius) {
                        float rel = distNorm / m_settings.craterRadius;
                        float craterShape = (rel * rel) - 0.8f; 
                        finalH += craterShape * m_settings.craterDepth;
                    }
                }
                else if (feature == FeatureType::MegaMountain) {
                    if (distNorm < 1.0f) {
                        float blend = (1.0f - distNorm);
                        finalH += blend * std::abs(bufMega[idx2D]) * m_settings.megaMntHeight;
                    }
                }

                // --- 4. Vertical Loop (Voxels) ---
                int colBase = x + (z * P);
                int strideY = P * P;
                float temp = bufTemp[idx2D];
                float humid = bufHumid[idx2D];
                
                // Biome determination
                int biomeId = 0;
                if (temp > 0.3f && humid < -0.2f) biomeId = 1; // Desert
                if (temp < -0.4f) biomeId = 2; // Snow

                for (int y = 0; y < P; y++) {
                    int wy = worldYBase + (y * lodScale);
                    int idx = colBase + (y * strideY);
                    
                    uint8_t block = 0;

                    // A. Heightmap Logic
                    if (wy <= finalH) {
                        block = 3; // Default Stone
                        
                        bool isSurface = (wy >= finalH - (2 * lodScale));
                        
                        // Biome Blocks
                        if (feature == FeatureType::Volcano) {
                            if (isSurface) block = 19; // Obsidian
                            else block = 3; 
                        } 
                        else if (feature == FeatureType::Crater) {
                            if (isSurface) block = 9; // Gravel
                            else block = 3;
                        }
                        else {
                            if (isSurface) {
                                if (biomeId == 1) block = 5; // Sand
                                else if (biomeId == 2) block = 4; // Snow
                                else block = 1; // Grass
                                
                                // Peak Logic
                                if (wy > 180) block = 4; // Snow caps
                                if (wy > 220) block = 12; // Ice peaks
                            } else {
                                // Subsurface
                                if (biomeId == 1) block = 11; // Sandstone
                                else block = 2; // Dirt
                            }
                        }
                    }

                    // B. Water / Lava
                    if (block == 0) {
                        if (wy <= m_settings.seaLevel) {
                             if (biomeId == 2) block = 12; // Ice
                             else block = 6; // Water
                        }
                        // Lava in volcano center
                        if (feature == FeatureType::Volcano && distNorm < 0.1f && wy < (finalH + 20) && wy > 10) {
                            block = 20; // Lava
                        }
                    }

                    // C. 3D Noise (Caves & Overhangs)
                    if (m_settings.enable3D) {
                        if (lodScale == 1) { 
                            float n3 = m_noise3D->GenSingle3D(wx * m_settings.noise3DScale, (float)wy * m_settings.noise3DScale, wz * m_settings.noise3DScale, m_settings.seed);
                            
                            // Cave Carver
                            if (block != 0 && block != 6 && block != 20) { // Don't carve water/lava
                                if (n3 < -0.4f) block = 0; 
                            }

                            // Overhang Adder
                            if (block == 0 && wy > m_settings.seaLevel) {
                                if (n3 > m_settings.noise3DThreshold) {
                                    float n3Above = m_noise3D->GenSingle3D(wx * m_settings.noise3DScale, (float)(wy + 1) * m_settings.noise3DScale, wz * m_settings.noise3DScale, m_settings.seed);
                                    if (n3Above <= m_settings.noise3DThreshold) block = 1; // Top (Grass)
                                    else block = 3; // Body (Stone)
                                }
                            }
                        }
                    }

                    voxels[idx] = block;
                }
            }
        }
    }

    // --------------------------------------------------------------------------------------------
    // API
    // --------------------------------------------------------------------------------------------
    uint8_t GetBlock(float x, float y, float z, int lodScale) const override {
        // Simplified Physics Query
        
        float scale = m_settings.featureScale;
        float fx = x * scale;
        float fz = z * scale;
        int ix = (int)floor(fx);
        int iz = (int)floor(fz);
        
        float minDist = 999.0f;
        int featureX = 0, featureZ = 0;

        for (int nx = -1; nx <= 1; nx++) {
            for (int nz = -1; nz <= 1; nz++) {
                int cx = ix + nx;
                int cz = iz + nz;
                int h = m_settings.seed + cx * 43758 + cz * 65487;
                float ox = (float)((h & 1023) / 1023.0f) * 0.8f + 0.1f;
                float oz = (float)(((h >> 10) & 1023) / 1023.0f) * 0.8f + 0.1f;
                float dx = (cx + ox) - fx;
                float dz = (cz + oz) - fz;
                float d = sqrt(dx*dx + dz*dz);
                if (d < minDist) { minDist = d; featureX = cx; featureZ = cz; }
            }
        }

        FeatureType feature = GetFeatureAtCell(featureX, featureZ);
        
        float baseH = m_settings.baseHeight + (m_detailNoise->GenSingle2D(x * 0.02f, z * 0.02f, m_settings.seed) * m_settings.hillAmp);
        float finalH = baseH;
        float distNorm = minDist * 2.0f;

        if (feature == FeatureType::Volcano) {
            if (distNorm < m_settings.volcanoRadius) {
                float vShape = (1.0f - (distNorm / m_settings.volcanoRadius));
                finalH += (vShape * vShape) * m_settings.volcanoHeight;
                if (distNorm < 0.15f) finalH -= ((0.15f - distNorm) / 0.15f) * m_settings.volcanoHeight * 1.2f;
            }
        }
        else if (feature == FeatureType::Crater) {
             if (distNorm < m_settings.craterRadius) {
                float rel = distNorm / m_settings.craterRadius;
                finalH += ((rel * rel) - 0.8f) * m_settings.craterDepth;
            }
        }
        else if (feature == FeatureType::MegaMountain) {
             if (distNorm < 1.0f) {
                 float mega = std::abs(m_megaMntNoise->GenSingle2D(x * 0.01f, z * 0.01f, m_settings.seed + 1));
                 finalH += (1.0f - distNorm) * mega * m_settings.megaMntHeight;
             }
        }

        if (y <= finalH) return 3; // Stone
        if (y <= m_settings.seaLevel) return 6; // Water
        
        if (m_settings.enable3D) {
             float n3 = m_noise3D->GenSingle3D(x * m_settings.noise3DScale, y * m_settings.noise3DScale, z * m_settings.noise3DScale, m_settings.seed);
             if (n3 > m_settings.noise3DThreshold) return 3; 
        }

        return 0;
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        minH = 0;
        maxH = 512; 
    }

    void OnImGui() override {
#ifdef IMGUI_VERSION
        ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "Complex Biome Gen");
        bool changed = false;

        if (ImGui::CollapsingHeader("Features & Probability", ImGuiTreeNodeFlags_DefaultOpen)) {
            if(ImGui::DragInt("Seed", &m_settings.seed)) changed = true;
            if(ImGui::SliderFloat("Global Feature Freq", &m_settings.featureScale, 0.001f, 0.02f)) changed = true;
            if(ImGui::SliderFloat("Volcano Chance", &m_settings.volcanoChance, 0.0f, 0.5f)) changed = true;
            if(ImGui::SliderFloat("Crater Chance", &m_settings.craterChance, 0.0f, 0.5f)) changed = true;
            if(ImGui::SliderFloat("Mega Mnt Chance", &m_settings.megaMntChance, 0.0f, 0.5f)) changed = true;
        }

        if (ImGui::CollapsingHeader("Feature Dimensions")) {
            ImGui::DragFloat("Volcano Height", &m_settings.volcanoHeight, 1.0f, 50.0f, 500.0f);
            if(ImGui::SliderFloat("Volcano Radius", &m_settings.volcanoRadius, 0.1f, 1.0f)) changed = true;
            ImGui::DragFloat("Crater Depth", &m_settings.craterDepth, 1.0f, 10.0f, 200.0f);
            ImGui::DragFloat("Mega Mnt Height", &m_settings.megaMntHeight, 1.0f, 50.0f, 1000.0f);
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        }

        if (ImGui::CollapsingHeader("Biome & Base")) {
            if(ImGui::SliderFloat("Biome Scale", &m_settings.biomeScale, 0.001f, 0.01f)) changed = true;
            if(ImGui::DragFloat("Base Height", &m_settings.baseHeight, 1.0f, 0.0f, 100.0f)) changed = true;
            if(ImGui::DragInt("Sea Level", &m_settings.seaLevel, 1, 0, 200)) changed = true;
        }
        
        if (ImGui::CollapsingHeader("3D Noise")) {
            if(ImGui::Checkbox("Enable 3D Overhangs", &m_settings.enable3D)) changed = true;
            if(ImGui::SliderFloat("3D Scale", &m_settings.noise3DScale, 0.01f, 0.2f)) changed = true;
            if(ImGui::SliderFloat("3D Threshold", &m_settings.noise3DThreshold, -0.5f, 0.8f)) changed = true;
        }

        if (changed) { m_dirty = true; Init(); }
#endif
    }

private:
    Settings m_settings;
    FastNoise::SmartNode<> m_featureNoise; // Cellular
    FastNoise::SmartNode<> m_randomNoise;
    FastNoise::SmartNode<> m_tempNoise;
    FastNoise::SmartNode<> m_humidNoise;
    FastNoise::SmartNode<> m_detailNoise;
    FastNoise::SmartNode<> m_megaMntNoise;
    FastNoise::SmartNode<> m_noise3D;
};