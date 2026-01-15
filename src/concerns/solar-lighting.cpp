// ============================================================================
// Solar Lighting Implementation
// ============================================================================
// Implements physically-based point lighting from the Sun with inverse-square
// falloff and 5778K blackbody color.

#include "solar-lighting.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>


namespace SolarLighting
{

// Current sun position in world space
static glm::vec3 g_sunPosition(0.0f);

// ============================================================================
// Initialization
// ============================================================================

void initialize()
{
    // Enable lighting
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_NORMALIZE);

    // CRITICAL: Disable OpenGL's global ambient light model
    // By default, OpenGL has a small global ambient (0.2, 0.2, 0.2)
    // This makes all surfaces lit even without any light source
    GLfloat noGlobalAmbient[] = {0.0f, 0.0f, 0.0f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, noGlobalAmbient);

    // Set up base light properties
    // These will be modified per-body based on distance
    GLfloat ambientLight[] = {AMBIENT_LEVEL, AMBIENT_LEVEL, AMBIENT_LEVEL, 1.0f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight);

    // No specular - Sun is the only light source, keep it simple
    GLfloat specularLight[] = {0.0f, 0.0f, 0.0f, 1.0f};
    glLightfv(GL_LIGHT0, GL_SPECULAR, specularLight);

    // No attenuation in OpenGL's model - we handle it manually
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.0f);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.0f);
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
    // In OpenGL, w=0.0 means directional light
    // The xyz components point FROM the surface TOWARD the light source
    GLfloat lightDirGL[] = {lightDir.x, lightDir.y, lightDir.z, 0.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, lightDirGL);

    // Calculate intensity based on distance (inverse square falloff)
    float intensity = calculateIntensity(distance, distanceScale);

    // Apply sun color with intensity
    GLfloat diffuseLight[] = {SUN_COLOR.r * intensity, SUN_COLOR.g * intensity, SUN_COLOR.b * intensity, 1.0f};
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseLight);

    // Ambient remains low and constant (represents scattered light)
    GLfloat ambientLight[] = {AMBIENT_LEVEL, AMBIENT_LEVEL, AMBIENT_LEVEL, 1.0f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight);
}

// ============================================================================
// Emissive Sphere (for Sun)
// ============================================================================

void drawEmissiveSphere(const glm::vec3 &center, float radius, const glm::vec3 &emissiveColor, int slices, int stacks)
{
    // Disable lighting - sun is self-illuminated
    glDisable(GL_LIGHTING);

    // Set emissive color directly
    glColor3f(emissiveColor.r, emissiveColor.g, emissiveColor.b);

    glPushMatrix();
    glTranslatef(center.x, center.y, center.z);

    const float PI = 3.14159265358979323846f;

    for (int i = 0; i < stacks; ++i)
    {
        float phi1 = PI * (-0.5f + static_cast<float>(i) / stacks);
        float phi2 = PI * (-0.5f + static_cast<float>(i + 1) / stacks);

        float y1 = radius * sin(phi1);
        float y2 = radius * sin(phi2);
        float r1 = radius * cos(phi1);
        float r2 = radius * cos(phi2);

        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; ++j)
        {
            float theta = 2.0f * PI * static_cast<float>(j) / slices;
            float cosTheta = cos(theta);
            float sinTheta = sin(theta);

            float x1 = r1 * cosTheta;
            float z1 = r1 * sinTheta;
            glVertex3f(x1, y1, z1);

            float x2 = r2 * cosTheta;
            float z2 = r2 * sinTheta;
            glVertex3f(x2, y2, z2);
        }
        glEnd();
    }

    glPopMatrix();

    // Re-enable lighting for subsequent draws
    glEnable(GL_LIGHTING);
}

// ============================================================================
// Lit Sphere (for planets/moons)
// ============================================================================

void drawLitSphere(const glm::vec3 &center, float radius, const glm::vec3 &baseColor, int slices, int stacks)
{
    // Default orientation: Y-up, no specific orientation
    drawOrientedLitSphere(center,
                          radius,
                          baseColor,
                          glm::vec3(0.0f, 1.0f, 0.0f), // Default pole: Y-up
                          glm::vec3(1.0f, 0.0f, 0.0f), // Default prime: +X
                          slices,
                          stacks);
}

void drawOrientedLitSphere(const glm::vec3 &center,
                           float radius,
                           const glm::vec3 &baseColor,
                           const glm::vec3 &poleDir,
                           const glm::vec3 &primeMeridianDir,
                           int slices,
                           int stacks)
{
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

    // Generate sphere with proper orientation
    // phi = latitude (-90° to +90°), theta = longitude (0° to 360°)
    for (int i = 0; i < stacks; ++i)
    {
        float phi1 = PI * (-0.5f + static_cast<float>(i) / stacks);
        float phi2 = PI * (-0.5f + static_cast<float>(i + 1) / stacks);

        float cosPhi1 = cos(phi1);
        float sinPhi1 = sin(phi1);
        float cosPhi2 = cos(phi2);
        float sinPhi2 = sin(phi2);

        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; ++j)
        {
            // Theta goes from 0 to 2*PI, starting at prime meridian
            float theta = 2.0f * PI * static_cast<float>(j) / slices;
            // Shift by PI so theta=0 is at the prime meridian (east direction)
            float thetaShifted = theta - PI;
            float cosTheta = cos(thetaShifted);
            float sinTheta = sin(thetaShifted);

            // First vertex (lower latitude)
            // Point on sphere: cosLat * (cosLon * east + sinLon * south90) + sinLat * north
            glm::vec3 localDir1 = cosPhi1 * (cosTheta * east + sinTheta * south90) + sinPhi1 * north;
            glm::vec3 worldPos1 = radius * localDir1;
            glNormal3f(localDir1.x, localDir1.y, localDir1.z);
            glVertex3f(worldPos1.x, worldPos1.y, worldPos1.z);

            // Second vertex (higher latitude)
            glm::vec3 localDir2 = cosPhi2 * (cosTheta * east + sinTheta * south90) + sinPhi2 * north;
            glm::vec3 worldPos2 = radius * localDir2;
            glNormal3f(localDir2.x, localDir2.y, localDir2.z);
            glVertex3f(worldPos2.x, worldPos2.y, worldPos2.z);
        }
        glEnd();
    }

    glPopMatrix();
}

} // namespace SolarLighting
