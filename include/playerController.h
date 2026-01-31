#pragma once

#include "camera.h"
#include "terrain/terrain_system.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <string>
#include "world.h"
#include "block_outliner.h"

#ifdef IMGUI_VERSION
#include "imgui.h"
#endif

// =============================================================
// CONFIGURATION STRUCT
// =============================================================
struct PlayerConfig {
    // --- Dimensions (Units: Blocks/Meters) ---
    float PlayerWidth       = 0.6f;
    float PlayerHeight      = 1.8f;
    float EyeLevelNormal    = 1.62f;
    float EyeLevelSneak     = 1.27f;

    // --- Movement Speeds (Units: Blocks/Second) ---
    float SpeedWalk         = 4.317f;
    float SpeedSprint       = 5.612f;
    float SpeedSneak        = 1.295f;
    float SpeedFly          = 10.92f;
    float SpeedFlySprint    = 21.6f;

    // --- Physics Constants ---
    float Gravity           = 32.0f;        // Downward acceleration (m/s^2)
    float JumpForce         = 9.0f;         // Initial upward velocity
    float TerminalVelocity  = -78.4f;       // Max falling speed

    // --- Inertia & Friction (Higher = Snappier stops) ---
    float DragGround        = 10.0f;        // Friction on ground
    float DragAir           = 1.5f;         // Air control friction
    float DragFly           = 5.0f;         // Flying friction

    // --- View Bobbing Settings ---
    float BobFrequency      = 10.0f;
    float BobAmplitude      = 0.07f;
    float BobSprintMult     = 1.25f;         // Multiplier for freq/amp when sprinting

    float BlockInteractionDistance = 8.0f;
};

class Player {
public:
    Camera camera;
    glm::vec3 position;
    glm::vec3 velocity;

    // Active Configuration
    PlayerConfig config;

    // =============================================================
    // DYNAMIC STATE
    // =============================================================
    bool isCreativeMode     = false;
    bool isSprinting        = false;
    bool isSneaking         = false;
    bool onGround           = false;

    // Interaction State
    int selectedBlockID     = 1; // 1 is grassss

private:
    // Store the "Factory Defaults" to allow resetting later
    const PlayerConfig defaultConfig; 

    // Internal State
    float walkDistance      = 0.0f;         // Accumulator for bobbing sine wave
    float lastSpaceTime     = 0.0f;         // For double-tap detection
    bool  wasSpaceDown      = false;

    // Mouse Debounce
    bool wasLeftClick       = false;
    bool wasRightClick      = false;

    // Smooth dampener for bobbing amplitude (Fixes jitter when stopping)
    float currentBobAmplitude = 0.0f; 

public:
    Player(glm::vec3 startPos) 
        : position(startPos), velocity(0.0f), defaultConfig() 
    {
        // Initialize active config with factory defaults
        config = defaultConfig;
        camera.Position = position + glm::vec3(0, config.EyeLevelNormal, 0);
        ApplyPreset("FAST"); // for now i wanna be fast 
        //BlockSelection::Get().; // init buffer for highlight lines
    }

    // =============================================================
    // IMGUI INTERFACE
    // =============================================================
    
    void ApplyPreset(const std::string& name) {
        if (name == "Minecraft (Default)") {
            config = defaultConfig;
        } 
        else if (name == "Quake (Fast)") {
            config.SpeedWalk = 8.0f; config.SpeedSprint = 12.0f; 
            config.JumpForce = 12.0f; config.Gravity = 28.0f;
            config.DragGround = 8.0f; config.DragAir = 1.0f; // Slippery air
        } 
        else if (name == "Cinematic (Slow)") {
            config.SpeedWalk = 2.0f; config.SpeedSprint = 3.5f; 
            config.SpeedFly = 5.0f;
            config.BobAmplitude = 0.02f; // Almost no bob
        }else if (name == "FAST") {
            config.SpeedWalk = 6.0f; config.SpeedSprint = 25.5f; 
            config.SpeedFly = 50.0f;
            config.BobAmplitude = 0.00f; // Almost no bob
            config.JumpForce = 40.0f;
            config.SpeedFlySprint = 120.0f;
            
        }
    }

    void DrawInterface() {
#ifdef IMGUI_VERSION
        ImGui::PushID("PlayerController"); // prevent ID collisions

        // --- Header: Controls & Presets ---
        if (ImGui::Button("Reset Defaults")) {
            config = defaultConfig;
        }
        ImGui::SameLine();
        
        // Preset Loader
        ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("##Presets", "Load Preset...")) {
            if (ImGui::Selectable("Minecraft (Default)")) ApplyPreset("Minecraft (Default)");
            if (ImGui::Selectable("Quake (Fast)"))        ApplyPreset("Quake (Fast)");
            if (ImGui::Selectable("Cinematic (Slow)"))    ApplyPreset("Cinematic (Slow)");
            if (ImGui::Selectable("FAST"))    ApplyPreset("FAST");
            ImGui::EndCombo();
        }

        ImGui::Separator();

        // --- Section 1: Read-Only State ---
        if (ImGui::CollapsingHeader("Player Status", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Pos: %.2f, %.2f, %.2f", position.x, position.y, position.z);
            ImGui::Text("Vel: %.2f, %.2f, %.2f", velocity.x, velocity.y, velocity.z);
            
            // Visual indicators
            ImGui::Text("Mode: "); ImGui::SameLine();
            if (isCreativeMode) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Creative");
            else                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Survival");

            ImGui::SameLine(); ImGui::Text("| Ground: "); ImGui::SameLine();
            if (onGround) ImGui::TextColored(ImVec4(0,1,0,1), "YES");
            else          ImGui::TextColored(ImVec4(1,0,0,1), "NO");
            
            ImGui::Checkbox("Creative Mode (Fly)", &isCreativeMode);
        }

        // --- Section 2: Movement Configuration ---
        if (ImGui::CollapsingHeader("Locomotion")) {
            ImGui::Indent();
            ImGui::TextDisabled("Ground Movement");
            ImGui::SliderFloat("Walk Speed",   &config.SpeedWalk,   0.0f, 20.0f, "%.2f b/s");
            ImGui::SliderFloat("Sprint Speed", &config.SpeedSprint, 0.0f, 60.0f, "%.2f b/s");
            ImGui::SliderFloat("Sneak Speed",  &config.SpeedSneak,  0.0f, 20.0f, "%.2f b/s");

            ImGui::Spacing();
            ImGui::TextDisabled("Flight");
            ImGui::SliderFloat("Fly Speed",    &config.SpeedFly,       0.0f, 50.0f, "%.1f b/s");
            ImGui::SliderFloat("Fly Sprint",   &config.SpeedFlySprint, 0.0f, 100.0f, "%.1f b/s");
            ImGui::Unindent();
        }

        // --- Section 3: Physics ---
        if (ImGui::CollapsingHeader("Physics & Gravity")) {
            ImGui::Indent();
            ImGui::SliderFloat("Gravity",      &config.Gravity,       -100.0f, 100.0f);
            ImGui::DragFloat("Jump Force",   &config.JumpForce,     0.1f, 0.0f, 90.0f);
            ImGui::DragFloat("Terminal Vel", &config.TerminalVelocity, 1.0f, -200.0f, 0.0f);
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Friction / Drag");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher values mean movement stops faster");

            ImGui::DragFloat("Ground Drag",  &config.DragGround,    0.1f, 0.0f, 50.0f);
            ImGui::DragFloat("Air Drag",     &config.DragAir,       0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("Fly Drag",     &config.DragFly,       0.1f, 0.0f, 20.0f);
            ImGui::Unindent();
        }
        
        // --- Section 4: Body ---
        if (ImGui::CollapsingHeader("Dimensions & View")) {
            ImGui::Indent();
            if (ImGui::TreeNode("Hitbox")) {
                ImGui::DragFloat("Width",  &config.PlayerWidth,  0.01f, 0.1f, 5.0f);
                ImGui::DragFloat("Height", &config.PlayerHeight, 0.01f, 0.1f, 5.0f);
                ImGui::TreePop();
            }
            
            if (ImGui::TreeNode("Eye Levels")) {
                ImGui::DragFloat("Normal", &config.EyeLevelNormal, 0.01f, 0.1f, 3.0f);
                ImGui::DragFloat("Sneak",  &config.EyeLevelSneak,  0.01f, 0.1f, 3.0f);
                ImGui::SliderFloat("Block Interaction Distance", &config.BlockInteractionDistance, 3.0f, 60.0f);
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Camera Bobbing")) {
                ImGui::DragFloat("Frequency", &config.BobFrequency, 0.1f, 0.0f, 30.0f);
                ImGui::DragFloat("Amplitude", &config.BobAmplitude, 0.001f, 0.0f, 1.0f);
                ImGui::DragFloat("Sprint Mult", &config.BobSprintMult, 0.1f, 1.0f, 3.0f);
                ImGui::TreePop();
            }
            ImGui::Unindent();
        }

        ImGui::PopID();
#else
        // If ImGui is not enabled, this function does nothing.
        // To enable, #define ENABLE_IMGUI in your project settings.
#endif
    }

    // =============================================================
    // UPDATE LOOP
    // =============================================================
    void Update(float deltaTime, GLFWwindow* window, World& world, bool isGameMode) {
        HandleInput(deltaTime, window, world, isGameMode);
        
        if (isCreativeMode) {
            ApplyCreativePhysics(deltaTime);
        } else {
            ApplySurvivalPhysics(deltaTime, world);
        }
        
        UpdateCamera(deltaTime);
    }

    // Pass-through for camera rotation
    void ProcessMouseMovement(float xoffset, float yoffset) {
        camera.ProcessMouseMovement(xoffset, yoffset);
    }

    // Pass-through for block selection
    void ProcessScroll(float yoffset) {
        if (yoffset > 0) selectedBlockID++;
        if (yoffset < 0) selectedBlockID--;
        if (selectedBlockID < 1) selectedBlockID = 1;
        if (selectedBlockID > 10) selectedBlockID = 10;
    }

private:

    void HandleInput(float dt, GLFWwindow* window, World& world, bool isGameMode) {
        // --- Input State Reading ---
        bool isSpaceDown = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
        bool isCtrlDown  = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);
        bool isShiftDown = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
        
        // cast a ray to highlight block, then decide if player clicked and destroy block
        // Raycast N blocks
        World::RaycastResult res = world.Raycast(camera.Position, camera.Front, config.BlockInteractionDistance);
        BlockSelection::Get().Update(res.success, res.blockPos);
        // ********** Interaction Logic ********** //
        if (isGameMode)
        {

            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                if (!wasLeftClick) {
                    //World::RaycastResult res = world.Raycast(camera.Position, camera.Front, 8.0f);
                    if (res.success) {
                        world.SetBlock(res.blockPos.x, res.blockPos.y, res.blockPos.z, 0); // 0 = Air
                    }
                }
                wasLeftClick = true;
            } else wasLeftClick = false;


            // (right click to place block)
            if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
                if (!wasRightClick) {
                    //World::RaycastResult res = world.Raycast(camera.Position, camera.Front, 8.0f);
                    if (res.success) {
                        glm::ivec3 target = res.blockPos + res.faceNormal;
                        
                        // Simple collision check to prevent placing block inside player's head
                        glm::vec3 pMin = position - glm::vec3(0.3f, 0.0f, 0.3f);
                        glm::vec3 pMax = position + glm::vec3(0.3f, 1.8f, 0.3f);
                        
                        bool insidePlayer = (target.x >= pMin.x && target.x <= pMax.x) &&
                                            (target.y >= pMin.y && target.y <= pMax.y) &&
                                            (target.z >= pMin.z && target.z <= pMax.z);
                        
                        if (!insidePlayer) {
                            world.SetBlock(target.x, target.y, target.z, (uint8_t)selectedBlockID);
                        }
                    }
                }
                wasRightClick = true;
            } else wasRightClick = false;
        }
        // ********** Interaction Logic ********** //
        



        // --- Double Tap Space (Creative Toggle) ---
        if (isSpaceDown && !wasSpaceDown) {
            float now = (float)glfwGetTime();
            if (now - lastSpaceTime < 0.25f) {
                isCreativeMode = !isCreativeMode;
                velocity = glm::vec3(0);
                std::cout << "[Player] Creative Mode: " << (isCreativeMode ? "ON" : "OFF") << std::endl;
                lastSpaceTime = -1.0f; 
            } else {
                lastSpaceTime = now;
            }
        }
        wasSpaceDown = isSpaceDown;

        // --- States ---
        isSprinting = isShiftDown; // Shift to Sprint
        isSneaking  = isCtrlDown;  // Ctrl to Sneak

        // Reset sprint if we stop moving or sneak
        glm::vec3 wishDir(0.0f);
        glm::vec3 forward = glm::normalize(glm::vec3(camera.Front.x, 0, camera.Front.z));
        glm::vec3 right   = glm::normalize(glm::vec3(camera.Right.x, 0, camera.Right.z));

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wishDir += forward;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wishDir -= forward;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wishDir += right;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wishDir -= right;

        if (glm::length(wishDir) > 0.01f) wishDir = glm::normalize(wishDir);

        // --- Target Speed Calculation ---
        float targetSpeed = config.SpeedWalk;
        if (isCreativeMode) {
            targetSpeed = isSprinting ? config.SpeedFlySprint : config.SpeedFly;
        } else {
            if (isSprinting) targetSpeed = config.SpeedSprint;
            if (isSneaking)  targetSpeed = config.SpeedSneak;
        }

        // --- Apply Velocity ---
        if (isCreativeMode) {
            if (isSpaceDown) wishDir.y += 1.0f;
            if (isSneaking)  wishDir.y -= 1.0f; // Sneak moves down in creative
            velocity = glm::mix(velocity, wishDir * targetSpeed, config.DragFly * dt);
        } else {
            // Survival Movement
            float acceleration = onGround ? config.DragGround : config.DragAir;
            
            // Apply horizontal force
            glm::vec3 targetVel = wishDir * targetSpeed;
            velocity.x = glm::mix(velocity.x, targetVel.x, acceleration * dt);
            velocity.z = glm::mix(velocity.z, targetVel.z, acceleration * dt);

            // Jump
            if (onGround && isSpaceDown) {
                 velocity.y = config.JumpForce;
                 onGround = false;
            }
        }
    }

    void ApplyCreativePhysics(float dt) {
        position += velocity * dt;
    }

    void ApplySurvivalPhysics(float dt, World& world) {
        // 1. Gravity
        velocity.y -= config.Gravity * dt;
        if (velocity.y < config.TerminalVelocity) velocity.y = config.TerminalVelocity;

        // 2. Integration & Collision (Sweep and Prune style per axis)
        
        // X Axis
        position.x += velocity.x * dt;
        if (CheckCollision(world)) {
            position.x -= velocity.x * dt;
            velocity.x = 0;
        }
        
        // Z Axis
        position.z += velocity.z * dt;
        if (CheckCollision(world)) {
            position.z -= velocity.z * dt;
            velocity.z = 0;
        }

        // Y Axis
        position.y += velocity.y * dt;
        onGround = false;
        
        if (CheckCollision(world)) {
            bool falling = velocity.y < 0;
            
            // Step back
            position.y -= velocity.y * dt;
            
            // Snap to block grid if falling (prevents jitter)
            if (falling) {
                // position.y = std::ceil(position.y - config.PlayerHeight) + 0.001f; 
                onGround = true;
            }
            velocity.y = 0;
        }

        // Void Floor
        // if (position.y < -50) {
        //     position = glm::vec3(0, 100, 0);
        //     velocity = glm::vec3(0);
        // }
    }

    void UpdateCamera(float dt) {
        // 1. Dynamic Eye Level (Sneaking)
        float targetEye = isSneaking ? config.EyeLevelSneak : config.EyeLevelNormal;
        static float currentEye = config.EyeLevelNormal;
        currentEye = glm::mix(currentEye, targetEye, 15.0f * dt);

        // 2. Dynamic FOV (Sprinting)
        // Note: Assumes camera.FovBase exists or we use a hardcoded base. 
        // Using camera.Fov logic from previous turn.
        float baseFov = 70.0f; // Could also be exposed in ImGui if added to Camera class
        float targetFov = isSprinting ? (baseFov * 1.15f) : baseFov;
        camera.SetFov(targetFov, dt);

        // 3. View Bobbing (Smoothed)
        // Fix: We dampen the *amplitude* to 0 when stopping, rather than resetting phase.
        // This ensures the camera gently settles back to center from any part of the sine wave.
        bool moving = onGround && !isSneaking && glm::length(glm::vec2(velocity.x, velocity.z)) > 0.1f;
        
        float targetAmp = 0.0f;
        float freq = config.BobFrequency;

        if (moving) {
            targetAmp = isSprinting ? (config.BobAmplitude * config.BobSprintMult) : config.BobAmplitude;
            freq      = isSprinting ? (config.BobFrequency * config.BobSprintMult) : config.BobFrequency;
            
            // Only increment sine wave phase while actually moving
            walkDistance += dt * freq;
        } 
        
        // Smoothly interpolate amplitude (10.0f = speed of fade out)
        currentBobAmplitude = glm::mix(currentBobAmplitude, targetAmp, 10.0f * dt);
        
        // Prevent tiny float drifts when completely stopped
        if (currentBobAmplitude < 0.001f) currentBobAmplitude = 0.0f;

        float bobOffset = sin(walkDistance) * currentBobAmplitude;

        camera.Position = position + glm::vec3(0, currentEye + bobOffset, 0);
    }

    bool CheckCollision(World& world) {
        // AABB Collision
        // We reduce width slightly to allow fitting through 1-block gaps
        // without getting stuck on walls due to floating point errors
        float collisionWidth = config.PlayerWidth - 0.1f; 
        
        glm::vec3 min = position - glm::vec3(collisionWidth/2, 0, collisionWidth/2);
        glm::vec3 max = position + glm::vec3(collisionWidth/2, config.PlayerHeight, collisionWidth/2);

        // Determine grid range to check
        int minX = static_cast<int>(std::floor(min.x));
        int maxX = static_cast<int>(std::floor(max.x));
        int minY = static_cast<int>(std::floor(min.y));
        int maxY = static_cast<int>(std::floor(max.y));
        int minZ = static_cast<int>(std::floor(min.z));
        int maxZ = static_cast<int>(std::floor(max.z));

        // Iterate through all blocks intersecting the player's bounding box
        for (int x = minX; x <= maxX; x++) {
            for (int y = minY; y <= maxY; y++) {
                for (int z = minZ; z <= maxZ; z++) {
                    
                    // Use the fast world lookup
                    uint8_t blockID = world.GetBlockAt(x, y, z);
                    
                    // Collision Logic:
                    // 0 = Air (no collision)
                    // 6 = Water/Liquid (no collision) - Adjust this ID as needed
                    if (blockID != 0 && blockID != 6) {
                        return true;
                    }
                }
            }
        }
        return false;
    }
};