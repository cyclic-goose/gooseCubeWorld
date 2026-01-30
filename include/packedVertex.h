#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

struct PackedVertex {
    uint32_t data; 

    PackedVertex() : data(0) {}

    PackedVertex(float x, float y, float z, float face, float ao, uint32_t textureId) {
        // Bias of 0.5f prevents float truncation errors (9.99 -> 9)
        // Mask 0x3F = 6 bits = values 0-63 (Enough for CHUNK_SIZE_PADDED = 34)
        
        uint32_t ix = (uint32_t)(x + 0.5f) & 0x3F;
        uint32_t iy = (uint32_t)(y + 0.5f) & 0x3F;
        uint32_t iz = (uint32_t)(z + 0.5f) & 0x3F;
        
        uint32_t iNorm = (uint32_t)(face + 0.5f) & 0x7;  // 3 bits
        uint32_t iAo   = (uint32_t)(ao + 0.5f)   & 0x3;  // 2 bits
        uint32_t iTex  = textureId      & 0x1FF; // 9 bits (512 texture IDs)

        // Packing Order: X, Y, Z, Norm, AO, Tex
        data = ix | 
              (iy << 6)  | 
              (iz << 12) | 
              (iNorm << 18) | 
              (iAo   << 21) | 
              (iTex  << 23);
    }
};