#version 120

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

// Outputs to fragment shader
varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying vec2 vTexCoord;

// Uniforms for WGS 84 scaling
uniform float uPlanetRadius; // Display radius of planet (for scaling)

// Uniforms for billboard imposter mode
uniform int uFlatCircleMode;    // 1 = rendering flat circle for distant sphere, 0 = normal sphere
uniform vec3 uSphereCenter;     // Sphere center position (for flat circle projection)
uniform float uSphereRadius;    // Sphere radius (for flat circle projection)
uniform vec3 uCameraPos;        // Camera position
uniform vec3 uCameraDir;        // Camera forward direction
uniform float uCameraFOV;       // Camera field of view (radians)
uniform vec3 uPoleDir;          // Planet's north pole direction
uniform vec3 uPrimeMeridianDir; // Planet's prime meridian direction

// Uniforms for vertex displacement
uniform sampler2D uHeightmap;      // Landmass heightmap (grayscale, sinusoidal projection)
uniform sampler2D uLandmassMask;   // Landmass mask (white=land, black=ocean, sinusoidal projection)
uniform int uUseDisplacement;      // Enable/disable vertex displacement (0 = disabled, 1 = enabled)
uniform float uDisplacementScale;  // Multiplier to scale displacement for visibility (default: 1.0)

void main()
{
    if (uFlatCircleMode == 1)
    {
        // ============================================================
        // Billboard Imposter Mode - All computation in shader
        // ============================================================
        // Input: gl_Vertex contains 2D coordinates on billboard plane (x, y, 0)
        // gl_MultiTexCoord0.xy contains normalized coordinates (-1 to 1) for positioning on billboard
        
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
        // gl_Vertex.xy contains the 2D position on the billboard plane (-1 to 1)
        vec2 billboardPos = gl_Vertex.xy;
        vec3 worldPos = billboardCenter + billboardPos.x * tangentEast * billboardRadius + 
                                       billboardPos.y * tangentNorth * billboardRadius;
        
        // Compute direction from sphere center to this vertex position
        vec3 direction = normalize(worldPos - uSphereCenter);
        
        // Output to fragment shader
        vWorldPos = worldPos;
        vWorldNormal = direction; // Radial direction for proper lighting
        vTexCoord = gl_MultiTexCoord0.xy; // Will be recomputed in fragment shader
        
        // Transform position for rendering
        vec4 vertexPos = vec4(worldPos, 1.0);
        gl_Position = gl_ModelViewProjectionMatrix * vertexPos;
    }
    else
    {
        // ============================================================
        // Normal Sphere Mode - Apply WGS 84 oblateness
        // ============================================================
        // Input: gl_Vertex is on a unit sphere (or scaled sphere)
        
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

        // Calculate ellipsoid direction for normal computation
        vec3 ellipsoidDirection = normalize(ellipsoidPosNormalized);

        // Apply vertex displacement for landmasses
        if (uUseDisplacement == 1)
        {
            // Use the UV coordinates provided by the sphere geometry (equirectangular)
            // Convert to sinusoidal projection to match texture format
            vec2 equirectUV = gl_MultiTexCoord0.xy;
            vec2 sinusoidalUV = toSinusoidalUV(equirectUV);
            
            // Sample landmass mask and heightmap
            float landMask = texture2D(uLandmassMask, sinusoidalUV).r;
            float heightmapValue = texture2D(uHeightmap, sinusoidalUV).r;
            
            // Only apply displacement for landmasses (mask out ocean)
            if (landMask > 0.5)
            {
                // Heightmap encoding:
                // - 128 (0.5) = sea level (0m elevation)
                // - 255 (1.0) = Mt. Everest (~8848m elevation)
                // Convert heightmap value [0,1] to elevation in meters
                // Map: 0.5 -> 0m, 1.0 -> 8848m
                float elevationMeters = (heightmapValue - 0.5) / 0.5 * 8848.0;
                
                // Clamp to reasonable range (0 to 8848m)
                elevationMeters = clamp(elevationMeters, 0.0, 8848.0);
                
                // Convert elevation to display units relative to sphere radius
                // Scale displacement proportionally to the planet's display radius
                // This ensures mountains scale correctly with dynamic LOD and tessellation
                // Earth radius = 6371 km = 6,371,000 m
                const float EARTH_RADIUS_M = 6371000.0;
                // Displacement is relative to radius: (elevation / radius) * displayRadius
                // This makes displacement scale correctly regardless of display radius
                float displacementRatio = elevationMeters / EARTH_RADIUS_M;
                float displacementDisplay = displacementRatio * uPlanetRadius * uDisplacementScale;
                
                // Displace vertex outward along surface normal direction
                // The displacement increases the distance from the sphere center
                vec3 displacementDir = normalize(ellipsoidPos);
                ellipsoidPos += displacementDir * displacementDisplay;
            }
        }

        // Calculate WGS 84 normal on the ellipsoid
        // ellipsoidDirection is already computed above for displacement
        vec3 ellipsoidNormal = wgs84_getEllipsoidNormal(ellipsoidDirection);

        // Output to fragment shader
        vWorldPos = ellipsoidPos;
        vWorldNormal = ellipsoidNormal;
        vTexCoord = gl_MultiTexCoord0.xy;

        // Transform position for rendering
        vec4 vertexPos = vec4(ellipsoidPos, 1.0);
        gl_Position = gl_ModelViewProjectionMatrix * vertexPos;
    }
}