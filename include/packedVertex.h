#pragma once
#include <cstdint>

struct PackedVertex {
    // We pack 3 coordinates (X, Y, Z) and auxiliary data (Texture ID, etc.) into two 32-bit integers.
    // Word 0: X (6 bits), Y (6 bits), Z (6 bits), Unused/AO (14 bits) -> Adjust logic as needed
    // Word 1: Normal/Face (3 bits), TextureID (16 bits), etc.
    
    // For simplicity in this example, we will store raw floats in a struct that *looks* packed
    // but ensures your shader (which expects raw floats/ints based on your provided code) works.
    // Ideally, you would use: uint32_t d0, d1;
    
    // However, based on your main.cpp usage: vertices.emplace_back(0, 0, 0, 4, 1);
    // Let's create a constructor that handles that.

    // 2x 32-bit integers = 8 bytes per vertex.
    uint32_t data1; 
    uint32_t data2;

    PackedVertex() : data1(0), data2(0) {}

    PackedVertex(float x, float y, float z, float axisOrNormal, float textureID) {
        // PACKING LOGIC
        // This assumes coordinates are local to the chunk (0-32).
        // 6 bits = 0 to 63 range.
        
        uint32_t ix = (uint32_t)x & 0x3F; // 6 bits
        uint32_t iy = (uint32_t)y & 0x3F; // 6 bits
        uint32_t iz = (uint32_t)z & 0x3F; // 6 bits
        uint32_t ia = (uint32_t)axisOrNormal & 0x07; // 3 bits for axis/normal
        uint32_t it = (uint32_t)textureID & 0xFFFF; // 16 bits for texture

        // Layout: |  Unused (11) | Axis (3) | Z (6) | Y (6) | X (6) |
        data1 = ix | (iy << 6) | (iz << 12) | (ia << 18);
        
        // Layout: | Unused (16) | Texture (16) |
        data2 = it;
    }
};