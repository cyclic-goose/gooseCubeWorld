#pragma once

// ================================================================================================
// 1. ENGINE CONFIGURATION
// Technical settings only. No terrain generation parameters here.
// ================================================================================================

struct RuntimeConfig {
    int lodCount = 3;                                                                           // starting number, changeable at runtime
    int lodRadius[12] = { 32, 32, 32, 0, 0, 0, 0, 0 , 0, 0, 0, 0};                            // starting number, changeable at runtime
    bool occlusionCulling = true;
    bool enableCaves = false; 

    // Memory & Debug
                                                      
    int worldHeightChunks = 64;                                                                 // World height in chunks, each chunk is 32x32x32 blocks so worldHeightChunks = 64 gives a world height of 32x64 = 2048 blocks
    int cubeDebugMode = 4;
};

struct EngineConfig {

    // IMMUTABLES

    const int VRAM_HEAP_ALLOCATION_MB = 2048;                                                   // Sets how much VRAM the application will allocate

    const int NODE_POOL_GROWTH_STRIDE = 512;                                                    // this is how many nodes (chunk voxel + chunk meta data) the object pool will increase the ram heap by each time it runs out of memory. Meaning if the program needs more ram, it will allocate enough for 512 more nodes
    const int NODE_POOL_INITIAL_SIZE = 1024;                                                    // we can initially hold 1024 chunks (really chunks + chunk meta data) in the world

    const int VOXEL_POOL_GROWTH_STRIDE = 32;                                                    // same as above but for the actual chunk voxel data, We start with a small size and grow slowly because once voxels are uploaded to VRAM, we dont need them in RAM anyways. This is always a transient pool
    const int VOXEL_POOL_INITIAL_SIZE = 1024; 

    const int NODE_GENERATION_LIMIT = 1024;                                                     // plain and simply how many nodes (chunks in most minds) can be in the queue to generate at any moment
    const int NODE_UPLOAD_LIMIT = 256;                                                          // same as above, how many nodes (chunks) can be in the upload queue total. I have found 512 to be a good number for my system. I actually saw lag when increasing this. Something about the generation/mesh/upload pipeline

    const int MAX_TRANSIENT_VOXEL_MESHES;    // transient voxels are basically just nodes marked as either GENERATING, MESHING, or UPLOADING. These are basically the nodes with actual voxel data ready to upload. Want to limit this to limit RAM usage.

    RuntimeConfig settings;

    EngineConfig() :    VRAM_HEAP_ALLOCATION_MB(2048), NODE_POOL_GROWTH_STRIDE(512), NODE_POOL_INITIAL_SIZE(1024),
                        VOXEL_POOL_GROWTH_STRIDE(32), VOXEL_POOL_INITIAL_SIZE(0), NODE_GENERATION_LIMIT(1024),
                        NODE_UPLOAD_LIMIT(512), MAX_TRANSIENT_VOXEL_MESHES(1024*32) {}


};
