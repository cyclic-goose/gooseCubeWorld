#pragma once

#include <glad/glad.h>
#include <vector>
#include <stack>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>

class Shader;

struct alignas(16) ChunkGpuData {
    glm::vec4 minAABB_scale; 
    glm::vec4 maxAABB_pad;   
    uint32_t firstVertex;
    uint32_t vertexCount;    
    uint32_t pad1;
    uint32_t pad2;
};

// New struct to expose controls to ImGui
struct CullerSettings {
    float zNear = 0.1f;
    float zFar = 100000000.0f; // Default huge far plane
    bool occlusionEnabled = true;
    bool freezeCulling = false; // Useful for debugging frustum
    float frustumPadding = 0.0f; // Expand/contract frustum slightly
};

class GpuCuller {
public:
    GpuCuller(size_t maxChunks);
    ~GpuCuller();

    uint32_t AddOrUpdateChunk(int64_t chunkID, const glm::vec3& minAABB, const glm::vec3& maxAABB, float scale, size_t firstVertex, size_t vertexCount);
    
    void RemoveChunk(int64_t chunkID);

    void GenerateHiZ(GLuint depthTexture, int width, int height);

    void Cull(const glm::mat4& viewProj, const glm::mat4& prevViewProj, const glm::mat4& proj, GLuint depthTexture);

    void DrawIndirect(GLuint dummyVAO);
    
    uint32_t GetDrawCount() const { return m_drawnCount; }

    // Accessor for ImGui
    CullerSettings& GetSettings() { return m_settings; }

private:
    void InitBuffers();

    size_t m_maxChunks;
    CullerSettings m_settings; // Instance of settings
    
    std::unique_ptr<Shader> m_cullShader;
    std::unique_ptr<Shader> m_hizShader;

    GLuint m_globalChunkBuffer = 0;   
    GLuint m_indirectBuffer = 0;      
    GLuint m_visibleChunkBuffer = 0;  
    GLuint m_atomicCounterBuffer = 0; 
    GLuint m_resultBuffer = 0;        

    int m_depthPyramidWidth = 0;
    int m_depthPyramidHeight = 0;
    GLuint m_depthSampler = 0; 

    std::unordered_map<int64_t, uint32_t> m_chunkSlots;
    std::stack<uint32_t> m_freeSlots;

    uint32_t m_drawnCount = 0;
};