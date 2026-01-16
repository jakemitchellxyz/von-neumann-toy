// ============================================================================
// Economy Renderer Setup
// ============================================================================
// Initializes shaders and resources for economy rendering

#include "economy-renderer.h"
#include "../../helpers/gl.h"

#include <iostream>

// ============================================================================
// Global Instance
// ============================================================================
EconomyRenderer g_economyRenderer;

// ============================================================================
// Constructor / Destructor
// ============================================================================

EconomyRenderer::EconomyRenderer()
    : initialized_(false), showCityLabels_(false), minPopulation_(0.0f), shaderProgram_(0), shaderAvailable_(false)
{
    // Default minimum population: 0 (show all cities)
    // Can be adjusted via setMinPopulation() if needed
}

EconomyRenderer::~EconomyRenderer()
{
    cleanup();
}

// ============================================================================
// Initialization
// ============================================================================

bool EconomyRenderer::initialize()
{
    if (initialized_)
    {
        return true;
    }

    // For now, we use the existing billboard text rendering system
    // Shader initialization can be added here if needed in the future
    // The billboard text function uses fixed-function OpenGL, so no shader needed
    shaderAvailable_ = true; // Using fixed-function pipeline for now

    initialized_ = true;
    std::cout << "Economy renderer initialized" << "\n";
    return true;
}

// ============================================================================
// Cleanup
// ============================================================================

void EconomyRenderer::cleanup()
{
    if (shaderProgram_ != 0)
    {
        glDeleteProgram(shaderProgram_);
        shaderProgram_ = 0;
    }

    initialized_ = false;
    shaderAvailable_ = false;
}
