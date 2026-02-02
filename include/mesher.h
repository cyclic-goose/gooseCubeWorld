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

inline uint32_t count_trailing_zeros(uint32_t x) {
#if defined(_MSC_VER)
    return _tzcnt_u32(x);
#else
    return __builtin_ctz(x);
#endif
}

inline bool is_transparent(uint8_t id) {
    // FIX: Only Water (6) should be in the transparent bucket.
    // Leaves (9) and Ice (8) are moved to Opaque to prevent them 
    // from being rendered with water shaders/effects and to ensure 
    // proper culling of blocks underneath them (like Stone under Ice).
    return id == 6; 
}

inline bool is_opaque(uint8_t id) {
    return id != 0 && !is_transparent(id);
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
        // face_dir: 2 = Top, 3 = Bottom
        
        // Grass Block (ID 1)
        if (block_id == 1) {
            if (face_dir == 2) return 1;      
            if (face_dir == 3) return 2;      
            return 3;                     
        }

        // Oak Log (ID 5)
        if (block_id == 5) {
            if (face_dir == 2 || face_dir == 3) return 5; 
            return 11;                             
        }
        
        // Dark Oak Log (ID 11)
        if (block_id == 11) {
            if (face_dir == 2 || face_dir == 3) return 12;
            return 11;
        }

        return block_id;
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
                
                // Water (6) doesn't merge at LOD 0 to allow shader displacement.
                // Leaves/Ice are now opaque, so they will merge (good for performance).
                bool can_merge = (current_block_type != 6) || (lod_level > 0);

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

                    if (is_opaque(current_id)) {
                        // Opaque blocks render if neighbor is Air or Transparent (Water)
                        if (neighbor_id == 0 || is_transparent(neighbor_id)) {
                            row_mask_opaque |= (1u << col_iter);
                        }
                    } 
                    else if (is_transparent(current_id)) {
                        // Transparent (Water only now)
                        // Render if neighbor is NOT Water (Air or Solid or different transparent)
                        if (current_id == 6) {
                            if (neighbor_id == 0 || (is_transparent(neighbor_id) && neighbor_id != 6)) {
                                row_mask_trans |= (1u << col_iter);
                            }
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