#version 120
#extension GL_ARB_shader_texture_lod : enable

// Atmosphere fragment shader
// Renders atmosphere using SDF-based cone marching with transmittance and scattering LUTs

varying vec2 vTexCoord;
varying vec3 vRayDir; // Ray direction in world space (normalized)

uniform vec3 uCameraPos;         // Camera position in world space
uniform vec3 uPlanetCenter;      // Planet center position
uniform float uPlanetRadius;     // Planet radius
uniform float uAtmosphereRadius; // Atmosphere outer radius
uniform vec3 uSunDir;            // Sun direction (normalized)
uniform vec3 uSunColor;          // Sun color/intensity

// LUT textures
uniform sampler2D uTransmittanceLUT; // Transmittance LUT (mu_sun x height)
uniform sampler2D uScatteringLUT;    // Scattering LUT (mu_sun x height x scattering_angle)
uniform float uDebugMode;            // Debug mode: 1.0 = solid color test, 2.0 = debug march, 0.0 = normal

// LUT dimensions
const float LUT_TRANSMITTANCE_WIDTH = 256.0;
const float LUT_TRANSMITTANCE_HEIGHT = 128.0;
const float LUT_SCATTERING_WIDTH = 256.0;
const float LUT_SCATTERING_HEIGHT = 128.0;
const float LUT_SCATTERING_DEPTH = 32.0; // Scattering angle resolution

const float PI = 3.14159265359;

// Atmospheric scattering constants (industry standard values, scaled for visibility)
const float RAYLEIGH_SCALE_HEIGHT = 8000.0; // meters - scale height for Rayleigh scattering
const float MIE_SCALE_HEIGHT = 1200.0;      // meters - scale height for Mie scattering
// Increased coefficients significantly for visibility (industry values are very small)
const float RAYLEIGH_COEFF = 5.8e-3; // Rayleigh scattering coefficient (increased from 5.8e-6)
const float MIE_COEFF = 2.0e-2;      // Mie scattering coefficient (increased from 2.0e-5)
const float MIE_G = 0.76;            // Mie asymmetry factor (forward scattering)

// Rayleigh phase function: (3/16π) * (1 + cos²θ)
float rayleighPhase(float cosTheta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

// Mie phase function (Henyey-Greenstein)
float miePhase(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(max(denom, 0.0001), 1.5));
}

// Atmospheric density at height h (exponential falloff)
float atmosphericDensity(float height, float scaleHeight)
{
    return exp(-height / scaleHeight);
}

// Compute atmosphere SDF (signed distance field)
float atmosphereSDF(vec3 pos, vec3 planetCenter, float atmosphereRadius)
{
    float dist = length(pos - planetCenter);
    return dist - atmosphereRadius; // Negative inside (dist < radius), positive outside
}

// Sample transmittance LUT
// muSun: cosine of angle between sun direction and ray direction
// height: normalized height [0,1] where 0 = surface, 1 = top of atmosphere
vec3 sampleTransmittanceLUT(float muSun, float height)
{
    muSun = clamp(muSun, -1.0, 1.0);
    height = clamp(height, 0.0, 1.0);

    float u = (muSun + 1.0) * 0.5;
    float v = 1.0 - height; // Invert so 0 = top, 1 = surface

    vec2 lutUV = vec2(u, v) + vec2(0.5 / LUT_TRANSMITTANCE_WIDTH, 0.5 / LUT_TRANSMITTANCE_HEIGHT);
    vec3 transmittance = texture2D(uTransmittanceLUT, lutUV).rgb;

    return clamp(transmittance, vec3(0.0), vec3(10.0));
}

// Sample scattering LUT
// muSun: cosine of angle between sun and ray direction
// height: normalized height [0,1]
// nu: cosine of scattering angle (angle between view and sun directions)
// The scattering LUT is 2D: (mu_sun x height) with RGB channels encoding scattering at different angles
vec3 sampleScatteringLUT(float muSun, float height, float nu)
{
    muSun = clamp(muSun, -1.0, 1.0);
    height = clamp(height, 0.0, 1.0);
    nu = clamp(nu, -1.0, 1.0);

    float u = (muSun + 1.0) * 0.5;
    float v = 1.0 - height; // Invert so 0 = top, 1 = surface

    vec2 lutUV = vec2(u, v) + vec2(0.5 / LUT_SCATTERING_WIDTH, 0.5 / LUT_SCATTERING_HEIGHT);

    // Sample scattering LUT
    // RGB channels encode scattering at different angles (typically forward, side, backward)
    // R = forward scattering (nu ≈ 1), G = side scattering (nu ≈ 0), B = backward scattering (nu ≈ -1)
    vec3 scattering = texture2D(uScatteringLUT, lutUV).rgb;

    // Interpolate between scattering angles based on nu
    // nu = 1.0 (forward) -> use R channel
    // nu = 0.0 (side) -> use G channel
    // nu = -1.0 (backward) -> use B channel
    float nuClamped = clamp(nu, -1.0, 1.0);
    vec3 interpolatedScattering;

    if (nuClamped >= 0.0)
    {
        // Forward to side scattering: blend R and G
        float t = nuClamped; // [0, 1] where 0 = side, 1 = forward
        interpolatedScattering = mix(vec3(scattering.g), vec3(scattering.r), t);
    }
    else
    {
        // Side to backward scattering: blend G and B
        float t = -nuClamped; // [0, 1] where 0 = side, 1 = backward
        interpolatedScattering = mix(vec3(scattering.g), vec3(scattering.b), t);
    }

    return interpolatedScattering;
}

// Compute transmittance along a ray through the atmosphere using SDF marching
// Returns transmittance and optionally the hit point
vec3 computeTransmittanceAlongRay(vec3 rayOrigin,
                                  vec3 rayDir,
                                  vec3 planetCenter,
                                  float planetRadius,
                                  float atmosphereRadius,
                                  out float hitT,
                                  out bool hitSurface)
{
    hitT = -1.0;
    hitSurface = false;

    // Find intersection with atmosphere boundary
    vec3 oc = rayOrigin - planetCenter;
    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - atmosphereRadius * atmosphereRadius;
    float disc = b * b - c;

    if (disc < 0.0)
    {
        return vec3(1.0); // No intersection, full transmittance
    }

    float h = sqrt(disc);
    float t0 = max(-b - h, 0.0);
    float t1 = -b + h;

    if (t1 < 0.0)
    {
        return vec3(1.0); // Ray behind atmosphere
    }

    // Check for surface intersection
    float planetDisc = b * b - (dot(oc, oc) - planetRadius * planetRadius);
    float surfaceT = -1.0;
    if (planetDisc >= 0.0)
    {
        float planetH = sqrt(planetDisc);
        surfaceT = -b - planetH;
        if (surfaceT > 0.0 && surfaceT < t1)
        {
            hitT = surfaceT;
            hitSurface = true;
            t1 = surfaceT; // Stop at surface
        }
    }

    // March from start to end (atmosphere boundary or surface)
    const float MIN_STEP = 0.0001;
    float MAX_STEP = (atmosphereRadius - planetRadius) * 0.05; // Not const - uses uniforms
    const int MAX_STEPS = 64;

    vec3 transmittance = vec3(1.0);
    float t = t0;
    float maxHeight = atmosphereRadius - planetRadius;
    int stepCount = 0;

    while (t < t1 && transmittance.r > 0.001 && stepCount < MAX_STEPS)
    {
        vec3 samplePos = rayOrigin + rayDir * t;
        float distToCenter = length(samplePos - planetCenter);
        float heightFromSurface = distToCenter - planetRadius;
        float normalizedHeight = clamp(heightFromSurface / maxHeight, 0.0, 1.0);

        // Compute muSun: angle between ray direction and vertical at this point
        vec3 verticalDir = normalize(samplePos - planetCenter);
        float muSun = dot(rayDir, verticalDir);

        // Sample transmittance LUT
        vec3 stepTransmittance = sampleTransmittanceLUT(muSun, normalizedHeight);

        // Adaptive step size based on SDF
        // Negative inside atmosphere, positive outside
        float sdf = distToCenter - atmosphereRadius; // Negative inside (dist < radius), positive outside
        float stepSize = max(abs(sdf) * 0.3, MIN_STEP);
        stepSize = min(stepSize, MAX_STEP);
        stepSize = min(stepSize, t1 - t);

        if (stepSize < MIN_STEP * 0.5)
            break;

        // Accumulate transmittance
        float stepWeight = stepSize / maxHeight;
        transmittance *= mix(vec3(1.0), stepTransmittance, stepWeight);

        t += stepSize;
        stepCount++;
    }

    if (!hitSurface && hitT < 0.0)
    {
        hitT = t1; // Hit atmosphere boundary
    }

    return transmittance;
}

// Compute single-scattering contribution with refraction
// Path: Sun -> through atmosphere -> surface -> bounce off normal -> through atmosphere -> sample point
// For each sample point, we trace the light path:
// 1. Sun -> atmosphere -> surface (with refraction)
// 2. Bounce off surface normal
// 3. Surface -> atmosphere -> sample point (with refraction)
vec3 computeSingleScattering(vec3 samplePos,
                             vec3 viewDir,
                             vec3 sunDir,
                             vec3 planetCenter,
                             float planetRadius,
                             float atmosphereRadius)
{
    vec3 sunDirNorm = normalize(sunDir);
    vec3 viewDirNorm = normalize(viewDir);

    // Step 1: Find surface point along view ray
    // The view ray goes from camera through sample point, we need to find where it hits the surface
    // We'll trace backward from sample point along view direction to find surface intersection
    vec3 rayOrigin = samplePos;
    vec3 rayDir = -viewDirNorm; // Backward along view ray

    vec3 oc = rayOrigin - planetCenter;
    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - planetRadius * planetRadius;
    float disc = b * b - c;

    if (disc < 0.0)
    {
        return vec3(0.0); // View ray doesn't hit surface
    }

    float h = sqrt(disc);
    float t0 = -b - h; // Closer intersection
    float t1 = -b + h; // Farther intersection

    // Use the intersection that's behind the sample point (along backward view ray)
    float surfaceT = (t0 > 0.0) ? t0 : t1;

    if (surfaceT < 0.0)
    {
        return vec3(0.0); // Surface behind sample (shouldn't happen)
    }

    vec3 surfacePos = rayOrigin + rayDir * surfaceT;
    vec3 surfaceNormal = normalize(surfacePos - planetCenter);

    // Step 2: Trace from sun through atmosphere to surface point
    // Start from far outside atmosphere in sun direction
    vec3 sunRayOrigin = planetCenter + sunDirNorm * (atmosphereRadius * 2.5);
    vec3 sunRayDir = normalize(surfacePos - sunRayOrigin);

    float sunHitT;
    bool hitSurface;
    vec3 sunToSurfaceTransmittance = computeTransmittanceAlongRay(sunRayOrigin,
                                                                  sunRayDir,
                                                                  planetCenter,
                                                                  planetRadius,
                                                                  atmosphereRadius,
                                                                  sunHitT,
                                                                  hitSurface);

    if (!hitSurface)
    {
        return vec3(0.0); // Sun light doesn't reach surface
    }

    // Step 3: Bounce off surface normal
    // Incoming direction is from sun toward surface
    vec3 incomingDir = -sunRayDir; // Direction from surface toward sun
    // Perfect specular reflection: reflect incoming direction off surface normal
    vec3 reflectedDir = normalize(incomingDir - 2.0 * dot(incomingDir, surfaceNormal) * surfaceNormal);

    // Step 4: Trace from surface through atmosphere to sample point
    vec3 surfaceToSampleDir = normalize(samplePos - surfacePos);
    float sampleHitT;
    vec3 surfaceToSampleTransmittance = computeTransmittanceAlongRay(surfacePos,
                                                                     surfaceToSampleDir,
                                                                     planetCenter,
                                                                     planetRadius,
                                                                     atmosphereRadius,
                                                                     sampleHitT,
                                                                     hitSurface);

    // Total transmittance along the entire light path
    vec3 totalTransmittance = sunToSurfaceTransmittance * surfaceToSampleTransmittance;

    // Calculate scattering angle (cosine of angle between view direction and reflected sun direction)
    float nu = dot(viewDirNorm, reflectedDir);

    // Get height at sample point for scattering LUT lookup
    float sampleDist = length(samplePos - planetCenter);
    float maxHeight = atmosphereRadius - planetRadius;
    float normalizedHeight = clamp((sampleDist - planetRadius) / maxHeight, 0.0, 1.0);

    // Compute muSun: cosine of angle between sun direction and vertical at sample point
    vec3 verticalDir = normalize(samplePos - planetCenter);
    float muSun = dot(sunDirNorm, verticalDir);

    // Sample scattering LUT
    // Note: normalizedHeight is 0 at surface, 1 at top of atmosphere
    // The LUT already includes density weighting, so we don't need to multiply by densityFactor again
    vec3 scatteringCoeff = sampleScatteringLUT(muSun, normalizedHeight, nu);

    // Return scattered light: transmittance * scattering coefficient * sun color
    // The scattering coefficient from LUT already accounts for density and scattering strength
    return totalTransmittance * scatteringCoeff * uSunColor;
}

// SDF-based cone marching through atmosphere
// Computes both transmittance and scattering along the view ray
// Implements single-scattering with refraction: Sun -> atmosphere -> surface -> bounce -> atmosphere -> sample
vec4 marchAtmosphere(vec3 ro, vec3 rd, vec3 planetCenter, float planetRadius, float atmosphereRadius)
{
    // Ray origin MUST be camera position (ro = uCameraPos)
    // Ray direction is from camera through pixel (rd = normalized view ray)

    // Find ray-atmosphere intersection
    vec3 oc = ro - planetCenter;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - atmosphereRadius * atmosphereRadius;
    float disc = b * b - c;

    // Check if camera is inside atmosphere
    float cameraDist = length(oc);
    bool cameraInsideAtmosphere = (cameraDist <= atmosphereRadius);

    // Determine marching range
    float t0, t1;
    if (cameraInsideAtmosphere)
    {
        // Camera is inside atmosphere - start from camera (t=0)
        t0 = 0.0;
        if (disc >= 0.0)
        {
            float h = sqrt(disc);
            t1 = -b + h; // Exit point
        }
        else
        {
            // No intersection found, march forward a reasonable distance
            t1 = atmosphereRadius * 2.0;
        }
    }
    else
    {
        // Camera is outside atmosphere
        if (disc < 0.0)
        {
            return vec4(0.0, 0.0, 0.0, 0.0); // No intersection, no atmosphere
        }

        float h = sqrt(disc);
        t0 = max(-b - h, 0.0); // Entry point (clamp to 0 to start from camera)
        t1 = -b + h;           // Exit point

        if (t1 < 0.0)
        {
            return vec4(0.0, 0.0, 0.0, 0.0); // Ray is behind atmosphere
        }
    }

    // SDF-based cone marching along view ray
    // ALWAYS start from camera (t=0) and march forward
    const float MIN_STEP = 0.0001;
    float MAX_STEP = (atmosphereRadius - planetRadius) * 0.1; // Not const - uses uniforms
    const int MAX_STEPS = 128;

    vec3 viewTransmittance = vec3(1.0); // Transmittance along view ray
    vec3 scattering = vec3(0.0);
    float t = t0; // Start from entry point (or 0 if camera is inside)

    float maxHeight = atmosphereRadius - planetRadius;
    vec3 sunDir = normalize(uSunDir);

    int stepCount = 0;
    while (t < t1 && viewTransmittance.r > 0.001 && stepCount < MAX_STEPS)
    {
        vec3 pos = ro + rd * t;

        // Compute SDF (distance to atmosphere boundary)
        // MUST be negative inside atmosphere, positive outside
        float distToCenter = length(pos - planetCenter);
        float sdf = distToCenter - atmosphereRadius; // Negative inside (dist < radius), positive outside

        // Ensure SDF is negative when inside atmosphere
        // If we're inside (distToCenter < atmosphereRadius), sdf should be negative
        bool insideAtmosphere = (distToCenter < atmosphereRadius);
        if (insideAtmosphere && sdf > 0.0)
        {
            sdf = -sdf; // Force negative when inside
        }

        // Adaptive step size based on SDF
        // Use absolute value for step size, but preserve sign for direction
        float stepSize = max(abs(sdf) * 0.3, MIN_STEP);
        stepSize = min(stepSize, MAX_STEP);
        stepSize = min(stepSize, t1 - t);

        if (stepSize < MIN_STEP * 0.5)
        {
            break;
        }

        // Height at sample point
        float heightFromSurface = distToCenter - planetRadius;

        // Only sample if we're actually inside the atmosphere layers
        // Atmosphere extends from planetRadius to atmosphereRadius
        if (distToCenter >= planetRadius && distToCenter <= atmosphereRadius)
        {
            // Simplified: use normalized height directly (0 = surface, 1 = top)
            float normalizedHeight = clamp(heightFromSurface / maxHeight, 0.0, 1.0);

            // Calculate density using normalized height (simpler, no unit conversion needed)
            // Density is highest at surface (height=0), decreases exponentially toward top
            float rayleighDensity = exp(-normalizedHeight * 10.0); // Exponential falloff
            float mieDensity = exp(-normalizedHeight * 15.0);      // Mie falls off faster

            // Calculate scattering angle (cosine of angle between view and sun directions)
            float cosTheta = dot(rd, sunDir);

            // Apply phase functions
            float rayleighPhaseValue = rayleighPhase(cosTheta);
            float miePhaseValue = miePhase(cosTheta, MIE_G);

            // Trace ray from sample point to sun to get transmittance
            float sunHitT;
            bool hitSurface;
            vec3 sunTransmittance = computeTransmittanceAlongRay(pos,
                                                                 sunDir,
                                                                 planetCenter,
                                                                 planetRadius,
                                                                 atmosphereRadius,
                                                                 sunHitT,
                                                                 hitSurface);

            // Calculate Rayleigh scattering (blue sky - wavelength dependent)
            // Rayleigh scatters blue more than red: R=1.0, G=1.0, B=1.5 (blue is stronger)
            vec3 rayleighScattering = vec3(1.0, 1.0, 1.5) * RAYLEIGH_COEFF * rayleighDensity * rayleighPhaseValue;

            // Calculate Mie scattering (hazy/glowy - white)
            vec3 mieScattering = vec3(1.0, 1.0, 1.0) * MIE_COEFF * mieDensity * miePhaseValue;

            // Total scattering at this point
            vec3 totalScattering = (rayleighScattering + mieScattering) * sunTransmittance * uSunColor;

            // Accumulate scattered light
            // Don't multiply by stepSize - accumulate per sample point
            // This makes scattering independent of step size and more visible
            float scatteringMultiplier = 10.0; // Multiplier for visibility
            vec3 scatteredLight = totalScattering * viewTransmittance * scatteringMultiplier;
            scattering += scatteredLight;

            // Update view ray transmittance (light is absorbed/scattered as we pass through)
            // Simplified: reduce transmittance based on density
            float extinctionFactor = (rayleighDensity + mieDensity) * 0.1;
            viewTransmittance *= (1.0 - extinctionFactor * stepSize);
        }

        t += stepSize;
        stepCount++;
    }

    // Return scattering (RGB) and intensity (alpha channel for additive blending)
    // For additive blending (GL_SRC_ALPHA, GL_ONE): result = src.rgb * src.a + dst.rgb

    // Calculate scattering magnitude
    float scatteringMagnitude = length(scattering);

    // DEBUG: If we're inside atmosphere, show something (even if scattering is zero)
    // This helps verify the shader is running and sampling the atmosphere
    vec3 oc = ro - planetCenter;
    float cameraDist = length(oc);
    bool insideAtmosphere = (cameraDist >= planetRadius && cameraDist <= atmosphereRadius);

    // If there's any scattering at all, use it
    if (scatteringMagnitude > 0.0001)
    {
        // Normalize color but preserve magnitude in alpha
        vec3 color = scattering / scatteringMagnitude;

        // Scale intensity to be visible (use magnitude directly, clamped)
        float intensity = min(scatteringMagnitude * 100.0, 1.0); // Much larger scale factor

        // Ensure minimum visibility
        intensity = max(intensity, 0.8); // Higher minimum for visibility

        return vec4(color, intensity);
    }
    else if (insideAtmosphere)
    {
        // DEBUG: Show blue tint if inside atmosphere but no scattering calculated
        // This helps diagnose if the scattering calculation is the problem
        return vec4(0.2, 0.4, 0.8, 0.3); // Light blue tint
    }

    // No scattering and not inside atmosphere - return transparent
    return vec4(0.0, 0.0, 0.0, 0.0);
}

void main()
{
    // Force uniform to be used (prevents optimization)
    float debugMode = uDebugMode;

    // Debug mode 1: render solid red to verify quad is visible
    if (debugMode >= 0.5 && debugMode < 1.5)
    {
        gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); // Solid red, fully opaque
        return;
    }

    // Ray from camera through this pixel (needed for both debug mode 2 and normal mode)
    vec3 rayOrigin = uCameraPos;
    vec3 rayDir = normalize(vRayDir);

    // Debug mode 2: Visualize the marching process
    if (debugMode >= 1.5 && debugMode < 2.5)
    {
        // Find ray-atmosphere intersection
        vec3 oc = rayOrigin - uPlanetCenter;
        float b = dot(oc, rayDir);
        float c = dot(oc, oc) - uAtmosphereRadius * uAtmosphereRadius;
        float disc = b * b - c;

        if (disc < 0.0)
        {
            // No intersection - show blue
            gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0);
            return;
        }

        float h = sqrt(disc);
        float t0 = -b - h; // Entry point (can be negative if camera is inside)
        float t1 = -b + h; // Exit point

        // Check camera distance from planet
        float cameraDist = length(rayOrigin - uPlanetCenter);
        bool cameraInsideAtmosphere = (cameraDist <= uAtmosphereRadius);

        // Determine marching range
        float startT, endT;
        if (cameraInsideAtmosphere)
        {
            // Camera is inside atmosphere - start from camera and march forward
            startT = 0.0;
            // If t1 is positive, use it; otherwise march forward a reasonable distance
            endT = (t1 > 0.0) ? t1 : (uAtmosphereRadius * 3.0);
        }
        else
        {
            // Camera is outside - march from entry to exit
            // Ensure both are positive and valid
            if (t0 < 0.0 && t1 < 0.0)
            {
                // Both points behind camera - shouldn't happen with valid intersection
                gl_FragColor = vec4(0.5, 0.0, 0.5, 1.0); // Purple = both behind
                return;
            }

            startT = max(t0, 0.0);
            endT = max(t1, startT + 0.001); // Ensure endT is at least slightly ahead

            // If t1 is negative or very small, extend the range
            if (t1 <= startT)
            {
                endT = startT + uAtmosphereRadius * 2.0; // Extend forward
            }
        }

        // Visualize the marching: show how many steps we take and where we sample
        // March through the atmosphere from start to end
        float atmosphereThickness = uAtmosphereRadius - uPlanetRadius;

        // Use adaptive step size based on distance to atmosphere
        // Start with a reasonable step size
        float baseStepSize = atmosphereThickness * 0.1; // 10% of atmosphere thickness per step
        float t = startT;
        int stepCount = 0;
        const int MAX_VIS_STEPS = 500; // Many steps to catch all samples

        // Sample multiple points along the ray
        while (t <= endT && stepCount < MAX_VIS_STEPS)
        {
            vec3 stepPos = rayOrigin + rayDir * t;
            float stepDist = length(stepPos - uPlanetCenter);

            // Check if we're within the atmosphere layers
            // Atmosphere extends from planetRadius to atmosphereRadius
            if (stepDist >= uPlanetRadius && stepDist <= uAtmosphereRadius)
            {
                // Calculate normalized height: 0 = surface, 1 = top of atmosphere
                float stepHeight = (stepDist - uPlanetRadius) / atmosphereThickness;
                stepHeight = clamp(stepHeight, 0.0, 1.0);

                // Color based on height: red = surface, green = middle, blue = top
                gl_FragColor = vec4(mix(1.0, 0.0, stepHeight), // Red at surface (height=0)
                                    stepHeight,                // Green in middle (height=0.5)
                                    mix(0.0, 1.0, stepHeight), // Blue at top (height=1.0)
                                    1.0                        // Fully opaque for debugging
                );
                return;
            }

            // Adaptive step size: smaller steps when close to atmosphere
            float distToAtmosphere = abs(stepDist - uAtmosphereRadius);
            float stepSize = max(baseStepSize * (1.0 + distToAtmosphere / atmosphereThickness), baseStepSize * 0.1);

            // Step forward along the ray
            t += stepSize;
            stepCount++;
        }

        // If we didn't find any samples, check if camera is inside atmosphere
        if (cameraInsideAtmosphere && cameraDist >= uPlanetRadius && cameraDist <= uAtmosphereRadius)
        {
            float cameraHeight = (cameraDist - uPlanetRadius) / atmosphereThickness;
            cameraHeight = clamp(cameraHeight, 0.0, 1.0);
            gl_FragColor = vec4(mix(1.0, 0.0, cameraHeight), cameraHeight, mix(0.0, 1.0, cameraHeight), 1.0);
            return;
        }

        // Show distance to planet as a color gradient for debugging
        // This helps us see if rays are even getting close
        float normalizedDist = clamp((cameraDist - uPlanetRadius) / (uAtmosphereRadius * 2.0), 0.0, 1.0);
        gl_FragColor = vec4(normalizedDist, normalizedDist * 0.5, 1.0 - normalizedDist, 1.0);
        return;
    }

    // Normal mode: March through atmosphere
    vec4 atmosphere = marchAtmosphere(rayOrigin, rayDir, uPlanetCenter, uPlanetRadius, uAtmosphereRadius);

    // Output: RGB = scattered light, Alpha = opacity for blending
    gl_FragColor = atmosphere;
}
