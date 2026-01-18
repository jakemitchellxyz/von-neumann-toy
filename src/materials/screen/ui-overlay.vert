#version 450

// 2D UI overlay vertex shader
// Takes vertex positions in NDC space (same as screen pipeline for shared fullscreen quad)

layout(location = 0) in vec2 inPosition; // NDC position (-1 to 1)
layout(location = 1) in vec4 inColor;    // Vertex color (RGBA)

layout(location = 0) out vec4 fragColor;

void main()
{
    // Position is already in NDC space (same coordinate system as screen pipeline)
    gl_Position = vec4(inPosition, 0.0, 1.0);
    fragColor = inColor;
}
