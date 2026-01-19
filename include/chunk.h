#pragma once
#include <FastNoise/FastNoise.h>
#include <cstring> // for memset

constexpr int CHUNK_SIZE = 32;
// Padded size includes 1-voxel border for neighbor culling
constexpr int CHUNK_SIZE_PADDED = CHUNK_SIZE + 2; 

struct Chunk {
    // FIXED: This was just 'uint8_t voxels' (1 byte). 
    // It is now an array of 34^3 = 39,304 bytes.
    uint8_t voxels[CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED];
    
    int worldX = 0;
    int worldY = 0;
    int worldZ = 0;

    Chunk() {
        // Always initialize to 0 (Air) to avoid garbage data
        std::memset(voxels, 0, sizeof(voxels));
    }

    // Indexing Helper:
    // We use (X * Area) + (Z * Width) + Y
    // This places the Y-column contiguously in memory, which is faster for heightmap generation.
    inline int GetIndex(int x, int y, int z) const {
        return (x * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED) + (z * CHUNK_SIZE_PADDED) + y;
    }

    inline uint8_t Get(int x, int y, int z) const {
        // Bounds safety check
        if (x < 0 || x >= CHUNK_SIZE_PADDED || 
            y < 0 || y >= CHUNK_SIZE_PADDED || 
            z < 0 || z >= CHUNK_SIZE_PADDED) return 0;
            
        return voxels[GetIndex(x, y, z)];
    }

    inline void Set(int x, int y, int z, uint8_t v) {
        if (x < 0 || x >= CHUNK_SIZE_PADDED || 
            y < 0 || y >= CHUNK_SIZE_PADDED || 
            z < 0 || z >= CHUNK_SIZE_PADDED) return;

        voxels[GetIndex(x, y, z)] = v;
    }
};

// Simple generator
void FillChunk(Chunk& chunk) {
    auto node = FastNoise::New<FastNoise::Simplex>();
    
    for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
        for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
            // Map 3D noise to world coordinates
            float wx = (chunk.worldX + (x - 1)) * 0.02f;
            float wz = (chunk.worldZ + (z - 1)) * 0.02f;
            
            // Simple heightmap: Base height 16 + noise
            // Simplex noise is roughly -1.0 to 1.0
            float noiseVal = node->GenSingle2D(wx, wz, 1337); 
            int height = 16 + (int)(noiseVal * 10.0f);

            for (int y = 0; y < CHUNK_SIZE_PADDED; y++) {
                int wy = chunk.worldY + (y - 1);
                uint8_t block = 0;
                
                // Debug Visualization:
                // Set blocks below 'height' to Stone (1)
                // Set blocks exactly at 'height' to Grass (2) for contrast if you have textures
                if (wy < height) block = 1; 
                
                chunk.Set(x, y, z, block);
            }
        }
    }
}