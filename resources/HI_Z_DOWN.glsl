#version 460 core

// 2D thread group allows 32x32 pixels per group efficiently
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

// Source image (Mip Level N)
layout(binding = 0, r32f) readonly uniform image2D u_InputImg;

// Destination image (Mip Level N+1)
layout(binding = 1, r32f) writeonly uniform image2D u_OutputImg;

// changed from ivec2 to vec2 because C++ sends it as setVec2 (floats).
// sending floats to an int uniform causes the update to fail in GLSL.
uniform vec2 u_OutDimension;

void main() {
    ivec2 outCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 dim = ivec2(u_OutDimension);


    if (outCoord.x >= dim.x || outCoord.y >= dim.y) {
        return;
    }


    ivec2 inCoord = outCoord * 2;


    float d1 = imageLoad(u_InputImg, inCoord + ivec2(0, 0)).r;
    float d2 = imageLoad(u_InputImg, inCoord + ivec2(1, 0)).r;
    float d3 = imageLoad(u_InputImg, inCoord + ivec2(0, 1)).r;
    float d4 = imageLoad(u_InputImg, inCoord + ivec2(1, 1)).r;
    
    float minDepth = min(min(d1, d2), min(d3, d4));

    imageStore(u_OutputImg, outCoord, vec4(minDepth, 0.0, 0.0, 0.0));
}