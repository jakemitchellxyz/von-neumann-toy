// ============================================================================
// Initialization - Load Combined Textures into OpenGL
// ============================================================================
// All textures are in sinusoidal projection (orange peel layout)

#include "../../../concerns/settings.h"
#include "../../helpers/gl.h"
#include "../../helpers/noise.h"
#include "../../helpers/shader-loader.h"
#include "../earth-material.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib> // For std::exit
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// stb_image for loading textures
// Note: STB_IMAGE_IMPLEMENTATION is defined in setup.cpp, so we just include the header here
#include <stb_image.h>


GLuint EarthMaterial::loadTexture(const std::string &filepath)
{
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true); // OpenGL expects bottom-to-top

    unsigned char *data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);

    if (!data)
    {
        std::cerr << "Failed to load texture: " << filepath << "\n";
        return 0;
    }

    GLenum format = GL_RGB;
    GLenum internalFormat = GL_RGB;
    if (channels == 1)
    {
        format = GL_LUMINANCE;
        internalFormat = GL_LUMINANCE;
    }
    else if (channels == 2)
    {
        // 2-channel RG format (for wind textures: R=u, G=v)
        format = GL_LUMINANCE_ALPHA; // GL_LUMINANCE_ALPHA maps R->LUMINANCE, G->ALPHA in older OpenGL
        internalFormat = GL_LUMINANCE_ALPHA;
    }
    else if (channels == 3)
    {
        format = GL_RGB;
        internalFormat = GL_RGB;
    }
    else if (channels == 4)
    {
        format = GL_RGBA;
        internalFormat = GL_RGBA;
    }

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    return textureId;
}

// Specialized loader for wind textures (2-channel RG format)
// Ensures proper 2-channel texture loading for wind force vectors
GLuint EarthMaterial::loadWindTexture(const std::string &filepath)
{
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true); // OpenGL expects bottom-to-top

    // Force load as 2 channels (RG format: R=u wind, G=v wind)
    unsigned char *data = stbi_load(filepath.c_str(), &width, &height, &channels, 2);

    if (!data)
    {
        std::cerr << "Failed to load wind texture: " << filepath << "\n";
        return 0;
    }

    if (channels != 2)
    {
        std::cerr << "WARNING: Wind texture has " << channels << " channels, expected 2 (RG format)" << "\n";
        std::cerr << "  This may cause incorrect wind data sampling" << "\n";
    }

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    // Try to use GL_RG format (OpenGL 3.0+) for proper 2-channel storage
    // Fall back to GL_LUMINANCE_ALPHA if GL_RG is not available (OpenGL 2.1)
    GLenum internalFormat = GL_LUMINANCE_ALPHA;
    GLenum format = GL_LUMINANCE_ALPHA;

    // Check if GL_RG is available (requires OpenGL 3.0+ or ARB_texture_rg extension)
    // For now, we'll use GL_LUMINANCE_ALPHA and sample with .ra in the shader
    // GL_LUMINANCE_ALPHA stores: first channel -> LUMINANCE (replicated to RGB), second channel -> ALPHA
    // So sampling gives (L, L, L, A), and we use .ra to get (u wind, v wind)
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    std::cout << "  Wind texture loaded: " << width << "x" << height << " (2 channels: RG)" << "\n";

    return textureId;
}

void EarthMaterial::generateNoiseTextures()
{
    if (noiseTexturesGenerated_)
        return;

    std::cout << "Generating noise textures for city light flickering..." << "\n";

    // Micro noise: Fine-grained (2048x1024) - ~20km per pixel
    // Higher resolution for fine detail where cities flicker independently
    const int microWidth = 2048;
    const int microHeight = 1024;

    // Hourly noise: Coarser (512x256) - ~80km per pixel
    // Lower resolution for regional variation
    const int hourlyWidth = 512;
    const int hourlyHeight = 256;

    // Generate micro noise texture
    {
        std::vector<unsigned char> microData(microWidth * microHeight);

        // Scale factor determines noise "grain size"
        // Higher = more peaks across the texture = finer detail
        float scale = 50.0f; // ~40 noise peaks across width

        for (int y = 0; y < microHeight; y++)
        {
            for (int x = 0; x < microWidth; x++)
            {
                // Map to UV coordinates
                float u = static_cast<float>(x) / microWidth;
                float v = static_cast<float>(y) / microHeight;

                // Generate noise (FBM for more natural appearance)
                float noise = perlinFBM(u * scale, v * scale * 0.5f, 4, 0.5f);

                // Map from [-1,1] to [0,255]
                int value = static_cast<int>((noise + 1.0f) * 127.5f);
                value = std::max(0, std::min(255, value));

                microData[y * microWidth + x] = static_cast<unsigned char>(value);
            }
        }

        glGenTextures(1, &microNoiseTexture_);
        glBindTexture(GL_TEXTURE_2D, microNoiseTexture_);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     microWidth,
                     microHeight,
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     microData.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_REPEAT); // Tileable
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_REPEAT); // Tileable
        glBindTexture(GL_TEXTURE_2D, 0);

        std::cout << "  Micro noise: " << microWidth << "x" << microHeight << " (fine flicker)" << "\n";
    }

    // Generate hourly noise texture
    {
        std::vector<unsigned char> hourlyData(hourlyWidth * hourlyHeight);

        // Coarser scale for regional variation
        float scale = 15.0f; // ~15 noise peaks across width

        for (int y = 0; y < hourlyHeight; y++)
        {
            for (int x = 0; x < hourlyWidth; x++)
            {
                float u = static_cast<float>(x) / hourlyWidth;
                float v = static_cast<float>(y) / hourlyHeight;

                // Use different seed offset to make it visually different from
                // micro
                float noise = perlinFBM(u * scale + 100.0f, v * scale * 0.5f + 100.0f, 4, 0.5f);

                int value = static_cast<int>((noise + 1.0f) * 127.5f);
                value = std::max(0, std::min(255, value));

                hourlyData[y * hourlyWidth + x] = static_cast<unsigned char>(value);
            }
        }

        glGenTextures(1, &hourlyNoiseTexture_);
        glBindTexture(GL_TEXTURE_2D, hourlyNoiseTexture_);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     hourlyWidth,
                     hourlyHeight,
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     hourlyData.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_REPEAT); // Tileable
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_REPEAT); // Tileable
        glBindTexture(GL_TEXTURE_2D, 0);

        std::cout << "  Hourly noise: " << hourlyWidth << "x" << hourlyHeight << " (regional variation)" << "\n";
    }

    noiseTexturesGenerated_ = true;
    std::cout << "Noise textures generated successfully" << "\n";
}

// ============================================================================
// Surface Shader Initialization
// ============================================================================

bool EarthMaterial::initializeSurfaceShader()
{
    // Early return if shader is already compiled
    if (shaderAvailable_ && shaderProgram_ != 0)
    {
        return true;
    }

    // Load shaders from files
    std::string vertexShaderPath = getShaderPath("earth-vertex.glsl");
    std::string vertexShaderSource = loadShaderFile(vertexShaderPath);
    if (vertexShaderSource.empty())
    {
        std::cerr << "ERROR: EarthMaterial::initializeSurfaceShader() - Could not load earth-vertex.glsl from file"
                  << '\n';
        std::cerr << "  Tried path: " << vertexShaderPath << '\n';
        std::cerr << "  Shader-based rendering is required. Cannot continue." << '\n';
        std::exit(1);
    }

    std::string fragmentShaderPath = getShaderPath("earth-fragment.glsl");
    std::string fragmentShaderSource = loadShaderFile(fragmentShaderPath);
    if (fragmentShaderSource.empty())
    {
        std::cerr << "ERROR: EarthMaterial::initializeSurfaceShader() - Could not load earth-fragment.glsl from file"
                  << '\n';
        std::cerr << "  Tried path: " << fragmentShaderPath << '\n';
        std::cerr << "  Shader-based rendering is required. Cannot continue." << '\n';
        std::exit(1);
    }

    // Compile vertex shader
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource.c_str());
    if (vertexShader == 0)
    {
        std::cerr << "ERROR: EarthMaterial::initializeSurfaceShader() - Vertex "
                     "shader compilation failed"
                  << '\n';
        std::cerr << "  Check console output above for shader compilation errors." << '\n';
        std::exit(1);
    }

    // Compile fragment shader
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource.c_str());
    if (fragmentShader == 0)
    {
        std::cerr << "ERROR: EarthMaterial::initializeSurfaceShader() - Fragment "
                     "shader compilation failed"
                  << '\n';
        std::cerr << "  Check console output above for shader compilation errors." << '\n';
        glDeleteShader(vertexShader);
        std::exit(1);
    }

    // Link program
    shaderProgram_ = linkProgram(vertexShader, fragmentShader);

    // Shaders can be deleted after linking
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (shaderProgram_ == 0)
    {
        std::cerr << "ERROR: EarthMaterial::initializeSurfaceShader() - Shader "
                     "program linking failed"
                  << '\n';
        std::cerr << "  Check console output above for shader linking errors." << '\n';
        std::exit(1);
    }

    // IMPORTANT: Activate shader program before getting uniform locations
    // Some drivers require the program to be active when querying uniform
    // locations
    glUseProgram(shaderProgram_);

    // Get uniform locations
    uniformModelMatrix_ = glGetUniformLocation(shaderProgram_, "uModelMatrix");
    uniformViewMatrix_ = glGetUniformLocation(shaderProgram_, "uViewMatrix");
    uniformProjectionMatrix_ = glGetUniformLocation(shaderProgram_, "uProjectionMatrix");
    uniformColorTexture_ = glGetUniformLocation(shaderProgram_, "uColorTexture");
    uniformColorTexture2_ = glGetUniformLocation(shaderProgram_, "uColorTexture2");
    uniformBlendFactor_ = glGetUniformLocation(shaderProgram_, "uBlendFactor");
    uniformNormalMap_ = glGetUniformLocation(shaderProgram_, "uNormalMap");
    uniformHeightmap_ = glGetUniformLocation(shaderProgram_, "uHeightmap");
    uniformUseHeightmap_ = glGetUniformLocation(shaderProgram_, "uUseHeightmap");
    uniformUseDisplacement_ = glGetUniformLocation(shaderProgram_, "uUseDisplacement");
    uniformUseSpecular_ = glGetUniformLocation(shaderProgram_, "uUseSpecular");
    uniformSpecular_ = glGetUniformLocation(shaderProgram_, "uSpecular");
    uniformLightDir_ = glGetUniformLocation(shaderProgram_, "uLightDir");
    uniformLightColor_ = glGetUniformLocation(shaderProgram_, "uLightColor");
    uniformMoonDir_ = glGetUniformLocation(shaderProgram_, "uMoonDir");
    uniformMoonColor_ = glGetUniformLocation(shaderProgram_, "uMoonColor");
    uniformAmbientColor_ = glGetUniformLocation(shaderProgram_, "uAmbientColor");
    uniformPoleDir_ = glGetUniformLocation(shaderProgram_, "uPoleDir");
    uniformUseNormalMap_ = glGetUniformLocation(shaderProgram_, "uUseNormalMap");

    // Restore program state (we activated it earlier to get uniform
    // locations)
    glUseProgram(0);

    uniformNightlights_ = glGetUniformLocation(shaderProgram_, "uNightlights");
    uniformWindTexture1_ = glGetUniformLocation(shaderProgram_, "uWindTexture1");
    uniformWindTexture2_ = glGetUniformLocation(shaderProgram_, "uWindTexture2");
    uniformWindBlendFactor_ = glGetUniformLocation(shaderProgram_, "uWindBlendFactor");
    uniformWindTextureSize_ = glGetUniformLocation(shaderProgram_, "uWindTextureSize");
    uniformTime_ = glGetUniformLocation(shaderProgram_, "uTime");
    uniformMicroNoise_ = glGetUniformLocation(shaderProgram_, "uMicroNoise");
    uniformHourlyNoise_ = glGetUniformLocation(shaderProgram_, "uHourlyNoise");
    uniformSpecular_ = glGetUniformLocation(shaderProgram_, "uSpecular");
    uniformIceMask_ = glGetUniformLocation(shaderProgram_, "uIceMask");
    uniformIceMask2_ = glGetUniformLocation(shaderProgram_, "uIceMask2");
    uniformIceBlendFactor_ = glGetUniformLocation(shaderProgram_, "uIceBlendFactor");
    uniformLandmassMask_ = glGetUniformLocation(shaderProgram_, "uLandmassMask");
    uniformCameraPos_ = glGetUniformLocation(shaderProgram_, "uCameraPos");
    uniformCameraDir_ = glGetUniformLocation(shaderProgram_, "uCameraDir");
    uniformCameraFOV_ = glGetUniformLocation(shaderProgram_, "uCameraFOV");
    uniformPrimeMeridianDir_ = glGetUniformLocation(shaderProgram_, "uPrimeMeridianDir");
    uniformBathymetryDepth_ = glGetUniformLocation(shaderProgram_, "uBathymetryDepth");
    uniformBathymetryNormal_ = glGetUniformLocation(shaderProgram_, "uBathymetryNormal");
    uniformCombinedNormal_ = glGetUniformLocation(shaderProgram_, "uCombinedNormal");
    uniformWindTexture1_ = glGetUniformLocation(shaderProgram_, "uWindTexture1");
    uniformWindTexture2_ = glGetUniformLocation(shaderProgram_, "uWindTexture2");
    uniformWindBlendFactor_ = glGetUniformLocation(shaderProgram_, "uWindBlendFactor");
    uniformWindTextureSize_ = glGetUniformLocation(shaderProgram_, "uWindTextureSize");
    uniformPlanetRadius_ = glGetUniformLocation(shaderProgram_, "uPlanetRadius");
    uniformFlatCircleMode_ = glGetUniformLocation(shaderProgram_, "uFlatCircleMode");
    uniformSphereCenter_ = glGetUniformLocation(shaderProgram_, "uSphereCenter");
    uniformSphereRadius_ = glGetUniformLocation(shaderProgram_, "uSphereRadius");
    uniformBillboardCenter_ = glGetUniformLocation(shaderProgram_, "uBillboardCenter");
    uniformDisplacementScale_ = glGetUniformLocation(shaderProgram_, "uDisplacementScale");

    shaderAvailable_ = true;
    return true;
}

bool EarthMaterial::initialize(const std::string &combinedBasePath, TextureResolution resolution)
{
    if (initialized_)
    {
        return true;
    }

    std::string combinedPath = combinedBasePath + "/" + getResolutionFolderName(resolution);
    bool lossless = (resolution == TextureResolution::Ultra);
    const char *ext = lossless ? ".png" : ".jpg";

    std::cout << "Loading Earth textures from: " << combinedPath << "\n";

    int loadedCount = 0;

    for (int month = 1; month <= 12; month++)
    {
        // Load Blue Marble equirectangular textures
        char bmName[64];
        snprintf(bmName, sizeof(bmName), "earth_month_%02d%s", month, ext);
        std::string filepath = combinedPath + "/" + bmName;

        if (!std::filesystem::exists(filepath))
        {
            std::cout << "  Month " << month << ": not found" << "\n";
            continue;
        }

        GLuint texId = loadTexture(filepath);
        if (texId != 0)
        {
            monthlyTextures_[month - 1] = texId;
            textureLoaded_[month - 1] = true;
            loadedCount++;
            std::cout << "  Month " << month << ": loaded" << "\n";
        }
        else
        {
            std::cout << "  Month " << month << ": failed to load" << "\n";
        }
    }

    std::cout << "Earth material: " << loadedCount << "/12 textures loaded" << "\n";

    // Load heightmap and normal map for bump mapping (equirectangular)
    std::string heightmapPath = combinedPath + "/earth_landmass_heightmap.png";
    std::string normalMapPath = combinedPath + "/earth_landmass_normal.png";

    if (std::filesystem::exists(heightmapPath))
    {
        heightmapTexture_ = loadTexture(heightmapPath);
        if (heightmapTexture_ != 0)
        {
            std::cout << "  Heightmap: loaded" << "\n";
        }
    }

    if (std::filesystem::exists(normalMapPath))
    {
        normalMapTexture_ = loadTexture(normalMapPath);
        if (normalMapTexture_ != 0)
        {
            elevationLoaded_ = true;
            std::cout << "  Normal map: loaded" << "\n";
        }
    }

    // Load nightlights texture (VIIRS Black Marble city lights)
    std::string nightlightsPath = combinedPath + "/earth_nightlights.png";
    if (std::filesystem::exists(nightlightsPath))
    {
        nightlightsTexture_ = loadTexture(nightlightsPath);
        if (nightlightsTexture_ != 0)
        {
            nightlightsLoaded_ = true;
            std::cout << "  Nightlights: loaded (city lights enabled)" << "\n";
        }
    }
    else
    {
        std::cout << "  Nightlights: not found (run preprocessNightlights first)" << "\n";
    }

    // Load wind textures (12 separate 2D textures, one per month)
    // Each JPG file: width x height, RGB format (R=u, G=v, B=0)
    int windTexturesLoadedCount = 0;
    for (int month = 1; month <= 12; month++)
    {
        char filename[64];
        snprintf(filename, sizeof(filename), "earth_wind_%02d.jpg", month);
        std::string windFile = combinedPath + "/" + filename;

        if (!std::filesystem::exists(windFile))
        {
            windTextures_[month - 1] = 0;
            windTexturesLoaded_[month - 1] = false;
            continue;
        }

        // Load JPG file using stb_image
        int jpgWidth, jpgHeight, jpgChannels;
        unsigned char *jpgData = stbi_load(windFile.c_str(), &jpgWidth, &jpgHeight, &jpgChannels, 3);

        if (!jpgData)
        {
            std::cerr << "  ERROR: Failed to load wind texture file: " << windFile << "\n";
            std::cerr << "  Error: " << stbi_failure_reason() << "\n";
            windTextures_[month - 1] = 0;
            windTexturesLoaded_[month - 1] = false;
            continue;
        }

        if (jpgChannels < 3)
        {
            std::cerr << "  ERROR: Wind texture has " << jpgChannels << " channels, expected 3 (RGB format)" << "\n";
            stbi_image_free(jpgData);
            windTextures_[month - 1] = 0;
            windTexturesLoaded_[month - 1] = false;
            continue;
        }

        // Create 2D texture
        GLuint textureId = 0;
        glGenTextures(1, &textureId);
        if (textureId == 0)
        {
            std::cerr << "  ERROR: Failed to generate texture for month " << month << "\n";
            stbi_image_free(jpgData);
            windTextures_[month - 1] = 0;
            windTexturesLoaded_[month - 1] = false;
            continue;
        }

        glBindTexture(GL_TEXTURE_2D, textureId);

        // Extract R and G channels from RGB JPG data to create LUMINANCE_ALPHA texture
        size_t pixelCount = static_cast<size_t>(jpgWidth) * jpgHeight;
        std::vector<unsigned char> twoChannelData(pixelCount * 2);
        for (size_t i = 0; i < pixelCount; i++)
        {
            twoChannelData[i * 2 + 0] = jpgData[i * 3 + 0]; // R -> LUMINANCE (u component)
            twoChannelData[i * 2 + 1] = jpgData[i * 3 + 1]; // G -> ALPHA (v component)
        }

        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE_ALPHA,
                     jpgWidth,
                     jpgHeight,
                     0,
                     GL_LUMINANCE_ALPHA,
                     GL_UNSIGNED_BYTE,
                     twoChannelData.data());

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_2D, 0);

        stbi_image_free(jpgData);

        windTextures_[month - 1] = textureId;
        windTexturesLoaded_[month - 1] = true;
        windTexturesLoadedCount++;
    }

    if (windTexturesLoadedCount > 0)
    {
        std::cout << "  Wind textures: " << windTexturesLoadedCount << "/12 loaded" << "\n";
    }
    else
    {
        std::cout << "  Wind textures: not found (run preprocessWindData first)" << "\n";
    }

    // Load specular/roughness texture (surface reflectivity from MODIS green channel)
    std::string specularPath = combinedPath + "/earth_specular.png";
    if (std::filesystem::exists(specularPath))
    {
        specularTexture_ = loadTexture(specularPath);
        if (specularTexture_ != 0)
        {
            specularLoaded_ = true;
            std::cout << "  Specular: loaded (surface roughness enabled)" << "\n";
        }
    }
    else
    {
        std::cout << "  Specular: not found (run preprocessSpecular first)" << "\n";
    }

    // Load ice mask textures (12 monthly masks for seasonal ice coverage)
    int iceMasksLoadedCount = 0;
    for (int month = 1; month <= 12; month++)
    {
        char maskFilename[64];
        snprintf(maskFilename, sizeof(maskFilename), "earth_ice_mask_%02d.png", month);
        std::string iceMaskPath = combinedPath + "/" + maskFilename;

        if (std::filesystem::exists(iceMaskPath))
        {
            iceMaskTextures_[month - 1] = loadTexture(iceMaskPath);
            if (iceMaskTextures_[month - 1] != 0)
            {
                iceMasksLoaded_[month - 1] = true;
                iceMasksLoadedCount++;
            }
        }
        else
        {
            iceMaskTextures_[month - 1] = 0;
            iceMasksLoaded_[month - 1] = false;
        }
    }
    std::cout << "  Ice masks: " << iceMasksLoadedCount << "/12 loaded (seasonal ice enabled)" << "\n";

    // Load landmass mask texture (for ocean detection)
    std::string landmassMaskPath = combinedPath + "/earth_landmass_mask.png";
    if (std::filesystem::exists(landmassMaskPath))
    {
        landmassMaskTexture_ = loadTexture(landmassMaskPath);
        if (landmassMaskTexture_ != 0)
        {
            landmassMaskLoaded_ = true;
            std::cout << "  Landmass mask: loaded (ocean effects enabled)" << "\n";
        }
    }
    else
    {
        std::cout << "  Landmass mask: not found (ocean effects disabled)" << "\n";
    }

    // Load bathymetry textures (ocean floor depth and normal)
    std::string bathymetryDepthPath = combinedPath + "/earth_bathymetry_heightmap.png";
    std::string bathymetryNormalPath = combinedPath + "/earth_bathymetry_normal.png";

    if (std::filesystem::exists(bathymetryDepthPath) && std::filesystem::exists(bathymetryNormalPath))
    {
        bathymetryDepthTexture_ = loadTexture(bathymetryDepthPath);
        bathymetryNormalTexture_ = loadTexture(bathymetryNormalPath);

        if (bathymetryDepthTexture_ != 0 && bathymetryNormalTexture_ != 0)
        {
            bathymetryLoaded_ = true;
            std::cout << "  Bathymetry: loaded (ocean depth-based scattering enabled)" << "\n";
        }
    }
    else
    {
        std::cout << "  Bathymetry: not found (using fallback depth estimation)" << "\n";
    }

    // Load combined normal map (landmass + bathymetry) for shadows
    std::string combinedNormalPath = combinedPath + "/earth_combined_normal.png";
    if (std::filesystem::exists(combinedNormalPath))
    {
        combinedNormalTexture_ = loadTexture(combinedNormalPath);
        if (combinedNormalTexture_ != 0)
        {
            combinedNormalLoaded_ = true;
            std::cout << "  Combined normal map: loaded (for ocean floor shadows)" << "\n";
        }
    }
    else
    {
        std::cout << "  Combined normal map: not found (shadows will use fallback)" << "\n";
    }

    // Initialize shaders for per-pixel normal mapping
    // MANDATORY: Shader initialization (will abort on failure)
    // This initializes both surface shader and atmosphere shader
    initializeShaders();
    std::cout << "  Shader: initialized (per-pixel normal mapping enabled)" << "\n";

    // Generate noise textures for city light flickering (requires GL context)
    generateNoiseTextures();

    // MANDATORY: Check required textures are loaded
    if (!elevationLoaded_ || normalMapTexture_ == 0)
    {
        std::cerr << "ERROR: EarthMaterial::initialize() - Normal map is required!" << "\n";
        std::cerr << "  elevationLoaded_ = " << elevationLoaded_ << "\n";
        std::cerr << "  normalMapTexture_ = " << normalMapTexture_ << "\n";
        std::exit(1);
    }

    if (loadedCount == 0)
    {
        std::cerr << "ERROR: EarthMaterial::initialize() - No monthly color "
                     "textures loaded!"
                  << "\n";
        std::cerr << "  Expected 12 monthly textures, found 0" << "\n";
        std::exit(1);
    }

    initialized_ = true;
    return true;
}