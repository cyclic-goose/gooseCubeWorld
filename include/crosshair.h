#pragma once

// singleton crosshair
// How to use it
// In your main.cpp (or wherever your main render loop is), call the Draw() function inside your ImGui frame (between ImGui::NewFrame() and ImGui::Render()).
// for me i put it in my ImGuiRenderGui loop

#ifdef IMGUI_VERSION
#include "imgui.h"
#endif

class Crosshair {
public:
    static Crosshair& Get() {
        static Crosshair instance;
        return instance;
    }

    bool m_enabled = true;
    float m_size = 10.0f;
    float m_thickness = 2.0f;
    bool m_dot = false; // Option to draw a dot instead of cross

    // Color: White with slight transparency (ABGR format for ImGui)
    // You can also use IM_COL32(255, 255, 255, 200) inside the function if preferred
    unsigned int m_color = 0xCCFFFFFF; 

    void Draw() {
#ifdef IMGUI_VERSION
        if (!m_enabled) return;

        // Use ForegroundDrawList to ensure it draws on top of all other UI windows
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        
        // Calculate center of the main viewport
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 center = viewport->GetCenter();

        if (m_dot) {
             drawList->AddCircleFilled(center, m_thickness, m_color);
        } else {
            // Horizontal line
            drawList->AddLine(
                ImVec2(center.x - m_size, center.y),
                ImVec2(center.x + m_size, center.y),
                m_color, 
                m_thickness
            );

            // Vertical line
            drawList->AddLine(
                ImVec2(center.x, center.y - m_size),
                ImVec2(center.x, center.y + m_size),
                m_color, 
                m_thickness
            );
        }
#endif
    }

private:
    Crosshair() {}
};