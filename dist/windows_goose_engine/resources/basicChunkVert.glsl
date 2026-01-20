#version 460 core

// ------------------------------------------------------------------------
// Attribute: Position
// Location:  0 (Matches Chunk::updateMesh glVertexAttribPointer(0...))
// Type:      vec3
// ------------------------------------------------------------------------
layout (location = 0) in vec3 aPos;

// ------------------------------------------------------------------------
// Attribute: Normal
// Location:  1 (Matches Chunk::updateMesh glVertexAttribPointer(1...))
// Type:      vec3
// ------------------------------------------------------------------------
layout (location = 1) in vec3 aNormal;

// ------------------------------------------------------------------------
// Outputs to Fragment Shader
// ------------------------------------------------------------------------
out vec3 Normal;
out vec3 FragPos;

// ------------------------------------------------------------------------
// Uniforms (Transformation Matrices)
// ------------------------------------------------------------------------
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// ------------------------------------------------------------------------
// Function: main
// Inputs:   Attributes (aPos, aNormal), Uniforms (matrices)
// Outputs:  gl_Position (Clip space coords), Normal/FragPos (World space)
// ------------------------------------------------------------------------
void main()
{
    // Calculate final vertex position in clip space
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    
    // Pass world position to fragment shader
    FragPos = vec3(model * vec4(aPos, 1.0));
    
    // Pass normal to fragment shader
    // Note: inverse/transpose is needed to handle non-uniform scaling correctly
    Normal = mat3(transpose(inverse(model))) * aNormal; 
}
