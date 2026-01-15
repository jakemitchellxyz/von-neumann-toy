#pragma once

#include <glm/glm.hpp>

// ============================================================================
// Coordinate Conversion Helpers
// ============================================================================
// Functions to convert between geographic coordinates (lat/lon) and 3D positions
// on Earth's surface using the same coordinate system as the simulation
// (starlink-ast compatible: Y-up, right-handed)

namespace EarthCoordinateConversion
{

// Convert latitude and longitude to a 3D position vector on Earth's surface
// latitude: geodetic latitude in radians (-π/2 to π/2, negative = south)
// longitude: longitude in radians (-π to π, negative = west)
// radius: radius of Earth sphere in display units
// Returns: 3D position vector on sphere surface (in display coordinate system)
//
// Coordinate system:
// - Y is up (north pole direction)
// - X points toward prime meridian (0° longitude) at equator
// - Z completes right-handed system (90°E longitude at equator)
//
// This matches the coordinate system used by SPICE ephemeris data after
// transformation via auToDisplayUnits() in entrypoint.cpp
glm::vec3 latLonToPosition(double latitude, double longitude, float radius);

// Convert a 3D position on Earth's surface to latitude and longitude
// position: 3D position vector (normalized or scaled by radius)
// Returns: pair of (latitude, longitude) in radians
std::pair<double, double> positionToLatLon(const glm::vec3 &position);

// Convert equirectangular UV coordinates to latitude/longitude
// uv: texture coordinates (u: 0-1 maps to longitude -180° to +180°, v: 0-1 maps to latitude +90° to -90°)
// Returns: pair of (latitude, longitude) in radians
std::pair<double, double> uvToLatLon(const glm::vec2 &uv);

// Convert latitude/longitude to equirectangular UV coordinates
// latitude: in radians (-π/2 to π/2)
// longitude: in radians (-π to π)
// Returns: UV coordinates (u: 0-1, v: 0-1)
glm::vec2 latLonToUV(double latitude, double longitude);

// Convert equirectangular UV to sinusoidal UV (for texture sampling)
// equirectUV: equirectangular UV coordinates (u: 0-1, v: 0-1)
// Returns: sinusoidal UV coordinates (u: 0-1, v: 0-1)
glm::vec2 equirectToSinusoidal(const glm::vec2 &equirectUV);

// Convert sinusoidal UV to equirectangular UV
// sinuUV: sinusoidal UV coordinates (u: 0-1, v: 0-1)
// Returns: equirectangular UV coordinates (u: 0-1, v: 0-1)
glm::vec2 sinusoidalToEquirect(const glm::vec2 &sinuUV);

} // namespace EarthCoordinateConversion
