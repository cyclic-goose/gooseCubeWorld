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
#include "chunkNode.h"
//#include "chunk.h"
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
#include "gui_utils.h"

//#include "debug_chunks.h"






// ================================================================================================
//                                          WORLD CLASS
// ================================================================================================

/**
 * @brief Main manager class for the Voxel World.
 * * Responsibilities:
 * 1. Manage the lifecycle of chunks (Generation -> Meshing -> Upload -> Unload).
 * 2. Maintaining the LOD structure around the camera.
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
    friend class ChunkDebugger;

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

        // ID 0: Chunk Metadata
        m_chunkMetadataPool.Init(
            m_config->nodePool.growthStride, 
            m_config->nodePool.initialSize, 
            nodeCapacity, 
            0 
        ); 

        // ID 1: Voxel Data
        m_voxelDataPool.Init(
            m_config->voxelPool.growthStride, 
            m_config->voxelPool.initialSize, 
            m_config->voxelPool.limit, 
            1 
        );

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


    // retrieve block ID at worldspace x, y, z (FAST)
  // Add this method to your World or ChunkManager class
// Assumes you have: std::unordered_map<int64_t, ChunkNode*> chunks;

inline uint8_t GetBlockAt(int x, int y, int z) const {
    // 1. Calculate Chunk Grid Coordinates
    // Standard floor ensures consistent behavior for negative coordinates
    int cx = static_cast<int>(std::floor(x / (float)CHUNK_SIZE));
    int cy = static_cast<int>(std::floor(y / (float)CHUNK_SIZE));
    int cz = static_cast<int>(std::floor(z / (float)CHUNK_SIZE));

    // 2. Generate Hash Key
    int64_t key = ChunkKey(cx, cy, cz, 0);

    // 3. Find the ChunkNode
    auto it = m_activeChunkMap.find(key);
    if (it == m_activeChunkMap.end()) {
        return 0; // Chunk doesn't exist yet
    }

    ChunkNode* node = it->second;

    // 4. Optimization Check (DEBUGGING: DISABLED)
    // POTENTIAL BUG: In chunkNode.h, your Reset() function does not set 
    // isUniform = false. If a node is recycled from an Air chunk, this
    // flag remains true, causing you to fall through solid ground.
    // Uncomment this ONLY after fixing ChunkNode::Reset()!
    
    if (node->isUniform) {
        return node->uniformBlockID;
    }
    

    // 5. Safety: Check if voxel data is actually generated
    if (node->voxelData == nullptr) {
        return 0; 
    }

    // 6. Calculate Local Coordinates
    // Using modulo with negative fix is the most robust way to handle this
    // (matches the logic of 'x & 31' but explicitly safe)
    int lx = x % CHUNK_SIZE; 
    int ly = y % CHUNK_SIZE; 
    int lz = z % CHUNK_SIZE;

    if (lx < 0) lx += CHUNK_SIZE;
    if (ly < 0) ly += CHUNK_SIZE;
    if (lz < 0) lz += CHUNK_SIZE;

    // 7. Retrieve from the standard Chunk struct
    // Adding +1 because your Chunk struct uses indices 1-32 for data
    return node->voxelData->Get(lx + 1, ly + 1, lz + 1);
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
    int getFrameCount() {return m_frameCounter;}
    
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


        //if (m_frameCounter > 1)
            
        
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

                // CHANGE: Want to keep now for physics calcs, need to release voxel data when node is released
                // Release Voxel Data to save RAM, BUT keep it for LOD 0 (Physics)
                if (node->voxelData) {
                    // If we are at LOD 0, we keep the data for GetBlockAt()
                    // If we are at LOD > 0, we don't need it for physics, so release it.
                    if (node->lodLevel != 0) {
                        m_voxelDataPool.Release(node->voxelData);
                        node->voxelData = nullptr;
                    }
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

                        // when transitioning LODs, make sure we release voxel data or it would leak memory
                        if (node->voxelData) {
                            m_voxelDataPool.Release(node->voxelData);
                            node->voxelData = nullptr;
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

    
    // This is the logic specifically put here to handle player interaction with a specific block
    // a modified block should flag the chunk for remesh and reupload
     struct RaycastResult {
        bool success = false;
        glm::ivec3 blockPos;   // The block we hit
        glm::ivec3 faceNormal; // The face we hit (for placement)
        float distance;
    };

    RaycastResult Raycast(glm::vec3 origin, glm::vec3 direction, float maxDist) {
        RaycastResult res;
        
        // Amanatides & Woo Voxel Traversal Algorithm
        int x = (int)floor(origin.x);
        int y = (int)floor(origin.y);
        int z = (int)floor(origin.z);

        int stepX = (direction.x > 0) ? 1 : -1;
        int stepY = (direction.y > 0) ? 1 : -1;
        int stepZ = (direction.z > 0) ? 1 : -1;

        float tDeltaX = (direction.x != 0) ? std::abs(1.0f / direction.x) : 999999.0f;
        float tDeltaY = (direction.y != 0) ? std::abs(1.0f / direction.y) : 999999.0f;
        float tDeltaZ = (direction.z != 0) ? std::abs(1.0f / direction.z) : 999999.0f;

        float distX = (stepX > 0) ? (x + 1 - origin.x) : (origin.x - x);
        float distY = (stepY > 0) ? (y + 1 - origin.y) : (origin.y - y);
        float distZ = (stepZ > 0) ? (z + 1 - origin.z) : (origin.z - z);

        float tMaxX = (tDeltaX < 999999.0f) ? (distX * tDeltaX) : 999999.0f;
        float tMaxY = (tDeltaY < 999999.0f) ? (distY * tDeltaY) : 999999.0f;
        float tMaxZ = (tDeltaZ < 999999.0f) ? (distZ * tDeltaZ) : 999999.0f;

        float traveled = 0.0f;
        int lastX = x, lastY = y, lastZ = z;

        while (traveled < maxDist) {
            // Check block (0 is Air)
            if (GetBlockAt(x, y, z) != 0) { 
                res.success = true;
                res.blockPos = glm::ivec3(x, y, z);
                res.faceNormal = glm::ivec3(lastX - x, lastY - y, lastZ - z);
                res.distance = traveled;
                return res;
            }

            lastX = x; lastY = y; lastZ = z;

            if (tMaxX < tMaxY) {
                if (tMaxX < tMaxZ) { x += stepX; traveled = tMaxX; tMaxX += tDeltaX; } 
                else               { z += stepZ; traveled = tMaxZ; tMaxZ += tDeltaZ; }
            } else {
                if (tMaxY < tMaxZ) { y += stepY; traveled = tMaxY; tMaxY += tDeltaY; } 
                else               { z += stepZ; traveled = tMaxZ; tMaxZ += tDeltaZ; }
            }
        }
        return res;
    }

    void SetBlock(int x, int y, int z, uint8_t id) {
    // 1. Find Chunk
    int cx = (int)floor(x / (float)CHUNK_SIZE);
    int cy = (int)floor(y / (float)CHUNK_SIZE);
    int cz = (int)floor(z / (float)CHUNK_SIZE);

    int64_t key = ChunkKey(cx, cy, cz, 0); 
    
    auto it = m_activeChunkMap.find(key);
    if (it == m_activeChunkMap.end()) return; 

    ChunkNode* node = it->second;
    if (node->currentState != ChunkState::ACTIVE) return; 

    // 2. Handle Uniform Inflation
    if (node->isUniform) {
        if (node->uniformBlockID == id) return;
        node->voxelData = m_voxelDataPool.Acquire();
        if (!node->voxelData) return;
        std::memset(node->voxelData->voxels, node->uniformBlockID, sizeof(Chunk));
        node->isUniform = false;
    }

    // 3. Update Voxel (Local)
    int lx = x % CHUNK_SIZE; if (lx < 0) lx += CHUNK_SIZE;
    int ly = y % CHUNK_SIZE; if (ly < 0) ly += CHUNK_SIZE;
    int lz = z % CHUNK_SIZE; if (lz < 0) lz += CHUNK_SIZE;

    node->voxelData->Set(lx + 1, ly + 1, lz + 1, id);

    // 4. Trigger Re-Mesh (Current Chunk)
    node->currentState = ChunkState::GENERATING;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queueGeneratedChunks.push(node);
    }

    // 5. Update Neighbors (Fix Seams & Update Padding)
    auto TriggerNeighbor = [&](int offsetX, int offsetY, int offsetZ) {
        int64_t nKey = ChunkKey(cx + offsetX, cy + offsetY, cz + offsetZ, 0);
        auto nIt = m_activeChunkMap.find(nKey);
        
        if (nIt != m_activeChunkMap.end()) {
            ChunkNode* nNode = nIt->second;

            // CRITICAL FIX: Update the neighbor's padding memory!
            // If the neighbor is Uniform, we MUST inflate it to store this padding change,
            // otherwise the mesher won't see the new hole/block next to it.
            if (nNode->isUniform) {
                // If you have a helper for this, use it. Otherwise duplicate logic:
                nNode->voxelData = m_voxelDataPool.Acquire();
                if (nNode->voxelData) {
                    std::memset(nNode->voxelData->voxels, nNode->uniformBlockID, sizeof(Chunk));
                    nNode->isUniform = false;
                }
            }

            if (nNode->voxelData) {
                // Map global pos to neighbor's local space
                // Formula: local_coord - (offset * CHUNK_SIZE)
                // If offsetX = -1 (Left Neighbor), lx=0 -> nx = 0 - (-32) = 32. 
                // Set(nx + 1) accesses index 33 (Right Padding).
                int nx = lx - (offsetX * CHUNK_SIZE); 
                int ny = ly - (offsetY * CHUNK_SIZE);
                int nz = lz - (offsetZ * CHUNK_SIZE);

                nNode->voxelData->Set(nx + 1, ny + 1, nz + 1, id);
            }

            // Flag for remesh
            if (nNode->currentState == ChunkState::ACTIVE) {
                nNode->currentState = ChunkState::GENERATING;
                std::lock_guard<std::mutex> lock(m_queueMutex);
                m_queueGeneratedChunks.push(nNode);
            }
        }
    };

    if (lx == 0) TriggerNeighbor(-1, 0, 0);
    if (lx == CHUNK_SIZE - 1) TriggerNeighbor(1, 0, 0);
    if (ly == 0) TriggerNeighbor(0, -1, 0);
    if (ly == CHUNK_SIZE - 1) TriggerNeighbor(0, 1, 0);
    if (lz == 0) TriggerNeighbor(0, 0, -1);
    if (lz == CHUNK_SIZE - 1) TriggerNeighbor(0, 0, 1);
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

                // same as updating lods, pretend as if we need to release all voxel data
                if (node->voxelData) {
                    m_voxelDataPool.Release(node->voxelData);
                    node->voxelData = nullptr;
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
        int cx = node->gridX; // chunk x
        int cy = node->gridY; // chunk y
        int cz = node->gridZ; // chunk z
        int scale = node->scaleFactor; // LOD scale factor

        int worldY = cy * CHUNK_SIZE * scale;
        int chunkBottomY = worldY;
        int chunkTopY = worldY + (CHUNK_SIZE * scale);

        // 1. Broad Phase Check: Skip generation if outside terrain bounds. IMPORTANT: This is done before generating, but theres also a change a mesh could end up uniform after generating (generator puts air blocks, we should run a check after and unload that set of voxel data)
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
        m_terrainGenerator->GenerateChunk(node->voxelData, cx, cy, cz, scale); // currently, the generator is dumb and has no way of marking if the block is all air

        // ************ If the generated chunk turned out to be all air, then check for that quickly and get rid of the allocated voxel data IDs and set as Uniform ********* //
        // --- OPTIMIZED POST-GENERATION CHECK (Raw Memory Scan) ---
        // Using raw pointers removes the overhead of index calculation in Get().
        
        bool allSame = true;
        uint8_t firstID = node->voxelData->Get(1, 1, 1); /// if things arent generating underground, this could be the culprit, maybe stricly set to ID 0 for air
        
        const uint8_t* voxels = node->voxelData->voxels;
        // Precompute strides for X-Contiguous layout
        const int strideY = CHUNK_SIZE_PADDED * CHUNK_SIZE_PADDED;
        const int strideZ = CHUNK_SIZE_PADDED;

        // Iterate strictly over the inner volume (1..32)
        // We skip padding (0 and 33) to ensure we check only relevant data
        for (int y = 1; y <= CHUNK_SIZE; ++y) {
            int offsetY = y * strideY;
            for (int z = 1; z <= CHUNK_SIZE; ++z) {
                int offset = offsetY + (z * strideZ) + 1; // Start of row X at 1
                
                // Check 32 contiguous bytes (Compiler will likely auto-vectorize this)
                for (int x = 0; x < CHUNK_SIZE; ++x) {
                     if (voxels[offset + x] != firstID) {
                         allSame = false;
                         goto check_complete;
                     }
                }
            }
        }

        check_complete:
        if (allSame) {
            m_voxelDataPool.Release(node->voxelData);
            node->voxelData = nullptr;
            node->isUniform = true;
            node->uniformBlockID = firstID;
        }
        // ************ If the generated chunk turned out to be all air, then check for that quickly and get rid of the allocated voxel data IDs and set as Uniform ********* //


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

        // trying to detect if a block is all air and uniform after this is just really the same maybe worse than doing it right after the generate call in fillChunk. could be empty but all underground or empty but all air either way check has to be run 
        
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

        // auto usage = m_voxelDataPool.GetUsedMB()+m_chunkMetadataPool.GetUsedMB(); 
        // if (usage > 5400.0f)
        //     GUI::DrawScreenMessage("CRITICAL MEMORY USAGE: Approaching 6GB Memory Limit", GUI::LEVEL_CRITICAL);
    


        // this section will only happen if the profiler is enabled
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