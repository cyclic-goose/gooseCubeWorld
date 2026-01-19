#version 460 core

in vec3 v_Normal;
in vec2 v_TexCoord;
in float v_TexID;
in vec3 v_Color;

out vec4 FragColor;

void main()
{
    // Procedural Grid Check (No Texture Required)
    // Creates a 1x1 checkerboard pattern on the blocks.
    // If you see this pattern cleanly, your UVs and Geometry are correct.
    vec2 uv = v_TexCoord;
    bool pattern = (mod(floor(uv.x) + floor(uv.y), 2.0) == 0.0);
    
    vec3 baseColor = pattern ? vec3(0.8) : vec3(0.4);
    
    // Tint based on Normal to see faces
    vec3 tint = v_Color;
    
    // Simple Lambert Lighting
    vec3 sunDir = normalize(vec3(0.2, 1.0, 0.3));
    float diff = max(dot(v_Normal, sunDir), 0.3); // 0.3 is Ambient floor

    FragColor = vec4(baseColor * tint * diff, 1.0);
}
