#include "app-state.h"
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <regex>
#include <sstream>

// ==================================
// AppState Singleton Implementation
// ==================================

AppState &AppState::instance()
{
    static AppState instance;
    return instance;
}

AppState::AppState() : m_runningTextureResolution(1), m_hasUnsavedChanges(false), m_loaded(false)
{
    // Initialize WorldState with J2000 epoch defaults
    worldState.julianDate = 2451545.0;         // J2000.0 = January 1, 2000, 12:00 TT
    worldState.timeDilation = 1.0f / 86400.0f; // Real-time: 1 second = 1 second (1/86400 days per second)
    worldState.isPaused = false;               // Not paused by default

    // Initialize camera state with defaults
    worldState.camera.position = glm::vec3(0.0f); // Will be set by camera controller on init
    worldState.camera.yaw = 0.0f;
    worldState.camera.pitch = 0.0f;
    worldState.camera.roll = 0.0f;
    worldState.camera.fov = 60.0f; // Default 60 degrees FOV

    // Initialize UIState with defaults
    // Visualization toggles (all off by default)
    uiState.showOrbits = 0;
    uiState.showRotationAxes = 0;
    uiState.showBarycenters = 0;
    uiState.showLagrangePoints = 0;
    uiState.showCoordinateGrids = 0;
    uiState.showMagneticFields = 0;
    uiState.showGravityGrid = 0;
    uiState.showForceVectors = 0;
    uiState.showSunSpot = 0;
    uiState.showConstellations = 0;
    uiState.showCelestialGrid = 0;
    uiState.showConstellationFigures = 0;
    uiState.showConstellationBounds = 0;
    uiState.showWireframe = 0;
    uiState.showVoxelWireframes = 0;
    uiState.showAtmosphereLayers = 0;

    // Render settings
    uiState.fxaaEnabled = 1;      // FXAA enabled by default
    uiState.vsyncEnabled = 0;     // VSync disabled by default (uncapped FPS)
    uiState.heightmapEnabled = 1; // Heightmap enabled by default
    uiState.normalMapEnabled = 1; // Normal mapping enabled by default
    uiState.roughnessEnabled = 1; // Roughness enabled by default
    uiState.citiesEnabled = 1;    // Cities enabled by default
    uiState.padding1 = 0;
    uiState.padding2 = 0;

    // Gravity grid parameters
    uiState.gravityGridResolution = 25;
    uiState.gravityWarpStrength = 1.0f;

    // Accordion states (settings and controls collapsed, lagrange and moons expanded)
    uiState.settingsExpanded = 0;
    uiState.controlsExpanded = 0;
    uiState.lagrangeExpanded = 1;
    uiState.moonsExpanded = 1;

    // Texture resolution (Medium by default)
    uiState.textureResolution = static_cast<int32_t>(TextureResolutionLevel::Medium);

    // FOV (default 60 degrees)
    uiState.currentFOV = 60.0f;

    // Fullscreen (off by default)
    uiState.isFullscreen = 0;
    uiState.padding3 = 0;
}

bool AppState::loadFromSettings(const std::string &filepath)
{
    m_loaded = true;

    if (!std::filesystem::exists(filepath))
    {
        // Create default settings file
        std::cout << "Creating default settings file: " << filepath << "\n";
        uiState.textureResolution = static_cast<int32_t>(TextureResolutionLevel::Medium);
        m_runningTextureResolution = uiState.textureResolution;
        saveToSettings(filepath);
        return true;
    }

    std::ifstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "Warning: Could not open settings file: " << filepath << "\n";
        return false;
    }

    // Read entire file
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // Parse textureResolution setting
    std::regex resRegex("\"textureResolution\"\\s*:\\s*\"(\\w+)\"");
    std::smatch match;

    if (std::regex_search(content, match, resRegex))
    {
        std::string resName = match[1].str();
        if (resName == "Low" || resName == "low")
        {
            uiState.textureResolution = static_cast<int32_t>(TextureResolutionLevel::Low);
        }
        else if (resName == "High" || resName == "high")
        {
            uiState.textureResolution = static_cast<int32_t>(TextureResolutionLevel::High);
        }
        else if (resName == "Ultra" || resName == "ultra")
        {
            uiState.textureResolution = static_cast<int32_t>(TextureResolutionLevel::Ultra);
        }
        else
        {
            uiState.textureResolution = static_cast<int32_t>(TextureResolutionLevel::Medium);
        }
        std::cout << "Loaded texture resolution setting: "
                  << getResolutionName(static_cast<TextureResolutionLevel>(uiState.textureResolution)) << "\n";
    }

    // Parse fxaaEnabled setting
    std::regex fxaaRegex("\"fxaaEnabled\"\\s*:\\s*(true|false)");
    std::smatch fxaaMatch;

    if (std::regex_search(content, fxaaMatch, fxaaRegex))
    {
        std::string fxaaValue = fxaaMatch[1].str();
        uiState.fxaaEnabled = (fxaaValue == "true") ? 1 : 0;
        std::cout << "Loaded FXAA setting: " << (uiState.fxaaEnabled ? "enabled" : "disabled") << "\n";
    }

    // Parse vsyncEnabled setting
    std::regex vsyncRegex("\"vsyncEnabled\"\\s*:\\s*(true|false)");
    std::smatch vsyncMatch;

    if (std::regex_search(content, vsyncMatch, vsyncRegex))
    {
        std::string vsyncValue = vsyncMatch[1].str();
        uiState.vsyncEnabled = (vsyncValue == "true") ? 1 : 0;
        std::cout << "Loaded VSync setting: " << (uiState.vsyncEnabled ? "enabled" : "disabled") << "\n";
    }

    // Parse FOV setting
    std::regex fovRegex("\"fov\"\\s*:\\s*(\\d+(?:\\.\\d+)?)");
    std::smatch fovMatch;

    if (std::regex_search(content, fovMatch, fovRegex))
    {
        float loadedFov = std::stof(fovMatch[1].str());
        // Clamp to valid range
        if (loadedFov < 5.0f)
        {
            loadedFov = 5.0f;
        }
        if (loadedFov > 120.0f)
        {
            loadedFov = 120.0f;
        }
        // Set FOV in camera state (primary) and UIState (for compatibility)
        worldState.camera.fov = loadedFov;
        uiState.currentFOV = loadedFov;
        std::cout << "Loaded FOV setting: " << worldState.camera.fov << " degrees\n";
    }

    m_runningTextureResolution = uiState.textureResolution;
    m_hasUnsavedChanges = false;

    return true;
}

bool AppState::saveToSettings(const std::string &filepath)
{
    std::ofstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "Error: Could not write settings file: " << filepath << "\n";
        return false;
    }

    // Write JSON5 format
    file << "// Von Neumann Toy Settings\n";
    file << "// This file is auto-generated. Edit with care.\n";
    file << "{\n";
    file << "    // Texture resolution for Earth surface\n";
    file << "    // Options: \"Low\" (1024x512), \"Medium\" (4096x2048), \"High\" (8192x4096), \"Ultra\" "
            "(16384x8192)\n";
    file << "    \"textureResolution\": \""
         << getResolutionName(static_cast<TextureResolutionLevel>(uiState.textureResolution)) << "\",\n";
    file << "    // FXAA antialiasing (Fast Approximate Anti-Aliasing)\n";
    file << "    \"fxaaEnabled\": " << (uiState.fxaaEnabled ? "true" : "false") << ",\n";
    file << "    // VSync (Vertical Synchronization) - caps framerate to display refresh rate\n";
    file << "    \"vsyncEnabled\": " << (uiState.vsyncEnabled ? "true" : "false") << ",\n";
    file << "    // Camera field of view in degrees (5-120)\n";
    file << "    \"fov\": " << worldState.camera.fov << "\n";
    file << "}\n";

    file.close();
    m_hasUnsavedChanges = false;

    std::cout << "Saved settings to: " << filepath << "\n";
    return true;
}

void AppState::markTextureResolutionAsRunning()
{
    m_runningTextureResolution = uiState.textureResolution;
}

bool AppState::needsRestart() const
{
    return uiState.textureResolution != m_runningTextureResolution;
}

bool AppState::hasUnsavedChanges() const
{
    return m_hasUnsavedChanges;
}

const char *AppState::getResolutionName(TextureResolutionLevel res)
{
    switch (res)
    {
    case TextureResolutionLevel::Low:
        return "Low";
    case TextureResolutionLevel::Medium:
        return "Medium";
    case TextureResolutionLevel::High:
        return "High";
    case TextureResolutionLevel::Ultra:
        return "Ultra";
    default:
        return "Medium";
    }
}

void AppState::getResolutionDimensions(TextureResolutionLevel res, int &width, int &height)
{
    switch (res)
    {
    case TextureResolutionLevel::Low:
        width = 1024;
        height = 512;
        break;
    case TextureResolutionLevel::Medium:
        width = 4096;
        height = 2048;
        break;
    case TextureResolutionLevel::High:
        width = 8192;
        height = 4096;
        break;
    case TextureResolutionLevel::Ultra:
        width = 16384;
        height = 8192;
        break;
    default:
        width = 4096;
        height = 2048;
        break;
    }
}

const char *AppState::getResolutionFolderName(TextureResolutionLevel res)
{
    switch (res)
    {
    case TextureResolutionLevel::Low:
        return "low";
    case TextureResolutionLevel::Medium:
        return "medium";
    case TextureResolutionLevel::High:
        return "high";
    case TextureResolutionLevel::Ultra:
        return "ultra";
    default:
        return "medium";
    }
}
