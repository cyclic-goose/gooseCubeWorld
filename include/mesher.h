#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>
#include <cstring>

#include "chunk.h"
#include "packedVertex.h"
#include "linearAllocator.h"

// --- configuration ---
constexpr int PADDING = 1; 

// --- Block ID Constants (must match advancedGenerator.h BlockID enum) ---
constexpr uint8_t BLOCK_WATER = 6;
constexpr uint8_t BLOCK_ICE = 8;
constexpr uint8_t BLOCK_OAK_LEAVES = 9;
constexpr uint8_t BLOCK_BIRCH_LEAVES = 21;
constexpr uint8_t BLOCK_JUNGLE_LEAVES = 23;
constexpr uint8_t BLOCK_DARK_OAK_LEAVES = 25;
constexpr uint8_t BLOCK_SPRUCE_LEAVES = 26;
constexpr uint8_t BLOCK_CACTUS = 30;
constexpr uint8_t BLOCK_FLOWER = 36;
constexpr uint8_t BLOCK_GLASS_RED = 37;
constexpr uint8_t BLOCK_GLASS_BLUE = 38;
constexpr uint8_t BLOCK_RIVER_WATER = 49;

// High-bit flag for shader to detect water movement (Bit 8, value 256)
constexpr uint32_t FLAG_ANIMATED_WAVE = 0x100; 

inline uint32_t count_trailing_zeros(uint32_t x) {
#if defined(_MSC_VER)
    return _tzcnt_u32(x);
#else
    return __builtin_ctz(x);
#endif
}

// Determines if a block belongs in the "Transparent" pass (Alpha Blended).
inline bool is_transparent(uint8_t id) {
    return id == BLOCK_WATER || id == BLOCK_GLASS_RED || id == BLOCK_GLASS_BLUE
        || id == BLOCK_RIVER_WATER;
}

// Is this a leaf block? (all leaf variants)
inline bool is_leaves(uint8_t id) {
    return id == BLOCK_OAK_LEAVES || id == BLOCK_BIRCH_LEAVES
        || id == BLOCK_JUNGLE_LEAVES || id == BLOCK_DARK_OAK_LEAVES
        || id == BLOCK_SPRUCE_LEAVES;
}

// Non-full decorative blocks
inline bool is_decoration(uint8_t id) {
    return id == BLOCK_FLOWER || id == BLOCK_CACTUS;
}

// Determines if a block belongs in the "Opaque" pass (includes Cutouts like Leaves).
inline bool is_opaque(uint8_t id) {
    return id != 0 && !is_transparent(id);
}

// Determines if a block fully blocks vision (for culling).
inline bool is_occluding(uint8_t id) {
    if (id == 0) return false;                 // Air
    if (is_leaves(id)) return false;           // All leaf types have holes
    if (is_transparent(id)) return false;      // Glass/Water (See-through)
    if (is_decoration(id)) return false;       // Flowers, cactus
    return true;                               // Stone, Dirt, Wood, etc.
}

// Determines if the block should have vertex displacement (waves/wind) in the shader.
inline bool should_wave(uint8_t id) {
    return id == BLOCK_WATER || id == BLOCK_RIVER_WATER || is_leaves(id) || id == BLOCK_FLOWER;
}

inline void MeshChunk(const Chunk& chunk, 
                      LinearAllocator<PackedVertex>& allocator_opaque, 
                      LinearAllocator<PackedVertex>& allocator_transparent,
                      int lod_level = 0) 
{
    auto get_block = [&](int x, int y, int z) -> uint8_t {
        if (x < 0 || x >= CHUNK_SIZE_PADDED || 
            y < 0 || y >= CHUNK_SIZE_PADDED || 
            z < 0 || z >= CHUNK_SIZE_PADDED) return 0;
        return chunk.Get(x, y, z);
    };

    auto get_texture_id = [&](uint8_t block_id, int face_dir) -> uint32_t {
        uint32_t tex_id = block_id; // Default: 1:1 mapping

        switch (block_id) {
            // Grass: top=1, bottom=2(dirt), sides=3(grass_side)
            case 1: // GRASS_TOP
                if (face_dir == 2)      tex_id = 1;
                else if (face_dir == 3) tex_id = 2;
                else                    tex_id = 3;
                break;
            // Dry Grass (savanna): top=45, bottom=2, sides=2
            case 45: // DRY_GRASS
                if (face_dir == 2)      tex_id = 45;
                else if (face_dir == 3) tex_id = 2;
                else                    tex_id = 2;
                break;
            // Podzol: top=27, bottom=2, sides=2
            case 27: // PODZOL
                if (face_dir == 2)      tex_id = 27;
                else if (face_dir == 3) tex_id = 2;
                else                    tex_id = 2;
                break;
            // Swamp Grass: top=28, bottom=2, sides=2
            case 28: // SWAMP_GRASS
                if (face_dir == 2)      tex_id = 28;
                else if (face_dir == 3) tex_id = 2;
                else                    tex_id = 2;
                break;
            // Snow Block: top=7, bottom=2, sides=47(snow_side)
            case 7: // SNOW_BLOCK
                if (face_dir == 2)      tex_id = 7;
                else if (face_dir == 3) tex_id = 2;
                else                    tex_id = 47;
                break;
            // Oak Log: top/bottom=39(oak_log_top), sides=5
            case 5: // OAK_LOG
                if (face_dir == 2 || face_dir == 3) tex_id = 39;
                else                                tex_id = 5;
                break;
            // Spruce Log: top/bottom=12(spruce_top), sides=11
            case 11: // SPRUCE_LOG
                if (face_dir == 2 || face_dir == 3) tex_id = 12;
                else                                tex_id = 11;
                break;
            // Birch Log: top/bottom=40(birch_log_top), sides=20
            case 20: // BIRCH_LOG
                if (face_dir == 2 || face_dir == 3) tex_id = 40;
                else                                tex_id = 20;
                break;
            // Jungle Log: top/bottom=41, sides=22
            case 22: // JUNGLE_LOG
                if (face_dir == 2 || face_dir == 3) tex_id = 41;
                else                                tex_id = 22;
                break;
            // Dark Oak Log: top/bottom=42, sides=24
            case 24: // DARK_OAK_LOG
                if (face_dir == 2 || face_dir == 3) tex_id = 42;
                else                                tex_id = 24;
                break;
            // Cactus: top/bottom=43(cactus_top), sides=30(cactus_side)
            case 30: // CACTUS
                if (face_dir == 2 || face_dir == 3) tex_id = 43;
                else                                tex_id = 30;
                break;
            // Sandstone: top=44(sandstone_block), sides/bottom=15
            case 15: // SANDSTONE
                if (face_dir == 2) tex_id = 44;
                else               tex_id = 15;
                break;
            default:
                tex_id = block_id;
                break;
        }

        if (should_wave(block_id)) {
            tex_id |= FLAG_ANIMATED_WAVE;
        }

        return tex_id;
    };

    auto perform_greedy_pass = [&](uint32_t* face_masks, LinearAllocator<PackedVertex>& target_allocator, int face_idx, int axis_idx, int direction, int slice_idx) {
        
        auto get_block_id_from_plane = [&](int u_grid, int v_grid) {
            int block_x, block_y, block_z;
            if (axis_idx == 0)      { block_x = slice_idx; block_y = v_grid; block_z = u_grid; } 
            else if (axis_idx == 1) { block_x = v_grid; block_y = slice_idx; block_z = u_grid; } 
            else                    { block_x = u_grid; block_y = v_grid; block_z = slice_idx; } 
            return get_block(block_x + PADDING, block_y + PADDING, block_z + PADDING);
        };

        for (int row_iter = 0; row_iter < CHUNK_SIZE; row_iter++) {
            uint32_t current_row_mask = face_masks[row_iter];
            while (current_row_mask != 0) {
                int run_start = count_trailing_zeros(current_row_mask); 
                int run_end = run_start;
                int u_pos = run_start; 
                int v_pos = row_iter;
                
                uint32_t current_block_type = get_block_id_from_plane(u_pos, v_pos);
                
                // --- Merge Logic ---
                // If it waves (water/leaves), we DO NOT merge at LOD 0.
                // This ensures every leaf block is an individual cube, allowing us to bend them.
                bool is_fluid = should_wave(current_block_type);
                bool can_merge = (!is_fluid) || (lod_level > 0);

                while (run_end < CHUNK_SIZE && (current_row_mask & (1ULL << run_end))) {
                    if (get_block_id_from_plane(run_end, v_pos) != current_block_type) break;
                    if (!can_merge && run_end > run_start) break;
                    run_end++;
                }
                
                int quad_width = run_end - run_start;
                uint32_t run_mask = (quad_width >= 32) ? 0xFFFFFFFFu : (uint32_t)(((1ULL << quad_width) - 1ULL) << run_start);
                int quad_height = 1;
                
                if (can_merge) {
                    for (int next_row_iter = row_iter + 1; next_row_iter < CHUNK_SIZE; next_row_iter++) {
                        uint32_t next_row_bits = face_masks[next_row_iter];
                        if ((next_row_bits & run_mask) == run_mask) {
                            bool rows_match = true;
                            for (int k = 0; k < quad_width; k++) {
                                if (get_block_id_from_plane(run_start + k, next_row_iter) != current_block_type) {
                                    rows_match = false;
                                    break;
                                }
                            }
                            if (rows_match) {
                                quad_height++;
                                face_masks[next_row_iter] &= ~run_mask;
                            } else {
                                break;
                            }
                        } else {
                            break;
                        }
                    }
                }

                current_row_mask &= ~run_mask;
                uint32_t visual_texture_id = get_texture_id(current_block_type, face_idx);

                auto push_vertex_to_buffer = [&](int delta_u, int delta_v) {
                    float final_x, final_y, final_z;
                    int rel_u = u_pos + delta_u; 
                    int rel_v = v_pos + delta_v; 

                    if (axis_idx == 0)      { final_x = slice_idx; final_y = rel_v; final_z = rel_u; } 
                    else if (axis_idx == 1) { final_x = rel_v; final_y = slice_idx; final_z = rel_u; } 
                    else                    { final_x = rel_u; final_y = rel_v; final_z = slice_idx; } 
                    
                    if (direction == 1) {
                        if (axis_idx == 0) final_x += 1.0f;
                        if (axis_idx == 1) final_y += 1.0f;
                        if (axis_idx == 2) final_z += 1.0f;
                    }
                    target_allocator.Push(PackedVertex(final_x, final_y, final_z, (float)face_idx, 1.0f, visual_texture_id));
                };

                bool needs_winding_flip = (axis_idx == 0); 
                bool is_positive_direction = (direction == 1);

                if (is_positive_direction != needs_winding_flip) { 
                    push_vertex_to_buffer(0, 0); 
                    push_vertex_to_buffer(quad_width, 0); 
                    push_vertex_to_buffer(quad_width, quad_height);
                    push_vertex_to_buffer(0, 0); 
                    push_vertex_to_buffer(quad_width, quad_height); 
                    push_vertex_to_buffer(0, quad_height);
                } else {
                    push_vertex_to_buffer(0, 0); 
                    push_vertex_to_buffer(quad_width, quad_height); 
                    push_vertex_to_buffer(quad_width, 0);
                    push_vertex_to_buffer(0, 0); 
                    push_vertex_to_buffer(0, quad_height); 
                    push_vertex_to_buffer(quad_width, quad_height);
                }
            }
            face_masks[row_iter] = current_row_mask;
        }
    };

    uint32_t masks_opaque[CHUNK_SIZE]; 
    uint32_t masks_transparent[CHUNK_SIZE];

    for (int face_idx = 0; face_idx < 6; face_idx++) {
        int axis_idx = face_idx / 2;
        int direction = (face_idx % 2) == 0 ? 1 : -1;

        for (int slice_idx = 0; slice_idx < CHUNK_SIZE; slice_idx++) {
            std::memset(masks_opaque, 0, sizeof(masks_opaque));
            std::memset(masks_transparent, 0, sizeof(masks_transparent));

            for (int row_iter = 0; row_iter < CHUNK_SIZE; row_iter++) {
                uint32_t row_mask_opaque = 0;
                uint32_t row_mask_trans = 0;
                
                for (int col_iter = 0; col_iter < CHUNK_SIZE; col_iter++) {
                    int x, y, z;
                    if (axis_idx == 0)      { x = slice_idx; y = row_iter; z = col_iter; } 
                    else if (axis_idx == 1) { x = row_iter;  y = slice_idx; z = col_iter; } 
                    else                    { x = col_iter;  y = row_iter;  z = slice_idx; } 
                    
                    uint8_t current_id = get_block(x + PADDING, y + PADDING, z + PADDING);
                    if (current_id == 0) continue; 

                    int neighbor_x = x + (axis_idx == 0 ? direction : 0);
                    int neighbor_y = y + (axis_idx == 1 ? direction : 0);
                    int neighbor_z = z + (axis_idx == 2 ? direction : 0);
                    
                    uint8_t neighbor_id = get_block(neighbor_x + PADDING, neighbor_y + PADDING, neighbor_z + PADDING);

                    // --- UPDATED CULLING LOGIC ---
                    if (is_opaque(current_id)) {
                        if (!is_occluding(neighbor_id)) {
                            row_mask_opaque |= (1u << col_iter);
                        }
                    } 
                    else if (is_transparent(current_id)) {
                        bool neighbor_is_self = (neighbor_id == current_id);
                        if (!neighbor_is_self && !is_occluding(neighbor_id)) {
                             row_mask_trans |= (1u << col_iter);
                        }
                    }
                }
                masks_opaque[row_iter] = row_mask_opaque;
                masks_transparent[row_iter]  = row_mask_trans;
            }
            perform_greedy_pass(masks_opaque, allocator_opaque, face_idx, axis_idx, direction, slice_idx);
            perform_greedy_pass(masks_transparent, allocator_transparent, face_idx, axis_idx, direction, slice_idx);
        }
    }
}