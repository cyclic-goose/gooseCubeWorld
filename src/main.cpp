#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

#include "chunk.h"
#include "camera.h"
#include "shader.h"
//#include "persistentSSBO.h"
#include "ringBufferSSBO.h"
#include "packedVertex.h"
#include "linearAllocator.h"

// Screen settings
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

// Camera
Camera camera(glm::vec3(0.0f, 0.0f, -10.0f)); // Start above the chunk
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ------------------------------------------------------------------------
// Function: framebuffer_size_callback
// Inputs:   window (GLFWwindow*), width (int), height (int)
// Outputs:  void - Resizes the OpenGL viewport when window size changes
// ------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// ------------------------------------------------------------------------
// Function: mouse_callback
// Inputs:   window (GLFWwindow*), xpos (double), ypos (double)
// Outputs:  void - Calculates mouse offsets and updates camera orientation
// ------------------------------------------------------------------------
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // Reversed (y-coordinates go bottom to top)

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

// ------------------------------------------------------------------------
// Function: processInput
// Inputs:   window (GLFWwindow*) - The active window
// Outputs:  void - Checks for key presses and moves camera
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
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); // Changed to 3.3 for wider compatibility, 4.6 is fine too
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 32); // Suggestion, not a guarantee of Float

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

    // DEBUG STEP 1: Disable Culling to ensure we aren't accidentally hiding our own triangles
    //glEnable(GL_CULL_FACE); 


    // ************* OLD TEST CHUNK BUFFER ************** //
    // VBO/VAO Init
    // unsigned int VAO, VBO;
    // glGenVertexArrays(1, &VAO);
    // glGenBuffers(1, &VBO);

    // // Create Mesh Data
    // Chunk myChunk;
    // myChunk.Generate(0, 0, 0); // Generates a sphere at origin
    
    // std::vector<Vertex> vertices;
    // myChunk.Mesh(vertices);

    // Shader basicShader("./resources/basicChunkVert.glsl", "./resources/basicChunkFrag.glsl");

    // // DEBUG STEP 2: Print vertex count to console
    // std::cout << "Generated Vertices: " << vertices.size() << std::endl;
    // if (vertices.empty()) {
    //     std::cout << "WARNING: Mesh is empty! Check Chunk::Generate()" << std::endl;
    // }

    // // Upload
    // glBindVertexArray(VAO);
    // glBindBuffer(GL_ARRAY_BUFFER, VBO);
    // glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    // // Attrib 0: Pos
    // glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    // glEnableVertexAttribArray(0);
    
    // // Attrib 1: Normal
    // glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    // glEnableVertexAttribArray(1);


    // ************* OLD TEST CHUNK BUFFER ************** //


    //glEnable(GL_CULL_FACE);
    //glCullFace(GL_FRONT);
    glFrontFace(GL_CW); 
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE); 
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL); // Reverse-Z
    glClearDepth(0.0f);


{
        // ***************** ATTEMPT AT USING SSBO and NEW RENDER SYSTEM ************* // 
        // // CREATE SSBO
        // PersistentSSBO SSBO(20); // input to constructor is number of vertices I think
        
        // // CREATE DATA
        // std::vector<PackedVertex> vertices;
        // vertices.emplace_back(0, 0, 0, 4, 1); // add a vertex at (0, 0, 0) facing up with texture 1
        // vertices.emplace_back(0, 2, 0, 4, 1);
        // vertices.emplace_back(4, 2, 0, 4, 1);
        // //std::cout << vertices.size() * sizeof(PackedVertex) << " bytes used" << std::endl;
        
        // SSBO.Bind(0); // tell gl that we are fixing the layout to binding layout 0 (as reflected in our shader program)
        
        // // UPLOAD DATA TO SSBO
        // SSBO.UploadData(vertices.data(), vertices.size() * sizeof(PackedVertex), 0);
        
        // // need empty VAO even though data is pulled straight from GPU
        // // its because gl is dumb and still expects a VAO for draw calls
        // GLuint emptyVAO;
        // glCreateVertexArrays(1, &emptyVAO);

        // Shader SSBOShaderTester("./resources/basicPackedVertexUnwrap.glsl", "./resources/basicSSBOFragTester.glsl");

        
        // // NOTE: If your shader doesn't have layout(location=2), you can keep this enabled 
        // // but the shader just won't use it. However, 'Vertex' struct MUST have 'texCoord' member
        // // for the stride (sizeof(Vertex)) to be correct.
        // // glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
        // // glEnableVertexAttribArray(2);
        
        
        // // 1. Bind the SSBO (Ensure you bind to the same target/ID used in UploadData)
        // glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO.GetID());
        
        // // 2. Query the Size
        // GLint allocatedBytes = 0;
        // glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &allocatedBytes);
        // ***************** ATTEMPT AT USING SSBO and NEW RENDER SYSTEM ************* // 

        // 3. Print Results
        //std::cout << "VRAM Allocated: " << allocatedBytes << " bytes" << std::endl;
        //std::cout << "VRAM Used:      " << (vertices.size() * sizeof(PackedVertex)) << " bytes" << std::endl;
        // Render Loop

        // A simple test manual Scaling Matrix (Scale = 1/40)
        // This makes coordinate "40" appear at edge of screen "1.0"
        float s = 1.0f / 40.0f; 
        float scalingMatrix[16] = {
            s,  0,  0,  0,
            0,  s,  0,  0,
            0,  0,  s,  0,
            -0.5, -0.5, 0,  1  // Simple translation to center (optional)
        };

        // **************************** RING BUFFER SETUP ************************** //
        const int MAX_VERTS = 100000;
        RingBufferSSBO renderer(MAX_VERTS, sizeof(PackedVertex)); // packed vertex is the stride
        Shader worldRingBufferShader("./resources/basicPackedVertexUnwrap.glsl", "./resources/basicSSBOFragTester.glsl");


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
    
            scratch.Reset(); 

            MeshChunk(myChunk, scratch); 

            //size_t currentOffset = renderer.m_head;



            void* gpuPtr = renderer.LockNextSegment();
            memcpy(gpuPtr, scratch.Data(), scratch.SizeBytes());


            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glLineWidth(20); // make the lines painfully obvious for debugging
            
            // ****************  World and camera Update Matrix
            //glm::mat4 projection = glm::perspective(glm::radians(65.0f), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);
            glm::mat4 projection = camera.GetProjectionMatrix(SCR_WIDTH / (float)SCR_HEIGHT, 0.1f);
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 model = glm::mat4(1.0f);
            
            // ************ End World and Camera Updates ********* // 
            
            // ******** Set Shader ********* //
            worldRingBufferShader.use();
            // Send view projections Matrix
            GLint locVP = glGetUniformLocation(worldRingBufferShader.ID, "u_ViewProjection");
            glUniformMatrix4fv(locVP, 1, GL_FALSE, glm::value_ptr(projection * view));

            // Send Chunk Position (Start at 0,0,0)
            GLint locChunk = glGetUniformLocation(worldRingBufferShader.ID, "u_ChunkOffset");
            glUniform3f(locChunk, 0.0f, 0.0f, 0.0f); // offset fixed to zero for now
            // ******** Set Shader ********* //
            
            renderer.UnlockAndDraw(scratch.Count()); // draw the amount of vertices we have 
            
            
            
            
            glfwSwapBuffers(window);
            glfwPollEvents();
        }

        // WHILE LOOP OUT OF SCOPE HERE
}

    //glDeleteBuffers(1, &emptyVAO);
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
    // 1. Column-major conversion
    // We need to access columns of voxels as uint64_t.
    // Since chunk is 32 high, one uint64_t covers 2 columns of height? 
    // No, standard Binary Greedy works best on 32x32 planes.
    // We iterate 6 faces.
    
    // Axis definitions for the 6 faces
    // 0: +X (Right), 1: -X (Left), 2: +Y (Top), 3: -Y (Bottom), 4: +Z (Front), 5: -Z (Back)
    for (int face = 0; face < 6; face++) {
        int axis = face / 2;
        int direction = (face % 2) == 0? 1 : -1;
        
        // We sweep along the main axis of the face (e.g. if Top face, we sweep Y)
        for (int slice = 1; slice <= CHUNK_SIZE; slice++) {
            
            // Step 2: Generate Face Masks for this slice
            // We pack a 32x32 plane of boolean visibility into an array of uint32s/uint64s
            // Since chunk is 32 wide, a single uint32_t represents a ROW.
            uint32_t colMasks[1]; 

            for (int row = 0; row < 32; row++) {
                uint32_t mask = 0;
                for (int col = 0; col < 32; col++) {
                    // Coordinates depend on axis rotation
                    int x, y, z;
                    
                    // Simple coordinate mapping logic (U, V, Slice)
                    if (axis == 0)      { x = slice; y = col + 1; z = row + 1; } // X-sweep
                    else if (axis == 1) { x = row + 1; y = slice; z = col + 1; } // Y-sweep
                    else                { x = col + 1; y = row + 1; z = slice; } // Z-sweep

                    // Check voxel and neighbor
                    // Note: In a real engine, we pre-convert the whole chunk to bitmasks to avoid this get loop.
                    uint8_t current = chunk.Get(x, y, z);
                    uint8_t neighbor = chunk.Get(x + (axis==0?direction:0), 
                                                 y + (axis==1?direction:0), 
                                                 z + (axis==2?direction:0));
                    
                    if (current!= 0 && neighbor == 0) {
                        mask |= (1u << col);
                    }
                }
                colMasks[row] = mask;
            }

            // Step 3: Greedy Merging
            // Iterate rows of the mask
            for (int i = 0; i < 32; i++) {
                uint32_t mask = colMasks[i];
                
                while (mask!= 0) {
                    // Find start of a run
                    int widthStart = ctz(mask); // count trailing zeros
                    
                    // Find end of run (first zero after start)
                    // We can shift out the start zeros, flip bits, and find next trailing zero
                    int widthEnd = widthStart;
                    while (widthEnd < 32 && (mask & (1u << widthEnd))) widthEnd++;
                    int width = widthEnd - widthStart;

                    // Compute height (check subsequent rows for same mask)
                    int height = 1;
                    for (int j = i + 1; j < 32; j++) {
                        // Check if the next row has the same bits set in this range
                        uint32_t nextRow = colMasks[j];
                        uint32_t runMask = ((1u << width) - 1u) << widthStart;
                        
                        if ((nextRow & runMask) == runMask) {
                            height++;
                            // Remove used bits from next row
                            colMasks[j] &= ~runMask;
                        } else {
                            break;
                        }
                    }

                    // Remove used bits from current row
                    uint32_t runMask = ((1u << width) - 1u) << widthStart;
                    mask &= ~runMask;

                    // Emit Quad
                    // You need to map (i, widthStart) back to (x, y, z) based on axis
                    // Output 4 vertices (for IBO) or 6 (for raw triangles)
                    // Let's assume we output 6 vertices to LinearAllocator
                    
                    //... (Vertex Packing Logic from Report 1)...
                    // allocator.Push(PackedVertex(...));
                }
            }
        }
    }
}