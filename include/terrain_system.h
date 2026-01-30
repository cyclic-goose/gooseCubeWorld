#pragma once

#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <cstdint>
#include <FastNoise/FastNoise.h>

// ================================================================================================
// 2. TERRAIN GENERATOR INTERFACE
// ================================================================================================
class ITerrainGenerator {
public:
    virtual ~ITerrainGenerator() = default;

    // Core Generation
    virtual void Init() = 0; 
    
    // Used for Culling only (Is this chunk totally empty or totally full?)
    // In 3D mode, this returns the max potential height of the terrain surface.
    virtual void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) = 0;

    // THE BIG CHANGE: 
    // We removed 'heightAtXZ'. The generator must calculate density internally at (x,y,z).
    virtual uint8_t GetBlock(float x, float y, float z, int lodScale) const = 0;

    // Returns the list of textures this generator expects
    virtual std::vector<std::string> GetTexturePaths() const = 0;

    // UI & Hot Reloading
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
    // Helper for internal use, though not strictly required by interface anymore
    int GetHeight(float x, float z) const; 
    
    uint8_t GetBlock(float x, float y, float z, int lodScale) const override;
    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override;

    std::vector<std::string> GetTexturePaths() const override;
    void OnImGui() override;

private:
    TerrainSettings m_settings;
    FastNoise::SmartNode<> m_baseNoise;
    FastNoise::SmartNode<> m_mountainNoise;
    FastNoise::SmartNode<> m_caveNoise;
};

