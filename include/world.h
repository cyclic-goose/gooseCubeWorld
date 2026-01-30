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
#include "terrain_system.h"
#include "engine_config.h"


// ================================================================================================
//                                       CHUNK STRUCTURES
// ================================================================================================

enum class ChunkState { MISSING, GENERATING, GENERATED, MESHING, MESHED, ACTIVE };

// the chunk node acts as the meta data, the what, where, when, how to the chunks actual voxel data
struct ChunkNode {
    Chunk *chunk = nullptr; 
    glm::vec3 position;
    int cx, cy, cz; 
    int lod;      
    int scale;    
    
    std::vector<PackedVertex> cachedMeshOpaque; 
    std::vector<PackedVertex> cachedMeshTrans;
    std::atomic<ChunkState> state{ChunkState::MISSING};
    
    long long gpuOffsetOpaque = -1; 
    long long gpuOffsetTrans = -1;

    size_t vertexCountOpaque = 0;
    size_t vertexCountTrans = 0;
    int64_t id; 

    glm::vec3 minAABB;
    glm::vec3 maxAABB;

    bool isUniform = false; 
    uint8_t uniformID = 0;  

    void Reset(int x, int y, int z, int lodLevel) {
        chunk = nullptr;

        lod = lodLevel;
        scale = 1 << lod; 

        cx = x; cy = y; cz = z;
        
        float size = (float)(CHUNK_SIZE * scale);
        position = glm::vec3(x * size, y * size, z * size);
        
        minAABB = position;
        maxAABB = position + glm::vec3(size);
        
        state = ChunkState::MISSING;
        cachedMeshOpaque.clear();
        cachedMeshTrans.clear();
        gpuOffsetOpaque = -1;
        gpuOffsetTrans = -1;
        vertexCountOpaque = 0;
        vertexCountTrans = 0;
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
    std::unique_ptr<EngineConfig> m_config; 
    std::unique_ptr<ITerrainGenerator> m_generator;
    
    std::unordered_map<int64_t, ChunkNode*> m_chunks;
    std::shared_mutex m_chunksMutex; 
    ObjectPool<ChunkNode> m_chunkPool;
    ObjectPool<Chunk> m_voxelPool;

    std::queue<ChunkNode*> m_generatedQueue; 
    std::queue<ChunkNode*> m_meshedQueue;    
    
    std::mutex m_queueMutex;
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
    std::atomic<int> m_activeWorkCount{0}; 

    friend class ImGuiManager;

public:
    World(EngineConfig config, std::unique_ptr<ITerrainGenerator> generator) : m_generator(std::move(generator)) {
        m_config = std::make_unique<EngineConfig>(config);

        size_t steadyStateNodes = 0;
        for(int i=0; i< m_config->settings.lodCount; i++) {
            int r = m_config->settings.lodRadius[i];
            steadyStateNodes += (size_t)(r * 2 + 1) * (r * 2 + 1) * m_config->settings.worldHeightChunks;
        }
        size_t nodeCapacity = steadyStateNodes + (steadyStateNodes / 5); 
        std::cout << "NODE CAPACITY " << nodeCapacity << std::endl;

        m_chunkPool.Init(m_config->NODE_POOL_GROWTH_STRIDE, steadyStateNodes, nodeCapacity); 
        m_voxelPool.Init(m_config->VOXEL_POOL_GROWTH_STRIDE, m_config->VOXEL_POOL_INITIAL_SIZE, m_config->MAX_TRANSIENT_VOXEL_MESHES); 

        m_gpuMemory = std::make_unique<GpuMemoryManager>(static_cast<size_t>(m_config->VRAM_HEAP_ALLOCATION_MB) * 1024 * 1024);
        m_culler = std::make_unique<GpuCuller>(nodeCapacity);
        
        glCreateVertexArrays(1, &m_dummyVAO);
    }

    ~World() { Dispose(); }
    
    void Dispose() {
        m_shutdown = true;
        while(m_activeWorkCount > 0) { std::this_thread::yield(); }
        if (m_dummyVAO) { glDeleteVertexArrays(1, &m_dummyVAO); m_dummyVAO = 0; }
        m_culler.reset();
    }

    bool IsBusy() {
        if (m_activeWorkCount > 0) return true;
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (!m_generatedQueue.empty()) return true;
        if (!m_meshedQueue.empty()) return true;
        return false;
    }

    void SwitchGenerator(std::unique_ptr<ITerrainGenerator> newGen, GLuint newTextureArrayID) {
        std::cout << "[World] Stopping tasks for generator switch..." << std::endl;
        bool wasFrozen = m_freezeLODs;
        m_freezeLODs = true;

        int waitCycles = 0;
        while (m_activeWorkCount > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            waitCycles++;
            if (waitCycles % 100 == 0) std::cout << "[World] Waiting for " << m_activeWorkCount << " threads..." << std::endl;
        }

        m_generator = std::move(newGen);
        m_generator->Init();
        
        if (m_textureArrayID != 0 && m_textureArrayID != newTextureArrayID) {
            glDeleteTextures(1, &m_textureArrayID);
        }
        m_textureArrayID = newTextureArrayID;
        
        Reload(*m_config);
        m_freezeLODs = wasFrozen;
    }

    ITerrainGenerator* GetGenerator() { return m_generator.get(); }
    void SetTextureArray(GLuint textureID) { m_textureArrayID = textureID; }
    void setCubeDebugMode(int mode) { m_config->settings.cubeDebugMode = mode; }
    void setOcclusionCulling (bool mode){ m_config->settings.occlusionCulling = mode; }
    bool getOcclusionCulling () { return m_config->settings.occlusionCulling; }
    void SetLODFreeze(bool freeze) { m_freezeLODs = freeze; }
    bool GetLODFreeze() const { return m_freezeLODs; }
    const EngineConfig& GetConfig() const { return *m_config; }

    void Update(glm::vec3 cameraPos) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("World::Update Total");
        
        if (m_gpuMemory->GetFragmentationRatio() > 0.6f) { 
             Reload(*m_config);
             return;
        }

        ProcessQueues(); 

        if (m_freezeLODs) return; 
        UpdateLODs_Async(cameraPos);
        UpdateProfilerPressure();
        m_frameCounter++;
    }

    void ProcessQueues() {
        if(m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("World::ProcessQueues");
        
        std::vector<ChunkNode*> nodesToMesh;
        std::vector<ChunkNode*> nodesToUpload;
        
        { 
            std::lock_guard<std::mutex> lock(m_queueMutex);
            int limitGen = m_config->NODE_GENERATION_LIMIT; 
            while (!m_generatedQueue.empty() && limitGen > 0) {
                nodesToMesh.push_back(m_generatedQueue.front());
                m_generatedQueue.pop();
                limitGen--;
            }
            int limitUpload = m_config->NODE_UPLOAD_LIMIT; 
            while (!m_meshedQueue.empty() && limitUpload > 0) {
                nodesToUpload.push_back(m_meshedQueue.front());
                m_meshedQueue.pop();
                limitUpload--;
            }
        }

        for (ChunkNode* node : nodesToMesh) {
            if(m_shutdown) return; 
            if (node->state == ChunkState::GENERATING) {
                if (node->isUniform) {
                    node->state = ChunkState::ACTIVE;
                } else {
                    node->state = ChunkState::MESHING;
                    m_activeWorkCount++;
                    m_pool.enqueue([this, node]() { 
                        this->Task_Mesh(node); 
                        m_activeWorkCount--; 
                    });
                }
            }
        }

        for (ChunkNode* node : nodesToUpload) {
            if(m_shutdown) return; 
            if (node->state == ChunkState::MESHING) {
                
                if (!node->cachedMeshOpaque.empty()) {
                    size_t bytes = node->cachedMeshOpaque.size() * sizeof(PackedVertex);
                    long long offset = m_gpuMemory->Allocate(bytes, sizeof(PackedVertex));
                    if (offset != -1) {
                        m_gpuMemory->Upload(offset, node->cachedMeshOpaque.data(), bytes);
                        node->gpuOffsetOpaque = offset;
                        node->vertexCountOpaque = node->cachedMeshOpaque.size();
                    }
                }

                if (!node->cachedMeshTrans.empty()) {
                    size_t bytes = node->cachedMeshTrans.size() * sizeof(PackedVertex);
                    long long offset = m_gpuMemory->Allocate(bytes, sizeof(PackedVertex));
                    if (offset != -1) {
                        m_gpuMemory->Upload(offset, node->cachedMeshTrans.data(), bytes);
                        node->gpuOffsetTrans = offset;
                        node->vertexCountTrans = node->cachedMeshTrans.size();
                    }
                }

                size_t opaqueIdx = (node->gpuOffsetOpaque != -1) ? (size_t)(node->gpuOffsetOpaque / sizeof(PackedVertex)) : 0;
                size_t transIdx = (node->gpuOffsetTrans != -1) ? (size_t)(node->gpuOffsetTrans / sizeof(PackedVertex)) : 0;

                // IMPORTANT: Use node->position or minAABB? 
                // Since minAABB is no longer modified to be "tight", it equals node->position.
                // This ensures the shader translates vertices from the chunk grid origin.
                m_culler->AddOrUpdateChunk(node->id, node->minAABB, node->maxAABB, (float)node->scale, opaqueIdx, node->vertexCountOpaque, transIdx, node->vertexCountTrans);

                node->cachedMeshOpaque.clear(); 
                node->cachedMeshOpaque.shrink_to_fit();
                node->cachedMeshTrans.clear(); 
                node->cachedMeshTrans.shrink_to_fit();

                if (node->chunk) {
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

        // UNLOAD LOGIC
        for (const auto& pair : m_chunks) {
            ChunkNode* node = pair.second;
            int lod = node->lod;
            int scale = 1 << lod;
            
            int camX = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
            int camZ = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));
            
            int dx = abs(node->cx - camX);
            int dz = abs(node->cz - camZ);
            
            bool shouldUnload = false;

            if (dx > m_config->settings.lodRadius[lod] || dz > m_config->settings.lodRadius[lod]) {
                 if (IsParentReady(node->cx, node->cy, node->cz, lod)) {
                     shouldUnload = true;
                 }
                 else if (lod < m_config->settings.lodCount - 1) {
                     int pLod = lod + 1;
                     int pRadius = m_config->settings.lodRadius[pLod];
                     int pScale = 1 << pLod;
                     
                     int pCamX = (int)floor(cameraPos.x / (CHUNK_SIZE * pScale));
                     int pCamZ = (int)floor(cameraPos.z / (CHUNK_SIZE * pScale));
                     int px = node->cx >> 1;
                     int pz = node->cz >> 1;
                     
                     if (abs(px - pCamX) > pRadius || abs(pz - pCamZ) > pRadius) {
                         shouldUnload = true;
                     }
                 }
            }
            else if (lod > 0) {
                int prevRadius = m_config->settings.lodRadius[lod - 1];
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

        // LOAD LOGIC
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

        for(int lod = 0; lod < m_config->settings.lodCount; lod++) {
            int scale = 1 << lod;
            int px = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
            int pz = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));
            
            int r = m_config->settings.lodRadius[lod];
            int rSq = r * r; 
            
            int minR = 0;
            if (lod > 0) {
                int prevR = m_config->settings.lodRadius[lod - 1];
                minR = ((prevR + 1) / 2); 
            }

            for (const auto& offset : spiralOffsets) {
                int distSq = offset.first*offset.first + offset.second*offset.second;
                if (distSq > (rSq * 2 + 100)) break; 
                
                if (std::abs(offset.first) > r || std::abs(offset.second) > r) continue;
                if (lod > 0 && std::abs(offset.first) < minR && std::abs(offset.second) < minR) continue;

                int x = px + offset.first;
                int z = pz + offset.second;
                
                int minH, maxH;
                // NOTE: For 3D gen, this returns approximate bounds of "interesting" terrain
                m_generator->GetHeightBounds(x, z, scale, minH, maxH);
                
                int chunkYStart = std::max(0, (minH / (CHUNK_SIZE * scale)) - 1); 
                int chunkYEnd = std::min(m_config->settings.worldHeightChunks - 1, (maxH / (CHUNK_SIZE * scale)) + 1);

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
        auto ProcessUnloads = [this]() {
            std::lock_guard<std::mutex> lock(m_lodResultMutex);
            if (m_pendingLODResult && !m_pendingLODResult->chunksToUnload.empty()) {
                 std::unique_lock<std::shared_mutex> writeLock(m_chunksMutex);
                 for (int64_t key : m_pendingLODResult->chunksToUnload) {
                    auto it = m_chunks.find(key);
                    if (it != m_chunks.end()) {
                        ChunkNode* node = it->second;
                        m_culler->RemoveChunk(node->id);
                        if (node->gpuOffsetOpaque != -1) {
                            m_gpuMemory->Free(node->gpuOffsetOpaque, node->vertexCountOpaque * sizeof(PackedVertex));
                            node->gpuOffsetOpaque = -1;
                        }
                        if (node->gpuOffsetTrans != -1) {
                            m_gpuMemory->Free(node->gpuOffsetTrans, node->vertexCountTrans * sizeof(PackedVertex));
                            node->gpuOffsetTrans = -1;
                        }
                        m_chunkPool.Release(node);
                        m_chunks.erase(it);
                    }
                }
                m_pendingLODResult->chunksToUnload.clear();
            }
        };

        if (!m_isLODWorkerRunning) {
             float distSq = glm::dot(cameraPos - m_lastLODCalculationPos, cameraPos - m_lastLODCalculationPos);
             if (distSq > 64.0f) { 
                 if (distSq > 10000.0f) { 
                     ProcessUnloads(); 
                     std::lock_guard<std::mutex> lock(m_lodResultMutex);
                     m_pendingLODResult = nullptr; 
                 }
                 m_lastLODCalculationPos = cameraPos;
                 m_isLODWorkerRunning = true;
                 m_activeWorkCount++;
                 m_pool.enqueue([this, cameraPos](){ 
                     this->Task_CalculateLODs(cameraPos); 
                     m_activeWorkCount--; 
                 });
             }
        }

        {
            Engine::Profiler::ScopedTimer timer("World::ApplyLODs");
            ProcessUnloads();

            std::lock_guard<std::mutex> lock(m_lodResultMutex);
            if (m_pendingLODResult) {
                std::unique_lock<std::shared_mutex> writeLock(m_chunksMutex);
                int queued = 0;
                int MAX_PER_FRAME = 500; 
                
                size_t& idx = m_pendingLODResult->loadIndex;
                const auto& loadList = m_pendingLODResult->chunksToLoad;

                size_t limit = (size_t)m_config->MAX_TRANSIENT_VOXEL_MESHES;
                if (limit > 100) limit -= 100;

                while (idx < loadList.size() && queued < MAX_PER_FRAME) {
                    {
                        std::lock_guard<std::mutex> qLock(m_queueMutex);
                        size_t totalInFlight = m_generatedQueue.size() + m_meshedQueue.size() + m_activeWorkCount;
                        if (totalInFlight >= limit) break; 
                    }

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
                            m_activeWorkCount++; 
                            m_pool.enqueue([this, newNode]() { 
                                this->Task_Generate(newNode); 
                                m_activeWorkCount--; 
                            });
                            queued++;
                        }
                    }
                }
                
                if (idx >= loadList.size()) {
                    m_pendingLODResult = nullptr;
                }
            }
        }
    }

    void Draw(Shader& shader, const glm::mat4& viewProj, const glm::mat4& previousViewProjMatrix, const glm::mat4& proj, const int CUR_SCR_WIDTH, const int CUR_SCR_HEIGHT, Shader* depthDebugShader, bool depthDebug, bool frustumLock) {
        if(m_shutdown) return;
        {
            Engine::Profiler::Get().BeginGPU("GPU: Buffer and Cull Compute"); 
            m_culler->Cull(viewProj, previousViewProjMatrix, proj, g_fbo.hiZTex);
            Engine::Profiler::Get().EndGPU();
        }
        {   
            Engine::Profiler::Get().BeginGPU("GPU: MDI DRAW"); 

            shader.use();
            glUniformMatrix4fv(glGetUniformLocation(shader.ID, "u_ViewProjection"), 1, GL_FALSE, glm::value_ptr(viewProj));
            glUniform1i(glGetUniformLocation(shader.ID, "u_DebugMode"), m_config->settings.cubeDebugMode);
            
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_gpuMemory->GetID());
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_culler->GetVisibleChunkBuffer());

            if (m_textureArrayID != 0) {
                 glActiveTexture(GL_TEXTURE0);
                 glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureArrayID);
                 shader.setInt("u_Textures", 0);
            }

            glBindVertexArray(m_dummyVAO); 

            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE); 

            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_culler->GetIndirectOpaque());
            glBindBuffer(GL_PARAMETER_BUFFER, m_culler->GetAtomicCounter());
            glMultiDrawArraysIndirectCount(GL_TRIANGLES, 0, 0, (GLsizei)m_culler->GetMaxChunks(), 0);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE); 
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_culler->GetIndirectTrans());
            glMultiDrawArraysIndirectCount(GL_TRIANGLES, 0, 0, (GLsizei)m_culler->GetMaxChunks(), 0);

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glDisable(GL_POLYGON_OFFSET_FILL);

            Engine::Profiler::Get().EndGPU();
            
             Engine::Profiler::Get().BeginGPU("GPU: Occlusion Cull COMPUTE"); 

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glCopyImageSubData(g_fbo.depthTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                            g_fbo.hiZTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                            CUR_SCR_WIDTH, CUR_SCR_HEIGHT, 1);
            
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            GetCuller()->GenerateHiZ(g_fbo.hiZTex, CUR_SCR_WIDTH, CUR_SCR_HEIGHT);
                            
            if (!depthDebug) {
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

    void Reload(EngineConfig newConfig) {
        m_config = std::make_unique<EngineConfig>(newConfig);
        m_generator->Init();
        {
            std::unique_lock<std::shared_mutex> lock(m_chunksMutex);
            for (auto& pair : m_chunks) {
                ChunkNode* node = pair.second;
                m_culler->RemoveChunk(node->id); 
                if (node->gpuOffsetOpaque != -1) {
                    m_gpuMemory->Free(node->gpuOffsetOpaque, node->vertexCountOpaque * sizeof(PackedVertex));
                    node->gpuOffsetOpaque = -1;
                }
                if (node->gpuOffsetTrans != -1) {
                    m_gpuMemory->Free(node->gpuOffsetTrans, node->vertexCountTrans * sizeof(PackedVertex));
                    node->gpuOffsetTrans = -1;
                }
                m_chunkPool.Release(node);
            }
            m_chunks.clear();
        }
        m_lastLODCalculationPos = glm::vec3(-99999.0f);
        m_pendingLODResult = nullptr;
    }

private:
    // ============================================================================================
    // UPDATED FILL CHUNK - NOW SUPPORTS 3D VOLUMETRIC GENERATION
    // ============================================================================================
    void Task_Generate(ChunkNode* node) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] Task: Generate");

        float outMinY, outMaxY;
        FillChunk(node, outMinY, outMaxY);
        
        // FIX: DO NOT update minAABB.y with the tight fit (outMinY).
        // The shader uses minAABB as the CHUNK ORIGIN to translate vertices.
        // If we set minAABB.y to outMinY (which might be > 0 if the bottom of chunk is air),
        // the shader will translate vertices upwards, doubling the offset.
        // node->minAABB.y = outMinY; <--- REMOVED
        
        // Updating maxAABB is safe (usually used for culling only), but to be 100% safe against
        // center-point calculation issues in culler, we leave it standard too.
        // node->maxAABB.y = outMaxY; 
        
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_shutdown) return;
        m_generatedQueue.push(node);
    }

    void FillChunk(ChunkNode* node, float& outMinY, float& outMaxY) {
        // Engine::Profiler::ScopedTimer timer("[ASYNC] Task: Fill Chunk");
        int cx = node->cx;
        int cy = node->cy;
        int cz = node->cz;
        int scale = node->scale;

        int worldY = cy * CHUNK_SIZE * scale;
        int chunkBottomY = worldY;
        int chunkTopY = worldY + (CHUNK_SIZE * scale);

        // 1. Broad Phase Check (Unchanged)
        int minGenH, maxGenH;
        m_generator->GetHeightBounds(cx, cz, scale, minGenH, maxGenH);

        if (chunkBottomY > maxGenH) {
            node->isUniform = true;
            node->uniformID = 0; // Air
            node->chunk = nullptr; 
            outMinY = (float)chunkBottomY;
            outMaxY = (float)chunkBottomY;
            return;
        }
        if (chunkTopY < minGenH) {
                node->isUniform = true;
                node->uniformID = 3; // Solid Stone
                node->chunk = nullptr;
                outMinY = (float)chunkBottomY;
                outMaxY = (float)chunkTopY;
                return;
        }

        // 2. Allocation
        node->isUniform = false;
        node->chunk = m_voxelPool.Acquire(); 

        if (!node->chunk) {
            // Handle error
            node->isUniform = true;
            node->uniformID = 0;
            return;
        }

        // 3. Batched Generation
        // We delegate the heavy lifting to the generator.
        // It handles the loop internally with raw pointers and SIMD.
        // This removes the virtual call overhead per-block.
        m_generator->GenerateChunk(node->chunk, cx, cy, cz, scale);

        // 4. Update AABB (Optional but recommended to keep logic separate)
        // If you need exact AABB, you can do a quick scan here, or have GenerateChunk return it.
        // For now, setting it to full chunk bounds is safer for culling than an empty AABB
        // if the chunk actually has blocks.
        outMinY = (float)chunkBottomY;
        outMaxY = (float)chunkTopY;
    }



    void Task_Mesh(ChunkNode* node) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("[ASYNC] Task: Mesh"); 
        
        LinearAllocator<PackedVertex> opaqueAllocator(100000); 
        LinearAllocator<PackedVertex> transAllocator(50000); 

        MeshChunk(*node->chunk, opaqueAllocator, transAllocator, false);
        
        node->cachedMeshOpaque.assign(opaqueAllocator.Data(), opaqueAllocator.Data() + opaqueAllocator.Count());
        node->cachedMeshTrans.assign(transAllocator.Data(), transAllocator.Data() + transAllocator.Count());
        
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_shutdown) return;
        m_meshedQueue.push(node);
    }

    bool AreChildrenReady(int cx, int cy, int cz, int lod) {
        if (lod == 0) return true; 
        
        int childLod = lod - 1;
        int scale = 1 << childLod; 
        int startX = cx * 2; int startY = cy * 2; int startZ = cz * 2;

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
        if (lod >= m_config->settings.lodCount - 1) return true; 
        
        int parentLod = lod + 1;
        int px = cx >> 1; int py = cy >> 1; int pz = cz >> 1;

        int64_t key = ChunkKey(px, py, pz, parentLod);
        auto it = m_chunks.find(key);
        
        if (it == m_chunks.end() || it->second->state.load() != ChunkState::ACTIVE) {
            return false;
        }
        return true;
    }

    void UpdateProfilerPressure() {
        if (!Engine::Profiler::Get().m_Enabled) return;
        size_t pendingGen = 0;
        {
            std::unique_lock<std::mutex> lock(m_lodResultMutex, std::try_to_lock);
            if (lock.owns_lock() && m_pendingLODResult) {
                pendingGen = m_pendingLODResult->chunksToLoad.size() - m_pendingLODResult->loadIndex;
            }
        }
        size_t waitingMesh = m_generatedQueue.size();
        size_t waitingUpload = m_meshedQueue.size();
        size_t activeThreads = m_activeWorkCount.load();
        size_t totalActive = m_chunks.size(); 

        Engine::Profiler::Get().SetPipelineStats(
            pendingGen, waitingMesh, waitingUpload, activeThreads, totalActive,
            (size_t)m_config->MAX_TRANSIENT_VOXEL_MESHES,
            m_voxelPool.GetAllocatedMB(), m_voxelPool.GetUsedMB(),
            m_chunkPool.GetAllocatedMB(), m_chunkPool.GetUsedMB()
        );
    }
};