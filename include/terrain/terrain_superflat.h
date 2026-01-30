#pragma once
#include "terrain_system.h"
#include "chunk.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

// ================================================================================================
// SUPERFLAT GENERATOR
// ================================================================================================
class SuperflatGenerator : public ITerrainGenerator {
public:
    struct GenSettings {
        int floorLevel = 10;           // Renamed from groundHeight
        int floorBlockID = 1;          // Renamed from groundBlockID
        bool enableBedrock = true;     // Renamed from generateBedrock
        bool enableStaircase = true;   // Renamed from generateStairs
    };

    SuperflatGenerator() : m_settings(GenSettings()) { Init(); }
    SuperflatGenerator(GenSettings settings) : m_settings(settings) { Init(); }

    void Init() override {}

    std::vector<std::string> GetTexturePaths() const override {
        std::vector<std::string> textures(30, "resources/textures/dirt1.jpg");
        return textures;
    }

    int GetHeight(float worldX, float worldZ) const {
        return m_settings.floorLevel;
    }

    void GetHeightBounds(int chunkX, int chunkZ, int lodScale, int& outMinHeight, int& outMaxHeight) override {
        outMinHeight = 0;
        outMaxHeight = m_settings.floorLevel + (m_settings.enableStaircase ? 15 : 0); 
    }

    // --------------------------------------------------------------------------------------------
    // GET BLOCK (Single Voxel Physics)
    // --------------------------------------------------------------------------------------------
    uint8_t GetBlock(float worldX, float worldY, float worldZ, int lodScale) const override {
        int integerWorldY = (int)std::floor(worldY);

        // 1. Bedrock Layer
        if (m_settings.enableBedrock && integerWorldY == 0) return 19;

        // 2. Staircase Logic (Physics)
        if (m_settings.enableStaircase) {
             int integerWorldX = (int)std::floor(worldX);
             int integerWorldZ = (int)std::floor(worldZ);
             int floorHeight = m_settings.floorLevel;

             // Simple Staircase: 3 blocks wide, 10 blocks long, going UP the X axis
             // X: 0 to 9 (Length)
             // Z: -1 to 1 (Width)
             if (integerWorldZ >= -1 && integerWorldZ <= 1 && integerWorldX >= 0 && integerWorldX < 10) {
                 int stepHeightOffset = integerWorldX + 1; // Step 1 starts at x=0
                 if (integerWorldY == floorHeight + stepHeightOffset) {
                     return 4; // Cobblestone Steps
                 }
             }
        }

        // 3. Flat Ground
        if (integerWorldY <= m_settings.floorLevel && integerWorldY > 0) return (uint8_t)m_settings.floorBlockID;

        return 0;
    }

    // --------------------------------------------------------------------------------------------
    // GENERATE CHUNK (Visual Mesh)
    // --------------------------------------------------------------------------------------------
    void GenerateChunk(Chunk* chunk, int chunkX, int chunkY, int chunkZ, int lodScale) override {
        // PADDED_SIZE is usually 34 (32 blocks + 1 block padding on each side)
        const int PADDED_SIZE = CHUNK_SIZE_PADDED; 
        
        // we must ensure the buffer is clean. Otherwise, we see "ghost" structures from recycled chunks.
        std::memset(chunk->voxels, 0, PADDED_SIZE * PADDED_SIZE * PADDED_SIZE * sizeof(uint8_t));

        // Calculate the world coordinate of the chunk's true origin
        int chunkOriginY = chunkY * CHUNK_SIZE * lodScale;
        
        // The voxel array index 0 actually corresponds to (Origin - 1).
        // Index 1 corresponds to Origin.
        // We must account for this offset, otherwise visuals shift by 1 block!
        int arrayPaddingOffset = 1;

        uint8_t* voxelData = chunk->voxels;

        // --- STEP 1: TERRAIN PASS ---
        for (int arrayY = 0; arrayY < PADDED_SIZE; arrayY++) {
            // Convert Array Index -> World Coordinate
            // Formula: World = Origin + (Index - Padding) * Scale
            int currentWorldY = chunkOriginY + ((arrayY - arrayPaddingOffset) * lodScale);
            
            uint8_t layerBlockID = 0;

            if (m_settings.enableBedrock && currentWorldY == 0) {
                layerBlockID = 19; 
            }
            else if (currentWorldY <= m_settings.floorLevel && currentWorldY > 0) {
                layerBlockID = (uint8_t)m_settings.floorBlockID;
            }

            // Fill this entire Y-layer with the block
            if (layerBlockID != 0) {
                for (int i = 0; i < PADDED_SIZE * PADDED_SIZE; i++) {
                    int voxelIndex = i + (arrayY * PADDED_SIZE * PADDED_SIZE);
                    voxelData[voxelIndex] = layerBlockID;
                }
            } else {
                // (Optional) Explicitly clear air, though usually pre-zeroed
                // memset logic could go here
            }
        }


        // --- STEP 2: STRUCTURE PASS (Staircase) ---
        // for now, only generate in LOD 0 (interaction LOD)
        if (m_settings.enableStaircase && lodScale == 1) {
            int floorHeight = m_settings.floorLevel;
            
            // Iterate over the structure's logical bounds
            for (int stepX = 0; stepX < 10; stepX++) {
                for (int stepZ = -1; stepZ <= 1; stepZ++) {
                    
                    // Logic: X=0 -> Height=Floor+1
                    int stepWorldY = floorHeight + stepX + 1;

                    // Convert World Space -> Chunk Array Index
                    // Formula: Index = (World - Origin) / Scale + Padding
                    int arrayX = (stepX - (chunkX * CHUNK_SIZE)) / lodScale + arrayPaddingOffset;
                    int arrayZ = (stepZ - (chunkZ * CHUNK_SIZE)) / lodScale + arrayPaddingOffset;
                    int arrayY = (stepWorldY - chunkOriginY) / lodScale + arrayPaddingOffset;

                    // Bounds Check: Is this step inside this specific chunk's memory?
                    if (arrayX >= 0 && arrayX < PADDED_SIZE && 
                        arrayZ >= 0 && arrayZ < PADDED_SIZE && 
                        arrayY >= 0 && arrayY < PADDED_SIZE) {
                        
                        int voxelIndex = arrayX + (arrayZ * PADDED_SIZE) + (arrayY * PADDED_SIZE * PADDED_SIZE);
                        voxelData[voxelIndex] = 4; // Cobblestone
                    }
                }
            }
        }
    }

    void OnImGui() override {
#ifdef IMGUI_VERSION
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Superflat Settings");
        bool changed = false;
        if (ImGui::SliderInt("Floor Height", &m_settings.floorLevel, 1, 255)) changed = true;
        if (ImGui::SliderInt("Floor Material", &m_settings.floorBlockID, 1, 20)) changed = true;
        if (ImGui::Checkbox("Enable Bedrock", &m_settings.enableBedrock)) changed = true;
        if (ImGui::Checkbox("Enable Staircase", &m_settings.enableStaircase)) changed = true;
        if (changed) m_dirty = true;
#endif
    }
private:
    GenSettings m_settings;
};