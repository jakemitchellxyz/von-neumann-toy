#version 120
#extension GL_ARB_shader_texture_lod : enable

// Inputs from vertex shader
varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying vec2 vTexCoord; // Equirectangular UV from sphere geometry

// Uniforms
uniform sampler2D uColorTexture;
uniform sampler2D uColorTexture2;
uniform float uBlendFactor;
uniform sampler2D uNormalMap;
uniform sampler2D uHeightmap;        // Landmass heightmap (grayscale)
uniform sampler2D uNightlights;      // City lights grayscale texture
uniform sampler2D uMicroNoise;       // Fine-grained noise for per-second flicker
uniform sampler2D uHourlyNoise;      // Coarse noise for hourly variation
uniform sampler2D uSpecular;         // Surface specular/roughness (grayscale)
uniform sampler2D uIceMask;          // Ice coverage mask (current month)
uniform sampler2D uIceMask2;         // Ice coverage mask (next month)
uniform sampler2D uLandmassMask;     // Landmass mask (white=land, black=ocean)
uniform sampler2D uBathymetryDepth;  // Ocean floor depth (0=surface, 1=deepest ~11km)
uniform sampler2D uBathymetryNormal; // Ocean floor normal map
uniform sampler2D uCombinedNormal;   // Combined normal map (landmass + bathymetry) for shadows
uniform sampler2D uWindTexture1;     // Wind texture for current month (RG = u, v components)
uniform sampler2D uWindTexture2;     // Wind texture for next month (RG = u, v components)
uniform float uWindBlendFactor;      // Blend factor between current and next month (0-1)
uniform vec2 uWindTextureSize;       // Wind texture resolution (width, height) for UV normalization
uniform float uIceBlendFactor;       // Blend factor between ice masks (0-1)
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uMoonDir;   // Direction to moon from Earth
uniform vec3 uMoonColor; // Moonlight color and intensity (pre-multiplied)
uniform vec3 uAmbientColor;
uniform vec3 uPoleDir;          // Planet's north pole direction
uniform vec3 uPrimeMeridianDir; // Planet's prime meridian direction (for coordinate system)
uniform vec3 uCameraPos;        // Camera position for view direction calculations
uniform vec3 uCameraDir;        // Camera forward direction
uniform float uCameraFOV;       // Camera field of view (radians)
uniform int uUseNormalMap;      // 0 = disabled, 1 = enabled
uniform int uUseHeightmap;      // 0 = disabled, 1 = enabled
uniform int uUseSpecular;       // 0 = disabled, 1 = enabled
uniform float uTime;            // Julian date fraction for animated noise
uniform int uFlatCircleMode;    // 1 = rendering flat circle for distant sphere, 0 = normal sphere
uniform vec3 uSphereCenter;     // Sphere center position (for flat circle projection)
uniform float uSphereRadius;    // Sphere radius (for flat circle projection)
uniform vec3 uBillboardCenter;  // Billboard center position (closest point on sphere to camera)

// Constants
const float PI = 3.14159265359;


// Ray-sphere intersection - returns the intersection on the same hemisphere as the billboard
// ro: ray origin (camera position)
// rd: ray direction (normalized)
// center: sphere center
// radius: sphere radius
// Returns: intersection distance along ray, or -1.0 if no intersection
// The billboard is always at the closest point on sphere to camera, so we need to use
// the intersection point on the same hemisphere as that closest point
float raySphereIntersect(vec3 ro, vec3 rd, vec3 center, float radius)
{
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;

    if (disc < 0.0)
    {
        return -1.0; // No intersection
    }

    float h = sqrt(disc);
    float t0 = -b - h; // Entry point (closest to camera - always use this for near side)
    float t1 = -b + h; // Exit point (farthest from camera - far side, ignore)

    // Always use the near intersection (t0) to ensure we sample the near side of the sphere
    // This prevents flipping when viewing from different angles relative to the sun
    if (t0 > 0.0)
        return t0;
    return -1.0; // No valid intersection
}

// Manual atan2 implementation for GLSL 120 compatibility
// Returns angle in range [-π, π]
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
// Uses the same coordinate system as the C++ code (sphere's local coordinate system)
// dir: normalized direction vector in world space
// poleDir: planet's north pole direction (normalized)
// primeMeridianDir: planet's prime meridian direction (for computing east vector)
// Returns: equirectangular UV coordinates (u: 0-1 maps to longitude -π to +π, v: 0-1 maps to latitude +π/2 to -π/2)
vec2 directionToEquirectUV(vec3 dir, vec3 poleDir, vec3 primeMeridianDir)
{
    // Build the same coordinate system as C++ code (exact match)
    // north = poleDir (already normalized)
    vec3 north = poleDir;

    // Compute east vector exactly as C++ code does:
    // east = primeDir - dot(primeDir, north) * north
    vec3 east = primeMeridianDir - dot(primeMeridianDir, north) * north;
    float eastLen = length(east);
    if (eastLen < 0.001)
    {
        // Degenerate case - use same fallback as C++ code
        if (abs(north.y) < 0.9)
        {
            east = cross(north, vec3(0.0, 1.0, 0.0));
        }
        else
        {
            east = cross(north, vec3(1.0, 0.0, 0.0));
        }
        eastLen = length(east);
    }
    east = east / eastLen;

    // Compute south90 = cross(north, east) - this is the third basis vector
    vec3 south90 = cross(north, east);

    // Project direction vector onto sphere's local coordinate system (same as C++ code)
    float localX = dot(dir, east);
    float localY = dot(dir, north);
    float localZ = dot(dir, south90);

    // Normalize (should already be normalized, but ensure)
    float len = sqrt(localX * localX + localY * localY + localZ * localZ);
    if (len < 0.001)
    {
        return vec2(0.5, 0.5); // Degenerate case
    }
    localX /= len;
    localY /= len;
    localZ /= len;

    // Convert to latitude/longitude using same formula as C++ code
    // Latitude: asin(localY) - angle from equator to north pole
    float latitude = asin(clamp(localY, -1.0, 1.0));

    // Longitude: atan2(localZ, localX) - angle around north pole from east
    float longitude = atan2_manual(localZ, localX);

    // Convert lat/lon to equirectangular UV (same as C++ latLonToUV)
    float u = (longitude / PI + 1.0) * 0.5; // 0 to 1
    float v = 0.5 - (latitude / PI);        // 0 to 1

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

// Decode normal map sample with UV coordinate swapping pattern
// The normal map has U and V components swapped every 180 degrees (checkerboard pattern)
// This function detects the pattern and swaps back to get the correct normal
vec3 decodeSwappedNormalMap(vec4 sample, vec2 texUV)
{
    // Determine which quadrant we're in (0-3) based on 180-degree boundaries
    int uQuadrant = int(floor(texUV.x * 2.0));
    int vQuadrant = int(floor(texUV.y * 2.0));

    // Check if this quadrant has swapped coordinates (checkerboard pattern)
    // Use mod() function instead of % operator for GLSL 120 compatibility
    float quadrantSum = float(uQuadrant + vQuadrant);
    bool swapUV = mod(quadrantSum, 2.0) >= 1.0;

    // Decode from [0,1] to [-1,1]
    vec3 normalTangent;
    if (swapUV)
    {
        // Normal map has (V, U, Z) - swap back to (U, V, Z)
        normalTangent.x = sample.g * 2.0 - 1.0;   // U from V slot
        normalTangent.y = (sample.r * 2.0 - 1.0); // V from U slot
        normalTangent.z = sample.b * 2.0 - 1.0;   // Z unchanged
    }
    else
    {
        // Normal order: (U, V, Z)
        normalTangent.x = sample.r * 2.0 - 1.0;
        normalTangent.y = (sample.g * 2.0 - 1.0);
        normalTangent.z = sample.b * 2.0 - 1.0;
    }

    return normalTangent;
}

float getDayNightFactor(vec3 surfaceNormal, vec3 lightDir)
{
    float NdotL = dot(surfaceNormal, lightDir);
    return 1.0 - smoothstep(-0.2, 0.34, NdotL);
}

// Ocean Surface Simulation - Physically Based

// Simple hash for procedural effects
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Smooth value noise
float noise2D(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Fractal Brownian Motion for wave generation
// GLSL 120 requires compile-time constant loop bounds, so we use a fixed 3 octaves
float fbm(vec2 p, int octaves)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    // Unroll loop for GLSL 120 compatibility (max 3 octaves)
    value += amplitude * noise2D(p * frequency);
    amplitude *= 0.5;
    frequency *= 2.0;

    if (octaves > 1)
    {
        value += amplitude * noise2D(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    if (octaves > 2)
    {
        value += amplitude * noise2D(p * frequency);
    }

    return value;
}

// Compute wind-based noise offset for localized wave propagation
// Samples the wind texture and returns an offset vector in UV space
// The offset is the inverse of the wind direction, scaled by wind speed and time
// This creates localized differences in noise sampling based on wind direction
// Time-dependent accumulation creates visible wave movement
vec2 computeWindNoiseOffset(vec2 uv, float time)
{
    // Convert equirectangular UV to sinusoidal UV before sampling
    vec2 windUV = toSinusoidalUV(uv);
    // Add half-pixel offset for proper sampling at pixel centers
    windUV = windUV + 0.5 / uWindTextureSize;

    // Sample both wind textures
    // Wind texture format: GL_LUMINANCE_ALPHA stores R=u (red/LUMINANCE) and G=v (green/ALPHA)
    // Sample red presence (R channel) for u direction and green presence (G channel) for v direction
    // GL_LUMINANCE_ALPHA format samples as (L, L, L, A), so we use .ra to get (u, v)
    vec4 windSample1Full = texture2D(uWindTexture1, windUV);
    vec4 windSample2Full = texture2D(uWindTexture2, windUV);
    // Extract u component from red/LUMINANCE channel, v component from green/ALPHA channel
    vec2 windSample1 = vec2(windSample1Full.r, windSample1Full.a); // .ra = (u, v) from LUMINANCE_ALPHA
    vec2 windSample2 = vec2(windSample2Full.r, windSample2Full.a); // .ra = (u, v) from LUMINANCE_ALPHA

    // Blend between the two months
    vec2 windSample = mix(windSample1, windSample2, uWindBlendFactor);

    // Wind values are normalized: [-1, 1] range (from [-50, 50] m/s)
    // Convert from [0, 1] to actual wind speed in m/s
    // Combine u (red) and v (green) components to produce directional vector
    vec2 windSpeedMS = (windSample * 2.0 - 1.0) * 50.0; // Wind speed in m/s (range: -50 to +50)

    // Compute wind magnitude for scaling
    float windMagnitudeMS = length(windSpeedMS); // Wind speed magnitude in m/s

    // If wind is significant, compute the inverse direction offset
    // The offset is the inverse of the wind direction, scaled by wind speed and time
    // This creates localized noise sampling differences based on wind
    // Using the inverse direction means we sample noise "upwind" to create wave propagation effect
    // Time-dependent accumulation creates visible wave movement over time
    if (windMagnitudeMS > 1.0)
    {
        // Normalize to get direction
        vec2 windDirection = windSpeedMS / windMagnitudeMS;

        // Convert wind speed from m/s to UV units per second
        // Earth's circumference at equator: ~40,000 km = 40,000,000 m
        // In UV space (sinusoidal projection), 1.0 UV unit ≈ 40,000 km at equator
        const float EARTH_CIRCUMFERENCE_M = 40000000.0;
        const float UV_TO_METERS = EARTH_CIRCUMFERENCE_M;

        // Convert wind speed to UV space: m/s → UV units/s
        float windSpeedUV = windMagnitudeMS / UV_TO_METERS;

        // Accumulate offset over time to create visible movement
        // Scale factor amplifies the effect for natural wave movement
        // Typical wind speeds: 5-15 m/s, storms up to 20-30 m/s
        // Wave propagation speed is typically slower than wind speed (about 60-80% of wind speed)
        // Use a scale factor to create visible but natural wave movement
        // The scale factor accounts for the fact that waves propagate slower than wind
        // and amplifies the effect to be visible at the scale of the Earth
        float secondsInDay = time * 86400.0;
        float offsetScale =
            windSpeedUV * secondsInDay * 25.0; // Scale factor for natural wave movement (amplified for visibility)

        // Use inverse direction (opposite of wind direction) for noise offset
        // This creates wave propagation in the direction the wind is blowing
        vec2 noiseOffset = -windDirection * offsetScale;

        return noiseOffset;
    }
    else
    {
        // No significant wind - return zero offset
        return vec2(0.0);
    }
}

// Compute noise gradient at a given UV position
// Returns the gradient vector (direction of steepest ascent in noise)
vec2 computeNoiseGradient(vec2 uv, float scale)
{
    float eps = 0.001;
    float noiseCenter = fbm(uv * scale, 3);
    float noiseX = fbm((uv + vec2(eps, 0.0)) * scale, 3);
    float noiseY = fbm((uv + vec2(0.0, eps)) * scale, 3);

    // Compute gradient (finite differences)
    vec2 gradient = vec2((noiseX - noiseCenter) / eps, (noiseY - noiseCenter) / eps);
    return normalize(gradient);
}

// ============================================================
// Shoreline SDF Functions
// ============================================================
// Consolidated SDF helpers are defined in signed-distance-fields.glsl
// These functions are duplicated here since GLSL 120 doesn't support #include
// For future use: see src/materials/helpers/signed-distance-fields.glsl

// Compute approximate Signed Distance Field (SDF) from landmass mask
// Returns distance to nearest shoreline (positive in ocean, negative in land)
// Uses gradient-based approximation for efficiency
float computeShorelineSDF(vec2 uv)
{
    // Sample mask (1.0 = land, 0.0 = ocean)
    float maskValue = texture2D(uLandmassMask, uv).r;

    // Narrow-band optimization: early exit for deep empty/solid regions
    if (maskValue < 0.01)
    {
        return 1.0; // Deep ocean - far from boundary
    }
    if (maskValue > 0.99)
    {
        return -1.0; // Deep land - far from boundary
    }

    // Near boundary - compute approximate SDF using gradient magnitude
    float eps = 0.003; // Sampling offset for gradient computation

    // Sample mask at nearby points to compute gradient
    float maskX = texture2D(uLandmassMask, uv + vec2(eps, 0.0)).r;
    float maskY = texture2D(uLandmassMask, uv + vec2(0.0, eps)).r;
    float maskXNeg = texture2D(uLandmassMask, uv + vec2(-eps, 0.0)).r;
    float maskYNeg = texture2D(uLandmassMask, uv + vec2(0.0, -eps)).r;

    // Compute gradient magnitude (how quickly mask changes)
    vec2 gradient = vec2((maskX - maskXNeg) / (2.0 * eps), (maskY - maskYNeg) / (2.0 * eps));
    float gradientMag = length(gradient);

    // Approximate distance to boundary using gradient magnitude
    float approximateDist = 0.0;
    if (gradientMag > 0.01)
    {
        float distToBoundary = abs(maskValue - 0.5) / gradientMag;
        approximateDist = distToBoundary;
    }
    else
    {
        approximateDist = abs(maskValue - 0.5) * 10.0; // Rough estimate
    }

    // Convert to signed distance (positive in ocean, negative in land)
    return maskValue < 0.5 ? approximateDist : -approximateDist;
}

// Compute shoreline normal from SDF gradient
// Returns normalized direction pointing from land toward ocean (shoreline normal)
vec2 computeShorelineNormal(vec2 uv)
{
    // Compute SDF gradient using finite differences
    float eps = 0.003;

    float sdfCenter = computeShorelineSDF(uv);
    float sdfX = computeShorelineSDF(uv + vec2(eps, 0.0));
    float sdfY = computeShorelineSDF(uv + vec2(0.0, eps));

    // Compute gradient of SDF
    vec2 sdfGradient = vec2((sdfX - sdfCenter) / eps, (sdfY - sdfCenter) / eps);

    // Normalize gradient to get normal
    float gradientLen = length(sdfGradient);
    if (gradientLen < 0.01)
    {
        return vec2(0.0, 0.0); // Not near a boundary
    }

    // Normalize to get unit normal (points from land toward ocean)
    return sdfGradient / gradientLen;
}

// Apply shoreline reflection to wave trajectory using SDF-based boundary detection
vec2 applyShorelineReflection(vec2 trajectory, vec2 uv, float reflectionStrength)
{
    // Compute SDF at current location
    float sdf = computeShorelineSDF(uv);

    // Narrow-band optimization: only process near boundaries
    float reflectionThreshold = 0.05; // UV units

    if (abs(sdf) > reflectionThreshold)
    {
        return trajectory; // Too far from shoreline
    }

    // Must be in ocean (positive SDF) to reflect
    if (sdf <= 0.0)
    {
        return trajectory; // On land - no reflection
    }

    // Get shoreline normal from SDF gradient
    vec2 shorelineNormal = computeShorelineNormal(uv);

    float normalLen = length(shorelineNormal);
    if (normalLen < 0.01)
    {
        return trajectory; // Not near shoreline
    }

    // Check if trajectory is pointing toward land
    float dotProduct = dot(trajectory, shorelineNormal);

    if (dotProduct < 0.0)
    {
        // Wave is approaching land - reflect it
        vec2 reflectedTrajectory = trajectory - 2.0 * dotProduct * shorelineNormal;

        // Compute reflection strength based on proximity to shoreline
        float proximityFactor = 1.0 - smoothstep(0.0, reflectionThreshold, sdf);

        // Blend between original and reflected trajectory
        vec2 blendedTrajectory = normalize(mix(trajectory, reflectedTrajectory, reflectionStrength * proximityFactor));

        // Apply damping near boundaries to prevent energy buildup
        float dampingFactor = clamp(sdf / reflectionThreshold, 0.3, 1.0);

        return blendedTrajectory * dampingFactor;
    }

    // Wave is moving away from land - no reflection needed
    return trajectory;
}

// Compute base trajectory vector (shared "current trajectory tensor")
// This stores a 2D momentum vector for every point on the surface
// Wind acts as a momentum modifier - accumulates over time
// Low wind values maintain existing momentum, large values change momentum dramatically
vec2 computeBaseTrajectory(vec2 uv, float time)
{
    // Sample wind textures (two 2D textures for current and next month, blend between them)
    // Convert equirectangular UV to sinusoidal UV before sampling
    vec2 windUV = toSinusoidalUV(uv);
    // Normalize UV coordinates for wind texture resolution
    // Add half-pixel offset for proper sampling at pixel centers
    // This accounts for the actual texture resolution (1024x512) for accurate sampling
    windUV = windUV + 0.5 / uWindTextureSize;

    // Sample both wind textures
    // Note: Textures are stored as GL_LUMINANCE_ALPHA, which samples as (L, L, L, A)
    // So we use .ra to get (LUMINANCE=u wind, ALPHA=v wind) = (u, v) force vector
    vec4 windSample1Full = texture2D(uWindTexture1, windUV);
    vec4 windSample2Full = texture2D(uWindTexture2, windUV);
    vec2 windSample1 = vec2(windSample1Full.r, windSample1Full.a); // .ra = (u, v) from LUMINANCE_ALPHA
    vec2 windSample2 = vec2(windSample2Full.r, windSample2Full.a); // .ra = (u, v) from LUMINANCE_ALPHA

    // Blend between the two months
    vec2 windSample = mix(windSample1, windSample2, uWindBlendFactor);

    // Wind values are normalized: [-1, 1] range (from [-50, 50] m/s)
    // Convert from [0, 1] to actual wind speed in m/s
    // This is the wind force vector that modifies momentum
    vec2 windForceMS = (windSample * 2.0 - 1.0) * 50.0; // Wind force in m/s (range: -50 to +50)

    // Convert wind force to UV space for momentum accumulation
    // Earth's circumference at equator: ~40,000 km = 40,000,000 m
    const float EARTH_CIRCUMFERENCE_M = 40000000.0;
    const float UV_TO_METERS = EARTH_CIRCUMFERENCE_M;
    vec2 windForceUV = windForceMS / UV_TO_METERS; // Wind force in UV units per second

    // Accumulate momentum over time: momentum = wind_force * time_scale
    // This simulates momentum accumulation - low wind = small changes, high wind = large changes
    // Time scale controls how quickly momentum accumulates (smaller = slower accumulation)
    // Increased accumulation rate to make medium waves more responsive and less static
    float momentumTimeScale = 0.02; // Accumulation rate (increased from 0.01 for better medium wave response)
    float secondsInDay = time * 86400.0;
    vec2 accumulatedMomentum = windForceUV * secondsInDay * momentumTimeScale;

    // Use accumulated momentum as trajectory
    // Low wind values result in small momentum (maintains existing direction)
    // Large wind values result in large momentum (changes direction dramatically)
    vec2 windTrajectory = accumulatedMomentum;

    // Apply shoreline reflection - waves bounce off shorelines
    // Reflection strength: 0.15 = subtle reflection near shorelines
    // Normalize for reflection calculation, then restore magnitude
    float momentumMagnitude = length(windTrajectory);
    if (momentumMagnitude > 0.001)
    {
        vec2 normalizedTrajectory = windTrajectory / momentumMagnitude;
        vec2 reflectedTrajectory = applyShorelineReflection(normalizedTrajectory, uv, 0.15);
        windTrajectory = reflectedTrajectory * momentumMagnitude; // Restore magnitude after reflection
    }

    return windTrajectory;
}

// Compute rotation factor for a specific layer by sampling noise
// Returns rotation factor from -1 to 1
float computeLayerRotationFactor(vec2 uv, float scale, float time, vec2 noiseSpaceOffset)
{
    // Sample noise to get rotation factor from -1 to 1
    // This determines how much to rotate from the current trajectory
    float noiseValue = fbm((uv + noiseSpaceOffset) * scale, 3);
    float rotationFactor = noiseValue * 2.0 - 1.0; // Map from [0,1] to [-1,1]

    return rotationFactor;
}

// Compute trajectory vector at a given UV position
// Pure wind-based trajectory without noise distortions
// Waves move based solely on wind direction and accumulated momentum
// Scale-dependent momentum accumulation: medium waves accumulate more for better visibility
vec2 computeTrajectory(vec2 uv,
                       float scale,
                       float time,
                       vec2 noiseSpaceOffset,
                       vec2 largeDistortion,
                       vec2 mediumDistortion,
                       vec2 smallDistortion,
                       float largeWeight,
                       float mediumWeight,
                       float smallWeight)
{
    // Get shared base trajectory (pure wind direction, no noise)
    vec2 windTrajectory = computeBaseTrajectory(uv, time);

    // Scale momentum accumulation based on wave scale
    // Medium waves (scale ~100) need more accumulation to appear less static
    // Small waves (scale ~200+) accumulate naturally, large waves (scale ~50) accumulate less
    // Normalize scale to create accumulation multiplier: medium waves get ~2x, small waves ~1x, large waves ~0.5x
    float scaleNormalized = scale / 100.0;                    // Normalize to medium wave scale
    float momentumMultiplier = 1.0 + scaleNormalized * 0.5;   // Range: 0.5x to 1.5x based on scale
    momentumMultiplier = clamp(momentumMultiplier, 0.5, 2.0); // Clamp to reasonable range

    // Apply scale-dependent momentum accumulation
    // Medium waves accumulate more momentum, making them more responsive to wind
    windTrajectory = windTrajectory * momentumMultiplier;

    // Return pure wind trajectory without adding noise-based distortions
    // This ensures advection is based solely on wind direction and accumulated momentum
    return windTrajectory;
}

// Compute directional distortion for a specific layer
// Samples noise to get rotation factor and computes how it rotates the current trajectory
// Returns the directional change (distortion) that should be added to the trajectory tensor
vec2 computeLayerDirectionalDistortion(vec2 uv, float scale, float time, vec2 noiseSpaceOffset, vec2 currentTrajectory)
{
    // Get rotation factor from noise sampling (-1 to 1)
    float rotationFactor = computeLayerRotationFactor(uv, scale, time, noiseSpaceOffset);

    // Rotation factor interpretation:
    // 0 = use same force as current trajectory (no rotation)
    // 1 = rotate 90 degrees right from current trajectory
    // -1 = rotate 90 degrees left from current trajectory
    // -0.5 = rotate 45 degrees left from current trajectory
    float rotationAngle = rotationFactor * PI * 0.5; // -90° to +90° range

    // Rotate current trajectory by the computed angle
    float cosA = cos(rotationAngle);
    float sinA = sin(rotationAngle);
    vec2 rotatedTrajectory = vec2(currentTrajectory.x * cosA - currentTrajectory.y * sinA,
                                  currentTrajectory.x * sinA + currentTrajectory.y * cosA);

    // Return the directional distortion (difference from current trajectory)
    return normalize(rotatedTrajectory) - currentTrajectory;
}

// Helper function to compute anisotropic wave height with advection
// Samples noise at current location and advected location (along trajectory)
// This creates the progressive force system with locally variable drift
// Each layer uses a distinct noise space offset to sample from different "image databases"
// Trajectory is constrained to the tangent plane to ensure it always points upward
// Considers wave-perturbed normal occlusion from sun on dark side
// Uses shared trajectory tensor with accumulated directional distortions
float computeAnisotropicWave(vec2 uv,
                             vec2 dir,
                             float scale,
                             vec2 timeOffset,
                             float timeScale,
                             float time,
                             vec2 noiseSpaceOffset,
                             vec3 surfaceNormal,
                             vec3 tangent,
                             vec3 bitangent,
                             vec3 sunDirection,
                             vec2 largeDistortion,
                             vec2 mediumDistortion,
                             vec2 smallDistortion,
                             float largeWeight,
                             float mediumWeight,
                             float smallWeight)
{
    // Get trajectory vector for this location
    // Uses shared base trajectory (accumulated momentum) without noise distortions
    vec2 trajectory2D = computeTrajectory(uv,
                                          scale,
                                          time,
                                          noiseSpaceOffset,
                                          largeDistortion,
                                          mediumDistortion,
                                          smallDistortion,
                                          largeWeight,
                                          mediumWeight,
                                          smallWeight);

    // Preserve momentum magnitude before normalization (needed for speed calculation)
    float momentumMagnitude = length(trajectory2D);

    // Convert 2D trajectory to 3D space using tangent and bitangent
    vec3 trajectory3D = tangent * trajectory2D.x + bitangent * trajectory2D.y;

    // Project trajectory onto tangent plane (remove component along surface normal)
    // This ensures the trajectory always points along the surface, never into the sphere
    float normalComponent = dot(trajectory3D, surfaceNormal);
    vec3 trajectoryTangent = trajectory3D - surfaceNormal * normalComponent;

    // Normalize the projected trajectory for direction
    float trajectoryLen = length(trajectoryTangent);
    if (trajectoryLen > 0.001)
    {
        trajectoryTangent = trajectoryTangent / trajectoryLen;
    }
    else
    {
        // Fallback: use tangent direction if trajectory is degenerate
        trajectoryTangent = tangent;
        momentumMagnitude = 0.0; // No momentum if degenerate
    }

    // Convert back to 2D tangent space
    // This ensures the trajectory is constrained to the tangent plane
    // Direction is normalized, but we preserve magnitude separately for speed
    vec2 trajectory = vec2(dot(trajectoryTangent, tangent), dot(trajectoryTangent, bitangent));

    // Store momentum magnitude as a separate variable for speed calculation
    // This represents accumulated wind force - affects both direction and speed

    // Compute preliminary wave-perturbed normal to check occlusion
    // Use advection-based gradient computation to match the wave height advection
    // This ensures the normal perturbation accurately represents the advected wave state

    // Compute advection for gradient (same as wave computation)
    // Physics-based wave propagation using realistic ocean wave speeds
    float secondsInDay = time * 86400.0;

    // Physics constants:
    // - Earth's circumference at equator: ~40,000 km = 40,000,000 m
    // - In UV space (sinusoidal projection), 1.0 UV unit ≈ 40,000 km at equator
    // - Typical ocean wave speeds: 7.8-23.4 m/s (for periods 5-15s)
    // - Wind speeds: 5-15 m/s typical, up to 20-30 m/s in storms
    // - Wave phase velocity: c ≈ 1.56 × T (m/s) for deep water waves

    // Convert wave speed from m/s to UV units per second
    // 1 UV unit = 40,000,000 m, so 1 m/s = 1/40,000,000 UV units/s
    const float EARTH_CIRCUMFERENCE_M = 40000000.0;   // meters
    const float UV_TO_METERS = EARTH_CIRCUMFERENCE_M; // 1 UV unit = circumference in meters

    // Typical ocean wave period: 8-12 seconds (moderate sea state)
    // Wave speed: c = 1.56 × T ≈ 12.5-18.7 m/s for typical waves
    // Use accumulated momentum magnitude to scale wave speed
    // Momentum magnitude represents accumulated wind force - larger momentum = faster waves
    // momentumMagnitude is preserved from trajectory calculation above

    // Base wave speed: typical ocean wave ~12 m/s (period ~7.7s)
    // Scale by momentum magnitude - this represents how much wind has accumulated
    // Low momentum (low wind) = slower waves, high momentum (high wind) = faster waves
    // Convert momentum magnitude (in UV units) back to m/s for speed calculation
    // (EARTH_CIRCUMFERENCE_M and UV_TO_METERS already declared above)
    float momentumMS = momentumMagnitude * UV_TO_METERS; // Convert UV momentum to m/s

    // Base speed scales with momentum magnitude
    // Normalize momentum to reasonable range: 0-30 m/s typical wind range
    float normalizedMomentum = clamp(momentumMS / 30.0, 0.0, 1.0);
    float baseWaveSpeedMS = 12.0;                                                     // m/s (typical ocean wave speed)
    float windInfluencedSpeedMS = baseWaveSpeedMS * (0.5 + normalizedMomentum * 1.0); // 6-24 m/s range

    // Vary speed based on wave scale: larger waves (smaller scale) move slower, smaller waves (larger scale) move faster
    // Scale factor: larger scale values mean smaller waves, so they should move faster
    // Inverse relationship: speed increases with scale
    // Typical scale values range from ~1.0 (large waves) to ~10.0+ (small waves)
    // Use a power curve to create natural speed variation: speed ∝ scale^power
    float scaleSpeedFactor = pow(scale / 5.0, 1.5); // Normalize to ~5.0, then apply power curve
    // Clamp to reasonable range: 0.3x to 2.0x speed variation
    scaleSpeedFactor = clamp(scaleSpeedFactor, 0.3, 2.0);

    // Apply scale-based speed variation
    float scaleAdjustedSpeedMS = windInfluencedSpeedMS * scaleSpeedFactor;

    // Convert to UV space: m/s → UV units/s
    float waveSpeedUV = scaleAdjustedSpeedMS / UV_TO_METERS; // UV units per second

    // Advection accumulates over time to create progressive wave propagation
    // The advection distance is the distance the wave has traveled
    // Larger waves (smaller scale) have less advection, smaller waves (larger scale) have more advection
    float advectionDistance = waveSpeedUV * secondsInDay; // UV units

    // Advect backwards: sample noise where the wave "came from"
    // This creates proper wave propagation with momentum - the noise encodes the advected state
    vec2 advectionVector = -trajectory * advectionDistance;

    // Check if advected location hits a shoreline using SDF
    // This creates realistic wave bouncing when waves hit shorelines
    vec2 advectedUVForReflection = uv + advectionVector;
    float sdfAtAdvected = computeShorelineSDF(advectedUVForReflection);

    // Reflection threshold: reflect if within this distance of shoreline
    float reflectionThreshold = 0.05; // UV units

    // If advected location is on land or very close to shore, reflect the advection vector
    if (sdfAtAdvected <= reflectionThreshold)
    {
        // Get shoreline normal at advected location using SDF gradient
        vec2 shorelineNormal = computeShorelineNormal(advectedUVForReflection);
        float normalLen = length(shorelineNormal);

        if (normalLen > 0.01)
        {
            // Reflect advection vector off shoreline
            vec2 advectionDir = normalize(advectionVector);
            float dotProduct = dot(advectionDir, shorelineNormal);

            if (dotProduct < 0.0)
            {
                // Advection is pointing toward land - reflect it
                // Standard reflection formula: R = I - 2 * dot(I, N) * N
                vec2 reflectedDir = advectionDir - 2.0 * dotProduct * shorelineNormal;

                // Compute reflection strength based on SDF proximity
                // Closer to boundary = stronger reflection
                float proximityFactor = 1.0 - smoothstep(0.0, reflectionThreshold, max(0.0, sdfAtAdvected));

                // Blend between original and reflected advection
                // Use subtle reflection: 0.2 instead of 0.8 for more natural wave behavior
                vec2 blendedDir = normalize(mix(advectionDir, reflectedDir, proximityFactor * 0.2));

                // Apply damping to prevent energy buildup at boundaries
                float dampingFactor = clamp(max(0.0, sdfAtAdvected) / reflectionThreshold, 0.2, 1.0);

                advectionVector = blendedDir * advectionDistance * dampingFactor;
            }
        }
    }

    // Sample wave state at advected locations for gradient
    float eps = 0.001;
    vec2 advectedUVCenter = uv + advectionVector;
    vec2 advectedUVX = uv + vec2(eps, 0.0) + advectionVector;
    vec2 advectedUVY = uv + vec2(0.0, eps) + advectionVector;

    // Compute wind-based noise offset for localized wave propagation
    // This creates localized differences in noise sampling based on wind direction
    // Time-dependent accumulation creates visible wave movement
    vec2 windNoiseOffset = computeWindNoiseOffset(uv, time);

    // Apply anisotropic distortion
    float projCenter = dot(advectedUVCenter * scale, dir);
    float projX = dot(advectedUVX * scale, dir);
    float projY = dot(advectedUVY * scale, dir);

    vec2 anisotropicUVCenter = (advectedUVCenter + noiseSpaceOffset + windNoiseOffset) * scale + dir * projCenter * 0.3;
    vec2 anisotropicUVX = (advectedUVX + noiseSpaceOffset + windNoiseOffset) * scale + dir * projX * 0.3;
    vec2 anisotropicUVY = (advectedUVY + noiseSpaceOffset + windNoiseOffset) * scale + dir * projY * 0.3;

    vec2 advectedTimeOffset = timeOffset * timeScale - advectionVector * scale * 0.2;

    float waveCenter = fbm(anisotropicUVCenter + advectedTimeOffset, 3);
    float waveX = fbm(anisotropicUVX + advectedTimeOffset, 3);
    float waveY = fbm(anisotropicUVY + advectedTimeOffset, 3);
    vec2 waveGradient = vec2((waveX - waveCenter) / eps, (waveY - waveCenter) / eps);

    // Compute wave-perturbed normal (preliminary estimate)
    float waveStrength = 0.05;
    vec3 wavePerturbedNormal =
        surfaceNormal + tangent * waveGradient.x * waveStrength + bitangent * waveGradient.y * waveStrength;
    wavePerturbedNormal = normalize(wavePerturbedNormal);

    // Check if wave-perturbed surface is occluded from sun (dark side)
    // If the perturbed normal faces away from sun, reduce anisotropic effect
    float sunDotPerturbedNormal = dot(sunDirection, wavePerturbedNormal);
    float occlusionFactor = max(0.0, sunDotPerturbedNormal);        // 0 = fully occluded, 1 = fully lit
    occlusionFactor = smoothstep(-0.2, 0.3, sunDotPerturbedNormal); // Smooth transition at terminator

    // Advection-based wave propagation:
    // The wave state is advected (moved) in the direction of the wind-modified trajectory
    // We sample noise at the advected location to see where the wave "came from"
    // This creates proper wave propagation with momentum - the noise encodes the advected state
    // Reuse the advection variables computed above for gradient computation

    // Sample noise at the advected location (where the wave state came from)
    // This represents the wave state that has been advected by the wind
    // The noise is no longer random - it encodes the wave state that has propagated
    vec2 advectedUV = uv + advectionVector;

    // Apply anisotropic distortion based on wave direction
    // Include wind-based noise offset for localized wave propagation effects
    float projAdvected = dot(advectedUV * scale, dir);
    vec2 anisotropicUVAdvected = (advectedUV + noiseSpaceOffset + windNoiseOffset) * scale + dir * projAdvected * 0.3;

    // Sample the advected wave state
    // The time offset creates animation, but we also account for advection in the noise space
    // This ensures the wave state properly propagates and updates over time
    // Reuse advectedTimeOffset computed above (same advection vector)
    float waveState = fbm(anisotropicUVAdvected + advectedTimeOffset, 3);

    // The wave state now represents advected noise that has been moved by wind
    // This creates proper wave propagation in the direction of the wind-modified trajectory
    // The noise encodes the updated/advected wave state, not random values

    // Modulate by occlusion factor - reduce anisotropic effect on dark side
    // This predicts occlusion based on wave-perturbed normal
    return waveState * occlusionFactor;
}

// Compute a single wave layer with specific scale and time speed
// Returns noise value proportional to the layer's scale
// Each layer uses a distinct noise space offset to create independent flow directions
// Trajectories are constrained to the tangent plane
// Considers wave-perturbed normal occlusion from sun
// Uses shared trajectory tensor with accumulated directional distortions
float computeWaveLayer(vec2 texUV,
                       float scale,
                       float timeSpeed,
                       float time,
                       vec2 waveDir1,
                       vec2 waveDir2,
                       float amplitude,
                       vec2 noiseSpaceOffset,
                       vec3 surfaceNormal,
                       vec3 tangent,
                       vec3 bitangent,
                       vec3 sunDirection,
                       vec2 largeDistortion,
                       vec2 mediumDistortion,
                       vec2 smallDistortion,
                       float largeWeight,
                       float mediumWeight,
                       float smallWeight)
{
    // Convert Julian date fraction to seconds
    float secondsInDay = time * 86400.0;

    // Time offset based on layer speed
    vec2 timeOffset = vec2(secondsInDay * timeSpeed, secondsInDay * timeSpeed * 0.7);

    // Compute wave height with advection for both directions
    // Each direction uses the same noise space offset for consistency
    // Trajectories are constrained to tangent plane to ensure upward flow
    // Uses shared trajectory tensor with accumulated directional distortions
    float wave1 = computeAnisotropicWave(texUV,
                                         waveDir1,
                                         scale,
                                         timeOffset,
                                         1.0,
                                         time,
                                         noiseSpaceOffset,
                                         surfaceNormal,
                                         tangent,
                                         bitangent,
                                         sunDirection,
                                         largeDistortion,
                                         mediumDistortion,
                                         smallDistortion,
                                         largeWeight,
                                         mediumWeight,
                                         smallWeight);
    float wave2 = computeAnisotropicWave(texUV,
                                         waveDir2,
                                         scale * 1.5,
                                         timeOffset,
                                         0.8,
                                         time,
                                         noiseSpaceOffset,
                                         surfaceNormal,
                                         tangent,
                                         bitangent,
                                         sunDirection,
                                         largeDistortion,
                                         mediumDistortion,
                                         smallDistortion,
                                         largeWeight,
                                         mediumWeight,
                                         smallWeight);

    // Combine directions and scale by amplitude (proportional to layer scale)
    float combinedWave = (wave1 * 0.7 + wave2 * 0.3) * amplitude;

    return combinedWave;
}

// Generate anisotropic wave normal perturbation for water surfaces
// Creates subtle wave patterns that are direction-dependent (like real waves)
// Uses three layers compounding from large to small: large (hours) -> medium (minutes) -> small (seconds)
// Considers wave-perturbed normal occlusion from sun on dark side
// Uses shared trajectory tensor with noise-sampled rotation factors
vec3 computeWaveNormal(vec2 texUV, vec3 surfaceNormal, vec3 tangent, vec3 bitangent, float time, vec3 sunDirection)
{
    // Anisotropic wave direction (waves tend to flow in one direction)
    // Primary wave direction (dominant wind/wave direction)
    vec2 waveDir1 = normalize(vec2(1.0, 0.5));
    // Secondary wave direction (cross-waves)
    vec2 waveDir2 = normalize(vec2(0.7, 1.0));

    // Base amplitude - larger waves have larger amplitude
    float baseAmplitude = 1.0;

    // Each layer uses a distinct noise space offset to create independent flow directions
    // These offsets act as separate "image databases" for each layer
    vec2 largeNoiseOffset = vec2(100.0, 200.0);  // Large wave noise space
    vec2 mediumNoiseOffset = vec2(300.0, 400.0); // Medium wave noise space
    vec2 smallNoiseOffset = vec2(500.0, 600.0);  // Small wave noise space

    // Layer scales
    float largeScale = 30.0;
    float mediumScale = 100.0;
    float smallScale = 200.0;

    // Directional distortion weights - larger layers have more pronounced effect
    float largeWeight = 1.0;  // Most pronounced
    float mediumWeight = 0.6; // Moderate
    float smallWeight = 0.3;  // Less pronounced

    // Get shared base trajectory (current trajectory tensor starting point)
    vec2 baseTrajectory = computeBaseTrajectory(texUV, time);

    // Compute directional distortions for each layer
    // Each layer samples noise to get rotation factor and rotates the base trajectory
    // All distortions are computed relative to the base trajectory, then accumulated
    vec2 largeDistortion = computeLayerDirectionalDistortion(texUV, largeScale, time, largeNoiseOffset, baseTrajectory);
    vec2 mediumDistortion =
        computeLayerDirectionalDistortion(texUV, mediumScale, time, mediumNoiseOffset, baseTrajectory);
    vec2 smallDistortion = computeLayerDirectionalDistortion(texUV, smallScale, time, smallNoiseOffset, baseTrajectory);

    // Layer 3: Large waves (wavefronts) - much larger noise, moving very slowly over hours
    // Start with largest scale - this is the base
    float largeSpeed = 0.0001; // Moves very slowly over hours (1/10000th speed - much slower than small waves)
    float largeAmplitude = baseAmplitude * 0.5; // Largest amplitude
    float combinedWave = computeWaveLayer(texUV,
                                          largeScale,
                                          largeSpeed,
                                          time,
                                          waveDir1,
                                          waveDir2,
                                          largeAmplitude,
                                          largeNoiseOffset,
                                          surfaceNormal,
                                          tangent,
                                          bitangent,
                                          sunDirection,
                                          largeDistortion,
                                          mediumDistortion,
                                          smallDistortion,
                                          largeWeight,
                                          mediumWeight,
                                          smallWeight);

    // Layer 2: Medium waves - larger scale, moving slowly over minutes
    // Add medium waves on top of large waves (different noise space = different angle)
    float mediumSpeed = 0.01;                    // Moves slowly over minutes (1/100th speed)
    float mediumAmplitude = baseAmplitude * 0.3; // Medium amplitude
    combinedWave += computeWaveLayer(texUV,
                                     mediumScale,
                                     mediumSpeed,
                                     time,
                                     waveDir1,
                                     waveDir2,
                                     mediumAmplitude,
                                     mediumNoiseOffset,
                                     surfaceNormal,
                                     tangent,
                                     bitangent,
                                     sunDirection,
                                     largeDistortion,
                                     mediumDistortion,
                                     smallDistortion,
                                     largeWeight,
                                     mediumWeight,
                                     smallWeight);

    // Layer 1: Small waves - fine detail, moving every second
    // Add small waves on top (different noise space = different angle)
    float smallSpeed = 0.1;                     // Moves every second (1000x faster than large waves)
    float smallAmplitude = baseAmplitude * 0.2; // Small amplitude
    combinedWave += computeWaveLayer(texUV,
                                     smallScale,
                                     smallSpeed,
                                     time,
                                     waveDir1,
                                     waveDir2,
                                     smallAmplitude,
                                     smallNoiseOffset,
                                     surfaceNormal,
                                     tangent,
                                     bitangent,
                                     sunDirection,
                                     largeDistortion,
                                     mediumDistortion,
                                     smallDistortion,
                                     largeWeight,
                                     mediumWeight,
                                     smallWeight);

    // Layer 0: Shoreline waves - very small waves that repeatedly approach shorelines
    // These create the characteristic "lapping" effect near coasts
    float shorelineWaveScale = smallScale * 3.0;         // Even smaller waves (higher scale = smaller waves)
    float shorelineWaveAmplitude = baseAmplitude * 0.15; // Moderate amplitude for visibility

    // Compute shoreline proximity and direction
    float sdf = computeShorelineSDF(texUV);
    float shorelineProximity = 1.0 - smoothstep(0.0, 0.08, max(0.0, sdf)); // Fade out beyond 0.08 UV units

    // Only add shoreline waves if we're in the ocean and near a shoreline
    if (sdf > 0.0 && shorelineProximity > 0.01)
    {
        // Get direction toward shoreline (from ocean toward land)
        vec2 shorelineNormal = computeShorelineNormal(texUV);
        float normalLen = length(shorelineNormal);

        if (normalLen > 0.01)
        {
            // Normalize shoreline normal (points from land toward ocean, so negate for toward-shore direction)
            vec2 towardShore = -normalize(shorelineNormal);

            // Create oscillating approach pattern using time-based sine wave
            // Waves approach the shore, then recede, creating repeated lapping effect
            float secondsInDay = time * 86400.0;
            float approachFrequency = 0.5; // Frequency of approach/recede cycles (cycles per second)
            float approachPhase = sin(secondsInDay * approachFrequency * 2.0 * PI);

            // Create asymmetric pattern: strong approach, weak recede
            // Use absolute value with stronger positive phase for realistic lapping
            float approachStrength = (0.7 + 0.3 * approachPhase) * shorelineProximity;
            // Clamp to ensure waves always have some movement toward shore
            approachStrength = max(0.3, approachStrength);

            // Create time offset that moves toward the shore
            vec2 shorelineTimeOffset =
                vec2(secondsInDay * 0.2 * approachStrength, secondsInDay * 0.2 * approachStrength * 0.7);

            // Sample noise with shoreline-specific offset
            vec2 shorelineNoiseOffset = vec2(0.3, 0.7); // Different noise space for shoreline waves
            vec2 shorelineUV = (texUV + shorelineNoiseOffset) * shorelineWaveScale;

            // Add directional component toward shore for wave pattern
            float projTowardShore = dot(shorelineUV, towardShore);
            vec2 anisotropicShorelineUV = shorelineUV + towardShore * projTowardShore * 0.5;

            // Sample wave pattern
            float shorelineWave = fbm(anisotropicShorelineUV + shorelineTimeOffset, 3);

            // Scale by approach strength and proximity
            float shorelineWaveHeight = shorelineWave * approachStrength * shorelineWaveAmplitude;

            // Add to combined wave
            combinedWave += shorelineWaveHeight;
        }
    }

    // Calculate wave gradients using finite differences
    // Sample neighboring points to compute gradient efficiently
    float eps = 0.001; // Small offset for gradient calculation

    // Compute gradients for each layer additively (compound from large to small)
    // Each layer samples from its own noise space, creating distinct angles
    float waveX = 0.0;
    float waveY = 0.0;

    // Large layer gradient (distinct noise space, constrained to tangent plane)
    float largeX = computeWaveLayer(texUV + vec2(eps, 0.0),
                                    largeScale,
                                    largeSpeed,
                                    time,
                                    waveDir1,
                                    waveDir2,
                                    largeAmplitude,
                                    largeNoiseOffset,
                                    surfaceNormal,
                                    tangent,
                                    bitangent,
                                    sunDirection,
                                    largeDistortion,
                                    mediumDistortion,
                                    smallDistortion,
                                    largeWeight,
                                    mediumWeight,
                                    smallWeight);
    float largeY = computeWaveLayer(texUV + vec2(0.0, eps),
                                    largeScale,
                                    largeSpeed,
                                    time,
                                    waveDir1,
                                    waveDir2,
                                    largeAmplitude,
                                    largeNoiseOffset,
                                    surfaceNormal,
                                    tangent,
                                    bitangent,
                                    sunDirection,
                                    largeDistortion,
                                    mediumDistortion,
                                    smallDistortion,
                                    largeWeight,
                                    mediumWeight,
                                    smallWeight);
    waveX += largeX;
    waveY += largeY;

    // Medium layer gradient (additive, distinct noise space = different angle, constrained to tangent plane)
    float mediumX = computeWaveLayer(texUV + vec2(eps, 0.0),
                                     mediumScale,
                                     mediumSpeed,
                                     time,
                                     waveDir1,
                                     waveDir2,
                                     mediumAmplitude,
                                     mediumNoiseOffset,
                                     surfaceNormal,
                                     tangent,
                                     bitangent,
                                     sunDirection,
                                     largeDistortion,
                                     mediumDistortion,
                                     smallDistortion,
                                     largeWeight,
                                     mediumWeight,
                                     smallWeight);
    float mediumY = computeWaveLayer(texUV + vec2(0.0, eps),
                                     mediumScale,
                                     mediumSpeed,
                                     time,
                                     waveDir1,
                                     waveDir2,
                                     mediumAmplitude,
                                     mediumNoiseOffset,
                                     surfaceNormal,
                                     tangent,
                                     bitangent,
                                     sunDirection,
                                     largeDistortion,
                                     mediumDistortion,
                                     smallDistortion,
                                     largeWeight,
                                     mediumWeight,
                                     smallWeight);
    waveX += mediumX;
    waveY += mediumY;

    // Small layer gradient (additive, distinct noise space = different angle, constrained to tangent plane)
    float smallX = computeWaveLayer(texUV + vec2(eps, 0.0),
                                    smallScale,
                                    smallSpeed,
                                    time,
                                    waveDir1,
                                    waveDir2,
                                    smallAmplitude,
                                    smallNoiseOffset,
                                    surfaceNormal,
                                    tangent,
                                    bitangent,
                                    sunDirection,
                                    largeDistortion,
                                    mediumDistortion,
                                    smallDistortion,
                                    largeWeight,
                                    mediumWeight,
                                    smallWeight);
    float smallY = computeWaveLayer(texUV + vec2(0.0, eps),
                                    smallScale,
                                    smallSpeed,
                                    time,
                                    waveDir1,
                                    waveDir2,
                                    smallAmplitude,
                                    smallNoiseOffset,
                                    surfaceNormal,
                                    tangent,
                                    bitangent,
                                    sunDirection,
                                    largeDistortion,
                                    mediumDistortion,
                                    smallDistortion,
                                    largeWeight,
                                    mediumWeight,
                                    smallWeight);
    waveX += smallX;
    waveY += smallY;

    // Compute gradient (finite difference approximation)
    // This uses the exact same noise values that created combinedWave
    vec2 gradient = vec2((waveX - combinedWave) / eps, (waveY - combinedWave) / eps);

    // Overall wave strength - subtle effect
    float waveStrength = 0.08;

    // Perturb normal using gradient from the exact same noise
    // The gradient tells us which direction the surface slopes
    // Using the same noise ensures the normal matches the wave shape exactly, creating depth
    // The final anisotropic angle is produced by compounding all layers from large to small
    vec3 waveNormal = surfaceNormal + tangent * gradient.x * waveStrength + bitangent * gradient.y * waveStrength;

    return normalize(waveNormal);
}


// Legacy function for compatibility
float getAtmosphericPathMultiplier(float NdotL)
{
    // Simple approximation for backward compatibility
    float cosAngle = max(NdotL, 0.01);
    float pathLength = min(1.0 / cosAngle, 40.0);
    return exp(-0.05 * (pathLength - 1.0));
}

float getWaterPathMultiplier(float NdotL, float depthMeters)
{
    float cosAngle = max(NdotL, 0.1);
    float sinAir = sqrt(1.0 - cosAngle * cosAngle);
    float sinWater = sinAir / 1.33;
    float cosWater = sqrt(1.0 - sinWater * sinWater);
    return depthMeters / max(cosWater, 0.3);
}


// Compute combined normal map from landmass (heightmap + normal) and ocean (bathymetry normal)
// This creates a global normal map that includes both land and ocean features
// Used for casting shadows from landmass onto ocean floor
vec3 computeCombinedNormalMap(vec2 texUV, vec3 surfaceNormal, vec3 tangent, vec3 bitangent)
{
    // Sample landmass mask
    float landMask = texture2DLod(uLandmassMask, texUV, 0.0).r;
    float oceanMask = 1.0 - landMask;

    // Initialize combined normal to base sphere normal
    vec3 combinedNormal = surfaceNormal;

    // Add landmass normal contribution (heightmap + normal map)
    if (landMask > 0.01 && uUseNormalMap == 1)
    {
        // Sample landmass normal map with UV swapping pattern
        vec4 normalSample = texture2DLod(uNormalMap, texUV, 0.0);
        vec3 landNormalTangent = decodeSwappedNormalMap(normalSample, texUV);

        // Build TBN matrix for landmass
        mat3 landTBN = mat3(tangent, bitangent, surfaceNormal);
        vec3 landNormalWorld = normalize(landTBN * landNormalTangent);

        // Blend landmass normal with base normal based on land mask
        combinedNormal = normalize(mix(combinedNormal, landNormalWorld, landMask));
    }

    // Add ocean bathymetry normal contribution
    if (oceanMask > 0.01 && uUseNormalMap == 1)
    {
        // Sample bathymetry normal map with UV swapping pattern
        vec4 normalSample = texture2DLod(uBathymetryNormal, texUV, 0.0);
        vec3 bathymetryNormalTangent = decodeSwappedNormalMap(normalSample, texUV);

        // Build TBN matrix for bathymetry (same as landmass)
        mat3 bathymetryTBN = mat3(tangent, bitangent, surfaceNormal);
        vec3 bathymetryNormalWorld = normalize(bathymetryTBN * bathymetryNormalTangent);

        // Blend bathymetry normal with combined normal based on ocean mask
        combinedNormal = normalize(mix(combinedNormal, bathymetryNormalWorld, oceanMask));
    }

    return combinedNormal;
}

// =========================================================================
// Texture Sampling and Effect Application
// =========================================================================

// Structure to hold sampled texture values
struct TextureSamples
{
    float height;       // Heightmap value [0,1]
    vec3 normalTangent; // Normal map in tangent space [-1,1]
    float roughness;    // Roughness value [0,1] (0=smooth/shiny, 1=rough/matte)
};

// Sample all landmass textures at the given UV coordinate
// All textures use the same UV mapping (sinusoidal, same orientation as color textures)
// ALL textures are REQUIRED - always sample them, no fallbacks
TextureSamples sampleLandmassTextures(vec2 texUV, float oceanMask, vec3 worldPos)
{
    TextureSamples samples;

    // ALWAYS sample all textures - they are required
    samples.height = texture2D(uHeightmap, texUV).r;

    // For ocean areas, use flat normal (no Perlin noise)
    if (oceanMask > 0.01)
    {
        // Use flat normal for water (no perturbation)
        samples.normalTangent = vec3(0.0, 0.0, 1.0);
        // Water is smooth/shiny (low roughness)
        samples.roughness = 0.1;
    }
    else
    {
        // For land areas, sample the normal map texture with UV swapping pattern
        vec4 normalSample = texture2D(uNormalMap, texUV);
        samples.normalTangent = decodeSwappedNormalMap(normalSample, texUV);
        // Sample roughness texture for land (already masked, ocean = 0)
        // Texture is inverted: lighter = less rough, darker = rougher
        // If specular is disabled or texture not loaded, use default roughness
        if (uUseSpecular == 1)
        {
            // Sample roughness texture using same UV coordinates as other textures
            vec4 roughnessSample = texture2D(uSpecular, texUV);
            samples.roughness = roughnessSample.r;
        }
        else
        {
            // Default roughness when texture is disabled
            samples.roughness = 0.5;
        }
    }

    return samples;
}

// Apply texture effects to base color and compute final surface properties
// Returns: modified color and final normal
struct SurfaceProperties
{
    vec3 color;      // Base color with heightmap effects applied
    vec3 normal;     // Final normal (with normal map applied if enabled)
    float roughness; // Surface roughness [0,1] for specular calculations
};

SurfaceProperties applyTextureEffects(vec3 baseColor,         // Base color from color textures
                                      TextureSamples samples, // Sampled texture values
                                      vec3 surfaceNormal,     // Base sphere normal
                                      vec3 tangent,           // Tangent vector (east)
                                      vec3 bitangent,         // Bitangent vector (north)
                                      float landMask,         // Land mask [0,1]
                                      float iceCoverage,      // Ice coverage [0,1]
                                      float iceAlbedo,        // Ice albedo value
                                      vec3 sunDir             // Sun direction (for heightmap lightening modulation)
)
{
    SurfaceProperties props;

    // Apply heightmap effect to base color (higher elevations = brighter)
    // CRITICAL: Modulate by sun angle - areas facing away from sun get no lightening,
    // areas facing toward sun get full effect
    vec3 heightModulatedColor = baseColor;
    if (uUseHeightmap == 1)
    {
        // Calculate sun angle: dot product of surface normal with sun direction
        // NdotL = 1.0 when sun is overhead, 0.0 at terminator, <0.0 on night side
        float NdotL = dot(surfaceNormal, sunDir);
        NdotL = max(0.0, NdotL); // Clamp to [0,1] - no lightening on night side

        // Sample heightmap and apply elevation-based lightening
        float elevation = samples.height;
        float heightFactor = elevation * 0.3;                     // Scale factor for height effect
        heightModulatedColor += heightFactor * NdotL * vec3(1.0); // Brighten based on elevation and sun angle
    }

    // Apply normal map if enabled
    vec3 finalNormal = surfaceNormal;
    if (uUseNormalMap == 1)
    {
        vec3 normalTangent = samples.normalTangent;
        mat3 TBN = mat3(tangent, bitangent, surfaceNormal);
        finalNormal = normalize(TBN * normalTangent);
    }

    // Apply ice coverage if present
    vec3 finalColor = heightModulatedColor;
    float finalRoughness = samples.roughness;

    if (iceCoverage > 0.01)
    {
        finalColor = mix(finalColor, vec3(iceAlbedo), iceCoverage);

        // Make ice regions more matte (higher roughness) at the center
        // Inverse effect: ice was shiny, now make it matte/diffuse
        // Higher iceCoverage (center of ice regions) = higher roughness (more matte)
        // Ice should reflect all light diffusely, not specularly
        float iceRoughness = 0.9;    // Very matte/rough for diffuse reflection
        float iceRoughnessMin = 0.7; // Minimum roughness even at edges

        // Interpolate roughness based on ice coverage
        // Center of ice (iceCoverage = 1.0) gets maximum roughness (0.9)
        // Edges of ice (iceCoverage = 0.01) get minimum roughness (0.7)
        float iceRoughnessValue = mix(iceRoughnessMin, iceRoughness, iceCoverage);

        // Blend between base roughness and ice roughness based on ice coverage
        finalRoughness = mix(finalRoughness, iceRoughnessValue, iceCoverage);
    }

    props.color = finalColor;
    props.normal = finalNormal;
    props.roughness = finalRoughness;

    return props;
}

void main()
{
    // If rendering flat circle mode, reconstruct sphere position from ray-sphere intersection
    // The billboard is not at the center of the planet, so we need to cast a ray from
    // the camera through each fragment and intersect with the sphere
    vec3 actualWorldPos = vWorldPos;
    vec3 actualWorldNormal = vWorldNormal;
    vec2 texCoord = vTexCoord; // Local copy for texture coordinates

    if (uFlatCircleMode == 1)
    {
        // In flat circle mode, vWorldPos is the position on the flat billboard plane
        // (computed in vertex shader). We need to cast a ray from the camera through
        // this fragment position and intersect it with the sphere to get the correct
        // surface point and UV coordinates.

        // Calculate ray from camera through fragment position on billboard
        vec3 rayOrigin = uCameraPos;
        vec3 rayDir = normalize(vWorldPos - uCameraPos);

        // Intersect ray with sphere
        float t = raySphereIntersect(rayOrigin, rayDir, uSphereCenter, uSphereRadius);

        if (t > 0.0)
        {
            // Calculate intersection point on sphere surface
            vec3 intersectionPoint = rayOrigin + rayDir * t;

            // Calculate direction from sphere center to intersection point
            vec3 direction = normalize(intersectionPoint - uSphereCenter);

            // Use intersection point as actual world position
            actualWorldPos = intersectionPoint;

            // Surface normal is radial from sphere center
            // This varies per-fragment across the billboard, creating depth illusion
            actualWorldNormal = direction;

            // Calculate correct UV coordinates from intersection point
            // Use the same coordinate system as C++ code (sphere's local coordinate system)
            vec2 equirectUV = directionToEquirectUV(direction, uPoleDir, uPrimeMeridianDir);
            // Update texture coordinates (note: v coordinate needs to be flipped)
            texCoord = vec2(equirectUV.x, 1.0 - equirectUV.y);
        }
        else
        {
            // Ray doesn't intersect sphere (shouldn't happen for visible fragments)
            // Fallback: use direction from sphere center to fragment position
            vec3 direction = normalize(vWorldPos - uSphereCenter);
            actualWorldPos = uSphereCenter + direction * uSphereRadius;
            actualWorldNormal = direction;
            vec2 equirectUV = directionToEquirectUV(direction, uPoleDir, uPrimeMeridianDir);
            texCoord = vec2(equirectUV.x, 1.0 - equirectUV.y);
        }
    }

    // Use the updated texture coordinates (may have been modified in flat circle mode)
    vec2 texUV = toSinusoidalUV(texCoord);

    // Calculate view direction (needed for specular calculations)
    vec3 viewDir = normalize(uCameraPos - actualWorldPos);

    // Sample color textures and blend
    vec4 col1 = texture2D(uColorTexture, texUV);
    vec4 col2 = texture2D(uColorTexture2, texUV);
    vec4 baseColor = mix(col1, col2, uBlendFactor);

    // Land/Ocean Mask
    float landMask = texture2D(uLandmassMask, texUV).r;
    float oceanMask = 1.0 - landMask; // Invert for ocean areas

    // Sample ice masks and blend between months for smooth seasonal transition
    float iceMask1 = texture2D(uIceMask, texUV).r;
    float iceMask2 = texture2D(uIceMask2, texUV).r;
    float iceCoverage = mix(iceMask1, iceMask2, uIceBlendFactor);

    // Ice has higher reflectance (shinier) than regular land
    // Use red channel from source color as ice albedo (approximates higher SWIR reflectance)
    vec4 sourceCol = mix(col1, col2, uBlendFactor); // Use same month blend
    float iceAlbedo = sourceCol.r;                  // Red channel = high reflectance for ice

    // Sample all landmass textures using centralized function
    // Ocean areas use flat normal (no Perlin noise)
    TextureSamples textureSamples = sampleLandmassTextures(texUV, oceanMask, actualWorldPos);

    // Get the interpolated surface normal (normalized sphere normal = outward direction)
    vec3 surfaceNormal = normalize(actualWorldNormal);

    // Compute tangent frame using the planet's pole direction
    // Tangent = east direction = cross(poleDir, surfaceNormal)
    vec3 tangent = cross(uPoleDir, surfaceNormal);
    float tangentLen = length(tangent);
    if (tangentLen < 0.001)
    {
        // At poles, tangent is degenerate - use arbitrary direction
        tangent = vec3(1.0, 0.0, 0.0);
    }
    else
    {
        tangent = tangent / tangentLen;
    }

    // Bitangent = north direction = cross(surfaceNormal, tangent)
    vec3 bitangent = cross(surfaceNormal, tangent);

    // Calculate sun direction for heightmap lightening modulation
    vec3 L = normalize(uLightDir);

    // Apply texture effects using centralized function
    SurfaceProperties surfaceProps = applyTextureEffects(baseColor.rgb,
                                                         textureSamples,
                                                         surfaceNormal,
                                                         tangent,
                                                         bitangent,
                                                         landMask,
                                                         iceCoverage,
                                                         iceAlbedo,
                                                         L); // Pass sun direction for heightmap modulation

    // Create final color with effects applied
    vec4 finalAlbedo = vec4(surfaceProps.color, baseColor.a);

    // Sample nightlights (grayscale - city lights intensity)
    float lightsIntensity = texture2D(uNightlights, texUV).r;

    // Use the final normal from texture effects (already computed in applyTextureEffects)
    vec3 N = surfaceProps.normal;

    // Apply procedural wave effects to water surfaces
    // Create anisotropic wave patterns that animate over time
    if (oceanMask > 0.01)
    {
        // Compute wave-perturbed normal for water
        // Pass sun direction to consider occlusion on dark side
        vec3 L = normalize(uLightDir);
        vec3 waveNormal = computeWaveNormal(texUV, N, tangent, bitangent, uTime, L);

        // Blend between base normal and wave normal based on ocean mask
        // Use smooth transition to avoid artifacts at land/water boundaries
        float waveBlend = smoothstep(0.01, 0.3, oceanMask);
        N = normalize(mix(N, waveNormal, waveBlend));
    }

    // Calculate day/night factor using sphere normal (not bump-mapped normal)
    // This gives consistent day/night across the surface
    // L is already calculated above for heightmap modulation
    float nightFactor = getDayNightFactor(surfaceNormal, L);

    // Atmospheric Attenuation of Sunlight
    // This creates realistic sunset colors only when viewing the sun's reflection

    // Calculate reflection direction: sunlight reflected off surface normal
    // This is the direction sunlight bounces after hitting the surface
    vec3 sunReflectDir = reflect(-L, N);

    // Check if reflected sunlight points toward camera
    // sunReflectDotView = 1.0 when reflection points directly at camera, 0.0 at 90°, <0.0 beyond 90°
    float sunReflectDotView = dot(normalize(sunReflectDir), -viewDir);

    // Only apply atmospheric scattering when reflected sunlight points toward camera
    // Use a tight threshold: only when reflection is within ~30° of camera (cos(30°) ≈ 0.866)
    // Fade from full effect at 0° (directly at camera) to no effect at 30°
    // This ensures the gradient only appears when viewing the sun's reflection
    float atmosphericFactor = 0.0;
    if (sunReflectDotView > 0.866)
    { // Only when within 30° of camera
        // Map from [0.866, 1.0] to [0.0, 1.0] with smooth fade
        atmosphericFactor = smoothstep(0.866, 1.0, sunReflectDotView);
    }

    // Solar lighting (only affects day side) - apply atmospheric transmittance
    float NdotL = dot(N, L); // Don't clamp yet - need raw value for shadow check
    float NdotLClamped = max(NdotL, 0.0);

    // Get roughness value for PBR calculations
    float roughness = surfaceProps.roughness;

    // Physically Based Rendering: Roughness controls light scattering
    // Smooth surfaces (low roughness): tight specular highlights, less diffuse scattering
    // Rough surfaces (high roughness): wide scattered highlights, more diffuse scattering

    // Calculate diffuse lighting (affected by roughness - rougher surfaces scatter more)
    // In PBR, rougher surfaces have more diffuse scattering but less specular intensity
    float diffuseFactor = 1.0; // Base diffuse factor
    vec3 solarDiffuse = uLightColor * NdotLClamped * diffuseFactor;

    // Specular Reflection with PBR
    // Roughness controls how light scatters: smooth = tight specular, rough = wide scattered
    vec3 specularReflection = vec3(0.0);

    // Apply specular reflection when surface is lit by sun
    // Roughness will control the size and intensity of the specular highlight
    // CRITICAL: Only apply specular when surface actually faces the light source
    // NdotL > 0 ensures the surface normal points toward the light
    if (NdotL > 0.0)
    {
        // Physically Based Rendering (PBR) using Cook-Torrance/GGX model
        // Specular BRDF = (D * G * F) / (4 * (N·L) * (N·V))
        // Roughness controls light scattering: low = tight specular, high = wide scattered

        float NdotV = max(dot(N, viewDir), 0.001);

        // Half-vector between light and view directions
        // L is direction FROM light TO surface, so -L is FROM surface TO light
        // viewDir is FROM surface TO camera
        vec3 lightDirToSurface = -L; // Convert to FROM surface TO light
        vec3 H = normalize(lightDirToSurface + viewDir);
        float NdotH = dot(N, H);

        // CRITICAL: Half-vector must point toward surface normal for valid specular reflection
        // If NdotH < 0, the half-vector points away from surface (physically invalid)
        // This prevents specular highlights on surfaces facing away from light
        if (NdotH <= 0.0)
        {
            // Surface is facing away from the reflection direction - no specular
            specularReflection = vec3(0.0);
        }
        else
        {
            // Valid specular reflection - proceed with PBR calculation
            NdotH = max(NdotH, 0.001); // Clamp for numerical stability
            float VdotH = max(dot(viewDir, H), 0.001);

            // Convert roughness to alpha (squared for GGX)
            float alpha = roughness * roughness;
            float alpha2 = alpha * alpha;

            // ============================================================
            // Metalness (derived from roughness - very small base value)
            // ============================================================
            // Base metalness is very small, derived from roughness
            // Smooth surfaces (low roughness) → slightly higher metalness
            // Rough surfaces (high roughness) → lower metalness
            // Water gets a small additional boost
            float baseMetalness = (1.0 - roughness) * 0.03; // Very small base: max 0.03

            // Check if this is water (ocean) - slightly increase metalness for water
            float landMaskForMetalness = texture2D(uLandmassMask, texUV).r;
            float oceanMaskForMetalness = 1.0 - landMaskForMetalness;
            float waterMetalnessBoost = oceanMaskForMetalness > 0.01 ? 0.02 : 0.0; // Small boost for water only

            float metalness = baseMetalness + waterMetalnessBoost;
            metalness = clamp(metalness, 0.0, 0.05); // Cap at 0.05 total

            // ============================================================
            // GGX/Trowbridge-Reitz Distribution (D)
            // ============================================================
            // Controls the microfacet distribution based on roughness
            float denom = (NdotH * NdotH * (alpha2 - 1.0) + 1.0);
            float D = alpha2 / (PI * denom * denom);

            // ============================================================
            // Smith Geometry Function (G) - GGX form
            // ============================================================
            // Accounts for self-shadowing and masking of microfacets
            // Standard numerically stable form: G1(v) = 2 / (1 + sqrt(1 + alpha^2 * tan^2(theta)))
            float cosThetaL = NdotLClamped;
            float cosThetaV = NdotV;

            // Calculate tan^2(theta) = sin^2(theta) / cos^2(theta) = (1 - cos^2) / cos^2
            // Use max to avoid division by zero at grazing angles
            float tan2ThetaL = (1.0 - cosThetaL * cosThetaL) / max(cosThetaL * cosThetaL, 0.001);
            float tan2ThetaV = (1.0 - cosThetaV * cosThetaV) / max(cosThetaV * cosThetaV, 0.001);

            // Smith G1 for light direction
            float G1_L = 2.0 / (1.0 + sqrt(1.0 + alpha2 * tan2ThetaL));

            // Smith G1 for view direction
            float G1_V = 2.0 / (1.0 + sqrt(1.0 + alpha2 * tan2ThetaV));

            // Combined geometry term (separable form)
            float G = G1_L * G1_V;

            // ============================================================
            // Fresnel (F) - Schlick approximation with metalness
            // ============================================================
            // F0 modulates specular intensity based on surface properties
            // Non-metallic (dirt, vegetation): F0 = 0.02
            // Metallic (smooth surfaces): F0 up to 0.12 (reduced to prevent bright edges)
            // Metalness blends between non-metallic and metallic F0
            float F0_nonMetallic = 0.02; // Base F0 for Earth's non-reflective surfaces
            float F0_metallic = 0.12;    // Reduced max F0 to prevent bright terminating edges
            float F0 = mix(F0_nonMetallic, F0_metallic, metalness);
            float F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

            // ============================================================
            // Cook-Torrance Specular BRDF
            // ============================================================
            // specular = (D * G * F) / (4 * (N·L) * (N·V))
            float denominator = 4.0 * NdotLClamped * NdotV;
            float specularBRDF = (D * G * F) / max(denominator, 0.001);

            // Calculate specular intensity from BRDF
            // Roughness controls scattering: low roughness = tight specular, high roughness = wide scattered
            float specularIntensity = specularBRDF * NdotLClamped;

            // Use sunlight color for specular
            float lightIntensity = max(max(uLightColor.r, uLightColor.g), uLightColor.b);
            vec3 specularColor = vec3(lightIntensity);

            // Apply specular reflection - roughness already affects it through D and G terms
            // The GGX distribution makes smooth surfaces have tight highlights, rough surfaces have wide highlights
            // Scale to make effect visible
            float specularScale = 0.3; // Increased scale to make roughness effect more visible
            specularReflection = specularColor * specularIntensity * specularScale;
        }

        // Also modulate diffuse lighting based on roughness
        // Rougher surfaces scatter more light diffusely (but this is already handled by the BRDF)
        // We can enhance the contrast by slightly reducing diffuse for smooth surfaces
        float smoothness = 1.0 - roughness;
        solarDiffuse *= (0.7 + 0.3 * smoothness); // Smooth surfaces get slightly less diffuse (more specular)
    }

    // Moonlight
    // The moon's albedo (~0.12) and distance already factored into uMoonColor
    vec3 M = normalize(uMoonDir);
    float NdotM = max(dot(N, M), 0.0);
    vec3 moonDiffuse = uMoonColor * NdotM;

    // Moonlight is more visible on the night side (where sun is hidden)
    // On day side, moonlight is overwhelmed by sunlight
    float moonVisibility = 1.0 - smoothstep(-0.1, 0.3, dot(surfaceNormal, L));
    moonDiffuse *= moonVisibility;

    // Combined lighting: ambient + sun + moon + specular reflection
    vec3 lighting = uAmbientColor + solarDiffuse + moonDiffuse;

    // Base color with combined lighting + specular reflection
    // Use surfaceProps.color which has heightmap effects applied
    vec3 dayColor = surfaceProps.color * lighting + specularReflection;

    // Ocean areas now use the same surface shader as land
    // No special water effects - ocean renders with standard PBR surface properties

    // Ensure landmass texture effects are preserved on land areas
    // Re-apply texture-modified color on pure land to prevent any override
    if (landMask > 0.7)
    {
        vec3 landColor = surfaceProps.color * lighting + specularReflection;
        float landBlend = smoothstep(0.7, 0.95, landMask);
        dayColor = mix(dayColor, landColor, landBlend);
    }

    // City Lights Effect

    // City light color - wide range from cool white to warm amber/yellow
    // Cool white (LED/modern lighting) - keep similar to original
    vec3 cityLightCool = vec3(0.94, 0.95, 0.96); // Slightly cool off-white

    // Warm hues (sodium vapor, incandescent, warm LED)
    vec3 cityLightWarm = vec3(1.0, 0.85, 0.6);   // Warm amber/yellow
    vec3 cityLightOrange = vec3(1.0, 0.75, 0.5); // Orange/amber (sodium vapor)
    vec3 cityLightYellow = vec3(1.0, 0.95, 0.7); // Bright yellow (warm LED)

    // Location-based Perlin noise for color variation
    // Each location has its own color that evolves over time
    // Use multiple octaves of noise at different time scales for natural variation

    float secondsInDay = uTime * 86400.0;

    // Base spatial noise (location-based, changes slowly over time)
    // Regional color zones that shift gradually
    vec2 baseUV = texUV * 8.0;
    float baseNoise = noise2D(baseUV + vec2(secondsInDay * 0.0001, secondsInDay * 0.00007));

    // Medium-scale noise (location-based, changes at medium speed)
    vec2 mediumUV = texUV * 15.0;
    float mediumNoise = noise2D(mediumUV + vec2(secondsInDay * 0.0002, secondsInDay * 0.00015));

    // Fine spatial noise (location-based, changes faster over time)
    // Local color variation that shifts more quickly
    vec2 fineUV = texUV * 25.0;
    float fineNoise = noise2D(fineUV + vec2(secondsInDay * 0.0003, secondsInDay * 0.0002));

    // Combine noise octaves with different weights
    // Base provides regional color zones, medium and fine provide local variation
    float combinedNoise = baseNoise * 0.5 + mediumNoise * 0.3 + fineNoise * 0.2;
    float hueBlend = combinedNoise;

    // Bias toward extremes: push values away from middle (0.5) toward 0 or 1
    // This creates preference for very warm or very cool colors, not middle tones
    // Remap to create a "gap" in the middle: [0, 0.4] -> [0, 0.3], [0.6, 1] -> [0.7, 1]
    // Values in [0.4, 0.6] get pushed to the nearest extreme
    float extremeHue;
    if (hueBlend < 0.4)
    {
        // Lower range: map [0, 0.4] -> [0, 0.3] (cool/white region)
        extremeHue = hueBlend * 0.75;
    }
    else if (hueBlend > 0.6)
    {
        // Upper range: map [0.6, 1] -> [0.7, 1] (warm/amber region)
        extremeHue = 0.7 + (hueBlend - 0.6) * 0.75;
    }
    else
    {
        // Middle range [0.4, 0.6]: push toward nearest extreme
        // Values closer to 0.4 go to cool, closer to 0.6 go to warm
        float t = (hueBlend - 0.4) / 0.2; // Normalize to [0, 1]
        extremeHue = mix(0.3, 0.7, t);    // Smooth transition across the gap
    }


    // Blend between warm colors first, then mix with cool white
    // Using extremeHue creates stronger preference for warm or cool extremes
    vec3 warmMix = mix(cityLightWarm, mix(cityLightOrange, cityLightYellow, extremeHue * 0.5), extremeHue);
    vec3 cityLightColor = mix(warmMix, cityLightCool, extremeHue * 0.4); // Bias toward warmer colors

    // How much to dim the surface where lights are (subtle darkening)
    float surfaceDim = 1.0 - (lightsIntensity * nightFactor * 0.3);
    // Brightness-Dependent Day/Night Transition
    // Dim cities: appear later into night, disappear earlier in morning
    //
    // MAJOR CITIES can persist up to 45° from the sun (deep into daylight)
    // Small towns only visible in deep night

    // For very bright cities, we calculate visibility directly from sun angle
    // This allows them to extend past the normal nightFactor range
    // NdotL = cos(angle from sun): 1.0 = noon, 0.707 = 45°, 0 = 90° (terminator)
    float cityLightNdotL = dot(surfaceNormal, L);

    // Brightness-dependent angle threshold (in NdotL space)
    // Extended range so lights stay on longer in morning and fade in earlier in evening
    // Use a power curve so only the VERY brightest cities reach deep daylight visibility
    float brightnessCurve = pow(lightsIntensity, 2.5); // Exponential - only top ~10% get full effect

    // Major metropolis (intensity>0.9): visible until NdotL ≈ 0.866 (30° from sun, deeper into day)
    // Large city (intensity=0.7): visible until NdotL ≈ 0.5 (60° from sun)
    // Medium city (intensity=0.5): visible until NdotL ≈ 0.2 (78° from sun)
    // Dim cities (intensity<0.3): visible until NdotL ≈ -0.1 (slightly past terminator)
    // Extended range: -0.1 to 0.866 (was -0.15 to 0.707) for longer morning/evening visibility
    float brightThresholdNdotL = mix(-0.1, 0.866, brightnessCurve); // Extended range for longer visibility

    // Fade range depends on brightness - bright cities fade more gradually
    // Extended fade range for smoother transitions, especially in morning/evening
    float fadeRange = mix(0.2, 0.45, lightsIntensity); // dim=moderate, bright=very gradual

    // Calculate visibility: 1.0 when darker than threshold, 0.0 when brighter
    float adjustedVisibility =
        1.0 - smoothstep(brightThresholdNdotL - fadeRange, brightThresholdNdotL + fadeRange, cityLightNdotL);

    // Apply power curve for even more gradual fade on bright cities
    // Softer fade for smoother morning/evening transitions
    float fadeSharpness = mix(1.5, 0.4, lightsIntensity); // dim=moderate, bright=very gradual
    adjustedVisibility = pow(adjustedVisibility, fadeSharpness);

    float dayNightModulation = adjustedVisibility;

    // Additional suppression for very dim lights - they need even darker conditions
    float dimLightFactor = 1.0 - smoothstep(0.0, 0.4, lightsIntensity);
    float dimLightSuppression = mix(1.0, nightFactor, dimLightFactor * 0.5);

    // Animated Noise Flicker
    // Noise textures are in sinusoidal projection (same as nightlights)

    float noiseFlicker = 1.0; // Default: no flicker

    // Only apply noise during daytime (nightFactor < 0.5) and for dim lights
    float dayFactor = 1.0 - nightFactor;                             // 1.0 = full day, 0.0 = full night
    float dimForNoise = 1.0 - smoothstep(0.0, 0.7, lightsIntensity); // 1.0 for dim, 0.0 for bright

    // How much this pixel should be affected by noise
    float noiseInfluence = dayFactor * dimForNoise;

    if (noiseInfluence > 0.01)
    {
        // Sample hourly noise texture with time-displaced UV
        // uTime goes 0-1 per day, we want ~1 cycle per hour
        // Offset UV so the noise pattern drifts over time
        vec2 hourlyOffset = vec2(uTime * 24.0 * 0.1, uTime * 24.0 * 0.07);
        vec2 hourlyUV = fract(texUV + hourlyOffset);

        // Sample noise texture (stores values 0-1, we need -1 to 1)
        float noise = texture2D(uHourlyNoise, hourlyUV).r * 2.0 - 1.0;

        // Map noise from [-1,1] to darkening factor [0,1]
        float darkenAmount = (noise + 1.0) * 0.5;

        // Apply as multiplicative darkening (can go to zero)
        noiseFlicker = mix(1.0, 1.0 - darkenAmount, noiseInfluence);
    }

    // Micro Flicker Effect

    float microFlicker = 1.0; // Default: no flicker

    // Apply only to dim lights (below 50% brightness) - big cities unaffected
    float flickerEligibility = 1.0 - smoothstep(0.0, 0.5, lightsIntensity);

    if (flickerEligibility > 0.01)
    {
        // Sample micro noise texture with fast time-displaced UV
        // uTime goes 0-1 per day (86400 seconds)
        // We want visible change every second, so offset moves rapidly
        float secondsInDay = uTime * 86400.0;
        vec2 microOffset = vec2(secondsInDay * 0.01, secondsInDay * 0.007);
        vec2 microUV = fract(texUV + microOffset);

        // Sample noise texture (stores values 0-1, we need -1 to 1)
        float microNoise = texture2D(uMicroNoise, microUV).r * 2.0 - 1.0;

        // Scale flicker intensity based on how dim the light is
        // flickerEligibility goes 0→1 as lights get dimmer (50%→0%)
        // Medium cities (25-50%, eligibility ~0.5): ±60% variation
        // Small towns (<25%, eligibility ~1.0): ±90% variation
        float flickerIntensity = 0.3 + flickerEligibility * 0.6; // 0.3 to 0.9

        // Map noise [-1,1] to brightness multiplier with scaled intensity
        float flickerAmount = 1.0 + microNoise * flickerIntensity;

        // Clamp to avoid negative brightness
        flickerAmount = max(flickerAmount, 0.1);

        // Apply based on eligibility
        microFlicker = mix(1.0, flickerAmount, flickerEligibility);
    }

    // Final emissive strength combines all effects
    float emissiveStrength =
        lightsIntensity * dayNightModulation * dimLightSuppression * noiseFlicker * microFlicker * 1.5;
    vec3 emissive = cityLightColor * emissiveStrength;

    // Combine: dimmed day color + city light emission
    // dayColor already includes: surfaceProps.color (which has heightmap effects) * lighting (which uses normal map)
    // Roughness from texture is already applied to surfaceProps.roughness and affects PBR specular reflection
    vec3 finalColor = dayColor * surfaceDim + emissive;

    gl_FragColor = vec4(finalColor, finalAlbedo.a);
}