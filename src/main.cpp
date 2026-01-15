#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "chunk.h"
#include "camera.h"
#include "shader.h"

// Screen settings
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

// Camera
Camera camera(glm::vec3(8.0f, 6.0f, 20.0f)); // Start above the chunk
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
    glEnable(GL_CULL_FACE); 
    //glDisable(GL_CULL_FACE); 
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE); 
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL); // Reverse-Z
    glClearDepth(0.0f);


    // VBO/VAO Init
    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    // Create Mesh Data
    Chunk myChunk;
    myChunk.Generate(0, 0, 0); // Generates a sphere at origin
    
    std::vector<Vertex> vertices;
    myChunk.Mesh(vertices);

    Shader basicShader("./resources/basicChunkVert.glsl", "./resources/basicChunkFrag.glsl");

    // DEBUG STEP 2: Print vertex count to console
    std::cout << "Generated Vertices: " << vertices.size() << std::endl;
    if (vertices.empty()) {
        std::cout << "WARNING: Mesh is empty! Check Chunk::Generate()" << std::endl;
    }

    // Upload
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    // Attrib 0: Pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Attrib 1: Normal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);

    // NOTE: If your shader doesn't have layout(location=2), you can keep this enabled 
    // but the shader just won't use it. However, 'Vertex' struct MUST have 'texCoord' member
    // for the stride (sizeof(Vertex)) to be correct.
    // glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
    // glEnableVertexAttribArray(2);

    // Render Loop
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        glClearColor(0.4f, 0.3f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(basicShader.ID);

        // Update Matrix
        //glm::mat4 projection = glm::perspective(glm::radians(65.0f), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);
        glm::mat4 projection = camera.GetProjectionMatrix(SCR_WIDTH / (float)SCR_HEIGHT, 0.1f);
        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 model = glm::mat4(1.0f);

        glUniformMatrix4fv(glGetUniformLocation(basicShader.ID, "projection"), 1, GL_FALSE, &projection[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(basicShader.ID, "view"), 1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(basicShader.ID, "model"), 1, GL_FALSE, &model[0][0]);

        glBindVertexArray(VAO);
        if (!vertices.empty()) {
             glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteBuffers(1, &VBO);
    glDeleteVertexArrays(1, &VAO);
    glfwTerminate();
    return 0;
}