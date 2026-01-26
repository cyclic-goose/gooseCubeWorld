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
#include "screen_quad.h"
#include "splash_screen.hpp"

// --- IMAGE LOADER IMPLEMENTATION ---
// #define STB_IMAGE_IMPLEMENTATION
#include "texture_manager.h" // Includes stb_image.h internally

// --- CONFIGURATION ---
const unsigned int SCR_WIDTH = 1920;
const unsigned int SCR_HEIGHT = 1080;
const bool START_FULLSCREEN = false; 

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
bool f3DepthDebug = false;
int mipLevelDebug = 0;


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
    // TAB: Toggle Cursor Lock
    static bool tabPressed = false;
    if (glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS) {
        if (!tabPressed) {
            appState.isGameMode = !appState.isGameMode;
            tabPressed = true;
            if (appState.isGameMode) glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            else glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    } else { tabPressed = false; }
    
    // Click to capture cursor (only if not clicking on ImGui)
    if (!appState.isGameMode && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        if (!ImGui::GetIO().WantCaptureMouse) {
            appState.isGameMode = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    }
    
    // F2: Toggle Debug Window
    static bool f2Pressed = false;
    if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS) {
        if (!f2Pressed) {
            appState.showDebugPanel = !appState.showDebugPanel;
            f2Pressed = true;
        }
    } else { f2Pressed = false; }
    
    static bool f3Pressed = false;
    if (glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS) {
        if (!f3Pressed) {
            f3DepthDebug = !f3DepthDebug;
            f3Pressed = true;
        }
    } else { f3Pressed = false; }

    static bool f4Pressed = false;
    if (glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS) {
        if (!f4Pressed) {
            mipLevelDebug += 1;
            if (mipLevelDebug > 5) {mipLevelDebug = 0;}
            f4Pressed = true;
        }
    } else { f4Pressed = false; }
    
    // P: Toggle Profiler Window
    static bool pPressed = false;
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS) {
        if (!pPressed) {
            Engine::Profiler::Get().Toggle();
            pPressed = true;
        }
    } else { pPressed = false; }
    
    // M: Toggle World Gen Window
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
    
    // F: Freeze Frustum
    if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS) {
        if (!fKeyPressed) {
            //lockFrustum = !lockFrustum;
            appState.lockFrustum = !appState.lockFrustum;
            fKeyPressed = true;
            std::cout << "[DEBUG] Frustum Lock: " << (lockFrustum ? "ON" : "OFF") << std::endl;
        }
    } else { fKeyPressed = false; }

    // O: Toggle LOD Update Freeze (Observation Mode)
    static bool oPressed = false;
    if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS) {
        if (!oPressed) {
            bool current = world.GetLODFreeze();
            world.SetLODFreeze(!current);
            std::cout << "[DEBUG] LOD Freeze: " << (!current ? "ON" : "OFF") << std::endl;
            oPressed = true;
        }
    } else { oPressed = false; }
    
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
    
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    glEnable(GL_DEPTH_TEST);
    //glDepthFunc(GL_GEQUAL); 
    glDepthFunc(GL_GREATER);
    //glDepthFunc(GL_LEQUAL);
    glClearDepth(0.0f);     
    glEnable(GL_CULL_FACE); 
    glCullFace(GL_BACK);
    glClearColor(0.53f, 0.81f, 0.22f, 1.0f); 

    // Initial FBO sizing
    int dw, dh;
    glfwGetFramebufferSize(window, &dw, &dh);
    std::cout << dw << ", " << dh << std::endl;
    g_fbo.Resize(dw, dh);
    
    
    { 
        Shader worldShader("./resources/VERT_PRIMARY.glsl", "./resources/FRAG_PRIMARY.glsl");
        Shader depthDebug("./resources/debug_quad_vert.glsl", "./resources/debug_quad_frag.glsl");
        
        WorldConfig globalConfig;
        globalConfig.VRAM_HEAP_ALLOCATION_MB = 1024; ////////////// ****************** THIS DICTATES HOW MUCH MEMORY ALLOCATED TO VRAM
        RenderLoadingScreen(window, gui, globalConfig.VRAM_HEAP_ALLOCATION_MB);
        
        // We can call this the development debug LOD settings since the app will boot faster
        globalConfig.lodCount = 8;
        globalConfig.lodRadius[0] = 7;   
        globalConfig.lodRadius[1] = 7;  
        globalConfig.lodRadius[2] = 11;   
        globalConfig.lodRadius[3] = 13;   
        globalConfig.lodRadius[4] = 17;  
        globalConfig.lodRadius[5] = 17; 
        globalConfig.lodRadius[6] = 21; 
        globalConfig.lodRadius[7] = 25; 
        
        // Higher LOD in general more expensive
        // globalConfig.lodCount = 7;
        // // new radii
        // globalConfig.lodRadius[0] = 6;   
        // globalConfig.lodRadius[1] = 12;  
        // globalConfig.lodRadius[2] = 16;   
        // globalConfig.lodRadius[3] = 20;   
        // globalConfig.lodRadius[4] = 20;  
        // globalConfig.lodRadius[5] = 20; 
        // globalConfig.lodRadius[6] = 32; 
        //globalConfig.lodRadius[7] = 12; 


        World world(globalConfig);



        // --- LOAD TEXTURES ---
        // Your shader subtracts 1 from the ID (max(0, TexID - 1))
        // So BlockID 1 maps to Array Index 0.
        std::vector<std::string> texturePaths = {
            "resources/textures/dirt1.jpg",   // ID 1
            "resources/textures/dirt1.jpg",    // ID 2
            "resources/textures/dirt1.jpg",   // ID 3
            "resources/textures/dirt1.jpg",    // ID 4
            "resources/textures/dirt1.jpg"     // ID 5
        };

        // Ensure you have these images in resources/textures/ !
        GLuint texArray = TextureManager::LoadTextureArray(texturePaths);
        world.SetTextureArray(texArray);
        int CUR_SCR_WIDTH = SCR_WIDTH, CUR_SCR_HEIGHT = SCR_HEIGHT;
        int PREV_SCR_WIDTH = CUR_SCR_WIDTH, PREV_SCR_HEIGHT = CUR_SCR_HEIGHT;
        // Store Previous View Projection for Reprojection Culling
        glm::mat4 prevViewProj = glm::mat4(1.0f);

        while (!glfwWindowShouldClose(window)) {
            float currentFrame = (float)glfwGetTime();
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            // ********* PROFILER UPDATE ********* // 
            // Inside main loop, before rendering:
            Engine::Profiler::Get().Update();
            //Engine::Profiler::ScopedTimer timer("ENTIRE MAIN THREAD");
            // Inside your GUI rendering block:
            // ********* PROFILER UPDATE ********* // 
            
            processInput(window, world);
            world.Update(camera.Position);
            
            gui.BeginFrame();
            Engine::Profiler::Get().DrawUI(appState.isGameMode);


            // get new window render size

            // Initial FBO sizing
            
            glfwGetFramebufferSize(window, &CUR_SCR_WIDTH, &CUR_SCR_HEIGHT);
            //std::cout << dw << ", " << dh << std::endl;
            if (CUR_SCR_WIDTH != PREV_SCR_WIDTH || CUR_SCR_HEIGHT != PREV_SCR_HEIGHT)
            {
                // need to potentially resize some buffer, particularly the hi-z depth target buffer
                g_fbo.Resize(CUR_SCR_WIDTH, CUR_SCR_HEIGHT);
                PREV_SCR_WIDTH = CUR_SCR_WIDTH;
                PREV_SCR_HEIGHT = CUR_SCR_HEIGHT;
            }
            
            
            // Recalc mvp
            glm::mat4 projection = camera.GetProjectionMatrix((float)CUR_SCR_WIDTH / (float)CUR_SCR_HEIGHT, 0.1f);
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 viewProj = projection * view;
            
            // Handle Frustum Locking Logic
            if (!appState.lockFrustum) {
                lockedCullMatrix = viewProj;
            }
            
            glBindFramebuffer(GL_FRAMEBUFFER, g_fbo.fbo);
            glViewport(0, 0, CUR_SCR_WIDTH, CUR_SCR_HEIGHT);

            // ImGui enables GL_SCISSOR_TEST and often leaves it enabled with a small rect at the end of the frame.
            // If we don't disable it here, glClear and glBlitNamedFramebuffer will be clipped, resulting in a black screen.
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.53f, 0.81f, 0.91f, 1.0f); 
            // Important: Reverse-Z clears to 0.0f
            glClearDepth(0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);




            // This triggers:
            // 1. Cull(PrevDepth)
            // 2. MultiDrawIndirect
            world.Draw(worldShader, viewProj, lockedCullMatrix, prevViewProj, projection, g_fbo.hiZTex);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);


            //// **************** TODO: Move this to world.draw()
            if (world.getOcclusionCulling())
            {
              // occlusion cull, generate HiZ and run compute shader
              // no where near perfect right now but does incur a performance gain if needed
                if (!appState.lockFrustum)
                {
                    glCopyImageSubData(g_fbo.depthTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                                    g_fbo.hiZTex, GL_TEXTURE_2D, 0, 0, 0, 0,
                                    CUR_SCR_WIDTH, CUR_SCR_HEIGHT, 1);
                    
                    // Barrier: Ensure copy finishes before Compute Shader reads it
                    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        
                    // Step B: Downsample the rest of the pyramid
                    world.GetCuller()->GenerateHiZ(g_fbo.hiZTex, CUR_SCR_WIDTH, CUR_SCR_HEIGHT);
                }
            }
            //// **************** TODO: Move this to world.draw()

            


            if (!f3DepthDebug)
            {
                // Draw normal rendered view
                glBlitNamedFramebuffer(g_fbo.fbo, 0, 
                    0, 0, CUR_SCR_WIDTH, CUR_SCR_HEIGHT, 
                    0, 0, CUR_SCR_WIDTH, CUR_SCR_HEIGHT, 
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
                
            } else {
                // render depth view instead 
                world.RenderHiZDebug(&depthDebug, g_fbo.hiZTex, mipLevelDebug, CUR_SCR_WIDTH, CUR_SCR_HEIGHT);
            }




            // Render GUI
            gui.RenderUI(world, appState, camera, globalConfig.VRAM_HEAP_ALLOCATION_MB);
            gui.EndFrame();

            glfwSwapBuffers(window);
            glfwPollEvents();

            // Update Previous Frame Matrix for next frame
            if (!appState.lockFrustum) {
                prevViewProj = viewProj;
            }
        }
    }
    Engine::Profiler::Get().Shutdown();
    gui.Shutdown();
    glfwTerminate();
    return 0;
}