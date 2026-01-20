#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>

#include "chunk.h"
#include "packedVertex.h"
#include "linearAllocator.h"

inline uint32_t ctz(uint64_t x) {
#if defined(_MSC_VER)
    return _tzcnt_u64(x);
#else
    return __builtin_ctzll(x);
#endif
}

inline void GenerateSkirts(const Chunk& chunk, LinearAllocator<PackedVertex>& allocator) {
    // Skirt Length: 2 units (in local coord). 
    float skirtLen = 2.0f; 

    auto PushSkirtQuad = [&](float x, float y, float z, int axis, int dir, uint32_t texID) {
        float wx = (axis == 2) ? 1.0f : 0.0f;
        float wz = (axis == 0) ? 1.0f : 0.0f;
        
        float face = 0.0f; 
        if (axis == 0) face = (dir == 1) ? 0.0f : 1.0f;
        if (axis == 2) face = (dir == 1) ? 4.0f : 5.0f;

        float px = x - 1.0f; 
        float py = y - 1.0f; 
        float pz = z - 1.0f;
        
        if (axis == 0 && dir == 1) px += 1.0f;
        if (axis == 2 && dir == 1) pz += 1.0f;

        // FIXED WINDING ORDER for Backface Culling
        // Vertices must be CCW when viewing from the FRONT of the face.
        
        if (dir == 1) {
            // Triangle 1: TL -> BL -> TR
            allocator.Push(PackedVertex(px, py, pz, face, 0.5f, texID)); // TL
            allocator.Push(PackedVertex(px, py - skirtLen, pz, face, 0.5f, texID)); // BL
            allocator.Push(PackedVertex(px + wx, py, pz + wz, face, 0.5f, texID)); // TR
            
            // Triangle 2: BL -> BR -> TR
            allocator.Push(PackedVertex(px, py - skirtLen, pz, face, 0.5f, texID)); // BL
            allocator.Push(PackedVertex(px + wx, py - skirtLen, pz + wz, face, 0.5f, texID)); // BR
            allocator.Push(PackedVertex(px + wx, py, pz + wz, face, 0.5f, texID)); // TR

        } else {
            // Negative Face (Points towards -X or -Z)
            // T1: TL -> TR -> BL
            allocator.Push(PackedVertex(px, py, pz, face, 0.5f, texID)); // TL
            allocator.Push(PackedVertex(px + wx, py, pz + wz, face, 0.5f, texID)); // TR
            allocator.Push(PackedVertex(px, py - skirtLen, pz, face, 0.5f, texID)); // BL
            
            // T2: BL -> TR -> BR
            allocator.Push(PackedVertex(px, py - skirtLen, pz, face, 0.5f, texID)); // BL
            allocator.Push(PackedVertex(px + wx, py, pz + wz, face, 0.5f, texID)); // TR
            allocator.Push(PackedVertex(px + wx, py - skirtLen, pz + wz, face, 0.5f, texID)); // BR
        }
    };

    for (int x = 1; x <= CHUNK_SIZE; x++) {
        // Back Edge (z=1) -> Dir -1
        {
            int y = 31;
            while (y > 0 && chunk.Get(x, y, 1) == 0) y--;
            if (y > 0) PushSkirtQuad(x, y, 1, 2, -1, chunk.Get(x, y, 1));
        }
        // Front Edge (z=32) -> Dir 1
        {
            int y = 31;
            while (y > 0 && chunk.Get(x, y, 32) == 0) y--;
            if (y > 0) PushSkirtQuad(x, y, 32, 2, 1, chunk.Get(x, y, 32));
        }
    }

    for (int z = 1; z <= CHUNK_SIZE; z++) {
        // Left Edge (x=1) -> Dir -1
        {
            int y = 31;
            while (y > 0 && chunk.Get(1, y, z) == 0) y--;
            if (y > 0) PushSkirtQuad(1, y, z, 0, -1, chunk.Get(1, y, z));
        }
        // Right Edge (x=32) -> Dir 1
        {
            int y = 31;
            while (y > 0 && chunk.Get(32, y, z) == 0) y--;
            if (y > 0) PushSkirtQuad(32, y, z, 0, 1, chunk.Get(32, y, z));
        }
    }
}

inline void MeshChunk(const Chunk& chunk, LinearAllocator<PackedVertex>& allocator, int scale, bool debug = false) {
    int quadCount = 0;

    for (int face = 0; face < 6; face++) {
        int axis = face / 2;
        int direction = (face % 2) == 0 ? 1 : -1;

        for (int slice = 1; slice <= CHUNK_SIZE; slice++) {
            
            // 1. Generate Binary Masks
            uint32_t colMasks[32]; 
            for (int row = 0; row < 32; row++) {
                uint32_t mask = 0;
                for (int col = 0; col < 32; col++) {
                    int x, y, z;
                    if (axis == 0)      { x = slice; y = col + 1; z = row + 1; }
                    else if (axis == 1) { x = row + 1; y = slice; z = col + 1; }
                    else                { x = col + 1; y = row + 1; z = slice; }

                    uint8_t current = chunk.Get(x, y, z);
                    uint8_t neighbor = chunk.Get(x + (axis == 0 ? direction : 0), 
                                                 y + (axis == 1 ? direction : 0), 
                                                 z + (axis == 2 ? direction : 0));
                    
                    if (current != 0 && neighbor == 0) mask |= (1u << col);
                }
                colMasks[row] = mask;
            }

            // 2. Greedy Merging
            for (int i = 0; i < 32; i++) {
                uint32_t mask = colMasks[i];
                while (mask != 0) {
                    int widthStart = ctz(mask); 
                    int widthEnd = widthStart;
                    while (widthEnd < 32 && (mask & (1u << widthEnd))) widthEnd++;
                    int width = widthEnd - widthStart;

                    uint32_t runMask;
                    if (width == 32) runMask = 0xFFFFFFFFu;
                    else runMask = ((1u << width) - 1u) << widthStart;

                    int height = 1;
                    for (int j = i + 1; j < 32; j++) {
                        uint32_t nextRow = colMasks[j];
                        if ((nextRow & runMask) == runMask) {
                            height++;
                            colMasks[j] &= ~runMask;
                        } else {
                            break;
                        }
                    }
                    mask &= ~runMask;

                    // 3. Generate Quads
                    int u = widthStart;
                    int v = i;
                    int w = width;
                    int h = height;
                    quadCount++;

                    int bx, by, bz;
                    if (axis == 0)      { bx = slice; by = u + 1; bz = v + 1; }
                    else if (axis == 1) { bx = v + 1; by = slice; bz = u + 1; }
                    else                { bx = u + 1; by = v + 1; bz = slice; }
                    
                    uint32_t texID = chunk.Get(bx, by, bz);

                    auto PushVert = [&](int du, int dv) {
                        float vx, vy, vz;
                        int r_row = v + dv; 
                        int r_col = u + du; 
                        
                        if (axis == 0)      { vx = slice; vy = r_col + 1; vz = r_row + 1; }
                        else if (axis == 1) { vx = r_row + 1; vy = slice; vz = r_col + 1; }
                        else                { vx = r_col + 1; vy = r_row + 1; vz = slice; }
                        
                        vx -= 1; vy -= 1; vz -= 1; 

                        if (direction == 1) {
                            if (axis == 0) vx += 1.0f;
                            if (axis == 1) vy += 1.0f;
                            if (axis == 2) vz += 1.0f;
                        }

                        allocator.Push(PackedVertex(vx, vy, vz, (float)face, 1.0f, texID));
                    };

                    if (direction == 1) {
                        PushVert(0, 0); PushVert(w, 0); PushVert(w, h);
                        PushVert(0, 0); PushVert(w, h); PushVert(0, h);
                    } else {
                        PushVert(0, 0); PushVert(w, h); PushVert(w, 0);
                        PushVert(0, 0); PushVert(0, h); PushVert(w, h);
                    }
                }
            }
        }
    }

    GenerateSkirts(chunk, allocator);
}