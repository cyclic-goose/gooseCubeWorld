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
#include "ImGuiManager.hpp"

// --- CONFIGURATION ---
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;
const bool START_FULLSCREEN = false; // Change to true to start in fullscreen

// Camera setup
Camera camera(glm::vec3(0.0f, 150.0f, 150.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, -45.0f);

float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// 1. Declare GUI Globally so callbacks can access it
ImGuiManager gui;

void framebuffer_size_callback(GLFWwindow* window, int width, int height) { glViewport(0, 0, width, height); }

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    // 2. Prevent camera movement if in Menu Mode
    if (!gui.m_GameMode) {
        firstMouse = true; // Reset this so camera doesn't jump when we resume
        return;
    }

    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    camera.ProcessMouseMovement(xpos - lastX, lastY - ypos);
    lastX = xpos; lastY = ypos;
}

void processInput(GLFWwindow *window, World& world) {
    // 3. Robust TAB Key Toggle
    static bool tabPressed = false;
    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS) {
        if (!tabPressed) {
            gui.m_GameMode = !gui.m_GameMode;
            tabPressed = true;
            if (gui.m_GameMode) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            else glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    } else {
        tabPressed = false;
    }

    // 4. F11 Fullscreen Toggle
    static bool f11Pressed = false;
    if (glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS) {
        if (!f11Pressed) {
            gui.ToggleFullscreen();
            f11Pressed = true;
        }
    } else {
        f11Pressed = false;
    }

    // Standard Exit
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    
    // 5. Input only allowed in Game Mode
    if (gui.m_GameMode) {
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.MovementSpeed = 500.0f;
        else camera.MovementSpeed = 50.0f;

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.ProcessKeyboard(DOWN, deltaTime);
    }

    // Hotkeys that work in both modes
    static bool rPressed = false;
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !rPressed) {
        WorldConfig newConfig = world.GetConfig();
        newConfig.seed = rand();
        std::cout << "[System] Reloading World... Seed: " << newConfig.seed << std::endl;
        world.Reload(newConfig);
        rPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) rPressed = false;
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Goose Voxels", NULL, NULL);
    if (window == NULL) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    
    // 1. Apply Startup Fullscreen Config
    if (START_FULLSCREEN) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }

    // 2. Default VSync to ON (safe default)
    // The gui.Init() below will respect the m_VSync default (true) or user can toggle later
    glfwSwapInterval(0); 

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;

    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL); 
    glClearDepth(0.0f);     
    glEnable(GL_CULL_FACE); 
    glCullFace(GL_BACK);
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f); 

    { // NEW SCOPE for destructors calling before glfwTerminate()
        Shader worldShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");
        
        WorldConfig globalConfig;
        // ... config setup ...
        globalConfig.lodCount = 5; 
        globalConfig.lodRadius[0] = 10; 
        globalConfig.lodRadius[1] = 10; 
        globalConfig.lodRadius[2] = 16; 
        globalConfig.lodRadius[3] = 24; 
        globalConfig.lodRadius[4] = 32;
        //globalConfig.lodRadius[5] = 20;

        World world(globalConfig);
        gui.Init(window); // Stores window pointer for window management

        while (!glfwWindowShouldClose(window)) {
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            processInput(window, world);
            world.Update(camera.Position);

            gui.BeginFrame();
            gui.RenderDebugPanel(world); 

            // if (gui.RenderStandardMenu()) {
            //     glfwSetWindowShouldClose(window, true);
            // }

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glm::mat4 projection = camera.GetProjectionMatrix((float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f);
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 viewProj = projection * view;

            world.Draw(worldShader, viewProj);

            gui.EndFrame();

            glfwSwapBuffers(window);
            glfwPollEvents();

            // --- PREVENT CPU HOGGING ---
            // Forces a context switch so the OS knows the window is responsive
            //std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    glfwTerminate();
    return 0;
}