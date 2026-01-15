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
uniform sampler2D uHeightmap;             // Landmass heightmap (grayscale)
uniform sampler2D uNightlights;           // City lights grayscale texture
uniform sampler2D uMicroNoise;            // Fine-grained noise for per-second flicker
uniform sampler2D uHourlyNoise;           // Coarse noise for hourly variation
uniform sampler2D uSpecular;              // Surface specular/roughness (grayscale)
uniform sampler2D uIceMask;               // Ice coverage mask (current month)
uniform sampler2D uIceMask2;              // Ice coverage mask (next month)
uniform sampler2D uLandmassMask;          // Landmass mask (white=land, black=ocean)
uniform sampler2D uBathymetryDepth;       // Ocean floor depth (0=surface, 1=deepest ~11km)
uniform sampler2D uBathymetryNormal;      // Ocean floor normal map
uniform sampler2D uCombinedNormal;        // Combined normal map (landmass + bathymetry) for shadows
uniform sampler2D uWaterScatteringLUT;    // Single scatter LUT (S1_water) - 4D packed as 2D
uniform sampler2D uWaterTransmittanceLUT; // Transmittance LUT (T_water) - 2D
uniform sampler2D uWaterMultiscatterLUT;  // Multiple scatter LUT (Sm_water) - 2D
uniform int uUseWaterScatteringLUT;       // Whether to use LUT (1) or ray-march (0)
uniform float uIceBlendFactor;            // Blend factor between ice masks (0-1)
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uMoonDir;   // Direction to moon from Earth
uniform vec3 uMoonColor; // Moonlight color and intensity (pre-multiplied)
uniform vec3 uAmbientColor;
uniform vec3 uPoleDir;     // Planet's north pole direction
uniform vec3 uCameraPos;   // Camera position for view direction calculations
uniform int uUseNormalMap; // 0 = disabled, 1 = enabled
uniform int uUseHeightmap; // 0 = disabled, 1 = enabled
uniform int uUseSpecular;  // 0 = disabled, 1 = enabled
uniform float uTime;       // Julian date fraction for animated noise

// Constants
const float PI = 3.14159265359;
const float MAX_DEPTH = 11000.0; // Maximum ocean depth (meters)

// Water scattering LUT resolution (must match preprocessing)
const float LUT_DEPTH_RES = 64.0;
const float LUT_MU_SUN_RES = 32.0;
const float LUT_MU_VIEW_RES = 32.0;
const float LUT_NU_RES = 32.0;          // Relative angle resolution (scattering angle between view and sun)
const float LUT_TRANS_DEPTH_RES = 64.0; // Transmittance LUT depth resolution
const float LUT_TRANS_MU_RES = 32.0;    // Transmittance LUT mu resolution

// Lookup water transmittance from precomputed 2D LUT
// depth: ocean depth in meters [0, MAX_DEPTH]
// mu: cos(zenith angle) [-1, 1], where 1 = straight down, -1 = straight up
// Returns: RGB transmittance exp(−∫ σ_t ds)
vec3 lookupWaterTransmittanceLUT(float depth, float mu)
{
    // Normalize inputs
    // Depth: [0, MAX_DEPTH] -> [0, 1] with square root for better shallow water resolution
    float depth_normalized = sqrt(clamp(depth / MAX_DEPTH, 0.0, 1.0));

    // mu: [-1, 1] -> [0, 1]
    float mu_normalized = clamp((mu + 1.0) * 0.5, 0.0, 1.0);

    // Convert to texture indices
    float depth_idx = depth_normalized * (LUT_TRANS_DEPTH_RES - 1.0);
    float mu_idx = mu_normalized * (LUT_TRANS_MU_RES - 1.0);

    // Pack into 2D texture coordinates
    // Transmittance LUT: width = depthRes, height = muRes
    float lutX = depth_idx / LUT_TRANS_DEPTH_RES;
    float lutY = mu_idx / LUT_TRANS_MU_RES;

    // Sample LUT
    return texture2D(uWaterTransmittanceLUT, vec2(lutX, lutY)).rgb;
}

// Lookup water multiple scattering from precomputed 2D LUT
// depth: ocean depth in meters [0, MAX_DEPTH]
// mu: cos(zenith angle) [-1, 1], where 1 = straight down, -1 = straight up
// Returns: RGB multiple scattering contribution
vec3 lookupWaterMultiscatterLUT(float depth, float mu)
{
    // Normalize inputs (same as transmittance LUT)
    float depth_normalized = sqrt(clamp(depth / MAX_DEPTH, 0.0, 1.0));
    float mu_normalized = clamp((mu + 1.0) * 0.5, 0.0, 1.0);

    // Convert to texture indices
    float depth_idx = depth_normalized * (LUT_TRANS_DEPTH_RES - 1.0);
    float mu_idx = mu_normalized * (LUT_TRANS_MU_RES - 1.0);

    // Pack into 2D texture coordinates
    // Multiscatter LUT: width = depthRes, height = muRes (same as transmittance)
    float lutX = depth_idx / LUT_TRANS_DEPTH_RES;
    float lutY = mu_idx / LUT_TRANS_MU_RES;

    // Sample LUT
    return texture2D(uWaterMultiscatterLUT, vec2(lutX, lutY)).rgb;
}

// Lookup water scattering from precomputed 4D LUT (packed as 2D)
// depth: ocean depth in meters [0, MAX_DEPTH]
// mu_sun: cos(angle between sun and surface normal) [-1, 1]
// mu_view: cos(angle between view and surface normal) [-1, 1]
// nu: cos(angle between view and sun directions) [-1, 1]
//     This accounts for relative camera rotation relative to sunlight direction
// Returns: RGB color transform
vec3 lookupWaterScatteringLUT(float depth, float mu_sun, float mu_view, float nu)
{
    // Normalize inputs
    // Depth: [0, MAX_DEPTH] -> [0, 1] with square root for better shallow water resolution
    float depth_normalized = sqrt(clamp(depth / MAX_DEPTH, 0.0, 1.0));

    // mu_sun: [-1, 1] -> [0, 1]
    float mu_sun_normalized = clamp((mu_sun + 1.0) * 0.5, 0.0, 1.0);

    // mu_view: [-1, 1] -> [0, 1]
    float mu_view_normalized = clamp((mu_view + 1.0) * 0.5, 0.0, 1.0);

    // nu: [-1, 1] -> [0, 1] (relative angle between view and sun)
    float nu_normalized = clamp((nu + 1.0) * 0.5, 0.0, 1.0);

    // Convert to texture indices
    float depth_idx = depth_normalized * (LUT_DEPTH_RES - 1.0);
    float mu_sun_idx = mu_sun_normalized * (LUT_MU_SUN_RES - 1.0);
    float mu_view_idx = mu_view_normalized * (LUT_MU_VIEW_RES - 1.0);
    float nu_idx = nu_normalized * (LUT_NU_RES - 1.0);

    // Pack into 2D texture coordinates
    // 4D -> 2D packing: width = depthRes * muSunRes * nuRes, height = muRes
    // x = depth_idx + mu_sun_idx * depthRes + nu_idx * (depthRes * muSunRes)
    float lutX = (depth_idx + mu_sun_idx * LUT_DEPTH_RES + nu_idx * (LUT_DEPTH_RES * LUT_MU_SUN_RES)) /
                 (LUT_DEPTH_RES * LUT_MU_SUN_RES * LUT_NU_RES);
    float lutY = mu_view_idx / LUT_MU_VIEW_RES;

    // Sample LUT
    return texture2D(uWaterScatteringLUT, vec2(lutX, lutY)).rgb;
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

// Procedural Ocean Wave Normal Perturbation
// Computes wave perturbations in tangent space and transforms to world space

vec3 getOceanWaveNormal(vec2 texUV, vec3 baseNormal, vec3 tangent, vec3 bitangent, float time)
{
    // CRITICAL: Use texture UV coordinates (already mapped to sphere's surface) instead of world position
    // This ensures noise sampling accounts for sphere curvature and follows the surface correctly
    // Texture UVs are in sinusoidal projection space, which properly maps to the sphere
    vec2 noiseUV = texUV * 8.0; // Scale for appropriate wave frequency

    float n1 = noise2D(noiseUV * 3.0 + vec2(time * 0.01, time * 0.007));
    float n2 = noise2D(noiseUV * 3.0 + vec2(0.5, 0.5) + vec2(time * 0.008, time * 0.012));
    float n3 = noise2D(noiseUV * 12.0 + vec2(time * 0.02, time * 0.015));
    float n4 = noise2D(noiseUV * 12.0 + vec2(0.3, 0.7) + vec2(time * 0.018, time * 0.022));
    float n5 = noise2D(noiseUV * 50.0 + vec2(time * 0.03, time * 0.025));

    // Compute perturbations in tangent space (X = tangent/east, Y = bitangent/north, Z = normal/up)
    float nx = (n1 - 0.5) * 0.02 + (n3 - 0.5) * 0.01 + (n5 - 0.5) * 0.005;
    float ny = (n2 - 0.5) * 0.02 + (n4 - 0.5) * 0.01;
    vec3 normalPerturbTangent = vec3(nx, ny, 1.0); // Z component is 1.0 (pointing along base normal)

    // Transform from tangent space to world space using TBN matrix
    // TBN: Column 0 = tangent (east), Column 1 = bitangent (north), Column 2 = normal (up)
    // The sphere's surface normal (baseNormal) properly warps the tangent-space normal
    // to follow the sphere's curvature at this point
    mat3 TBN = mat3(tangent, bitangent, baseNormal);
    vec3 waveNormal = normalize(TBN * normalPerturbTangent);

    return waveNormal;
}

// Light Path Length Through Atmosphere and Water

float getAtmosphericPathMultiplier(float NdotL)
{
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

// Ray-Marched Subsurface Scattering Through Water Column
// Scientific absorption coefficients for pure seawater (per meter)
// Reference: Pope & Fry (1997) "Absorption spectrum (380-700 nm) of pure water"
// Typical values for clear ocean water:
// Red (680nm): ~0.4-0.5 m^-1 (strongly absorbed)
// Green (550nm): ~0.02-0.05 m^-1 (weakly absorbed)
// Blue (440nm): ~0.01-0.02 m^-1 (weakly absorbed)
const vec3 WATER_ABSORPTION = vec3(0.45,   // Red (680nm): strongly absorbed
                                   0.03,   // Green (550nm): weakly absorbed
                                   0.015); // Blue (440nm): weakly absorbed
// Scattering coefficient - minimal, let texture colors show through
const vec3 WATER_SCATTERING = vec3(0.0003, 0.0005, 0.0008);
// Index of refraction for seawater (slightly higher than pure water due to salinity)
// Pure water at 20°C: 1.333, Seawater (35‰ salinity): ~1.339
const float WATER_IOR = 1.339; // Seawater index of refraction

// Consistent ocean floor color (sand/sediment) for refraction calculations
// Typical ocean floor sediment colors: light beige/tan to brown
const vec3 OCEAN_FLOOR_COLOR = vec3(0.75, 0.70, 0.65); // Light beige/tan sediment color

// Refract a ray at water surface using Snell's law
vec3 refractRay(vec3 incident, vec3 normal, float eta)
{
    float cosI = -dot(normal, incident);
    float sinT2 = eta * eta * (1.0 - cosI * cosI);

    if (sinT2 > 1.0)
    {
        // Total internal reflection
        return reflect(incident, normal);
    }

    float cosT = sqrt(1.0 - sinT2);
    return eta * incident + (eta * cosI - cosT) * normal;
}

// Sample bathymetry at a UV offset (for ray marching)
float sampleBathymetry(vec2 baseUV, vec2 offset)
{
    vec2 sampleUV = baseUV + offset;
    // Clamp to valid range
    sampleUV = clamp(sampleUV, vec2(0.001), vec2(0.999));
    return texture2D(uBathymetryDepth, sampleUV).r * 11000.0;
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

// Water scattering: Ray-march from camera into water, sampling LUTs at each step
// Uses precomputed LUT for fast lookup based on depth, sun angle, view angle, and relative angle
// Accumulates scattering contribution as ray travels through water, using depth/elevation
// and normal vectors at each point to determine trajectory and refraction
vec3 submarineScattering(vec2 texUV,         // Base texture UV
                         float surfaceDepth, // Depth at this pixel (meters)
                         vec3 viewDir,       // View direction (from surface to camera)
                         vec3 lightDir,      // Sun direction
                         vec3 lightColor,    // Actual sun color
                         vec3 surfaceNormal, // Water surface normal at starting point
                         float NdotL,        // Surface illumination
                         vec3 worldPos,      // Starting world position on sphere surface
                         float time,
                         vec3 tangent,      // Tangent vector (east) at starting point
                         vec3 bitangent,    // Bitangent vector (north) at starting point
                         vec3 surfaceColor) // Surface color (sky reflection) - unused, kept for compatibility
{
    // Use LUT-based ray-marching if available
    if (uUseWaterScatteringLUT == 1)
    {
        // Ray-march from camera into water, sampling LUTs progressively at each step
        // Uses depth/elevation and normal vectors at each point to determine trajectory
        // Applies color based on refraction indices at each point
        const float UV_STEP_SCALE = 0.002;
        const float STEP_DISTANCE_METERS = 20.0; // meters per step
        const int MAX_REFRACTION_STEPS = 100;

        // CRITICAL: Refract view ray from camera into water at surface (air -> water)
        // viewDir is from surface to camera, so -viewDir is from camera to surface
        // Refract from air (IOR=1.0) to water (IOR=WATER_IOR)
        vec3 cameraToSurface = -viewDir; // Direction from camera to surface
        vec3 currentRay = refractRay(cameraToSurface, surfaceNormal, 1.0 / WATER_IOR);
        vec2 currentUV = texUV;

        // Track current sphere normal (will be updated as we move along sphere)
        vec3 currentNormal = surfaceNormal;
        vec3 currentTangent = tangent;
        vec3 currentBitangent = bitangent;

        // Accumulated scattering contribution
        vec3 accumulatedScattering = vec3(0.0);
        float totalPathLength = 0.0; // Total distance traveled through water

        // Previous depth for incremental scattering computation
        float prevDepth = 0.0;
        vec3 prevScattering = vec3(0.0);

        // Ray-march from camera through water
        for (int step = 0; step < MAX_REFRACTION_STEPS; step++)
        {
            vec2 sampleUV = clamp(currentUV, vec2(0.001), vec2(0.999));

            // Check if we've hit land
            float landMaskAtStep = texture2DLod(uLandmassMask, sampleUV, 0.0).r;
            if (landMaskAtStep > 0.01)
            {
                break; // Hit land, terminate
            }

            // Sample depth at current position
            float currentDepth = texture2DLod(uBathymetryDepth, sampleUV, 0.0).r * MAX_DEPTH;

            // Compute sphere normal at current UV position (needed for floor check and scattering)
            // This must be computed before floor check since floor calculations need localNormal
            float v_sinu = sampleUV.y;
            float lat = (v_sinu - 0.5) * PI;
            float cosLat = cos(lat);
            float u_sinu = sampleUV.x;
            float uMin = 0.5 - 0.5 * abs(cosLat);
            float uMax = 0.5 + 0.5 * abs(cosLat);
            float u_clamped = clamp(u_sinu, uMin, uMax);
            float x_sinu = (u_clamped - 0.5) * 2.0 * PI;
            float lon = x_sinu / max(abs(cosLat), 0.01);

            float cosLon = cos(lon);
            float sinLon = sin(lon);
            float cosLat_safe = cos(lat);
            float sinLat = sin(lat);
            vec3 sphereNormalAtUV = vec3(cosLat_safe * cosLon, sinLat, cosLat_safe * sinLon);

            currentNormal = sphereNormalAtUV;

            // Recompute tangent frame
            currentTangent = cross(uPoleDir, currentNormal);
            float currentTangentLen = length(currentTangent);
            if (currentTangentLen < 0.001)
            {
                currentTangent = vec3(1.0, 0.0, 0.0);
            }
            else
            {
                currentTangent = currentTangent / currentTangentLen;
            }
            currentBitangent = cross(currentNormal, currentTangent);

            // Get local normal from bathymetry (for refraction and floor calculations)
            vec3 localNormal = currentNormal;
            if (uUseNormalMap == 1)
            {
                vec4 normalSample = texture2DLod(uBathymetryNormal, sampleUV, 0.0);
                vec3 bathymetryNormalTangent = decodeSwappedNormalMap(normalSample, sampleUV);
                bathymetryNormalTangent.y = -bathymetryNormalTangent.y;
                mat3 TBN = mat3(currentTangent, currentBitangent, currentNormal);
                localNormal = normalize(TBN * bathymetryNormalTangent);
            }

            // Check if we've reached the ocean floor (now that localNormal is computed)
            if (totalPathLength >= currentDepth && currentDepth > 1.0)
            {
                // Hit ocean floor - use floor color with multiscattering
                vec3 floorColor = OCEAN_FLOOR_COLOR;

                // Compute angles for floor scattering
                float mu_sun_floor = dot(normalize(lightDir), localNormal);
                float mu_view_floor = dot(normalize(-currentRay), localNormal);
                vec3 viewDirNormalized = normalize(-currentRay);
                vec3 sunDirNormalized = normalize(lightDir);

                // Check if camera ray reflects directly to sun from floor
                vec3 reflectedViewDir = reflect(-viewDirNormalized, localNormal);
                float reflectDotSun = dot(normalize(reflectedViewDir), sunDirNormalized);
                bool reflectsToSun = reflectDotSun > 0.866; // Within 30° of sun

                // Sample transmittance to floor (camera -> floor path)
                vec3 transmittanceToFloor = lookupWaterTransmittanceLUT(currentDepth, mu_view_floor);

                vec3 floorContribution;

                if (reflectsToSun)
                {
                    // Camera ray reflects directly to sun from floor - use single scatter LUT
                    float nu_floor = dot(viewDirNormalized, sunDirNormalized);
                    nu_floor = clamp(nu_floor, -1.0, 1.0);

                    vec3 floorScatteringLUT =
                        lookupWaterScatteringLUT(currentDepth, mu_sun_floor, mu_view_floor, nu_floor);
                    floorContribution = lightColor * floorScatteringLUT * transmittanceToFloor;
                }
                else
                {
                    // Camera ray scatters off floor - use floor color with multiscattering
                    // Multiscattering accounts for how floor reflects light toward sun
                    vec3 multiscatter = lookupWaterMultiscatterLUT(currentDepth, mu_sun_floor);
                    floorContribution = floorColor * lightColor * multiscatter * transmittanceToFloor;
                }

                accumulatedScattering += floorContribution;

                break; // Hit floor, terminate
            }

            // Compute angles for LUT lookup at this step
            float mu_sun = dot(normalize(lightDir), localNormal);
            float mu_view = dot(normalize(-currentRay), localNormal);
            vec3 viewDirNormalized = normalize(-currentRay);
            vec3 sunDirNormalized = normalize(lightDir);
            float nu = dot(viewDirNormalized, sunDirNormalized);
            nu = clamp(nu, -1.0, 1.0);

            // Check if camera ray reflects directly toward the sun
            // Compute reflection direction of view ray
            vec3 reflectedViewDir = reflect(-viewDirNormalized, localNormal);
            float reflectDotSun = dot(normalize(reflectedViewDir), sunDirNormalized);

            // Threshold for "direct reflection" - when reflection points toward sun
            // Use tight threshold: only when reflection is within ~30° of sun (cos(30°) ≈ 0.866)
            bool reflectsToSun = reflectDotSun > 0.866;

            // Sample transmittance LUT at current depth and view angle
            // This gives us absorption along the path
            vec3 transmittance = lookupWaterTransmittanceLUT(totalPathLength, mu_view);

            vec3 stepContribution;

            if (reflectsToSun)
            {
                // Camera ray reflects directly to sun - use LUT scattering color
                // Sample single scatter LUT at current depth
                vec3 currentScattering = lookupWaterScatteringLUT(totalPathLength, mu_sun, mu_view, nu);

                // Compute incremental scattering contribution
                // Difference between current and previous scattering gives incremental contribution
                vec3 incrementalScattering = currentScattering - prevScattering;

                // Apply transmittance (absorption) to incremental scattering
                // This accounts for light that survives to reach this depth
                stepContribution = lightColor * incrementalScattering * transmittance;

                // Update for next iteration (only when using scattering LUT)
                prevScattering = currentScattering;
            }
            else
            {
                // Camera ray scatters off - use ocean floor color with multiscattering
                // Multiscattering accounts for how light reflects from floor toward sun

                // Sample ocean floor color at this depth
                vec3 floorColor = OCEAN_FLOOR_COLOR;

                // Sample multiscatter LUT for sun angle (floor -> sun path)
                // mu_sun represents the angle from floor normal toward sun
                vec3 multiscatter = lookupWaterMultiscatterLUT(totalPathLength, mu_sun);

                // Floor color with multiscattering: floor color modulated by multiscatter and transmittance
                // Multiscatter accounts for how the floor reflects light toward the sun
                stepContribution = floorColor * lightColor * multiscatter * transmittance;

                // Reset prevScattering when switching to floor color mode
                // This ensures next iteration starts fresh if it switches back to scattering
                prevScattering = vec3(0.0);
            }

            accumulatedScattering += stepContribution;

            // Update for next iteration
            prevDepth = totalPathLength;
            totalPathLength += STEP_DISTANCE_METERS;

            // Check if ray is exiting water (water -> air refraction at surface)
            // If path length is very small and we're near surface, ray might exit
            if (totalPathLength < 10.0 && currentDepth < 10.0)
            {
                // Ray is near surface - check if it would exit
                // If ray direction points upward relative to surface normal, it exits
                float exitDot = dot(normalize(currentRay), localNormal);
                if (exitDot > 0.0)
                {
                    // Ray is exiting water - refract from water to air
                    vec3 exitRay = refractRay(-normalize(currentRay), localNormal, WATER_IOR / 1.0);

                    // If refraction fails (total internal reflection), use reflection
                    if (length(exitRay) < 0.001)
                    {
                        exitRay = reflect(-normalize(currentRay), localNormal);
                    }

                    // Apply transmittance for exit path
                    float mu_exit = dot(normalize(exitRay), localNormal);
                    vec3 transmittanceExit = lookupWaterTransmittanceLUT(totalPathLength, mu_exit);

                    // Modulate accumulated scattering by exit transmittance
                    accumulatedScattering *= transmittanceExit;

                    break; // Ray exits water, terminate
                }
            }

            // Refract ray at this position using local normal and refraction index
            // This determines trajectory for next step based on refraction at this point
            // Inside water: continue refracting with water IOR
            currentRay = refractRay(-normalize(currentRay), localNormal, 1.0 / WATER_IOR);

            // Update UV based on refracted ray direction
            float rayTangent = dot(currentRay, currentTangent);
            float rayBitangent = dot(currentRay, currentBitangent);
            vec2 uvOffset = vec2(rayTangent, rayBitangent) * UV_STEP_SCALE / max(abs(cosLat), 0.01);
            currentUV = clamp(currentUV + uvOffset, vec2(0.001), vec2(0.999));
        }

        // Return accumulated scattering from ray-marching
        return accumulatedScattering;
    }

    // Fallback to ray-marching if LUT not available
    // CRITICAL: Estimate planet radius from starting position
    const float WGS84_MEAN_RADIUS_M = (2.0 * 6378137.0 + 6356752.314245) / 3.0;
    float planetRadiusEstimate = length(worldPos);
    float scale = WGS84_MEAN_RADIUS_M / planetRadiusEstimate; // Meters per display unit

    // Refract view ray into water (Snell's law)
    vec3 currentRay = refractRay(-viewDir, surfaceNormal, 1.0 / WATER_IOR);
    vec2 currentUV = texUV;

    // Track current sphere normal (will be updated as we move along sphere)
    vec3 currentNormal = surfaceNormal;
    vec3 currentTangent = tangent;
    vec3 currentBitangent = bitangent;

    // Accumulated color transforms (absorption coefficients accumulated along path)
    // Start with no absorption (vec3(1.0) = no change)
    vec3 accumulatedAbsorption = vec3(1.0);
    float totalPathLength = 0.0; // Total distance traveled through water

    // Step size for moving along refracted ray in UV space
    const float UV_STEP_SCALE = 0.002;
    const float STEP_DISTANCE_METERS = 20.0; // meters per step

    // Maximum steps to prevent infinite loops (but no depth thresholding)
    const int MAX_REFRACTION_STEPS = 100;

    bool hitOceanFloor = false;
    vec2 floorUV = vec2(0.0);
    vec3 floorNormal = vec3(0.0);
    float floorDepth = 0.0;

    // Phase 1: Ray-march from camera through water until we hit ocean floor or exit
    for (int step = 0; step < MAX_REFRACTION_STEPS; step++)
    {
        vec2 sampleUV = clamp(currentUV, vec2(0.001), vec2(0.999));

        // Check if we've hit land (stop if we hit land)
        float landMaskAtStep = texture2DLod(uLandmassMask, sampleUV, 0.0).r;
        if (landMaskAtStep > 0.01)
        {
            break; // Hit land, terminate
        }

        // Sample depth at current position (always from heightmap)
        // Depth is always used for absorption and floor detection
        float currentDepth = texture2DLod(uBathymetryDepth, sampleUV, 0.0).r * 11000.0;

        // Compute sphere normal at current UV position
        float v_sinu = sampleUV.y;
        float lat = (v_sinu - 0.5) * PI;
        float cosLat = cos(lat);
        float u_sinu = sampleUV.x;
        float uMin = 0.5 - 0.5 * abs(cosLat);
        float uMax = 0.5 + 0.5 * abs(cosLat);
        float u_clamped = clamp(u_sinu, uMin, uMax);
        float x_sinu = (u_clamped - 0.5) * 2.0 * PI;
        float lon = x_sinu / max(abs(cosLat), 0.01);

        float cosLon = cos(lon);
        float sinLon = sin(lon);
        float cosLat_safe = cos(lat);
        float sinLat = sin(lat);
        vec3 sphereNormalAtUV = vec3(cosLat_safe * cosLon, sinLat, cosLat_safe * sinLon);

        currentNormal = sphereNormalAtUV;

        // Recompute tangent frame
        currentTangent = cross(uPoleDir, currentNormal);
        float currentTangentLen = length(currentTangent);
        if (currentTangentLen < 0.001)
        {
            currentTangent = vec3(1.0, 0.0, 0.0);
        }
        else
        {
            currentTangent = currentTangent / currentTangentLen;
        }
        currentBitangent = cross(currentNormal, currentTangent);

        // Get local normal from bathymetry
        vec3 localNormal = currentNormal;
        if (uUseNormalMap == 1)
        {
            vec4 normalSample = texture2DLod(uBathymetryNormal, sampleUV, 0.0);
            vec3 bathymetryNormalTangent = decodeSwappedNormalMap(normalSample, sampleUV);
            bathymetryNormalTangent.y = -bathymetryNormalTangent.y;
            mat3 TBN = mat3(currentTangent, currentBitangent, currentNormal);
            localNormal = normalize(TBN * bathymetryNormalTangent);
        }

        // Check if we've reached the ocean floor
        // Use a small threshold to detect floor contact
        // Depth is always used regardless of heightmap toggle
        float depthDifference = abs(currentDepth - totalPathLength);
        if (depthDifference < STEP_DISTANCE_METERS * 2.0 && currentDepth > 1.0)
        {
            // Hit ocean floor - store position and continue ray-marching toward sun
            hitOceanFloor = true;
            floorUV = sampleUV;
            floorNormal = localNormal;
            floorDepth = currentDepth;
            break;
        }

        // Accumulate absorption along path (Beer-Lambert law)
        // Apply absorption for this step's distance
        vec3 stepAbsorption = exp(-WATER_ABSORPTION * STEP_DISTANCE_METERS);
        accumulatedAbsorption *= stepAbsorption;
        totalPathLength += STEP_DISTANCE_METERS;

        // Refract ray at this position
        currentRay = refractRay(-normalize(currentRay), localNormal, 1.0 / WATER_IOR);

        // Update UV based on refracted ray direction
        float rayTangent = dot(currentRay, currentTangent);
        float rayBitangent = dot(currentRay, currentBitangent);
        vec2 uvOffset = vec2(rayTangent, rayBitangent) * UV_STEP_SCALE / max(abs(cosLat), 0.01);
        currentUV = clamp(currentUV + uvOffset, vec2(0.001), vec2(0.999));
    }

    // Subsurface scattering contribution from sunlight
    // Start with sunlight and apply view path absorption (camera -> surface)
    vec3 subsurfaceScattering = lightColor * accumulatedAbsorption;

    // Phase 2: If we hit ocean floor, add floor contribution
    // The floor color adds to subsurface scattering when light reaches the floor
    if (hitOceanFloor)
    {
        // Compute surface normal for sun path (modulated by normal map if enabled)
        vec3 surfaceNormalForSun = surfaceNormal;
        if (uUseNormalMap == 1)
        {
            vec3 waveNormal = getOceanWaveNormal(texUV, surfaceNormal, tangent, bitangent, time);
            surfaceNormalForSun = waveNormal;
        }

        // Refract sun ray into water at surface (Snell's law)
        vec3 sunRayInWater = refractRay(-normalize(lightDir), surfaceNormalForSun, 1.0 / WATER_IOR);

        // Ray-march from floor toward sun, continuing to scatter
        // Start from floor position and march toward sun direction
        vec2 currentFloorUV = floorUV;
        vec3 currentFloorNormal = floorNormal;
        float pathFromFloor = 0.0;
        vec3 absorptionFromFloor = vec3(1.0);

        // Direction from floor toward sun (refracted sun ray direction)
        vec3 rayFromFloor = normalize(sunRayInWater);

        // Continue ray-marching from floor, scattering toward sun
        // This captures bounces from both edges and flat bottoms
        for (int floorStep = 0; floorStep < MAX_REFRACTION_STEPS; floorStep++)
        {
            vec2 sampleUV = clamp(currentFloorUV, vec2(0.001), vec2(0.999));

            // Check if we've hit land
            float landMask = texture2DLod(uLandmassMask, sampleUV, 0.0).r;
            if (landMask > 0.01)
            {
                break;
            }

            // Sample depth at current position
            float currentDepth = texture2DLod(uBathymetryDepth, sampleUV, 0.0).r * 11000.0;

            // Compute sphere normal at current position
            float v_sinu = sampleUV.y;
            float lat = (v_sinu - 0.5) * PI;
            float cosLat = cos(lat);
            float u_sinu = sampleUV.x;
            float uMin = 0.5 - 0.5 * abs(cosLat);
            float uMax = 0.5 + 0.5 * abs(cosLat);
            float u_clamped = clamp(u_sinu, uMin, uMax);
            float x_sinu = (u_clamped - 0.5) * 2.0 * PI;
            float lon = x_sinu / max(abs(cosLat), 0.01);

            float cosLon = cos(lon);
            float sinLon = sin(lon);
            float cosLat_safe = cos(lat);
            float sinLat = sin(lat);
            vec3 sphereNormal = vec3(cosLat_safe * cosLon, sinLat, cosLat_safe * sinLon);

            // Update tangent frame
            vec3 floorTangent = cross(uPoleDir, sphereNormal);
            float floorTangentLen = length(floorTangent);
            if (floorTangentLen < 0.001)
            {
                floorTangent = vec3(1.0, 0.0, 0.0);
            }
            else
            {
                floorTangent = floorTangent / floorTangentLen;
            }
            vec3 floorBitangent = cross(sphereNormal, floorTangent);

            // Get local normal from bathymetry (for flat bottoms and edges)
            vec3 localFloorNormal = sphereNormal;
            if (uUseNormalMap == 1)
            {
                vec4 normalSample = texture2DLod(uBathymetryNormal, sampleUV, 0.0);
                vec3 bathymetryNormalTangent = decodeSwappedNormalMap(normalSample, sampleUV);
                bathymetryNormalTangent.y = -bathymetryNormalTangent.y;
                mat3 TBN = mat3(floorTangent, floorBitangent, sphereNormal);
                localFloorNormal = normalize(TBN * bathymetryNormalTangent);
            }

            // Accumulate absorption along path from floor toward sun
            vec3 stepAbsorption = exp(-WATER_ABSORPTION * STEP_DISTANCE_METERS);
            absorptionFromFloor *= stepAbsorption;
            pathFromFloor += STEP_DISTANCE_METERS;

            // Check if we've reached surface (path length >= floor depth)
            // Account for sun angle: path is longer when sun is low
            float sunRayDotNormal = dot(normalize(sunRayInWater), surfaceNormalForSun);
            float sunPathLengthToFloor = floorDepth / max(abs(sunRayDotNormal), 0.1);

            if (pathFromFloor >= sunPathLengthToFloor)
            {
                break; // Reached surface
            }

            // Bounce/scatter ray off floor surface
            // Use reflection for floor bounce (light reflects off ocean floor)
            // This captures bounces from both edges and flat bottoms
            rayFromFloor = reflect(-normalize(rayFromFloor), localFloorNormal);

            // Add some scattering (slight random perturbation for diffuse reflection)
            // This helps capture bounces from flat bottoms, not just specular edges
            // Use a small random offset based on position to simulate diffuse scattering
            float scatterAmount = 0.1; // Small scattering angle
            vec3 scatterDir = normalize(cross(localFloorNormal, vec3(1.0, 0.0, 0.0)));
            if (abs(dot(scatterDir, localFloorNormal)) > 0.9)
            {
                scatterDir = normalize(cross(localFloorNormal, vec3(0.0, 1.0, 0.0)));
            }
            vec3 scatterOffset =
                scatterDir * scatterAmount * (fract(sin(dot(sampleUV, vec2(12.9898, 78.233))) * 43758.5453) - 0.5);
            rayFromFloor = normalize(rayFromFloor + scatterOffset);

            // Update UV along refracted ray
            float rayTangent = dot(rayFromFloor, floorTangent);
            float rayBitangent = dot(rayFromFloor, floorBitangent);
            vec2 uvOffset = vec2(rayTangent, rayBitangent) * UV_STEP_SCALE / max(abs(cosLat), 0.01);
            currentFloorUV = clamp(currentFloorUV + uvOffset, vec2(0.001), vec2(0.999));
        }

        // Compute absorption for sun's path to floor
        float sunRayDotSurfaceNormal = dot(normalize(sunRayInWater), surfaceNormalForSun);
        float sunPathLengthToFloor = floorDepth / max(abs(sunRayDotSurfaceNormal), 0.1);
        vec3 absorptionSunToFloor = exp(-WATER_ABSORPTION * sunPathLengthToFloor);

        // Total absorption: sun -> floor -> surface (scattering path)
        vec3 totalAbsorption = absorptionSunToFloor * absorptionFromFloor;

        // Floor color contribution (only when light reaches floor)
        vec3 floorColor = OCEAN_FLOOR_COLOR;
        vec3 floorScattering = floorColor * lightColor * totalAbsorption * accumulatedAbsorption;

        // Blend floor contribution with subsurface scattering
        // More floor contribution when light actually reaches the floor
        // The absorption determines how much floor color contributes
        float floorBlendFactor = 1.0 - dot(totalAbsorption, vec3(0.333)); // Average absorption
        floorBlendFactor = smoothstep(0.3, 0.9, floorBlendFactor);        // Smooth transition

        subsurfaceScattering =
            mix(subsurfaceScattering, floorScattering, floorBlendFactor * 0.5); // Max 50% floor contribution
    }

    // Return subsurface scattering contribution directly
    // Surface reflection (Fresnel) removed - scattering handles all color contribution
    return subsurfaceScattering;
}

// =========================================================================
// Ocean Rendering with Ray-Marched Subsurface Scattering
// =========================================================================

vec3 calculateOceanColor(vec2 texUV,         // Texture UV for bathymetry sampling
                         float depthMeters,  // Actual bathymetry depth in meters
                         vec3 surfaceNormal, // Sphere normal (base water surface)
                         vec3 worldPos,      // World position for noise sampling
                         vec3 viewDir,       // Camera direction (from surface to camera)
                         vec3 lightDir,      // Sun direction
                         vec3 lightColor,    // Actual sunlight color from uLightColor
                         float NdotL,        // Surface lit by sun
                         vec3 tangent,       // Tangent vector (east)
                         vec3 bitangent,     // Bitangent vector (north)
                         float time          // For animated waves
)
{
    // Wave-Perturbed Normal (now uses texture UV for correct sphere curvature mapping)
    vec3 waveNormal = getOceanWaveNormal(texUV, surfaceNormal, tangent, bitangent, time);
    float waveNdotL = max(dot(waveNormal, lightDir), 0.0);

    // Compute surface color (sky reflection) - used when looking away from sun
    // Sky color comes from atmosphere scattering
    vec3 cameraToSurface = -viewDir; // Direction from camera to surface
    vec3 reflectDir = reflect(cameraToSurface, waveNormal);
    float skyGradient = reflectDir.y * 0.5 + 0.5;

    // Sky is sunlight scattered by atmosphere (blue shift)
    vec3 sunDirection = normalize(lightDir);
    float reflectDotSun = dot(normalize(reflectDir), sunDirection);
    reflectDotSun = max(0.0, reflectDotSun);
    float chromaticFactor = smoothstep(0.0, 1.0, reflectDotSun);

    float lightIntensity = max(max(lightColor.r, lightColor.g), lightColor.b);
    vec3 neutralLight = vec3(lightIntensity);
    vec3 chromaticLight = lightColor;
    vec3 effectiveLight = mix(neutralLight, chromaticLight, chromaticFactor);

    vec3 rayleighSky = effectiveLight * vec3(0.4, 0.6, 1.0);
    vec3 horizonSky = rayleighSky * 0.7;
    vec3 zenithSky = rayleighSky * 0.5;
    vec3 surfaceColor = mix(horizonSky, zenithSky, skyGradient);

    // Get subsurface scattering contribution (LUT or ray-marching)
    // This handles depth-dependent absorption and scattering through the water column
    vec3 subsurfaceColor = submarineScattering(texUV,
                                               depthMeters,
                                               viewDir,
                                               lightDir,
                                               lightColor,
                                               waveNormal,
                                               waveNdotL,
                                               worldPos,
                                               time,
                                               tangent,
                                               bitangent,
                                               surfaceColor);

    // Blend between surface color and underwater color based on view direction relative to sun
    // When looking toward sun: show underwater color (subsurface scattering)
    // When looking away from sun: show surface color (sky reflection)
    vec3 cameraToSurfaceNormalized = normalize(cameraToSurface);
    vec3 sunDirNormalized = normalize(lightDir);
    float viewDotSun = dot(cameraToSurfaceNormalized, sunDirNormalized);

    // Blend factor: 1.0 when looking toward sun, 0.0 when looking away
    // Use smooth transition for natural blending
    float underwaterBlend = smoothstep(-0.3, 0.5, viewDotSun);

    // Ocean color accumulation also depends on depth and sun angle
    // Deeper water and angles closer to sun show more underwater color
    // Use subtle enhancement to preserve scattering nuances
    float depthFactor = clamp(depthMeters / 1000.0, 0.0, 1.0); // Normalize to 1km
    float sunAngleFactor = max(0.0, dot(waveNormal, sunDirNormalized));
    float accumulationFactor = depthFactor * (0.5 + 0.5 * sunAngleFactor);

    // Final blend: combine view direction blend with subtle depth/sun angle accumulation
    // Reduce accumulation impact to preserve scattering details
    float finalBlend = mix(underwaterBlend, 1.0, accumulationFactor * 0.2); // Reduced from 0.5 to 0.2

    // Blend surface color (sky) with underwater color (subsurface scattering)
    vec3 oceanColor = mix(surfaceColor, subsurfaceColor, finalBlend);

    return oceanColor;
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

// Generate ocean normal using Perlin noise gradients
// Returns tangent-space normal that forms smooth curves across the ocean
vec3 generateOceanNormalNoise(vec2 texUV, vec3 worldPos)
{
    // Use consistent UV coordinates based on texture space for smooth curves
    // Scale to create large-scale patterns across the ocean
    vec2 noiseUV = texUV * 8.0;

    // Small offset for gradient computation
    float eps = 0.01;

    // Sample noise at multiple scales and compute gradients
    // Large scale waves (smooth curves)
    float scale1 = 2.0;
    float n1 = noise2D(noiseUV * scale1);
    float n1x = noise2D(noiseUV * scale1 + vec2(eps, 0.0));
    float n1y = noise2D(noiseUV * scale1 + vec2(0.0, eps));
    float grad1x = (n1x - n1) / eps;
    float grad1y = (n1y - n1) / eps;

    // Medium scale waves
    float scale2 = 5.0;
    float n2 = noise2D(noiseUV * scale2 + vec2(0.5, 0.5));
    float n2x = noise2D(noiseUV * scale2 + vec2(0.5, 0.5) + vec2(eps, 0.0));
    float n2y = noise2D(noiseUV * scale2 + vec2(0.5, 0.5) + vec2(0.0, eps));
    float grad2x = (n2x - n2) / eps;
    float grad2y = (n2y - n2) / eps;

    // Fine scale detail
    float scale3 = 12.0;
    float n3 = noise2D(noiseUV * scale3 + vec2(0.3, 0.7));
    float n3x = noise2D(noiseUV * scale3 + vec2(0.3, 0.7) + vec2(eps, 0.0));
    float n3y = noise2D(noiseUV * scale3 + vec2(0.3, 0.7) + vec2(0.0, eps));
    float grad3x = (n3x - n3) / eps;
    float grad3y = (n3y - n3) / eps;

    // Combine gradients from different scales
    // Larger scales contribute more to form smooth curves
    float gradX = grad1x * 0.5 + grad2x * 0.3 + grad3x * 0.2;
    float gradY = grad1y * 0.5 + grad2y * 0.3 + grad3y * 0.2;

    // Scale gradients to create visible surface variation
    gradX *= 0.3;
    gradY *= 0.3;

    // Form tangent-space normal from gradients
    // The normal points in the direction of steepest ascent
    vec3 normalPerturb = vec3(-gradX, -gradY, 1.0);
    return normalize(normalPerturb);
}

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

            // DEBUG: Check if texture is being sampled (should see values between 0-1)
            // If roughness is always 0 or 1, texture might not be loaded or UVs are wrong
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
        float sunAngle = dot(surfaceNormal, sunDir);

        // Modulate heightmap lightening by sun angle
        // Areas facing away from sun (sunAngle < 0) get no lightening
        // Areas facing toward sun (sunAngle > 0) get full effect, scaled by angle
        // Use smoothstep for gradual transition at terminator
        float sunModulation = smoothstep(-0.1, 0.3, sunAngle); // Fade from -0.1 to 0.3

        // Heightmap affects brightness: higher elevations are brighter (snow/ice at peaks)
        // Only apply when surface is facing toward sun
        float heightBrightness =
            1.0 + samples.height * 0.3 * sunModulation; // Up to 30% brighter at peaks, modulated by sun
        heightModulatedColor *= heightBrightness;
    }

    // Compute roughness from sampled specular texture
    // Roughness: 0 = smooth/shiny (high specular), 1 = rough/matte (low specular)
    // samples.roughness is already set: 0.1 for ocean, texture value for land
    float surfaceRoughness = samples.roughness;

    // Only apply roughness effects to land areas
    if (landMask > 0.01 && uUseSpecular == 1)
    {
        // Base roughness from texture (intrinsic surface roughness)
        // Texture is already inverted: lighter = less rough, darker = rougher
        float baseRoughness = samples.roughness;

        // Ice roughness effect: ice is smoother/shiny, so decrease roughness
        // Ice has lower roughness (higher specular) than regular land
        // Scale down the effect and apply as reduction on base roughness
        const float ICE_ROUGHNESS_SCALE = 0.3; // Scale down ice effect (30% of full effect)

        // Calculate ice smoothness: how much ice decreases roughness relative to base
        // iceAlbedo is typically 0.5-0.9 (high reflectance = low roughness)
        // Base roughness is typically 0.3-0.7
        // We want: iceSmoothness = (1.0 - iceAlbedo) * coverage * scale
        // Higher iceAlbedo (more reflective) = lower roughness
        float iceSmoothness = (1.0 - iceAlbedo) * iceCoverage * ICE_ROUGHNESS_SCALE;

        // Apply ice smoothness as reduction: baseRoughness * (1.0 - smoothness)
        // This preserves the underlying roughness variation while making ice areas smoother
        float iceMultiplier = 1.0 - iceSmoothness;

        // Final roughness: underlying texture roughness reduced by ice effect
        surfaceRoughness = baseRoughness * iceMultiplier;

        // Clamp to reasonable range [0.05, 0.95]
        surfaceRoughness = clamp(surfaceRoughness, 0.05, 0.95);
    }
    else if (landMask > 0.01 && uUseSpecular == 0)
    {
        // When specular is disabled, use default roughness for land
        surfaceRoughness = 0.5;
    }
    // For ocean (landMask <= 0.01), keep the default 0.1 from sampleLandmassTextures

    props.roughness = surfaceRoughness;

    // Color is not affected by roughness (roughness only affects specular)
    props.color = heightModulatedColor;

    // Apply normal map effect
    if (uUseNormalMap == 1)
    {
        // Build TBN matrix to transform from tangent space to world space
        mat3 TBN = mat3(tangent, bitangent, surfaceNormal);
        props.normal = normalize(TBN * samples.normalTangent);
    }
    else
    {
        props.normal = surfaceNormal;
    }

    return props;
}

void main()
{
    vec2 texUV = toSinusoidalUV(vTexCoord);

    // Calculate view direction (needed for specular calculations)
    vec3 viewDir = normalize(uCameraPos - vWorldPos);

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
    TextureSamples textureSamples = sampleLandmassTextures(texUV, oceanMask, vWorldPos);

    // Get the interpolated surface normal (normalized sphere normal = outward direction)
    vec3 surfaceNormal = normalize(vWorldNormal);

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

    // Solar lighting (only affects day side) - use neutral color, no atmospheric scattering
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
        // L is direction FROM surface TO light, viewDir is FROM surface TO camera
        vec3 H = normalize(L + viewDir);
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
            // Metalness (derived from roughness)
            // ============================================================
            // Metalness = inverse of roughness, scaled to max 0.5
            // Smooth surfaces (low roughness) → higher metalness (up to 0.5)
            // Rough surfaces (high roughness) → lower metalness (down to 0.0)
            // This limits specular intensity for Earth's non-metallic surfaces
            float metalness = (1.0 - roughness) * 0.5; // Max metalness = 0.5 for Earth

            // ============================================================
            // GGX/Trowbridge-Reitz Distribution (D)
            // ============================================================
            // Controls the microfacet distribution based on roughness
            float denom = (NdotH * NdotH * (alpha2 - 1.0) + 1.0);
            float D = alpha2 / (PI * denom * denom);

            // ============================================================
            // Smith Geometry Function (G)
            // ============================================================
            // Accounts for self-shadowing and masking of microfacets
            float lambdaL = sqrt(1.0 + (alpha2 * (1.0 - NdotLClamped * NdotLClamped)) / (NdotLClamped * NdotLClamped));
            float lambdaV = sqrt(1.0 + (alpha2 * (1.0 - NdotV * NdotV)) / (NdotV * NdotV));
            float G = 1.0 / (lambdaL + lambdaV);

            // ============================================================
            // Fresnel (F) - Schlick approximation with metalness
            // ============================================================
            // F0 modulates specular intensity based on surface properties
            // Non-metallic (dirt, vegetation): F0 = 0.02
            // Metallic (smooth surfaces): F0 up to 0.5 (capped for Earth)
            // Metalness blends between non-metallic and metallic F0
            float F0_nonMetallic = 0.02; // Base F0 for Earth's non-reflective surfaces
            float F0_metallic = 0.5;     // Max F0 for Earth (capped to prevent too much metalness)
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

    // Base color with combined lighting + specular reflection (single-bounce atmospheric effect)
    // Use surfaceProps.color which has heightmap effects applied
    vec3 dayColor = surfaceProps.color * lighting + specularReflection;

    // Ocean Surface Effects
    // Real ocean color comes from light scattering within the water column

    if (oceanMask > 0.01)
    {
        // View direction (camera to surface)
        vec3 viewDir = normalize(uCameraPos - vWorldPos);

        // Sample actual bathymetry depth from ETOPO data (sinusoidal projection)
        // 0 = surface/land (masked), 1 = deepest (~11km Mariana Trench)
        // Use sinusoidal UV directly (texUV is already in sinusoidal space)
        // Land areas are masked to 0 in the texture, so this automatically excludes land
        float bathyDepth = texture2D(uBathymetryDepth, texUV).r;

        // Convert to meters (max ~11000m)
        // If depth is 0 (land), return 0; otherwise use actual depth
        float depthMeters = bathyDepth * 11000.0;
        depthMeters = max(depthMeters, 0.0); // Land areas are 0, ocean has minimum 0

        // Use sphere normal for lighting angle
        float oceanNdotL = max(dot(surfaceNormal, L), 0.0);

        // Calculate ocean color using ray-marched subsurface scattering
        // This uses actual bathymetry and real texture colors - no hardcoded blues
        // Use neutral sun color - atmospheric effects are handled by specular reflection
        vec3 oceanColor = calculateOceanColor(texUV, // For texture sampling during ray march
                                              depthMeters,
                                              surfaceNormal, // Base sphere normal
                                              vWorldPos,     // World position for procedural noise
                                              viewDir,
                                              L,           // Sun direction
                                              uLightColor, // Neutral sunlight color (no atmospheric scattering)
                                              oceanNdotL,
                                              tangent,   // Tangent vector (east) for wave normal computation
                                              bitangent, // Bitangent vector (north) for wave normal computation
                                              uTime      // For animated waves
        );

        // Coastal Blending
        // Smooth transition from land to ocean
        // Only blend on actual ocean areas (oceanMask > 0.7) to preserve landmass texture effects
        // This ensures landmass textures aren't overridden by ocean color
        float oceanBlend = smoothstep(0.7, 0.95, oceanMask);
        dayColor = mix(dayColor, oceanColor, oceanBlend);
    }

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