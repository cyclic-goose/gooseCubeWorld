#version 460 core

// 2D thread group allows 32x32 pixels per group efficiently
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

// Source image (Mip Level N)
layout(binding = 0, r32f) readonly uniform image2D u_InputImg;

// Destination image (Mip Level N+1)
layout(binding = 1, r32f) writeonly uniform image2D u_OutputImg;

// The size of the OUTPUT image
uniform ivec2 u_OutDimension;

void main() {
    ivec2 outCoord = ivec2(gl_GlobalInvocationID.xy);
    
    // Bounds check
    if (outCoord.x >= u_OutDimension.x || outCoord.y >= u_OutDimension.y) {
        return;
    }

    // Corresponding top-left pixel in the input image (2x upscaling coordinate)
    ivec2 inCoord = outCoord * 2;

    // Fetch 4 texels from the previous mip level
    // We use imageLoad instead of texture() to avoid sampler filtering issues
    float d1 = imageLoad(u_InputImg, inCoord + ivec2(0, 0)).r;
    float d2 = imageLoad(u_InputImg, inCoord + ivec2(1, 0)).r;
    float d3 = imageLoad(u_InputImg, inCoord + ivec2(0, 1)).r;
    float d4 = imageLoad(u_InputImg, inCoord + ivec2(1, 1)).r;

    // --- REVERSE-Z REDUCTION ---
    // In Reverse-Z (Near=1, Far=0), "Further Away" means a LOWER value.
    // For conservative culling, we need the "Furthest" value in the tile.
    // If we used Max, a single close pixel (1.0) would hide a hole (0.0) behind it.
    // We want the hole (0.0) to be propagated so we don't accidentally cull visible objects behind it.
    float minDepth = min(min(d1, d2), min(d3, d4));

    imageStore(u_OutputImg, outCoord, vec4(minDepth, 0.0, 0.0, 0.0));
}