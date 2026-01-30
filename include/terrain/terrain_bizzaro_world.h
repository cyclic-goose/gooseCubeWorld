#pragma once
#include "terrain_system.h"
#include <algorithm>
#include <vector>
#include <cmath>

#ifndef IMGUI_VERSION
// forward declaration or include your imgui header here if needed
// #include "imgui.h"
#endif

// ================================================================================================
// BIZZARO GENERATOR: CRATER WORLD
// Desolate landscape with sharp craters and alien obelisks.
// ================================================================================================
class BizzaroGenerator : public ITerrainGenerator {
public:
    struct BizzaroSettings {
        int seed = 4242;
        
        // Base Terrain (The Ground)
        float baseHeight = 110.0f;
        float groundFreq = 0.009f;   // Rolling hills scale
        float groundAmp = 30.0f;

        // Craters (Rigid Noise)
        float craterFreq = 0.02f;    // Size of craters
        float craterAmp = 45.0f;     // Depth/Height of crater rims
        int craterOctaves = 4;
        float craterBias = 0.5f;     // Pushes terrain down/up

        // Obelisks (Alien Structures)
        float obeliskFreq = 0.15f;   // Distribution (High freq = small clusters)
        float obeliskThreshold = 0.75f; // Rarity (Higher = rarer)
        int obeliskHeight = 60;      // Height of pillars
        
        // Materials (IDs from fallback_texture_id_table.txt)
        int matSurface = 9;          // Gravel (Noise Grey)
        int matSub = 8;              // Clay (Blue-Grey)
        int matCrater = 19;          // Bedrock/Obsidian (Dark)
        int matObelisk = 23;         // Gem/Diamond (Alien Cyan)
        int matObeliskCore = 22;     // Gold (Inner core)
    };

    BizzaroGenerator();
    void Init() override;
    
    // Batched Generation (Optimized for Chunk Layout)
    void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int lodScale) override;

    // Single Block fallback
    uint8_t GetBlock(float x, float y, float z, int lodScale) const override;
    
    // Bounds & Textures
    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override;
    std::vector<std::string> GetTexturePaths() const override;
    
    // UI
    void OnImGui() override;

private:
    BizzaroSettings m_settings;
    FastNoise::SmartNode<> m_groundNoise;
    FastNoise::SmartNode<> m_craterNoise;
    FastNoise::SmartNode<> m_obeliskNoise;
};


// ================================================================================================
// IMPLEMENTATION
// ================================================================================================

BizzaroGenerator::BizzaroGenerator() { Init(); }

void BizzaroGenerator::Init() {
    // 1. Ground Noise (Gentle Rolling Hills)
    auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
    auto fnGround = FastNoise::New<FastNoise::FractalFBm>();
    fnGround->SetSource(fnPerlin);
    fnGround->SetOctaveCount(3);
    fnGround->SetGain(0.5f);
    m_groundNoise = fnGround;

    // 2. Crater Noise (Ridged for sharp ridges/dips)
    // Replaced FractalRigidMulti with FractalRidged
    auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
    auto fnCrater = FastNoise::New<FastNoise::FractalRidged>(); // CORRECTED CLASS NAME
    fnCrater->SetSource(fnSimplex);
    fnCrater->SetOctaveCount(m_settings.craterOctaves);
    fnCrater->SetGain(0.5f);
    fnCrater->SetLacunarity(2.0f);
    m_craterNoise = fnCrater;

    // 3. Obelisk Mask (High Frequency Simplex)
    auto fnObelisk = FastNoise::New<FastNoise::Simplex>();
    m_obeliskNoise = fnObelisk;
}

std::vector<std::string> BizzaroGenerator::GetTexturePaths() const {
    // Return dummy paths. The renderer uses the ID to pick colors from the fallback table.
    // We need enough entries to cover IDs up to 24.
    std::vector<std::string> paths(30, "resources/textures/dirt1.jpg");
    return paths;
}

void BizzaroGenerator::GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) {
    minH = 0; 
    maxH = 256; // High cap for obelisks
}

// Single block query (Slow, mainly for physics)
uint8_t BizzaroGenerator::GetBlock(float x, float y, float z, int lodScale) const {
    // Replicate logic from GenerateChunk (simplified)
    float g = m_groundNoise->GenSingle2D(x * m_settings.groundFreq, z * m_settings.groundFreq, m_settings.seed);
    float c = m_craterNoise->GenSingle2D(x * m_settings.craterFreq, z * m_settings.craterFreq, m_settings.seed + 1);
    float o = m_obeliskNoise->GenSingle2D(x * m_settings.obeliskFreq, z * m_settings.obeliskFreq, m_settings.seed + 2);

    // Calc Terrain Height
    float craterMod = (c - m_settings.craterBias) * m_settings.craterAmp; 
    
    float terrainH = m_settings.baseHeight + (g * m_settings.groundAmp) + craterMod;

    // Obelisks
    bool isObelisk = (o > m_settings.obeliskThreshold);
    float finalH = terrainH;
    
    if (isObelisk) {
        finalH += m_settings.obeliskHeight;
    }

    if (y > finalH) return 0; // Air

    if (isObelisk && y > terrainH) {
        return (uint8_t)m_settings.matObelisk;
    }

    // Ground
    if (y > terrainH - 3) return (uint8_t)m_settings.matSurface;
    if (craterMod < -5.0f && y > terrainH - 5) return (uint8_t)m_settings.matCrater; // Dark bottoms
    return (uint8_t)m_settings.matSub;
}

// ================================================================================================
// BATCHED CHUNK GENERATION (Optimized)
// ================================================================================================
void BizzaroGenerator::GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int lodScale) {
    // Thread-local buffers to reuse memory
    static thread_local std::vector<float> bufGround;
    static thread_local std::vector<float> bufCrater;
    static thread_local std::vector<float> bufObelisk;
    static thread_local std::vector<int>   mapHeight;
    static thread_local std::vector<uint8_t> mapMat;

    int P = CHUNK_SIZE_PADDED;
    int size2D = P * P;

    if (bufGround.size() != size2D) {
        bufGround.resize(size2D);
        bufCrater.resize(size2D);
        bufObelisk.resize(size2D);
        mapHeight.resize(size2D);
        mapMat.resize(size2D);
    }

    // 1. Calculate World 2D Coordinates
    float scale = lodScale; 
    float startX = (cx * CHUNK_SIZE - 1) * scale;
    float startZ = (cz * CHUNK_SIZE - 1) * scale;
    
    // FastNoise GenUniformGrid2D generates: X then Y (which is Z in world space)
    
    // A. Ground
    m_groundNoise->GenUniformGrid2D(bufGround.data(), 
                                    startX * m_settings.groundFreq, startZ * m_settings.groundFreq, 
                                    P, P, 
                                    scale * m_settings.groundFreq, scale * m_settings.groundFreq, 
                                    m_settings.seed);

    // B. Craters
    m_craterNoise->GenUniformGrid2D(bufCrater.data(), 
                                    startX * m_settings.craterFreq, startZ * m_settings.craterFreq, 
                                    P, P, 
                                    scale * m_settings.craterFreq, scale * m_settings.craterFreq, 
                                    m_settings.seed + 1);

    // C. Obelisks
    m_obeliskNoise->GenUniformGrid2D(bufObelisk.data(), 
                                     startX * m_settings.obeliskFreq, startZ * m_settings.obeliskFreq, 
                                     P, P, 
                                     scale * m_settings.obeliskFreq, scale * m_settings.obeliskFreq, 
                                     m_settings.seed + 2);

    // 2. Pre-calculate Heightmap & Material Map
    for (int i = 0; i < size2D; i++) {
        float g = bufGround[i];
        float c = bufCrater[i]; 
        float o = bufObelisk[i];

        // Combine to form Terrain Height
        float craterMod = (c - m_settings.craterBias) * m_settings.craterAmp;
        float hFloat = m_settings.baseHeight + (g * m_settings.groundAmp) + craterMod;
        
        uint8_t mat = (uint8_t)m_settings.matSub; 
        
        // Obelisk Check
        bool isObelisk = (o > m_settings.obeliskThreshold);
        
        if (isObelisk) {
            hFloat += m_settings.obeliskHeight;
            mat = (uint8_t)m_settings.matObelisk; 
        } else {
            if (craterMod < -10.0f) mat = (uint8_t)m_settings.matCrater; 
            else mat = (uint8_t)m_settings.matSurface; 
        }
        
        mapHeight[i] = (int)hFloat;
        mapMat[i] = mat;
    }

    // 3. Fill Voxels (Optimized Loop Order: Y -> Z -> X)
    uint8_t* voxels = chunk->voxels;
    int worldYBase = (cy * CHUNK_SIZE * lodScale) - (1 * lodScale);
    int matObelisk = m_settings.matObelisk;
    int matCore = m_settings.matObeliskCore;
    int matSub = m_settings.matSub;

    for (int y = 0; y < P; y++) {
        int worldY = worldYBase + (y * lodScale);
        int chunkStrideY = P * P;
        int voxelIdxBase = y * chunkStrideY; 

        for (int z = 0; z < P; z++) {
            int mapRowOffset = z * P; 
            int chunkRowOffset = z * P; 

            for (int x = 0; x < P; x++) {
                int i2D = x + mapRowOffset;
                int idxVoxel = voxelIdxBase + chunkRowOffset + x;

                int h = mapHeight[i2D];
                
                if (worldY > h) {
                    voxels[idxVoxel] = 0; // Air
                } else {
                    uint8_t mat = mapMat[i2D]; 
                    int depth = h - worldY;

                    if (mat == matObelisk) {
                        if (depth < 10) voxels[idxVoxel] = (uint8_t)matObelisk;
                        else voxels[idxVoxel] = (uint8_t)matCore;
                    } 
                    else {
                        if (depth == 0) voxels[idxVoxel] = mat; 
                        else if (depth < 4) voxels[idxVoxel] = (uint8_t)matSub; 
                        else voxels[idxVoxel] = 3; 
                    }
                }
            }
        }
    }
}

// ================================================================================================
// UI CONTROLS
// ================================================================================================
#ifdef IMGUI_VERSION
void BizzaroGenerator::OnImGui() {
    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.8f, 1.0f), "CRATER WORLD GEN");
    
    bool changed = false;

    if (ImGui::Button("Reroll Seed")) {
        m_settings.seed = rand();
        changed = true;
    }
    
    if (ImGui::CollapsingHeader("Ground & Craters", ImGuiTreeNodeFlags_DefaultOpen)) {
        if(ImGui::DragFloat("Base Height", &m_settings.baseHeight)) changed = true;
        if(ImGui::SliderFloat("Ground Freq", &m_settings.groundFreq, 0.001f, 0.05f)) changed = true;
        if(ImGui::SliderFloat("Crater Freq", &m_settings.craterFreq, 0.001f, 0.1f)) changed = true;
        if(ImGui::SliderFloat("Crater Amp", &m_settings.craterAmp, 0.0f, 100.0f)) changed = true;
        if(ImGui::SliderFloat("Crater Bias", &m_settings.craterBias, -1.0f, 1.0f)) changed = true;
        if(ImGui::SliderInt("Crater Detail", &m_settings.craterOctaves, 1, 6)) changed = true;
    }

    if (ImGui::CollapsingHeader("Obelisks")) {
        if(ImGui::SliderFloat("Distribution", &m_settings.obeliskFreq, 0.01f, 0.5f)) changed = true;
        if(ImGui::SliderFloat("Rarity", &m_settings.obeliskThreshold, 0.5f, 1.0f)) changed = true;
        if(ImGui::SliderInt("Height", &m_settings.obeliskHeight, 10, 200)) changed = true;
    }

    if (ImGui::CollapsingHeader("Materials (IDs)")) {
        ImGui::TextDisabled("Ref: 9=Gravel, 19=Bedrock, 23=Gem");
        if(ImGui::SliderInt("Surface", &m_settings.matSurface, 1, 24)) changed = true;
        if(ImGui::SliderInt("Crater Bottom", &m_settings.matCrater, 1, 24)) changed = true;
        if(ImGui::SliderInt("Obelisk Outer", &m_settings.matObelisk, 1, 24)) changed = true;
        if(ImGui::SliderInt("Obelisk Core", &m_settings.matObeliskCore, 1, 24)) changed = true;
    }

    if (changed) {
        m_dirty = true;
        Init(); 
    }
}
#endif