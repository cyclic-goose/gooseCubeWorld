#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// the goal is to create a lightweight struct that holds everything the game render loop needs per frame, things that change often
// but doesnt hold data like texture or vertices as those are handles by buffers and memory
// "data for this frame", i may even find later that certain things should be taken out such as sun direction, it should probably be handled by another system

class Camera; // forward declare 
struct EngineConfig; // forward declare

struct RenderContext {
    // 1. Time & Frame Data
    float deltaTime;
    float globalTime;
    uint64_t frameNumber;

    // 2. Camera / View Data (The most critical part)
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::mat4 viewProjection;     // Pre-calculated for speed
    glm::mat4 prevViewProjection; // For temporal effects / occlusion
    glm::vec3 cameraPosition;
    glm::vec3 cameraFront;        // Useful for LOD calculations

    // 3. Screen / Window Data
    int screenWidth;
    int screenHeight;
    float aspectRatio;

    // 4. Global Scene State (Lighting/Environment)
    glm::vec3 sunDirection;
    glm::vec3 sunColor;
    float fogDensity;

    // 5. Debug / Config Flags (Optional, but very useful)
    bool wireframeMode;
    bool debugFreezeCulling;
};