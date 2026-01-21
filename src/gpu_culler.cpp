#include "gpu_culler.h"
#include "shader.h"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>

struct DrawArraysIndirectCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t first;
    uint32_t baseInstance;
};

GpuCuller::GpuCuller(size_t maxChunks) : m_maxChunks(maxChunks) {
    InitBuffers();
    
    // Initialize Free List
    // We fill it in reverse so slot 0 is used first, just for neatness
    for (size_t i = 0; i < m_maxChunks; ++i) {
        m_freeSlots.push((uint32_t)(m_maxChunks - 1 - i));
    }

    m_cullShader = std::make_unique<Shader>("./resources/CULL_COMPUTE.glsl");
}

GpuCuller::~GpuCuller() {
    if (m_globalChunkBuffer) glDeleteBuffers(1, &m_globalChunkBuffer);
    if (m_indirectBuffer) glDeleteBuffers(1, &m_indirectBuffer);
    if (m_visibleChunkBuffer) glDeleteBuffers(1, &m_visibleChunkBuffer);
    if (m_atomicCounterBuffer) glDeleteBuffers(1, &m_atomicCounterBuffer);
    if (m_resultBuffer) glDeleteBuffers(1, &m_resultBuffer);
}

void GpuCuller::InitBuffers() {
    // 1. Global Chunk Buffer (Persistent Input)
    glCreateBuffers(1, &m_globalChunkBuffer);
    glNamedBufferStorage(m_globalChunkBuffer, m_maxChunks * sizeof(ChunkGpuData), nullptr, GL_DYNAMIC_STORAGE_BIT);

    // 2. Indirect Draw Buffer (Output)
    glCreateBuffers(1, &m_indirectBuffer);
    glNamedBufferStorage(m_indirectBuffer, m_maxChunks * sizeof(DrawArraysIndirectCommand), nullptr, 0);

    // 3. Visible Chunk Offsets (Output to Vertex Shader)
    glCreateBuffers(1, &m_visibleChunkBuffer);
    glNamedBufferStorage(m_visibleChunkBuffer, m_maxChunks * sizeof(glm::vec4), nullptr, 0);

    // 4. Atomic Counter
    glCreateBuffers(1, &m_atomicCounterBuffer);
    glNamedBufferStorage(m_atomicCounterBuffer, sizeof(GLuint), nullptr, GL_DYNAMIC_STORAGE_BIT);

    // 5. Result Buffer (Readback)
    glCreateBuffers(1, &m_resultBuffer);
    glNamedBufferStorage(m_resultBuffer, sizeof(GLuint), nullptr, GL_DYNAMIC_STORAGE_BIT);
    
    // Initialize result to 0
    uint32_t zero = 0;
    glNamedBufferSubData(m_resultBuffer, 0, sizeof(GLuint), &zero);
}

uint32_t GpuCuller::AddOrUpdateChunk(int64_t chunkID, const glm::vec3& minAABB, float scale, size_t firstVertex, size_t vertexCount) {
    uint32_t slot;
    
    auto it = m_chunkSlots.find(chunkID);
    if (it != m_chunkSlots.end()) {
        slot = it->second;
    } else {
        if (m_freeSlots.empty()) {
            std::cerr << "[GpuCuller] Error: Out of GPU Chunk slots! Increase MaxChunks." << std::endl;
            return 0; // Fallback
        }
        slot = m_freeSlots.top();
        m_freeSlots.pop();
        m_chunkSlots[chunkID] = slot;
    }

    // Prepare Data
    ChunkGpuData data;
    data.minAABB_scale = glm::vec4(minAABB, scale);
    data.maxAABB_pad = glm::vec4(minAABB + (float)(32 * scale), 0.0f); // Assuming CHUNK_SIZE=32, calculate max
    data.firstVertex = (uint32_t)firstVertex;
    data.vertexCount = (uint32_t)vertexCount; // Non-zero vertex count marks slot as ACTIVE
    data.pad1 = 0; data.pad2 = 0;

    // Upload Single Struct (Fast enough for streaming updates)
    glNamedBufferSubData(m_globalChunkBuffer, slot * sizeof(ChunkGpuData), sizeof(ChunkGpuData), &data);

    return slot;
}

void GpuCuller::RemoveChunk(int64_t chunkID) {
    auto it = m_chunkSlots.find(chunkID);
    if (it == m_chunkSlots.end()) return;

    uint32_t slot = it->second;
    m_chunkSlots.erase(it);
    m_freeSlots.push(slot);

    // Mark as empty on GPU by setting vertexCount to 0
    // We only need to write the vertexCount field, but writing the whole struct is often cleaner/aligned
    // Let's just write 0 to the vertexCount offset
    uint32_t zero = 0;
    // Offset of vertexCount is 32 + 4 = 36 bytes into the struct
    size_t offset = (slot * sizeof(ChunkGpuData)) + offsetof(ChunkGpuData, vertexCount);
    glNamedBufferSubData(m_globalChunkBuffer, offset, sizeof(uint32_t), &zero);
}

void GpuCuller::Cull(const glm::mat4& viewProj, GLuint chunkVertexSSBO) {
    // 1. Async Readback from PREVIOUS frame
    glGetNamedBufferSubData(m_resultBuffer, 0, sizeof(GLuint), &m_drawnCount);

    // 2. Reset Atomic Counter
    uint32_t zero = 0;
    glNamedBufferSubData(m_atomicCounterBuffer, 0, sizeof(GLuint), &zero);

    // 3. Dispatch Compute
    m_cullShader->use();
    m_cullShader->setMat4("u_ViewProjection", glm::value_ptr(viewProj));
    m_cullShader->setUInt("u_MaxChunks", (uint32_t)m_maxChunks);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_globalChunkBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_indirectBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_visibleChunkBuffer);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, m_atomicCounterBuffer);

    // Dispatch enough groups to cover MAX capacity, not just active count.
    // The shader will skip empty slots efficiently.
    glDispatchCompute((GLuint)(m_maxChunks + 63) / 64, 1, 1);

    // Barrier: Ensure Compute finishes writing before Draw reads
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    // 4. Copy Atomic Counter to Result Buffer for next frame readback
    glCopyNamedBufferSubData(m_atomicCounterBuffer, m_resultBuffer, 0, 0, sizeof(GLuint));
}

void GpuCuller::DrawIndirect(GLuint dummyVAO) {
    glBindVertexArray(dummyVAO);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer);
    glBindBuffer(GL_PARAMETER_BUFFER, m_atomicCounterBuffer); // Use atomic counter value as draw count

    // Binding 1 matches VERT_PRIMARY.glsl 'ChunkOffsets'
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_visibleChunkBuffer);

    glMultiDrawArraysIndirectCount(GL_TRIANGLES, 0, 0, (GLsizei)m_maxChunks, 0);

    glBindBuffer(GL_PARAMETER_BUFFER, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);
}