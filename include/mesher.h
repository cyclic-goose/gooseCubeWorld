#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>
#include <cstring>

#include "chunk.h"
#include "packedVertex.h"
#include "linearAllocator.h"

// --- CONFIGURATION ---
constexpr int PADDING = 1; 

inline uint32_t ctz(uint32_t x) {
#if defined(_MSC_VER)
    return _tzcnt_u32(x);
#else
    return __builtin_ctz(x);
#endif
}

inline bool IsTransparent(uint8_t id) {
    // SHOULD add leaves (14=Oak, 16=Pine) to transparent list.
    // but i need to work on how its handled for occlusion, i really need to fix AABB tighening system because right now system sees entire tree as a 32x32x32 occluder which creates many false positive
    return id == 6 || id == 7;
}

inline bool IsOpaque(uint8_t id) {
    return id != 0 && !IsTransparent(id);
}

inline void MeshChunk(const Chunk& chunk, 
                      LinearAllocator<PackedVertex>& allocatorOpaque, 
                      LinearAllocator<PackedVertex>& allocatorTrans,
                      bool debug = false) 
{
    // Helper to safely get block from chunk including padding.
    // Returns 0 (Air) if the padding index is out of valid bounds or uninitialized assumption.
    auto GetBlock = [&](int x, int y, int z) -> uint8_t {
        if (x < 0 || x >= CHUNK_SIZE_PADDED || 
            y < 0 || y >= CHUNK_SIZE_PADDED || 
            z < 0 || z >= CHUNK_SIZE_PADDED) return 0;
        return chunk.Get(x, y, z);
    };

    // --- TEXTURE MAPPING LOGIC ---
    // Maps (Block ID + Face Direction) -> Texture Layer ID
    // Face Order: 0=+X (Right), 1=-X (Left), 2=+Y (Top), 3=-Y (Bottom), 4=+Z (Front), 5=-Z (Back)
    auto GetTextureID = [&](uint8_t blockID, int face) -> uint32_t {
        
        // Example: Grass Block (ID 1)
        if (blockID == 1) {
            if (face == 2) return 1;      // Top: Green Grass (Texture ID 1)
            if (face == 3) return 2;      // Bottom: Dirt (Texture ID 2)
            return 3;                     // Sides: Grass Side (Texture ID 3)
        }

        // Example: Oak Log (ID 13)
        if (blockID == 13) {
            if (face == 2 || face == 3) return 25; // Top/Bottom: Log Rings (Example ID)
            return 13;                             // Sides: Log Bark (Example ID)
        }

        // Default: If no special case, the Texture ID is the same as the Block ID
        return blockID;
    };

    auto GreedyPass = [&](uint32_t* colMasks, LinearAllocator<PackedVertex>& targetAllocator, int face, int axis, int direction, int slice) {
        // 2D -> 3D Coordinate Mapping Helper
        auto GetBlockID = [&](int u_chk, int v_chk) {
            int bx, by, bz;
            if (axis == 0)      { bx = slice; by = u_chk; bz = v_chk; } 
            else if (axis == 1) { bx = v_chk; by = slice; bz = u_chk; } 
            else                { bx = u_chk; by = v_chk; bz = slice; } 
            // Note: passing PADDING here because GetBlockID is working in local 0..31 space
            return GetBlock(bx + PADDING, by + PADDING, bz + PADDING);
        };

        // i iterates the 'row' (Vertical axis of the 2D plane)
        for (int i = 0; i < CHUNK_SIZE; i++) {
            uint32_t mask = colMasks[i];
            
            while (mask != 0) {
                int widthStart = ctz(mask); 
                int widthEnd = widthStart;
                int u = widthStart; 
                int v = i;
                
                uint32_t currentBlock = GetBlockID(u, v);

                // 1. Compute Width
                while (widthEnd < CHUNK_SIZE && (mask & (1ULL << widthEnd))) {
                    if (GetBlockID(widthEnd, v) != currentBlock) break;
                    widthEnd++;
                }
                int width = widthEnd - widthStart;
                
                uint32_t runMask = (width >= 32) ? 0xFFFFFFFFu : (uint32_t)(((1ULL << width) - 1ULL) << widthStart);

                // 2. Compute Height
                int height = 1;
                for (int j = i + 1; j < CHUNK_SIZE; j++) {
                    uint32_t nextRow = colMasks[j];
                    if ((nextRow & runMask) == runMask) {
                        bool textureMatch = true;
                        for (int k = 0; k < width; k++) {
                            if (GetBlockID(widthStart + k, j) != currentBlock) {
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

                // Determine the correct visual Texture ID for this face
                uint32_t visualTexID = GetTextureID(currentBlock, face);

                auto PushVert = [&](int du, int dv) {
                    float vx, vy, vz;
                    int r_u = u + du; 
                    int r_v = v + dv; 

                    if (axis == 0)      { vx = slice; vy = r_u; vz = r_v; } 
                    else if (axis == 1) { vx = r_v; vy = slice; vz = r_u; } 
                    else                { vx = r_u; vy = r_v; vz = slice; } 
                    
                    if (direction == 1) {
                        if (axis == 0) vx += 1.0f;
                        if (axis == 1) vy += 1.0f;
                        if (axis == 2) vz += 1.0f;
                    }

                    // Use the resolved visual Texture ID here
                    targetAllocator.Push(PackedVertex(vx, vy, vz, (float)face, 1.0f, visualTexID));
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

    uint32_t colMasksOpaque[CHUNK_SIZE]; 
    uint32_t colMasksTrans[CHUNK_SIZE];

    for (int face = 0; face < 6; face++) {
        int axis = face / 2;
        int direction = (face % 2) == 0 ? 1 : -1;

        for (int slice = 0; slice < CHUNK_SIZE; slice++) {
            
            std::memset(colMasksOpaque, 0, sizeof(colMasksOpaque));
            std::memset(colMasksTrans, 0, sizeof(colMasksTrans));

            for (int row = 0; row < CHUNK_SIZE; row++) {
                uint32_t maskOp = 0;
                uint32_t maskTr = 0;
                
                for (int col = 0; col < CHUNK_SIZE; col++) {
                    int x, y, z;
                    
                    if (axis == 0)      { x = slice; y = col; z = row; } 
                    else if (axis == 1) { x = row;   y = slice; z = col; } 
                    else                { x = col;   y = row;   z = slice; } 
                    
                    // Use PADDING here for lookups
                    uint8_t current = GetBlock(x + PADDING, y + PADDING, z + PADDING);
                    if (current == 0) continue; 

                    int nx = x + (axis == 0 ? direction : 0);
                    int ny = y + (axis == 1 ? direction : 0);
                    int nz = z + (axis == 2 ? direction : 0);
                    
                    // Safer neighbor check that doesn't trust uninitialized padding memory
                    uint8_t neighbor = GetBlock(nx + PADDING, ny + PADDING, nz + PADDING);

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