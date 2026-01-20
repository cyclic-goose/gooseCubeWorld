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

const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;

// Camera setup
Camera camera(glm::vec3(0.0f, 150.0f, 150.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, -45.0f);

float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;
bool keyProcessed = false; 

WorldConfig globalConfig;

void framebuffer_size_callback(GLFWwindow* window, int width, int height) { glViewport(0, 0, width, height); }
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    camera.ProcessMouseMovement(xpos - lastX, lastY - ypos);
    lastX = xpos; lastY = ypos;
}

void processInput(GLFWwindow *window, World& world) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    
    // Speed boost for flying around large maps
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.MovementSpeed = 500.0f;
    else camera.MovementSpeed = 50.0f;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(UP, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.ProcessKeyboard(DOWN, deltaTime);

    if (!keyProcessed) {
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS) {
            globalConfig.seed = rand();
            std::cout << "[System] Reloading World... Seed: " << globalConfig.seed << std::endl;
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

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Goose Voxels: Stacked LODs", NULL, NULL);
    if (window == NULL) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    {
        // 1. REVERSE-Z SETUP (Best for large distances)
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GEQUAL); 
        glClearDepth(0.0f);     

        // 2. CULLING
        // With "Stacked" LODs, we want normal backface culling.
        // We removed the skirts, so we don't need to worry about skirt winding.
        glEnable(GL_CULL_FACE); 
        glCullFace(GL_BACK);
        
        glClearColor(0.53f, 0.81f, 0.92f, 1.0f); 

        // 3. SHADER
        Shader worldShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");
        
        // 4. CONFIGURATION (LOD CONTROL)
        globalConfig.seed = 1337;
        globalConfig.worldHeightChunks = 8;
        
        // -- LOD CONTROL SECTION --
        // lodCount: How many stacked layers to render. 
        // 5 layers = Scales 1, 2, 4, 8, 16.
        globalConfig.lodCount = 5; 

        // lodRadius[i]: How many *chunks* out to render for that layer.
        // Note: Chunks at higher LODs are physically larger.
        // LOD 0 (Scale 1, 32m):  Radius 12 = 384m range
        // LOD 1 (Scale 2, 64m):  Radius 12 = 768m range
        // LOD 2 (Scale 4, 128m): Radius 16 = 2048m range
        // LOD 3 (Scale 8, 256m): Radius 16 = 4096m range
        // LOD 4 (Scale 16, 512m): Radius 16 = 8192m range
        //
        // Stacking Strategy: Increase radius for higher LODs so they stick out 
        // from underneath the detailed layers.
        globalConfig.lodRadius[0] = 12; 
        globalConfig.lodRadius[1] = 12; 
        globalConfig.lodRadius[2] = 16; 
        globalConfig.lodRadius[3] = 16; 
        globalConfig.lodRadius[4] = 16; 

        globalConfig.scale = 0.08f;           
        globalConfig.hillAmplitude = 15.0f;   
        globalConfig.mountainAmplitude = 80.0f; 
        globalConfig.seaLevel = 10;
        globalConfig.enableCaves = false;      
        
        World world(globalConfig);

        // 5. RENDER LOOP
        while (!glfwWindowShouldClose(window)) {
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;
            processInput(window, world);

            world.Update(camera.Position);

            std::string title = "Goose Voxels | FPS: " + std::to_string((int)(1.0f / deltaTime)) + " | Pos: " 
                + std::to_string((int)camera.Position.x) + ", " 
                + std::to_string((int)camera.Position.y) + ", " 
                + std::to_string((int)camera.Position.z);
            glfwSetWindowTitle(window, title.c_str());

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Infinite Projection Matrix (far plane at infinity)
            glm::mat4 projection = camera.GetProjectionMatrix((float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f);
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 viewProj = projection * view;

            world.Draw(worldShader, viewProj);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    } 

    glfwTerminate();
    return 0;
}