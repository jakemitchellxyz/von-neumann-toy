#pragma once

#include "settings.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Forward declaration
struct CelestialBody;
struct GLFWwindow;
struct LagrangePoint;

// ==================================
// Lagrange Point Info for UI
// ==================================
struct LagrangePointInfo
{
    std::string label;   // "L1", "L2", etc.
    bool available;      // Whether we have data for this point
    glm::vec3 position;  // Position if available
    float displayRadius; // Display radius for focusing
};

// ==================================
// UI Interaction Results
// ==================================
struct UIInteraction
{
    CelestialBody *clickedBody;       // Body that was single-clicked this frame (nullptr if none)
    CelestialBody *doubleClickedBody; // Body that was double-clicked this frame (nullptr if none)
    CelestialBody *hoveredBody;       // Body currently being hovered in the UI list
    int clickedLagrangeIndex;         // Index of clicked Lagrange point (0-4 for L1-L5, -1 if none)
    CelestialBody *clickedMoon;       // Moon clicked in details panel (nullptr if none) - triggers focus
    CelestialBody
        *focusOnOrbitingBody; // Orbiting body button clicked (Sun for planets, parent for moons) - triggers focus
    bool contextMenuGhostingClicked; // True if "Toggle Trail" was clicked in context menu
    bool contextMenuShouldClose;     // True if context menu should be closed
    bool pauseToggled;               // True if pause/resume button was clicked
    bool orbitsToggled;              // True if orbit visibility was toggled
    bool axesToggled;                // True if rotation axes visibility was toggled
    bool barycentersToggled;         // True if barycenter visibility was toggled
    bool lagrangePointsToggled;      // True if Lagrange points visibility was toggled
    bool coordGridsToggled;          // True if coordinate grids visibility was toggled
    bool magneticFieldsToggled;      // True if magnetic fields visibility was toggled
    bool gravityGridToggled;         // True if gravity grid visibility was toggled
    bool constellationsToggled;      // True if constellations visibility was toggled
    bool constellationGridToggled;   // True if celestial grid visibility was toggled
    bool constellationFiguresToggled; // True if constellation figures visibility was toggled
    bool constellationBoundsToggled;  // True if constellation bounds visibility was toggled
    bool forceVectorsToggled;        // True if force vectors visibility was toggled
    bool sunSpotToggled;             // True if sun spot visibility was toggled
    bool wireframeToggled;           // True if wireframe mode was toggled
    bool fxaaToggled;                 // True if FXAA antialiasing was toggled
    bool vsyncToggled;                // True if VSync was toggled
    bool citiesToggled;              // True if cities visibility was toggled
    bool heightmapToggled;           // True if heightmap effect was toggled
    bool normalMapToggled;           // True if normal map was toggled
    bool roughnessToggled;           // True if roughness/specular effect was toggled
    int newGravityGridResolution;    // New grid resolution (-1 if unchanged)
    float newGravityWarpStrength;    // New warp strength (-1 if unchanged)
    float newFOV;                    // New FOV value from slider (-1 if unchanged)
    bool uiConsumedClick;            // True if the UI handled this click (don't clear focus)
    bool uiSliderDragging;           // True if any UI slider is being dragged (block camera input)
    bool fullscreenToggled;          // True if fullscreen button was clicked
    int newTextureResolution;        // New texture resolution (0-3 for Low/Medium/High/Ultra, -1 if unchanged)
    bool followModeToggled;          // True if follow mode toggle was clicked in context menu
    bool surfaceViewToggled;         // True if surface view toggle was clicked in context menu
    bool uiHideToggled;              // True if hide UI button was clicked
};

// ==================================
// Context Menu Parameters
// ==================================
// Forward declare CameraFollowMode from camera-controller.h
enum class CameraFollowMode;

struct ContextMenuParams
{
    bool isOpen;                 // Whether context menu is showing
    CelestialBody *targetBody;   // Body the menu is for
    double menuX;                // Screen X position
    double menuY;                // Screen Y position
    CameraFollowMode followMode; // Current camera follow mode
    bool isFocusedOnBody;        // True if camera is focused on this body (show follow mode toggle)
    bool isInSurfaceView;        // True if camera is in surface view mode on this body
};

// ==================================
// Time Control Parameters
// ==================================
struct TimeControlParams
{
    double currentJD;                    // Current Julian Date (TDB)
    double minJD;                        // Earliest supported Julian Date
    double maxJD;                        // Latest supported Julian Date
    double *timeDilation;                // Pointer to time dilation factor (days per second, modifiable)
    bool isPaused;                       // Whether time is currently paused
    bool showOrbits;                     // Current orbit visibility state
    bool showRotationAxes;               // Current rotation axes visibility state
    bool showBarycenters;                // Current barycenter visibility state
    bool showLagrangePoints;             // Current Lagrange points visibility state
    bool showCoordinateGrids;            // Current coordinate grids visibility state
    bool showMagneticFields;             // Current magnetic fields visibility state
    bool showGravityGrid;                // Current gravity grid visibility state
    bool showConstellations;             // Current constellation visibility state
    bool showForceVectors;               // Current force vectors visibility state
    bool showSunSpot;                    // Current sun spot visibility state
    bool showWireframe;                  // Current wireframe mode state
    bool fxaaEnabled;                    // Current FXAA antialiasing state
    bool vsyncEnabled;                   // Current VSync state
    int gravityGridResolution;           // Current grid resolution (lines per axis)
    float gravityWarpStrength;           // Current warp strength multiplier
    float currentFOV;                    // Current camera field of view in degrees
    bool isFullscreen;                   // Current fullscreen state
    TextureResolution textureResolution; // Current texture resolution setting

    // Surface view state
    bool isInSurfaceView;        // True if camera is in surface view mode
    float surfaceLatitude;       // Current latitude in degrees (when in surface view)
    float surfaceLongitude;      // Current longitude in degrees (when in surface view)
    std::string surfaceBodyName; // Name of body we're on (when in surface view)
};

// ==================================
// Tooltip Parameters
// ==================================
struct TooltipParams
{
    bool show;        // Whether to show the tooltip
    std::string text; // Text to display
    double mouseX;    // Mouse X position
    double mouseY;    // Mouse Y position
};

// ==================================
// Measurement Mode
// ==================================
enum class MeasurementMode
{
    None,              // No measurement active
    LongitudeLatitude, // Measuring lat/lon
    AltitudeDepth,     // Measuring altitude/depth
    ColorPicker        // Color picker tool
};

// ==================================
// Measurement Result
// ==================================
struct MeasurementResult
{
    bool hasHit;            // Whether mouse ray hit a celestial body
    glm::vec3 hitPoint;     // 3D position of hit point
    CelestialBody *hitBody; // Body that was hit (nullptr if none)
    double latitude;        // Latitude in radians (if body has coordinate system)
    double longitude;       // Longitude in radians (if body has coordinate system)
    float elevation;        // Elevation in meters (if body has heightmap)
    // Color picker fields
    bool hasColor;          // Whether color was successfully read
    float colorR;           // Red component (0.0-1.0)
    float colorG;           // Green component (0.0-1.0)
    float colorB;           // Blue component (0.0-1.0)
    int colorRInt;          // Red component (0-255)
    int colorGInt;          // Green component (0-255)
    int colorBInt;          // Blue component (0-255)
};

// ==================================
// Moon Info for UI
// ==================================
struct MoonInfo
{
    CelestialBody *body; // Pointer to the moon's CelestialBody
    std::string name;    // Moon name for display
};

// ==================================
// Selected Body Display Parameters
// ==================================
struct SelectedBodyParams
{
    const CelestialBody *body;  // Currently selected body (nullptr if none)
    float axialTiltDegrees;     // Actual axial tilt from SPICE/fallback in degrees
    double orbitalVelocityKmS;  // Orbital velocity in km/s
    double rotationPeriodHours; // Rotation period in hours

    // Lagrange points (for planets only)
    bool isPlanet;                       // True if this is a planet with Lagrange points
    std::string lagrangeSystemName;      // e.g., "Sun-Earth"
    LagrangePointInfo lagrangePoints[5]; // L1-L5

    // Moons (for planets with moons)
    std::vector<MoonInfo> moons; // List of moons orbiting this planet
};

// ==================================
// 2D UI Overlay Rendering
// ==================================
// Renders 2D UI elements on top of the 3D scene

// Initialize the UI system (call once at startup)
void InitUI();

// Main UI drawing function - draws the complete user interface
// screenWidth/screenHeight: current window dimensions
// fps: current frames per second
// triangleCount: current number of triangles rendered (after culling)
// bodies: array of all celestial bodies to list
// timeParams: time control parameters (current date, range, dilation)
// mouseX/mouseY: current mouse position
// window: GLFW window for cursor changes
// tooltip: optional tooltip to display (for 3D hover)
// selectedBody: optional selected body info for details panel
// contextMenu: optional context menu to display (for right-click)
// Returns interaction results (clicked/hovered bodies)
UIInteraction DrawUserInterface(int screenWidth,
                                int screenHeight,
                                int fps,
                                int triangleCount,
                                const std::vector<CelestialBody *> &bodies,
                                const TimeControlParams &timeParams,
                                double mouseX,
                                double mouseY,
                                GLFWwindow *window,
                                const TooltipParams *tooltip = nullptr,
                                const SelectedBodyParams *selectedBody = nullptr,
                                const ContextMenuParams *contextMenu = nullptr);

// ==================================
// FPS Calculation Helper
// ==================================

// Update FPS calculation - call once per frame
// Returns the current measured FPS
int UpdateFPS();

// ==================================
// Triangle Count Helper
// ==================================

// Start triangle counting query - call before 3D rendering
void StartTriangleCountQuery();

// End triangle counting query - call after 3D rendering (before UI)
void EndTriangleCountQuery();

// Update triangle count calculation - call once per frame after EndTriangleCountQuery
// Returns the current triangle count
int UpdateTriangleCount();

// Helper function to count triangles from a draw call
// Call this from draw functions to manually count triangles
// primitiveType: GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_QUAD_STRIP, etc.
// vertexCount: number of vertices in the primitive
void CountTriangles(GLenum primitiveType, int vertexCount);

// Check if mouse is over any UI element (panels, buttons, etc.)
// Returns true if mouse is over UI, false otherwise
bool IsMouseOverUI(int screenWidth, int screenHeight, double mouseX, double mouseY, bool uiVisible);

// Get current UI visibility state
bool IsUIVisible();

// ==================================
// Measurement Functions
// ==================================

// Get current measurement mode
MeasurementMode GetMeasurementMode();

// Set measurement mode (called from UI interactions)
void SetMeasurementMode(MeasurementMode mode);

// Get current measurement result (for rendering measurement sphere and tooltip)
// This should be called after DrawUserInterface to get the latest measurement data
// Note: Returns const reference - use const_cast if you need to modify (e.g., for color picker)
const MeasurementResult &GetMeasurementResult();

// Update measurement result based on raycast
// This should be called from entrypoint.cpp after raycasting
// cameraPos: camera position in world space
// rayDir: normalized ray direction from camera through mouse
// bodies: list of all celestial bodies
// maxRayDistance: maximum ray distance to check
void UpdateMeasurementResult(const glm::vec3 &cameraPos,
                             const glm::vec3 &rayDir,
                             const std::vector<CelestialBody *> &bodies,
                             float maxRayDistance);

// ==================================
// Lower-level Drawing Functions
// ==================================

// Begin 2D UI rendering mode
void BeginUI(int screenWidth, int screenHeight);

// End 2D UI rendering mode
void EndUI();

// Draw a rounded rectangle
void DrawRoundedRect(float x, float y, float width, float height, float radius, float r, float g, float b, float a);

// Font rendering functions (DrawText, GetTextWidth, DrawNumber) are now in font-rendering.h
