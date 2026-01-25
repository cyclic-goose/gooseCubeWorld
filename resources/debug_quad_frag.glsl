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
    FragColor = vec4(vec3(depth*1000.0), 1.0);
}