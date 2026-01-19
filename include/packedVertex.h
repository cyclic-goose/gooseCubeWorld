#pragma once
#include <cstdint>

/**
 * struct PackedVertex
 * -------------------
 * PURPOSE: 
 * Compresses vertex data to minimize memory bandwidth.
 * Standard vertex: Float Pos(12) + Float Norm(12) + Float UV(8) = 32 bytes.
 * This vertex:     8 bytes total.
 * * MATH & LOGIC:
 * We use bit manipulation to store multiple small values into 32-bit integers.
 * - Position (0-32) needs 6 bits (2^6 = 64).
 * - Normal (0-5) needs 3 bits.
 */
struct PackedVertex {
    // Word 0: Stores X, Y, Z, and Normal/Axis
    uint32_t data1; 
    // Word 1: Stores Texture ID (and potentially lighting/AO in the future)
    uint32_t data2;

    // Default constructor
    PackedVertex() : data1(0), data2(0) {}

    // Constructor with packing logic
    // x,y,z: Coordinates within the chunk (0-32)
    // axisOrNormal: The face index (0-5)
    // textureID: The block texture index
    PackedVertex(float x, float y, float z, float axisOrNormal, float textureID) {
        // ---------------------------------------------------------
        // PACKING WORD 0 (data1)
        // ---------------------------------------------------------
        // Masking with 0x3F (binary 111111) ensures we only take the bottom 6 bits.
        // This effectively handles the range [0, 63].
        uint32_t ix = (uint32_t)x & 0x3F; 
        uint32_t iy = (uint32_t)y & 0x3F; 
        uint32_t iz = (uint32_t)z & 0x3F; 
        
        // Masking with 0x07 (binary 111) takes bottom 3 bits. Range [0, 7].
        uint32_t ia = (uint32_t)axisOrNormal & 0x07; 
        
        // Bitwise OR (|) combines them.
        // Bitwise Shift (<<) moves them to their slot.
        // Format: [Axis: 3 bits] [Z: 6 bits] [Y: 6 bits] [X: 6 bits]
        data1 = ix | (iy << 6) | (iz << 12) | (ia << 18);
        
        // ---------------------------------------------------------
        // PACKING WORD 1 (data2)
        // ---------------------------------------------------------
        // Masking with 0xFFFF takes bottom 16 bits. Range [0, 65535].
        data2 = (uint32_t)textureID & 0xFFFF;
    }
};