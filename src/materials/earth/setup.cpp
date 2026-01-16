// ============================================================================
// Earth Material Implementation
// ============================================================================
// Uses NASA Blue Marble Next Generation imagery for monthly Earth textures.
// Combines 8 source tiles per month into equirectangular images at startup.
// Supports multiple resolution presets stored in separate folders.
//
// Also processes ETOPO elevation data to generate heightmap and normal map
// textures for bump/displacement mapping.

#include "../helpers/gl.h"
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
      combinedNormalTexture_(0), combinedNormalLoaded_(false),
      nightlightsTexture_(0), nightlightsLoaded_(false),
      windTextures_{}, windTexturesLoaded_{},
      shaderProgram_(0), shaderAvailable_(false), uniformModelMatrix_(-1), uniformViewMatrix_(-1),
      uniformProjectionMatrix_(-1), uniformColorTexture_(-1), uniformColorTexture2_(-1), uniformBlendFactor_(-1),
      uniformNormalMap_(-1), uniformHeightmap_(-1), uniformLightDir_(-1), uniformLightColor_(-1), uniformMoonDir_(-1),
      uniformMoonColor_(-1), uniformAmbientColor_(-1), uniformPoleDir_(-1), uniformUseNormalMap_(-1),
      uniformUseHeightmap_(-1), uniformUseDisplacement_(-1), uniformUseSpecular_(-1), uniformNightlights_(-1), uniformTime_(-1),
      uniformMicroNoise_(-1), uniformHourlyNoise_(-1), uniformSpecular_(-1), uniformIceMask_(-1), uniformIceMask2_(-1),
      uniformIceBlendFactor_(-1), uniformLandmassMask_(-1), uniformCameraPos_(-1), uniformCameraDir_(-1), uniformCameraFOV_(-1), uniformPrimeMeridianDir_(-1), uniformBathymetryDepth_(-1),
      uniformBathymetryNormal_(-1), uniformCombinedNormal_(-1),
      uniformPlanetRadius_(-1), uniformFlatCircleMode_(-1), uniformSphereCenter_(-1), uniformSphereRadius_(-1), uniformBillboardCenter_(-1),
      uniformDisplacementScale_(-1),
      uniformWindTexture1_(-1), uniformWindTexture2_(-1), uniformWindBlendFactor_(-1), uniformWindTextureSize_(-1),
      microNoiseTexture_(0), hourlyNoiseTexture_(0), noiseTexturesGenerated_(false),
      monthlyTextures_{}, textureLoaded_{}
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
        glDeleteTextures(1, &fallbackTexture_);
        fallbackTexture_ = 0;
    }

    if (heightmapTexture_ != 0)
    {
        glDeleteTextures(1, &heightmapTexture_);
        heightmapTexture_ = 0;
    }

    if (normalMapTexture_ != 0)
    {
        glDeleteTextures(1, &normalMapTexture_);
        normalMapTexture_ = 0;
    }

    if (specularTexture_ != 0)
    {
        glDeleteTextures(1, &specularTexture_);
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
        glDeleteTextures(1, &landmassMaskTexture_);
        landmassMaskTexture_ = 0;
        landmassMaskLoaded_ = false;
    }

    // Delete bathymetry textures
    if (bathymetryDepthTexture_ != 0)
    {
        glDeleteTextures(1, &bathymetryDepthTexture_);
        bathymetryDepthTexture_ = 0;
    }
    if (bathymetryNormalTexture_ != 0)
    {
        glDeleteTextures(1, &bathymetryNormalTexture_);
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
        glDeleteTextures(1, &nightlightsTexture_);
        nightlightsTexture_ = 0;
        nightlightsLoaded_ = false;
    }

    // Delete wind textures (already handled in the loop above, but keeping for clarity)
    // The wind textures are deleted in the loop that deletes all textures

    // Cleanup noise textures
    if (microNoiseTexture_ != 0)
    {
        glDeleteTextures(1, &microNoiseTexture_);
        microNoiseTexture_ = 0;
    }

    if (hourlyNoiseTexture_ != 0)
    {
        glDeleteTextures(1, &hourlyNoiseTexture_);
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

    elevationLoaded_ = false;
    initialized_ = false;
}
