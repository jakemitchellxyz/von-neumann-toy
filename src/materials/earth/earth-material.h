#pragma once

#include "../../concerns/constants.h"
#include "../../concerns/settings.h"
#include <GLFW/glfw3.h>
#include <array>
#include <glm/glm.hpp>
#include <string>

// ==================================
// Earth Material with Monthly Textures
// ==================================
// Specialized material for Earth that uses NASA Blue Marble imagery
// with month selection based on Julian Date.
//
// The source tiles are 8 images per month (A1,B1,C1,D1,A2,B2,C2,D2) that get
// combined into equirectangular images at application startup.
//
// Tile layout (west to east from -180° to +180°):
//   A1 | B1 | C1 | D1   (Northern hemisphere, 90°N to 0°)
//   A2 | B2 | C2 | D2   (Southern hemisphere, 0° to 90°S)
//
// Resolution presets:
//   Low:    1024x512
//   Medium: 4096x2048 (default)
//   High:   8192x4096
//   Ultra:  16384x8192 (16K, lossless PNG)
//
// Elevation data (ETOPO 2022):
//   Source: GeoTIFF with elevation values in meters
//   Generates: Heightmap (grayscale) + Normal map (RGB)
//   Used for: Bump/displacement mapping in the material

class EarthMaterial
{
public:
    EarthMaterial();
    ~EarthMaterial();

    // Delete copy and move operations (manages OpenGL resources)
    EarthMaterial(const EarthMaterial &) = delete;
    EarthMaterial &operator=(const EarthMaterial &) = delete;
    EarthMaterial(EarthMaterial &&) = delete;
    EarthMaterial &operator=(EarthMaterial &&) = delete;

    // ==================================
    // Preprocessing (call BEFORE OpenGL init)
    // ==================================

    // Preprocess Blue Marble tiles into combined monthly images.
    // This runs at application startup BEFORE the window is created.
    // - Checks if combined images already exist for the given resolution (skips
    // if they do)
    // - Loads source tiles, combines them, saves to disk in resolution-specific
    // folder
    // - Frees all source image memory
    //
    // defaultsPath: path to defaults folder containing earth-surface/blue-marble/
    // outputBasePath: base path for output (e.g., "earth-textures")
    // resolution: which resolution preset to use
    // Returns: number of months successfully processed or already cached
    static int preprocessTiles(const std::string &defaultsPath,
                               const std::string &outputBasePath,
                               TextureResolution resolution);

    // Preprocess elevation data into heightmap and normal map textures.
    // This runs at application startup BEFORE the window is created.
    // - Loads ETOPO GeoTIFF elevation data
    // - Generates heightmap (grayscale, normalized elevation)
    // - Generates normal map (RGB, computed from heightmap gradients)
    // - Saves both to disk in resolution-specific folder
    //
    // defaultsPath: path to defaults folder containing earth-surface/elevation/
    // outputBasePath: base path for output (e.g., "earth-textures")
    // resolution: which resolution preset to use
    // Returns: true if successful or already cached
    static bool preprocessElevation(const std::string &defaultsPath,
                                    const std::string &outputBasePath,
                                    TextureResolution resolution);

    // Preprocess MODIS reflectance data into specular/roughness texture.
    // - Scans defaults/earth-surface/albedo for MODIS imagery files
    // - Extracts relative green (green - red) for surface specular/roughness
    // - Generates Earth Specular texture (grayscale) for roughness mapping
    // - Saves to earth-textures/resolution/earth_specular.png
    static int preprocessSpecular(const std::string &defaultsPath,
                                  const std::string &outputBasePath,
                                  TextureResolution resolution);

    // Preprocess VIIRS Black Marble nightlights data into emissive texture.
    // - Reads HDF5 file from defaults/earth-surface/human-lights/
    // - Extracts radiance data and converts to grayscale
    // - Outputs sinusoidal projected nightlights texture
    // - Used for: City lights emissive glow at night, color dimming
    static bool preprocessNightlights(const std::string &defaultsPath,
                                      const std::string &outputBasePath,
                                      TextureResolution resolution);

    // Preprocess Ice Masks from Blue Marble Monthly Textures
    // Creates 12 ice masks (one per month) based on white/ice colors
    // - Input: Monthly Blue Marble sinusoidal textures
    // - Outputs: earth_ice_mask_01.png through earth_ice_mask_12.png
    // - Used for: Future ice/snow coverage features
    static bool preprocessIceMasks(const std::string &defaultsPath,
                                   const std::string &outputBasePath,
                                   TextureResolution resolution);

    // Preprocess Landmass Mask from Blue Marble Color Texture
    // Creates a landmass mask (white=land, black=ocean) by detecting blue water
    // colors and using edge expansion to capture all water pixels
    // - Input: Blue Marble monthly texture (earth_month_01.jpg/png) + raw elevation GeoTIFF
    // - Output: earth_landmass_mask.png (sinusoidal projection)
    // - Used for: Filtering ocean pixels from other textures
    // - Samples raw elevation data directly from GeoTIFF file
    static bool preprocessLandmassMask(const std::string &defaultsPath,
                                       const std::string &outputBasePath,
                                       TextureResolution resolution);

    // Preprocess Wind Data from CCMP NetCDF files
    // Processes 12 monthly NetCDF files and creates a static 3D LUT binary file
    // - Input: NetCDF files from defaults/wind-forces/
    // - Output: earth_wind_3dlut.bin (raw binary: width x height x 12 months x 2 channels)
    // - Used for: Loading into OpenGL 3D texture during initialization
    static bool preprocessWindData(const std::string &defaultsPath,
                                   const std::string &outputBasePath,
                                   TextureResolution resolution);

    // Preprocess Atmosphere LUTs
    // Generates transmittance and scattering lookup tables for atmosphere rendering
    // - Output: earth_atmosphere_transmittance_lut.hdr and earth_atmosphere_scattering_lut.hdr
    // - Creates directory structure if needed
    // - Generates simple placeholder LUTs if they don't exist (can be replaced with proper ones)
    static bool preprocessAtmosphereLUTs(const std::string &outputBasePath);


    // ==================================
    // Initialization (call AFTER OpenGL init)
    // ==================================

    // Initialize the material by loading pre-combined textures into OpenGL.
    // Call this after OpenGL context is created.
    // combinedBasePath: base path to combined images (e.g., "earth-textures")
    // resolution: which resolution to load
    // Returns true if at least one texture was loaded successfully
    bool initialize(const std::string &combinedBasePath, TextureResolution resolution);

    // Check if the material is ready for rendering
    bool isInitialized() const
    {
        return initialized_;
    }

    // ==================================
    // Rendering
    // ==================================

    // Draw Earth as a textured sphere
    // cameraPos: camera position in world space
    // moonDirection: direction to moon (for moonlight illumination)
    void draw(const glm::vec3 &position,
              float displayRadius,
              const glm::vec3 &poleDirection,
              const glm::vec3 &primeMeridianDirection,
              double julianDate,
              const glm::vec3 &cameraPos,
              const glm::vec3 &sunDirection,
              const glm::vec3 &moonDirection);

    // Get the current month (1-12) for a given Julian Date
    static int getMonthFromJulianDate(double julianDate);

    // Set camera info for geometry culling (call before rendering each frame)
    static void setCameraInfo(const glm::vec3 &cameraPos, const glm::vec3 &cameraDir, float fovRadians);

    // Set screen dimensions for occlusion culling
    static void setScreenDimensions(int width, int height);

    // Calculate dynamic tessellation based on camera distance
    // Returns (baseSlices, baseStacks, localSlices, localStacks) tuple and closest point on sphere to camera
    // baseSlices/baseStacks: tessellation for regions outside the local high-detail area
    // localSlices/localStacks: tessellation for the circular region (radius = 0.25 * planet radius) around closest point
    static std::tuple<int, int, int, int> calculateTessellation(const glm::vec3 &spherePosition,
                                                                float sphereRadius,
                                                                const glm::vec3 &cameraPos,
                                                                glm::vec3 &closestPointOnSphere);

private:
    // Generate sphere geometry with proper UV mapping
    // cameraPos: camera position for back-face and frustum culling
    // cameraDir: camera forward direction for frustum culling
    // fovRadians: camera field of view in radians for frustum culling
    static void drawTexturedSphere(const glm::vec3 &position,
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
                                   bool disableCulling = false,
                                   bool enableOcclusionCulling = false,
                                   const glm::vec3 &closestPointOnSphere = glm::vec3(0.0f),
                                   GLint uniformFlatCircleMode = -1,
                                   GLint uniformSphereCenter = -1,
                                   GLint uniformSphereRadius = -1,
                                   GLint uniformBillboardCenter = -1);

    // Load a single texture from file into OpenGL
    GLuint loadTexture(const std::string &filepath);

    // Load wind texture (2-channel RG format) - specialized for wind force vectors
    GLuint loadWindTexture(const std::string &filepath);

    // Cleanup all textures
    void cleanup();

    // ==================================
    // Static preprocessing helpers
    // ==================================

    // Combine 8 source tiles into a single image for one month
    // Returns true on success, writes to outputPath
    static bool combineTilesForMonth(int month,
                                     const std::string &sourcePath,
                                     const std::string &outputPath,
                                     int outputWidth,
                                     int outputHeight,
                                     bool lossless);

    // Load a source tile and resize it
    // Returns pixel data (caller must free with delete[])
    static unsigned char *loadAndResizeTile(const std::string &filepath,
                                            int targetWidth,
                                            int targetHeight,
                                            int &channels);

    // Simple bilinear resize
    static void resizeImage(const unsigned char *src,
                            int srcW,
                            int srcH,
                            unsigned char *dst,
                            int dstW,
                            int dstH,
                            int channels);

    // ==================================
    // Static elevation processing helpers
    // ==================================

    // Load GeoTIFF elevation data
    // Returns float array of elevation values in meters (caller must delete[])
    // Sets width and height to the loaded dimensions
    static float *loadGeoTiffElevation(const std::string &filepath, int &width, int &height);

    // Generate heightmap from elevation data
    // Normalizes elevation values to 0-255 range
    static unsigned char *generateHeightmap(const float *elevation,
                                            int srcWidth,
                                            int srcHeight,
                                            int dstWidth,
                                            int dstHeight);

    // Generate normal map from heightmap using Sobel operators
    // Returns RGB data where R=X, G=Y, B=Z of surface normal
    static unsigned char *generateNormalMap(const unsigned char *heightmap, int width, int height, float heightScale);
    static void generateNormalMapSinusoidal(const unsigned char *heightmapSinu,
                                            unsigned char *normalMapSinu,
                                            int width,
                                            int height,
                                            float heightScale);

    // OpenGL texture IDs for each month (index 0 = January, etc.)
    std::array<GLuint, MONTHS_PER_YEAR> monthlyTextures_;

    // Track which textures are available
    std::array<bool, MONTHS_PER_YEAR> textureLoaded_;

    // Heightmap and normal map textures
    GLuint heightmapTexture_;
    GLuint normalMapTexture_;
    bool elevationLoaded_;

    // Specular/Roughness texture (surface reflectivity from MODIS green channel)
    GLuint specularTexture_;
    bool specularLoaded_;

    // Ice mask textures (12 monthly masks for seasonal ice coverage)
    std::array<GLuint, MONTHS_PER_YEAR> iceMaskTextures_;
    std::array<bool, MONTHS_PER_YEAR> iceMasksLoaded_;

    // Landmass mask texture (for ocean detection)
    GLuint landmassMaskTexture_;
    bool landmassMaskLoaded_;

    // Bathymetry textures (ocean floor depth and normal)
    GLuint bathymetryDepthTexture_;
    GLuint bathymetryNormalTexture_;
    bool bathymetryLoaded_;

    // Combined normal map (landmass + bathymetry) for shadows
    GLuint combinedNormalTexture_;
    bool combinedNormalLoaded_;

    // Nightlights texture (VIIRS Black Marble - city lights at night)
    GLuint nightlightsTexture_;
    bool nightlightsLoaded_;

    // Wind textures (12 separate 2D textures, one per month)
    // RG channels = u, v wind components (normalized to [-1, 1])
    std::array<GLuint, MONTHS_PER_YEAR> windTextures_;
    std::array<bool, MONTHS_PER_YEAR> windTexturesLoaded_;

    // Initialization state
    bool initialized_;

    // Fallback texture (used when monthly texture is missing)
    GLuint fallbackTexture_;

    // ==================================
    // Shader-based normal mapping
    // ==================================

    // Shader program for per-pixel normal mapping
    GLuint shaderProgram_;
    bool shaderAvailable_;

    // Shader uniform locations
    GLint uniformModelMatrix_;
    GLint uniformViewMatrix_;
    GLint uniformProjectionMatrix_;
    GLint uniformColorTexture_;
    GLint uniformColorTexture2_; // Second texture for blending
    GLint uniformBlendFactor_;   // 0.0 = Texture1, 1.0 = Texture2
    GLint uniformNormalMap_;
    GLint uniformHeightmap_;       // Landmass heightmap texture
    GLint uniformUseHeightmap_;    // Enable/disable heightmap effect
    GLint uniformUseDisplacement_; // Enable/disable vertex displacement
    GLint uniformUseSpecular_;     // Enable/disable specular/roughness effect
    GLint uniformLightDir_;
    GLint uniformLightColor_;
    GLint uniformMoonDir_;   // Moon direction (for moonlight)
    GLint uniformMoonColor_; // Moonlight color and intensity
    GLint uniformAmbientColor_;
    GLint uniformPoleDir_;
    GLint uniformUseNormalMap_;
    GLint uniformNightlights_;       // Nightlights texture (grayscale, city lights)
    GLint uniformTime_;              // Julian date for animated noise
    GLint uniformMicroNoise_;        // Micro flicker noise texture
    GLint uniformHourlyNoise_;       // Hourly variation noise texture
    GLint uniformSpecular_;          // Surface specular/roughness texture (grayscale)
    GLint uniformIceMask_;           // Ice mask texture (current month)
    GLint uniformIceMask2_;          // Ice mask texture (next month for blending)
    GLint uniformIceBlendFactor_;    // Blend factor between ice masks
    GLint uniformLandmassMask_;      // Landmass mask texture (land=white, ocean=black)
    GLint uniformCameraPos_;         // Camera position for view direction calculations
    GLint uniformCameraDir_;         // Camera forward direction
    GLint uniformCameraFOV_;         // Camera field of view (radians)
    GLint uniformPrimeMeridianDir_;  // Planet's prime meridian direction
    GLint uniformBathymetryDepth_;   // Ocean floor depth texture (0=surface,
                                     // 255=deepest)
    GLint uniformBathymetryNormal_;  // Ocean floor normal map
    GLint uniformCombinedNormal_;    // Combined normal map (landmass + bathymetry) for shadows
    GLint uniformWindTexture1_;      // Wind texture for current month (RG = u,v components)
    GLint uniformWindTexture2_;      // Wind texture for next month (RG = u,v components)
    GLint uniformWindBlendFactor_;   // Blend factor between current and next month (0-1)
    GLint uniformWindTextureSize_;   // Wind texture resolution (width, height) for UV normalization
    GLint uniformPlanetRadius_;      // Planet radius for WGS 84 oblateness
    GLint uniformFlatCircleMode_;    // 1 = rendering flat circle, 0 = normal sphere
    GLint uniformSphereCenter_;      // Sphere center position (for flat circle projection)
    GLint uniformSphereRadius_;      // Sphere radius (for flat circle projection)
    GLint uniformBillboardCenter_;   // Billboard center position (closest point on sphere to camera)
    GLint uniformDisplacementScale_; // Multiplier to scale vertex displacement for visibility

    // Initialize surface shader (earth-vertex.glsl + earth-fragment.glsl)
    bool initializeSurfaceShader();

    // Procedural noise textures for city light flickering
    GLuint microNoiseTexture_;  // Fine-grained noise (per-second flicker)
    GLuint hourlyNoiseTexture_; // Coarse noise (hourly variation)
    bool noiseTexturesGenerated_;

    // Generate tileable Perlin noise texture
    void generateNoiseTextures();

    // Initialize shaders (called from initialize())
    bool initializeShaders();

    // Compile a shader from source
    static GLuint compileShader(GLenum type, const char *source);

    // Link vertex and fragment shaders into a program
    static GLuint linkProgram(GLuint vertexShader, GLuint fragmentShader);


public:
    // Get elevation data loading status
    bool getElevationLoaded() const
    {
        return elevationLoaded_;
    }

    // Get heightmap texture ID (for sampling elevation)
    GLuint getHeightmapTexture() const
    {
        return heightmapTexture_;
    }

    // Toggle texture effects for debugging
    void setUseHeightmap(bool use)
    {
        useHeightmap_ = use;
    }
    bool getUseHeightmap() const
    {
        return useHeightmap_;
    }

    void setUseNormalMap(bool use)
    {
        useNormalMap_ = use;
    }
    bool getUseNormalMap() const
    {
        return useNormalMap_;
    }

    void setUseSpecular(bool use)
    {
        useSpecular_ = use;
    }
    bool getUseSpecular() const
    {
        return useSpecular_;
    }

    // Draw wireframe version of Earth (for wireframe overlay mode)
    // Renders the same geometry as draw() but without shaders, so glPolygonMode works
    void drawWireframe(const glm::vec3 &position,
                       float displayRadius,
                       const glm::vec3 &poleDirection,
                       const glm::vec3 &primeMeridianDirection,
                       double julianDate,
                       const glm::vec3 &cameraPos);

private:
    // Draw a wireframe ring
    void drawDebugRing(const glm::vec3 &center, float radius, const glm::vec3 &color);

    // Draw 3D text oriented towards camera
    // targetPixelSize: desired height in screen pixels (default 12px)
    static constexpr float DEFAULT_TEXT_PIXEL_SIZE = 12.0F;
    void drawBillboardText(const glm::vec3 &pos,
                           const std::string &text,
                           const glm::vec3 &cameraPos,
                           float targetPixelSize = DEFAULT_TEXT_PIXEL_SIZE);

    bool useHeightmap_ = true; // Enable/disable heightmap effect (default enabled)
    bool useNormalMap_ = true; // Enable/disable normal map (default enabled)
    bool useSpecular_ = true;  // Enable/disable specular/roughness effect (default enabled)
};

// ==================================
// Global Earth Material Instance
// ==================================
// Cannot be const because it's initialized and modified at runtime
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern EarthMaterial g_earthMaterial;
