#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

#include "chunk.h"
#include "camera.h"
#include "shader.h"
#include "ringBufferSSBO.h"
#include "packedVertex.h"
#include "linearAllocator.h"

// Screen settings
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

// Camera
Camera camera(glm::vec3(16.0f, 30.0f, 40.0f)); // Start nicely positioned to see the chunk
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ------------------------------------------------------------------------
// Function: framebuffer_size_callback
// ------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// ------------------------------------------------------------------------
// Function: mouse_callback
// ------------------------------------------------------------------------
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; 

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

// ------------------------------------------------------------------------
// Function: processInput
// ------------------------------------------------------------------------
void processInput(GLFWwindow *window) {
    static bool tabPressed = false;
    static bool wireframeMode = false;


    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.ProcessKeyboard(RIGHT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        camera.ProcessKeyboard(UP, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        camera.ProcessKeyboard(DOWN, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_RELEASE)
    {
        if (!tabPressed) {
            wireframeMode = !wireframeMode;
            if (wireframeMode) {
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            }
            else{
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }
            tabPressed = true;
        } 
    } else {
        tabPressed = false;
    }
}

// prototype 
void MeshChunk(const Chunk& chunk, LinearAllocator<PackedVertex>& allocator);


int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 32); 

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Goose Voxels", NULL, NULL);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glFrontFace(GL_CW); 
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE); 
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL); // Reverse-Z
    glClearDepth(0.0f);

    {
        // **************************** RING BUFFER SETUP ************************** //
        const int MAX_VERTS = 100000;
        
        // CRITICAL FIX: The RingBuffer expects size in bytes, not vertex count.
        RingBufferSSBO renderer(MAX_VERTS * sizeof(PackedVertex), sizeof(PackedVertex)); 
        
        Shader worldRingBufferShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");

        LinearAllocator<PackedVertex> scratch(1024 * 1024); // 1 million verts space

        Chunk myChunk;
        FillChunk(myChunk); // Generate noise
        // **************************** RING BUFFER SETUP ************************** //

        while (!glfwWindowShouldClose(window)) {

            // ********* Loop Maintainence ********** //
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            processInput(window);

            glClearColor(0.4f, 0.3f, 0.5f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            // ********* End Loop Maintainence ********** //
        
            // 1. Reset Linear Allocator
            scratch.Reset(); 

            // 2. Mesh Chunk directly into scratch memory
            MeshChunk(myChunk, scratch); 

            // 3. Upload to GPU via Ring Buffer
            void* gpuPtr = renderer.LockNextSegment();
            memcpy(gpuPtr, scratch.Data(), scratch.SizeBytes());
            
            // **************** World and camera Update Matrix *************** //
            glm::mat4 projection = camera.GetProjectionMatrix(SCR_WIDTH / (float)SCR_HEIGHT, 0.1f);
            glm::mat4 view = camera.GetViewMatrix();
            
            // ******** Set Shader ********* //
            worldRingBufferShader.use();
            
            GLint locVP = glGetUniformLocation(worldRingBufferShader.ID, "u_ViewProjection");
            glUniformMatrix4fv(locVP, 1, GL_FALSE, glm::value_ptr(projection * view));

            GLint locChunk = glGetUniformLocation(worldRingBufferShader.ID, "u_ChunkOffset");
            glUniform3f(locChunk, 0.0f, 0.0f, 0.0f); 
            // ******** Set Shader ********* //
            
            // 4. Draw
            renderer.UnlockAndDraw(scratch.Count()); 
            
            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    }

    glfwTerminate();
    return 0;
}

// Using bit manipulation to count trailing zeros
inline uint32_t ctz(uint64_t x) {
#if defined(_MSC_VER)
    return _tzcnt_u64(x);
#else
    return __builtin_ctzll(x);
#endif
}

void MeshChunk(const Chunk& chunk, LinearAllocator<PackedVertex>& allocator) {
    // Axis definitions for the 6 faces
    // 0: +X (Right), 1: -X (Left), 2: +Y (Top), 3: -Y (Bottom), 4: +Z (Front), 5: -Z (Back)
    for (int face = 0; face < 6; face++) {
        int axis = face / 2;
        int direction = (face % 2) == 0? 1 : -1;
        
        // We sweep along the main axis of the face
        for (int slice = 1; slice <= CHUNK_SIZE; slice++) {
            
            // CRITICAL FIX: Array size was [1], causing stack smashing when writing to index > 0
            uint32_t colMasks[32]; 

            for (int row = 0; row < 32; row++) {
                uint32_t mask = 0;
                for (int col = 0; col < 32; col++) {
                    int x, y, z;
                    
                    // Coordinate mapping
                    if (axis == 0)      { x = slice; y = col + 1; z = row + 1; } // X-sweep (Y=Col, Z=Row)
                    else if (axis == 1) { x = row + 1; y = slice; z = col + 1; } // Y-sweep (X=Row, Z=Col)
                    else                { x = col + 1; y = row + 1; z = slice; } // Z-sweep (X=Col, Y=Row)

                    uint8_t current = chunk.Get(x, y, z);
                    uint8_t neighbor = chunk.Get(x + (axis==0?direction:0), 
                                                 y + (axis==1?direction:0), 
                                                 z + (axis==2?direction:0));
                    
                    // We draw if current is solid and neighbor is air (or other way around depending on winding)
                    // Simplified: Draw if current exists and neighbor doesn't (assuming we are inside block looking out? or normal culling)
                    // For standard faces: Draw if current is solid and neighbor is air (and direction is +)
                    // OR current is air and neighbor is solid (and direction is -) -> but loop iterates all solid blocks usually
                    
                    if (current != 0 && neighbor == 0) {
                        mask |= (1u << col);
                    }
                }
                colMasks[row] = mask;
            }

            // Step 3: Greedy Merging
            for (int i = 0; i < 32; i++) {
                uint32_t mask = colMasks[i];
                
                while (mask != 0) {
                    int widthStart = ctz(mask); 
                    
                    int widthEnd = widthStart;
                    while (widthEnd < 32 && (mask & (1u << widthEnd))) widthEnd++;
                    int width = widthEnd - widthStart;

                    int height = 1;
                    for (int j = i + 1; j < 32; j++) {
                        uint32_t nextRow = colMasks[j];
                        uint32_t runMask = ((1u << width) - 1u) << widthStart;
                        
                        if ((nextRow & runMask) == runMask) {
                            height++;
                            colMasks[j] &= ~runMask;
                        } else {
                            break;
                        }
                    }

                    uint32_t runMask = ((1u << width) - 1u) << widthStart;
                    mask &= ~runMask;

                    // Emit Quad (Two Triangles)
                    // i = row index (V axis)
                    // widthStart = col index (U axis)
                    
                    // Construct the 4 corners relative to the plane
                    // u1 = widthStart, v1 = i
                    // u2 = widthStart + width, v2 = i + height
                    
                    int u = widthStart;
                    int v = i;
                    int w = width;
                    int h = height;

                    // Determine X,Y,Z for the quad corners based on Axis
                    // We need 6 vertices (2 triangles)
                    // Triangle 1: (0,0), (w,0), (0,h)
                    // Triangle 2: (w,0), (w,h), (0,h)
                    // Note: Winding order matters!
                    
                    // Helper lambda to push a vertex
                    auto PushVert = [&](int du, int dv) {
                        float vx, vy, vz;
                        // Map back to world coords
                        // Offset by +1 because chunk padding puts (0,0,0) at (1,1,1) in array
                        // but visual world mesh usually starts at 0.
                        // However, slice is 1-based index into Padded array.
                        
                        // We use the same mapping logic as the mask generation
                        // slice is the constant axis
                        // row (v) is i
                        // col (u) is widthStart
                        
                        int r_row = v + dv; 
                        int r_col = u + du; 
                        // Note: slice is the boundary. 
                        // If direction is positive (looking towards +X), the face is on the +X side of the voxel (slice).
                        // If direction is negative (looking towards -X), the face is on the -X side. 
                        // Visual pos usually: if direction is positive, pos = slice. If negative, pos = slice - 1?
                        // Let's stick to simple blocky integer coords.
                        
                        float px, py, pz;
                        if (axis == 0)      { px = slice; py = r_col + 1; pz = r_row + 1; }
                        else if (axis == 1) { px = r_row + 1; py = slice; pz = r_col + 1; }
                        else                { px = r_col + 1; py = r_row + 1; pz = slice; }

                        // Correction: loop used +1 offset for padding access.
                        // Visual mesh should be 0-32.
                        px -= 1; py -= 1; pz -= 1; 

                        // Handle face offset for positive/negative directions
                        // If direction is positive (e.g. Right face), x should be at the far end of the block?
                        // Actually, slice iterates voxels. If we are at slice S, and neighbor S+1 is empty,
                        // the face is at coordinate S+1 (between S and S+1).
                        if (direction == 1) {
                            if (axis == 0) px += 1.0f;
                            if (axis == 1) py += 1.0f;
                            if (axis == 2) pz += 1.0f;
                        }
                        
                        // Push with Texture ID 1 for now
                        allocator.Push(PackedVertex(px, py, pz, (float)face, 1.0f));
                    };

                    // Winding order flip based on direction?
                    // Standard GL is CCW.
                    if (direction == 1) {
                        // Face pointing "Forward"
                        PushVert(0, 0); PushVert(w, 0); PushVert(w, h);
                        PushVert(0, 0); PushVert(w, h); PushVert(0, h);
                    } else {
                        // Face pointing "Backward"
                        PushVert(0, 0); PushVert(w, h); PushVert(w, 0);
                        PushVert(0, 0); PushVert(0, h); PushVert(w, h);
                    }
                }
            }
        }
    }
}