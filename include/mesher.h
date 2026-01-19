#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>

#include "chunk.h"
#include "packedVertex.h"
#include "linearAllocator.h"

// Platform-independent Count Trailing Zeros (CTZ).
// Returns the number of zero bits starting from the least significant bit.
// Used to find the start index of a run of 1s in a bitmask.
inline uint32_t ctz(uint64_t x) {
#if defined(_MSC_VER)
    return _tzcnt_u64(x);
#else
    return __builtin_ctzll(x);
#endif
}

inline void PrintVert(float x, float y, float z, int axis) {
    std::cout << "  V(" << x << ", " << y << ", " << z << ") Face: " << axis << std::endl;
}

/**
 * MeshChunk
 * ---------
 * Converts Voxel Data -> Vertices (Quads).
 * USES: Binary Greedy Meshing.
 * * CONCEPT:
 * 1. For each slice of the chunk, we generate a 32x32 boolean grid of "Face exists here".
 * 2. We compress each ROW of that grid into a single uint32_t (32 bits).
 * 3. We use bitwise math to find runs of 1s (horizontal merging).
 * 4. We compare integers to find matching rows (vertical merging).
 */
inline void MeshChunk(const Chunk& chunk, LinearAllocator<PackedVertex>& allocator, bool debug = false) {
    if (debug) std::cout << "--- Meshing Chunk ---" << std::endl;
    int quadCount = 0;

    // Iterate 6 Faces (Right, Left, Top, Bottom, Front, Back)
    for (int face = 0; face < 6; face++) {
        int axis = face / 2;                 // 0=X, 1=Y, 2=Z
        int direction = (face % 2) == 0 ? 1 : -1; // 1=Positive, -1=Negative direction

        // Sweep through the chunk layers along the current axis
        for (int slice = 1; slice <= CHUNK_SIZE; slice++) {
            
            // --- STEP 1: GENERATE MASKS ---
            // colMasks[row] holds 32 bits representing the 32 columns in that row.
            uint32_t colMasks[32]; 
            
            for (int row = 0; row < 32; row++) {
                uint32_t mask = 0;
                for (int col = 0; col < 32; col++) {
                    // Coordinate Mapping:
                    // Depending on which axis we are sweeping (X, Y, or Z), 
                    // we map "slice, row, col" to real "x, y, z".
                    int x, y, z;
                    if (axis == 0)      { x = slice; y = col + 1; z = row + 1; }
                    else if (axis == 1) { x = row + 1; y = slice; z = col + 1; }
                    else                { x = col + 1; y = row + 1; z = slice; }

                    // Visibility Check:
                    // Face is visible if Current is Solid AND Neighbor is Air.
                    uint8_t current = chunk.Get(x, y, z);
                    uint8_t neighbor = chunk.Get(x + (axis == 0 ? direction : 0), 
                                                 y + (axis == 1 ? direction : 0), 
                                                 z + (axis == 2 ? direction : 0));
                    
                    // Set the N-th bit if visible
                    if (current != 0 && neighbor == 0) mask |= (1u << col);
                }
                colMasks[row] = mask;
            }

            // --- STEP 2: GREEDY MERGING ---
            for (int i = 0; i < 32; i++) {
                uint32_t mask = colMasks[i];
                while (mask != 0) {
                    // Find start of run (Least Significant Bit that is set)
                    int widthStart = ctz(mask); 
                    
                    // Find end of run (First 0 bit after start)
                    int widthEnd = widthStart;
                    while (widthEnd < 32 && (mask & (1u << widthEnd))) widthEnd++;
                    int width = widthEnd - widthStart;

                    // Create a bitmask representing ONLY this run.
                    // Example: width=3, start=2 -> binary 00011100
                    uint32_t runMask;
                    if (width == 32) runMask = 0xFFFFFFFFu; // Prevent overflow shift
                    else runMask = ((1u << width) - 1u) << widthStart;

                    // Check subsequent rows to merge vertically
                    int height = 1;
                    for (int j = i + 1; j < 32; j++) {
                        uint32_t nextRow = colMasks[j];
                        // If next row has the EXACT same bits set in this area...
                        if ((nextRow & runMask) == runMask) {
                            height++;
                            colMasks[j] &= ~runMask; // Clear them so they aren't processed again
                        } else {
                            break; // Stop merging if row doesn't match
                        }
                    }
                    mask &= ~runMask; // Clear bits from current row

                    // --- STEP 3: GENERATE QUAD ---
                    int u = widthStart;
                    int v = i;
                    int w = width;
                    int h = height;
                    quadCount++;

                    // Helper to push vertices
                    auto PushVert = [&](int du, int dv) {
                        float vx, vy, vz;
                        int r_row = v + dv; 
                        int r_col = u + du; 
                        
                        // Map 2D quad coords back to 3D world coords
                        if (axis == 0)      { vx = slice; vy = r_col + 1; vz = r_row + 1; }
                        else if (axis == 1) { vx = r_row + 1; vy = slice; vz = r_col + 1; }
                        else                { vx = r_col + 1; vy = r_row + 1; vz = slice; }
                        
                        // Adjust padding offset
                        vx -= 1; vy -= 1; vz -= 1; 

                        // Offset for Positive Faces (Front/Right/Top)
                        if (direction == 1) {
                            if (axis == 0) vx += 1.0f;
                            if (axis == 1) vy += 1.0f;
                            if (axis == 2) vz += 1.0f;
                        }

                        if (debug) PrintVert(vx, vy, vz, face);
                        
                        // Push to Allocator
                        allocator.Push(PackedVertex(vx, vy, vz, (float)face, 1.0f));
                    };

                    // Winding Order: Ensure Counter-Clockwise (CCW)
                    // If direction is -1, the normal is flipped, so we must flip vertex order
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