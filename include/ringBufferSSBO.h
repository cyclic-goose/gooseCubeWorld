#pragma once
#include "persistentSSBO.h"
#include <vector>
#include <iostream>
#include <cassert>

/**
 * class RingBufferSSBO
 * --------------------
 * PURPOSE:
 * Uploading data to the GPU is dangerous if the GPU is still reading that data.
 * This is a "Write-After-Read" (WAR) hazard.
 * * SOLUTION:
 * We split the GPU buffer into 3 parts (Triple Buffering).
 * Frame 1: CPU writes Part A. GPU reads Part A.
 * Frame 2: CPU writes Part B. GPU reads Part B.
 * Frame 3: CPU writes Part C. GPU reads Part C.
 * Frame 4: CPU loops back to Part A. It waits (Fencing) to ensure GPU is done with Frame 1.
 */
class RingBufferSSBO {
    size_t m_segmentSize;  // Size of one segment (aligned to 256 bytes)
    size_t m_vertexStride; // Size of one vertex (8 bytes)
    PersistentSSBO m_SSBO; // The actual OpenGL Buffer wrapper
    int m_bufferCount = 3; // Triple buffering
    int m_head = 0;        // Current segment index (0, 1, or 2)
    
    // Fences: Markers inserted into the GPU command queue.
    // We check these to see if the GPU has passed a certain point.
    std::vector<GLsync> m_fences;
    GLuint m_vao; 

    // Helper: OpenGL requires buffers to be aligned to specific byte boundaries (usually 256).
    // This function rounds up 'originalSize' to the next multiple of 256.
    static size_t GetAlignedSize(size_t originalSize) {
        GLint alignment = 256;
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &alignment);
        if (alignment <= 0) alignment = 256;
        return (originalSize + alignment - 1) & ~(alignment - 1);
    }

public:
    RingBufferSSBO(size_t rawSegmentSize, size_t stride) 
        : m_vertexStride(stride), 
          m_segmentSize(GetAlignedSize(rawSegmentSize)),
          m_SSBO(m_segmentSize * 3) // Allocate 3x the size for ring buffering
    {
        m_fences.resize(m_bufferCount, 0); 
        glCreateVertexArrays(1, &m_vao); // Create Empty VAO
    }

    ~RingBufferSSBO() {
        glDeleteVertexArrays(1, &m_vao); 
        for (auto fence: m_fences) {
            if (fence) glDeleteSync(fence); 
        }
    }

    // Locks the next segment for writing.
    // If GPU is still using it, this function BLOCKS the CPU until GPU is done.
    void* LockNextSegment() {
        m_head = (m_head + 1) % m_bufferCount; // Move to next segment
        WaitForFence(m_fences[m_head]);        // Ensure it is safe to write
        
        // Return pointer to mapped memory for this segment
        return (uint8_t*)m_SSBO.m_mappedPtr + (m_head * m_segmentSize);
    }

    // Issues the draw command and places a fence.
    void UnlockAndDraw(int vertexCount) {
        // Bind only the active segment range
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, m_SSBO.GetID(), m_head * m_segmentSize, m_segmentSize);
        
        glBindVertexArray(m_vao); 
        glDrawArrays(GL_TRIANGLES, 0, vertexCount); 
        glBindVertexArray(0); 

        // Place a fence AFTER the draw command.
        // When we come back to this segment index later, we check this fence.
        if (m_fences[m_head]) glDeleteSync(m_fences[m_head]); 
        m_fences[m_head] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);         
    }

    // Checks if fence is signaled. If not, waits.
    void WaitForFence(GLsync fence) {
        if (!fence) return; 
        GLenum result; 
        // glClientWaitSync blocks CPU. 
        // Ideally, with 3 buffers, result is ALREADY_SIGNALED (no wait).
        do {
            result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000); 
        } while (result == GL_TIMEOUT_EXPIRED); 
    }
};