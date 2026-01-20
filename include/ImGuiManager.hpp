#pragma once

// Make sure these are included in your project structure
// or adjust paths accordingly.
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <string>
#include <vector>
#include <numeric>

// Include World to access engine internals
#include "world.h"

// A single-header drop-in class for ImGui management with GLAD/GLFW/OpenGL 4.6
class ImGuiManager {
public:
    ImGuiManager() = default;
    
    // Disable copying to prevent double-freeing contexts
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    ~ImGuiManager() {
        Shutdown();
    }

    // Initialize ImGui, GLFW and OpenGL3 backends
    void Init(GLFWwindow* window, const char* glslVersion = "#version 460") {
        if (m_Initialized) return;

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        CustomizeStyle();

        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init(glslVersion);

        m_Initialized = true;
    }

    // Call this at the start of your render loop (before clearing buffers usually)
    void BeginFrame() {
        if (!m_Initialized) return;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    // Call this at the end of your render loop (before glfwSwapBuffers)
    void EndFrame() {
        if (!m_Initialized) return;

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // Explicit cleanup if needed before destructor
    void Shutdown() {
        if (!m_Initialized) return;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_Initialized = false;
    }

    // -------------------------------------------------------------------------
    // HELPERS
    // -------------------------------------------------------------------------

    // Draws a standard simple menu bar
    // Returns true if "Exit" was clicked
    bool RenderStandardMenu() {
        bool exitClicked = false;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    exitClicked = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Overlay", NULL, &m_ShowOverlay);
                ImGui::MenuItem("Show Debug Stats", NULL, &m_ShowDebugPanel);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        
        if (m_ShowOverlay) RenderSimpleOverlay();
        
        return exitClicked;
    }

    // The main Debug Panel for your Voxel Engine
    void RenderDebugPanel(World& world) {
        if (!m_ShowDebugPanel) return;

        ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Engine Statistics", &m_ShowDebugPanel)) {
            
            // 1. Framerate / Timing
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "Performance");
            ImGui::Separator();
            float framerate = ImGui::GetIO().Framerate;
            ImGui::Text("FPS: %.1f", framerate);
            ImGui::Text("Frame Time: %.3f ms", 1000.0f / framerate);

            // 2. Memory Usage (VRAM)
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "GPU Memory");
            ImGui::Separator();
            
            size_t used = world.m_gpuMemory->GetUsedMemory();
            size_t total = world.m_gpuMemory->GetTotalMemory();
            size_t freeBlocks = world.m_gpuMemory->GetFreeBlockCount();
            
            float usedMB = used / (1024.0f * 1024.0f);
            float totalMB = total / (1024.0f * 1024.0f);
            float percent = (float)used / (float)total;

            ImGui::Text("VRAM Usage: %.2f MB / %.2f MB", usedMB, totalMB);
            ImGui::ProgressBar(percent, ImVec2(-1.0f, 0.0f));
            ImGui::Text("Fragmentation (Free Blocks): %zu", freeBlocks);

            // 3. World Stats (Heavy calculation, maybe throttle this if N > 100k)
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "World / Geometry");
            ImGui::Separator();

            // Calculate totals (Iterate map)
            // Note: In a massive world, you might want to cache these values and update every 0.5s
            size_t totalChunks = world.m_chunks.size();
            size_t activeChunks = 0;
            size_t totalVertices = 0;
            
            // Limit iteration for UI responsiveness if you have > 100k chunks
            // For now, simple iteration is usually fine for < 10k chunks
            for (const auto& pair : world.m_chunks) {
                ChunkNode* node = pair.second;
                if (node->state == ChunkState::ACTIVE) {
                    activeChunks++;
                    totalVertices += node->vertexCount;
                }
            }
            
            size_t triangles = totalVertices / 3; // Assuming GL_TRIANGLES

            ImGui::Text("Total Chunks (Managed): %zu", totalChunks);
            ImGui::Text("Active/Renderable Chunks: %zu", activeChunks);
            ImGui::Text("Total Vertices: %s", FormatNumber(totalVertices).c_str());
            ImGui::Text("Total Triangles: %s", FormatNumber(triangles).c_str());

            // 4. Threading / Queues
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "Threading & Generation");
            ImGui::Separator();

            size_t threads = world.m_pool.GetWorkerCount();
            size_t tasksPending = world.m_pool.GetQueueSize();
            
            // Access queues strictly under lock if we were modifying, but size() is usually safe-ish for display
            // or we rely on the specific implementation. 
            // World::m_generatedQueue and m_meshedQueue are std::queue, size() is not atomic but mostly fine for debug UI.
            size_t genQueue = world.m_generatedQueue.size(); 
            size_t meshQueue = world.m_meshedQueue.size();

            ImGui::Text("Active Threads: %zu", threads);
            ImGui::Text("ThreadPool Tasks: %zu", tasksPending);
            ImGui::Text("Generate Queue: %zu", genQueue);
            ImGui::Text("Upload Queue: %zu", meshQueue);

            // 5. Config (Optional Toggles)
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "Control");
            ImGui::Separator();
            
            // Example: toggle caves if you make m_config accessible or add setters
            bool caves = world.m_config.enableCaves;
            if (ImGui::Checkbox("Enable Caves (Requires Reload)", &caves)) {
                world.m_config.enableCaves = caves;
                // Ideally trigger a reload here if safe
            }
        }
        ImGui::End();
    }

    // Efficient, corner-anchored overlay for stats (FPS, etc.)
    void RenderSimpleOverlay() {
        const float PAD = 10.0f;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 work_pos = viewport->WorkPos; // Use WorkArea to avoid menu-bar/task-bar
        ImVec2 work_size = viewport->WorkSize;
        ImVec2 window_pos, window_pos_pivot;
        
        // Top-right corner
        window_pos.x = work_pos.x + work_size.x - PAD;
        window_pos.y = work_pos.y + PAD;
        window_pos_pivot.x = 1.0f;
        window_pos_pivot.y = 0.0f;

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        
        // Transparent background, no inputs, always on top
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | 
                                      ImGuiWindowFlags_AlwaysAutoResize | 
                                      ImGuiWindowFlags_NoSavedSettings | 
                                      ImGuiWindowFlags_NoFocusOnAppearing | 
                                      ImGuiWindowFlags_NoNav | 
                                      ImGuiWindowFlags_NoMove;
        
        ImGui::SetNextWindowBgAlpha(0.35f); 

        if (ImGui::Begin("StatsOverlay", &m_ShowOverlay, window_flags)) {
            ImGui::Text("Stats Overlay");
            ImGui::Separator();
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        }
        ImGui::End();
    }

    bool& ShowOverlayState() { return m_ShowOverlay; }
    bool& ShowDebugPanelState() { return m_ShowDebugPanel; }

private:
    bool m_Initialized = false;
    bool m_ShowOverlay = true;
    bool m_ShowDebugPanel = true;

    // Helper to format large numbers with commas (e.g. 1,000,000)
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
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 12.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;
    }
};