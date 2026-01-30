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
        float everestAmp = 1200.0f;     // How tall? (Added on top of existing mountains)
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
        auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
        auto fnHeight = FastNoise::New<FastNoise::FractalFBm>();
        fnHeight->SetSource(fnPerlin);
        fnHeight->SetOctaveCount(5);
        fnHeight->SetGain(0.5f);
        fnHeight->SetLacunarity(2.0f);
        m_heightNoise = fnHeight;

        auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
        auto fnMnt = FastNoise::New<FastNoise::FractalFBm>();
        fnMnt->SetSource(fnSimplex);
        fnMnt->SetOctaveCount(4);
        m_mountainNoise = fnMnt;

        m_tempNoise = FastNoise::New<FastNoise::Perlin>(); 
        m_moistNoise = FastNoise::New<FastNoise::Perlin>();
        m_caveNoise = FastNoise::New<FastNoise::Perlin>();
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

    int GetHeight(float x, float z) const {
        float nx = x * m_settings.scale;
        float nz = z * m_settings.scale;

        float baseH = m_heightNoise->GenSingle2D(nx * m_settings.hillFreq, nz * m_settings.hillFreq, m_settings.seed);
        float mntH = m_mountainNoise->GenSingle2D(nx * 0.5f * m_settings.mntFreq, nz * 0.5f * m_settings.mntFreq, m_settings.seed + 1);
        
        float megaZone = m_megaNoise->GenSingle2D(nx * m_settings.everestFreq * 0.1f, nz * m_settings.everestFreq * 0.1f, m_settings.seed + 99);
        float megaBoost = 0.0f;
        
        if (megaZone > m_settings.everestThreshold) {
            float factor = (megaZone - m_settings.everestThreshold) / (1.0f - m_settings.everestThreshold);
            factor = factor * factor; 
            megaBoost = factor * m_settings.everestAmp;
        }

        float mntFactor = std::abs(mntH); 
        mntFactor = mntFactor * mntFactor * mntFactor; 

        float finalH = m_settings.minHeight + (baseH * m_settings.hillAmp) + (mntFactor * m_settings.mntAmp) + megaBoost;
        return (int)std::clamp(finalH, 0.0f, (float)m_settings.maxHeight);
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        minH = 0;
        maxH = m_settings.maxHeight;
    }

    // UPDATED: Now includes Tree generation logic so Physics/Collision works.
    uint8_t GetBlock(float x, float y, float z, int lodScale) const override {
        int ix = (int)std::floor(x);
        int iz = (int)std::floor(z);
        int wy = (int)std::floor(y);

        // 1. Base Height
        int h = GetHeight(x, z);

        // 2. Bedrock
        if (wy <= m_settings.bedrockDepth) return 19; 

        // 3. Biome Data (Needed for correct tree type)
        // We do a simplified single-point check here for physics
        float nx = x * m_settings.scale;
        float nz = z * m_settings.scale;
        float tempVal = m_tempNoise->GenSingle2D(nx * 0.1f, nz * 0.1f, m_settings.seed + 2);
        float moistVal = m_moistNoise->GenSingle2D(nx * 0.1f, nz * 0.1f, m_settings.seed + 3);
        float temp = tempVal - (h - m_settings.seaLevel) * 0.005f;
        
        uint8_t biome = 0; 
        if (temp > 0.4f && moistVal < -0.2f) biome = 2; // Desert
        else if (temp < -0.3f || h > 220) biome = 3;    // Snow
        else if (moistVal > 0.2f) biome = 1;            // Forest

        // 4. Tree Collision Logic
        if (lodScale == 1 && wy > h) {
            // Check immediate area for tree origins
            // This is slightly expensive but necessary for accurate collision
            for (int dx = -2; dx <= 2; ++dx) {
                for (int dz = -2; dz <= 2; ++dz) {
                    int checkX = ix + dx;
                    int checkZ = iz + dz;
                    
                    int chance = 0; uint8_t treeType = 0;
                    // Note: Ideally we'd sample biome for neighbor, but using current biome is a safe approx for physics
                    if (biome == 1) { chance = m_settings.treeDensityForest; treeType = 1; }
                    else if (biome == 0) { chance = m_settings.treeDensityPlains; treeType = 1; }
                    else if (biome == 2) { chance = m_settings.treeDensityDesert; treeType = 3; }
                    else if (biome == 3) { chance = 60; treeType = 2; }
                    
                    if (chance > 0) {
                         int neighborH = GetHeight((float)checkX, (float)checkZ);
                         // Check valid tree spawn height
                         if (neighborH > m_settings.seaLevel && neighborH < 200) {
                            if ((std::abs(Hash(checkX, checkZ, m_settings.seed)) % chance) == 0) {
                                int relY = wy - neighborH;
                                int distSq = dx*dx + dz*dz;
                                
                                // Trunk
                                if (distSq == 0) {
                                    if ((treeType == 1 || treeType == 2) && relY > 0 && relY < 6) return (treeType == 2) ? 15 : 13;
                                    if (treeType == 3 && relY > 0 && relY < 4) return 14; 
                                } 
                                // Leaves
                                else {
                                    if (treeType == 1 && relY >= 4 && relY <= 6 && distSq <= 2) return 14; 
                                    if (treeType == 2 && relY >= 3 && relY <= 6 && distSq <= 2) return 16; 
                                }
                            }
                         }
                    }
                }
            }
        }

        // 5. Standard Blocks
        if (wy > h) {
            if (wy <= m_settings.seaLevel) return (biome == 3) ? 12 : 6; 
            return 0; 
        }

        // 6. Underground / Surface
        int depth = h - wy;
        uint8_t block = 3; 
        if (depth < lodScale) {
            block = (biome == 2) ? 5 : (biome == 3 ? 4 : 1);
        } else if (depth < (4 * lodScale)) {
            block = (biome == 2) ? 11 : (biome == 3 ? 4 : 2);
        }
        
        if (wy > 220) {
            if (depth < lodScale) block = 4;
            else if (depth < (10 * lodScale)) block = 12;
            else block = 19;
        }

        // Cave Logic (Replicated from GenerateChunk)
        if (lodScale == 1 && wy > m_settings.bedrockDepth) {
             float cs = 0.03f;
             // IMPORTANT: Axis mapping must match GenerateChunk (X, Z, Y)
             float caveVal = m_caveNoise->GenSingle3D(x * cs, z * cs, y * cs, m_settings.seed);
             
             // FIX: Surface Bias prevents surface holes (and thus floating trees)
             int depthFromSurface = h - wy;
             float surfaceBias = 0.0f;
             if (depthFromSurface < 8) {
                 surfaceBias = (8 - depthFromSurface) * 0.08f; 
             }
             
             if ((caveVal - surfaceBias) > 0.4f) return 0;
        }

        return block; 
    }

    void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int lodScale) override {
        // Using static thread_local is efficient for buffer reuse
        static thread_local std::vector<float> bufHeightRaw;
        static thread_local std::vector<float> bufMntRaw;
        static thread_local std::vector<float> bufMegaRaw;
        static thread_local std::vector<float> bufTemp;
        static thread_local std::vector<float> bufMoist;
        static thread_local std::vector<float> bufCave;
        static thread_local std::vector<int>   mapHeight;
        static thread_local std::vector<uint8_t> mapBiome; 
        static thread_local std::vector<uint8_t> mapTree;  

        const int P = CHUNK_SIZE_PADDED;
        const int size2D = P * P;
        const int size3D = P * P * P;

        if (bufHeightRaw.size() != size2D) {
            bufHeightRaw.resize(size2D); bufMntRaw.resize(size2D); bufMegaRaw.resize(size2D);
            bufTemp.resize(size2D); bufMoist.resize(size2D);
            mapHeight.resize(size2D); mapBiome.resize(size2D); mapTree.resize(size2D);
            bufCave.resize(size3D);
        }

        float s = m_settings.scale;
        
        float worldStartX = (float)((cx * CHUNK_SIZE - 1) * lodScale);
        float worldStartZ = (float)((cz * CHUNK_SIZE - 1) * lodScale);
        float worldYStart = (float)((cy * CHUNK_SIZE - 1) * lodScale);
        float worldStep   = (float)lodScale;

        m_heightNoise->GenUniformGrid2D(bufHeightRaw.data(), worldStartX * s * m_settings.hillFreq, worldStartZ * s * m_settings.hillFreq, P, P, worldStep * s * m_settings.hillFreq, worldStep * s * m_settings.hillFreq, m_settings.seed);
        m_mountainNoise->GenUniformGrid2D(bufMntRaw.data(), worldStartX * s * 0.5f * m_settings.mntFreq, worldStartZ * s * 0.5f * m_settings.mntFreq, P, P, worldStep * s * 0.5f * m_settings.mntFreq, worldStep * s * 0.5f * m_settings.mntFreq, m_settings.seed + 1);
        m_megaNoise->GenUniformGrid2D(bufMegaRaw.data(), worldStartX * s * m_settings.everestFreq * 0.1f, worldStartZ * s * m_settings.everestFreq * 0.1f, P, P, worldStep * s * m_settings.everestFreq * 0.1f, worldStep * s * m_settings.everestFreq * 0.1f, m_settings.seed + 99);
        m_tempNoise->GenUniformGrid2D(bufTemp.data(), worldStartX * s * 0.1f, worldStartZ * s * 0.1f, P, P, worldStep * s * 0.1f, worldStep * s * 0.1f, m_settings.seed + 2);
        m_moistNoise->GenUniformGrid2D(bufMoist.data(), worldStartX * s * 0.1f, worldStartZ * s * 0.1f, P, P, worldStep * s * 0.1f, worldStep * s * 0.1f, m_settings.seed + 3);

        for (int i = 0; i < size2D; i++) {
            float megaVal = bufMegaRaw[i];
            float megaBoost = (megaVal > m_settings.everestThreshold) ? 
                std::pow((megaVal - m_settings.everestThreshold) / (1.0f - m_settings.everestThreshold), 2) * m_settings.everestAmp : 0.0f;

            float mFac = std::pow(std::abs(bufMntRaw[i]), 3);
            float finalH = m_settings.minHeight + (bufHeightRaw[i] * m_settings.hillAmp) + (mFac * m_settings.mntAmp) + megaBoost;
            mapHeight[i] = (int)std::clamp(finalH, 0.0f, (float)m_settings.maxHeight);

            float temp = bufTemp[i] - (mapHeight[i] - m_settings.seaLevel) * 0.005f;
            float moist = bufMoist[i];

            uint8_t biome = 0; 
            if (temp > 0.4f && moist < -0.2f) biome = 2; // Desert
            else if (temp < -0.3f || mapHeight[i] > 220) biome = 3; // Snow/Peak
            else if (moist > 0.2f) biome = 1; // Forest
            mapBiome[i] = biome;

            mapTree[i] = 0;
            if (lodScale == 1) {
                int wx = (cx * CHUNK_SIZE - 1) + (i % P);
                int wz = (cz * CHUNK_SIZE - 1) + (i / P);
                int chance = 0; uint8_t treeType = 0;
                if (biome == 1) { chance = m_settings.treeDensityForest; treeType = 1; }
                else if (biome == 0) { chance = m_settings.treeDensityPlains; treeType = 1; }
                else if (biome == 2) { chance = m_settings.treeDensityDesert; treeType = 3; }
                else if (biome == 3) { chance = 60; treeType = 2; }

                if (mapHeight[i] > m_settings.seaLevel && mapHeight[i] < 200 && chance > 0) {
                    if ((std::abs(Hash(wx, wz, m_settings.seed)) % chance) == 0) mapTree[i] = treeType;
                }
            }
        }

        if (lodScale == 1) {
            float cs = 0.03f;
            m_caveNoise->GenUniformGrid3D(bufCave.data(), 
                worldStartX * cs,   
                worldStartZ * cs,   
                worldYStart * cs,   
                P, P, P,                  
                cs, cs, cs,                 
                m_settings.seed);
        }

        uint8_t* voxels = chunk->voxels;
        
        for (int y = 0; y < P; y++) {
            int wy = (int)worldYStart + (y * lodScale);
            for (int z = 0; z < P; z++) {
                for (int x = 0; x < P; x++) {
                    int idx2D = x + (z * P);
                    int idx3D = x + (z * P) + (y * P * P); 
                    
                    int h = mapHeight[idx2D];
                    uint8_t biome = mapBiome[idx2D];
                    uint8_t block = 0;

                    if (wy <= m_settings.bedrockDepth && wy >= 0) {
                        block = 19;
                    } else if (wy <= h) {
                        block = 3; 
                        int depth = h - wy;
                        if (depth < lodScale) {
                            block = (biome == 2) ? 5 : (biome == 3 ? 4 : 1);
                        } else if (depth < (4 * lodScale)) {
                            block = (biome == 2) ? 11 : (biome == 3 ? 4 : 2);
                        }
                        
                        if (wy > 220) {
                            if (depth < lodScale) block = 4;
                            else if (depth < (10 * lodScale)) block = 12;
                            else block = 19;
                        }

                        if (lodScale == 1 && wy > m_settings.bedrockDepth) {
                             // FIX: Surface Bias to prevent holes in surface
                             int depthFromSurface = h - wy;
                             float surfaceBias = 0.0f;
                             if (depthFromSurface < 8) {
                                 surfaceBias = (8 - depthFromSurface) * 0.08f; 
                             }
                             
                             if ((bufCave[idx3D] - surfaceBias) > 0.4f) {
                                block = 0;
                             }
                        }
                    } else if (wy <= m_settings.seaLevel) {
                        block = (biome == 3) ? 12 : 6;
                    }

                    if (lodScale == 1 && block == 0) {
                        int treeTypeHere = mapTree[idx2D];
                        int relY = wy - h;
                        
                        // FIX: Ensure trees don't spawn if the ground was eaten by a cave
                        // This prevents floating trees over invisible holes
                        if (treeTypeHere > 0 && relY > 0) {
                            // Map wy back to local buffer Y
                            int localGroundY = h - (int)worldYStart;
                            if (localGroundY >= 0 && localGroundY < P) {
                                int idxGround = x + (z*P) + (localGroundY*P*P);
                                // Check if the ground is gone (using same logic as above, but at ground level)
                                // Depth at ground level is 0, so bias is max (0.64).
                                // A cave value would need to be > 1.04 to eat the surface now. Unlikely.
                                // But good to be safe.
                                float biasAtSurface = 8.0f * 0.08f;
                                if ((bufCave[idxGround] - biasAtSurface) > 0.4f) {
                                    treeTypeHere = 0; // Ground is gone, no tree
                                }
                            }
                        }

                        if (treeTypeHere > 0) {
                            if ((treeTypeHere == 1 || treeTypeHere == 2) && relY > 0 && relY < 6) block = (treeTypeHere == 2) ? 15 : 13;
                            else if (treeTypeHere == 3 && relY > 0 && relY < 4) block = 14;
                        }
                        if (block == 0) {
                            for (int nz = -1; nz <= 1; nz++) {
                                for (int nx = -1; nx <= 1; nx++) {
                                    if (nx == 0 && nz == 0) continue;
                                    int ni = (x+nx) + (z+nz)*P;
                                    if (x+nx>=0 && x+nx<P && z+nz>=0 && z+nz<P) {
                                        int nTree = mapTree[ni];
                                        int nRelY = wy - mapHeight[ni];
                                        if (nTree == 1 && nRelY >= 4 && nRelY <= 6) block = 14;
                                        else if (nTree == 2 && nRelY >= 3 && nRelY <= 6) block = 16;
                                    }
                                }
                            }
                        }
                    }
                    voxels[idx3D] = block;
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
            if(ImGui::SliderInt("Max Height", &m_settings.maxHeight, 50, 2048)) changed = true; 
            if(ImGui::SliderInt("Sea Level", &m_settings.seaLevel, 0, 200)) changed = true;
        }
        if (ImGui::CollapsingHeader("Mega Peaks")) {
            if(ImGui::SliderFloat("Peak Amp", &m_settings.everestAmp, 0.0f, 4000.0f)) changed = true;
            if(ImGui::SliderFloat("Peak Rarity", &m_settings.everestFreq, 0.01f, 1.0f)) changed = true;
            if(ImGui::SliderFloat("Peak Threshold", &m_settings.everestThreshold, 0.1f, 0.95f)) changed = true;
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