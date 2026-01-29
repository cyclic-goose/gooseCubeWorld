#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

struct PackedVertex {
    uint32_t data; 

    PackedVertex() : data(0) {}

    PackedVertex(float x, float y, float z, float face, float ao, uint32_t textureId) {
        // FIX: Add 0.5f bias before casting to uint32_t to prevent 9.999f truncating to 9.
        // This solves the "Random Y Offset" / jittering.
        uint32_t ix = (uint32_t)(x + 0.5f) & 0x3F;
        uint32_t iy = (uint32_t)(y + 0.5f) & 0x3F;
        uint32_t iz = (uint32_t)(z + 0.5f) & 0x3F;
        
        uint32_t iNorm = (uint32_t)(face + 0.5f) & 0x7;
        uint32_t iAo   = (uint32_t)(ao + 0.5f)   & 0x3;
        uint32_t iTex  = textureId      & 0x1FF; 

        data = ix | 
              (iy << 6)  | 
              (iz << 12) | 
              (iNorm << 18) | 
              (iAo   << 21) | 
              (iTex  << 23);
    }
};