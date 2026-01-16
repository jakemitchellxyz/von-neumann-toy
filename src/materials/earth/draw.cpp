// ============================================================================
// Drawing
// ============================================================================

#include "../../concerns/constants.h"
#include "../../concerns/font-rendering.h"
#include "../../concerns/ui-overlay.h"
#include "../helpers/gl.h"
#include "earth-material.h"
#include "helpers/coordinate-conversion.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <tuple>
#include <utility>

namespace
{
// OpenGL 4x4 matrix size (4 rows × 4 columns = 16 elements)
constexpr size_t OPENGL_MATRIX_SIZE = 16;

// Camera info for geometry culling (set before rendering)
static glm::vec3 g_cameraPosition(0.0f);
static glm::vec3 g_cameraDirection(0.0f, 0.0f, 1.0f);
static float g_cameraFovRadians = glm::radians(60.0f);

// Screen dimensions for depth buffer queries (set before rendering)
static int g_screenWidth = 1920;
static int g_screenHeight = 1080;

// Check if a triangle is occluded by checking depth buffer at its vertices
// This is expensive (3 glReadPixels calls per triangle), so use sparingly
// Returns true if all 3 vertices are behind existing depth values
bool isTriangleOccluded(const glm::vec3 &v1,
                        const glm::vec3 &v2,
                        const glm::vec3 &v3,
                        const glm::mat4 &modelView,
                        const glm::mat4 &projection)
{
    // Get current matrices
    GLfloat modelViewArray[16];
    GLfloat projectionArray[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, modelViewArray);
    glGetFloatv(GL_PROJECTION_MATRIX, projectionArray);

    // Convert to glm matrices (OpenGL matrices are column-major)
    glm::mat4 currentModelView = glm::make_mat4(modelViewArray);
    glm::mat4 currentProjection = glm::make_mat4(projectionArray);
    glm::mat4 mvp = currentProjection * currentModelView;

    // Project vertices to clip space
    glm::vec4 clip1 = mvp * glm::vec4(v1, 1.0f);
    glm::vec4 clip2 = mvp * glm::vec4(v2, 1.0f);
    glm::vec4 clip3 = mvp * glm::vec4(v3, 1.0f);

    // Perspective divide
    if (std::abs(clip1.w) < 0.001f || std::abs(clip2.w) < 0.001f || std::abs(clip3.w) < 0.001f)
    {
        return false; // Can't determine occlusion for degenerate triangles
    }

    glm::vec3 ndc1 = glm::vec3(clip1) / clip1.w;
    glm::vec3 ndc2 = glm::vec3(clip2) / clip2.w;
    glm::vec3 ndc3 = glm::vec3(clip3) / clip3.w;

    // Convert NDC to screen coordinates
    float screenX1 = (ndc1.x + 1.0f) * 0.5f * g_screenWidth;
    float screenY1 = (ndc1.y + 1.0f) * 0.5f * g_screenHeight;
    float screenX2 = (ndc2.x + 1.0f) * 0.5f * g_screenWidth;
    float screenY2 = (ndc2.y + 1.0f) * 0.5f * g_screenHeight;
    float screenX3 = (ndc3.x + 1.0f) * 0.5f * g_screenWidth;
    float screenY3 = (ndc3.y + 1.0f) * 0.5f * g_screenHeight;

    // Check if vertices are outside viewport
    if (screenX1 < 0 || screenX1 >= g_screenWidth || screenY1 < 0 || screenY1 >= g_screenHeight || screenX2 < 0 ||
        screenX2 >= g_screenWidth || screenY2 < 0 || screenY2 >= g_screenHeight || screenX3 < 0 ||
        screenX3 >= g_screenWidth || screenY3 < 0 || screenY3 >= g_screenHeight)
    {
        return false; // Can't check occlusion for off-screen vertices
    }

    // Read depth values at vertex positions (OpenGL Y is bottom-up, screen Y is top-down)
    float depth1, depth2, depth3;
    glReadPixels(static_cast<GLint>(screenX1),
                 static_cast<GLint>(g_screenHeight - screenY1 - 1),
                 1,
                 1,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 &depth1);
    glReadPixels(static_cast<GLint>(screenX2),
                 static_cast<GLint>(g_screenHeight - screenY2 - 1),
                 1,
                 1,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 &depth2);
    glReadPixels(static_cast<GLint>(screenX3),
                 static_cast<GLint>(g_screenHeight - screenY3 - 1),
                 1,
                 1,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 &depth3);

    // Convert NDC depth to [0,1] range (OpenGL depth buffer range)
    // OpenGL depth buffer uses [0,1] where 0 is near and 1 is far
    // NDC z is in [-1,1] where -1 is near and 1 is far
    float ndcDepth1 = (ndc1.z + 1.0f) * 0.5f;
    float ndcDepth2 = (ndc2.z + 1.0f) * 0.5f;
    float ndcDepth3 = (ndc3.z + 1.0f) * 0.5f;

    // Triangle is occluded if all vertices are behind existing depth
    // Use a small epsilon to account for floating point precision
    const float EPSILON = 0.0001f;
    return (ndcDepth1 > depth1 + EPSILON) && (ndcDepth2 > depth2 + EPSILON) && (ndcDepth3 > depth3 + EPSILON);
}
} // namespace

// Set camera info for geometry culling (called from entrypoint before rendering)
void EarthMaterial::setCameraInfo(const glm::vec3 &cameraPos, const glm::vec3 &cameraDir, float fovRadians)
{
    g_cameraPosition = cameraPos;
    g_cameraDirection = cameraDir;
    g_cameraFovRadians = fovRadians;
}

// Set screen dimensions for occlusion culling
void EarthMaterial::setScreenDimensions(int width, int height)
{
    g_screenWidth = width;
    g_screenHeight = height;
}

// Calculate dynamic tessellation based on camera distance
// Returns (baseSlices, baseStacks, localSlices, localStacks) tuple and closest point on sphere to camera
// When distance > 5 * radius: base tessellation
// When distance < 5 * radius: tessellation increases as camera approaches
// Additionally applies local high-detail tessellation in a circular region (radius = 0.25 * planet radius)
// centered at the closest point to camera, with smooth blending
std::tuple<int, int, int, int> EarthMaterial::calculateTessellation(const glm::vec3 &spherePosition,
                                                                    float sphereRadius,
                                                                    const glm::vec3 &cameraPos,
                                                                    glm::vec3 &closestPointOnSphere)
{
    glm::vec3 toSphere = spherePosition - cameraPos;
    float distance = glm::length(toSphere);
    float distanceInRadii = distance / sphereRadius;

    // Find the point on the sphere closest to the camera
    // This is the point where the ray from camera to sphere center intersects the sphere surface
    glm::vec3 toSphereNorm = distance > 0.001f ? toSphere / distance : glm::vec3(0.0f, 0.0f, 1.0f);
    closestPointOnSphere = spherePosition - toSphereNorm * sphereRadius; // Point on sphere surface closest to camera

    // If distance > 5 * radius, use base tessellation (no local detail)
    if (distanceInRadii >= TESSELATION_DISTANCE_THRESHOLD)
    {
        return {SPHERE_BASE_SLICES, SPHERE_BASE_STACKS, SPHERE_BASE_SLICES, SPHERE_BASE_STACKS};
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

    // Calculate local high-detail tessellation for the circular region
    // The local region has radius = LOCAL_TESSELATION_RADIUS * sphereRadius (0.25 * radius)
    // Apply LOCAL_TESSELATION_MULTIPLIER only to the local region
    int localSlices = baseSlices * LOCAL_TESSELATION_MULTIPLIER;
    int localStacks = baseStacks * LOCAL_TESSELATION_MULTIPLIER;

    return {baseSlices, baseStacks, localSlices, localStacks};
}

void EarthMaterial::draw(const glm::vec3 &position,
                         float displayRadius,
                         const glm::vec3 &poleDirection,
                         const glm::vec3 &primeMeridianDirection,
                         double julianDate,
                         const glm::vec3 &cameraPos,
                         const glm::vec3 &sunDirection,
                         const glm::vec3 &moonDirection)
{
    if (!initialized_)
    {
        return;
    }

    // Calculate fractional month index for blending
    // J2000 is Jan 1.5, 2000.
    double daysSinceJ2000 = julianDate - JD_J2000;

    // Day of year (approximate, ignoring leap year variations for visual
    // blending)
    double yearFraction = std::fmod(daysSinceJ2000, DAYS_PER_TROPICAL_YEAR) / DAYS_PER_TROPICAL_YEAR;
    if (yearFraction < 0)
    {
        yearFraction += 1.0;
    }

    // Map year fraction (0.0-1.0) to month index (0.0-MONTHS_PER_YEAR.0)
    // We shift by -0.5 so that index X.0 corresponds to the MIDDLE of month
    // X+1 e.g., 0.0 = Mid-Jan, 1.0 = Mid-Feb. This ensures that at the middle
    // of the month, we are 100% on that texture.
    double monthPos = (yearFraction * static_cast<double>(MONTHS_PER_YEAR)) - 0.5;
    if (monthPos < 0)
    {
        monthPos += static_cast<double>(MONTHS_PER_YEAR);
    }

    int idx1 = static_cast<int>(std::floor(monthPos));
    int idx2 = (idx1 + 1) % MONTHS_PER_YEAR;
    float blendFactor = static_cast<float>(monthPos - idx1);

    // Handle wrap-around for idx1 (e.g. -0.5 floor is -1 -> 11)
    if (idx1 < 0)
    {
        idx1 = (idx1 % MONTHS_PER_YEAR + MONTHS_PER_YEAR) % MONTHS_PER_YEAR;
    }

    // MANDATORY: Required textures must be loaded - no fallbacks
    if (!textureLoaded_[idx1] || monthlyTextures_[idx1] == 0)
    {
        std::cerr << "ERROR: EarthMaterial::draw() - Color texture for month " << (idx1 + 1) << " is missing!"
                  << "\n";
        std::cerr << "  Month index: " << idx1 << " (0-based)" << "\n";
        std::exit(1);
    }

    if (!textureLoaded_[idx2] || monthlyTextures_[idx2] == 0)
    {
        std::cerr << "ERROR: EarthMaterial::draw() - Color texture for month " << (idx2 + 1) << " is missing!"
                  << "\n";
        std::cerr << "  Month index: " << idx2 << " (0-based)" << "\n";
        std::exit(1);
    }

    GLuint tex1 = monthlyTextures_[idx1];
    GLuint tex2 = monthlyTextures_[idx2];

    if (!shaderAvailable_)
    {
        std::cerr << "ERROR: EarthMaterial::draw() - Shader not available!" << "\n";
        std::cerr << "  Shader compilation or linking failed. Check console "
                     "for shader errors."
                  << "\n";
        std::exit(1);
    }

    if (!elevationLoaded_)
    {
        std::cerr << "ERROR: EarthMaterial::draw() - Elevation data not loaded!" << "\n";
        std::cerr << "  Normal map is required for shader-based rendering." << "\n";
        std::exit(1);
    }

    if (normalMapTexture_ == 0)
    {
        std::cerr << "ERROR: EarthMaterial::draw() - Normal map texture is missing!" << "\n";
        std::cerr << "  Normal map texture ID is 0. Check texture loading." << "\n";
        std::exit(1);
    }

    // Use shader-based rendering (MANDATORY - no fallback)
    {
        // Get current matrices from OpenGL fixed-function state
        std::array<GLfloat, OPENGL_MATRIX_SIZE> modelviewMatrix{};
        std::array<GLfloat, OPENGL_MATRIX_SIZE> projectionMatrix{};
        glGetFloatv(GL_MODELVIEW_MATRIX, modelviewMatrix.data());
        glGetFloatv(GL_PROJECTION_MATRIX, projectionMatrix.data());

        // Use the sunDirection passed as parameter - this is the direction FROM
        // Earth TO Sun computed correctly in celestial-body.cpp as
        // normalize(sunPos - earthPos)
        glm::vec3 lightDir = sunDirection;

        // Use shader
        glUseProgram(shaderProgram_);

        // Set matrices - use identity for model since we'll transform vertices
        // directly
        glm::mat4 identity = glm::mat4(1.0f);
        glUniformMatrix4fv(uniformModelMatrix_, 1, GL_FALSE, glm::value_ptr(identity));
        glUniformMatrix4fv(uniformViewMatrix_, 1, GL_FALSE, modelviewMatrix.data());
        glUniformMatrix4fv(uniformProjectionMatrix_, 1, GL_FALSE, projectionMatrix.data());

        // Set textures
        // Unit 0: Color Texture 1
        glActiveTexture_ptr(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex1);
        glUniform1i(uniformColorTexture_, 0);

        // Unit 1: Color Texture 2
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex2);
        glUniform1i(uniformColorTexture2_, 1);

        // Blend Factor
        glUniform1f(uniformBlendFactor_, blendFactor);

// Unit 2: Normal Map (only bind if enabled and loaded)
#ifndef GL_TEXTURE2
#define GL_TEXTURE2 0x84C2
#endif

        glActiveTexture_ptr(GL_TEXTURE2);
        if (useNormalMap_ && elevationLoaded_ && normalMapTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, normalMapTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (uniformNormalMap_ >= 0)
        {
            glUniform1i(uniformNormalMap_, 2);
        }

// Unit 12: Heightmap (landmass elevation) - used by both vertex and fragment shaders
#ifndef GL_TEXTURE12
#define GL_TEXTURE12 0x84CC
#endif
        glActiveTexture_ptr(GL_TEXTURE12);
        if (useHeightmap_ && elevationLoaded_ && heightmapTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, heightmapTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (uniformHeightmap_ >= 0)
        {
            glUniform1i(uniformHeightmap_, 12);
        }

// Unit 3: Nightlights (city lights)
#ifndef GL_TEXTURE3
#define GL_TEXTURE3 0x84C3
#endif

        glActiveTexture_ptr(GL_TEXTURE3);
        if (nightlightsLoaded_ && nightlightsTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, nightlightsTexture_);
        }
        else
        {
            // Bind a black texture (no lights) if nightlights not available
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformNightlights_, 3);

// Unit 4: Micro noise texture (fine-grained flicker)
#ifndef GL_TEXTURE4
#define GL_TEXTURE4 0x84C4
#endif
        glActiveTexture_ptr(GL_TEXTURE4);
        if (noiseTexturesGenerated_ && microNoiseTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, microNoiseTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformMicroNoise_, 4);

// Unit 5: Hourly noise texture (regional variation)
#ifndef GL_TEXTURE5
#define GL_TEXTURE5 0x84C5
#endif
        glActiveTexture_ptr(GL_TEXTURE5);
        if (noiseTexturesGenerated_ && hourlyNoiseTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, hourlyNoiseTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformHourlyNoise_, 5);

        // Unit 6 & 7: Wind textures (two 2D textures for current and next month)
        // The shader blends between them based on the current date
#ifndef GL_TEXTURE6
#define GL_TEXTURE6 0x84C6
#endif
#ifndef GL_TEXTURE7
#define GL_TEXTURE7 0x84C7
#endif
        // Bind wind textures (current and next month for blending)
        // Calculate which two months to blend between
        double daysSinceJ2000 = julianDate - JD_J2000;
        double yearFraction = std::fmod(daysSinceJ2000, DAYS_PER_TROPICAL_YEAR) / DAYS_PER_TROPICAL_YEAR;
        if (yearFraction < 0)
        {
            yearFraction += 1.0;
        }

        // Map year fraction (0.0-1.0) to month index (0-11)
        // month 1 = index 0, month 12 = index 11
        double monthPos = yearFraction * static_cast<double>(MONTHS_PER_YEAR);
        int currentMonthIdx = static_cast<int>(monthPos) % 12;
        int nextMonthIdx = (currentMonthIdx + 1) % 12;
        float blendFactor = static_cast<float>(monthPos - static_cast<int>(monthPos));

        // Bind current month texture (GL_TEXTURE6)
        glActiveTexture_ptr(GL_TEXTURE6);
        if (windTexturesLoaded_[currentMonthIdx] && windTextures_[currentMonthIdx] != 0)
        {
            glBindTexture(GL_TEXTURE_2D, windTextures_[currentMonthIdx]);
            glUniform1i(uniformWindTexture1_, 6);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
            glUniform1i(uniformWindTexture1_, 6);
        }

        // Bind next month texture (GL_TEXTURE7)
        glActiveTexture_ptr(GL_TEXTURE7);
        if (windTexturesLoaded_[nextMonthIdx] && windTextures_[nextMonthIdx] != 0)
        {
            glBindTexture(GL_TEXTURE_2D, windTextures_[nextMonthIdx]);
            glUniform1i(uniformWindTexture2_, 7);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
            glUniform1i(uniformWindTexture2_, 7);
        }

        // Set blend factor between months
        if (uniformWindBlendFactor_ >= 0)
        {
            glUniform1f(uniformWindBlendFactor_, blendFactor);
        }

        // Set wind texture resolution for UV normalization (1024x512 fixed resolution)
        if ((windTexturesLoaded_[currentMonthIdx] || windTexturesLoaded_[nextMonthIdx]) && uniformWindTextureSize_ >= 0)
        {
            glUniform2f(uniformWindTextureSize_, 1024.0f, 512.0f);
        }

// Unit 8: Specular/Roughness texture (surface reflectivity) (only bind if enabled and
// loaded)
#ifndef GL_TEXTURE8
#define GL_TEXTURE8 0x84C8
#endif
        glActiveTexture_ptr(GL_TEXTURE8);
        if (useSpecular_ && specularLoaded_ && specularTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, specularTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (uniformSpecular_ >= 0)
        {
            glUniform1i(uniformSpecular_, 8);
        }

        // Calculate ice mask month indices based on Julian date (same as color blending)
        // Reuse the monthPos calculation from wind texture (already computed above)
        // This ensures consistency across all monthly texture blending
        int iceIdx1 = static_cast<int>(std::floor(monthPos));
        int iceIdx2 = (iceIdx1 + 1) % MONTHS_PER_YEAR;
        float iceBlendFactor = static_cast<float>(monthPos - iceIdx1);

// Unit 14: Ice mask texture (current month) - moved to avoid conflict with landmass mask
#ifndef GL_TEXTURE14
#define GL_TEXTURE14 0x84CE
#endif
        glActiveTexture_ptr(GL_TEXTURE14);
        if (iceMasksLoaded_[iceIdx1] && iceMaskTextures_[iceIdx1] != 0)
        {
            glBindTexture(GL_TEXTURE_2D, iceMaskTextures_[iceIdx1]);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformIceMask_, 14);

// Unit 15: Ice mask texture (next month for blending)
#ifndef GL_TEXTURE15
#define GL_TEXTURE15 0x84CF
#endif
        glActiveTexture_ptr(GL_TEXTURE15);
        if (iceMasksLoaded_[iceIdx2] && iceMaskTextures_[iceIdx2] != 0)
        {
            glBindTexture(GL_TEXTURE_2D, iceMaskTextures_[iceIdx2]);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformIceMask2_, 15);

        // Ice blend factor (same as color blend factor for consistent monthly
        // transition)
        glUniform1f(uniformIceBlendFactor_, iceBlendFactor);

// Unit 9: Landmass mask texture (for ocean detection)
#ifndef GL_TEXTURE9
#define GL_TEXTURE9 0x84C9
#endif
        glActiveTexture_ptr(GL_TEXTURE9);
        if (landmassMaskLoaded_ && landmassMaskTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, landmassMaskTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformLandmassMask_, 9);

// Unit 10: Bathymetry depth texture (ocean floor depth)
#ifndef GL_TEXTURE10
#define GL_TEXTURE10 0x84CA
#endif
        glActiveTexture_ptr(GL_TEXTURE10);
        if (bathymetryLoaded_ && bathymetryDepthTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, bathymetryDepthTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformBathymetryDepth_, 10);

// Unit 11: Bathymetry normal texture (ocean floor terrain)
#ifndef GL_TEXTURE11
#define GL_TEXTURE11 0x84CB
#endif
        glActiveTexture_ptr(GL_TEXTURE11);
        if (bathymetryLoaded_ && bathymetryNormalTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, bathymetryNormalTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformBathymetryNormal_, 11);

// Unit 13: Combined normal map (landmass + bathymetry) for shadows
// Moved to unit 13 to avoid conflict with heightmap on unit 12 (needed for vertex displacement)
#ifndef GL_TEXTURE13
#define GL_TEXTURE13 0x84CD
#endif
        glActiveTexture_ptr(GL_TEXTURE13);
        if (combinedNormalLoaded_ && combinedNormalTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, combinedNormalTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformCombinedNormal_, 13);

        // Set lighting uniforms
        glUniform3f(uniformLightDir_, lightDir.x, lightDir.y, lightDir.z);
        glUniform3f(uniformLightColor_, 1.0f, 1.0f, 1.0f); // White sunlight
        glUniform3f(uniformAmbientColor_, 0.0f, 0.0f,
                    0.0f); // No ambient - Sun is exclusive light source

        // =========================================================================
        // Moonlight calculation
        // =========================================================================
        // Moonlight is reflected sunlight. Physical properties:
        // - Moon's albedo: ~0.12 (reflects 12% of incident sunlight)
        // - Full moon illuminance: ~0.1-0.25 lux (vs sun's ~100,000 lux)
        // - For visual purposes, we use a visible but realistic ratio
        //
        // Moon phase affects intensity:
        // - Full moon: sun and moon on opposite sides of Earth
        // - New moon: sun and moon on same side (moon not visible at night)
        // We approximate phase by dot(sunDir, moonDir):
        //   -1 = full moon (opposite), +1 = new moon (same side)

        glm::vec3 moonDir = glm::normalize(moonDirection);
        float sunMoonDot = glm::dot(lightDir, moonDir);

        // Moon phase factor: 0 at new moon, 1 at full moon
        // This is simplified - real phase depends on viewing angle
        float moonPhase = 0.5f - (0.5f * sunMoonDot);

        // Moonlight intensity: base intensity * phase
        // Using ~0.03 as base (visible but not overwhelming sun)
        // Full moon: 0.03, half moon: 0.015, new moon: ~0
        float moonIntensity = 0.03f * moonPhase;

        // Moonlight color: slightly warm/gray (reflected from gray lunar
        // surface) Slight blue-shift from Earth's atmosphere at night
        glm::vec3 moonColor = glm::vec3(0.8f, 0.85f, 1.0f) * moonIntensity;

        glUniform3f(uniformMoonDir_, moonDir.x, moonDir.y, moonDir.z);
        glUniform3f(uniformMoonColor_, moonColor.r, moonColor.g, moonColor.b);

        // Camera position for view direction calculations
        glUniform3f(uniformCameraPos_, cameraPos.x, cameraPos.y, cameraPos.z);

        // Camera direction and FOV for billboard imposter computation
        if (uniformCameraDir_ >= 0)
        {
            glm::vec3 cameraDirNorm = glm::normalize(g_cameraDirection);
            glUniform3f(uniformCameraDir_, cameraDirNorm.x, cameraDirNorm.y, cameraDirNorm.z);
        }
        if (uniformCameraFOV_ >= 0)
        {
            glUniform1f(uniformCameraFOV_, g_cameraFovRadians);
        }

        // Pass pole direction for tangent frame calculation
        glm::vec3 poleNorm = glm::normalize(poleDirection);
        glUniform3f(uniformPoleDir_, poleNorm.x, poleNorm.y, poleNorm.z);

        // Pass prime meridian direction for coordinate system (matching C++ UV calculation)
        glm::vec3 primeNorm = glm::normalize(primeMeridianDirection);
        if (uniformPrimeMeridianDir_ >= 0)
        {
            glUniform3f(uniformPrimeMeridianDir_, primeNorm.x, primeNorm.y, primeNorm.z);
        }

        // Pass time (Julian date fraction) for animated noise
        // Use fractional part so noise cycles smoothly
        float timeFrac = static_cast<float>(std::fmod(julianDate, 1.0));
        glUniform1f(uniformTime_, timeFrac);

        // Pass planet radius for WGS 84 oblateness calculation
        if (uniformPlanetRadius_ >= 0)
        {
            glUniform1f(uniformPlanetRadius_, displayRadius);
        }

        // Set displacement scale multiplier (moderate exaggeration for visibility)
        // 10x makes mountains visible at planetary scale while maintaining reasonable proportions
        // Mt. Everest (8848m) on Earth (6371km) = 0.00139 ratio
        // With 10x multiplier: displacement = 0.0139 * radius (visible but not exaggerated)
        if (uniformDisplacementScale_ >= 0)
        {
            glUniform1f(uniformDisplacementScale_, 10.0f);
        }

        // Set flat circle mode uniforms (will be set to 0 for normal rendering, 1 for flat circle)
        if (uniformFlatCircleMode_ >= 0)
        {
            glUniform1i(uniformFlatCircleMode_, 0); // Default to normal sphere mode
        }
        if (uniformSphereCenter_ >= 0)
        {
            glUniform3f(uniformSphereCenter_, position.x, position.y, position.z);
        }
        if (uniformSphereRadius_ >= 0)
        {
            glUniform1f(uniformSphereRadius_, displayRadius);
        }

        // Set toggle uniforms
        if (uniformUseNormalMap_ >= 0)
        {
            glUniform1i(uniformUseNormalMap_, useNormalMap_ ? 1 : 0);
        }
        if (uniformUseHeightmap_ >= 0)
        {
            glUniform1i(uniformUseHeightmap_, useHeightmap_ ? 1 : 0);
        }
        if (uniformUseDisplacement_ >= 0)
        {
            // Enable displacement if heightmap and landmass mask are loaded
            bool enableDisplacement = useHeightmap_ && elevationLoaded_ && landmassMaskLoaded_;
            glUniform1i(uniformUseDisplacement_, enableDisplacement ? 1 : 0);
        }
        if (uniformUseSpecular_ >= 0)
        {
            glUniform1i(uniformUseSpecular_, useSpecular_ ? 1 : 0);
        }

        // Draw sphere with shader (moderate tessellation is fine with per-pixel
        // normals)
        // Use camera info set via setCameraInfo() for geometry culling
        // Calculate dynamic tessellation based on camera distance
        glm::vec3 closestPointOnSphere;
        auto [baseSlices, baseStacks, localSlices, localStacks] =
            calculateTessellation(position, displayRadius, g_cameraPosition, closestPointOnSphere);
        drawTexturedSphere(position,
                           displayRadius,
                           poleDirection,
                           primeMeridianDirection,
                           baseSlices,
                           baseStacks,
                           localSlices,
                           localStacks,
                           g_cameraPosition,
                           g_cameraDirection,
                           g_cameraFovRadians,
                           false,
                           false,
                           closestPointOnSphere, // Enable culling for normal rendering
                           uniformFlatCircleMode_,
                           uniformSphereCenter_,
                           uniformSphereRadius_,
                           uniformBillboardCenter_);

        // Restore state - unbind all texture units we used
        glUseProgram(0);
        glActiveTexture_ptr(GL_TEXTURE13);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE11);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE10);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE9);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// ============================================================================
// Textured Sphere Rendering
// ============================================================================
// Draws a textured sphere using immediate mode OpenGL.
// When shaders are active, the fragment shader handles per-pixel normal
// mapping. The geometry just needs to provide position, normal (for TBN), and
// UV coords.

void EarthMaterial::drawTexturedSphere(const glm::vec3 &position,
                                       float radius,
                                       const glm::vec3 &poleDir,
                                       const glm::vec3 &primeDir,
                                       int baseSlices,
                                       int baseStacks,
                                       int localSlices,
                                       int localStacks,
                                       const glm::vec3 &cameraPos,
                                       const glm::vec3 &cameraDir,
                                       float fovRadians,
                                       bool disableCulling,
                                       bool enableOcclusionCulling,
                                       const glm::vec3 &closestPointOnSphere,
                                       GLint uniformFlatCircleMode,
                                       GLint uniformSphereCenter,
                                       GLint uniformSphereRadius,
                                       GLint uniformBillboardCenter)
{
    // Calculate distance to sphere
    glm::vec3 toSphere = position - cameraPos;
    float distance = glm::length(toSphere);
    float distanceInRadii = distance / radius;

    // If distance > 5 * radius, render as billboard imposter
    // All computation happens in shaders - C++ just provides data and renders simple geometry
    if (distanceInRadii > TESSELATION_DISTANCE_THRESHOLD)
    {
        // Calculate number of triangles: interpolate from 128 (far) to 8 (at 5 radii)
        // Use a reasonable maximum distance for interpolation (e.g., 20 radii)
        constexpr float MAX_FAR_DISTANCE_RADII = 20.0f;
        float t = (distanceInRadii - TESSELATION_DISTANCE_THRESHOLD) /
                  (MAX_FAR_DISTANCE_RADII - TESSELATION_DISTANCE_THRESHOLD);
        t = glm::clamp(t, 0.0f, 1.0f);
        // At far distance (t=1): use MAX triangles, at 5 radii (t=0): use MIN triangles
        int numTriangles = static_cast<int>(
            std::round(FAR_TRIANGLE_COUNT_MAX - t * (FAR_TRIANGLE_COUNT_MAX - FAR_TRIANGLE_COUNT_MIN)));
        numTriangles = std::max(numTriangles, FAR_TRIANGLE_COUNT_MIN);
        numTriangles = std::min(numTriangles, FAR_TRIANGLE_COUNT_MAX);

        // Check if we're in wireframe mode (shaders disabled)
        // If so, render fan geometry directly in world space like DynamicLODSphere does
        if (uniformFlatCircleMode < 0)
        {
            // Wireframe mode: render fan geometry directly in world space
            glm::vec3 toSphereNorm = distance > 0.001f ? toSphere / distance : glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 closestPointOnSphere = position - toSphereNorm * radius;

            // Calculate visible circle radius on sphere surface
            float sphereAngularRadius = std::asin(glm::clamp(radius / distance, 0.0f, 1.0f));
            float hemisphereAngle = 1.57079632679f; // PI/2 = 90 degrees
            float actualAngularRadius = std::min(sphereAngularRadius, hemisphereAngle);

            // Build orthonormal basis for the circle plane
            glm::vec3 centerDir = glm::normalize(closestPointOnSphere - position);
            glm::vec3 toClosestPoint = closestPointOnSphere - cameraPos;
            float distanceToCircle = glm::length(toClosestPoint);

            // Build coordinate system
            glm::vec3 north = glm::normalize(poleDir);
            glm::vec3 east = primeDir - glm::dot(primeDir, north) * north;
            if (glm::length(east) < 0.001f)
            {
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

            // Project onto tangent plane
            glm::vec3 tangentNorth = north - glm::dot(north, centerDir) * centerDir;
            glm::vec3 tangentEast = east - glm::dot(east, centerDir) * centerDir;

            if (glm::length(tangentNorth) > 0.001f)
            {
                tangentNorth = glm::normalize(tangentNorth);
            }
            else
            {
                tangentNorth = glm::normalize(glm::cross(centerDir, glm::vec3(1.0f, 0.0f, 0.0f)));
                if (glm::length(tangentNorth) < 0.001f)
                {
                    tangentNorth = glm::normalize(glm::cross(centerDir, glm::vec3(0.0f, 0.0f, 1.0f)));
                }
            }

            if (glm::length(tangentEast) > 0.001f)
            {
                tangentEast = glm::normalize(tangentEast);
            }
            else
            {
                tangentEast = glm::normalize(glm::cross(centerDir, tangentNorth));
            }

            tangentEast = glm::normalize(glm::cross(centerDir, tangentNorth));

            float circleRadius = distanceToCircle * std::tan(actualAngularRadius);

            glPushMatrix();
            glTranslatef(position.x, position.y, position.z);

            glBegin(GL_TRIANGLES);

            glm::vec3 centerNormal = centerDir;

            for (int i = 0; i < numTriangles; i++)
            {
                float angle1 = 2.0f * static_cast<float>(PI) * static_cast<float>(i) / static_cast<float>(numTriangles);
                float angle2 =
                    2.0f * static_cast<float>(PI) * static_cast<float>(i + 1) / static_cast<float>(numTriangles);

                float circleX1 = circleRadius * std::cos(angle1);
                float circleY1 = circleRadius * std::sin(angle1);
                float circleX2 = circleRadius * std::cos(angle2);
                float circleY2 = circleRadius * std::sin(angle2);

                glm::vec3 flatPoint1 = closestPointOnSphere + circleX1 * tangentEast + circleY1 * tangentNorth;
                glm::vec3 flatPoint2 = closestPointOnSphere + circleX2 * tangentEast + circleY2 * tangentNorth;

                glm::vec3 dir1 = glm::normalize(flatPoint1 - position);
                glm::vec3 dir2 = glm::normalize(flatPoint2 - position);
                glm::vec3 normal1 = dir1;
                glm::vec3 normal2 = dir2;

                // Render triangle: center, edge1, edge2
                glTexCoord2f(0.5f, 0.5f);
                glNormal3f(centerNormal.x, centerNormal.y, centerNormal.z);
                glVertex3f(closestPointOnSphere.x - position.x,
                           closestPointOnSphere.y - position.y,
                           closestPointOnSphere.z - position.z);

                glTexCoord2f(0.5f, 0.5f);
                glNormal3f(normal1.x, normal1.y, normal1.z);
                glVertex3f(flatPoint1.x - position.x, flatPoint1.y - position.y, flatPoint1.z - position.z);

                glTexCoord2f(0.5f, 0.5f);
                glNormal3f(normal2.x, normal2.y, normal2.z);
                glVertex3f(flatPoint2.x - position.x, flatPoint2.y - position.y, flatPoint2.z - position.z);

                CountTriangles(GL_TRIANGLES, 1);
            }

            glEnd();
            glPopMatrix();

            return;
        }

        // Shader-based rendering (normal mode)
        // Set all uniforms - shaders will do all the computation
        glUniform1i(uniformFlatCircleMode, 1); // Enable billboard imposter mode
        if (uniformSphereCenter >= 0)
        {
            glUniform3f(uniformSphereCenter, position.x, position.y, position.z);
        }
        if (uniformSphereRadius >= 0)
        {
            glUniform1f(uniformSphereRadius, radius);
        }
        // Note: uBillboardCenter is computed in vertex shader, not needed here

        // Set camera and coordinate system uniforms (need to add these)
        // These will be set elsewhere, but we need to ensure they're available

        // Render simple triangle fan - vertex shader will compute all positions
        // Input vertices are normalized coordinates (-1 to 1) on the billboard plane
        glBegin(GL_TRIANGLES);

        for (int i = 0; i < numTriangles; i++)
        {
            float angle1 = 2.0f * static_cast<float>(PI) * static_cast<float>(i) / static_cast<float>(numTriangles);
            float angle2 = 2.0f * static_cast<float>(PI) * static_cast<float>(i + 1) / static_cast<float>(numTriangles);

            // Calculate normalized coordinates on billboard plane (-1 to 1)
            float x1 = std::cos(angle1);
            float y1 = std::sin(angle1);
            float x2 = std::cos(angle2);
            float y2 = std::sin(angle2);

            // Render triangle: center, edge1, edge2
            // Vertex shader will compute actual world positions from these normalized coordinates
            // All UVs and normals are dummy values - fragment shader will recompute everything

            // Center point
            glTexCoord2f(0.5f, 0.5f);
            glNormal3f(0.0f, 0.0f, 1.0f);
            glVertex3f(0.0f, 0.0f, 0.0f);

            // Edge point 1
            glTexCoord2f(0.5f, 0.5f);
            glNormal3f(0.0f, 0.0f, 1.0f);
            glVertex3f(x1, y1, 0.0f);

            // Edge point 2
            glTexCoord2f(0.5f, 0.5f);
            glNormal3f(0.0f, 0.0f, 1.0f);
            glVertex3f(x2, y2, 0.0f);

            CountTriangles(GL_TRIANGLES, 1);
        }

        glEnd();

        // Disable flat circle mode for normal rendering
        glUniform1i(uniformFlatCircleMode, 0);

        return;
    }

    glm::vec3 north = glm::normalize(poleDir);

    glm::vec3 east = primeDir - glm::dot(primeDir, north) * north;
    if (glm::length(east) < 0.001f)
    {
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

    glm::vec3 south90 = glm::normalize(glm::cross(north, east));

    // Calculate camera-to-sphere vector for back-face culling
    float distToSphere = distance;
    glm::vec3 toSphereNorm = distToSphere > 0.001f ? toSphere / distToSphere : glm::vec3(0.0f, 0.0f, 1.0f);

    // For back-face culling: only render hemisphere facing camera
    // A vertex normal faces the camera if dot(normal, cameraToVertex) < 0
    // Since normals point outward, we check dot(normal, cameraToSphere) < 0

    // Calculate frustum cone parameters
    // Expand frustum by 15 degrees for better edge handling
    float halfFov = fovRadians * 0.5f;
    float expandedHalfFov = halfFov + glm::radians(15.0f);
    float cosExpandedHalfFov = cos(expandedHalfFov);

    glPushMatrix();
    glTranslatef(position.x, position.y, position.z);

    // Calculate tessellation multipliers for each LOD level
    // LOD 0 = base, LOD 1 = 1/2 radius, LOD 2 = 1/4 radius, LOD 3 = 1/8 radius, LOD 4 = 1/16 radius
    // Each level gets progressively higher tessellation
    int lodSlices[5], lodStacks[5];
    lodSlices[0] = baseSlices;
    lodStacks[0] = baseStacks;
    lodSlices[1] = baseSlices * 2; // 2x for 1/2 radius
    lodStacks[1] = baseStacks * 2;
    lodSlices[2] = baseSlices * 4; // 4x for 1/4 radius
    lodStacks[2] = baseStacks * 4;
    lodSlices[3] = baseSlices * 8; // 8x for 1/8 radius
    lodStacks[3] = baseStacks * 8;
    lodSlices[4] = baseSlices * 16; // 16x for 1/16 radius
    lodStacks[4] = baseStacks * 16;

    // Strategy: Render base-resolution mesh first, then add subdivision in local region
    // This ensures we have a complete base mesh, then stitch high-res triangles onto it

    int verticesDrawn = 0;

    // Helper function to determine LOD level based on distance from camera
    // Returns: 0 = base, 1 = 1/2 radius, 2 = 1/4 radius, 3 = 1/8 radius, 4 = 1/16 radius
    // The radii are centered around the camera position, not the closest point on sphere
    auto getLODLevel = [&](const glm::vec3 &worldPos) -> int {
        // Calculate distance from camera to this point on the sphere surface
        glm::vec3 toPoint = worldPos - cameraPos;
        float distance = glm::length(toPoint);

        // Define 4 radii around camera position: 1/2, 1/4, 1/8, 1/16 of planet radius
        float radius1 = radius * 0.5f;    // 1/2 radius
        float radius2 = radius * 0.25f;   // 1/4 radius
        float radius3 = radius * 0.125f;  // 1/8 radius
        float radius4 = radius * 0.0625f; // 1/16 radius

        // Return LOD level based on which radius band the point falls into
        // Higher number = closer to camera = higher tessellation
        if (distance <= radius4)
            return 4; // Highest detail (1/16 radius)
        else if (distance <= radius3)
            return 3; // High detail (1/8 radius)
        else if (distance <= radius2)
            return 2; // Medium-high detail (1/4 radius)
        else if (distance <= radius1)
            return 1; // Medium detail (1/2 radius)
        else
            return 0; // Base detail (outside all radii)
    };

    // Helper function to check triangle visibility
    auto isTriangleVisible = [&](const glm::vec3 &v1,
                                 const glm::vec3 &v2,
                                 const glm::vec3 &v3,
                                 const glm::vec3 &n1,
                                 const glm::vec3 &n2,
                                 const glm::vec3 &n3) -> bool {
        if (disableCulling)
            return true;

        const float MAX_ANGLE_FROM_CAMERA = 0.6f * static_cast<float>(PI);
        const float COS_MAX_ANGLE = std::cos(MAX_ANGLE_FROM_CAMERA);

        glm::vec3 toV1 = v1 - cameraPos;
        glm::vec3 toV2 = v2 - cameraPos;
        glm::vec3 toV3 = v3 - cameraPos;

        float dist1 = glm::length(toV1);
        float dist2 = glm::length(toV2);
        float dist3 = glm::length(toV3);

        glm::vec3 dir1 = dist1 > 0.001f ? toV1 / dist1 : glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 dir2 = dist2 > 0.001f ? toV2 / dist2 : glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 dir3 = dist3 > 0.001f ? toV3 / dist3 : glm::vec3(0.0f, 0.0f, 1.0f);

        bool v1FrontFacing = dist1 <= radius * 0.1f || glm::dot(n1, -cameraDir) >= COS_MAX_ANGLE;
        bool v2FrontFacing = dist2 <= radius * 0.1f || glm::dot(n2, -cameraDir) >= COS_MAX_ANGLE;
        bool v3FrontFacing = dist3 <= radius * 0.1f || glm::dot(n3, -cameraDir) >= COS_MAX_ANGLE;

        bool allBackFacing = !v1FrontFacing && !v2FrontFacing && !v3FrontFacing;
        if (allBackFacing)
            return false;

        bool v1InFrustum = dist1 <= radius * 0.1f || glm::dot(dir1, cameraDir) >= cosExpandedHalfFov;
        bool v2InFrustum = dist2 <= radius * 0.1f || glm::dot(dir2, cameraDir) >= cosExpandedHalfFov;
        bool v3InFrustum = dist3 <= radius * 0.1f || glm::dot(dir3, cameraDir) >= cosExpandedHalfFov;

        return v1InFrustum || v2InFrustum || v3InFrustum;
    };

    // Helper function to render a single triangle
    auto renderTriangle = [&](const glm::vec3 &v1,
                              const glm::vec3 &v2,
                              const glm::vec3 &v3,
                              const glm::vec3 &n1,
                              const glm::vec3 &n2,
                              const glm::vec3 &n3,
                              const glm::vec2 &uv1,
                              const glm::vec2 &uv2,
                              const glm::vec2 &uv3) {
        if (!isTriangleVisible(v1, v2, v3, n1, n2, n3))
            return;

        glTexCoord2f(uv1.x, uv1.y);
        glNormal3f(n1.x, n1.y, n1.z);
        glVertex3f(v1.x - position.x, v1.y - position.y, v1.z - position.z);

        glTexCoord2f(uv2.x, uv2.y);
        glNormal3f(n2.x, n2.y, n2.z);
        glVertex3f(v2.x - position.x, v2.y - position.y, v2.z - position.z);

        glTexCoord2f(uv3.x, uv3.y);
        glNormal3f(n3.x, n3.y, n3.z);
        glVertex3f(v3.x - position.x, v3.y - position.y, v3.z - position.z);

        verticesDrawn += 3;
        CountTriangles(GL_TRIANGLES, 3);
    };

    // First pass: Render base-resolution mesh for entire sphere
    // This creates a complete base-resolution mesh covering the entire sphere
    glBegin(GL_TRIANGLES);
    if (disableCulling)
    {
        glColor3f(0.8f, 0.9f, 1.0f);
    }

    for (int i = 0; i < baseStacks; i++)
    {
        float phi1 = static_cast<float>(PI) * (static_cast<float>(i) / static_cast<float>(baseStacks) - 0.5f);
        float phi2 = static_cast<float>(PI) * (static_cast<float>(i + 1) / static_cast<float>(baseStacks) - 0.5f);

        float vTexCoord1 = static_cast<float>(i) / static_cast<float>(baseStacks);
        float vTexCoord2 = static_cast<float>(i + 1) / static_cast<float>(baseStacks);

        float cosPhi1 = cos(phi1);
        float sinPhi1 = sin(phi1);
        float cosPhi2 = cos(phi2);
        float sinPhi2 = sin(phi2);

        for (int j = 0; j < baseSlices; j++)
        {
            float theta1 = 2.0f * static_cast<float>(PI) * static_cast<float>(j) / static_cast<float>(baseSlices);
            float theta2 = 2.0f * static_cast<float>(PI) * static_cast<float>(j + 1) / static_cast<float>(baseSlices);

            float uCoord1 = static_cast<float>(j) / static_cast<float>(baseSlices);
            float uCoord2 = static_cast<float>(j + 1) / static_cast<float>(baseSlices);

            float theta1Shifted = theta1 - static_cast<float>(PI);
            float theta2Shifted = theta2 - static_cast<float>(PI);

            float cosTheta1 = cos(theta1Shifted);
            float sinTheta1 = sin(theta1Shifted);
            float cosTheta2 = cos(theta2Shifted);
            float sinTheta2 = sin(theta2Shifted);

            // Four vertices forming a quad (two triangles)
            glm::vec3 localDir1 = cosPhi1 * (cosTheta1 * east + sinTheta1 * south90) + sinPhi1 * north;
            glm::vec3 worldPos1 = position + radius * localDir1;

            glm::vec3 localDir2 = cosPhi2 * (cosTheta1 * east + sinTheta1 * south90) + sinPhi2 * north;
            glm::vec3 worldPos2 = position + radius * localDir2;

            glm::vec3 localDir3 = cosPhi2 * (cosTheta2 * east + sinTheta2 * south90) + sinPhi2 * north;
            glm::vec3 worldPos3 = position + radius * localDir3;

            glm::vec3 localDir4 = cosPhi1 * (cosTheta2 * east + sinTheta2 * south90) + sinPhi1 * north;
            glm::vec3 worldPos4 = position + radius * localDir4;

            // Determine LOD level for each vertex (based on distance from camera)
            int lod1 = getLODLevel(worldPos1);
            int lod2 = getLODLevel(worldPos2);
            int lod3 = getLODLevel(worldPos3);
            int lod4 = getLODLevel(worldPos4);

            // Find maximum LOD level needed for this quad
            int maxLOD = std::max({lod1, lod2, lod3, lod4});

            // Render base-resolution triangles only if max LOD is 0 (outside all radii)
            if (maxLOD == 0)
            {
                // Triangle 1: v1, v2, v3
                renderTriangle(worldPos1,
                               worldPos2,
                               worldPos3,
                               localDir1,
                               localDir2,
                               localDir3,
                               glm::vec2(uCoord1, vTexCoord1),
                               glm::vec2(uCoord1, vTexCoord2),
                               glm::vec2(uCoord2, vTexCoord2));

                // Triangle 2: v1, v3, v4
                renderTriangle(worldPos1,
                               worldPos3,
                               worldPos4,
                               localDir1,
                               localDir3,
                               localDir4,
                               glm::vec2(uCoord1, vTexCoord1),
                               glm::vec2(uCoord2, vTexCoord2),
                               glm::vec2(uCoord2, vTexCoord1));
            }
        }
    }
    glEnd();

    // Render subdivided triangles for each LOD level (1-4)
    // Process from highest to lowest LOD to ensure proper overdraw (higher detail covers lower detail)
    for (int lodLevel = 4; lodLevel >= 1; lodLevel--)
    {
        int targetSlices = lodSlices[lodLevel];
        int targetStacks = lodStacks[lodLevel];

        // Skip if this LOD level doesn't require more tessellation than base
        if (targetSlices <= baseSlices && targetStacks <= baseStacks)
            continue;

        glBegin(GL_TRIANGLES);
        if (disableCulling)
        {
            glColor3f(0.8f, 0.9f, 1.0f);
        }

        // Calculate subdivision factors for this LOD level
        int sliceSubdiv = targetSlices / baseSlices;
        int stackSubdiv = targetStacks / baseStacks;

        for (int i = 0; i < baseStacks; i++)
        {
            float phiBase1 = static_cast<float>(PI) * (static_cast<float>(i) / static_cast<float>(baseStacks) - 0.5f);
            float phiBase2 =
                static_cast<float>(PI) * (static_cast<float>(i + 1) / static_cast<float>(baseStacks) - 0.5f);

            for (int j = 0; j < baseSlices; j++)
            {
                float thetaBase1 =
                    2.0f * static_cast<float>(PI) * static_cast<float>(j) / static_cast<float>(baseSlices);
                float thetaBase2 =
                    2.0f * static_cast<float>(PI) * static_cast<float>(j + 1) / static_cast<float>(baseSlices);

                // Check LOD level once per base quad (at center) to avoid n-squared issue
                float thetaMid = (thetaBase1 + thetaBase2) * 0.5f;
                float phiMid = (phiBase1 + phiBase2) * 0.5f;
                float thetaMidShifted = thetaMid - static_cast<float>(PI);
                float cosThetaMid = cos(thetaMidShifted);
                float sinThetaMid = sin(thetaMidShifted);
                float cosPhiMid = cos(phiMid);
                float sinPhiMid = sin(phiMid);

                glm::vec3 localDirMid = cosPhiMid * (cosThetaMid * east + sinThetaMid * south90) + sinPhiMid * north;
                glm::vec3 worldPosMid = position + radius * localDirMid;

                // Use discrete LOD level for now (can be enhanced with smooth transitions later)
                int baseQuadLOD = getLODLevel(worldPosMid);

                // Allow rendering in transition zones (render if LOD is within 1 level)
                // This creates smoother edges between tessellation levels
                if (baseQuadLOD < lodLevel - 1)
                {
                    continue; // Skip this base quad - it's too far from this LOD level
                }

                // Subdivide this quad
                for (int si = 0; si < stackSubdiv; si++)
                {
                    float t1 = static_cast<float>(si) / static_cast<float>(stackSubdiv);
                    float t2 = static_cast<float>(si + 1) / static_cast<float>(stackSubdiv);
                    float phi1 = phiBase1 + (phiBase2 - phiBase1) * t1;
                    float phi2 = phiBase1 + (phiBase2 - phiBase1) * t2;

                    float cosPhi1 = cos(phi1);
                    float sinPhi1 = sin(phi1);
                    float cosPhi2 = cos(phi2);
                    float sinPhi2 = sin(phi2);

                    float vTexCoord1 = (static_cast<float>(i) + t1) / static_cast<float>(baseStacks);
                    float vTexCoord2 = (static_cast<float>(i) + t2) / static_cast<float>(baseStacks);

                    for (int sj = 0; sj < sliceSubdiv; sj++)
                    {
                        float s1 = static_cast<float>(sj) / static_cast<float>(sliceSubdiv);
                        float s2 = static_cast<float>(sj + 1) / static_cast<float>(sliceSubdiv);
                        float theta1 = thetaBase1 + (thetaBase2 - thetaBase1) * s1;
                        float theta2 = thetaBase1 + (thetaBase2 - thetaBase1) * s2;

                        float theta1Shifted = theta1 - static_cast<float>(PI);
                        float theta2Shifted = theta2 - static_cast<float>(PI);
                        float cosTheta1 = cos(theta1Shifted);
                        float sinTheta1 = sin(theta1Shifted);
                        float cosTheta2 = cos(theta2Shifted);
                        float sinTheta2 = sin(theta2Shifted);

                        float uCoord1 = (static_cast<float>(j) + s1) / static_cast<float>(baseSlices);
                        float uCoord2 = (static_cast<float>(j) + s2) / static_cast<float>(baseSlices);

                        // Four vertices forming a subdivided quad
                        glm::vec3 localDir1 = cosPhi1 * (cosTheta1 * east + sinTheta1 * south90) + sinPhi1 * north;
                        glm::vec3 worldPos1 = position + radius * localDir1;

                        glm::vec3 localDir2 = cosPhi2 * (cosTheta1 * east + sinTheta1 * south90) + sinPhi2 * north;
                        glm::vec3 worldPos2 = position + radius * localDir2;

                        glm::vec3 localDir3 = cosPhi2 * (cosTheta2 * east + sinTheta2 * south90) + sinPhi2 * north;
                        glm::vec3 worldPos3 = position + radius * localDir3;

                        glm::vec3 localDir4 = cosPhi1 * (cosTheta2 * east + sinTheta2 * south90) + sinPhi1 * north;
                        glm::vec3 worldPos4 = position + radius * localDir4;

                        // Render triangles directly - LOD check already done at base quad level
                        // Triangle 1: v1, v2, v3
                        renderTriangle(worldPos1,
                                       worldPos2,
                                       worldPos3,
                                       localDir1,
                                       localDir2,
                                       localDir3,
                                       glm::vec2(uCoord1, vTexCoord1),
                                       glm::vec2(uCoord1, vTexCoord2),
                                       glm::vec2(uCoord2, vTexCoord2));

                        // Triangle 2: v1, v3, v4
                        renderTriangle(worldPos1,
                                       worldPos3,
                                       worldPos4,
                                       localDir1,
                                       localDir3,
                                       localDir4,
                                       glm::vec2(uCoord1, vTexCoord1),
                                       glm::vec2(uCoord2, vTexCoord2),
                                       glm::vec2(uCoord2, vTexCoord1));
                    }
                }
            }
        }
        glEnd();
    }

    glPopMatrix();
}

// Draw wireframe version of Earth (for wireframe overlay mode)
void EarthMaterial::drawWireframe(const glm::vec3 &position,
                                  float displayRadius,
                                  const glm::vec3 &poleDirection,
                                  const glm::vec3 &primeMeridianDirection,
                                  double julianDate,
                                  const glm::vec3 &cameraPos)
{
    // Render the same geometry as drawTexturedSphere but without shaders
    // This allows glPolygonMode(GL_LINE) to work
    // Shader should already be unbound by the caller, but ensure it's off
    glUseProgram(0);

    // CRITICAL: Ensure lighting is disabled and material properties don't affect color
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
    glDisable(GL_COLOR_MATERIAL);

    // Set material to fully emissive so color is used directly (no lighting calculation)
    GLfloat matEmissive[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, matEmissive);

    // Ensure color is set explicitly (will be used directly since lighting is off)
    glColor3f(0.8f, 0.9f, 1.0f);

    // Calculate dynamic tessellation based on camera distance
    glm::vec3 closestPointOnSphere;
    auto [baseSlices, baseStacks, localSlices, localStacks] =
        calculateTessellation(position, displayRadius, cameraPos, closestPointOnSphere);
    drawTexturedSphere(position,
                       displayRadius,
                       poleDirection,
                       primeMeridianDirection,
                       baseSlices,
                       baseStacks,
                       localSlices,
                       localStacks,
                       cameraPos,
                       g_cameraDirection,
                       g_cameraFovRadians,
                       true, // Disable culling for wireframe (show all edges)
                       false,
                       closestPointOnSphere,
                       -1, // No flat circle mode for wireframe
                       -1,
                       -1);
}
