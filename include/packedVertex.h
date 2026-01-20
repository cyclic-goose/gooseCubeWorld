#pragma once
#include <cstdint>
#include <cmath>

// Matches shader: uvec2 packedVertices[];
struct PackedVertex {
    uint32_t d0; // x, y, z, normal
    uint32_t d1; // textureId, (ao unused in shader currently)

    PackedVertex() : d0(0), d1(0) {}

    // Compress data into 2 integers
    PackedVertex(float x, float y, float z, float face, float ao, uint32_t textureId) {
        uint32_t ix = (uint32_t)x;
        uint32_t iy = (uint32_t)y;
        uint32_t iz = (uint32_t)z;
        uint32_t iface = (uint32_t)face;
        
        // Packing scheme matching VERT_PRIMARY.glsl:
        // x: bits 0-5   (6 bits)
        // y: bits 6-11  (6 bits)
        // z: bits 12-17 (6 bits)
        // norm: bits 18-20 (3 bits)
        d0 = (ix & 0x3F) | 
             ((iy & 0x3F) << 6) | 
             ((iz & 0x3F) << 12) | 
             ((iface & 0x7) << 18);

        // d1: Texture ID in lower 16 bits
        d1 = (textureId & 0xFFFF);
        
        // (Optional) Pack AO into upper bits of d1 if you add it to shader later
        // uint32_t iao = (uint32_t)(ao * 3.0f); // map 0-1 to 0-3
        // d1 |= (iao & 0x3) << 16;
    }
};