#version 460 core

// 2D thread group allows 32x32 pixels per group efficiently
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

// Source image (Mip Level N)
layout(binding = 0, r32f) readonly uniform image2D u_InputImg;

// Destination image (Mip Level N+1)
layout(binding = 1, r32f) writeonly uniform image2D u_OutputImg;

// Dimensions
uniform vec2 u_OutDimension;
uniform vec2 u_InDimension; // Added: Needed to check for odd-size edge cases

void main() {
    ivec2 outCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outDim = ivec2(u_OutDimension);
    ivec2 inDim = ivec2(u_InDimension);

    // Bounds check
    if (outCoord.x >= outDim.x || outCoord.y >= outDim.y) {
        return;
    }

    // Corresponding top-left pixel in the input image (2x upscaling coordinate)
    ivec2 inCoord = outCoord * 2;

    // Fetch 4 basic texels
    float d1 = imageLoad(u_InputImg, inCoord + ivec2(0, 0)).r;
    float d2 = imageLoad(u_InputImg, inCoord + ivec2(1, 0)).r;
    float d3 = imageLoad(u_InputImg, inCoord + ivec2(0, 1)).r;
    float d4 = imageLoad(u_InputImg, inCoord + ivec2(1, 1)).r;

    float minDepth = min(min(d1, d2), min(d3, d4));

    // --- EDGE CASE HANDLING (NPOT DRIFT FIX) ---
    // If the input dimension is odd, the last column/row of the output
    // needs to include the "extra" orphan pixel from the input.
    // Otherwise, the top/right of the screen gets ignored, causing the row below it
    // to stretch up, leading to incorrect occlusion (Sky becomes Mountain).
    
    bool isLastX = (outCoord.x == outDim.x - 1);
    bool isLastY = (outCoord.y == outDim.y - 1);
    
    // Check Right Edge (Extra Column)
    if (isLastX && (inDim.x & 1) != 0) {
        float dExtra1 = imageLoad(u_InputImg, inCoord + ivec2(2, 0)).r;
        float dExtra2 = imageLoad(u_InputImg, inCoord + ivec2(2, 1)).r;
        minDepth = min(minDepth, min(dExtra1, dExtra2));
    }

    // Check Bottom/Top Edge (Extra Row)
    if (isLastY && (inDim.y & 1) != 0) {
        float dExtra3 = imageLoad(u_InputImg, inCoord + ivec2(0, 2)).r;
        float dExtra4 = imageLoad(u_InputImg, inCoord + ivec2(1, 2)).r;
        minDepth = min(minDepth, min(dExtra3, dExtra4));
    }

    // Check Corner (Extra Pixel)
    if (isLastX && isLastY && (inDim.x & 1) != 0 && (inDim.y & 1) != 0) {
        float dCorner = imageLoad(u_InputImg, inCoord + ivec2(2, 2)).r;
        minDepth = min(minDepth, dCorner);
    }

    imageStore(u_OutputImg, outCoord, vec4(minDepth, 0.0, 0.0, 0.0));
}