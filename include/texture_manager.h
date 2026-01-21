#pragma once

#include <glad/glad.h>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>

// Define this ONLY in one .cpp file (e.g., main.cpp) if not already defined
// #define STB_IMAGE_IMPLEMENTATION 
#include "stb_image.h" // Ensure you have this header

class TextureManager {
public:
    // Creates a GL_TEXTURE_2D_ARRAY from a list of file paths.
    // Assumes all textures have the same width/height/channels as the first one.
    static GLuint LoadTextureArray(const std::vector<std::string>& filePaths, bool generateMipmaps = true) {
        if (filePaths.empty()) return 0;

        int width, height, nrChannels;
        
        // 1. Load the first image to determine dimensions
        unsigned char* data = stbi_load(filePaths[0].c_str(), &width, &height, &nrChannels, 0);
        if (!data) {
            std::cerr << "[TextureManager] Failed to load first texture: " << filePaths[0] << std::endl;
            return 0;
        }

        // We force 4 channels (RGBA) for consistency in the shader
        GLenum format = GL_RGBA8;
        GLenum sourceFormat = GL_RGBA; // usually determined by nrChannels, but we'll force load as 4 if needed
        stbi_image_free(data); // Free the probe, we'll reload it in the loop properly

        // 2. Create the Texture Array
        GLuint textureID;
        glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &textureID);
        
        // Calculate Mipmap Levels
        int levels = 1;
        if (generateMipmaps) {
            levels = (int)floor(log2(std::max(width, height))) + 1;
        }

        // 3. Allocate Immutable Storage on GPU
        // This is more efficient than glTexImage3D as the driver can optimize memory layout ahead of time.
        glTextureStorage3D(textureID, levels, format, width, height, (GLsizei)filePaths.size());

        // 4. Set Texture Parameters
        glTextureParameteri(textureID, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(textureID, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTextureParameteri(textureID, GL_TEXTURE_MIN_FILTER, generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTextureParameteri(textureID, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // Nearest is often better for Voxel games
        
        // Anisotropic Filtering (Max Quality)
        if (generateMipmaps) {
            GLfloat maxAnisotropy = 0.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAnisotropy);
            glTextureParameterf(textureID, GL_TEXTURE_MAX_ANISOTROPY, maxAnisotropy);
        }

        // 5. Load and Upload Images Layer by Layer
        stbi_set_flip_vertically_on_load(true); // Align with OpenGL bottom-left UVs

        for (size_t i = 0; i < filePaths.size(); ++i) {
            int w, h, c;
            // Force 4 channels (RGBA) to match storage format
            unsigned char* imgData = stbi_load(filePaths[i].c_str(), &w, &h, &c, 4); 
            
            if (imgData) {
                if (w != width || h != height) {
                    std::cerr << "[TextureManager] Mismatch dimension for " << filePaths[i] 
                              << ". Expected " << width << "x" << height 
                              << ", got " << w << "x" << h << ". Skipping upload." << std::endl;
                } else {
                    // Upload to Layer 'i'
                    glTextureSubImage3D(textureID, 0, 0, 0, (GLint)i, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, imgData);
                }
                stbi_image_free(imgData);
            } else {
                std::cerr << "[TextureManager] Failed to load texture: " << filePaths[i] << std::endl;
            }
        }

        // 6. Generate Mipmaps
        if (generateMipmaps) {
            glGenerateTextureMipmap(textureID);
        }

        std::cout << "[TextureManager] Created Texture Array with " << filePaths.size() << " layers. (ID: " << textureID << ")" << std::endl;
        return textureID;
    }
};