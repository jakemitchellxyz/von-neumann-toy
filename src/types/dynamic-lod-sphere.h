#pragma once

#include "../concerns/constants.h"
#include <glm/glm.hpp>
#include <tuple>

// ============================================================================
// DynamicLODSphere
// ============================================================================
// A sphere renderer with dynamic level-of-detail (LOD) based on camera distance
// and adaptive tessellation in a local high-detail region.
//
// Features:
// - Dynamic tessellation based on camera distance (base resolution increases as camera approaches)
// - Local high-detail tessellation in a circular region (radius = 0.25 * planet radius) around closest point
// - Triangle-level occlusion culling (frustum + back-face culling with 0.6π angle threshold)
// - Variable resolution mesh: base-resolution triangles cover entire sphere,
//   with high-resolution subdivision in the local region
//
// Rendering:
// - Uses GL_TRIANGLES for flexible adaptive tessellation
// - Two-pass rendering: base mesh first, then subdivision in local region
// - Properly stitches high-res triangles onto base mesh

class DynamicLODSphere
{
public:
    // Calculate dynamic tessellation based on camera distance
    // Returns (baseSlices, baseStacks, localSlices, localStacks) tuple and closest point on sphere to camera
    // baseSlices/baseStacks: tessellation for regions outside the local high-detail area
    // localSlices/localStacks: tessellation for the circular region (radius = 0.25 * planet radius) around closest point
    static std::tuple<int, int, int, int> calculateTessellation(const glm::vec3 &spherePosition,
                                                                 float sphereRadius,
                                                                 const glm::vec3 &cameraPos,
                                                                 glm::vec3 &closestPointOnSphere);

    // Draw a sphere with dynamic LOD and adaptive tessellation
    // position: center of the sphere in world space
    // radius: radius of the sphere
    // poleDir: direction of the sphere's north pole (rotation axis)
    // primeDir: direction of the prime meridian (0° longitude at equator)
    // cameraPos: camera position in world space
    // cameraDir: camera forward direction (normalized)
    // fovRadians: camera field of view in radians
    // disableCulling: if true, disables visibility culling (useful for wireframe mode)
    //
    // Rendering modes:
    // - When distance > 5 * radius: renders as a circular fan (pie-style) with center point
    //   - Triangle count: 128 at far distance, reducing to 8 at 5 radii
    // - When distance <= 5 * radius: renders full sphere with adaptive tessellation
    static void draw(const glm::vec3 &position,
                     float radius,
                     const glm::vec3 &poleDir,
                     const glm::vec3 &primeDir,
                     const glm::vec3 &cameraPos,
                     const glm::vec3 &cameraDir,
                     float fovRadians,
                     bool disableCulling = false);

private:
    // Helper function to determine LOD level based on distance from camera
    // Returns: 0 = base, 1 = 1/2 radius, 2 = 1/4 radius, 3 = 1/8 radius, 4 = 1/16 radius
    // The radii are centered around the camera position, not the closest point on sphere
    static int getLODLevel(const glm::vec3 &worldPos,
                           const glm::vec3 &cameraPos,
                           float sphereRadius);

    // Get smooth LOD factor (0.0 to 4.0) with transition zones for smooth blending
    // Uses smoothstep to create gradual transitions between LOD levels
    static float getSmoothLODFactor(const glm::vec3 &worldPos,
                                    const glm::vec3 &cameraPos,
                                    float sphereRadius);

    // Helper function to check triangle visibility
    static bool isTriangleVisible(const glm::vec3 &v1,
                                  const glm::vec3 &v2,
                                  const glm::vec3 &v3,
                                  const glm::vec3 &n1,
                                  const glm::vec3 &n2,
                                  const glm::vec3 &n3,
                                  const glm::vec3 &cameraPos,
                                  const glm::vec3 &cameraDir,
                                  float fovRadians,
                                  float radius,
                                  bool disableCulling);

    // Helper function to render a single triangle
    static void renderTriangle(const glm::vec3 &v1,
                               const glm::vec3 &v2,
                               const glm::vec3 &v3,
                               const glm::vec3 &n1,
                               const glm::vec3 &n2,
                               const glm::vec3 &n3,
                               const glm::vec2 &uv1,
                               const glm::vec2 &uv2,
                               const glm::vec2 &uv3,
                               const glm::vec3 &position,
                               const glm::vec3 &cameraPos,
                               const glm::vec3 &cameraDir,
                               float fovRadians,
                               float radius,
                               bool disableCulling,
                               int &verticesDrawn);
};
