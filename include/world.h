#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <atomic>

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

struct DrawArraysIndirectCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t first;
    uint32_t baseInstance;
};

struct WorldConfig {
    int seed = 1337;
    int worldHeightChunks = 12;
    int lodCount = 5; 

    // LOD Radii: High detail close, low detail far.
    // Stacking strategy: We draw ALL of these. 
    // LOD 0 is drawn 12 chunks out.
    // LOD 1 is drawn 16 chunks out (underneath LOD 0).
    // ...
    int lodRadius[8] = { 10, 16, 24, 32, 48, 0, 0, 0 }; 
    
    float scale = 0.1f;          
    float hillAmplitude = 15.0f;  
    float hillFrequency = 1.0f;   
    float mountainAmplitude = 80.0f; 
    float mountainFrequency = 0.5f; 
    int seaLevel = 10;            
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

    glm::vec3 minAABB;
    glm::vec3 maxAABB;

    void Reset(int x, int y, int z, int lodLevel) {
        lod = lodLevel;
        scale = 1 << lod; 

        cx = x; cy = y; cz = z;
        
        float size = (float)(CHUNK_SIZE * scale);
        position = glm::vec3(x * size, y * size, z * size);
        
        // AABB covers the full cubic volume of the chunk slot
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

    // Scalar noise lookup (SIMD optimization would require batched generation)
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

struct Frustum {
    glm::vec4 planes[6];
    void Update(const glm::mat4& viewProj) {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 4; ++j) {
                planes[i * 2][j] = viewProj[j][3] + viewProj[j][i];
                planes[i * 2 + 1][j] = viewProj[j][3] - viewProj[j][i];
            }
        }
        for (int i = 0; i < 6; ++i) {
            float length = glm::length(glm::vec3(planes[i]));
            planes[i] /= length;
        }
    }
    bool IsBoxVisible(const glm::vec3& min, const glm::vec3& max) const {
        for (int i = 0; i < 6; i++) {
            if (glm::dot(planes[i], glm::vec4(min.x, min.y, min.z, 1.0f)) < 0.0f &&
                glm::dot(planes[i], glm::vec4(max.x, min.y, min.z, 1.0f)) < 0.0f &&
                glm::dot(planes[i], glm::vec4(min.x, max.y, min.z, 1.0f)) < 0.0f &&
                glm::dot(planes[i], glm::vec4(max.x, max.y, min.z, 1.0f)) < 0.0f &&
                glm::dot(planes[i], glm::vec4(min.x, min.y, max.z, 1.0f)) < 0.0f &&
                glm::dot(planes[i], glm::vec4(max.x, min.y, max.z, 1.0f)) < 0.0f &&
                glm::dot(planes[i], glm::vec4(min.x, max.y, max.z, 1.0f)) < 0.0f &&
                glm::dot(planes[i], glm::vec4(max.x, max.y, max.z, 1.0f)) < 0.0f)
                return false;
        }
        return true;
    }
};

class World {
private:
    WorldConfig m_config;
    TerrainGenerator m_generator;
    
    std::unordered_map<int64_t, ChunkNode*> m_chunks;
    ObjectPool<ChunkNode> m_chunkPool;
    
    std::mutex m_queueMutex;
    std::queue<ChunkNode*> m_generatedQueue; 
    std::queue<ChunkNode*> m_meshedQueue;    
    
    ThreadPool m_pool; 
    int lastPx[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    int lastPz[8] = {-999,-999,-999,-999,-999,-999,-999,-999};
    
    int updateTimer = 0;
    std::atomic<bool> m_shutdown{false};
    Frustum m_frustum;
    
    std::unique_ptr<GpuMemoryManager> m_gpuMemory;
    GLuint m_indirectBuffer; 
    GLuint m_batchSSBO; 
    GLuint m_dummyVAO; 
    
    struct ChunkRequest { int x, y, z; int lod; int distSq; };

public:
    World(WorldConfig config) : m_config(config), m_generator(config) {
        size_t maxChunks = 0;
        for(int i=0; i<m_config.lodCount; i++) {
            int r = m_config.lodRadius[i];
            maxChunks += (r * 2 + 1) * (r * 2 + 1) * m_config.worldHeightChunks;
        }
        m_chunkPool.Init(maxChunks + 3000); 

        m_gpuMemory = std::make_unique<GpuMemoryManager>(512 * 1024 * 1024);

        glCreateBuffers(1, &m_indirectBuffer);
        glNamedBufferStorage(m_indirectBuffer, maxChunks * sizeof(DrawArraysIndirectCommand), nullptr, GL_DYNAMIC_STORAGE_BIT);

        glCreateBuffers(1, &m_batchSSBO);
        glNamedBufferStorage(m_batchSSBO, maxChunks * sizeof(glm::vec4), nullptr, GL_DYNAMIC_STORAGE_BIT);

        glCreateVertexArrays(1, &m_dummyVAO);
    }

    ~World() { 
        m_shutdown = true;
        m_chunks.clear();
        glDeleteBuffers(1, &m_indirectBuffer);
        glDeleteBuffers(1, &m_batchSSBO);
        glDeleteVertexArrays(1, &m_dummyVAO);
    }

    void Update(glm::vec3 cameraPos) {
        if (m_shutdown) return;
        
        ProcessQueues(); 

        updateTimer++;
        if (updateTimer > 5) {
            bool moved = false;
            for (int lod = 0; lod < m_config.lodCount; lod++) {
                int scale = 1 << lod;
                int px = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
                int pz = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));
                
                if (px != lastPx[lod] || pz != lastPz[lod]) {
                    lastPx[lod] = px;
                    lastPz[lod] = pz;
                    moved = true;
                }
            }

            if (moved) UnloadChunks(cameraPos);
            QueueNewChunks(cameraPos);
            updateTimer = 0;
        }
    }

    void ProcessQueues() {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        int processed = 0; 
        
        while (!m_generatedQueue.empty() && processed < 50) {
            ChunkNode* node = m_generatedQueue.front(); m_generatedQueue.pop();
            if (node->state == ChunkState::GENERATING) {
                if (node->chunk.isUniform && node->chunk.uniformID == 0) node->state = ChunkState::ACTIVE;
                else {
                    node->state = ChunkState::MESHING;
                    m_pool.enqueue([this, node]() { this->Task_Mesh(node); });
                }
            }
            processed++;
        }

        while (!m_meshedQueue.empty() && processed < 50) {
            ChunkNode* node = m_meshedQueue.front(); m_meshedQueue.pop();
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
                    }
                }
                node->state = ChunkState::ACTIVE;
            }
            processed++;
        }
    }

    void UnloadChunks(glm::vec3 cameraPos) {
        std::vector<int64_t> toRemove;
        
        for (auto& pair : m_chunks) {
            ChunkNode* node = pair.second;
            int lod = node->lod;
            int scale = node->scale;
            int limit = m_config.lodRadius[lod] + 3; 

            int camX = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
            int camZ = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));

            if (abs(node->cx - camX) > limit || abs(node->cz - camZ) > limit) {
                ChunkState s = node->state.load();
                if (s != ChunkState::GENERATING && s != ChunkState::MESHING) {
                    if (node->gpuOffset != -1) {
                        m_gpuMemory->Free(node->gpuOffset, node->vertexCount * sizeof(PackedVertex));
                        node->gpuOffset = -1;
                    }
                    toRemove.push_back(pair.first);
                    m_chunkPool.Release(node); 
                }
            }
        }
        for (auto k : toRemove) m_chunks.erase(k);
    }

    void Draw(Shader& shader, const glm::mat4& viewProj) {
        m_frustum.Update(viewProj);

        static std::vector<DrawArraysIndirectCommand> commands;
        static std::vector<glm::vec4> batchOffsets;
        commands.clear();
        batchOffsets.clear();

        int instanceID = 0;
        for (auto& pair : m_chunks) {
            ChunkNode* node = pair.second;
            
            if (node->state != ChunkState::ACTIVE) continue;
            if (node->gpuOffset == -1) continue; 
            
            // FRUSTUM CULLING
            if (!m_frustum.IsBoxVisible(node->minAABB, node->maxAABB)) continue;

            // STACKED RENDERING:
            // We draw ALL LODs that are active. 
            // The vertex shader handles Z-fighting by sinking lower LODs.
            
            DrawArraysIndirectCommand cmd;
            cmd.count = (uint32_t)node->vertexCount;
            cmd.instanceCount = 1;
            cmd.first = (uint32_t)(node->gpuOffset / sizeof(PackedVertex)); 
            cmd.baseInstance = instanceID; 

            commands.push_back(cmd);
            batchOffsets.push_back(glm::vec4(node->position, (float)node->scale));
            
            instanceID++;
        }

        if (commands.empty()) return;

        glNamedBufferSubData(m_indirectBuffer, 0, commands.size() * sizeof(DrawArraysIndirectCommand), commands.data());
        glNamedBufferSubData(m_batchSSBO, 0, batchOffsets.size() * sizeof(glm::vec4), batchOffsets.data());

        shader.use();
        glUniformMatrix4fv(glGetUniformLocation(shader.ID, "u_ViewProjection"), 1, GL_FALSE, glm::value_ptr(viewProj));
        
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_gpuMemory->GetID());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_batchSSBO);

        glBindVertexArray(m_dummyVAO);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer);
        glMultiDrawArraysIndirect(GL_TRIANGLES, 0, (GLsizei)commands.size(), 0);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        glBindVertexArray(0); 
    }

    void QueueNewChunks(glm::vec3 cameraPos) {
        std::vector<ChunkRequest> missing;
        missing.reserve(1000); 

        const int MAX_TASKS = 64; 

        for (int lod = 0; lod < m_config.lodCount; lod++) {
            int scale = 1 << lod;
            int r = m_config.lodRadius[lod];
            
            int px = (int)floor(cameraPos.x / (CHUNK_SIZE * scale));
            int pz = (int)floor(cameraPos.z / (CHUNK_SIZE * scale));

            for (int x = px - r; x <= px + r; x++) {
                for (int z = pz - r; z <= pz + r; z++) {
                    // NOTE: NO Inner Radius Check. We generate EVERYTHING. Stacked.
                    for (int y = 0; y < m_config.worldHeightChunks; y++) {
                        int64_t key = ChunkKey(x, y, z, lod);
                        if (m_chunks.find(key) == m_chunks.end()) {
                            int dx = x - px;
                            int dz = z - pz;
                            int distSq = dx*dx + dz*dz; 
                            missing.push_back({x, y, z, lod, distSq + (lod * 10000)});
                        }
                    }
                }
            }
        }

        std::sort(missing.begin(), missing.end(), [](const ChunkRequest& a, const ChunkRequest& b){
            return a.distSq < b.distSq;
        });

        int queued = 0;
        for (const auto& req : missing) {
            if (queued >= MAX_TASKS) break;

            int64_t key = ChunkKey(req.x, req.y, req.z, req.lod);
            if (m_chunks.find(key) == m_chunks.end()) {
                ChunkNode* newNode = m_chunkPool.Acquire();
                if (newNode) {
                    newNode->Reset(req.x, req.y, req.z, req.lod);
                    m_chunks[key] = newNode;
                    newNode->state = ChunkState::GENERATING;
                    m_pool.enqueue([this, newNode]() { this->Task_Generate(newNode); });
                    queued++;
                }
            }
        }
    }

    void Task_Generate(ChunkNode* node) {
        if (m_shutdown) return;
        FillChunk(node->chunk, node->cx, node->cy, node->cz, node->scale);
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_generatedQueue.push(node);
    }

    void Task_Mesh(ChunkNode* node) {
        LinearAllocator<PackedVertex> threadAllocator(1000000); 
        MeshChunk(node->chunk, threadAllocator, false); // Removed Scale, Removed Skirts
        node->cachedMesh.assign(threadAllocator.Data(), threadAllocator.Data() + threadAllocator.Count());
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_meshedQueue.push(node);
    }

    void FillChunk(Chunk& chunk, int cx, int cy, int cz, int scale) {
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

                // Terracing is crucial for far LODs to mesh well
                if (scale > 1) {
                    h = (h / scale) * scale;
                }
                
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
                    if (isFarLOD) {
                        blockID = 1; 
                    } else {
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
        for (auto& pair : m_chunks) m_chunkPool.Release(pair.second);
        m_chunks.clear();
        for(int i=0; i<8; i++) { lastPx[i] = -999; lastPz[i] = -999; }
    }
};