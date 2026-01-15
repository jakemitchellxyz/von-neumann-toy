#pragma once

#include "../../../concerns/settings.h"
#include "../../helpers/gl.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Forward declaration
struct GLFWwindow;

// ============================================================================
// City Data Structure
// ============================================================================
struct CityData
{
    std::string name;
    std::string country;
    double latitude;  // In radians
    double longitude;  // In radians
    float population;  // Population count (may be 0 if not available)
    glm::vec3 position; // 3D position on Earth sphere (computed from lat/lon)
};

// ============================================================================
// Earth Economy System
// ============================================================================
// Handles city data loading, preprocessing, and runtime queries for city
// information on Earth's surface. Used for displaying city names when hovering
// over Earth's surface.

class EarthEconomy
{
public:
    EarthEconomy();
    ~EarthEconomy();

    // Delete copy and move operations
    EarthEconomy(const EarthEconomy &) = delete;
    EarthEconomy &operator=(const EarthEconomy &) = delete;
    EarthEconomy(EarthEconomy &&) = delete;
    EarthEconomy &operator=(EarthEconomy &&) = delete;

    // ==================================
    // Preprocessing (call BEFORE OpenGL init)
    // ==================================

    // Load city data from Excel file and preprocess into texture
    // This runs at application startup BEFORE the window is created.
    // - Parses worldcities.xlsx file
    // - Generates city location texture (sinusoidal projection)
    // - Builds spatial index for fast nearest-neighbor queries
    //
    // xlsxPath: path to worldcities.xlsx file
    // outputBasePath: base path for output texture (e.g., "earth-textures")
    // resolution: which resolution preset to use
    // Returns: true if successful or already cached
    static bool preprocessCities(const std::string &xlsxPath,
                                  const std::string &outputBasePath,
                                  TextureResolution resolution);

    // ==================================
    // Initialization (call AFTER OpenGL init)
    // ==================================

    // Initialize the economy system by loading preprocessed data
    // Call this after OpenGL context is created.
    // combinedBasePath: base path to combined images (e.g., "earth-textures")
    // resolution: which resolution to load
    // Returns true if city data was loaded successfully
    bool initialize(const std::string &combinedBasePath, TextureResolution resolution);

    // Check if the system is ready
    bool isInitialized() const { return initialized_; }

    // ==================================
    // Runtime Queries
    // ==================================

    // Find the nearest city to a given surface position
    // surfacePosition: 3D position on Earth's surface (normalized or scaled)
    // maxDistance: maximum angular distance in radians (default: ~50km)
    // Returns: pointer to nearest city, or nullptr if none found
    const CityData *findNearestCity(const glm::vec3 &surfacePosition, double maxDistance = 0.008) const;

    // Find the N nearest cities to a given surface position
    // surfacePosition: 3D position on Earth's surface
    // count: number of cities to return (default: 5)
    // maxDistance: maximum angular distance in radians
    // Returns: vector of city pointers, sorted by distance (nearest first)
    std::vector<const CityData *> findNearestCities(const glm::vec3 &surfacePosition,
                                                     size_t count = 5,
                                                     double maxDistance = 0.008) const;

    // Get city name for a given surface position (for tooltip display)
    // Returns empty string if no city found
    std::string getCityName(const glm::vec3 &surfacePosition) const;

    // Get all loaded cities (for debugging)
    const std::vector<CityData> &getAllCities() const { return cities_; }

    // Get number of loaded cities
    size_t getCityCount() const { return cities_.size(); }

private:
    // Load city data from Excel file
    static bool loadCityDataFromExcel(const std::string &xlsxPath, std::vector<CityData> &cities);

    // Build spatial index for fast nearest-neighbor queries
    void buildSpatialIndex();

    // Load preprocessed city texture into OpenGL
    GLuint loadCityTexture(const std::string &filepath);

    // Cleanup resources
    void cleanup();

    // ==================================
    // Data Storage
    // ==================================
    std::vector<CityData> cities_; // All loaded cities
    GLuint cityTexture_;            // Preprocessed city location texture
    bool initialized_;               // Whether system is initialized

    // Spatial index for fast queries (simple grid-based)
    struct SpatialCell
    {
        std::vector<size_t> cityIndices; // Indices into cities_ vector
    };
    static constexpr int SPATIAL_GRID_SIZE = 64; // 64x64 grid for lat/lon
    std::vector<std::vector<SpatialCell>> spatialGrid_; // [lat][lon] grid
};

// ==================================
// Global Earth Economy Instance
// ==================================
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern EarthEconomy g_earthEconomy;

// Forward declaration
class EconomyRenderer;
