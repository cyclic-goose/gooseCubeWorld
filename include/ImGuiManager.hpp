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
#include "terrain/terrain_system.h"
#include "terrain/terrain_selector_ImGuiExpose.h"
#include "playerController.h"
#include "gui_utils.h"
#include "crosshair.h"

// ================================================================================================
// UI CONFIGURATION
// ================================================================================================

struct UIConfig {
    // --- Master Toggles ---
    bool showDebugPanel = false;    // F2: Controls Stats, Camera, Culler
    bool showGameControls = false;  // ESC/Pause: Basic settings
    bool showWorldSettings = false; // M: World Generation
    bool showOverlay = true;        // HUD
    bool showWireframe = false;
    bool showTerrainGui = false;
    
    // --- Sub-window Toggles (Managed by F2 master switch usually) ---
    bool showCameraControls = true;
    bool showCullerControls = true;

    // --- Settings ---
    bool vsync = true;
    bool lockFrustum = false;
    float FPS_OVERLAY_FONT_SCALE = 1.35f;
    float DEBUG_FONT_SCALE = 1.4f;
    float menuFontScale = 1.8f;     // Default increased from 1.8f to 2.2f

    // --- State ---
    bool isGameMode = true;         // TAB: Mouse Lock toggle
    bool editConfigInitialized = false;
    bool crossHairEnabled = true;
    
    // --- World Edit State ---
    std::unique_ptr<EngineConfig> editConfig;        
    int currentLODPreset = 1;       // 0=Low, 1=Med, 2=High, 3=Extreme
};

// ================================================================================================
// IMGUI MANAGER CLASS
// ================================================================================================

class ImGuiManager {
public:
    // --------------------------------------------------------------------------------------------
    // LIFECYCLE
    // --------------------------------------------------------------------------------------------
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

        // Ensure vsync starts off if needed, though usually controlled by window creation
        glfwSwapInterval(0);

        m_Initialized = true;
    }

    void Shutdown() {
        if (!m_Initialized) return;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_Initialized = false;
    }

    // --------------------------------------------------------------------------------------------
    // FRAME MANAGEMENT
    // --------------------------------------------------------------------------------------------

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

    // --------------------------------------------------------------------------------------------
    // MAIN RENDER INTERFACE
    // --------------------------------------------------------------------------------------------

    // now takes in player which holds its camera 
    void RenderUI(World& world, UIConfig& config, Player& player, const float VRAM_HEAP_SIZE_MB) {
        Engine::Profiler::ScopedTimer timer("ImGui::Render");
        
        // One-time init for edit config
        if (!config.editConfig) {
            // Copy the current world config into our editable pointer
            config.editConfig = std::make_unique<EngineConfig>(world.GetConfig());
        }

        // Handle VSync State
        static bool lastVsync = config.vsync;
        if (config.vsync != lastVsync) {
            glfwSwapInterval(config.vsync ? 1 : 0);
            lastVsync = config.vsync;
        }

        // Overlay (Always on top/visible)
        if (config.showOverlay) RenderSimpleOverlay(config, player);

        if (config.showGameControls) RenderGameControls(world, config, player);

        if (config.showTerrainGui) {RenderTerrainControls(world, config);}

        // Debug Suite (F2)
        if (config.showDebugPanel) {
            RenderDebugPanel(world, config, VRAM_HEAP_SIZE_MB); // Top Left
            //RenderCameraControls(player, config);               // Top Right
            RenderCullerControls(world, config);                // Bottom Right
        }

        if (config.crossHairEnabled)
            Crosshair::Get().Draw();

        // World Generation (M)
        //if (config.showWorldSettings) RenderWorldSettings(world, config);
        
        // RenderMenuBar(config); // Optional: Currently disabled
    }

    // --------------------------------------------------------------------------------------------
    // WINDOW HELPERS
    // --------------------------------------------------------------------------------------------

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
    // --------------------------------------------------------------------------------------------
    // INTERNAL STATE
    // --------------------------------------------------------------------------------------------
    bool m_Initialized = false;
    GLFWwindow* m_Window = nullptr;
    int m_WindowedX = 0, m_WindowedY = 0;
    int m_WindowedW = 1280, m_WindowedH = 720;

    // --------------------------------------------------------------------------------------------
    // INTERNAL UTILS
    // --------------------------------------------------------------------------------------------

    bool IsFullscreen() const { return glfwGetWindowMonitor(m_Window) != nullptr; }

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

    // --------------------------------------------------------------------------------------------
    // RENDER WIDGETS
    // --------------------------------------------------------------------------------------------

    void RenderGameControls(World& world, UIConfig& config, Player& player) {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse; 
        // Note: We do NOT set NoInputs here because this is the Pause Menu
        ImGui::SetNextWindowBgAlpha(0.95f); 

        // Center the window
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("PAUSE MENU", nullptr, flags)) {
            ImGui::SetWindowFontScale(config.menuFontScale); 

            float footerHeight = 70.0f;
            float availableHeight = ImGui::GetContentRegionAvail().y - footerHeight;

            // Begin Child region for Tabs
            if (ImGui::BeginChild("MenuTabs", ImVec2(0, availableHeight), false)) {
                ImGui::SetWindowFontScale(config.menuFontScale);

                if (ImGui::BeginTabBar("PauseMenuTabs")) {
                    


                    
                    
                    // 
                    if (ImGui::BeginTabItem("Engine")) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "World Settings");
                        ImGui::Separator();
                        
                        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
                        ImGui::TextDisabled("The LOD system renders distant terrain at lower resolutions. "
                            "Adding more LOD levels exponentially increases view distance but consumes VRAM. ONE CHUNK = 32x32x32 Blocks");
                            ImGui::PopTextWrapPos();
                            ImGui::Spacing();
                            
                            // --- LOD Density Presets ---
                            ImGui::Text("Render Distance Preset");
                            bool presetChanged = false;
                            
                            if (!world.IsBusy()) {
                                
                                // Helper lambda for radio buttons with tooltips
                                auto RadioWithTooltip = [&](const char* label, int v, const char* tip) {
                                    if (ImGui::RadioButton(label, &config.currentLODPreset, v)) presetChanged = true;
                                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                                    ImGui::SameLine();
                                };
                                
                                RadioWithTooltip("Very Low", 0, "For low performing PCs");
                                RadioWithTooltip("Standard", 1, "Balanced");
                                RadioWithTooltip("High", 2, "Good view range, reasonable VRAM");
                                RadioWithTooltip("Ultra", 3, "High Rasterization Cost");
                                // Remove SameLine for the last one to wrap if needed, or keep it
                                if (ImGui::RadioButton("Extreme", &config.currentLODPreset, 4)) presetChanged = true;
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("FOR SUPERCOMPUTERS (If you think you qualify, you probably still don't)");
                            }
                            
                            if (presetChanged) {
                                struct LODPreset {
                                    int activeCount;
                                    std::vector<int> radii;
                                };
                                
                                // OPTIMIZATION: Static const to prevent re-allocation every frame
                                static const std::vector<LODPreset> presets = {
                                    { 4, { 9, 9, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0 } },                        // Low
                                    { 5, { 15, 15, 15, 15, 7, 0, 0, 0 , 0, 0, 0, 0} },                   // Standard
                                    { 6, { 17, 17, 17, 17, 17, 11, 0, 0, 0, 0, 0, 0 } },                  // Medium
                                    { 7, { 21, 21, 21, 21, 21, 21, 21, 0, 0, 0, 0, 0 } },                 // High
                                    { 9, { 25, 23, 21, 21, 21, 21, 21, 21, 21, 0, 0, 0 } }                // Extreme
                                };
                                
                                if (config.currentLODPreset >= 0 && config.currentLODPreset < (int)presets.size()) {
                                    const auto& selected = presets[config.currentLODPreset];
                                    config.editConfig->settings.lodCount = selected.activeCount;
                                    for(int i = 0; i < 12; i++) {
                                        if(i < (int)selected.radii.size()) 
                                        config.editConfig->settings.lodRadius[i] = selected.radii[i];
                                    }
                                    world.ReloadWorld(*config.editConfig);
                                }
                        }
                        
                        ImGui::Spacing();
                        
                        // --- Effective Distance Calculation ---
                        int currentLODs = config.editConfig->settings.lodCount;
                        int lastLODIndex = std::clamp(currentLODs - 1, 0, 11);
                        int radius = config.editConfig->settings.lodRadius[lastLODIndex];
                        int scale = 1 << lastLODIndex;
                        int effectiveDistChunks = radius * scale;
                        
                        ImGui::Text("Effective Render Distance:");
                        ImGui::SameLine();
                        if (effectiveDistChunks == 0) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid (Radius 0)");
                        } else {
                            ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%d Chunks", effectiveDistChunks);
                        }
                        
                        if (!world.IsBusy())
                        {
                            
                            if (ImGui::SliderInt("##lodslider", &currentLODs, 1, 12, "LOD Level: %d")) {
                                config.editConfig->settings.lodCount = currentLODs;
                            }
                            
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Level 1-6: Standard Playable Area\nLevel 7-9: Far Horizon\nLevel 10+: Extreme Distance");
                            
                            if (ImGui::IsItemDeactivatedAfterEdit()) {
                                world.ReloadWorld(*config.editConfig);
                            }
                        }
                        
                        ImGui::Spacing();
                        
                        // --- Advanced Manual Tuning ---
                        if (ImGui::TreeNodeEx("Advanced LOD Tuning", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::TextDisabled("Adjust the radius (in chunks) for each detail ring.");
                            ImGui::Spacing();
                            
                            // check if the world threads are busy before allowing LOD tuning or else
                            // program can crash hard
                            if (!world.IsBusy())
                            {
                                
                                for (int i = 0; i < config.editConfig->settings.lodCount; i++) {
                                    int currentScale = 1 << i;
                                    ImGui::Text("LOD %d (1:%dx Scale)", i, currentScale);
                                    ImGui::SameLine();
                                    std::string sliderLabel = "##lodradius" + std::to_string(i);
                                    ImGui::SliderInt(sliderLabel.c_str(), &config.editConfig->settings.lodRadius[i], 2, (int)(32.0));
                                    if (i == 0) {
                                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Each chunk (32^3) that isnt uniform (AIR) in LOD 0 uses ~40KB of ram.\n IDs are saved in ram on LOD 0 to perform physics calculations.");
                                     }
                                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                                        world.ReloadWorld(*config.editConfig);
                                    }
                                }
                            }
                            ImGui::TreePop();
                        }
                        
                        ImGui::Spacing();
                        ImGui::Separator();
                        if (ImGui::Button("Reset World State", ImVec2(-1, 40))) {
                            world.ReloadWorld(*config.editConfig);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("SPAMMING THIS CAN CAUSE VRAM CRASH. ALLOW WORLD TO GENERATE");
                        ImGui::EndTabItem();
                    }
                    

                    // TAB PLAYER
                    if (ImGui::BeginTabItem("Player")) {
                        
                        player.DrawInterface(); // call player internal gui exposure

                        ImGui::EndTabItem();
                    }


                    // --- TAB: GRAPHICS ---
                    if (ImGui::BeginTabItem("Graphics")) {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Display Options");
                        ImGui::Separator();
                        
                        bool isFs = IsFullscreen();
                        if (ImGui::Checkbox("Fullscreen Mode", &isFs)) {
                            ToggleFullscreen();
                        }
                        
                        ImGui::Checkbox("VSync", &config.vsync);
                        
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Visuals");
                        ImGui::Separator();
                        ImGui::Checkbox("Wireframe Mode", &config.showWireframe);
                        if (config.showWireframe) {
                            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                        } else {
                            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                        }
                        ImGui::EndTabItem();
                        world.RenderWaterUI();
                        //world.sh
                    }

                    // --- TAB: INTERFACE & STYLE ---
                    if (ImGui::BeginTabItem("Interface")) {
                        ImGuiStyle& style = ImGui::GetStyle();
                        
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Scaling");
                        ImGui::Separator();
                        
                        ImGui::SliderFloat("Menu Scale", &config.menuFontScale, 1.0f, 4.0f, "%.1fx");
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adjusts the size of the Pause Menu text");

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Theme Presets");
                        ImGui::Separator();
                        
                        if (ImGui::Button("Dark Mode", ImVec2(100, 0))) ImGui::StyleColorsDark();
                        ImGui::SameLine();
                        if (ImGui::Button("Light Mode", ImVec2(100, 0))) ImGui::StyleColorsLight();
                        ImGui::SameLine();
                        if (ImGui::Button("Classic", ImVec2(100, 0))) ImGui::StyleColorsClassic();

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Geometry & Opacity");
                        ImGui::Separator();
                        
                        ImGui::SliderFloat("Global Alpha", &style.Alpha, 0.2f, 1.0f);
                        ImGui::SliderFloat("Window Rounding", &style.WindowRounding, 0.0f, 20.0f);
                        if (ImGui::IsItemEdited()) {
                            style.FrameRounding = style.WindowRounding * 0.5f;
                            style.GrabRounding = style.WindowRounding * 0.5f;
                        }
                        ImGui::SliderFloat("Item Spacing X", &style.ItemSpacing.x, 0.0f, 20.0f);
                        ImGui::SliderFloat("Item Spacing Y", &style.ItemSpacing.y, 0.0f, 20.0f);

                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Theme Colors");
                        ImGui::Separator();
                        
                        ImGui::ColorEdit4("Text", (float*)&style.Colors[ImGuiCol_Text]);
                        ImGui::ColorEdit4("Window Bg", (float*)&style.Colors[ImGuiCol_WindowBg]);
                        ImGui::ColorEdit4("Border", (float*)&style.Colors[ImGuiCol_Border]);
                        ImGui::ColorEdit4("Title Bar", (float*)&style.Colors[ImGuiCol_TitleBgActive]);
                        
                        ImGui::Dummy(ImVec2(0, 5));
                        ImGui::Text("Controls");
                        ImGui::ColorEdit4("Button", (float*)&style.Colors[ImGuiCol_Button]);
                        ImGui::ColorEdit4("Button Hover", (float*)&style.Colors[ImGuiCol_ButtonHovered]);
                        ImGui::ColorEdit4("Button Active", (float*)&style.Colors[ImGuiCol_ButtonActive]);
                        
                        ImGui::Dummy(ImVec2(0, 5));
                        ImGui::Text("Accents");
                        ImGui::ColorEdit4("Header", (float*)&style.Colors[ImGuiCol_Header]);
                        ImGui::ColorEdit4("Checkmark", (float*)&style.Colors[ImGuiCol_CheckMark]);
                        ImGui::ColorEdit4("Slider Grab", (float*)&style.Colors[ImGuiCol_SliderGrabActive]);

                        ImGui::EndTabItem();
                    }

                    // --- TAB: RESOLUTION ---
                    if (ImGui::BeginTabItem("Resolution")) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("(man you think i got time for this?)");
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("About")) {
                        // --- 1. HEADER SECTION ---
                        //ImGui::PushFont(fontBold); // Optional: if you have a bold font loaded
                        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Voxel Engine Alpha v0.5");
                        //ImGui::PopFont();
                        ImGui::TextDisabled("Developed by Brenden Stevens");
                        ImGui::Separator();

                        // --- 2. ENGINE ARCHITECTURE ---
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.4f, 1.0f), "Engine");
                        ImGui::TextWrapped(
                            "A 'hybrid' polygon-based cube rendering engine built from scratch in C++. "
                            "Unlike raw volumetric engines, this utilizes a mesh-based approach optimized "
                            "for extreme render distances via a custom Level of Detail (LOD) system."
                        );

                        // --- 3. RECENT UPDATES & OPTIMIZATIONS ---
                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("Latest Updates (v0.3+)", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::BeginGroup();
                            ImGui::BulletText("Gameplay: Collision, Block Breaking/Placing.");
                            ImGui::BulletText("Terrain: Virtualized generation classes for runtime switching.");
                            ImGui::BulletText("Memory: Dynamic RAM growth; allocation only for filled chunks.");
                            ImGui::BulletText("Optimization: Cache-efficient terrain generation (thank you -o3 flag).");
                            ImGui::EndGroup();

                            ImGui::Indent();
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                            ImGui::Text("Debug Hotkeys: F2 (Profiling), F3 (Depth), F4 (Chunk Layout)");
                            ImGui::PopStyleColor();
                            ImGui::Unindent();
                        }

                        // --- 4. TECHNICAL SPECIFICATIONS ---
                        ImGui::Spacing();
                        if (ImGui::CollapsingHeader("Tech Stack")) {
                            ImGui::Columns(2, "techstack", false);
                            ImGui::SetColumnWidth(0, 150.0f);
                            
                            ImGui::Text("Graphics API"); ImGui::NextColumn(); ImGui::Text("OpenGL 4.6 (GLAD/GLFW)"); ImGui::NextColumn();
                            ImGui::Text("Interface");    ImGui::NextColumn(); ImGui::Text("Dear ImGui");           ImGui::NextColumn();
                            ImGui::Text("Mathematics");  ImGui::NextColumn(); ImGui::Text("GLM");                  ImGui::NextColumn();
                            ImGui::Text("Build Tool");   ImGui::NextColumn(); ImGui::Text("CMake");                ImGui::NextColumn();
                            ImGui::Text("Terrain Gen");   ImGui::NextColumn(); ImGui::Text("FastNoise2");                ImGui::NextColumn();
                            ImGui::Text("Optimization");   ImGui::NextColumn(); ImGui::Text("FASTSIMD");                ImGui::NextColumn();
                            ImGui::Columns(1);
                            ImGui::Text("Figuring out wierd niche problems in my meshing algorithm: ");   ImGui::Text("Google Gemini 3");
                            
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        
                        ImGui::EndTabItem();
                    }
                    
                    ImGui::EndTabBar();
                }
            }
            ImGui::EndChild();

            // --- FOOTER: QUIT GAME BUTTON ---
            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 65.0f);
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("QUIT GAME", ImVec2(-1, 55))) {
                ImGui::TextDisabled("FREEING MEMORY/THREADS");
                glfwSetWindowShouldClose(m_Window, true);
            }
            ImGui::PopStyleColor(2);

            ImGui::End();
        }
    }


    void RenderTerrainControls(World& world, UIConfig& config) {
        ImGuiWindowFlags flags = config.isGameMode ? (ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMouseInputs) : 0;
        if (config.isGameMode) ImGui::SetNextWindowBgAlpha(0.6f);

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 330, vp->WorkPos.y + 16), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(310, 300), ImGuiCond_FirstUseEver);
        
        if (ImGui::Begin("Terrain Generation (T)", &config.showTerrainGui, flags)) {
            ImGui::SetWindowFontScale(config.DEBUG_FONT_SCALE);
            GeneratorSelector::Render(world);

            if (ImGui::Button("Reset World State", ImVec2(-1, 40))) {
                world.ReloadWorld(*config.editConfig);
            }

            ImGui::End();
        }


    }



    void RenderCullerControls(World& world, UIConfig& config) {
        ImGuiWindowFlags flags = config.isGameMode ? (ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMouseInputs) : 0;
        if (config.isGameMode) ImGui::SetNextWindowBgAlpha(0.6f);

        GpuCuller* culler = world.GetCuller();
        if (!culler) return;

        // Position: Bottom Right
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 330, vp->WorkPos.y + vp->WorkSize.y - 350), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(310, 330), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("GPU Culler Controls", &config.showCullerControls, flags)) {
            ImGui::SetWindowFontScale(config.DEBUG_FONT_SCALE);
            
            CullerSettings& settings = culler->GetSettings();

            // ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "Shader Uniforms");
            // ImGui::Separator();
            
            // ImGui::DragFloat("zNear", &settings.zNear, 0.01f, 0.001f, 10.0f);
            // ImGui::DragFloat("zFar", &settings.zFar, 1000.0f, 100.0f, 100000000.0f, "%.0f");
            
            // ImGui::Spacing();
            // ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "Logic");
            // ImGui::Separator();
            ImGui::TextWrapped("Currently partially working. Can greatly increase FPS while on the ground. But many false positives around complex goemtry.");

            ImGui::SliderFloat("'1 - Aggressiveness'", &settings.epsilonConstant, 0.0001, 0.01, "%.6f");
            ImGui::Checkbox("Enable Occlusion Culling", &settings.occlusionEnabled);
            ImGui::Checkbox("Freeze Culling Result", &settings.freezeCulling);
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Chunks Drawn: %u", culler->GetDrawCount());
            
            ImGui::End();
        }
    }

    void RenderSimpleOverlay(const UIConfig& config, const Player& player) {
        //Engine::Profiler::ScopedTimer timer("ImGui::Overlay Render Time");
        const float PAD = 10.0f;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 window_pos = ImVec2(viewport->WorkPos.x + PAD, viewport->WorkPos.y + PAD);

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.35f); 
        
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
        if (ImGui::Begin("StatsOverlay", nullptr, flags)) {
            ImGui::SetWindowFontScale(config.FPS_OVERLAY_FONT_SCALE);
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s", "[ESC] Menu | [T] Terrain Gen | [SPCBAR x 2] Toggle Creative \n Mouse Lock/Unlock [TAB] Mouse Lock/Unlock | [F2] Debug Menus\n");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s", config.isGameMode ? "[MOUSE LOCKED]" : "[MOUSE UNLOCKED]");
            ImGui::Separator();
            ImGui::Text("XYZ: %.1f, %.1f, %.1f", player.camera.Position.x, player.camera.Position.y, player.camera.Position.z);
            ImGui::Text("Angle: Y:%.1f P:%.1f", player.camera.Yaw, player.camera.Pitch);
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1), "Selected Block: %d",  player.selectedBlockID);
        }
        ImGui::End();
    }

    void RenderDebugPanel(World& world, UIConfig& config, const float VRAM_HEAP_SIZE_MB) {
        ImGuiWindowFlags flags = 0;
        if (config.isGameMode) {
            flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMouseInputs;
            ImGui::SetNextWindowBgAlpha(0.75f);
        } else {
            ImGui::SetNextWindowBgAlpha(0.85f); 
        }

        // Position: Top Left (below overlay)
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 16, vp->WorkPos.y + 100), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350,550), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Engine Debug (F2)", &config.showDebugPanel, flags)) {
            ImGui::SetWindowFontScale(config.DEBUG_FONT_SCALE);
            
            // --- Performance ---
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "PERFORMANCE");
            ImGui::Separator();
            float fps = ImGui::GetIO().Framerate;
            ImGui::Text("FPS: %.1f", fps);
            ImGui::Text("Frame Time: %.3f ms", 1000.0f / fps);
            ImGui::Checkbox("VSync", &config.vsync);
            
            // --- Memory ---
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "GPU MEMORY");
            ImGui::Separator();
            size_t used = world.getVRAMUsed();
            size_t total = world.getVRAMAllocated();
            float usedMB = (used / 1024.0f / 1024.0f);
            float totalMB = VRAM_HEAP_SIZE_MB;
            float ratio = (float)used / (float)total;
            
            ImGui::Text("VRAM: %.1f / %.1f MB", usedMB, totalMB);
            ImGui::ProgressBar(ratio, ImVec2(-1.0f, 15.0f));
            ImGui::Text("Fragmentation: %zu free blocks", world.getVRAMFreeBlocks());

            // --- Geometry ---
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "WORLD GEOMETRY");
            ImGui::Separator();
            
            size_t activeChunks = 0;
            size_t totalVertices = 0;
            world.calculateTotalVertices(activeChunks, totalVertices);
            // for (const auto& pair : world.m_chunks) {
            //     if (pair.second->state == ChunkState::ACTIVE) {
            //         activeChunks++;
            //         totalVertices += pair.second->vertexCountOpaque;
            //     }
            // }
            
            ImGui::Text("Active Chunks: %zu", activeChunks);
            ImGui::Text("Resident Vertices: %s", FormatNumber(totalVertices).c_str());
            
            if (ImGui::Checkbox("Wireframe Mode", &config.showWireframe)) {
                glPolygonMode(GL_FRONT_AND_BACK, config.showWireframe ? GL_LINE : GL_FILL);
            }
            ImGui::Checkbox("Lock Frustum (F)", &config.lockFrustum);
            if (config.lockFrustum) ImGui::TextColored(ImVec4(1,0,0,1), "FRUSTUM LOCKED");

            if (world.GetLODFreeze())
                ImGui::TextColored(ImVec4(1,0,0,1), "CHUNK/LOD Loading Frozen (O to toggle)");

            // --- Shader Debugging ---
            ImGui::Text("Cube Texture Debugging:");
            bool debugChanged = false;
            if (ImGui::RadioButton("Normal Shader", &config.editConfig->settings.cubeDebugMode, 0)) debugChanged = true;
            if (ImGui::RadioButton("Debug Normals", &config.editConfig->settings.cubeDebugMode, 1)) debugChanged = true;
            if (ImGui::RadioButton("Debug AO", &config.editConfig->settings.cubeDebugMode, 2)) debugChanged = true;
            if (ImGui::RadioButton("Debug UVs", &config.editConfig->settings.cubeDebugMode, 3)) debugChanged = true;
            if (ImGui::RadioButton("Flat Color", &config.editConfig->settings.cubeDebugMode, 4)) debugChanged = true;

            if (debugChanged) {
                world.setCubeDebugMode(config.editConfig->settings.cubeDebugMode);
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0, 1, 1, 1), "THREADING");
            ImGui::Separator();
            //ImGui::Text("Pool Tasks: %zu", world.m_pool.GetQueueSize());
            
            // grab all of the ImGui controls its that easy
            //world.GetGenerator()->OnImGui();
            if (ImGui::Button("Reset World State", ImVec2(-1, 40))) {
                world.ReloadWorld(*config.editConfig);
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