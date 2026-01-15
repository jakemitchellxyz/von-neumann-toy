#pragma once

#include <glm/glm.hpp>

// ==================================
// Solar Lighting System
// ==================================
// Implements physically-based lighting from the Sun.
// The Sun is a 5778K blackbody emitter with inverse-square falloff.
//
// Sun light color: vec3(1.0, 0.976, 0.921) - warm white
// Intensity falls off as 1/r² where r is distance to sun in AU
//
// Reference distances (for intensity calibration):
//   Mercury: 0.39 AU -> intensity ~6.6x Earth
//   Venus:   0.72 AU -> intensity ~1.9x Earth
//   Earth:   1.00 AU -> intensity 1.0 (reference)
//   Mars:    1.52 AU -> intensity ~0.43x Earth
//   Jupiter: 5.20 AU -> intensity ~0.037x Earth
//   Saturn:  9.58 AU -> intensity ~0.011x Earth
//   Uranus: 19.22 AU -> intensity ~0.0027x Earth
//   Neptune:30.05 AU -> intensity ~0.0011x Earth
//   Pluto:  39.48 AU -> intensity ~0.00064x Earth

namespace SolarLighting {

// Sun's blackbody color (5778K, normalized)
constexpr glm::vec3 SUN_COLOR = glm::vec3(1.0f, 0.976f, 0.921f);

// Base intensity at 1 AU (used for diffuse lighting)
constexpr float BASE_INTENSITY_AT_1AU = 1.0f;

// Ambient light level (minimum light for bodies in shadow or far from sun)
// Set to zero so Sun is the exclusive light source - night sides are truly dark
constexpr float AMBIENT_LEVEL = 0.0f;

// Initialize the solar lighting system
// Call once during OpenGL setup
void initialize();

// Set the sun's position in world space
// This should be called each frame with the sun's current position
void setSunPosition(const glm::vec3& sunPos);

// Get the current sun position
glm::vec3 getSunPosition();

// Configure lighting for a body at the given position
// This updates GL_LIGHT0 to point from the sun toward the body
// with intensity based on inverse-square falloff
// 
// bodyPosition: world position of the body to be lit
// distanceScale: scale factor for distance (display units per AU)
void setupLightingForBody(const glm::vec3& bodyPosition, float distanceScale);

// Calculate light intensity at a given distance from the sun
// Returns intensity multiplier (1.0 at 1 AU, falls off as 1/r²)
// distance: distance in display units
// distanceScale: display units per AU
float calculateIntensity(float distance, float distanceScale);

// Draw a sphere with emissive material (for the Sun)
// The sun is self-illuminated and not affected by lighting
void drawEmissiveSphere(const glm::vec3& center, float radius, const glm::vec3& emissiveColor, int slices, int stacks);

// Draw a sphere with solar lighting applied
// Uses the currently configured light (call setupLightingForBody first)
void drawLitSphere(const glm::vec3& center, float radius, const glm::vec3& baseColor, int slices, int stacks);

// Draw an oriented sphere with solar lighting applied
// Uses pole and prime meridian directions from SPICE data for correct orientation
// poleDir: direction of the planet's north pole (rotation axis)
// primeMeridianDir: direction of the prime meridian (0° longitude at equator)
void drawOrientedLitSphere(const glm::vec3& center, float radius, const glm::vec3& baseColor,
                           const glm::vec3& poleDir, const glm::vec3& primeMeridianDir,
                           int slices, int stacks);

} // namespace SolarLighting
