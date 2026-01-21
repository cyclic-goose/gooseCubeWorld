#pragma once

#include <glad/glad.h>
#include <vector>
#include <stack>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

class Shader;

// Matches the GPU-side structure exactly
struct alignas(16) ChunkGpuData {
    glm::vec4 minAABB_scale; 
    glm::vec4 maxAABB_pad;   
    uint32_t firstVertex;
    uint32_t vertexCount;    
    uint32_t pad1;
    uint32_t pad2;
};

class GpuCuller {
public:
    GpuCuller(size_t maxChunks);
    ~GpuCuller();

    uint32_t AddOrUpdateChunk(int64_t chunkID, const glm::vec3& minAABB, float scale, size_t firstVertex, size_t vertexCount);
    void RemoveChunk(int64_t chunkID);

    // Generates the Mip Chain for the depth texture.
    // Call this AFTER rendering the scene but BEFORE culling the next frame.
    void GenerateHiZ(GLuint depthTexture, int width, int height);

    // Executes Compute Shader for Culling
    // depthTexture: The full Hi-Z pyramid
    // viewProj: Camera View Projection Matrix
    // proj: Camera Projection Matrix (needed for screen space box projection)
    void Cull(const glm::mat4& viewProj, const glm::mat4& proj, GLuint depthTexture);

    void DrawIndirect(GLuint dummyVAO);
    uint32_t GetDrawCount() const { return m_drawnCount; }

private:
    void InitBuffers();

    size_t m_maxChunks;
    
    // Shaders
    std::unique_ptr<Shader> m_cullShader;
    std::unique_ptr<Shader> m_hizShader; // Downsampler

    // GPU Buffers
    GLuint m_globalChunkBuffer = 0;   
    GLuint m_indirectBuffer = 0;      
    GLuint m_visibleChunkBuffer = 0;  
    GLuint m_atomicCounterBuffer = 0; 
    GLuint m_resultBuffer = 0;        

    // Hi-Z State
    int m_depthPyramidWidth = 0;
    int m_depthPyramidHeight = 0;
    GLuint m_depthSampler = 0; // Sampler object for nearest_mipmap_nearest

    // Slot Management
    std::unordered_map<int64_t, uint32_t> m_chunkSlots;
    std::stack<uint32_t> m_freeSlots;

    uint32_t m_drawnCount = 0;
};