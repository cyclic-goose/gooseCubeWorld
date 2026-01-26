/* * ======================================================================================
 * GOOSE VOXELS - MAIN ENTRY POINT
 * ======================================================================================
 * Overview:
 * Initializes GLFW, GLAD, and ImGui. Sets up the main render loop, handles input 
 * processing, manages window events, and coordinates the World, Camera, and Profiler.
 * ======================================================================================
 */

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <iomanip> 
#include <string>
#include <vector>

#include "world.h" 
#include "camera.h"
#include "shader.h"
#include "ImGuiManager.hpp"
#include "profiler.h"
#include "screen_quad.h"
#include "splash_screen.hpp"
#include "texture_manager.h"
#include "input_manager.h"

// ======================================================================================
// --- CONFIGURATION & GLOBALS ---
// ======================================================================================

const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;
const bool START_FULLSCREEN = true; 

// Camera 
Camera camera(glm::vec3(0.0f, 150.0f, 150.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, -45.0f);

// Mouse State
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Debug & State Flags
bool lockFrustum = false;
bool f3DepthDebug = false;
int mipLevelDebug = 0;
glm::mat4 lockedCullMatrix;

// Systems
ImGuiManager gui;
UIConfig appState; 

// ======================================================================================
// --- CALLBACKS ---
// ======================================================================================

void framebuffer_size_callback(GLFWwindow* window, int width, int height) { 
    glViewport(0, 0, width, height); 
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (!appState.isGameMode) {
        firstMouse = true;
        return;
    }
    
    if (firstMouse) { 
        lastX = xpos; 
        lastY = ypos; 
        firstMouse = false; 
    }
    
    camera.ProcessMouseMovement(xpos - lastX, lastY - ypos);
    lastX = xpos; 
    lastY = ypos;
}

// ======================================================================================
// --- INPUT PROCESSING ---
// ======================================================================================

void processInput(GLFWwindow *window, World& world) {
    // --- Global Application Controls ---
    
    // TAB: Toggle Cursor Lock / Game Mode
    if (Input::IsJustPressed(window, GLFW_KEY_TAB)) {
        appState.isGameMode = !appState.isGameMode;
        glfwSetInputMode(window, GLFW_CURSOR, appState.isGameMode ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
    
    // DELETE: Exit Application
    if (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    // ESCAPE: Toggle Game Controls Menu
    if (Input::IsJustPressed(window, GLFW_KEY_ESCAPE)) {
        appState.showGameControls = !appState.showGameControls;

        if (appState.showGameControls) {
            appState.isGameMode = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else {
            appState.isGameMode = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            firstMouse = true; 
        }
    }

    // Click to capture cursor (if not over UI)
    if (!appState.isGameMode && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        if (!ImGui::GetIO().WantCaptureMouse) {
            appState.isGameMode = true;
            appState.showGameControls = false;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }
    
    // --- Debug Toggles ---

    // F2: Toggle Debug Panels
    if (Input::IsJustPressed(window, GLFW_KEY_F2)) {
        appState.showDebugPanel = !appState.showDebugPanel;
        appState.showCameraControls = !appState.showCameraControls;
        appState.showCullerControls = !appState.showCullerControls;
        Engine::Profiler::Get().Toggle();
    }
    
    // F3: Toggle Depth Debug View
    if (Input::IsJustPressed(window, GLFW_KEY_F3)) {
        f3DepthDebug = !f3DepthDebug;
    }

    // F4: Cycle Mip Levels
    if (Input::IsJustPressed(window, GLFW_KEY_F4)) {
        mipLevelDebug = (mipLevelDebug + 1) % 6; // Cycles 0-5
    }
    
    // M: Toggle World Gen Window
    if (Input::IsJustPressed(window, GLFW_KEY_M)) {
        appState.showWorldSettings = !appState.showWorldSettings;
    }
    
    // O: Toggle LOD Update Freeze (Observation Mode)
    if (Input::IsJustPressed(window, GLFW_KEY_O)) {
        bool current = world.GetLODFreeze();
        world.SetLODFreeze(!current);
        std::cout << "[DEBUG] LOD Freeze: " << (!current ? "ON" : "OFF") << std::endl;
    }

    // // R: Reload World
    // if (Input::IsJustPressed(window, GLFW_KEY_R)) {
    //     world.Reload(appState.editConfig);
    // }
    
    // --- Movement (Game Mode Only) ---
    if (appState.isGameMode) {
        // Speed Modifier
        camera.MovementSpeed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 500.0f : 50.0f;

        // Directional
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);
        
        // Vertical
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(UP, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.ProcessKeyboard(DOWN, deltaTime);
    }
}

// ======================================================================================
// --- MAIN ---
// ======================================================================================

int main() {
    // 1. Initialize GLFW
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
    
    // 2. Initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    
    // 3. Initialize GUI & GL State
    gui.Init(window);
    
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE); // Reverse-Z requirement
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER); // Reverse-Z comparison
    glClearDepth(0.0f);      // Reverse-Z clear value
    glEnable(GL_CULL_FACE); 
    glCullFace(GL_BACK);
    glClearColor(0.53f, 0.81f, 0.22f, 1.0f); 

    // Initial FBO sizing
    int curScrWidth = SCR_WIDTH, curScrHeight = SCR_HEIGHT;
    int prevScrWidth = curScrWidth, prevScrHeight = curScrHeight;
    glfwGetFramebufferSize(window, &curScrWidth, &curScrHeight);
    
    // NOTE: g_fbo is assumed to be an external global. 
    // Ideally this should be encapsulated, but maintained here for existing logic.
    g_fbo.Resize(curScrWidth, curScrHeight);
    
    // 4. World & Resources Scope
    { 
        Shader worldShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");
        Shader depthDebug("./resources/debug_quad_vert.glsl", "./resources/debug_quad_frag.glsl");
        
        // --- World Configuration ---
        WorldConfig globalConfig;
        globalConfig.VRAM_HEAP_ALLOCATION_MB = 2048; // *********************************** VRAM STATIC ALLOCATION 
        
        RenderLoadingScreen(window, gui, globalConfig.VRAM_HEAP_ALLOCATION_MB);

        // Configure LODs
        globalConfig.lodCount = 5;
        for (int i = 0; i < 5; i++) globalConfig.lodRadius[i] = 12;
        for (int i = 5; i < 12; i++) globalConfig.lodRadius[i] = 0; // Reserves

        World world(globalConfig); 

        // --- Load Textures ---
        std::vector<std::string> texturePaths = {
            "resources/textures/dirt1.jpg",   // ID 1
            "resources/textures/dirt1.jpg",   // ID 2
            "resources/textures/dirt1.jpg",   // ID 3
            "resources/textures/dirt1.jpg",   // ID 4
            "resources/textures/dirt1.jpg"    // ID 5
        };

        GLuint texArray = TextureManager::LoadTextureArray(texturePaths);
        world.SetTextureArray(texArray);

        glm::mat4 prevViewProj = glm::mat4(1.0f);

        // 5. Main Render Loop
        while (!glfwWindowShouldClose(window)) {
            // Timing
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            Engine::Profiler::Get().Update();
            
            // logic/world gen, chunk loading/unloading
            processInput(window, world);
            world.Update(camera.Position);
            
            // GUI and PROFILER START (profiler returns in constant time if its disabled)
            gui.BeginFrame();
            Engine::Profiler::Get().DrawUI(appState.isGameMode);

            // Handle Resize
            glfwGetFramebufferSize(window, &curScrWidth, &curScrHeight);
            if (curScrWidth != prevScrWidth || curScrHeight != prevScrHeight) {
                g_fbo.Resize(curScrWidth, curScrHeight);
                prevScrWidth = curScrWidth;
                prevScrHeight = curScrHeight;
            }
            
            // model view projection (mvp)
            glm::mat4 projection = camera.GetProjectionMatrix((float)curScrWidth / (float)curScrHeight, 0.1f);
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 viewProj = projection * view;
            
            if (!appState.lockFrustum) {
                lockedCullMatrix = viewProj;
            }
            
            // Render Prep
            glBindFramebuffer(GL_FRAMEBUFFER, g_fbo.fbo);
            glViewport(0, 0, curScrWidth, curScrHeight);
            glDisable(GL_SCISSOR_TEST); // Fix for ImGui scissor issues
            
            glClearColor(0.53f, 0.81f, 0.91f, 1.0f); 
            glClearDepth(0.0f); // Reverse-Z
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // World Draw
            // Triggers: Cull(PrevDepth) -> MultiDrawIndirect, then HI-Z occlusion calc for next frame
            // see GPU_CULLING_RENDER_SYSTEM.md for render pipeline info
            world.Draw(worldShader, viewProj, lockedCullMatrix, prevViewProj, projection, 
                       curScrWidth, curScrHeight, &depthDebug, f3DepthDebug, lockFrustum);

            // GUI Render
            gui.RenderUI(world, appState, camera, globalConfig.VRAM_HEAP_ALLOCATION_MB);
            gui.EndFrame();

            // swap to screen buffer
            glfwSwapBuffers(window);
            glfwPollEvents();

            // Reprojection History
            if (!appState.lockFrustum) {
                prevViewProj = viewProj;
            }
        }
    }
    
    // 6. Shutdown
    Engine::Profiler::Get().Shutdown();
    gui.Shutdown();
    glfwTerminate();
    return 0;
}