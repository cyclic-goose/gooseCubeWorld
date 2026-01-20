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
#include "ringBufferSSBO.h"
#include "linearAllocator.h"
#include "shader.h"
#include "threadpool.h"
#include "chunk_pool.h"

// -----------------------------------------------------------------------------
// CONFIGURATION
// -----------------------------------------------------------------------------
struct WorldConfig {
    int seed = 1337;
    int renderDistance = 16;      
    int worldHeightChunks = 8;    
    
    float scale = 0.02f;          
    float hillAmplitude = 15.0f;  
    float hillFrequency = 1.0f;   
    float mountainAmplitude = 80.0f; 
    float mountainFrequency = 0.5f; 
    int seaLevel = 10;            
    bool enableCaves = false;     
    float caveThreshold = 0.5f;   
};

// -----------------------------------------------------------------------------
// CHUNK NODE
// -----------------------------------------------------------------------------
enum class ChunkState { MISSING, GENERATING, GENERATED, MESHING, MESHED, ACTIVE };

struct ChunkNode {
    Chunk chunk;
    glm::vec3 position;
    int cx, cy, cz; 
    
    std::vector<PackedVertex> cachedMesh; 
    std::atomic<ChunkState> state{ChunkState::MISSING};
    
    void Reset(int x, int y, int z) {
        cx = x; cy = y; cz = z;
        position = glm::vec3(x * CHUNK_SIZE, y * CHUNK_SIZE, z * CHUNK_SIZE);
        chunk.worldX = (int)position.x;
        chunk.worldY = (int)position.y;
        chunk.worldZ = (int)position.z;
        state = ChunkState::MISSING;
        cachedMesh.clear();
        chunk.isUniform = false;
    }
};

// -----------------------------------------------------------------------------
// TERRAIN GENERATOR
// -----------------------------------------------------------------------------
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

    bool IsCave(float x, float y, float z) const {
        if (!m_config.enableCaves) return false;
        float val = m_caveNoise->GenSingle3D(x * 0.02f, y * 0.04f, z * 0.02f, m_config.seed);
        return val > m_config.caveThreshold;
    }
};

inline int64_t ChunkKey(int x, int y, int z) {
    uint64_t ux = (uint64_t)(uint32_t)x & 0xFFFFFF; 
    uint64_t uz = (uint64_t)(uint32_t)z & 0xFFFFFF; 
    uint64_t uy = (uint64_t)(uint32_t)y & 0xFFFF;   
    return (ux << 40) | (uz << 16) | uy;
}

class World {
private:
    WorldConfig m_config;
    TerrainGenerator m_generator;
    ThreadPool m_pool;
    std::unordered_map<int64_t, ChunkNode*> m_chunks;
    ObjectPool<ChunkNode> m_chunkPool;
    std::mutex m_queueMutex;
    std::queue<ChunkNode*> m_generatedQueue; 
    std::queue<ChunkNode*> m_meshedQueue;    
    int lastPx = -99999, lastPz = -99999;

    struct ChunkRequest {
        int x, y, z;
        int distSq;
    };

public:
    World(WorldConfig config) : m_config(config), m_generator(config), m_pool() {
        int r = m_config.renderDistance + 5; 
        size_t maxChunks = (r * 2 + 1) * (r * 2 + 1) * m_config.worldHeightChunks;
        m_chunkPool.Init(maxChunks);
    }

    ~World() { 
        m_chunks.clear();
    }

    void Update(glm::vec3 cameraPos) {
        int px = (int)floor(cameraPos.x / CHUNK_SIZE);
        int pz = (int)floor(cameraPos.z / CHUNK_SIZE);

        ProcessQueues();

        if (px != lastPx || pz != lastPz) {
            UnloadChunks(px, pz);
            QueueNewChunks(px, pz);
            lastPx = px; lastPz = pz;
        }
    }

    // -------------------------------------------------------------------------
    // SCHEDULING (SORTED)
    // -------------------------------------------------------------------------
    void QueueNewChunks(int px, int pz) {
        int r = m_config.renderDistance;
        int h = m_config.worldHeightChunks;
        
        // 1. Collect ALL missing chunks in range
        std::vector<ChunkRequest> missing;
        // Reserve to prevent reallocation
        missing.reserve(2000); 

        for (int x = px - r; x <= px + r; x++) {
            for (int z = pz - r; z <= pz + r; z++) {
                for (int y = 0; y < h; y++) {
                    int64_t key = ChunkKey(x, y, z);
                    if (m_chunks.find(key) == m_chunks.end()) {
                        int dx = x - px;
                        int dz = z - pz;
                        // Use simple 2D distance for priority to load full columns
                        int distSq = dx*dx + dz*dz; 
                        missing.push_back({x, y, z, distSq});
                    }
                }
            }
        }

        // 2. Sort by distance (Closest first)
        std::sort(missing.begin(), missing.end(), [](const ChunkRequest& a, const ChunkRequest& b){
            return a.distSq < b.distSq;
        });

        // 3. Queue up to LIMIT
        const int MAX_NEW_TASKS = 64; 
        int queued = 0;

        for (const auto& req : missing) {
            if (queued >= MAX_NEW_TASKS) break;

            int64_t key = ChunkKey(req.x, req.y, req.z);
            // Double check existence (race condition safety)
            if (m_chunks.find(key) == m_chunks.end()) {
                ChunkNode* newNode = m_chunkPool.Acquire();
                if (newNode) {
                    newNode->Reset(req.x, req.y, req.z);
                    m_chunks[key] = newNode;
                    newNode->state = ChunkState::GENERATING;
                    m_pool.enqueue([this, newNode]() { this->Task_Generate(newNode); });
                    queued++;
                }
            }
        }
    }

    void UnloadChunks(int px, int pz) {
        // HYSTERESIS: Unload radius must be LARGER than load radius
        // If renderDistance is 16, unload at 20.
        // This keeps a 4-chunk buffer where chunks exist but aren't re-queued.
        int unloadRadius = m_config.renderDistance + 4; 
        
        std::vector<int64_t> toRemove;
        
        for (auto& pair : m_chunks) {
            ChunkNode* node = pair.second;
            // Check distance
            if (abs(node->cx - px) > unloadRadius || abs(node->cz - pz) > unloadRadius) {
                ChunkState s = node->state.load();
                // SAFE UNLOAD: Only recycle if idle
                if (s != ChunkState::GENERATING && s != ChunkState::MESHING) {
                    toRemove.push_back(pair.first);
                    m_chunkPool.Release(node); 
                }
            }
        }
        for (auto k : toRemove) m_chunks.erase(k);
    }

    // ... (Tasks, ProcessQueues, FillChunk, Draw, Reload remain same as previous)
    
    void Task_Generate(ChunkNode* node) {
        FillChunk(node->chunk, node->cx, node->cy, node->cz);
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_generatedQueue.push(node);
    }

    void Task_Mesh(ChunkNode* node) {
        LinearAllocator<PackedVertex> threadAllocator(200000); 
        MeshChunk(node->chunk, threadAllocator, 0, false);
        node->cachedMesh.assign(threadAllocator.Data(), threadAllocator.Data() + threadAllocator.Count());
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_meshedQueue.push(node);
    }

    void ProcessQueues() {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        int processed = 0; 
        const int LIMIT = 200;

        while (!m_generatedQueue.empty() && processed < LIMIT) {
            ChunkNode* node = m_generatedQueue.front(); m_generatedQueue.pop();
            if (node->state == ChunkState::GENERATING) {
                if (node->chunk.isUniform && node->chunk.uniformID == 0) {
                    node->state = ChunkState::ACTIVE;
                } else {
                    node->state = ChunkState::MESHING;
                    m_pool.enqueue([this, node]() { this->Task_Mesh(node); });
                }
            }
            processed++;
        }

        while (!m_meshedQueue.empty() && processed < LIMIT) {
            ChunkNode* node = m_meshedQueue.front(); m_meshedQueue.pop();
            if (node->state == ChunkState::MESHING) {
                node->state = ChunkState::ACTIVE;
            }
            processed++;
        }
    }

    void FillChunk(Chunk& chunk, int cx, int cy, int cz) {
        chunk.worldX = cx * CHUNK_SIZE;
        chunk.worldY = cy * CHUNK_SIZE;
        chunk.worldZ = cz * CHUNK_SIZE;

        int chunkBottomY = chunk.worldY;
        int chunkTopY = chunk.worldY + CHUNK_SIZE;

        int heights[CHUNK_SIZE_PADDED][CHUNK_SIZE_PADDED];
        int minHeight = 99999;
        int maxHeight = -99999;

        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                float wx = (float)(chunk.worldX + (x - 1));
                float wz = (float)(chunk.worldZ + (z - 1));
                int h = m_generator.GetHeight(wx, wz);
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
                int localMaxY = std::min(height - chunkBottomY + 2, CHUNK_SIZE_PADDED - 1);
                if (localMaxY < 0) continue; 

                for (int y = 0; y <= localMaxY; y++) {
                    int wy = chunk.worldY + (y - 1); 
                    uint8_t blockID = 1; 
                    if (wy <= height) {
                        if (wy == 0) blockID = 3; 
                        else if (wy == height) {
                            if (wy > 55) blockID = 4; else if (wy > 35) blockID = 1; else blockID = 2;
                        } else if (wy > height - 4) {
                            if (wy > 55) blockID = 4; else if (wy > 35) blockID = 1; else blockID = 5;
                        }
                        if (m_generator.IsCave((float)(chunk.worldX + (x-1)), (float)wy, (float)(chunk.worldZ + (z-1)))) blockID = 0;
                        chunk.Set(x, y, z, blockID);
                    }
                }
            }
        }
    }

    void Draw(RingBufferSSBO& renderer, LinearAllocator<PackedVertex>& scratch, Shader& shader, const glm::mat4& viewProj) {
        scratch.Reset();
        const int MAX_BATCH = 64; 
        std::vector<glm::vec3> batchOffsets;
        batchOffsets.reserve(MAX_BATCH);
        int currentBatchCount = 0;

        auto FlushBatch = [&]() {
            if (currentBatchCount == 0) return;
            void* gpuPtr = renderer.LockNextSegment();
            memcpy(gpuPtr, scratch.Data(), scratch.SizeBytes());
            shader.use();
            glUniformMatrix4fv(glGetUniformLocation(shader.ID, "u_ViewProjection"), 1, GL_FALSE, glm::value_ptr(viewProj));
            glUniform3fv(glGetUniformLocation(shader.ID, "u_ChunkOffsets"), currentBatchCount, glm::value_ptr(batchOffsets[0]));
            renderer.UnlockAndDraw(scratch.Count());
            scratch.Reset();
            batchOffsets.clear();
            currentBatchCount = 0;
        };

        for (auto& pair : m_chunks) {
            ChunkNode* node = pair.second;
            if (node->state != ChunkState::ACTIVE) continue;
            if (node->cachedMesh.empty()) continue;

            PackedVertex* dest = scratch.Allocate(node->cachedMesh.size());
            if (!dest) break; 

            memcpy(dest, node->cachedMesh.data(), node->cachedMesh.size() * sizeof(PackedVertex));
            
            uint32_t batchIDMask = (uint32_t)currentBatchCount << 16;
            size_t count = node->cachedMesh.size();
            for(size_t i=0; i<count; i++) dest[i].data2 = (dest[i].data2 & 0xFFFF) | batchIDMask;

            batchOffsets.push_back(node->position);
            currentBatchCount++;

            if (currentBatchCount >= MAX_BATCH) FlushBatch();
        }
        FlushBatch();
    }
    
    void Reload(WorldConfig newConfig) {
        m_config = newConfig;
        m_generator = TerrainGenerator(m_config);
        for (auto& pair : m_chunks) m_chunkPool.Release(pair.second);
        m_chunks.clear();
        lastPx = -99999;
    }
};