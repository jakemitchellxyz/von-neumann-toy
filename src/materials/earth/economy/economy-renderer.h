#pragma once

#include "earth-economy.h"
#include <glm/glm.hpp>

// ============================================================================
// Economy Renderer
// ============================================================================
// Handles rendering of city labels and economy-related visualizations
// on Earth's surface.

class EconomyRenderer
{
public:
    EconomyRenderer();
    ~EconomyRenderer();

    // Delete copy and move operations
    EconomyRenderer(const EconomyRenderer &) = delete;
    EconomyRenderer &operator=(const EconomyRenderer &) = delete;
    EconomyRenderer(EconomyRenderer &&) = delete;
    EconomyRenderer &operator=(EconomyRenderer &&) = delete;

    // ==================================
    // Initialization
    // ==================================

    // Initialize the renderer (load shaders, etc.)
    // Call this after OpenGL context is created.
    // Returns true if successful
    bool initialize();

    // Check if renderer is ready
    bool isInitialized() const { return initialized_; }

    // ==================================
    // Rendering
    // ==================================

    // Draw city labels on Earth's surface
    // earthPosition: Earth's center position in world space
    // earthRadius: Earth's display radius
    // cameraPos: Camera position for billboarding
    // cameraFront: Camera forward direction
    // cameraUp: Camera up direction
    // poleDirection: Planet's north pole direction (from SPICE, rotates with planet)
    // primeMeridianDirection: Prime meridian direction (from SPICE, rotates with planet)
    // maxDistance: Maximum distance to render labels (cull far cities)
    void drawCityLabels(const glm::vec3 &earthPosition,
                        float earthRadius,
                        const glm::vec3 &cameraPos,
                        const glm::vec3 &cameraFront,
                        const glm::vec3 &cameraUp,
                        const glm::vec3 &poleDirection,
                        const glm::vec3 &primeMeridianDirection,
                        float maxDistance = 200000.0f); // Increased default distance

    // Enable/disable city label rendering
    void setShowCityLabels(bool show) { showCityLabels_ = show; }
    bool getShowCityLabels() const { return showCityLabels_; }

    // Set minimum city population to display (filters small cities)
    void setMinPopulation(float minPop) { minPopulation_ = minPop; }
    float getMinPopulation() const { return minPopulation_; }

private:
    // Cleanup resources
    void cleanup();

    // ==================================
    // State
    // ==================================
    bool initialized_;
    bool showCityLabels_;
    float minPopulation_; // Minimum population to display label

    // Shader program for city rendering (if needed in future)
    GLuint shaderProgram_;
    bool shaderAvailable_;
};

// ==================================
// Global Economy Renderer Instance
// ==================================
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern EconomyRenderer g_economyRenderer;
