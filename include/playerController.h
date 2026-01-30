#pragma once

#include "camera.h"
#include "terrain/terrain_system.h"
#include <algorithm>
#include <cmath>
#include <iostream>

class Player {
public:
    Camera camera;
    glm::vec3 position;
    glm::vec3 velocity;
    
    // --- State ---
    bool isCreativeMode = false; // Flying, No Gravity
    bool isSprinting = false;
    bool onGround = false;

    // --- Settings ---
    // Physical dimensions
    float width = 0.6f;
    float height = 1.8f;
    float eyeLevel = 1.6f; 
    
    // Movement Speeds
    float walkSpeed = 5.0f;
    float runSpeed = 40.0f;     // Fast sprint
    float flySpeed = 70.0f;      // Creative flight speed
    float flySprintSpeed = 100.0f; // Shift + Fly

    // Physics Constants
    float jumpForce = 15.5f;
    float gravity = 22.0f;       // Snappy gravity
    float groundDrag = 8.0f;     // High friction for tight controls
    float airDrag = 1.0f;        // Lower friction in air
    float flyDrag = 5.0f;        // Friction while flying (stops sliding)

    // Double Tap Logic
    float lastSpacePressTime = 0.0f;
    bool wasSpaceDown = false;   // Fixed: Track state as member variable

    Player(glm::vec3 startPos) 
        : position(startPos), velocity(0.0f) 
    {
        camera.Position = position + glm::vec3(0, eyeLevel, 0);
    }


    void Update(float deltaTime, GLFWwindow* window, ITerrainGenerator* terrain) {
        HandleInput(deltaTime, window);
        
        if (isCreativeMode) {
            ApplyCreativePhysics(deltaTime); // No collision, no gravity
        } else {
            ApplySurvivalPhysics(deltaTime, terrain); // Gravity + Collision
        }
        
        // Sync camera
        camera.Position = position + glm::vec3(0, eyeLevel, 0);
    }

    // Pass-through for camera rotation
    void ProcessMouseMovement(float xoffset, float yoffset) {
        camera.ProcessMouseMovement(xoffset, yoffset);
    }

    //glm::vec3 Position() {return position;}


private:

    void HandleInput(float dt, GLFWwindow* window) {
        // --- Double Tap Space for Creative Mode ---
        bool isSpaceDown = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);

        // Check for "Rising Edge" (Just Pressed)
        if (isSpaceDown && !wasSpaceDown) {
            float now = (float)glfwGetTime();
            float timeSinceLast = now - lastSpacePressTime;

            if (timeSinceLast < 0.25f) {
                // Double tap detected!
                isCreativeMode = !isCreativeMode;
                velocity = glm::vec3(0); // Stop momentum
                std::cout << "[Player] Creative Mode: " << (isCreativeMode ? "ON" : "OFF") << std::endl;
                lastSpacePressTime = -1.0f; // Reset timer prevents triple-tap issues
            } else {
                lastSpacePressTime = now;
            }
        }
        wasSpaceDown = isSpaceDown;

        // --- Movement Request ---
        isSprinting = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);

        // Calculate Target Speed
        float targetSpeed = walkSpeed;
        if (isCreativeMode) {
            targetSpeed = isSprinting ? flySprintSpeed : flySpeed;
        } else {
            targetSpeed = isSprinting ? runSpeed : walkSpeed;
        }

        // Get Input Vectors
        glm::vec3 forward = glm::normalize(glm::vec3(camera.Front.x, 0, camera.Front.z));
        glm::vec3 right = glm::normalize(glm::vec3(camera.Right.x, 0, camera.Right.z));
        glm::vec3 wishDir(0.0f);

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wishDir += forward;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wishDir -= forward;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wishDir += right;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wishDir -= right;

        // Normalize input
        if (glm::length(wishDir) > 0.01f) wishDir = glm::normalize(wishDir);

        // --- Apply Input Acceleration ---
        if (isCreativeMode) {
            // Vertical Fly Input
            if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) wishDir.y += 1.0f;
            if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) wishDir.y -= 1.0f;
            
            // Immediate response in creative mode
            velocity = glm::mix(velocity, wishDir * targetSpeed, flyDrag * dt);
            
        } else {
            // Survival Ground Movement
            // Ground control is sharp (high acceleration), Air control is floaty
            float accel = onGround ? 15.0f : 2.0f; 
            
            // We only effect X/Z with WASD. Y is gravity.
            glm::vec3 targetVel = wishDir * targetSpeed;
            
            velocity.x = glm::mix(velocity.x, targetVel.x, accel * dt);
            velocity.z = glm::mix(velocity.z, targetVel.z, accel * dt);
            

            // white boy cant jump
            if (onGround && isSpaceDown && !isCreativeMode) {
                 velocity.y = jumpForce;
                 onGround = false;
            }

        }

    }

    void ApplyCreativePhysics(float dt) {
        // Simple Euler integration (velocity is already set by input)
        position += velocity * dt;
    }

    void ApplySurvivalPhysics(float dt, ITerrainGenerator* terrain) {
        //  Gravity
        velocity.y -= gravity * dt;
        if (velocity.y < -50.0f) velocity.y = -50.0f; // Terminal velocity

        //  Collision & Integration per Axis
        // X
        position.x += velocity.x * dt;
        if (CheckCollision(terrain)) {
            position.x -= velocity.x * dt;
            velocity.x = 0;
        }
        // Z
        position.z += velocity.z * dt;
        if (CheckCollision(terrain)) {
            position.z -= velocity.z * dt;
            velocity.z = 0;
        }
        // Y
        position.y += velocity.y * dt;
        onGround = false;
        if (CheckCollision(terrain)) {
            bool falling = velocity.y < 0;
            position.y -= velocity.y * dt;
            velocity.y = 0;
            if (falling) onGround = true;
        }

        // Kill floor
        if (position.y < -50) {
            position = glm::vec3(0, 100, 0);
            velocity = glm::vec3(0);
        }
    }

    // UPDATED: Now supports full 3D collision (caves, overhangs, bridges)
    bool CheckCollision(ITerrainGenerator* terrain) {
        glm::vec3 min = position - glm::vec3(width/2, 0, width/2);
        glm::vec3 max = position + glm::vec3(width/2, height, width/2);

        int minX = static_cast<int>(std::floor(min.x));
        int maxX = static_cast<int>(std::floor(max.x));
        int minY = static_cast<int>(std::floor(min.y));
        int maxY = static_cast<int>(std::floor(max.y));
        int minZ = static_cast<int>(std::floor(min.z));
        int maxZ = static_cast<int>(std::floor(max.z));

        for (int x = minX; x <= maxX; x++) {
            for (int y = minY; y <= maxY; y++) {
                for (int z = minZ; z <= maxZ; z++) {
                    // We use LOD 1 for physics (finest detail)
                    // We no longer need GetHeight(); GetBlock handles 3D checks internally.
                    auto texture = terrain->GetBlock((float)x, (float)y, (float)z, 1); // get the block underneath the player aabb

                    if (!(texture == 0 || texture == 6)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }
};