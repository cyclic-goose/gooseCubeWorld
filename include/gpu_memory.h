#pragma once
#include <glad/glad.h>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>

class GpuMemoryManager {
    GLuint m_bufferId;
    size_t m_capacity;
    size_t m_used = 0;
    std::map<size_t, size_t> m_freeBlocks;

    // Helper to align a value to the specific alignment requirement
    static size_t AlignTo(size_t value, size_t alignment) {
        if (alignment == 0) return value;
        size_t remainder = value % alignment;
        if (remainder == 0) return value;
        return value + (alignment - remainder);
    }

public:
    GpuMemoryManager(size_t sizeBytes) : m_capacity(sizeBytes) {
        glCreateBuffers(1, &m_bufferId);
        // Use DYNAMIC_STORAGE_BIT for frequent updates
        glNamedBufferStorage(m_bufferId, m_capacity, nullptr, GL_DYNAMIC_STORAGE_BIT);
        
        // Initial block covering entire buffer
        m_freeBlocks[0] = m_capacity;
        
        std::cout << "[GpuMem] Allocated " << (sizeBytes / 1024 / 1024) << "MB Static VRAM" << std::endl;
    }

    ~GpuMemoryManager() {
        glDeleteBuffers(1, &m_bufferId);
    }

    // UPDATED: Now takes an alignment parameter.
    // critical for MDI: offset must be divisible by sizeof(Vertex)
    long long Allocate(size_t rawSize, size_t alignment = 256) {
        // We align the SIZE to 4 bytes to keep things sane, but we align the OFFSET to 'alignment'
        size_t size = AlignTo(rawSize, 4); 

        for (auto it = m_freeBlocks.begin(); it != m_freeBlocks.end(); ++it) {
            size_t blockOffset = it->first;
            size_t blockSize = it->second;

            // Check if this block's start is aligned, or if we can pad it to be aligned
            size_t alignedOffset = AlignTo(blockOffset, alignment);
            size_t padding = alignedOffset - blockOffset;

            if (blockSize >= size + padding) {
                // We found a fit!
                
                // 1. Remove the original free block
                m_freeBlocks.erase(it);

                // 2. If we needed padding, the padding becomes a new (small) free block
                if (padding > 0) {
                    m_freeBlocks[blockOffset] = padding;
                }

                // 3. If there is leftover space AFTER our allocation, add it back as a free block
                size_t allocatedEnd = alignedOffset + size;
                size_t blockEnd = blockOffset + blockSize;
                
                if (blockEnd > allocatedEnd) {
                    m_freeBlocks[allocatedEnd] = blockEnd - allocatedEnd;
                }

                m_used += size;
                return (long long)alignedOffset;
            }
        }
        return -1; // VRAM Full
    }

    void Free(size_t offset, size_t rawSize) {
        // Simple free, ideally we should coalesce neighbors here
        m_used -= rawSize; // Approximate tracking
        m_freeBlocks[offset] = rawSize; 
        
        // TODO: Merge with adjacent free blocks to reduce fragmentation
    }

    void Upload(size_t offset, const void* data, size_t rawSize) {
        glNamedBufferSubData(m_bufferId, offset, rawSize, data);
    }

    GLuint GetID() const { return m_bufferId; }
};