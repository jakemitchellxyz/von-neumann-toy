#pragma once

#include <glm/glm.hpp>
#include <vector>

// Forward declaration
struct CelestialBody;

// ==================================
// Gravity Grid Visualization
// ==================================
// Renders a 3D volumetric grid that is warped by gravitational potential
// to visualize spacetime curvature throughout the solar system

class GravityGrid {
public:
    // Grid configuration
    static constexpr int GRID_LAYERS = 5;          // Number of layers in the third dimension
    static constexpr float WARP_SCALE = 0.3f;      // How much gravity warps the grid
    
    GravityGrid() = default;
    
    // Update the grid deformation based on celestial bodies
    // extent: half-size of the grid (should be > Pluto's orbit)
    // bodies: list of all celestial bodies affecting the grid
    // gridLines: number of lines per axis (from global setting)
    void update(float extent, const std::vector<CelestialBody*>& bodies, int gridLines);
    
    // Render the warped 3D grid
    // cameraPos: camera position for distance-based opacity fading
    void draw(const glm::vec3& cameraPos) const;
    
private:
    // 3D grid vertex data for each plane orientation
    // XZ planes (horizontal at different Y levels)
    std::vector<std::vector<glm::vec3>> xzPlanes;
    // XY planes (vertical, facing Z)
    std::vector<std::vector<glm::vec3>> xyPlanes;
    // YZ planes (vertical, facing X)
    std::vector<std::vector<glm::vec3>> yzPlanes;
    
    float gridExtent = 1.0f;
    int currentGridLines = 25;
    
    // Calculate warping direction and magnitude at a point
    glm::vec3 calculateWarp(const glm::vec3& point, const std::vector<CelestialBody*>& bodies) const;
};

// Global gravity grid instance
extern GravityGrid g_gravityGrid;
