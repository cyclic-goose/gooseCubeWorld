#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iomanip> 

#include "chunk.h"
#include "camera.h"
#include "shader.h"
#include "ringBufferSSBO.h"
#include "packedVertex.h"
#include "linearAllocator.h"

const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

// Camera setup for viewing the chunk (0,0,0 to 32,32,32)
Camera camera(glm::vec3(40.0f, 40.0f, 40.0f)); 
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Debug State
bool useTestChunk = false;
bool triggerDebugPrint = false;
bool enableMeshing = true;
bool keyProcessed = false; 

// --- Test Chunk Generation ---
void FillTestChunk(Chunk& chunk) {
    std::memset(chunk.voxels, 0, sizeof(chunk.voxels));
    // Create a 3x3x3 block in the middle
    for (int x = 14; x <= 16; x++) {
        for (int y = 14; y <= 16; y++) {
            for (int z = 14; z <= 16; z++) {
                chunk.Set(x, y, z, 1);
            }
        }
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; 
    lastX = xpos; lastY = ypos;
    camera.ProcessMouseMovement(xoffset, yoffset);
}

void processInput(GLFWwindow *window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(UP, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.ProcessKeyboard(DOWN, deltaTime);

    // DEBUG KEYS
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS && !keyProcessed) {
        triggerDebugPrint = true; keyProcessed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS && !keyProcessed) {
        useTestChunk = !useTestChunk;
        std::cout << "[Debug] Switched to: " << (useTestChunk ? "TEST CHUNK" : "NOISE CHUNK") << std::endl;
        keyProcessed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS && !keyProcessed) {
        enableMeshing = !enableMeshing;
        std::cout << "[Debug] Meshing: " << (enableMeshing ? "ON" : "OFF") << std::endl;
        keyProcessed = true;
    }
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_RELEASE && 
        glfwGetKey(window, GLFW_KEY_T) == GLFW_RELEASE && 
        glfwGetKey(window, GLFW_KEY_M) == GLFW_RELEASE) {
        keyProcessed = false;
    }
}

void MeshChunk(const Chunk& chunk, LinearAllocator<PackedVertex>& allocator, bool debug = false);
void PrintVert(float x, float y, float z, int axis);

int main() {
    glfwInit();
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

    // --- RENDER SETTINGS ---
    // Standard Depth for now to reduce variables
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0f);
    glDisable(GL_CULL_FACE); // Show backfaces for debugging
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    {
        const int MAX_VERTS = 100000;
        RingBufferSSBO renderer(MAX_VERTS * sizeof(PackedVertex), sizeof(PackedVertex)); 
        Shader worldShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");
        LinearAllocator<PackedVertex> scratch(1024 * 1024); 

        Chunk noiseChunk; FillChunk(noiseChunk); 
        Chunk testChunk; FillTestChunk(testChunk);

        while (!glfwWindowShouldClose(window)) {
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;
            processInput(window);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // LOGIC FIX: Mesh if enabled OR if we requested a debug print
            if (enableMeshing || triggerDebugPrint) {
                scratch.Reset(); 
                Chunk& currentChunk = useTestChunk ? testChunk : noiseChunk;
                MeshChunk(currentChunk, scratch, triggerDebugPrint); 
            }

            // Upload
            void* gpuPtr = renderer.LockNextSegment();
            memcpy(gpuPtr, scratch.Data(), scratch.SizeBytes());

            // Draw
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
    glfwTerminate();
    return 0;
}

void PrintVert(float x, float y, float z, int axis) {
    std::cout << "  V(" << x << ", " << y << ", " << z << ") Face: " << axis << std::endl;
}

inline uint32_t ctz(uint64_t x) {
#if defined(_MSC_VER)
    return _tzcnt_u64(x);
#else
    return __builtin_ctzll(x);
#endif
}

// ... MeshChunk function is same as previous ...
void MeshChunk(const Chunk& chunk, LinearAllocator<PackedVertex>& allocator, bool debug) {
    if (debug) std::cout << "--- Meshing Chunk Debug Log ---" << std::endl;
    int quadCount = 0;

    for (int face = 0; face < 6; face++) {
        int axis = face / 2;
        int direction = (face % 2) == 0? 1 : -1;
        for (int slice = 1; slice <= CHUNK_SIZE; slice++) {
            uint32_t colMasks[32]; 
            for (int row = 0; row < 32; row++) {
                uint32_t mask = 0;
                for (int col = 0; col < 32; col++) {
                    int x, y, z;
                    if (axis == 0)      { x = slice; y = col + 1; z = row + 1; }
                    else if (axis == 1) { x = row + 1; y = slice; z = col + 1; }
                    else                { x = col + 1; y = row + 1; z = slice; }

                    uint8_t current = chunk.Get(x, y, z);
                    uint8_t neighbor = chunk.Get(x + (axis==0?direction:0), 
                                                 y + (axis==1?direction:0), 
                                                 z + (axis==2?direction:0));
                    
                    if (current != 0 && neighbor == 0) mask |= (1u << col);
                }
                colMasks[row] = mask;
            }

            for (int i = 0; i < 32; i++) {
                uint32_t mask = colMasks[i];
                while (mask != 0) {
                    int widthStart = ctz(mask); 
                    int widthEnd = widthStart;
                    while (widthEnd < 32 && (mask & (1u << widthEnd))) widthEnd++;
                    int width = widthEnd - widthStart;

                    uint32_t runMask;
                    if (width == 32) runMask = 0xFFFFFFFFu;
                    else runMask = ((1u << width) - 1u) << widthStart;

                    int height = 1;
                    for (int j = i + 1; j < 32; j++) {
                        uint32_t nextRow = colMasks[j];
                        if ((nextRow & runMask) == runMask) {
                            height++;
                            colMasks[j] &= ~runMask;
                        } else break;
                    }
                    mask &= ~runMask;

                    int u = widthStart;
                    int v = i;
                    int w = width;
                    int h = height;
                    quadCount++;

                    if (debug) std::cout << "Quad: Face=" << face << " Slice=" << slice << " U=" << u << " V=" << v << " W=" << w << " H=" << h << std::endl;

                    auto PushVert = [&](int du, int dv) {
                        float vx, vy, vz;
                        int r_row = v + dv; 
                        int r_col = u + du; 
                        
                        float px, py, pz;
                        if (axis == 0)      { px = slice; py = r_col + 1; pz = r_row + 1; }
                        else if (axis == 1) { px = r_row + 1; py = slice; pz = r_col + 1; }
                        else                { px = r_col + 1; py = r_row + 1; pz = slice; }

                        px -= 1; py -= 1; pz -= 1; 

                        if (direction == 1) {
                            if (axis == 0) px += 1.0f;
                            if (axis == 1) py += 1.0f;
                            if (axis == 2) pz += 1.0f;
                        }
                        
                        if (debug) PrintVert(px, py, pz, face);
                        allocator.Push(PackedVertex(px, py, pz, (float)face, 1.0f));
                    };

                    if (direction == 1) {
                        PushVert(0, 0); PushVert(w, 0); PushVert(w, h);
                        PushVert(0, 0); PushVert(w, h); PushVert(0, h);
                    } else {
                        PushVert(0, 0); PushVert(w, h); PushVert(w, 0);
                        PushVert(0, 0); PushVert(0, h); PushVert(w, h);
                    }
                }
            }
        }
    }
    if (debug) std::cout << "Total Quads: " << quadCount << " (" << quadCount * 6 << " Verts)" << std::endl;
}