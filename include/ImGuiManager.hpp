#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <string>
#include <vector>
#include "world.h"

class ImGuiManager {
public:
    ImGuiManager() = default;
    
    // State to track if we are in "Game Mode" (Mouse Locked) or "Menu Mode"
    bool m_GameMode = true; 
    
    // Window State
    bool m_VSync = true; // Default to true (safe)

    // Disable copying
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    ~ImGuiManager() {
        Shutdown();
    }

    void Init(GLFWwindow* window, const char* glslVersion = "#version 460") {
        if (m_Initialized) return;
        
        m_Window = window; // Store window for resizing/vsync calls

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; 
        
        ImGui::StyleColorsDark();
        CustomizeStyle();
        
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init(glslVersion);
        
        // Initialize Cache for windowed restore
        glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);
        glfwGetWindowSize(m_Window, &m_WindowedW, &m_WindowedH);

        m_Initialized = true;
    }

    void BeginFrame() {
        if (!m_Initialized) return;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void EndFrame() {
        if (!m_Initialized) return;
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void Shutdown() {
        if (!m_Initialized) return;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_Initialized = false;
    }

    // --- WINDOW MANAGEMENT HELPERS ---

    bool IsFullscreen() {
        return glfwGetWindowMonitor(m_Window) != nullptr;
    }

    void SetFullscreen(bool enable) {
        if (IsFullscreen() == enable) return;

        if (enable) {
            // Save current windowed params before switching
            glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);
            glfwGetWindowSize(m_Window, &m_WindowedW, &m_WindowedH);

            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(m_Window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        } else {
            // Restore windowed params
            glfwSetWindowMonitor(m_Window, nullptr, m_WindowedX, m_WindowedY, m_WindowedW, m_WindowedH, 0);
        }
        
        // Re-apply VSync setting after monitor switch (context might reset)
        SetVSync(m_VSync);
    }

    void ToggleFullscreen() {
        SetFullscreen(!IsFullscreen());
    }

    void SetVSync(bool enable) {
        m_VSync = enable;
        glfwSwapInterval(m_VSync ? 1 : 0);
    }

    // ---------------------------------

    // Returns TRUE if "Exit" is clicked in the menu
    bool RenderStandardMenu() {
        bool exitClicked = false;
        
        // Don't show interactive menu if we are playing (Game Mode)
        if (m_GameMode) return false; 

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit", "Alt+F4")) exitClicked = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Overlay", NULL, &m_ShowOverlay);
                ImGui::MenuItem("Show Debug Stats", NULL, &m_ShowDebugPanel);
                ImGui::Separator();
                if (ImGui::MenuItem("Lock Mouse (Resume Game)", "TAB")) {
                    m_GameMode = true;
                }
                ImGui::EndMenu();
            }
            
            // --- NEW WINDOW MENU ---
            if (ImGui::BeginMenu("Window")) {
                // VSync Toggle
                if (ImGui::MenuItem("VSync (Cap FPS)", NULL, &m_VSync)) {
                    SetVSync(m_VSync);
                }

                // Fullscreen Toggle
                bool fs = IsFullscreen();
                if (ImGui::MenuItem("Fullscreen", "F11", &fs)) {
                    SetFullscreen(fs);
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
        
        if (m_ShowOverlay) RenderSimpleOverlay();
        return exitClicked;
    }

    void RenderDebugPanel(World& world) {
        if (!m_ShowDebugPanel) return;
        
        ImGuiWindowFlags flags = 0;
        if (m_GameMode) {
            flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMouseInputs;
            ImGui::SetNextWindowBgAlpha(0.3f); 
        } else {
            ImGui::SetNextWindowBgAlpha(0.9f); 
        }

        ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Engine Statistics", &m_ShowDebugPanel, flags)) {
            
            // --- INPUT TOGGLE BUTTON ---
            if (!m_GameMode) {
                if (ImGui::Button("RESUME GAME (Lock Mouse)", ImVec2(-1, 40))) {
                    m_GameMode = true;
                }
                ImGui::Separator();
            } else {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "[Press TAB to Unlock Mouse]");
                ImGui::Separator();
            }

            // 1. PERFORMANCE
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "Performance");
            ImGui::Separator();
            float framerate = ImGui::GetIO().Framerate;
            ImGui::Text("FPS: %.1f", framerate);
            ImGui::Text("Frame Time: %.3f ms", 1000.0f / framerate);
            
            // Show VSync Status
            ImGui::TextColored(m_VSync ? ImVec4(0,1,0,1) : ImVec4(1,0,0,1), 
                "VSync: %s", m_VSync ? "ON (Capped)" : "OFF (Uncapped)");

            // 2. MEMORY
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "GPU Memory (Best Fit)");
            ImGui::Separator();
            
            size_t used = world.m_gpuMemory->GetUsedMemory();
            size_t total = world.m_gpuMemory->GetTotalMemory();
            size_t freeBlocks = world.m_gpuMemory->GetFreeBlockCount();
            
            float usedMB = used / (1024.0f * 1024.0f);
            float totalMB = total / (1024.0f * 1024.0f);
            float percent = (float)used / (float)total;

            ImGui::Text("VRAM: %.2f / %.2f MB", usedMB, totalMB);
            ImGui::ProgressBar(percent, ImVec2(-1.0f, 0.0f));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fragment Count: %zu", freeBlocks);
            ImGui::Text("Free Blocks (Frag): %zu", freeBlocks);

            // 3. GEOMETRY
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "World / Geometry");
            ImGui::Separator();

            size_t totalChunks = world.m_chunks.size();
            size_t activeChunks = 0;
            size_t totalVertices = 0;
            
            for (const auto& pair : world.m_chunks) {
                ChunkNode* node = pair.second;
                if (node->state == ChunkState::ACTIVE) {
                    activeChunks++;
                    totalVertices += node->vertexCount;
                }
            }
            ImGui::Text("Chunks: %zu (Active: %zu)", totalChunks, activeChunks);
            ImGui::Text("Vertices: %s", FormatNumber(totalVertices).c_str());

            // 4. THREADING
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "Threading & Generation");
            ImGui::Separator();

            size_t threads = world.m_pool.GetWorkerCount();
            size_t tasksPending = world.m_pool.GetQueueSize();
            size_t genQueue = world.m_generatedQueue.size(); 
            size_t meshQueue = world.m_meshedQueue.size();

            ImGui::Text("Active Threads: %zu", threads);
            ImGui::Text("ThreadPool Tasks: %zu", tasksPending);
            ImGui::Text("Generate Queue: %zu", genQueue);
            ImGui::Text("Upload Queue: %zu", meshQueue);

            // 5. RENDER DISTANCE
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "Render Distance");
            ImGui::Separator();

            int maxDistBlocks = 0;
            for(int i = 0; i < world.m_config.lodCount; i++) {
                int scale = 1 << i;
                int dist = world.m_config.lodRadius[i] * CHUNK_SIZE * scale;
                if(dist > maxDistBlocks) maxDistBlocks = dist;
            }
            float km = (float)maxDistBlocks / 1000.0f;
            int effectiveChunks = maxDistBlocks / CHUNK_SIZE;

            ImGui::Text("Effective Range: %.2f km", km);
            ImGui::Text("Max Radius: %d chunks (%d blocks)", effectiveChunks, maxDistBlocks);
            
            // 6. CONTROL
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "Control");
            ImGui::Separator();
            bool caves = world.m_config.enableCaves;
            if (ImGui::Checkbox("Enable Caves (Needs Reload)", &caves)) {
                // Note: Actual modification needs world setter access, keeping visual for now
            }
        }
        ImGui::End();
    }

    void RenderSimpleOverlay() {
        if (!m_ShowOverlay) return;
        const float PAD = 10.0f;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 work_pos = viewport->WorkPos; 
        ImVec2 work_size = viewport->WorkSize;
        ImVec2 window_pos, window_pos_pivot;
        
        window_pos.x = work_pos.x + work_size.x - PAD;
        window_pos.y = work_pos.y + PAD;
        window_pos_pivot.x = 1.0f;
        window_pos_pivot.y = 0.0f;

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
        
        ImGui::SetNextWindowBgAlpha(0.35f); 
        if (ImGui::Begin("StatsOverlay", &m_ShowOverlay, window_flags)) {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("%s", m_GameMode ? "[GAME MODE]" : "[MENU MODE]");
        }
        ImGui::End();
    }

private:
    bool m_Initialized = false;
    bool m_ShowOverlay = true;
    bool m_ShowDebugPanel = true;
    
    // Internal Window state for restoration
    GLFWwindow* m_Window = nullptr;
    int m_WindowedX = 0, m_WindowedY = 0, m_WindowedW = 1280, m_WindowedH = 720;

    std::string FormatNumber(size_t n) {
        std::string s = std::to_string(n);
        int insertPosition = s.length() - 3;
        while (insertPosition > 0) {
            s.insert(insertPosition, ",");
            insertPosition -= 3;
        }
        return s;
    }

    void CustomizeStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 5.0f;
    }
};