#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

enum Camera_Movement { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };

const float YAW         =  90.0f;
const float PITCH       =  0.0f;
const float SPEED       =  30.5f;
const float SENSITIVITY =  0.1f;
const float ZOOM        =  60.0f; 

class Camera
{
public:
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;

    float Yaw;
    float Pitch;
    float MovementSpeed;
    float MouseSensitivity;
    float Zoom;

    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH) 
        : Front(glm::vec3(0.0f, 0.0f, 0.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
    {
        Position = position;
        WorldUp = up;
        Yaw = yaw;
        Pitch = pitch;
        updateCameraVectors();
    }

    glm::mat4 GetViewMatrix() const {
        return glm::lookAt(Position, Position + Front, Up);
    }

    /**
     * GetProjectionMatrix (Reverse-Z Infinite)
     * ----------------------------------------
     * Standard OpenGL Projection: Near= -1, Far= 1. Low precision at distance.
     * This Projection:            Near= 1, Far= 0. High precision everywhere.
     * * WHY?
     * Floating point numbers have more precision near 0.
     * By mapping the Far Plane (infinity) to 0.0, we get huge precision for distant mountains.
     * This requires `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)` in main.cpp.
     */
    glm::mat4 GetProjectionMatrix(float aspectRatio, float zNear = 0.1f) const
    {
        float f = 1.0f / tan(glm::radians(Zoom) / 2.0f);
        glm::mat4 projection(0.0f);
        projection[0][0] = f / aspectRatio;
        projection[1][1] = f;
        projection[2][3] = -1.0f; 
        projection[3][2] = zNear; // Maps Near to 1.0, Far to 0.0
        return projection;
    }

    void ProcessKeyboard(Camera_Movement direction, float deltaTime) {
        float velocity = MovementSpeed * deltaTime;
        if (direction == FORWARD)  Position += Front * velocity;
        if (direction == BACKWARD) Position -= Front * velocity;
        if (direction == LEFT)     Position -= Right * velocity;
        if (direction == RIGHT)    Position += Right * velocity;
        if (direction == UP)       Position += WorldUp * velocity;
        if (direction == DOWN)     Position -= WorldUp * velocity;
    }

    void ProcessMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch = true) {
        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;
        Yaw   += xoffset;
        Pitch += yoffset;
        if (constrainPitch) {
            if (Pitch > 89.0f) Pitch = 89.0f;
            if (Pitch < -89.0f) Pitch = -89.0f;
        }
        updateCameraVectors();
    }

private:
    void updateCameraVectors() {
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
        Front = glm::normalize(front);
        Right = glm::normalize(glm::cross(Front, WorldUp));  
        Up    = glm::normalize(glm::cross(Right, Front));
    }
};