#pragma once
#include "terrain_system.h"
#include "chunk.h"
#include <FastNoise/FastNoise.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

#ifndef IMGUI_VERSION
// #include "imgui.h" 
#endif

// ================================================================================================
// STANDARD GENERATOR OPTIMIZED (Header Only)
// ================================================================================================
class StandardGenerator2 : public ITerrainGenerator {
public:
    struct TerrainSettings {
        int seed = 1337;
        float scale = 0.08f;          
        float hillAmplitude = 100.0f;  
        float hillFrequency = 4.0f;   
        float mountainAmplitude = 500.0f; 
        float mountainFrequency = 0.26f; 
        int seaLevel = 90;            
        float caveThreshold = 0.5f; 
    };

    StandardGenerator2() : m_settings(TerrainSettings()) { Init(); }
    StandardGenerator2(int seed) { m_settings = TerrainSettings(); m_settings.seed = seed; Init(); }
    StandardGenerator2(TerrainSettings settings) : m_settings(settings) { Init(); }

    void Init() override {
        // 1. Base Hill Noise
        auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
        auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnFractal->SetSource(fnPerlin);
        fnFractal->SetOctaveCount(4);
        m_baseNoise = fnFractal;

        // 2. Mountain Noise
        auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
        auto fnFractal2 = FastNoise::New<FastNoise::FractalFBm>();
        fnFractal2->SetSource(fnSimplex);
        fnFractal2->SetOctaveCount(3);
        m_mountainNoise = fnFractal2;

        // 3. Cave Noise
        m_caveNoise = FastNoise::New<FastNoise::Perlin>();
    }

    std::vector<std::string> GetTexturePaths() const override {
        return {
            "resources/textures/dirt1.jpg",    
            "resources/textures/dirt1.jpg",     
            "resources/textures/dirt1.jpg",    
            "resources/textures/dirt1.jpg"   
        };
    }

    // --------------------------------------------------------------------------------------------
    // LEGACY SINGLE BLOCK API (Fallback / Physics)
    // --------------------------------------------------------------------------------------------
    int GetHeight(float x, float z) const {
        float nx = x * m_settings.scale;
        float nz = z * m_settings.scale;
        
        float baseVal = m_baseNoise->GenSingle2D(nx * m_settings.hillFrequency, nz * m_settings.hillFrequency, m_settings.seed);
        float hillHeight = baseVal * m_settings.hillAmplitude;
        
        float mountainVal = m_mountainNoise->GenSingle2D(nx * m_settings.mountainFrequency, nz * m_settings.mountainFrequency, m_settings.seed + 1);
        mountainVal = std::abs(mountainVal); 
        mountainVal = std::pow(mountainVal, 2.0f); 
        float mountainHeight = mountainVal * m_settings.mountainAmplitude;
        
        return m_settings.seaLevel + (int)std::floor(hillHeight + mountainHeight);
    }

    uint8_t GetBlock(float x, float y, float z, int lodScale) const override {
        int heightAtXZ = GetHeight(x, z);
        int wy = (int)y;
        
        // Cave Check
        if (wy < heightAtXZ && lodScale == 1) { 
            // Matches .cpp: y uses 0.04 (2x stretch), others 0.02
            float val = m_caveNoise->GenSingle3D(x * 0.02f, y * 0.04f, z * 0.02f, m_settings.seed);
            if (val > m_settings.caveThreshold) return 0; // Air
        }

        if (wy > heightAtXZ) return 0; 

        if (wy == heightAtXZ) {
            if (wy > 180) return 4; 
            return 1; // Grass
        } 
        else if (wy > heightAtXZ - (4 * lodScale)) {
            return 2; // Dirt
        }
        
        if (wy == 0) return 4; // Bedrock

        return 3; // Stone
    }

    // --------------------------------------------------------------------------------------------
    // OPTIMIZED BATCHED GENERATION
    // --------------------------------------------------------------------------------------------
    void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int lodScale) override {
        static thread_local std::vector<float> bufBase;
        static thread_local std::vector<float> bufMount;
        static thread_local std::vector<int>   heightMap;
        static thread_local std::vector<float> bufCaves;

        int size2D = CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED;
        int size3D = CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED;

        if (bufBase.size() != size2D) {
            bufBase.resize(size2D);
            bufMount.resize(size2D);
            heightMap.resize(size2D);
            bufCaves.resize(size3D);
        }

        // -----------------------------------------------------------------------
        // COORDINATE MATH FIX
        // -----------------------------------------------------------------------
        // startX is in "Chunk Grid Units" (indices). 
        // 1 Index Unit = 1 * lodScale in World Space.
        int32_t startX = (cx * CHUNK_SIZE) - 1;
        int32_t startY = (cy * CHUNK_SIZE) - 1;
        int32_t startZ = (cz * CHUNK_SIZE) - 1;

        // -----------------------------------------------------------------------
        // STEP 1: GENERATE HEIGHTMAP (2D Batch)
        // -----------------------------------------------------------------------
        
        float hillFreq = m_settings.hillFrequency * m_settings.scale;
        float hillStep = hillFreq * (float)lodScale;
        
        // FIX: Use hillStep for offset, not hillFreq. 
        // Logic: WorldPos = startX * lodScale. 
        // NoisePos = WorldPos * hillFreq = startX * lodScale * hillFreq = startX * hillStep.
        float hillOffX = (float)startX * hillStep; 
        float hillOffZ = (float)startZ * hillStep;

        m_baseNoise->GenUniformGrid2D(
            bufBase.data(),
            hillOffX, hillOffZ,
            CHUNK_SIZE_PADDED, CHUNK_SIZE_PADDED,
            hillStep, hillStep, 
            m_settings.seed
        );

        float mountFreq = m_settings.mountainFrequency * m_settings.scale;
        float mountStep = mountFreq * (float)lodScale;
        float mountOffX = (float)startX * mountStep;
        float mountOffZ = (float)startZ * mountStep;

        m_mountainNoise->GenUniformGrid2D(
            bufMount.data(),
            mountOffX, mountOffZ,
            CHUNK_SIZE_PADDED, CHUNK_SIZE_PADDED,
            mountStep, mountStep, 
            m_settings.seed + 1
        );

        int seaLevel = m_settings.seaLevel;
        float hillAmp = m_settings.hillAmplitude;
        float mountAmp = m_settings.mountainAmplitude;
        
        for(size_t i = 0; i < heightMap.size(); i++) {
            float baseVal = bufBase[i];
            float mountVal = bufMount[i];
            
            mountVal = std::abs(mountVal);
            mountVal = mountVal * mountVal;

            float totalH = (baseVal * hillAmp) + (mountVal * mountAmp);
            heightMap[i] = seaLevel + (int)std::floor(totalH);
        }

        // -----------------------------------------------------------------------
        // STEP 2: GENERATE CAVES (3D Batch)
        // -----------------------------------------------------------------------
        bool doCaves = (lodScale == 1); 
        if (doCaves) {
            float caveScaleX = 0.02f;
            float caveScaleY = 0.04f; // Y is stretched 2x (0.04 vs 0.02)
            float caveScaleZ = 0.02f;

            float stepX = caveScaleX * (float)lodScale;
            float stepY = caveScaleY * (float)lodScale;
            float stepZ = caveScaleZ * (float)lodScale;

            // FIX: Offset must also scale by step/lod relation
            float offX = (float)startX * stepX;
            float offY = (float)startY * stepY;
            float offZ = (float)startZ * stepZ;

            m_caveNoise->GenUniformGrid3D(
                bufCaves.data(),
                offX, offY, offZ,
                CHUNK_SIZE_PADDED, CHUNK_SIZE_PADDED, CHUNK_SIZE_PADDED,
                stepX, stepY, stepZ, 
                m_settings.seed
            );
        }

        // -----------------------------------------------------------------------
        // STEP 3: FILL CHUNK
        // -----------------------------------------------------------------------
        uint8_t* voxels = chunk->voxels;
        float caveThresh = m_settings.caveThreshold;
        
        int worldYBase = (cy * CHUNK_SIZE * lodScale) - (1 * lodScale);

        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                
                // FastNoise GenUniformGrid2D is X-Major (x + width*y)
                // Our inputs were (OffX, OffZ). So X is inner, Z is outer.
                // Index = x + (CHUNK_SIZE_PADDED * z)
                int idxHeight = x + (CHUNK_SIZE_PADDED * z);
                int height = heightMap[idxHeight];

                for (int y = 0; y < CHUNK_SIZE_PADDED; y++) {
                    
                    // Chunk storage is (x*P*P) + (z*P) + y (matches Chunk.h)
                    int idxVoxel = (x * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED) + (z * CHUNK_SIZE_PADDED) + y;
                    int wy = worldYBase + (y * lodScale);
                    uint8_t blockID = 0;

                    if (wy > height) {
                        blockID = 0; // Air
                    } else {
                        blockID = 3; // Stone default

                        if (wy == height) {
                            blockID = (wy > 180) ? 4 : 1; 
                        } else if (wy > height - (4 * lodScale)) {
                            blockID = 2; // Dirt
                        } else if (wy == 0) {
                            blockID = 4; // Bedrock
                        }

                        if (doCaves) {
                            // FastNoise 3D is X (fast), Y (med), Z (slow)
                            // Index = x + (y * SizeX) + (z * SizeX * SizeY)
                            int idxCave = x + (y * CHUNK_SIZE_PADDED) + (z * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED);
                            
                            if (bufCaves[idxCave] > caveThresh) {
                                blockID = 0; 
                            }
                        }
                    }
                    // Write directly to linear array
                    voxels[idxVoxel] = blockID;
                }
            }
        }
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        const int SIZE = CHUNK_SIZE * scale;
        int worldX = cx * SIZE;
        int worldZ = cz * SIZE;

        int h1 = GetHeight((float)worldX, (float)worldZ);
        int h2 = GetHeight((float)(worldX + SIZE), (float)worldZ);
        int h3 = GetHeight((float)worldX, (float)(worldZ + SIZE));
        int h4 = GetHeight((float)(worldX + SIZE), (float)(worldZ + SIZE));
        int h5 = GetHeight((float)(worldX + SIZE/2), (float)(worldZ + SIZE/2));

        int min = std::min({h1, h2, h3, h4, h5});
        int max = std::max({h1, h2, h3, h4, h5});

        minH = min - (16 * scale); 
        maxH = max + (4 * scale);
    }

    void OnImGui() override {
#ifdef IMGUI_VERSION
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Standard Noise Gen");
        bool changed = false;
        if(ImGui::DragInt("Seed", &m_settings.seed)) changed = true;
        if(ImGui::SliderFloat("Scale", &m_settings.scale, 0.001f, 0.1f)) changed = true;
        if(ImGui::DragFloat("Hill Amp", &m_settings.hillAmplitude)) changed = true;
        if(ImGui::DragFloat("Mount Amp", &m_settings.mountainAmplitude)) changed = true;
        if (changed) { m_dirty = true; Init(); }
#endif
    }

private:
    TerrainSettings m_settings;
    FastNoise::SmartNode<> m_baseNoise;
    FastNoise::SmartNode<> m_mountainNoise;
    FastNoise::SmartNode<> m_caveNoise;
};