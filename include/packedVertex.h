#pragma once
#include <cstdint>

struct PackedVertex {
    uint32_t data1; 
    uint32_t data2;

    PackedVertex() : data1(0), data2(0) {}

    // Added 'int chunkId' to params
    PackedVertex(float x, float y, float z, float axisOrNormal, float textureID, int chunkId) {
        // --- Data 1 (Position + Normal) ---
        uint32_t ix = (uint32_t)x & 0x3F; 
        uint32_t iy = (uint32_t)y & 0x3F; 
        uint32_t iz = (uint32_t)z & 0x3F; 
        uint32_t ia = (uint32_t)axisOrNormal & 0x07; 
        
        data1 = ix | (iy << 6) | (iz << 12) | (ia << 18);
        
        // --- Data 2 (Texture + Chunk ID) ---
        uint32_t tex = (uint32_t)textureID & 0xFFFF; // Bottom 16 bits
        uint32_t cid = (uint32_t)chunkId & 0xFFFF;   // Bottom 16 bits (0-65535)

        // Pack Texture in lower half, Chunk ID in upper half
        data2 = tex | (cid << 16); 
    }
};