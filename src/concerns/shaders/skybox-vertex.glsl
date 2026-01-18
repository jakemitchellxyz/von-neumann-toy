#version 450
#extension GL_EXT_scalar_block_layout : require

// Skybox vertex shader for Vulkan 1.3

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

layout(location = 0) out vec2 vTexCoord;

// Model-View-Projection matrix
layout(set = 0, binding = 0, scalar) uniform Uniforms
{
    mat4 uMVP;
};

void main()
{
    vTexCoord = aTexCoord;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
