#pragma once

// ================================================================================================
// 1. ENGINE CONFIGURATION
// Technical settings only. No terrain generation parameters here.
// ================================================================================================

struct RuntimeConfig {
    int lodCount = 4;                                           
    int lodRadius[12] = { 15, 15, 15, 15, 0, 0, 0, 0 , 0, 0, 0, 0};
    bool occlusionCulling = true;
    bool enableCaves = false; 

    // Memory & Debug                                           
    int worldHeightChunks = 64;                                 
    int cubeDebugMode = 4;
};

// Struct for actual memory pools (Node/Voxel data)
struct PoolConfig {
    size_t growthStride;    // How many items to add when empty
    size_t initialSize;     // How many items to start with
    size_t limit;           // Hard limit (0 = infinite)

    PoolConfig(size_t growth, size_t initial, size_t cap) 
        : growthStride(growth), initialSize(initial), limit(cap) {}
};

struct EngineConfig {

    // -- READABILITY HELPERS --
    
    // 1. For Memory Sizes (VRAM, Buffers) - Returns Total Bytes
    static constexpr size_t Bytes_MB(size_t megabytes) { return megabytes * 1024 * 1024; }
    
    // 2. For Object Pools - Returns ITEM COUNT
    // Use these for Pool Init/Limits. Do not use Bytes_MB for pools!
    static constexpr size_t Items_K(size_t thousands) { return thousands * 1024; }
    static constexpr size_t Items_M(size_t millions) { return millions * 1024 * 1024; }

    // -- IMMUTABLES --

    // General Memory
    const int VRAM_HEAP_ALLOCATION_MB;                          

    // Actual Pool Allocations
    const PoolConfig nodePool;
    const PoolConfig voxelPool;

    // Workload Limits & Caps
    const int NODE_GENERATION_LIMIT;                            
    const int NODE_UPLOAD_LIMIT;
    const int MAX_TRANSIENT_VOXEL_MESHES; 

    RuntimeConfig settings;

    EngineConfig() : 
        VRAM_HEAP_ALLOCATION_MB(1024),
        
        // Node Pool (Chunk Metadata)
        // Stride: 512 items, Initial: 64k items, Limit: Infinite
        // ChunkMetadata is ~168 bytes, 64k items = ~10.5 MB
        nodePool(512, Items_K(16), 0), // this means start with size that will allow 16 thousand chunks, grow by 512 chunks if more is needed



        // Voxel Data Pool (Transient raw voxel data)
        // Stride: 256k items, Initial: 1M items, Limit: 4M items
        // (If VoxelData is 1 byte, 1M items = 1 MB.


        //34×34×34=39,304 bytes per Chunk.
        //1 Chunk ≈ 39 KB.
        voxelPool(Items_K(1), Items_K(10), Items_K(30)), //start with enough memory for 1 million voxels, grow by 16 thousand if more is needed, never go beyond enough for 4 million voxels

        // 3. Limits
        NODE_GENERATION_LIMIT(2048),
        NODE_UPLOAD_LIMIT(512),
        MAX_TRANSIENT_VOXEL_MESHES(Items_K(32)) // 32k limit
    {}
};