#pragma once

// Make sure these are included in your project structure
// or adjust paths accordingly.
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <string>

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
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        
        if (m_ShowOverlay) {
            RenderSimpleOverlay();
        }

        return exitClicked;
    }

    // Efficient, corner-anchored overlay for stats (FPS, etc.)
    // Doesn't take input focus, purely informational
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
        // Removed SetNextWindowViewport to ensure compatibility with standard ImGui branches
        
        // Transparent background, no inputs, always on top
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | 
                                      ImGuiWindowFlags_AlwaysAutoResize | 
                                      ImGuiWindowFlags_NoSavedSettings | 
                                      ImGuiWindowFlags_NoFocusOnAppearing | 
                                      ImGuiWindowFlags_NoNav | 
                                      ImGuiWindowFlags_NoMove;
        
        // Set semi-transparent background
        ImGui::SetNextWindowBgAlpha(0.35f); 

        if (ImGui::Begin("StatsOverlay", &m_ShowOverlay, window_flags)) {
            ImGui::Text("Stats Overlay");
            ImGui::Separator();
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Ms/Frame: %.3f", 1000.0f / ImGui::GetIO().Framerate);
            
            // Example of adding custom data
            if (ImGui::IsMousePosValid())
                ImGui::Text("Mouse Position: (%.1f,%.1f)", ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
            else
                ImGui::Text("Mouse Position: <invalid>");
        }
        ImGui::End();
    }

    // Toggle state for external control
    bool& ShowOverlayState() { return m_ShowOverlay; }

private:
    bool m_Initialized = false;
    bool m_ShowOverlay = true;

    // Optional: Make the UI look slightly more modern/flat
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