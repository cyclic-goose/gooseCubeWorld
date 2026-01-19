#pragma once
#include <FastNoise/FastNoise.h>
#include <cstring> // for memset

/**
 * CONFIGURATION
 * -------------
 * CHUNK_SIZE: The logical size of the chunk (32x32x32).
 * CHUNK_SIZE_PADDED: Adds a 1-voxel border around the entire chunk.
 * * WHY PADDING?
 * When meshing a block at x=0, we need to check x=-1 to see if the face is visible.
 * Without padding, checking x=-1 would crash or require checking a different Chunk object.
 * Padding allows us to check neighbors locally in memory.
 */
constexpr int CHUNK_SIZE = 32;
constexpr int CHUNK_SIZE_PADDED = CHUNK_SIZE + 2; 

struct Chunk {
    // The raw block data. 34 * 34 * 34 = 39,304 bytes.
    // Fits easily in CPU L2 Cache (usually 256KB+), ensuring very fast access.
    uint8_t voxels[CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED];
    
    int worldX = 0;
    int worldY = 0;
    int worldZ = 0;

    Chunk() {
        // Critical: Initialize all memory to 0 (Air) to prevent garbage data
        std::memset(voxels, 0, sizeof(voxels));
    }

    /**
     * GetIndex
     * --------
     * Flattens 3D coordinates (x,y,z) into a 1D array index.
     * We use Y as the innermost dimension (stride 1). 
     * This is "Column-Major" storage, beneficial for Minecraft-like games 
     * where loops often iterate vertically (gravity, sunlight).
     */
    inline int GetIndex(int x, int y, int z) const {
        return (x * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED) + (z * CHUNK_SIZE_PADDED) + y;
    }

    // Returns block ID at coordinate. Returns 0 (Air) if out of bounds.
    inline uint8_t Get(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE_PADDED || 
            y < 0 || y >= CHUNK_SIZE_PADDED || 
            z < 0 || z >= CHUNK_SIZE_PADDED) return 0;
            
        return voxels[GetIndex(x, y, z)];
    }

    // Sets block ID. Silently fails if out of bounds.
    inline void Set(int x, int y, int z, uint8_t v) {
        if (x < 0 || x >= CHUNK_SIZE_PADDED || 
            y < 0 || y >= CHUNK_SIZE_PADDED || 
            z < 0 || z >= CHUNK_SIZE_PADDED) return;

        voxels[GetIndex(x, y, z)] = v;
    }
};