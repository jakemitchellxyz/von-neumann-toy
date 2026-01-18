#version 450

// Fullscreen quad vertex shader with UV output for ray marching
// Takes vertex positions in NDC space [-1, 1] from vertex buffer

layout(location = 0) in vec2 inPosition; // NDC position (-1 to 1)

layout(location = 0) out vec2 fragUV; // UV coordinates for fragment shader (0 to 1)

void main()
{
    // Output position in NDC space (fullscreen quad)
    gl_Position = vec4(inPosition, 0.0, 1.0);

    // Convert NDC position (-1 to 1) to UV coordinates (0 to 1)
    fragUV = inPosition * 0.5 + 0.5;
}
