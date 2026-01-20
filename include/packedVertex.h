#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm> // for std::clamp

struct PackedVertex {
    uint32_t d0; 
    uint32_t d1; 

    PackedVertex() : d0(0), d1(0) {}

    PackedVertex(float x, float y, float z, float face, float ao, uint32_t textureId) {
        // OFFSET: 128.0f allows coordinates from -128 to +127.
        // This safely covers 0..32 chunk range and -10..0 skirts.
        float ox = x + 128.0f;
        float oy = y + 128.0f;
        float oz = z + 128.0f;

        // SAFETY: Clamp to 0..255 range to prevent integer underflow/overflow
        // which causes the "Sky Skirts" and potentially UB crashes.
        uint32_t ix = (uint32_t)std::clamp(ox, 0.0f, 255.0f);
        uint32_t iy = (uint32_t)std::clamp(oy, 0.0f, 255.0f);
        uint32_t iz = (uint32_t)std::clamp(oz, 0.0f, 255.0f);
        uint32_t iface = (uint32_t)face;
        
        d0 = (ix & 0xFF) | 
             ((iy & 0xFF) << 8) | 
             ((iz & 0xFF) << 16) | 
             ((iface & 0x7) << 24);

        d1 = (textureId & 0xFFFF);
    }
};