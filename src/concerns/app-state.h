#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

// ==================================
// CameraState - CPU-side Camera State
// ==================================
// Contains camera position, orientation, and field of view
// The camera controller modifies position/orientation, UI controls FOV
struct CameraState
{
    glm::vec3 position = glm::vec3(0.0f); // Camera position in world space
    float yaw = 0.0f;                     // Horizontal angle in degrees
    float pitch = 0.0f;                   // Vertical angle in degrees
    float roll = 0.0f;                    // Roll angle in degrees
    float fov = 60.0f;                    // Field of view in degrees

    // Get camera direction vectors
    glm::vec3 getFront() const
    {
        glm::vec3 front;
        front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        return glm::normalize(front);
    }

    glm::vec3 getRight() const
    {
        glm::vec3 front = getFront();
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(front, worldUp));

        // Apply roll rotation around the forward axis
        if (std::abs(roll) > 0.001f)
        {
            float rollRad = glm::radians(roll);
            float cosR = cos(rollRad);
            float sinR = sin(rollRad);
            right = right * cosR + glm::cross(front, right) * sinR + front * glm::dot(front, right) * (1.0f - cosR);
            right = glm::normalize(right);
        }

        return right;
    }

    glm::vec3 getUp() const
    {
        glm::vec3 front = getFront();
        glm::vec3 right = getRight();
        glm::vec3 up = glm::normalize(glm::cross(right, front));

        // Apply roll rotation around the forward axis
        if (std::abs(roll) > 0.001f)
        {
            float rollRad = glm::radians(roll);
            float cosR = cos(rollRad);
            float sinR = sin(rollRad);
            up = up * cosR + glm::cross(front, up) * sinR + front * glm::dot(front, up) * (1.0f - cosR);
            up = glm::normalize(up);
        }

        return up;
    }

    // Get view matrix for rendering
    glm::mat4 getViewMatrix() const
    {
        glm::vec3 front = getFront();
        glm::vec3 target = position + front;
        glm::vec3 up = getUp();
        return glm::lookAt(position, target, up);
    }

    // Get projection matrix for rendering
    glm::mat4 getProjectionMatrix(float aspectRatio, float nearPlane, float farPlane) const
    {
        return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    }
};

// ==================================
// CameraPushConstants - GPU Push Constants for Camera (144 bytes)
// ==================================
// This struct contains camera data passed to shaders as push constants
// Includes view/projection matrices and camera position
struct CameraPushConstants
{
    glm::mat4 viewMatrix;       // 64 bytes - camera view matrix
    glm::mat4 projectionMatrix; // 64 bytes - camera projection matrix
    glm::vec3 cameraPosition;   // 12 bytes - camera world position
    float fov;                  // 4 bytes - field of view in degrees
};

// ==================================
// WorldPushConstants - GPU Push Constants (16 bytes)
// ==================================
// This struct contains ONLY the fields passed to shaders as push constants
// Keep this small - push constants have limited size (128-256 bytes typically)
struct WorldPushConstants
{
    double julianDate;  // 8 bytes - current simulation Julian Date (TDB)
    float timeDilation; // 4 bytes - time speed modifier (days per second)
    float padding;      // 4 bytes - alignment padding
};

// ==================================
// CelestialObject - GPU-compatible Celestial Body Data
// ==================================
// Packed struct for celestial objects sent to GPU via SSBO
// Uses std430 layout: vec4 aligned to 16 bytes
// Each object represents a sphere in world space with rotation data from SPICE
struct CelestialObject
{
    glm::vec3 position; // Position in display units (AU * UNITS_PER_AU)
    float radius;       // Radius in display units

    glm::vec3 color; // RGB color for rendering
    int32_t naifId;  // NAIF SPICE ID for identification

    glm::vec3 poleDirection; // North pole direction from SPICE (J2000 coords, Z-up)
    float _padding1;         // Alignment padding

    glm::vec3 primeMeridianDirection; // Prime meridian direction from SPICE (J2000 coords)
    float _padding2;                  // Alignment padding

    // Default constructor - J2000 coords: Z-up, X toward vernal equinox
    CelestialObject()
        : position(0.0f), radius(1.0f), color(1.0f), naifId(0), poleDirection(0.0f, 0.0f, 1.0f), _padding1(0.0f),
          primeMeridianDirection(1.0f, 0.0f, 0.0f), _padding2(0.0f)
    {
    }

    // Constructor with parameters - J2000 coords: Z-up, X toward vernal equinox
    CelestialObject(glm::vec3 pos, float rad, glm::vec3 col, int32_t id)
        : position(pos), radius(rad), color(col), naifId(id), poleDirection(0.0f, 0.0f, 1.0f), _padding1(0.0f),
          primeMeridianDirection(1.0f, 0.0f, 0.0f), _padding2(0.0f)
    {
    }
};

// Maximum number of celestial objects in SSBO
// Includes Sun, 9 planets, and major moons
constexpr uint32_t MAX_CELESTIAL_OBJECTS = 32;

// ==================================
// WorldState - CPU-side World State
// ==================================
// This struct contains all world/simulation state on the CPU
// Only a subset (WorldPushConstants) is sent to shaders
struct WorldState
{
    // Fields that ARE sent to shaders as push constants
    double julianDate;  // Current simulation Julian Date (TDB)
    float timeDilation; // Time speed modifier (days per second)

    // Fields that are NOT sent to shaders (CPU-only)
    // Add additional CPU-side fields here as needed
    bool isPaused; // Whether simulation is paused

    // Camera movement control
    // maxCameraStep: Maximum distance the camera can move per scroll tick
    // This is dynamically adjusted based on distance to terrain surfaces
    // Starts at a large value and shrinks as camera approaches surfaces
    float maxCameraStep = 1.0f;

    // Minimum surface distance from last frame (read back from GPU)
    // Used to calculate maxCameraStep for terrain collision avoidance
    float minSurfaceDistance = 1000.0f;

    // Base scroll speed multiplier (user-adjustable)
    float scrollSpeedMultiplier = 0.1f;

    // Camera state - position, orientation, and FOV
    // Camera controller modifies position/orientation, UI controls FOV
    CameraState camera;

    // Celestial objects (planets, moons, sun) for GPU rendering
    // Updated each frame based on Julian date from SPICE ephemeris
    std::vector<CelestialObject> celestialObjects;

    // Track whether celestial objects have been initialized
    bool celestialObjectsInitialized = false;

    // Convert to GPU push constants struct
    WorldPushConstants toPushConstants() const
    {
        WorldPushConstants pc{};
        pc.julianDate = julianDate;
        pc.timeDilation = timeDilation;
        pc.padding = 0.0f;
        return pc;
    }

    // Convert camera state to GPU push constants
    // aspectRatio, nearPlane, farPlane needed for projection matrix
    CameraPushConstants toCameraPushConstants(float aspectRatio, float nearPlane, float farPlane) const
    {
        CameraPushConstants pc{};
        pc.viewMatrix = camera.getViewMatrix();
        pc.projectionMatrix = camera.getProjectionMatrix(aspectRatio, nearPlane, farPlane);
        pc.cameraPosition = camera.position;
        pc.fov = camera.fov;
        return pc;
    }
};

// ==================================
// UIState - SSBO (GPU-aligned struct)
// ==================================
// This struct is passed to shaders as an SSBO
// Contains all UI toggles and visualization settings
// Uses uint32_t for booleans (GLSL bool in SSBO is 4 bytes)
struct UIState
{
    // Visualization toggles (16 flags = 64 bytes)
    uint32_t showOrbits;               // Show/hide orbital paths
    uint32_t showRotationAxes;         // Show/hide rotation axes and equators
    uint32_t showBarycenters;          // Show/hide barycenter markers
    uint32_t showLagrangePoints;       // Show/hide Lagrange points
    uint32_t showCoordinateGrids;      // Show/hide planet coordinate grids
    uint32_t showMagneticFields;       // Show/hide magnetic field lines
    uint32_t showGravityGrid;          // Show/hide gravity spacetime grid
    uint32_t showForceVectors;         // Show/hide gravity and momentum force vectors
    uint32_t showSunSpot;              // Show/hide sun spot visualization
    uint32_t showConstellations;       // Show/hide constellation stars
    uint32_t showCelestialGrid;        // Show/hide celestial grid overlay
    uint32_t showConstellationFigures; // Show/hide constellation figure lines
    uint32_t showConstellationBounds;  // Show/hide constellation boundary lines
    uint32_t showWireframe;            // Show/hide wireframe mode
    uint32_t showVoxelWireframes;      // Show/hide voxel wireframes
    uint32_t showAtmosphereLayers;     // Show/hide atmosphere layers

    // Render settings (8 flags = 32 bytes)
    uint32_t fxaaEnabled;      // FXAA antialiasing
    uint32_t vsyncEnabled;     // VSync
    uint32_t heightmapEnabled; // Heightmap effect
    uint32_t normalMapEnabled; // Normal mapping
    uint32_t roughnessEnabled; // Roughness/specular
    uint32_t citiesEnabled;    // Cities visualization
    uint32_t padding1;         // Alignment padding
    uint32_t padding2;         // Alignment padding

    // Gravity grid parameters (8 bytes)
    int32_t gravityGridResolution; // Grid lines per axis (10-50)
    float gravityWarpStrength;     // Warp strength multiplier (0.1-5.0)

    // Accordion states (4 flags = 16 bytes)
    uint32_t settingsExpanded; // Settings accordion expanded
    uint32_t controlsExpanded; // Visualizations accordion expanded
    uint32_t lagrangeExpanded; // Lagrange points accordion expanded
    uint32_t moonsExpanded;    // Moons accordion expanded

    // Texture resolution (4 bytes)
    int32_t textureResolution; // 0=Low, 1=Medium, 2=High, 3=Ultra

    // Current FOV (4 bytes)
    float currentFOV; // Camera field of view in degrees

    // Fullscreen state (4 bytes)
    uint32_t isFullscreen; // Whether window is fullscreen

    // Padding to align to 16 bytes (4 bytes)
    uint32_t padding3;
};

// ==================================
// HoverState - CPU-side Hover State (not sent to GPU)
// ==================================
// Tracks which body is currently being hovered and selected
// Used for tooltip display and camera follow
struct HoverState
{
    int32_t hoveredNaifId = 0;       // NAIF ID of body mouse is over (0 = none)
    std::string hoveredBodyName;     // Name of hovered body for tooltip
    int32_t selectedNaifId = 0;      // NAIF ID of selected body (0 = none)
    std::string selectedBodyName;    // Name of selected body
    float selectedBodyRadius = 1.0f; // Radius of selected body (for movement scaling)
    bool followingSelected = false;  // True if camera is following selected body
    float followDistance = 3.0f;     // Distance from body in radii
    glm::vec3 cameraOffset{0.0f};    // Offset from body center to camera (used for orbit)
};

// Texture resolution enum values (matching existing TextureResolution enum)
enum class TextureResolutionLevel : int32_t
{
    Low = 0,
    Medium = 1,
    High = 2,
    Ultra = 3
};

// ==================================
// AppState Singleton
// ==================================
// Central application state manager
// Owns WorldState and UIState
class AppState
{
public:
    // Get singleton instance
    static AppState &instance();

    // Delete copy/move constructors and assignment operators
    AppState(const AppState &) = delete;
    AppState &operator=(const AppState &) = delete;
    AppState(AppState &&) = delete;
    AppState &operator=(AppState &&) = delete;

    // State data (publicly accessible for direct modification)
    WorldState worldState;
    UIState uiState;
    HoverState hoverState;

    // Load state from settings file
    bool loadFromSettings(const std::string &filepath = "settings.json5");

    // Save state to settings file
    bool saveToSettings(const std::string &filepath = "settings.json5");

    // Mark current texture resolution as running (for restart detection)
    void markTextureResolutionAsRunning();

    // Check if texture resolution changed since startup (needs restart)
    bool needsRestart() const;

    // Flag indicating unsaved changes
    bool hasUnsavedChanges() const;

    // Helper to get resolution name string
    static const char *getResolutionName(TextureResolutionLevel res);

    // Helper to get resolution dimensions
    static void getResolutionDimensions(TextureResolutionLevel res, int &width, int &height);

    // Helper to get resolution folder name
    static const char *getResolutionFolderName(TextureResolutionLevel res);

private:
    // Private constructor for singleton
    AppState();

    // Running texture resolution (for restart detection)
    int32_t m_runningTextureResolution;

    // Unsaved changes flag
    bool m_hasUnsavedChanges;

    // Whether state has been loaded
    bool m_loaded;
};

// Convenience macro for accessing AppState
#define APP_STATE AppState::instance()
