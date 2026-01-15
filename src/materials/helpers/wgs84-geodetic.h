#pragma once

#include <cmath>
#include <glm/glm.hpp>


// ============================================================
// WGS 84 Geodetic Model
// ============================================================
// World Geodetic System 1984 - Standard Earth ellipsoid model
// Reference: NIMA TR8350.2 "Department of Defense World Geodetic System 1984"
//
// The Earth is an oblate spheroid (ellipsoid), not a perfect sphere.
// The equatorial radius is larger than the polar radius due to rotation.

namespace WGS84
{
// ============================================================
// WGS 84 Ellipsoid Parameters
// ============================================================
// Semi-major axis (equatorial radius) in meters
constexpr double SEMI_MAJOR_AXIS_M = 6378137.0;

// Flattening: f = (a - b) / a
constexpr double FLATTENING = 1.0 / 298.257223563;

// Semi-minor axis (polar radius) in meters: b = a(1 - f)
constexpr double SEMI_MINOR_AXIS_M = SEMI_MAJOR_AXIS_M * (1.0 - FLATTENING); // ≈ 6356752.314245

// First eccentricity squared: e² = 2f - f²
constexpr double ECCENTRICITY_SQUARED = 2.0 * FLATTENING - FLATTENING * FLATTENING; // ≈ 0.00669437999014

// Mean radius (approximation): R = (2a + b) / 3
constexpr double MEAN_RADIUS_M = (2.0 * SEMI_MAJOR_AXIS_M + SEMI_MINOR_AXIS_M) / 3.0; // ≈ 6371008.771415

// ============================================================
// Helper Functions
// ============================================================

// Get radius at a given latitude (geodetic latitude in radians)
// Latitude: 0 = equator, π/2 = north pole, -π/2 = south pole
inline double getRadiusAtLatitude(double latitudeRad)
{
    double sinLat = std::sin(latitudeRad);
    double sinLatSq = sinLat * sinLat;
    double denom = 1.0 - ECCENTRICITY_SQUARED * sinLatSq;
    return SEMI_MAJOR_AXIS_M * std::sqrt((1.0 - ECCENTRICITY_SQUARED) / denom);
}

// Convert geocentric position to geodetic latitude
// pos: position vector (x, y, z) in meters, assuming Y-up coordinate system
// Returns: geodetic latitude in radians
inline double getGeodeticLatitude(const glm::dvec3 &pos)
{
    // For Y-up: latitude is angle from equator (XZ plane) to Y axis
    // Latitude = asin(y / r) where r is distance from center
    double r = glm::length(pos);
    if (r < 1e-6)
        return 0.0; // At center, return equator
    return std::asin(pos.y / r);
}

// Get ellipsoid radius at a given geocentric position
// pos: position vector (x, y, z) in meters
// Returns: radius from center to ellipsoid surface at this position
inline double getEllipsoidRadius(const glm::dvec3 &pos)
{
    double lat = getGeodeticLatitude(pos);
    return getRadiusAtLatitude(lat);
}

// Calculate normal vector on ellipsoid surface at a given position
// pos: position vector (x, y, z) in meters
// Returns: normalized normal vector pointing outward from ellipsoid surface
inline glm::dvec3 getEllipsoidNormal(const glm::dvec3 &pos)
{
    // For an ellipsoid: normal = (x/a², y/b², z/a²) normalized
    // This accounts for the different radii in different directions
    double a2 = SEMI_MAJOR_AXIS_M * SEMI_MAJOR_AXIS_M;
    double b2 = SEMI_MINOR_AXIS_M * SEMI_MINOR_AXIS_M;

    glm::dvec3 normal(pos.x / a2, pos.y / b2, pos.z / a2);
    return glm::normalize(normal);
}

// Apply oblateness to a sphere position
// spherePos: position on unit sphere (normalized)
// Returns: position on WGS 84 ellipsoid (scaled to meters)
inline glm::dvec3 applyOblateness(const glm::dvec3 &spherePos)
{
    // Get latitude from sphere position
    double lat = getGeodeticLatitude(spherePos);

    // Get radius at this latitude
    double radius = getRadiusAtLatitude(lat);

    // Scale position by radius
    return spherePos * radius;
}

} // namespace WGS84
