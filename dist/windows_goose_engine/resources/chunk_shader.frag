#version 330 core

// ------------------------------------------------------------------------
// Output: Final Pixel Color
// ------------------------------------------------------------------------
out vec4 FragColor;

// ------------------------------------------------------------------------
// Inputs from Vertex Shader
// ------------------------------------------------------------------------
in vec3 Normal;
in vec3 FragPos;

// ------------------------------------------------------------------------
// Function: main
// Inputs:   Interpolated Normal and FragPos
// Outputs:  FragColor (vec4 RGBA)
// Description: Calculates simple directional lighting so cubes look 3D
// ------------------------------------------------------------------------
void main()
{
    // Hardcoded color for the blocks (Greenish-Blue)
    vec3 objectColor = vec3(0.2f, 0.7f, 0.5f);
    
    // Hardcoded light direction (coming from up-right)
    vec3 lightDir = normalize(vec3(0.5f, 1.0f, 0.3f));
    
    // Ambient component (base brightness so shadows aren't pitch black)
    float ambientStrength = 0.3f;
    vec3 ambient = ambientStrength * vec3(1.0f, 1.0f, 1.0f);
    
    // Diffuse component (directional brightness based on face angle)
    vec3 norm = normalize(Normal);
    float diff = max(dot(norm, lightDir), 0.0f);
    vec3 diffuse = diff * vec3(1.0f, 1.0f, 1.0f);
    
    // Combine results
    vec3 result = (ambient + diffuse) * objectColor;
    
    FragColor = vec4(result, 1.0f);
}
