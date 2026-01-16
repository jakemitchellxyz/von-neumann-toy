#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>


// Forward declaration
enum class TextureResolution;

// ==================================
// Dynamic Skybox / Constellation Rendering
// ==================================
// Renders a starfield using either:
// 1. Pre-computed texture (efficient, generated at startup)
// 2. Dynamic per-frame computation (fallback)
//
// Note: SKYBOX_RADIUS is defined in constants.h (50000.0f)

// Global flags to control skybox layer visibility (toggled via UI)
extern bool g_showConstellations;
extern bool g_showCelestialGrid;
extern bool g_showConstellationFigures;
extern bool g_showConstellationBounds;

// Initialize the skybox system - loads star catalog and constellation files
// Must be called before DrawSkybox
// defaultsPath: Path to the defaults directory
void InitializeSkybox(const std::string &defaultsPath = "defaults");

// Check if skybox has been initialized
bool IsSkyboxInitialized();

// ==================================
// Star Texture Generation (Pre-computation)
// ==================================

// Preprocess all skybox textures (TIF and EXR files)
// Loads textures from defaults/celestial-skybox/ and resizes them to 2x the user's selected resolution
// defaultsPath: path to defaults folder (for celestial-skybox/ directory)
// outputPath: where to save the processed textures (e.g., "celestial-skybox")
// resolution: texture resolution setting (output will be 2x this resolution)
// Returns: true if successful or already cached
bool PreprocessSkyboxTextures(const std::string &defaultsPath,
                              const std::string &outputPath,
                              TextureResolution resolution);

// Legacy function name - now calls PreprocessSkyboxTextures
// Preprocess constellation figures texture
// Loads the 32k TIF file and resizes it to 2x the user's selected resolution
// defaultsPath: path to defaults folder (for constellation_figures_32k.tif)
// outputPath: where to save the processed texture (e.g., "celestial-skybox")
// resolution: texture resolution setting (output will be 2x this resolution)
// Returns: true if successful or already cached
bool PreprocessConstellationTexture(const std::string &defaultsPath,
                                    const std::string &outputPath,
                                    TextureResolution resolution);


// Initialize the star texture material (load pre-generated texture into OpenGL)
// Call after OpenGL context is created
// texturePath: path to generated star texture folder
// resolution: texture resolution setting
// Returns: true if texture loaded successfully
bool InitializeStarTextureMaterial(const std::string &texturePath, TextureResolution resolution);

// Check if star texture material is ready
bool IsStarTextureReady();

// Draw the skybox using pre-computed texture (emissive material)
// cameraPos: current camera position (skybox is centered on camera)
void DrawSkyboxTextured(const glm::vec3 &cameraPos);

// Draw wireframe version of skybox (for wireframe overlay mode)
// Renders the same geometry as DrawSkyboxTextured but without shaders
// cameraPos: current camera position (skybox is centered on camera)
void DrawSkyboxWireframe(const glm::vec3 &cameraPos);


// Convert Right Ascension (hours) and Declination (degrees) to 3D Cartesian
// Uses proper coordinate transformation: J2000 equatorial -> J2000 ecliptic -> display
glm::vec3 raDecToCartesianHours(float raHours, float decDeg, float radius);

// Calculate Earth's rotation angle (GMST) for the given Julian Date
// Returns angle in degrees (kept for reference, not used in ecliptic-aligned rendering)
float getEarthRotationAngle(double jd);
