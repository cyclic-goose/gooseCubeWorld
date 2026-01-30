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

        // World Coordinates
        int32_t startX = (cx * CHUNK_SIZE) - 1;
        int32_t startY = (cy * CHUNK_SIZE) - 1;
        int32_t startZ = (cz * CHUNK_SIZE) - 1;

        // -----------------------------------------------------------------------
        // STEP 1: GENERATE HEIGHTMAP
        // -----------------------------------------------------------------------
        float hillFreq = m_settings.hillFrequency * m_settings.scale;
        float hillStep = hillFreq * (float)lodScale;
        float hillOffX = (float)startX * hillStep;
        float hillOffZ = (float)startZ * hillStep;

        // Note: FastNoise UniformGrid2D output is X-Major (Index = x + y * width)
        // Since we pass (width, width), our buffer is buf[x + z * P]
        m_baseNoise->GenUniformGrid2D(bufBase.data(), hillOffX, hillOffZ, CHUNK_SIZE_PADDED, CHUNK_SIZE_PADDED, hillStep, hillStep, m_settings.seed);

        float mountFreq = m_settings.mountainFrequency * m_settings.scale;
        float mountStep = mountFreq * (float)lodScale;
        float mountOffX = (float)startX * mountStep;
        float mountOffZ = (float)startZ * mountStep;

        m_mountainNoise->GenUniformGrid2D(bufMount.data(), mountOffX, mountOffZ, CHUNK_SIZE_PADDED, CHUNK_SIZE_PADDED, mountStep, mountStep, m_settings.seed + 1);

        int seaLevel = m_settings.seaLevel;
        float hillAmp = m_settings.hillAmplitude;
        float mountAmp = m_settings.mountainAmplitude;

        for(size_t i = 0; i < heightMap.size(); i++) {
            float baseVal = bufBase[i];
            float mountVal = std::abs(bufMount[i]);
            mountVal *= mountVal;
            heightMap[i] = seaLevel + (int)std::floor((baseVal * hillAmp) + (mountVal * mountAmp));
        }

        // -----------------------------------------------------------------------
        // STEP 2: GENERATE CAVES
        // -----------------------------------------------------------------------
        bool doCaves = (lodScale == 1); 
        if (doCaves) {
            float cX = 0.02f, cY = 0.04f, cZ = 0.02f;
            float sX = cX * lodScale, sY = cY * lodScale, sZ = cZ * lodScale;
            float oX = startX * sX, oY = startY * sY, oZ = startZ * sZ;

            // FastNoise UniformGrid3D is X-Fastest: Index = x + (y * sizeX) + (z * sizeX * sizeY)
            // Wait, Standard FastNoise 3D order is usually X, Y, Z.
            // Check signature: (out, xOff, yOff, zOff, xSize, ySize, zSize...)
            // Output index: x + xSize * y + xSize * ySize * z
            m_caveNoise->GenUniformGrid3D(bufCaves.data(), oX, oY, oZ, CHUNK_SIZE_PADDED, CHUNK_SIZE_PADDED, CHUNK_SIZE_PADDED, sX, sY, sZ, m_settings.seed);
        }

        // -----------------------------------------------------------------------
        // STEP 3: FILL CHUNK
        // -----------------------------------------------------------------------
        uint8_t* voxels = chunk->voxels;
        float caveThresh = m_settings.caveThreshold;
        int worldYBase = (cy * CHUNK_SIZE * lodScale) - (1 * lodScale);
        
        // Pointers for stride optimization
        int strideY = CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED; // New Chunk Layout Y-stride
        int strideZ = CHUNK_SIZE_PADDED;                     // New Chunk Layout Z-stride

        for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
            for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
                
                // Heightmap Index: X is fast, Z is slow (matches FastNoise 2D)
                int idxHeight = x + (z * CHUNK_SIZE_PADDED);
                int height = heightMap[idxHeight];

                // Chunk Index Base for this Column (x, z)
                // New Chunk Layout: x + z*P + y*P*P
                int colBaseIdx = x + (z * strideZ);

                for (int y = 0; y < CHUNK_SIZE_PADDED; y++) {
                    int wy = worldYBase + (y * lodScale);
                    int idxVoxel = colBaseIdx + (y * strideY);
                    
                    uint8_t blockID = 0;

                    if (wy <= height) {
                        blockID = 3; // Stone
                        if (wy == height) blockID = (wy > 180) ? 4 : 1; // Grass/Snow
                        else if (wy > height - (4 * lodScale)) blockID = 2; // Dirt
                        else if (wy == 0) blockID = 4; // Bedrock

                        if (doCaves) {
                            // Cave Index: x + (y * P) + (z * P * P) ??
                            // NO. FastNoise GenUniformGrid3D produces: x + (y * P) + (z * P * P)
                            // We must match that exactly.
                            int idxCave = x + (y * CHUNK_SIZE_PADDED) + (z * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED);
                            if (bufCaves[idxCave] > caveThresh) blockID = 0;
                        }
                    }
                    
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