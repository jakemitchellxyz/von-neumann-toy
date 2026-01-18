#pragma once

#include <glm/glm.hpp>
#include <string>

// Forward declaration
enum class TextureResolution;

// ==================================
// Skybox Preprocessing
// ==================================
// The skybox is rendered via the ray-miss case in single-pass-screen.frag
// Preprocessing converts source textures (EXR/TIF) to cubemap format

// Preprocess all skybox textures (TIF and EXR files)
// Loads textures from defaults/celestial-skybox/ and converts to cubemap format
// defaultsPath: path to defaults folder (for celestial-skybox/ directory)
// outputPath: where to save the processed textures (e.g., "celestial-skybox")
// resolution: texture resolution setting (output will be 2x this resolution)
// Returns: true if successful or already cached
bool PreprocessSkyboxTextures(const std::string &defaultsPath,
                              const std::string &outputPath,
                              TextureResolution resolution);

// Legacy function name - now calls PreprocessSkyboxTextures
bool PreprocessConstellationTexture(const std::string &defaultsPath,
                                    const std::string &outputPath,
                                    TextureResolution resolution);

// ==================================
// Legacy API (Stubs)
// ==================================
// These functions are kept for API compatibility but are no longer functional.
// The skybox is now rendered via ray-miss in single-pass-screen.frag using
// the cubemap texture loaded by loadSkyboxTexture().

void InitializeSkybox(const std::string &defaultsPath = "defaults");
bool IsSkyboxInitialized();
bool InitializeStarTextureMaterial(const std::string &texturePath, TextureResolution resolution);
bool IsStarTextureReady();
void DrawSkyboxTextured(const glm::vec3 &cameraPos,
                        const glm::mat4 &viewMatrix = glm::mat4(1.0f),
                        const glm::mat4 &projectionMatrix = glm::mat4(1.0f));
void DrawSkyboxWireframe(const glm::vec3 &cameraPos);
void CleanupSkyboxVulkan();

// ==================================
// Utility Functions
// ==================================

// Convert Right Ascension (hours) and Declination (degrees) to 3D Cartesian
// Uses J2000 equatorial coordinates
glm::vec3 raDecToCartesianHours(float raHours, float decDeg, float radius);

// Calculate Earth's rotation angle (GMST) for the given Julian Date
// Returns angle in degrees
float getEarthRotationAngle(double jd);
