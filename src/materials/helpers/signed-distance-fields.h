#pragma once

#include <glm/glm.hpp>
#include <cmath>

// ============================================================
// Signed Distance Field (SDF) Functions for Ray Marching (C++)
// ============================================================
// These functions provide consistent distance calculations for
// ray marching in C++ preprocessing code, matching the GLSL versions.
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
inline float sdSphere(const glm::vec3 &pos, const glm::vec3 &center, float radius)
{
    return glm::length(pos - center) - radius;
}

// ============================================================
// Ray-Sphere Intersection
// ============================================================
// Computes intersection distances along a ray with a sphere
// ro: ray origin
// rd: ray direction (normalized)
// center: sphere center
// radius: sphere radius
// t0: output entry distance
// t1: output exit distance
// Returns: true if intersection exists
inline bool raySphereIntersect(const glm::vec3 &ro, const glm::vec3 &rd, const glm::vec3 &center, float radius,
                                float &t0, float &t1)
{
    glm::vec3 oc = ro - center;
    float b = glm::dot(oc, rd);
    float c = glm::dot(oc, oc) - radius * radius;
    float disc = b * b - c;

    if (disc < 0.0f)
    {
        t0 = 0.0f;
        t1 = 0.0f;
        return false;
    }

    float h = std::sqrt(disc);
    t0 = -b - h; // Entry point
    t1 = -b + h; // Exit point
    return true;
}
