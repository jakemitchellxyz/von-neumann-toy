#version 120

// ============================================================
// WGS 84 Geodetic Model (GLSL)
// ============================================================
// World Geodetic System 1984 - Standard Earth ellipsoid model
// Reference: NIMA TR8350.2 "Department of Defense World Geodetic System 1984"

// WGS 84 Ellipsoid Parameters
const float WGS84_SEMI_MAJOR_AXIS_M = 6378137.0;
const float WGS84_FLATTENING = 1.0 / 298.257223563;
const float WGS84_SEMI_MINOR_AXIS_M = WGS84_SEMI_MAJOR_AXIS_M * (1.0 - WGS84_FLATTENING);
const float WGS84_ECCENTRICITY_SQUARED = 2.0 * WGS84_FLATTENING - WGS84_FLATTENING * WGS84_FLATTENING;

// Get radius at a given latitude (geodetic latitude in radians)
float wgs84_getRadiusAtLatitude(float latitudeRad)
{
    float sinLat = sin(latitudeRad);
    float sinLatSq = sinLat * sinLat;
    float denom = 1.0 - WGS84_ECCENTRICITY_SQUARED * sinLatSq;
    return WGS84_SEMI_MAJOR_AXIS_M * sqrt((1.0 - WGS84_ECCENTRICITY_SQUARED) / denom);
}

// Convert geocentric position to geodetic latitude
float wgs84_getGeodeticLatitude(vec3 pos)
{
    float r = length(pos);
    if (r < 1e-6)
        return 0.0;
    return asin(pos.y / r);
}

// Calculate normal vector on ellipsoid surface at a given position
vec3 wgs84_getEllipsoidNormal(vec3 pos)
{
    // For an ellipsoid: normal = (x/a², y/b², z/a²) normalized
    float a2 = WGS84_SEMI_MAJOR_AXIS_M * WGS84_SEMI_MAJOR_AXIS_M;
    float b2 = WGS84_SEMI_MINOR_AXIS_M * WGS84_SEMI_MINOR_AXIS_M;
    vec3 normal = vec3(pos.x / a2, pos.y / b2, pos.z / a2);
    return normalize(normal);
}

// Apply oblateness to a sphere position
vec3 wgs84_applyOblateness(vec3 spherePos)
{
    float lat = wgs84_getGeodeticLatitude(spherePos);
    float radius = wgs84_getRadiusAtLatitude(lat);
    return spherePos * radius;
}

// Outputs to fragment shader
varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying vec2 vTexCoord;

// Uniforms for WGS 84 scaling
uniform float uPlanetRadius; // Display radius of planet (for scaling)

void main()
{
    // Input: gl_Vertex is on a unit sphere (or scaled sphere)
    // We need to apply WGS 84 oblateness

    // Get the base position (assuming sphere geometry)
    vec3 basePos = gl_Vertex.xyz;

    // Normalize to get direction from center
    vec3 direction = normalize(basePos);

    // Apply WGS 84 oblateness to get ellipsoid position
    // The input sphere may already be scaled, so we need to work in normalized space first
    vec3 ellipsoidPosNormalized = wgs84_applyOblateness(direction);

    // Scale to display units: ellipsoidPosNormalized is in meters, scale to display units
    // uPlanetRadius is the display radius (what the sphere would be without oblateness)
    // We need to scale the ellipsoid to match the display scale
    // Use mean radius as reference: WGS84_MEAN_RADIUS_M ≈ 6371008.771415
    const float WGS84_MEAN_RADIUS_M = (2.0 * WGS84_SEMI_MAJOR_AXIS_M + WGS84_SEMI_MINOR_AXIS_M) / 3.0;
    float scaleFactor = uPlanetRadius / WGS84_MEAN_RADIUS_M;
    vec3 ellipsoidPos = ellipsoidPosNormalized * scaleFactor;

    // Calculate WGS 84 normal on the ellipsoid
    // Normalize ellipsoidPos to get direction, then compute normal
    vec3 ellipsoidDirection = normalize(ellipsoidPosNormalized);
    vec3 ellipsoidNormal = wgs84_getEllipsoidNormal(ellipsoidDirection);

    // Output to fragment shader
    vWorldPos = ellipsoidPos;
    vWorldNormal = ellipsoidNormal;
    vTexCoord = gl_MultiTexCoord0.xy;

    // Transform position for rendering
    vec4 vertexPos = vec4(ellipsoidPos, 1.0);
    gl_Position = gl_ModelViewProjectionMatrix * vertexPos;
}