// ============================================================================
// DynamicLODSphere Implementation
// ============================================================================

#include "dynamic-lod-sphere.h"
#include "../concerns/constants.h"
#include "../concerns/ui-overlay.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// ============================================================================
// Tessellation Calculation
// ============================================================================

std::tuple<int, int, int, int> DynamicLODSphere::calculateTessellation(const glm::vec3 &spherePosition,
                                                                       float sphereRadius,
                                                                       const glm::vec3 &cameraPos,
                                                                       glm::vec3 &closestPointOnSphere)
{
    glm::vec3 toSphere = spherePosition - cameraPos;
    float distance = glm::length(toSphere);
    float distanceInRadii = distance / sphereRadius;

    // Find the point on the sphere closest to the camera
    glm::vec3 toSphereNorm = distance > 0.001f ? toSphere / distance : glm::vec3(0.0f, 0.0f, 1.0f);
    closestPointOnSphere = spherePosition - toSphereNorm * sphereRadius;

    // If distance > 5 * radius, use base tessellation (no local detail)
    if (distanceInRadii >= TESSELATION_DISTANCE_THRESHOLD)
    {
        return {SPHERE_BASE_SLICES, SPHERE_BASE_STACKS, SPHERE_BASE_SLICES, SPHERE_BASE_STACKS};
    }

    // Calculate base tessellation multiplier based on distance
    float t = (TESSELATION_DISTANCE_THRESHOLD - distanceInRadii) / (TESSELATION_DISTANCE_THRESHOLD - 1.0f);
    t = glm::clamp(t, 0.0f, 1.0f);

    float baseMultiplier = 1.0f + t * (MAX_TESSELATION_MULTIPLIER - 1.0f);

    // Calculate base tessellation values (round to nearest even number)
    int baseSlices = static_cast<int>(std::round(SPHERE_BASE_SLICES * baseMultiplier / 2.0f)) * 2;
    int baseStacks = static_cast<int>(std::round(SPHERE_BASE_STACKS * baseMultiplier / 2.0f)) * 2;

    // Ensure minimum tessellation
    baseSlices = std::max(baseSlices, SPHERE_BASE_SLICES);
    baseStacks = std::max(baseStacks, SPHERE_BASE_STACKS);

    // Calculate local high-detail tessellation for the circular region
    int localSlices = baseSlices * LOCAL_TESSELATION_MULTIPLIER;
    int localStacks = baseStacks * LOCAL_TESSELATION_MULTIPLIER;

    return {baseSlices, baseStacks, localSlices, localStacks};
}

// ============================================================================
// Helper Functions
// ============================================================================

int DynamicLODSphere::getLODLevel(const glm::vec3 &worldPos, const glm::vec3 &cameraPos, float sphereRadius)
{
    // Calculate distance from camera to this point on the sphere surface
    glm::vec3 toPoint = worldPos - cameraPos;
    float distance = glm::length(toPoint);

    // Define 4 radii around camera position: 1/2, 1/4, 1/8, 1/16 of planet radius
    float radius1 = sphereRadius * 0.5f;    // 1/2 radius
    float radius2 = sphereRadius * 0.25f;   // 1/4 radius
    float radius3 = sphereRadius * 0.125f;  // 1/8 radius
    float radius4 = sphereRadius * 0.0625f; // 1/16 radius

    // Return LOD level based on which radius band the point falls into
    // Higher number = closer to camera = higher tessellation
    if (distance <= radius4)
        return 4; // Highest detail (1/16 radius)
    else if (distance <= radius3)
        return 3; // High detail (1/8 radius)
    else if (distance <= radius2)
        return 2; // Medium-high detail (1/4 radius)
    else if (distance <= radius1)
        return 1; // Medium detail (1/2 radius)
    else
        return 0; // Base detail (outside all radii)
}

// Get smooth LOD factor (0.0 to 4.0) with transition zones for smooth blending
float DynamicLODSphere::getSmoothLODFactor(const glm::vec3 &worldPos, const glm::vec3 &cameraPos, float sphereRadius)
{
    // Calculate distance from camera to this point on the sphere surface
    glm::vec3 toPoint = worldPos - cameraPos;
    float distance = glm::length(toPoint);

    // Define 4 radii around camera position: 1/2, 1/4, 1/8, 1/16 of planet radius
    float radius1 = sphereRadius * 0.5f;    // 1/2 radius
    float radius2 = sphereRadius * 0.25f;   // 1/4 radius
    float radius3 = sphereRadius * 0.125f;  // 1/8 radius
    float radius4 = sphereRadius * 0.0625f; // 1/16 radius

    // Add transition zones (20% overlap) for smooth blending between LOD levels
    const float transitionWidth = 0.2f; // 20% transition zone

    // Use smoothstep for smooth interpolation between levels
    // Calculate continuous LOD factor based on distance with smooth transitions

    if (distance <= radius4)
    {
        // LOD 4 region - smooth transition out to LOD 3
        float transitionStart = radius3;
        float transitionEnd = radius4;
        if (distance > transitionEnd)
        {
            float t = (distance - transitionEnd) / (transitionStart - transitionEnd);
            t = glm::clamp(t, 0.0f, 1.0f);
            return 4.0f - glm::smoothstep(0.0f, 1.0f, t);
        }
        return 4.0f;
    }
    else if (distance <= radius3 * (1.0f + transitionWidth))
    {
        // Transition zone between LOD 3 and LOD 4
        float t = (distance - radius3) / (radius3 * transitionWidth);
        t = glm::clamp(t, 0.0f, 1.0f);
        return 3.0f + glm::smoothstep(0.0f, 1.0f, t);
    }
    else if (distance <= radius3)
    {
        // LOD 3 region - smooth transition out to LOD 2
        float transitionStart = radius2;
        float transitionEnd = radius3;
        if (distance > transitionEnd)
        {
            float t = (distance - transitionEnd) / (transitionStart - transitionEnd);
            t = glm::clamp(t, 0.0f, 1.0f);
            return 3.0f - glm::smoothstep(0.0f, 1.0f, t);
        }
        return 3.0f;
    }
    else if (distance <= radius2 * (1.0f + transitionWidth))
    {
        // Transition zone between LOD 2 and LOD 3
        float t = (distance - radius2) / (radius2 * transitionWidth);
        t = glm::clamp(t, 0.0f, 1.0f);
        return 2.0f + glm::smoothstep(0.0f, 1.0f, t);
    }
    else if (distance <= radius2)
    {
        // LOD 2 region - smooth transition out to LOD 1
        float transitionStart = radius1;
        float transitionEnd = radius2;
        if (distance > transitionEnd)
        {
            float t = (distance - transitionEnd) / (transitionStart - transitionEnd);
            t = glm::clamp(t, 0.0f, 1.0f);
            return 2.0f - glm::smoothstep(0.0f, 1.0f, t);
        }
        return 2.0f;
    }
    else if (distance <= radius1 * (1.0f + transitionWidth))
    {
        // Transition zone between LOD 1 and LOD 2
        float t = (distance - radius1) / (radius1 * transitionWidth);
        t = glm::clamp(t, 0.0f, 1.0f);
        return 1.0f + glm::smoothstep(0.0f, 1.0f, t);
    }
    else if (distance <= radius1)
    {
        // LOD 1 region - smooth transition out to LOD 0
        float transitionStart = radius1 * 2.0f;
        float transitionEnd = radius1;
        if (distance > transitionEnd)
        {
            float t = (distance - transitionEnd) / (transitionStart - transitionEnd);
            t = glm::clamp(t, 0.0f, 1.0f);
            return 1.0f - glm::smoothstep(0.0f, 1.0f, t);
        }
        return 1.0f;
    }
    else
    {
        // LOD 0 (base detail)
        return 0.0f;
    }
}

bool DynamicLODSphere::isTriangleVisible(const glm::vec3 &v1,
                                         const glm::vec3 &v2,
                                         const glm::vec3 &v3,
                                         const glm::vec3 &n1,
                                         const glm::vec3 &n2,
                                         const glm::vec3 &n3,
                                         const glm::vec3 &cameraPos,
                                         const glm::vec3 &cameraDir,
                                         float fovRadians,
                                         float radius,
                                         bool disableCulling)
{
    if (disableCulling)
        return true;

    const float MAX_ANGLE_FROM_CAMERA = 0.6f * static_cast<float>(PI);
    const float COS_MAX_ANGLE = std::cos(MAX_ANGLE_FROM_CAMERA);

    // Expand frustum by 15 degrees for better edge handling
    float halfFov = fovRadians * 0.5f;
    constexpr float FRUSTUM_EXPANSION_DEGREES = 15.0f;
    float expandedHalfFov = halfFov + FRUSTUM_EXPANSION_DEGREES * static_cast<float>(PI) / 180.0f;
    float cosExpandedHalfFov = std::cos(expandedHalfFov);

    glm::vec3 toV1 = v1 - cameraPos;
    glm::vec3 toV2 = v2 - cameraPos;
    glm::vec3 toV3 = v3 - cameraPos;

    float dist1 = glm::length(toV1);
    float dist2 = glm::length(toV2);
    float dist3 = glm::length(toV3);

    glm::vec3 dir1 = dist1 > 0.001f ? toV1 / dist1 : glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 dir2 = dist2 > 0.001f ? toV2 / dist2 : glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 dir3 = dist3 > 0.001f ? toV3 / dist3 : glm::vec3(0.0f, 0.0f, 1.0f);

    bool v1FrontFacing = dist1 <= radius * 0.1f || glm::dot(n1, -cameraDir) >= COS_MAX_ANGLE;
    bool v2FrontFacing = dist2 <= radius * 0.1f || glm::dot(n2, -cameraDir) >= COS_MAX_ANGLE;
    bool v3FrontFacing = dist3 <= radius * 0.1f || glm::dot(n3, -cameraDir) >= COS_MAX_ANGLE;

    bool allBackFacing = !v1FrontFacing && !v2FrontFacing && !v3FrontFacing;
    if (allBackFacing)
        return false;

    bool v1InFrustum = dist1 <= radius * 0.1f || glm::dot(dir1, cameraDir) >= cosExpandedHalfFov;
    bool v2InFrustum = dist2 <= radius * 0.1f || glm::dot(dir2, cameraDir) >= cosExpandedHalfFov;
    bool v3InFrustum = dist3 <= radius * 0.1f || glm::dot(dir3, cameraDir) >= cosExpandedHalfFov;

    return v1InFrustum || v2InFrustum || v3InFrustum;
}

void DynamicLODSphere::renderTriangle(const glm::vec3 &v1,
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
                                      int &verticesDrawn)
{
    if (!isTriangleVisible(v1, v2, v3, n1, n2, n3, cameraPos, cameraDir, fovRadians, radius, disableCulling))
        return;

    // TODO: Migrate dynamic LOD sphere rendering to Vulkan
    // glTexCoord2f(uv1.x, uv1.y); // REMOVED - migrate to Vulkan vertex buffer
    // glNormal3f(n1.x, n1.y, n1.z); // REMOVED - migrate to Vulkan vertex buffer
    // glVertex3f(v1.x - position.x, v1.y - position.y, v1.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

    // glTexCoord2f(uv2.x, uv2.y); // REMOVED - migrate to Vulkan vertex buffer
    // glNormal3f(n2.x, n2.y, n2.z); // REMOVED - migrate to Vulkan vertex buffer
    // glVertex3f(v2.x - position.x, v2.y - position.y, v2.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

    // glTexCoord2f(uv3.x, uv3.y); // REMOVED - migrate to Vulkan vertex buffer
    // glNormal3f(n3.x, n3.y, n3.z); // REMOVED - migrate to Vulkan vertex buffer
    // glVertex3f(v3.x - position.x, v3.y - position.y, v3.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

    verticesDrawn += 3;
    CountTriangles(GL_TRIANGLES, 3);
}

// ============================================================================
// Main Rendering Function
// ============================================================================

void DynamicLODSphere::draw(const glm::vec3 &position,
                            float radius,
                            const glm::vec3 &poleDir,
                            const glm::vec3 &primeDir,
                            const glm::vec3 &cameraPos,
                            const glm::vec3 &cameraDir,
                            float fovRadians,
                            bool disableCulling)
{
    // Calculate distance to sphere
    glm::vec3 toSphere = position - cameraPos;
    float distance = glm::length(toSphere);
    float distanceInRadii = distance / radius;

    // Find the point on the sphere closest to the camera
    glm::vec3 toSphereNorm = distance > 0.001f ? toSphere / distance : glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 closestPointOnSphere = position - toSphereNorm * radius;

    // If distance > 5 * radius, render as pie-style circle (fan of triangles)
    if (distanceInRadii > TESSELATION_DISTANCE_THRESHOLD)
    {
        // Calculate number of triangles: interpolate from 128 (far) to 8 (at 5 radii)
        // Use a reasonable maximum distance for interpolation (e.g., 20 radii)
        constexpr float MAX_FAR_DISTANCE_RADII = 20.0f;
        float t = (distanceInRadii - TESSELATION_DISTANCE_THRESHOLD) /
                  (MAX_FAR_DISTANCE_RADII - TESSELATION_DISTANCE_THRESHOLD);
        t = glm::clamp(t, 0.0f, 1.0f);
        // At far distance (t=1): use MAX triangles, at 5 radii (t=0): use MIN triangles
        int numTriangles = static_cast<int>(
            std::round(FAR_TRIANGLE_COUNT_MAX - t * (FAR_TRIANGLE_COUNT_MAX - FAR_TRIANGLE_COUNT_MIN)));
        numTriangles = std::max(numTriangles, FAR_TRIANGLE_COUNT_MIN);
        numTriangles = std::min(numTriangles, FAR_TRIANGLE_COUNT_MAX);

        // Calculate visible circle radius on sphere surface
        // The visible angular radius is determined by the sphere's apparent size from the camera
        // For a sphere of radius r at distance d, the angular radius is: asin(r/d)
        // This gives us how big the sphere appears, which determines the visible region
        float sphereAngularRadius = std::asin(glm::clamp(radius / distance, 0.0f, 1.0f));
        float hemisphereAngle = 1.57079632679f; // PI/2 = 90 degrees (full hemisphere)

        // Use the sphere's angular radius, but cap at hemisphere (can't see more than half the sphere)
        // This is used for determining UV coverage, not for visual size
        float VISIBLE_ANGLE_RADIANS = std::min(sphereAngularRadius, hemisphereAngle);

        // Ensure minimum visible size - if sphere is very far, still show a reasonable region
        // This prevents the fan from becoming too small when extremely far away
        VISIBLE_ANGLE_RADIANS = std::max(VISIBLE_ANGLE_RADIANS, 0.5f); // At least 0.5 radians (~28 degrees)

        // Build orthonormal basis for the circle plane
        // The circle is centered at closestPointOnSphere and lies in a plane tangent to the sphere
        glm::vec3 centerDir = glm::normalize(closestPointOnSphere - position);

        // Calculate distance from camera to the circle plane (at closestPointOnSphere)
        glm::vec3 toClosestPoint = closestPointOnSphere - cameraPos;
        float distanceToCircle = glm::length(toClosestPoint);

        // For circle radius calculation, use the actual angular size (without minimum clamp)
        // to ensure the circle matches the sphere's visual size at all distances
        float actualAngularRadius = std::min(sphereAngularRadius, hemisphereAngle);

        // Build two perpendicular vectors in the tangent plane at closestPointOnSphere
        // Use pole direction and prime meridian to create a consistent basis
        glm::vec3 north = glm::normalize(poleDir);
        glm::vec3 east = primeDir - glm::dot(primeDir, north) * north;
        if (glm::length(east) < 0.001f)
        {
            if (std::abs(north.y) < 0.9f)
            {
                east = glm::normalize(glm::cross(north, glm::vec3(0.0f, 1.0f, 0.0f)));
            }
            else
            {
                east = glm::normalize(glm::cross(north, glm::vec3(1.0f, 0.0f, 0.0f)));
            }
        }
        else
        {
            east = glm::normalize(east);
        }

        // Project north and east onto the tangent plane at closestPointOnSphere
        glm::vec3 tangentNorth = north - glm::dot(north, centerDir) * centerDir;
        glm::vec3 tangentEast = east - glm::dot(east, centerDir) * centerDir;

        // Normalize and ensure they're perpendicular
        if (glm::length(tangentNorth) > 0.001f)
        {
            tangentNorth = glm::normalize(tangentNorth);
        }
        else
        {
            // If centerDir is parallel to north, use a different basis
            tangentNorth = glm::normalize(glm::cross(centerDir, glm::vec3(1.0f, 0.0f, 0.0f)));
            if (glm::length(tangentNorth) < 0.001f)
            {
                tangentNorth = glm::normalize(glm::cross(centerDir, glm::vec3(0.0f, 0.0f, 1.0f)));
            }
        }

        if (glm::length(tangentEast) > 0.001f)
        {
            tangentEast = glm::normalize(tangentEast);
        }
        else
        {
            tangentEast = glm::normalize(glm::cross(centerDir, tangentNorth));
        }

        // Ensure they're perpendicular
        tangentEast = glm::normalize(glm::cross(centerDir, tangentNorth));

        // Build sphere's local coordinate system for UV mapping
        glm::vec3 south90 = glm::normalize(glm::cross(north, east));

        // Helper function to convert direction to UV using sphere's coordinate system
        auto directionToUVSphere = [&](const glm::vec3 &dir) -> glm::vec2 {
            float localX = glm::dot(dir, east);
            float localY = glm::dot(dir, north);
            float localZ = glm::dot(dir, south90);

            float len = std::sqrt(localX * localX + localY * localY + localZ * localZ);
            if (len < 0.001f)
            {
                return glm::vec2(0.5f, 0.5f);
            }
            localX /= len;
            localY /= len;
            localZ /= len;

            double latitude = std::asin(glm::clamp(static_cast<double>(localY), -1.0, 1.0));
            double longitude = std::atan2(static_cast<double>(localZ), static_cast<double>(localX));

            double u = (longitude / static_cast<double>(PI) + 1.0) * 0.5;
            double v = 0.5 - (latitude / static_cast<double>(PI));

            return glm::vec2(static_cast<float>(u), static_cast<float>(v));
        };

        // Calculate the visual radius of the flat circle
        // The circle is at distanceToCircle from the camera and should subtend the same angle
        // as the sphere (actualAngularRadius) to appear the same size
        // Use actualAngularRadius (not clamped to minimum) so it scales correctly with distance
        float circleRadius = distanceToCircle * std::tan(actualAngularRadius);

        glPushMatrix();
        glTranslatef(position.x, position.y, position.z);

        int verticesDrawn = 0;

        // TODO: Migrate dynamic LOD sphere rendering to Vulkan
        // Render pie-style circle as flat fan of triangles
        // glBegin(GL_TRIANGLES); // REMOVED - migrate to Vulkan
        if (disableCulling)
        {
            glColor3f(0.8f, 0.9f, 1.0f);
        }

        // Center point (on flat circle, at closestPointOnSphere)
        glm::vec3 centerNormal = centerDir; // Radial normal from sphere center
        glm::vec2 centerUV = directionToUVSphere(centerDir);
        centerUV.y = 1.0f - centerUV.y; // Flip V coordinate to match shader expectations

        for (int i = 0; i < numTriangles; i++)
        {
            float angle1 = 2.0f * static_cast<float>(PI) * static_cast<float>(i) / static_cast<float>(numTriangles);
            float angle2 = 2.0f * static_cast<float>(PI) * static_cast<float>(i + 1) / static_cast<float>(numTriangles);

            // Calculate points on flat circle edge
            float circleX1 = circleRadius * std::cos(angle1);
            float circleY1 = circleRadius * std::sin(angle1);
            float circleX2 = circleRadius * std::cos(angle2);
            float circleY2 = circleRadius * std::sin(angle2);

            // Convert to world positions on the flat circle (tangent plane)
            glm::vec3 flatPoint1 = closestPointOnSphere + circleX1 * tangentEast + circleY1 * tangentNorth;
            glm::vec3 flatPoint2 = closestPointOnSphere + circleX2 * tangentEast + circleY2 * tangentNorth;

            // Calculate UV coordinates directly from direction from sphere center to flat point
            glm::vec3 dir1 = glm::normalize(flatPoint1 - position);
            glm::vec3 dir2 = glm::normalize(flatPoint2 - position);
            glm::vec2 uv1 = directionToUVSphere(dir1);
            glm::vec2 uv2 = directionToUVSphere(dir2);

            // Flip V coordinate to match shader expectations
            uv1.y = 1.0f - uv1.y;
            uv2.y = 1.0f - uv2.y;

            // Calculate normals as radial directions from sphere center (for proper shader calculations)
            glm::vec3 normal1 = dir1; // Radial normal from sphere center to point
            glm::vec3 normal2 = dir2; // Radial normal from sphere center to point

            // Render triangle: center, edge1, edge2 (all on flat circle)
            if (disableCulling || isTriangleVisible(closestPointOnSphere,
                                                    flatPoint1,
                                                    flatPoint2,
                                                    centerNormal,
                                                    normal1,
                                                    normal2,
                                                    cameraPos,
                                                    cameraDir,
                                                    fovRadians,
                                                    radius,
                                                    disableCulling))
            {
                // glTexCoord2f(centerUV.x, centerUV.y); // REMOVED - migrate to Vulkan vertex buffer
                // glNormal3f(centerNormal.x, centerNormal.y, centerNormal.z); // REMOVED - migrate to Vulkan vertex buffer
                // glVertex3f(closestPointOnSphere.x - position.x,
                //            closestPointOnSphere.y - position.y,
                //            closestPointOnSphere.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

                // glTexCoord2f(uv1.x, uv1.y); // REMOVED - migrate to Vulkan vertex buffer
                // glNormal3f(normal1.x, normal1.y, normal1.z); // REMOVED - migrate to Vulkan vertex buffer
                // glVertex3f(flatPoint1.x - position.x, flatPoint1.y - position.y, flatPoint1.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

                // glTexCoord2f(uv2.x, uv2.y); // REMOVED - migrate to Vulkan vertex buffer
                // glNormal3f(normal2.x, normal2.y, normal2.z); // REMOVED - migrate to Vulkan vertex buffer
                // glVertex3f(flatPoint2.x - position.x, flatPoint2.y - position.y, flatPoint2.z - position.z); // REMOVED - migrate to Vulkan vertex buffer

                verticesDrawn += 3;
                CountTriangles(GL_TRIANGLES, 3);
            }
        }

        // glEnd(); // REMOVED - migrate to Vulkan
        glPopMatrix();
        return;
    }

    // Calculate tessellation for normal rendering
    auto [baseSlices, baseStacks, localSlices, localStacks] =
        calculateTessellation(position, radius, cameraPos, closestPointOnSphere);

    // Calculate tessellation multipliers for each LOD level
    // LOD 0 = base, LOD 1 = 1/2 radius, LOD 2 = 1/4 radius, LOD 3 = 1/8 radius, LOD 4 = 1/16 radius
    // Each level gets progressively higher tessellation
    int lodSlices[5], lodStacks[5];
    lodSlices[0] = baseSlices;
    lodStacks[0] = baseStacks;
    lodSlices[1] = baseSlices * 2; // 2x for 1/2 radius
    lodStacks[1] = baseStacks * 2;
    lodSlices[2] = baseSlices * 4; // 4x for 1/4 radius
    lodStacks[2] = baseStacks * 4;
    lodSlices[3] = baseSlices * 8; // 8x for 1/8 radius
    lodStacks[3] = baseStacks * 8;
    lodSlices[4] = baseSlices * 16; // 16x for 1/16 radius
    lodStacks[4] = baseStacks * 16;

    // Build orthonormal basis from pole and prime meridian
    glm::vec3 north = glm::normalize(poleDir);
    glm::vec3 east = primeDir - glm::dot(primeDir, north) * north;
    if (glm::length(east) < 0.001f)
    {
        if (std::abs(north.y) < 0.9f)
        {
            east = glm::normalize(glm::cross(north, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        else
        {
            east = glm::normalize(glm::cross(north, glm::vec3(1.0f, 0.0f, 0.0f)));
        }
    }
    else
    {
        east = glm::normalize(east);
    }
    glm::vec3 south90 = glm::normalize(glm::cross(north, east));

    glPushMatrix();
    glTranslatef(position.x, position.y, position.z);

    int verticesDrawn = 0;

    // TODO: Migrate dynamic LOD sphere rendering to Vulkan
    // First pass: Render base-resolution mesh for entire sphere
    // glBegin(GL_TRIANGLES); // REMOVED - migrate to Vulkan
    if (disableCulling)
    {
        glColor3f(0.8f, 0.9f, 1.0f);
    }

    for (int i = 0; i < baseStacks; i++)
    {
        float phi1 = static_cast<float>(PI) * (static_cast<float>(i) / static_cast<float>(baseStacks) - 0.5f);
        float phi2 = static_cast<float>(PI) * (static_cast<float>(i + 1) / static_cast<float>(baseStacks) - 0.5f);

        float vTexCoord1 = static_cast<float>(i) / static_cast<float>(baseStacks);
        float vTexCoord2 = static_cast<float>(i + 1) / static_cast<float>(baseStacks);

        float cosPhi1 = std::cos(phi1);
        float sinPhi1 = std::sin(phi1);
        float cosPhi2 = std::cos(phi2);
        float sinPhi2 = std::sin(phi2);

        for (int j = 0; j < baseSlices; j++)
        {
            float theta1 = 2.0f * static_cast<float>(PI) * static_cast<float>(j) / static_cast<float>(baseSlices);
            float theta2 = 2.0f * static_cast<float>(PI) * static_cast<float>(j + 1) / static_cast<float>(baseSlices);

            float uCoord1 = static_cast<float>(j) / static_cast<float>(baseSlices);
            float uCoord2 = static_cast<float>(j + 1) / static_cast<float>(baseSlices);

            float theta1Shifted = theta1 - static_cast<float>(PI);
            float theta2Shifted = theta2 - static_cast<float>(PI);

            float cosTheta1 = std::cos(theta1Shifted);
            float sinTheta1 = std::sin(theta1Shifted);
            float cosTheta2 = std::cos(theta2Shifted);
            float sinTheta2 = std::sin(theta2Shifted);

            // Four vertices forming a quad (two triangles)
            glm::vec3 localDir1 = cosPhi1 * (cosTheta1 * east + sinTheta1 * south90) + sinPhi1 * north;
            glm::vec3 worldPos1 = position + radius * localDir1;

            glm::vec3 localDir2 = cosPhi2 * (cosTheta1 * east + sinTheta1 * south90) + sinPhi2 * north;
            glm::vec3 worldPos2 = position + radius * localDir2;

            glm::vec3 localDir3 = cosPhi2 * (cosTheta2 * east + sinTheta2 * south90) + sinPhi2 * north;
            glm::vec3 worldPos3 = position + radius * localDir3;

            glm::vec3 localDir4 = cosPhi1 * (cosTheta2 * east + sinTheta2 * south90) + sinPhi1 * north;
            glm::vec3 worldPos4 = position + radius * localDir4;

            // Determine LOD level for each vertex (based on distance from camera)
            int lod1 = getLODLevel(worldPos1, cameraPos, radius);
            int lod2 = getLODLevel(worldPos2, cameraPos, radius);
            int lod3 = getLODLevel(worldPos3, cameraPos, radius);
            int lod4 = getLODLevel(worldPos4, cameraPos, radius);

            // Find maximum LOD level needed for this quad
            int maxLOD = std::max({lod1, lod2, lod3, lod4});

            // Render base-resolution triangles only if max LOD is 0 (outside all radii)
            if (maxLOD == 0)
            {
                // Triangle 1: v1, v2, v3
                renderTriangle(worldPos1,
                               worldPos2,
                               worldPos3,
                               localDir1,
                               localDir2,
                               localDir3,
                               glm::vec2(uCoord1, vTexCoord1),
                               glm::vec2(uCoord1, vTexCoord2),
                               glm::vec2(uCoord2, vTexCoord2),
                               position,
                               cameraPos,
                               cameraDir,
                               fovRadians,
                               radius,
                               disableCulling,
                               verticesDrawn);

                // Triangle 2: v1, v3, v4
                renderTriangle(worldPos1,
                               worldPos3,
                               worldPos4,
                               localDir1,
                               localDir3,
                               localDir4,
                               glm::vec2(uCoord1, vTexCoord1),
                               glm::vec2(uCoord2, vTexCoord2),
                               glm::vec2(uCoord2, vTexCoord1),
                               position,
                               cameraPos,
                               cameraDir,
                               fovRadians,
                               radius,
                               disableCulling,
                               verticesDrawn);
            }
        }
    }
    glEnd();

    // Render subdivided triangles for each LOD level (1-4)
    // Process from highest to lowest LOD to ensure proper overdraw (higher detail covers lower detail)
    for (int lodLevel = 4; lodLevel >= 1; lodLevel--)
    {
        int targetSlices = lodSlices[lodLevel];
        int targetStacks = lodStacks[lodLevel];

        // Skip if this LOD level doesn't require more tessellation than base
        if (targetSlices <= baseSlices && targetStacks <= baseStacks)
            continue;

        // glBegin(GL_TRIANGLES); // REMOVED - migrate to Vulkan
        if (disableCulling)
        {
            glColor3f(0.8f, 0.9f, 1.0f);
        }

        // Calculate subdivision factors for this LOD level
        int sliceSubdiv = targetSlices / baseSlices;
        int stackSubdiv = targetStacks / baseStacks;

        for (int i = 0; i < baseStacks; i++)
        {
            float phiBase1 = static_cast<float>(PI) * (static_cast<float>(i) / static_cast<float>(baseStacks) - 0.5f);
            float phiBase2 =
                static_cast<float>(PI) * (static_cast<float>(i + 1) / static_cast<float>(baseStacks) - 0.5f);

            for (int j = 0; j < baseSlices; j++)
            {
                float thetaBase1 =
                    2.0f * static_cast<float>(PI) * static_cast<float>(j) / static_cast<float>(baseSlices);
                float thetaBase2 =
                    2.0f * static_cast<float>(PI) * static_cast<float>(j + 1) / static_cast<float>(baseSlices);

                // Check LOD level once per base quad (at center) to avoid n-squared issue
                float thetaMid = (thetaBase1 + thetaBase2) * 0.5f;
                float phiMid = (phiBase1 + phiBase2) * 0.5f;
                float thetaMidShifted = thetaMid - static_cast<float>(PI);
                float cosThetaMid = std::cos(thetaMidShifted);
                float sinThetaMid = std::sin(thetaMidShifted);
                float cosPhiMid = std::cos(phiMid);
                float sinPhiMid = std::sin(phiMid);

                glm::vec3 localDirMid = cosPhiMid * (cosThetaMid * east + sinThetaMid * south90) + sinPhiMid * north;
                glm::vec3 worldPosMid = position + radius * localDirMid;

                // Use smooth LOD factor for smoother transitions
                float smoothLOD = getSmoothLODFactor(worldPosMid, cameraPos, radius);

                // Render if smooth LOD is at or above current level (with small threshold for smooth transitions)
                // This allows overlapping rendering in transition zones for smoother edges
                if (smoothLOD < static_cast<float>(lodLevel) - 0.5f)
                {
                    continue; // Skip this base quad - it doesn't need this LOD level
                }

                // Subdivide this quad
                for (int si = 0; si < stackSubdiv; si++)
                {
                    float t1 = static_cast<float>(si) / static_cast<float>(stackSubdiv);
                    float t2 = static_cast<float>(si + 1) / static_cast<float>(stackSubdiv);
                    float phi1 = phiBase1 + (phiBase2 - phiBase1) * t1;
                    float phi2 = phiBase1 + (phiBase2 - phiBase1) * t2;

                    float cosPhi1 = std::cos(phi1);
                    float sinPhi1 = std::sin(phi1);
                    float cosPhi2 = std::cos(phi2);
                    float sinPhi2 = std::sin(phi2);

                    float vTexCoord1 = (static_cast<float>(i) + t1) / static_cast<float>(baseStacks);
                    float vTexCoord2 = (static_cast<float>(i) + t2) / static_cast<float>(baseStacks);

                    for (int sj = 0; sj < sliceSubdiv; sj++)
                    {
                        float s1 = static_cast<float>(sj) / static_cast<float>(sliceSubdiv);
                        float s2 = static_cast<float>(sj + 1) / static_cast<float>(sliceSubdiv);
                        float theta1 = thetaBase1 + (thetaBase2 - thetaBase1) * s1;
                        float theta2 = thetaBase1 + (thetaBase2 - thetaBase1) * s2;

                        float theta1Shifted = theta1 - static_cast<float>(PI);
                        float theta2Shifted = theta2 - static_cast<float>(PI);
                        float cosTheta1 = std::cos(theta1Shifted);
                        float sinTheta1 = std::sin(theta1Shifted);
                        float cosTheta2 = std::cos(theta2Shifted);
                        float sinTheta2 = std::sin(theta2Shifted);

                        float uCoord1 = (static_cast<float>(j) + s1) / static_cast<float>(baseSlices);
                        float uCoord2 = (static_cast<float>(j) + s2) / static_cast<float>(baseSlices);

                        // Four vertices forming a subdivided quad
                        glm::vec3 localDir1 = cosPhi1 * (cosTheta1 * east + sinTheta1 * south90) + sinPhi1 * north;
                        glm::vec3 worldPos1 = position + radius * localDir1;

                        glm::vec3 localDir2 = cosPhi2 * (cosTheta1 * east + sinTheta1 * south90) + sinPhi2 * north;
                        glm::vec3 worldPos2 = position + radius * localDir2;

                        glm::vec3 localDir3 = cosPhi2 * (cosTheta2 * east + sinTheta2 * south90) + sinPhi2 * north;
                        glm::vec3 worldPos3 = position + radius * localDir3;

                        glm::vec3 localDir4 = cosPhi1 * (cosTheta2 * east + sinTheta2 * south90) + sinPhi1 * north;
                        glm::vec3 worldPos4 = position + radius * localDir4;

                        // Render triangles directly - LOD check already done at base quad level
                        // Triangle 1: v1, v2, v3
                        renderTriangle(worldPos1,
                                       worldPos2,
                                       worldPos3,
                                       localDir1,
                                       localDir2,
                                       localDir3,
                                       glm::vec2(uCoord1, vTexCoord1),
                                       glm::vec2(uCoord1, vTexCoord2),
                                       glm::vec2(uCoord2, vTexCoord2),
                                       position,
                                       cameraPos,
                                       cameraDir,
                                       fovRadians,
                                       radius,
                                       disableCulling,
                                       verticesDrawn);

                        // Triangle 2: v1, v3, v4
                        renderTriangle(worldPos1,
                                       worldPos3,
                                       worldPos4,
                                       localDir1,
                                       localDir3,
                                       localDir4,
                                       glm::vec2(uCoord1, vTexCoord1),
                                       glm::vec2(uCoord2, vTexCoord2),
                                       glm::vec2(uCoord2, vTexCoord1),
                                       position,
                                       cameraPos,
                                       cameraDir,
                                       fovRadians,
                                       radius,
                                       disableCulling,
                                       verticesDrawn);
                    }
                }
            }
        }
        // glEnd(); // REMOVED - migrate to Vulkan
    }

    glPopMatrix();
}
