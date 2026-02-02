#pragma once

#include <iostream>
#include <cstdint>
#include <cmath>
#include <cstring>

#include "chunk.h"
#include "packedVertex.h"
#include "linearAllocator.h"

// --- configuration ---
// padding ensures we don't access out of bounds when checking neighbors
constexpr int PADDING = 1; 

// helper to count trailing zeros (efficient bit scanning)
inline uint32_t count_trailing_zeros(uint32_t x) {
#if defined(_MSC_VER)
    // microsoft compiler intrinsic
    return _tzcnt_u32(x);
#else
    // gcc/clang compiler intrinsic
    return __builtin_ctz(x);
#endif
}

// checks if a block is transparent (water or glass)
inline bool is_transparent(uint8_t id) {
    // id 6 is water, id 7 is glass
    return id == 6 || id == 7;
}

// checks if a block is opaque (solid)
inline bool is_opaque(uint8_t id) {
    // not air (0) and not transparent
    return id != 0 && !is_transparent(id);
}

// main meshing function
inline void MeshChunk(const Chunk& chunk, 
                      LinearAllocator<PackedVertex>& allocator_opaque, 
                      LinearAllocator<PackedVertex>& allocator_transparent,
                      int lod_level = 0) // Changed to accept LOD level
{
    // lambda to get block id safely including padding
    auto get_block = [&](int x, int y, int z) -> uint8_t {
        // check boundaries to prevent crashes
        if (x < 0 || x >= CHUNK_SIZE_PADDED || 
            y < 0 || y >= CHUNK_SIZE_PADDED || 
            z < 0 || z >= CHUNK_SIZE_PADDED) return 0;
        
        // return the block from the chunk array
        return chunk.Get(x, y, z);
    };

    // lambda to resolve texture ids based on block type and face direction
    auto get_texture_id = [&](uint8_t block_id, int face_dir) -> uint32_t {
        
        // example: grass block logic
        if (block_id == 1) {
            // top face gets grass texture
            if (face_dir == 2) return 1;      
            // bottom face gets dirt texture
            if (face_dir == 3) return 2;      
            // side faces get grass_side texture
            return 3;                     
        }

        // example: oak log logic
        if (block_id == 13) {
            // top and bottom get rings
            if (face_dir == 2 || face_dir == 3) return 25; 
            // sides get bark
            return 13;                             
        }

        // default: texture id matches block id
        return block_id;
    };

    // lambda for the greedy meshing pass on a specific face/axis
    auto perform_greedy_pass = [&](uint32_t* face_masks, LinearAllocator<PackedVertex>& target_allocator, int face_idx, int axis_idx, int direction, int slice_idx) {
        
        // helper to map 2d loop coordinates back to 3d chunk coordinates
        auto get_block_id_from_plane = [&](int u_grid, int v_grid) {
            int block_x, block_y, block_z;
            
            // map the 2d grid (u, v) to the correct 3d axis (x, y, z)
            // axis 0 (x-face): u->z, v->y (swapped to fix rotation issues logically)
            if (axis_idx == 0)      { block_x = slice_idx; block_y = v_grid; block_z = u_grid; } 
            // axis 1 (y-face): u->x, v->z
            else if (axis_idx == 1) { block_x = v_grid; block_y = slice_idx; block_z = u_grid; } 
            // axis 2 (z-face): u->x, v->y
            else                    { block_x = u_grid; block_y = v_grid; block_z = slice_idx; } 
            
            // return block using padding offset
            return get_block(block_x + PADDING, block_y + PADDING, block_z + PADDING);
        };

        // iterate over the 'height' of the 2d slice (v axis)
        for (int row_iter = 0; row_iter < CHUNK_SIZE; row_iter++) {
            
            // get the bitmask for this row
            uint32_t current_row_mask = face_masks[row_iter];
            
            // while there are still set bits (blocks) in this row
            while (current_row_mask != 0) {
                
                // find the start of the next run of blocks
                int run_start = count_trailing_zeros(current_row_mask); 
                int run_end = run_start;
                
                // set current position
                int u_pos = run_start; 
                int v_pos = row_iter;
                
                // get the block type at the start of the run
                uint32_t current_block_type = get_block_id_from_plane(u_pos, v_pos);

                // --- step 1: determine run width (horizontal merge) ---
                
                // expand to the right as long as blocks match
                while (run_end < CHUNK_SIZE && (current_row_mask & (1ULL << run_end))) {
                    // if block type changes, stop merging
                    if (get_block_id_from_plane(run_end, v_pos) != current_block_type) break;
                    
                    // --- OPTIMIZATION: SMART WATER FIX ---
                    // ONLY disable greedy meshing if it is Water (6) AND the Top Face (2) AND we are LOD 0.
                    // If LOD > 0, we allow greedy meshing to merge water into huge flat quads.
                    if (current_block_type == 6 && face_idx == 2 && lod_level == 0) {
                        // verify we at least process the first block, then break
                        if (run_end > run_start) break;
                    }
                    // -------------------------------------

                    run_end++;
                }
                
                // calculate total width of the quad
                int quad_width = run_end - run_start;
                
                // create a mask representing the width we just found
                uint32_t run_mask = (quad_width >= 32) ? 0xFFFFFFFFu : (uint32_t)(((1ULL << quad_width) - 1ULL) << run_start);

                // --- step 2: determine run height (vertical merge) ---
                
                int quad_height = 1;
                
                // Check if we should try to merge vertically
                // Same logic: if it's water top-face at LOD 0, DO NOT merge. Otherwise, optimize away!
                if (current_block_type != 6 || face_idx != 2 || lod_level > 0) {
                    
                    // check subsequent rows to see if they match the current run
                    for (int next_row_iter = row_iter + 1; next_row_iter < CHUNK_SIZE; next_row_iter++) {
                        
                        // get mask for the next row
                        uint32_t next_row_bits = face_masks[next_row_iter];
                        
                        // if the next row has the same block pattern in this range
                        if ((next_row_bits & run_mask) == run_mask) {
                            
                            // verify exact block ids match (needed for different blocks with same opacity)
                            bool rows_match = true;
                            for (int k = 0; k < quad_width; k++) {
                                if (get_block_id_from_plane(run_start + k, next_row_iter) != current_block_type) {
                                    rows_match = false;
                                    break;
                                }
                            }
                            
                            // if valid, extend height and clear bits from that row
                            if (rows_match) {
                                quad_height++;
                                face_masks[next_row_iter] &= ~run_mask;
                            } else {
                                // stop if rows don't match
                                break;
                            }
                        } else {
                            // stop if the mask pattern doesn't match
                            break;
                        }
                    }
                } // end vertical merge check

                // remove the processed bits from the current row mask
                current_row_mask &= ~run_mask;

                // --- step 3: generate vertices ---
                
                // get correct texture id for this face
                uint32_t visual_texture_id = get_texture_id(current_block_type, face_idx);

                // helper to push a single vertex
                auto push_vertex_to_buffer = [&](int delta_u, int delta_v) {
                    float final_x, final_y, final_z;
                    
                    // calculate relative coordinates
                    int rel_u = u_pos + delta_u; 
                    int rel_v = v_pos + delta_v; 

                    // map back to 3d space based on axis
                    // axis 0: u->z, v->y
                    if (axis_idx == 0)      { final_x = slice_idx; final_y = rel_v; final_z = rel_u; } 
                    // axis 1: u->x, v->z (swapped because u is usually primary width)
                    else if (axis_idx == 1) { final_x = rel_v; final_y = slice_idx; final_z = rel_u; } 
                    // axis 2: u->x, v->y
                    else                    { final_x = rel_u; final_y = rel_v; final_z = slice_idx; } 
                    
                    // offset by 1.0 if we are on the positive side of the block
                    if (direction == 1) {
                        if (axis_idx == 0) final_x += 1.0f;
                        if (axis_idx == 1) final_y += 1.0f;
                        if (axis_idx == 2) final_z += 1.0f;
                    }

                    // push vertex to allocator
                    target_allocator.Push(PackedVertex(final_x, final_y, final_z, (float)face_idx, 1.0f, visual_texture_id));
                };

                // determine winding order
                // axis 0 (x-face) needs a flip because of the coordinate system change
                bool needs_winding_flip = (axis_idx == 0); 
                bool is_positive_direction = (direction == 1);

                // generate the two triangles for the quad
                if (is_positive_direction != needs_winding_flip) { 
                    // standard counter-clockwise winding
                    push_vertex_to_buffer(0, 0); 
                    push_vertex_to_buffer(quad_width, 0); 
                    push_vertex_to_buffer(quad_width, quad_height);
                    
                    push_vertex_to_buffer(0, 0); 
                    push_vertex_to_buffer(quad_width, quad_height); 
                    push_vertex_to_buffer(0, quad_height);
                } else {
                    // inverted winding
                    push_vertex_to_buffer(0, 0); 
                    push_vertex_to_buffer(quad_width, quad_height); 
                    push_vertex_to_buffer(quad_width, 0);
                    
                    push_vertex_to_buffer(0, 0); 
                    push_vertex_to_buffer(0, quad_height); 
                    push_vertex_to_buffer(quad_width, quad_height);
                }
            }
            
            // update the original mask array since we modified the local variable
            face_masks[row_iter] = current_row_mask;
        }
    };

    // arrays to store binary masks for opaque and transparent faces
    uint32_t masks_opaque[CHUNK_SIZE]; 
    uint32_t masks_transparent[CHUNK_SIZE];

    // iterate over all 6 faces of a cube
    for (int face_idx = 0; face_idx < 6; face_idx++) {
        
        // axis: 0=x, 1=y, 2=z
        int axis_idx = face_idx / 2;
        // direction: 1=positive, -1=negative
        int direction = (face_idx % 2) == 0 ? 1 : -1;

        // iterate through the chunk slices along the current axis
        for (int slice_idx = 0; slice_idx < CHUNK_SIZE; slice_idx++) {
            
            // reset masks for the new slice
            std::memset(masks_opaque, 0, sizeof(masks_opaque));
            std::memset(masks_transparent, 0, sizeof(masks_transparent));

            // build the masks by checking block visibility
            for (int row_iter = 0; row_iter < CHUNK_SIZE; row_iter++) {
                
                uint32_t row_mask_opaque = 0;
                uint32_t row_mask_trans = 0;
                
                for (int col_iter = 0; col_iter < CHUNK_SIZE; col_iter++) {
                    int x, y, z;
                    
                    // map loop indices to 3d coordinates
                    if (axis_idx == 0)      { x = slice_idx; y = row_iter; z = col_iter; } 
                    else if (axis_idx == 1) { x = row_iter;  y = slice_idx; z = col_iter; } 
                    else                    { x = col_iter;  y = row_iter;  z = slice_idx; } 
                    
                    // get current block type
                    uint8_t current_id = get_block(x + PADDING, y + PADDING, z + PADDING);
                    
                    // skip air blocks
                    if (current_id == 0) continue; 

                    // calculate neighbor position
                    int neighbor_x = x + (axis_idx == 0 ? direction : 0);
                    int neighbor_y = y + (axis_idx == 1 ? direction : 0);
                    int neighbor_z = z + (axis_idx == 2 ? direction : 0);
                    
                    // get neighbor block type
                    uint8_t neighbor_id = get_block(neighbor_x + PADDING, neighbor_y + PADDING, neighbor_z + PADDING);

                    // logic for face culling (hiding hidden faces)
                    if (is_opaque(current_id)) {
                        // if neighbor is air or transparent, show this face
                        if (neighbor_id == 0 || is_transparent(neighbor_id)) {
                            row_mask_opaque |= (1u << col_iter);
                        }
                    } 
                    else if (is_transparent(current_id)) {
                        // for transparent blocks, only show if neighbor is air (simplification)
                        // this prevents water faces from rendering inside other water blocks
                        if (neighbor_id == 0) {
                            row_mask_trans |= (1u << col_iter);
                        }
                    }
                }
                
                // store the computed masks
                masks_opaque[row_iter] = row_mask_opaque;
                masks_transparent[row_iter]  = row_mask_trans;
            }

            // run greedy meshing algorithm on the computed masks
            perform_greedy_pass(masks_opaque, allocator_opaque, face_idx, axis_idx, direction, slice_idx);
            perform_greedy_pass(masks_transparent, allocator_transparent, face_idx, axis_idx, direction, slice_idx);
        }
    }
}