// Chunk.h
#pragma once
#include <FastNoise/FastNoise.h>

constexpr int CHUNK_SIZE = 32;
// Padded size includes 1-voxel border for neighbor culling
constexpr int CHUNK_SIZE_PADDED = CHUNK_SIZE + 2; 

struct Chunk {
    // 34^3 = 39,304 bytes. Fits in L2 Cache.
    uint8_t voxels;
    int worldX, worldY, worldZ;

    // Helper to index padded array
    // (1,1,1) in this array is (0,0,0) in the logical chunk
    inline uint8_t Get(int x, int y, int z) const {
        return voxels;
    }

    inline void Set(int x, int y, int z, uint8_t v) {
        voxels = v;
    }
};

// Simple generator
void FillChunk(Chunk& chunk) {
    auto node = FastNoise::New<FastNoise::Simplex>();
    // Get SIMD-optimized noise for the whole 32x32x32 block
    // Note: In real code, generate 34x34x34 to cover padding!
    
    // Naive loops for clarity (FastNoise2 has GenTile which is faster)
    for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
        for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
            // Map 3D noise to world coordinates
            float wx = (chunk.worldX + (x - 1)) * 0.02f;
            float wz = (chunk.worldZ + (z - 1)) * 0.02f;
            
            // Simple heightmap: Base height 16 + noise
            float noiseVal = node->GenSingle2D(wx, wz, 1337); 
            int height = 16 + (int)(noiseVal * 10.0f);

            for (int y = 0; y < CHUNK_SIZE_PADDED; y++) {
                int wy = chunk.worldY + (y - 1);
                uint8_t block = 0;
                if (wy < height) block = 1; // Stone
                chunk.Set(x, y, z, block);
            }
        }
    }
}