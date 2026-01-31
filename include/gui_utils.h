#pragma once
#include "imgui.h"
#include <string>
#include <algorithm>

namespace GUI {

    // Levels of severity for the on-screen display
    enum MessageLevel {
        LEVEL_INFO = 0,     // Green/White, Normal Size
        LEVEL_WARN = 1,     // Yellow, Normal Size
        LEVEL_ERROR = 2,    // Orange, Slightly Larger
        LEVEL_CRITICAL = 3  // Red, Large, "Fat" text
    };

    /**
     * @brief Draws a message at the top-center of the screen immediately for this frame.
     * @param alphaOverride Optional transparency (0.0 - 1.0) for fading effects.
     */
    inline void DrawScreenMessage(const char* message, int level, float alphaOverride = 1.0f) {
        // 1. Setup Colors & Scaling based on Level
        ImVec4 color;
        float fontScale = 1.0f;

        switch (level) {
            case LEVEL_INFO: 
                color = ImVec4(0.8f, 1.0f, 0.8f, alphaOverride); // Pale Green
                break;
            case LEVEL_WARN: 
                color = ImVec4(1.0f, 0.9f, 0.0f, alphaOverride); // Yellow
                break;
            case LEVEL_ERROR: 
                color = ImVec4(1.0f, 0.5f, 0.0f, alphaOverride); // Orange
                fontScale = 1.2f;
                break;
            case LEVEL_CRITICAL: 
                color = ImVec4(1.0f, 0.1f, 0.1f, alphaOverride); // Bright Red
                fontScale = 2.0f;
                break;
            default:
                color = ImVec4(1.0f, 1.0f, 1.0f, alphaOverride);
                break;
        }

        // 2. Setup Window Flags for "Overlay" style
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | 
                                 ImGuiWindowFlags_NoInputs | 
                                 ImGuiWindowFlags_AlwaysAutoResize | 
                                 ImGuiWindowFlags_NoBackground | 
                                 ImGuiWindowFlags_NoFocusOnAppearing | 
                                 ImGuiWindowFlags_NoNav;

        // 3. Position: Top Center
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 workPos = viewport->WorkPos;
        ImVec2 workSize = viewport->WorkSize;
        
        // Pivot (0.5, 0.0) means the x,y coordinate we set refers to the top-middle of the window
        ImGui::SetNextWindowPos(ImVec2(workPos.x + workSize.x * 0.5f, workPos.y + 50.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.0f); // Ensure transparent background

        // 4. Draw
        std::string winName = "##ScreenMessage_Level_" + std::to_string(level);
        
        if (ImGui::Begin(winName.c_str(), nullptr, flags)) {
            ImGui::SetWindowFontScale(fontScale);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            
            ImGui::Text("%s", message);
            
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(1.0f); 
        }
        ImGui::End();
    }

    // --- TEMPORARY MESSAGE SYSTEM ---

    struct TempMsgState {
        std::string text;
        int level = 0;
        float timeLeft = 0.0f;
    };

    // Singleton accessor to ensure all files share the same message state
    inline TempMsgState& GetMessageState() {
        static TempMsgState s_state;
        return s_state;
    }

    /**
     * @brief Trigger a message to appear for 'durationSeconds'.
     * @param durationSeconds How long the message stays up (default 2.5s).
     * @param forceRestart If true (default), resets the timer to full duration immediately.
     * If false, ignores the call if the message is already displaying.
     * Set to 'false' if calling every frame to create a pulsing effect.
     */
    inline void TriggerTemporaryMessage(const char* message, int level, float durationSeconds = 2.5f, bool forceRestart = true) {
        auto& state = GetMessageState();
        
        // If we are NOT forcing a restart, and the message is currently active, do nothing.
        // This allows the current message to play out its full fade animation before restarting.
        if (!forceRestart && state.timeLeft > 0.0f) {
            return;
        }

        state.text = message;
        state.level = level;
        state.timeLeft = durationSeconds;
    }

    /**
     * @brief Must be called every frame in your main render loop.
     * Handles the timer and fading logic.
     */
    inline void UpdateTemporaryMessage(float deltaTime) {
        auto& state = GetMessageState();
        if (state.timeLeft > 0.0f) {
            
            // Calculate Fade Out (starts fading in the last 0.5 seconds)
            float alpha = 1.0f;
            if (state.timeLeft < 0.5f) {
                alpha = std::max(0.0f, state.timeLeft / 0.5f);
            }

            DrawScreenMessage(state.text.c_str(), state.level, alpha);
            
            state.timeLeft -= deltaTime;
        }
    }
}