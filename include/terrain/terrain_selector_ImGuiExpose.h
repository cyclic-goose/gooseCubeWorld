#pragma once

#include "terrain_system.h"

// Include your specific generators
#include "advancedGenerator.h"
#include "terrain_smooth_noise.h"
#include "terrain_bizzaro_world.h"

// Direct import of your World class
#include "world.h"

#include <memory>
#include <vector>
#include <string>

// We need ImGui for the UI and GetIO().DeltaTime
#include "imgui.h"
// Assuming TextureManager is available
#include "texture_manager.h" 

namespace GeneratorSelector {

    // Helper to keep track of selection state.
    static int currentGenIndex = 0;
    static float switchCooldown = 0.0f; // Internal cooldown state

    static const char* genNames[] = { 
        "Advanced (Standard)", 
        "Overhang (3D Noise)", 
        "Bizzaro (Crater World)" 
    };

    // Factory function to create generators based on index
    inline std::unique_ptr<ITerrainGenerator> CreateGenerator(int index, int seed) {
        switch (index) {
            case 0: return std::make_unique<AdvancedGenerator>(seed);
            case 1: return std::make_unique<OverhangGenerator>(); 
            case 2: return std::make_unique<BizzaroGenerator>();
            default: return std::make_unique<AdvancedGenerator>(seed);
        }
    }

    // Call this inside your main OnImGui loop
    // Updated signature: only World is required
    inline void Render(World& world) {
        
        // 1. AUTOMATICALLY UPDATE COOLDOWN
        if (switchCooldown > 0.0f) {
            switchCooldown -= ImGui::GetIO().DeltaTime;
            if (switchCooldown < 0.0f) switchCooldown = 0.0f;
        }

        // 2. Disable controls if currently switching
        if (switchCooldown > 0.0f) {
            ImGui::BeginDisabled();
            ImGui::Combo("Generator", &currentGenIndex, genNames, IM_ARRAYSIZE(genNames));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Generating... %.1fs", switchCooldown);
            return;
        }

        // 3. The Dropdown
        if (ImGui::Combo("Generator", &currentGenIndex, genNames, IM_ARRAYSIZE(genNames))) {
            
            // --- SWITCH LOGIC ---
            
            // Create the new specific generator
            auto newGen = CreateGenerator(currentGenIndex, rand());

            // Get textures required by this generator
            std::vector<std::string> newPaths = newGen->GetTexturePaths();
            
            // Load into GPU array via your TextureManager
            GLuint newTexArray = TextureManager::LoadTextureArray(newPaths);

            // Inject into World
            // This transfers ownership (std::move) and updates the texture reference
            world.SwitchGenerator(std::move(newGen), newTexArray);
            
            // Set internal cooldown
            switchCooldown = 2.0f;
        }

        ImGui::Separator();

        // 4. Show the specific controls for the active generator
        if (world.GetGenerator()) {
            world.GetGenerator()->OnImGui();
        }
    }
}