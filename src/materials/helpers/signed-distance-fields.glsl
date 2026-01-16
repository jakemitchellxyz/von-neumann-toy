// ============================================================
// Cone Marching Functions for Efficient Sphere Ray Marching
// ============================================================
// Cone marching is more efficient than SDF ray marching for spheres
// because it allows larger step sizes when the cone doesn't intersect
// the sphere. The cone expands linearly along the ray.
//
// Reference: Crassin et al. (2011) "Interactive Indirect Illumination 
// Using Voxel Cone Tracing"

// ============================================================
// Ray-Sphere Intersection
// ============================================================
// Computes intersection distances along a ray with a sphere
// ro: ray origin
// rd: ray direction (normalized)
// center: sphere center
// radius: sphere radius
// Returns: true if intersection exists, with t0 and t1 as entry/exit distances
bool raySphereIntersect(vec3 ro, vec3 rd, vec3 center, float radius, out float t0, out float t1)
{
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    
    if (disc < 0.0)
    {
        // No intersection
        t0 = 0.0;
        t1 = 0.0;
        return false;
    }
    
    float h = sqrt(disc);
    t0 = -b - h;  // Entry point
    t1 = -b + h;  // Exit point
    return true;
}

// ============================================================
// Cone-Sphere Intersection
// ============================================================
// Computes if a cone intersects a sphere and returns safe step size
// ro: ray origin
// rd: ray direction (normalized)
// coneAngle: half-angle of the cone in radians (typically 0.01-0.1)
// center: sphere center
// radius: sphere radius
// Returns: safe step size along the ray (distance to cone-sphere intersection)
//          Returns large value if no intersection
float coneSphereIntersect(vec3 ro, vec3 rd, float coneAngle, vec3 center, float radius)
{
    vec3 oc = center - ro;
    float ocLen = length(oc);
    
    // Early out if sphere is behind ray origin
    float proj = dot(oc, rd);
    if (proj < 0.0)
    {
        return 1e10; // No intersection
    }
    
    // Distance from ray to sphere center
    vec3 projPoint = ro + rd * proj;
    float distToRay = length(center - projPoint);
    
    // Cone radius at distance proj
    float coneRadius = proj * tan(coneAngle);
    
    // Check if sphere intersects cone
    // Sphere intersects if: distToRay - radius < coneRadius
    float sphereRadiusAtRay = distToRay - radius;
    
    if (sphereRadiusAtRay > coneRadius)
    {
        // Sphere is outside cone - safe to step forward
        // Compute distance to closest approach
        float closestDist = proj - sqrt(max(0.0, radius * radius - distToRay * distToRay));
        return max(closestDist - coneRadius / tan(coneAngle), 0.0);
    }
    else
    {
        // Sphere intersects or is inside cone - need smaller steps
        // Use conservative step size based on cone expansion
        return max(radius * 0.5, 0.001);
    }
}

// ============================================================
// Cone Marching Step Size for Atmosphere
// ============================================================
// Computes safe step size for cone marching through atmosphere
// ro: current ray origin
// rd: ray direction (normalized)
// coneAngle: half-angle of the cone in radians
// planetCenter: planet center
// planetRadius: planet radius
// atmosphereRadius: atmosphere outer radius
// Returns: safe step size that won't overshoot sphere boundaries
float coneMarchStepSize(vec3 ro, vec3 rd, float coneAngle, vec3 planetCenter, float planetRadius, float atmosphereRadius)
{
    // Check intersection with planet (inner sphere)
    float planetStep = coneSphereIntersect(ro, rd, coneAngle, planetCenter, planetRadius);
    
    // Check intersection with atmosphere (outer sphere)
    float atmoStep = coneSphereIntersect(ro, rd, coneAngle, planetCenter, atmosphereRadius);
    
    // Use the minimum step size (most conservative)
    // But ensure we don't step past the atmosphere
    float step = min(planetStep, atmoStep);
    
    // Clamp to reasonable bounds
    return clamp(step, 0.001, 1000.0);
}

// ============================================================
// Cone Marching Step Size for Water Scattering
// ============================================================
// Computes adaptive step size for cone marching through water
// Uses cone marching principles but adapts to water depth constraints
// currentDepth: current depth in meters (distance traveled through water)
// maxDepth: maximum depth at current position (ocean floor depth)
// coneAngle: half-angle of the cone in radians (typically 0.02-0.05)
// minStep: minimum step size in meters
// maxStep: maximum step size in meters
// Returns: adaptive step size in meters
float coneMarchWaterStepSize(float currentDepth, float maxDepth, float coneAngle, float minStep, float maxStep)
{
    // Remaining distance to floor
    float remainingToFloor = maxDepth - currentDepth;
    
    // If very close to floor, use small steps
    if (remainingToFloor < 5.0)
    {
        return max(remainingToFloor * 0.5, minStep);
    }
    
    // Cone-based adaptive step size
    // The cone expands linearly, so we can take larger steps when far from boundaries
    float coneExpansion = currentDepth * tan(coneAngle);
    
    // Adaptive step based on distance to floor and cone expansion
    // When far from floor, use larger steps (limited by cone expansion)
    float adaptiveStep = min(remainingToFloor * 0.8, coneExpansion * 2.0);
    
    // Clamp to reasonable bounds
    return clamp(adaptiveStep, minStep, maxStep);
}

// ============================================================
// Legacy SDF Functions (for backward compatibility)
// ============================================================
// These are kept for compatibility but cone marching should be preferred

float sdSphere(vec3 pos, vec3 center, float radius)
{
    return length(pos - center) - radius;
}

float safeStepSize(float sdfDist)
{
    return max(sdfDist * 0.8, 0.001);
}

float sphereDistance(vec3 pos, vec3 center, float radius, out bool isInside)
{
    float dist = length(pos - center);
    float sdf = dist - radius;
    isInside = (sdf < 0.0);
    return abs(sdf);
}

// ============================================================
// 2D Signed Distance Field from Binary Mask
// ============================================================
// Computes approximate SDF from a binary mask texture
// Used for efficient boundary detection and reflection in wave simulations

// Compute approximate Signed Distance Field (SDF) from a binary mask texture
// maskSampler: sampler2D containing the mask (1.0 = solid/obstacle, 0.0 = empty/water)
// uv: texture coordinates to sample
// Returns: distance to nearest boundary (positive in empty space, negative in solid)
// Uses gradient-based approximation for efficiency
float computeMaskSDF(sampler2D maskSampler, vec2 uv)
{
    // Sample mask (1.0 = solid, 0.0 = empty)
    float maskValue = texture2D(maskSampler, uv).r;
    
    // Narrow-band optimization: early exit for deep empty/solid regions
    if (maskValue < 0.01)
    {
        return 1.0; // Deep empty space - far from boundary
    }
    if (maskValue > 0.99)
    {
        return -1.0; // Deep solid - far from boundary
    }
    
    // Near boundary - compute approximate SDF using gradient magnitude
    // This is more efficient than multi-directional search while still accurate
    float eps = 0.003; // Sampling offset for gradient computation
    
    // Sample mask at nearby points to compute gradient
    float maskX = texture2D(maskSampler, uv + vec2(eps, 0.0)).r;
    float maskY = texture2D(maskSampler, uv + vec2(0.0, eps)).r;
    float maskXNeg = texture2D(maskSampler, uv + vec2(-eps, 0.0)).r;
    float maskYNeg = texture2D(maskSampler, uv + vec2(0.0, -eps)).r;
    
    // Compute gradient magnitude (how quickly mask changes)
    vec2 gradient = vec2((maskX - maskXNeg) / (2.0 * eps), (maskY - maskYNeg) / (2.0 * eps));
    float gradientMag = length(gradient);
    
    // Approximate distance to boundary using gradient magnitude
    // Steep gradient = close to boundary, shallow gradient = far from boundary
    float approximateDist = 0.0;
    if (gradientMag > 0.01)
    {
        // Distance estimate: how far we'd need to travel to cross the boundary
        // Based on gradient magnitude and current mask value
        float distToBoundary = abs(maskValue - 0.5) / gradientMag;
        approximateDist = distToBoundary;
    }
    else
    {
        // Very shallow gradient - use fallback estimate
        approximateDist = abs(maskValue - 0.5) * 10.0; // Rough estimate
    }
    
    // Convert to signed distance (positive in empty space, negative in solid)
    float sdf = maskValue < 0.5 ? approximateDist : -approximateDist;
    
    return sdf;
}

// Compute boundary normal from SDF gradient
// maskSampler: sampler2D containing the mask
// uv: texture coordinates to sample
// Returns: normalized direction pointing from solid toward empty space (boundary normal)
// Uses SDF gradient for accurate normals even on jagged boundaries
vec2 computeMaskSDFNormal(sampler2D maskSampler, vec2 uv)
{
    // Compute SDF gradient using finite differences
    float eps = 0.003; // Small offset for gradient computation
    
    float sdfCenter = computeMaskSDF(maskSampler, uv);
    float sdfX = computeMaskSDF(maskSampler, uv + vec2(eps, 0.0));
    float sdfY = computeMaskSDF(maskSampler, uv + vec2(0.0, eps));
    
    // Compute gradient of SDF
    vec2 sdfGradient = vec2((sdfX - sdfCenter) / eps, (sdfY - sdfCenter) / eps);
    
    // Normalize gradient to get normal
    // SDF gradient points from low values (solid) to high values (empty)
    float gradientLen = length(sdfGradient);
    if (gradientLen < 0.01)
    {
        // Not near a boundary - return zero vector
        return vec2(0.0, 0.0);
    }
    
    // Normalize to get unit normal (points from solid toward empty space)
    vec2 boundaryNormal = sdfGradient / gradientLen;
    
    return boundaryNormal;
}

// Apply boundary reflection to a trajectory vector using SDF-based boundary detection
// maskSampler: sampler2D containing the mask
// trajectory: input trajectory vector (normalized)
// uv: texture coordinates to sample
// reflectionStrength: strength of reflection (0.0 = no reflection, 1.0 = full reflection)
// Returns: modified trajectory that accounts for boundary reflection
// Uses SDF for accurate boundary detection and energy-conserving reflection
vec2 applyBoundaryReflection(sampler2D maskSampler, vec2 trajectory, vec2 uv, float reflectionStrength)
{
    // Compute SDF at current location
    float sdf = computeMaskSDF(maskSampler, uv);
    
    // Narrow-band optimization: only process near boundaries
    // Reflection threshold: only reflect when within this distance of boundary
    float reflectionThreshold = 0.05; // UV units
    
    if (abs(sdf) > reflectionThreshold)
    {
        return trajectory; // Too far from boundary
    }
    
    // Must be in empty space (positive SDF) to reflect
    if (sdf <= 0.0)
    {
        return trajectory; // In solid - no reflection
    }
    
    // Get boundary normal from SDF gradient (accurate even on jagged boundaries)
    vec2 boundaryNormal = computeMaskSDFNormal(maskSampler, uv);
    
    // If normal is invalid, return original trajectory
    float normalLen = length(boundaryNormal);
    if (normalLen < 0.01)
    {
        return trajectory; // Not near boundary
    }
    
    // Check if trajectory is pointing toward solid
    // Boundary normal points from solid toward empty space
    // If trajectory points toward solid, dot product with normal will be negative
    float dotProduct = dot(trajectory, boundaryNormal);
    
    if (dotProduct < 0.0)
    {
        // Trajectory is approaching solid - reflect it using standard reflection formula
        // Reflection: R = I - 2 * dot(I, N) * N
        vec2 reflectedTrajectory = trajectory - 2.0 * dotProduct * boundaryNormal;
        
        // Compute reflection strength based on proximity to boundary
        // Closer to boundary = stronger reflection
        // Use SDF to determine proximity (sdf = 0 at boundary, increases with distance)
        float proximityFactor = 1.0 - smoothstep(0.0, reflectionThreshold, sdf);
        
        // Blend between original and reflected trajectory
        // Energy-conserving: velocity magnitude preserved
        vec2 blendedTrajectory = normalize(mix(trajectory, reflectedTrajectory, reflectionStrength * proximityFactor));
        
        // Apply damping near boundaries to prevent energy buildup
        float dampingFactor = clamp(sdf / reflectionThreshold, 0.3, 1.0);
        
        return blendedTrajectory * dampingFactor;
    }
    
    // Trajectory is moving away from solid - no reflection needed
    return trajectory;
}
