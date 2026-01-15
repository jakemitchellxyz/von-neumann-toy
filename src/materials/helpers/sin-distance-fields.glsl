// ============================================================
// Signed Distance Field (SDF) Functions for Ray Marching
// ============================================================
// These functions provide consistent distance calculations for
// ray marching in atmosphere and surface shaders.
//
// Reference: Inigo Quilez - https://iquilezles.org/articles/distfunctions/

// ============================================================
// Sphere SDF
// ============================================================
// Returns signed distance to sphere surface
// pos: point in world space
// center: sphere center in world space
// radius: sphere radius
// Returns: negative inside sphere, positive outside, zero on surface
float sdSphere(vec3 pos, vec3 center, float radius)
{
    return length(pos - center) - radius;
}

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
// Ray Marching Step Size Helper
// ============================================================
// Computes safe step size for ray marching based on SDF distance
// sdfDist: signed distance from current point to nearest surface
// Returns: safe step size (never overshoots surface)
float safeStepSize(float sdfDist)
{
    // Use a fraction of the distance to ensure we don't overshoot
    // Common practice: use 0.5-0.9 of the distance
    return max(sdfDist * 0.8, 0.001); // Minimum step to avoid infinite loops
}

// ============================================================
// Sphere Distance Query
// ============================================================
// Gets distance from point to sphere surface and checks if inside
// pos: point in world space
// center: sphere center
// radius: sphere radius
// Returns: distance (always positive), and sets isInside flag
float sphereDistance(vec3 pos, vec3 center, float radius, out bool isInside)
{
    float dist = length(pos - center);
    float sdf = dist - radius;
    isInside = (sdf < 0.0);
    return abs(sdf); // Return absolute distance
}
