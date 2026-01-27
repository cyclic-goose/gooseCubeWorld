#pragma once

#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <cstdint>
#include <FastNoise/FastNoise.h>

// ================================================================================================
// 1. ENGINE CONFIGURATION
// Technical settings only. No terrain generation parameters here.
// ================================================================================================
struct EngineConfig {
    // LOD & Culling
    int lodCount = 5;
    int lodRadius[12] = { 10, 16, 24, 32, 48, 0, 0, 0 , 0, 0, 0, 0};
    bool occlusionCulling = true;
    bool enableCaves = false; 

    // Memory & Debug
    float VRAM_HEAP_ALLOCATION_MB = 1024;
    int worldHeightChunks = 64;
    int cubeDebugMode = 4;
};

// ================================================================================================
// 2. TERRAIN GENERATOR INTERFACE
// ================================================================================================
class ITerrainGenerator {
public:
    virtual ~ITerrainGenerator() = default;

    // Core Generation
    virtual void Init() = 0; 
    virtual int GetHeight(float x, float z) const = 0;
    virtual uint8_t GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const = 0;
    virtual void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) = 0;

    // Returns the list of textures this generator expects, in order of Block ID - 1
    virtual std::vector<std::string> GetTexturePaths() const = 0;

    // UI & Hot Reloading
    virtual void OnImGui() {} 
    virtual bool HasChanged() { return m_dirty; }
    virtual void ClearDirtyFlag() { m_dirty = false; }

protected:
    bool m_dirty = false;
};

// ================================================================================================
// 3. STANDARD GENERATOR
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

    // constructors
    StandardGenerator(); 
    StandardGenerator(int seed);
    StandardGenerator(TerrainSettings settings);
    
    void Init() override;
    int GetHeight(float x, float z) const override;
    uint8_t GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const override;
    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override;

    // override superclass to define texturess for this specific biome YO
    std::vector<std::string> GetTexturePaths() const override;
    
    void OnImGui() override;

private:
    TerrainSettings m_settings;
    FastNoise::SmartNode<> m_baseNoise;
    FastNoise::SmartNode<> m_mountainNoise;
    FastNoise::SmartNode<> m_caveNoise;
};