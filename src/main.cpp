/* * ======================================================================================
 * GOOSE Cube Engine - MAIN ENTRY POINT
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
//#include "terrain_superflat.h"
//#include "terrain_superflat_withGlass.h"
#include "engine_config.h"
#include "playerController.h"


///////// Terrain includes, each terrain base is a base class to terrain_system
#include "terrain_smooth_noise.h"
#include "terrain_standard_gen_fast.h"


// ======================================================================================
// --- CONFIGURATION & GLOBALS ---
// ======================================================================================



const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;
const bool START_FULLSCREEN = true; 

//static const uint16_t GLOBAL_VRAM_ALLOC_SIZE_MB = 1024 * 2;

// Camera 
//Camera camera(glm::vec3(0.0f, 150.0f, 150.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, -45.0f);
Player player(glm::vec3(0.0f, 150.0f, 150.0f));

// Mouse State
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;
float switchCooldown = 0.0f; // Cooldown timer so user cant spam buttom presses

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
    
    player.ProcessMouseMovement(xpos - lastX, lastY - ypos);
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


    if (switchCooldown > 0.0f) {
        switchCooldown -= deltaTime;
    }

    // ==========================================================
    // RUNTIME SWITCHING LOGIC, will later use this to change dimensions in game
    // ==========================================================
    
    if (Input::IsJustPressed(window, GLFW_KEY_T)) { // 'T' for Terraforming
        if (world.IsBusy())
        {
            std::cout << "World is still generating. Please Wait..." << std::endl;
        }else if (switchCooldown > 0.0f) 
        {
            std::cout << "Generation on cooldown..." << std::endl;
        
        } else {
            std::cout << "[Main] Switching Generator..." << std::endl;

            // Create the NEW Generator (e.g. Mars Generator)
            // auto newGen = std::make_unique<MarsGenerator>(12345); 
            // For now, let's just re-create Standard with a different seed to simulate a switch
            auto newGen = std::make_unique<StandardGenerator2>(rand());

            // Load Textures for the NEW Generator
            // Important: We must unload the old array if we are strictly managing VRAM, 
            // but typically we just overwrite the ID reference in World.
            // Ideally, TextureManager should have a 'DeleteTexture(id)' method.
            std::vector<std::string> newPaths = newGen->GetTexturePaths();
            GLuint newTexArray = TextureManager::LoadTextureArray(newPaths);

            // Inject into World
            
            // Call the helper (assuming you added it to World class)
            world.SwitchGenerator(std::move(newGen), newTexArray);
            switchCooldown = 2.0f;
        }
        
    }

    // // R: Reload World
    // if (Input::IsJustPressed(window, GLFW_KEY_R)) {
    //     world.Reload(appState.editConfig);
    // }
    
    // --- Movement (Game Mode Only) ---
    // if (appState.isGameMode) {
    //     // Speed Modifier
    //     camera.MovementSpeed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 500.0f : 50.0f;

    //     // Directional
    //     if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) camera.ProcessKeyboard(FORWARD, deltaTime);
    //     if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) camera.ProcessKeyboard(BACKWARD, deltaTime);
    //     if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) camera.ProcessKeyboard(LEFT, deltaTime);
    //     if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) camera.ProcessKeyboard(RIGHT, deltaTime);
        
    //     // Vertical
    //     if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) camera.ProcessKeyboard(UP, deltaTime);
    //     if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) camera.ProcessKeyboard(DOWN, deltaTime);
    // }
}

// ======================================================================================
// --- MAIN ---
// ======================================================================================

int main() {
    // Initialize GLFW
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Goose Cube World", NULL, NULL);
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
    
    // Initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    
    //Initialize GUI & GL State
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

    player.camera.Yaw = 30.0f;
    player.camera.Pitch = 5.0f;
    player.camera.updateCameraVectors();
    
    // World & Resources Scope
    { 
        // shader for world and then shader that helped me debug depth buffer
        Shader worldShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");
        Shader depthDebug("./resources/debug_quad_vert.glsl", "./resources/debug_quad_frag.glsl");
        
        // --- World Configuration ---
        EngineConfig globalConfig; // GLOBAL CONFIG overwrites the engine config sent into world
        //globalConfig.VRAM_HEAP_ALLOCATION_MB = GLOBAL_VRAM_ALLOC_SIZE_MB; // *********************************** VRAM STATIC ALLOCATION 
        
        // lil splash screen while VRAM and RAM buffers are allocated
        RenderLoadingScreen(window, gui, globalConfig.VRAM_HEAP_ALLOCATION_MB);

        // Configure LODs
        // globalConfig.lodCount = 5;
        // for (int i = 0; i < 5; i++) globalConfig.lodRadius[i] = 12;
        // for (int i = 5; i < 12; i++) globalConfig.lodRadius[i] = 0; // Reserves



        // create our terrain generation by choosing which class we send in
        auto defaultTerrainGenerator = std::make_unique<StandardGenerator2>(); // seed input
        // Ask the generator what textures it needs
        std::vector<std::string> texturePaths = defaultTerrainGenerator->GetTexturePaths();
        // Load them into GPU
        GLuint texArray = TextureManager::LoadTextureArray(texturePaths);
        World world(globalConfig, std::move(defaultTerrainGenerator));


        // *********** 
        // --- Manually Load Textures for testing ---
        // std::vector<std::string> texturePaths = {
        //     "resources/textures/dirt1.jpg",   // ID 1
        //     "resources/textures/dirt1.jpg",   // ID 2
        //     "resources/textures/dirt1.jpg",   // ID 3
        //     "resources/textures/dirt1.jpg",   // ID 4
        //     "resources/textures/dirt1.jpg"    // ID 5
        // };
        // GLuint texArray = TextureManager::LoadTextureArray(texturePaths);
        world.SetTextureArray(texArray);



        glm::mat4 prevViewProj = glm::mat4(1.0f);



        // ****** GAME LOOP ******* // 

        while (!glfwWindowShouldClose(window)) {
            // Timing
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            Engine::Profiler::Get().Update();

            // update player before? or after world update???
            if (appState.isGameMode) {
                // player needs world generator because it exposes getBlock (needed for raycasting/collisions etc)
                player.Update(deltaTime, window, world.GetGenerator());
            }
            

            // logic/world gen, chunk loading/unloading
            processInput(window, world);
            world.Update(player.camera.Position);
            

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
            glm::mat4 projection = player.camera.GetProjectionMatrix((float)curScrWidth / (float)curScrHeight, 0.1f);
            glm::mat4 view = player.camera.GetViewMatrix();
            glm::mat4 viewProj = projection * view;
            
            if (!appState.lockFrustum) {
                lockedCullMatrix = viewProj;
            }
            

            // Render Prep
            
            // Clear Screen (Framebuffer 0) to Debug Magenta
            // If you see this color, the Blit from FBO to Screen failed.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, curScrWidth, curScrHeight);
            glClearColor(1.0f, 0.0f, 1.0f, 1.0f); 
            glClear(GL_COLOR_BUFFER_BIT);

            // Clear FBO (Game View)
            glBindFramebuffer(GL_FRAMEBUFFER, g_fbo.fbo);
            glViewport(0, 0, curScrWidth, curScrHeight);
            glDisable(GL_SCISSOR_TEST); 
            
            glClearColor(0.53f, 0.81f, 0.91f, 1.0f); 
            glClearDepth(0.000000000f); // Reverse-Z
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);



            // World Draw
            // Triggers: Cull(PrevDepth) -> MultiDrawIndirect, then HI-Z occlusion calc for next frame
            // see GPU_CULLING_RENDER_SYSTEM.md for render pipeline info
            world.Draw(worldShader, viewProj, prevViewProj, projection, 
                       curScrWidth, curScrHeight, &depthDebug, f3DepthDebug, lockFrustum);


            // GUI Render
            gui.RenderUI(world, appState, player, globalConfig.VRAM_HEAP_ALLOCATION_MB);
            gui.EndFrame();

            // swap to screen buffer
            glfwSwapBuffers(window);
            glfwPollEvents();

            // Reprojection History
            
            prevViewProj = viewProj;
            
        }
    }
    
    // 6. Shutdown
    Engine::Profiler::Get().Shutdown();
    gui.Shutdown();
    glfwTerminate();
    return 0;
}