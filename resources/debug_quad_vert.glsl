#version 460 core

// Generates a full screen triangle that covers the screen
// No VBO needed, just draw 3 vertices
void main() {
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 2);
    gl_Position = vec4(x, y, 0.0, 1.0);
}