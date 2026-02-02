#version 460 core

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

layout(binding = 0, r32f) readonly uniform image2D u_InputImg;
layout(binding = 1, r32f) writeonly uniform image2D u_OutputImg;

uniform vec2 u_OutDimension;
uniform vec2 u_InDimension;
uniform bool u_IsCopyPass; //Toggle for the first pass

void main() {
    ivec2 outCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 outDim = ivec2(u_OutDimension);
    ivec2 inDim = ivec2(u_InDimension);

    if (outCoord.x >= outDim.x || outCoord.y >= outDim.y) {
        return;
    }
    

    ivec2 inCoord = outCoord * 2;

    // Fetch 4 samples with boundary checks
    // Default to 1.0 (Near) for Reverse-Z so we don't pollute the MIN reduction.
    float d1 = 1.0; 
    float d2 = 1.0; 
    float d3 = 1.0; 
    float d4 = 1.0;

    if (inCoord.x < inDim.x && inCoord.y < inDim.y)
        d1 = imageLoad(u_InputImg, inCoord + ivec2(0, 0)).r;

    if (inCoord.x + 1 < inDim.x && inCoord.y < inDim.y)
        d2 = imageLoad(u_InputImg, inCoord + ivec2(1, 0)).r;

    if (inCoord.x < inDim.x && inCoord.y + 1 < inDim.y)
        d3 = imageLoad(u_InputImg, inCoord + ivec2(0, 1)).r;

    if (inCoord.x + 1 < inDim.x && inCoord.y + 1 < inDim.y)
        d4 = imageLoad(u_InputImg, inCoord + ivec2(1, 1)).r;

    float minDepth = min(min(d1, d2), min(d3, d4));

    // Extra sampling for odd dimensions (Non-Power-Of-Two)
    // If the input width is odd, the last column of output needs to cover 3 input pixels horizontally
    // If the input height is odd, the last row needs to cover 3 input pixels vertically
    
    // Check Right Edge (Extra Column x+2)
    if ((inDim.x & 1) != 0 && outCoord.x == outDim.x - 1) {
        float extra = 1.0;
        if (inCoord.x + 2 < inDim.x && inCoord.y < inDim.y)
             extra = imageLoad(u_InputImg, inCoord + ivec2(2, 0)).r;
        minDepth = min(minDepth, extra);
        
        if (inCoord.x + 2 < inDim.x && inCoord.y + 1 < inDim.y) {
             extra = imageLoad(u_InputImg, inCoord + ivec2(2, 1)).r;
             minDepth = min(minDepth, extra);
        }
    }

    // Check Bottom Edge (Extra Row y+2)
    if ((inDim.y & 1) != 0 && outCoord.y == outDim.y - 1) {
        float extra = 1.0;
        if (inCoord.x < inDim.x && inCoord.y + 2 < inDim.y)
            extra = imageLoad(u_InputImg, inCoord + ivec2(0, 2)).r;
        minDepth = min(minDepth, extra);

        if (inCoord.x + 1 < inDim.x && inCoord.y + 2 < inDim.y) {
            extra = imageLoad(u_InputImg, inCoord + ivec2(1, 2)).r;
            minDepth = min(minDepth, extra);
        }
    }

    // Check Corner (x+2, y+2)
    if ((inDim.x & 1) != 0 && (inDim.y & 1) != 0 && 
        outCoord.x == outDim.x - 1 && outCoord.y == outDim.y - 1) {
        
        float extra = 1.0;
        if (inCoord.x + 2 < inDim.x && inCoord.y + 2 < inDim.y)
            extra = imageLoad(u_InputImg, inCoord + ivec2(2, 2)).r;
        minDepth = min(minDepth, extra);
    }

    imageStore(u_OutputImg, outCoord, vec4(minDepth, 0.0, 0.0, 0.0));
}