#pragma once
#include "terrain_system.h"
#include "chunk.h"
#include <FastNoise/FastNoise.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

// ================================================================================================
// ADVANCED GENERATOR
// Biomes, Trees, Water, Snow, efficient batching, and RARE MEGA PEAKS.
// ================================================================================================
class AdvancedGenerator : public ITerrainGenerator {
public:
    struct GenSettings {
        int seed = 1804289383;
        float scale = 0.073f;           // Global scale
        
        // Height params
        float minHeight = 20.0f;
        float hillAmp = 60.0f;
        float hillFreq = 5.0f;         
        float mntAmp = 25.0f;
        float mntFreq = 1.0f;          
        
        // Everest / Mega Peak Settings
        float everestFreq = 0.6f;      // How rare are they? (Lower = rarer)
        float everestAmp = 600.0f;     // How tall? (Added on top of existing mountains)
        float everestThreshold = 0.7f; // Threshold to trigger a mega peak (0.0 - 1.0)

        // Limits
        int seaLevel = 30;
        int maxHeight = 1024;           // Raised limit for Everest
        int bedrockDepth = 3;          
        
        // Biome Thresholds
        float tempScale = 0.005f;      
        float rainScale = 0.005f;
        
        // Trees
        int treeDensityForest = 15;    
        int treeDensityPlains = 100;
        int treeDensityDesert = 200;   
    };

    AdvancedGenerator() : m_settings(GenSettings()) { Init(); }
    AdvancedGenerator(int seed) { m_settings = GenSettings(); m_settings.seed = seed; Init(); }
    AdvancedGenerator(GenSettings settings) : m_settings(settings) { Init(); }

    void Init() override {
        // 1. Height Noise (Fractal)
        auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
        auto fnHeight = FastNoise::New<FastNoise::FractalFBm>();
        fnHeight->SetSource(fnPerlin);
        fnHeight->SetOctaveCount(5);
        fnHeight->SetGain(0.5f);
        fnHeight->SetLacunarity(2.0f);
        m_heightNoise = fnHeight;

        // 2. Mountain/Roughness Noise (Simplex)
        auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
        auto fnMnt = FastNoise::New<FastNoise::FractalFBm>();
        fnMnt->SetSource(fnSimplex);
        fnMnt->SetOctaveCount(4);
        m_mountainNoise = fnMnt;

        // 3. Climate Noises (Low frequency Perlin)
        m_tempNoise = FastNoise::New<FastNoise::Perlin>(); 
        m_moistNoise = FastNoise::New<FastNoise::Perlin>();
        
        // 4. Cave Noise
        m_caveNoise = FastNoise::New<FastNoise::Perlin>();

        // 5. Mega Peak Noise (Very Low Freq Simplex)
        m_megaNoise = FastNoise::New<FastNoise::Simplex>();
    }

    std::vector<std::string> GetTexturePaths() const override {
        std::vector<std::string> textures(30, "resources/textures/dirt1.jpg");
        return textures;
    }

    inline int Hash(int x, int z, int seed) const {
        int h = seed + x * 374761393 + z * 668265263;
        h = (h ^ (h >> 13)) * 1274126177;
        return (h ^ (h >> 16));
    }

    // --------------------------------------------------------------------------------------------
    // PHYSICS / SINGLE BLOCK API
    // --------------------------------------------------------------------------------------------
    int GetHeight(float x, float z) const {
        float nx = x * m_settings.scale;
        float nz = z * m_settings.scale;

        // Composite Height
        float baseH = m_heightNoise->GenSingle2D(nx * m_settings.hillFreq, nz * m_settings.hillFreq, m_settings.seed);
        float mntH = m_mountainNoise->GenSingle2D(nx * 0.5f * m_settings.mntFreq, nz * 0.5f * m_settings.mntFreq, m_settings.seed + 1);
        
        // Mega Peak Logic
        float megaZone = m_megaNoise->GenSingle2D(nx * m_settings.everestFreq * 0.1f, nz * m_settings.everestFreq * 0.1f, m_settings.seed + 99);
        float megaBoost = 0.0f;
        
        if (megaZone > m_settings.everestThreshold) {
            float factor = (megaZone - m_settings.everestThreshold) / (1.0f - m_settings.everestThreshold);
            factor = factor * factor; 
            megaBoost = factor * m_settings.everestAmp;
        }

        // Make mountains pointier
        float mntFactor = std::abs(mntH); 
        mntFactor = mntFactor * mntFactor * mntFactor; 

        float finalH = m_settings.minHeight + (baseH * m_settings.hillAmp) + (mntFactor * m_settings.mntAmp) + megaBoost;
        
        if (finalH > (float)m_settings.maxHeight) finalH = (float)m_settings.maxHeight;

        return (int)finalH;
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        minH = 0;
        maxH = m_settings.maxHeight;
    }

    uint8_t GetBlock(float x, float y, float z, int lodScale) const override {
        int h = GetHeight(x, z);
        int wy = (int)y;
        
        if (wy <= m_settings.bedrockDepth) return 19; 

        if (wy > h) {
            if (wy <= m_settings.seaLevel) return 6; 
            return 0; 
        }
        return 3; 
    }

    // --------------------------------------------------------------------------------------------
    // BATCHED GENERATION
    // --------------------------------------------------------------------------------------------
    void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int lodScale) override {
        static thread_local std::vector<float> bufHeightRaw;
        static thread_local std::vector<float> bufMntRaw;
        static thread_local std::vector<float> bufMegaRaw;
        static thread_local std::vector<float> bufTemp;
        static thread_local std::vector<float> bufMoist;
        static thread_local std::vector<float> bufCave;
        static thread_local std::vector<int>   mapHeight;
        static thread_local std::vector<uint8_t> mapBiome; 
        static thread_local std::vector<uint8_t> mapTree;  

        int P = CHUNK_SIZE_PADDED;
        int size2D = P * P;
        int size3D = P * P * P;

        if (bufHeightRaw.size() != size2D) {
            bufHeightRaw.resize(size2D); bufMntRaw.resize(size2D); bufMegaRaw.resize(size2D);
            bufTemp.resize(size2D); bufMoist.resize(size2D);
            mapHeight.resize(size2D); mapBiome.resize(size2D); mapTree.resize(size2D);
            bufCave.resize(size3D);
        }

        // 1. Coordinates
        float scale = m_settings.scale;
        float startX = (cx * CHUNK_SIZE - 1) * scale * lodScale;
        float startZ = (cz * CHUNK_SIZE - 1) * scale * lodScale;
        float step = scale * lodScale;
        
        // 2. Noises
        float hillStep = step * m_settings.hillFreq;
        m_heightNoise->GenUniformGrid2D(bufHeightRaw.data(), startX * m_settings.hillFreq, startZ * m_settings.hillFreq, P, P, hillStep, hillStep, m_settings.seed);
        
        float mntStep = step * 0.5f * m_settings.mntFreq;
        m_mountainNoise->GenUniformGrid2D(bufMntRaw.data(), startX * 0.5f * m_settings.mntFreq, startZ * 0.5f * m_settings.mntFreq, P, P, mntStep, mntStep, m_settings.seed + 1);
        
        float megaStep = step * m_settings.everestFreq * 0.1f;
        m_megaNoise->GenUniformGrid2D(bufMegaRaw.data(), startX * m_settings.everestFreq * 0.1f, startZ * m_settings.everestFreq * 0.1f, P, P, megaStep, megaStep, m_settings.seed + 99);

        float climStep = step * 0.1f; 
        m_tempNoise->GenUniformGrid2D(bufTemp.data(), startX, startZ, P, P, climStep, climStep, m_settings.seed + 2);
        m_moistNoise->GenUniformGrid2D(bufMoist.data(), startX, startZ, P, P, climStep, climStep, m_settings.seed + 3);

        // 3. Process Maps
        for (int i = 0; i < size2D; i++) {
            // -- Mega Peak Boost --
            float megaVal = bufMegaRaw[i];
            float megaBoost = 0.0f;
            if (megaVal > m_settings.everestThreshold) {
                float factor = (megaVal - m_settings.everestThreshold) / (1.0f - m_settings.everestThreshold);
                megaBoost = (factor * factor) * m_settings.everestAmp;
            }

            // -- Height --
            float hBase = bufHeightRaw[i];
            float mRaw = bufMntRaw[i];
            float mFac = std::abs(mRaw);
            mFac = mFac * mFac * mFac;
            
            float finalH = m_settings.minHeight + (hBase * m_settings.hillAmp) + (mFac * m_settings.mntAmp) + megaBoost;
            if (finalH > (float)m_settings.maxHeight) finalH = (float)m_settings.maxHeight;
            
            int h = (int)finalH;
            mapHeight[i] = h;

            // -- Biome --
            float temp = bufTemp[i];  
            float moist = bufMoist[i];
            temp -= (h - m_settings.seaLevel) * 0.005f; // Height makes it cold

            uint8_t biome = 0; 
            if (temp > 0.4f && moist < -0.2f) biome = 2; // Desert
            else if (temp < -0.3f) biome = 3; // Snow
            else if (moist > 0.2f) biome = 1; // Forest
            
            // Force "Peak" biome behavior if extremely high
            if (h > 220) biome = 3; // Permanent Snow/Ice zone

            mapBiome[i] = biome;

            // -- Trees --
            mapTree[i] = 0;
            if (lodScale == 1) {
                int localX = i % P;
                int localZ = i / P;
                int wx = (cx * CHUNK_SIZE - 1) + localX;
                int wz = (cz * CHUNK_SIZE - 1) + localZ;

                int chance = 0; 
                uint8_t treeType = 0;

                if (biome == 1) { chance = m_settings.treeDensityForest; treeType = 1; }
                else if (biome == 0) { chance = m_settings.treeDensityPlains; treeType = 1; }
                else if (biome == 2) { chance = m_settings.treeDensityDesert; treeType = 3; }
                else if (biome == 3) { chance = 60; treeType = 2; }

                if (h > m_settings.seaLevel && h < 200 && chance > 0) {
                    int roll = std::abs(Hash(wx, wz, m_settings.seed)) % chance;
                    if (roll == 0) mapTree[i] = treeType;
                }
            }
        }

        // 4. Caves
        if (lodScale == 1) {
            float cScale = 0.03f; 
            float csX = (cx * CHUNK_SIZE - 1) * cScale;
            float csY = (cy * CHUNK_SIZE - 1) * cScale;
            float csZ = (cz * CHUNK_SIZE - 1) * cScale;
            m_caveNoise->GenUniformGrid3D(bufCave.data(), csX, csY, csZ, P, P, P, cScale, cScale, cScale, m_settings.seed);
        }

        // 5. Fill Voxels
        uint8_t* voxels = chunk->voxels;
        int worldYBase = (cy * CHUNK_SIZE * lodScale) - (1 * lodScale);
        bool doCaves = (lodScale == 1);
        int bedrockLevel = m_settings.bedrockDepth;

        for (int z = 0; z < P; z++) {
            for (int x = 0; x < P; x++) {
                int idx2D = x + (z * P);
                int h = mapHeight[idx2D];
                int biome = mapBiome[idx2D];
                int treeTypeHere = mapTree[idx2D];
                int colBaseIdx = x + (z * P); 
                int strideY = P * P;

                for (int y = 0; y < P; y++) {
                    int wy = worldYBase + (y * lodScale);
                    int idxVoxel = colBaseIdx + (y * strideY);
                    
                    uint8_t block = 0; 

                    if (wy <= bedrockLevel && wy >= 0) {
                        block = 19; // Bedrock
                    }
                    else if (wy <= h) {
                        block = 3; // Default Stone
                        
                        // Scale the layer checks by LOD to prevent missing the surface
                        // "Depth" is how far we are below the true surface height
                        int depth = h - wy;

                        // Surface layers
                        // Ensure we catch the surface even if lodScale is large
                        if (depth < lodScale) {
                            if (biome == 2) block = 5;      
                            else if (biome == 3) block = 4; // Snow
                            else block = 1;                 // Grass
                        } 
                        // Sub-surface
                        else if (depth < (4 * lodScale)) {
                            if (biome == 2) block = 11;     // Sandstone
                            else if (biome == 3) block = 4; // Snow (Use pure snow for depth at distance)
                            else block = 2;                 // Dirt
                        }
                        
                        // EVEREST SPECIAL: Exposed Rock/Ice at very high altitudes
                        if (wy > 220) {
                            // Check depth-based logic for consistent caps at distance
                            if (depth < lodScale) block = 4; // Snow cap
                            else if (depth < (10 * lodScale)) block = 12; // Ice sheet below snow
                            else block = 19; // Exposed dark bedrock/obsidian core
                        }

                        // Caves (Only detail level 1)
                        if (doCaves && wy > bedrockLevel) {
                            int idx3D = x + (y * P) + (z * P * P);
                            if (bufCave[idx3D] > 0.4f) block = 0;
                        }
                    } 
                    else {
                        if (wy <= m_settings.seaLevel) {
                             if (biome == 3) block = 12; 
                             else block = 6;             
                        }
                    }

                    // Trees
                    if (block == 0 && lodScale == 1) {
                        if (treeTypeHere != 0) {
                            int relY = wy - h;
                            if ((treeTypeHere == 1 || treeTypeHere == 2) && relY > 0 && relY < 6) {
                                block = (treeTypeHere == 2) ? 15 : 13; 
                            }
                            if (treeTypeHere == 3 && relY > 0 && relY < 4) {
                                block = 14; 
                            }
                        }
                        if (block == 0) {
                            for (int nz = -1; nz <= 1; nz++) {
                                for (int nx = -1; nx <= 1; nx++) {
                                    int neighborX = x + nx;
                                    int neighborZ = z + nz;
                                    
                                    if (neighborX >= 0 && neighborX < P && neighborZ >= 0 && neighborZ < P) {
                                        int nIdx = neighborX + (neighborZ * P);
                                        int nTree = mapTree[nIdx];
                                        int nH = mapHeight[nIdx];
                                        int nRelY = wy - nH;
                                        if (nTree == 1 && nRelY >= 4 && nRelY <= 6) block = 14; 
                                        else if (nTree == 2 && nRelY >= 3 && nRelY <= 6) block = 16;
                                    }
                                }
                            }
                        }
                    }

                    voxels[idxVoxel] = block;
                }
            }
        }
    }
    
    void OnImGui() override {
#ifdef IMGUI_VERSION
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 1.0f, 1.0f), "Advanced Gen Settings");
        bool changed = false;
        
        if (ImGui::CollapsingHeader("Global & Limits", ImGuiTreeNodeFlags_DefaultOpen)) {
            if(ImGui::DragInt("Seed", &m_settings.seed)) changed = true;
            if(ImGui::SliderFloat("Scale", &m_settings.scale, 0.001f, 0.1f)) changed = true;
            if(ImGui::SliderFloat("Biome Temperature Scale", &m_settings.tempScale, 0.001f, 0.1f)) changed = true;
            if(ImGui::SliderFloat("Biome Rain Map Scale", &m_settings.rainScale, 0.001f, 0.1f)) changed = true;
            if(ImGui::SliderInt("Max Height", &m_settings.maxHeight, 50, 2048)) changed = true; 
            if(ImGui::SliderInt("Bedrock Depth", &m_settings.bedrockDepth, 0, 10)) changed = true;
            if(ImGui::SliderInt("Sea Level", &m_settings.seaLevel, 0, 200)) changed = true;
        }

        if (ImGui::CollapsingHeader("Terrain Shape")) {
            if(ImGui::DragFloat("Hill Amp", &m_settings.hillAmp, 1.0f, 0.0f, 200.0f)) changed = true;
            if(ImGui::DragFloat("Mnt Amp", &m_settings.mntAmp, 1.0f, 0.0f, 500.0f)) changed = true;
            if(ImGui::DragFloat("Min Height", &m_settings.minHeight, 1.0f, -50.0f, 100.0f)) changed = true;
        }

        if (ImGui::CollapsingHeader("Mega Peaks (Everest)")) {
            ImGui::TextDisabled("Rare, massive mountains");
            if(ImGui::SliderFloat("Peak Amp", &m_settings.everestAmp, 0.0f, 1000.0f)) changed = true;
            if(ImGui::SliderFloat("Peak Rarity", &m_settings.everestFreq, 0.01f, 1.0f)) changed = true;
            if(ImGui::SliderFloat("Peak Threshold", &m_settings.everestThreshold, 0.1f, 0.95f)) changed = true;
        }

        if (ImGui::CollapsingHeader("Biomes & Trees")) {
             if(ImGui::SliderInt("Forest Density", &m_settings.treeDensityForest, 1, 100)) changed = true;
             if(ImGui::SliderInt("Plains Density", &m_settings.treeDensityPlains, 1, 200)) changed = true;
        }

        if (changed) { m_dirty = true; Init(); }
#endif
    }

private:
    GenSettings m_settings;
    FastNoise::SmartNode<> m_heightNoise;
    FastNoise::SmartNode<> m_mountainNoise;
    FastNoise::SmartNode<> m_tempNoise;
    FastNoise::SmartNode<> m_moistNoise;
    FastNoise::SmartNode<> m_caveNoise;
    FastNoise::SmartNode<> m_megaNoise; 
};