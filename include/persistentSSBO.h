#pragma once
#include <glad/glad.h>
#include <vector>
#include <iostream>
#include <cstring>
#include <cassert>

// class encapsulates a shader storage buffer object (SSBO), this is an immutable storage that is persistently mapped throughout the objects lifetime and does not change its parameters

class PersistentSSBO {
public:
    // constructor
    PersistentSSBO(size_t size) : m_capacity(size), m_rendererID(0), m_mappedPtr(nullptr) {
        // create the buffer using DSA (direct state access)
        // note: glCreateBuffers (plural) is the DSA equivalent of glGenBuffers
        // creates the name and inits the object state
        glCreateBuffers(1, &m_rendererID);

        // define storage flags
        // these flags constitute the contract with the driver
        // we intend to write this to memory
        // GL_MAP_PERSISTENT_BIT: this mapping stays valid while the GPU reads it
        // GL_MAP_COHERENT_BIT: writes are visible to the map buffer
        // GL_DYNAMIC_STORAGE_BIT required to map this buffer
        GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT; // the mapped coherent bit flag is huge here. Without, the CPU would be unaware of changes to the GPU memory and we would have to call a flush each time we wanted to write and synchronize
        // allocate immutable storage
        // glNamedBufferStorage allocates the memory but does not init it. once
        // once called, the size and flags are frozen
        glNamedBufferStorage(m_rendererID, m_capacity, nullptr, flags);
        
        // map the buffer range
        // map the entire buffer. The returned pointer is valid until the buffer is deleted
        // the access flags must match the storage flags
        m_mappedPtr = glMapNamedBufferRange(m_rendererID, 0, m_capacity, flags);
        std::cout << "Persistent Buffer Created. Capacity: " << 4 * size <<  " bytes of VRAM" << std::endl;
        // check if any failures
        if (!m_mappedPtr) {
            std::cerr << "[CRITICAL ERROR: Failed to map persistent SSBO. VRAM not initialized]" << std::endl;
            // in production/release should gracefully exit here
        }
    }

    // destructor, cleans up and deletes memory 
    ~PersistentSSBO(){
        if (m_rendererID) {
            // unmapping is stricly required before deletion
            glUnmapNamedBuffer(m_rendererID);
            glDeleteBuffers(1, &m_rendererID);
            std::cout << "Buffer unmapped and destroyed successfully." << std::endl;
        }
    }

    // send data to the GPU by copying it to the mapped pointer
    // in a "ring buffer" scenario, "offset" determines where we write to 
    void UploadData(const void* data, size_t size, size_t offset = 0) {
        // sanity check to prevent buffer overruns
        assert(offset + size <= m_capacity);

        // direct memory copy
        // because we used GL_MAP_COHERENT_BIT, we do not need glMemoryBarrier here
        // for the transfer, we will need to resync fences for logic flow later
        std::memcpy((uint8_t*)m_mappedPtr + offset, data, size);
    }

    // binds the buffer to a specific binding point defined in the shader
    // corresponds to layout (std430, binding = x) buffer... 
    void Bind(GLuint bindingPoint) {
        // bind the buffer to the indexed target 
        // this connects the buffer to the binding = x in the shader
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, bindingPoint, m_rendererID);
    }

    // returns raw OpenGL handle
    GLuint GetID() const {return m_rendererID; }

    void* m_mappedPtr; // cpu side pointer to GPU memory
private:
    GLuint m_rendererID; // openGL objects name
    size_t m_capacity; // total size in BYTES


};