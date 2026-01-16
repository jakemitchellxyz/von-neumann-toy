#version 120

// Atmosphere vertex shader
// Renders a fullscreen quad just inside the near plane

varying vec2 vTexCoord;
varying vec3 vRayDir; // Ray direction in world space

uniform vec3 uCameraPos;        // Camera position in world space
uniform vec3 uCameraDir;        // Camera forward direction (normalized)
uniform vec3 uCameraRight;      // Camera right direction (normalized)
uniform vec3 uCameraUp;         // Camera up direction (normalized)
uniform float uCameraFOV;       // Camera field of view (radians)
uniform float uAspectRatio;     // Screen aspect ratio (width/height)
uniform float uNearPlane;       // Near plane distance

void main()
{
    // Get vertex position in NDC space [-1, 1]
    vec2 ndcPos = gl_Vertex.xy;
    vTexCoord = gl_MultiTexCoord0.st;
    
    // Calculate ray direction for this pixel in world space
    // Convert NDC to camera space ray direction
    float tanHalfFOV = tan(uCameraFOV * 0.5);
    vec3 cameraSpaceRay = vec3(
        ndcPos.x * tanHalfFOV * uAspectRatio,
        ndcPos.y * tanHalfFOV,
        -1.0 // Forward in camera space (negative Z)
    );
    
    // Normalize camera space ray
    float rayLen = length(cameraSpaceRay);
    cameraSpaceRay = cameraSpaceRay / rayLen;
    
    // Transform to world space using camera basis vectors
    vRayDir = normalize(
        uCameraRight * cameraSpaceRay.x +
        uCameraUp * cameraSpaceRay.y +
        uCameraDir * cameraSpaceRay.z
    );
    
    // Output position: fullscreen quad in NDC space
    // Position just inside the near plane (z = -0.999 in NDC) to ensure it's visible
    // With depth test disabled, this should render on top of everything
    gl_Position = vec4(ndcPos.x, ndcPos.y, -0.999, 1.0);
}
