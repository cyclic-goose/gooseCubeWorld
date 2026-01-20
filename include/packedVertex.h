#pragma once
#include <cstdint>
#include <cmath>

struct PackedVertex {
    uint32_t d0; 
    uint32_t d1; 

    PackedVertex() : d0(0), d1(0) {}

    // UPDATED: Now uses 8 bits per axis with +64 offset to handle negative values (skirts)
    PackedVertex(float x, float y, float z, float face, float ao, uint32_t textureId) {
        // Offset to handle negative coordinates (e.g. skirts going down)
        // Range: -64.0 to +191.0 covers 0..32 chunk + skirts
        uint32_t ix = (uint32_t)(x + 64.0f);
        uint32_t iy = (uint32_t)(y + 64.0f);
        uint32_t iz = (uint32_t)(z + 64.0f);
        uint32_t iface = (uint32_t)face;
        
        // Packing scheme:
        // x: bits 0-7   (8 bits)
        // y: bits 8-15  (8 bits)
        // z: bits 16-23 (8 bits)
        // norm: bits 24-26 (3 bits)
        d0 = (ix & 0xFF) | 
             ((iy & 0xFF) << 8) | 
             ((iz & 0xFF) << 16) | 
             ((iface & 0x7) << 24);

        d1 = (textureId & 0xFFFF);
    }
};