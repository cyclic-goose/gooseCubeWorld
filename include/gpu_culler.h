#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <stack>
#include <memory>
#include <unordered_map>

// Forward Declarations
class Shader;

// ================================================================================================
// GPU DATA STRUCTURES
// ================================================================================================

// Represents the static data of a chunk on the GPU.
// Must be aligned to 16 bytes for std140/std430 layout compatibility.
struct alignas(16) ChunkGpuData {
    glm::vec4 minAABB_scale; // xyz: min bounds, w: scale
    glm::vec4 maxAABB_pad;   // xyz: max bounds, w: padding
    
    // Opaque Mesh Range
    uint32_t firstVertexOpaque;    
    uint32_t vertexCountOpaque;   

    // Transparent Mesh Range
    uint32_t firstVertexTrans;
    uint32_t vertexCountTrans;
};

// Settings exposed to the UI (ImGui) to control culling behavior live.
struct CullerSettings {
    float zNear = 0.01f;
    float zFar = 10000000000.0f;   // Default: Infinite horizon
    bool occlusionEnabled = true; // with new terrain systems, cant get this working, either second mesh or non-collidables are screwing it up
    bool freezeCulling = false;  // Stops the compute shader updates (locks visibility)
    float frustumPadding = 0.0f; // Expand/Contract frustum for debugging
    float epsilonConstant = 0.0031; // 0.0031 seems to be a good aggresiveness without too much artifacting
};

// ================================================================================================
// GPU CULLER CLASS
// ================================================================================================

class GpuCuller {
public:
    // --------------------------------------------------------------------------------------------
    // LIFECYCLE
    // --------------------------------------------------------------------------------------------
    explicit GpuCuller(size_t maxChunks);
    ~GpuCuller();

    // --------------------------------------------------------------------------------------------
    // DATA MANAGEMENT
    // --------------------------------------------------------------------------------------------
    
    // Uploads chunk metadata to the GPU.
    // If chunkID exists, updates it. If new, allocates a new slot.
    uint32_t AddOrUpdateChunk(int64_t chunkID, 
                              const glm::vec3& minAABB, 
                              const glm::vec3& maxAABB, 
                              float scale, 
                              size_t firstVertexOpaque, 
                              size_t vertexCountOpaque,
                              size_t firstVertexTrans,
                              size_t vertexCountTrans);
    
    // Marks a slot as free and zeroes out the vertex count on the GPU to prevent drawing.
    void RemoveChunk(int64_t chunkID);

    // --------------------------------------------------------------------------------------------
    // FRAME PIPELINE
    // --------------------------------------------------------------------------------------------

    // Step 1: Compute Shader - Downsample the depth buffer for Occlusion Culling.
    void GenerateHiZ(GLuint depthTexture, int width, int height);

    // Step 2: Compute Shader - Determine which chunks are visible.
    // Populates the Indirect Buffers and Atomic Counters.
    void Cull(const glm::mat4& viewProj, 
              const glm::mat4& prevViewProj, 
              const glm::mat4& proj, 
              const glm::vec3& playerPos,
              GLuint depthTexture);

    // --------------------------------------------------------------------------------------------
    // GETTERS
    // --------------------------------------------------------------------------------------------
    
    uint32_t GetDrawCount() const { return m_drawnCount; }
    CullerSettings& GetSettings() { return m_settings; }
    size_t GetMaxChunks() const { return m_maxChunks; }

    // Buffers needed for rendering in World::Draw
    GLuint GetIndirectOpaque() const { return m_indirectBufferOpaque; }
    GLuint GetIndirectTrans() const { return m_indirectBufferTrans; }
    GLuint GetVisibleChunkBuffer() const { return m_visibleChunkBuffer; }
    GLuint GetAtomicCounter() const { return m_atomicCounterBuffer; }

private:
    // --------------------------------------------------------------------------------------------
    // INTERNAL HELPERS
    // --------------------------------------------------------------------------------------------
    void InitBuffers();

    // --------------------------------------------------------------------------------------------
    // STATE & SETTINGS
    // --------------------------------------------------------------------------------------------
    size_t m_maxChunks;
    CullerSettings m_settings;
    uint32_t m_drawnCount = 0;

    // Slot Management (allocating indices in the GPU array)
    std::unordered_map<int64_t, uint32_t> m_chunkSlots;
    std::stack<uint32_t> m_freeSlots;

    // --------------------------------------------------------------------------------------------
    // RENDER RESOURCES
    // --------------------------------------------------------------------------------------------
    std::unique_ptr<Shader> m_cullShader;
    std::unique_ptr<Shader> m_hizShader;

    // GPU Buffers (SSBOs)
    GLuint m_globalChunkBuffer = 0;   // Input: All chunk data
    GLuint m_indirectBufferOpaque = 0; // Output: Draw commands (Opaque)
    GLuint m_indirectBufferTrans = 0;  // Output: Draw commands (Trans)
    GLuint m_visibleChunkBuffer = 0;  // Output: IDs of visible chunks
    GLuint m_atomicCounterBuffer = 0; // Output: Count of visible chunks
    GLuint m_resultBuffer = 0;        // CPU-side copy of count (for UI)

    // Hi-Z Resources
    int m_depthPyramidWidth = 0;
    int m_depthPyramidHeight = 0;
    GLuint m_depthSampler = 0; 

    // Sync Object to prevent CPU stalls
    GLsync m_fence = nullptr;
};