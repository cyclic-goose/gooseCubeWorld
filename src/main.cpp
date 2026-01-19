#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iomanip> 
#include <fstream> 

#include "chunk.h"
#include "camera.h"
#include "shader.h"
#include "ringBufferSSBO.h"
#include "packedVertex.h"
#include "linearAllocator.h"
#include "mesher.h"

// Screen Resolution
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

// Camera Setup: Start at (40,40,40) looking back at the chunk
Camera camera(glm::vec3(40.0f, 40.0f, 40.0f), glm::vec3(0.0f, 1.0f, 0.0f), -135.0f, -35.0f);

float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Debug Flags
bool useTestChunk = false;
bool triggerDebugPrint = false;
bool enableMeshing = true;
bool wireframeMode = false;
bool keyProcessed = false; 

// --- WORLD GENERATION LOGIC ---
// Fills the chunk with a sine wave pattern.
// Safe to modify the math here to change terrain.
void FillSineChunk(Chunk& chunk) {
    std::memset(chunk.voxels, 0, sizeof(chunk.voxels));
    for (int x = 0; x < CHUNK_SIZE_PADDED; x++) {
        for (int z = 0; z < CHUNK_SIZE_PADDED; z++) {
            // Keep padding clear
            if (x == 0 || x == CHUNK_SIZE_PADDED - 1 || 
                z == 0 || z == CHUNK_SIZE_PADDED - 1) continue;

            float fx = (float)x * 0.5f;
            float fz = (float)z * 0.5f;
            float val = sin(fx) * cos(fz); 
            int height = 15 + (int)(val * 10.0f);

            for (int y = 0; y < CHUNK_SIZE_PADDED; y++) {
                if (y > 0 && y < height) chunk.Set(x, y, z, 1);
            }
        }
    }
}

// Fills a simple 3x3x3 cube for debugging mesher issues.
void FillTestChunk(Chunk& chunk) {
    std::memset(chunk.voxels, 0, sizeof(chunk.voxels));
    for (int x = 14; x <= 16; x++) {
        for (int y = 14; y <= 16; y++) {
            for (int z = 14; z <= 16; z++) {
                chunk.Set(x, y, z, 1);
            }
        }
    }
}

// Standard GLFW Callbacks
void framebuffer_size_callback(GLFWwindow* window, int width, int height) { glViewport(0, 0, width, height); }
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    camera.ProcessMouseMovement(xpos - lastX, lastY - ypos);
    lastX = xpos; lastY = ypos;
}

// Input Handling
void processInput(GLFWwindow *window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    // Camera Movement
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(UP, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.ProcessKeyboard(DOWN, deltaTime);

    // Toggles
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS && !keyProcessed) { triggerDebugPrint = true; keyProcessed = true; }
    
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS && !keyProcessed) { 
        useTestChunk = !useTestChunk; 
        std::cout << "[Debug] Mode: " << (useTestChunk ? "TEST CUBE" : "SINE WAVE") << std::endl; 
        keyProcessed = true; 
    }
    
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS && !keyProcessed) { 
        enableMeshing = !enableMeshing; 
        std::cout << "[Debug] Meshing: " << (enableMeshing ? "ON" : "OFF") << std::endl; 
        keyProcessed = true; 
    }

    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS && !keyProcessed) {
        wireframeMode = !wireframeMode;
        if (wireframeMode) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            std::cout << "[Debug] Wireframe: ON" << std::endl;
        } else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            std::cout << "[Debug] Wireframe: OFF" << std::endl;
        }
        keyProcessed = true;
    }

    // Debounce Logic
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_RELEASE && 
        glfwGetKey(window, GLFW_KEY_T) == GLFW_RELEASE && 
        glfwGetKey(window, GLFW_KEY_M) == GLFW_RELEASE &&
        glfwGetKey(window, GLFW_KEY_TAB) == GLFW_RELEASE) {
        keyProcessed = false;
    }
}

int main() {
    glfwInit();
    // Use OpenGL 4.6 Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Goose Voxels", NULL, NULL);
    if (window == NULL) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    // --- SCOPED BLOCK START ---
    // Why this scope?
    // C++ destructors for 'RingBufferSSBO' need to call glDeleteBuffers.
    // If this scope doesn't exist, those destructors run AFTER glfwTerminate().
    // Calling glDeleteBuffers after Terminate causes a Segmentation Fault.
    {
        // 1. REVERSE-Z SETUP
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE); // Depth range 0..1
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GEQUAL); // Pass if newZ >= currentZ (1 is Near, 0 is Far)
        glClearDepth(0.0f);     // Clear to Far Plane (0.0)

        // 2. CULLING SETUP
        glFrontFace(GL_CCW);    // Counter-Clockwise triangles are front
        glEnable(GL_CULL_FACE); 
        glCullFace(GL_BACK);    // Hide back faces

        glClearColor(0.4f, 0.1f, 0.1f, 1.0f);

        // 3. TEXTURE CONFIG
        // Forces pixelated look (Nearest Neighbor) and Repeating textures
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        // 4. RESOURCE ALLOCATION
        // 100k verts max per draw call.
        const int MAX_VERTS = 100000;
        RingBufferSSBO renderer(MAX_VERTS * sizeof(PackedVertex), sizeof(PackedVertex)); 
        Shader worldShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");
        LinearAllocator<PackedVertex> scratch(1024 * 1024); // 1MB Temporary buffer

        // 5. CHUNK INIT
        Chunk noiseChunk; FillSineChunk(noiseChunk); 
        Chunk testChunk; FillTestChunk(testChunk);

        // --- RENDER LOOP ---
        while (!glfwWindowShouldClose(window)) {
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;
            processInput(window);

            // Clear buffers. Depth must be cleared to 0.0 for Reverse-Z.
            glClearDepth(0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // MESHING PHASE
            if (enableMeshing || triggerDebugPrint) {
                scratch.Reset(); // Wipe scratch memory (costs nothing)
                Chunk& currentChunk = useTestChunk ? testChunk : noiseChunk;
                MeshChunk(currentChunk, scratch, triggerDebugPrint); 
            }

            // UPLOAD PHASE
            // Lock memory segment on GPU and copy data
            void* gpuPtr = renderer.LockNextSegment();
            memcpy(gpuPtr, scratch.Data(), scratch.SizeBytes());

            // DRAW PHASE
            worldShader.use();
            glm::mat4 projection = camera.GetProjectionMatrix(SCR_WIDTH / (float)SCR_HEIGHT, 0.1f);
            glm::mat4 view = camera.GetViewMatrix();
            
            glUniformMatrix4fv(glGetUniformLocation(worldShader.ID, "u_ViewProjection"), 1, GL_FALSE, glm::value_ptr(projection * view));
            glUniform3f(glGetUniformLocation(worldShader.ID, "u_ChunkOffset"), 0.0f, 0.0f, 0.0f);
            
            renderer.UnlockAndDraw(scratch.Count()); 

            if (triggerDebugPrint) {
                triggerDebugPrint = false; 
                std::cout << "--- End of Debug Frame ---" << std::endl;
            }

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    } 
    // --- SCOPED BLOCK END ---

    glfwTerminate();
    return 0;
}