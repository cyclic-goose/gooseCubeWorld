#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "shader.h"

class BlockSelection {
public:
    static BlockSelection& Get() {
        static BlockSelection instance;
        return instance;
    }

    // --- State ---
    bool m_hasSelection = false;
    glm::ivec3 m_selectedBlock = glm::ivec3(0);
    glm::vec4 m_color = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f); 
    float m_lineWidth = 6.0f;

    // --- Resources ---
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    BlockSelection() {}

    /**
     * @brief Updates the visualizer state based on Raycast results.
     * Call this from PlayerController.
     */
    void Update(bool success, const glm::ivec3& blockPos) {
        m_hasSelection = success;
        m_selectedBlock = blockPos;
    }

    /**
     * @brief Renders the selection box.
     * Call this in your main render loop (after opaque chunks, before UI).
     */
    void Render(Shader& shader, const glm::mat4& view, const glm::mat4& projection) {
        if (!m_hasSelection) return;

        if (m_vao == 0) InitializeResources();

        shader.use();
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        shader.setVec4("u_Color", m_color);

        // 1. Calculate Model Matrix
        // Translate to block position
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(m_selectedBlock));
        
        // 2. Scale & Offset Correction
        // We scale slightly larger (1.002) to prevent Z-fighting with the block surface.
        // We offset by -0.001 to keep the expanded box centered.
        model = glm::translate(model, glm::vec3(-0.001f)); 
        model = glm::scale(model, glm::vec3(1.002f));      

        shader.setMat4("model", model);

        glLineWidth(m_lineWidth);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_LINES, 0, 24);
        glBindVertexArray(0);
        glLineWidth(1.0f); // Reset
    }

private:
    void InitializeResources() {
        if (m_vao != 0) return;

        // Unit Cube (0,0,0) to (1,1,1)
        // This matches standard voxel grid coordinates where a block at (0,0,0) occupies 0.0 to 1.0.
        float vertices[] = {
            // Bottom square
            0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 0.0f,
            // Top square
            0.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f,  0.0f, 1.0f, 1.0f,
            0.0f, 1.0f, 1.0f,  0.0f, 1.0f, 0.0f,
            // Vertical pillars
            0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 0.0f,  1.0f, 1.0f, 0.0f,
            1.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f,
            0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 1.0f
        };

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
};