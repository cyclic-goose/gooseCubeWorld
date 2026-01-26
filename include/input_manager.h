#pragma once
#include <GLFW/glfw3.h>
#include <unordered_map>

class Input {
public:
    // Returns true ONLY on the first frame the key is pressed (Rising Edge)
    static bool IsJustPressed(GLFWwindow* window, int key) {
        static std::unordered_map<int, bool> keyStates;
        
        bool isPressed = glfwGetKey(window, key) == GLFW_PRESS;
        bool wasPressed = keyStates[key];
        
        // Update state
        keyStates[key] = isPressed;

        // Return true only if pressed now but wasn't before
        return isPressed && !wasPressed;
    }

    // Returns true as long as the key is held down
    static bool IsDown(GLFWwindow* window, int key) {
        return glfwGetKey(window, key) == GLFW_PRESS;
    }
};