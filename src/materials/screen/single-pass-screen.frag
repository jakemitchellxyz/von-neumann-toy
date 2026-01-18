#version 450

// SDF Ray Marching Fragment Shader
// Optimized sphere tracing with frustum-culled objects
// Samples skybox cubemap on ray miss

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 fragColor;

// ==================================
// Skybox Cubemap Texture
// ==================================
// Stored as vertical strip: 6 faces (faceSize x faceSize each)
// Face order: +X, -X, +Y, -Y, +Z, -Z (matches Vulkan cubemap convention)
layout(set = 0, binding = 3) uniform sampler2D skyboxCubemap;

// Skybox sampling parameters
const float SKYBOX_EXPOSURE = 5.0; // HDR exposure multiplier

// ==================================
// Earth Material Textures (NAIF ID 399)
// ==================================
// Used when a ray hits Earth in the scene
layout(set = 0, binding = 4) uniform sampler2D earthColorTexture;       // Monthly Blue Marble color
layout(set = 0, binding = 5) uniform sampler2D earthNormalTexture;      // Normal map for terrain
layout(set = 0, binding = 6) uniform sampler2D earthNightlightsTexture; // City lights at night
layout(set = 0, binding = 7) uniform sampler2D earthSpecularTexture;    // Specular/roughness

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
// Skybox Cubemap Sampling (Vertical Strip Format)
// ==================================
// The cubemap is stored as a vertical strip with 6 faces:
// Face 0: +X (right)   - row 0
// Face 1: -X (left)    - row 1
// Face 2: +Y (top)     - row 2
// Face 3: -Y (bottom)  - row 3
// Face 4: +Z (front)   - row 4
// Face 5: -Z (back)    - row 5

// Determine which cubemap face a direction vector hits
// Returns face index (0-5) and UV coordinates within that face
void getCubemapFaceUV(vec3 dir, out int face, out vec2 faceUV)
{
    vec3 absDir = abs(dir);
    float maxAxis = max(absDir.x, max(absDir.y, absDir.z));
    float ma; // Major axis value (for normalization)
    vec2 uv;

    if (absDir.x >= absDir.y && absDir.x >= absDir.z)
    {
        // X is dominant axis
        ma = absDir.x;
        if (dir.x > 0.0)
        {
            // +X face (right)
            face = 0;
            uv = vec2(-dir.z, -dir.y);
        }
        else
        {
            // -X face (left)
            face = 1;
            uv = vec2(dir.z, -dir.y);
        }
    }
    else if (absDir.y >= absDir.x && absDir.y >= absDir.z)
    {
        // Y is dominant axis
        ma = absDir.y;
        if (dir.y > 0.0)
        {
            // +Y face (top)
            face = 2;
            uv = vec2(dir.x, dir.z);
        }
        else
        {
            // -Y face (bottom)
            face = 3;
            uv = vec2(dir.x, -dir.z);
        }
    }
    else
    {
        // Z is dominant axis
        ma = absDir.z;
        if (dir.z > 0.0)
        {
            // +Z face (front)
            face = 4;
            uv = vec2(dir.x, -dir.y);
        }
        else
        {
            // -Z face (back)
            face = 5;
            uv = vec2(-dir.x, -dir.y);
        }
    }

    // Normalize UV to [-1, 1] then convert to [0, 1]
    faceUV = (uv / ma) * 0.5 + 0.5;
}

// Sample the cubemap vertical strip texture using a 3D direction
vec3 sampleCubemapStrip(sampler2D cubemapTex, vec3 direction)
{
    int face;
    vec2 faceUV;
    getCubemapFaceUV(normalize(direction), face, faceUV);

    // The texture is arranged as a vertical strip with 6 faces
    // Each face occupies 1/6 of the texture height
    // Face 0 is at the top (v = 0 to 1/6), face 5 is at the bottom (v = 5/6 to 1)
    float faceHeight = 1.0 / 6.0;
    float v = float(face) * faceHeight + faceUV.y * faceHeight;

    // U coordinate is just the face UV x
    vec2 texCoord = vec2(faceUV.x, v);

    // Sample the texture
    vec3 color = texture(cubemapTex, texCoord).rgb;

    // Apply HDR exposure
    color *= SKYBOX_EXPOSURE;

    return color;
}

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
// Earth Material Rendering
// ==================================
// Calculate UV coordinates from a point on Earth's sphere
// Uses equirectangular projection (latitude/longitude)
// Assumes Y is up (north pole), X-Z plane is equator
vec2 sphereToEquirectangularUV(vec3 surfaceNormal)
{
    // Convert surface normal to latitude/longitude
    // Latitude: angle from equator (-90 to 90 degrees) -> V coordinate (0 to 1)
    // Longitude: angle around Y axis (-180 to 180 degrees) -> U coordinate (0 to 1)

    float latitude = asin(clamp(surfaceNormal.y, -1.0, 1.0)); // -PI/2 to PI/2
    float longitude = atan(surfaceNormal.x, surfaceNormal.z); // -PI to PI

    // Convert to UV coordinates
    // V: 0 = south pole (-90°), 1 = north pole (+90°)
    float v = 1.0 - (latitude / 3.14159265359 + 0.5); // Flip V so north is at top of texture
    // U: 0 = -180° (west), 1 = +180° (east)
    float u = longitude / (2.0 * 3.14159265359) + 0.5;

    return vec2(u, v);
}

// Sample Earth material and compute final color
vec3 sampleEarthMaterial(vec3 hitPoint, vec3 surfaceNormal, vec3 toSun)
{
    // Get UV coordinates from surface normal
    vec2 uv = sphereToEquirectangularUV(surfaceNormal);

    // Sample base color texture
    vec3 baseColor = texture(earthColorTexture, uv).rgb;

    // Sample normal map (if available)
    vec3 normalSample = texture(earthNormalTexture, uv).rgb;
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

    // Sample nightlights for the dark side
    float nightlights = texture(earthNightlightsTexture, uv).r;
    vec3 nightlightColor = vec3(1.0, 0.9, 0.7) * nightlights * 2.0; // Warm city light color

    // Sample specular map for ocean reflections
    float specularMask = texture(earthSpecularTexture, uv).r;

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
        vec3 skyColor = sampleCubemapStrip(skyboxCubemap, rd);
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
            // Find Earth's center from celestial objects
            vec3 earthCenter = vec3(0.0);
            for (uint i = 0u; i < celestialData.objectCount && i < 32u; ++i)
            {
                if (celestialData.objects[i].naifId == NAIF_EARTH)
                {
                    earthCenter = celestialData.objects[i].position;
                    break;
                }
            }

            // Calculate surface normal relative to Earth's center
            vec3 surfaceNormal = normalize(p - earthCenter);

            // Sample Earth material textures
            color = sampleEarthMaterial(p, surfaceNormal, toSun);
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
        // Ray miss: sample skybox cubemap using ray direction
        // The skybox is centered on J2000 epoch coordinate system
        color = sampleCubemapStrip(skyboxCubemap, rd);
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
