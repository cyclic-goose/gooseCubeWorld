#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include "ImGuiManager.hpp"

// a quick one off "splash screen" so the player doesnt think the computer just froze (although it is kind of, its allocating vram)
void RenderLoadingScreen(GLFWwindow* window, ImGuiManager &gui, float HEAP_SIZE_FOR_DISPLAYING) {
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
        ImGui::Text("Reserving Memory...");
        ImGui::Text("Allocating %.1f MB VRAM...", HEAP_SIZE_FOR_DISPLAYING);
        ImGui::Text("Spooling Threadpool...");
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
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "v0.2.6-alpha"); 
        ImGui::End();
        ImGui::End();
        
        gui.EndFrame();
        
        // Swap and Poll
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    // Force synchronization before hanging the CPU
    glFinish(); 
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}