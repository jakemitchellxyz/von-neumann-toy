#version 450

// SDF Ray Marching Fragment Shader
// Optimized sphere tracing with frustum-culled objects
// Uses native Vulkan cubemaps for hardware-accelerated skybox and Earth texture sampling

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 fragColor;

// ==================================
// Skybox Cubemap Texture (Native Vulkan Cubemap)
// ==================================
// Hardware-accelerated cubemap sampling with seamless edge filtering
// Face order: +X, -X, +Y, -Y, +Z, -Z (Vulkan convention)
layout(set = 0, binding = 3) uniform samplerCube skyboxCubemap;

// Skybox sampling parameters
const float SKYBOX_EXPOSURE = 5.0; // HDR exposure multiplier

// ==================================
// Earth Material Textures (Native Vulkan Cubemaps)
// ==================================
// Hardware-accelerated cubemap sampling for all Earth textures
layout(set = 0, binding = 4) uniform samplerCube earthColorTexture;       // Monthly Blue Marble color
layout(set = 0, binding = 5) uniform samplerCube earthNormalTexture;      // Normal map for terrain
layout(set = 0, binding = 6) uniform samplerCube earthNightlightsTexture; // City lights at night
layout(set = 0, binding = 7) uniform samplerCube earthSpecularTexture;    // Specular/roughness
layout(set = 0, binding = 8) uniform samplerCube earthHeightmapTexture;   // Heightmap for parallax/displacement

// Earth NAIF ID constant
const int NAIF_EARTH = 399;

// ==================================
// Terrain Displacement Constants
// ==================================
// Real Earth terrain: Everest = 8.849 km, Mariana Trench = -10.994 km
// ============================================================================
// Elevation Range Constants (Combined Heightmap: Landmass + Bathymetry)
// ============================================================================
// Real-world elevation data normalized to [0, 1] where:
//   0.0 = Mariana Trench (Challenger Deep): -10,994 meters
//   SEA_LEVEL_NORMALIZED (~0.554) = Sea level: 0 meters
//   1.0 = Mt. Everest: +8,849 meters
// Total range: ~19,843 meters
//
// Visual exaggeration is applied for visibility from space:
// Earth radius = 6,371 km, so real Everest height = ~0.0014 radius (0.14%)
// We exaggerate by ~15x for visibility from space viewing distances

const float SEA_LEVEL_NORMALIZED = 0.554;     // Sea level in normalized heightmap (10994 / 19843)
const float ELEVATION_RANGE_METERS = 19843.0; // Total elevation range in meters
const float TERRAIN_EXAGGERATION = 15.0;      // Visual exaggeration factor

// Maximum displacement as fraction of radius (applies to both mountains and ocean depths)
// At exaggeration factor 15x: Everest = ~2% radius, Mariana = ~2.6% radius
const float MAX_HEIGHT_DISPLACEMENT = 0.026; // Maximum absolute displacement from sphere surface

// ==================================
// SSBOs
// ==================================
layout(std430, set = 0, binding = 1) buffer HoverOutput
{
    uint hitMaterialID;
}
hoverOut;

// Min distance output for camera collision detection
// Shader atomically updates this with minimum distance to displaced terrain
layout(std430, set = 0, binding = 9) buffer MinDistanceOutput
{
    uint minDistanceBits; // Float stored as bits for atomic min operation
}
minDistOut;

struct CelestialObjectGPU
{
    vec3 position;
    float radius;
    vec3 color;
    int naifId;
    vec3 poleDirection; // From SPICE (display coords)
    float _padding1;
    vec3 primeMeridianDirection; // From SPICE (display coords)
    float _padding2;
};

layout(std430, set = 0, binding = 2) buffer CelestialObjects
{
    uint objectCount;
    uint _padding[3];
    CelestialObjectGPU objects[32];
}
celestialData;

// ==================================
// Push Constants
// ==================================
layout(push_constant) uniform PushConstants
{
    vec2 julianDate;
    float timeDilation;
    float worldPadding;

    float mouseX;
    float mouseY;
    uint mouseDown;
    float inputPadding;

    mat4 viewMatrix;
    mat4 projectionMatrix;
    vec3 cameraPosition;
    float fov;
}
pc;

// ==================================
// Ray Marching Constants
// ==================================
const int MAX_STEPS = 256; // More steps for detailed terrain
const float MAX_DIST = 100000.0;
const float SURF_DIST = 0.00001; // Surface hit threshold (slightly larger for stability)
const float MIN_STEP = 0.00001;  // Minimum step to prevent infinite loops

// ==================================
// Procedural Noise for Detail
// ==================================
// Simple hash function for pseudo-random values
float hash(vec3 p)
{
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Smooth 3D noise
float noise3D(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f); // Smoothstep interpolation

    return mix(mix(mix(hash(i + vec3(0, 0, 0)), hash(i + vec3(1, 0, 0)), f.x),
                   mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x),
                   f.y),
               mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
                   mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x),
                   f.y),
               f.z);
}

// Fractal Brownian Motion - multi-octave noise for natural-looking detail
float fbm(vec3 p, int octaves, float lacunarity, float gain)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++)
    {
        value += amplitude * noise3D(p * frequency);
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return value;
}

// ==================================
// SDF Primitives
// ==================================

float sdSphere(vec3 p, vec3 center, float radius)
{
    return length(p - center) - radius;
}

// SDF for Earth with heightmap displacement (combined landmass + bathymetry)
// Incorporates terrain height AND ocean depth into the implicit surface
// Heightmap format: 0.0 = Mariana Trench, ~0.554 = sea level, 1.0 = Everest
// - Above sea level (> 0.554): surface rises above sphere (mountains)
// - Below sea level (< 0.554): surface dips below sphere (ocean floor)
float sdEarthWithHeightmap(vec3 p, vec3 center, float radius, vec3 poleDir, vec3 primeMeridianDir)
{
    // Base sphere distance (negative = inside sphere, positive = outside)
    // The base sphere represents sea level
    float baseDist = length(p - center) - radius;

    // Maximum possible terrain displacement (both up and down from sea level)
    float maxTerrainHeight = MAX_HEIGHT_DISPLACEMENT * radius;

    // Optimization: when far from the sphere, use conservative distance estimate
    // to avoid expensive heightmap sampling while ensuring we don't overstep terrain
    if (abs(baseDist) > maxTerrainHeight * 4.0)
    {
        // Conservative: assume tallest possible terrain, so we don't overstep
        return baseDist - maxTerrainHeight;
    }

    // Get direction from Earth center to sample point (surface direction)
    vec3 surfaceDir = normalize(p - center);

    // Build body frame for coordinate transformation
    vec3 north = normalize(poleDir);
    vec3 east = primeMeridianDir - dot(primeMeridianDir, north) * north;
    float eastLen = length(east);
    if (eastLen < 0.001)
    {
        east = (abs(north.y) < 0.9) ? normalize(cross(north, vec3(0.0, 1.0, 0.0)))
                                    : normalize(cross(north, vec3(1.0, 0.0, 0.0)));
    }
    else
    {
        east = east / eastLen;
    }
    vec3 south90 = cross(north, east);
    mat3 bodyFrame = mat3(east, south90, north);

    // Transform to body-fixed coordinates
    vec3 bodyDir = transpose(bodyFrame) * surfaceDir;

    // Transform to cubemap coordinates (J2000 Z-up to cubemap Y-up)
    vec3 cubemapDir = vec3(bodyDir.x, bodyDir.z, -bodyDir.y);

    // Sample heightmap (combined landmass + bathymetry)
    // 0.0 = Mariana Trench, SEA_LEVEL_NORMALIZED = sea level, 1.0 = Everest
    float heightSample = textureLod(earthHeightmapTexture, cubemapDir, 0.0).r;

    // === PROCEDURAL DETAIL NOISE ===
    // Add multi-octave noise to break up quantization artifacts
    // Apply more noise to terrain than ocean floor
    float noiseScale = 5000.0;
    vec3 noisePos = surfaceDir * noiseScale;
    float detailNoise = fbm(noisePos, 4, 2.0, 0.5) - 0.5;

    // Scale noise based on whether we're on land or ocean
    // More noise on elevated terrain, less in deep ocean
    float terrainFactor = max(heightSample - SEA_LEVEL_NORMALIZED, 0.0) / (1.0 - SEA_LEVEL_NORMALIZED);
    float noiseStrength = 0.05 + 0.1 * terrainFactor;
    float detailContribution = detailNoise * noiseStrength;

    // Combine base heightmap with procedural detail
    float combinedHeight = clamp(heightSample + detailContribution, 0.0, 1.0);

    // === ELEVATION DISPLACEMENT ===
    // Convert heightmap value to signed displacement relative to sea level
    // - heightSample = SEA_LEVEL_NORMALIZED (0.554) → displacement = 0 (at sphere surface)
    // - heightSample = 1.0 (Everest) → positive displacement (above sphere)
    // - heightSample = 0.0 (Mariana) → negative displacement (below sphere)

    // Calculate signed offset from sea level
    float elevationOffset = combinedHeight - SEA_LEVEL_NORMALIZED;

    // Scale to actual displacement
    // Above sea level: scale by (1.0 - SEA_LEVEL) range for mountains
    // Below sea level: scale by SEA_LEVEL range for ocean depths
    float heightDisplacement;
    if (elevationOffset >= 0.0)
    {
        // Mountains: elevationOffset / (1.0 - SEA_LEVEL) gives 0..1 range, scale to max height
        float mountainFraction = elevationOffset / (1.0 - SEA_LEVEL_NORMALIZED);
        heightDisplacement = mountainFraction * maxTerrainHeight * (1.0 - SEA_LEVEL_NORMALIZED) / 0.5;
    }
    else
    {
        // Ocean depths: elevationOffset / SEA_LEVEL gives -1..0 range, scale to max depth
        float oceanFraction = elevationOffset / SEA_LEVEL_NORMALIZED;
        heightDisplacement = oceanFraction * maxTerrainHeight * SEA_LEVEL_NORMALIZED / 0.5;
    }

    // The displaced surface: positive displacement = outward (mountains), negative = inward (ocean floor)
    // SDF = distance_to_center - (radius + heightDisplacement)
    return baseDist - heightDisplacement;
}

// ==================================
// Scene SDF
// ==================================
// Returns distance to nearest surface and outputs hit info
// For Earth, incorporates heightmap for terrain collision
float sceneSDF(vec3 p, out int hitID, out vec3 hitColor)
{
    float minDist = MAX_DIST;
    hitID = 0;
    hitColor = vec3(0.5);

    uint count = celestialData.objectCount;
    for (uint i = 0; i < count && i < 32u; ++i)
    {
        CelestialObjectGPU obj = celestialData.objects[i];

        float d;
        if (obj.naifId == NAIF_EARTH)
        {
            // Earth: use heightmap-displaced SDF for terrain collision
            d = sdEarthWithHeightmap(p, obj.position, obj.radius, obj.poleDirection, obj.primeMeridianDirection);
        }
        else
        {
            // Other bodies: simple sphere
            d = sdSphere(p, obj.position, obj.radius);
        }

        if (d < minDist)
        {
            minDist = d;
            hitID = obj.naifId;
            hitColor = obj.color;
        }
    }

    return minDist;
}

// Overload without hit info (for normal calculation)
float sceneSDF(vec3 p)
{
    int unused;
    vec3 unusedColor;
    return sceneSDF(p, unused, unusedColor);
}

// ==================================
// Normal Calculation
// ==================================
// Uses central differences with adaptive step size
vec3 calcNormal(vec3 p)
{
    // Use smaller step for more accurate normals on detailed terrain
    // Step should be small enough to capture terrain detail but not cause noise
    const float h = 0.0001;

    // Central difference gradient
    vec2 e = vec2(h, 0.0);
    return normalize(vec3(sceneSDF(p + e.xyy) - sceneSDF(p - e.xyy),
                          sceneSDF(p + e.yxy) - sceneSDF(p - e.yxy),
                          sceneSDF(p + e.yyx) - sceneSDF(p - e.yyx)));
}

// ==================================
// Ray Marching (Sphere Tracing with Surface Crossing Detection)
// ==================================
float rayMarch(vec3 ro, vec3 rd, out int hitID, out vec3 hitColor)
{
    float t = 0.0;
    hitID = 0;
    hitColor = vec3(0.5);

    float prevD = MAX_DIST;
    float prevT = 0.0;

    for (int i = 0; i < MAX_STEPS; i++)
    {
        vec3 p = ro + rd * t;
        float d = sceneSDF(p, hitID, hitColor);

        // Check for surface hit (close enough to surface)
        if (abs(d) < SURF_DIST)
        {
            return t;
        }

        // Detect surface crossing: sign change from positive to negative
        // means we've passed through the surface - need to bisect
        if (d < 0.0 && prevD > 0.0)
        {
            // Binary search to find precise intersection
            float tLow = prevT;
            float tHigh = t;
            for (int j = 0; j < 8; j++) // 8 bisection steps for precision
            {
                float tMid = (tLow + tHigh) * 0.5;
                vec3 pMid = ro + rd * tMid;
                float dMid = sceneSDF(pMid, hitID, hitColor);

                if (abs(dMid) < SURF_DIST)
                {
                    return tMid;
                }

                if (dMid > 0.0)
                {
                    tLow = tMid;
                }
                else
                {
                    tHigh = tMid;
                }
            }
            // Return best estimate after bisection
            return (tLow + tHigh) * 0.5;
        }

        // If we're inside (negative SDF) without a crossing detected,
        // step by absolute value to try to escape
        float stepSize = abs(d);

        // Enforce minimum step to prevent infinite loops,
        // but not too large to miss thin features
        stepSize = max(stepSize, MIN_STEP);

        // Save previous state for crossing detection
        prevD = d;
        prevT = t;

        // Step forward along ray
        t += stepSize;

        // Too far - no hit
        if (t > MAX_DIST)
        {
            hitID = 0;
            return MAX_DIST;
        }
    }

    hitID = 0;
    return MAX_DIST;
}

// ==================================
// Ray Direction
// ==================================
vec3 getRayDirection(vec2 uv, float fovDegrees, float aspectRatio)
{
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    ndc.x *= aspectRatio;

    // Clamp FOV to safe range to prevent infinity from tan()
    float safeFov = clamp(fovDegrees, 1.0, 179.0);
    float tanHalfFov = tan(radians(safeFov) * 0.5);
    vec3 rd = normalize(vec3(ndc * tanHalfFov, -1.0));

    mat3 camRot = transpose(mat3(pc.viewMatrix));
    return normalize(camRot * rd);
}

// ==================================
// Atomic Min Helper for Float (using integer bits)
// ==================================
// Updates minDistOut with the minimum of current and new value
// Uses atomic operations on the integer representation
void atomicMinDistance(float dist)
{
    if (dist <= 0.0 || dist > 1000000.0)
        return; // Ignore invalid distances

    uint newBits = floatBitsToUint(dist);

    // Atomic min for positive floats: smaller float = smaller uint when positive
    // This works because IEEE 754 positive floats have the same ordering as unsigned ints
    atomicMin(minDistOut.minDistanceBits, newBits);
}

// ==================================
// Sun Lighting Constants
// ==================================
const vec3 SUN_COLOR = vec3(1.0, 0.98, 0.95); // Slightly warm white sun color
const float SUN_BASE_INTENSITY = 1.0;         // Base intensity at 1 AU
const float AU_IN_DISPLAY_UNITS = 600.0;      // 1 AU = 600 display units (matches UNITS_PER_AU)
const int NAIF_SUN = 10;                      // Sun NAIF ID

// ==================================
// Heightmap/Parallax Utilities
// ==================================

// Sample heightmap and get raw height value (0-1 range)
// 0.0 = Mariana Trench, SEA_LEVEL_NORMALIZED = sea level, 1.0 = Everest
float sampleHeight(vec3 cubemapDir)
{
    // Use textureLod with LOD 0 - ray marching breaks automatic mipmap selection
    return textureLod(earthHeightmapTexture, cubemapDir, 0.0).r;
}

// Get signed displacement from sea level based on heightmap
// Returns: world-space displacement amount (positive = above sea level, negative = below)
float getHeightDisplacement(vec3 cubemapDir, float earthRadius)
{
    float height = sampleHeight(cubemapDir);

    // Convert to signed offset from sea level
    float elevationOffset = height - SEA_LEVEL_NORMALIZED;

    // Scale to world-space displacement
    float maxDisp = MAX_HEIGHT_DISPLACEMENT * earthRadius;

    if (elevationOffset >= 0.0)
    {
        // Mountains
        return elevationOffset / (1.0 - SEA_LEVEL_NORMALIZED) * maxDisp * (1.0 - SEA_LEVEL_NORMALIZED) / 0.5;
    }
    else
    {
        // Ocean depths
        return elevationOffset / SEA_LEVEL_NORMALIZED * maxDisp * SEA_LEVEL_NORMALIZED / 0.5;
    }
}

// ==================================
// Sun Position and Lighting Helpers
// ==================================

// Get sun position from SSBO
vec3 getSunPosition()
{
    for (uint i = 0u; i < celestialData.objectCount && i < 32u; ++i)
    {
        if (celestialData.objects[i].naifId == NAIF_SUN)
        {
            return celestialData.objects[i].position;
        }
    }
    return vec3(0.0); // Fallback to origin if sun not found
}

// Get sun color from SSBO (or default)
vec3 getSunColor()
{
    for (uint i = 0u; i < celestialData.objectCount && i < 32u; ++i)
    {
        if (celestialData.objects[i].naifId == NAIF_SUN)
        {
            return celestialData.objects[i].color;
        }
    }
    return SUN_COLOR; // Fallback to default sun color
}

// Calculate sun light intensity with inverse-square falloff
// Returns intensity multiplier based on distance to sun
float getSunIntensity(vec3 worldPos, vec3 sunPos)
{
    float dist = length(worldPos - sunPos);
    // Inverse-square law: I = I0 / (d^2)
    // Normalize so that 1 AU gives intensity 1.0
    float distAU = max(dist / AU_IN_DISPLAY_UNITS, 0.001); // Prevent division by zero
    return SUN_BASE_INTENSITY / (distAU * distAU);
}

// ==================================
// Body Frame Utilities (using SPICE data from SSBO)
// ==================================

// Build rotation matrix from SPICE-derived pole and prime meridian directions
// This transforms from world space to body-fixed coordinates
mat3 buildBodyFrame(vec3 poleDir, vec3 primeMeridianDir)
{
    // Pole direction is the Z-axis of the body-fixed frame (north pole)
    vec3 north = normalize(poleDir);

    // Prime meridian direction should be perpendicular to pole
    // Project it onto the equatorial plane and normalize
    vec3 east = primeMeridianDir - dot(primeMeridianDir, north) * north;
    float eastLen = length(east);
    if (eastLen < 0.001)
    {
        // Fallback if prime meridian is too close to pole
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
        east = east / eastLen;
    }

    // Complete the right-handed coordinate system
    vec3 south90 = cross(north, east);

    // Build rotation matrix (columns are body-frame axes in world coordinates)
    // To transform world -> body, we need the transpose (inverse of orthonormal)
    return mat3(east, south90, north);
}

// ==================================
// Earth Material Rendering (Native Cubemap with Height-Based Parallax)
// ==================================
// Uses hardware-accelerated cubemap sampling for all Earth textures
// Uses SPICE-derived pole and prime meridian from SSBO
// Applies parallax occlusion mapping based on actual heightmap values

// Sample Earth material and compute final color
// hitPoint: actual hit position on displaced terrain
// earthCenter: center of Earth
// terrainNormal: SDF gradient normal at hit point (includes terrain shape, for lighting)
vec3 sampleEarthMaterial(vec3 hitPoint,
                         vec3 earthCenter,
                         vec3 terrainNormal,
                         vec3 sunPos,
                         vec3 sunColor,
                         float sunIntensity,
                         vec3 poleDir,
                         vec3 primeMeridianDir,
                         float earthRadius)
{
    // Radial direction from Earth center to hit point
    vec3 radialDir = normalize(hitPoint - earthCenter);

    // View direction from hit point to camera
    vec3 viewDir = normalize(pc.cameraPosition - hitPoint);

    // Build body-fixed frame from SPICE data (J2000: Z-up, X toward prime meridian)
    mat3 bodyFrame = buildBodyFrame(poleDir, primeMeridianDir);

    // === PARALLAX OCCLUSION MAPPING ===
    // When looking at displaced terrain from an angle, we need to offset texture
    // coordinates based on the local height and view angle

    // Build tangent space at the hit point
    // Tangent points east, bitangent points north (in world space)
    vec3 tangent = normalize(bodyFrame[1]);
    vec3 bitangent = normalize(bodyFrame[2]);

    // Project onto tangent plane (perpendicular to radial direction)
    tangent = normalize(tangent - dot(tangent, radialDir) * radialDir);
    bitangent = normalize(cross(radialDir, tangent));

    // TBN matrix: tangent space to world space
    mat3 TBN = mat3(tangent, bitangent, radialDir);
    mat3 TBN_inv = transpose(TBN); // world to tangent space

    // View direction in tangent space
    vec3 viewDirTangent = normalize(TBN_inv * viewDir);

    // Initial cubemap direction (before parallax)
    vec3 bodyDir = transpose(bodyFrame) * radialDir;
    vec3 baseCubemapDir = vec3(bodyDir.x, bodyDir.z, -bodyDir.y);

    // Sample height at current location (LOD 0 for ray marching)
    // Heightmap: 0.0 = Mariana, SEA_LEVEL_NORMALIZED = sea level, 1.0 = Everest
    float heightSample = textureLod(earthHeightmapTexture, baseCubemapDir, 0.0).r;

    // Parallax offset calculation:
    // - Higher terrain (above sea level) needs positive offset
    // - Ocean depths (below sea level) need negative offset
    // - Grazing angles (small viewDirTangent.z) need more offset
    float parallaxScale = MAX_HEIGHT_DISPLACEMENT * 0.5;

    // Convert to signed height relative to sea level
    float signedHeight = heightSample - SEA_LEVEL_NORMALIZED;
    float heightOffset = signedHeight * parallaxScale;

    // Prevent division by zero for grazing angles, but allow significant offset
    float viewZ = max(abs(viewDirTangent.z), 0.1);
    vec2 parallaxOffset = viewDirTangent.xy * heightOffset / viewZ;

    // Apply parallax offset to the radial direction in tangent space
    // This shifts the texture sample point based on height and view angle
    vec3 offsetTangent = vec3(-parallaxOffset.x, -parallaxOffset.y, 0.0);
    vec3 offsetWorld = TBN * offsetTangent;

    // Create parallax-adjusted texture sample direction
    vec3 textureSampleDir = normalize(radialDir + offsetWorld);

    // Transform to cubemap coordinates
    vec3 parallaxBodyDir = transpose(bodyFrame) * textureSampleDir;
    vec3 cubemapDir = vec3(parallaxBodyDir.x, parallaxBodyDir.z, -parallaxBodyDir.y);

    // Sample base color texture with parallax-corrected coordinates
    // Use textureLod LOD 0 - ray marching breaks automatic mipmap selection
    vec3 baseColor = textureLod(earthColorTexture, cubemapDir, 0.0).rgb;

    // Sample and decode normal map with parallax-corrected coordinates
    vec3 normalSample = textureLod(earthNormalTexture, cubemapDir, 0.0).rgb;

    // Start with terrain normal from SDF gradient (captures heightmap shape)
    vec3 worldNormal = terrainNormal;

    // Blend in normal map details for micro-surface detail
    if (normalSample.r > 0.001 || normalSample.g > 0.001 || normalSample.b > 0.001)
    {
        // Decode tangent-space normal from texture (stored as 0-1, convert to -1 to 1)
        vec3 tangentNormal = normalSample * 2.0 - 1.0;
        // Transform to world space using the TBN at the hit point
        vec3 mapNormal = normalize(TBN * tangentNormal);
        // Blend terrain normal with normal map for combined detail
        worldNormal = normalize(terrainNormal + mapNormal * 0.3);
    }

    // Calculate direction to sun from hit point
    vec3 toSun = normalize(sunPos - hitPoint);

    // viewDir already computed above for parallax

    // === Diffuse Lighting ===
    // Use the detailed normal for micro-surface lighting
    float diffuseNdotL = max(dot(worldNormal, toSun), 0.0);

    // Use radial direction for global day/night determination (more stable terminator)
    float radialNdotL = dot(radialDir, toSun);

    // Soft terminator transition (twilight zone)
    float dayFactor = smoothstep(-0.1, 0.2, radialNdotL);

    // === Specular Lighting (Blinn-Phong) ===
    // Reduced reflectiveness for more realistic Earth appearance
    float specularMask = textureLod(earthSpecularTexture, cubemapDir, 0.0).r;
    vec3 halfDir = normalize(toSun + viewDir);
    float specNdotH = max(dot(worldNormal, halfDir), 0.0);
    float specular = pow(specNdotH, 128.0) * specularMask * 0.3; // Tight highlights, reduced intensity

    // === Night Lights ===
    float nightlights = textureLod(earthNightlightsTexture, cubemapDir, 0.0).r;
    vec3 nightlightColor = vec3(1.0, 0.9, 0.7) * nightlights * 3.0; // Warm city lights

    // === Combine Lighting ===
    // Apply sun color and intensity to lighting
    vec3 sunLight = sunColor * sunIntensity;

    // Ambient light (very dim, blueish for sky scatter approximation)
    vec3 ambientLight = vec3(0.02, 0.025, 0.04);

    // Day side: sun-lit diffuse + specular (reduced specular contribution)
    vec3 dayColor = baseColor * (ambientLight + sunLight * diffuseNdotL) + sunLight * specular * 0.15;

    // Night side: very dark base + city lights (no sun contribution)
    vec3 nightColor = baseColor * ambientLight * 0.1 + nightlightColor;

    // Blend between day and night based on sun angle
    vec3 finalColor = mix(nightColor, dayColor, dayFactor);

    return finalColor;
}

// ==================================
// Main
// ==================================
void main()
{
    // Safe aspect ratio calculation (avoid division by zero)
    float denom = pc.projectionMatrix[0][0];
    float aspect = (abs(denom) > 0.0001) ? pc.projectionMatrix[1][1] / denom : 1.0;
    vec3 ro = pc.cameraPosition;
    vec3 rd = getRayDirection(fragUV, pc.fov, aspect);

    // Early out if no objects to render - just show skybox
    if (celestialData.objectCount == 0u)
    {
        // Native cubemap sampling with HDR exposure
        vec3 skyColor = texture(skyboxCubemap, rd).rgb * SKYBOX_EXPOSURE;
        fragColor = vec4(skyColor, 1.0);
        gl_FragDepth = 1.0;
        return;
    }

    int hitID;
    vec3 hitColor;
    float t = rayMarch(ro, rd, hitID, hitColor);

    // Get sun position and properties for lighting calculations
    vec3 sunPos = getSunPosition();
    vec3 sunColor = getSunColor();

    vec3 color;
    float depth;

    if (t < MAX_DIST)
    {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p);

        // Direction from hit point to sun
        vec3 toSun = normalize(sunPos - p);

        // Sun intensity with inverse-square falloff
        float sunIntensity = getSunIntensity(p, sunPos);

        if (hitID == NAIF_SUN)
        {
            // Sun is emissive - no lighting calculation needed
            // Use its stored color directly with high intensity
            color = hitColor * 2.0;

            // Standard depth calculation for Sun
            vec4 clip = pc.projectionMatrix * pc.viewMatrix * vec4(p, 1.0);
            depth = clamp(clip.z / clip.w, 0.0, 1.0);
        }
        else if (hitID == NAIF_EARTH)
        {
            // Earth: use advanced material with textures
            // Note: p is already at the displaced terrain surface (SDF includes heightmap)

            // Find Earth's data from celestial objects SSBO
            vec3 earthCenter = vec3(0.0);
            float earthRadius = 1.0;
            vec3 earthPoleDir = vec3(0.0, 0.0, 1.0);       // J2000: Z-up
            vec3 earthPrimeMeridian = vec3(1.0, 0.0, 0.0); // J2000: X toward vernal equinox

            for (uint i = 0u; i < celestialData.objectCount && i < 32u; ++i)
            {
                if (celestialData.objects[i].naifId == NAIF_EARTH)
                {
                    earthCenter = celestialData.objects[i].position;
                    earthRadius = celestialData.objects[i].radius;
                    earthPoleDir = celestialData.objects[i].poleDirection;
                    earthPrimeMeridian = celestialData.objects[i].primeMeridianDirection;
                    break;
                }
            }

            // Terrain normal: SDF gradient captures the heightmap-displaced surface orientation
            vec3 terrainNormal = n; // n = calcNormal(p), already computed above

            // Sample Earth material textures using native cubemaps with parallax correction
            // Pass earthCenter so the function can compute parallax-corrected texture coordinates
            color = sampleEarthMaterial(p,
                                        earthCenter,
                                        terrainNormal,
                                        sunPos,
                                        sunColor,
                                        sunIntensity,
                                        earthPoleDir,
                                        earthPrimeMeridian,
                                        earthRadius);

            // Distance from camera to terrain surface (p is already at terrain)
            float distToSurface = length(pc.cameraPosition - p);

            // Write minimum distance to SSBO for camera collision detection
            atomicMinDistance(distToSurface);

            // Depth calculation uses actual hit position (terrain surface)
            vec4 clip = pc.projectionMatrix * pc.viewMatrix * vec4(p, 1.0);
            depth = clamp(clip.z / clip.w, 0.0, 1.0);
        }
        else
        {
            // Other celestial objects: diffuse lighting with sun color and intensity
            float diff = max(dot(n, toSun), 0.0);

            // Apply sun color and intensity falloff
            vec3 ambient = vec3(0.02); // Minimal ambient
            vec3 diffuseLight = sunColor * sunIntensity * diff;
            color = hitColor * (ambient + diffuseLight);

            // Standard depth calculation for non-Earth objects
            vec4 clip = pc.projectionMatrix * pc.viewMatrix * vec4(p, 1.0);
            depth = clamp(clip.z / clip.w, 0.0, 1.0);
        }
    }
    else
    {
        // Ray miss: sample skybox using native cubemap with HDR exposure
        color = texture(skyboxCubemap, rd).rgb * SKYBOX_EXPOSURE;
        depth = 1.0;
    }

    // Hover detection
    vec2 mouse = vec2(pc.mouseX, pc.mouseY);
    if (abs(fragUV.x - mouse.x) < 0.002 && abs(fragUV.y - mouse.y) < 0.002)
    {
        atomicExchange(hoverOut.hitMaterialID, t < MAX_DIST ? uint(hitID) : 0u);
    }

    fragColor = vec4(color, 1.0);
    gl_FragDepth = depth;
}
