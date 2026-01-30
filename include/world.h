#pragma once

// ================================================================================================
//                                       INCLUDES
// ================================================================================================

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

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// Engine Subsystems
#include "chunk.h"
#include "mesher.h"
#include "linearAllocator.h"
#include "shader.h"
#include "threadpool.h"
#include "object_pool.h"
#include "gpu_memory.h"
#include "packedVertex.h"
#include "profiler.h"
#include "gpu_culler.h"
#include "screen_quad.h"
#include "terrain/terrain_system.h"
#include "engine_config.h"

// ================================================================================================
//                                    DATA STRUCTURES
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

// ================================================================================================
//                                          WORLD CLASS
// ================================================================================================

/**
 * @brief Main manager class for the Voxel World.
 * * Responsibilities:
 * 1. Managing the lifecycle of chunks (Generation -> Meshing -> Upload -> Unload).
 * 2. Maintaining the Octree/LOD structure around the camera.
 * 3. Interfacing with GPU memory managers and Cullers.
 * 4. Dispatching tasks to the ThreadPool.
 */
class World {
private:
    // --- Configuration & Generation ---
    std::unique_ptr<EngineConfig> m_config;       // Global engine settings.
    std::unique_ptr<ITerrainGenerator> m_terrainGenerator; // Abstract interface for procedural terrain logic.
    
    // --- Chunk Management ---
    std::unordered_map<int64_t, ChunkNode*> m_activeChunkMap; // Lookup for all currently tracked chunks.
    std::shared_mutex m_chunkMapMutex;            // R/W lock for the chunk map (Read heavily by LOD thread, Written by Main thread).
    
    ObjectPool<ChunkNode> m_chunkMetadataPool;    // Memory pool for lightweight ChunkNodes.
    ObjectPool<Chunk> m_voxelDataPool;            // Memory pool for heavy Chunk (voxel) data.

    // --- Processing Queues ---
    std::queue<ChunkNode*> m_queueGeneratedChunks; // Chunks with data ready to be meshed.
    std::queue<ChunkNode*> m_queueMeshedChunks;    // Chunks with meshes ready to be uploaded to GPU.
    
    std::mutex m_queueMutex;                      // Protects access to the queues above.
    ThreadPool m_workerThreadPool;                // Worker threads for generation and meshing.

    // --- LOD System Types ---
    struct ChunkLoadRequest { 
        int x, y, z; 
        int lod; 
        int distSq; // Used for sorting priority.
    };

    struct LODUpdateResult {
        std::vector<ChunkLoadRequest> chunksToLoad;
        std::vector<int64_t> chunksToUnload;
        size_t loadIndex = 0; // Tracks progress through the load vector across frames.
    };
    
    // --- LOD Thread State ---
    std::atomic<bool> m_isLODWorkerRunning { false }; // Prevents scheduling multiple LOD calculations.
    std::mutex m_lodResultMutex;                      // Protects the pending result pointer.
    std::unique_ptr<LODUpdateResult> m_pendingLODResult = nullptr; // Result from the async thread waiting to be applied.
    glm::vec3 m_lastLODCalculationPos = glm::vec3(-9999.0f); // Camera position during last LOD calculation.

    // --- Control State ---
    int m_frameCounter = 0; 
    std::atomic<bool> m_isShuttingDown{false};
    bool m_freezeLODUpdates = false; // Debug flag to pause LOD updates.

    // --- GPU Subsystems ---
    std::unique_ptr<GpuMemoryManager> m_vramManager; // Manages the massive bindless SSBO for geometry.
    std::unique_ptr<GpuCuller> m_gpuOcclusionCuller; // Handles GPU-side frustum and occlusion culling.
    GLuint m_dummyVAO = 0;                           // Empty VAO for index-less rendering.
    GLuint m_textureArrayID = 0;                     // Handle to the block texture array.
    
    std::atomic<int> m_activeWorkerTaskCount{0};     // Number of tasks currently running on the thread pool.

    // Allow UI to inspect private members
    friend class ImGuiManager;

public:
    /**
     * @brief Construct a new World object.
     * @param config Engine settings.
     * @param generator Terrain generation strategy implementation.
     */
    World(EngineConfig config, std::unique_ptr<ITerrainGenerator> generator) 
        : m_terrainGenerator(std::move(generator)) {
        
        m_config = std::make_unique<EngineConfig>(config);

        // -- Calculate Pool Size --
        // Estimate the steady-state number of chunks based on LOD radii.
        size_t steadyStateNodes = 0;
        for(int i = 0; i < m_config->settings.lodCount; i++) {
            int r = m_config->settings.lodRadius[i];
            // Volume approximation: (Width * Depth) * Height
            steadyStateNodes += (size_t)(r * 2 + 1) * (r * 2 + 1) * m_config->settings.worldHeightChunks;
        }
        
        // Add 20% buffer for transition states
        size_t nodeCapacity = steadyStateNodes + (steadyStateNodes / 5); 
        std::cout << "[World] Estimated Node Capacity: " << nodeCapacity << std::endl;

        // -- Initialize Pools --
        m_chunkMetadataPool.Init(m_config->NODE_POOL_GROWTH_STRIDE, m_config->NODE_POOL_INITIAL_SIZE, nodeCapacity, static_cast<uint8_t>(0)); 
        // Voxel pool is smaller as we only hold voxels during generation/meshing, then discard them (unless modifying).
        m_voxelDataPool.Init(m_config->VOXEL_POOL_GROWTH_STRIDE, m_config->VOXEL_POOL_INITIAL_SIZE, m_config->MAX_TRANSIENT_VOXEL_MESHES, static_cast<uint8_t>(1)); 

        // -- Initialize GPU Systems --
        m_vramManager = std::make_unique<GpuMemoryManager>(static_cast<size_t>(m_config->VRAM_HEAP_ALLOCATION_MB) * 1024 * 1024);
        m_gpuOcclusionCuller = std::make_unique<GpuCuller>(nodeCapacity);
        
        glCreateVertexArrays(1, &m_dummyVAO);
    }

    ~World() { Dispose(); }
    
    /**
     * @brief Clean up resources and ensure threads stop before destruction.
     */
    void Dispose() {
        m_isShuttingDown = true;
        // Spin-wait until all worker threads finish their current tasks.
        while(m_activeWorkerTaskCount > 0) { std::this_thread::yield(); }
        
        if (m_dummyVAO) { glDeleteVertexArrays(1, &m_dummyVAO); m_dummyVAO = 0; }
        m_gpuOcclusionCuller.reset();
    }

    /**
     * @brief Checks if the world system is currently processing data.
     * @return true If queues are not empty or threads are active.
     */
    bool IsBusy() {
        if (m_activeWorkerTaskCount > 0) return true;
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (!m_queueGeneratedChunks.empty()) return true;
        if (!m_queueMeshedChunks.empty()) return true;
        return false;
    }

    /**
     * @brief Hot-swaps the terrain generator and resets the world.
     * Useful for live-editing terrain parameters or algorithms.
     */
    void SwitchGenerator(std::unique_ptr<ITerrainGenerator> newGen, GLuint newTextureArrayID) {
        std::cout << "[World] Stopping tasks for generator switch..." << std::endl;
        bool wasFrozen = m_freezeLODUpdates;
        m_freezeLODUpdates = true;

        // Wait for workers to drain
        int waitCycles = 0;
        while (m_activeWorkerTaskCount > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            waitCycles++;
            if (waitCycles % 100 == 0) std::cout << "[World] Waiting for " << m_activeWorkerTaskCount << " threads..." << std::endl;
        }

        m_terrainGenerator = std::move(newGen);
        m_terrainGenerator->Init();
        
        // Update texture if changed
        if (m_textureArrayID != 0 && m_textureArrayID != newTextureArrayID) {
            glDeleteTextures(1, &m_textureArrayID);
        }
        m_textureArrayID = newTextureArrayID;
        
        ReloadWorld(*m_config);
        m_freezeLODUpdates = wasFrozen;
    }

    // --- Getters & Setters ---
    ITerrainGenerator* GetGenerator() { return m_terrainGenerator.get(); }
    void SetTextureArray(GLuint textureID) { m_textureArrayID = textureID; }
    void setCubeDebugMode(int mode) { m_config->settings.cubeDebugMode = mode; }
    void setOcclusionCulling (bool mode){ m_config->settings.occlusionCulling = mode; }
    bool getOcclusionCulling () { return m_config->settings.occlusionCulling; }
    void SetLODFreeze(bool freeze) { m_freezeLODUpdates = freeze; }
    bool GetLODFreeze() const { return m_freezeLODUpdates; }
    const EngineConfig& GetConfig() const { return *m_config; }
    size_t getVRAMUsed () {return m_vramManager.get()->GetUsedMemory();}
    size_t getVRAMAllocated () {return m_vramManager.get()->GetTotalMemory();}
    size_t getVRAMFreeBlocks () {return m_vramManager.get()->GetFreeBlockCount();}
    
    void calculateTotalVertices (size_t& activeChunkCount, size_t& totalVertices) {
        //size_t activeChunkCount = 0;
        //size_t totalVertices = 0;

        for (const auto& pair : m_activeChunkMap) {
            if (pair.second->currentState == ChunkState::ACTIVE) {
                activeChunkCount++;
                totalVertices += pair.second->vertexCountOpaque;
            }
        }
    }

    /**
     * @brief Main update loop called every frame.
     * 1. Checks memory fragmentation.
     * 2. Processes completion queues from worker threads.
     * 3. Triggers async LOD calculations if camera moved.
     * @param cameraPos Current player position.
     */
    void Update(glm::vec3 cameraPos) {
        if (m_isShuttingDown) return;
        Engine::Profiler::ScopedTimer timer("World::Update Total");
        
        // Safety Valve: Reset world if VRAM fragmentation gets critical.
        if (m_vramManager->GetFragmentationRatio() > 0.6f) { 
             ReloadWorld(*m_config);
             return;
        }

        ProcessCompletedWorkerQueues(); 

        if (m_freezeLODUpdates) return; 
        
        ScheduleAsyncLODUpdate(cameraPos);
        UpdateProfilerPressure();
        
        m_frameCounter++;
    }

    /**
     * @brief Processes chunks that have finished a stage in the background.
     * Moves chunks from Generated -> Meshing or Meshed -> GPU Upload.
     */
    void ProcessCompletedWorkerQueues() {
        if(m_isShuttingDown) return;
        Engine::Profiler::ScopedTimer timer("World::ProcessQueues");
        
        std::vector<ChunkNode*> nodesToMesh;
        std::vector<ChunkNode*> nodesToUpload;
        
        // 1. Drain queues thread-safely
        { 
            std::lock_guard<std::mutex> lock(m_queueMutex);
            
            int limitGen = m_config->NODE_GENERATION_LIMIT; // Rate limiting to prevent hiccups
            while (!m_queueGeneratedChunks.empty() && limitGen > 0) {
                nodesToMesh.push_back(m_queueGeneratedChunks.front());
                m_queueGeneratedChunks.pop();
                limitGen--;
            }
            
            int limitUpload = m_config->NODE_UPLOAD_LIMIT; 
            while (!m_queueMeshedChunks.empty() && limitUpload > 0) {
                nodesToUpload.push_back(m_queueMeshedChunks.front());
                m_queueMeshedChunks.pop();
                limitUpload--;
            }
        }

        // 2. Dispatch Mesh Tasks
        for (ChunkNode* node : nodesToMesh) {
            if(m_isShuttingDown) return; 
            
            if (node->currentState == ChunkState::GENERATING) {
                // Uniform chunks (all air/solid) need no mesh
                if (node->isUniform) {
                    node->currentState = ChunkState::ACTIVE;
                } else {
                    // Send to ThreadPool for meshing
                    node->currentState = ChunkState::MESHING;
                    m_activeWorkerTaskCount++;
                    m_workerThreadPool.enqueue([this, node]() { 
                        this->ExecuteAsyncMeshingTask(node); 
                        m_activeWorkerTaskCount--; 
                    });
                }
            }
        }

        // 3. Upload Meshes to GPU (Must be on Main Thread)
        for (ChunkNode* node : nodesToUpload) {
            if(m_isShuttingDown) return; 
            if (node->currentState == ChunkState::MESHING) {
                
                // --- Upload Opaque Mesh ---
                if (!node->cachedMeshOpaque.empty()) {
                    size_t bytes = node->cachedMeshOpaque.size() * sizeof(PackedVertex);
                    long long offset = m_vramManager->Allocate(bytes, sizeof(PackedVertex));
                    if (offset != -1) {
                        m_vramManager->Upload(offset, node->cachedMeshOpaque.data(), bytes);
                        node->vramOffsetOpaque = offset;
                        node->vertexCountOpaque = node->cachedMeshOpaque.size();
                    }
                }

                // --- Upload Transparent Mesh ---
                if (!node->cachedMeshTransparent.empty()) {
                    size_t bytes = node->cachedMeshTransparent.size() * sizeof(PackedVertex);
                    long long offset = m_vramManager->Allocate(bytes, sizeof(PackedVertex));
                    if (offset != -1) {
                        m_vramManager->Upload(offset, node->cachedMeshTransparent.data(), bytes);
                        node->vramOffsetTransparent = offset;
                        node->vertexCountTransparent = node->cachedMeshTransparent.size();
                    }
                }

                // Calculate element indices for the indirect draw command
                size_t opaqueIdx = (node->vramOffsetOpaque != -1) ? (size_t)(node->vramOffsetOpaque / sizeof(PackedVertex)) : 0;
                size_t transIdx = (node->vramOffsetTransparent != -1) ? (size_t)(node->vramOffsetTransparent / sizeof(PackedVertex)) : 0;

                // Register with the GPU Culler (this updates the compute shader's buffer)
                m_gpuOcclusionCuller->AddOrUpdateChunk(
                    node->uniqueID, 
                    node->aabbMinWorld, 
                    node->aabbMaxWorld, 
                    (float)node->scaleFactor, 
                    opaqueIdx, node->vertexCountOpaque, 
                    transIdx, node->vertexCountTransparent
                );

                // Clear CPU caches to save RAM
                node->cachedMeshOpaque.clear(); 
                node->cachedMeshOpaque.shrink_to_fit();
                node->cachedMeshTransparent.clear(); 
                node->cachedMeshTransparent.shrink_to_fit();

                // Release Voxel Data to save RAM (unless we want to keep it for physics/editing)
                if (node->voxelData) {
                    m_voxelDataPool.Release(node->voxelData);
                    node->voxelData = nullptr;
                } 
                
                node->currentState = ChunkState::ACTIVE;
            }
        }
    }

    /**
     * @brief Asynchronous job to calculate which chunks need to be loaded/unloaded based on LOD logic.
     * Executes on a background thread.
     */
    void AsyncJob_CalculateLODs(glm::vec3 cameraPos) {
        if(m_isShuttingDown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] World::LOD Calc");
        auto result = std::make_unique<LODUpdateResult>();

        std::shared_lock<std::shared_mutex> readLock(m_chunkMapMutex);

        // --- STEP 1: Unload Logic ---
        // Iterate current chunks to see if they are out of range or need splitting/merging.
        for (const auto& pair : m_activeChunkMap) {
            ChunkNode* node = pair.second;
            int lod = node->lodLevel;
            int scale = 1 << lod;
            
            int camChunkX = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
            int camChunkZ = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));
            
            int dx = abs(node->gridX - camChunkX);
            int dz = abs(node->gridZ - camChunkZ);
            
            bool shouldUnload = false;

            // Condition A: Too far for current LOD (Needs to switch to Lower Detail Parent)
            if (dx > m_config->settings.lodRadius[lod] || dz > m_config->settings.lodRadius[lod]) {
                 // Only unload if the coarser parent is ready to take over (prevents holes)
                 if (IsParentReady(node->gridX, node->gridY, node->gridZ, lod)) {
                     shouldUnload = true;
                 }
                 // Edge Case: If we are at boundary of world, maybe unload anyway?
                 else if (lod < m_config->settings.lodCount - 1) {
                     int pLod = lod + 1;
                     int pRadius = m_config->settings.lodRadius[pLod];
                     int pScale = 1 << pLod;
                     
                     int pCamX = (int)floor(cameraPos.x / (CHUNK_SIZE * pScale));
                     int pCamZ = (int)floor(cameraPos.z / (CHUNK_SIZE * pScale));
                     int px = node->gridX >> 1;
                     int pz = node->gridZ >> 1;
                     
                     if (abs(px - pCamX) > pRadius || abs(pz - pCamZ) > pRadius) {
                         shouldUnload = true;
                     }
                 }
            }
            // Condition B: Too close for current LOD (Needs to split into Higher Detail Children)
            else if (lod > 0) {
                int prevRadius = m_config->settings.lodRadius[lod - 1];
                int innerBoundary = ((prevRadius + 1) / 2);
                if (dx < innerBoundary && dz < innerBoundary) {
                    // Only unload if the children are ready (prevents holes)
                    if (AreChildrenReady(node->gridX, node->gridY, node->gridZ, lod)) {
                        shouldUnload = true;
                    }
                }
            }

            if (shouldUnload) {
                ChunkState s = node->currentState.load();
                // Don't unload mid-generation to avoid race conditions with worker threads
                if (s != ChunkState::GENERATING && s != ChunkState::MESHING) {
                    result->chunksToUnload.push_back(pair.first);
                }
            }
        }

        // --- STEP 2: Load Logic ---
        // Precompute spiral offsets once to iterate outward from player.
        static std::vector<std::pair<int, int>> spiralOffsets;
        static std::once_flag flag;
        std::call_once(flag, [](){
            int maxR = 128; 
            for (int x = -maxR; x <= maxR; x++) {
                for (int z = -maxR; z <= maxR; z++) {
                    spiralOffsets.push_back({x, z});
                }
            }
            // Sort by distance from center
            std::sort(spiralOffsets.begin(), spiralOffsets.end(), [](const std::pair<int,int>& a, const std::pair<int,int>& b){
                return (a.first*a.first + a.second*a.second) < (b.first*b.first + b.second*b.second);
            });
        });

        // Iterate through LOD levels (High Detail -> Low Detail)
        for(int lod = 0; lod < m_config->settings.lodCount; lod++) {
            int scale = 1 << lod;
            int playerChunkX = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
            int playerChunkZ = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));
            
            int radius = m_config->settings.lodRadius[lod];
            int radiusSq = radius * radius; 
            
            // Define a "donut" hole where higher detail LODs exist
            int minRadius = 0;
            if (lod > 0) {
                int prevR = m_config->settings.lodRadius[lod - 1];
                minRadius = ((prevR + 1) / 2); 
            }

            for (const auto& offset : spiralOffsets) {
                int distSq = offset.first*offset.first + offset.second*offset.second;
                if (distSq > (radiusSq * 2 + 100)) break; // Optimization: Stop if outside current LOD radius
                
                // Box check
                if (std::abs(offset.first) > radius || std::abs(offset.second) > radius) continue;
                // Donut hole check
                if (lod > 0 && std::abs(offset.first) < minRadius && std::abs(offset.second) < minRadius) continue;

                int targetX = playerChunkX + offset.first;
                int targetZ = playerChunkZ + offset.second;
                
                // Vertical Check: Ask generator for height bounds at this X/Z to skip empty sky/underground chunks
                int minH, maxH;
                m_terrainGenerator->GetHeightBounds(targetX, targetZ, scale, minH, maxH);
                
                int chunkYStart = std::max(0, (minH / (CHUNK_SIZE * scale)) - 1); 
                int chunkYEnd = std::min(m_config->settings.worldHeightChunks - 1, (maxH / (CHUNK_SIZE * scale)) + 1);

                for (int y = chunkYStart; y <= chunkYEnd; y++) {
                    int64_t key = ChunkKey(targetX, y, targetZ, lod);
                    
                    if (m_activeChunkMap.find(key) == m_activeChunkMap.end()) {
                        // Calculate priority distance (3D distance to camera)
                        int dx = targetX - playerChunkX; 
                        int dz = targetZ - playerChunkZ; 
                        int chunkWorldY = y * CHUNK_SIZE * scale;
                        int dy = (chunkWorldY - (int)cameraPos.y) / (CHUNK_SIZE * scale); 
                        int distMetric = dx*dx + dz*dz + (dy*dy); 
                        
                        result->chunksToLoad.push_back({targetX, y, targetZ, lod, distMetric});
                    }
                }
            }
        }
        
        readLock.unlock(); 

        // Sort requests by distance so closest load first
        std::sort(result->chunksToLoad.begin(), result->chunksToLoad.end(), 
            [](const ChunkLoadRequest& a, const ChunkLoadRequest& b){ return a.distSq < b.distSq; });

        // Submit result to main thread
        std::lock_guard<std::mutex> lock(m_lodResultMutex);
        m_pendingLODResult = std::move(result);
        m_isLODWorkerRunning = false;
    }

    /**
     * @brief Manages the async dispatching of the LOD calculation job.
     * Also applies the results (creating/destroying chunks) on the main thread.
     */
    void ScheduleAsyncLODUpdate(glm::vec3 cameraPos) {
        // --- Helper: Process Deletions ---
        auto ProcessUnloads = [this]() {
            std::lock_guard<std::mutex> lock(m_lodResultMutex);
            if (m_pendingLODResult && !m_pendingLODResult->chunksToUnload.empty()) {
                 std::unique_lock<std::shared_mutex> writeLock(m_chunkMapMutex);
                 
                 for (int64_t key : m_pendingLODResult->chunksToUnload) {
                    auto it = m_activeChunkMap.find(key);
                    if (it != m_activeChunkMap.end()) {
                        ChunkNode* node = it->second;
                        
                        // Notify GPU Culler to stop drawing this
                        m_gpuOcclusionCuller->RemoveChunk(node->uniqueID);
                        
                        // Free GPU Memory
                        if (node->vramOffsetOpaque != -1) {
                            m_vramManager->Free(node->vramOffsetOpaque, node->vertexCountOpaque * sizeof(PackedVertex));
                            node->vramOffsetOpaque = -1;
                        }
                        if (node->vramOffsetTransparent != -1) {
                            m_vramManager->Free(node->vramOffsetTransparent, node->vertexCountTransparent * sizeof(PackedVertex));
                            node->vramOffsetTransparent = -1;
                        }
                        
                        // Return to Pool
                        m_chunkMetadataPool.Release(node);
                        m_activeChunkMap.erase(it);
                    }
                }
                m_pendingLODResult->chunksToUnload.clear();
            }
        };

        // --- Trigger Async Thread ---
        if (!m_isLODWorkerRunning) {
             float distSq = glm::dot(cameraPos - m_lastLODCalculationPos, cameraPos - m_lastLODCalculationPos);
             
             // Only recalculate if player moved significantly (64 units)
             if (distSq > 64.0f) { 
                 // If teleported (huge distance), force immediate cleanup
                 if (distSq > 10000.0f) { 
                     ProcessUnloads(); 
                     std::lock_guard<std::mutex> lock(m_lodResultMutex);
                     m_pendingLODResult = nullptr; 
                 }
                 
                 m_lastLODCalculationPos = cameraPos;
                 m_isLODWorkerRunning = true;
                 m_activeWorkerTaskCount++;
                 
                 // Enqueue Job
                 m_workerThreadPool.enqueue([this, cameraPos](){ 
                     this->AsyncJob_CalculateLODs(cameraPos); 
                     m_activeWorkerTaskCount--; 
                 });
             }
        }

        // --- Apply Results (Main Thread) ---
        {
            Engine::Profiler::ScopedTimer timer("World::ApplyLODs");
            ProcessUnloads(); // Prioritize freeing memory

            std::lock_guard<std::mutex> lock(m_lodResultMutex);
            if (m_pendingLODResult) {
                std::unique_lock<std::shared_mutex> writeLock(m_chunkMapMutex);
                int queued = 0;
                int MAX_CREATIONS_PER_FRAME = 500; // Throttle to prevent frame spikes
                
                size_t& idx = m_pendingLODResult->loadIndex;
                const auto& loadList = m_pendingLODResult->chunksToLoad;

                // Flow control: Don't spawn more tasks if queue is full
                size_t queueLimit = (size_t)m_config->MAX_TRANSIENT_VOXEL_MESHES;
                if (queueLimit > 100) queueLimit -= 100;

                while (idx < loadList.size() && queued < MAX_CREATIONS_PER_FRAME) {
                    {
                        std::lock_guard<std::mutex> qLock(m_queueMutex);
                        size_t totalInFlight = m_queueGeneratedChunks.size() + m_queueMeshedChunks.size() + m_activeWorkerTaskCount;
                        if (totalInFlight >= queueLimit) break; 
                    }

                    const auto& req = loadList[idx];
                    idx++;
                    
                    int64_t key = ChunkKey(req.x, req.y, req.z, req.lod);
                    if (m_activeChunkMap.find(key) == m_activeChunkMap.end()) {
                        ChunkNode* newNode = m_chunkMetadataPool.Acquire();
                        if (newNode) {
                            newNode->Reset(req.x, req.y, req.z, req.lod);
                            newNode->uniqueID = key; 
                            m_activeChunkMap[key] = newNode;
                            
                            newNode->currentState = ChunkState::GENERATING;
                            m_activeWorkerTaskCount++; 
                            
                            m_workerThreadPool.enqueue([this, newNode]() { 
                                this->ExecuteTask_GenerateVoxelData(newNode); 
                                m_activeWorkerTaskCount--; 
                            });
                            queued++;
                        }
                    }
                }
                
                if (idx >= loadList.size()) {
                    m_pendingLODResult = nullptr; // All loaded
                }
            }
        }
    }

    /**
     * @brief Renders the world using GPU Culling and Multi-Draw Indirect.
     * * Pipeline:
     * 1. GPU Compute Cull (Frustum + HiZ Occlusion)
     * 2. Indirect Draw (Opaque)
     * 3. Indirect Draw (Transparent)
     * 4. Generate HiZ Depth Pyramid for next frame.
     */
    void Draw(Shader& shader, const glm::mat4& viewProj, const glm::mat4& previousViewProjMatrix, const glm::mat4& proj, const int CUR_SCR_WIDTH, const int CUR_SCR_HEIGHT, Shader* depthDebugShader, bool depthDebug, bool frustumLock, glm::vec3 playerPosition) {
        if(m_isShuttingDown) return;
        
        // --- PASS 1: GPU CULLING ---
        // Runs a compute shader to check every chunk against frustum and Hi-Z buffer.
        // Outputs draw commands to an Indirect Buffer.
        {
            Engine::Profiler::Get().BeginGPU("GPU: Buffer and Cull Compute"); 
            m_gpuOcclusionCuller->Cull(viewProj, previousViewProjMatrix, proj, g_fbo.hiZTex);
            Engine::Profiler::Get().EndGPU();
        }

        // --- PASS 2: RENDER GEOMETRY ---
        {   
            Engine::Profiler::Get().BeginGPU("GPU: MDI DRAW"); 

            shader.use();
            // Standard Uniforms
            glUniformMatrix4fv(glGetUniformLocation(shader.ID, "u_ViewProjection"), 1, GL_FALSE, glm::value_ptr(viewProj));
            glUniform3fv(glGetUniformLocation(shader.ID, "u_CameraPos"), 1, glm::value_ptr(playerPosition)); 
            glUniform1i(glGetUniformLocation(shader.ID, "u_DebugMode"), m_config->settings.cubeDebugMode);
            
            // Bind SSBOs (Shader Storage Buffer Objects)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_vramManager->GetID());           // Big Vertex Buffer
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_gpuOcclusionCuller->GetVisibleChunkBuffer()); // Chunk Meta Data

            if (m_textureArrayID != 0) {
                 glActiveTexture(GL_TEXTURE0);
                 glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureArrayID);
                 shader.setInt("u_Textures", 0);
            }

            glBindVertexArray(m_dummyVAO); // We generate vertices in VS via SV_VertexID, or pull from SSBO

            // Setup State
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f); // Prevent Z-fighting
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE); 

            // -- Draw Opaque --
            // uses glMultiDrawArraysIndirectCount to draw only visible chunks
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_gpuOcclusionCuller->GetIndirectOpaque());
            glBindBuffer(GL_PARAMETER_BUFFER, m_gpuOcclusionCuller->GetAtomicCounter()); // Contains count of visible chunks
            glMultiDrawArraysIndirectCount(GL_TRIANGLES, 0, 0, (GLsizei)m_gpuOcclusionCuller->GetMaxChunks(), 0);

            // -- Draw Transparent --
            // Drawn after opaque for blending
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE); // Don't write to depth buffer
            
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_gpuOcclusionCuller->GetIndirectTrans());
            glMultiDrawArraysIndirectCount(GL_TRIANGLES, 0, 0, (GLsizei)m_gpuOcclusionCuller->GetMaxChunks(), 0);

            // Restore State
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glDisable(GL_POLYGON_OFFSET_FILL);

            Engine::Profiler::Get().EndGPU();
            
            // --- PASS 3: HI-Z DEPTH GENERATION ---
            // Downsample the depth buffer to create a mip-chain for efficient occlusion culling next frame
             Engine::Profiler::Get().BeginGPU("GPU: Occlusion Cull COMPUTE"); 

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            // Copy current depth to texture
            glCopyImageSubData(g_fbo.depthTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                            g_fbo.hiZTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                            CUR_SCR_WIDTH, CUR_SCR_HEIGHT, 1);
            
            // Barrier to ensure copy is done before compute reads it
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            
            GetCuller()->GenerateHiZ(g_fbo.hiZTex, CUR_SCR_WIDTH, CUR_SCR_HEIGHT);
                            
            // Optional Debug Visualization
            if (!depthDebug) {
                // Normal render: Blit FBO to screen
                glBlitNamedFramebuffer(g_fbo.fbo, 0, 
                    0, 0, CUR_SCR_WIDTH, CUR_SCR_HEIGHT, 
                    0, 0, CUR_SCR_WIDTH, CUR_SCR_HEIGHT, 
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
            } else {
                RenderHiZDebug(depthDebugShader, g_fbo.hiZTex, 0, CUR_SCR_WIDTH, CUR_SCR_HEIGHT);
            }

            Engine::Profiler::Get().EndGPU();
        }
    }

    GpuCuller* GetCuller() { return m_gpuOcclusionCuller.get(); }

    void RenderHiZDebug(Shader* debugShader, GLuint hizTexture, int mipLevel, int screenW, int screenH) {
        glDisable(GL_DEPTH_TEST); 
        glDisable(GL_CULL_FACE);
        
        debugShader->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hizTexture);
        // Force Nearest filtering to see pixels clearly
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0); 
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 10); 

        debugShader->setInt("u_DepthTexture", 0);
        debugShader->setInt("u_MipLevel", mipLevel);
        debugShader->setVec2("u_ScreenSize", glm::vec2(screenW, screenH));

        glBindVertexArray(m_dummyVAO); 
        glDrawArrays(GL_TRIANGLES, 0, 3); // Fullscreen triangle
        
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    void ReloadWorld(EngineConfig newConfig) {
        m_config = std::make_unique<EngineConfig>(newConfig);
        m_terrainGenerator->Init();
        {
            std::unique_lock<std::shared_mutex> lock(m_chunkMapMutex);
            for (auto& pair : m_activeChunkMap) {
                ChunkNode* node = pair.second;
                m_gpuOcclusionCuller->RemoveChunk(node->uniqueID); 
                if (node->vramOffsetOpaque != -1) {
                    m_vramManager->Free(node->vramOffsetOpaque, node->vertexCountOpaque * sizeof(PackedVertex));
                    node->vramOffsetOpaque = -1;
                }
                if (node->vramOffsetTransparent != -1) {
                    m_vramManager->Free(node->vramOffsetTransparent, node->vertexCountTransparent * sizeof(PackedVertex));
                    node->vramOffsetTransparent = -1;
                }
                m_chunkMetadataPool.Release(node);
            }
            m_activeChunkMap.clear();
        }
        m_lastLODCalculationPos = glm::vec3(-99999.0f);
        m_pendingLODResult = nullptr;
    }

private:
    // ============================================================================================
    // INTERNAL WORKER TASKS
    // ============================================================================================
    
    /**
     * @brief Async Task: Generates voxel data for a chunk.
     * Delegates to the TerrainGenerator.
     */
    void ExecuteTask_GenerateVoxelData(ChunkNode* node) {
        if (m_isShuttingDown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] Task: Generate");

        float outMinY, outMaxY;
        FillChunkVoxels(node, outMinY, outMaxY);
        
        // Note: outMinY/outMaxY can be used to tighten AABB here if desired.
        
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_isShuttingDown) return;
        m_queueGeneratedChunks.push(node);
    }

    /**
     * @brief Helper to allocate and fill the Chunk object with blocks.
     */
    void FillChunkVoxels(ChunkNode* node, float& outMinY, float& outMaxY) {
        int cx = node->gridX;
        int cy = node->gridY;
        int cz = node->gridZ;
        int scale = node->scaleFactor;

        int worldY = cy * CHUNK_SIZE * scale;
        int chunkBottomY = worldY;
        int chunkTopY = worldY + (CHUNK_SIZE * scale);

        // 1. Broad Phase Check: Skip generation if outside terrain bounds
        int minGenH, maxGenH;
        m_terrainGenerator->GetHeightBounds(cx, cz, scale, minGenH, maxGenH);

        // Case: Fully Air
        if (chunkBottomY > maxGenH) {
            node->isUniform = true;
            node->uniformBlockID = 0; // Air
            node->voxelData = nullptr; 
            outMinY = (float)chunkBottomY;
            outMaxY = (float)chunkBottomY;
            return;
        }
        // Case: Fully Solid (Underground)
        if (chunkTopY < minGenH) {
                node->isUniform = true;
                node->uniformBlockID = 3; // Solid Stone
                node->voxelData = nullptr;
                outMinY = (float)chunkBottomY;
                outMaxY = (float)chunkTopY;
                return;
        }

        // 2. Allocation
        node->isUniform = false;
        node->voxelData = m_voxelDataPool.Acquire(); 

        if (!node->voxelData) {
            // Fallback if pool empty
            node->isUniform = true;
            node->uniformBlockID = 0;
            return;
        }

        // 3. Batched Generation via SIMD/Internal Generator Logic
        m_terrainGenerator->GenerateChunk(node->voxelData, cx, cy, cz, scale);

        outMinY = (float)chunkBottomY;
        outMaxY = (float)chunkTopY;
    }


    /**
     * @brief Async Task: Generates geometry (vertices/indices) from voxel data.
     * Uses Greedy Meshing or Standard Meshing.
     */
    void ExecuteAsyncMeshingTask(ChunkNode* node) {
        if (m_isShuttingDown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] Task: Mesh"); 
        
        // Temporary stack-like allocators for building mesh
        LinearAllocator<PackedVertex> opaqueAllocator(100000); 
        LinearAllocator<PackedVertex> transAllocator(50000); 

        // Execute meshing algorithm
        MeshChunk(*node->voxelData, opaqueAllocator, transAllocator, false);
        
        // Copy to node cache (heap allocation happening here)
        node->cachedMeshOpaque.assign(opaqueAllocator.Data(), opaqueAllocator.Data() + opaqueAllocator.Count());
        node->cachedMeshTransparent.assign(transAllocator.Data(), transAllocator.Data() + transAllocator.Count());
        
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_isShuttingDown) return;
        m_queueMeshedChunks.push(node);
    }

    /**
     * @brief Checks if the children (higher detail chunks) of a node are loaded.
     * Used to prevent cracks when transitioning LODs.
     * @return true if all 8 children are ACTIVE or empty.
     */
    bool AreChildrenReady(int cx, int cy, int cz, int lod) {
        if (lod == 0) return true; // Lowest level has no children
        
        int childLod = lod - 1;
        int scale = 1 << childLod; 
        int startX = cx * 2; int startY = cy * 2; int startZ = cz * 2;

        for (int x = 0; x < 2; x++) {
            for (int z = 0; z < 2; z++) {
                // Optimization: Check bounds before lookups
                int minH, maxH;
                m_terrainGenerator->GetHeightBounds((startX + x), (startZ + z), scale, minH, maxH);
                int chunkYStart = (minH / (CHUNK_SIZE * scale)) - 1; 
                int chunkYEnd = (maxH / (CHUNK_SIZE * scale)) + 1;

                for (int y = 0; y < 2; y++) {
                    int64_t key = ChunkKey(startX + x, startY + y, startZ + z, childLod);
                    auto it = m_activeChunkMap.find(key);
                    
                    // If chunk exists, it MUST be active (meshed & uploaded)
                    if (it != m_activeChunkMap.end()) {
                         if (it->second->currentState.load() != ChunkState::ACTIVE) return false;
                    } else {
                        // If chunk doesn't exist, ensure it's supposed to be empty
                        int myY = startY + y;
                        if (myY >= chunkYStart && myY <= chunkYEnd) {
                            return false; // Should exist but doesn't
                        }
                    }
                }
            }
        }
        return true;
    }


    /**
     * @brief Checks if the parent (lower detail chunk) is loaded.
     * Used to prevent holes when unloading high detail chunks.
     */
    bool IsParentReady(int cx, int cy, int cz, int lod) {
        if (lod >= m_config->settings.lodCount - 1) return true; 
        
        int parentLod = lod + 1;
        int px = cx >> 1; int py = cy >> 1; int pz = cz >> 1;

        int64_t key = ChunkKey(px, py, pz, parentLod);
        auto it = m_activeChunkMap.find(key);
        
        if (it == m_activeChunkMap.end() || it->second->currentState.load() != ChunkState::ACTIVE) {
            return false;
        }
        return true;
    }

    /**
     * @brief Pushes current world stats to the global profiler for UI visualization.
     */
    void UpdateProfilerPressure() {
        if (!Engine::Profiler::Get().m_Enabled) return;
        size_t pendingGen = 0;
        {
            std::unique_lock<std::mutex> lock(m_lodResultMutex, std::try_to_lock);
            if (lock.owns_lock() && m_pendingLODResult) {
                pendingGen = m_pendingLODResult->chunksToLoad.size() - m_pendingLODResult->loadIndex;
            }
        }
        size_t waitingMesh = m_queueGeneratedChunks.size();
        size_t waitingUpload = m_queueMeshedChunks.size();
        size_t activeThreads = m_activeWorkerTaskCount.load();
        size_t totalActive = m_activeChunkMap.size(); 

        Engine::Profiler::Get().SetPipelineStats(
            pendingGen, waitingMesh, waitingUpload, activeThreads, totalActive,
            (size_t)m_config->MAX_TRANSIENT_VOXEL_MESHES,
            m_voxelDataPool.GetAllocatedMB(), m_voxelDataPool.GetUsedMB(),
            m_chunkMetadataPool.GetAllocatedMB(), m_chunkMetadataPool.GetUsedMB()
        );
    }
};