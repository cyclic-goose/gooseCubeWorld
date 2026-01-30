#pragma once

#include <glad/glad.h>
#include <cassert>
#include <glm/glm.hpp>
#include <string>
#include <iostream>
#include <algorithm> // Required for std::max
#include <cmath>     // Required for floor, log2

// --- FRAMEBUFFER RESOURCES ---
struct FramebufferResources {
    GLuint fbo = 0;
    GLuint depthTex = 0; // Render Target (Depth Component)
    GLuint hiZTex = 0;   // Compute Target (R32F)
    GLuint colorTex = 0; 
    int width = 0;
    int height = 0;

    void Resize(int w, int h) {
        // 1. Safety Check: Prevent 0 or negative dimensions
        if (w <= 0 || h <= 0) {
            std::cerr << "[FBO] Warning: Attempted to resize to invalid dimensions (" << w << "x" << h << "). Skipping." << std::endl;
            return; 
        }

        if (width == w && height == h) return;
        
        std::cout << "[FBO] Resizing buffer to: " << w << "x" << h << std::endl;
        width = w; height = h;

        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (depthTex) glDeleteTextures(1, &depthTex);
        if (hiZTex) glDeleteTextures(1, &hiZTex);
        if (colorTex) glDeleteTextures(1, &colorTex);

        // 2. Create Render Depth Texture (Fixed: uses Depth Component)
        // This is strictly for rendering the scene.
        glCreateTextures(GL_TEXTURE_2D, 1, &depthTex);
        glTextureStorage2D(depthTex, 1, GL_DEPTH_COMPONENT32F, width, height);
        
        glTextureParameteri(depthTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(depthTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(depthTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(depthTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 3. Create Hi-Z Texture (Fixed: uses R32F)
        // This is for the Compute Shader downsampling.
        glCreateTextures(GL_TEXTURE_2D, 1, &hiZTex);
        
        // Safety: Ensure at least 1 level, prevent log2(0)
        int levels = 1 + (int)floor(log2(std::max(1, std::max(width, height))));
        glTextureStorage2D(hiZTex, levels, GL_R32F, width, height);

        glTextureParameteri(hiZTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTextureParameteri(hiZTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTextureParameteri(hiZTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(hiZTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 4. Create Color Texture
        glCreateTextures(GL_TEXTURE_2D, 1, &colorTex);
        glTextureStorage2D(colorTex, 1, GL_RGBA8, width, height);

        // 5. Assemble FBO
        glCreateFramebuffers(1, &fbo);
        glNamedFramebufferTexture(fbo, GL_DEPTH_ATTACHMENT, depthTex, 0); // Valid Depth Attachment
        glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, colorTex, 0);

        GLenum status = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Framebuffer Incomplete! Status: " << status 
                      << " (0x" << std::hex << status << std::dec << ")" << std::endl;
            
            // Helpful debug for common error codes
            if (status == GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT) 
                std::cerr << " -> GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT (Usually invalid texture size or format)" << std::endl;
        }
    }
} g_fbo;