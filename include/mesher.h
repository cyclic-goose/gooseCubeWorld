#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>
#include <immintrin.h> // SIMD

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

// --- BLOCK TYPE HELPERS ---
// Adjust these IDs based on your game's block dictionary
inline bool IsTransparent(uint8_t id) {
    return id == 6 || id == 7; // Example: 6=Water, 7=Glass
}

inline bool IsOpaque(uint8_t id) {
    return id != 0 && !IsTransparent(id);
}

//// BINARY GREEDY MESHER (Updated for Opaque/Trans Split)
inline void MeshChunk(const Chunk& chunk, 
                      LinearAllocator<PackedVertex>& allocatorOpaque, 
                      LinearAllocator<PackedVertex>& allocatorTrans,
                      bool debug = false) 
{
    // Helper to run the Greedy Quad merging algorithm on a prepared mask set
    // UPDATED: Added 'face' to arguments
    auto GreedyPass = [&](uint32_t* colMasks, LinearAllocator<PackedVertex>& targetAllocator, int face, int axis, int direction, int slice) {
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
                    // Strict Merge: Only merge if geometry identical. 
                    // Note: This simple mesher merges based on geometry only. 
                    // Texturing assumes the entire run uses the texture of the bottom-left block.
                    if ((nextRow & runMask) == runMask) {
                        height++;
                        colMasks[j] &= ~runMask;
                    } else {
                        break;
                    }
                }
                mask &= ~runMask;

                int u = widthStart;
                int v = i;
                int w = width;
                int h = height;

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

                    targetAllocator.Push(PackedVertex(vx, vy, vz, (float)face, 1.0f, texID));
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
    };

    for (int face = 0; face < 6; face++) {
        int axis = face / 2;
        int direction = (face % 2) == 0 ? 1 : -1;

        for (int slice = 1; slice <= CHUNK_SIZE; slice++) {
            
            uint32_t colMasksOpaque[32]; 
            uint32_t colMasksTrans[32];

            // 1. Generate Masks
            for (int row = 0; row < 32; row++) {
                uint32_t maskOp = 0;
                uint32_t maskTr = 0;
                
                // Note: Reverted to scalar for clarity and correctness with dual-material logic.
                // AVX can be reintroduced if strict Block ID layout is known.
                for (int col = 0; col < 32; col++) {
                    int x, y, z;
                    if (axis == 1) { x = row + 1; y = slice; z = col + 1; }
                    else           { x = col + 1; y = row + 1; z = slice; }
                    
                    // Current Block
                    uint8_t current = chunk.Get(x, y, z);
                    if (current == 0) continue; // Optimization: Skip if air

                    // Neighbor Block
                    uint8_t neighbor = chunk.Get(x + (axis == 0 ? direction : 0), 
                                                 y + (axis == 1 ? direction : 0), 
                                                 z + (axis == 2 ? direction : 0));

                    // LOGIC:
                    // 1. Opaque draws if Neighbor is Air OR Transparent (Under-water dirt visible)
                    // 2. Transparent draws if Neighbor is Air (Water-against-water is culled)

                    if (IsOpaque(current)) {
                        if (neighbor == 0 || IsTransparent(neighbor)) {
                            maskOp |= (1u << col);
                        }
                    } 
                    else if (IsTransparent(current)) {
                        if (neighbor == 0) {
                            maskTr |= (1u << col);
                        }
                    }
                }
                colMasksOpaque[row] = maskOp;
                colMasksTrans[row]  = maskTr;
            }

            // 2. Run Greedy Pass for Opaque
            // UPDATED: Passing 'face' argument
            GreedyPass(colMasksOpaque, allocatorOpaque, face, axis, direction, slice);
            
            // 3. Run Greedy Pass for Transparent
            // UPDATED: Passing 'face' argument
            GreedyPass(colMasksTrans, allocatorTrans, face, axis, direction, slice);
        }
    }
}