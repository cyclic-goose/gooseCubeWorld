#pragma once
#include <FastNoise/FastNoise.h>
#include <cstring> 

constexpr int CHUNK_SIZE = 32;
constexpr int CHUNK_SIZE_PADDED = CHUNK_SIZE + 2; 

struct Chunk {
    uint8_t voxels[CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED];
    

    Chunk() {
        std::memset(voxels, 0, sizeof(voxels));
    }

    inline int GetIndex(int x, int y, int z) const {
        return (x * CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED) + (z * CHUNK_SIZE_PADDED) + y;
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
        //isUniform = false; // Break uniform assumption on write
    }

    inline void Set(int x, int y, int z, uint8_t v) {
        // set (dangerous) skip check but if our generation logic is already checking, why waste cycles here
        voxels[GetIndex(x, y, z)] = v;
        //isUniform = false; // Break uniform assumption on write
    }
    
    // // Fast fill for Air/Solid chunks
    // void FillUniform(uint8_t id) {
    //     std::memset(voxels, id, sizeof(voxels));
    //     isUniform = true;
    //     uniformID = id;
    // }
};