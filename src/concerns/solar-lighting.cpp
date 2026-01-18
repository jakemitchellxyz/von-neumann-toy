// ============================================================================
// Solar Lighting Implementation
// ============================================================================
// Implements physically-based point lighting from the Sun with inverse-square
// falloff and 5778K blackbody color.

#include "solar-lighting.h"
#include "../concerns/constants.h"
#include "ui-overlay.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <utility>


namespace SolarLighting
{

// Current sun position in world space
static glm::vec3 g_sunPosition(0.0f);

// Camera info for geometry culling (set before rendering)
static glm::vec3 g_cameraPosition(0.0f);
static glm::vec3 g_cameraDirection(0.0f, 0.0f, 1.0f);
static float g_cameraFovRadians = 60.0f * 3.14159265358979323846f / 180.0f; // 60 degrees in radians

// ============================================================================
// Initialization
// ============================================================================

void initialize()
{
    // TODO: Migrate solar lighting to Vulkan
    // Enable lighting
    // glEnable(GL_LIGHTING); // REMOVED - migrate to Vulkan (lighting in shaders)
    // glEnable(GL_LIGHT0); // REMOVED - migrate to Vulkan
    // glEnable(GL_COLOR_MATERIAL); // REMOVED - migrate to Vulkan
    // glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE); // REMOVED - migrate to Vulkan
    // glShadeModel(GL_SMOOTH); // REMOVED - migrate to Vulkan
    // glEnable(GL_NORMALIZE); // REMOVED - migrate to Vulkan

    // CRITICAL: Disable OpenGL's global ambient light model
    // By default, OpenGL has a small global ambient (0.2, 0.2, 0.2)
    // This makes all surfaces lit even without any light source
    GLfloat noGlobalAmbient[] = {0.0f, 0.0f, 0.0f, 1.0f};
    // glLightModelfv(GL_LIGHT_MODEL_AMBIENT, noGlobalAmbient); // REMOVED - migrate to Vulkan uniform buffer

    // Set up base light properties
    // These will be modified per-body based on distance
    GLfloat ambientLight[] = {AMBIENT_LEVEL, AMBIENT_LEVEL, AMBIENT_LEVEL, 1.0f};
    // glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight); // REMOVED - migrate to Vulkan uniform buffer

    // No specular - Sun is the only light source, keep it simple
    GLfloat specularLight[] = {0.0f, 0.0f, 0.0f, 1.0f};
    // glLightfv(GL_LIGHT0, GL_SPECULAR, specularLight); // REMOVED - migrate to Vulkan uniform buffer

    // No attenuation in OpenGL's model - we handle it manually
    // glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f); // REMOVED - migrate to Vulkan uniform buffer
    // glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.0f); // REMOVED - migrate to Vulkan uniform buffer
    // glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.0f); // REMOVED - migrate to Vulkan uniform buffer
}

// ============================================================================
// Sun Position
// ============================================================================

void setSunPosition(const glm::vec3 &sunPos)
{
    g_sunPosition = sunPos;
}

glm::vec3 getSunPosition()
{
    return g_sunPosition;
}

// ============================================================================
// Camera Info for Geometry Culling
// ============================================================================

void setCameraInfo(const glm::vec3 &cameraPos, const glm::vec3 &cameraDir, float fovRadians)
{
    g_cameraPosition = cameraPos;
    g_cameraDirection = cameraDir;
    g_cameraFovRadians = fovRadians;
}

glm::vec3 getCameraPosition()
{
    return g_cameraPosition;
}

glm::vec3 getCameraDirection()
{
    return g_cameraDirection;
}

float getCameraFov()
{
    return g_cameraFovRadians;
}

// ============================================================================
// Light Intensity Calculation
// ============================================================================

float calculateIntensity(float distance, float distanceScale)
{
    // Convert display distance to AU
    float distanceAU = distance / distanceScale;

    // Clamp minimum distance to avoid division by zero or extreme values
    // Mercury is at ~0.39 AU, so 0.2 AU is a safe minimum
    distanceAU = std::max(distanceAU, 0.2f);

    // Use a softer falloff curve for visibility while maintaining distance variation
    // Physical inverse square: I = I₀ / r² is too aggressive for visualization
    // Use I = I₀ / r^1.3 instead - softer falloff that keeps distant planets visible
    // This preserves the sense of distance while ensuring all planets are lit
    float intensity = BASE_INTENSITY_AT_1AU / std::pow(distanceAU, 1.3f);

    // Scale up for OpenGL lighting visibility
    // Scale factor chosen so Jupiter (5.2 AU) gets ~0.4 intensity (above floor)
    // This ensures variation between Jupiter, Saturn, etc. while keeping them visible
    intensity *= 4.0f;

    // Apply minimum intensity floor for very distant planets
    // Ensures outer planets (especially Uranus, Neptune, Pluto) are still visible
    // Floor is low enough that Jupiter and Saturn show variation
    float minVisibleIntensity = 0.25f; // Minimum 25% intensity for visibility
    intensity = std::max(intensity, minVisibleIntensity);

    // Clamp maximum intensity (for very close planets like Mercury)
    return std::clamp(intensity, minVisibleIntensity, 10.0f);
}

// ============================================================================
// Per-Body Lighting Setup
// ============================================================================

void setupLightingForBody(const glm::vec3 &bodyPosition, float distanceScale)
{
    // Don't enable lighting if we're in wireframe mode (wireframes should be unlit)
    if (g_showWireframe)
    {
        return; // Skip lighting setup entirely in wireframe mode
    }

    // Ensure lighting is enabled for this body (independent of sun visibility)
    // This ensures planets are lit even if the sun mesh is culled
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    // Calculate direction from sun to body (this is the light direction)
    glm::vec3 toBody = bodyPosition - g_sunPosition;
    float distance = glm::length(toBody);

    // Avoid division by zero for the sun itself
    if (distance < 0.001f)
    {
        return;
    }

    // Normalize direction vector (light travels FROM sun TO body)
    glm::vec3 lightTravelDir = glm::normalize(toBody);

    // OpenGL directional lights expect the direction vector to point FROM surface TOWARD light
    // (opposite of the light's travel direction)
    // This is because OpenGL's lighting uses dot(normal, lightDir) where lightDir points to the light
    glm::vec3 lightDir = -lightTravelDir; // Negate to point FROM body TO sun

    // Set up DIRECTIONAL light (not point light)
    // TODO: Migrate solar lighting to Vulkan
    // In OpenGL, w=0.0 means directional light
    // The xyz components point FROM the surface TOWARD the light source
    GLfloat lightDirGL[] = {lightDir.x, lightDir.y, lightDir.z, 0.0f};
    // glLightfv(GL_LIGHT0, GL_POSITION, lightDirGL); // REMOVED - migrate to Vulkan uniform buffer

    // Calculate intensity based on distance (inverse square falloff)
    float intensity = calculateIntensity(distance, distanceScale);

    // Apply sun color with intensity
    GLfloat diffuseLight[] = {SUN_COLOR.r * intensity, SUN_COLOR.g * intensity, SUN_COLOR.b * intensity, 1.0f};
    // glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseLight); // REMOVED - migrate to Vulkan uniform buffer

    // Ambient remains low and constant (represents scattered light)
    GLfloat ambientLight[] = {AMBIENT_LEVEL, AMBIENT_LEVEL, AMBIENT_LEVEL, 1.0f};
    // glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight); // REMOVED - migrate to Vulkan uniform buffer
}

// ============================================================================
// Emissive Sphere (for Sun)
// ============================================================================

void drawEmissiveSphere(const glm::vec3 &center, float radius, const glm::vec3 &emissiveColor, int slices, int stacks)
{
    // Disable lighting - sun is self-illuminated
    // In wireframe mode, lighting should already be disabled, but ensure it stays off
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);

    // TODO: Migrate solar lighting rendering to Vulkan
    // Set emissive color directly
    // glColor3f(emissiveColor.r, emissiveColor.g, emissiveColor.b); // REMOVED - migrate to Vulkan uniform buffer

    // glPushMatrix(); // REMOVED - migrate to Vulkan (matrices in uniform buffers)
    // glTranslatef(center.x, center.y, center.z); // REMOVED - migrate to Vulkan (matrices in uniform buffers)

    const float PI = 3.14159265358979323846f;

    for (int i = 0; i < stacks; ++i)
    {
        float phi1 = PI * (-0.5f + static_cast<float>(i) / stacks);
        float phi2 = PI * (-0.5f + static_cast<float>(i + 1) / stacks);

        float y1 = radius * sin(phi1);
        float y2 = radius * sin(phi2);
        float r1 = radius * cos(phi1);
        float r2 = radius * cos(phi2);

        // glBegin(GL_TRIANGLE_STRIP); // REMOVED - migrate to Vulkan
        for (int j = 0; j <= slices; ++j)
        {
            float theta = 2.0f * PI * static_cast<float>(j) / slices;
            float cosTheta = cos(theta);
            float sinTheta = sin(theta);

            float x1 = r1 * cosTheta;
            float z1 = r1 * sinTheta;
            // glVertex3f(x1, y1, z1); // REMOVED - migrate to Vulkan vertex buffer

            float x2 = r2 * cosTheta;
            float z2 = r2 * sinTheta;
            // glVertex3f(x2, y2, z2); // REMOVED - migrate to Vulkan vertex buffer
        }
        // glEnd(); // REMOVED - migrate to Vulkan
    }

    // glPopMatrix(); // REMOVED - migrate to Vulkan (matrices in uniform buffers)

    // Re-enable lighting for subsequent draws (unless in wireframe mode)
    if (!g_showWireframe)
    {
        glEnable(GL_LIGHTING);
    }
}

// ============================================================================
// Lit Sphere (for planets/moons)
// ============================================================================

void drawLitSphere(const glm::vec3 &center, float radius, const glm::vec3 &baseColor, int slices, int stacks)
{
    // Default orientation: Y-up, no specific orientation
    // Use global camera info for culling
    drawOrientedLitSphere(center,
                          radius,
                          baseColor,
                          glm::vec3(0.0f, 1.0f, 0.0f), // Default pole: Y-up
                          glm::vec3(1.0f, 0.0f, 0.0f), // Default prime: +X
                          slices,
                          stacks,
                          g_cameraPosition,
                          g_cameraDirection,
                          g_cameraFovRadians);
}

void drawOrientedLitSphere(const glm::vec3 &center,
                           float radius,
                           const glm::vec3 &baseColor,
                           const glm::vec3 &poleDir,
                           const glm::vec3 &primeMeridianDir,
                           int slices,
                           int stacks,
                           const glm::vec3 &cameraPos,
                           const glm::vec3 &cameraDir,
                           float fovRadians,
                           bool disableCulling)
{
    // Use provided camera info, or fall back to global if not provided (for backward compatibility)
    glm::vec3 actualCameraPos =
        (glm::length(cameraPos) < 0.001f && glm::length(g_cameraPosition) > 0.001f) ? g_cameraPosition : cameraPos;
    glm::vec3 actualCameraDir =
        (glm::length(cameraDir) < 0.001f && glm::length(g_cameraDirection) > 0.001f) ? g_cameraDirection : cameraDir;
    float actualFov = (fovRadians < 0.001f && g_cameraFovRadians > 0.001f) ? g_cameraFovRadians : fovRadians;

    glPushMatrix();
    glTranslatef(center.x, center.y, center.z);

    // Set material color
    glColor3f(baseColor.r, baseColor.g, baseColor.b);

    const float PI = 3.14159265358979323846f;

    // Build orthonormal basis from pole and prime meridian
    // north = pole direction (Z-axis of body-fixed frame in SPICE convention)
    glm::vec3 north = glm::normalize(poleDir);

    // east = prime meridian direction (X-axis of body-fixed frame)
    // Ensure it's perpendicular to north
    glm::vec3 east = primeMeridianDir - glm::dot(primeMeridianDir, north) * north;
    if (glm::length(east) < 0.001f)
    {
        // Prime meridian nearly parallel to pole - use fallback
        if (std::abs(north.y) < 0.9f)
        {
            east = glm::normalize(glm::cross(north, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        else
        {
            east = glm::normalize(glm::cross(north, glm::vec3(1.0f, 0.0f, 0.0f)));
        }
    }
    else
    {
        east = glm::normalize(east);
    }

    // south90 = Y-axis of body-fixed frame (90° East longitude at equator)
    glm::vec3 south90 = glm::normalize(glm::cross(north, east));

    // Calculate frustum cone parameters for culling
    float halfFov = actualFov * 0.5f;
    float expandedHalfFov = halfFov + 15.0f * 3.14159265358979323846f / 180.0f; // 15 degrees in radians
    float cosExpandedHalfFov = cos(expandedHalfFov);

    // Generate sphere with proper orientation and aggressive back-face culling
    // phi = latitude (-90° to +90°), theta = longitude (0° to 360°)
    for (int i = 0; i < stacks; ++i)
    {
        float phi1 = PI * (-0.5f + static_cast<float>(i) / stacks);
        float phi2 = PI * (-0.5f + static_cast<float>(i + 1) / stacks);

        float cosPhi1 = cos(phi1);
        float sinPhi1 = sin(phi1);
        float cosPhi2 = cos(phi2);
        float sinPhi2 = sin(phi2);

        // Build TRIANGLE_STRIP dynamically, only adding front-facing triangles
        int stripVertexCount = 0;
        bool stripActive = false;

        // Track previous vertex pair for triangle culling (TRIANGLE_STRIP forms triangles from consecutive vertices)
        glm::vec3 prevLocalDir1, prevLocalDir2;
        glm::vec3 prevWorldPos1, prevWorldPos2;
        bool hasPrevPair = false;

        for (int j = 0; j <= slices; ++j)
        {
            // Theta goes from 0 to 2*PI, starting at prime meridian
            float theta = 2.0f * PI * static_cast<float>(j) / slices;
            // Shift by PI so theta=0 is at the prime meridian (east direction)
            float thetaShifted = theta - PI;
            float cosTheta = cos(thetaShifted);
            float sinTheta = sin(thetaShifted);

            // First vertex (lower latitude)
            glm::vec3 localDir1 = cosPhi1 * (cosTheta * east + sinTheta * south90) + sinPhi1 * north;
            glm::vec3 worldPos1 = center + radius * localDir1;
            glm::vec3 toVertex1 = worldPos1 - actualCameraPos;
            float distToVertex1 = glm::length(toVertex1);
            glm::vec3 dirToVertex1 = distToVertex1 > 0.001f ? toVertex1 / distToVertex1 : glm::vec3(0.0f, 0.0f, 1.0f);

            // Second vertex (higher latitude)
            glm::vec3 localDir2 = cosPhi2 * (cosTheta * east + sinTheta * south90) + sinPhi2 * north;
            glm::vec3 worldPos2 = center + radius * localDir2;
            glm::vec3 toVertex2 = worldPos2 - actualCameraPos;
            float distToVertex2 = glm::length(toVertex2);
            glm::vec3 dirToVertex2 = distToVertex2 > 0.001f ? toVertex2 / distToVertex2 : glm::vec3(0.0f, 0.0f, 1.0f);

            bool segmentVisible = true; // Default to visible if culling disabled

            if (!disableCulling)
            {
                // For TRIANGLE_STRIP, triangles are formed by (prevV1, prevV2, currV1) and (prevV2, currV1, currV2)
                // Only cull if ALL 3 vertices of BOTH triangles are back-facing

                bool vertex1FrontFacing = true;
                bool vertex2FrontFacing = true;
                bool vertex1InFrustum = true;
                bool vertex2InFrustum = true;

                // Back-face culling: check if normals face camera
                float backFaceDot1 = glm::dot(localDir1, -dirToVertex1);
                float backFaceDot2 = glm::dot(localDir2, -dirToVertex2);

                if (distToVertex1 > radius * 0.1f)
                {
                    vertex1FrontFacing = backFaceDot1 >= 0.0f;
                    float cosAngle1 = glm::dot(dirToVertex1, actualCameraDir);
                    vertex1InFrustum = cosAngle1 >= cosExpandedHalfFov;
                }

                if (distToVertex2 > radius * 0.1f)
                {
                    vertex2FrontFacing = backFaceDot2 >= 0.0f;
                    float cosAngle2 = glm::dot(dirToVertex2, actualCameraDir);
                    vertex2InFrustum = cosAngle2 >= cosExpandedHalfFov;
                }

                if (hasPrevPair)
                {
                    // Check triangles formed with previous vertex pair
                    // Triangle 1: (prevV1, prevV2, currV1)
                    // Triangle 2: (prevV2, currV1, currV2)

                    glm::vec3 toPrevV1 = prevWorldPos1 - actualCameraPos;
                    float distToPrevV1 = glm::length(toPrevV1);
                    glm::vec3 dirToPrevV1 =
                        distToPrevV1 > 0.001f ? toPrevV1 / distToPrevV1 : glm::vec3(0.0f, 0.0f, 1.0f);
                    float prevBackFaceDot1 = glm::dot(prevLocalDir1, -dirToPrevV1);
                    bool prevV1FrontFacing = distToPrevV1 <= radius * 0.1f || prevBackFaceDot1 >= 0.0f;

                    glm::vec3 toPrevV2 = prevWorldPos2 - actualCameraPos;
                    float distToPrevV2 = glm::length(toPrevV2);
                    glm::vec3 dirToPrevV2 =
                        distToPrevV2 > 0.001f ? toPrevV2 / distToPrevV2 : glm::vec3(0.0f, 0.0f, 1.0f);
                    float prevBackFaceDot2 = glm::dot(prevLocalDir2, -dirToPrevV2);
                    bool prevV2FrontFacing = distToPrevV2 <= radius * 0.1f || prevBackFaceDot2 >= 0.0f;

                    // Only cull if ALL 3 vertices of BOTH triangles are back-facing
                    // Triangle 1: prevV1, prevV2, currV1
                    bool triangle1AllBackFacing = !prevV1FrontFacing && !prevV2FrontFacing && !vertex1FrontFacing;
                    // Triangle 2: prevV2, currV1, currV2
                    bool triangle2AllBackFacing = !prevV2FrontFacing && !vertex1FrontFacing && !vertex2FrontFacing;

                    // Cull only if both triangles are completely back-facing
                    bool bothTrianglesBackFacing = triangle1AllBackFacing && triangle2AllBackFacing;

                    // Also check frustum - at least one vertex must be in frustum
                    bool atLeastOneInFrustum =
                        vertex1InFrustum || vertex2InFrustum ||
                        (distToPrevV1 > radius * 0.1f &&
                         glm::dot(dirToPrevV1, actualCameraDir) >= cosExpandedHalfFov) ||
                        (distToPrevV2 > radius * 0.1f && glm::dot(dirToPrevV2, actualCameraDir) >= cosExpandedHalfFov);

                    segmentVisible = !bothTrianglesBackFacing && atLeastOneInFrustum;
                }
                else
                {
                    // First pair - just check if at least one vertex is front-facing and in frustum
                    segmentVisible =
                        (vertex1FrontFacing || vertex2FrontFacing) && (vertex1InFrustum || vertex2InFrustum);
                }
            }

            if (segmentVisible)
            {
                if (!stripActive)
                {
                    // glBegin(GL_TRIANGLE_STRIP); // REMOVED - migrate to Vulkan
                    stripActive = true;
                }

                // TODO: Migrate solar lighting rendering to Vulkan
                // In wireframe mode, ensure color is set explicitly (not affected by material)
                if (disableCulling) // disableCulling is true in wireframe mode
                {
                    // glColor3f(0.8f, 0.9f, 1.0f); // REMOVED - migrate to Vulkan uniform buffer
                }

                // glNormal3f(localDir1.x, localDir1.y, localDir1.z); // REMOVED - migrate to Vulkan vertex buffer
                // glVertex3f(worldPos1.x - center.x, worldPos1.y - center.y, worldPos1.z - center.z); // REMOVED - migrate to Vulkan vertex buffer
                stripVertexCount++;

                // glNormal3f(localDir2.x, localDir2.y, localDir2.z); // REMOVED - migrate to Vulkan vertex buffer
                // glVertex3f(worldPos2.x - center.x, worldPos2.y - center.y, worldPos2.z - center.z); // REMOVED - migrate to Vulkan vertex buffer
                stripVertexCount++;

                // Store current pair as previous for next iteration
                prevLocalDir1 = localDir1;
                prevLocalDir2 = localDir2;
                prevWorldPos1 = worldPos1;
                prevWorldPos2 = worldPos2;
                hasPrevPair = true;
            }
            else if (stripActive)
            {
                // End current strip if we hit back-facing or culled vertices
                glEnd();
                if (stripVertexCount >= 2)
                {
                    CountTriangles(GL_TRIANGLE_STRIP, stripVertexCount);
                }
                stripActive = false;
                stripVertexCount = 0;
                hasPrevPair = false; // Reset when strip ends
            }
        }

        // End strip if still active
        if (stripActive)
        {
            // glEnd(); // REMOVED - migrate to Vulkan
            if (stripVertexCount >= 2)
            {
                CountTriangles(GL_TRIANGLE_STRIP, stripVertexCount);
            }
        }
    }

    // glPopMatrix(); // REMOVED - migrate to Vulkan (matrices in uniform buffers)
}

// Calculate dynamic tessellation based on camera distance for celestial bodies
// Applies second layer of tessellation around closest point to camera
std::pair<int, int> calculateCelestialBodyTessellation(const glm::vec3 &spherePosition,
                                                       float sphereRadius,
                                                       const glm::vec3 &cameraPos)
{
    glm::vec3 toSphere = spherePosition - cameraPos;
    float distance = glm::length(toSphere);
    float distanceInRadii = distance / sphereRadius;

    // If distance > 5 * radius, use base tessellation
    if (distanceInRadii >= TESSELATION_DISTANCE_THRESHOLD)
    {
        return {SPHERE_BASE_SLICES, SPHERE_BASE_STACKS};
    }

    // Calculate base tessellation multiplier based on distance
    // At distance = 5 * radius: multiplier = 1.0 (base)
    // At distance = 1 * radius: multiplier = MAX_TESSELATION_MULTIPLIER
    // Linear interpolation between these points
    float t = (TESSELATION_DISTANCE_THRESHOLD - distanceInRadii) / (TESSELATION_DISTANCE_THRESHOLD - 1.0f);
    t = glm::clamp(t, 0.0f, 1.0f); // Clamp to [0, 1]

    float baseMultiplier = 1.0f + t * (MAX_TESSELATION_MULTIPLIER - 1.0f);

    // Calculate base tessellation values (round to nearest even number for better triangle strip rendering)
    int baseSlices = static_cast<int>(std::round(SPHERE_BASE_SLICES * baseMultiplier / 2.0f)) * 2;
    int baseStacks = static_cast<int>(std::round(SPHERE_BASE_STACKS * baseMultiplier / 2.0f)) * 2;

    // Ensure minimum tessellation
    baseSlices = std::max(baseSlices, SPHERE_BASE_SLICES);
    baseStacks = std::max(baseStacks, SPHERE_BASE_STACKS);

    // Apply local high-detail tessellation multiplier for region around closest point
    // The smooth blend happens naturally - vertices near the closest point will have
    // higher effective tessellation due to the increased overall tessellation
    int slices = baseSlices * LOCAL_TESSELATION_MULTIPLIER;
    int stacks = baseStacks * LOCAL_TESSELATION_MULTIPLIER;

    return {slices, stacks};
}

} // namespace SolarLighting
