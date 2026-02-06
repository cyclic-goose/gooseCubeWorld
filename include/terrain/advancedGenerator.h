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
// ================================================================================================
class AdvancedGenerator : public ITerrainGenerator {
public:
    // --------------------------------------------------------------------------------------------
    // GENERATION SETTINGS
    // --------------------------------------------------------------------------------------------
    struct GenSettings {
        int seed = 1804289383;              
        float coordinateScale = 0.073f;     
        
        // --- Base Terrain ---
        float minimumHeight = 20.0f;        
        float hillAmplitude = 60.0f;        
        float hillFrequency = 5.0f;         
        float mountainAmplitude = 25.0f;    
        float mountainFrequency = 1.0f;     
        
        // --- Mega Peaks ---
        float megaPeakRarity = 0.6f;        
        float megaPeakHeight = 1200.0f;     
        float megaPeakThreshold = 0.7f;     
        
        // --- Mega Craters ---
        float craterScale = 2.0f;           
        float craterDepth = 50.0f;          
        float craterTriggerThreshold = 0.9f;
        float craterRimWidth = 0.01f;       
        float craterFloorLift = 250.0f;     

        // --- World Limits ---
        int seaLevel = 30;                  
        int maxWorldHeight = 1024;          
        int bedrockDepth = 3;               
        int deepslateLevel = 10;            
        
        // --- Biome Settings ---
        float biomeMapScale = 0.1f;         
        
        // --- Vegetation ---
        int treeChanceForest = 65;          
        int treeChancePlains = 100;         
        int treeChanceDesert = 200;
        int maxTreeLOD = 4;                 // Max LOD level to generate standard vegetation
    };

    AdvancedGenerator() : m_settings(GenSettings()) { Init(); }
    AdvancedGenerator(int seed) { m_settings = GenSettings(); m_settings.seed = seed; Init(); }
    AdvancedGenerator(GenSettings settings) : m_settings(settings) { Init(); }

    void Init() override {
        auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
        auto fnHeightFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnHeightFractal->SetSource(fnPerlin);
        fnHeightFractal->SetOctaveCount(5);       
        fnHeightFractal->SetGain(0.5f);           
        fnHeightFractal->SetLacunarity(2.0f);     
        m_baseTerrainNoise = fnHeightFractal;

        auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
        auto fnMountainFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnMountainFractal->SetSource(fnSimplex);
        fnMountainFractal->SetOctaveCount(4);
        m_mountainNoise = fnMountainFractal;

        m_temperatureNoise = FastNoise::New<FastNoise::Perlin>();  
        m_moistureNoise    = FastNoise::New<FastNoise::Perlin>();  
        m_caveNoise        = FastNoise::New<FastNoise::Perlin>();  
        m_megaPeakNoise    = FastNoise::New<FastNoise::Simplex>(); 
        m_craterNoise      = FastNoise::New<FastNoise::Simplex>(); 
    }

    // --------------------------------------------------------------------------------------------
    // ASSET MANAGEMENT
    // --------------------------------------------------------------------------------------------
    std::vector<std::string> GetTexturePaths() const override {
        std::vector<std::string> texturePaths = {
            "resources/textures/minecraftDefaults/OldGrassTop.png",          // ID 1
            "resources/textures/minecraftDefaults/default_dirt.png",              // ID 2
            "resources/textures/minecraftDefaults/default_grass_side.png",  // ID 3
            "resources/textures/minecraftDefaults/default_stone.png",             // ID 4
            "resources/textures/minecraftDefaults/default_tree.png",       // ID 5
            "resources/textures/minecraftDefaults/default_dirt.png",              // ID 6 (Water Placeholder)
            "resources/textures/minecraftDefaults/snow1.png",              // ID 7
            "resources/textures/minecraftDefaults/default_ice.png",               // ID 8
            "resources/textures/minecraftDefaults/default_leaves.png",        // ID 9
            "resources/textures/minecraftDefaults/default_obsidian.png",          // ID 10
            "resources/textures/minecraftDefaults/default_pine_tree.png",      // ID 11
            "resources/textures/minecraftDefaults/default_pine_tree_top.png",  // ID 12
            "resources/textures/minecraftDefaults/default_mineral_gold.png"          // ID 13
        };
        return texturePaths;
    }

    inline int PseudoRandomHash(int x, int z, int seed) const {
        int h = seed + x * 374761393 + z * 668265263;
        h = (h ^ (h >> 13)) * 1274126177;
        return (h ^ (h >> 16));
    }
    
    // 3D Hash for leaf randomness
    inline int PseudoRandomHash3D(int x, int y, int z, int seed) const {
        int h = seed + x * 374761393 + y * 668265263 + z * 432352357;
        h = (h ^ (h >> 13)) * 1274126177;
        return (h ^ (h >> 16));
    }

    // Helper to get consistent random float 0..1 from coords
    inline float HashFloat(int x, int z, int seed) const {
        int val = PseudoRandomHash(x, z, seed);
        return (val & 0xFFFF) / 65535.0f;
    }

    int GetHeight(float x, float z) const {
        float normalizedX = x * m_settings.coordinateScale;
        float normalizedZ = z * m_settings.coordinateScale;

        float baseHeightVal = m_baseTerrainNoise->GenSingle2D(normalizedX * m_settings.hillFrequency, normalizedZ * m_settings.hillFrequency, m_settings.seed);
        float mountainVal   = m_mountainNoise->GenSingle2D(normalizedX * 0.5f * m_settings.mountainFrequency, normalizedZ * 0.5f * m_settings.mountainFrequency, m_settings.seed + 1);
        
        float megaPeakZone = m_megaPeakNoise->GenSingle2D(normalizedX * m_settings.megaPeakRarity * 0.1f, normalizedZ * m_settings.megaPeakRarity * 0.1f, m_settings.seed + 99);
        float megaPeakBoost = 0.0f;
        
        if (megaPeakZone > m_settings.megaPeakThreshold) {
            float factor = (megaPeakZone - m_settings.megaPeakThreshold) / (1.0f - m_settings.megaPeakThreshold);
            factor = factor * factor; 
            megaPeakBoost = factor * m_settings.megaPeakHeight;
        }

        float craterZone = m_craterNoise->GenSingle2D(normalizedX * m_settings.craterScale * 0.1f, normalizedZ * m_settings.craterScale * 0.1f, m_settings.seed + 55);
        float craterModifier = 0.0f;
        
        if (craterZone > m_settings.craterTriggerThreshold) {
            float factor = (craterZone - m_settings.craterTriggerThreshold) / (1.0f - m_settings.craterTriggerThreshold);
            float sineShape = std::sin(factor * 3.14159f); 
            float lift = sineShape * m_settings.craterFloorLift;             
            float rim  = sineShape * (m_settings.craterDepth * m_settings.craterRimWidth); 
            float dig  = (factor * factor) * m_settings.craterDepth;         
            craterModifier = lift + rim - dig;
        }

        float mountainFactor = std::abs(mountainVal); 
        mountainFactor = mountainFactor * mountainFactor * mountainFactor; 

        float finalHeight = m_settings.minimumHeight 
                          + (baseHeightVal * m_settings.hillAmplitude) 
                          + (mountainFactor * m_settings.mountainAmplitude) 
                          + megaPeakBoost 
                          + craterModifier;

        return (int)std::clamp(finalHeight, 0.0f, (float)m_settings.maxWorldHeight);
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        minH = 0;
        maxH = m_settings.maxWorldHeight;
    }

    // --------------------------------------------------------------------------------------------
    // GET BLOCK (Slow single access, use GenerateChunk for speed)
    // --------------------------------------------------------------------------------------------
    uint8_t GetBlock(float x, float y, float z, int lodScale) const override {
        // NOTE: This function is slow because we can't precompute trees for single block access.
        // It's mostly used for player collision/raycasting, not terrain gen.
        int integerX = (int)std::floor(x);
        int integerZ = (int)std::floor(z);
        int worldY   = (int)std::floor(y);

        int surfaceHeight = GetHeight(x, z);

        // Bedrock
        if (worldY <= m_settings.bedrockDepth) return 10; 

        // Biome Calculation
        float normalizedX = x * m_settings.coordinateScale;
        float normalizedZ = z * m_settings.coordinateScale;
        float tempVal = m_temperatureNoise->GenSingle2D(normalizedX * m_settings.biomeMapScale, normalizedZ * m_settings.biomeMapScale, m_settings.seed + 2);
        float moistVal = m_moistureNoise->GenSingle2D(normalizedX * m_settings.biomeMapScale, normalizedZ * m_settings.biomeMapScale, m_settings.seed + 3);
        
        float adjustedTemp = tempVal - (surfaceHeight - m_settings.seaLevel) * 0.005f;
        
        uint8_t biomeID = 0; // 0: Plains
        if (moistVal > 0.4f && adjustedTemp > 0.2f) biomeID = 1; // Mega Flora removed -> Forest
        else if (adjustedTemp > 0.4f && moistVal < -0.2f) biomeID = 2; // Desert
        else if (adjustedTemp < -0.3f || surfaceHeight > 220) biomeID = 3; // Snow
        else if (moistVal > 0.2f) biomeID = 1; // Forest

        // Terrain Fill (Air/Water)
        if (worldY > surfaceHeight) {
            if (worldY <= m_settings.seaLevel) return (biomeID == 3) ? 8 : 6;
            return 0; // Air
        }

        // Underground vs Surface Blocks
        int depth = surfaceHeight - worldY;
        uint8_t blockID = (worldY < m_settings.deepslateLevel) ? 13 : 4;
        if (depth < lodScale) {
            blockID = (biomeID == 2) ? 2 : (biomeID == 3 ? 7 : (biomeID == 4 ? 3 : 1)); 
        } else if (depth < (4 * lodScale)) {
            blockID = 2; 
        }
        if (worldY > 220) {
            if (depth < lodScale) blockID = 7; 
            else if (depth < (10 * lodScale)) blockID = 8; 
            else blockID = 4; 
        }
        float craterZone = m_craterNoise->GenSingle2D(normalizedX * m_settings.craterScale * 0.1f, normalizedZ * m_settings.craterScale * 0.1f, m_settings.seed + 55);
        if (craterZone > m_settings.craterTriggerThreshold && worldY <= surfaceHeight) {
             if (blockID != 0 && blockID != 6) {
                 float factor = (craterZone - m_settings.craterTriggerThreshold) / (1.0f - m_settings.craterTriggerThreshold);
                 if (factor > 0.2f) blockID = 10; else blockID = 10;
             }
        }
        
        // CAVES (Only at high detail)
        if (lodScale == 1 && worldY > m_settings.bedrockDepth) {
             float caveScale = 0.03f;
             float caveVal = m_caveNoise->GenSingle3D(x * caveScale, z * caveScale, y * caveScale, m_settings.seed);
             int depthFromSurface = surfaceHeight - worldY;
             float surfaceBias = 0.0f;
             if (depthFromSurface < 8) surfaceBias = (8 - depthFromSurface) * 0.08f; 
             if ((caveVal - surfaceBias) > 0.4f) return 0;
        }
        return blockID; 
    }

    // --------------------------------------------------------------------------------------------
    // GENERATE CHUNK (Optimized Batch)
    // --------------------------------------------------------------------------------------------
    void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int lodScale) override {
        static thread_local std::vector<float> bufferHeightMap;
        static thread_local std::vector<float> bufferMountain;
        static thread_local std::vector<float> bufferMegaPeak;
        static thread_local std::vector<float> bufferCrater; 
        static thread_local std::vector<float> bufferTemperature;
        static thread_local std::vector<float> bufferMoisture;
        static thread_local std::vector<float> bufferCave3D;
        static thread_local std::vector<int>   mapFinalHeight; 
        static thread_local std::vector<uint8_t> mapBiomeID; 
        static thread_local std::vector<uint8_t> mapTreeData;  

        const int PADDED_CHUNK_SIZE = CHUNK_SIZE_PADDED; 
        const int size2D = PADDED_CHUNK_SIZE * PADDED_CHUNK_SIZE;
        const int size3D = PADDED_CHUNK_SIZE * PADDED_CHUNK_SIZE * PADDED_CHUNK_SIZE;

        if (bufferHeightMap.size() != size2D) {
            bufferHeightMap.resize(size2D); bufferMountain.resize(size2D); bufferMegaPeak.resize(size2D); bufferCrater.resize(size2D);
            bufferTemperature.resize(size2D); bufferMoisture.resize(size2D);
            mapFinalHeight.resize(size2D); mapBiomeID.resize(size2D); mapTreeData.resize(size2D);
            bufferCave3D.resize(size3D);
        }

        float genScale = m_settings.coordinateScale;
        float worldStartX = (float)((cx * CHUNK_SIZE - 1) * lodScale);
        float worldStartZ = (float)((cz * CHUNK_SIZE - 1) * lodScale);
        float worldStartY = (float)((cy * CHUNK_SIZE - 1) * lodScale);
        float worldStep   = (float)lodScale;

        // Phase 1: Noise (Parallelized by library)
        m_baseTerrainNoise->GenUniformGrid2D(bufferHeightMap.data(), worldStartX * genScale * m_settings.hillFrequency, worldStartZ * genScale * m_settings.hillFrequency, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * m_settings.hillFrequency, worldStep * genScale * m_settings.hillFrequency, m_settings.seed);
        m_mountainNoise->GenUniformGrid2D(bufferMountain.data(), worldStartX * genScale * 0.5f * m_settings.mountainFrequency, worldStartZ * genScale * 0.5f * m_settings.mountainFrequency, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * 0.5f * m_settings.mountainFrequency, worldStep * genScale * 0.5f * m_settings.mountainFrequency, m_settings.seed + 1);
        m_megaPeakNoise->GenUniformGrid2D(bufferMegaPeak.data(), worldStartX * genScale * m_settings.megaPeakRarity * 0.1f, worldStartZ * genScale * m_settings.megaPeakRarity * 0.1f, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * m_settings.megaPeakRarity * 0.1f, worldStep * genScale * m_settings.megaPeakRarity * 0.1f, m_settings.seed + 99);
        m_craterNoise->GenUniformGrid2D(bufferCrater.data(), worldStartX * genScale * m_settings.craterScale * 0.1f, worldStartZ * genScale * m_settings.craterScale * 0.1f, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * m_settings.craterScale * 0.1f, worldStep * genScale * m_settings.craterScale * 0.1f, m_settings.seed + 55);
        m_temperatureNoise->GenUniformGrid2D(bufferTemperature.data(), worldStartX * genScale * m_settings.biomeMapScale, worldStartZ * genScale * m_settings.biomeMapScale, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * m_settings.biomeMapScale, worldStep * genScale * m_settings.biomeMapScale, m_settings.seed + 2);
        m_moistureNoise->GenUniformGrid2D(bufferMoisture.data(), worldStartX * genScale * m_settings.biomeMapScale, worldStartZ * genScale * m_settings.biomeMapScale, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, worldStep * genScale * m_settings.biomeMapScale, worldStep * genScale * m_settings.biomeMapScale, m_settings.seed + 3);

        // Phase 2: Process Heights/Biomes
        for (int i = 0; i < size2D; i++) {
            float megaVal = bufferMegaPeak[i];
            float megaBoost = 0.0f;
            if (megaVal > m_settings.megaPeakThreshold) {
                float factor = (megaVal - m_settings.megaPeakThreshold) / (1.0f - m_settings.megaPeakThreshold);
                megaBoost = (factor * factor) * m_settings.megaPeakHeight;
            }

            float craterVal = bufferCrater[i];
            float craterMod = 0.0f;
            if (craterVal > m_settings.craterTriggerThreshold) {
                float factor = (craterVal - m_settings.craterTriggerThreshold) / (1.0f - m_settings.craterTriggerThreshold);
                float shape = std::sin(factor * 3.14159f); 
                float lift = shape * m_settings.craterFloorLift;
                float rim = shape * (m_settings.craterDepth * m_settings.craterRimWidth);
                float dig = (factor * factor) * m_settings.craterDepth;
                craterMod = lift + rim - dig;
            }

            float mntPower = std::pow(std::abs(bufferMountain[i]), 3);
            float finalH = m_settings.minimumHeight + (bufferHeightMap[i] * m_settings.hillAmplitude) + (mntPower * m_settings.mountainAmplitude) + megaBoost + craterMod;
            mapFinalHeight[i] = (int)std::clamp(finalH, 0.0f, (float)m_settings.maxWorldHeight);

            float temp = bufferTemperature[i] - (mapFinalHeight[i] - m_settings.seaLevel) * 0.005f;
            float moist = bufferMoisture[i];

            uint8_t biome = 0; 
            if (moist > 0.4f && temp > 0.2f) biome = 1; // Mega Flora removed -> Forest
            else if (temp > 0.4f && moist < -0.2f) biome = 2; // Desert
            else if (temp < -0.3f || mapFinalHeight[i] > 220) biome = 3; // Snow
            else if (moist > 0.2f) biome = 1; // Forest
            mapBiomeID[i] = biome;

            mapTreeData[i] = 0;
            // Updated LOD check for standard trees
            if (lodScale <= m_settings.maxTreeLOD) {
                int absX = (int)worldStartX + (i % PADDED_CHUNK_SIZE) * lodScale;
                int absZ = (int)worldStartZ + (i / PADDED_CHUNK_SIZE) * lodScale;
                
                int chance = 0; uint8_t treeType = 0;
                if (biome == 1)      { chance = m_settings.treeChanceForest; treeType = 1; }
                else if (biome == 0) { chance = m_settings.treeChancePlains; treeType = 2; }
                else if (biome == 3) { chance = 60; treeType = 2; }

                if (mapFinalHeight[i] > m_settings.seaLevel && mapFinalHeight[i] < 200 && chance > 0) {
                    // Reduce chance slightly if LOD is high to prevent dense noise
                    int effectiveChance = chance * lodScale;
                    if ((std::abs(PseudoRandomHash(absX, absZ, m_settings.seed)) % effectiveChance) == 0) {
                        mapTreeData[i] = treeType;
                    }
                }
            }
        }

        // Phase 3: Caves
        if (lodScale == 1) {
            float caveScale = 0.03f;
            m_caveNoise->GenUniformGrid3D(bufferCave3D.data(), worldStartX * caveScale, worldStartZ * caveScale, worldStartY * caveScale, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, PADDED_CHUNK_SIZE, caveScale, caveScale, caveScale, m_settings.seed);
        }

        // Phase 4: Voxel Loop (TERRAIN ONLY)
        uint8_t* voxels = chunk->voxels;
        //memset(voxels, 0, size3D * sizeof(uint8_t)); // Only needed if chunk wasn't cleared

        for (int y = 0; y < PADDED_CHUNK_SIZE; y++) {
            int currentWorldY = (int)worldStartY + (y * lodScale);
            
            for (int z = 0; z < PADDED_CHUNK_SIZE; z++) {
                for (int x = 0; x < PADDED_CHUNK_SIZE; x++) {
                    int idx2D = x + (z * PADDED_CHUNK_SIZE);
                    int idx3D = x + (z * PADDED_CHUNK_SIZE) + (y * PADDED_CHUNK_SIZE * PADDED_CHUNK_SIZE); 
                    
                    int h = mapFinalHeight[idx2D];
                    uint8_t biome = mapBiomeID[idx2D];
                    uint8_t block = 0;

                    if (currentWorldY <= m_settings.bedrockDepth && currentWorldY >= 0) {
                        block = 10;
                    } else if (currentWorldY <= h) {
                        block = (currentWorldY < m_settings.deepslateLevel) ? 13 : 4;
                        int depth = h - currentWorldY;
                        
                        // Topsoil
                        if (depth < lodScale) {
                            block = (biome == 2) ? 2 : (biome == 3 ? 7 : (biome == 4 ? 3 : 1));
                        } else if (depth < (4 * lodScale)) {
                            block = 2; 
                        }
                        
                        // Caps
                        if (currentWorldY > 220) {
                            if (depth < lodScale) block = 7;
                            else if (depth < (10 * lodScale)) block = 8;
                            else block = 4;
                        }

                        // Volcanic
                        float craterVal = bufferCrater[idx2D];
                        if (craterVal > m_settings.craterTriggerThreshold && currentWorldY > m_settings.bedrockDepth) {
                             if (block != 0 && block != 6) {
                                 float factor = (craterVal - m_settings.craterTriggerThreshold) / (1.0f - m_settings.craterTriggerThreshold);
                                 if (factor > 0.2f) block = 10; 
                                 else block = 10; 
                             }
                        }

                        // Caves
                        if (lodScale == 1 && currentWorldY > m_settings.bedrockDepth) {
                             int depthFromSurface = h - currentWorldY;
                             float surfaceBias = 0.0f;
                             if (depthFromSurface < 8) surfaceBias = (8 - depthFromSurface) * 0.08f; 
                             if ((bufferCave3D[idx3D] - surfaceBias) > 0.4f) block = 0;
                        }
                    } else if (currentWorldY <= m_settings.seaLevel) {
                        block = (biome == 3) ? 8 : 6;
                    }
                    
                    voxels[idx3D] = block;
                }
            }
        }

        // Phase 5: Vegetation "PUSH" Optimization
        // Instead of scanning neighbors for every air block (Pull), we iterate known trees and write them (Push).
        // This dramatically reduces checks in empty air.
        if (lodScale <= m_settings.maxTreeLOD) {
            for (int z = 0; z < PADDED_CHUNK_SIZE; z++) {
                for (int x = 0; x < PADDED_CHUNK_SIZE; x++) {
                    int idx2D = x + (z * PADDED_CHUNK_SIZE);
                    int treeType = mapTreeData[idx2D];
                    
                    if (treeType > 0) {
                        int rootH = mapFinalHeight[idx2D];
                        
                        // Convert world height to local voxel index Y
                        // We need the tree to start at 'rootH + 1'
                        // localY = (worldY - startY) / lod
                        
                        int localRootY = (rootH - (int)worldStartY) / lodScale;
                        
                        // If tree root is completely above chunk, skip
                        if (localRootY >= PADDED_CHUNK_SIZE) continue;
                        
                        int absX = (int)worldStartX + (x * lodScale);
                        int absZ = (int)worldStartZ + (z * lodScale);
                        int treeSeed = PseudoRandomHash(absX, absZ, m_settings.seed);
                        int treeHeight = 5 + (std::abs(treeSeed) % 4); 
                        if (treeType == 1) treeHeight += 2; 

                        // Pre-calculate canopy bounds
                        int maxH = treeHeight;
                        int leavesStart = treeHeight - 4; 
                        
                        // Write Tree blocks directly into voxels array
                        // We loop relative to the tree root
                        
                        // 1. TRUNK
                        for (int th = 1; th < treeHeight - 1; th++) {
                            int vy = localRootY + th;
                            if (vy >= 0 && vy < PADDED_CHUNK_SIZE) {
                                int vIdx = x + z * PADDED_CHUNK_SIZE + vy * PADDED_CHUNK_SIZE * PADDED_CHUNK_SIZE;
                                // Overwrite only if air or water (optional check)
                                if (voxels[vIdx] == 0 || voxels[vIdx] == 6) {
                                    voxels[vIdx] = (treeType == 1) ? 11 : 5;
                                }
                            }
                        }

                        // 2. CANOPY (Iterate local volume)
                        int rad = 3; // Max radius
                        for (int ly = leavesStart; ly <= maxH; ly++) {
                             int vy = localRootY + ly;
                             if (vy < 0 || vy >= PADDED_CHUNK_SIZE) continue;
                             
                             // Calculate target radius for this height layer
                             float progress = (float)ly / (float)treeHeight;
                             float maxRad = (treeType == 1) ? 3.5f : 2.5f;
                             maxRad += (treeSeed % 100) / 100.0f;
                             float radiusShape = 1.0f - std::pow(std::abs(progress - 0.7f) * 2.5f, 2.0f);
                             float currentRadius = maxRad * std::max(0.0f, radiusShape);
                             float currentRadSq = currentRadius * currentRadius;

                             // Scan local area for leaves
                             for (int lz = -rad; lz <= rad; lz++) {
                                 for (int lx = -rad; lx <= rad; lx++) {
                                     int vx = x + lx;
                                     int vz = z + lz;
                                     
                                     // Check Bounds
                                     if (vx >= 0 && vx < PADDED_CHUNK_SIZE && vz >= 0 && vz < PADDED_CHUNK_SIZE) {
                                         float dSq = (float)(lx*lx + lz*lz) * (lodScale * lodScale); // Scale distance check? No, voxel distance.
                                         // If lodScale > 1, the tree looks "chunky" which is expected. 
                                         // Distance is in "voxels" here to match shape.
                                         
                                         if ((float)(lx*lx + lz*lz) < currentRadSq) {
                                              // Fluff/Edge noise
                                              bool isEdge = (float)(lx*lx + lz*lz) > (currentRadSq * 0.6f);
                                              int absVX = (int)worldStartX + vx * lodScale;
                                              int absVZ = (int)worldStartZ + vz * lodScale;
                                              int absVY = (int)worldStartY + vy * lodScale;

                                              if (!isEdge || (PseudoRandomHash3D(absVX, absVY, absVZ, m_settings.seed) % 100 < 70)) {
                                                  int vIdx = vx + vz * PADDED_CHUNK_SIZE + vy * PADDED_CHUNK_SIZE * PADDED_CHUNK_SIZE;
                                                  if (voxels[vIdx] == 0) { // Only place leaves in air
                                                      voxels[vIdx] = 9;
                                                  }
                                              }
                                         }
                                     }
                                 }
                             }
                        }
                    }
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
            if(ImGui::SliderFloat("Scale", &m_settings.coordinateScale, 0.001f, 0.1f)) changed = true;
            if(ImGui::SliderInt("Max Height", &m_settings.maxWorldHeight, 50, 2048)) changed = true; 
            if(ImGui::SliderInt("Sea Level", &m_settings.seaLevel, 0, 200)) changed = true;
        }
        
        if (ImGui::CollapsingHeader("Features & Biomes")) {
             if(ImGui::SliderFloat("Hill Freq", &m_settings.hillFrequency, 1.0f, 10.0f)) changed = true;
             if(ImGui::SliderFloat("Hill Amp", &m_settings.hillAmplitude, 10.0f, 200.0f)) changed = true;
             if(ImGui::SliderFloat("Mnt Freq", &m_settings.mountainFrequency, 0.1f, 5.0f)) changed = true;
             if(ImGui::SliderFloat("Mnt Amp", &m_settings.mountainAmplitude, 10.0f, 200.0f)) changed = true;
             if(ImGui::SliderFloat("Biome Size", &m_settings.biomeMapScale, 0.01f, 0.5f)) changed = true;
        }

        if (ImGui::CollapsingHeader("Vegetation (Standard)")) {
             if(ImGui::SliderInt("Max LOD for Trees", &m_settings.maxTreeLOD, 1, 8)) changed = true;
             if(ImGui::SliderInt("Forest Density (1/N)", &m_settings.treeChanceForest, 2, 100)) changed = true;
             if(ImGui::SliderInt("Plains Density (1/N)", &m_settings.treeChancePlains, 10, 500)) changed = true;
        }
        
        if (ImGui::CollapsingHeader("Mega Craters")) {
            if(ImGui::SliderFloat("Crater Scale (Lower=Big)", &m_settings.craterScale, 0.01f, 2.0f)) changed = true;
            float rarityChance = 1.0f - m_settings.craterTriggerThreshold;
            if(ImGui::SliderFloat("Crater Chance", &rarityChance, 0.01f, 0.9f)) {
                m_settings.craterTriggerThreshold = 1.0f - rarityChance;
                changed = true;
            }
            if(ImGui::SliderFloat("Crater Depth", &m_settings.craterDepth, 1.0f, 500.0f)) changed = true;
            if(ImGui::SliderFloat("Crater Rim Height", &m_settings.craterRimWidth, 0.0f, 1.0f)) changed = true;
            if(ImGui::SliderFloat("Crater Lift (Volcano)", &m_settings.craterFloorLift, 0.0f, 2000.0f)) changed = true;
        }
        
        if (ImGui::CollapsingHeader("Mega Peaks")) {
            if(ImGui::SliderFloat("Peak Amp", &m_settings.megaPeakHeight, 0.0f, 4000.0f)) changed = true;
            if(ImGui::SliderFloat("Peak Rarity", &m_settings.megaPeakRarity, 0.01f, 1.0f)) changed = true;
            if(ImGui::SliderFloat("Peak Thresh", &m_settings.megaPeakThreshold, 0.1f, 0.95f)) changed = true;
        }
        
        if (changed) { m_dirty = true; Init(); }
#endif
    }

private:
    GenSettings m_settings;
    FastNoise::SmartNode<> m_baseTerrainNoise;
    FastNoise::SmartNode<> m_mountainNoise;
    FastNoise::SmartNode<> m_temperatureNoise;
    FastNoise::SmartNode<> m_moistureNoise;
    FastNoise::SmartNode<> m_caveNoise;
    FastNoise::SmartNode<> m_megaPeakNoise; 
    FastNoise::SmartNode<> m_craterNoise;
    FastNoise::SmartNode<> m_treeDensityNoise;
};