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
    int lodRadius[8] = { 10, 16, 24, 32, 48, 0, 0, 0 }; 
    
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
            if (wy > 55) return 4; // Snow
            if (wy > 35) return 1; // Grass
            return 2;              // Sand/Dirt
        } 
        else if (wy > heightAtXZ - (4 * lodScale)) {
            // Sub-surface (4 blocks deep)
            if (wy > 55) return 4; // Snow
            if (wy > 35) return 2; // Dirt
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

    // --- LOD TRACKING ---
    int lastPx[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    int lastPz[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    int lastUnloadPx[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    int lastUnloadPz[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    bool m_lodComplete[8] = {false};

    int m_frameCounter = 0; 
    std::atomic<bool> m_shutdown{false};

    // --- GPU RESOURCES ---
    std::unique_ptr<GpuMemoryManager> m_gpuMemory;
    std::unique_ptr<GpuCuller> m_culler;
    GLuint m_dummyVAO = 0; 
    
    // TEXTURE ARRAY HANDLE
    // Bind your GL_TEXTURE_2D_ARRAY here via SetTextureArray()
    GLuint m_textureArrayID = 0;

    struct ChunkRequest { int x, y, z; int lod; int distSq; };

    friend class ImGuiManager;

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

    // // Toggles between 0 and 1
    // void ToggleViewNormals() { 
    //     m_config.viewNormals = !m_config.viewNormals; 
    // }
    
    const WorldConfig& GetConfig() const { return m_config; }

    // ================================================================================================
    //                                     MAIN UPDATE LOOP
    // ================================================================================================

    void Update(glm::vec3 cameraPos) {
        Engine::Profiler::ScopedTimer timer("World::Update Total");
        if (m_shutdown) return;
        
        // --- AUTO-DEFRAG / RELOAD ---
        // Checks fragmentation of the persistent mapped buffer.
        // If fragmentation > 60%, we wipe and reload to prevent allocation failures.
        if (m_gpuMemory->GetFragmentationRatio() > 0.6f) { 
             Reload(m_config);
             return;
        }

        ProcessQueues(); 
        
        // --- PRIORITY UPDATING ---
        // We iterate from LOD 0 (Nearest) to LOD N (Farthest).
        // This ensures High-Res chunks near the player are queued FIRST.
        for(int i = 0; i < m_config.lodCount; i++) {
            UnloadChunks(cameraPos, i);
            QueueNewChunks(cameraPos, i);
        }
        
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
    //                                     LOD MANAGEMENT
    // ================================================================================================

    void UnloadChunks(glm::vec3 cameraPos, int targetLod) {
        if(m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("World::UnloadChunks");
        
        int scale = 1 << targetLod;
        int camX = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
        int camZ = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));

        // OPTIMIZATION: Coordinate Caching
        // If we remain in the same chunk coordinate, the set of chunks to unload is identical. Skip.
        if (camX == lastUnloadPx[targetLod] && camZ == lastUnloadPz[targetLod]) {
            return;
        }

        lastUnloadPx[targetLod] = camX;
        lastUnloadPz[targetLod] = camZ;
        
        std::vector<int64_t> toRemove;
        
        {
            std::shared_lock<std::shared_mutex> lock(m_chunksMutex);
            for (auto& pair : m_chunks) {
                ChunkNode* node = pair.second;
                if (node->lod != targetLod) continue;
                
                int dx = abs(node->cx - camX);
                int dz = abs(node->cz - camZ);
                
                // 1. UNLOAD TOO FAR (Outer Radius)
                int outerLimit = m_config.lodRadius[targetLod] + 3; 
                bool shouldRemove = false;
                if (dx > outerLimit || dz > outerLimit) {
                    shouldRemove = true;
                }

                // 2. UNLOAD TOO CLOSE (Inner Radius / Donut Hole)
                // Strict boundary logic prevents "Z-Fighting" between LOD layers.
                if (targetLod > 0) {
                    int prevRadius = m_config.lodRadius[targetLod - 1];
                    // Radius of LOD(n-1) in LOD(n) coordinates is exactly half.
                    int innerBoundary = (prevRadius / 2);
                    
                    if (dx < innerBoundary && dz < innerBoundary) {
                        shouldRemove = true;
                    }
                }

                if (shouldRemove) {
                    ChunkState s = node->state.load();
                    if (s != ChunkState::GENERATING && s != ChunkState::MESHING) {
                        toRemove.push_back(pair.first);
                    }
                }
            }
        }

        if (!toRemove.empty()) {
            std::unique_lock<std::shared_mutex> lock(m_chunksMutex);
            for (auto k : toRemove) {
                auto it = m_chunks.find(k);
                if (it != m_chunks.end()) {
                    ChunkNode* node = it->second;
                    if (node->gpuOffset != -1) {
                        // Deregister from GPU Culler
                        m_culler->RemoveChunk(node->id);
                        // Free VRAM
                        m_gpuMemory->Free(node->gpuOffset, node->vertexCount * sizeof(PackedVertex));
                        node->gpuOffset = -1;
                    }
                    m_chunkPool.Release(node);
                    m_chunks.erase(it);
                }
            }
        }
    }

    void QueueNewChunks(glm::vec3 cameraPos, int targetLod) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("World::QueueNewChunks");
        
        int lod = targetLod;
        int scale = 1 << lod;
        int px = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
        int pz = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));

        if (px == lastPx[lod] && pz == lastPz[lod] && m_lodComplete[lod]) return;

        if (px != lastPx[lod] || pz != lastPz[lod]) {
            m_lodComplete[lod] = false;
            lastPx[lod] = px;
            lastPz[lod] = pz;
        }

        // --- SPIRAL GENERATION ---
        // Generates coordinates from center outward.
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

        std::vector<ChunkRequest> missing;
        int r = m_config.lodRadius[lod];
        int rSq = r * r; 

        // Donut Logic: Where does the hole start?
        int minR = 0;
        if (lod > 0) {
            int prevR = m_config.lodRadius[lod - 1];
            minR = (prevR / 2); 
        }

        std::shared_lock<std::shared_mutex> readLock(m_chunksMutex);

        for (const auto& offset : spiralOffsets) {
            int distSq = offset.first*offset.first + offset.second*offset.second;
            
            // Optimization: Stop iterating if outside radius
            if (distSq > (rSq * 2 + 100)) break; 
            
            // Box check for exact radius
            if (std::abs(offset.first) > r || std::abs(offset.second) > r) continue;

            // Donut Check: Skip if inside the hole (covered by higher detail LOD)
            if (lod > 0) {
                 if (std::abs(offset.first) < minR && std::abs(offset.second) < minR) continue;
            }

            int x = px + offset.first;
            int z = pz + offset.second;
            
            // Height Culling: Only generate chunks that actually contain terrain surface
            int minH, maxH;
            m_generator->GetHeightBounds(x, z, scale, minH, maxH);
            
            int chunkYStart = (minH / (CHUNK_SIZE * scale)) - 1; 
            int chunkYEnd = (maxH / (CHUNK_SIZE * scale)) + 1;
            chunkYStart = std::max(0, chunkYStart);
            chunkYEnd = std::min(m_config.worldHeightChunks - 1, chunkYEnd);

            for (int y = chunkYStart; y <= chunkYEnd; y++) {
                int64_t key = ChunkKey(x, y, z, lod);
                
                // If chunk doesn't exist, queue it
                if (m_chunks.find(key) == m_chunks.end()) {
                    int dx = x - px; 
                    int dz = z - pz; 
                    int chunkWorldY = y * CHUNK_SIZE * scale;
                    int dy = (chunkWorldY - (int)cameraPos.y) / (CHUNK_SIZE * scale); 
                    
                    // Simple distance metric for sorting queue
                    int distMetric = dx*dx + dz*dz + (dy*dy); 
                    missing.push_back({x, y, z, lod, distMetric});
                }
            }
        }
        readLock.unlock();

        // Sort by distance (nearest first)
        std::sort(missing.begin(), missing.end(), [](const ChunkRequest& a, const ChunkRequest& b){ return a.distSq < b.distSq; });

        int queued = 0;
        int MAX_TASKS = 2000; 
        
        if (!missing.empty()) {
            std::unique_lock<std::shared_mutex> writeLock(m_chunksMutex);
            for (const auto& req : missing) {
                if (queued >= MAX_TASKS) break;
                
                int64_t key = ChunkKey(req.x, req.y, req.z, req.lod);
                if (m_chunks.find(key) == m_chunks.end()) {
                    ChunkNode* newNode = m_chunkPool.Acquire();
                    if (newNode) {
                        newNode->Reset(req.x, req.y, req.z, req.lod);
                        newNode->id = key; 
                        m_chunks[key] = newNode;
                        
                        // Start Thread Task
                        newNode->state = ChunkState::GENERATING;
                        m_pool.enqueue([this, newNode]() { this->Task_Generate(newNode); });
                        queued++;
                    }
                }
            }
        }

        if (queued < MAX_TASKS) m_lodComplete[lod] = true;
        else m_lodComplete[lod] = false;
    }

    // ================================================================================================
    //                                         RENDERING
    // ================================================================================================

    void Draw(Shader& shader, const glm::mat4& renderViewProj, const glm::mat4& cullViewProj) {
        if(m_shutdown) return;
        //Engine::Profiler::ScopedTimer timer("World::Draw");

        // 1. COMPUTE PASS (CULLING)
        // Filters visible chunks into the Indirect Buffer
        {
            Engine::Profiler::Get().BeginGPU("GPU: Buffer and Cull Compute"); 
            //Engine::Profiler::ScopedTimer timerGPU("GPU: Culling Compute"); 
            m_culler->Cull(cullViewProj, m_gpuMemory->GetID());
            Engine::Profiler::Get().EndGPU();
        }

        // 2. GEOMETRY PASS
        // MultiDrawIndirect using the buffer generated above
        {
            Engine::Profiler::Get().BeginGPU("GPU: MDI DRAW"); 

            shader.use();
            glUniformMatrix4fv(glGetUniformLocation(shader.ID, "u_ViewProjection"), 1, GL_FALSE, glm::value_ptr(renderViewProj));

            // View normals by activating Frag shader debug uniform
            // 0 = Normal, 1 = Debug Normals
            glUniform1i(glGetUniformLocation(shader.ID, "u_DebugMode"), m_config.cubeDebugMode);
            
            // Bind Persistent Vertex Storage (SSBO Binding 0)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_gpuMemory->GetID());

            
            
            // --- TEXTURE ARRAY BINDING ---
            // Ensure your shader has: uniform sampler2DArray u_Textures;
            // And you bind it to Texture Unit 0.
            if (m_textureArrayID != 0) {
                 glActiveTexture(GL_TEXTURE0);
                 glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureArrayID);
                 shader.setInt("u_Textures", 0);
            }

            m_culler->DrawIndirect(m_dummyVAO);
            Engine::Profiler::Get().EndGPU();
        }
    }

    // ================================================================================================
    //                                      WORKER TASKS
    // ================================================================================================

    void Task_Generate(ChunkNode* node) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("Task: Generate");
        
        FillChunk(node->chunk, node->cx, node->cy, node->cz, node->scale);
        
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_shutdown) return;
        m_generatedQueue.push(node);
    }

    void Task_Mesh(ChunkNode* node) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("Task: Mesh"); 

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

    // ================================================================================================
    //                                         RESET / RELOAD
    // ================================================================================================

    void Reload(WorldConfig newConfig) {
        Engine::Profiler::ScopedTimer timer("World::Reload");
        m_config = newConfig;
        
        // Reset Generator
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
        for(int i=0; i<8; i++) { 
            lastPx[i] = -999; 
            lastPz[i] = -999; 
            lastUnloadPx[i] = -999;
            lastUnloadPz[i] = -999;
            m_lodComplete[i] = false; 
        }
    }
};