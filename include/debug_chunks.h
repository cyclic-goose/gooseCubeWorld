#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>

// Simple self-contained renderer for wireframe debug boxes
class DebugRenderer {
private:
    struct BoxInstance {
        glm::vec3 minPos;
        glm::vec3 maxPos;
        glm::vec3 color;
    };

    GLuint m_vao = 0;
    GLuint m_vbo = 0; 
    GLuint m_program = 0;
    std::vector<BoxInstance> m_queue;

public:
    void Init() {
        if (m_vao != 0) return;

        // --- 1. COMPILE INTERNAL SHADER ---
        // Uses gl_VertexID to generate unit cube wireframe vertices on the fly
        const char* vsSource = R"(
            #version 460 core
            
            // Hardcoded unit cube corners (0..1)
            const vec3 corners[8] = vec3[8](
                vec3(0,0,0), vec3(1,0,0), vec3(0,1,0), vec3(1,1,0),
                vec3(0,0,1), vec3(1,0,1), vec3(0,1,1), vec3(1,1,1)
            );

            // Wireframe indices (12 lines * 2 vertices = 24 indices)
            const int indices[24] = int[24](
                0,1, 1,3, 3,2, 2,0, // Bottom face
                4,5, 5,7, 7,6, 6,4, // Top face
                0,4, 1,5, 2,6, 3,7  // Connecting pillars
            );

            layout(location = 0) in vec3 aMin;   // Instance Data
            layout(location = 1) in vec3 aMax;   // Instance Data
            layout(location = 2) in vec3 aColor; // Instance Data

            uniform mat4 u_ViewProj;
            out vec3 vColor;

            void main() {
                vColor = aColor;
                int idx = indices[gl_VertexID % 24];
                vec3 rawPos = corners[idx];
                
                // Mix min and max based on the 0..1 corner coords
                vec3 worldPos = mix(aMin, aMax, rawPos);
                gl_Position = u_ViewProj * vec4(worldPos, 1.0);
            }
        )";

        const char* fsSource = R"(
            #version 460 core
            in vec3 vColor;
            out vec4 FragColor;
            void main() {
                FragColor = vec4(vColor, 1.0);
            }
        )";

        auto createShader = [](GLenum type, const char* src) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            return s;
        };

        GLuint vs = createShader(GL_VERTEX_SHADER, vsSource);
        GLuint fs = createShader(GL_FRAGMENT_SHADER, fsSource);
        m_program = glCreateProgram();
        glAttachShader(m_program, vs);
        glAttachShader(m_program, fs);
        glLinkProgram(m_program);
        glDeleteShader(vs);
        glDeleteShader(fs);

        // --- 2. SETUP BUFFERS ---
        glCreateVertexArrays(1, &m_vao);
        glCreateBuffers(1, &m_vbo);

        // Bind VBO to Binding Index 0
        glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(BoxInstance));

        // Attrib 0: Min (vec3)
        glEnableVertexArrayAttrib(m_vao, 0);
        glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(BoxInstance, minPos));
        glVertexArrayBindingDivisor(m_vao, 0, 1); // Per-instance

        // Attrib 1: Max (vec3)
        glEnableVertexArrayAttrib(m_vao, 1);
        glVertexArrayAttribFormat(m_vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(BoxInstance, maxPos));
        glVertexArrayBindingDivisor(m_vao, 1, 1); // Per-instance

        // Attrib 2: Color (vec3)
        glEnableVertexArrayAttrib(m_vao, 2);
        glVertexArrayAttribFormat(m_vao, 2, 3, GL_FLOAT, GL_FALSE, offsetof(BoxInstance, color));
        glVertexArrayBindingDivisor(m_vao, 2, 1); // Per-instance
        
        // Link all attribs to binding 0
        glVertexArrayAttribBinding(m_vao, 0, 0);
        glVertexArrayAttribBinding(m_vao, 1, 0);
        glVertexArrayAttribBinding(m_vao, 2, 0);
    }

    void QueueBox(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color) {
        m_queue.push_back({min, max, color});
    }

    void Render(const glm::mat4& viewProj) {
        if (m_queue.empty()) return;

        glUseProgram(m_program);
        glUniformMatrix4fv(glGetUniformLocation(m_program, "u_ViewProj"), 1, GL_FALSE, glm::value_ptr(viewProj));

        // Upload instances
        glNamedBufferData(m_vbo, m_queue.size() * sizeof(BoxInstance), m_queue.data(), GL_STREAM_DRAW);

        glBindVertexArray(m_vao);
        // Draw 24 vertices (lines) per instance
        glDrawArraysInstanced(GL_LINES, 0, 24, (GLsizei)m_queue.size());
        
        glBindVertexArray(0);
        glUseProgram(0);
        
        m_queue.clear();
    }

    ~DebugRenderer() {
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_program) glDeleteProgram(m_program);
    }
};