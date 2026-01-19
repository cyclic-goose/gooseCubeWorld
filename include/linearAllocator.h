#pragma once
// using std::vector::push_back is slow because it checks capacity and reallocates heap memory constantly. We need a "fire and forget" allocator that is reset every frame
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>

template<typename T>
class LinearAllocator {
public:
    LinearAllocator(size_t maxElements) {
        m_start = (T*)malloc(maxElements * sizeof(T)); 
        m_Current = m_Start;
        m_End = m_Start + maxElements;
    }

    // destructor 
    ~LinearAllocator() {
        free(m_Start);
    }
    // reset the pointer to the start, does NOT free memory
    // call this before meshing a new batch of chunks
    void Reset() {
        m_Current = m_Start;
    }

    // allocate 'count' elements and return pointer to the start of the block
    T* Allocate(size_t count) {
        T* ptr = m_Current;
        if (m_Current + count > m_End) {
            return nullptr;
        }
        m_Current += count;
        return ptr;
    }

    // push a single element
    void Push(const T& element) {
        if (m_Current < m_End) {
            *m_Current = element;
            m_Current++;
        }
    }

    size_t SizeBytes() const {
        return (uint8_t*)m_Current - (uint8_t*)m_Start;
    }

    size_t Count() const {
        return m_Current - m_Start;
    }
    
    T* Data() const {
        return m_Start;
    }

private:
    T* m_Start;
    T* m_Current;
    T* m_End;
};
