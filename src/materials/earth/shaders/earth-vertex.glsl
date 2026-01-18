#version 450
#extension GL_EXT_scalar_block_layout : require

// Constants
const float PI = 3.14159265359;

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

// Manual atan2 implementation for GLSL 120 compatibility
float atan2_manual(float y, float x)
{
    if (x > 0.0)
    {
        return atan(y / x);
    }
    else if (x < 0.0 && y >= 0.0)
    {
        return atan(y / x) + PI;
    }
    else if (x < 0.0 && y < 0.0)
    {
        return atan(y / x) - PI;
    }
    else if (abs(x) < 0.0001 && y > 0.0)
    {
        return PI * 0.5;
    }
    else if (abs(x) < 0.0001 && y < 0.0)
    {
        return -PI * 0.5;
    }
    else
    {
        return 0.0; // x == 0 && y == 0
    }
}

// Convert direction vector to equirectangular UV coordinates
vec2 directionToEquirectUV(vec3 dir, vec3 poleDir, vec3 primeMeridianDir)
{
    vec3 north = poleDir;
    vec3 east = primeMeridianDir - dot(primeMeridianDir, north) * north;
    float eastLen = length(east);
    if (eastLen < 0.001)
    {
        if (abs(north.y) < 0.9)
        {
            east = normalize(cross(north, vec3(0.0, 1.0, 0.0)));
        }
        else
        {
            east = normalize(cross(north, vec3(1.0, 0.0, 0.0)));
        }
        eastLen = length(east);
    }
    east = east / eastLen;
    vec3 south90 = cross(north, east);

    float localX = dot(dir, east);
    float localY = dot(dir, north);
    float localZ = dot(dir, south90);

    float len = sqrt(localX * localX + localY * localY + localZ * localZ);
    if (len < 0.001)
    {
        return vec2(0.5, 0.5);
    }
    localX /= len;
    localY /= len;
    localZ /= len;

    float latitude = asin(clamp(localY, -1.0, 1.0));
    float longitude = atan2_manual(localZ, localX);

    float u = (longitude / PI + 1.0) * 0.5;
    float v = 0.5 - (latitude / PI);

    return vec2(u, v);
}

// Convert equirectangular UV to sinusoidal UV
vec2 toSinusoidalUV(vec2 equirectUV)
{
    float lon = (equirectUV.x - 0.5) * 2.0 * PI;
    float lat = (equirectUV.y - 0.5) * PI;
    float cosLat = cos(lat);
    float absCosLat = abs(cosLat);

    float u_tex;
    if (absCosLat < 0.01)
    {
        u_tex = 0.5;
    }
    else
    {
        float x_sinu = lon * cosLat;
        u_tex = x_sinu / (2.0 * PI) + 0.5;
        float uMin = 0.5 - 0.5 * absCosLat;
        float uMax = 0.5 + 0.5 * absCosLat;
        u_tex = clamp(u_tex, uMin, uMax);
    }

    float v_tex = 0.5 + lat / PI;
    v_tex = clamp(v_tex, 0.0, 1.0);
    return vec2(u_tex, v_tex);
}

// Vertex attributes (GLSL 3.30 core profile)
layout(location = 0) in vec3 aPosition; // Vertex position
layout(location = 1) in vec2 aTexCoord; // Texture coordinates
layout(location = 2) in vec3 aNormal;   // Vertex normal

// Outputs to fragment shader
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vTexCoord;

// Uniforms - Samplers (require binding qualifiers for Vulkan)
layout(binding = 0) uniform sampler2D uHeightmap;    // Landmass heightmap (grayscale, sinusoidal projection)
layout(binding = 1) uniform sampler2D uLandmassMask; // Landmass mask (white=land, black=ocean, sinusoidal projection)

// Uniform block for non-opaque uniforms (required for Vulkan)
// Changed from binding 2 to binding 17 to avoid conflict with fragment shader's uNormalMap texture (binding 2)
layout(set = 0, binding = 17, scalar) uniform Uniforms
{
    float uPlanetRadius;      // Display radius of planet (for scaling)
    int uFlatCircleMode;      // 1 = rendering flat circle for distant sphere, 0 = normal sphere
    vec3 uSphereCenter;       // Sphere center position (for flat circle projection)
    float uSphereRadius;      // Sphere radius (for flat circle projection)
    vec3 uCameraPos;          // Camera position
    vec3 uCameraDir;          // Camera forward direction
    float uCameraFOV;         // Camera field of view (radians)
    vec3 uPoleDir;            // Planet's north pole direction
    vec3 uPrimeMeridianDir;   // Planet's prime meridian direction
    int uUseDisplacement;     // Enable/disable vertex displacement (0 = disabled, 1 = enabled)
    float uDisplacementScale; // Multiplier to scale displacement for visibility (default: 1.0)
    mat4 uMVP;                // Model-View-Projection matrix
};

void main()
{
    if (uFlatCircleMode == 1)
    {
        // ============================================================
        // Billboard Imposter Mode - All computation in shader
        // ============================================================
        // Input: aPosition contains 2D coordinates on billboard plane (x, y, 0)
        // aTexCoord.xy contains normalized coordinates (-1 to 1) for positioning on billboard

        // Compute billboard center (closest point on sphere to camera)
        vec3 toSphere = uSphereCenter - uCameraPos;
        float distanceToSphere = length(toSphere);
        vec3 toSphereNorm = distanceToSphere > 0.001 ? normalize(toSphere) : vec3(0.0, 0.0, 1.0);
        vec3 billboardCenter = uSphereCenter - toSphereNorm * uSphereRadius;

        // Compute billboard plane orientation
        // The billboard plane is tangent to the sphere at billboardCenter
        vec3 centerDir = normalize(billboardCenter - uSphereCenter);

        // Build tangent basis vectors for the billboard plane
        // Use pole and prime meridian directions to create consistent basis
        vec3 north = normalize(uPoleDir);
        vec3 east = uPrimeMeridianDir - dot(uPrimeMeridianDir, north) * north;
        float eastLen = length(east);
        if (eastLen < 0.001)
        {
            if (abs(north.y) < 0.9)
            {
                east = normalize(cross(north, vec3(0.0, 1.0, 0.0)));
            }
            else
            {
                east = normalize(cross(north, vec3(1.0, 0.0, 0.0)));
            }
        }
        else
        {
            east = normalize(east);
        }

        // Project onto tangent plane
        vec3 tangentNorth = north - dot(north, centerDir) * centerDir;
        vec3 tangentEast = east - dot(east, centerDir) * centerDir;

        // Normalize tangent vectors
        float tangentNorthLen = length(tangentNorth);
        if (tangentNorthLen < 0.001)
        {
            tangentNorth = normalize(cross(centerDir, vec3(1.0, 0.0, 0.0)));
            if (length(tangentNorth) < 0.001)
            {
                tangentNorth = normalize(cross(centerDir, vec3(0.0, 0.0, 1.0)));
            }
        }
        else
        {
            tangentNorth = normalize(tangentNorth);
        }

        float tangentEastLen = length(tangentEast);
        if (tangentEastLen < 0.001)
        {
            tangentEast = normalize(cross(centerDir, tangentNorth));
        }
        else
        {
            tangentEast = normalize(tangentEast);
        }

        // Ensure perpendicular
        tangentEast = normalize(cross(centerDir, tangentNorth));

        // Calculate billboard size based on sphere's angular radius
        float sphereAngularRadius = asin(clamp(uSphereRadius / distanceToSphere, 0.0, 1.0));
        float hemisphereAngle = 1.57079632679; // PI/2
        float actualAngularRadius = min(sphereAngularRadius, hemisphereAngle);

        // Distance from camera to billboard plane
        vec3 toBillboardCenter = billboardCenter - uCameraPos;
        float distanceToBillboard = length(toBillboardCenter);

        // Calculate billboard radius (size of flat circle)
        float billboardRadius = distanceToBillboard * tan(actualAngularRadius);

        // Get vertex position on billboard from input coordinates
        // aPosition.xy contains the 2D position on the billboard plane (-1 to 1)
        vec2 billboardPos = aPosition.xy;
        vec3 worldPos = billboardCenter + billboardPos.x * tangentEast * billboardRadius +
                        billboardPos.y * tangentNorth * billboardRadius;

        // Compute direction from sphere center to this vertex position
        vec3 direction = normalize(worldPos - uSphereCenter);

        // Output to fragment shader
        vWorldPos = worldPos;
        vWorldNormal = direction; // Radial direction for proper lighting
        vTexCoord = aTexCoord;    // Will be recomputed in fragment shader

        // Transform position for rendering
        vec4 vertexPos = vec4(worldPos, 1.0);
        gl_Position = uMVP * vertexPos;
    }
    else
    {
        // ============================================================
        // Octree Mesh Mode - Use mesh vertices in local space
        // ============================================================
        // Input: aPosition contains the mesh vertex position in LOCAL SPACE
        // (relative to planet center at origin). The mesh already includes height
        // variations from the heightmap, so no sphere computation or displacement is needed.

        // Mesh vertices are in local space (centered at origin)
        // Transform to world space by adding planet's world position
        vec3 localPos = aPosition;
        vec3 worldPos = localPos + uSphereCenter;

        // Use normal from mesh (provided via aNormal)
        // Normals are in local space and don't need translation
        vec3 meshNormal = aNormal;
        if (length(meshNormal) < 0.001)
        {
            // Fallback: compute normal from local position (radial direction)
            meshNormal = normalize(localPos);
        }

        // Calculate UV coordinates as if vertex is on a sphere of Earth's radius
        // Use LOCAL position direction (before world transform) for UV mapping
        // This ensures UVs map correctly to sinusoidal textures as if on a sphere
        vec3 directionFromCenter = normalize(localPos);

        // Calculate equirectangular UV using proper coordinate system
        // This matches how textures are mapped (as if on a sphere of Earth's radius)
        vec2 equirectUV = directionToEquirectUV(directionFromCenter, uPoleDir, uPrimeMeridianDir);

        // Use calculated UV (fragment shader will convert to sinusoidal)
        vec2 meshUV = equirectUV;

        // Output to fragment shader (world space position for lighting/rendering)
        vWorldPos = worldPos;
        vWorldNormal = meshNormal;
        vTexCoord = meshUV;

        // Transform position for rendering (world space)
        vec4 vertexPos = vec4(worldPos, 1.0);
        gl_Position = uMVP * vertexPos;
    }
}