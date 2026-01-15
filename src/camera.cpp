#include "camera.h"

// ------------------------------------------------------------------------
// Function: Constructor
// Inputs:   position (vec3) - Starting world position
//           up (vec3)       - World up vector
//           yaw (float)     - Initial horizontal angle
//           pitch (float)   - Initial vertical angle
// Outputs:  None            - Sets vars and calculates initial Front vector
// ------------------------------------------------------------------------
Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch) 
    : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY) 
{
    Position = position;
    WorldUp = up;
    Yaw = yaw;
    Pitch = pitch;
    updateCameraVectors();
}

// ------------------------------------------------------------------------
// Function: GetViewMatrix
// Inputs:   None
// Outputs:  mat4 - Returns the LookAt matrix calculated from Pos and Front
// ------------------------------------------------------------------------
glm::mat4 Camera::GetViewMatrix() {
    return glm::lookAt(Position, Position + Front, Up);
}

// ------------------------------------------------------------------------
// Function: ProcessKeyboard
// Inputs:   direction (Enum) - The direction to move
//           deltaTime (float) - Delta time to ensure consistent speed
// Outputs:  void             - Modifies Position vector
// ------------------------------------------------------------------------
void Camera::ProcessKeyboard(Camera_Movement direction, float deltaTime) {
    float velocity = MovementSpeed * deltaTime;
    if (direction == FORWARD)
        Position += Front * velocity;
    if (direction == BACKWARD)
        Position -= Front * velocity;
    if (direction == LEFT)
        Position -= Right * velocity;
    if (direction == RIGHT)
        Position += Right * velocity;
    if (direction == UP)
        Position += WorldUp * velocity;
    if (direction == DOWN)
        Position -= WorldUp * velocity;
}

// ------------------------------------------------------------------------
// Function: ProcessMouseMovement
// Inputs:   xoffset (float) - Offset in X direction
//           yoffset (float) - Offset in Y direction
//           constrainPitch (bool) - Limits pitch to +/- 89 degrees
// Outputs:  void - Updates Euler angles and re-calculates vectors
// ------------------------------------------------------------------------
void Camera::ProcessMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch) {
    xoffset *= MouseSensitivity;
    yoffset *= MouseSensitivity;

    Yaw   += xoffset;
    Pitch += yoffset;

    // Make sure that when pitch is out of bounds, screen doesn't get flipped
    if (constrainPitch) {
        if (Pitch > 89.0f)
            Pitch = 89.0f;
        if (Pitch < -89.0f)
            Pitch = -89.0f;
    }

    // Update Front, Right and Up Vectors using the updated Euler angles
    updateCameraVectors();
}

// ------------------------------------------------------------------------
// Function: updateCameraVectors
// Inputs:   None
// Outputs:  void - Calculates the new Front vector using Trig
// ------------------------------------------------------------------------
void Camera::updateCameraVectors() {
    // Calculate the new Front vector
    glm::vec3 front;
    front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    front.y = sin(glm::radians(Pitch));
    front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Front = glm::normalize(front);
    
    // Also re-calculate the Right and Up vector
    Right = glm::normalize(glm::cross(Front, WorldUp));  
    Up    = glm::normalize(glm::cross(Right, Front));
}