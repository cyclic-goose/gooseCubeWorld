#version 460 core

in vec3 v_Normal;
in vec2 v_TexCoord;
in float v_TexID;
in vec3 v_Color;

out vec4 FragColor;

void main()
{
    // Basic lighting to show depth (Simple Lambert)
    vec3 sunDir = normalize(vec3(0.5, 0.8, 0.3));
    float diff = max(dot(v_Normal, sunDir), 0.2); // 0.2 ambient
    
    // Base color
    vec3 objectColor = vec3(0.7, 0.7, 0.7); // Grey default
    
    // If ID is 1 (Stone/Block), make it slightly reddish/brown
    if (v_TexID == 1.0) {
        objectColor = vec3(0.8, 0.4, 0.4); 
    }

    // Combine
    vec3 finalColor = objectColor * diff;

    FragColor = vec4(finalColor, 1.0);
}
