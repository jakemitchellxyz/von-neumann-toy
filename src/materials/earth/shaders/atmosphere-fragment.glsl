#version 120

varying vec2 vScreenUV; // Screen UV [0,1]

// Uniforms for ray reconstruction
uniform mat4 uInvViewProj; // Inverse(Projection * View) matrix
uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec3 uPlanetPos;
uniform float uPlanetRadius;
uniform float uAtmosphereRadius;
uniform sampler1D uDensityLUT;       // 1D lookup: normalized altitude -> normalized density
uniform float uMaxAltitude;          // Maximum altitude for texture lookup (meters)
uniform sampler2D uTransmittanceLUT; // 2D lookup: Bruneton (r, mu_s) -> RGB transmittance (optional)
uniform sampler2D uSingleScatterLUT; // 2D lookup: Single scattering (r, mu, mu_s) packed as 2D (optional)
uniform sampler2D uMultiscatterLUT;  // 2D lookup: Hillaire multiscattering (r, mu_s) -> RGB radiance (optional)
uniform bool uUseTransmittanceLUT;   // Whether transmittance LUT is available
uniform bool uUseSingleScatterLUT;   // Whether single scatter LUT is available
uniform bool uUseMultiscatterLUT;    // Whether multiscatter LUT is available

// ============================================================
// Signed Distance Field (SDF) Functions for Ray Marching
// ============================================================
// Consistent distance calculations for ray marching

// Sphere SDF: Returns signed distance to sphere surface
// pos: point in world space, center: sphere center, radius: sphere radius
// Returns: negative inside sphere, positive outside, zero on surface
float sdSphere(vec3 pos, vec3 center, float radius)
{
    return length(pos - center) - radius;
}

// Ray-Sphere Intersection: Computes intersection distances along a ray with a sphere
// ro: ray origin, rd: ray direction (normalized), center: sphere center, radius: sphere radius
// Returns: true if intersection exists, with t0 and t1 as entry/exit distances
bool raySphereIntersect(vec3 ro, vec3 rd, vec3 center, float radius, out float t0, out float t1)
{
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;

    if (disc < 0.0)
    {
        t0 = 0.0;
        t1 = 0.0;
        return false;
    }

    float h = sqrt(disc);
    t0 = -b - h; // Entry point
    t1 = -b + h; // Exit point
    return true;
}

// Physical constants
const float PI = 3.14159265359;
const float g0 = 9.80665;      // m/s^2
const float R_gas = 287.05287; // J/(kg·K) - specific gas constant for air

// US Standard Atmosphere 1976 - Layer definitions (in meters)
// Extends to 84,852m (mesopause), but we also model thermosphere decay

// US Standard Atmosphere 1976 - Layer definitions (in meters)
// Extends to 84,852m (mesopause), but we also model thermosphere decay

// Sea-level density for normalization
const float rho_sea_level = 1.225; // kg/m^3

// ============================================================
// Rayleigh Scattering Coefficients at Sea Level
// ============================================================
// Based on the Rayleigh scattering formula: β_R(λ) = (8π³(n²-1)²) / (3Nλ⁴)
// where n = refractive index of air (1.00029), N = molecular number density
//
// Standard wavelengths for RGB display:
//   Red:   680 nm  →  β_R = 5.802 × 10⁻⁶ m⁻¹
//   Green: 550 nm  →  β_R = 13.558 × 10⁻⁶ m⁻¹
//   Blue:  440 nm  →  β_R = 33.100 × 10⁻⁶ m⁻¹
//
// Reference: Bucholtz (1995) "Rayleigh-scattering calculations for the
// terrestrial atmosphere", Applied Optics 34(15):2765-2773
const vec3 BETA_R_SEA = vec3(5.802e-6, 13.558e-6, 33.100e-6);

// ============================================================
// Mie Scattering Coefficients at Sea Level
// ============================================================
// Mie scattering from aerosols is approximately wavelength-independent
// Typical clean atmosphere: 2.0 × 10⁻⁵ m⁻¹
// Hazy atmosphere: up to 10⁻⁴ m⁻¹
//
// Reference: Preetham et al. (1999) "A Practical Analytic Model for Daylight"
const vec3 BETA_M_SEA = vec3(2.0e-5); // Clean atmosphere aerosol scattering

// Scale height for exponential density falloff above 84km (thermosphere/exosphere)
const float H_SCALE_UPPER = 8500.0; // meters

// Mie anisotropy (forward scattering bias)
const float MIE_G = 0.76;

// Get atmospheric density at altitude using texture lookup OR USSA76 fallback
// When uMaxAltitude > 0, we use the real data from xlsx via texture lookup
// Otherwise, we fall back to the analytical USSA76 model
float getAtmosphereDensity(float altitude_m)
{
    if (altitude_m < 0.0)
        altitude_m = 0.0;

    // Use texture lookup if available (uMaxAltitude > 0 indicates loaded data)
    if (uMaxAltitude > 0.0)
    {
        // Normalize altitude to [0,1] for texture lookup
        float normalizedAlt = clamp(altitude_m / uMaxAltitude, 0.0, 1.0);
        // Sample density from 1D texture (returns normalized density relative to sea level)
        return texture1D(uDensityLUT, normalizedAlt).r;
    }

    // === FALLBACK: Analytical USSA76 model ===
    // CRITICAL: Ensure density goes to exactly zero at TOA
    // Clamp to atmosphere top (10,000km exosphere)
    float atmosphereTopMeters = 10000000.0; // 10,000km exosphere
    if (altitude_m >= atmosphereTopMeters)
    {
        return 0.0; // Exactly zero at and above TOA
    }

    // Above mesopause: use exponential decay based on thermosphere
    if (altitude_m > 84852.0)
    {
        // Density at 84,852m (end of USSA76 model)
        float rho_84km = 3.956420 / (R_gas * 214.65) / rho_sea_level; // ~0.000012
        // Exponential decay with scale height
        float density = rho_84km * exp(-(altitude_m - 84852.0) / H_SCALE_UPPER);

        // CRITICAL: Smooth fade-out near TOA to ensure exactly zero at boundary
        // This prevents black fog from tiny density values
        float fadeOutStart = atmosphereTopMeters * 0.95; // Start fading at 95% of TOA
        if (altitude_m > fadeOutStart)
        {
            float fadeOut = 1.0 - smoothstep(fadeOutStart, atmosphereTopMeters, altitude_m);
            density *= fadeOut;
        }

        return density;
    }

    // Find which USSA76 layer we're in and get parameters
    float h0, T0, P0, L;

    if (altitude_m < 11000.0)
    {
        // Layer 0: Troposphere
        h0 = 0.0;
        T0 = 288.15;
        P0 = 101325.0;
        L = -0.0065;
    }
    else if (altitude_m < 20000.0)
    {
        // Layer 1: Tropopause
        h0 = 11000.0;
        T0 = 216.65;
        P0 = 22632.06;
        L = 0.0;
    }
    else if (altitude_m < 32000.0)
    {
        // Layer 2: Stratosphere 1
        h0 = 20000.0;
        T0 = 216.65;
        P0 = 5474.889;
        L = 0.001;
    }
    else if (altitude_m < 47000.0)
    {
        // Layer 3: Stratosphere 2
        h0 = 32000.0;
        T0 = 228.65;
        P0 = 868.0187;
        L = 0.0028;
    }
    else if (altitude_m < 51000.0)
    {
        // Layer 4: Stratopause
        h0 = 47000.0;
        T0 = 270.65;
        P0 = 110.9063;
        L = 0.0;
    }
    else if (altitude_m < 71000.0)
    {
        // Layer 5: Mesosphere
        h0 = 51000.0;
        T0 = 270.65;
        P0 = 66.93887;
        L = -0.0028;
    }
    else
    {
        // Layer 6: Mesopause
        h0 = 71000.0;
        T0 = 214.65;
        P0 = 3.956420;
        L = -0.002;
    }

    float dh = altitude_m - h0;

    float T, P;

    if (abs(L) > 1e-6)
    {
        // Non-isothermal layer
        T = T0 + L * dh;
        P = P0 * pow(T / T0, -g0 / (L * R_gas));
    }
    else
    {
        // Isothermal layer
        T = T0;
        P = P0 * exp(-g0 * dh / (R_gas * T));
    }

    // Density from ideal gas law: rho = P / (R * T)
    float rho = P / (R_gas * T);

    // Return normalized density (relative to sea level)
    return rho / rho_sea_level;
}

// Legacy function name for compatibility - use raySphereIntersect instead
bool intersectSphere(vec3 ro, vec3 rd, vec3 center, float radius, out float t0, out float t1)
{
    return raySphereIntersect(ro, rd, center, radius, t0, t1);
}

// Rayleigh phase function (symmetric scattering from molecules)
float rayleighPhase(float mu)
{
    return (3.0 / (16.0 * PI)) * (1.0 + mu * mu);
}

// Henyey-Greenstein phase function for Mie scattering (aerosols, forward bias)
float miePhase(float mu)
{
    float g = MIE_G;
    float g2 = g * g;
    return (3.0 / (8.0 * PI)) * ((1.0 - g2) * (1.0 + mu * mu)) / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
}

// ============================================================
// Bruneton-style LUT Lookup Functions
// ============================================================
// Parameterization: r (distance from planet center), mu_s (cos(sun zenith angle))
// Reference: Bruneton & Neyret (2008) "Precomputed Atmospheric Scattering"

// Get normalized r coordinate for LUT lookup
// r: distance from planet center in meters
// planetRadiusMeters: planet radius in meters (must match LUT generation)
// atmosphereTopRadiusMeters: atmosphere top radius in meters (must match LUT generation)
float getLUTCoordR(float r, float planetRadiusMeters, float atmosphereTopRadiusMeters)
{
    // CRITICAL: Use the actual atmosphere radius, not a hardcoded value
    // This ensures the LUT coordinates match the generation exactly
    float atmosphereRadius = atmosphereTopRadiusMeters;

    // Normalize r to [0, 1] with nonlinear mapping (better resolution near surface)
    // Inverse of: r = planetRadius + u*u * (atmosphereRadius - planetRadius)
    // This matches the preprocessing: r = planetRadius + u*u * (atmosphereRadius - planetRadius)
    float u = sqrt((r - planetRadiusMeters) / (atmosphereRadius - planetRadiusMeters));
    return clamp(u, 0.0, 1.0);
}

// Get normalized mu_s coordinate for LUT lookup
// mu_s: cos(sun zenith angle) [-1, 1]
float getLUTCoordMuS(float mu_s)
{
    // Map mu_s from [-1, 1] to [0, 1]
    return clamp((mu_s + 1.0) * 0.5, 0.0, 1.0);
}

// Lookup transmittance from Bruneton-style precomputed LUT
// Returns RGB transmittance (exp(-tau)) for Rayleigh wavelengths
// r: distance from planet center in meters
// mu_s: cos(sun zenith angle) at point P
// planetRadiusMeters: planet radius in meters (must match LUT generation)
// atmosphereTopRadiusMeters: atmosphere top radius in meters (must match LUT generation)
vec3 lookupTransmittanceLUTBruneton(float r, float mu_s, float planetRadiusMeters, float atmosphereTopRadiusMeters)
{
    float lutU = getLUTCoordR(r, planetRadiusMeters, atmosphereTopRadiusMeters);
    float lutV = getLUTCoordMuS(mu_s);

    // Sample LUT
    return texture2D(uTransmittanceLUT, vec2(lutU, lutV)).rgb;
}

// Lookup single scattering from precomputed 3D LUT (packed as 2D)
// Returns RGB radiance from single scattering
// r: distance from planet center in meters
// mu: cos(view zenith angle) [-1, 1]
// mu_s: cos(sun zenith angle) [-1, 1]
// planetRadiusMeters: planet radius in meters (must match LUT generation)
// atmosphereTopRadiusMeters: atmosphere top radius in meters (must match LUT generation)
// LUT is packed as: width = R_res * mu_res, height = mu_s_res
vec3 lookupSingleScatterLUT(float r, float mu, float mu_s, float planetRadiusMeters, float atmosphereTopRadiusMeters)
{
    // LUT resolution (must match preprocessing)
    const float SINGLE_R_RES = 128.0;
    const float SINGLE_MU_RES = 64.0;
    const float SINGLE_MU_S_RES = 64.0;

    // Get normalized coordinates
    float lutR = getLUTCoordR(r, planetRadiusMeters, atmosphereTopRadiusMeters); // [0, 1]
    float lutMu = clamp((mu + 1.0) * 0.5, 0.0, 1.0);                             // Map mu from [-1,1] to [0,1]
    float lutMuS = clamp((mu_s + 1.0) * 0.5, 0.0, 1.0);                          // Map mu_s from [-1,1] to [0,1]

    // Convert to texture indices
    float r_idx = lutR * (SINGLE_R_RES - 1.0);
    float mu_idx = lutMu * (SINGLE_MU_RES - 1.0);
    float mu_s_idx = lutMuS * (SINGLE_MU_S_RES - 1.0);

    // Pack into 2D texture coordinates
    // x = r_idx + mu_idx * R_resolution
    float lutX = (r_idx + mu_idx * SINGLE_R_RES) / (SINGLE_R_RES * SINGLE_MU_RES);
    float lutY = mu_s_idx / SINGLE_MU_S_RES;

    // Sample LUT
    return texture2D(uSingleScatterLUT, vec2(lutX, lutY)).rgb;
}

// Lookup multiscattering from Hillaire-style precomputed LUT
// Returns RGB radiance from multiscattering
// r: distance from planet center in meters
// mu_s: cos(sun zenith angle) at point P
// planetRadiusMeters: planet radius in meters (must match LUT generation)
// atmosphereTopRadiusMeters: atmosphere top radius in meters (must match LUT generation)
vec3 lookupMultiscatterLUTHillaire(float r, float mu_s, float planetRadiusMeters, float atmosphereTopRadiusMeters)
{
    float lutU = getLUTCoordR(r, planetRadiusMeters, atmosphereTopRadiusMeters);
    float lutV = getLUTCoordMuS(mu_s);

    // Sample LUT
    return texture2D(uMultiscatterLUT, vec2(lutU, lutV)).rgb;
}
void main()
{
    // Reconstruct view ray from screen pixel
    vec2 ndc = vScreenUV * 2.0 - 1.0;

    // CRITICAL: The fullscreen quad is drawn at the near plane, but we need to reconstruct
    // rays as if the camera were at the correct distance for the FOV.
    // Unproject both near and far plane points to get correct ray origin and direction.

    // Unproject near plane point (NDC z = -1)
    vec4 nearClip = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec3 nearWorld = nearClip.xyz / nearClip.w;

    // Unproject far plane point (NDC z = 1)
    vec4 farClip = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vec3 farWorld = farClip.xyz / farClip.w;

    // Use near plane point as ray origin (where the fullscreen quad actually is)
    // This ensures the FOV is correct even though the quad is drawn at the near plane
    vec3 rayOrigin = nearWorld;
    vec3 rayDir = normalize(farWorld - nearWorld);

    // Scale: meters per display unit
    // Convert display units to meters using the planet radius uniform
    // uPlanetRadius is in display units, we need to convert to meters
    // Use WGS 84 mean radius as reference (more accurate than simple sphere)
    const float WGS84_MEAN_RADIUS_M = (2.0 * 6378137.0 + 6356752.314245) / 3.0; // ≈ 6371008.771415

    // CRITICAL: Use planet radius directly from uniform
    // This MUST match the radius used for rendering the planet ellipsoid
    // If there's a mismatch, atmosphere will appear inside or outside the planet
    float planetRadiusDisplay = uPlanetRadius;

    // Safety check: ensure radius is positive (should always be, but check for errors)
    if (planetRadiusDisplay <= 0.0)
    {
        // Invalid radius - output transparent
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    float scale = WGS84_MEAN_RADIUS_M / planetRadiusDisplay;

    // Planet radius in meters (for LUT lookups and calculations)
    // This should always equal WGS84_MEAN_RADIUS_M, but we compute it for clarity
    float planetRadiusMeters = planetRadiusDisplay * scale;

    // Scattering coefficients scaled to display units
    vec3 betaR = BETA_R_SEA * scale;
    vec3 betaM = BETA_M_SEA * scale;

    // CRITICAL: Use the atmosphere radius passed from C++ (uAtmosphereRadius)
    // This ensures consistency with the planet rendering
    // C++ calculates: atmosphereRadius = displayRadius * (RADIUS_EARTH_KM + EXOSPHERE_HEIGHT_KM) / RADIUS_EARTH_KM
    float atmosphereTopRadiusDisplay = abs(uAtmosphereRadius); // Use the uniform, ensure positive

    // Convert atmosphere radius to meters for altitude checks
    // This should match: (planetRadiusDisplay + atmosphereHeightDisplay) * scale
    float atmosphereTopRadiusMeters = atmosphereTopRadiusDisplay * scale;

    // Atmosphere height in meters (for altitude clamping)
    float atmosphereTopMeters = atmosphereTopRadiusMeters - planetRadiusMeters;

    // Intersect ray with atmosphere sphere
    // Use computed atmosphere radius to ensure it matches our calculations
    float tAtmo0, tAtmo1;
    bool hitAtmosphere = intersectSphere(rayOrigin, rayDir, uPlanetPos, atmosphereTopRadiusDisplay, tAtmo0, tAtmo1);

    // Check planet (Earth) intersection
    // CRITICAL: Use the same planet radius that's used for rendering
    // This must match exactly with the planet sphere rendering
    float tPlanet0, tPlanet1;
    bool hitPlanet = intersectSphere(rayOrigin, rayDir, uPlanetPos, planetRadiusDisplay, tPlanet0, tPlanet1);

    // CRITICAL: Ensure planet radius matches what's being rendered
    // If there's a mismatch, the atmosphere will appear inside the planet
    // Verify: planetRadiusDisplay should equal uPlanetRadius (or abs(uPlanetRadius))
    // If not, there's a unit mismatch between rendering and shader

    // If ray doesn't intersect atmosphere at all, return transparent (no rendering)
    if (!hitAtmosphere)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Determine ray segment through atmosphere
    // tStart: where we enter atmosphere (or 0 if camera is inside)
    // tEnd: where we exit atmosphere or hit planet
    float tStart = max(tAtmo0, 0.0);
    float tEnd = tAtmo1;

    // If ray hits planet, terminate at planet surface
    // CRITICAL: Use a small epsilon to ensure we don't sample inside the planet
    // This prevents atmosphere from rendering inside the planet surface
    const float PLANET_SURFACE_EPSILON = 0.001; // Small epsilon in display units
    if (hitPlanet && tPlanet0 > 0.0)
    {
        // Terminate slightly before planet surface to avoid sampling inside
        tEnd = min(tEnd, tPlanet0 - PLANET_SURFACE_EPSILON);
    }

    // CRITICAL: Clamp atmosphere evaluation to bounds
    // Never evaluate outside atmosphere - this prevents black fog
    // atmosphereTopMeters, atmosphereTopRadiusMeters, and atmosphereTopRadiusDisplay are already computed above

    // Check if ray is entirely outside atmosphere bounds
    vec3 startPos = rayOrigin + rayDir * tStart;
    vec3 endPos = rayOrigin + rayDir * tEnd;
    float startDist = length(startPos - uPlanetPos) * scale;
    float endDist = length(endPos - uPlanetPos) * scale;

    // Early-out if entirely outside atmosphere
    if (startDist > atmosphereTopRadiusMeters && endDist > atmosphereTopRadiusMeters)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Clamp tEnd to atmosphere top
    // Find where ray exits atmosphere top
    float tAtmoTop0, tAtmoTop1;
    bool hitAtmoTop = intersectSphere(rayOrigin, rayDir, uPlanetPos, atmosphereTopRadiusDisplay, tAtmoTop0, tAtmoTop1);
    if (hitAtmoTop && tAtmoTop1 > tStart)
    {
        tEnd = min(tEnd, tAtmoTop1);
    }

    // Check for valid atmosphere segment
    float segmentLength = tEnd - tStart;
    float fadeFactor = 1.0;
    if (segmentLength <= 0.0)
    {
        // Very short or no segment - return transparent
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Ray marching through atmosphere

    const int VIEW_STEPS = 64;  // Steps along view ray (increased for smoother gradients)
    const int LIGHT_STEPS = 24; // Steps toward sun for shadow/transmittance
    float pathLength = tEnd - tStart;
    float stepSize = pathLength / float(VIEW_STEPS);

    vec3 totalRayleigh = vec3(0.0);
    vec3 totalMie = vec3(0.0);
    float opticalDepthR = 0.0;
    float opticalDepthM = 0.0;

    // CRITICAL: Accumulate total transmittance along view ray
    // This is used to attenuate background (stars/space)
    vec3 totalViewTransmittance = vec3(1.0); // Start with full transmittance

    for (int i = 0; i < VIEW_STEPS; i++)
    {
        // Sample point along view ray
        float t = tStart + (float(i) + 0.5) * stepSize;
        vec3 samplePos = rayOrigin + rayDir * t;

        // Altitude above planet surface (in display units, then meters)
        // CRITICAL: Use SDF for consistent distance calculations
        // Distance from planet center = length(samplePos - uPlanetPos)
        // Altitude = distance - radius
        float sdfDist = sdSphere(samplePos, uPlanetPos, planetRadiusDisplay);
        float distFromCenter = length(samplePos - uPlanetPos);
        float distFromCenterMeters = distFromCenter * scale;

        // CRITICAL: Check if point is inside planet BEFORE calculating altitude
        // Use a small epsilon to account for floating point precision
        const float EPSILON_METERS = 100.0; // 100m tolerance for floating point errors
        if (distFromCenterMeters < planetRadiusMeters - EPSILON_METERS)
        {
            // Point is definitely inside planet - skip immediately
            continue;
        }

        float altDisplay = distFromCenter - planetRadiusDisplay; // Use absolute radius
        float altMeters = altDisplay * scale;

        // CRITICAL: Clamp to atmosphere bounds - never evaluate outside
        // Skip if below surface (with tolerance) or above atmosphere top
        // Use negative tolerance to ensure we don't sample inside planet
        if (altMeters < -EPSILON_METERS || altMeters > atmosphereTopMeters)
            continue;

        // Get atmospheric density at this altitude (USSA76 model)
        float density = getAtmosphereDensity(altMeters);

        // CRITICAL: Ensure density goes to exactly zero at TOA
        // Apply smooth fade-out near atmosphere top to prevent black fog
        float fadeOutStart = atmosphereTopMeters * 0.95; // Start fading at 95% of TOA
        if (altMeters > fadeOutStart)
        {
            float fadeOut = 1.0 - smoothstep(fadeOutStart, atmosphereTopMeters, altMeters);
            density *= fadeOut;
        }

        // Convert step size from display units to meters for correct optical depth calculation
        float stepSizeMeters = stepSize * scale;

        // CRITICAL: Always accumulate transmittance, even if density is zero
        // Transmittance represents how much background light passes through
        // This must be computed for every step, regardless of scattering
        vec3 tauStep = BETA_R_SEA * density * stepSizeMeters + BETA_M_SEA * density * stepSizeMeters;
        totalViewTransmittance *= exp(-tauStep);

        // CRITICAL: Early-out when density is effectively zero
        // This prevents accumulating scattering without density (black fog)
        // But transmittance has already been accumulated above
        const float MIN_DENSITY_THRESHOLD = 1e-6;
        if (density < MIN_DENSITY_THRESHOLD)
            continue;

        // Accumulate optical depth along view ray (for scattering computation)
        opticalDepthR += density * stepSizeMeters;
        opticalDepthM += density * stepSizeMeters;

        // Light ray to sun
        // (accounting for absorption and scattering along the way)

        float tSun0, tSun1;
        // Use computed atmosphere radius for sun ray intersection
        intersectSphere(samplePos, uSunDir, uPlanetPos, atmosphereTopRadiusDisplay, tSun0, tSun1);

        // If sun ray doesn't exit atmosphere properly, skip
        if (tSun1 <= 0.0)
            continue;

        float sunPathLength = tSun1;
        float sunStep = sunPathLength / float(LIGHT_STEPS);
        float sunStepMeters = sunStep * scale; // Convert display units to meters for optical depth

        float lightDepthR = 0.0;
        float lightDepthM = 0.0;
        float sunlightFraction = 1.0; // How much of sun ray reaches this point (0-1)

        for (int j = 0; j < LIGHT_STEPS; j++)
        {
            vec3 lightSamplePos = samplePos + uSunDir * (float(j) + 0.5) * sunStep;
            float lightAltDisplay = length(lightSamplePos - uPlanetPos) - planetRadiusDisplay;
            float lightAltMeters = lightAltDisplay * scale;

            // Soft shadow: light diminishes as ray approaches/enters planet
            // Instead of binary shadow, use smooth falloff near surface
            if (lightAltMeters < 0.0)
            {
                // Ray hit planet - fade smoothly instead of hard cutoff
                // Calculate how much of the path completed before hitting surface
                float pathProgress = float(j) / float(LIGHT_STEPS);
                // Smooth fade: don't go to zero, fade to small value for natural falloff
                // Use path progress with minimum value to avoid hard black
                sunlightFraction = max(0.1, pathProgress * 0.5);
                break;
            }
            else if (lightAltMeters < 5000.0)
            {
                // Near-surface: gradual dimming (atmospheric extinction + soft shadow)
                float surfaceProximity = 1.0 - (lightAltMeters / 5000.0);
                // Ensure minimum sunlight fraction for natural falloff (no hard black)
                // Fade from 1.0 to 0.3 (not zero) as we approach surface
                sunlightFraction *= mix(0.3, 1.0, 1.0 - surfaceProximity * 0.7);
            }

            // Get density from standard atmosphere model (normalized 0-1 relative to sea level)
            float lightDensity = getAtmosphereDensity(lightAltMeters);

            // Accumulate optical depth: tau = integral(beta * density * ds)
            // beta has units m^-1, density is dimensionless (normalized), ds is in meters
            // So we need path length in meters, not display units
            lightDepthR += lightDensity * sunStepMeters;
            lightDepthM += lightDensity * sunStepMeters;
        }

        // Wavelength-Dependent Sun Attenuation
        //   Red (680nm):   5.802e-6 m^-1  (scatters least)
        //   Green (550nm): 13.558e-6 m^-1
        //   Blue (440nm):  33.100e-6 m^-1 (scatters most)
        //
        // When light travels through more atmosphere (longer path), blue scatters away
        // more than red, leaving the transmitted light reddened (sunset effect).

        // Compute sun transmittance: use LUT if available, otherwise ray march
        vec3 sunColorAtPoint;

        // Check if transmittance LUT is available (sampler2D is bound if LUT loaded)
        // We detect this by checking if we can sample the LUT (if uniform is set)
        // For now, always try LUT first, fall back to ray marching if needed

        // Calculate Bruneton parameters for LUT lookup
        // CRITICAL: r must be distance from planet center in meters
        // This MUST match how the LUT was generated: r ranges from planetRadius to atmosphereRadius
        float distFromCenterDisplay = length(samplePos - uPlanetPos);
        float r = distFromCenterDisplay * scale; // Convert to meters (distance from planet center)

        // CRITICAL: Clamp r to valid LUT range [planetRadiusMeters, atmosphereTopRadiusMeters]
        // The LUT was generated with r values in this range
        r = max(planetRadiusMeters, min(r, atmosphereTopRadiusMeters));

        // mu_s: cos(sun zenith angle) = dot(sun direction, vertical)
        vec3 vertical = normalize(samplePos - uPlanetPos); // Radial direction (upward at this point)
        float mu_s = dot(uSunDir, vertical);               // [-1, 1]: -1 = sun below, 1 = sun overhead

        // Use LUT if available, otherwise compute via ray marching
        if (uUseTransmittanceLUT)
        {
            // Use Bruneton-style precomputed transmittance LUT (much faster than ray marching)
            // Pass planetRadiusMeters and atmosphereTopRadiusMeters so LUT coordinates match generation
            vec3 lutTransmittance =
                lookupTransmittanceLUTBruneton(r, mu_s, planetRadiusMeters, atmosphereTopRadiusMeters);

            // If LUT returned black (sun ray hit planet in LUT computation), skip this sample
            // This prevents accumulating black fog in upper atmosphere
            float lutBrightness = max(max(lutTransmittance.r, lutTransmittance.g), lutTransmittance.b);
            if (lutBrightness < 0.001)
            {
                // LUT indicates no valid light path - skip to avoid accumulating darkness
                continue;
            }

            sunColorAtPoint = lutTransmittance * sunlightFraction;
        }
        else
        {
            // Fallback: compute via ray marching (original method)
            vec3 tauSun = BETA_R_SEA * lightDepthR + BETA_M_SEA * lightDepthM;
            sunColorAtPoint = exp(-tauSun) * sunlightFraction;
        }

        // NOTE: View transmittance is now accumulated per-step above
        // We don't need to recompute it here - totalViewTransmittance is already correct

        // Dynamic Scattering Factor
        // 1. How much sunlight reaches this point (sunlightFraction)
        // 2. Position relative to terminator (sunAngle)
        // 3. Altitude (higher = more scattering visible)
        // Note: vertical was already computed above for sunZenithAngle calculation

        float sunAngle = dot(vertical, uSunDir); // -1 = far dark side, 0 = terminator, +1 = sun side
        float altFactor = smoothstep(0.0, 60000.0, altMeters);

        // Base scattering scales with how much sun reaches this point
        // Plus a small terminator glow for atmospheric refraction effect
        float terminatorGlow = smoothstep(-0.2, 0.1, sunAngle) * (1.0 - sunlightFraction) * 0.15;
        float scatterFactor = sunlightFraction + terminatorGlow * (0.5 + 0.5 * altFactor);

        // ===== MULTISCATTERING: Use Hillaire LUT =====
        // Light that has been scattered multiple times through the atmosphere
        // Uses precomputed iterative energy redistribution from Hillaire method
        vec3 multiScatterRayleigh = vec3(0.0);
        vec3 multiScatterMie = vec3(0.0);

        if (uUseMultiscatterLUT)
        {
            // Use Hillaire-style precomputed multiscattering LUT
            // r and mu_s were already computed above for transmittance lookup
            // Pass planetRadiusMeters and atmosphereTopRadiusMeters so LUT coordinates match generation
            vec3 multiscatterRadiance =
                lookupMultiscatterLUTHillaire(r, mu_s, planetRadiusMeters, atmosphereTopRadiusMeters);

            // Multiscatter LUT contains total radiance (Rayleigh + Mie combined)
            // Split approximately: Rayleigh dominates blue, Mie is more uniform
            // Approximate split: 70% Rayleigh, 30% Mie (based on scattering coefficients)
            multiScatterRayleigh = multiscatterRadiance * 0.7;
            multiScatterMie = multiscatterRadiance * 0.3;
        }
        else
        {
            // Fallback: use simplified multiscattering approximation
            // This is a simplified version that doesn't require the LUT
            // For better quality, use the multiscatter LUT
            float multiscatterFactor = density * 0.1; // Scale with density
            multiScatterRayleigh = sunColorAtPoint * multiscatterFactor * 0.7;
            multiScatterMie = sunColorAtPoint * multiscatterFactor * 0.3;
        }

        // Multiscattering strength is altitude-dependent: stronger at lower altitudes (more dense)
        // At sea level (density ~1.0): full multiscattering
        // At high altitude (density ~0.0): minimal multiscattering
        // CRITICAL: Multiscatter LUT should be zero at TOA - if not, it adds dark energy
        float multiscatterWeight = clamp(density * 0.5, 0.0, 0.5); // Scale with density, max 0.5

        // CRITICAL: Correct compositing formula
        // Atmosphere is: L = L_background * T + L_scattered
        // We are computing L_scattered here (additive emission)
        // Transmittance T is applied to background in compositing, not here
        // So we only accumulate scattered light, weighted by density

        // ===== SINGLE SCATTERING: Use LUT if available =====
        vec3 singleScatterRayleigh = vec3(0.0);
        vec3 singleScatterMie = vec3(0.0);

        if (uUseSingleScatterLUT)
        {
            // Use precomputed single-scatter LUT
            // mu: cos(view zenith angle) = dot(view direction, vertical)
            // vertical was already computed above for sunZenithAngle calculation
            float mu = dot(rayDir, vertical); // cos(angle between view ray and vertical)

            // Lookup single scatter radiance from LUT
            // Pass planetRadiusMeters and atmosphereTopRadiusMeters so LUT coordinates match generation
            vec3 singleScatterRadiance =
                lookupSingleScatterLUT(r, mu, mu_s, planetRadiusMeters, atmosphereTopRadiusMeters);

            // Single scatter LUT contains total radiance (Rayleigh + Mie combined)
            // Split approximately: Rayleigh dominates blue, Mie is more uniform
            // Approximate split: 70% Rayleigh, 30% Mie (based on scattering coefficients)
            singleScatterRayleigh = singleScatterRadiance * 0.7;
            singleScatterMie = singleScatterRadiance * 0.3;
        }
        else
        {
            // Fallback: use simplified single scattering computation
            // This is a simplified version that doesn't require the LUT
            // For better quality, use the single scatter LUT
            singleScatterRayleigh = sunColorAtPoint * scatterFactor;
            singleScatterMie = sunColorAtPoint * scatterFactor;
        }

        // Combine single and multiscattering
        // Both are additive contributions (emission), not multiplicative (transmittance)
        vec3 totalScatterRayleigh = singleScatterRayleigh + multiScatterRayleigh * multiscatterWeight;
        vec3 totalScatterMie = singleScatterMie + multiScatterMie * multiscatterWeight;

        // Accumulate in-scattered light at this sample point
        // Use step size in meters for correct accumulation
        // CRITICAL: Only accumulate if contribution has meaningful light - skip black/dark values
        // This prevents accumulating darkness that creates fog and hides the sun
        vec3 contributionRayleigh = density * totalScatterRayleigh * stepSizeMeters;
        vec3 contributionMie = density * totalScatterMie * stepSizeMeters;

        // Check if contribution has meaningful brightness - if black or near-black, skip accumulation
        // Use a threshold to avoid accumulating tiny values that effectively contribute nothing
        float brightnessR = max(max(contributionRayleigh.r, contributionRayleigh.g), contributionRayleigh.b);
        float brightnessM = max(max(contributionMie.r, contributionMie.g), contributionMie.b);

        // Only accumulate if contribution adds meaningful light (not darkness or negligible values)
        // Threshold prevents accumulating very small values that accumulate to black fog
        const float MIN_CONTRIBUTION_BRIGHTNESS = 1e-8;
        if (brightnessR > MIN_CONTRIBUTION_BRIGHTNESS)
        {
            totalRayleigh += contributionRayleigh;
        }
        if (brightnessM > MIN_CONTRIBUTION_BRIGHTNESS)
        {
            totalMie += contributionMie;
        }
    }
    // ===== PHASE FUNCTIONS =====
    // How light scatters based on angle between view direction and sun direction
    float mu = dot(rayDir, uSunDir); // cos(scattering angle)
    float phaseR = rayleighPhase(mu);
    float phaseM = miePhase(mu);

    // For multi-scattered light, use more isotropic phase (light comes from all directions)
    // Blend between directional phase and isotropic (uniform) phase
    float isotropicPhase = 1.0 / (4.0 * PI);
    float multiScatterPhaseR = mix(isotropicPhase, phaseR, 0.7); // 70% directional, 30% isotropic
    float multiScatterPhaseM = mix(isotropicPhase, phaseM, 0.5); // 50% directional, 50% isotropic

    // Solar intensity
    float sunIntensity = 40.0;

    // ===== FINAL SCATTERED LIGHT =====
    // Combine Rayleigh (blue sky) and Mie (hazy/white around sun) scattering
    // Use the blended phase functions that account for multi-scatter
    //
    // Rayleigh scattering naturally creates reddening at sunset:
    // - Blue light (440nm) scatters more (BETA_R_SEA.b = 33.1e-6 m^-1)
    // - Red light (680nm) scatters less (BETA_R_SEA.r = 5.8e-6 m^-1)
    // - When sun path is long (sunset), blue scatters away more, leaving red/orange
    // - This reddening is already in sunColorAtPoint via exp(-tauSun)
    // - Use standard atmosphere model coefficients directly (in m^-1)
    vec3 scatter =
        sunIntensity * (totalRayleigh * BETA_R_SEA * multiScatterPhaseR + totalMie * BETA_M_SEA * multiScatterPhaseM);

    // ============================================================
    // Tone Mapping for Physically-Based Atmospheric Scattering
    // ============================================================
    // Scale factor to convert from physical units to visible range
    // This accounts for the thin atmosphere shell (100km) relative to display scale
    scatter *= 150.0;

    // Exposure tone mapping with saturation preservation
    float exposure = 1.2;
    vec3 tonemapped = vec3(1.0) - exp(-scatter * exposure);

    // Preserve color saturation through tone mapping
    // Standard exp tone mapping desaturates highlights
    float preBrightness = max(max(scatter.r, scatter.g), scatter.b);
    float postBrightness = max(max(tonemapped.r, tonemapped.g), tonemapped.b);
    if (preBrightness > 0.001 && postBrightness > 0.001)
    {
        // Restore some of the original color ratio
        vec3 colorRatio = scatter / preBrightness;
        tonemapped = mix(tonemapped, colorRatio * postBrightness, 0.3);
    }
    scatter = tonemapped;

    // CRITICAL: Correct compositing for atmosphere overlay
    // Formula: L = L_background * T + L_scattered
    //
    // With standard alpha blending GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA:
    //   final = src * alpha + dst * (1-alpha)
    //
    // We want: final = scattered + background * transmittance
    //
    // To achieve this with standard blending, we need:
    //   src * alpha = scattered
    //   dst * (1-alpha) = background * T
    //   So: (1-alpha) = T, meaning alpha = 1-T
    //   And: src = scattered / alpha = scattered / (1-T)
    //
    // However, when T ≈ 1 (no atmosphere), alpha ≈ 0, causing division issues.
    // Solution: Only output atmosphere when there's meaningful scattering OR attenuation.

    // Compute average transmittance (for alpha channel)
    float avgTransmittance = (totalViewTransmittance.r + totalViewTransmittance.g + totalViewTransmittance.b) / 3.0;

    // Scattered light brightness
    float scatterBrightness = max(max(scatter.r, scatter.g), scatter.b);

    // CRITICAL: Only render atmosphere when there's either:
    // 1. Visible scattered light (scatterBrightness > threshold), OR
    // 2. Significant transmittance attenuation (T < 0.99)
    // This prevents black pixels when there's no atmosphere

    const float MIN_SCATTER_BRIGHTNESS = 0.001;
    const float MIN_ATTENUATION = 0.99; // Only render if transmittance < 99%

    bool hasScattering = scatterBrightness > MIN_SCATTER_BRIGHTNESS;
    bool hasAttenuation = avgTransmittance < MIN_ATTENUATION;

    if (!hasScattering && !hasAttenuation)
    {
        // No atmosphere effect - output transparent to avoid darkening
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Compute alpha based on transmittance
    // Alpha = 1 - T (how much atmosphere blocks background)
    float alpha = 1.0 - avgTransmittance;

    // CRITICAL: Scale scattered light to account for alpha blending
    // When alpha is low (high transmittance), we need brighter scattered light
    // When alpha is high (low transmittance), scattered light should be normal
    // This ensures scattered light is always visible
    if (alpha > 0.001)
    {
        // Scale scattered light so it shows correctly: src = scattered / alpha
        // This compensates for the alpha blending formula
        scatter = scatter / max(alpha, 0.01); // Prevent division by zero
    }
    else if (hasScattering)
    {
        // Very high transmittance but we have scattering - use minimum alpha
        alpha = 0.1;               // Small alpha to allow scattered light through
        scatter = scatter / alpha; // Scale accordingly
    }

    // Clamp alpha to valid range
    alpha = clamp(alpha, 0.0, 1.0);

    // CRITICAL: Ensure we never output black pixels
    // If scattered light is black or near-black, output transparent instead
    scatterBrightness = max(max(scatter.r, scatter.g), scatter.b);
    if (scatterBrightness < MIN_SCATTER_BRIGHTNESS)
    {
        // No visible scattered light - only output if there's significant attenuation
        if (!hasAttenuation)
        {
            // No scattering and no attenuation - output transparent
            gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
            return;
        }
        // There's attenuation but no scattering - set scattered light to very small value
        // This prevents black pixels while still allowing transmittance to work
        scatter = vec3(0.001); // Tiny non-zero value to prevent black
    }

    // Apply fade factor for natural falloff (removes hard black sections)
    scatter *= fadeFactor;
    alpha *= fadeFactor;

    // Final safety check: never output black with non-zero alpha
    scatterBrightness = max(max(scatter.r, scatter.g), scatter.b);
    if (scatterBrightness < 0.0001 && alpha > 0.01)
    {
        // Black color with significant alpha would darken the scene
        // Reduce alpha to prevent darkening
        alpha = min(alpha, scatterBrightness * 1000.0); // Scale alpha down with brightness
    }

    gl_FragColor = vec4(scatter, alpha);
}