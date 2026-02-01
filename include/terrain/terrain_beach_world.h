#pragma once
#include "terrain_system.h"
#include "chunk.h"
#include <FastNoise/FastNoise.h>
#include <vector>
#include <cmath>
#include <algorithm>

// ================================================================================================
// BEACH WORLD GENERATOR
// Combines 3D Volumetric Noise (Overhangs/Coves) with Biome logic (Sand/Water/Vegetation)
// ================================================================================================
class BeachGenerator : public ITerrainGenerator {
public:
    struct BeachSettings {
        int seed = 999;
        
        // 3D Noise Shape (The Coves & Cliffs)
        float noiseScale = 0.05f;      // Lower = bigger massive coves
        float yStretch = 1.5f;         // Stretch Y to make cliffs vertical
        int octaves = 4;
        float gain = 0.5f;
        float lacunarity = 2.0f;
        
        // Density / Physics
        float surfaceThreshold = 0.0f; // > This is solid
        float floorLevel = 10.0f;      // Deep ocean floor
        float solidFalloff = 80.0f;    // How fast density fades as we go up (controls cliff height)
        
        // Beach & Water
        int seaLevel = 45;             // Water height
        int sandHeight = 6;            // How high above water does sand go?
        
        // Biome / Vegetation
        float vegetationChance = 0.05f; // Chance for trees on grass
        bool enableCaves = true;
    };

    BeachGenerator() : m_settings(BeachSettings()) { Init(); }
    BeachGenerator(int seed) { m_settings = BeachSettings(); m_settings.seed = seed; Init(); }

    void Init() override {
        // 1. The 3D Shape Noise (Simplex for organic cliffs)
        auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
        auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnFractal->SetSource(fnSimplex);
        fnFractal->SetOctaveCount(m_settings.octaves);
        fnFractal->SetGain(m_settings.gain);
        fnFractal->SetLacunarity(m_settings.lacunarity);
        m_shapeNoise = fnFractal;

        // 2. Optional: A warper or secondary noise for variation could go here
    }

    std::vector<std::string> GetTexturePaths() const override {
        // Mapping typical to standard engine slots
        // 1=Grass, 2=Dirt, 3=Stone, 4=Snow, 5=Sand, 6=Water
        return {
            "resources/textures/grass.jpg",  // 1
            "resources/textures/dirt.jpg",   // 2
            "resources/textures/stone.jpg",  // 3
            "resources/textures/snow.jpg",   // 4
            "resources/textures/sand.jpg",   // 5
            "resources/textures/water.jpg"   // 6
        };
    }

    // --------------------------------------------------------------------------------------------
    // UTILS
    // --------------------------------------------------------------------------------------------
    
    // Calculates density. Positive = Solid, Negative = Air
    inline float GetDensity(float x, float y, float z) const {
        // Base 3D Noise
        float noiseVal = m_shapeNoise->GenSingle3D(
            x * m_settings.noiseScale, 
            y * m_settings.noiseScale * m_settings.yStretch, 
            z * m_settings.noiseScale, 
            m_settings.seed
        );

        // Gradient: Force solid at bottom, air at top
        // (y - seaLevel) makes 0 at sea level.
        float heightBias = -((y - (float)m_settings.seaLevel) / m_settings.solidFalloff);
        
        // Add a hard floor bias to ensure we don't fall out of the world
        if (y < m_settings.floorLevel) heightBias += 10.0f;

        return noiseVal + heightBias;
    }

    // --------------------------------------------------------------------------------------------
    // CORE GENERATION
    // --------------------------------------------------------------------------------------------

    void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int lodScale) override {
        static thread_local std::vector<float> bufNoise;

        int P = CHUNK_SIZE_PADDED;
        int sizeX = P;
        int sizeY = P + 1; // +1 to peek density above for surface detection
        int sizeZ = P;
        int totalSize = sizeX * sizeY * sizeZ;

        if (bufNoise.size() != totalSize) bufNoise.resize(totalSize);

        // 1. Setup Coordinates
        float scale = m_settings.noiseScale;
        float yStretch = m_settings.yStretch;
        
        float startX = (cx * CHUNK_SIZE - 1) * lodScale * scale;
        float startY = ((cy * CHUNK_SIZE * lodScale) - (1 * lodScale)) * scale * yStretch;
        float startZ = (cz * CHUNK_SIZE - 1) * lodScale * scale;

        float stepX = lodScale * scale;
        float stepY = lodScale * scale * yStretch;
        float stepZ = lodScale * scale;

        // 2. Generate Batch 3D Noise
        m_shapeNoise->GenUniformGrid3D(bufNoise.data(), 
            startX, startY, startZ, 
            sizeX, sizeY, sizeZ, 
            stepX, stepY, stepZ, 
            m_settings.seed
        );

        // 3. Voxel Filling
        uint8_t* voxels = chunk->voxels;
        int worldYBase = (cy * CHUNK_SIZE * lodScale) - (1 * lodScale);
        
        float seaLevel = (float)m_settings.seaLevel;
        float sandCap = seaLevel + m_settings.sandHeight;
        float solidFalloff = m_settings.solidFalloff;
        float thresh = m_settings.surfaceThreshold;

        for (int z = 0; z < P; z++) {
            for (int x = 0; x < P; x++) {
                
                // Indexing for Noise Buffer (X -> Y -> Z)
                int noiseColBase = x + (z * sizeX * sizeY);
                // Indexing for Chunk Voxel Array (X -> Z -> Y typically, or Z->X->Y)
                // Assuming standard: x + (z * P) + (y * P * P)
                int voxelColBase = x + (z * P);
                int voxelStrideY = P * P;

                for (int y = 0; y < P; y++) {
                    int wy = worldYBase + (y * lodScale);
                    int idxVoxel = voxelColBase + (y * voxelStrideY);
                    
                    // -- Calculate Density --
                    int bufIdx = noiseColBase + (y * sizeX);
                    float rawNoise = bufNoise[bufIdx];
                    
                    // Apply Gradient
                    float heightBias = -((wy - seaLevel) / solidFalloff);
                    if (wy < m_settings.floorLevel) heightBias += 2.0f; // Hard floor
                    
                    float density = rawNoise + heightBias;

                    // -- Block Logic --
                    if (density > thresh) {
                        // IT IS SOLID
                        
                        // Look at block above to see if we are "Surface"
                        int bufIdxAbove = noiseColBase + ((y + 1) * sizeX);
                        float noiseAbove = bufNoise[bufIdxAbove];
                        float biasAbove = -(((wy + lodScale) - seaLevel) / solidFalloff);
                        float densityAbove = noiseAbove + biasAbove;

                        bool isSurface = (densityAbove <= thresh);

                        if (isSurface) {
                            if (wy <= sandCap && wy >= seaLevel - 5) {
                                voxels[idxVoxel] = 5; // Sand (Beach)
                            } else if (wy < seaLevel - 5) {
                                voxels[idxVoxel] = 2; // Dirt/Gravel underwater
                            } else {
                                voxels[idxVoxel] = 1; // Grass (Cliff top)
                            }
                        } else {
                            // Subsurface
                            if (wy <= sandCap && wy >= seaLevel - 2) {
                                voxels[idxVoxel] = 5; // Deep Sand
                            } else {
                                voxels[idxVoxel] = 3; // Stone
                            }
                        }
                    } else {
                        // IT IS AIR (or Water)
                        if (wy <= seaLevel) {
                            voxels[idxVoxel] = 6; // Water
                        } else {
                            voxels[idxVoxel] = 0; // Air
                        }
                    }
                }
            }
        }
    }

    // --------------------------------------------------------------------------------------------
    // API / INTERFACE
    // --------------------------------------------------------------------------------------------

    uint8_t GetBlock(float x, float y, float z, int lodScale) const override {
        // Single block query for physics/raycasts
        float density = GetDensity(x, y, z);
        
        if (density > m_settings.surfaceThreshold) {
            // Check above for surface
            float densityAbove = GetDensity(x, y + lodScale, z);
            bool isSurface = (densityAbove <= m_settings.surfaceThreshold);

            if (isSurface) {
                if (y <= m_settings.seaLevel + m_settings.sandHeight && y >= m_settings.seaLevel - 5) return 5; // Sand
                if (y < m_settings.seaLevel) return 2; // Underwater dirt
                return 1; // Grass
            }
            return 3; // Stone
        }

        if (y <= m_settings.seaLevel) return 6; // Water
        return 0; // Air
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        // Approximate bounds for 3D noise
        minH = 0;
        maxH = 256; 
    }

    void OnImGui() override {
#ifdef IMGUI_VERSION
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.5f, 1.0f), "Beach World Generator");
        bool changed = false;

        if (ImGui::CollapsingHeader("Global Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            if(ImGui::DragInt("Seed", &m_settings.seed)) changed = true;
            if(ImGui::DragInt("Sea Level", &m_settings.seaLevel, 1, 0, 128)) changed = true;
            if(ImGui::DragInt("Sand Height", &m_settings.sandHeight, 0.2f, 0, 20)) changed = true;
        }

        if (ImGui::CollapsingHeader("3D Shape (Coves & Cliffs)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Controls the volumetric noise");
            if(ImGui::SliderFloat("Scale (Zoom)", &m_settings.noiseScale, 0.01f, 0.15f)) changed = true;
            if(ImGui::SliderFloat("Cliff Verticality", &m_settings.yStretch, 0.1f, 5.0f)) changed = true;
            if(ImGui::SliderFloat("Cliff Height (Falloff)", &m_settings.solidFalloff, 10.0f, 200.0f)) changed = true;
            if(ImGui::SliderFloat("Surface Threshold", &m_settings.surfaceThreshold, -1.0f, 1.0f)) changed = true;
        }

        if (ImGui::CollapsingHeader("Noise Detail")) {
            if(ImGui::SliderInt("Octaves", &m_settings.octaves, 1, 8)) changed = true;
            if(ImGui::SliderFloat("Gain", &m_settings.gain, 0.0f, 1.0f)) changed = true;
        }

        if (changed) { m_dirty = true; Init(); }
#endif
    }

private:
    BeachSettings m_settings;
    FastNoise::SmartNode<> m_shapeNoise;
};