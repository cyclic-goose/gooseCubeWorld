#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <queue>
#include <atomic>
#include <chrono>
#include <utility>
#include <fstream>
#include <sstream>
#include <memory> 
#include <cstring> 

#include "chunk.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "packedVertex.h"
// ================================================================================================
//                                    CHUNK DATA STRUCTURES
// The "Chunk Node" is a little more vague than a "Chunk"
// it contains all of the meta data for the chunk as well as the "raw" chunk data
// voxelData is the flattened set of IDs that tell us what each blocks ID is in the chunk
// the cachedMeshes are the actual "renderable" data that is uploaded to the GPU
// currently a chunkNode can either have an opaque or transparent cached mesh, even both at the same time (ocean with shallow land underneath)
// this layout allows us to have a set of "active" chunks but be able to check if they have renderable data yet (hopefully its in the generation queue if it doesnt)
// ================================================================================================


/**
 * @brief Lifecycle state of a single chunk in the world.
 * Used to synchronize state between the main thread and worker threads.
 */
enum class ChunkState { 
    MISSING,    // Metadata exists, but no data generated.
    GENERATING, // Currently being filled with voxels by a worker thread.
    GENERATED,  // Voxel data exists, waiting in queue for meshing.
    MESHING,    // Currently generating geometry (vertices/indices) in a worker thread.
    MESHED,     // Geometry generated, waiting for upload to GPU.
    ACTIVE      // Fully uploaded and potentially visible in the world.
};

/**
 * @brief Metadata node representing a chunk in the world.
 * * This structure acts as the "header" for a chunk. It contains spatial information,
 * flags, and handles to the heavy data (voxel pointers, mesh vectors).
 * It is pooled to avoid fragmentation.
 */
struct ChunkNode {
    Chunk *voxelData = nullptr;     // Pointer to the heavy voxel data (blocks). Null if uniform or not generated.

    // --- Spatial Data ---
    glm::vec3 worldPosition;        // World space coordinate of the chunk's minimum corner.
    int gridX, gridY, gridZ;        // Integer grid coordinates (in chunk units, not blocks).
    int lodLevel;                   // Level of Detail index (0 = highest detail/smallest scale).
    int scaleFactor;                // Multiplier for size (1 << lodLevel). 1, 2, 4, 8, etc.
    
    // --- Mesh Cache (CPU Side) ---
    // These vectors hold vertex data temporarily before uploading to the GPU.
    std::vector<PackedVertex> cachedMeshOpaque; 
    std::vector<PackedVertex> cachedMeshTransparent;

    // --- State & Synchronization ---
    std::atomic<ChunkState> currentState{ChunkState::MISSING}; // Atomic to allow lock-free state checks.
    
    // --- GPU Memory Handles ---
    long long vramOffsetOpaque = -1;       // Byte offset in the global GPU vertex buffer (Opaque).
    long long vramOffsetTransparent = -1;  // Byte offset in the global GPU vertex buffer (Transparent).

    size_t vertexCountOpaque = 0;          // Number of vertices to draw (Opaque).
    size_t vertexCountTransparent = 0;     // Number of vertices to draw (Transparent).

    int64_t uniqueID;                      // Unique 64-bit spatial hash key.

    // --- Bounding Box for Culling ---
    glm::vec3 aabbMinWorld;                // Axis Aligned Bounding Box Min (World Space).
    glm::vec3 aabbMaxWorld;                // Axis Aligned Bounding Box Max (World Space).

    // --- Optimization Flags ---
    bool isUniform = false;                // If true, chunk contains only one block type (e.g., all Air or all Stone).
    uint8_t uniformBlockID = 0;            // The ID of the block if the chunk is uniform.

    /**
     * @brief Resets the node for reuse from the object pool.
     * @param x Grid X coordinate.
     * @param y Grid Y coordinate.
     * @param z Grid Z coordinate.
     * @param level LOD level (0-7 typically).
     */
    void Reset(int x, int y, int z, int level) {
        voxelData = nullptr;

        lodLevel = level;
        scaleFactor = 1 << lodLevel; // Bitwise optimization for pow(2, lod).

        gridX = x; gridY = y; gridZ = z;
        
        float sizeInUnits = (float)(CHUNK_SIZE * scaleFactor);
        worldPosition = glm::vec3(x * sizeInUnits, y * sizeInUnits, z * sizeInUnits);
        
        aabbMinWorld = worldPosition;
        aabbMaxWorld = worldPosition + glm::vec3(sizeInUnits);
        
        currentState = ChunkState::MISSING;
        cachedMeshOpaque.clear();
        cachedMeshTransparent.clear();
        vramOffsetOpaque = -1;
        vramOffsetTransparent = -1;
        vertexCountOpaque = 0;
        vertexCountTransparent = 0;
    }
};

/**
 * @brief Generates a unique 64-bit integer key for a chunk based on position and LOD.
 * * Bit Layout (Total 64 bits):
 * [3 bits: LOD] [20 bits: X] [20 bits: Z] [21 bits: Y]
 * * @param x Chunk Grid X
 * @param y Chunk Grid Y
 * @param z Chunk Grid Z
 * @param lod Level of Detail
 * @return int64_t Unique Hash
 */
inline int64_t ChunkKey(int x, int y, int z, int lod) {
    uint64_t ulod = (uint64_t)(lod & 0x7) << 61;        // Mask LOD to 3 bits, shift to top.
    uint64_t ux = (uint64_t)(uint32_t)x & 0xFFFFF;      // Mask X to 20 bits.
    uint64_t uz = (uint64_t)(uint32_t)z & 0xFFFFF;      // Mask Z to 20 bits.
    uint64_t uy = (uint64_t)(uint32_t)y & 0x1FFFFF;     // Mask Y to 21 bits (height is usually smaller/larger range).
    
    // Combine: LOD | X | Z | Y
    return ulod | (ux << 41) | (uz << 21) | uy;
}