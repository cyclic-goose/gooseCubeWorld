#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <FastNoise/FastNoise.h>

#include "chunk.h"
#include "mesher.h"
#include "ringBufferSSBO.h"
#include "linearAllocator.h"
#include "shader.h"

struct WorldConfig {
    int seed = 1337;
    int renderDistance = 9;      
    float noiseFrequency = 0.005f; 
    float heightScale = 40.0f;    
    float baseHeight = 1.0f;     
    bool enableCaves = false;     
    float caveThreshold = 0.6f;   
};

class World {
private:
    WorldConfig m_config;
    
    struct ChunkNode {
        Chunk chunk;
        glm::vec3 position;
        std::vector<PackedVertex> cachedMesh; 
    };
    std::vector<ChunkNode> m_chunks;

    FastNoise::SmartNode<> m_terrainNoise;
    FastNoise::SmartNode<> m_caveNoise;

public:
    World(WorldConfig config) : m_config(config) {
        InitNoise();
        GenerateWorld();
    }

    void InitNoise() {
        auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
        auto fnFractal = FastNoise::New<FastNoise::FractalFBm>();
        fnFractal->SetSource(fnSimplex);
        fnFractal->SetOctaveCount(3);       
        fnFractal->SetGain(0.5f);           
        fnFractal->SetLacunarity(2.0f);     
        m_terrainNoise = fnFractal;

        auto fnCaves = FastNoise::New<FastNoise::Simplex>();
        m_caveNoise = fnCaves;
    }

    void GenerateWorld() {
        std::cout << "[World] Generating chunks (Radius: " << m_config.renderDistance << ")..." << std::endl;
        m_chunks.clear();

        int r = m_config.renderDistance;
        for (int x = -r; x <= r; x++) {
            for (int z = -r; z <= r; z++) {
                ChunkNode node;
                node.position = glm::vec3(x * CHUNK_SIZE, 0, z * CHUNK_SIZE);
                
                node.chunk.worldX = (int)node.position.x;
                node.chunk.worldY = (int)node.position.y;
                node.chunk.worldZ = (int)node.position.z;

                FillChunk(node.chunk);
                m_chunks.push_back(node);
            }
        }
        std::cout << "[World] Generated " << m_chunks.size() << " chunks. Meshing..." << std::endl;
        ReMeshWorld();
        std::cout << "[World] Ready!" << std::endl;
    }

    void ReMeshWorld() {
        // CRITICAL FIX: Increased buffer size from 20k to 200k.
        // A complex chunk can easily have 30k+ vertices.
        LinearAllocator<PackedVertex> tempAllocator(200000); 

        int totalVerts = 0;

        for (int i = 0; i < m_chunks.size(); i++) {
            ChunkNode& node = m_chunks[i];
            
            tempAllocator.Reset();
            // Mesh with ID 0, we patch it later
            MeshChunk(node.chunk, tempAllocator, 0, false);

            // Safety Check
            if (tempAllocator.Count() == 0) {
                // If this happens, either the chunk is empty Air, or the Allocator failed.
                // You can uncomment this to debug specific chunks:
                // std::cout << "Warning: Chunk " << i << " produced 0 vertices." << std::endl;
            }

            // Copy to cache
            node.cachedMesh.assign(tempAllocator.Data(), tempAllocator.Data() + tempAllocator.Count());
            totalVerts += node.cachedMesh.size();
        }
        std::cout << "[World] Total Static Vertices: " << totalVerts << std::endl;
    }

    void FillChunk(Chunk& chunk) {
        std::memset(chunk.voxels, 0, sizeof(chunk.voxels));
        for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
            for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
                float wx = (float)(chunk.worldX + (x - 1));
                float wz = (float)(chunk.worldZ + (z - 1));
                float noiseVal = m_terrainNoise->GenSingle2D(wx * m_config.noiseFrequency, wz * m_config.noiseFrequency, m_config.seed);
                int surfaceHeight = m_config.baseHeight + (int)((noiseVal + 1.0f) * 0.5f * m_config.heightScale);

                for (int y = 0; y < CHUNK_SIZE_PADDED; y++) {
                    int wy = chunk.worldY + (y - 1); 
                    if (wy == 0) { chunk.Set(x, y, z, 1); continue; } // Bedrock
                    if (wy < surfaceHeight) {
                        uint8_t blockID = 1; 
                        if (wy > surfaceHeight - 3) blockID = 2; 
                        if (m_config.enableCaves) {
                            float caveVal = m_caveNoise->GenSingle3D(wx * 0.05f, (float)wy * 0.1f, wz * 0.05f, m_config.seed);
                            if (caveVal > m_config.caveThreshold) blockID = 0; 
                        }
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

        for (const auto& node : m_chunks) {
            // Memory Patching for Batch ID
            PackedVertex* dest = scratch.Allocate(node.cachedMesh.size());
            if (dest) {
                memcpy(dest, node.cachedMesh.data(), node.cachedMesh.size() * sizeof(PackedVertex));
                uint32_t batchIDMask = (uint32_t)currentBatchCount << 16;
                size_t count = node.cachedMesh.size();
                for(size_t i=0; i<count; i++) {
                    dest[i].data2 = (dest[i].data2 & 0xFFFF) | batchIDMask;
                }
            }
            batchOffsets.push_back(node.position);
            currentBatchCount++;

            if (currentBatchCount >= MAX_BATCH) FlushBatch();
        }
        FlushBatch();
    }
    
    void Reload(WorldConfig newConfig) {
        m_config = newConfig;
        InitNoise();
        GenerateWorld();
    }
};