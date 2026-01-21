#version 460 core

//layout(early_fragment_tests) in;

in vec3 v_Normal;
in vec2 v_TexCoord;
in float v_TexID;
in vec3 v_Color;

out vec4 FragColor;

void main()
{
    // Procedural Grid Pattern
    vec2 uv = v_TexCoord;
    bool pattern = (mod(floor(uv.x) + floor(uv.y), 2.0) == 0.0);
    
    // Colors
    vec3 baseColor = pattern ? vec3(0.8) : vec3(0.4);
    
    // Lighting (Simple Lambert)
    vec3 sunDir = normalize(vec3(0.2, 1.0, 0.3));
    float diff = max(dot(v_Normal, sunDir), 0.3); // 0.3 ambient

    // Tint based on block type/normals for now
    vec3 finalColor = baseColor * v_Color * diff;

    FragColor = vec4(finalColor, 1.0);
}