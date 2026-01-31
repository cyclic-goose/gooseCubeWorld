#pragma once
#include <FastNoise/FastNoise.h>
#include <cstring> 

constexpr int CHUNK_SIZE = 32;
constexpr int CHUNK_SIZE_PADDED = CHUNK_SIZE + 2; 

struct Chunk {
    // Standardized Layout: Y-Major (Y is slow, X is fast).
    // Conceptually: voxels[y][z][x]
    uint8_t voxels[CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED];

    //34×34×34=39,304 bytes per Chunk.
    //1 Chunk ≈ 39 KB.
    
    Chunk() {
        std::memset(voxels, 0, sizeof(voxels));
    }

    // New Standard: X is contiguous. 
    // This matches standard C 3D array layout: arr[y][z][x]
    inline int GetIndex(int x, int y, int z) const {
        return x + (z * CHUNK_SIZE_PADDED) + (y * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED);
    }

    inline uint8_t Get(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE_PADDED || 
            y < 0 || y >= CHUNK_SIZE_PADDED || 
            z < 0 || z >= CHUNK_SIZE_PADDED) return 0;
        return voxels[GetIndex(x, y, z)];
    }

    inline void SetSafe(int x, int y, int z, uint8_t v) {
        if (x < 0 || x >= CHUNK_SIZE_PADDED || 
            y < 0 || y >= CHUNK_SIZE_PADDED || 
            z < 0 || z >= CHUNK_SIZE_PADDED) return;
        voxels[GetIndex(x, y, z)] = v;
    }

    inline void Set(int x, int y, int z, uint8_t v) {
        voxels[GetIndex(x, y, z)] = v;
    }
};