#pragma once
#include <glad/glad.h>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>
#include <limits>
#include <cstring> // Required for memcpy

class GpuMemoryManager {
    GLuint m_bufferId;
    void* m_mappedPtr = nullptr; // Persistent CPU pointer to GPU memory
    size_t m_capacity;
    size_t m_used = 0;
    std::map<size_t, size_t> m_freeBlocks;

    static size_t AlignTo(size_t value, size_t alignment) {
        if (alignment == 0) return value;
        size_t remainder = value % alignment;
        if (remainder == 0) return value;
        return value + (alignment - remainder);
    }

public:
    GpuMemoryManager(size_t sizeBytes) : m_capacity(sizeBytes) {
        glCreateBuffers(1, &m_bufferId);
        
        // PERSISTENT MAPPING SETUP
        // We ask for a buffer that we can write to (WRITE) while the GPU is using it (PERSISTENT).
        // COHERENT means the GPU sees our writes automatically (no manual flush needed).
        GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        
        glNamedBufferStorage(m_bufferId, m_capacity, nullptr, flags);
        m_mappedPtr = glMapNamedBufferRange(m_bufferId, 0, m_capacity, flags);
        
        m_freeBlocks[0] = m_capacity;
        std::cout << "[GpuMem] Allocated " << (sizeBytes / 1024 / 1024) << "MB Persistent VRAM" << std::endl;
    }

    ~GpuMemoryManager() {
        if (m_mappedPtr) {
            glUnmapNamedBuffer(m_bufferId);
        }
        glDeleteBuffers(1, &m_bufferId);
    }

    // Best Fit Allocation Strategy
    long long Allocate(size_t rawSize, size_t alignment = 256) {
        size_t size = AlignTo(rawSize, 4); 

        auto bestIt = m_freeBlocks.end();
        size_t minWaste = std::numeric_limits<size_t>::max();
        size_t bestAlignedOffset = 0;
        size_t bestPadding = 0;

        for (auto it = m_freeBlocks.begin(); it != m_freeBlocks.end(); ++it) {
            size_t blockOffset = it->first;
            size_t blockSize = it->second;

            size_t alignedOffset = AlignTo(blockOffset, alignment);
            size_t padding = alignedOffset - blockOffset;

            if (blockSize >= size + padding) {
                size_t waste = blockSize - (size + padding);
                
                if (waste == 0) {
                    bestIt = it;
                    bestAlignedOffset = alignedOffset;
                    bestPadding = padding;
                    break; 
                }

                if (waste < minWaste) {
                    minWaste = waste;
                    bestIt = it;
                    bestAlignedOffset = alignedOffset;
                    bestPadding = padding;
                }
            }
        }

        if (bestIt != m_freeBlocks.end()) {
            size_t blockOffset = bestIt->first;
            size_t blockSize = bestIt->second;

            m_freeBlocks.erase(bestIt);

            if (bestPadding > 0) {
                m_freeBlocks[blockOffset] = bestPadding;
            }

            size_t allocatedEnd = bestAlignedOffset + size;
            size_t blockEnd = blockOffset + blockSize;
            
            if (blockEnd > allocatedEnd) {
                m_freeBlocks[allocatedEnd] = blockEnd - allocatedEnd;
            }

            m_used += size;
            return (long long)bestAlignedOffset;
        }

        return -1; // VRAM Full
    }

    void Free(size_t offset, size_t rawSize) {
        size_t size = AlignTo(rawSize, 4); 
        m_used -= size; 
        
        auto ret = m_freeBlocks.insert({offset, size});
        auto it = ret.first;

        // Coalesce Right
        auto nextIt = std::next(it);
        if (nextIt != m_freeBlocks.end()) {
            if (offset + size == nextIt->first) {
                it->second += nextIt->second;
                m_freeBlocks.erase(nextIt);
            }
        }

        // Coalesce Left
        if (it != m_freeBlocks.begin()) {
            auto prevIt = std::prev(it);
            if (prevIt->first + prevIt->second == it->first) {
                prevIt->second += it->second;
                m_freeBlocks.erase(it);
            }
        }
    }

    // NON-BLOCKING UPLOAD
    void Upload(size_t offset, const void* data, size_t rawSize) {
        if (m_mappedPtr) {
            // Direct memory copy. No driver interaction. No stall.
            std::memcpy((uint8_t*)m_mappedPtr + offset, data, rawSize);
        }
    }

    GLuint GetID() const { return m_bufferId; }
    size_t GetUsedMemory() const { return m_used; }
    size_t GetTotalMemory() const { return m_capacity; }
    size_t GetFreeBlockCount() const { return m_freeBlocks.size(); }
};