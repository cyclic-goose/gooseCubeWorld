#pragma once

#include <glad/glad.h>
#include <vector>
#include <stack>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

// Forward declaration
class Shader;

// Matches the GPU-side structure exactly
struct alignas(16) ChunkGpuData {
    glm::vec4 minAABB_scale; // xyz = min bounds, w = scale
    glm::vec4 maxAABB_pad;   // xyz = max bounds, w = padding
    uint32_t firstVertex;
    uint32_t vertexCount;    // If 0, slot is considered empty by GPU
    uint32_t pad1;
    uint32_t pad2;
};

class GpuCuller {
public:
    GpuCuller(size_t maxChunks);
    ~GpuCuller();

    // No longer O(N). Call this only when a chunk becomes ACTIVE or changes mesh.
    // Returns the GPU slot index assigned to this chunk.
    uint32_t AddOrUpdateChunk(int64_t chunkID, const glm::vec3& minAABB, float scale, size_t firstVertex, size_t vertexCount);

    // Call this when a chunk is unloaded.
    void RemoveChunk(int64_t chunkID);

    // Executes the Compute Shader to fill the IndirectDrawBuffer
    void Cull(const glm::mat4& viewProj, GLuint chunkVertexSSBO);

    // Binds buffers and executes the MultiDrawIndirect
    void DrawIndirect(GLuint dummyVAO);

    // Returns the number of chunks drawn in the PREVIOUS frame (async readback)
    uint32_t GetDrawCount() const { return m_drawnCount; }

private:
    void InitBuffers();

    size_t m_maxChunks;
    
    // Shader
    std::unique_ptr<Shader> m_cullShader;

    // GPU Buffers
    GLuint m_globalChunkBuffer = 0;   // SSBO Binding 0: Persistent store of ALL chunks
    GLuint m_indirectBuffer = 0;      // SSBO Binding 1: Draw Commands (Output)
    GLuint m_visibleChunkBuffer = 0;  // SSBO Binding 2: Offsets for Vertex Shader (Output)
    GLuint m_atomicCounterBuffer = 0; // Atomic Counter
    GLuint m_resultBuffer = 0;        // Async Readback Buffer

    // Slot Management
    // Maps ChunkKey (int64) -> GPU Array Index (uint32)
    std::unordered_map<int64_t, uint32_t> m_chunkSlots;
    
    // Stack of empty indices in the GlobalChunkBuffer
    std::stack<uint32_t> m_freeSlots;

    uint32_t m_drawnCount = 0;
};