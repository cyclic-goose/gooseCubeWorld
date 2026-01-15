#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Defines several possible options for camera movement
enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

// Default camera values
const float YAW         = -90.0f;
const float PITCH       =  0.0f;
const float SPEED       =  10.0f; // Blocks per second
const float SENSITIVITY =  0.1f;

class Camera {
public:
    // Camera Attributes
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;

    // Euler Angles
    float Yaw;
    float Pitch;

    // Camera options
    float MovementSpeed;
    float MouseSensitivity;

    // ------------------------------------------------------------------------
    // Function: Constructor
    // Inputs:   position (vec3) - Starting world position
    //           up (vec3)       - World up vector (usually 0,1,0)
    //           yaw (float)     - Initial horizontal angle
    //           pitch (float)   - Initial vertical angle
    // Outputs:  None            - Initializes vectors
    // ------------------------------------------------------------------------
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH);

    // ------------------------------------------------------------------------
    // Function: GetViewMatrix
    // Inputs:   None
    // Outputs:  mat4 - The LookAt matrix for the vertex shader
    // ------------------------------------------------------------------------
    glm::mat4 GetViewMatrix();

    // ------------------------------------------------------------------------
    // Function: ProcessKeyboard
    // Inputs:   direction (Enum) - Which WASD key was pressed
    //           deltaTime (float) - Time between frames (for smooth speed)
    // Outputs:  void             - Updates Camera Position
    // ------------------------------------------------------------------------
    void ProcessKeyboard(Camera_Movement direction, float deltaTime);

    // ------------------------------------------------------------------------
    // Function: ProcessMouseMovement
    // Inputs:   xoffset (float)     - Mouse X movement since last frame
    //           yoffset (float)     - Mouse Y movement since last frame
    //           constrainPitch (bool) - Prevent screen flipping (default true)
    // Outputs:  void                - Updates Yaw, Pitch, and Front vector
    // ------------------------------------------------------------------------
    void ProcessMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch = true);

private:
    // ------------------------------------------------------------------------
    // Function: updateCameraVectors
    // Inputs:   None
    // Outputs:  void - Recalculates Front, Right, and Up based on Euler angles
    // ------------------------------------------------------------------------
    void updateCameraVectors();
};