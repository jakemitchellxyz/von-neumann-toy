#include "gravity-grid.h"
#include "../types/celestial-body.h"
#include "constants.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>
#include <iostream>

// Global instance
GravityGrid g_gravityGrid;

// Debug flag - print once per update
static bool g_debugPrinted = false;

// ==================================
// Gravitational Warp Calculation
// ==================================
// Creates visible warping toward massive bodies using actual gravitational physics
// G, AU_IN_METERS, UNITS_PER_AU from constants.h

glm::vec3 GravityGrid::calculateWarp(const glm::vec3& point, const std::vector<CelestialBody*>& bodies) const {
    // Convert display units to meters
    // 1 display unit = AU_IN_METERS / UNITS_PER_AU meters
    const double displayToMeters = AU_IN_METERS / static_cast<double>(UNITS_PER_AU);
    
    glm::dvec3 pointMeters = glm::dvec3(point) * displayToMeters;
    glm::dvec3 totalField(0.0);
    
    for (const auto* body : bodies) {
        if (!body || body->mass <= 0.0) continue;
        
        // Body position in meters
        glm::dvec3 bodyPosMeters = glm::dvec3(body->position) * displayToMeters;
        
        // Vector from point to body (gravity pulls TOWARD the body)
        glm::dvec3 toBody = bodyPosMeters - pointMeters;
        double distanceMeters = glm::length(toBody);
        
        // Softening to avoid singularity - use body's display radius in meters
        double softeningMeters = static_cast<double>(body->displayRadius) * displayToMeters * 2.0;
        double effectiveDistance = std::max(distanceMeters, softeningMeters);
        
        if (effectiveDistance < 1.0) continue;
        
        // Gravitational field magnitude: g = GM/r² (in m/s²)
        double fieldMagnitude = G * body->mass / (effectiveDistance * effectiveDistance);
        
        // Direction toward the body
        glm::dvec3 direction = (distanceMeters > 1.0) ? (toBody / distanceMeters) : glm::dvec3(0.0);
        
        // Accumulate field contribution
        totalField += direction * fieldMagnitude;
        
        // Debug output
        if (!g_debugPrinted && body->name == "Sun") {
            std::cout << "[GravityGrid] Sun: mass=" << body->mass << " kg"
                      << ", G=" << G
                      << ", dist=" << distanceMeters << " m"
                      << ", field=" << fieldMagnitude << " m/s²\n";
        }
    }
    
    double fieldMagnitude = glm::length(totalField);
    
    if (fieldMagnitude < 1e-30) {
        return glm::vec3(0.0f);
    }
    
    // Normalize direction
    glm::dvec3 fieldDirection = totalField / fieldMagnitude;
    
    // Convert gravitational field (m/s²) to visual warp (display units)
    // The field at Earth's orbit from Sun is ~0.006 m/s²
    // We want this to produce a visible warp, so we use aggressive scaling
    
    // Use logarithmic scaling: log10(1 + field * scaleFactor)
    // This compresses the huge dynamic range while preserving relative strengths
    double logScaleFactor = 1e14;  // Amplify small field values
    double logField = std::log10(1.0 + fieldMagnitude * logScaleFactor);
    
    // logField will range from ~0 (very weak) to ~15 (very strong near Sun)
    // Scale to display units based on grid extent
    double warpDisplayUnits = logField * static_cast<double>(gridExtent) * 0.02 
                             * static_cast<double>(g_gravityWarpStrength);
    
    // Clamp to reasonable maximum
    double maxWarp = static_cast<double>(gridExtent) * 0.25;
    warpDisplayUnits = std::min(warpDisplayUnits, maxWarp);
    
    if (!g_debugPrinted) {
        std::cout << "[GravityGrid] First point: fieldMag=" << fieldMagnitude 
                  << " m/s², logField=" << logField
                  << ", warp=" << warpDisplayUnits << " display units\n";
        g_debugPrinted = true;
    }
    
    return glm::vec3(fieldDirection * warpDisplayUnits);
}

void GravityGrid::update(float extent, const std::vector<CelestialBody*>& bodies, int gridLines) {
    gridExtent = extent;
    currentGridLines = gridLines;
    
    // Reset debug flag
    g_debugPrinted = false;
    
    std::cout << "[GravityGrid] Update: " << bodies.size() << " bodies, extent=" << extent 
              << ", G=" << G << ", AU_IN_METERS=" << AU_IN_METERS 
              << ", UNITS_PER_AU=" << UNITS_PER_AU << "\n";
    
    float spacing = (2.0f * extent) / static_cast<float>(gridLines - 1);
    float layerSpacing = (2.0f * extent) / static_cast<float>(GRID_LAYERS + 1);
    
    // Clear previous data
    xzPlanes.clear();
    xyPlanes.clear();
    yzPlanes.clear();
    
    // Generate XZ planes (horizontal planes at different Y levels)
    xzPlanes.resize(GRID_LAYERS);
    for (int layer = 0; layer < GRID_LAYERS; ++layer) {
        float y = -extent + layerSpacing * (layer + 1);
        xzPlanes[layer].reserve(gridLines * gridLines);
        
        for (int i = 0; i < gridLines; ++i) {
            for (int j = 0; j < gridLines; ++j) {
                float x = -extent + static_cast<float>(i) * spacing;
                float z = -extent + static_cast<float>(j) * spacing;
                
                glm::vec3 basePos(x, y, z);
                glm::vec3 warp = calculateWarp(basePos, bodies);
                
                xzPlanes[layer].push_back(basePos + warp);
            }
        }
    }
    
    // Generate XY planes (vertical planes at different Z levels)
    xyPlanes.resize(GRID_LAYERS);
    for (int layer = 0; layer < GRID_LAYERS; ++layer) {
        float z = -extent + layerSpacing * (layer + 1);
        xyPlanes[layer].reserve(gridLines * gridLines);
        
        for (int i = 0; i < gridLines; ++i) {
            for (int j = 0; j < gridLines; ++j) {
                float x = -extent + static_cast<float>(i) * spacing;
                float y = -extent + static_cast<float>(j) * spacing;
                
                glm::vec3 basePos(x, y, z);
                glm::vec3 warp = calculateWarp(basePos, bodies);
                
                xyPlanes[layer].push_back(basePos + warp);
            }
        }
    }
    
    // Generate YZ planes (vertical planes at different X levels)
    yzPlanes.resize(GRID_LAYERS);
    for (int layer = 0; layer < GRID_LAYERS; ++layer) {
        float x = -extent + layerSpacing * (layer + 1);
        yzPlanes[layer].reserve(gridLines * gridLines);
        
        for (int i = 0; i < gridLines; ++i) {
            for (int j = 0; j < gridLines; ++j) {
                float y = -extent + static_cast<float>(i) * spacing;
                float z = -extent + static_cast<float>(j) * spacing;
                
                glm::vec3 basePos(x, y, z);
                glm::vec3 warp = calculateWarp(basePos, bodies);
                
                yzPlanes[layer].push_back(basePos + warp);
            }
        }
    }
}

void GravityGrid::draw(const glm::vec3& cameraPos) const {
    if (xzPlanes.empty() && xyPlanes.empty() && yzPlanes.empty()) return;
    
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);
    
    int gridLines = currentGridLines;
    
    // Distance at which grid becomes fully invisible (half of grid extent)
    float fadeDistance = gridExtent * 0.5f;
    
    // Helper lambda to calculate opacity based on distance from camera
    auto getAlpha = [&](const glm::vec3& vertex, float baseAlpha) -> float {
        float dist = glm::length(vertex - cameraPos);
        if (dist >= fadeDistance) return 0.0f;
        // Linear fade from baseAlpha at dist=0 to 0 at dist=fadeDistance
        float fade = 1.0f - (dist / fadeDistance);
        return baseAlpha * fade;
    };
    
    // XZ planes (horizontal) - slightly blue tint
    for (const auto& plane : xzPlanes) {
        // Draw lines along X
        for (int j = 0; j < gridLines; ++j) {
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i < gridLines; ++i) {
                int idx = i * gridLines + j;
                if (idx < static_cast<int>(plane.size())) {
                    const glm::vec3& v = plane[idx];
                    float alpha = getAlpha(v, 0.4f);
                    if (alpha > 0.001f) {
                        glColor4f(0.4f, 0.45f, 0.55f, alpha);
                        glVertex3f(v.x, v.y, v.z);
                    }
                }
            }
            glEnd();
        }
        // Draw lines along Z
        for (int i = 0; i < gridLines; ++i) {
            glBegin(GL_LINE_STRIP);
            for (int j = 0; j < gridLines; ++j) {
                int idx = i * gridLines + j;
                if (idx < static_cast<int>(plane.size())) {
                    const glm::vec3& v = plane[idx];
                    float alpha = getAlpha(v, 0.4f);
                    if (alpha > 0.001f) {
                        glColor4f(0.4f, 0.45f, 0.55f, alpha);
                        glVertex3f(v.x, v.y, v.z);
                    }
                }
            }
            glEnd();
        }
    }
    
    // XY planes (vertical, facing Z) - slightly green tint
    for (const auto& plane : xyPlanes) {
        // Draw lines along X
        for (int j = 0; j < gridLines; ++j) {
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i < gridLines; ++i) {
                int idx = i * gridLines + j;
                if (idx < static_cast<int>(plane.size())) {
                    const glm::vec3& v = plane[idx];
                    float alpha = getAlpha(v, 0.35f);
                    if (alpha > 0.001f) {
                        glColor4f(0.4f, 0.55f, 0.45f, alpha);
                        glVertex3f(v.x, v.y, v.z);
                    }
                }
            }
            glEnd();
        }
        // Draw lines along Y
        for (int i = 0; i < gridLines; ++i) {
            glBegin(GL_LINE_STRIP);
            for (int j = 0; j < gridLines; ++j) {
                int idx = i * gridLines + j;
                if (idx < static_cast<int>(plane.size())) {
                    const glm::vec3& v = plane[idx];
                    float alpha = getAlpha(v, 0.35f);
                    if (alpha > 0.001f) {
                        glColor4f(0.4f, 0.55f, 0.45f, alpha);
                        glVertex3f(v.x, v.y, v.z);
                    }
                }
            }
            glEnd();
        }
    }
    
    // YZ planes (vertical, facing X) - slightly red tint
    for (const auto& plane : yzPlanes) {
        // Draw lines along Y
        for (int j = 0; j < gridLines; ++j) {
            glBegin(GL_LINE_STRIP);
            for (int i = 0; i < gridLines; ++i) {
                int idx = i * gridLines + j;
                if (idx < static_cast<int>(plane.size())) {
                    const glm::vec3& v = plane[idx];
                    float alpha = getAlpha(v, 0.35f);
                    if (alpha > 0.001f) {
                        glColor4f(0.55f, 0.45f, 0.4f, alpha);
                        glVertex3f(v.x, v.y, v.z);
                    }
                }
            }
            glEnd();
        }
        // Draw lines along Z
        for (int i = 0; i < gridLines; ++i) {
            glBegin(GL_LINE_STRIP);
            for (int j = 0; j < gridLines; ++j) {
                int idx = i * gridLines + j;
                if (idx < static_cast<int>(plane.size())) {
                    const glm::vec3& v = plane[idx];
                    float alpha = getAlpha(v, 0.35f);
                    if (alpha > 0.001f) {
                        glColor4f(0.55f, 0.45f, 0.4f, alpha);
                        glVertex3f(v.x, v.y, v.z);
                    }
                }
            }
            glEnd();
        }
    }
    
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}
