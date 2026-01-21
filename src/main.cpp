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
#include "profiler.h"

// --- CONFIGURATION ---
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;
const bool START_FULLSCREEN = true; 

// Camera setup
Camera camera(glm::vec3(0.0f, 150.0f, 150.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, -45.0f);

float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Debug Logic
bool lockFrustum = false;
bool fKeyPressed = false;
glm::mat4 lockedCullMatrix;

// GLOBAL GUI MANAGERS
ImGuiManager gui;
UIConfig appState; 




void framebuffer_size_callback(GLFWwindow* window, int width, int height) { glViewport(0, 0, width, height); }

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (!appState.isGameMode) {
        firstMouse = true;
        return;
    }
    
    if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }
    camera.ProcessMouseMovement(xpos - lastX, lastY - ypos);
    lastX = xpos; lastY = ypos;
}


void processInput(GLFWwindow *window, World& world) {
    // 1. TAB: Toggle Cursor Lock
    static bool tabPressed = false;
    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS) {
        if (!tabPressed) {
            appState.isGameMode = !appState.isGameMode;
            tabPressed = true;
            if (appState.isGameMode) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            else glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    } else { tabPressed = false; }
    
    // 2. Click to capture cursor (only if not clicking on ImGui)
    if (!appState.isGameMode && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        if (!ImGui::GetIO().WantCaptureMouse) {
            appState.isGameMode = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }
    
    // 3. F2: Toggle Debug Window
    static bool f2Pressed = false;
    if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS) {
        if (!f2Pressed) {
            appState.showDebugPanel = !appState.showDebugPanel;
            f2Pressed = true;
        }
    } else { f2Pressed = false; }
    
    
    // P: Toggle Profiler Window
    static bool pPressed = false;
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
        if (!pPressed) {
            Engine::Profiler::Get().Toggle();
            pPressed = true;
        }
    } else { pPressed = false; }
    
    // 4. M: Toggle World Gen Window
    static bool mPressed = false;
    if (glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS) {
        if (!mPressed) {
            appState.showWorldSettings = !appState.showWorldSettings;
            mPressed = true;
        }
    } else { mPressed = false; }
    
    // Standard Exit
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    
    // Movement
    if (appState.isGameMode) {
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) camera.MovementSpeed = 500.0f;
        else camera.MovementSpeed = 50.0f;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.ProcessKeyboard(DOWN, deltaTime);
    }
    
    // R: Reload shortcut
    static bool rPressed = false;
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !rPressed) {
        world.Reload(appState.editConfig);
        rPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_R) == GLFW_RELEASE) rPressed = false;
    
    // 5. F: Freeze Frustum
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) {
        if (!fKeyPressed) {
            //lockFrustum = !lockFrustum;
            appState.lockFrustum = !appState.lockFrustum;
            fKeyPressed = true;
            std::cout << "[DEBUG] Frustum Lock: " << (lockFrustum ? "ON" : "OFF") << std::endl;
        }
    } else { fKeyPressed = false; }
    
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
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "v0.2.1-alpha"); 
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
    
    if (START_FULLSCREEN) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    
    gui.Init(window);
    RenderLoadingScreen(window);

    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GEQUAL); 
    //glDepthFunc(GL_LEQUAL);
    glClearDepth(0.0f);     
    glEnable(GL_CULL_FACE); 
    glCullFace(GL_BACK);
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f); 

    { 
        Shader worldShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");
        
        WorldConfig globalConfig;
        globalConfig.lodCount = 8; 
        globalConfig.lodRadius[0] = 10;   
        globalConfig.lodRadius[1] = 6;  
        globalConfig.lodRadius[2] = 6;   
        globalConfig.lodRadius[3] = 6;   
        globalConfig.lodRadius[4] = 8;  
        globalConfig.lodRadius[5] = 12; 
        globalConfig.lodRadius[6] = 14; 
        globalConfig.lodRadius[7] = 16; 
        
        World world(globalConfig);

        while (!glfwWindowShouldClose(window)) {
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            // ********* PROFILER UPDATE ********* // 
            // Inside main loop, before rendering:
            Engine::Profiler::Get().Update();

            // Inside your GUI rendering block:
            // ********* PROFILER UPDATE ********* // 
            
            processInput(window, world);
            world.Update(camera.Position);
            
            gui.BeginFrame();
            Engine::Profiler::Get().DrawUI(appState.isGameMode);

            
            // CLEAR 
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            
            // Recalc mvp
            glm::mat4 projection = camera.GetProjectionMatrix((float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f);
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 viewProj = projection * view;
            
            // Handle Frustum Locking Logic
            if (!appState.lockFrustum) {
                lockedCullMatrix = viewProj;
            }
            
            // RENDER WORLD
            // Draw with separate ViewProj for Rendering and Culling
            world.Draw(worldShader, viewProj, lockedCullMatrix);


            // Render GUI
            gui.RenderUI(world, appState, camera);
            gui.EndFrame();

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
    }
    Engine::Profiler::Get().Shutdown();
    gui.Shutdown();
    glfwTerminate();
    return 0;
}