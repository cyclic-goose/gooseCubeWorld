#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

struct PackedVertex {
    uint32_t data; // Reduced from 8 bytes (2x uint32) to 4 bytes (1x uint32)

    PackedVertex() : data(0) {}

    PackedVertex(float x, float y, float z, float face, float ao, uint32_t textureId) {
        // LAYOUT (32 BITS TOTAL):
        // [00-05] X        (6 bits) : Range 0..63
        // [06-11] Y        (6 bits) : Range 0..63
        // [12-17] Z        (6 bits) : Range 0..63
        // [18-20] Normal   (3 bits) : Range 0..7
        // [21-22] AO       (2 bits) : Range 0..3
        // [23-31] TexID    (9 bits) : Range 0..511

        // FIX: Removed +8.0f offset. The mesher generates coordinates 0..32.
        // If your shader expects 0-based coordinates, adding 8.0f causes a 
        // permanent 8-block offset in all directions.
        uint32_t ix = (uint32_t)(x) & 0x3F;
        uint32_t iy = (uint32_t)(y) & 0x3F;
        uint32_t iz = (uint32_t)(z) & 0x3F;
        
        uint32_t iNorm = (uint32_t)face & 0x7;
        uint32_t iAo   = (uint32_t)ao   & 0x3;
        uint32_t iTex  = textureId      & 0x1FF; // Cap at 512 textures

        data = ix | 
              (iy << 6)  | 
              (iz << 12) | 
              (iNorm << 18) | 
              (iAo   << 21) | 
              (iTex  << 23);
    }
};