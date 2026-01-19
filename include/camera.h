#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <cmath>

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
const float YAW         =  90.0f;
const float PITCH       =  0.0f;
const float SPEED       =  30.5f;
const float SENSITIVITY =  0.1f;
const float ZOOM        =  60.0f; // This acts as our FOV

class Camera
{
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
    float Zoom;

    // Constructor with vectors
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH) 
        : Front(glm::vec3(0.0f, 0.0f, 0.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
    {
        Position = position;
        WorldUp = up;
        Yaw = yaw;
        Pitch = pitch;
        updateCameraVectors();
    }

    // Returns the view matrix calculated using Euler Angles and the LookAt Matrix
    glm::mat4 GetViewMatrix() const
    {
        return glm::lookAt(Position, Position + Front, Up);
    }

    // --------------------------------------------------------------------------------------
    // CUSTOM PROJECTION: INFINITE REVERSE Z
    // --------------------------------------------------------------------------------------
    // Creates a projection matrix where:
    // 1. Far plane is at Infinity.
    // 2. Z range is [0, 1] (Requires glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)).
    // 3. Reversed: Near plane maps to Z=1, Far (Infinity) maps to Z=0.
    //
    // This provides vastly superior depth precision for large open worlds.
    glm::mat4 GetProjectionMatrix(float aspectRatio, float zNear = 0.1f) const
    {
        float f = 1.0f / tan(glm::radians(Zoom) / 2.0f);

        glm::mat4 projection(0.0f); // Initialize to all zeros

        // Column 0
        projection[0][0] = f / aspectRatio;
        
        // Column 1
        projection[1][1] = f;

        // Column 2
        // Normally -1 for standard GL, but because we rely on zNear mapping to 1
        // and Far mapping to 0, the setup simplifies.
        projection[2][3] = -1.0f; 

        // Column 3
        // In Infinite Reverse Z:
        // NDC_Z = zNear / -ViewZ
        // If ViewZ = -zNear -> NDC_Z = 1.0
        // If ViewZ = -infinity -> NDC_Z = 0.0
        projection[3][2] = zNear; 

        return projection;
    }

    // Processes input received from any keyboard-like input system
    void ProcessKeyboard(Camera_Movement direction, float deltaTime)
    {
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

    // Processes input received from a mouse input system. Expects the offset value in both the x and y direction.
    void ProcessMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch = true)
    {
        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        Yaw   += xoffset;
        Pitch += yoffset;

        // Make sure that when pitch is out of bounds, screen doesn't get flipped
        if (constrainPitch)
        {
            if (Pitch > 89.0f)
                Pitch = 89.0f;
            if (Pitch < -89.0f)
                Pitch = -89.0f;
        }

        updateCameraVectors();
    }

    // Processes input received from a mouse scroll-wheel event. Only requires input on the vertical wheel-axis
    void ProcessMouseScroll(float yoffset)
    {
        Zoom -= (float)yoffset;
        if (Zoom < 1.0f)
            Zoom = 1.0f;
        if (Zoom > 90.0f)
            Zoom = 90.0f;
    }

private:
    // Calculates the front vector from the Camera's (updated) Euler Angles
    void updateCameraVectors()
    {
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
};