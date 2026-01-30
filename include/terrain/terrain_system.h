#pragma once

#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <cstdint>
#include <FastNoise/FastNoise.h>
#include "chunk.h" // Include chunk definition so we can write to it directly

// ================================================================================================
// 2. TERRAIN GENERATOR INTERFACE
// ================================================================================================
class ITerrainGenerator {
public:
    virtual ~ITerrainGenerator() = default;

    virtual void Init() = 0; 
    virtual void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) = 0;

    // OLD: Slow per-block call
    virtual uint8_t GetBlock(float x, float y, float z, int lodScale) const = 0;

    // NEW: Fast batched generation
    // We pass the chunk pointer to write directly to the voxel array
    virtual void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int scale) = 0;

    virtual std::vector<std::string> GetTexturePaths() const = 0;
    virtual void OnImGui() {} 
    virtual bool HasChanged() { return m_dirty; }
    virtual void ClearDirtyFlag() { m_dirty = false; }

protected:
    bool m_dirty = false;
};

// ================================================================================================
// 3. STANDARD GENERATOR (Heightmap Backward Compatibility)
// ================================================================================================
class StandardGenerator : public ITerrainGenerator {
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

    StandardGenerator(); 
    StandardGenerator(int seed);
    StandardGenerator(TerrainSettings settings);
    
    void Init() override;
    int GetHeight(float x, float z) const; 
    
    uint8_t GetBlock(float x, float y, float z, int lodScale) const override;
    
    // Fallback implementation for standard generator (or implement optimization here too)
    void GenerateChunk(Chunk* chunk, int cx, int cy, int cz, int scale) override {
        // Fallback to slow loop if you haven't optimized StandardGenerator yet
        int worldX = cx * CHUNK_SIZE * scale;
        int worldY = cy * CHUNK_SIZE * scale;
        int worldZ = cz * CHUNK_SIZE * scale;

        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            float wx = (float)(worldX + (x - 1) * scale);
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                float wz = (float)(worldZ + (z - 1) * scale);
                for (int y = 0; y < CHUNK_SIZE_PADDED; y++) {
                    int wy = worldY + (y - 1) * scale;
                    chunk->Set(x, y, z, GetBlock(wx, (float)wy, wz, scale));
                }
            }
        }
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override;
    std::vector<std::string> GetTexturePaths() const override;
    void OnImGui() override;

private:
    TerrainSettings m_settings;
    FastNoise::SmartNode<> m_baseNoise;
    FastNoise::SmartNode<> m_mountainNoise;
    FastNoise::SmartNode<> m_caveNoise;
};