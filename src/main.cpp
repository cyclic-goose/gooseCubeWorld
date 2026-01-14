#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include "constants.h"


GeneralSettings generalSettings;



// Function Prototypes
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void processInput(GLFWwindow *window);

int main()
{

    // =========================================================================================
    // GLFW (WINDOW LIBRARY) and GLAD (OpenGL Loader) Init
    // =========================================================================================
    // GLFW Init
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, generalSettings.OPEN_GL_CORE_MAJOR);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, generalSettings.OPEN_GL_CORE_MINOR);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // create a window
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "Goose Voxels", primaryMonitor, NULL);

    glfwMakeContextCurrent(window);
    
    // Register Callbacks (Resize, Mouse Move)
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);

    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Initialize GLAD (OpenGL Function Pointers)
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }


    // New Scope for loop
    {
        
        // =========================================================================================
        // RENDER LOOP ENTRY
        // =========================================================================================
        while (!glfwWindowShouldClose(window))
            {
                processInput(window);// process user input 



                glfwSwapBuffers(window);
                glfwPollEvents();
            }
        // =========================================================================================
        // RENDER LOOP ENTRY
        // =========================================================================================

    }
   
    // POST RENDER LOOP, CLEANUP


    glfwTerminate();
    return 0;

}

// framebuffer callback definition
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{

}

void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

}
