#pragma once

#include <glm/glm.hpp>
#include <string>
#include <GLFW/glfw3.h>

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

// Global flag to control constellation visibility (toggled via UI)
extern bool g_showConstellations;

// Initialize the skybox system - loads star catalog and constellation files
// Must be called before DrawSkybox
// defaultsPath: Path to the defaults directory
void InitializeSkybox(const std::string& defaultsPath = "defaults");

// Check if skybox has been initialized
bool IsSkyboxInitialized();

// ==================================
// Star Texture Generation (Pre-computation)
// ==================================

// Generate star texture at startup (call BEFORE render loop, after Earth textures)
// This pre-computes all star positions into an equirectangular HDR texture
// defaultsPath: path to defaults folder (for star catalog)
// outputPath: where to save the generated texture (e.g., "star-textures")
// resolution: texture resolution setting (Low=2K, Medium=4K, High/Ultra=8K)
// jd: Julian Date for star positions (proper motion)
// Returns: number of stars rendered, or -1 on failure
int GenerateStarTexture(const std::string& defaultsPath,
                        const std::string& outputPath,
                        TextureResolution resolution,
                        double jd);

// Initialize the star texture material (load pre-generated texture into OpenGL)
// Call after OpenGL context is created
// texturePath: path to generated star texture folder
// resolution: texture resolution setting
// Returns: true if texture loaded successfully
bool InitializeStarTextureMaterial(const std::string& texturePath, TextureResolution resolution);

// Check if star texture material is ready
bool IsStarTextureReady();

// Draw the skybox using pre-computed texture (emissive material)
// cameraPos: current camera position (skybox is centered on camera)
void DrawSkyboxTextured(const glm::vec3& cameraPos);

// ==================================
// Legacy Dynamic Rendering (fallback)
// ==================================

// Draw the skybox dynamically (stars computed each frame)
// cameraPos: Current camera position in world space
// jd: Current Julian Date for calculating star positions (proper motion)
// cameraFront: Camera forward direction (for billboard orientation)
// cameraUp: Camera up direction (for billboard orientation)
void DrawSkybox(const glm::vec3& cameraPos, double jd,
                const glm::vec3& cameraFront = glm::vec3(0.0f, 0.0f, -1.0f),
                const glm::vec3& cameraUp = glm::vec3(0.0f, 1.0f, 0.0f));

// Convert Right Ascension (hours) and Declination (degrees) to 3D Cartesian
// Uses proper coordinate transformation: J2000 equatorial -> J2000 ecliptic -> display
glm::vec3 raDecToCartesianHours(float raHours, float decDeg, float radius);

// Calculate Earth's rotation angle (GMST) for the given Julian Date
// Returns angle in degrees (kept for reference, not used in ecliptic-aligned rendering)
float getEarthRotationAngle(double jd);
