// stars-dynamic-skybox.cpp
// Stub file - skybox rendering is now handled via ray-miss in single-pass-screen.frag
// Preprocessing is handled by skybox-textures.cpp
// This file only provides stub implementations for legacy API compatibility

#include "stars-dynamic-skybox.h"
#include "helpers/vulkan.h"
#include <cmath>
#include <glm/gtc/constants.hpp>

// ==================================
// Legacy API Stubs
// ==================================
// These functions are no longer used since the skybox is rendered
// via the ray-miss case in single-pass-screen.frag, which samples
// the cubemap texture loaded by loadSkyboxTexture().

void InitializeSkybox([[maybe_unused]] const std::string &defaultsPath)
{
    // No-op: Skybox initialization is no longer needed
    // The cubemap texture is loaded via loadSkyboxTexture() in screen-renderer.cpp
}

bool IsSkyboxInitialized()
{
    // Always return true - skybox is implicitly ready when the cubemap texture is loaded
    return true;
}

bool InitializeStarTextureMaterial([[maybe_unused]] const std::string &texturePath,
                                   [[maybe_unused]] TextureResolution resolution)
{
    // No-op: Star texture is loaded via loadSkyboxTexture() in screen-renderer.cpp
    return true;
}

bool IsStarTextureReady()
{
    // Check if the skybox texture is loaded in Vulkan
    extern VulkanContext *g_vulkanContext;
    if (g_vulkanContext)
    {
        return g_vulkanContext->skyboxTextureReady;
    }
    return false;
}

void DrawSkyboxTextured([[maybe_unused]] const glm::vec3 &cameraPos,
                        [[maybe_unused]] const glm::mat4 &viewMatrix,
                        [[maybe_unused]] const glm::mat4 &projectionMatrix)
{
    // No-op: Skybox is rendered via ray-miss in single-pass-screen.frag
}

void DrawSkyboxWireframe([[maybe_unused]] const glm::vec3 &cameraPos)
{
    // No-op: Wireframe skybox not implemented for ray-miss rendering
}

void CleanupSkyboxVulkan()
{
    // No-op: Skybox texture cleanup is handled by cleanupSkyboxTexture() in vulkan.cpp
}

// ==================================
// Utility Functions
// ==================================

glm::vec3 raDecToCartesianHours(float raHours, float decDeg, float radius)
{
    // Convert RA from hours to radians (24 hours = 2*PI)
    float raRad = raHours * (glm::pi<float>() / 12.0f);
    // Convert Dec from degrees to radians
    float decRad = glm::radians(decDeg);

    // Standard spherical to Cartesian conversion
    // RA=0 points toward +X (vernal equinox), Dec=0 is equatorial plane
    float x = radius * std::cos(decRad) * std::cos(raRad);
    float y = radius * std::cos(decRad) * std::sin(raRad);
    float z = radius * std::sin(decRad);

    return glm::vec3(x, y, z);
}

float getEarthRotationAngle(double jd)
{
    // Calculate Greenwich Mean Sidereal Time (GMST) for the given Julian Date
    // Based on the IAU 2006 precession model
    constexpr double JD2000 = 2451545.0;
    double T = (jd - JD2000) / 36525.0;
    double gmst = 280.46061837 + 360.98564736629 * (jd - JD2000) + 0.000387933 * T * T;
    gmst = std::fmod(gmst, 360.0);
    if (gmst < 0)
        gmst += 360.0;
    return static_cast<float>(gmst);
}
