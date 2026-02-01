#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp> // Required for glm::value_ptr

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

class Shader {
public:
    unsigned int ID;

    // ------------------------------------------------------------------------
    // Constructor: Vertex + Fragment Shaders
    // ------------------------------------------------------------------------
    Shader(const char* vertexPath, const char* fragmentPath) {
        std::string vertexCode, fragmentCode;
        std::ifstream vShaderFile, fShaderFile;
        
        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        
        try {
            vShaderFile.open(vertexPath);
            fShaderFile.open(fragmentPath);
            std::stringstream vShaderStream, fShaderStream;
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();
        } catch (std::ifstream::failure& e) {
            std::cout << "[ERROR] SHADER FILE NOT READ: " << e.what() << std::endl;
        }

        const char* vShaderCode = vertexCode.c_str();
        const char * fShaderCode = fragmentCode.c_str();
        unsigned int vertex, fragment;
        
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);
        checkCompileErrors(vertex, "VERTEX");
        
        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");
        
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        checkCompileErrors(ID, "PROGRAM");
        
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }

    // ------------------------------------------------------------------------
    // Constructor: Compute Shader
    // ------------------------------------------------------------------------
    Shader(const char* computePath) {
        std::string computeCode;
        std::ifstream cShaderFile;
        
        cShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        
        try {
            cShaderFile.open(computePath);
            std::stringstream cShaderStream;
            cShaderStream << cShaderFile.rdbuf();
            computeCode = cShaderStream.str();
        } catch (std::ifstream::failure& e) {
            std::cout << "[ERROR] COMPUTE SHADER FILE NOT READ (" << computePath << "): " << e.what() << std::endl;
        }

        const char* cShaderCode = computeCode.c_str();
        unsigned int compute;
        
        compute = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(compute, 1, &cShaderCode, NULL);
        glCompileShader(compute);
        checkCompileErrors(compute, "COMPUTE");
        
        ID = glCreateProgram();
        glAttachShader(ID, compute);
        glLinkProgram(ID);
        checkCompileErrors(ID, "PROGRAM");
        
        glDeleteShader(compute);
    }

    // ------------------------------------------------------------------------
    // State Management
    // ------------------------------------------------------------------------
    void use() const { 
        glUseProgram(ID); 
    }
    
    // ------------------------------------------------------------------------
    // Uniform Setters
    // ------------------------------------------------------------------------
    void setBool(const std::string &name, bool value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
    }

    void setInt(const std::string &name, int value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
    }

    void setUInt(const std::string &name, unsigned int value) const {
        glUniform1ui(glGetUniformLocation(ID, name.c_str()), value);
    }

    void setFloat(const std::string &name, float value) const {
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }

    // --- Vec2 ---
    void setVec2(const std::string &name, const glm::vec2 &value) const {
        glUniform2fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
    }
    void setVec2(const std::string &name, float x, float y) const {
        glUniform2f(glGetUniformLocation(ID, name.c_str()), x, y);
    }

    // --- Vec3 ---
    void setVec3(const std::string &name, const glm::vec3 &value) const {
        glUniform3fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
    }
    void setVec3(const std::string &name, float x, float y, float z) const {
        glUniform3f(glGetUniformLocation(ID, name.c_str()), x, y, z);
    }

    // --- Vec4 ---
    void setVec4(const std::string &name, const glm::vec4 &value) const {
        glUniform4fv(glGetUniformLocation(ID, name.c_str()), 1, &value[0]);
    }
    void setVec4(const std::string &name, float x, float y, float z, float w) const {
        glUniform4f(glGetUniformLocation(ID, name.c_str()), x, y, z, w);
    }

    // --- Matrices ---
    void setMat2(const std::string &name, const glm::mat2 &mat) const {
        glUniformMatrix2fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }

    void setMat3(const std::string &name, const glm::mat3 &mat) const {
        glUniformMatrix3fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }

    void setMat4(const std::string &name, const glm::mat4 &mat) const {
        glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
    }
    
    // Keep the raw pointer version just in case
    void setMat4(const std::string &name, const float* value) const {
        glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, value);
    }

private:
    void checkCompileErrors(unsigned int shader, std::string type) {
        int success;
        char infoLog[1024];
        if (type != "PROGRAM") {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                glGetShaderInfoLog(shader, 1024, NULL, infoLog);
                std::cout << "[ERROR] SHADER_COMPILATION (" << type << "):\n" << infoLog << std::endl;
            }
        } else {
            glGetProgramiv(shader, GL_LINK_STATUS, &success);
            if (!success) {
                glGetProgramInfoLog(shader, 1024, NULL, infoLog);
                std::cout << "[ERROR] PROGRAM_LINKING:\n" << infoLog << std::endl;
            }
        }
    }
};
#endif