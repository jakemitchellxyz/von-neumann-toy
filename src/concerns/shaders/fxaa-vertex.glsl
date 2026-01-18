#version 330 core

// FXAA vertex shader
// Renders a fullscreen quad for post-processing

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main()
{
    // Fullscreen quad: positions are in NDC space (-1 to 1)
    // Map to texture coordinates (0 to 1)
    vTexCoord = aTexCoord;

    gl_Position = vec4(aPosition, 0.0, 1.0);
}
