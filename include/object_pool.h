#pragma once
#include <vector>
#include <mutex>
#include <iostream>
#include <algorithm>

// A Dynamic Object Pool that grows in "pages" (blocks) rather than one massive allocation.
// Reduces initial RAM usage significantly.

template <typename T>
class ObjectPool {
private:
    std::vector<T*> m_pool;           // Stack of available pointers
    std::vector<T*> m_memoryBlocks;   // Track all allocated pages to delete them later
    
    size_t m_growthSize = 1;          // How many items to allocate when running empty
    size_t m_totalAllocated = 0;      // Track total usage
    size_t m_maxCapacity = 0;         // Hard limit (0 = no limit)
    
    std::mutex m_mutex;
    uint8_t m_uniqueID;

public:
    // growthSize: How many items to allocate at once when the pool is empty.
    // initialSize: How many to pre-allocate immediately (can be 0).
    // maxCapacity: Hard limit on total items to prevent OOM (0 = unlimited).
    void Init(size_t growthSize, size_t initialSize = 0, size_t maxCapacity = 0, uint8_t uniqueID = 0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_growthSize = std::max((size_t)1, growthSize);
        m_maxCapacity = maxCapacity;

        if (initialSize > 0) {
            Expand(initialSize);
        }
        
        std::cout << "[System] ObjectPool Initialized. Growth Rate: " << m_growthSize 
                  << ", Pre-alloc: " << initialSize 
                  << ", Max: " << (m_maxCapacity == 0 ? "Unlimited" : std::to_string(m_maxCapacity)) << std::endl;
        m_uniqueID = uniqueID;
    }

    ~ObjectPool() {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Free all allocated blocks
        for (T* block : m_memoryBlocks) {
            delete[] block;
        }
        m_memoryBlocks.clear();
        m_pool.clear();
    }

    T* Acquire() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_pool.empty()) {
            Expand(m_growthSize);
        }

        // Check again in case expansion failed (hit max capacity)
        if (m_pool.empty()) {
            return nullptr; 
        }

        T* ptr = m_pool.back();
        m_pool.pop_back();
        return ptr;
    }

    void Release(T* ptr) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pool.push_back(ptr);
    }
    
    // Statistics
    size_t Available() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pool.size();
    }

    size_t TotalAllocated() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_totalAllocated;
    }

    // added to help with profiling
    float GetAllocatedMB() {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t totalBytes = m_totalAllocated * sizeof(T);
        return static_cast<float>(totalBytes) / (1024.0f * 1024.0f);
    }

    // Calculate RAM currently in use by active objects (in Megabytes)
    float GetUsedMB() {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t usedCount = m_totalAllocated - m_pool.size();
        size_t usedBytes = usedCount * sizeof(T);
        return static_cast<float>(usedBytes) / (1024.0f * 1024.0f);
    }

private:
    void Expand(size_t count) {
        // Check limits
        if (m_maxCapacity > 0 && m_totalAllocated + count > m_maxCapacity) {
            if (m_totalAllocated >= m_maxCapacity) {
                // Hard limit reached
                return;
            }
            // Allocate whatever remains up to the limit
            count = m_maxCapacity - m_totalAllocated;
        }

        try {
            // Allocate a new "Slab"
            T* newBlock = new T[count];
            m_memoryBlocks.push_back(newBlock);
            m_totalAllocated += count;

            // Reserve space in vector to prevent reallocations
            m_pool.reserve(m_pool.size() + count);

            // Add new items to the available stack
            for (size_t i = 0; i < count; ++i) {
                m_pool.push_back(&newBlock[i]);
            }
            
            // Optional: Debug logging
            std::cout << "[ObjectPool " << (int)m_uniqueID << "]" << " Expanded by " << count << ". Total: " << m_totalAllocated << std::endl;

        } catch (const std::bad_alloc& e) {
            std::cerr << "[ObjectPool " << (int)m_uniqueID << "]" << " CRITICAL: Memory allocation failed during expansion: " << e.what() << std::endl;
        }
    }
};