#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <queue>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <FastNoise/FastNoise.h>

#include "chunk.h"
#include "mesher.h"
#include "ringBufferSSBO.h"
#include "linearAllocator.h"
#include "shader.h"
#include "threadpool.h"

// -----------------------------------------------------------------------------
// CONFIGURATION
// -----------------------------------------------------------------------------
struct WorldConfig {
    int seed = 1337;
    int renderDistance = 16;      
    float scale = 0.02f;          
    float hillAmplitude = 15.0f;  
    float hillFrequency = 1.0f;   
    float mountainAmplitude = 50.0f; 
    float mountainFrequency = 0.5f; 
    int seaLevel = 0;            
    bool enableCaves = false;     
    float caveThreshold = 0.5f;   
};

// -----------------------------------------------------------------------------
// TERRAIN GENERATOR (Thread Safe Copy)
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

    const WorldConfig& GetConfig() const { return m_config; }
};

// -----------------------------------------------------------------------------
// CHUNK NODE & COORDINATE HASHING
// -----------------------------------------------------------------------------
// Packs X,Z into a single 64-bit int for map keys. 
// Assumes chunk coords fit in 32 bits (Range +/- 2 Billion).
inline int64_t ChunkKey(int x, int z) {
    return ((int64_t)x << 32) | ((int64_t)z & 0xFFFFFFFF);
}

enum class ChunkState {
    MISSING,
    GENERATING, // In thread pool
    GENERATED,  // Terrain done, waiting for mesh
    MESHING,    // In thread pool
    MESHED,     // Mesh done, waiting for upload
    ACTIVE      // On GPU / Ready to Draw
};

struct ChunkNode {
    Chunk chunk;
    glm::vec3 position;
    int cx, cz; // Chunk Coordinates
    
    std::vector<PackedVertex> cachedMesh; 
    std::atomic<ChunkState> state{ChunkState::MISSING};
    
    ChunkNode(int x, int z) : cx(x), cz(z), position(x * CHUNK_SIZE, 0, z * CHUNK_SIZE) {
        chunk.worldX = (int)position.x;
        chunk.worldY = (int)position.y;
        chunk.worldZ = (int)position.z;
    }
};

// -----------------------------------------------------------------------------
// WORLD CLASS (STREAMING)
// -----------------------------------------------------------------------------
class World {
private:
    WorldConfig m_config;
    TerrainGenerator m_generator;
    ThreadPool m_pool;

    // Chunk Storage
    std::unordered_map<int64_t, ChunkNode*> m_chunks;
    
    // Coordination Queues (Thread Safe)
    std::mutex m_resultMutex;
    std::queue<ChunkNode*> m_generatedQueue; // Chunks finished generating
    std::queue<ChunkNode*> m_meshedQueue;    // Chunks finished meshing

public:
    World(WorldConfig config) : m_config(config), m_generator(config), m_pool() {
        // No initial generation loop here. We let Update() handle it.
    }

    ~World() {
        // Cleanup memory
        for (auto& pair : m_chunks) {
            delete pair.second;
        }
    }

    // Call this every frame with camera position
    void Update(glm::vec3 cameraPos) {
        int playerChunkX = (int)floor(cameraPos.x / CHUNK_SIZE);
        int playerChunkZ = (int)floor(cameraPos.z / CHUNK_SIZE);

        // 1. Unload far chunks
        UnloadChunks(playerChunkX, playerChunkZ);

        // 2. Queue new chunks (Spiral Outwards pattern is best, simple box for now)
        LoadChunks(playerChunkX, playerChunkZ);

        // 3. Process Async Results
        ProcessQueues();
    }

    // -------------------------------------------------------------------------
    // LOAD / UNLOAD LOGIC
    // -------------------------------------------------------------------------
    void LoadChunks(int px, int pz) {
        int r = m_config.renderDistance;
        
        // Simple loop, prioritizing closer chunks would be better
        for (int x = px - r; x <= px + r; x++) {
            for (int z = pz - r; z <= pz + r; z++) {
                int64_t key = ChunkKey(x, z);
                
                // If chunk doesn't exist, create it
                if (m_chunks.find(key) == m_chunks.end()) {
                    ChunkNode* newNode = new ChunkNode(x, z);
                    m_chunks[key] = newNode;
                    
                    // Dispatch Generation Task
                    newNode->state = ChunkState::GENERATING;
                    m_pool.enqueue([this, newNode]() {
                        this->Task_Generate(newNode);
                    });
                }
                // If it exists and is GENERATED, check if we can mesh it
                else {
                    ChunkNode* node = m_chunks[key];
                    if (node->state == ChunkState::GENERATED) {
                        TryMeshChunk(node);
                    }
                }
            }
        }
    }

    void UnloadChunks(int px, int pz) {
        int r = m_config.renderDistance + 2; // Keep a buffer
        
        // Iterate and remove far chunks
        // Note: Safe removal from map while iterating is tricky, using simple copy list approach
        std::vector<int64_t> toRemove;
        for (auto& pair : m_chunks) {
            ChunkNode* node = pair.second;
            if (abs(node->cx - px) > r || abs(node->cz - pz) > r) {
                // Only unload if not currently being processed by a thread to avoid segfaults
                // (Simple approach: leak memory slightly until thread done, or wait. 
                // For this demo, we assume threads finish fast enough or we just delete only IDLE ones)
                if (node->state == ChunkState::ACTIVE || node->state == ChunkState::GENERATED || node->state == ChunkState::MESHED) {
                    toRemove.push_back(pair.first);
                    delete node;
                }
            }
        }
        for (auto k : toRemove) m_chunks.erase(k);
    }

    void TryMeshChunk(ChunkNode* node) {
        // We need all 4 neighbors + neighbors-of-neighbors for complete AO/Smoothing, 
        // but for basic meshing we need immediate neighbors.
        // Actually, greedy meshing inside a chunk handles padding internally, 
        // BUT the padding logic inside FillChunk relies on local math, so it is self-contained!
        // Wait, chunk.h padding access is self-contained. 
        // So we don't strictly need neighbors loaded to mesh *if* FillChunk generated the border padding.
        // My FillChunk implementation DOES generate padding! 
        
        // So we can mesh immediately.
        node->state = ChunkState::MESHING;
        m_pool.enqueue([this, node]() {
            this->Task_Mesh(node);
        });
    }

    // -------------------------------------------------------------------------
    // WORKER TASKS
    // -------------------------------------------------------------------------
    void Task_Generate(ChunkNode* node) {
        FillChunk(node->chunk, node->cx, node->cz);
        
        // Notify Main Thread
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_generatedQueue.push(node);
    }

    void Task_Mesh(ChunkNode* node) {
        // Use a local allocator for thread safety
        LinearAllocator<PackedVertex> threadAllocator(200000); 
        
        // Perform Meshing
        // Note: passing 0 as batchId, we patch it later in Draw
        MeshChunk(node->chunk, threadAllocator, 0, false);

        // Copy result to node
        node->cachedMesh.assign(threadAllocator.Data(), threadAllocator.Data() + threadAllocator.Count());

        // Notify Main Thread
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_meshedQueue.push(node);
    }

    // -------------------------------------------------------------------------
    // MAIN THREAD PROCESSING
    // -------------------------------------------------------------------------
    void ProcessQueues() {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        
        // Process Generated Chunks
        while (!m_generatedQueue.empty()) {
            ChunkNode* node = m_generatedQueue.front();
            m_generatedQueue.pop();
            if (node->state == ChunkState::GENERATING) {
                node->state = ChunkState::GENERATED;
            }
        }

        // Process Meshed Chunks
        while (!m_meshedQueue.empty()) {
            ChunkNode* node = m_meshedQueue.front();
            m_meshedQueue.pop();
            if (node->state == ChunkState::MESHING) {
                node->state = ChunkState::ACTIVE;
            }
        }
    }

    // -------------------------------------------------------------------------
    // GENERATION LOGIC (Thread Safe)
    // -------------------------------------------------------------------------
    void FillChunk(Chunk& chunk, int cx, int cz) {
        std::memset(chunk.voxels, 0, sizeof(chunk.voxels));
        
        // Need to calculate worldX based on chunk coord
        chunk.worldX = cx * CHUNK_SIZE;
        chunk.worldY = 0;
        chunk.worldZ = cz * CHUNK_SIZE;

        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                float wx = (float)(chunk.worldX + (x - 1));
                float wz = (float)(chunk.worldZ + (z - 1));
                int height = m_generator.GetHeight(wx, wz);

                for (int y = 0; y < CHUNK_SIZE_PADDED; y++) {
                    int wy = chunk.worldY + (y - 1); 
                    if (wy == 0) { chunk.Set(x, y, z, 3); continue; } 

                    if (wy <= height) {
                        uint8_t blockID = 1; 
                        if (wy == height) {
                            if (wy > 55) blockID = 4;      
                            else if (wy > 35) blockID = 1; 
                            else blockID = 2;              
                        } else if (wy > height - 4) {
                            if (wy > 55) blockID = 4;      
                            else if (wy > 35) blockID = 1; 
                            else blockID = 5;              
                        }
                        if (m_generator.IsCave(wx, (float)wy, wz)) blockID = 0;
                        chunk.Set(x, y, z, blockID);
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // DRAW
    // -------------------------------------------------------------------------
    void Draw(RingBufferSSBO& renderer, LinearAllocator<PackedVertex>& scratch, Shader& shader, const glm::mat4& viewProj) {
        scratch.Reset();
        const int MAX_BATCH = 64; 
        std::vector<glm::vec3> batchOffsets;
        batchOffsets.reserve(MAX_BATCH);
        int currentBatchCount = 0;
        bool bufferFull = false;

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
            
            // Only draw ACTIVE chunks
            if (node->state != ChunkState::ACTIVE) continue;

            // Frustum Culling Placeholder (Check distance for simple cull)
            // if (glm::distance(node->position, cameraPos) > m_config.renderDistance * CHUNK_SIZE * 1.5) continue;

            PackedVertex* dest = scratch.Allocate(node->cachedMesh.size());
            if (!dest) {
                if (!bufferFull) {
                    std::cout << "[Warning] Scratch Buffer Full!" << std::endl;
                    bufferFull = true;
                }
                continue; 
            }

            memcpy(dest, node->cachedMesh.data(), node->cachedMesh.size() * sizeof(PackedVertex));
            
            // Batch Patching
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
        
        // Clear all chunks
        for (auto& pair : m_chunks) delete pair.second;
        m_chunks.clear();
        
        // Will regenerate automatically in next Update()
    }
};