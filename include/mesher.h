#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>
#include <cstring>

#include "chunk.h"
#include "packedVertex.h"
#include "linearAllocator.h"

// --- CONFIGURATION ---
// Set to 1 if your chunk data is 34x34x34 (index 1 is block 0).
// Set to 0 if your chunk data is 32x32x32 (index 0 is block 0).
constexpr int PADDING = 1; 

inline uint32_t ctz(uint32_t x) {
#if defined(_MSC_VER)
    return _tzcnt_u32(x);
#else
    return __builtin_ctz(x);
#endif
}

inline bool IsTransparent(uint8_t id) {
    return id == 6 || id == 7; // Water/Glass
}

inline bool IsOpaque(uint8_t id) {
    return id != 0 && !IsTransparent(id);
}

inline void MeshChunk(const Chunk& chunk, 
                      LinearAllocator<PackedVertex>& allocatorOpaque, 
                      LinearAllocator<PackedVertex>& allocatorTrans,
                      bool debug = false) 
{
    // Local Helper to fetch block with padding support.
    // Coordinates x,y,z are ALWAYS 0..CHUNK_SIZE-1 (local chunk space).
    auto GetBlock = [&](int x, int y, int z) -> uint8_t {
        // Safety check to prevent reading garbage if loops overshoot
        if (x < 0 || x >= CHUNK_SIZE || 
            y < 0 || y >= CHUNK_SIZE || 
            z < 0 || z >= CHUNK_SIZE) return 0;

        return chunk.Get(x + PADDING, y + PADDING, z + PADDING);
    };

    // Helper to generate a quad
    auto GreedyPass = [&](uint32_t* colMasks, LinearAllocator<PackedVertex>& targetAllocator, int face, int axis, int direction, int slice) {
        // i iterates the 'row' (Vertical axis of the 2D plane)
        for (int i = 0; i < CHUNK_SIZE; i++) {
            uint32_t mask = colMasks[i];
            
            while (mask != 0) {
                int widthStart = ctz(mask); 
                int widthEnd = widthStart;

                // u = Horizontal Axis (Bit Index)
                // v = Vertical Axis (Loop Index)
                int u = widthStart; 
                int v = i;

                // 2D -> 3D Coordinate Mapping Helper
                auto GetBlockID = [&](int u_chk, int v_chk) {
                    int bx, by, bz;
                    if (axis == 0)      { bx = slice; by = u_chk; bz = v_chk; } // Axis 0 (X-Face): U=Y, V=Z
                    else if (axis == 1) { bx = v_chk; by = slice; bz = u_chk; } // Axis 1 (Y-Face): U=Z, V=X
                    else                { bx = u_chk; by = v_chk; bz = slice; } // Axis 2 (Z-Face): U=X, V=Y
                    return GetBlock(bx, by, bz);
                };

                uint32_t currentTex = GetBlockID(u, v);

                // 1. Compute Width (Horizontal Run)
                // Use 1ULL (64-bit) to prevent overflow if widthEnd == 32
                while (widthEnd < CHUNK_SIZE && (mask & (1ULL << widthEnd))) {
                    if (GetBlockID(widthEnd, v) != currentTex) break;
                    widthEnd++;
                }
                int width = widthEnd - widthStart;

                // Create runMask safely using 64-bit arithmetic to avoid 32-bit shift overflow
                uint32_t runMask = (width >= 32) ? 0xFFFFFFFFu : (uint32_t)(((1ULL << width) - 1ULL) << widthStart);

                // 2. Compute Height (Vertical Run)
                int height = 1;
                for (int j = i + 1; j < CHUNK_SIZE; j++) {
                    uint32_t nextRow = colMasks[j];
                    if ((nextRow & runMask) == runMask) {
                        bool textureMatch = true;
                        // Check texture consistency across the entire width of the next row
                        for (int k = 0; k < width; k++) {
                            if (GetBlockID(widthStart + k, j) != currentTex) {
                                textureMatch = false;
                                break;
                            }
                        }
                        if (textureMatch) {
                            height++;
                            colMasks[j] &= ~runMask;
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                }
                mask &= ~runMask;

                // 3. Generate Quad Vertices
                int w = width;
                int h = height;

                // Lambda to push vertices based on 2D->3D mapping
                auto PushVert = [&](int du, int dv) {
                    float vx, vy, vz;
                    
                    // du applies to u (Horizontal/Width)
                    // dv applies to v (Vertical/Height)
                    int r_u = u + du; 
                    int r_v = v + dv; 

                    if (axis == 0)      { vx = slice; vy = r_u; vz = r_v; } // X-Face: Y comes from Width(u), Z from Height(v)
                    else if (axis == 1) { vx = r_v; vy = slice; vz = r_u; } // Y-Face: X comes from Height(v), Z from Width(u)
                    else                { vx = r_u; vy = r_v; vz = slice; } // Z-Face: X comes from Width(u), Y from Height(v)
                    
                    // Add thickness for positive faces
                    if (direction == 1) {
                        if (axis == 0) vx += 1.0f;
                        if (axis == 1) vy += 1.0f;
                        if (axis == 2) vz += 1.0f;
                    }

                    targetAllocator.Push(PackedVertex(vx, vy, vz, (float)face, 1.0f, currentTex));
                };

                // Winding order
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

    // Arrays allocated on stack. Explicitly zero them to prevent garbage data causing random slices.
    uint32_t colMasksOpaque[CHUNK_SIZE]; 
    uint32_t colMasksTrans[CHUNK_SIZE];

    for (int face = 0; face < 6; face++) {
        int axis = face / 2;
        int direction = (face % 2) == 0 ? 1 : -1;

        // Loop 0..CHUNK_SIZE (Local Space)
        for (int slice = 0; slice < CHUNK_SIZE; slice++) {
            
            // FIX: Explicitly zero out masks every slice to remove garbage data artifacts
            std::memset(colMasksOpaque, 0, sizeof(colMasksOpaque));
            std::memset(colMasksTrans, 0, sizeof(colMasksTrans));

            // Generate Masks
            for (int row = 0; row < CHUNK_SIZE; row++) {
                uint32_t maskOp = 0;
                uint32_t maskTr = 0;
                
                for (int col = 0; col < CHUNK_SIZE; col++) {
                    int x, y, z;
                    
                    if (axis == 0)      { x = slice; y = col; z = row; } 
                    else if (axis == 1) { x = row;   y = slice; z = col; } 
                    else                { x = col;   y = row;   z = slice; } 
                    
                    uint8_t current = GetBlock(x, y, z);
                    if (current == 0) continue; 

                    int nx = x + (axis == 0 ? direction : 0);
                    int ny = y + (axis == 1 ? direction : 0);
                    int nz = z + (axis == 2 ? direction : 0);
                    
                    uint8_t neighbor = chunk.Get(nx + PADDING, ny + PADDING, nz + PADDING);

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

            GreedyPass(colMasksOpaque, allocatorOpaque, face, axis, direction, slice);
            GreedyPass(colMasksTrans, allocatorTrans, face, axis, direction, slice);
        }
    }
}