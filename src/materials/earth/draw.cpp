// ============================================================================
// Drawing
// ============================================================================

#include "../../concerns/constants.h"
#include "../../concerns/helpers/gl.h"
#include "../../concerns/helpers/vulkan.h"
#include "../../concerns/ui-overlay.h"
#include "earth-material.h"

#include <array>
#include <cmath>
#include <cstddef> // for offsetof
#include <cstdlib>
#include <cstring> // for memcpy
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> // for lookAt, perspective
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <tuple>

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

// View and projection matrices for Vulkan rendering (set before rendering each frame)
static glm::mat4 g_viewMatrix = glm::mat4(1.0f);
static glm::mat4 g_projectionMatrix = glm::mat4(1.0f);
static bool g_matricesSet = false;

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

// Set view and projection matrices for Vulkan rendering
void EarthMaterial::setViewProjectionMatrices(const glm::mat4 &viewMatrix, const glm::mat4 &projectionMatrix)
{
    g_viewMatrix = viewMatrix;
    // Convert OpenGL-style projection matrix to Vulkan format by flipping Y-axis
    // Vulkan's NDC has Y pointing down, while OpenGL has Y pointing up
    glm::mat4 vulkanFlip = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
    g_projectionMatrix = vulkanFlip * projectionMatrix;
    g_matricesSet = true;
}

glm::mat4 EarthMaterial::getViewMatrix()
{
    return g_viewMatrix;
}

glm::mat4 EarthMaterial::getProjectionMatrix()
{
    return g_projectionMatrix;
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
        static bool errorPrinted = false;
        if (!errorPrinted)
        {
            std::cerr << "ERROR: EarthMaterial::draw() - Shader not available!" << "\n";
            std::cerr << "  Shader compilation or linking failed. Check console "
                         "for shader errors."
                      << "\n";
            std::cerr << "  NOTE: Vulkan shader compilation requires glslangValidator from the Vulkan SDK." << "\n";
            std::cerr << "  Install Vulkan SDK from https://vulkan.lunarg.com/ and set VULKAN_SDK environment variable."
                      << "\n";
            errorPrinted = true;
        }
        return; // Return early instead of exiting - allows application to continue (though rendering won't work)
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

    // Generate octree mesh on first use (MANDATORY - required for rendering)
    if (!meshGenerated_)
    {
        if (!elevationLoaded_)
        {
            std::cerr << "ERROR: EarthMaterial::draw() - Cannot generate octree mesh without elevation data!" << "\n";
            std::cerr << "  Elevation data must be loaded before rendering." << "\n";
            std::exit(1);
        }

        // Calculate max radius (exosphere): Earth radius + 10,000 km atmosphere
        const float EARTH_RADIUS_KM = 6371.0f;
        const float EXOSPHERE_HEIGHT_KM = 10000.0f;
        float maxRadius = displayRadius * (1.0f + EXOSPHERE_HEIGHT_KM / EARTH_RADIUS_KM);
        generateOctreeMesh(displayRadius, maxRadius);

        // Verify octree was built successfully (we're using voxels directly, not meshes)
        if (!meshGenerated_ || !octreeMesh_)
        {
            std::cerr << "ERROR: EarthMaterial::draw() - Failed to build octree!" << "\n";
            std::cerr << "  Octree build completed but octree is null or invalid." << "\n";
            std::exit(1);
        }
    }

    // Use shader-based rendering (MANDATORY - no fallback)
    {
        // Get Vulkan command buffer for recording draw commands
        extern VulkanContext *g_vulkanContext;
        if (!g_vulkanContext || graphicsPipeline_ == VK_NULL_HANDLE)
        {
            std::cerr << "ERROR: Vulkan not initialized or pipeline not created!" << "\n";
            return;
        }

        VkCommandBuffer cmd = getCurrentCommandBuffer(*g_vulkanContext);
        if (cmd == VK_NULL_HANDLE)
        {
            std::cerr << "ERROR: No Vulkan command buffer available for drawing!" << "\n";
            return;
        }

        // Compute MVP matrix
        // Model matrix: identity (vertices are already in world space)
        glm::mat4 modelMatrix = glm::mat4(1.0f);

        // Use view and projection matrices set by main loop (via setViewProjectionMatrices)
        // If not set, fall back to computing from camera position (for backwards compatibility)
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;

        if (g_matricesSet)
        {
            // Use matrices set by main loop (already converted to Vulkan format)
            viewMatrix = g_viewMatrix;
            projectionMatrix = g_projectionMatrix;
        }
        else
        {
            // Fallback: compute from camera position (should not happen in normal operation)
            glm::vec3 cameraTarget = position; // Look at planet center
            glm::vec3 cameraUp = glm::vec3(0, 1, 0);
            viewMatrix = glm::lookAt(cameraPos, cameraTarget, cameraUp);

            // Compute projection matrix with Vulkan Y-axis flip
            float aspect = static_cast<float>(g_screenWidth) / static_cast<float>(g_screenHeight);
            float fov = g_cameraFovRadians;
            float nearPlane = 0.1f;
            float farPlane = 100000.0f;
            glm::mat4 glProjection = glm::perspective(fov, aspect, nearPlane, farPlane);
            // Flip Y-axis for Vulkan
            glm::mat4 vulkanFlip = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
            projectionMatrix = vulkanFlip * glProjection;
        }

        glm::mat4 mvp = projectionMatrix * viewMatrix * modelMatrix;

        // Use the sunDirection passed as parameter
        glm::vec3 lightDir = sunDirection;

        // Update vertex shader uniform buffer (binding 2)
        // Structure matches earth-vertex.glsl Uniforms block
        struct VertexUniforms
        {
            float uPlanetRadius;
            int uFlatCircleMode;
            glm::vec3 uSphereCenter;
            float uSphereRadius;
            glm::vec3 uCameraPos;
            glm::vec3 uCameraDir;
            float uCameraFOV;
            glm::vec3 uPoleDir;
            glm::vec3 uPrimeMeridianDir;
            int uUseDisplacement;
            float uDisplacementScale;
            glm::mat4 uMVP;
        } vertexUniforms;

        vertexUniforms.uPlanetRadius = displayRadius;
        vertexUniforms.uFlatCircleMode = 0;
        vertexUniforms.uSphereCenter = position;
        vertexUniforms.uSphereRadius = displayRadius;
        vertexUniforms.uCameraPos = cameraPos;
        vertexUniforms.uCameraDir = g_cameraDirection;  // Use actual camera direction
        vertexUniforms.uCameraFOV = g_cameraFovRadians; // Use actual camera FOV
        vertexUniforms.uPoleDir = poleDirection;
        vertexUniforms.uPrimeMeridianDir = primeMeridianDirection;
        vertexUniforms.uUseDisplacement = useHeightmap_ ? 1 : 0;
        vertexUniforms.uDisplacementScale = 1.0f;
        vertexUniforms.uMVP = mvp; // Will be updated with actual MVP

        updateUniformBuffer(*g_vulkanContext, vertexUniformBuffer_, &vertexUniforms, sizeof(vertexUniforms));

        // Update fragment shader uniform buffer (binding 16)
        // Structure matches earth-fragment.glsl Uniforms block
        struct FragmentUniforms
        {
            float uBlendFactor;
            float uWindBlendFactor;
            glm::vec2 uWindTextureSize;
            float uIceBlendFactor;
            glm::vec3 uLightDir;
            glm::vec3 uLightColor;
            glm::vec3 uMoonDir;
            glm::vec3 uMoonColor;
            glm::vec3 uAmbientColor;
            glm::vec3 uPoleDir;
            glm::vec3 uPrimeMeridianDir;
            glm::vec3 uCameraPos;
            glm::vec3 uCameraDir;
            float uCameraFOV;
            int uUseNormalMap;
            int uUseHeightmap;
            int uUseSpecular;
            float uTime;
            int uFlatCircleMode;
            glm::vec3 uSphereCenter;
            float uSphereRadius;
            glm::vec3 uBillboardCenter;
            int uShowWireframe;
        } fragmentUniforms;

        // Calculate month position for texture blending
        double daysSinceJ2000 = julianDate - JD_J2000;
        double yearFraction = std::fmod(daysSinceJ2000, DAYS_PER_TROPICAL_YEAR) / DAYS_PER_TROPICAL_YEAR;
        if (yearFraction < 0)
            yearFraction += 1.0;
        double monthPos = yearFraction * static_cast<double>(MONTHS_PER_YEAR);
        float blendFactor = static_cast<float>(monthPos - static_cast<int>(monthPos));

        // Calculate ice mask month indices (used for both blend factor and texture selection)
        int iceIdx1Calc = static_cast<int>(std::floor(monthPos));
        int iceIdx2Calc = (iceIdx1Calc + 1) % MONTHS_PER_YEAR;
        if (iceIdx1Calc < 0)
        {
            iceIdx1Calc = (iceIdx1Calc % MONTHS_PER_YEAR + MONTHS_PER_YEAR) % MONTHS_PER_YEAR;
        }
        float iceBlendFactor = static_cast<float>(monthPos - static_cast<int>(std::floor(monthPos)));

        // Store ice indices for later use in texture binding
        int iceIdx1 = iceIdx1Calc;
        int iceIdx2 = iceIdx2Calc;

        fragmentUniforms.uBlendFactor = blendFactor;
        fragmentUniforms.uWindBlendFactor = blendFactor;
        fragmentUniforms.uWindTextureSize = glm::vec2(1024.0f, 512.0f);
        fragmentUniforms.uIceBlendFactor = iceBlendFactor;

        fragmentUniforms.uLightDir = lightDir;
        fragmentUniforms.uLightColor = glm::vec3(1.0f, 1.0f, 1.0f);   // White sunlight
        fragmentUniforms.uAmbientColor = glm::vec3(0.0f, 0.0f, 0.0f); // No ambient - Sun is exclusive light source

        // Moonlight calculation
        glm::vec3 moonDir = glm::normalize(moonDirection);
        float sunMoonDot = glm::dot(lightDir, moonDir);
        float moonPhase = 0.5f - (0.5f * sunMoonDot); // 0 at new moon, 1 at full moon
        float moonIntensity = 0.03f * moonPhase;
        glm::vec3 moonColor = glm::vec3(0.8f, 0.85f, 1.0f) * moonIntensity;
        fragmentUniforms.uMoonDir = moonDir;
        fragmentUniforms.uMoonColor = moonColor;

        fragmentUniforms.uPoleDir = glm::normalize(poleDirection);
        fragmentUniforms.uPrimeMeridianDir = glm::normalize(primeMeridianDirection);
        fragmentUniforms.uCameraPos = cameraPos;
        fragmentUniforms.uCameraDir = glm::vec3(0, 0, 1);  // TODO: Get from camera
        fragmentUniforms.uCameraFOV = glm::radians(60.0f); // TODO: Get from camera
        fragmentUniforms.uUseNormalMap = useNormalMap_ ? 1 : 0;
        fragmentUniforms.uUseHeightmap = useHeightmap_ ? 1 : 0;
        fragmentUniforms.uUseSpecular = useSpecular_ ? 1 : 0;
        fragmentUniforms.uTime = static_cast<float>(std::fmod(julianDate, 1.0)); // Fractional part for animated noise
        fragmentUniforms.uFlatCircleMode = 0;                                    // Default to normal sphere mode
        fragmentUniforms.uSphereCenter = position;
        fragmentUniforms.uSphereRadius = displayRadius;
        fragmentUniforms.uBillboardCenter = position; // TODO: Calculate closest point
        fragmentUniforms.uShowWireframe = 0;          // TODO: Get from settings

        updateUniformBuffer(*g_vulkanContext, fragmentUniformBuffer_, &fragmentUniforms, sizeof(fragmentUniforms));

        // Update descriptor sets with textures
        uint32_t currentFrame = g_vulkanContext->currentFrame;
        VkDescriptorSet descSet = descriptorSets_[currentFrame];

        // Get textures from registry
        extern TextureRegistry *g_textureRegistry;
        if (!g_textureRegistry)
        {
            std::cerr << "ERROR: Texture registry not available!" << "\n";
            return;
        }

        // Update texture bindings in descriptor set
        // Binding 0: uColorTexture (tex1)
        VulkanTexture *tex1Vk = g_textureRegistry->getTexture(tex1);
        if (tex1Vk)
        {
            updateDescriptorSetTexture(*g_vulkanContext, descSet, 0, tex1Vk->imageView, tex1Vk->sampler);
        }

        // Binding 1: uColorTexture2 (tex2)
        VulkanTexture *tex2Vk = g_textureRegistry->getTexture(tex2);
        if (tex2Vk)
        {
            updateDescriptorSetTexture(*g_vulkanContext, descSet, 1, tex2Vk->imageView, tex2Vk->sampler);
        }

        // Binding 2: uNormalMap
        if (useNormalMap_ && elevationLoaded_ && normalMapTexture_ != 0)
        {
            VulkanTexture *normalVk = g_textureRegistry->getTexture(normalMapTexture_);
            if (normalVk)
            {
                updateDescriptorSetTexture(*g_vulkanContext, descSet, 2, normalVk->imageView, normalVk->sampler);
            }
        }

        // Binding 3: uHeightmap (also used by vertex shader at binding 0 - conflict!)
        // TODO: Fix shader binding conflict - vertex shader uses binding 0 for heightmap
        // but fragment shader uses binding 3. For now, update both.
        if (useHeightmap_ && elevationLoaded_ && heightmapTexture_ != 0)
        {
            VulkanTexture *heightVk = g_textureRegistry->getTexture(heightmapTexture_);
            if (heightVk)
            {
                updateDescriptorSetTexture(*g_vulkanContext,
                                           descSet,
                                           0,
                                           heightVk->imageView,
                                           heightVk->sampler); // Vertex shader
                updateDescriptorSetTexture(*g_vulkanContext,
                                           descSet,
                                           3,
                                           heightVk->imageView,
                                           heightVk->sampler); // Fragment shader
            }
        }

        // Binding 10: uLandmassMask (also used by vertex shader at binding 1 - conflict!)
        if (landmassMaskLoaded_ && landmassMaskTexture_ != 0)
        {
            VulkanTexture *landmassVk = g_textureRegistry->getTexture(landmassMaskTexture_);
            if (landmassVk)
            {
                updateDescriptorSetTexture(*g_vulkanContext,
                                           descSet,
                                           1,
                                           landmassVk->imageView,
                                           landmassVk->sampler); // Vertex shader
                updateDescriptorSetTexture(*g_vulkanContext,
                                           descSet,
                                           10,
                                           landmassVk->imageView,
                                           landmassVk->sampler); // Fragment shader
            }
        }

        // Binding 4: uNightlights
        if (nightlightsLoaded_ && nightlightsTexture_ != 0)
        {
            VulkanTexture *nightlightsVk = g_textureRegistry->getTexture(nightlightsTexture_);
            if (nightlightsVk)
            {
                updateDescriptorSetTexture(*g_vulkanContext,
                                           descSet,
                                           4,
                                           nightlightsVk->imageView,
                                           nightlightsVk->sampler);
            }
        }

        // Binding 5: uMicroNoise
        if (noiseTexturesGenerated_ && microNoiseTexture_ != 0)
        {
            VulkanTexture *microNoiseVk = g_textureRegistry->getTexture(microNoiseTexture_);
            if (microNoiseVk)
            {
                updateDescriptorSetTexture(*g_vulkanContext,
                                           descSet,
                                           5,
                                           microNoiseVk->imageView,
                                           microNoiseVk->sampler);
            }
        }

        // Binding 6: uHourlyNoise
        if (noiseTexturesGenerated_ && hourlyNoiseTexture_ != 0)
        {
            VulkanTexture *hourlyNoiseVk = g_textureRegistry->getTexture(hourlyNoiseTexture_);
            if (hourlyNoiseVk)
            {
                updateDescriptorSetTexture(*g_vulkanContext,
                                           descSet,
                                           6,
                                           hourlyNoiseVk->imageView,
                                           hourlyNoiseVk->sampler);
            }
        }

        // Binding 7: uSpecular
        if (useSpecular_ && specularLoaded_ && specularTexture_ != 0)
        {
            VulkanTexture *specularVk = g_textureRegistry->getTexture(specularTexture_);
            if (specularVk)
            {
                updateDescriptorSetTexture(*g_vulkanContext, descSet, 7, specularVk->imageView, specularVk->sampler);
            }
        }

        // Binding 8: uIceMask (current month) - iceIdx1 and iceIdx2 calculated above
        if (iceMasksLoaded_[iceIdx1] && iceMaskTextures_[iceIdx1] != 0)
        {
            VulkanTexture *iceMask1Vk = g_textureRegistry->getTexture(iceMaskTextures_[iceIdx1]);
            if (iceMask1Vk)
            {
                updateDescriptorSetTexture(*g_vulkanContext, descSet, 8, iceMask1Vk->imageView, iceMask1Vk->sampler);
            }
        }

        // Binding 9: uIceMask2 (next month)
        if (iceMasksLoaded_[iceIdx2] && iceMaskTextures_[iceIdx2] != 0)
        {
            VulkanTexture *iceMask2Vk = g_textureRegistry->getTexture(iceMaskTextures_[iceIdx2]);
            if (iceMask2Vk)
            {
                updateDescriptorSetTexture(*g_vulkanContext, descSet, 9, iceMask2Vk->imageView, iceMask2Vk->sampler);
            }
        }

        // Binding 11: uBathymetryDepth
        if (bathymetryLoaded_ && bathymetryDepthTexture_ != 0)
        {
            VulkanTexture *bathymetryDepthVk = g_textureRegistry->getTexture(bathymetryDepthTexture_);
            if (bathymetryDepthVk)
            {
                updateDescriptorSetTexture(*g_vulkanContext,
                                           descSet,
                                           11,
                                           bathymetryDepthVk->imageView,
                                           bathymetryDepthVk->sampler);
            }
        }

        // Binding 12: uBathymetryNormal
        if (bathymetryLoaded_ && bathymetryNormalTexture_ != 0)
        {
            VulkanTexture *bathymetryNormalVk = g_textureRegistry->getTexture(bathymetryNormalTexture_);
            if (bathymetryNormalVk)
            {
                updateDescriptorSetTexture(*g_vulkanContext,
                                           descSet,
                                           12,
                                           bathymetryNormalVk->imageView,
                                           bathymetryNormalVk->sampler);
            }
        }

        // Binding 13: uCombinedNormal
        if (combinedNormalLoaded_ && combinedNormalTexture_ != 0)
        {
            VulkanTexture *combinedNormalVk = g_textureRegistry->getTexture(combinedNormalTexture_);
            if (combinedNormalVk)
            {
                updateDescriptorSetTexture(*g_vulkanContext,
                                           descSet,
                                           13,
                                           combinedNormalVk->imageView,
                                           combinedNormalVk->sampler);
            }
        }

        // Calculate wind texture month indices (reuse monthPos from above)
        int currentMonthIdx = static_cast<int>(monthPos) % 12;
        int nextMonthIdx = (currentMonthIdx + 1) % 12;

        // Binding 14: uWindTexture1 (current month)
        if (windTexturesLoaded_[currentMonthIdx] && windTextures_[currentMonthIdx] != 0)
        {
            VulkanTexture *wind1Vk = g_textureRegistry->getTexture(windTextures_[currentMonthIdx]);
            if (wind1Vk)
            {
                updateDescriptorSetTexture(*g_vulkanContext, descSet, 14, wind1Vk->imageView, wind1Vk->sampler);
            }
        }

        // Binding 15: uWindTexture2 (next month)
        if (windTexturesLoaded_[nextMonthIdx] && windTextures_[nextMonthIdx] != 0)
        {
            VulkanTexture *wind2Vk = g_textureRegistry->getTexture(windTextures_[nextMonthIdx]);
            if (wind2Vk)
            {
                updateDescriptorSetTexture(*g_vulkanContext, descSet, 15, wind2Vk->imageView, wind2Vk->sampler);
            }
        }

        // All textures and uniforms are now set via Vulkan descriptor sets and uniform buffers
        // The actual drawing happens in drawOctreeMesh() which records Vulkan commands

        // Render using voxels directly (no mesh generation)
        if (!meshGenerated_ || !octreeMesh_)
        {
            std::cerr << "ERROR: EarthMaterial::draw() - Octree not built!" << "\n";
            std::cerr << "  meshGenerated_: " << meshGenerated_ << "\n";
            std::cerr << "  octreeMesh_: " << (octreeMesh_ ? "valid" : "null") << "\n";
            std::cerr << "  Voxel-based rendering is required. Cannot render without octree." << "\n";
            std::exit(1);
        }

        // Update octree with proximity-based subdivision (dynamic LOD)
        // Subdivide nodes near camera to increase resolution as we approach
        // Multiple LOD levels with different radius thresholds (much larger for low base resolution):
        // - Base: lowest resolution (outside all radii) - uses root octree nodes only
        // - Within 50 radii: first level of subdivision (very low detail)
        // - Within 20 radii: second level (low detail)
        // - Within 10 radii: third level (medium detail)
        // - Within 5 radii: fourth level (medium-high detail)
        // - Within 2 radii: maximum detail (high detail, only when very close)
        // Process LOD levels in chunks (one per frame) to avoid frame drops
        const float lodRadii[] = {5.0f, 3.0f, 2.0f, 1.5f, 1.25f};
        const int numLodLevels = sizeof(lodRadii) / sizeof(lodRadii[0]);

        // Calculate distance from camera to planet center
        float distanceToPlanet = glm::length(cameraPos - position);
        float distanceInRadii = distanceToPlanet / displayRadius;

        // Reset chunked processing if camera moved significantly
        const float CAMERA_MOVEMENT_THRESHOLD = displayRadius * 0.1f; // 10% of radius
        if (std::abs(distanceToPlanet - lastCameraDistance_) > CAMERA_MOVEMENT_THRESHOLD)
        {
            currentLodLevel_ = -1; // Reset to start processing from beginning
            lastCameraDistance_ = distanceToPlanet;
        }

        // Only subdivide if we're reasonably close (within 5 radii)
        // This prevents unnecessary subdivision when viewing from far away
        if (distanceInRadii < 5.0f)
        {
            // Process one LOD level per frame (chunked processing)
            // Find which LOD levels need processing
            int firstLodNeeded = -1;
            for (int i = 0; i < numLodLevels; ++i)
            {
                if (distanceInRadii < lodRadii[i])
                {
                    firstLodNeeded = i;
                    break;
                }
            }

            // Process next LOD level if needed
            if (firstLodNeeded >= 0)
            {
                // Start from current level or first needed level
                int lodToProcess = (currentLodLevel_ >= 0) ? currentLodLevel_ : firstLodNeeded;

                if (lodToProcess < numLodLevels && distanceInRadii < lodRadii[lodToProcess])
                {
                    float maxSubdivisionDistance = displayRadius * lodRadii[lodToProcess];
                    updateOctreeMeshForProximity(cameraPos, position, displayRadius, maxSubdivisionDistance);

                    // Move to next LOD level for next frame
                    currentLodLevel_ = lodToProcess + 1;
                }
                else
                {
                    // All needed LOD levels processed, reset
                    currentLodLevel_ = -1;
                }
            }
            else
            {
                // Camera too far, reset processing
                currentLodLevel_ = -1;
            }
        }
        else
        {
            // Camera too far, reset processing
            currentLodLevel_ = -1;
        }

        // Use adaptive extraction distance based on camera distance
        // When far away, use large extraction distance (low detail)
        // When close, use small extraction distance (high detail)
        float extractionRadius;
        if (distanceInRadii >= 50.0f)
        {
            // Very far: use base octree only (no subdivision extraction)
            extractionRadius = displayRadius * 1000.0f; // Effectively extract everything at base level
        }
        else if (distanceInRadii >= 20.0f)
        {
            // Far: low detail
            extractionRadius = displayRadius * 50.0f;
        }
        else if (distanceInRadii >= 10.0f)
        {
            // Medium distance: medium-low detail
            extractionRadius = displayRadius * 20.0f;
        }
        else if (distanceInRadii >= 5.0f)
        {
            // Medium-close: medium detail
            extractionRadius = displayRadius * 10.0f;
        }
        else if (distanceInRadii >= 2.0f)
        {
            // Close: medium-high detail
            extractionRadius = displayRadius * 5.0f;
        }
        else
        {
            // Very close: high detail
            extractionRadius = displayRadius * 2.0f;
        }

        float maxSubdivisionDistance = extractionRadius;

        // Extract mesh from octree for rendering
        // This generates meshVertices_ and meshIndices_ from the voxel octree
        if (octreeMesh_)
        {
            meshVertices_.clear();
            meshIndices_.clear();
            octreeMesh_->extractSurfaceMesh(cameraPos, maxSubdivisionDistance, meshVertices_, meshIndices_);

            static bool debugPrinted = false;
            if (!debugPrinted && !meshVertices_.empty())
            {
                std::cout << "DEBUG: Extracted mesh from octree - " << meshVertices_.size() << " vertices, "
                          << meshIndices_.size() << " indices" << "\n";
                debugPrinted = true;
            }
        }

        // Render the octree mesh using Vulkan
        // TODO: Implement ray marching shader that queries octree voxels directly
        drawOctreeMesh(position);
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

        // Render fan geometry directly in world space
        if (uniformFlatCircleMode < 0)
        {
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

                // TODO: Migrate wireframe rendering to Vulkan
                // Render triangle: center, edge1, edge2
                // glTexCoord2f(0.5f, 0.5f); // REMOVED - migrate to Vulkan vertex buffer
                // glNormal3f(centerNormal.x, centerNormal.y, centerNormal.z); // REMOVED - migrate to Vulkan vertex buffer
                // glVertex3f(closestPointOnSphere.x - position.x,
                //            closestPointOnSphere.y - position.y,
                //            closestPointOnSphere.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

                // glTexCoord2f(0.5f, 0.5f); // REMOVED - migrate to Vulkan vertex buffer
                // glNormal3f(normal1.x, normal1.y, normal1.z); // REMOVED - migrate to Vulkan vertex buffer
                // glVertex3f(flatPoint1.x - position.x, flatPoint1.y - position.y, flatPoint1.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

                // glTexCoord2f(0.5f, 0.5f); // REMOVED - migrate to Vulkan vertex buffer
                // glNormal3f(normal2.x, normal2.y, normal2.z); // REMOVED - migrate to Vulkan vertex buffer
                // glVertex3f(flatPoint2.x - position.x, flatPoint2.y - position.y, flatPoint2.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

                CountTriangles(GL_TRIANGLES, 1);
            }

            // glEnd(); // REMOVED - migrate to Vulkan
            // glPopMatrix(); // REMOVED - migrate to Vulkan (matrices in uniform buffers)

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

            // TODO: Migrate billboard rendering to Vulkan
            // Center point
            // glTexCoord2f(0.5f, 0.5f); // REMOVED - migrate to Vulkan vertex buffer
            // glNormal3f(0.0f, 0.0f, 1.0f); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex3f(0.0f, 0.0f, 0.0f); // REMOVED - migrate to Vulkan vertex buffer

            // Edge point 1
            // glTexCoord2f(0.5f, 0.5f); // REMOVED - migrate to Vulkan vertex buffer
            // glNormal3f(0.0f, 0.0f, 1.0f); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex3f(x1, y1, 0.0f); // REMOVED - migrate to Vulkan vertex buffer

            // Edge point 2
            // glTexCoord2f(0.5f, 0.5f); // REMOVED - migrate to Vulkan vertex buffer
            // glNormal3f(0.0f, 0.0f, 1.0f); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex3f(x2, y2, 0.0f); // REMOVED - migrate to Vulkan vertex buffer

            CountTriangles(GL_TRIANGLES, 1);
        }

        // glEnd(); // REMOVED - migrate to Vulkan

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

        // TODO: Migrate triangle rendering to Vulkan
        // glTexCoord2f(uv1.x, uv1.y); // REMOVED - migrate to Vulkan vertex buffer
        // glNormal3f(n1.x, n1.y, n1.z); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex3f(v1.x - position.x, v1.y - position.y, v1.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

        // glTexCoord2f(uv2.x, uv2.y); // REMOVED - migrate to Vulkan vertex buffer
        // glNormal3f(n2.x, n2.y, n2.z); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex3f(v2.x - position.x, v2.y - position.y, v2.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

        // glTexCoord2f(uv3.x, uv3.y); // REMOVED - migrate to Vulkan vertex buffer
        // glNormal3f(n3.x, n3.y, n3.z); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex3f(v3.x - position.x, v3.y - position.y, v3.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

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
        // glEnd(); // REMOVED - migrate to Vulkan
    }

    glPopMatrix();
}

// Draw octree mesh with shader (vertices in local space, transformed to world space in shader)
void EarthMaterial::drawOctreeMesh(const glm::vec3 &position)
{
    // Check if we have mesh data
    if (meshVertices_.empty() || meshIndices_.empty())
    {
        static bool debugPrinted = false;
        if (!debugPrinted)
        {
            std::cerr << "WARNING: drawOctreeMesh() - No mesh data to render!" << "\n";
            std::cerr << "  meshVertices_.size(): " << meshVertices_.size() << "\n";
            std::cerr << "  meshIndices_.size(): " << meshIndices_.size() << "\n";
            std::cerr << "  The octree may not be generating mesh data (using voxels directly)." << "\n";
            debugPrinted = true;
        }
        return; // No mesh to render
    }

    // Get Vulkan command buffer
    extern VulkanContext *g_vulkanContext;
    if (!g_vulkanContext)
    {
        std::cerr << "ERROR: Vulkan context not available!" << "\n";
        return;
    }

    VkCommandBuffer cmd = getCurrentCommandBuffer(*g_vulkanContext);
    if (cmd == VK_NULL_HANDLE)
    {
        std::cerr << "WARNING: No Vulkan command buffer available for drawing octree mesh" << "\n";
        return;
    }

    // Check if we have valid data
    if (meshIndices_.empty() || graphicsPipeline_ == VK_NULL_HANDLE)
    {
        return;
    }

    // Create or update Vulkan buffers
    size_t vertexBufferSize = meshVertices_.size() * sizeof(EarthVoxelOctree::MeshVertex);
    size_t indexBufferSize = meshIndices_.size() * sizeof(unsigned int);

    if (!buffersCreated_ || vertexBuffer_.size < vertexBufferSize || indexBuffer_.size < indexBufferSize)
    {
        // Destroy old buffers if they exist
        if (vertexBuffer_.buffer != VK_NULL_HANDLE)
        {
            destroyBuffer(*g_vulkanContext, vertexBuffer_);
        }
        if (indexBuffer_.buffer != VK_NULL_HANDLE)
        {
            destroyBuffer(*g_vulkanContext, indexBuffer_);
        }

        // Create new buffers with enough size (round up for dynamic updates)
        size_t vertexSize = std::max(vertexBufferSize, size_t(1024 * 1024)); // At least 1MB
        size_t indexSize = std::max(indexBufferSize, size_t(512 * 1024));    // At least 512KB

        // Create device-local buffers (GPU-only, faster access)
        // Use staging buffer for initial data upload
        vertexBuffer_ = createBuffer(*g_vulkanContext,
                                     vertexSize,
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                     meshVertices_.data());

        indexBuffer_ = createBuffer(*g_vulkanContext,
                                    indexSize,
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    meshIndices_.data());

        buffersCreated_ = true;
    }
    else
    {
        // Update buffer data (mesh changes every frame)
        // Use staging buffer to update device-local buffers
        VulkanBuffer stagingVertexBuffer =
            createBuffer(*g_vulkanContext,
                         vertexBufferSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         meshVertices_.data());

        VulkanBuffer stagingIndexBuffer =
            createBuffer(*g_vulkanContext,
                         indexBufferSize,
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         meshIndices_.data());

        // Copy from staging buffers to device-local buffers
        copyBuffer(*g_vulkanContext, stagingVertexBuffer.buffer, vertexBuffer_.buffer, vertexBufferSize);
        copyBuffer(*g_vulkanContext, stagingIndexBuffer.buffer, indexBuffer_.buffer, indexBufferSize);

        // Cleanup staging buffers
        destroyBuffer(*g_vulkanContext, stagingVertexBuffer);
        destroyBuffer(*g_vulkanContext, stagingIndexBuffer);
    }

    // Bind pipeline and descriptor sets
    uint32_t currentFrame = g_vulkanContext->currentFrame;
    std::vector<VkDescriptorSet> sets = {descriptorSets_[currentFrame]};
    bindPipelineAndDescriptors(cmd, graphicsPipeline_, pipelineLayout_, sets);

    // Validate buffers before binding
    if (vertexBuffer_.buffer == VK_NULL_HANDLE || indexBuffer_.buffer == VK_NULL_HANDLE)
    {
        std::cerr << "ERROR: drawOctreeMesh() - Buffers not created!" << "\n";
        return;
    }

    // Bind vertex and index buffers
    VkBuffer vertexBuffers[] = {vertexBuffer_.buffer};
    VkDeviceSize offsets[] = {0};
    recordBindVertexBuffers(cmd,
                            0,
                            std::vector<VkBuffer>(vertexBuffers, vertexBuffers + 1),
                            std::vector<VkDeviceSize>(offsets, offsets + 1));
    recordBindIndexBuffer(cmd, indexBuffer_.buffer, 0, VK_INDEX_TYPE_UINT32);

    // Record draw command
    recordDrawIndexed(cmd, static_cast<uint32_t>(meshIndices_.size()), 1, 0, 0, 0);
}

// Debug: Render voxel wireframes
void EarthMaterial::drawVoxelWireframes(const glm::vec3 &position)
{
    if (voxelWireframeEdges_.empty())
    {
        return; // No wireframe data
    }

    // TODO: Migrate wireframe rendering to Vulkan
    // Save OpenGL state
    // glPushAttrib(GL_ENABLE_BIT | GL_LINE_BIT | GL_CURRENT_BIT); // REMOVED - migrate to Vulkan
    // glPushMatrix(); // REMOVED - migrate to Vulkan

    // Don't translate - vertices are in local space
    // The shader is already bound and uSphereCenter is already set to position

    // Set up wireframe rendering
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    // TODO: Migrate wireframe rendering to Vulkan
    // glColor3f(1.0f, 1.0f, 0.0f); // REMOVED - migrate to Vulkan uniform buffer
    // glLineWidth(1.0f); // REMOVED - migrate to Vulkan pipeline state

    // Enable depth test for proper occlusion
    // glEnable(GL_DEPTH_TEST); // REMOVED - migrate to Vulkan pipeline depth state
    // glDepthFunc(GL_LEQUAL); // REMOVED - migrate to Vulkan pipeline depth state

    // Disable culling so we see all edges
    // glDisable(GL_CULL_FACE); // REMOVED - migrate to Vulkan pipeline cull state

    // Render wireframe lines
    // glBegin(GL_LINES); // REMOVED - migrate to Vulkan
    for (size_t i = 0; i < voxelWireframeEdges_.size(); i += 2)
    {
        if (i + 1 < voxelWireframeEdges_.size())
        {
            const glm::vec3 &v0 = voxelWireframeEdges_[i];
            const glm::vec3 &v1 = voxelWireframeEdges_[i + 1];

            // Transform to world space (add planet position)
            glm::vec3 w0 = v0 + position;
            glm::vec3 w1 = v1 + position;

            // glVertex3f(w0.x, w0.y, w0.z); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex3f(w1.x, w1.y, w1.z); // REMOVED - migrate to Vulkan vertex buffer
        }
    }
    // glEnd(); // REMOVED - migrate to Vulkan

    // Restore OpenGL state
    // TODO: Migrate to Vulkan
    // glPopMatrix(); // REMOVED - migrate to Vulkan
    // glPopAttrib(); // REMOVED - migrate to Vulkan
}
