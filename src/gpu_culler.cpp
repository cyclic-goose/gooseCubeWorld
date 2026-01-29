#include "gpu_culler.h"
#include "shader.h"

#include <iostream>
#include <cmath>
#include <algorithm> 
#include <glm/gtc/type_ptr.hpp>

// ================================================================================================
// INTERNAL STRUCTURES
// ================================================================================================

// This matches the OpenGL command structure required for glMultiDrawArraysIndirect
struct DrawArraysIndirectCommand {
    uint32_t count;         // Number of vertices to draw
    uint32_t instanceCount; // Number of instances (usually 1)
    uint32_t first;         // Index of the first vertex
    uint32_t baseInstance;  // Instance ID offset
};

// ================================================================================================
// LIFECYCLE
// ================================================================================================

GpuCuller::GpuCuller(size_t maxChunks) : m_maxChunks(maxChunks) {
    InitBuffers();
    
    // Fill the free slots stack (descending order so we use slot 0 first)
    for (size_t i = 0; i < m_maxChunks; ++i) {
        m_freeSlots.push((uint32_t)(m_maxChunks - 1 - i));
    }

    // Load Shaders
    m_cullShader = std::make_unique<Shader>("./resources/CULL_COMPUTE.glsl");
    m_hizShader = std::make_unique<Shader>("./resources/HI_Z_DOWN.glsl");

    // Create Sampler for Depth Texture reading
    glCreateSamplers(1, &m_depthSampler);
    glSamplerParameteri(m_depthSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glSamplerParameteri(m_depthSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(m_depthSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(m_depthSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GpuCuller::~GpuCuller() {
    if (m_globalChunkBuffer)   glDeleteBuffers(1, &m_globalChunkBuffer);
    if (m_indirectBufferOpaque) glDeleteBuffers(1, &m_indirectBufferOpaque);
    if (m_indirectBufferTrans)  glDeleteBuffers(1, &m_indirectBufferTrans);
    if (m_visibleChunkBuffer)  glDeleteBuffers(1, &m_visibleChunkBuffer);
    if (m_atomicCounterBuffer) glDeleteBuffers(1, &m_atomicCounterBuffer);
    if (m_resultBuffer)        glDeleteBuffers(1, &m_resultBuffer);
    if (m_depthSampler)        glDeleteSamplers(1, &m_depthSampler);
    if (m_fence)               glDeleteSync(m_fence);
}

void GpuCuller::InitBuffers() {
    // 1. Global Chunk Data (Input)
    // Stores AABB and Vertex info for *every* chunk in existence.
    glCreateBuffers(1, &m_globalChunkBuffer);
    glNamedBufferStorage(m_globalChunkBuffer, m_maxChunks * sizeof(ChunkGpuData), nullptr, GL_DYNAMIC_STORAGE_BIT);

    // 2a. Indirect Draw Command Buffer (Output - Opaque)
    glCreateBuffers(1, &m_indirectBufferOpaque);
    glNamedBufferStorage(m_indirectBufferOpaque, m_maxChunks * sizeof(DrawArraysIndirectCommand), nullptr, 0);

    // 2b. Indirect Draw Command Buffer (Output - Transparent)
    glCreateBuffers(1, &m_indirectBufferTrans);
    glNamedBufferStorage(m_indirectBufferTrans, m_maxChunks * sizeof(DrawArraysIndirectCommand), nullptr, 0);

    // 3. Visible Chunk Index Buffer (Output)
    // Stores indices/IDs of chunks that passed the test.
    glCreateBuffers(1, &m_visibleChunkBuffer);
    glNamedBufferStorage(m_visibleChunkBuffer, m_maxChunks * sizeof(glm::vec4), nullptr, 0);

    // 4. Atomic Counter (Output)
    // Tracks how many chunks passed the test this frame.
    glCreateBuffers(1, &m_atomicCounterBuffer);
    glNamedBufferStorage(m_atomicCounterBuffer, sizeof(GLuint), nullptr, GL_DYNAMIC_STORAGE_BIT);

    // 5. Result Buffer (CPU Readback)
    // Used to copy the Atomic Counter value so we can read it on CPU without stalling heavily.
    glCreateBuffers(1, &m_resultBuffer);
    glNamedBufferStorage(m_resultBuffer, sizeof(GLuint), nullptr, GL_DYNAMIC_STORAGE_BIT);
    
    // Initialize result to 0
    uint32_t zero = 0;
    glNamedBufferSubData(m_resultBuffer, 0, sizeof(GLuint), &zero);
}

// ================================================================================================
// DATA MANAGEMENT
// ================================================================================================

uint32_t GpuCuller::AddOrUpdateChunk(int64_t chunkID, 
                                     const glm::vec3& minAABB, 
                                     const glm::vec3& maxAABB, 
                                     float scale, 
                                     size_t firstVertexOpaque, 
                                     size_t vertexCountOpaque,
                                     size_t firstVertexTrans,
                                     size_t vertexCountTrans) 
{
    uint32_t slot;
    auto it = m_chunkSlots.find(chunkID);
    
    // Find existing slot or allocate new one
    if (it != m_chunkSlots.end()) {
        slot = it->second;
    } else {
        if (m_freeSlots.empty()) {
            std::cerr << "[GpuCuller] Error: No free slots available for new chunk!" << std::endl;
            return 0; 
        }
        slot = m_freeSlots.top();
        m_freeSlots.pop();
        m_chunkSlots[chunkID] = slot;
    }

    // Prepare data
    ChunkGpuData data;
    data.minAABB_scale = glm::vec4(minAABB, scale);
    data.maxAABB_pad   = glm::vec4(maxAABB, 0.0f); // tight Bounds
    
    data.firstVertexOpaque = (uint32_t)firstVertexOpaque;
    data.vertexCountOpaque = (uint32_t)vertexCountOpaque;
    
    data.firstVertexTrans  = (uint32_t)firstVertexTrans;
    data.vertexCountTrans  = (uint32_t)vertexCountTrans;

    // Upload to GPU (DSA)
    glNamedBufferSubData(m_globalChunkBuffer, slot * sizeof(ChunkGpuData), sizeof(ChunkGpuData), &data);
    
    return slot;
}

void GpuCuller::RemoveChunk(int64_t chunkID) {
    auto it = m_chunkSlots.find(chunkID);
    if (it == m_chunkSlots.end()) return;

    uint32_t slot = it->second;
    m_chunkSlots.erase(it);
    m_freeSlots.push(slot);

    // Zero out the vertex count on GPU so the compute shader ignores this slot immediately
    ChunkGpuData zeroData = {}; 
    glNamedBufferSubData(m_globalChunkBuffer, slot * sizeof(ChunkGpuData), sizeof(ChunkGpuData), &zeroData);
}

// ================================================================================================
// FRAME PIPELINE
// ================================================================================================

void GpuCuller::GenerateHiZ(GLuint depthTexture, int width, int height) {
    m_depthPyramidWidth = width;
    m_depthPyramidHeight = height;

    // Calculate number of mip levels needed
    int numLevels = 1 + (int)floor(log2(std::max(width, height)));

    m_hizShader->use();
    
    int inW = width;
    int inH = height;

    // Iteratively downsample the depth texture
    for (int i = 0; i < numLevels - 1; ++i) {
        int outW = std::max(1, inW >> 1);
        int outH = std::max(1, inH >> 1);

        // Bind Read (Level i) and Write (Level i+1) images
        glBindImageTexture(0, depthTexture, i, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
        glBindImageTexture(1, depthTexture, i+1, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

        m_hizShader->setVec2("u_OutDimension", glm::vec2(outW, outH));
        m_hizShader->setVec2("u_InDimension", glm::vec2(inW, inH));
        
        // Dispatch compute (8x8 local group size assumed in shader, so 32 fits nice)
        int groupsX = (outW + 31) / 32;
        int groupsY = (outH + 31) / 32;
        
        glDispatchCompute(groupsX, groupsY, 1);
        
        // Ensure writes to Level i+1 are finished before reading it as Level i in next iteration
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        inW = outW;
        inH = outH;
    }
}

void GpuCuller::Cull(const glm::mat4& viewProj, const glm::mat4& prevViewProj, const glm::mat4& proj, GLuint depthTexture) {
    // ASYNC READBACK
    // Instead of forcing a stall with glGetNamedBufferSubData, we check if the GPU is done.
    if (m_fence) {
        // Wait up to 0ns (instant check)
        GLenum waitReturn = glClientWaitSync(m_fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
        
        if (waitReturn == GL_ALREADY_SIGNALED || waitReturn == GL_CONDITION_SATISFIED) {
            // GPU is done, safe to read without stalling
            glGetNamedBufferSubData(m_resultBuffer, 0, sizeof(GLuint), &m_drawnCount);
            
            // Cleanup fence
            glDeleteSync(m_fence);
            m_fence = nullptr;
        }
    }
    
    // 2. Reset atomic counter for this frame
    uint32_t zero = 0;
    glNamedBufferSubData(m_atomicCounterBuffer, 0, sizeof(GLuint), &zero);

    // 3. Setup Compute Shader
    m_cullShader->use();
    m_cullShader->setMat4("u_ViewProjection", glm::value_ptr(viewProj));
    m_cullShader->setMat4("u_PrevViewProjection", glm::value_ptr(prevViewProj));
    m_cullShader->setUInt("u_MaxChunks", (uint32_t)m_maxChunks);
    
    // Projection parameters for screen-space AABB projection
    m_cullShader->setFloat("u_P00", proj[0][0]);
    m_cullShader->setFloat("u_P11", proj[1][1]);
    
    // Settings from ImGui
    m_cullShader->setFloat("u_zNear", m_settings.zNear);
    m_cullShader->setFloat("u_zFar", m_settings.zFar);
    
    // Occlusion Settings
    // CRITICAL FIX: Only enable occlusion if we actually drew something previously!
    // If m_drawnCount == 0, the depth buffer is likely empty/clear. 
    // If the shader compares against "Clear Depth" (Wall), it will cull everything forever.
    bool occlusionActive = m_settings.occlusionEnabled && 
                           depthTexture != 0 && 
                           m_depthPyramidWidth > 0 &&
                           m_drawnCount > 0; // <--- SELF HEALING CHECK

    if (occlusionActive) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glBindSampler(0, m_depthSampler); 
        
        m_cullShader->setInt("u_DepthPyramid", 0);
        m_cullShader->setVec2("u_PyramidSize", glm::vec2(m_depthPyramidWidth, m_depthPyramidHeight));
        m_cullShader->setBool("u_OcclusionEnabled", true);
    } else {
        m_cullShader->setBool("u_OcclusionEnabled", false);
    }

    // 4. Bind Buffers (SSBOs)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_globalChunkBuffer);   // Binding 0: Input Data
    
    // Output Buffers (Bindings must match CULL_COMPUTE.glsl)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_indirectBufferOpaque);      
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_visibleChunkBuffer);  
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_indirectBufferTrans); 

    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, m_atomicCounterBuffer); // Binding 0: Counter

    // 5. Dispatch
    // One thread per chunk. Group size is typically 64.
    glDispatchCompute((GLuint)(m_maxChunks + 63) / 64, 1, 1);

    // 6. Memory Barrier
    // Wait for Shader Storage writes (Indirect Buffer) and Atomic Counter writes
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);
    
    // 7. Copy Atomic Counter to Result Buffer (for CPU readback next frame)
    glCopyNamedBufferSubData(m_atomicCounterBuffer, m_resultBuffer, 0, 0, sizeof(GLuint));

    // 8. Create Sync Fence
    // This allows us to query (in the next frame) if this Copy is finished before we try to read it.
    if (m_fence) glDeleteSync(m_fence); // Should be null, but safety first
    m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}