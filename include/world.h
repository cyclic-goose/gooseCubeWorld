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
#include "screen_quad.h"

// ================================================================================================
//                                     CONFIGURATION
// ================================================================================================

struct WorldConfig {
    int seed = 1337;
    int worldHeightChunks = 64;
    int lodCount = 5; 
    
    // LOD Radii: Defines the distance for each Detail Level.
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

    bool occlusionCulling = true;
    int cubeDebugMode = 4; 
};

// ================================================================================================
//                                  TERRAIN GENERATOR INTERFACE
// ================================================================================================

class ITerrainGenerator {
public:
    virtual ~ITerrainGenerator() = default;
    virtual void Init() = 0; 
    virtual int GetHeight(float x, float z) const = 0;
    virtual uint8_t GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const = 0;
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
        auto fnPerlin = FastNoise::New<FastNoise::Perlin>();
        auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnFractal->SetSource(fnPerlin);
        fnFractal->SetOctaveCount(4);
        m_baseNoise = fnFractal;

        auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
        auto fnFractal2 = FastNoise::New<FastNoise::FractalFBm>();
        fnFractal2->SetSource(fnSimplex);
        fnFractal2->SetOctaveCount(3);
        m_mountainNoise = fnFractal2;

        m_caveNoise = FastNoise::New<FastNoise::Perlin>();
    }

    int GetHeight(float x, float z) const override {
        float nx = x * m_config.scale;
        float nz = z * m_config.scale;
        
        float baseVal = m_baseNoise->GenSingle2D(nx * m_config.hillFrequency, nz * m_config.hillFrequency, m_config.seed);
        float hillHeight = baseVal * m_config.hillAmplitude;
        
        float mountainVal = m_mountainNoise->GenSingle2D(nx * m_config.mountainFrequency, nz * m_config.mountainFrequency, m_config.seed + 1);
        mountainVal = std::abs(mountainVal); 
        mountainVal = std::pow(mountainVal, 2.0f); 
        float mountainHeight = mountainVal * m_config.mountainAmplitude;
        
        return m_config.seaLevel + (int)(hillHeight + mountainHeight);
    }

    uint8_t GetBlock(float x, float y, float z, int heightAtXZ, int lodScale) const override {
        int wy = (int)y;
        
        if (m_config.enableCaves && lodScale == 1) { 
             float val = m_caveNoise->GenSingle3D(x * 0.02f, y * 0.04f, z * 0.02f, m_config.seed);
             if (val > m_config.caveThreshold) return 0; 
        }

        if (wy > heightAtXZ) return 0; 

        if (wy == heightAtXZ) {
            if (wy > 550) return 4; 
            if (wy > 350) return 1; 
            return 2;              
        } 
        else if (wy > heightAtXZ - (4 * lodScale)) {
            if (wy > 550) return 4; 
            if (wy > 350) return 2; 
            return 5;              
        }
        
        return 3; 
    }

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) override {
        int worldX = cx * CHUNK_SIZE * scale;
        int worldZ = cz * CHUNK_SIZE * scale;
        int size = CHUNK_SIZE * scale;

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

// the chunk node acts as the meta data, the what, where, when, how to the chunks actual voxel data
struct ChunkNode {
    Chunk *chunk = nullptr; // ptr to actual raw chunk voxel data
    glm::vec3 position;
    int cx, cy, cz; 
    int lod;      
    int scale;    
    
    std::vector<PackedVertex> cachedMesh; 
    std::atomic<ChunkState> state{ChunkState::MISSING};
    
    long long gpuOffset = -1; 
    size_t vertexCount = 0;
    int64_t id; 

    // Bounding Box (Tight fit to geometry)
    glm::vec3 minAABB;
    glm::vec3 maxAABB;

    // we can store flags for if it is an all air or all solid block to skip rendering
    bool isUniform = false; // True if all blocks are the same ID
    uint8_t uniformID = 0;  // The ID if uniform

    void Reset(int x, int y, int z, int lodLevel) {
        chunk = nullptr;

        lod = lodLevel;
        scale = 1 << lod; 

        cx = x; cy = y; cz = z;
        
        float size = (float)(CHUNK_SIZE * scale);
        position = glm::vec3(x * size, y * size, z * size);
        
        // Default to full size, will be tightened in Generate
        minAABB = position;
        maxAABB = position + glm::vec3(size);

        //chunk.worldX = (int)position.x;
        //chunk.worldY = (int)position.y;
        //chunk.worldZ = (int)position.z;
        
        state = ChunkState::MISSING;
        cachedMesh.clear();
        //chunk.isUniform = false;
        isUniform = false;
        gpuOffset = -1;
        vertexCount = 0;
    }
};

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
    
    std::unordered_map<int64_t, ChunkNode*> m_chunks;
    std::shared_mutex m_chunksMutex; 

    // This is the pool of chunk meta data: position, uniformity, id, LOD, etc
    ObjectPool<ChunkNode> m_chunkPool;
    // This is the pool of strictly voxel data for the chunk, the actual packed vertices for the gpu
    ObjectPool<Chunk> m_voxelPool;

    std::mutex m_queueMutex;
    std::queue<ChunkNode*> m_generatedQueue; 
    std::queue<ChunkNode*> m_meshedQueue;    
    ThreadPool m_pool; 

    struct ChunkRequest { int x, y, z; int lod; int distSq; };
    struct LODUpdateResult {
        std::vector<ChunkRequest> chunksToLoad;
        std::vector<int64_t> chunksToUnload;
        size_t loadIndex = 0;
    };
    
    std::atomic<bool> m_isLODWorkerRunning { false };
    std::mutex m_lodResultMutex;
    std::unique_ptr<LODUpdateResult> m_pendingLODResult = nullptr;
    glm::vec3 m_lastLODCalculationPos = glm::vec3(-9999.0f); 

    int m_frameCounter = 0; 
    std::atomic<bool> m_shutdown{false};
    bool m_freezeLODs = false; 

    std::unique_ptr<GpuMemoryManager> m_gpuMemory;
    std::unique_ptr<GpuCuller> m_culler;
    GLuint m_dummyVAO = 0; 
    GLuint m_textureArrayID = 0;

    friend class ImGuiManager;

    bool AreChildrenReady(int cx, int cy, int cz, int lod) {
        if (lod == 0) return true; 
        
        int childLod = lod - 1;
        int scale = 1 << childLod; 
        
        int startX = cx * 2;
        int startY = cy * 2;
        int startZ = cz * 2;

        for (int x = 0; x < 2; x++) {
            for (int z = 0; z < 2; z++) {
                int minH, maxH;
                m_generator->GetHeightBounds((startX + x), (startZ + z), scale, minH, maxH);
                int chunkYStart = (minH / (CHUNK_SIZE * scale)) - 1; 
                int chunkYEnd = (maxH / (CHUNK_SIZE * scale)) + 1;

                for (int y = 0; y < 2; y++) {
                    int64_t key = ChunkKey(startX + x, startY + y, startZ + z, childLod);
                    auto it = m_chunks.find(key);
                    
                    if (it != m_chunks.end()) {
                         if (it->second->state.load() != ChunkState::ACTIVE) return false;
                    } else {
                        int myY = startY + y;
                        if (myY >= chunkYStart && myY <= chunkYEnd) {
                            return false; 
                        }
                    }
                }
            }
        }
        return true;
    }

    bool IsParentReady(int cx, int cy, int cz, int lod) {
        if (lod >= m_config.lodCount - 1) return true; 
        
        int parentLod = lod + 1;
        int px = cx >> 1;
        int py = cy >> 1;
        int pz = cz >> 1;

        int64_t key = ChunkKey(px, py, pz, parentLod);
        auto it = m_chunks.find(key);
        
        if (it == m_chunks.end() || it->second->state.load() != ChunkState::ACTIVE) {
            return false;
        }
        return true;
    }

public:
    World(WorldConfig config) : m_config(config) {
        m_generator = std::make_unique<DefaultGenerator>(m_config);

        size_t maxChunks = 0;
        for(int i=0; i<m_config.lodCount; i++) {
            int r = m_config.lodRadius[i];
            maxChunks += (r * 2 + 1) * (r * 2 + 1) * m_config.worldHeightChunks;
        }
        size_t capacity = maxChunks + 5000; 
        
        m_chunkPool.Init(capacity); 
        m_voxelPool.Init(capacity); // HOW MANY BYTES TO ALLOC FOR ONE CHUNK 
        m_gpuMemory = std::make_unique<GpuMemoryManager>(config.VRAM_HEAP_ALLOCATION_MB * 1024 * 1024); 

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

    void SetTextureArray(GLuint textureID) { m_textureArrayID = textureID; }
    void setCubeDebugMode(int mode) { m_config.cubeDebugMode = mode; }
    void setOcclusionCulling (bool mode){ m_config.occlusionCulling = mode; }
    bool getOcclusionCulling () { return m_config.occlusionCulling; }
    void SetLODFreeze(bool freeze) { m_freezeLODs = freeze; }
    bool GetLODFreeze() const { return m_freezeLODs; }
    const WorldConfig& GetConfig() const { return m_config; }

    void Update(glm::vec3 cameraPos) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("World::Update Total");
        
        if (m_gpuMemory->GetFragmentationRatio() > 0.6f) { 
             Reload(m_config);
             return;
        }

        ProcessQueues(); 

        if (m_freezeLODs) return; 
        UpdateLODs_Async(cameraPos);
        m_frameCounter++;
    }

    void ProcessQueues() {
        if(m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("World::ProcessQueues");
        
        std::vector<ChunkNode*> nodesToMesh;
        std::vector<ChunkNode*> nodesToUpload;
        
        { // Pop from thread safe queues only
            std::lock_guard<std::mutex> lock(m_queueMutex);
            
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

        // dispatch to mesher when a node is ready
        for (ChunkNode* node : nodesToMesh) {
            if(m_shutdown) return; 

            if (node->state == ChunkState::GENERATING) {

                if (node->isUniform) {
                    // mark it as active (skips meshing)
                    node->state = ChunkState::ACTIVE;

                } else {
                    // must be actual terrain, queue threadpool for meshing
                    node->state = ChunkState::MESHING;
                    m_pool.enqueue([this, node]() { this->Task_Mesh(node); });
                }
            }
        }

        for (ChunkNode* node : nodesToUpload) {
            if(m_shutdown) return; 

            if (node->state == ChunkState::MESHING) {
                // only upload if we actually generated vertices (in normal ram right now)
                if (!node->cachedMesh.empty()) {
                    size_t bytes = node->cachedMesh.size() * sizeof(PackedVertex);
                    long long offset = m_gpuMemory->Allocate(bytes, sizeof(PackedVertex));
                    //long long offset = m_gpuMemory->Allocate(bytes, 256); // FORCE 256 BYTE CACHE LINE ALIGNMENT
                    
                    if (offset != -1) {
                        m_gpuMemory->Upload(offset, node->cachedMesh.data(), bytes);
                        node->gpuOffset = offset;
                        node->vertexCount = node->cachedMesh.size();

                        // clear cpu side cache to save ram
                        // note this is clearing the nodes cached mesh (who, when, where) but not the m_voxelPool data (the what)
                        node->cachedMesh.clear(); 
                        node->cachedMesh.shrink_to_fit();
                        
                        // pass bounding box to culler
                        m_culler->AddOrUpdateChunk(
                            node->id, 
                            node->minAABB,
                            node->maxAABB,
                            (float)node->scale, 
                            (size_t)(offset / sizeof(PackedVertex)), 
                            node->vertexCount
                        );

                    } 
                }

                // can release chunk from ram once its uploaded to VRAM
                // if !nullptr
                // THIS is clearing the voxel data from memory
                if (node->chunk)
                {
                    m_voxelPool.Release(node->chunk);
                    node->chunk = nullptr;
                } 

                node->state = ChunkState::ACTIVE;
            }
        }
    }

    void Task_CalculateLODs(glm::vec3 cameraPos) {
        if(m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] World::LOD Calc");
        auto result = std::make_unique<LODUpdateResult>();

        std::shared_lock<std::shared_mutex> readLock(m_chunksMutex);

        for (const auto& pair : m_chunks) {
            ChunkNode* node = pair.second;
            int lod = node->lod;
            int scale = 1 << lod;
            
            int camX = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
            int camZ = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));
            
            int dx = abs(node->cx - camX);
            int dz = abs(node->cz - camZ);
            
            bool shouldUnload = false;

            if (dx > m_config.lodRadius[lod] || dz > m_config.lodRadius[lod]) {
                 if (IsParentReady(node->cx, node->cy, node->cz, lod)) {
                     shouldUnload = true;
                 }
            }
            else if (lod > 0) {
                int prevRadius = m_config.lodRadius[lod - 1];
                int innerBoundary = ((prevRadius + 1) / 2);
                
                if (dx < innerBoundary && dz < innerBoundary) {
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

                if (lod > 0) {
                     if (std::abs(offset.first) < minR && std::abs(offset.second) < minR) continue;
                }

                int x = px + offset.first;
                int z = pz + offset.second;
                
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

    void Draw(Shader& shader, const glm::mat4& viewProj, const glm::mat4& cullViewMatrix,const glm::mat4& previousViewMatrix, const glm::mat4& proj, const int CUR_SCR_WIDTH, const int CUR_SCR_HEIGHT, Shader* depthDebugShader, bool depthDebug, bool frustumLock) {
        if(m_shutdown) return;


        // ************************** FRUSTUM AND OCCLUSION CULL (CALLS CULL_COMPUTE) *********************** //
        {
            Engine::Profiler::Get().BeginGPU("GPU: Buffer and Cull Compute"); 
            m_culler->Cull(cullViewMatrix, previousViewMatrix, proj, g_fbo.hiZTex);
            Engine::Profiler::Get().EndGPU();
        }
        // ************************** FRUSTUM AND OCCLUSION CULL (CALLS CULL_COMPUTE) *********************** //





        {   // ************************** DRAW CALL (ACTUAL CALL INSIDE GPU_CULLER) *********************** //
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
            // ************************** DRAW CALL (ACTUAL CALL INSIDE GPU_CULLER) *********************** //




            


            // ************************** Hiearchical Z Buffer (OCCLUSION CULL DEPTH BUFFER AND MIPMAPS, CALLS HI_Z_DOWN.glsl) *********************** //
             Engine::Profiler::Get().BeginGPU("GPU: Occlusion Cull COMPUTE"); 

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glCopyImageSubData(g_fbo.depthTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                            g_fbo.hiZTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                            CUR_SCR_WIDTH, CUR_SCR_HEIGHT, 1);
            
            // Barrier: Ensure copy finishes before Compute Shader reads it
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            // Step B: Downsample the rest of the pyramid
            GetCuller()->GenerateHiZ(g_fbo.hiZTex, CUR_SCR_WIDTH, CUR_SCR_HEIGHT);
                            


            if (!depthDebug)
            {
                // Draw normal rendered view to screen quad
                glBlitNamedFramebuffer(g_fbo.fbo, 0, 
                    0, 0, CUR_SCR_WIDTH, CUR_SCR_HEIGHT, 
                    0, 0, CUR_SCR_WIDTH, CUR_SCR_HEIGHT, 
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
                
            } else {
                // render depth view instead 
                RenderHiZDebug(depthDebugShader, g_fbo.hiZTex, 0, CUR_SCR_WIDTH, CUR_SCR_HEIGHT);
            }

            Engine::Profiler::Get().EndGPU();

            // ************************** Hiearchical Z Buffer (OCCLUSION CULL DEPTH BUFFER AND MIPMAPS, CALLS HI_Z_DOWN.glsl) *********************** //


        }
    }

    GpuCuller* GetCuller() { return m_culler.get(); }

    void RenderHiZDebug(Shader* debugShader, GLuint hizTexture, int mipLevel, int screenW, int screenH) {
        glDisable(GL_DEPTH_TEST); 
        glDisable(GL_CULL_FACE);
        
        debugShader->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hizTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0); 
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 10); 

        debugShader->setInt("u_DepthTexture", 0);
        debugShader->setInt("u_MipLevel", mipLevel);
        debugShader->setVec2("u_ScreenSize", glm::vec2(screenW, screenH));

        glBindVertexArray(m_dummyVAO); 
        glDrawArrays(GL_TRIANGLES, 0, 3);
        
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    void Reload(WorldConfig newConfig) {
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
        m_lastLODCalculationPos = glm::vec3(-99999.0f);
        m_pendingLODResult = nullptr;
    }

private:
    void Task_Generate(ChunkNode* node) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] Task: Generate");
        
        
        float minY, maxY;
        FillChunk(node, minY, maxY);
        
        // Update AABB with tight bounds
        // X and Z remain full size, but Y is clamped to geometry
        node->minAABB.y = minY;
        node->maxAABB.y = maxY;
        
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_shutdown) return;
        m_generatedQueue.push(node);
    }

    void Task_Mesh(ChunkNode* node) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] Task: Mesh"); 

        LinearAllocator<PackedVertex> threadAllocator(1000000); 
        MeshChunk(*node->chunk, threadAllocator, false);
        
        node->cachedMesh.assign(threadAllocator.Data(), threadAllocator.Data() + threadAllocator.Count());
        
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_shutdown) return;
        m_meshedQueue.push(node);
    }


    // --------------------------------------------------------------------------------------------
    // FILL CHUNK 
    // 1. Analyzes terrain height.
    // 2. Decides if memory is needed (Optimization A).
    // 3. Allocates ONLY if necessary.
    // --------------------------------------------------------------------------------------------

    void FillChunk(ChunkNode* node, float& outMinY, float& outMaxY) {
        int cx = node->cx;
        int cy = node->cy;
        int cz = node->cz;
        int scale = node->scale;

        // position for voxels 
        int worldX = cx * CHUNK_SIZE * scale;
        int worldY = cy * CHUNK_SIZE * scale;
        int worldZ = cz * CHUNK_SIZE * scale;

        
        int chunkBottomY = worldY;
        int chunkTopY = worldY + (CHUNK_SIZE * scale);

        // Initialize bounds inverted (or clamped)
        // If no blocks are placed, these will be set to bottom Y
        float actualMinY = (float)chunkTopY;
        float actualMaxY = (float)chunkBottomY;
        bool hasBlocks = false;

        int heights[CHUNK_SIZE_PADDED][CHUNK_SIZE_PADDED];
        int minHeight = 99999;
        int maxHeight = -99999;

        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                float wx = (float)(worldX + (x - 1) * scale);
                float wz = (float)(worldZ + (z - 1) * scale);
                
                int h = m_generator->GetHeight(wx, wz);
                if (scale > 1) { h = (h / scale) * scale; } 
                
                heights[x][z] = h;
                if (h < minHeight) minHeight = h;
                if (h > maxHeight) maxHeight = h;
            }
        }

        // case A: chunk is completely above terrain and empty
        if (maxHeight < chunkBottomY) { 
            node->isUniform = true;
            node->uniformID = 0; // mark as air block
            node->chunk = nullptr; // do not allocate voxels its not neccessary
            outMinY = (float)chunkBottomY; 
            outMaxY = (float)chunkBottomY; // Empty
            return; 
        } 
        // case B: completely below ground and full
        if (minHeight > chunkTopY && !m_config.enableCaves) { 
            node->isUniform = true;
            node->uniformID = 0; 
            node->chunk = nullptr; // do not allocate voxels its not neccessary
            outMinY = (float)chunkBottomY;
            outMaxY = (float)chunkTopY; // Full
            return; 
        } 

        // case C: chunk has voxels
        node->isUniform = false;
        node->chunk = m_voxelPool.Acquire(); // allocate memory from pool

        // we need to check if the voxel pool is nearing the allocated total limit
        if (!node->chunk) {
            // pools CLOSED
            std::cerr << "[World] CRITICAL: Voxel Pool Exhausted. Allocate More m_voxelPool Memory" << std::endl;
            node->isUniform = true;
            node->uniformID = 0;
            return;
        }

        std::memset(node->chunk->voxels, 0, sizeof(node->chunk->voxels));

        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                int height = heights[x][z]; 
                
                // Optimization: Only loop Y where the surface intersects the chunk
                int localMaxY = (height - chunkBottomY) / scale;
                localMaxY = std::min(localMaxY + 2, CHUNK_SIZE_PADDED - 1);
                
                // If checking for caves, we might need to scan the whole height
                int startY = 0;
                if (!m_config.enableCaves) {
                    if (localMaxY < 0) continue; 
                } else {
                    localMaxY = CHUNK_SIZE_PADDED - 1; // Check full chunk for caves
                }

                for (int y = startY; y <= localMaxY; y++) {
                    int wy = worldY + (y - 1) * scale; 
                    float wx = (float)(worldX + (x - 1) * scale);
                    float wz = (float)(worldZ + (z - 1) * scale);

                    // Use 'node->chunk->Set' since we are now using a pointer
                    uint8_t blockID = m_generator->GetBlock(wx, (float)wy, wz, height, scale);
                    
                    if (blockID != 0) {
                        node->chunk->Set(x, y, z, blockID);
                        
                        hasBlocks = true;
                        float blockBase = (float)wy;
                        float blockTop = blockBase + scale;
                        if (blockBase < actualMinY) actualMinY = blockBase;
                        if (blockTop > actualMaxY) actualMaxY = blockTop;
                    }
                }
            }
        }

        if (hasBlocks) {
            outMinY = actualMinY;
            outMaxY = actualMaxY;
        } else {
            // We allocated memory, but turns out it was empty (e.g. caves cleared everything)
            // Optimization: Give the memory back immediately
            m_voxelPool.Release(node->chunk);
            node->chunk = nullptr;
            node->isUniform = true;
            node->uniformID = 0;
            
            outMinY = (float)chunkBottomY;
            outMaxY = (float)chunkBottomY;
        }
    }
};