#version 450

// 2D UI overlay fragment shader
// Simple pass-through with alpha blending

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main()
{
    // Output color with alpha for blending
    outColor = fragColor;
}
