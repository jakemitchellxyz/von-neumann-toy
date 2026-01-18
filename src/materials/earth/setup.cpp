// ============================================================================
// Earth Material Implementation
// ============================================================================
// Uses NASA Blue Marble Next Generation imagery for monthly Earth textures.
// Combines 8 source tiles per month into equirectangular images at startup.
// Supports multiple resolution presets stored in separate folders.
//
// Also processes ETOPO elevation data to generate heightmap and normal map
// textures for bump/displacement mapping.

#include "../../concerns/helpers/gl.h"
#include "../../concerns/helpers/vulkan.h"
#include "earth-material.h"

#include <cstdlib> // For std::exit

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

// stb_image for loading
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// stb_image_write for saving combined images
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// libtiff for loading GeoTIFF elevation data
#include <tiffio.h>

// GDAL for warping/mosaicing
#include <cpl_conv.h>
#include <gdal_priv.h>
#include <gdal_utils.h>


// Global instance
// Cannot be const because it's initialized and modified at runtime
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
EarthMaterial g_earthMaterial;


// Source tile dimensions
static constexpr int SOURCE_TILE_SIZE = 21600;


// ============================================================================
// Constructor / Destructor
// ============================================================================

EarthMaterial::EarthMaterial()
    : initialized_(false), fallbackTexture_(0), heightmapTexture_(0), normalMapTexture_(0), elevationLoaded_(false),
      specularTexture_(0), specularLoaded_(false), iceMaskTextures_{}, iceMasksLoaded_{}, landmassMaskTexture_(0),
      landmassMaskLoaded_(false), bathymetryDepthTexture_(0), bathymetryNormalTexture_(0), bathymetryLoaded_(false),
      combinedNormalTexture_(0), combinedNormalLoaded_(false), nightlightsTexture_(0), nightlightsLoaded_(false),
      windTextures_{}, windTexturesLoaded_{}, shaderProgram_(0), shaderAvailable_(false), uniformModelMatrix_(-1),
      uniformViewMatrix_(-1), uniformProjectionMatrix_(-1), uniformColorTexture_(-1), uniformColorTexture2_(-1),
      uniformBlendFactor_(-1), uniformNormalMap_(-1), uniformHeightmap_(-1), uniformLightDir_(-1),
      uniformLightColor_(-1), uniformMoonDir_(-1), uniformMoonColor_(-1), uniformAmbientColor_(-1), uniformPoleDir_(-1),
      uniformUseNormalMap_(-1), uniformUseHeightmap_(-1), uniformUseDisplacement_(-1), uniformUseSpecular_(-1),
      uniformNightlights_(-1), uniformTime_(-1), uniformMicroNoise_(-1), uniformHourlyNoise_(-1), uniformSpecular_(-1),
      uniformIceMask_(-1), uniformIceMask2_(-1), uniformIceBlendFactor_(-1), uniformLandmassMask_(-1),
      uniformCameraPos_(-1), uniformCameraDir_(-1), uniformCameraFOV_(-1), uniformPrimeMeridianDir_(-1),
      uniformBathymetryDepth_(-1), uniformBathymetryNormal_(-1), uniformCombinedNormal_(-1), uniformPlanetRadius_(-1),
      uniformFlatCircleMode_(-1), uniformSphereCenter_(-1), uniformSphereRadius_(-1), uniformBillboardCenter_(-1),
      uniformDisplacementScale_(-1), uniformShowWireframe_(-1), uniformWindTexture1_(-1), uniformWindTexture2_(-1),
      uniformWindBlendFactor_(-1), uniformWindTextureSize_(-1), microNoiseTexture_(0), hourlyNoiseTexture_(0),
      noiseTexturesGenerated_(false), monthlyTextures_{}, textureLoaded_{}, meshGenerated_(false), meshVAO_(0),
      meshVBO_(0), meshEBO_(0), meshVAOCreated_(false)
{
    monthlyTextures_.fill(0);
    textureLoaded_.fill(false);
    windTextures_.fill(0);
    windTexturesLoaded_.fill(false);
}

EarthMaterial::~EarthMaterial()
{
    cleanup();
}

// ============================================================================
// Shader Initialization
// ============================================================================

// Stub implementation when earth-surface.cpp is not linked
// This allows setup.cpp to be used for STB implementations without requiring earth-surface
// The real implementation is in earth-surface.cpp and will override this stub when linked
bool EarthMaterial::initializeSurfaceShader()
{
    // Stub: surface shader initialization not available when earth-surface.cpp is not linked
    // Return false to indicate shader is not initialized
    // This is a weak implementation - earth-surface.cpp provides the real one when enabled
    return false;
}

bool EarthMaterial::initializeShaders()
{
    // Early return if shader is already initialized
    if (shaderAvailable_)
    {
        return true;
    }

    // Load GL extensions (required for surface shader)
    if (!loadGLExtensions())
    {
        std::cerr << "ERROR: EarthMaterial::initializeShaders() - OpenGL "
                     "shader extensions not available"
                  << '\n';
        std::cerr << "  Shader-based rendering is required. Cannot continue." << '\n';
        std::exit(1);
    }

    // Initialize surface shader (earth-vertex.glsl + earth-fragment.glsl)
    // Only attempt if earth-surface.cpp is linked (provides real implementation)
    if (!shaderAvailable_ && !initializeSurfaceShader())
    {
        return false;
    }

    return true;
}

// ============================================================================
// Cleanup
// ============================================================================

void EarthMaterial::cleanup()
{
    for (int i = 0; i < MONTHS_PER_YEAR; i++)
    {
        if (monthlyTextures_[i] != 0)
        {
            glDeleteTextures(1, &monthlyTextures_[i]);
            monthlyTextures_[i] = 0;
            textureLoaded_[i] = false;
        }
    }

    if (fallbackTexture_ != 0)
    {
        // glDeleteTextures(1, &fallbackTexture_); // REMOVED - migrate to Vulkan (textures managed via texture registry)
        fallbackTexture_ = 0;
    }

    if (heightmapTexture_ != 0)
    {
        // glDeleteTextures(1, &heightmapTexture_); // REMOVED - migrate to Vulkan (textures managed via texture registry)
        heightmapTexture_ = 0;
    }

    if (normalMapTexture_ != 0)
    {
        // glDeleteTextures(1, &normalMapTexture_); // REMOVED - migrate to Vulkan (textures managed via texture registry)
        normalMapTexture_ = 0;
    }

    if (specularTexture_ != 0)
    {
        // glDeleteTextures(1, &specularTexture_); // REMOVED - migrate to Vulkan (textures managed via texture registry)
        specularTexture_ = 0;
        specularLoaded_ = false;
    }


    // Delete ice mask textures
    for (int i = 0; i < MONTHS_PER_YEAR; i++)
    {
        if (iceMaskTextures_[i] != 0)
        {
            glDeleteTextures(1, &iceMaskTextures_[i]);
            iceMaskTextures_[i] = 0;
            iceMasksLoaded_[i] = false;
        }
    }

    // Delete landmass mask texture
    if (landmassMaskTexture_ != 0)
    {
        // glDeleteTextures(1, &landmassMaskTexture_); // REMOVED - migrate to Vulkan (textures managed via texture registry)
        landmassMaskTexture_ = 0;
        landmassMaskLoaded_ = false;
    }

    // Delete bathymetry textures
    if (bathymetryDepthTexture_ != 0)
    {
        // glDeleteTextures(1, &bathymetryDepthTexture_); // REMOVED - migrate to Vulkan (textures managed via texture registry)
        bathymetryDepthTexture_ = 0;
    }
    if (bathymetryNormalTexture_ != 0)
    {
        // glDeleteTextures(1, &bathymetryNormalTexture_); // REMOVED - migrate to Vulkan (textures managed via texture registry)
        bathymetryNormalTexture_ = 0;
    }
    bathymetryLoaded_ = false;

    if (combinedNormalTexture_ != 0)
    {
        glDeleteTextures(1, &combinedNormalTexture_);
        combinedNormalTexture_ = 0;
    }
    combinedNormalLoaded_ = false;

    if (nightlightsTexture_ != 0)
    {
        // glDeleteTextures(1, &nightlightsTexture_); // REMOVED - migrate to Vulkan (textures managed via texture registry)
        nightlightsTexture_ = 0;
        nightlightsLoaded_ = false;
    }

    // Delete wind textures (already handled in the loop above, but keeping for clarity)
    // The wind textures are deleted in the loop that deletes all textures

    // Cleanup noise textures
    if (microNoiseTexture_ != 0)
    {
        // glDeleteTextures(1, &microNoiseTexture_); // REMOVED - migrate to Vulkan (textures managed via texture registry)
        microNoiseTexture_ = 0;
    }

    if (hourlyNoiseTexture_ != 0)
    {
        // glDeleteTextures(1, &hourlyNoiseTexture_); // REMOVED - migrate to Vulkan (textures managed via texture registry)
        hourlyNoiseTexture_ = 0;
    }
    noiseTexturesGenerated_ = false;

    // Cleanup shader programs
    if (shaderProgram_ != 0 && glDeleteProgram != nullptr)
    {
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
    }
    shaderAvailable_ = false;

    // Cleanup Vulkan buffers
    extern VulkanContext *g_vulkanContext;
    if (g_vulkanContext)
    {
        std::cerr << "Cleaning up EarthMaterial Vulkan buffers..." << std::endl;
        if (vertexBuffer_.buffer != VK_NULL_HANDLE || vertexBuffer_.allocation != VK_NULL_HANDLE)
        {
            destroyBuffer(*g_vulkanContext, vertexBuffer_);
        }
        if (indexBuffer_.buffer != VK_NULL_HANDLE || indexBuffer_.allocation != VK_NULL_HANDLE)
        {
            destroyBuffer(*g_vulkanContext, indexBuffer_);
        }
        if (vertexUniformBuffer_.buffer != VK_NULL_HANDLE || vertexUniformBuffer_.allocation != VK_NULL_HANDLE)
        {
            destroyBuffer(*g_vulkanContext, vertexUniformBuffer_);
        }
        if (fragmentUniformBuffer_.buffer != VK_NULL_HANDLE || fragmentUniformBuffer_.allocation != VK_NULL_HANDLE)
        {
            destroyBuffer(*g_vulkanContext, fragmentUniformBuffer_);
        }
    }

    elevationLoaded_ = false;
    initialized_ = false;
}

GLuint EarthMaterial::compileShader(GLenum type, const char *source)
{
    if (glCreateShader == nullptr)
    {
        std::cerr << "Failed to load OpenGL shader extensions" << '\n';
        return 0;
    }

    GLuint shader = glCreateShader(type);
    if (shader == 0)
    {
        std::cerr << "Failed to create shader" << '\n';
        return 0;
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == 0)
    {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(logLength);
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        std::cerr << "Shader compilation failed:\n" << log.data() << '\n';
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint EarthMaterial::linkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    if (glCreateProgram == nullptr)
    {
        std::cerr << "Failed to load OpenGL shader extensions" << '\n';
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0)
    {
        std::cerr << "Failed to create shader program" << '\n';
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == 0)
    {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(logLength);
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        std::cerr << "Shader linking failed:\n" << log.data() << '\n';
        glDeleteProgram(program);
        return 0;
    }

    return program;
}
