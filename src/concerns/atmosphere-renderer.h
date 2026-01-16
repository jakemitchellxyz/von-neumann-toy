#pragma once

#include <glm/glm.hpp>
#include <string>

// ==================================
// Atmosphere Renderer
// ==================================
// Renders atmosphere as a fullscreen overlay using SDF-based cone marching
// Uses transmittance and scattering LUTs for efficient atmosphere rendering

// Initialize atmosphere renderer (call once at startup, after OpenGL context creation)
// Returns true if initialization succeeded
bool InitAtmosphereRenderer();

// Cleanup atmosphere renderer resources (call on shutdown)
void CleanupAtmosphereRenderer();

// Load atmosphere LUTs
// transmittancePath: path to transmittance LUT HDR file
// scatteringPath: path to scattering LUT HDR file
// Returns true if both LUTs loaded successfully
bool LoadAtmosphereLUTs(const std::string &transmittancePath, const std::string &scatteringPath);

// Render atmosphere overlay
// Should be called after rendering the scene, before UI
// cameraPos: camera position in world space
// cameraDir: camera forward direction (normalized)
// cameraRight: camera right direction (normalized)
// cameraUp: camera up direction (normalized)
// fovRadians: camera field of view in radians
// aspectRatio: screen aspect ratio (width/height)
// nearPlane: near plane distance
// planetCenter: planet center position in world space
// planetRadius: planet radius
// atmosphereRadius: atmosphere outer radius
// sunDir: sun direction (normalized)
// sunColor: sun color/intensity
void RenderAtmosphere(const glm::vec3 &cameraPos,
                      const glm::vec3 &cameraDir,
                      const glm::vec3 &cameraRight,
                      const glm::vec3 &cameraUp,
                      float fovRadians,
                      float aspectRatio,
                      float nearPlane,
                      const glm::vec3 &planetCenter,
                      float planetRadius,
                      float atmosphereRadius,
                      const glm::vec3 &sunDir,
                      const glm::vec3 &sunColor);

// Check if atmosphere renderer is initialized and ready
bool IsAtmosphereRendererReady();
