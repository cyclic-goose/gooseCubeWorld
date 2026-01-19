#pragma once
// the point of this class is to try and treat out persistent shader storage buffer object (SSBO) object as a "toroidal clipmap" or a ring buffer
// We also need to do so while preventing Write-After-Read (WAR) hazards, resulting in tearing and memory corruption
// Frame N: CPU writes to segment A, GPU reads segment C (From frame N-2)
// Frame N+1: CPU writes segment B, GPU reads segment A
#include "persistentSSBO.h"
#include <vector>
#include <iostream>

class RingBufferSSBO {
    PersistentSSBO m_SSBO;
    size_t m_segmentSize;
    int m_bufferCount = 3;
    int m_head = 0; // current write segment
    // We use a glFenceSync to place a marker in the command stream after a draw call, before writing to a segment we verify that the fence associated with that segments last use has been passed
    std::vector<GLsync> m_fences;
    GLuint m_vao; // dummy VAO because opengl requires one be bound to draw 

    // static helper that runs before members are init
    // this will help ensure alignment to 256 for segments inside the ring buffer
    static size_t GetAlignedSize(size_t originalSize) {
        GLint alignment;
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &alignment);

        // Bitwise math to round up to the next multiple of 'alignment'
        // Example: If alignment is 256 and size is 300, this returns 512
        return (originalSize + alignment -1) & ~(alignment-1); 
    }

public:
    RingBufferSSBO(size_t rawSegmentSize) : m_segmentSize(GetAlignedSize(rawSegmentSize)), m_SSBO(m_segmentSize * 3) {
        m_fences.resize(m_bufferCount, 0); 
        glGenVertexArrays(1, &m_vao); 

        std::cout << "RingBuffer: Requested " << rawSegmentSize  << " bytes, aligned to " << m_segmentSize << " bytes." << std::endl;
    }
    // destructor to clean things up 
    ~RingBufferSSBO() {
        glDeleteVertexArrays(1, &m_vao); 
        for (auto fence: m_fences)
        {
            if (fence) 
                glDeleteSync(fence); 
        }
    }

    // new frame upload, writing data means writing to the pointer returned by this very function. Assume it will do the work of checking for free segments
    void* LockNextSegment() {
        //move head
        m_head = (m_head + 1) % m_bufferCount;
        
        // wait for this segment to be free
        WaitForFence(m_fences[m_head]);

        // return pointer to start of this segment
        return (uint8_t*)m_SSBO.m_mappedPtr + (m_head * m_segmentSize);
    }

    // void UnlockAndDraw(int vertexCount) { 
    //     // bind buffer (base + offset) or just uniform offset in shader
    //     m_SSBO.Bind(0);

    //     // draw command
    //     glDrawArrays(GL_TRIANGLES, 0, vertexCount);

    //     // place fence
    //     // delete old fence if it still exists
    //     if (m_fences[m_head]) glDeleteSync(m_fences[m_head]);

    //     // now insert new fence
    //     m_fences[m_head] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); 
    // }

    void UnlockAndDraw(int vertexCount) {
        // bind only current segment range so that the shader things the segment starts at index 0
        // segmentSize must be aligned to GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT (usually 256 bytes)
        glBindVertexArray(m_vao); 
        glDrawArrays(GL_TRIANGLES, 0, vertexCount); 
        glBindVertexArray(0); 

        // standard fencing locig 
        if (m_fences[m_head]) glDeleteSync(m_fences[m_head]); 
        m_fences[m_head] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);         
    }

    void WaitForFence(GLsync fence) {
        if (!fence)  // no fence means the segment is free
            return; 
        GLenum result; 
        do {
            result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000); // BLOCKS THE CPU UNTIL GPU PASSES THE FENCE
        } while (result == GL_TIMEOUT_EXPIRED); 
    }


};