#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>

// Import modules
#include "concerns/app-state.h"
#include "concerns/constants.h"
#include "concerns/input-controller.h"
#include "concerns/preprocess-data.h"
#include "concerns/screen-renderer.h"
#include "concerns/settings.h" // Keep for TextureResolution enum (temporary compatibility)
#include "concerns/spice-ephemeris.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - global flag to indicate shutdown request
bool g_shouldShutdown = false;

// Get the path to the defaults directory
// Checks next to the executable first, then falls back to current directory
std::string getDefaultsPath()
{
    fs::path exePath;

#ifdef _WIN32
    char exePathBuf[1024];
    DWORD result = GetModuleFileNameA(nullptr, exePathBuf, sizeof(exePathBuf));
    if (result != 0 && result < sizeof(exePathBuf))
    {
        exePath = fs::path(exePathBuf).parent_path();
    }
#else
    char exePathBuf[1024];
    ssize_t len = readlink("/proc/self/exe", exePathBuf, sizeof(exePathBuf) - 1);
    if (len != -1 && len < static_cast<ssize_t>(sizeof(exePathBuf)))
    {
        exePathBuf[len] = '\0';
        exePath = fs::path(exePathBuf).parent_path();
    }
#endif

    // Check if defaults exists next to executable
    if (!exePath.empty())
    {
        fs::path defaultsPath = exePath / "defaults";
        if (fs::exists(defaultsPath) && fs::is_directory(defaultsPath))
        {
            return defaultsPath.string();
        }
    }

    // Fall back to "defaults" in current directory
    fs::path defaultsPath = fs::current_path() / "defaults";
    if (fs::exists(defaultsPath) && fs::is_directory(defaultsPath))
    {
        return defaultsPath.string();
    }

    // Last resort: return "defaults" relative to current directory
    return "defaults";
}

// Convert UTC calendar date/time to Julian Date
// Uses the algorithm from the Astronomical Almanac
double CalendarToJulianDate(int year, int month, int day, int hour, int minute, int second)
{
    // Algorithm for Julian Day Number (integer part)
    // Valid for dates after November 23, -4713 (proleptic Gregorian calendar)
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;

    // Julian Day Number at noon
    int jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;

    // Convert to Julian Date with fractional day
    // JDN is at noon (12:00), so subtract 12 hours from the time
    double fractionOfDay = (static_cast<double>(hour) - 12.0) / 24.0 + static_cast<double>(minute) / 1440.0 +
                           static_cast<double>(second) / 86400.0;

    return static_cast<double>(jdn) + fractionOfDay;
}

// Get current system time as Julian Date (UTC)
double GetCurrentJulianDate()
{
    // Get current time
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    // Convert to UTC (gmtime)
    std::tm *utcTime = std::gmtime(&nowTime);

    // tm_year is years since 1900, tm_mon is 0-11
    int year = utcTime->tm_year + 1900;
    int month = utcTime->tm_mon + 1;
    int day = utcTime->tm_mday;
    int hour = utcTime->tm_hour;
    int minute = utcTime->tm_min;
    int second = utcTime->tm_sec;

    return CalendarToJulianDate(year, month, day, hour, minute, second);
}

// ==================================
// Celestial Objects Initialization
// ==================================

// Get a color for a celestial body based on its NAIF ID
// Returns a reasonable approximation color for known bodies
glm::vec3 GetBodyColor(int naifId)
{
    switch (naifId)
    {
    case SpiceEphemeris::NAIF_SUN:
        return {1.0f, 0.95f, 0.8f}; // Yellowish white
    case SpiceEphemeris::NAIF_MERCURY:
        return {0.6f, 0.6f, 0.6f}; // Gray
    case SpiceEphemeris::NAIF_VENUS:
        return {0.9f, 0.8f, 0.5f}; // Yellowish
    case SpiceEphemeris::NAIF_EARTH:
        return {0.2f, 0.5f, 1.0f}; // Blue
    case SpiceEphemeris::NAIF_MOON:
        return {0.7f, 0.7f, 0.7f}; // Gray
    case SpiceEphemeris::NAIF_MARS:
        return {0.9f, 0.4f, 0.2f}; // Reddish
    case SpiceEphemeris::NAIF_JUPITER:
        return {0.8f, 0.7f, 0.5f}; // Tan/brown
    case SpiceEphemeris::NAIF_SATURN:
        return {0.9f, 0.8f, 0.6f}; // Pale yellow
    case SpiceEphemeris::NAIF_URANUS:
        return {0.6f, 0.8f, 0.9f}; // Cyan
    case SpiceEphemeris::NAIF_NEPTUNE:
        return {0.3f, 0.4f, 0.9f}; // Blue
    case SpiceEphemeris::NAIF_PLUTO:
        return {0.7f, 0.6f, 0.5f}; // Tan
    default:
        return {0.8f, 0.8f, 0.8f}; // Default gray for unknown bodies
    }
}

// Get fallback radius in km for known bodies when PCK data isn't available
double GetFallbackRadiusKm(int naifId)
{
    switch (naifId)
    {
    case SpiceEphemeris::NAIF_SUN:
        return RADIUS_SUN_KM;
    case SpiceEphemeris::NAIF_MERCURY:
        return RADIUS_MERCURY_KM;
    case SpiceEphemeris::NAIF_VENUS:
        return RADIUS_VENUS_KM;
    case SpiceEphemeris::NAIF_EARTH:
        return RADIUS_EARTH_KM;
    case SpiceEphemeris::NAIF_MOON:
        return RADIUS_MOON_KM;
    case SpiceEphemeris::NAIF_MARS:
        return RADIUS_MARS_KM;
    case SpiceEphemeris::NAIF_JUPITER:
        return RADIUS_JUPITER_KM;
    case SpiceEphemeris::NAIF_SATURN:
        return RADIUS_SATURN_KM;
    case SpiceEphemeris::NAIF_URANUS:
        return RADIUS_URANUS_KM;
    case SpiceEphemeris::NAIF_NEPTUNE:
        return RADIUS_NEPTUNE_KM;
    case SpiceEphemeris::NAIF_PLUTO:
        return RADIUS_PLUTO_KM;
    case SpiceEphemeris::NAIF_IO:
        return RADIUS_IO_KM;
    case SpiceEphemeris::NAIF_EUROPA:
        return RADIUS_EUROPA_KM;
    case SpiceEphemeris::NAIF_GANYMEDE:
        return RADIUS_GANYMEDE_KM;
    case SpiceEphemeris::NAIF_CALLISTO:
        return RADIUS_CALLISTO_KM;
    case SpiceEphemeris::NAIF_TITAN:
        return RADIUS_TITAN_KM;
    case SpiceEphemeris::NAIF_TRITON:
        return RADIUS_TRITON_KM;
    case SpiceEphemeris::NAIF_CHARON:
        return RADIUS_CHARON_KM;
    default:
        return 500.0; // Default for unknown bodies
    }
}

// Initialize celestial objects from SPICE ephemeris - uses all discovered bodies
void InitializeCelestialObjects()
{
    auto &objects = APP_STATE.worldState.celestialObjects;
    objects.clear();

    // Get all bodies discovered in the loaded SPICE kernels
    auto availableBodies = SpiceEphemeris::getAvailableBodies();

    for (const auto &body : availableBodies)
    {
        CelestialObject obj;
        obj.naifId = body.naifId;
        obj.color = GetBodyColor(body.naifId);
        obj.position = glm::vec3(0.0f); // Will be updated by UpdateCelestialObjectPositions

        // Use radius from SPICE PCK if available, otherwise use our constants
        double radiusKm = body.radiusKm;
        if (radiusKm <= 0.0)
        {
            radiusKm = GetFallbackRadiusKm(body.naifId);
        }
        obj.radius = getDisplayRadius(radiusKm);

        std::cout << "  " << body.name << " (NAIF " << body.naifId << "): radius=" << radiusKm
                  << " km -> display=" << obj.radius << "\n";

        objects.push_back(obj);
    }

    std::cout << "Initialized " << objects.size() << " celestial objects from SPICE kernels\n";
    APP_STATE.worldState.celestialObjectsInitialized = true;
}

// Update celestial object positions and rotations based on Julian date
void UpdateCelestialObjectPositions(double julianDate)
{
    static bool firstUpdate = true;

    for (auto &obj : APP_STATE.worldState.celestialObjects)
    {
        // Get position from SPICE (returns AU relative to SSB)
        glm::dvec3 posAU = SpiceEphemeris::getBodyPosition(obj.naifId, julianDate);

        // Convert AU to display units
        obj.position = glm::vec3(posAU) * UNITS_PER_AU;

        // Get body frame (pole and prime meridian) from SPICE
        // Keep in J2000 coordinates (Z-up) to match positions
        glm::dvec3 pole, primeMeridian;
        if (SpiceEphemeris::getBodyFrame(obj.naifId, julianDate, pole, primeMeridian))
        {
            // Use SPICE data directly in J2000 coordinates (no transformation)
            // Positions are also in J2000, so surface normals will match
            obj.poleDirection = glm::normalize(glm::vec3(pole));
            obj.primeMeridianDirection = glm::normalize(glm::vec3(primeMeridian));
        }
        else
        {
            // Fallback: default orientation in J2000 (Z-up, X toward vernal equinox)
            obj.poleDirection = glm::vec3(0.0f, 0.0f, 1.0f);
            obj.primeMeridianDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        }

        if (firstUpdate)
        {
            std::cout << "NAIF " << obj.naifId << ": pos=(" << obj.position.x << ", " << obj.position.y << ", "
                      << obj.position.z << ") AU=(" << posAU.x << ", " << posAU.y << ", " << posAU.z << ")\n";
        }
    }

    if (firstUpdate)
    {
        firstUpdate = false;
        std::cout << "\n";
    }
}

// Get Earth's position in display units for a given Julian date
glm::vec3 GetEarthPosition(double julianDate)
{
    glm::dvec3 posAU = SpiceEphemeris::getBodyPosition(SpiceEphemeris::NAIF_EARTH, julianDate);
    return glm::vec3(posAU) * UNITS_PER_AU;
}

// Get Earth's display radius
float GetEarthDisplayRadius()
{
    return getDisplayRadius(RADIUS_EARTH_KM);
}

// Initialize camera to look at Earth from a comfortable viewing distance
void InitializeCameraForEarth(double julianDate)
{
    auto &camera = APP_STATE.worldState.camera;

    // Get Earth's position
    glm::vec3 earthPos = GetEarthPosition(julianDate);
    float earthRadius = GetEarthDisplayRadius();

    // Position camera far enough to see Earth clearly
    // 10 Earth radii gives a good view
    float cameraDistance = earthRadius * 10.0f;

    // Camera position: offset from Earth along +X axis (looking back toward Sun)
    // This means Earth is between camera and Sun, showing the lit side
    camera.position = earthPos + glm::vec3(cameraDistance, earthRadius * 2.0f, cameraDistance * 0.5f);

    // Calculate direction to Earth
    glm::vec3 toEarth = glm::normalize(earthPos - camera.position);

    // Yaw: angle in XZ plane from +X axis
    // Pitch: angle from XZ plane (elevation)
    camera.yaw = glm::degrees(atan2(toEarth.z, toEarth.x));
    camera.pitch = glm::degrees(asin(glm::clamp(toEarth.y, -1.0f, 1.0f)));
    camera.roll = 0.0f;

    std::cout << "\n=== Camera Initialization ===\n";
    std::cout << "Earth position: (" << earthPos.x << ", " << earthPos.y << ", " << earthPos.z << ")\n";
    std::cout << "Earth radius (display): " << earthRadius << "\n";
    std::cout << "Camera position: (" << camera.position.x << ", " << camera.position.y << ", " << camera.position.z
              << ")\n";
    std::cout << "Distance to Earth: " << glm::length(earthPos - camera.position) << "\n";
    std::cout << "Camera yaw: " << camera.yaw << ", pitch: " << camera.pitch << "\n";
    std::cout << "=============================\n\n";
}

} // namespace

#ifdef _WIN32
// Windows console control handler
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT)
    {
        g_shouldShutdown = true;
        return TRUE; // Signal handled
    }
    return FALSE; // Let default handler process other events
}
#else
// Unix signal handler
void SignalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM)
    {
        g_shouldShutdown = true;
    }
}
#endif

// ============================================================================
// Cleanup All Subsystems
// ============================================================================

void CleanupApplication(ScreenRendererState &screenState)
{
    // Cleanup Screen Renderer (handles Vulkan and OpenGL cleanup)
    CleanupScreenRenderer(screenState);

    // Cleanup SPICE ephemeris
    SpiceEphemeris::cleanup();

    // Restore default signal handlers
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
#else
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
#endif
}

// ============================================================================
// Main Program
// ============================================================================

int main()
{
    // Set up signal handler for graceful shutdown on Ctrl+C
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
#endif
    // ========================================================================
    // Load Application Settings (using AppState singleton)
    // ========================================================================
    APP_STATE.loadFromSettings("settings.json5");
    APP_STATE.markTextureResolutionAsRunning();

    // Convert AppState texture resolution to legacy TextureResolution enum for compatibility
    TextureResolution textureRes = static_cast<TextureResolution>(APP_STATE.uiState.textureResolution);
    std::cout << "Texture resolution: " << getResolutionName(textureRes) << "\n";

    // ========================================================================
    // Preprocess all application data (includes SPICE ephemeris initialization)
    // ========================================================================
    if (!PreprocessAllData(textureRes))
    {
        std::cerr << "Failed to preprocess application data!" << "\n";
        return -1;
    }

    // ========================================================================
    // Initialize Screen Renderer (handles GLFW, Vulkan, and OpenGL setup)
    // Also loads the skybox cubemap texture for ray-miss background
    // ========================================================================
    ScreenRendererState screenState;
    if (!InitScreenRenderer(screenState, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "Von Neumann Toy", textureRes))
    {
        std::cerr << "Failed to initialize screen renderer!" << "\n";
        return -1;
    }

    // Initialize simulation time to current real-world time (UTC as Julian Date)
    APP_STATE.worldState.julianDate = GetCurrentJulianDate();

    // ========================================================================
    // Initialize Celestial Objects
    // ========================================================================
    InitializeCelestialObjects();

    // Update celestial object positions for initial Julian date
    UpdateCelestialObjectPositions(APP_STATE.worldState.julianDate);

    // Initialize camera to look at Earth from 3 Earth radii away
    InitializeCameraForEarth(APP_STATE.worldState.julianDate);

    // Initialize time tracking for frame-rate independent simulation
    auto lastFrameTime = std::chrono::high_resolution_clock::now();

    // Mouse drag state for camera control
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    bool wasMouseDown = false;
    constexpr float ROTATE_SPEED = 0.15f;
    constexpr float ORBIT_SPEED = 0.005f;

    // Main loop - render scene with UI overlay
    while (!g_shouldShutdown && !ShouldClose(screenState))
    {
        // Calculate delta time for frame-rate independent updates
        auto currentFrameTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> deltaSeconds = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;
        double deltaTime = deltaSeconds.count();

        // Clamp delta time to prevent huge jumps (e.g., after window minimization or debugging)
        constexpr double MAX_DELTA_TIME = 0.25; // 250ms max (4 FPS minimum)
        if (deltaTime > MAX_DELTA_TIME)
        {
            deltaTime = MAX_DELTA_TIME;
        }

        // Update simulation time (Julian date) if not paused
        // timeDilation is in days per second, so multiply by delta time (seconds)
        if (!APP_STATE.worldState.isPaused)
        {
            APP_STATE.worldState.julianDate += deltaTime * static_cast<double>(APP_STATE.worldState.timeDilation);
        }

        // Update celestial object positions for current Julian date
        UpdateCelestialObjectPositions(APP_STATE.worldState.julianDate);

        // Poll events first - beginFrame clears state, then callbacks set new input values
        PollEvents(screenState);

        // ====================================
        // Handle hover and selection
        // ====================================

        // Get hovered body (from GPU shader via SSBO)
        uint32_t hoveredNaifId = GetHoveredNaifId(screenState);

        // Update hover state in AppState (for UI tooltip rendering)
        APP_STATE.hoverState.hoveredNaifId = static_cast<int32_t>(hoveredNaifId);
        APP_STATE.hoverState.hoveredBodyName =
            hoveredNaifId > 0 ? SpiceEphemeris::getBodyName(static_cast<int>(hoveredNaifId)) : "";

        // Get current mouse state
        const InputState &inputState = g_input.getState();
        double currentMouseX = inputState.mouseX;
        double currentMouseY = inputState.mouseY;
        bool isMouseDown = inputState.mouseButtonDown[0]; // Left mouse button
        bool altKeyPressed = (glfwGetKey(screenState.window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                              glfwGetKey(screenState.window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);

        // Handle click-to-select (ignore if clicking on already-selected body)
        if (g_input.wasMouseButtonPressed(MouseButton::Left) && hoveredNaifId > 0 &&
            static_cast<int32_t>(hoveredNaifId) != APP_STATE.hoverState.selectedNaifId)
        {
            // Select the hovered body (different from current selection)
            APP_STATE.hoverState.selectedNaifId = static_cast<int32_t>(hoveredNaifId);
            APP_STATE.hoverState.selectedBodyName = APP_STATE.hoverState.hoveredBodyName;
            APP_STATE.hoverState.followingSelected = true;

            // Find the selected body and position camera
            for (const auto &obj : APP_STATE.worldState.celestialObjects)
            {
                if (obj.naifId == APP_STATE.hoverState.selectedNaifId)
                {
                    // Position camera between body and sun, 3 radii away
                    glm::vec3 sunPos(0.0f); // Sun is at origin in SSB coordinates
                    glm::vec3 bodyPos = obj.position;
                    float bodyRadius = obj.radius;

                    // Store the selected body's radius for movement scaling
                    APP_STATE.hoverState.selectedBodyRadius = bodyRadius;

                    // Direction from sun to body (camera will be on this line, between sun and body)
                    glm::vec3 sunToBody = bodyPos - sunPos;
                    float sunDist = glm::length(sunToBody);
                    if (sunDist > 0.001f)
                    {
                        sunToBody = sunToBody / sunDist;
                    }
                    else
                    {
                        sunToBody = glm::vec3(1.0f, 0.0f, 0.0f); // Default direction
                    }

                    // Camera position: 3 radii away from body, toward the sun
                    float distance = bodyRadius * APP_STATE.hoverState.followDistance;
                    glm::vec3 cameraPos = bodyPos - sunToBody * distance;

                    // Store offset from body to camera (for orbit control)
                    APP_STATE.hoverState.cameraOffset = cameraPos - bodyPos;

                    // Update camera position
                    APP_STATE.worldState.camera.position = cameraPos;

                    // Point camera at the body
                    glm::vec3 toBody = glm::normalize(bodyPos - cameraPos);
                    APP_STATE.worldState.camera.yaw = glm::degrees(atan2f(toBody.z, toBody.x));
                    APP_STATE.worldState.camera.pitch = glm::degrees(asinf(glm::clamp(toBody.y, -1.0f, 1.0f)));

                    std::cout << "Selected and following: " << APP_STATE.hoverState.selectedBodyName << "\n";
                    break;
                }
            }
        }

        // ====================================
        // Handle mouse drag for orbit/free-look while following
        // ====================================
        if (APP_STATE.hoverState.followingSelected && APP_STATE.hoverState.selectedNaifId > 0)
        {
            // Calculate mouse delta
            double deltaX = currentMouseX - lastMouseX;
            double deltaY = currentMouseY - lastMouseY;

            // Only process drag if mouse was already down (not just pressed)
            if (isMouseDown && wasMouseDown && (std::abs(deltaX) > 0.01 || std::abs(deltaY) > 0.01))
            {
                if (altKeyPressed)
                {
                    // Alt+drag: Orbit camera around the body center
                    // Use rotation matrices around fixed world axes (longitude/latitude style)
                    // This is completely decoupled from camera orientation
                    glm::vec3 &offset = APP_STATE.hoverState.cameraOffset;
                    float distance = glm::length(offset);

                    if (distance > 0.001f)
                    {
                        // Longitude rotation: around world Y axis (horizontal drag)
                        float lonAngle = -static_cast<float>(deltaX) * ORBIT_SPEED;
                        glm::mat4 lonRot = glm::rotate(glm::mat4(1.0f), lonAngle, glm::vec3(0.0f, 1.0f, 0.0f));

                        // Latitude rotation: around horizontal axis perpendicular to offset in XZ plane
                        // This axis is (-z, 0, x) normalized, which keeps us on a meridian
                        glm::vec3 offsetXZ(offset.x, 0.0f, offset.z);
                        float xzLen = glm::length(offsetXZ);
                        glm::vec3 latAxis;
                        if (xzLen > 0.001f)
                        {
                            latAxis = glm::normalize(glm::vec3(-offset.z, 0.0f, offset.x));
                        }
                        else
                        {
                            // Near pole, use arbitrary horizontal axis
                            latAxis = glm::vec3(1.0f, 0.0f, 0.0f);
                        }
                        float latAngle = static_cast<float>(deltaY) * ORBIT_SPEED;
                        glm::mat4 latRot = glm::rotate(glm::mat4(1.0f), latAngle, latAxis);

                        // Apply rotations
                        offset = glm::vec3(lonRot * latRot * glm::vec4(offset, 1.0f));

                        // Normalize to exact distance (prevent any floating point drift)
                        offset = glm::normalize(offset) * distance;
                    }
                }
                else
                {
                    // Normal drag: Free-look (rotate camera without moving position)
                    APP_STATE.worldState.camera.yaw += static_cast<float>(deltaX) * ROTATE_SPEED;
                    APP_STATE.worldState.camera.pitch -= static_cast<float>(deltaY) * ROTATE_SPEED;

                    // Clamp pitch to prevent flipping
                    APP_STATE.worldState.camera.pitch = glm::clamp(APP_STATE.worldState.camera.pitch, -89.0f, 89.0f);
                }
            }

            // Update camera position to follow the body using stored offset
            for (const auto &obj : APP_STATE.worldState.celestialObjects)
            {
                if (obj.naifId == APP_STATE.hoverState.selectedNaifId)
                {
                    APP_STATE.worldState.camera.position = obj.position + APP_STATE.hoverState.cameraOffset;
                    break;
                }
            }
        }

        // ====================================
        // Handle scroll wheel for camera movement
        // ====================================
        // Scroll moves camera forward/backward along its forward vector
        // Base movement is proportional to selected body's radius
        // Movement is capped by maxCameraStep to prevent clipping through terrain
        if (std::abs(inputState.scrollY) > 0.001)
        {
            // Get camera forward direction in world space
            glm::vec3 forward = APP_STATE.worldState.camera.getFront();

            // Calculate movement distance:
            // - Base step is proportional to selected body's radius (larger body = larger steps)
            // - scrollY positive = scroll up = zoom in (move forward)
            // - scrollY negative = scroll down = zoom out (move backward)
            // - Capped by maxCameraStep to prevent terrain penetration
            float bodyRadius = APP_STATE.hoverState.selectedBodyRadius;
            float baseStep =
                static_cast<float>(inputState.scrollY) * bodyRadius * APP_STATE.worldState.scrollSpeedMultiplier;
            float clampedStep =
                glm::clamp(baseStep, -APP_STATE.worldState.maxCameraStep, APP_STATE.worldState.maxCameraStep);

            // Apply movement
            glm::vec3 movement = forward * clampedStep;
            APP_STATE.worldState.camera.position += movement;

            // If following a body, update the offset as well
            if (APP_STATE.hoverState.followingSelected && APP_STATE.hoverState.selectedNaifId != 0)
            {
                for (const auto &obj : APP_STATE.worldState.celestialObjects)
                {
                    if (obj.naifId == APP_STATE.hoverState.selectedNaifId)
                    {
                        // Recalculate offset from body to new camera position
                        APP_STATE.hoverState.cameraOffset = APP_STATE.worldState.camera.position - obj.position;
                        break;
                    }
                }
            }
        }

        // Update last mouse position
        lastMouseX = currentMouseX;
        lastMouseY = currentMouseY;
        wasMouseDown = isMouseDown;

        // Render frame - UI will read hover state from APP_STATE to display tooltip
        RenderFrame(screenState);

        // Read back min distance from GPU for camera collision detection
        // This gives us the closest distance to displaced terrain from the previous frame
        float minDist = readMinSurfaceDistance(screenState.vulkanRenderer.context);
        APP_STATE.worldState.minSurfaceDistance = minDist;

        // Update maxCameraStep based on proximity to terrain
        // The max step is the smaller of:
        // 1. Distance-based limit (prevents clipping through terrain)
        // 2. Radius-based limit (scales movement to body size)
        constexpr float COLLISION_MARGIN = 0.5f; // Only allow moving 50% of the way to terrain per scroll
        constexpr float MIN_STEP = 0.00001f;     // Minimum step size (prevents getting stuck)

        float bodyRadius = APP_STATE.hoverState.selectedBodyRadius;

        // Distance-based limit: fraction of distance to terrain
        float distanceLimit = minDist * COLLISION_MARGIN;

        // Radius-based limit: when far from terrain, cap at body radius
        // This ensures movement feels natural relative to the body's scale
        float radiusLimit = bodyRadius * APP_STATE.worldState.scrollSpeedMultiplier;

        // Use the smaller of the two limits
        float newMaxStep = glm::min(distanceLimit, radiusLimit);
        APP_STATE.worldState.maxCameraStep = glm::max(newMaxStep, MIN_STEP);

        // Propagate shutdown signal to renderer
        if (g_shouldShutdown)
        {
            screenState.shouldExit = true;
            screenState.vulkanRenderer.shouldExit = true;
        }
    }

    // Cleanup all subsystems
    CleanupApplication(screenState);

    return 0;
}
