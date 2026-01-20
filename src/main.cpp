#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iomanip> 
#include <string>

#include "world.h" 
#include "camera.h"
#include "shader.h"
#include "ringBufferSSBO.h"
#include "linearAllocator.h"

const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

// Camera
Camera camera(glm::vec3(0.0f, 60.0f, 60.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, -45.0f);

float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;
bool keyProcessed = false; 

void framebuffer_size_callback(GLFWwindow* window, int width, int height) { glViewport(0, 0, width, height); }
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    camera.ProcessMouseMovement(xpos - lastX, lastY - ypos);
    lastX = xpos; lastY = ypos;
}

WorldConfig globalConfig;

void processInput(GLFWwindow *window, World& world) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.MovementSpeed = 100.0f;
    else camera.MovementSpeed = 30.0f;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(UP, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.ProcessKeyboard(DOWN, deltaTime);

    if (!keyProcessed) {
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            globalConfig.seed = rand();
            world.Reload(globalConfig);
            keyProcessed = true;
        }
        if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS) {
            static bool wire = false;
            wire = !wire;
            glPolygonMode(GL_FRONT_AND_BACK, wire ? GL_LINE : GL_FILL);
            keyProcessed = true;
        }
    }
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE && glfwGetKey(window, GLFW_KEY_TAB) == GLFW_RELEASE) {
        keyProcessed = false;
    }
}

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

    {
        // Debug Render Settings
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glClearDepth(1.0f);
        glDisable(GL_CULL_FACE); // Show both sides of faces
        glClearColor(0.53f, 0.81f, 0.92f, 1.0f); 

        // Allocator sizes
        const int MAX_VERTS = 5000000; // 5M GPU buffer
        RingBufferSSBO renderer(MAX_VERTS * sizeof(PackedVertex), sizeof(PackedVertex)); 
        Shader worldShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");
        LinearAllocator<PackedVertex> scratch(32 * 1024 * 1024); // 32MB scratch for frame

        globalConfig.seed = 1337;
        globalConfig.renderDistance = 6;
        globalConfig.heightScale = 40.0f;
        globalConfig.noiseFrequency = 0.005f; 
        
        World world(globalConfig);

        while (!glfwWindowShouldClose(window)) {
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;
            processInput(window, world);

            std::string title = "Goose Voxels | FPS: " + std::to_string((int)(1.0f / deltaTime));
            glfwSetWindowTitle(window, title.c_str());

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 viewProj = projection * view;

            world.Draw(renderer, scratch, worldShader, viewProj);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    } 

    glfwTerminate();
    return 0;
}