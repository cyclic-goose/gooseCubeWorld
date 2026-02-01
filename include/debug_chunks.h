#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifdef IMGUI_VERSION
#include "imgui.h"
#endif

#include "chunkNode.h" 
#include "shader.h"

class ChunkDebugger {
public:
    static ChunkDebugger& Get() {
        static ChunkDebugger instance;
        return instance;
    }

    // --- Configuration ---
    bool m_enabled = false;
    bool m_lockSelection = false;
    float m_rayDistance = 10.0f;
    glm::vec4 m_highlightColor = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f); // Magenta

    // --- Internal State ---
    ChunkNode* m_selectedNode = nullptr;
    glm::vec3 m_targetWorldPos = glm::vec3(0.0f);
    
    // Rendering Resources
    GLuint m_debugVAO = 0;
    GLuint m_debugVBO = 0;

    ChunkDebugger() {
        // Don't initialize GL resources here in case Context isn't ready yet.
    }

    /**
     * @brief Updates the raycast.
     */
    template <typename WorldT>
    void Update(WorldT& world, const glm::vec3& camPos, const glm::vec3& camFront) {
        if (!m_enabled) return;
        
        if (m_lockSelection && m_selectedNode) return;

        m_targetWorldPos = camPos + (camFront * m_rayDistance);
        m_selectedNode = nullptr;

        for (int lod = 0; lod < world.GetConfig().settings.lodCount; lod++) {
            int scale = 1 << lod;
            int size = CHUNK_SIZE * scale;

            int cx = (int)floor(m_targetWorldPos.x / size);
            int cy = (int)floor(m_targetWorldPos.y / size);
            int cz = (int)floor(m_targetWorldPos.z / size);

            int64_t key = ChunkKey(cx, cy, cz, lod);

            auto it = world.m_activeChunkMap.find(key);
            if (it != world.m_activeChunkMap.end()) {
                m_selectedNode = it->second;
                break; 
            }
        }
    }

    void Shutdown() {
        if (m_debugVAO != 0) {
            glDeleteVertexArrays(1, &m_debugVAO);
            m_debugVAO = 0;
        }
        if (m_debugVBO != 0) {
            glDeleteBuffers(1, &m_debugVBO);
            m_debugVBO = 0;
        }
    }

    /**
     * @brief Draws the ImGui window with chunk details.
     */
    void DrawUI() {
#ifdef IMGUI_VERSION
        if (!m_enabled) return;

        if (ImGui::Begin("Chunk Debugger (F4)", &m_enabled)) {
            ImGui::Checkbox("Lock Selection", &m_lockSelection);
            ImGui::SliderFloat("Ray Distance", &m_rayDistance, 1.0f, 100.0f);
            ImGui::ColorEdit4("Color", (float*)&m_highlightColor);

            ImGui::Separator();
            ImGui::Text("Target World Pos: (%.1f, %.1f, %.1f)", m_targetWorldPos.x, m_targetWorldPos.y, m_targetWorldPos.z);

            if (m_selectedNode) {
                ChunkNode* n = m_selectedNode;
                
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0,1,0,1), "CHUNK FOUND");
                ImGui::Text("ID: %ld", n->uniqueID);
                ImGui::Text("LOD Level: %d (Scale: %d)", n->lodLevel, n->scaleFactor);
                ImGui::Text("Grid Coords: [%d, %d, %d]", n->gridX, n->gridY, n->gridZ);
                
                ImGui::Separator();
                ImGui::Text("State: "); ImGui::SameLine();
                switch(n->currentState.load()) {
                    case ChunkState::MISSING:    ImGui::TextColored(ImVec4(1,0,0,1), "MISSING"); break;
                    case ChunkState::GENERATING: ImGui::TextColored(ImVec4(1,1,0,1), "GENERATING"); break;
                    case ChunkState::GENERATED:  ImGui::TextColored(ImVec4(0,1,1,1), "GENERATED (Wait Mesh)"); break;
                    case ChunkState::MESHING:    ImGui::TextColored(ImVec4(1,1,0,1), "MESHING"); break;
                    case ChunkState::MESHED:     ImGui::TextColored(ImVec4(0,1,1,1), "MESHED (Wait Upload)"); break;
                    case ChunkState::ACTIVE:     ImGui::TextColored(ImVec4(0,1,0,1), "ACTIVE"); break;
                }




                ImGui::Separator();
                ImGui::Text("Data:");
                ImGui::Text("Is Uniform: %s", n->isUniform ? "YES" : "NO");
                

                 // --- MEMORY USAGE SECTION ---
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "RAM Memory Usage (Pools)");
                
                size_t metaBytes = sizeof(ChunkNode);
                size_t voxelBytes = (n->voxelData != nullptr) ? sizeof(Chunk) : 0;
                size_t totalBytes = metaBytes + voxelBytes;

                ImGui::Text("Node Metadata: %zu bytes", metaBytes);
                
                if (n->voxelData) {
                     ImGui::Text("Voxel Data:    %zu bytes (%.2f KB)", voxelBytes, voxelBytes / 1024.0f);
                     ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Total:         %.2f KB", totalBytes / 1024.0f);
                } else {
                     ImGui::Text("Voxel Data:    0 bytes (Uniform/Optimized)");
                     ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "Total:         %zu bytes", totalBytes);
                }
                
                ImGui::Separator();
                ImGui::Text("Geometry:");
                ImGui::Text("Note: ACTIVE = vertex data in VRAM");
                ImGui::Text("Opaque Verts: %zu", n->vertexCountOpaque);
                ImGui::Text("Transp Verts: %zu", n->vertexCountTransparent);
                ImGui::Text("GPU Offset Opaque: %lld", n->vramOffsetOpaque);
                
                // Bounding Box info
                ImGui::Text("AABB Min: %.1f, %.1f, %.1f", n->aabbMinWorld.x, n->aabbMinWorld.y, n->aabbMinWorld.z);
                ImGui::Text("AABB Max: %.1f, %.1f, %.1f", n->aabbMaxWorld.x, n->aabbMaxWorld.y, n->aabbMaxWorld.z);

            } else {
                ImGui::TextColored(ImVec4(1,0,0,1), "NO CHUNK AT TARGET");
                ImGui::TextDisabled("(Try increasing Ray Distance or pointing at ground)");
            }
        }
        ImGui::End();
#endif
    }

    void RenderGizmo(Shader& debugShader, const glm::mat4& viewProj) {
        if (!m_enabled || !m_selectedNode) return;
        
        // Lazy Init: Ensures OpenGL context is ready
        if (m_debugVAO == 0) InitializeResources();

        // 1. Disable Depth Test to draw ON TOP of blocks
        GLboolean depthEnabled;
        glGetBooleanv(GL_DEPTH_TEST, &depthEnabled);
        glDisable(GL_DEPTH_TEST);

        debugShader.use();
        debugShader.setMat4("u_ViewProjection", viewProj);
        debugShader.setVec4("u_Color", m_highlightColor);

        // Calculate Model Matrix for the AABB
        glm::vec3 size = m_selectedNode->aabbMaxWorld - m_selectedNode->aabbMinWorld;
        glm::vec3 center = m_selectedNode->aabbMinWorld + (size * 0.5f);
        
        glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
        model = glm::scale(model, size); 
        
        debugShader.setMat4("u_Model", model);

        glLineWidth(5.0f);
        glBindVertexArray(m_debugVAO);
        glDrawArrays(GL_LINES, 0, 24);
        glBindVertexArray(0);

        // 2. Restore Depth Test
        if (depthEnabled) glEnable(GL_DEPTH_TEST);
    }

private:
    void InitializeResources() {
        if (m_debugVAO != 0) return;

        float vertices[] = {
            -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,
            -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
            -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,
            -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
            -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,
             0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
            -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
            -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f,
             0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
            -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f,
             0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f
        };

        // Use standard GL 3.3+ methods for maximum safety
        glGenVertexArrays(1, &m_debugVAO);
        glGenBuffers(1, &m_debugVBO);

        glBindVertexArray(m_debugVAO);

        glBindBuffer(GL_ARRAY_BUFFER, m_debugVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        // Position attribute (Location 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind VBO
        glBindVertexArray(0);             // Unbind VAO
    }
};