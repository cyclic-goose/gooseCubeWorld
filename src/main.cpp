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
const bool START_FULLSCREEN = true; // Change to true to start in fullscreen

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
        //newConfig.seed = rand(); // change see upon reload? why
        std::cout << "[System] Reloading World... Seed: " << newConfig.seed << std::endl;
        world.Reload(newConfig);
        rPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) rPressed = false;
}

// a quick one off "splash screen" so the player doesnt think the computer just froze (although it is kind of, its allocating vram)
void RenderLoadingScreen(GLFWwindow* window) {
    // Force a few frames to render to clear out the swap chain buffers
    // 3 iterations ensures we handle Double or Triple buffering correctly
    for (int i = 0; i < 3; i++) {
        // 1. Viewport & Clear
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 2. Draw UI
        gui.BeginFrame();

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        
        ImGui::Begin("Loading", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        ImGui::SetWindowFontScale(3.0f);
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Cyclic Goose Voxel Engine");
        ImGui::SetWindowFontScale(2.0f);
        ImGui::Separator();
        ImGui::Text("Allocating VRAM Buffers (approx 900000GB)...");
        ImGui::Text("Allocating Threadpool...");
        ImGui::Text("Please Wait...");

        // --- VERSION WINDOW (Anchored Relative to Main) ---
        // Calculate position: Right edge of main box, 5 pixels down
        // CAPTURE GEOMETRY: Get the bottom-right corner of this window
        ImVec2 mainPos = ImGui::GetWindowPos();
        ImVec2 mainSize = ImGui::GetWindowSize();
        ImVec2 versionPos;
        versionPos.x = mainPos.x + mainSize.x; 
        versionPos.y = mainPos.y + mainSize.y + 5.0f; 

        // Pivot (1.0f, 0.0f) = Top-Right of the version text
        // This aligns the right edge of the text with the right edge of the box above it
        ImGui::SetNextWindowPos(versionPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.0f); 
        
        ImGui::Begin("Version", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "v0.1.0-alpha"); 
        ImGui::End();
        ImGui::End();

        gui.EndFrame();
        
        // 3. Swap and Poll
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    // 4. Force synchronization before hanging the CPU
    glFinish(); 
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
    
    
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;


    // load the gui and use it to render a quick splash screen
    // --- RENDER SPLASH SCREEN ---
    gui.Init(window); // Stores window pointer for window management
    
    RenderLoadingScreen(window);



    // pre game loop configurations
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
        globalConfig.lodCount = 8; 
        globalConfig.lodRadius[0] = 10; 
        globalConfig.lodRadius[1] = 5; 
        globalConfig.lodRadius[2] = 5; 
        globalConfig.lodRadius[3] = 5; 
        globalConfig.lodRadius[4] = 5;
        globalConfig.lodRadius[5] = 20;
        globalConfig.lodRadius[6] = 20;
        globalConfig.lodRadius[7] = 32;

        // This takes a while but is only run once
        World world(globalConfig);

        while (!glfwWindowShouldClose(window)) {
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            processInput(window, world);
            world.Update(camera.Position);

            gui.BeginFrame();
            gui.RenderDebugPanel(world); 

            if (gui.RenderStandardMenu()) {
                glfwSetWindowShouldClose(window, true);
            }

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

    gui.Shutdown(); // shutdown gui since its scope is global
    glfwTerminate();
    return 0;
}