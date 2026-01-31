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

// utility and debug
#include "gui_utils.h"
#include "debug_chunks.h"

///////// Terrain includes, each terrain base is a base class to terrain_system
#include "terrain/terrain_smooth_noise.h"
#include "terrain/terrain_standard_gen_fast.h"
#include "terrain/advancedGenerator.h"
#include "terrain/terrain_bizzaro_world.h"
#include "terrain/terrain_superflat.h"


// ======================================================================================
// --- CONFIGURATION & GLOBALS ---
// ======================================================================================



const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;
const bool START_FULLSCREEN = true; 

//static const uint16_t GLOBAL_VRAM_ALLOC_SIZE_MB = 1024 * 2;

// Camera 
//Camera camera(glm::vec3(0.0f, 150.0f, 150.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, -45.0f);
//Player player(glm::vec3(-529.0f, 76.0f, 61.0f)); // good starting position for advanced generator
//Player player(glm::vec3(4855.0f, 100.0f, -4005.0f)); // debug spot back when I needed to find this spot, turned out i was sampling the noise generator wrong resulting in bad terrain at LOD 0
Player player(glm::vec3(0.0f, 15.0f, 9.0f));


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
        //mipLevelDebug = (mipLevelDebug + 1) % 6; // Cycles 0-5
        ChunkDebugger::Get().m_enabled = !ChunkDebugger::Get().m_enabled;
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
        appState.showTerrainGui = !appState.showTerrainGui;
        // if (world.IsBusy())
        // {
        //     std::cout << "World is still generating. Please Wait..." << std::endl;
        // }else if (switchCooldown > 0.0f) 
        // {
        //     std::cout << "Generation on cooldown..." << std::endl;
        
        // } else {
        //     std::cout << "[Main] Switching Generator..." << std::endl;

        //     // Create the NEW Generator (e.g. Mars Generator)
        //     // auto newGen = std::make_unique<MarsGenerator>(12345); 
        //     // For now, let's just re-create Standard with a different seed to simulate a switch
        //     // auto newGen = std::make_unique<AdvancedGenerator>(rand());

        //     // // Load Textures for the NEW Generator
        //     // // Important: We must unload the old array if we are strictly managing VRAM, 
        //     // // but typically we just overwrite the ID reference in World.
        //     // // Ideally, TextureManager should have a 'DeleteTexture(id)' method.
        //     // std::vector<std::string> newPaths = newGen->GetTexturePaths();
        //     // GLuint newTexArray = TextureManager::LoadTextureArray(newPaths);

        //     // // Inject into World
            
        //     // // Call the helper (assuming you added it to World class)
        //     // world.SwitchGenerator(std::move(newGen), newTexArray);
        //     // switchCooldown = 2.0f;
        // }
        
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
    /////////////////// ******* Initialize GLFW ********* /////////
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

    /////////////////// ******* Initialize GLFW ********* /////////



    /////////////////// ******* Initialize GLAD ********* /////////
    
    // Initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return -1;
    
    //Initialize GUI & GL State
    gui.Init(window);
    
    // openGL setup
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE); // Reverse-Z requirement
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER); // Reverse-Z comparison
    glClearDepth(0.0f);      // Reverse-Z clear value
    glEnable(GL_CULL_FACE); 
    glCullFace(GL_BACK);
    glClearColor(0.53f, 0.81f, 0.22f, 1.0f); 

    /////////////////// ******* Initialize GLAD ********* /////////



    //////////// ********** Initial FBO sizing, the framebuffer object is what ultimately paints the screen regardless of whats rendered to what
    int curScrWidth = SCR_WIDTH, curScrHeight = SCR_HEIGHT;
    int prevScrWidth = curScrWidth, prevScrHeight = curScrHeight;
    glfwGetFramebufferSize(window, &curScrWidth, &curScrHeight);

    // crash on windows likely related to trying to resize before window had a valid size
    // Guard against 0 size on initial startup (common on Windows with Fullscreen)
    if (curScrWidth > 0 && curScrHeight > 0) {
        g_fbo.Resize(curScrWidth, curScrHeight);
    } else {
        std::cout << "[Main] Warning: Initial window size is 0x0. Deferring FBO creation." << std::endl;
    }
    
    // NOTE: g_fbo is assumed to be an external global. 
    // Ideally this should be encapsulated, but maintained here for existing logic.
    g_fbo.Resize(curScrWidth, curScrHeight);

    ///////// tweak player start
    player.camera.Yaw = -142.0f;
    player.camera.Pitch = -1.0f;
    player.camera.updateCameraVectors();
    ///////// tweak player start




    
    // World & Resources Scope
   try { 

        ////// ************* SHADERS *********** //////////
        // shader for world and then shader that helped me debug depth buffer
        Shader worldShader("./resources/VERT_UPGRADED.glsl", "./resources/FRAG_UPGRADED.glsl");
        //Shader worldShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");
        Shader depthDebug("./resources/debug_quad_vert.glsl", "./resources/debug_quad_frag.glsl"); // press F3 to see depth buffer

        Shader chunkDebugShader("./resources/shaders/chunkDebugVert.glsl", "./resources/shaders/chunkDebugFrag.glsl");
        Shader selectionShader("./resources/shaders/cubeSelectionVert.glsl", "./resources/shaders/cubeSelectionFrag.glsl");;
        ////// ************* SHADERS *********** //////////



        // --- World Configuration --- //
        EngineConfig globalConfig; // GLOBAL CONFIG overwrites the engine config sent into world
        //globalConfig.VRAM_HEAP_ALLOCATION_MB = GLOBAL_VRAM_ALLOC_SIZE_MB; // *********************************** VRAM STATIC ALLOCATION 
        // --- World Configuration --- //
        
        // lil splash screen while VRAM and RAM buffers are allocated
        RenderLoadingScreen(window, gui, globalConfig.VRAM_HEAP_ALLOCATION_MB);


        ////////////// *********** create our start terrain generator by choosing which class we send in
        auto defaultTerrainGenerator = std::make_unique<SuperflatGenerator>(); // seed input
        // Ask the generator what textures it needs
        std::vector<std::string> texturePaths = defaultTerrainGenerator->GetTexturePaths();
        // Load them into GPU
        GLuint texArray = TextureManager::LoadTextureArray(texturePaths);
        World world(globalConfig, std::move(defaultTerrainGenerator));
        ////////////// *********** create our start terrain generator by choosing which class we send in



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


        // initialize for occlusion culler retroprojection
        glm::mat4 prevViewProj = glm::mat4(1.0f);
        


        /////////////////// ****** GAME LOOP ******* //////////////////

        while (!glfwWindowShouldClose(window)) { ///// ************ GAME START SCOPE ************* 
            //////// *************** Timing
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            // Prevents massive physics/movement jumps if the app hangs or is dragged
            deltaTime = std::min(deltaTime, 0.05f); 

            Engine::Profiler::Get().Update();
            //////// *************** Timing

            
            
            
            
            ///////// *****************  logic/world gen, chunk loading/unloading
            // update player before? or after world update???
            if (appState.isGameMode) {
                // player needs world generator because it exposes getBlock (needed for raycasting/collisions etc)
                player.Update(deltaTime, window, world); // pass in world to get collision if any
            }

            // GUI and PROFILER START (profiler returns in constant time if its disabled)
            gui.BeginFrame(); // start ImGui Frame (its normal to do this every loop)
            Engine::Profiler::Get().DrawUI(appState.isGameMode); // returns in extremely small constant time if not in debug mode (usually)
            if (world.getFrameCount()  < 20000)
            {
                GUI::DrawScreenMessage("Welcome", GUI::LEVEL_WARN);

            }
            processInput(window, world); // process keyboard and mouse input
            world.Update(player.camera.Position); // calc world updates like chunk loading/unloading
            

            ///////// *****************  logic/world gen, chunk loading/unloading



            
            
            
            
            ///////////// *********** SCREEN PREP 
            // Minimization / Background Throttling
            // If dimensions are 0 (minimized), pause thread to save resources
            while (curScrWidth == 0 || curScrHeight == 0) { 
                glfwGetFramebufferSize(window, &curScrWidth, &curScrHeight);
                glfwWaitEvents(); // Sleeps until event (resize/restore) occurs
                if (glfwWindowShouldClose(window)) break;
                // Reset timer so we don't jump when restoring
                lastFrame = (float)glfwGetTime();
            }
            if (glfwWindowShouldClose(window)) break;
            // Handle Resize
            glfwGetFramebufferSize(window, &curScrWidth, &curScrHeight);
            // Only resize if dimensions changed AND they are valid (>0)
            if ((curScrWidth != prevScrWidth || curScrHeight != prevScrHeight) && curScrWidth > 0 && curScrHeight > 0) {
                g_fbo.Resize(curScrWidth, curScrHeight);
                prevScrWidth = curScrWidth;
                prevScrHeight = curScrHeight;
            }
            ///////////// *********** SCREEN PREP 
            
            
            
            
            
            
            
            
            // **************  model view projection (mvp)
            glm::mat4 projection = player.camera.GetProjectionMatrix((float)curScrWidth / (float)curScrHeight, 0.1f);
            glm::mat4 view = player.camera.GetViewMatrix();
            glm::mat4 viewProj = projection * view;
            
            if (!appState.lockFrustum) {
                lockedCullMatrix = viewProj;
            }
            // **************  model view projection (mvp)
            
            
            
            
            
            
            
            
            ////////// ******************** Render Prep
            
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
            ////////// ******************** Render Prep
            
            
            
            ////// ****** block selection highlighter ****** /////
            BlockSelection::Get().Render(selectionShader, view, projection);
            ////// ****** block selection highlighter ****** /////
            
            
            
            //////////////////// ****************************** World Draw Call
            // Triggers: Cull(PrevDepth) -> MultiDrawIndirect, then HI-Z occlusion calc for next frame
            // see GPU_CULLING_RENDER_SYSTEM.md for render pipeline info
            world.Draw(worldShader, viewProj, prevViewProj, projection, 
                curScrWidth, curScrHeight, &depthDebug, f3DepthDebug, lockFrustum, player.position);
                //////////////////// ****************************** World Draw Call
                
                
                
                ///// ****************** DEBUG HELPERS BEFORE UI *************** /////////
                if (ChunkDebugger::Get().m_enabled)
                {
                    ChunkDebugger::Get().Update(world, player.camera.Position, player.camera.Front);
                    ChunkDebugger::Get().RenderGizmo(chunkDebugShader, viewProj);
                    ChunkDebugger::Get().DrawUI();
                }
                ///// ****************** DEBUG HELPERS *************** /////////
                

                



                ///////////// ********************  GUI Render
                gui.RenderUI(world, appState, player, globalConfig.VRAM_HEAP_ALLOCATION_MB);
                gui.EndFrame();
                ///////////// ********************  GUI Render
                




            /////// ******* swap to screen buffer
            glfwSwapBuffers(window);
            glfwPollEvents();
            /////// ******* swap to screen buffer

            ///////  Reprojection History for occlusion culling 
            prevViewProj = viewProj;
            ///////  Reprojection History for occlusion culling
            
        } ///// ************ GAME LOOP CLOSE SCOPE ************* 
    }
    catch (const std::exception& e) {
        std::cerr << "\n[FATAL CRASH]: " << e.what() << "\n" << std::endl;
        
        return -1;
    }
    
    // 6. Shutdown
    Engine::Profiler::Get().Shutdown();
    gui.Shutdown();
    glfwTerminate();
    return 0;
}