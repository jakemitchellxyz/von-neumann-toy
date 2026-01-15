#version 120

// Fullscreen quad - vertices in normalized device coordinates [-1,1]
varying vec2 vScreenUV;  // Screen UV [0,1] for ray reconstruction

void main() {
    // Input: gl_Vertex = (x, y, 0, 1) where x,y in [-1, 1]
    // This is a fullscreen quad in NDC
    gl_Position = gl_Vertex;
    
    // Convert from NDC [-1,1] to screen UV [0,1]
    vScreenUV = gl_Vertex.xy * 0.5 + 0.5;
}