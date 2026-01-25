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

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <FastNoise/FastNoise.h>

#include "chunk.h"
#include "mesher.h"
#include "linearAllocator.h"
#include "shader.h"
#include "threadpool.h"
#include "chunk_pool.h"
#include "gpu_memory.h"
#include "packedVertex.h"
#include "profiler.h"
#include "gpu_culler.h" 

// ================================================================================================
//                                     CONFIGURATION
// ================================================================================================

struct WorldConfig {
    int seed = 1337;
    int worldHeightChunks = 64;
    int lodCount = 5; 
    
    // LOD Radii: Defines the distance for each Detail Level.
    // Index 0 = Highest Detail (LOD 0), Index 4 = Lowest Detail.
    int lodRadius[12] = { 10, 16, 24, 32, 48, 0, 0, 0 , 0, 0, 0, 0}; 
    
    // Terrain Parameters
    float scale = 0.08f;          
    float hillAmplitude = 100.0f;  
    float hillFrequency = 4.0f;   
    float mountainAmplitude = 500.0f; 
    float mountainFrequency = 0.26f; 
    int seaLevel = 90;            
    bool enableCaves = false;     
    float caveThreshold = 0.5f; 
    float VRAM_HEAP_ALLOCATION_MB = 1024;

    // debug
    int cubeDebugMode = 4; // a value of zero means run the normal shader program 
};

// ================================================================================================
//                                  TERRAIN GENERATOR INTERFACE
// ================================================================================================
// GUIDE: To implement a new Terrain Generator:
// 1. Create a class that inherits from ITerrainGenerator.
// 2. Implement Init(), GetHeight(), and GetBlock().
// 3. In World::World(), replace 'm_generator = std::make_unique<DefaultGenerator>(m_config);'
//    with your new class.
// ================================================================================================

class ITerrainGenerator {
public:
    virtual ~ITerrainGenerator() = default;
    
    // Called once on World init
    virtual void Init() = 0; 
    
    // Used for LOD calculations and height culling
    virtual int GetHeight(float x, float z) const = 0;
    
    // Used for actual voxel placement. Return Block ID (0 = Air).
    virtual uint8_t GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const = 0;
    
    // Helper for Quadtree/LOD bounding boxes
    virtual void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) = 0;
};

// ================================================================================================
//                                  DEFAULT GENERATOR (FastNoise)
// ================================================================================================

class DefaultGenerator : public ITerrainGenerator {
private:
    FastNoise::SmartNode<> m_baseNoise;
    FastNoise::SmartNode<> m_mountainNoise;
    FastNoise::SmartNode<> m_caveNoise;
    WorldConfig m_config;

public:
    DefaultGenerator(WorldConfig config) : m_config(config) { Init(); }

    void Init() override {
        // 1. Base Rolling Hills
        auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
        auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnFractal->SetSource(fnPerlin);
        fnFractal->SetOctaveCount(4);
        m_baseNoise = fnFractal;

        // 2. Sharp Mountains
        auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
        auto fnFractal2 = FastNoise::New<FastNoise::FractalFBm>();
        fnFractal2->SetSource(fnSimplex);
        fnFractal2->SetOctaveCount(3);
        m_mountainNoise = fnFractal2;

        // 3. Caves (3D Noise)
        m_caveNoise = FastNoise::New<FastNoise::Perlin>();
    }

    int GetHeight(float x, float z) const override {
        float nx = x * m_config.scale;
        float nz = z * m_config.scale;
        
        // Base Terrain
        float baseVal = m_baseNoise->GenSingle2D(nx * m_config.hillFrequency, nz * m_config.hillFrequency, m_config.seed);
        float hillHeight = baseVal * m_config.hillAmplitude;
        
        // Mountains
        float mountainVal = m_mountainNoise->GenSingle2D(nx * m_config.mountainFrequency, nz * m_config.mountainFrequency, m_config.seed + 1);
        mountainVal = std::abs(mountainVal); 
        mountainVal = std::pow(mountainVal, 2.0f); 
        float mountainHeight = mountainVal * m_config.mountainAmplitude;
        
        return m_config.seaLevel + (int)(hillHeight + mountainHeight);
    }

    // --- TEXTURE / BLOCK DEFINITION ---
    // GUIDE: Map your logic to Texture IDs here.
    // blockID 1 = Grass, 2 = Dirt, 3 = Stone, 4 = Snow, etc.
    // These IDs correspond to the layer index in your Texture Array.
    uint8_t GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const override {
        int wy = (int)y;
        
        // Cave Check
        if (m_config.enableCaves && lodScale == 1) { // Only caves at LOD 0
             float val = m_caveNoise->GenSingle3D(x * 0.02f, y * 0.04f, z * 0.02f, m_config.seed);
             if (val > m_config.caveThreshold) return 0; // Air
        }

        if (wy > heightAtXZ) return 0; // Air above terrain

        // Surface Logic
        if (wy == heightAtXZ) {
            // Top Block
            if (wy > 550) return 4; // Snow
            if (wy > 350) return 1; // Grass
            return 2;              // Sand/Dirt
        } 
        else if (wy > heightAtXZ - (4 * lodScale)) {
            // Sub-surface (4 blocks deep)
            if (wy > 550) return 4; // Snow
            if (wy > 350) return 2; // Dirt
            return 5;              // SandStone
        }
        
        return 3; // Deep Stone
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        int worldX = cx * CHUNK_SIZE * scale;
        int worldZ = cz * CHUNK_SIZE * scale;
        int size = CHUNK_SIZE * scale;

        // Sample 5 points (corners + center) for rough AABB calculation
        int h1 = GetHeight(worldX, worldZ);
        int h2 = GetHeight(worldX + size, worldZ);
        int h3 = GetHeight(worldX, worldZ + size);
        int h4 = GetHeight(worldX + size, worldZ + size);
        int h5 = GetHeight(worldX + size/2, worldZ + size/2);

        minH = std::min({h1, h2, h3, h4, h5});
        maxH = std::max({h1, h2, h3, h4, h5});
    }
};

// ================================================================================================
//                                       CHUNK STRUCTURES
// ================================================================================================

enum class ChunkState { MISSING, GENERATING, GENERATED, MESHING, MESHED, ACTIVE };

struct ChunkNode {
    Chunk chunk;
    glm::vec3 position;
    int cx, cy, cz; 
    int lod;      
    int scale;    
    
    std::vector<PackedVertex> cachedMesh; 
    std::atomic<ChunkState> state{ChunkState::MISSING};
    
    long long gpuOffset = -1; 
    size_t vertexCount = 0;
    int64_t id; 

    glm::vec3 minAABB;
    glm::vec3 maxAABB;

    void Reset(int x, int y, int z, int lodLevel) {
        lod = lodLevel;
        scale = 1 << lod; 

        cx = x; cy = y; cz = z;
        
        float size = (float)(CHUNK_SIZE * scale);
        position = glm::vec3(x * size, y * size, z * size);
        
        minAABB = position;
        maxAABB = position + glm::vec3(size);

        chunk.worldX = (int)position.x;
        chunk.worldY = (int)position.y;
        chunk.worldZ = (int)position.z;
        
        state = ChunkState::MISSING;
        cachedMesh.clear();
        chunk.isUniform = false;
        gpuOffset = -1;
        vertexCount = 0;
    }
};

// Helper for hashing chunk coordinates
inline int64_t ChunkKey(int x, int y, int z, int lod) {
    uint64_t ulod = (uint64_t)(lod & 0x7) << 61; 
    uint64_t ux = (uint64_t)(uint32_t)x & 0xFFFFF; 
    uint64_t uz = (uint64_t)(uint32_t)z & 0xFFFFF; 
    uint64_t uy = (uint64_t)(uint32_t)y & 0x1FFFFF;   
    return ulod | (ux << 41) | (uz << 21) | uy;
}

// ================================================================================================
//                                          WORLD CLASS
// ================================================================================================

class World {
private:
    WorldConfig m_config;
    std::unique_ptr<ITerrainGenerator> m_generator;
    
    // --- CHUNK STORAGE ---
    // Protected by m_chunksMutex
    std::unordered_map<int64_t, ChunkNode*> m_chunks;
    std::shared_mutex m_chunksMutex; // R/W Lock for the map

    ObjectPool<ChunkNode> m_chunkPool;

    // --- ASYNC QUEUES ---
    std::mutex m_queueMutex;
    std::queue<ChunkNode*> m_generatedQueue; 
    std::queue<ChunkNode*> m_meshedQueue;    
    
    ThreadPool m_pool; 

    // --- ASYNC LOD UPDATING ---
    // Struct to hold results from the calculation worker
    struct ChunkRequest { int x, y, z; int lod; int distSq; };
    struct LODUpdateResult {
        std::vector<ChunkRequest> chunksToLoad;
        std::vector<int64_t> chunksToUnload;
        size_t loadIndex = 0;
    };
    
    std::atomic<bool> m_isLODWorkerRunning { false };
    std::mutex m_lodResultMutex;
    std::unique_ptr<LODUpdateResult> m_pendingLODResult = nullptr;
    glm::vec3 m_lastLODCalculationPos = glm::vec3(-9999.0f); // For optimization
    
    // ------------------------------------


    // --- LOD TRACKING ---
    int lastPx[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    int lastPz[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    int lastUnloadPx[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    int lastUnloadPz[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    bool m_lodComplete[8] = {false};

    int m_frameCounter = 0; 
    std::atomic<bool> m_shutdown{false};

    bool m_freezeLODs = false; // Toggle to pause chunk updates

    // --- GPU RESOURCES ---
    std::unique_ptr<GpuMemoryManager> m_gpuMemory;
    std::unique_ptr<GpuCuller> m_culler;
    GLuint m_dummyVAO = 0; 
    
    // TEXTURE ARRAY HANDLE
    // Bind your GL_TEXTURE_2D_ARRAY here via SetTextureArray()
    GLuint m_textureArrayID = 0;


    friend class ImGuiManager;


    // Checks if the 8 sub-chunks of this parent are fully active (visible).
    // Used to prevent holes when unloading parents.
    bool AreChildrenReady(int cx, int cy, int cz, int lod) {
        if (lod == 0) return true; // LOD 0 has no children
        
        int childLod = lod - 1;
        int scale = 1 << childLod; // Scale of the children
        
        int startX = cx * 2;
        int startY = cy * 2;
        int startZ = cz * 2;

        for (int x = 0; x < 2; x++) {
            for (int z = 0; z < 2; z++) {
                
                // Optimization: Compute bounds for this column once
                // This determines if children in this column SHOULD exist.
                int minH, maxH;
                m_generator->GetHeightBounds((startX + x), (startZ + z), scale, minH, maxH);
                int chunkYStart = (minH / (CHUNK_SIZE * scale)) - 1; 
                int chunkYEnd = (maxH / (CHUNK_SIZE * scale)) + 1;

                for (int y = 0; y < 2; y++) {
                    int64_t key = ChunkKey(startX + x, startY + y, startZ + z, childLod);
                    auto it = m_chunks.find(key);
                    
                    if (it != m_chunks.end()) {
                         // Child is in pipeline. Must be ACTIVE (visible).
                         if (it->second->state.load() != ChunkState::ACTIVE) return false;
                    } else {
                        // Child is missing from map.
                        // Is it missing because it's empty air (Valid), or pending load (Invalid)?
                        int myY = startY + y;
                        if (myY >= chunkYStart && myY <= chunkYEnd) {
                            // It lies within terrain bounds, so it SHOULD exist.
                            // Since it's missing, it hasn't loaded yet.
                            return false; 
                        }
                        // Else: It's outside terrain bounds (Air/Deep Underground). Treat as Ready.
                    }
                }
            }
        }
        return true;
    }

    // --- HELPER: Is Parent Active? ---
    // Checks if the Parent chunk (Lower Detail) is ready.
    // Used to prevent holes when unloading children (moving away).
    bool IsParentReady(int cx, int cy, int cz, int lod) {
        if (lod >= m_config.lodCount - 1) return true; // Lowest LOD has no parent
        
        int parentLod = lod + 1;
        // Integer division >> 1 works correctly for our coord system (spatial subdivision)
        int px = cx >> 1;
        int py = cy >> 1;
        int pz = cz >> 1;

        int64_t key = ChunkKey(px, py, pz, parentLod);
        auto it = m_chunks.find(key);
        
        // If parent missing or not active, we are NOT ready to unload.
        // Waiting prevents a hole in the world.
        if (it == m_chunks.end() || it->second->state.load() != ChunkState::ACTIVE) {
            return false;
        }
        return true;
    }

    //bool m_viewNormals = false;

public:
    // ================================================================================================
    //                                     LIFECYCLE
    // ================================================================================================

    World(WorldConfig config) : m_config(config) {
        // PLUG IN: Swapping generators
        // To use a custom generator, change this line:
        m_generator = std::make_unique<DefaultGenerator>(m_config);

        size_t maxChunks = 0;
        for(int i=0; i<m_config.lodCount; i++) {
            int r = m_config.lodRadius[i];
            maxChunks += (r * 2 + 1) * (r * 2 + 1) * m_config.worldHeightChunks;
        }
        size_t capacity = maxChunks + 5000; 
        
        m_chunkPool.Init(capacity); 
        m_gpuMemory = std::make_unique<GpuMemoryManager>(config.VRAM_HEAP_ALLOCATION_MB * 1024 * 1024); // 1GB Voxel Heap

        m_culler = std::make_unique<GpuCuller>(capacity);
        glCreateVertexArrays(1, &m_dummyVAO);
    }

    ~World() { 
        Dispose();
    }
    
    void Dispose() {
        m_shutdown = true;
        if (m_dummyVAO) { glDeleteVertexArrays(1, &m_dummyVAO); m_dummyVAO = 0; }
        m_culler.reset();
    }

    void SetTextureArray(GLuint textureID) {
        m_textureArrayID = textureID;
    }

    void setCubeDebugMode(int mode) { 
        m_config.cubeDebugMode = mode;
    }

    void SetLODFreeze(bool freeze) { m_freezeLODs = freeze; }
    bool GetLODFreeze() const { return m_freezeLODs; }

    // // Toggles between 0 and 1
    // void ToggleViewNormals() { 
    //     m_config.viewNormals = !m_config.viewNormals; 
    // }
    
    const WorldConfig& GetConfig() const { return m_config; }

    // ================================================================================================
    //                                     MAIN UPDATE LOOP
    // ================================================================================================

    void Update(glm::vec3 cameraPos) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("World::Update Total");
        
        // --- AUTO-DEFRAG / RELOAD ---
        // Checks fragmentation of the persistent mapped buffer.
        // If fragmentation > 60%, we wipe and reload to prevent allocation failures.
        if (m_gpuMemory->GetFragmentationRatio() > 0.6f) { 
             Reload(m_config);
             return;
        }

        ProcessQueues(); 

        // Skip Chunk Loading/Unloading if frozen
        if (m_freezeLODs) return; 
        
        // --- [CHANGE] ASYNC DISPATCH ---
        // Instead of calculating LODs on main thread, we delegate to worker.
        UpdateLODs_Async(cameraPos);
        
        m_frameCounter++;
    }

    // ================================================================================================
    //                                  ASYNC TASK PROCESSING
    // ================================================================================================

    void ProcessQueues() {
        if(m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("World::ProcessQueues");
        
        std::vector<ChunkNode*> nodesToMesh;
        std::vector<ChunkNode*> nodesToUpload;
        
        // 1. HARVEST RESULTS
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            
            // Limit throughput to prevent frame stutters, but keep it high to clear backlog.
            int limitGen = 1024; 
            while (!m_generatedQueue.empty() && limitGen > 0) {
                nodesToMesh.push_back(m_generatedQueue.front());
                m_generatedQueue.pop();
                limitGen--;
            }
            
            int limitUpload = 512; 
            while (!m_meshedQueue.empty() && limitUpload > 0) {
                nodesToUpload.push_back(m_meshedQueue.front());
                m_meshedQueue.pop();
                limitUpload--;
            }
        }

        // 2. DISPATCH MESHING TASKS
        for (ChunkNode* node : nodesToMesh) {
            if(m_shutdown) return; 
            if (node->state == ChunkState::GENERATING) {
                // Uniform chunks (all air/solid) don't need meshing
                if (node->chunk.isUniform && node->chunk.uniformID == 0) {
                    node->state = ChunkState::ACTIVE;
                } else {
                    node->state = ChunkState::MESHING;
                    m_pool.enqueue([this, node]() { this->Task_Mesh(node); });
                }
            }
        }

        // 3. UPLOAD TO GPU
        // This runs on the Main Thread (OpenGL Context required for non-persistent mapped buffers, 
        // though we use persistent mapping here so it's just a memcpy).
        for (ChunkNode* node : nodesToUpload) {
            if(m_shutdown) return; 
            if (node->state == ChunkState::MESHING) {
                if (!node->cachedMesh.empty()) {
                    size_t bytes = node->cachedMesh.size() * sizeof(PackedVertex);
                    long long offset = m_gpuMemory->Allocate(bytes, sizeof(PackedVertex));
                    
                    if (offset != -1) {
                        m_gpuMemory->Upload(offset, node->cachedMesh.data(), bytes);
                        node->gpuOffset = offset;
                        node->vertexCount = node->cachedMesh.size();
                        node->cachedMesh.clear(); 
                        node->cachedMesh.shrink_to_fit();
                        
                        // Register with GPU Driven Culler
                        m_culler->AddOrUpdateChunk(
                            node->id, 
                            node->minAABB,
                            (float)node->scale, 
                            (size_t)(offset / sizeof(PackedVertex)), 
                            node->vertexCount
                        );
                    } 
                }
                node->state = ChunkState::ACTIVE;
            }
        }
    }

    // ================================================================================================
    //                             ASYNC LOD SYSTEM
    // ================================================================================================

    // This runs on the WORKER THREAD. Does the math, touches no GPU.
    void Task_CalculateLODs(glm::vec3 cameraPos) {
        if(m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] World::LOD Calc");
        auto result = std::make_unique<LODUpdateResult>();

        // 1. SNAPSHOT STATE (Read-Only Lock)
        std::shared_lock<std::shared_mutex> readLock(m_chunksMutex);

        // --- PART A: CALCULATE UNLOADS ---
        for (const auto& pair : m_chunks) {
            ChunkNode* node = pair.second;
            int lod = node->lod;
            int scale = 1 << lod;
            
            int camX = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
            int camZ = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));
            
            int dx = abs(node->cx - camX);
            int dz = abs(node->cz - camZ);
            
            bool shouldUnload = false;

            // 1. Outer Radius (Too Far - Transition to LOWER detail)
            if (dx > m_config.lodRadius[lod] || dz > m_config.lodRadius[lod]) {
                 // [FIX] Wait for Parent (Lower Detail) to be ready before unloading this child.
                 // This overlaps them, preventing the flash/hole.
                 if (IsParentReady(node->cx, node->cy, node->cz, lod)) {
                     shouldUnload = true;
                 }
            }
            // 2. Inner Radius (Too Close - Transition to HIGHER detail)
            else if (lod > 0) {
                int prevRadius = m_config.lodRadius[lod - 1];
                int innerBoundary = ((prevRadius + 1) / 2);
                
                if (dx < innerBoundary && dz < innerBoundary) {
                    // [FIX] Wait for Children (Higher Detail) to be ready before unloading this parent.
                    if (AreChildrenReady(node->cx, node->cy, node->cz, lod)) {
                        shouldUnload = true;
                    }
                }
            }

            if (shouldUnload) {
                ChunkState s = node->state.load();
                if (s != ChunkState::GENERATING && s != ChunkState::MESHING) {
                    result->chunksToUnload.push_back(pair.first);
                }
            }
        }

        // --- PART B: CALCULATE LOADS ---
        static std::vector<std::pair<int, int>> spiralOffsets;
        static std::once_flag flag;
        std::call_once(flag, [](){
            int maxR = 128; 
            for (int x = -maxR; x <= maxR; x++) {
                for (int z = -maxR; z <= maxR; z++) {
                    spiralOffsets.push_back({x, z});
                }
            }
            std::sort(spiralOffsets.begin(), spiralOffsets.end(), [](const std::pair<int,int>& a, const std::pair<int,int>& b){
                return (a.first*a.first + a.second*a.second) < (b.first*b.first + b.second*b.second);
            });
        });

        for(int lod = 0; lod < m_config.lodCount; lod++) {
            int scale = 1 << lod;
            int px = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
            int pz = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));
            
            int r = m_config.lodRadius[lod];
            int rSq = r * r; 
            
            int minR = 0;
            if (lod > 0) {
                int prevR = m_config.lodRadius[lod - 1];
                minR = ((prevR + 1) / 2); 
            }

            for (const auto& offset : spiralOffsets) {
                int distSq = offset.first*offset.first + offset.second*offset.second;
                if (distSq > (rSq * 2 + 100)) break; 
                
                if (std::abs(offset.first) > r || std::abs(offset.second) > r) continue;

                // Donut Check
                if (lod > 0) {
                     if (std::abs(offset.first) < minR && std::abs(offset.second) < minR) continue;
                }

                int x = px + offset.first;
                int z = pz + offset.second;
                
                // Height Cull
                int minH, maxH;
                m_generator->GetHeightBounds(x, z, scale, minH, maxH);
                int chunkYStart = std::max(0, (minH / (CHUNK_SIZE * scale)) - 1); 
                int chunkYEnd = std::min(m_config.worldHeightChunks - 1, (maxH / (CHUNK_SIZE * scale)) + 1);

                for (int y = chunkYStart; y <= chunkYEnd; y++) {
                    int64_t key = ChunkKey(x, y, z, lod);
                    
                    if (m_chunks.find(key) == m_chunks.end()) {
                        int dx = x - px; 
                        int dz = z - pz; 
                        int chunkWorldY = y * CHUNK_SIZE * scale;
                        int dy = (chunkWorldY - (int)cameraPos.y) / (CHUNK_SIZE * scale); 
                        int distMetric = dx*dx + dz*dz + (dy*dy); 
                        
                        result->chunksToLoad.push_back({x, y, z, lod, distMetric});
                    }
                }
            }
        }
        
        readLock.unlock(); 

        std::sort(result->chunksToLoad.begin(), result->chunksToLoad.end(), 
            [](const ChunkRequest& a, const ChunkRequest& b){ return a.distSq < b.distSq; });

        std::lock_guard<std::mutex> lock(m_lodResultMutex);
        m_pendingLODResult = std::move(result);
        m_isLODWorkerRunning = false;
    }

    // This runs on the MAIN THREAD. Commits the results.
    void UpdateLODs_Async(glm::vec3 cameraPos) {
        
        {
            Engine::Profiler::ScopedTimer timer("World::ApplyLODs");
            std::lock_guard<std::mutex> lock(m_lodResultMutex);
            if (m_pendingLODResult) {
                
                std::unique_lock<std::shared_mutex> writeLock(m_chunksMutex);

                if (m_pendingLODResult->loadIndex == 0) {
                    for (int64_t key : m_pendingLODResult->chunksToUnload) {
                        auto it = m_chunks.find(key);
                        if (it != m_chunks.end()) {
                            ChunkNode* node = it->second;
                            if (node->gpuOffset != -1) {
                                m_culler->RemoveChunk(node->id);
                                m_gpuMemory->Free(node->gpuOffset, node->vertexCount * sizeof(PackedVertex));
                                node->gpuOffset = -1;
                            }
                            m_chunkPool.Release(node);
                            m_chunks.erase(it);
                        }
                    }
                }

                int queued = 0;
                int MAX_PER_FRAME = 50; 
                
                size_t& idx = m_pendingLODResult->loadIndex;
                const auto& loadList = m_pendingLODResult->chunksToLoad;

                while (idx < loadList.size() && queued < MAX_PER_FRAME) {
                    const auto& req = loadList[idx];
                    idx++;
                    
                    int64_t key = ChunkKey(req.x, req.y, req.z, req.lod);
                    if (m_chunks.find(key) == m_chunks.end()) {
                        ChunkNode* newNode = m_chunkPool.Acquire();
                        if (newNode) {
                            newNode->Reset(req.x, req.y, req.z, req.lod);
                            newNode->id = key; 
                            m_chunks[key] = newNode;
                            
                            newNode->state = ChunkState::GENERATING;
                            m_pool.enqueue([this, newNode]() { this->Task_Generate(newNode); });
                            queued++;
                        }
                    }
                }
                
                if (idx >= loadList.size()) {
                    m_pendingLODResult = nullptr;
                }
            }
        }

        if (!m_isLODWorkerRunning && !m_pendingLODResult) {
            float distSq = glm::dot(cameraPos - m_lastLODCalculationPos, cameraPos - m_lastLODCalculationPos);
            if (distSq > 64.0f) { 
                m_lastLODCalculationPos = cameraPos;
                m_isLODWorkerRunning = true;
                m_pool.enqueue([this, cameraPos](){ this->Task_CalculateLODs(cameraPos); });
            }
        }
    }

    // ================================================================================================
    //                                         RENDERING
    // ================================================================================================

    void Draw(Shader& shader, const glm::mat4& viewProj, const glm::mat4& cullViewMatrix, const glm::mat4& proj, GLuint depthPyramidTex) {
        if(m_shutdown) return;

        {
            Engine::Profiler::Get().BeginGPU("GPU: Buffer and Cull Compute"); 
            m_culler->Cull(cullViewMatrix, proj, depthPyramidTex);
            Engine::Profiler::Get().EndGPU();
        }

        {
            Engine::Profiler::Get().BeginGPU("GPU: MDI DRAW"); 

            shader.use();
            glUniformMatrix4fv(glGetUniformLocation(shader.ID, "u_ViewProjection"), 1, GL_FALSE, glm::value_ptr(viewProj));
            glUniform1i(glGetUniformLocation(shader.ID, "u_DebugMode"), m_config.cubeDebugMode);
            
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_gpuMemory->GetID());

            if (m_textureArrayID != 0) {
                 glActiveTexture(GL_TEXTURE0);
                 glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureArrayID);
                 shader.setInt("u_Textures", 0);
            }

            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);

            m_culler->DrawIndirect(m_dummyVAO); 
            glDisable(GL_POLYGON_OFFSET_FILL);

            Engine::Profiler::Get().EndGPU();
        }
    }

    GpuCuller* GetCuller() { return m_culler.get(); }





    void RenderHiZDebug(Shader* debugShader, GLuint hizTexture, int mipLevel, int screenW, int screenH) {
    // 1. Setup State
    glDisable(GL_DEPTH_TEST); // Always draw on top
    glDisable(GL_CULL_FACE);
    
    debugShader->use();
    
    // 2. Bind the Hi-Z Texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hizTexture);
    
    // IMPORTANT: Ensure the sampler allows reading lower mips
    // If you used the sampler from GpuCuller, it effectively does this, 
    // but for debugging, standard GL_NEAREST is fine.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0); 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 10); // Ensure access to low mips

    // 3. Set Uniforms
    debugShader->setInt("u_DepthTexture", 0);
    debugShader->setInt("u_MipLevel", mipLevel);
    debugShader->setVec2("u_ScreenSize", glm::vec2(screenW, screenH));

    // 4. Draw Full Screen Triangle (Vertex-less)
    // We draw 3 vertices, the vertex shader positions them to cover the screen
    glBindVertexArray(m_dummyVAO); // Core profile requires a VAO bound, even if empty
    glDrawArrays(GL_TRIANGLES, 0, 3);
    
    // 5. Restore State
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

    // ================================================================================================
    //                                         RESET / RELOAD
    // ================================================================================================

    void Reload(WorldConfig newConfig) {
        //Engine::Profiler::ScopedTimer timer("World::Reload");
        m_config = newConfig;
        
        m_generator = std::make_unique<DefaultGenerator>(m_config);
        
        {
            std::unique_lock<std::shared_mutex> lock(m_chunksMutex);
            for (auto& pair : m_chunks) {
                ChunkNode* node = pair.second;
                if (node->gpuOffset != -1) {
                    m_culler->RemoveChunk(node->id); 
                    m_gpuMemory->Free(node->gpuOffset, node->vertexCount * sizeof(PackedVertex));
                    node->gpuOffset = -1;
                }
                m_chunkPool.Release(node);
            }
            m_chunks.clear();
        }

        // Reset tracking vars
        // [FIX] Reset position to negative infinity to force immediate update on next frame
        m_lastLODCalculationPos = glm::vec3(-99999.0f);
        m_pendingLODResult = nullptr;
    }



    // ================================================================================================
    //                                      WORKER TASKS
    // ================================================================================================
private:


    void Task_Generate(ChunkNode* node) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] Task: Generate");
        
        FillChunk(node->chunk, node->cx, node->cy, node->cz, node->scale);
        
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_shutdown) return;
        m_generatedQueue.push(node);
    }

    void Task_Mesh(ChunkNode* node) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] Task: Mesh"); 

        LinearAllocator<PackedVertex> threadAllocator(1000000); 
        MeshChunk(node->chunk, threadAllocator, false);
        
        // Copy to node storage
        node->cachedMesh.assign(threadAllocator.Data(), threadAllocator.Data() + threadAllocator.Count());
        
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_shutdown) return;
        m_meshedQueue.push(node);
    }

    // Delegates logic to the Abstract Terrain Generator
    void FillChunk(Chunk& chunk, int cx, int cy, int cz, int scale) {
        chunk.worldX = cx * CHUNK_SIZE * scale;
        chunk.worldY = cy * CHUNK_SIZE * scale;
        chunk.worldZ = cz * CHUNK_SIZE * scale;
        
        int chunkBottomY = chunk.worldY;
        int chunkTopY = chunk.worldY + (CHUNK_SIZE * scale);

        // Pre-calculate heights for optimization
        int heights[CHUNK_SIZE_PADDED][CHUNK_SIZE_PADDED];
        int minHeight = 99999;
        int maxHeight = -99999;

        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                float wx = (float)(chunk.worldX + (x - 1) * scale);
                float wz = (float)(chunk.worldZ + (z - 1) * scale);
                
                int h = m_generator->GetHeight(wx, wz);
                if (scale > 1) { h = (h / scale) * scale; } // Snap to grid for LOD
                
                heights[x][z] = h;
                if (h < minHeight) minHeight = h;
                if (h > maxHeight) maxHeight = h;
            }
        }

        // Fast Fail: Empty Air
        if (maxHeight < chunkBottomY) { 
            chunk.FillUniform(0); 
            return; 
        } 
        
        // Fast Fail: Solid Underground (only if caves disabled)
        if (minHeight > chunkTopY && !m_config.enableCaves) { 
            chunk.FillUniform(1); 
            return; 
        } 

        std::memset(chunk.voxels, 0, sizeof(chunk.voxels));
        
        // Voxel Fill Loop
        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                int height = heights[x][z]; 
                
                // Convert world height to local Y index
                int localMaxY = (height - chunkBottomY) / scale;
                localMaxY = std::min(localMaxY + 2, CHUNK_SIZE_PADDED - 1);
                
                if (localMaxY < 0) continue; 

                for (int y = 0; y <= localMaxY; y++) {
                    int wy = chunk.worldY + (y - 1) * scale; 
                    float wx = (float)(chunk.worldX + (x - 1) * scale);
                    float wz = (float)(chunk.worldZ + (z - 1) * scale);

                    uint8_t blockID = m_generator->GetBlock(wx, (float)wy, wz, height, scale);
                    
                    if (blockID != 0) {
                        chunk.Set(x, y, z, blockID);
                    }
                }
            }
        }
    }


};