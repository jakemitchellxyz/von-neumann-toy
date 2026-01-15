// ============================================================
// WGS 84 Geodetic Model (GLSL)
// ============================================================
// World Geodetic System 1984 - Standard Earth ellipsoid model
// Reference: NIMA TR8350.2 "Department of Defense World Geodetic System 1984"
//
// The Earth is an oblate spheroid (ellipsoid), not a perfect sphere.
// The equatorial radius is larger than the polar radius due to rotation.

// ============================================================
// WGS 84 Ellipsoid Parameters
// ============================================================
// Semi-major axis (equatorial radius) in meters
const float WGS84_SEMI_MAJOR_AXIS_M = 6378137.0;

// Flattening: f = (a - b) / a
const float WGS84_FLATTENING = 1.0 / 298.257223563;

// Semi-minor axis (polar radius) in meters: b = a(1 - f)
const float WGS84_SEMI_MINOR_AXIS_M = WGS84_SEMI_MAJOR_AXIS_M * (1.0 - WGS84_FLATTENING); // ≈ 6356752.314245

// First eccentricity squared: e² = 2f - f²
const float WGS84_ECCENTRICITY_SQUARED = 2.0 * WGS84_FLATTENING - WGS84_FLATTENING * WGS84_FLATTENING; // ≈ 0.00669437999014

// Mean radius (approximation): R = (2a + b) / 3
const float WGS84_MEAN_RADIUS_M = (2.0 * WGS84_SEMI_MAJOR_AXIS_M + WGS84_SEMI_MINOR_AXIS_M) / 3.0; // ≈ 6371008.771415

// ============================================================
// Helper Functions
// ============================================================

// Get radius at a given latitude (geodetic latitude in radians)
// Latitude: 0 = equator, π/2 = north pole, -π/2 = south pole
float wgs84_getRadiusAtLatitude(float latitudeRad)
{
    float sinLat = sin(latitudeRad);
    float sinLatSq = sinLat * sinLat;
    float denom = 1.0 - WGS84_ECCENTRICITY_SQUARED * sinLatSq;
    return WGS84_SEMI_MAJOR_AXIS_M * sqrt((1.0 - WGS84_ECCENTRICITY_SQUARED) / denom);
}

// Convert geocentric position to geodetic latitude
// pos: position vector (x, y, z), assuming Y-up coordinate system
// Returns: geodetic latitude in radians
float wgs84_getGeodeticLatitude(vec3 pos)
{
    // For Y-up: latitude is angle from equator (XZ plane) to Y axis
    // Latitude = asin(y / r) where r is distance from center
    float r = length(pos);
    if (r < 1e-6)
        return 0.0; // At center, return equator
    return asin(pos.y / r);
}

// Get ellipsoid radius at a given geocentric position
// pos: position vector (x, y, z)
// Returns: radius from center to ellipsoid surface at this position
float wgs84_getEllipsoidRadius(vec3 pos)
{
    float lat = wgs84_getGeodeticLatitude(pos);
    return wgs84_getRadiusAtLatitude(lat);
}

// Calculate normal vector on ellipsoid surface at a given position
// pos: position vector (x, y, z)
// Returns: normalized normal vector pointing outward from ellipsoid surface
vec3 wgs84_getEllipsoidNormal(vec3 pos)
{
    // For an ellipsoid: normal = (x/a², y/b², z/a²) normalized
    // This accounts for the different radii in different directions
    float a2 = WGS84_SEMI_MAJOR_AXIS_M * WGS84_SEMI_MAJOR_AXIS_M;
    float b2 = WGS84_SEMI_MINOR_AXIS_M * WGS84_SEMI_MINOR_AXIS_M;

    vec3 normal = vec3(pos.x / a2, pos.y / b2, pos.z / a2);
    return normalize(normal);
}

// Apply oblateness to a sphere position
// spherePos: position on unit sphere (normalized)
// Returns: position on WGS 84 ellipsoid (scaled to meters)
vec3 wgs84_applyOblateness(vec3 spherePos)
{
    // Get latitude from sphere position
    float lat = wgs84_getGeodeticLatitude(spherePos);

    // Get radius at this latitude
    float radius = wgs84_getRadiusAtLatitude(lat);

    // Scale position by radius
    return spherePos * radius;
}
