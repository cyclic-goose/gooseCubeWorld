#version 460 core
out vec4 FragColor;

uniform sampler2D u_DepthTexture;
uniform int u_MipLevel;
uniform vec2 u_ScreenSize; 

void main() {
    // Calculate UV based on gl_FragCoord
    vec2 uv = gl_FragCoord.xy / u_ScreenSize;
    
    // Sample the specific mip level
    // We use textureLod to force a specific level of the pyramid
    float depth = textureLod(u_DepthTexture, uv, u_MipLevel).r;

    // Visualization:
    // In Reverse-Z: 1.0 is Close (White), 0.0 is Far (Black).
    // Because the values drop off to <0.01 very quickly, we use a power function
    // to boost the dark values so they are visible to the human eye.
    float displayDepth = pow(depth, 0.1); 
    
    FragColor = vec4(vec3(displayDepth), 1.0);
}