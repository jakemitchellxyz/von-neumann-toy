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

// Earth NAIF ID constant
const int NAIF_EARTH = 399;

// ==================================
// SSBOs
// ==================================
layout(std430, set = 0, binding = 1) buffer HoverOutput
{
    uint hitMaterialID;
}
hoverOut;

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
const int MAX_STEPS = 128;
const float MAX_DIST = 100000.0;
const float SURF_DIST = 0.0001;
const float MIN_STEP = 0.001; // Minimum step to prevent infinite loops

// ==================================
// SDF Primitives
// ==================================

float sdSphere(vec3 p, vec3 center, float radius)
{
    return length(p - center) - radius;
}

// ==================================
// Scene SDF
// ==================================
// Returns distance to nearest surface and outputs hit info
float sceneSDF(vec3 p, out int hitID, out vec3 hitColor)
{
    float minDist = MAX_DIST;
    hitID = 0;
    hitColor = vec3(0.5);

    uint count = celestialData.objectCount;
    for (uint i = 0; i < count && i < 32u; ++i)
    {
        CelestialObjectGPU obj = celestialData.objects[i];
        float d = sdSphere(p, obj.position, obj.radius);

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
vec3 calcNormal(vec3 p)
{
    const float h = 0.001;
    return normalize(vec3(sceneSDF(p + vec3(h, 0, 0)) - sceneSDF(p - vec3(h, 0, 0)),
                          sceneSDF(p + vec3(0, h, 0)) - sceneSDF(p - vec3(0, h, 0)),
                          sceneSDF(p + vec3(0, 0, h)) - sceneSDF(p - vec3(0, 0, h))));
}

// ==================================
// Ray Marching (Sphere Tracing)
// ==================================
float rayMarch(vec3 ro, vec3 rd, out int hitID, out vec3 hitColor)
{
    float t = 0.0;
    hitID = 0;
    hitColor = vec3(0.5);

    for (int i = 0; i < MAX_STEPS; i++)
    {
        vec3 p = ro + rd * t;
        float d = sceneSDF(p, hitID, hitColor);

        // Hit surface
        if (d < SURF_DIST)
            return t;

        // Step by at least MIN_STEP to prevent infinite loops
        t += max(d, MIN_STEP);

        // Too far
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
// Earth Material Rendering (Native Cubemap)
// ==================================
// Uses hardware-accelerated cubemap sampling for all Earth textures
// Uses SPICE-derived pole and prime meridian from SSBO

// Sample Earth material and compute final color
vec3 sampleEarthMaterial(vec3 hitPoint, vec3 surfaceNormal, vec3 toSun, vec3 poleDir, vec3 primeMeridianDir)
{
    // Build body-fixed frame from SPICE data (J2000: Z-up, X toward prime meridian)
    mat3 bodyFrame = buildBodyFrame(poleDir, primeMeridianDir);

    // Transform surface normal to body-fixed J2000 coordinates
    // In body-fixed J2000: Z = north pole, X = prime meridian, Y = 90°E
    vec3 bodyDir = transpose(bodyFrame) * surfaceNormal;

    // Transform from body-fixed J2000 (Z-up) to cubemap convention (Y-up)
    // The cubemap textures are baked with:
    // - +Y is north pole
    // - +X is prime meridian (0° longitude)
    // - +Z is 90°W longitude
    // Transform: cubemap.x = body.x, cubemap.y = body.z, cubemap.z = -body.y
    vec3 dir = vec3(bodyDir.x, bodyDir.z, -bodyDir.y);

    // Sample base color texture (native cubemap - hardware accelerated)
    vec3 baseColor = texture(earthColorTexture, dir).rgb;

    // Sample normal map (native cubemap)
    vec3 normalSample = texture(earthNormalTexture, dir).rgb;
    vec3 tangentNormal = normalSample * 2.0 - 1.0;

    // Build tangent space basis from surface normal
    // For a sphere, we can compute tangent and bitangent from the normal
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 tangent = normalize(cross(up, surfaceNormal));
    if (length(tangent) < 0.001)
    {
        tangent = vec3(1.0, 0.0, 0.0);
    }
    vec3 bitangent = normalize(cross(surfaceNormal, tangent));

    // Transform normal from tangent space to world space
    mat3 TBN = mat3(tangent, bitangent, surfaceNormal);
    vec3 worldNormal = normalize(TBN * tangentNormal);

    // Mix between flat normal and detailed normal (avoid artifacts if normal map is missing)
    if (normalSample.r == 0.0 && normalSample.g == 0.0 && normalSample.b == 0.0)
    {
        worldNormal = surfaceNormal; // No normal map, use flat surface
    }
    else
    {
        worldNormal = normalize(mix(surfaceNormal, worldNormal, 0.5)); // Blend for subtlety
    }

    // Calculate sun diffuse lighting
    float sunDiff = max(dot(worldNormal, toSun), 0.0);

    // Calculate daylight/nightside factor
    float dayFactor = clamp(dot(surfaceNormal, toSun) * 2.0 + 0.5, 0.0, 1.0);

    // Sample nightlights for the dark side (native cubemap)
    float nightlights = texture(earthNightlightsTexture, dir).r;
    vec3 nightlightColor = vec3(1.0, 0.9, 0.7) * nightlights * 2.0; // Warm city light color

    // Sample specular map for ocean reflections (native cubemap)
    float specularMask = texture(earthSpecularTexture, dir).r;

    // Calculate specular reflection (simplified Blinn-Phong)
    vec3 viewDir = normalize(pc.cameraPosition - hitPoint);
    vec3 halfDir = normalize(toSun + viewDir);
    float spec = pow(max(dot(worldNormal, halfDir), 0.0), 32.0) * specularMask;
    vec3 specularColor = vec3(1.0) * spec * 0.5;

    // Combine lighting
    // Day side: diffuse lit with specular highlights
    vec3 dayColor = baseColor * (0.05 + 0.95 * sunDiff) + specularColor * sunDiff;

    // Night side: very dark with city lights
    vec3 nightColor = baseColor * 0.01 + nightlightColor;

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

    vec3 color;
    float depth;

    if (t < MAX_DIST)
    {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p);

        // Lighting: sun at origin
        vec3 toSun = normalize(-p);

        if (hitID == 10)
        {
            // Sun is emissive - no lighting calculation needed
            color = hitColor;
        }
        else if (hitID == NAIF_EARTH)
        {
            // Earth: use advanced material with textures
            // Find Earth's data from celestial objects SSBO
            vec3 earthCenter = vec3(0.0);
            vec3 earthPoleDir = vec3(0.0, 0.0, 1.0);       // J2000: Z-up
            vec3 earthPrimeMeridian = vec3(1.0, 0.0, 0.0); // J2000: X toward vernal equinox

            for (uint i = 0u; i < celestialData.objectCount && i < 32u; ++i)
            {
                if (celestialData.objects[i].naifId == NAIF_EARTH)
                {
                    earthCenter = celestialData.objects[i].position;
                    earthPoleDir = celestialData.objects[i].poleDirection;
                    earthPrimeMeridian = celestialData.objects[i].primeMeridianDirection;
                    break;
                }
            }

            // Calculate surface normal relative to Earth's center
            vec3 surfaceNormal = normalize(p - earthCenter);

            // Sample Earth material textures using native cubemaps
            // Pole and prime meridian come directly from SPICE via SSBO
            color = sampleEarthMaterial(p, surfaceNormal, toSun, earthPoleDir, earthPrimeMeridian);
        }
        else
        {
            // Other objects: simple diffuse lighting
            float diff = max(dot(n, toSun), 0.0);
            color = hitColor * (0.1 + 0.9 * diff);
        }

        vec4 clip = pc.projectionMatrix * pc.viewMatrix * vec4(p, 1.0);
        depth = clamp(clip.z / clip.w, 0.0, 1.0);
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
