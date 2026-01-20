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

// NOTE: Skirt generation removed. Stacked LODs render geometry underneath, filling gaps.

inline void MeshChunk(const Chunk& chunk, LinearAllocator<PackedVertex>& allocator, bool debug = false) {
    int quadCount = 0;

    for (int face = 0; face < 6; face++) {
        int axis = face / 2;
        int direction = (face % 2) == 0 ? 1 : -1;

        for (int slice = 1; slice <= CHUNK_SIZE; slice++) {
            
            uint32_t colMasks[32]; 

            for (int row = 0; row < 32; row++) {
                // AVX2 OPTIMIZATION (Axis 0 Only)
                if (axis == 0) { 
                    const int P = CHUNK_SIZE_PADDED;
                    const int P2 = P * P;

                    int c_idx = slice * P2 + (row + 1) * P + 1;
                    int n_idx = (slice + direction) * P2 + (row + 1) * P + 1;
                    
                    const uint8_t* pCurrentRow = &chunk.voxels[c_idx];
                    const uint8_t* pNeighborRow = &chunk.voxels[n_idx];

                    __m256i vCurrent = _mm256_loadu_si256((const __m256i*)pCurrentRow);
                    __m256i vNeighbor = _mm256_loadu_si256((const __m256i*)pNeighborRow);
                    
                    __m256i vZero = _mm256_setzero_si256();
                    __m256i vCurrIsZero = _mm256_cmpeq_epi8(vCurrent, vZero);
                    __m256i vNeighIsZero = _mm256_cmpeq_epi8(vNeighbor, vZero);
                    
                    __m256i vResult = _mm256_andnot_si256(vCurrIsZero, vNeighIsZero);
                    colMasks[row] = (uint32_t)_mm256_movemask_epi8(vResult);
                } 
                else {
                    // OPTIMIZED SCALAR FALLBACK
                    uint32_t mask = 0;
                    
                    // Precompute pointers for speed if possible, or just raw access
                    // Axis 1: y=slice. x=row+1. z=col+1.
                    // Axis 2: z=slice. y=row+1. x=col+1.
                    
                    for (int col = 0; col < 32; col++) {
                        int x, y, z;
                        if (axis == 1) { x = row + 1; y = slice; z = col + 1; }
                        else           { x = col + 1; y = row + 1; z = slice; }
                        
                        // Raw access is safe here because loops are strictly 0..31 and Padded size is 34.
                        // However, chunk.Get() is inlined and simple. 
                        // To be safer, we stick to Get() but rely on compiler inlining.
                        
                        uint8_t current = chunk.Get(x, y, z);
                        uint8_t neighbor = chunk.Get(x + (axis == 0 ? direction : 0), 
                                                     y + (axis == 1 ? direction : 0), 
                                                     z + (axis == 2 ? direction : 0));
                        if (current != 0 && neighbor == 0) mask |= (1u << col);
                    }
                    colMasks[row] = mask;
                }
            }

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
}