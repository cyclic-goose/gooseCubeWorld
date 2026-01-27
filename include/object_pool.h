#pragma once
#include <vector>
#include <mutex>
#include <iostream>

// this is a generic object handler (that will mostly be used for chunks and chunk nodes)
// it handles allocating and freeing ram for chunks and chunk nodes

template <typename T>
class ObjectPool {
private:
    std::vector<T*> m_pool;
    T* m_memoryBlock = nullptr; // Raw array instead of vector
    size_t m_size = 0;
    std::mutex m_mutex;

public:
    // Pre-allocate 'size' objects
    void Init(size_t size) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // Allocate raw array. 
        // T must have a default constructor, but doesn't need copy/move.
        m_memoryBlock = new T[size];
        m_size = size;

        m_pool.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            m_pool.push_back(&m_memoryBlock[i]);
        }
        std::cout << "[System] ObjectPool Initialized with " << size << " items." << std::endl;
    }

    ~ObjectPool() {
        // We own the memory block
        if (m_memoryBlock) {
            delete[] m_memoryBlock;
        }
    }

    T* Acquire() {
        std::lock_guard<std::mutex> lock(m_mutex);
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
    
    size_t Available() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pool.size();
    }
};