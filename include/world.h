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
#include "gpu_culler.h" // [NEW] Integrated GPU Culler

// --- CONFIG ---
struct WorldConfig {
    int seed = 1337;
    int worldHeightChunks = 32;
    int lodCount = 5; 
    int lodRadius[8] = { 10, 16, 24, 32, 48, 0, 0, 0 }; 
    float scale = 0.08f;          
    float hillAmplitude = 100.0f;  
    float hillFrequency = 4.0f;   
    float mountainAmplitude = 500.0f; 
    float mountainFrequency = 0.26f; 
    int seaLevel = 90;            
    bool enableCaves = false;     
    float caveThreshold = 0.5f;   
};

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
    int64_t id; // Added ID for easier tracking

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

class TerrainGenerator {
private:
    FastNoise::SmartNode<> m_baseNoise;
    FastNoise::SmartNode<> m_mountainNoise;
    FastNoise::SmartNode<> m_caveNoise;
    WorldConfig m_config;

public:
    TerrainGenerator(WorldConfig config) : m_config(config) { Init(); }

    void Init() {
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

    int GetHeight(float x, float z) const {
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

    void GetHeightBounds(int cx, int cz, int scale, int& minH, int& maxH) {
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

    bool IsCave(float x, float y, float z) const {
        if (!m_config.enableCaves) return false;
        float val = m_caveNoise->GenSingle3D(x * 0.02f, y * 0.04f, z * 0.02f, m_config.seed);
        return val > m_config.caveThreshold;
    }
};

inline int64_t ChunkKey(int x, int y, int z, int lod) {
    uint64_t ulod = (uint64_t)(lod & 0x7) << 61; 
    uint64_t ux = (uint64_t)(uint32_t)x & 0xFFFFF; 
    uint64_t uz = (uint64_t)(uint32_t)z & 0xFFFFF; 
    uint64_t uy = (uint64_t)(uint32_t)y & 0x1FFFFF;   
    return ulod | (ux << 41) | (uz << 21) | uy;
}

class World {
private:
    WorldConfig m_config;
    TerrainGenerator m_generator;
    
    // Protected by m_chunksMutex
    std::unordered_map<int64_t, ChunkNode*> m_chunks;
    std::shared_mutex m_chunksMutex; // R/W Lock for the map

    ObjectPool<ChunkNode> m_chunkPool;

    std::mutex m_queueMutex;
    std::queue<ChunkNode*> m_generatedQueue; 
    std::queue<ChunkNode*> m_meshedQueue;    
    
    ThreadPool m_pool; 

    // Used for tracking movement per LOD
    int lastPx[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    int lastPz[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    bool m_lodComplete[8] = {false};

    int m_frameCounter = 0; 
    std::atomic<bool> m_shutdown{false};

    std::unique_ptr<GpuMemoryManager> m_gpuMemory;

    // --- GPU DRIVEN CULLING (REPLACED OLD SYSTEM) ---
    std::unique_ptr<GpuCuller> m_culler;
    GLuint m_dummyVAO = 0; 

    struct ChunkRequest { int x, y, z; int lod; int distSq; };

    friend class ImGuiManager;

    bool IsFullyCovered(int cx, int cz, int currentLod) {
        if (currentLod == 0) return false;
        int prevLod = currentLod - 1;
        int scaleRatio = 2; 
        int minX = cx * scaleRatio;
        int minZ = cz * scaleRatio;
        int maxX = minX + 1; 
        int maxZ = minZ + 1;

        for (int x = minX; x <= maxX; x++) {
            for (int z = minZ; z <= maxZ; z++) {
                int64_t key = ChunkKey(x, 0, z, prevLod);
                auto it = m_chunks.find(key);
                if (it == m_chunks.end()) return false; 
                if (it->second->state != ChunkState::ACTIVE) return false; 
            }
        }
        return true; 
    }

public:
    World(WorldConfig config) : m_config(config), m_generator(config) {
        size_t maxChunks = 0;
        for(int i=0; i<m_config.lodCount; i++) {
            int r = m_config.lodRadius[i];
            maxChunks += (r * 2 + 1) * (r * 2 + 1) * m_config.worldHeightChunks;
        }
        // Buffer extra capacity for transitions
        size_t capacity = maxChunks + 5000; 
        
        m_chunkPool.Init(capacity); 
        m_gpuMemory = std::make_unique<GpuMemoryManager>(1024 * 1024 * 1024); // 1GB Voxel Heap

        // Initialize the new GPU Culler
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
    
    const WorldConfig& GetConfig() const { return m_config; }

    void Update(glm::vec3 cameraPos) {
        //Engine::Profiler::ScopedTimer timer("World Update Total: ");
        if (m_shutdown) return;
        ProcessQueues(); 
        
        int lodToUpdate = m_frameCounter % m_config.lodCount;
        m_frameCounter++;
        UnloadChunks(cameraPos, lodToUpdate);
        QueueNewChunks(cameraPos, lodToUpdate);
    }

    void ProcessQueues() {
        if(m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("Chunk Queue Updating");
        std::vector<ChunkNode*> nodesToMesh;
        std::vector<ChunkNode*> nodesToUpload;
        
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            int limitGen = 64; 
            while (!m_generatedQueue.empty() && limitGen > 0) {
                nodesToMesh.push_back(m_generatedQueue.front());
                m_generatedQueue.pop();
                limitGen--;
            }
            int limitUpload = 64; 
            while (!m_meshedQueue.empty() && limitUpload > 0) {
                nodesToUpload.push_back(m_meshedQueue.front());
                m_meshedQueue.pop();
                limitUpload--;
            }
        }

        for (ChunkNode* node : nodesToMesh) {
            if(m_shutdown) return; 
            if (node->state == ChunkState::GENERATING) {
                if (node->chunk.isUniform && node->chunk.uniformID == 0) {
                    node->state = ChunkState::ACTIVE;
                } else {
                    node->state = ChunkState::MESHING;
                    m_pool.enqueue([this, node]() { this->Task_Mesh(node); });
                }
            }
        }

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
                        
                        // [NEW] REGISTER WITH GPU CULLER
                        // We do this ONCE when the chunk becomes ready, not every frame.
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

    void UnloadChunks(glm::vec3 cameraPos, int targetLod) {
        Engine::Profiler::ScopedTimer timer("Chunk Unloading");
        if(m_shutdown) return;
        std::vector<int64_t> toRemove;
        
        {
            std::shared_lock<std::shared_mutex> lock(m_chunksMutex);
            for (auto& pair : m_chunks) {
                ChunkNode* node = pair.second;
                if (node->lod != targetLod) continue;

                int scale = node->scale;
                int limit = m_config.lodRadius[targetLod] + 3; 
                int camX = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
                int camZ = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));

                if (abs(node->cx - camX) > limit || abs(node->cz - camZ) > limit) {
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
                        // [NEW] REMOVE FROM GPU CULLER
                        m_culler->RemoveChunk(node->id);

                        m_gpuMemory->Free(node->gpuOffset, node->vertexCount * sizeof(PackedVertex));
                        node->gpuOffset = -1;
                    }
                    m_chunkPool.Release(node);
                    m_chunks.erase(it);
                }
            }
        }
    }

    void Draw(Shader& shader, const glm::mat4& renderViewProj, const glm::mat4& cullViewProj) {
        if(m_shutdown) return;

        // [NEW] GPU DRIVEN PIPELINE

        // 1. Compute Pass
        Engine::Profiler::Get().BeginGPU("GPU: Culling Compute"); 
        
        // Pass the Voxel Heap SSBO ID just in case (though currently Culler uses its own internal buffers)
        // We pass the ViewProj to cull against.
        m_culler->Cull(cullViewProj, m_gpuMemory->GetID());
        
        Engine::Profiler::Get().EndGPU();                       

        // 2. Render Pass
        Engine::Profiler::Get().BeginGPU("GPU: Geometry Draw");

        shader.use();
        glUniformMatrix4fv(glGetUniformLocation(shader.ID, "u_ViewProjection"), 1, GL_FALSE, glm::value_ptr(renderViewProj));
        
        // Bind the Voxel Heap (Geometry) to Binding 0
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_gpuMemory->GetID());
        
        // Culler handles Indirect Buffer, Parameter Buffer, and Offset Buffer binding internally
        m_culler->DrawIndirect(m_dummyVAO);

        Engine::Profiler::Get().EndGPU();
    }

    void QueueNewChunks(glm::vec3 cameraPos, int targetLod) {
        // Logic remains identical to original file...
        // ... (Omitting full implementation for brevity as requested, logic is unchanged) ...
        // Note: When calling newNode->Reset, ensure we set the ID
        
        // *Included abbreviated version of the loop logic for context*
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("New Chunk Queuing:");
        
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

        // ... [Spiral Logic Identical to original] ...

        // When acquiring new node:
        // ChunkNode* newNode = m_chunkPool.Acquire();
        // newNode->Reset(...);
        // newNode->id = key; // ENSURE ID IS SET HERE
        
        // To save tokens, I am trusting you to keep the QueueNewChunks implementation 
        // essentially the same, just ensure newNode->id = key is set when creating a chunk.
        
        // RE-IMPLEMENTATION OF KEY QUEUE LOOP FOR SAFETY:
        
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

        std::shared_lock<std::shared_mutex> readLock(m_chunksMutex);

        for (const auto& offset : spiralOffsets) {
            int distSq = offset.first*offset.first + offset.second*offset.second;
            if (distSq > (rSq * 2 + 100)) break; 
            if (std::abs(offset.first) > r || std::abs(offset.second) > r) continue;

            int x = px + offset.first;
            int z = pz + offset.second;
            
            int minH, maxH;
            m_generator.GetHeightBounds(x, z, scale, minH, maxH);
            
            int chunkYStart = (minH / (CHUNK_SIZE * scale)) - 1; 
            int chunkYEnd = (maxH / (CHUNK_SIZE * scale)) + 1;
            chunkYStart = std::max(0, chunkYStart);
            chunkYEnd = std::min(m_config.worldHeightChunks - 1, chunkYEnd);

            for (int y = chunkYStart; y <= chunkYEnd; y++) {
                int64_t key = ChunkKey(x, y, z, lod);
                if (m_chunks.find(key) == m_chunks.end()) {
                    int dx = x - px; 
                    int dz = z - pz; 
                    int chunkWorldY = y * CHUNK_SIZE * scale;
                    int dy = (chunkWorldY - (int)cameraPos.y) / (CHUNK_SIZE * scale); 
                    int distMetric = dx*dx + dz*dz + (dy*dy); 
                    missing.push_back({x, y, z, lod, distMetric});
                }
            }
        }
        readLock.unlock();

        std::sort(missing.begin(), missing.end(), [](const ChunkRequest& a, const ChunkRequest& b){ return a.distSq < b.distSq; });

        int queued = 0;
        int MAX_TASKS = 128;
        if (!missing.empty()) {
            std::unique_lock<std::shared_mutex> writeLock(m_chunksMutex);
            for (const auto& req : missing) {
                if (queued >= MAX_TASKS) break;
                int64_t key = ChunkKey(req.x, req.y, req.z, req.lod);
                if (m_chunks.find(key) == m_chunks.end()) {
                    ChunkNode* newNode = m_chunkPool.Acquire();
                    if (newNode) {
                        newNode->Reset(req.x, req.y, req.z, req.lod);
                        newNode->id = key; // CRITICAL: Set ID for GpuCuller
                        m_chunks[key] = newNode;
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

    void Task_Generate(ChunkNode* node) {
        if (m_shutdown) return;
        FillChunk(node->chunk, node->cx, node->cy, node->cz, node->scale);
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_shutdown) return;
        m_generatedQueue.push(node);
    }

    void Task_Mesh(ChunkNode* node) {
        if (m_shutdown) return;
        Engine::Profiler::ScopedTimer timer("Mesh Generation"); 

        LinearAllocator<PackedVertex> threadAllocator(1000000); 
        MeshChunk(node->chunk, threadAllocator, false);
        node->cachedMesh.assign(threadAllocator.Data(), threadAllocator.Data() + threadAllocator.Count());
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_shutdown) return;
        m_meshedQueue.push(node);
    }

    void FillChunk(Chunk& chunk, int cx, int cy, int cz, int scale) {
        // [Logic Unchanged from uploaded file]
        chunk.worldX = cx * CHUNK_SIZE * scale;
        chunk.worldY = cy * CHUNK_SIZE * scale;
        chunk.worldZ = cz * CHUNK_SIZE * scale;
        int chunkBottomY = chunk.worldY;
        int chunkTopY = chunk.worldY + (CHUNK_SIZE * scale);
        bool isFarLOD = scale >= 4; 
        bool isMidLOD = scale >= 2;

        int heights[CHUNK_SIZE_PADDED][CHUNK_SIZE_PADDED];
        int minHeight = 99999;
        int maxHeight = -99999;

        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                float wx = (float)(chunk.worldX + (x - 1) * scale);
                float wz = (float)(chunk.worldZ + (z - 1) * scale);
                int h = m_generator.GetHeight(wx, wz);
                if (scale > 1) { h = (h / scale) * scale; }
                heights[x][z] = h;
                if (h < minHeight) minHeight = h;
                if (h > maxHeight) maxHeight = h;
            }
        }

        if (maxHeight < chunkBottomY) { chunk.FillUniform(0); return; } 
        if (minHeight > chunkTopY && !m_config.enableCaves) { chunk.FillUniform(1); return; } 

        std::memset(chunk.voxels, 0, sizeof(chunk.voxels));
        
        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                int height = heights[x][z]; 
                int localMaxY = (height - chunkBottomY) / scale;
                localMaxY = std::min(localMaxY + 2, CHUNK_SIZE_PADDED - 1);
                if (localMaxY < 0) continue; 

                for (int y = 0; y <= localMaxY; y++) {
                    int wy = chunk.worldY + (y - 1) * scale; 
                    uint8_t blockID = 1; 
                    if (isFarLOD) { blockID = 1; } else {
                        if (wy <= height) {
                            if (wy == 0) blockID = 3; 
                            else if (wy >= height - scale) { 
                                if (wy > 55) blockID = 4; else if (wy > 35) blockID = 1; else blockID = 2;
                            } else if (wy > height - (4 * scale)) {
                                if (wy > 55) blockID = 4; else if (wy > 35) blockID = 1; else blockID = 5;
                            }
                        }
                    }
                    if (wy <= height) {
                        if (!isMidLOD) {
                            if (m_generator.IsCave((float)(chunk.worldX + (x-1)*scale), (float)wy, (float)(chunk.worldZ + (z-1)*scale))) blockID = 0;
                        }
                        chunk.Set(x, y, z, blockID);
                    }
                }
            }
        }
    }

    void Reload(WorldConfig newConfig) {
        m_config = newConfig;
        m_generator = TerrainGenerator(m_config);
        
        {
            std::unique_lock<std::shared_mutex> lock(m_chunksMutex);
            for (auto& pair : m_chunks) {
                ChunkNode* node = pair.second;
                if (node->gpuOffset != -1) {
                    m_culler->RemoveChunk(node->id); // [NEW]
                    m_gpuMemory->Free(node->gpuOffset, node->vertexCount * sizeof(PackedVertex));
                    node->gpuOffset = -1;
                }
                m_chunkPool.Release(node);
            }
            m_chunks.clear();
        }

        for(int i=0; i<8; i++) { 
            lastPx[i] = -999; 
            lastPz[i] = -999; 
            m_lodComplete[i] = false; 
        }
    }
};