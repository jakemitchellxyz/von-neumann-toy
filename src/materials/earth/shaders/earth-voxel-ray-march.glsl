#version 450
#extension GL_EXT_scalar_block_layout : require

// Voxel Mesh Ray Marching Fragment Shader
// Hybrid ray marching: SDF sphere → fixed-distance atmosphere → octree voxel traversal

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vRayDir; // Ray direction in world space (normalized)

// Fragment shader output
layout(location = 0) out vec4 fragColor;

// Uniforms - Samplers (require binding qualifiers for Vulkan)
layout(binding = 0) uniform sampler2D uHeightmap;    // Heightmap texture (sinusoidal projection)
layout(binding = 1) uniform sampler2D uLandmassMask; // Landmass mask texture

// Uniform block for non-opaque uniforms (required for Vulkan)
layout(set = 0, binding = 2, scalar) uniform Uniforms
{
    vec3 uCameraPos;     // Camera position in world space
    vec3 uPlanetCenter;  // Planet center position
    float uPlanetRadius; // Planet average radius
    float uMaxRadius;    // Spherical bounding volume radius (exosphere)
    float uKarmanLine;   // Karman line height (100km above surface)
};

const float PI = 3.14159265359;
const float EARTH_RADIUS_M = 6371000.0; // Earth radius in meters

// SDF for sphere (bounding volume)
float sphereSDF(vec3 pos, vec3 center, float radius)
{
    return length(pos - center) - radius;
}

// Sample heightmap at world position
float sampleHeightmapElevation(vec3 worldPos)
{
    // Convert world position to direction from center
    float dist = length(worldPos - uPlanetCenter);
    if (dist < 0.001)
    {
        return 0.0;
    }
    vec3 dir = (worldPos - uPlanetCenter) / dist;

    // Convert to equirectangular UV
    float latitude = asin(clamp(dir.y, -1.0, 1.0));
    float longitude = atan(dir.z, dir.x);
    float u = (longitude / PI + 1.0) * 0.5;
    float v = 0.5 - (latitude / PI);

    // Convert to sinusoidal UV (matching texture format)
    float lon = (u - 0.5) * 2.0 * PI;
    float lat = (0.5 - v) * PI;
    float cosLat = cos(lat);
    float absCosLat = abs(cosLat);

    float u_sinu;
    if (absCosLat < 0.01)
    {
        u_sinu = 0.5;
    }
    else
    {
        float x_sinu = lon * cosLat;
        u_sinu = x_sinu / (2.0 * PI) + 0.5;
        float uMin = 0.5 - 0.5 * absCosLat;
        float uMax = 0.5 + 0.5 * absCosLat;
        u_sinu = clamp(u_sinu, uMin, uMax);
    }

    float v_sinu = 0.5 + lat / PI;
    v_sinu = clamp(v_sinu, 0.0, 1.0);

    // Sample heightmap
    float heightValue = texture(uHeightmap, vec2(u_sinu, v_sinu)).r;

    // Convert to elevation in meters
    // Heightmap: 0.5 = sea level (0m), 1.0 = Mt. Everest (~8848m)
    float normalizedHeight = heightValue;
    float elevationMeters = 0.0;
    if (normalizedHeight >= 0.5)
    {
        elevationMeters = (normalizedHeight - 0.5) / 0.5 * 8848.0;
    }
    else
    {
        elevationMeters = (normalizedHeight - 0.5) / 0.5 * 11000.0;
    }

    return elevationMeters;
}

// Get surface radius at position (base radius + heightmap offset)
float getSurfaceRadius(vec3 worldPos)
{
    float elevation = sampleHeightmapElevation(worldPos);
    return uPlanetRadius + elevation;
}

// Sample density for voxel traversal
// Returns: < 0 if inside planet, > 0 if outside, 0 at surface
float sampleDensity(vec3 pos)
{
    float distFromCenter = length(pos - uPlanetCenter);
    float surfaceRadius = getSurfaceRadius(pos);
    return distFromCenter - surfaceRadius;
}

// Calculate surface normal from heightmap gradient
vec3 calculateSurfaceNormal(vec3 worldPos)
{
    // Sample heightmap at nearby points to compute gradient
    float eps = 100.0; // 100m offset for gradient calculation

    vec3 posX = worldPos + vec3(eps, 0.0, 0.0);
    vec3 posY = worldPos + vec3(0.0, eps, 0.0);
    vec3 posZ = worldPos + vec3(0.0, 0.0, eps);

    float hCenter = sampleHeightmapElevation(worldPos);
    float hX = sampleHeightmapElevation(posX);
    float hY = sampleHeightmapElevation(posY);
    float hZ = sampleHeightmapElevation(posZ);

    // Compute gradient
    vec3 gradient = vec3((hX - hCenter) / eps, (hY - hCenter) / eps, (hZ - hCenter) / eps);

    // Convert to world space normal
    vec3 toCenter = normalize(worldPos - uPlanetCenter);
    vec3 tangent = normalize(cross(toCenter, vec3(0.0, 1.0, 0.0)));
    if (length(tangent) < 0.01)
    {
        tangent = normalize(cross(toCenter, vec3(1.0, 0.0, 0.0)));
    }
    vec3 bitangent = normalize(cross(toCenter, tangent));

    // Normal points outward (away from center) + gradient adjustment
    vec3 normal = toCenter + gradient.x * tangent + gradient.y * bitangent;
    return normalize(normal);
}

// 3D DDA (Amanatides-Woo) voxel traversal
// Returns step distance to next voxel boundary and whether current voxel is filled
// Based on industry-standard ray-voxel traversal algorithm
// Returns vec2(stepSize, isFilled) where isFilled is 1.0 if filled, 0.0 otherwise
vec2 voxelTraversal3DDDA(vec3 rayOrigin, vec3 rayDir, float tCurrent)
{
    float isFilled = 0.0;

    // Calculate voxel grid size based on distance from surface
    // Use local space coordinates (relative to planet center)
    vec3 localPos = rayOrigin + rayDir * tCurrent - uPlanetCenter;
    float distFromCenter = length(localPos);
    float surfaceRadius = getSurfaceRadius(rayOrigin + rayDir * tCurrent);
    float heightAboveSurface = distFromCenter - surfaceRadius;

    // Voxel size increases with distance from surface
    // Near surface: small voxels for detail (100m)
    // Far from surface: larger voxels for efficiency (1km+)
    float baseVoxelSize = 100.0; // 100m base voxel size near surface
    float voxelSize = baseVoxelSize * max(1.0, heightAboveSurface / 1000.0);

    // Convert to voxel grid coordinates
    vec3 voxelPos = localPos / voxelSize;

    // Ray direction in voxel space
    vec3 rayDirNorm = normalize(rayDir);
    vec3 invRayDir = vec3(abs(rayDirNorm.x) > 0.0001 ? 1.0 / rayDirNorm.x : 1e10,
                          abs(rayDirNorm.y) > 0.0001 ? 1.0 / rayDirNorm.y : 1e10,
                          abs(rayDirNorm.z) > 0.0001 ? 1.0 / rayDirNorm.z : 1e10);

    // Current voxel coordinates (integer - use floor)
    vec3 voxelCoord = floor(voxelPos);

    // Calculate tMax for each axis (distance to next voxel boundary)
    vec3 nextBoundary = vec3(rayDirNorm.x > 0.0 ? voxelCoord.x + 1.0 : voxelCoord.x,
                             rayDirNorm.y > 0.0 ? voxelCoord.y + 1.0 : voxelCoord.y,
                             rayDirNorm.z > 0.0 ? voxelCoord.z + 1.0 : voxelCoord.z);

    vec3 tMax = (nextBoundary - voxelPos) * invRayDir;

    // Check if current voxel is filled
    vec3 voxelCenterWorld = uPlanetCenter + voxelCoord * voxelSize;
    float density = sampleDensity(voxelCenterWorld);
    isFilled = (density < 0.0) ? 1.0 : 0.0; // Inside planet (solid)

    // Find minimum step to next voxel boundary
    float stepSize;
    if (tMax.x < tMax.y)
    {
        if (tMax.x < tMax.z)
        {
            stepSize = tMax.x * voxelSize;
        }
        else
        {
            stepSize = tMax.z * voxelSize;
        }
    }
    else
    {
        if (tMax.y < tMax.z)
        {
            stepSize = tMax.y * voxelSize;
        }
        else
        {
            stepSize = tMax.z * voxelSize;
        }
    }

    // Ensure minimum step size
    stepSize = max(stepSize, 1.0);

    return vec2(stepSize, isFilled);
}

// Fixed distance step for atmosphere layers
float atmosphereFixedStep(vec3 pos)
{
    float distFromCenter = length(pos - uPlanetCenter);
    float surfaceRadius = getSurfaceRadius(pos);
    float heightAboveSurface = distFromCenter - surfaceRadius;

    // Fixed step size based on atmosphere layer
    // Lower atmosphere: smaller steps for detail
    // Upper atmosphere: larger steps
    if (heightAboveSurface < 10000.0) // Below 10km
    {
        return 100.0; // 100m steps
    }
    else if (heightAboveSurface < 50000.0) // 10-50km
    {
        return 500.0; // 500m steps
    }
    else // Above 50km
    {
        return 2000.0; // 2km steps
    }
}

void main()
{
    // Ray starts from camera and marches through the planet
    vec3 rayOrigin = uCameraPos;

    // Calculate ray direction from camera to this fragment's world position
    // vRayDir contains the vector from camera to vertex, we normalize it for direction
    vec3 rayDir = normalize(vRayDir);

    // Ensure ray direction is valid
    if (length(rayDir) < 0.001)
    {
        discard;
        return;
    }

    // Phase 1: Find entry point into exosphere bounding sphere
    // Use ray-sphere intersection to find where ray enters the bounding volume
    vec3 toSphere = uPlanetCenter - rayOrigin;
    float b = dot(toSphere, rayDir);
    float c = dot(toSphere, toSphere) - uMaxRadius * uMaxRadius;
    float discriminant = b * b - c;

    float t = 0.0;
    if (discriminant < 0.0)
    {
        // Ray doesn't intersect exosphere - render space
        fragColor = vec4(0.0, 0.0, 0.1, 1.0);
        return;
    }

    // Find entry point (closer intersection) - this is where ray enters exosphere
    float tEntry = b - sqrt(discriminant);
    if (tEntry < 0.0)
    {
        // Camera is inside exosphere, start from camera
        tEntry = 0.0;
    }

    // Start ray marching from entry point into exosphere
    // We will continue marching until we find the actual planet surface
    t = tEntry;

    // Phase 2: Fixed-distance ray marching through atmosphere
    // March from exosphere entry point with fixed steps in the same direction
    // Continue until we hit the voxel surface (density < 0)
    // Later, this phase will include atmosphere effects, but for now just march through
    const float FIXED_STEP_SIZE_FAR = 1000.0; // Fixed 1km steps when far from surface
    const float FIXED_STEP_SIZE_NEAR = 10.0;  // Smaller 10m steps when near surface
    const int MAX_ATMOSPHERE_STEPS = 100000;  // Enough to traverse from exosphere to surface

    for (int i = 0; i < MAX_ATMOSPHERE_STEPS; i++)
    {
        vec3 pos = rayOrigin + rayDir * t;
        float distFromCenter = length(pos - uPlanetCenter);

        // Check if we've hit the voxel surface (density < 0 means we're inside the planet)
        float density = sampleDensity(pos);

        // If we've crossed into the planet (density < 0), we've hit the voxel surface
        // Break and let Phase 3 handle the precise surface finding
        if (density < 0.0)
        {
            break; // Hit voxel surface, will be handled in Phase 3
        }

        // Check if we've exited the exosphere without hitting the planet
        // CRITICAL: Check this BEFORE stepping, so we don't step past the exosphere
        // If we've exited, the ray went through without hitting - render nothing
        if (distFromCenter > uMaxRadius)
        {
            // Ray exited exosphere without hitting planet - render space
            fragColor = vec4(0.0, 0.0, 0.1, 1.0);
            return;
        }

        // Adaptive step size: use smaller steps when close to surface to avoid stepping over it
        float stepSize = FIXED_STEP_SIZE_FAR;
        float expectedSurfaceRadius = getSurfaceRadius(pos);
        float distToSurface = distFromCenter - expectedSurfaceRadius;

        // If we're very close (within 100m), use smallest steps
        if (distToSurface < 100.0)
        {
            stepSize = 1.0; // 1m steps when very close
        }
        // If we're within 10km of the surface, use smaller steps
        else if (distToSurface < 10000.0)
        {
            stepSize = FIXED_STEP_SIZE_NEAR;
        }

        // Fixed-distance step in the same direction we entered
        t += stepSize;

        // Safety check - don't go past the planet center
        if (distFromCenter < uPlanetRadius - 11000.0)
        {
            break; // Went too deep (shouldn't happen if density check works)
        }
    }

    // Phase 3: Precise surface finding using binary search
    // Check if we actually hit the planet voxel surface in Phase 2
    // If not, the ray went through the exosphere without hitting the planet - discard immediately
    vec3 posAfterPhase2 = rayOrigin + rayDir * t;
    float distAfterPhase2 = length(posAfterPhase2 - uPlanetCenter);
    float densityAfterPhase2 = sampleDensity(posAfterPhase2);

    // CRITICAL FIRST CHECK: If we're at exosphere distance, we didn't hit the planet - discard
    // The exosphere is just a bounding volume - we should NEVER render it
    // Check if we're anywhere near exosphere radius (within 5% of it)
    if (distAfterPhase2 > uMaxRadius * 0.95)
    {
        // We're at exosphere distance - discard immediately, don't render anything
        discard;
        return;
    }

    // Also check if we're way too far from planet surface (more than 50km)
    if (distAfterPhase2 > uPlanetRadius + 50000.0)
    {
        // Way too far from planet - discard
        discard;
        return;
    }

    bool hitSurface = false;

    // CRITICAL: Only proceed if we actually hit the planet (density < 0)
    // AND we're at planet surface distance, NOT exosphere distance
    // If density >= 0, the ray went through without hitting planet - discard
    if (densityAfterPhase2 < 0.0 && distAfterPhase2 < uPlanetRadius + 20000.0)
    {
        // We hit the planet voxel - now find exact surface using binary search
        float tEntryPhase3 = t; // Store entry point for Phase 3 (where we detected density < 0)

        // We're currently inside the planet (density < 0 from Phase 2)
        // Use binary search to find the exact surface boundary
        float tInside = t; // We know we're inside here
        float tOutside = t;
        float searchStep = FIXED_STEP_SIZE_NEAR; // Start with small step size for precision

        // Back up until we find a point outside (density > 0)
        for (int k = 0; k < 50; k++)
        {
            tOutside -= searchStep;
            if (tOutside < tEntry) // Don't go before exosphere entry
            {
                tOutside = tEntry;
                break;
            }
            vec3 posOutside = rayOrigin + rayDir * tOutside;
            float densityOutside = sampleDensity(posOutside);
            if (densityOutside > 0.0)
            {
                break; // Found outside point
            }
            // If still inside, increase step size to back up faster
            searchStep *= 1.5;
        }

        // Verify we have an outside point
        vec3 testPos = rayOrigin + rayDir * tOutside;
        float testDensity = sampleDensity(testPos);
        if (testDensity < 0.0)
        {
            // Still inside - go back to entry point
            tOutside = max(tEntry, tEntryPhase3 - FIXED_STEP_SIZE_FAR * 10.0);
        }

        // Now binary search between outside and inside to find exact surface
        for (int j = 0; j < 30; j++) // More iterations for precision
        {
            float tMid = (tInside + tOutside) * 0.5;
            vec3 posMid = rayOrigin + rayDir * tMid;
            float densityMid = sampleDensity(posMid);

            if (densityMid < 0.0)
            {
                // Inside, move inside point inward
                tInside = tMid;
            }
            else
            {
                // Outside, move outside point outward
                tOutside = tMid;
            }

            // Early exit if we're very close to surface
            if (abs(densityMid) < 1.0) // Within 1m of surface
            {
                t = tMid;
                hitSurface = true;
                break;
            }

            // Also check if we've converged
            if (abs(tInside - tOutside) < 0.1) // Less than 10cm difference
            {
                t = tMid;
                hitSurface = true;
                break;
            }
        }

        if (!hitSurface)
        {
            t = (tInside + tOutside) * 0.5; // Use midpoint
            hitSurface = true;
        }
    }
    else
    {
        // Ray went through exosphere without hitting planet voxel - discard immediately
        // We should NEVER render the exosphere surface, only the planet voxel surface
        discard;
        return;
    }

    // Output result
    if (hitSurface)
    {
        vec3 hitPos = rayOrigin + rayDir * t;
        vec3 toCenter = hitPos - uPlanetCenter;
        float distFromCenter = length(toCenter);

        // CRITICAL: Verify we're actually on the planet voxel surface, NOT the exosphere
        // The planet surface should be at approximately uPlanetRadius + elevation
        // If we're anywhere near exosphere distance (uMaxRadius), we didn't hit the planet - discard
        float expectedSurfaceRadius = getSurfaceRadius(hitPos);
        float distToExpectedSurface = distFromCenter - expectedSurfaceRadius;

        // STRICT CHECK: If we're more than 10km above expected surface OR anywhere near exosphere, discard
        // We should ONLY render the actual planet voxel surface, not the exosphere bounding sphere
        // Check multiple conditions to ensure we're at planet distance, not exosphere
        if (distToExpectedSurface > 10000.0 || distFromCenter > uPlanetRadius + 20000.0 ||
            distFromCenter > uMaxRadius * 0.95)
        {
            // We're at exosphere distance or too far from planet - discard this fragment
            discard;
            return;
        }

        // Double-check: verify density at hit position is actually near zero (on surface)
        float densityAtHit = sampleDensity(hitPos);
        if (abs(densityAtHit) > 500.0) // More than 500m from surface
        {
            // Not actually on surface - discard
            discard;
            return;
        }

        vec3 normal = normalize(toCenter);

        // Backface culling: discard if surface is facing away from camera
        // For a sphere viewed from outside:
        // - normal points outward from center (normalize(surface - center))
        // - rayDir points from camera to surface (normalize(surface - camera))
        // - For visible faces, these should point in roughly opposite directions
        // - So dot(normal, rayDir) should be negative for visible faces
        // - If dot > 0, the surface is facing away (backface)
        float dotNormalRay = dot(normal, rayDir);
        // Temporarily disable backface culling to debug visibility
        // if (dotNormalRay > 0.0)
        // {
        //     discard; // Backface - surface is facing away from camera
        // }

        // Calculate surface normal from heightmap gradient
        vec3 surfaceNormal = calculateSurfaceNormal(hitPos);

        // Use surface normal for lighting (simple diffuse)
        vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0)); // Simple directional light
        float diffuse = max(0.0, dot(surfaceNormal, lightDir));

        // Sample heightmap for basic color
        float elevation = sampleHeightmapElevation(hitPos);
        vec3 color = vec3(0.2, 0.5, 0.8); // Base blue-green color

        // Adjust color based on elevation
        if (elevation > 0.0)
        {
            color = mix(vec3(0.2, 0.5, 0.2), vec3(0.6, 0.4, 0.2), min(elevation * 2.0, 1.0)); // Green to brown
        }

        // Apply lighting
        color *= (0.3 + 0.7 * diffuse); // Ambient + diffuse

        fragColor = vec4(color, 1.0);
    }
    else
    {
        // No surface hit - render sky/space
        // Debug: render a color to see if rays are hitting
        // Uncomment the next line to see if rays are being processed (should see red if rays are hitting but not finding surface)
        // fragColor = vec4(1.0, 0.0, 0.0, 1.0); // Red for debugging
        fragColor = vec4(0.0, 0.0, 0.1, 1.0); // Dark blue space color
    }
}
