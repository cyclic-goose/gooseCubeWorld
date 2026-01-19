#pragma once
#include <cstdint> 

// our new packed vertex struct fits the data more compactly, 
//x (6 bits), y (6 bits), z (6 bits), normal (3 bits), texture (11 bits)
struct PackedVertex {
    uint32_t data;
    // constructor to pack data on init
    PackedVertex(uint32_t x, uint32_t y, uint32_t z, uint32_t norm, uint32_t tex) {
        data = 0;
        data |= (x & 0x3f) << 0; // mask 6 bits, shift 0
        data |= (y & 0x3f) << 6; // mask 6 bits, shift 6
        data |= (z & 0x3f) << 12; // mask 6 bits, shift 12
        data |= (norm & 0x7) << 18;// mask 3 bits, shift 18
        data |= (tex & 0x7ff) << 21; // mask 11 bits, shift 21
    }
};
