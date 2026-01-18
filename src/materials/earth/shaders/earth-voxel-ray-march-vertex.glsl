#version 450
#extension GL_EXT_scalar_block_layout : require

// Voxel Ray Marching Vertex Shader
// Renders a fullscreen quad covering the camera (same as atmosphere rendering)

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vRayDir; // Ray direction in world space

// Uniform block for non-opaque uniforms (required for Vulkan)
layout(set = 0, binding = 0, scalar) uniform Uniforms
{
    vec3 uCameraPos;    // Camera position in world space
    vec3 uCameraDir;    // Camera forward direction (normalized)
    vec3 uCameraRight;  // Camera right direction (normalized)
    vec3 uCameraUp;     // Camera up direction (normalized)
    float uCameraFOV;   // Camera field of view (radians)
    float uAspectRatio; // Screen aspect ratio (width/height)
    float uNearPlane;   // Near plane distance
};

void main()
{
    // Get vertex position in NDC space [-1, 1]
    vec2 ndcPos = aPosition;
    vTexCoord = aTexCoord;

    // Calculate ray direction for this pixel in world space
    // Convert NDC to camera space ray direction
    float tanHalfFOV = tan(uCameraFOV * 0.5);
    vec3 cameraSpaceRay = vec3(ndcPos.x * tanHalfFOV * uAspectRatio,
                               ndcPos.y * tanHalfFOV,
                               -1.0 // Forward in camera space (negative Z)
    );

    // Normalize camera space ray
    float rayLen = length(cameraSpaceRay);
    cameraSpaceRay = cameraSpaceRay / rayLen;

    // Transform to world space using camera basis vectors
    vRayDir = normalize(uCameraRight * cameraSpaceRay.x + uCameraUp * cameraSpaceRay.y + uCameraDir * cameraSpaceRay.z);

    // Output position: fullscreen quad in NDC space
    // Position just inside the near plane (z = -0.999 in NDC) to ensure it's visible
    gl_Position = vec4(ndcPos.x, ndcPos.y, -0.999, 1.0);
}
