#pragma once
#include <cstdint>

struct PackedVertex {
    // Total Size: 8 Bytes (2x uint32_t)
    uint32_t data1; 
    uint32_t data2;

    PackedVertex() : data1(0), data2(0) {}

    PackedVertex(float x, float y, float z, float axisOrNormal, float textureID) {
        // Packing Logic
        uint32_t ix = (uint32_t)x & 0x3F; // 6 bits (0-63)
        uint32_t iy = (uint32_t)y & 0x3F; 
        uint32_t iz = (uint32_t)z & 0x3F; 
        uint32_t ia = (uint32_t)axisOrNormal & 0x07; // 3 bits
        
        // Data 1: Position + Normal
        data1 = ix | (iy << 6) | (iz << 12) | (ia << 18);
        
        // Data 2: Texture ID
        data2 = (uint32_t)textureID & 0xFFFF;
    }
};