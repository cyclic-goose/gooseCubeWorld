#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <string>
#include <vector>
#include "world.h"
#include "camera.h"
#include "profiler.h"

// --- CONFIGURATION STRUCT ---
struct UIConfig {
    bool showDebugPanel = true;    // F2 to toggle
    bool showWorldSettings = false; // M to toggle
    bool showOverlay = true;        
    bool showWireframe = false;
    bool vsync = false;
    bool lockFrustum = false;
    float FPS_OVERLAY_FONT_SCALE = 1.35f;
    float DEBUG_FONT_SCALE = 1.4f;
    
    // Input State
    bool isGameMode = true;         // TAB to toggle (Mouse Lock)

    // Current settings being tweaked in the GUI
    WorldConfig editConfig;         
    bool editConfigInitialized = false;
};

class ImGuiManager {
public:
    ImGuiManager() = default;
    
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    ~ImGuiManager() { Shutdown(); }

    void Init(GLFWwindow* window, const char* glslVersion = "#version 460") {
        if (m_Initialized) return;
        m_Window = window;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; 
        
        ImGui::StyleColorsDark();
        CustomizeStyle();
        
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init(glslVersion);
        
        glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);
        glfwGetWindowSize(m_Window, &m_WindowedW, &m_WindowedH);

        glfwSwapInterval(0);

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

    void RenderUI(World& world, UIConfig& config, const Camera& camera, const float VRAM_HEAP_SIZE_MB) {
        Engine::Profiler::ScopedTimer timer("ImGui::Render");
        if (!config.editConfigInitialized) {
            config.editConfig = world.GetConfig();
            config.editConfigInitialized = true;
        }

        static bool lastVsync = config.vsync;
        if (config.vsync != lastVsync) {
            glfwSwapInterval(config.vsync ? 1 : 0);
            lastVsync = config.vsync;
        }

        // Always render the minimal overlay
        if (config.showOverlay) RenderSimpleOverlay(config, camera);

        // // Hide menus and windows if we are in Game Mode (Locked Mouse)
        // if (!config.isGameMode) {
        //     if (config.showDebugPanel) RenderDebugPanel(world, config);
        //     if (config.showWorldSettings) RenderWorldSettings(world, config);
        //     RenderMenuBar(config);
        // }
        if (config.showDebugPanel) RenderDebugPanel(world, config, VRAM_HEAP_SIZE_MB);
        if (config.showWorldSettings) RenderWorldSettings(world, config);
    }

    void ToggleFullscreen() {
        if (IsFullscreen()) {
            glfwSetWindowMonitor(m_Window, nullptr, m_WindowedX, m_WindowedY, m_WindowedW, m_WindowedH, 0);
        } else {
            glfwGetWindowPos(m_Window, &m_WindowedX, &m_WindowedY);
            glfwGetWindowSize(m_Window, &m_WindowedW, &m_WindowedH);
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(m_Window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
    }

private:
    bool m_Initialized = false;
    GLFWwindow* m_Window = nullptr;
    int m_WindowedX = 0, m_WindowedY = 0, m_WindowedW = 1280, m_WindowedH = 720;

    bool IsFullscreen() { return glfwGetWindowMonitor(m_Window) != nullptr; }

    std::string FormatNumber(size_t n) {
        std::string s = std::to_string(n);
        int insertPosition = (int)s.length() - 3;
        while (insertPosition > 0) {
            s.insert(insertPosition, ",");
            insertPosition -= 3;
        }
        return s;
    }

    void CustomizeStyle() {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 5.0f;
        style.FrameRounding = 3.0f;
        style.WindowPadding = ImVec2(10, 10);
    }

    void RenderSimpleOverlay(const UIConfig& config, const Camera& camera) {
        const float PAD = 10.0f;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 window_pos = ImVec2(viewport->WorkPos.x + PAD, viewport->WorkPos.y + PAD);

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.35f); 
        
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("StatsOverlay", nullptr, flags)) {
            ImGui::SetWindowFontScale(config.FPS_OVERLAY_FONT_SCALE);
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s", "[TAB] Mouse Lock/Unlock | [F2] Debug Menu\n [M] World Settings    | [P] Profiler");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s", config.isGameMode ? "[MOUSE LOCKED]" : "[MOUSE UNLOCKED]");
            ImGui::Separator();
            ImGui::Text("XYZ: %.1f, %.1f, %.1f", camera.Position.x, camera.Position.y, camera.Position.z);
            ImGui::Text("Angle: Y:%.1f P:%.1f", camera.Yaw, camera.Pitch);
        }
        ImGui::End();
    }

    void RenderDebugPanel(World& world, UIConfig& config, const float VRAM_HEAP_SIZE_MB) {
        ImGuiWindowFlags flags = 0;
        if (config.isGameMode) {
            flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMouseInputs;
            ImGui::SetNextWindowBgAlpha(0.75f); // Faintly visible while playing
        } else {
            ImGui::SetNextWindowBgAlpha(0.85f); 
        }

        ImGui::SetNextWindowPos(ImVec2(14,184), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(400,600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Engine Debug (F2)", &config.showDebugPanel, flags)) {
            ImGui::SetWindowFontScale(config.DEBUG_FONT_SCALE);
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "PERFORMANCE");
            ImGui::Separator();
            float fps = ImGui::GetIO().Framerate;
            ImGui::Text("FPS: %.1f", fps);
            ImGui::Text("Frame Time: %.3f ms", 1000.0f / fps);
            ImGui::Checkbox("VSync", &config.vsync);
            
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "GPU MEMORY");
            ImGui::Separator();
            size_t used = world.m_gpuMemory->GetUsedMemory();
            size_t total = world.m_gpuMemory->GetTotalMemory();
            float usedMB = (used / 1024.0f / 1024.0f);
            float totalMB = VRAM_HEAP_SIZE_MB;
            float ratio = (float)used / (float)total;
            
            ImGui::Text("VRAM: %.1f / %.1f MB", usedMB, totalMB);
            ImGui::ProgressBar(ratio, ImVec2(-1.0f, 15.0f));
            ImGui::Text("Fragmentation: %zu free blocks", world.m_gpuMemory->GetFreeBlockCount());

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "WORLD GEOMETRY");
            ImGui::Separator();
            
            size_t activeChunks = 0;
            size_t totalVertices = 0;
            for (const auto& pair : world.m_chunks) {
                if (pair.second->state == ChunkState::ACTIVE) {
                    activeChunks++;
                    totalVertices += pair.second->vertexCount;
                }
            }
            
            ImGui::Text("Active Chunks: %zu", activeChunks);
            ImGui::Text("Resident Vertices: %s", FormatNumber(totalVertices).c_str());
            //ImGui::TextColored(ImVec4(0,1,0,1), "Drawn Vertices:    %s", FormatNumber(world.m_drawnVertices).c_str());

            // size_t culledChunks = 0;
            // if (activeChunks > world.m_drawnChunks) {
            //     culledChunks = activeChunks - world.m_drawnChunks;
            // }
            // ImGui::Text("Drawn Chunks:  %s", FormatNumber(world.m_drawnChunks).c_str());
            //ImGui::Text("Culled Chunks: %s", FormatNumber(culledChunks).c_str());
            
            if (ImGui::Checkbox("Wireframe Mode", &config.showWireframe)) {
                glPolygonMode(GL_FRONT_AND_BACK, config.showWireframe ? GL_LINE : GL_FILL);
            }
            ImGui::Checkbox("Lock Frustum (F)", &config.lockFrustum);
            if (config.lockFrustum) ImGui::TextColored(ImVec4(1,0,0,1), "FRUSTUM LOCKED");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "THREADING");
            ImGui::Separator();
            ImGui::Text("Pool Tasks: %zu", world.m_pool.GetQueueSize());
            
            ImGui::End();
        }
    }

    void RenderWorldSettings(World& world, UIConfig& config) {


        ImGuiWindowFlags flags = 0;
        if (config.isGameMode) {
            flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMouseInputs;
            ImGui::SetNextWindowBgAlpha(0.75f);
        }
        ImGui::SetNextWindowPos(ImVec2(16,801), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(593,550), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("World Generation (M)", &config.showWorldSettings)) {
            ImGui::SetWindowFontScale(config.DEBUG_FONT_SCALE);
            if (ImGui::CollapsingHeader("Terrain Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragInt("Seed", &config.editConfig.seed);
                ImGui::SliderFloat("Noise Scale", &config.editConfig.scale, 0.001f, 0.1f);
                ImGui::SliderFloat("Hill Amp", &config.editConfig.hillAmplitude, 0.0f, 500.0f);
                ImGui::SliderFloat("Hill Freq", &config.editConfig.hillFrequency, 0.05f, 10.0f);
                ImGui::SliderFloat("Mountain Amp", &config.editConfig.mountainAmplitude, 0.0f, 8000.0f);
                ImGui::SliderFloat("Mountain Freq", &config.editConfig.mountainFrequency, 0.01f, 0.2f);
                ImGui::SliderInt("Sea Level", &config.editConfig.seaLevel, 0, 500);
            }

            if (ImGui::CollapsingHeader("World Dimensions")) {
                ImGui::SliderInt("Height (Chunks)", &config.editConfig.worldHeightChunks, 8, 128);
                ImGui::TextColored(ImVec4(0.7,0.7,0.7,1), "Note: Height changes require full reload.");
            }

            if (ImGui::CollapsingHeader("LOD Settings")) {
                ImGui::SliderInt("LOD Count", &config.editConfig.lodCount, 1, 8);
                for (int i = 0; i < config.editConfig.lodCount; i++) {
                    std::string label = "LOD " + std::to_string(i) + " Radius";
                    ImGui::SliderInt(label.c_str(), &config.editConfig.lodRadius[i], 1, 64);
                }
            }

            if (ImGui::CollapsingHeader("Caves")) {
                ImGui::Checkbox("Enable Caves", &config.editConfig.enableCaves);
                if (config.editConfig.enableCaves) {
                    ImGui::SliderFloat("Threshold", &config.editConfig.caveThreshold, 0.0f, 1.0f);
                }
            }
            
            ImGui::Separator();
            if (ImGui::Button("REGENERATE WORLD (R)", ImVec2(-1, 40))) {
                world.Reload(config.editConfig);
            }
            ImGui::End();
        }
    }

    void RenderMenuBar(UIConfig& config) {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Windows")) {
                ImGui::MenuItem("Debug Stats (F2)", "F2", &config.showDebugPanel);
                ImGui::MenuItem("World Generation (M)", "M", &config.showWorldSettings);
                ImGui::MenuItem("HUD Overlay", nullptr, &config.showOverlay);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }
};