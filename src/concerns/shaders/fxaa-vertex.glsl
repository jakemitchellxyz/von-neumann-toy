#version 120

// FXAA vertex shader
// Renders a fullscreen quad for post-processing

varying vec2 vTexCoord;

void main()
{
    // Fullscreen quad: positions are in NDC space (-1 to 1)
    // Map to texture coordinates (0 to 1)
    vTexCoord = gl_MultiTexCoord0.xy;
    
    gl_Position = gl_Vertex;
}
