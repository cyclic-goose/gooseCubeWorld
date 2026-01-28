#pragma once

#include "camera.h"
#include "terrain_system.h"
#include <algorithm>
#include <cmath>

class Player {
public:
    Camera camera;
    glm::vec3 position;
    glm::vec3 velocity;
    
    // Physical dimensions
    float width = 0.6f;
    float height = 1.8f;
    float eyeLevel = 1.6f; 
    
    // Movement settings
    float walkSpeed = 5.0f;
    float runSpeed = 8.0f;
    float jumpForce = 8.0f;
    float gravity = 22.0f; // Slightly higher than earth gravity feels snappier in games
    float drag = 6.0f;     // Ground friction
    float airDrag = 1.0f;  // Air resistance
    
    bool onGround = false;

    Player(glm::vec3 startPos) 
        : position(startPos), velocity(0.0f) 
    {
        // Initialize camera at eye level
        camera.Position = position + glm::vec3(0, eyeLevel, 0);
    }

    // Call this every frame before rendering
    void Update(float deltaTime, GLFWwindow* window, ITerrainGenerator* terrain) {
        HandleInput(deltaTime, window);
        ApplyPhysics(deltaTime, terrain);
        
        // Sync camera position to player head
        camera.Position = position + glm::vec3(0, eyeLevel, 0);
    }

    // Proxy for mouse movement to keep main.cpp clean
    void ProcessMouseMovement(float xoffset, float yoffset) {
        camera.ProcessMouseMovement(xoffset, yoffset);
    }

private:
    void HandleInput(float dt, GLFWwindow* window) {
        float speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? runSpeed : walkSpeed;
        
        // Get generic direction vectors flattened to XZ plane (no flying)
        glm::vec3 forward = glm::normalize(glm::vec3(camera.Front.x, 0, camera.Front.z));
        glm::vec3 right = glm::normalize(glm::vec3(camera.Right.x, 0, camera.Right.z));
        
        glm::vec3 wishDir(0.0f);
        
        // Standard WASD
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wishDir += forward;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wishDir -= forward;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wishDir += right;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wishDir -= right;

        // Apply acceleration if we have input
        if (glm::length(wishDir) > 0.01f) {
            wishDir = glm::normalize(wishDir);
            
            // Ground acceleration is high for snappy movement, air control is lower
            float accel = onGround ? 50.0f : 8.0f; 
            
            velocity.x += wishDir.x * accel * dt;
            velocity.z += wishDir.z * accel * dt;
        }

        // Jump
        if (onGround && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
            velocity.y = jumpForce;
            onGround = false;
        }
    }

    void ApplyPhysics(float dt, ITerrainGenerator* terrain) {
        // 1. Apply Drag (Friction)
        float currentDrag = onGround ? drag : airDrag;
        // Simple damping
        velocity.x *= std::max(0.0f, 1.0f - currentDrag * dt);
        velocity.z *= std::max(0.0f, 1.0f - currentDrag * dt);

        // 2. Apply Gravity
        velocity.y -= gravity * dt;
        if (velocity.y < -50.0f) velocity.y = -50.0f; // Terminal velocity

        // 3. Move and Resolve Collisions per axis
        // We move the player, check if they are inside a block, and if so, push them back.
        
        // --- X Axis ---
        position.x += velocity.x * dt;
        if (CheckCollision(terrain)) {
            position.x -= velocity.x * dt; // Undo move
            velocity.x = 0;
        }

        // --- Z Axis ---
        position.z += velocity.z * dt;
        if (CheckCollision(terrain)) {
            position.z -= velocity.z * dt; // Undo move
            velocity.z = 0;
        }

        // --- Y Axis ---
        position.y += velocity.y * dt;
        onGround = false; // Assume in air until we hit floor
        if (CheckCollision(terrain)) {
            bool falling = velocity.y < 0;
            
            position.y -= velocity.y * dt; // Undo move
            velocity.y = 0;
            
            if (falling) {
                onGround = true;
            }
        }
        
        // Basic kill floor
        if (position.y < -50) {
            position = glm::vec3(0, 100, 0);
            velocity = glm::vec3(0);
        }
    }

    // Returns true if the player's bounding box intersects with any solid block
    bool CheckCollision(ITerrainGenerator* terrain) {
        // Define Player Bounding Box
        // Position is at the bottom center of the feet
        glm::vec3 min = position - glm::vec3(width/2, 0, width/2);
        glm::vec3 max = position + glm::vec3(width/2, height, width/2);

        // Convert to Voxel Coordinates
        int minX = static_cast<int>(std::floor(min.x));
        int maxX = static_cast<int>(std::floor(max.x));
        int minY = static_cast<int>(std::floor(min.y));
        int maxY = static_cast<int>(std::floor(max.y));
        int minZ = static_cast<int>(std::floor(min.z));
        int maxZ = static_cast<int>(std::floor(max.z));

        // Iterate over all voxels the player touches
        for (int x = minX; x <= maxX; x++) {
            for (int z = minZ; z <= maxZ; z++) {
                
                // Get terrain height for this column (optimization)
                int h = terrain->GetHeight(x, z);

                for (int y = minY; y <= maxY; y++) {
                    // Check block solidity (LOD 1 for accurate physics)
                    uint8_t block = terrain->GetBlock(x, y, z, h, 1);
                    
                    if (block != 0) { // 0 is Air
                        return true;
                    }
                }
            }
        }
        return false;
    }
};