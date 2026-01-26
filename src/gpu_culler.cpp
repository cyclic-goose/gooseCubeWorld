#include "gpu_culler.h"
#include "shader.h"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

struct DrawArraysIndirectCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t first;
    uint32_t baseInstance;
};

GpuCuller::GpuCuller(size_t maxChunks) : m_maxChunks(maxChunks) {
    InitBuffers();
    
    for (size_t i = 0; i < m_maxChunks; ++i) {
        m_freeSlots.push((uint32_t)(m_maxChunks - 1 - i));
    }

    m_cullShader = std::make_unique<Shader>("./resources/CULL_COMPUTE.glsl");
    m_hizShader = std::make_unique<Shader>("./resources/HI_Z_DOWN.glsl");

    // Create a sampler that forces Nearest Mipmap Nearest.
    // This is crucial. Linear filtering blurs depth values, destroying the conservative "Min" property.
    glCreateSamplers(1, &m_depthSampler);
    glSamplerParameteri(m_depthSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glSamplerParameteri(m_depthSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(m_depthSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(m_depthSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GpuCuller::~GpuCuller() {
    if (m_globalChunkBuffer) glDeleteBuffers(1, &m_globalChunkBuffer);
    if (m_indirectBuffer) glDeleteBuffers(1, &m_indirectBuffer);
    if (m_visibleChunkBuffer) glDeleteBuffers(1, &m_visibleChunkBuffer);
    if (m_atomicCounterBuffer) glDeleteBuffers(1, &m_atomicCounterBuffer);
    if (m_resultBuffer) glDeleteBuffers(1, &m_resultBuffer);
    if (m_depthSampler) glDeleteSamplers(1, &m_depthSampler);
}

void GpuCuller::InitBuffers() {
    glCreateBuffers(1, &m_globalChunkBuffer);
    glNamedBufferStorage(m_globalChunkBuffer, m_maxChunks * sizeof(ChunkGpuData), nullptr, GL_DYNAMIC_STORAGE_BIT);

    glCreateBuffers(1, &m_indirectBuffer);
    glNamedBufferStorage(m_indirectBuffer, m_maxChunks * sizeof(DrawArraysIndirectCommand), nullptr, 0);

    glCreateBuffers(1, &m_visibleChunkBuffer);
    glNamedBufferStorage(m_visibleChunkBuffer, m_maxChunks * sizeof(glm::vec4), nullptr, 0);

    glCreateBuffers(1, &m_atomicCounterBuffer);
    glNamedBufferStorage(m_atomicCounterBuffer, sizeof(GLuint), nullptr, GL_DYNAMIC_STORAGE_BIT);

    glCreateBuffers(1, &m_resultBuffer);
    glNamedBufferStorage(m_resultBuffer, sizeof(GLuint), nullptr, GL_DYNAMIC_STORAGE_BIT);
    
    uint32_t zero = 0;
    glNamedBufferSubData(m_resultBuffer, 0, sizeof(GLuint), &zero);
}

uint32_t GpuCuller::AddOrUpdateChunk(int64_t chunkID, const glm::vec3& minAABB, float scale, size_t firstVertex, size_t vertexCount) {
    uint32_t slot;
    auto it = m_chunkSlots.find(chunkID);
    if (it != m_chunkSlots.end()) {
        slot = it->second;
    } else {
        if (m_freeSlots.empty()) return 0;
        slot = m_freeSlots.top();
        m_freeSlots.pop();
        m_chunkSlots[chunkID] = slot;
    }

    ChunkGpuData data;
    data.minAABB_scale = glm::vec4(minAABB, scale);
    data.maxAABB_pad = glm::vec4(minAABB + (float)(32 * scale), 0.0f); 
    data.firstVertex = (uint32_t)firstVertex;
    data.vertexCount = (uint32_t)vertexCount;
    data.pad1 = 0; data.pad2 = 0;

    glNamedBufferSubData(m_globalChunkBuffer, slot * sizeof(ChunkGpuData), sizeof(ChunkGpuData), &data);
    return slot;
}

void GpuCuller::RemoveChunk(int64_t chunkID) {
    auto it = m_chunkSlots.find(chunkID);
    if (it == m_chunkSlots.end()) return;

    uint32_t slot = it->second;
    m_chunkSlots.erase(it);
    m_freeSlots.push(slot);

    uint32_t zero = 0;
    size_t offset = (slot * sizeof(ChunkGpuData)) + offsetof(ChunkGpuData, vertexCount);
    glNamedBufferSubData(m_globalChunkBuffer, offset, sizeof(uint32_t), &zero);
}

void GpuCuller::GenerateHiZ(GLuint depthTexture, int width, int height) {
    m_depthPyramidWidth = width;
    m_depthPyramidHeight = height;

    // Calculate total mip levels: log2(max(w,h)) + 1
    int numLevels = 1 + (int)floor(log2(std::max(width, height)));

    m_hizShader->use();
    
    // Track input width/height for each level
    int inW = width;
    int inH = height;

    for (int i = 0; i < numLevels - 1; ++i) {
        // Dimensions of the DESTINATION mip level
        int outW = std::max(1, inW >> 1);
        int outH = std::max(1, inH >> 1);

        // Bind Read Image (Level i)
        glBindImageTexture(0, depthTexture, i, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
        
        // Bind Write Image (Level i+1)
        glBindImageTexture(1, depthTexture, i+1, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

        m_hizShader->setVec2("u_OutDimension", glm::vec2(outW, outH));
        m_hizShader->setVec2("u_InDimension", glm::vec2(inW, inH));
        
        // Dispatch
        // Local size is 32x32. 
        int groupsX = (outW + 31) / 32;
        int groupsY = (outH + 31) / 32;
        
        glDispatchCompute(groupsX, groupsY, 1);
        
        // Barrier to ensure Level i+1 is written before being read as Level i in next iteration
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        // Prepare for next iteration
        inW = outW;
        inH = outH;
    }
}

void GpuCuller::Cull(const glm::mat4& viewProj, const glm::mat4& prevViewProj, const glm::mat4& proj, GLuint depthTexture, bool occlusionCullingOn) {
    glGetNamedBufferSubData(m_resultBuffer, 0, sizeof(GLuint), &m_drawnCount);
    
    uint32_t zero = 0;
    glNamedBufferSubData(m_atomicCounterBuffer, 0, sizeof(GLuint), &zero);

    m_cullShader->use();
    m_cullShader->setMat4("u_ViewProjection", glm::value_ptr(viewProj));
    m_cullShader->setMat4("u_PrevViewProjection", glm::value_ptr(prevViewProj)); // Pass Previous Matrix
    m_cullShader->setUInt("u_MaxChunks", (uint32_t)m_maxChunks);
    
    // Frustum Params (extracted from Proj matrix)
    m_cullShader->setFloat("u_P00", proj[0][0]);
    m_cullShader->setFloat("u_P11", proj[1][1]);

    // Bind Depth Pyramid
    if (depthTexture != 0 && m_depthPyramidWidth > 0 && occlusionCullingOn) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glBindSampler(0, m_depthSampler); 
        
        m_cullShader->setInt("u_DepthPyramid", 0);
        m_cullShader->setVec2("u_PyramidSize", glm::vec2(m_depthPyramidWidth, m_depthPyramidHeight));
        m_cullShader->setBool("u_OcclusionEnabled", true);
    } else {
        m_cullShader->setBool("u_OcclusionEnabled", false);
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_globalChunkBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_indirectBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_visibleChunkBuffer);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, m_atomicCounterBuffer);

    glDispatchCompute((GLuint)(m_maxChunks + 63) / 64, 1, 1);

    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);
    glCopyNamedBufferSubData(m_atomicCounterBuffer, m_resultBuffer, 0, 0, sizeof(GLuint));
}

void GpuCuller::DrawIndirect(GLuint dummyVAO) {
    glBindVertexArray(dummyVAO);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer);
    glBindBuffer(GL_PARAMETER_BUFFER, m_atomicCounterBuffer); 
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_visibleChunkBuffer);

    glMultiDrawArraysIndirectCount(GL_TRIANGLES, 0, 0, (GLsizei)m_maxChunks, 0);

    glBindBuffer(GL_PARAMETER_BUFFER, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);
}