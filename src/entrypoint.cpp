#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>


// Import our concerns modules
#include "concerns/camera-controller.h"
#include "concerns/constants.h"
#include "concerns/constellation-loader.h" // For getDefaultsPath()
#include "concerns/gravity-grid.h"
#include "concerns/settings.h"
#include "concerns/solar-lighting.h"
#include "concerns/spice-ephemeris.h"
#include "concerns/stars-dynamic-skybox.h"
#include "concerns/ui-overlay.h"
#include "materials/earth/earth-material.h"
#include "materials/earth/economy/earth-economy.h"
#include "materials/earth/economy/economy-renderer.h"
#include "materials/earth/helpers/coordinate-conversion.h"
#include "types/celestial-body.h"
#include "types/lagrange-point.h"
#include "types/magnetic-field.h"


// Window dimensions (updated on resize)
int screenWidth = 1280;
int screenHeight = 720;

// Fullscreen state
bool g_isFullscreen = false;
int g_windowedX = 100; // Saved windowed position
int g_windowedY = 100;
int g_windowedWidth = 1280; // Saved windowed size
int g_windowedHeight = 720;

// Current Julian Date (mutable simulation state)
double currentJD = JD_J2000; // Will be set to current date on startup

// Time scaling: how many simulation days pass per real second
// This is controlled by the UI slider (range: 0.01 to 100 days/sec)
double timeDilation = 1.0 / 86400.0; // days per second - start at real-time (1 sec/sec)
bool g_timePaused = false;           // Whether time is paused
double lastTime = 0.0;

// Trail recording - record once per JD step (not every frame)
double lastTrailRecordJD = 0.0;
const double TRAIL_RECORD_INTERVAL = 0.1; // Record trail point every 0.1 days (~2.4 hours sim time)

// Global camera controller instance
CameraController camera;

// Window resize callback
void FramebufferSizeCallback(GLFWwindow *window, int width, int height)
{
    screenWidth = width;
    screenHeight = height;
    glViewport(0, 0, width, height);

    // Update camera controller's screen dimensions for raycasting
    camera.updateScreenSize(width, height);
}

// Toggle fullscreen mode
void ToggleFullscreen(GLFWwindow *window)
{
    if (g_isFullscreen)
    {
        // Switch to windowed mode - restore saved position and size
        glfwSetWindowMonitor(window,
                             nullptr,
                             g_windowedX,
                             g_windowedY,
                             g_windowedWidth,
                             g_windowedHeight,
                             GLFW_DONT_CARE);
        g_isFullscreen = false;
    }
    else
    {
        // Save current windowed position and size before going fullscreen
        glfwGetWindowPos(window, &g_windowedX, &g_windowedY);
        glfwGetWindowSize(window, &g_windowedWidth, &g_windowedHeight);

        // Get the primary monitor and its video mode
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);

        // Switch to exclusive fullscreen
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        g_isFullscreen = true;
    }
}

// Key callback for global shortcuts
void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS)
    {
        // F11 or Alt+Enter toggles fullscreen
        if (key == GLFW_KEY_F11 || (key == GLFW_KEY_ENTER && (mods & GLFW_MOD_ALT)))
        {
            ToggleFullscreen(window);
        }
        // Escape exits fullscreen (or could close window if not fullscreen)
        else if (key == GLFW_KEY_ESCAPE && g_isFullscreen)
        {
            ToggleFullscreen(window);
        }
    }
}

// Initialize Julian Date from SPICE ephemeris data
// Uses the latest available time in TDB from the SPICE kernels
void initializeFromSpice()
{
    // Get the latest time available in the SPICE data
    double latestJD = SpiceEphemeris::getLatestAvailableTime();
    double earliestJD = SpiceEphemeris::getEarliestAvailableTime();

    // Get current system time
    std::time_t now = std::time(nullptr);
    std::tm *utc = std::gmtime(&now);

    int year = utc->tm_year + 1900;
    int month = utc->tm_mon + 1;
    int day = utc->tm_mday;
    int hour = utc->tm_hour;
    int minute = utc->tm_min;
    int second = utc->tm_sec;

    // Convert current time to TDB Julian Date
    double nowJD = SpiceEphemeris::utcToTdbJulian(year, month, day, hour, minute, static_cast<double>(second));

    // Use current time if within SPICE coverage, otherwise use latest available
    if (nowJD >= earliestJD && nowJD <= latestJD)
    {
        currentJD = nowJD;
        std::cout << "Simulation starting at current time: " << year << "-" << (month < 10 ? "0" : "") << month << "-"
                  << (day < 10 ? "0" : "") << day << " " << (hour < 10 ? "0" : "") << hour << ":"
                  << (minute < 10 ? "0" : "") << minute << " UTC\n";
    }
    else
    {
        currentJD = latestJD;
        std::cout << "Current time outside SPICE coverage. Using latest available time.\n";
    }

    std::cout << "Julian Date (TDB): " << currentJD << "\n";
    std::cout << "SPICE coverage: JD " << earliestJD << " to " << latestJD << "\n";
}

// Forward declarations
GLFWwindow *StartGLFW();
void DrawSphere(const glm::vec3 &center, float radius, const glm::vec3 &color, int slices, int stacks);
void DrawOrbit(const glm::vec3 &center,
               const glm::vec3 &bodyPosition,
               float lineWidth,
               const glm::vec3 &color,
               int segments = 128);

// ============================================================================
// Render Ordering and Culling System
// ============================================================================
// Proper back-to-front rendering with frustum culling and occlusion

struct RenderItem
{
    CelestialBody *body;
    float distanceToCamera;
    float angularRadius; // How big the object appears (for occlusion)
};

// Check if a sphere is within the view frustum
// Uses a simple cone test based on camera direction and FOV
// Frustum is expanded by 15 degrees for better edge handling and pre-rendering during rotation
bool isInFrustum(const glm::vec3 &sphereCenter,
                 float sphereRadius,
                 const glm::vec3 &cameraPos,
                 const glm::vec3 &cameraDir,
                 float fovRadians)
{
    glm::vec3 toSphere = sphereCenter - cameraPos;
    float distance = glm::length(toSphere);

    // Object behind camera (with generous tolerance for large objects)
    float behindTolerance = sphereRadius * 3.0f;
    if (glm::dot(toSphere, cameraDir) < -behindTolerance)
    {
        return false;
    }

    // Very close objects are always visible
    if (distance < sphereRadius * 2.0f)
    {
        return true;
    }

    // Cone test: is the sphere within the expanded view cone?
    // 1. Start with camera FOV
    // 2. Add 15 degrees expansion for edge handling and pre-rendering during rotation
    // 3. Add the object's angular size so partially visible objects aren't culled
    const float FRUSTUM_EXPANSION = glm::radians(15.0f); // 15 degrees extra in all directions

    float angularSize = atan(sphereRadius / distance);
    float halfFov = fovRadians * 0.5f;
    float expandedHalfFov = halfFov + FRUSTUM_EXPANSION + angularSize;

    glm::vec3 dirToSphere = glm::normalize(toSphere);
    float cosAngle = glm::dot(dirToSphere, cameraDir);
    float cosExpandedFov = cos(expandedHalfFov);

    return cosAngle >= cosExpandedFov;
}

// Check if object A fully occludes object B from the camera's perspective
bool isFullyOccluded(const RenderItem &target, const RenderItem &occluder, const glm::vec3 &cameraPos)
{
    // Target must be further away
    if (target.distanceToCamera <= occluder.distanceToCamera)
    {
        return false;
    }

    // Check if target is behind the occluder from camera's view
    glm::vec3 toTarget = target.body->position - cameraPos;
    glm::vec3 toOccluder = occluder.body->position - cameraPos;

    glm::vec3 dirTarget = glm::normalize(toTarget);
    glm::vec3 dirOccluder = glm::normalize(toOccluder);

    // Angular separation between target and occluder centers
    float cosAngle = glm::dot(dirTarget, dirOccluder);
    float angle = acos(glm::clamp(cosAngle, -1.0f, 1.0f));

    // Target angular radius from camera
    float targetAngular =
        (target.distanceToCamera > 0.001f) ? atan(target.body->displayRadius / target.distanceToCamera) : 3.14159f;

    // Occluder angular radius from camera
    float occluderAngular =
        (occluder.distanceToCamera > 0.001f) ? atan(occluder.body->displayRadius / occluder.distanceToCamera) : 0.0f;

    // Target is fully occluded if it's entirely within the occluder's angular disk
    // (occluder angular radius must cover both the angle to target AND the target's own size)
    return (angle + targetAngular) < occluderAngular;
}

// Sort render items back-to-front
void sortRenderItems(std::vector<RenderItem> &items)
{
    std::sort(items.begin(), items.end(), [](const RenderItem &a, const RenderItem &b) {
        return a.distanceToCamera > b.distanceToCamera; // Furthest first
    });
}

// Build render queue with frustum culling and distance sorting
std::vector<RenderItem> buildRenderQueue(std::vector<CelestialBody *> &bodies,
                                         const glm::vec3 &cameraPos,
                                         const glm::vec3 &cameraDir,
                                         float fovRadians,
                                         bool enableOcclusionCulling = true,
                                         CelestialBody *selectedBody = nullptr // If provided, this body is never culled
)
{
    std::vector<RenderItem> queue;
    queue.reserve(bodies.size());

    // First pass: frustum culling and distance calculation
    for (CelestialBody *body : bodies)
    {
        float dist = glm::length(body->position - cameraPos);

        // Frustum culling - skip culling for selected body (so atmosphere always renders)
        if (body != selectedBody && !isInFrustum(body->position, body->displayRadius, cameraPos, cameraDir, fovRadians))
        {
            continue;
        }

        RenderItem item;
        item.body = body;
        item.distanceToCamera = dist;
        item.angularRadius = (dist > 0.001f) ? atan(body->displayRadius / dist) : 3.14159f;
        queue.push_back(item);
    }

    // Sort back-to-front (furthest first)
    sortRenderItems(queue);

    // Second pass: occlusion culling (optional, can be expensive)
    if (enableOcclusionCulling && queue.size() > 1)
    {
        std::vector<RenderItem> visibleQueue;
        visibleQueue.reserve(queue.size());

        for (size_t i = 0; i < queue.size(); i++)
        {
            bool occluded = false;

            // Check against all closer objects (which come later in sorted order)
            for (size_t j = i + 1; j < queue.size(); j++)
            {
                if (isFullyOccluded(queue[i], queue[j], cameraPos))
                {
                    occluded = true;
                    break;
                }
            }

            if (!occluded)
            {
                visibleQueue.push_back(queue[i]);
            }
        }

        return visibleQueue;
    }

    return queue;
}

// ============================================================================
// SPICE Ephemeris Helpers
// ============================================================================

// Convert AU position from SPICE (J2000 equatorial) to display units (Y-up)
// SPICE J2000 frame: X toward vernal equinox, Y in equatorial plane, Z toward celestial north pole
// Display coordinates: X (right), Y (up), Z (forward/depth)
// Transformation: X stays X, Z becomes Y (up), Y becomes Z
// This same transformation is used for both positions AND directions (pole, prime meridian)
// in CelestialBody::updatePoleDirection() to ensure consistency.
glm::vec3 auToDisplayUnits(const glm::dvec3 &posAU)
{
    return glm::vec3(static_cast<float>(posAU.x * UNITS_PER_AU),
                     static_cast<float>(posAU.z * UNITS_PER_AU), // SPICE Z -> Display Y (up)
                     static_cast<float>(posAU.y * UNITS_PER_AU)  // SPICE Y -> Display Z
    );
}

// Get body position from SPICE relative to Solar System Barycenter
// Returns position in display units
glm::vec3 getBodyPositionSpice(int naifId, double jdTdb)
{
    glm::dvec3 posAU = SpiceEphemeris::getBodyPosition(naifId, jdTdb);
    return auToDisplayUnits(posAU);
}

// Get moon position relative to its parent planet
// Applies distance scaling for visibility
glm::vec3 getMoonPositionSpice(int moonNaifId, int parentNaifId, double jdTdb, const glm::vec3 &parentPos)
{
    // Get absolute positions
    glm::dvec3 moonPosAU = SpiceEphemeris::getBodyPosition(moonNaifId, jdTdb);
    glm::dvec3 parentPosAU = SpiceEphemeris::getBodyPosition(parentNaifId, jdTdb);

    // Calculate relative position
    glm::dvec3 relativeAU = moonPosAU - parentPosAU;

    // Scale for visibility and convert to display units
    glm::vec3 relativeDisplay = auToDisplayUnits(relativeAU);
    relativeDisplay *= MOON_DISTANCE_SCALE;

    return parentPos + relativeDisplay;
}

// Fallback: compute simple circular orbit for moon without SPICE data
// smaAU: semi-major axis in AU
// periodDays: orbital period in days
// jdTdb: current Julian Date
// parentPos: current position of the parent body
glm::vec3 getMoonPositionFallback(double smaAU, double periodDays, double jdTdb, const glm::vec3 &parentPos)
{
    // Calculate orbital angle based on time
    double daysSinceJ2000 = jdTdb - JD_J2000;
    double orbits = daysSinceJ2000 / periodDays;
    double angle = orbits * 2.0 * PI; // Convert to radians

    // Compute position in circular orbit (XZ plane)
    double orbitRadiusDisplay = smaAU * UNITS_PER_AU * MOON_DISTANCE_SCALE;

    glm::vec3 offset(static_cast<float>(cos(angle) * orbitRadiusDisplay),
                     0.0f, // Flat orbit in XZ plane
                     static_cast<float>(sin(angle) * orbitRadiusDisplay));

    return parentPos + offset;
}

// Update body position and velocity from SPICE
void updateBodyStateSpice(CelestialBody &body, int naifId, double jdTdb)
{
    glm::dvec3 posAU, velAUDay;
    if (SpiceEphemeris::getBodyState(naifId, jdTdb, posAU, velAUDay))
    {
        body.position = auToDisplayUnits(posAU);
        // Convert velocity from AU/day to display units/day
        body.velocity = glm::vec3(static_cast<float>(velAUDay.x * UNITS_PER_AU),
                                  static_cast<float>(velAUDay.z * UNITS_PER_AU), // SPICE Z -> Display Y
                                  static_cast<float>(velAUDay.y * UNITS_PER_AU)  // SPICE Y -> Display Z
        );
    }
    else
    {
        body.position = glm::vec3(0.0f);
        body.velocity = glm::vec3(0.0f);
    }
}

// ============================================================================
// Main Program
// ============================================================================

int main()
{
    // ========================================================================
    // Load Application Settings
    // ========================================================================
    Settings::load("settings.json5");
    TextureResolution textureRes = Settings::getTextureResolution();
    Settings::markAsRunning(); // Mark current resolution as the running one

    std::cout << "Texture resolution: " << getResolutionName(textureRes) << "\n";

    // ========================================================================
    // Pre-window initialization: Process Earth textures
    // ========================================================================
    // Combine Blue Marble tiles into monthly textures at the configured resolution.
    // This runs before OpenGL is initialized, so textures are ready when needed.
    std::cout << "\n";
    int earthColorTexturesReady =
        EarthMaterial::preprocessTiles("defaults",       // Source tiles in defaults/earth-surface/blue-marble/
                                       "earth-textures", // Output combined images next to executable
                                       textureRes        // Use configured resolution
        );
    std::cout << "\n";

    // Process elevation data into heightmap and normal map textures.
    // This generates bump mapping textures from ETOPO GeoTIFF elevation data.
    bool earthElevationReady =
        EarthMaterial::preprocessElevation("defaults",       // Source elevation in defaults/earth-surface/elevation/
                                           "earth-textures", // Output next to color textures
                                           textureRes        // Use same resolution as color textures
        );
    std::cout << "\n";

    // Process MODIS reflectance data into specular/roughness texture.
    // This extracts relative green (green - red) for surface roughness mapping.
    int earthSpecularReady =
        EarthMaterial::preprocessSpecular("defaults",       // Source MODIS data in defaults/earth-surface/albedo/
                                          "earth-textures", // Output next to executable
                                          textureRes);
    std::cout << "\n";

    // Process VIIRS Black Marble nightlights for city lights at night
    // This converts HDF5 radiance data into grayscale emissive texture
    bool earthNightlightsReady =
        EarthMaterial::preprocessNightlights("defaults",       // Source in defaults/earth-surface/human-lights/
                                             "earth-textures", // Output next to executable
                                             textureRes);
    std::cout << "\n";

    // Generate ice masks from Blue Marble monthly textures
    // Creates 12 masks (one per month) for ice/snow coverage
    bool earthIceMasksReady = EarthMaterial::preprocessIceMasks("defaults", // Not used (reads from earth-textures)
                                                                "earth-textures", // Where monthly textures are
                                                                textureRes);
    (void)earthIceMasksReady; // Currently unused, prepared for future feature
    std::cout << "\n";

    // Generate atmosphere transmittance LUT (precomputed to avoid ray marching every frame)
    // Creates 2D lookup table: altitude vs sun zenith angle -> RGB transmittance
    bool atmosphereLUTReady = EarthMaterial::preprocessAtmosphereTransmittanceLUT("earth-textures");
    (void)atmosphereLUTReady; // Prepared for future use
    std::cout << "\n";

    // Preprocess city data from Excel file into texture
    // Loads worldcities.xlsx and generates city location texture (sinusoidal projection)
    std::string citiesXlsxPath = getDefaultsPath() + "/economy/worldcities.xlsx";
    bool citiesReady = EarthEconomy::preprocessCities(citiesXlsxPath, "earth-textures", textureRes);
    (void)citiesReady; // Prepared for runtime use
    std::cout << "\n";

    // Combined result: color textures + elevation textures + specular + nightlights
    int earthTexturesReady =
        earthColorTexturesReady + (earthElevationReady ? 1 : 0) + earthSpecularReady + (earthNightlightsReady ? 1 : 0);

    // ========================================================================
    // Generate star texture at configured resolution (if not already cached).
    // Uses J2000.0 epoch for star positions - proper motion is negligible at human timescales.
    const double J2000_JD = 2451545.0;                       // January 1, 2000, 12:00 TT
    int starsRendered = GenerateStarTexture("defaults",      // Source star catalog
                                            "star-textures", // Output texture folder
                                            textureRes,      // Use configured resolution
                                            J2000_JD         // Reference epoch for star positions
    );
    std::cout << "\n";

    GLFWwindow *window = StartGLFW();
    if (!window)
        return -1;

    glfwMakeContextCurrent(window);

    // Set up window resize callback
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);

    // Set up key callback for fullscreen toggle (F11, Alt+Enter)
    glfwSetKeyCallback(window, KeyCallback);

    // Get initial framebuffer size (may differ from window size on HiDPI displays)
    glfwGetFramebufferSize(window, &screenWidth, &screenHeight);
    glViewport(0, 0, screenWidth, screenHeight);

    // Initialize camera controller (sets up callbacks and cursors)
    camera.initCallbacks(window);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    SolarLighting::initialize();

    // ========================================================================
    // Initialize SPICE Ephemeris System
    // ========================================================================
    std::cout << "Solar System Simulator using NASA/NAIF SPICE Ephemeris\n";
    std::cout << "All positions relative to Solar System Barycenter (SSB)\n";
    std::cout << "Time system: Barycentric Dynamical Time (TDB)\n\n";

    // Load SPICE kernels from defaults/kernels directory
    if (!SpiceEphemeris::initialize("defaults/kernels"))
    {
        std::cerr << "\n=== SPICE KERNEL SETUP REQUIRED ===\n";
        std::cerr << "Please download SPICE kernels and place them in: defaults/kernels/\n";
        std::cerr << "\nRequired kernels:\n";
        std::cerr << "  1. Planetary ephemeris (SPK): de440s.bsp or de440.bsp\n";
        std::cerr << "     Download: https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/\n";
        std::cerr << "  2. Leap seconds (LSK): naif0012.tls\n";
        std::cerr << "     Download: https://naif.jpl.nasa.gov/pub/naif/generic_kernels/lsk/\n";
        std::cerr << "  3. (Optional) Satellite ephemeris for moons:\n";
        std::cerr << "     - jup365.bsp (Jupiter moons)\n";
        std::cerr << "     - sat441.bsp (Saturn moons)\n";
        std::cerr << "     Download: https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/satellites/\n";
        std::cerr << "=====================================\n\n";
        // Continue anyway - will use fallback positions
    }

    // Initialize skybox with constellation data from JSON5 files
    InitializeSkybox("defaults");

    // Initialize star texture material (load pre-generated texture into OpenGL)
    if (InitializeStarTextureMaterial("star-textures", textureRes))
    {
        std::cout << "Star texture material initialized successfully\n";
    }
    else
    {
        std::cout << "Star texture not available, will use dynamic rendering\n";
    }

    // Initialize UI system
    InitUI();

    // Initialize Earth material (load pre-combined monthly textures into OpenGL)
    if (g_earthMaterial.initialize("earth-textures", textureRes))
    {
        std::cout << "Earth textured material initialized successfully\n";
    }
    else
    {
        std::cout << "Earth textured material not available (no Blue Marble source tiles found)\n";
    }

    // Initialize Earth economy system (load city data for hover tooltips)
    if (g_earthEconomy.initialize("earth-textures", textureRes))
    {
        std::cout << "Earth economy system initialized successfully\n";
    }
    else
    {
        std::cout << "Earth economy system not available (city data not loaded)\n";
    }

    // Initialize economy renderer (for city label rendering)
    if (g_economyRenderer.initialize())
    {
        std::cout << "Economy renderer initialized successfully\n";
    }
    else
    {
        std::cout << "Economy renderer initialization failed\n";
    }

    lastTime = glfwGetTime();

    // ========================================================================
    // Create celestial bodies (positions updated each frame from SPICE)
    // ========================================================================

    // Import NAIF IDs from SpiceEphemeris namespace
    using SpiceEphemeris::NAIF_CALLISTO;
    using SpiceEphemeris::NAIF_CHARON;
    using SpiceEphemeris::NAIF_EARTH;
    using SpiceEphemeris::NAIF_EUROPA;
    using SpiceEphemeris::NAIF_GANYMEDE;
    using SpiceEphemeris::NAIF_IO;
    using SpiceEphemeris::NAIF_JUPITER;
    using SpiceEphemeris::NAIF_MARS;
    using SpiceEphemeris::NAIF_MERCURY;
    using SpiceEphemeris::NAIF_MOON;
    using SpiceEphemeris::NAIF_NEPTUNE;
    using SpiceEphemeris::NAIF_PLUTO;
    using SpiceEphemeris::NAIF_SATURN;
    using SpiceEphemeris::NAIF_SUN;
    using SpiceEphemeris::NAIF_TITAN;
    using SpiceEphemeris::NAIF_TRITON;
    using SpiceEphemeris::NAIF_URANUS;
    using SpiceEphemeris::NAIF_VENUS;

    // Sun - accurate radius (109x Earth = ~164 display units)
    // Using SPICE NAIF IDs for rotation data from PCK kernel
    CelestialBody sun("Sun", NAIF_SUN, glm::vec3(1.0f, 0.92f, 0.4f), getDisplayRadius(RADIUS_SUN_KM), MASS_SUN, 7.25f);

    // Planets - accurate relative sizes with NAIF IDs and fallback axial tilts
    CelestialBody mercury("Mercury",
                          NAIF_MERCURY,
                          glm::vec3(0.7f, 0.7f, 0.7f),
                          getDisplayRadius(RADIUS_MERCURY_KM),
                          MASS_MERCURY,
                          MERCURY_AXIAL_TILT);
    CelestialBody venus("Venus",
                        NAIF_VENUS,
                        glm::vec3(0.95f, 0.9f, 0.7f),
                        getDisplayRadius(RADIUS_VENUS_KM),
                        MASS_VENUS,
                        VENUS_AXIAL_TILT);
    CelestialBody earth("Earth",
                        NAIF_EARTH,
                        glm::vec3(0.2f, 0.5f, 0.9f),
                        getDisplayRadius(RADIUS_EARTH_KM),
                        MASS_EARTH,
                        EARTH_AXIAL_TILT);
    CelestialBody mars("Mars",
                       NAIF_MARS,
                       glm::vec3(0.9f, 0.4f, 0.2f),
                       getDisplayRadius(RADIUS_MARS_KM),
                       MASS_MARS,
                       MARS_AXIAL_TILT);
    CelestialBody jupiter("Jupiter",
                          NAIF_JUPITER,
                          glm::vec3(0.9f, 0.8f, 0.6f),
                          getDisplayRadius(RADIUS_JUPITER_KM),
                          MASS_JUPITER,
                          JUPITER_AXIAL_TILT);
    CelestialBody saturn("Saturn",
                         NAIF_SATURN,
                         glm::vec3(0.95f, 0.88f, 0.65f),
                         getDisplayRadius(RADIUS_SATURN_KM),
                         MASS_SATURN,
                         SATURN_AXIAL_TILT);
    CelestialBody uranus("Uranus",
                         NAIF_URANUS,
                         glm::vec3(0.6f, 0.85f, 0.92f),
                         getDisplayRadius(RADIUS_URANUS_KM),
                         MASS_URANUS,
                         URANUS_AXIAL_TILT);
    CelestialBody neptune("Neptune",
                          NAIF_NEPTUNE,
                          glm::vec3(0.3f, 0.5f, 0.95f),
                          getDisplayRadius(RADIUS_NEPTUNE_KM),
                          MASS_NEPTUNE,
                          NEPTUNE_AXIAL_TILT);
    CelestialBody pluto("Pluto",
                        NAIF_PLUTO,
                        glm::vec3(0.8f, 0.75f, 0.7f),
                        getDisplayRadius(RADIUS_PLUTO_KM),
                        MASS_PLUTO,
                        PLUTO_AXIAL_TILT);

    // Moons - with NAIF IDs for rotation data
    CelestialBody luna("Moon", NAIF_MOON, glm::vec3(0.78f, 0.78f, 0.8f), getDisplayRadius(RADIUS_MOON_KM), MASS_MOON);
    CelestialBody io("Io", NAIF_IO, glm::vec3(0.95f, 0.9f, 0.45f), getDisplayRadius(RADIUS_IO_KM), MASS_IO);
    CelestialBody europa("Europa",
                         NAIF_EUROPA,
                         glm::vec3(0.92f, 0.94f, 0.98f),
                         getDisplayRadius(RADIUS_EUROPA_KM),
                         MASS_EUROPA);
    CelestialBody ganymede("Ganymede",
                           NAIF_GANYMEDE,
                           glm::vec3(0.65f, 0.6f, 0.55f),
                           getDisplayRadius(RADIUS_GANYMEDE_KM),
                           MASS_GANYMEDE);
    CelestialBody callisto("Callisto",
                           NAIF_CALLISTO,
                           glm::vec3(0.45f, 0.42f, 0.4f),
                           getDisplayRadius(RADIUS_CALLISTO_KM),
                           MASS_CALLISTO);
    CelestialBody titan("Titan",
                        NAIF_TITAN,
                        glm::vec3(0.9f, 0.7f, 0.4f),
                        getDisplayRadius(RADIUS_TITAN_KM),
                        MASS_TITAN);
    CelestialBody triton("Triton",
                         NAIF_TRITON,
                         glm::vec3(0.85f, 0.82f, 0.85f),
                         getDisplayRadius(RADIUS_TRITON_KM),
                         MASS_TRITON);
    CelestialBody charon("Charon",
                         NAIF_CHARON,
                         glm::vec3(0.6f, 0.58f, 0.56f),
                         getDisplayRadius(RADIUS_CHARON_KM),
                         MASS_CHARON);

    // Enable textured material for Earth (uses Blue Marble monthly textures)
    earth.useTexturedMaterial = true;

    // ========================================================================
    // Solar Lighting Setup
    // ========================================================================
    // Sun is emissive (self-luminous, 5778K blackbody)
    sun.isEmissive = true;
    sun.color = SolarLighting::SUN_COLOR; // Use accurate sun color

    // Set parent body for moons (they receive same lighting as their parent planet)
    luna.parentBody = &earth;
    io.parentBody = &jupiter;
    europa.parentBody = &jupiter;
    ganymede.parentBody = &jupiter;
    callisto.parentBody = &jupiter;
    titan.parentBody = &saturn;
    triton.parentBody = &neptune;
    charon.parentBody = &pluto;

    // Set rotation periods (sidereal day in hours)
    sun.rotationPeriod = SUN_ROTATION_HOURS;
    mercury.rotationPeriod = MERCURY_ROTATION_HOURS;
    venus.rotationPeriod = VENUS_ROTATION_HOURS;
    earth.rotationPeriod = EARTH_ROTATION_HOURS;
    mars.rotationPeriod = MARS_ROTATION_HOURS;
    jupiter.rotationPeriod = JUPITER_ROTATION_HOURS;
    saturn.rotationPeriod = SATURN_ROTATION_HOURS;
    uranus.rotationPeriod = URANUS_ROTATION_HOURS;
    neptune.rotationPeriod = NEPTUNE_ROTATION_HOURS;
    pluto.rotationPeriod = PLUTO_ROTATION_HOURS;
    luna.rotationPeriod = MOON_ROTATION_HOURS;
    // Galilean moons are tidally locked (orbital period = rotation period)
    io.rotationPeriod = 42.5;        // ~1.77 days
    europa.rotationPeriod = 85.2;    // ~3.55 days
    ganymede.rotationPeriod = 171.7; // ~7.15 days
    callisto.rotationPeriod = 400.5; // ~16.7 days
    titan.rotationPeriod = 382.7;    // ~15.9 days (tidally locked)
    triton.rotationPeriod = 141.0;   // ~5.88 days (tidally locked, retrograde)
    charon.rotationPeriod = 153.3;   // ~6.39 days (tidally locked to Pluto)

    // ========================================================================
    // Load Magnetic Field Models
    // ========================================================================

    // Earth - WMMHR-2025 high-resolution model (or fallback to IGRF-14)
    std::string earthCofPath = getDefaultsPath() + "/magnetic-models/earth-high-detail-coeffs.COF";
    std::string earthTxtPath = getDefaultsPath() + "/magnetic-models/earth-coeffs.txt";

    std::unique_ptr<IGRFModel> earthMagModel;

    // Try high-resolution COF file first
    if (std::filesystem::exists(earthCofPath))
    {
        earthMagModel = IGRFModel::loadFromCOF(earthCofPath);
    }

    // Fall back to traditional IGRF file
    if (!earthMagModel && std::filesystem::exists(earthTxtPath))
    {
        earthMagModel = IGRFModel::loadFromFile(earthTxtPath);
    }

    if (earthMagModel)
    {
        earth.setMagneticFieldModel(std::move(earthMagModel));
        std::cout << "Earth magnetic field model loaded" << "\n";
    }
    else
    {
        std::cerr << "Warning: Failed to load Earth magnetic field model" << "\n";
    }

    // Saturn - Cassini model (load from xlsx if available)
    std::string saturnXlsxPath = getDefaultsPath() + "/magnetic-models/saturn-coeffs.xlsx";
    auto saturnMagModel = SaturnMagneticModel::loadFromXlsx(saturnXlsxPath);
    if (!saturnMagModel)
    {
        // Fall back to default coefficients
        saturnMagModel = SaturnMagneticModel::createDefault();
    }
    if (saturnMagModel)
    {
        saturn.setMagneticFieldModel(std::move(saturnMagModel));
    }

    // Jupiter - Juno/JRM33 model
    std::string jupiterPath = getDefaultsPath() + "/magnetic-models/jupiter-coeffs.dat";
    auto jupiterMagModel = JupiterMagneticModel::loadFromFile(jupiterPath);
    if (jupiterMagModel)
    {
        jupiter.setMagneticFieldModel(std::move(jupiterMagModel));
    }
    else
    {
        std::cerr << "Warning: Failed to load Jupiter magnetic field model from: " << jupiterPath << "\n";
    }

    // Mars - MGS crustal anomaly model (Purucker 2008)
    // Mars has no active dynamo but has strong crustal magnetic anomalies
    std::string marsPath = getDefaultsPath() + "/magnetic-models/mars-coeffs.txt";
    auto marsMagModel = MarsMagneticModel::loadFromFile(marsPath);
    if (marsMagModel)
    {
        mars.setMagneticFieldModel(std::move(marsMagModel));
    }
    else
    {
        std::cerr << "Warning: Failed to load Mars magnetic field model from: " << marsPath << "\n";
    }

    // Create vector of all bodies for raycasting
    std::vector<CelestialBody *> allBodies = {&sun,
                                              &mercury,
                                              &venus,
                                              &earth,
                                              &mars,
                                              &jupiter,
                                              &saturn,
                                              &uranus,
                                              &neptune,
                                              &pluto,
                                              &luna,
                                              &io,
                                              &europa,
                                              &ganymede,
                                              &callisto,
                                              &titan,
                                              &triton,
                                              &charon};

    // ========================================================================
    // Create Lagrange Point Systems
    // ========================================================================
    // Lagrange points are gravitationally stable positions in two-body systems
    // Display radius is proportional to the secondary body for visibility

    // Sun-Earth Lagrange points (includes James Webb Space Telescope at L2)
    LagrangeSystem sunEarthLagrange("Sun", "Earth", MASS_SUN, MASS_EARTH, earth.displayRadius * 0.3f);

    // Sun-Jupiter Lagrange points (Trojan asteroids at L4/L5)
    LagrangeSystem sunJupiterLagrange("Sun", "Jupiter", MASS_SUN, MASS_JUPITER, jupiter.displayRadius * 0.2f);

    // Earth-Moon Lagrange points
    LagrangeSystem earthMoonLagrange("Earth", "Moon", MASS_EARTH, MASS_MOON, luna.displayRadius * 0.5f);

    // Sun-Mars Lagrange points
    LagrangeSystem sunMarsLagrange("Sun", "Mars", MASS_SUN, MASS_MARS, mars.displayRadius * 0.25f);

    // ========================================================================
    // Set Magnetosphere Extent (L1 distance) for magnetic field visualization
    // ========================================================================
    // The L1 Lagrange point distance represents the magnetopause boundary
    // where the planet's magnetic field meets the solar wind.
    // We calculate this once at startup using typical orbital distances.

    // Average orbital distances in km (semi-major axes)
    const double EARTH_ORBIT_KM = 149597870.7;   // 1 AU
    const double JUPITER_ORBIT_KM = 778547200.0; // 5.2 AU
    const double SATURN_ORBIT_KM = 1433449370.0; // 9.58 AU
    const double MARS_ORBIT_KM = 227943824.0;    // 1.52 AU

    // Calculate L1 distances (Hill sphere approximation)
    earth.magnetosphereExtentKm = calculateL1L2Distance(EARTH_ORBIT_KM, MASS_SUN, MASS_EARTH);
    jupiter.magnetosphereExtentKm = calculateL1L2Distance(JUPITER_ORBIT_KM, MASS_SUN, MASS_JUPITER);
    saturn.magnetosphereExtentKm = calculateL1L2Distance(SATURN_ORBIT_KM, MASS_SUN, MASS_SATURN);
    mars.magnetosphereExtentKm = calculateL1L2Distance(MARS_ORBIT_KM, MASS_SUN, MASS_MARS);

    std::cout << "Magnetosphere extents (L1 distances):" << "\n";
    std::cout << "  Earth: " << earth.magnetosphereExtentKm << " km (" << (earth.magnetosphereExtentKm / 6371.0)
              << " Earth radii)" << "\n";
    std::cout << "  Jupiter: " << jupiter.magnetosphereExtentKm << " km (" << (jupiter.magnetosphereExtentKm / 71492.0)
              << " Jupiter radii)" << "\n";
    std::cout << "  Saturn: " << saturn.magnetosphereExtentKm << " km (" << (saturn.magnetosphereExtentKm / 58232.0)
              << " Saturn radii)" << "\n";
    std::cout << "  Mars: " << mars.magnetosphereExtentKm << " km (" << (mars.magnetosphereExtentKm / 3396.0)
              << " Mars radii)" << "\n";

    // Initialize simulation time from SPICE data
    initializeFromSpice();
    lastTrailRecordJD = currentJD; // Initialize trail recording to current time

    std::cout << "Time rate: " << timeDilation << " days per real second (adjustable via UI)\n";
    std::cout << "Controls: WS=forward/back, AD=left/right, Space/Ctrl=up/down\n";
    std::cout << "          Mouse drag=rotate, Right-drag=pan, Scroll=zoom\n";
    std::cout << "          Click=select, Double-click=focus, Alt+drag=orbit\n";

    // Track if camera has been initialized
    bool cameraInitialized = false;

    // ========================================================================
    // Main Loop
    // ========================================================================
    while (!glfwWindowShouldClose(window))
    {
        // Update simulation time
        double currentTime = glfwGetTime();
        double deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        // Advance Julian Date (TDB) - timeDilation is days per real second
        // Only advance time if not paused
        if (!g_timePaused)
        {
            currentJD += deltaTime * timeDilation;
        }

        camera.processKeyboard(window);

        glClearColor(0.003f, 0.003f, 0.012f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Skip rendering if window is minimized (zero size)
        if (screenWidth <= 0 || screenHeight <= 0)
        {
            glfwSwapBuffers(window);
            glfwPollEvents();
            continue;
        }

        // Projection - updates each frame to handle window resizing
        // Near plane is dynamic to allow close-up views at ground level
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float aspect = static_cast<float>(screenWidth) / static_cast<float>(screenHeight);
        float nearPlane = camera.getDynamicNearPlane(); // Dynamic: 1e-7 to 0.1 based on proximity
        float farPlane = 100000.0f;                     // Pluto is at ~24000 units, need margin
        float tanHalfFov = tan(glm::radians(camera.fov / 2.0f));
        float top = nearPlane * tanHalfFov;
        float right = top * aspect;
        glFrustum(-right, right, -top, top, nearPlane, farPlane);

        // NOTE: Camera view matrix is set LATER, after body positions and camera.updateFollowTarget()
        // This ensures camera and bodies are in sync for the same frame

        // ====================================================================
        // Record trail points (for orbital path visualization)
        // Only record once per JD step, not every frame
        // ====================================================================
        if (currentJD - lastTrailRecordJD >= TRAIL_RECORD_INTERVAL)
        {
            for (CelestialBody *body : allBodies)
            {
                body->recordTrailPoint();
            }
            lastTrailRecordJD = currentJD;
        }

        // ====================================================================
        // Update positions and velocities from SPICE ephemeris (relative to SSB)
        // ====================================================================
        using namespace SpiceEphemeris;

        // Sun and planets from SPICE (all relative to Solar System Barycenter)
        // This also updates velocity vectors for the details panel
        updateBodyStateSpice(sun, NAIF_SUN, currentJD);
        updateBodyStateSpice(mercury, NAIF_MERCURY, currentJD);
        updateBodyStateSpice(venus, NAIF_VENUS, currentJD);
        updateBodyStateSpice(earth, NAIF_EARTH, currentJD);
        updateBodyStateSpice(mars, NAIF_MARS, currentJD);
        updateBodyStateSpice(jupiter, NAIF_JUPITER, currentJD);
        updateBodyStateSpice(saturn, NAIF_SATURN, currentJD);
        updateBodyStateSpice(uranus, NAIF_URANUS, currentJD);
        updateBodyStateSpice(neptune, NAIF_NEPTUNE, currentJD);
        updateBodyStateSpice(pluto, NAIF_PLUTO, currentJD);

        // Moons - use SPICE if available, otherwise use circular orbit fallback
        if (hasBodyData(NAIF_MOON))
        {
            luna.position = getMoonPositionSpice(NAIF_MOON, NAIF_EARTH, currentJD, earth.position);
        }
        else
        {
            luna.position = getMoonPositionFallback(LUNA_SMA_AU, LUNA_PERIOD, currentJD, earth.position);
        }

        if (hasBodyData(NAIF_IO))
        {
            io.position = getMoonPositionSpice(NAIF_IO, NAIF_JUPITER, currentJD, jupiter.position);
        }
        else
        {
            io.position = getMoonPositionFallback(IO_SMA_AU, IO_PERIOD, currentJD, jupiter.position);
        }

        if (hasBodyData(NAIF_EUROPA))
        {
            europa.position = getMoonPositionSpice(NAIF_EUROPA, NAIF_JUPITER, currentJD, jupiter.position);
        }
        else
        {
            europa.position = getMoonPositionFallback(EUROPA_SMA_AU, EUROPA_PERIOD, currentJD, jupiter.position);
        }

        if (hasBodyData(NAIF_GANYMEDE))
        {
            ganymede.position = getMoonPositionSpice(NAIF_GANYMEDE, NAIF_JUPITER, currentJD, jupiter.position);
        }
        else
        {
            ganymede.position = getMoonPositionFallback(GANYMEDE_SMA_AU, GANYMEDE_PERIOD, currentJD, jupiter.position);
        }

        if (hasBodyData(NAIF_CALLISTO))
        {
            callisto.position = getMoonPositionSpice(NAIF_CALLISTO, NAIF_JUPITER, currentJD, jupiter.position);
        }
        else
        {
            callisto.position = getMoonPositionFallback(CALLISTO_SMA_AU, CALLISTO_PERIOD, currentJD, jupiter.position);
        }

        if (hasBodyData(NAIF_TITAN))
        {
            titan.position = getMoonPositionSpice(NAIF_TITAN, NAIF_SATURN, currentJD, saturn.position);
        }
        else
        {
            titan.position = getMoonPositionFallback(TITAN_SMA_AU, TITAN_PERIOD, currentJD, saturn.position);
        }

        if (hasBodyData(NAIF_TRITON))
        {
            triton.position = getMoonPositionSpice(NAIF_TRITON, NAIF_NEPTUNE, currentJD, neptune.position);
        }
        else
        {
            triton.position = getMoonPositionFallback(TRITON_SMA_AU, TRITON_PERIOD, currentJD, neptune.position);
        }

        if (hasBodyData(NAIF_CHARON))
        {
            charon.position = getMoonPositionSpice(NAIF_CHARON, NAIF_PLUTO, currentJD, pluto.position);
        }
        else
        {
            charon.position = getMoonPositionFallback(CHARON_SMA_AU, CHARON_PERIOD, currentJD, pluto.position);
        }

        // ====================================================================
        // Update pole directions from SPICE PCK kernel (or fallback)
        // ====================================================================
        sun.updatePoleDirection(currentJD);
        mercury.updatePoleDirection(currentJD);
        venus.updatePoleDirection(currentJD);
        earth.updatePoleDirection(currentJD);
        mars.updatePoleDirection(currentJD);
        jupiter.updatePoleDirection(currentJD);
        saturn.updatePoleDirection(currentJD);
        uranus.updatePoleDirection(currentJD);
        neptune.updatePoleDirection(currentJD);
        pluto.updatePoleDirection(currentJD);
        luna.updatePoleDirection(currentJD);
        io.updatePoleDirection(currentJD);
        europa.updatePoleDirection(currentJD);
        ganymede.updatePoleDirection(currentJD);
        callisto.updatePoleDirection(currentJD);
        titan.updatePoleDirection(currentJD);
        triton.updatePoleDirection(currentJD);
        charon.updatePoleDirection(currentJD);

        // ====================================================================
        // Compute Barycenters
        // ====================================================================

        // Solar system barycenter (Sun + all planets + all moons)
        sun.barycenter = computeBarycenter(allBodies);
        sun.barycenterDisplayRadius = sun.displayRadius * 0.5f;

        // Planetary barycenters (planet + its moons)
        std::vector<CelestialBody *> earthMoons = {&luna};
        std::vector<CelestialBody *> jupiterMoons = {&io, &europa, &ganymede, &callisto};
        std::vector<CelestialBody *> saturnMoons = {&titan};
        std::vector<CelestialBody *> neptuneMoons = {&triton};
        std::vector<CelestialBody *> plutoMoons = {&charon};

        computePlanetaryBarycenter(earth, earthMoons);
        computePlanetaryBarycenter(jupiter, jupiterMoons);
        computePlanetaryBarycenter(saturn, saturnMoons);
        computePlanetaryBarycenter(neptune, neptuneMoons);
        computePlanetaryBarycenter(pluto, plutoMoons);

        // ====================================================================
        // Update Lagrange Points
        // ====================================================================
        sunEarthLagrange.update(sun.position, earth.position);
        sunJupiterLagrange.update(sun.position, jupiter.position);
        earthMoonLagrange.update(earth.position, luna.position);
        sunMarsLagrange.update(sun.position, mars.position);

        // Update focused Lagrange point position if camera is following one
        if (camera.isFocused && camera.focusIsLagrangePoint)
        {
            // Find which Lagrange system contains the focused point by name
            std::string focusName = camera.focusedLagrangeName;
            if (focusName.find("Sun-Earth") != std::string::npos)
            {
                auto points = sunEarthLagrange.getAllPoints();
                for (auto *lp : points)
                {
                    if (lp->name == focusName)
                    {
                        camera.updateFocusedLagrangePosition(lp->position);
                        break;
                    }
                }
            }
            else if (focusName.find("Sun-Jupiter") != std::string::npos)
            {
                auto points = sunJupiterLagrange.getAllPoints();
                for (auto *lp : points)
                {
                    if (lp->name == focusName)
                    {
                        camera.updateFocusedLagrangePosition(lp->position);
                        break;
                    }
                }
            }
            else if (focusName.find("Earth-Moon") != std::string::npos)
            {
                auto points = earthMoonLagrange.getAllPoints();
                for (auto *lp : points)
                {
                    if (lp->name == focusName)
                    {
                        camera.updateFocusedLagrangePosition(lp->position);
                        break;
                    }
                }
            }
            else if (focusName.find("Sun-Mars") != std::string::npos)
            {
                auto points = sunMarsLagrange.getAllPoints();
                for (auto *lp : points)
                {
                    if (lp->name == focusName)
                    {
                        camera.updateFocusedLagrangePosition(lp->position);
                        break;
                    }
                }
            }
        }

        // Initialize camera to view Earth on first frame
        if (!cameraInitialized)
        {
            camera.initializeForEarth(earth.position, earth.displayRadius);
            camera.selectedBody = &earth;
            camera.isFocused = true;
            camera.focusIsLagrangePoint = false;
            // Store offset from Earth to camera (for wobble-free tracking)
            camera.focusOffset = camera.position - earth.position;
            cameraInitialized = true;
            std::cout << "Camera initialized focused on Earth\n";
        }

        // ====================================================================
        // Update camera position to follow focused body
        // ====================================================================
        // This must be called AFTER body/Lagrange positions are updated
        // Camera will move with the target if in focus/orbit mode
        camera.updateFollowTarget(currentJD);

        // ====================================================================
        // Set camera view matrix - MUST be after body positions AND camera.updateFollowTarget()
        // This ensures camera and bodies are rendered in sync (no jitter)
        // ====================================================================
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glm::mat4 view = camera.getViewMatrix();
        glLoadMatrixf(glm::value_ptr(view));

        // ====================================================================
        // Raycast for mouse picking (handled by camera controller)
        // Skip raycast if mouse is over UI elements
        // ====================================================================
        double raycastMouseX, raycastMouseY;
        glfwGetCursorPos(window, &raycastMouseX, &raycastMouseY);
        bool mouseOverUI = IsMouseOverUI(screenWidth, screenHeight, raycastMouseX, raycastMouseY, IsUIVisible());
        camera.updateRaycast(allBodies, window, mouseOverUI);

        // ====================================================================
        // Update measurement result if measurement mode is active
        // ====================================================================
        if (GetMeasurementMode() != MeasurementMode::None && !mouseOverUI)
        {
            glm::vec3 rayDir = camera.getMouseRayDirection();
            UpdateMeasurementResult(camera.position, rayDir, allBodies, camera.maxRayDistance);
        }

        // ====================================================================
        // Draw skybox (stars/constellations) first
        // ====================================================================
        if (IsStarTextureReady())
        {
            // Use pre-computed star texture (efficient)
            DrawSkyboxTextured(camera.position);
        }
        else
        {
            // Fall back to dynamic per-frame star rendering
            DrawSkybox(camera.position, currentJD, camera.getFront(), camera.getUp());
        }

        // ====================================================================
        // Draw orbital paths (computed from actual body positions)
        // ====================================================================
        glm::vec3 sunCenter(0.0f);

        // Planet orbits around the Sun (line width = half planet radius)
        // Orbits are computed to pass through each planet's actual position
        if (g_showOrbits)
        {
            DrawOrbit(sunCenter, mercury.position, mercury.displayRadius * 0.5f, mercury.color);
            DrawOrbit(sunCenter, venus.position, venus.displayRadius * 0.5f, venus.color);
            DrawOrbit(sunCenter, earth.position, earth.displayRadius * 0.5f, earth.color);
            DrawOrbit(sunCenter, mars.position, mars.displayRadius * 0.5f, mars.color);
            DrawOrbit(sunCenter, jupiter.position, jupiter.displayRadius * 0.5f, jupiter.color);
            DrawOrbit(sunCenter, saturn.position, saturn.displayRadius * 0.5f, saturn.color);
            DrawOrbit(sunCenter, uranus.position, uranus.displayRadius * 0.5f, uranus.color);
            DrawOrbit(sunCenter, neptune.position, neptune.displayRadius * 0.5f, neptune.color);
            DrawOrbit(sunCenter, pluto.position, pluto.displayRadius * 0.5f, pluto.color);

            // Moon orbits around their parent planets
            DrawOrbit(earth.position, luna.position, luna.displayRadius * 0.5f, luna.color, 64);
            DrawOrbit(jupiter.position, io.position, io.displayRadius * 0.5f, io.color, 64);
            DrawOrbit(jupiter.position, europa.position, europa.displayRadius * 0.5f, europa.color, 64);
            DrawOrbit(jupiter.position, ganymede.position, ganymede.displayRadius * 0.5f, ganymede.color, 64);
            DrawOrbit(jupiter.position, callisto.position, callisto.displayRadius * 0.5f, callisto.color, 64);
            DrawOrbit(saturn.position, titan.position, titan.displayRadius * 0.5f, titan.color, 64);
            DrawOrbit(neptune.position, triton.position, triton.displayRadius * 0.5f, triton.color, 64);
            DrawOrbit(pluto.position, charon.position, charon.displayRadius * 0.5f, charon.color, 64);
        }

        // ====================================================================
        // Draw orbital trails (before solid bodies for transparency)
        // ====================================================================
        for (CelestialBody *body : allBodies)
        {
            body->drawTrail();
        }

        // ====================================================================
        // Update Sun Position for Lighting
        // ====================================================================
        // All bodies are lit by the sun with inverse-square falloff
        SolarLighting::setSunPosition(sun.position);

        // ====================================================================
        // Draw all planet and moon bodies (with frustum culling and back-to-front sorting)
        // ====================================================================
        // Build render queue: frustum cull, sort by distance (furthest first), occlusion cull
        // Pass selected body so it's never culled (ensures atmosphere always renders)
        float fovRadians = glm::radians(camera.fov);
        std::vector<RenderItem> renderQueue = buildRenderQueue(allBodies,
                                                               camera.position,
                                                               camera.getFront(),
                                                               fovRadians,
                                                               true,               // Enable occlusion culling
                                                               camera.selectedBody // Selected body is never culled
        );

        // Render back-to-front (furthest objects first, so closer objects correctly overdraw)
        for (const RenderItem &item : renderQueue)
        {
            item.body->draw(currentJD, camera.position);

            // Draw city labels for Earth after rendering the planet
            if (item.body->name == "Earth" && g_economyRenderer.isInitialized())
            {
                g_economyRenderer.drawCityLabels(item.body->position,
                                                 item.body->displayRadius,
                                                 camera.position,
                                                 camera.getFront(),
                                                 camera.getUp(),
                                                 item.body->poleDirection,
                                                 item.body->primeMeridianDirection);
            }
        }

        // ====================================================================
        // Draw magnetic field lines (for bodies with magnetic field models)
        // ====================================================================
        // Only render magnetic field for the currently selected body (when enabled)
        // This is more efficient than computing/rendering for all planets
        if (g_showMagneticFields && camera.selectedBody && camera.selectedBody->hasMagneticField())
        {
            // Convert Julian Date to decimal year
            double yearFraction = 2000.0 + (currentJD - JD_J2000) / 365.25;

            CelestialBody *body = camera.selectedBody;
            static CelestialBody *lastMagneticBody = nullptr;
            static bool needsFieldUpdate = true;

            // Check if we switched to a different body
            if (body != lastMagneticBody)
            {
                needsFieldUpdate = true;
                lastMagneticBody = body;
            }

            // Update field lines if needed (only on first enable or body change)
            if (needsFieldUpdate)
            {
                // Choose resolution based on body type
                // Earth: simple dipole-like visualization with even longitude spacing
                // Jupiter: more complex field needs more detail
                // Mars: crustal anomalies need high resolution
                int numLats = 4; // Just a few latitude bands
                int numLons = 8; // Even spacing around the globe

                if (body->name == "Earth")
                {
                    // Earth: clean, simple visualization (4 lat bands × 8 longitudes)
                    numLats = 4;
                    numLons = 8;
                }
                else if (body->name == "Jupiter")
                {
                    numLats = 6;
                    numLons = 12;
                }
                else if (body->name == "Mars")
                {
                    // Mars has crustal anomalies - more detail needed
                    numLats = 8;
                    numLons = 12;
                }
                else if (body->name == "Saturn")
                {
                    // Saturn has highly axisymmetric field
                    numLats = 4;
                    numLons = 8;
                }

                body->updateFieldLines(yearFraction, numLats, numLons);

                if (!body->cachedFieldLines.empty())
                {
                    std::cout << "Generated " << body->cachedFieldLines.size() << " field lines for " << body->name
                              << "\n";
                    std::cout << "  Year: " << yearFraction << "\n";
                    std::cout << "  Display radius: " << body->displayRadius << "\n";
                    if (body->name == "Mars")
                    {
                        std::cout << "  (Note: Mars has crustal anomalies, not a global dipole)" << "\n";
                    }
                }
                needsFieldUpdate = false;
            }

            // Draw the field lines for the selected body
            body->drawMagneticFieldLines();
        }

        // ====================================================================
        // Draw planet coordinate grids (lat/long lines with labels)
        // Only for the selected body when enabled
        // ====================================================================
        if (g_showCoordinateGrids && camera.selectedBody)
        {
            camera.selectedBody->showCoordinateGrid = true;
            glm::vec3 camFront = camera.getFront();
            glm::vec3 camUp = camera.getUp();
            camera.selectedBody->drawCoordinateGrid(camera.position, camFront, camUp);
        }
        else if (camera.selectedBody)
        {
            camera.selectedBody->showCoordinateGrid = false;
        }

        // ====================================================================
        // Draw rotation axes (green = north, red = south) and equators
        // Only for the selected body when enabled
        // ====================================================================
        if (g_showRotationAxes && camera.selectedBody)
        {
            camera.selectedBody->drawRotationAxis();
            camera.selectedBody->drawEquator();
        }

        // ====================================================================
        // Draw force vectors (gravity acceleration + momentum)
        // Only for the selected body when enabled
        // ====================================================================
        if (g_showForceVectors && camera.selectedBody)
        {
            // Calculate gravitational acceleration for the selected body from all other bodies
            auto calcGravityAccel = [&](const CelestialBody &body) -> glm::vec3 {
                glm::dvec3 accel(0.0);
                for (const CelestialBody *other : allBodies)
                {
                    if (other == &body || other->mass <= 0.0)
                        continue;

                    glm::dvec3 toOther = glm::dvec3(other->position) - glm::dvec3(body.position);
                    double dist = glm::length(toOther);
                    if (dist < 0.001)
                        continue;

                    // a = GM/r² toward the other body (in display units)
                    // Convert to display units: G in SI, mass in kg, distance needs conversion
                    double displayToMeters = AU_IN_METERS / static_cast<double>(UNITS_PER_AU);
                    double distMeters = dist * displayToMeters;
                    double accelMag = G * other->mass / (distMeters * distMeters);

                    // Convert back to display units per day^2 for visualization
                    // accelMag is m/s², convert to display_units/day²
                    double metersToDisplay = 1.0 / displayToMeters;
                    double secondsPerDay = 86400.0;
                    double accelDisplay = accelMag * metersToDisplay * secondsPerDay * secondsPerDay;

                    glm::dvec3 dir = toOther / dist;
                    accel += dir * accelDisplay;
                }
                return glm::vec3(accel);
            };

            // Draw force vectors for the selected body only
            glm::vec3 gravAccel = calcGravityAccel(*camera.selectedBody);
            camera.selectedBody->drawForceVectors(gravAccel);
        }

        // ====================================================================
        // Draw barycenter markers
        // ====================================================================
        if (g_showBarycenters)
        {
            sun.drawBarycenter();     // Solar system barycenter
            earth.drawBarycenter();   // Earth-Moon barycenter
            jupiter.drawBarycenter(); // Jupiter system barycenter
            saturn.drawBarycenter();  // Saturn system barycenter
            neptune.drawBarycenter(); // Neptune system barycenter
            pluto.drawBarycenter();   // Pluto-Charon barycenter
        }

        // ====================================================================
        // Draw Lagrange points (green spheres)
        // ====================================================================
        if (g_showLagrangePoints)
        {
            sunEarthLagrange.draw();   // Sun-Earth L1-L5 (JWST at L2)
            sunJupiterLagrange.draw(); // Sun-Jupiter L1-L5 (Trojan asteroids at L4/L5)
            earthMoonLagrange.draw();  // Earth-Moon L1-L5
            sunMarsLagrange.draw();    // Sun-Mars L1-L5
        }

        // ====================================================================
        // Draw gravity grid (spacetime curvature visualization)
        // ====================================================================
        if (g_showGravityGrid)
        {
            // Calculate grid extent to encompass entire solar system
            // Find the furthest body from the sun (should be Pluto at aphelion)
            float maxDistance = 0.0f;
            for (const auto *body : allBodies)
            {
                if (body && body != &sun)
                {
                    float dist = glm::length(body->position - sun.position);
                    maxDistance = std::max(maxDistance, dist);
                }
            }

            // Add some margin beyond the furthest body
            float gridExtent = maxDistance * 1.3f;

            // Minimum extent in case everything is close
            gridExtent = std::max(gridExtent, 50.0f);

            // Update grid with gravitational warping from all bodies
            g_gravityGrid.update(gridExtent, allBodies, g_gravityGridResolution);

            // Draw the warped 3D grid with distance-based fading from camera
            g_gravityGrid.draw(camera.position);
        }

        // ====================================================================
        // Draw sun spot visualization (circle + cross at overhead position)
        // ====================================================================
        if (g_showSunSpot && camera.selectedBody)
        {
            CelestialBody *body = camera.selectedBody;
            glm::vec3 bodyCenter = body->position;
            float bodyRadius = body->displayRadius;

            // Compute sun direction from body center to sun
            glm::vec3 toSun = sun.position - bodyCenter;
            float sunDist = glm::length(toSun);
            if (sunDist > 0.001f)
            {
                glm::vec3 sunDir = glm::normalize(toSun);

                // Find intersection point on body surface (where sun is directly overhead)
                glm::vec3 overheadPoint = bodyCenter + sunDir * bodyRadius;

                // Circle radius = 1/3 of body radius
                float circleRadius = bodyRadius / 3.0f;

                // Find two perpendicular vectors to sun direction for circle plane
                glm::vec3 perp1, perp2;
                if (std::abs(sunDir.y) < 0.9f)
                {
                    // Use Y-up as reference
                    perp1 = glm::normalize(glm::cross(sunDir, glm::vec3(0.0f, 1.0f, 0.0f)));
                }
                else
                {
                    // Sun direction is nearly vertical, use X-axis as reference
                    perp1 = glm::normalize(glm::cross(sunDir, glm::vec3(1.0f, 0.0f, 0.0f)));
                }
                perp2 = glm::normalize(glm::cross(sunDir, perp1));

                // Disable lighting for debug visualization
                glDisable(GL_LIGHTING);
                glDisable(GL_TEXTURE_2D);

                // Enable blending for visibility
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                // Draw circle on surface (perpendicular to sun direction)
                glLineWidth(2.0f);
                glColor4f(1.0f, 0.8f, 0.2f, 0.9f); // Yellow/orange for sun spot
                glBegin(GL_LINE_LOOP);
                const int circleSegments = 64;
                for (int i = 0; i < circleSegments; i++)
                {
                    float angle = 2.0f * static_cast<float>(PI) * static_cast<float>(i) / circleSegments;
                    float cosA = cos(angle);
                    float sinA = sin(angle);
                    glm::vec3 circlePoint = overheadPoint + (perp1 * cosA + perp2 * sinA) * circleRadius;
                    glVertex3f(circlePoint.x, circlePoint.y, circlePoint.z);
                }
                glEnd();

                // Draw plus/cross at exact overhead position
                // Cross size relative to body radius
                float crossSize = bodyRadius * 0.05f; // 5% of body radius
                glLineWidth(3.0f);
                glColor4f(1.0f, 1.0f, 0.0f, 1.0f); // Bright yellow for cross
                glBegin(GL_LINES);
                // Horizontal line of cross
                glm::vec3 crossH1 = overheadPoint + perp1 * crossSize;
                glm::vec3 crossH2 = overheadPoint - perp1 * crossSize;
                glVertex3f(crossH1.x, crossH1.y, crossH1.z);
                glVertex3f(crossH2.x, crossH2.y, crossH2.z);
                // Vertical line of cross
                glm::vec3 crossV1 = overheadPoint + perp2 * crossSize;
                glm::vec3 crossV2 = overheadPoint - perp2 * crossSize;
                glVertex3f(crossV1.x, crossV1.y, crossV1.z);
                glVertex3f(crossV2.x, crossV2.y, crossV2.z);
                glEnd();

                // Draw arrows around circle showing path sun rays travel (for debugging surface normals)
                // Arrows point from sun toward surface (opposite of sunDir)
                const int numArrows = 32;
                float arrowLength = bodyRadius * 0.08f;   // 8% of body radius
                float arrowHeadSize = bodyRadius * 0.02f; // 2% of body radius
                glLineWidth(2.0f);
                glColor4f(1.0f, 0.6f, 0.0f, 0.9f); // Orange for arrows
                glBegin(GL_LINES);
                for (int i = 0; i < numArrows; i++)
                {
                    // Position arrow evenly around circle
                    float angle = 2.0f * static_cast<float>(PI) * static_cast<float>(i) / numArrows;
                    float cosA = cos(angle);
                    float sinA = sin(angle);
                    glm::vec3 arrowBase = overheadPoint + (perp1 * cosA + perp2 * sinA) * circleRadius;

                    // Arrow points in direction sun rays travel (from sun toward surface = -sunDir)
                    glm::vec3 rayDir = -sunDir; // Opposite of sun direction (sun -> surface)
                    glm::vec3 arrowTip = arrowBase + rayDir * arrowLength;

                    // Draw arrow shaft
                    glVertex3f(arrowBase.x, arrowBase.y, arrowBase.z);
                    glVertex3f(arrowTip.x, arrowTip.y, arrowTip.z);

                    // Draw arrowhead (small perpendicular lines at tip)
                    // Find two perpendicular vectors to arrow direction for arrowhead
                    glm::vec3 arrowDir = rayDir;
                    glm::vec3 arrowPerp1, arrowPerp2;
                    if (std::abs(arrowDir.y) < 0.9f)
                    {
                        arrowPerp1 = glm::normalize(glm::cross(arrowDir, glm::vec3(0.0f, 1.0f, 0.0f)));
                    }
                    else
                    {
                        arrowPerp1 = glm::normalize(glm::cross(arrowDir, glm::vec3(1.0f, 0.0f, 0.0f)));
                    }
                    arrowPerp2 = glm::normalize(glm::cross(arrowDir, arrowPerp1));

                    // Arrowhead points backward along arrow direction
                    glm::vec3 headBase = arrowTip - arrowDir * arrowHeadSize;
                    glm::vec3 headTip1 = headBase + arrowPerp1 * arrowHeadSize * 0.5f;
                    glm::vec3 headTip2 = headBase - arrowPerp1 * arrowHeadSize * 0.5f;
                    glm::vec3 headTip3 = headBase + arrowPerp2 * arrowHeadSize * 0.5f;
                    glm::vec3 headTip4 = headBase - arrowPerp2 * arrowHeadSize * 0.5f;

                    // Draw arrowhead (4 lines from tip to head base)
                    glVertex3f(arrowTip.x, arrowTip.y, arrowTip.z);
                    glVertex3f(headTip1.x, headTip1.y, headTip1.z);
                    glVertex3f(arrowTip.x, arrowTip.y, arrowTip.z);
                    glVertex3f(headTip2.x, headTip2.y, headTip2.z);
                    glVertex3f(arrowTip.x, arrowTip.y, arrowTip.z);
                    glVertex3f(headTip3.x, headTip3.y, headTip3.z);
                    glVertex3f(arrowTip.x, arrowTip.y, arrowTip.z);
                    glVertex3f(headTip4.x, headTip4.y, headTip4.z);
                }
                glEnd();

                glDisable(GL_BLEND);
                glEnable(GL_LIGHTING);
                glLineWidth(1.0f); // Reset
            }
        }

        // ====================================================================
        // Draw 2D UI overlay
        // ====================================================================
        int fps = UpdateFPS();

        // Get mouse position for UI interaction
        double mouseX, mouseY;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        // Prepare time control parameters for UI
        TimeControlParams timeParams;
        timeParams.currentJD = currentJD;
        timeParams.minJD = SpiceEphemeris::getEarliestAvailableTime();
        timeParams.maxJD = SpiceEphemeris::getLatestAvailableTime();
        timeParams.timeDilation = &timeDilation;
        timeParams.isPaused = g_timePaused;
        timeParams.showOrbits = g_showOrbits;
        timeParams.showRotationAxes = g_showRotationAxes;
        timeParams.showBarycenters = g_showBarycenters;
        timeParams.showLagrangePoints = g_showLagrangePoints;
        timeParams.showCoordinateGrids = g_showCoordinateGrids;
        timeParams.showMagneticFields = g_showMagneticFields;
        timeParams.showGravityGrid = g_showGravityGrid;
        timeParams.showConstellations = g_showConstellations;
        timeParams.showForceVectors = g_showForceVectors;
        timeParams.showAtmosphereLayers = g_showAtmosphereLayers;
        timeParams.showSunSpot = g_showSunSpot;
        timeParams.enableAtmosphere = g_enableAtmosphere;
        timeParams.useAtmosphereLUT = g_useAtmosphereLUT;
        timeParams.useMultiscatterLUT = g_useMultiscatterLUT;
        timeParams.gravityGridResolution = g_gravityGridResolution;
        timeParams.gravityWarpStrength = g_gravityWarpStrength;
        timeParams.currentFOV = camera.fov;
        timeParams.isFullscreen = g_isFullscreen;
        timeParams.textureResolution = Settings::getTextureResolution();

        // Surface view state
        timeParams.isInSurfaceView = camera.isInSurfaceView();
        timeParams.surfaceLatitude = glm::degrees(camera.surfaceLatitude);
        timeParams.surfaceLongitude = glm::degrees(camera.surfaceLongitude);
        timeParams.surfaceBodyName = camera.selectedBody ? camera.selectedBody->name : "";

        // ====================================================================
        // Draw measurement sphere if measurement mode is active
        // ====================================================================
        MeasurementResult measureResult = GetMeasurementResult();
        if (GetMeasurementMode() != MeasurementMode::None && measureResult.hasHit)
        {
            // Draw a small sphere at the hit point
            float sphereRadius = measureResult.hitBody->displayRadius * 0.01f; // 1% of body radius
            glm::vec3 sphereColor(1.0f, 0.5f, 0.0f);                           // Orange color
            DrawSphere(measureResult.hitPoint, sphereRadius, sphereColor, 16, 16);
        }

        // Prepare tooltip for 3D hovered body or measurement
        TooltipParams tooltip;

        // Show measurement tooltip if measurement mode is active and we have a hit
        if (GetMeasurementMode() != MeasurementMode::None && measureResult.hasHit && measureResult.hitBody)
        {
            tooltip.show = true;
            tooltip.mouseX = mouseX;
            tooltip.mouseY = mouseY;

            std::string tooltipText = "";
            if (GetMeasurementMode() == MeasurementMode::LongitudeLatitude)
            {
                // Format lat/lon in degrees
                double latDeg = glm::degrees(measureResult.latitude);
                double lonDeg = glm::degrees(measureResult.longitude);
                char latDir = latDeg >= 0 ? 'N' : 'S';
                char lonDir = lonDeg >= 0 ? 'E' : 'W';
                char buf[128];
                snprintf(buf,
                         sizeof(buf),
                         "%s\n%.4f° %c, %.4f° %c",
                         measureResult.hitBody->name.c_str(),
                         std::abs(latDeg),
                         latDir,
                         std::abs(lonDeg),
                         lonDir);
                tooltipText = buf;
            }
            else if (GetMeasurementMode() == MeasurementMode::AltitudeDepth)
            {
                // For now, show lat/lon (elevation will be added later)
                double latDeg = glm::degrees(measureResult.latitude);
                double lonDeg = glm::degrees(measureResult.longitude);
                char latDir = latDeg >= 0 ? 'N' : 'S';
                char lonDir = lonDeg >= 0 ? 'E' : 'W';
                char buf[128];
                snprintf(buf,
                         sizeof(buf),
                         "%s\n%.4f° %c, %.4f° %c\nElevation: %.1f m",
                         measureResult.hitBody->name.c_str(),
                         std::abs(latDeg),
                         latDir,
                         std::abs(lonDeg),
                         lonDir,
                         measureResult.elevation);
                tooltipText = buf;
            }
            tooltip.text = tooltipText;
        }
        else
        {
            // Show normal hover tooltip
            tooltip.show = (camera.hoveredBody != nullptr);
            if (camera.hoveredBody)
            {
                // If hovering over Earth and we found a city, show city name
                if (camera.hoveredBody->name == "Earth" && !camera.hoveredCityName.empty())
                {
                    tooltip.text = camera.hoveredCityName;
                }
                else
                {
                    tooltip.text = camera.hoveredBody->name;
                }
            }
            else
            {
                tooltip.text = "";
            }
            tooltip.mouseX = mouseX;
            tooltip.mouseY = mouseY;
        }

        // Prepare selected body info for details panel
        SelectedBodyParams selectedBodyParams;
        selectedBodyParams.body = camera.selectedBody;
        selectedBodyParams.isPlanet = false;

        // Track current Lagrange system for click handling
        LagrangeSystem *activeLagrangeSystem = nullptr;

        if (camera.selectedBody)
        {
            // Calculate axial tilt from pole direction
            // The tilt is the angle between the pole and the ECLIPTIC normal (not equatorial)
            //
            // J2000 frame is aligned with Earth's equator, so we need to account for
            // the obliquity of the ecliptic (~23.439°) to get the ecliptic normal.
            //
            // Ecliptic north pole in J2000: (0, -sin(ε), cos(ε)) where ε = 23.439°
            // In our display coords (J2000 Z→Y, J2000 Y→Z): (0, cos(ε), -sin(ε))
            constexpr float OBLIQUITY_RAD = glm::radians(23.439f); // J2000 obliquity
            glm::vec3 eclipticNormal(0.0f, cos(OBLIQUITY_RAD), -sin(OBLIQUITY_RAD));

            float dotProduct = glm::dot(camera.selectedBody->poleDirection, eclipticNormal);
            selectedBodyParams.axialTiltDegrees = glm::degrees(acos(glm::clamp(dotProduct, -1.0f, 1.0f)));

            // Calculate orbital velocity from velocity vector
            // Convert from display units/day to km/s
            // display units/day -> AU/day -> km/s
            float velMagnitude = glm::length(camera.selectedBody->velocity);
            double velAUPerDay = velMagnitude / UNITS_PER_AU;
            double velKmPerSec = velAUPerDay * 149597870.7 / 86400.0; // AU/day to km/s
            selectedBodyParams.orbitalVelocityKmS = velKmPerSec;

            // Rotation period
            selectedBodyParams.rotationPeriodHours = camera.selectedBody->rotationPeriod;

            // Check if this is a planet with Lagrange points
            std::string bodyName = camera.selectedBody->name;
            if (bodyName == "Earth")
            {
                selectedBodyParams.isPlanet = true;
                selectedBodyParams.lagrangeSystemName = "Sun-Earth";
                activeLagrangeSystem = &sunEarthLagrange;
            }
            else if (bodyName == "Jupiter")
            {
                selectedBodyParams.isPlanet = true;
                selectedBodyParams.lagrangeSystemName = "Sun-Jupiter";
                activeLagrangeSystem = &sunJupiterLagrange;
            }
            else if (bodyName == "Mars")
            {
                selectedBodyParams.isPlanet = true;
                selectedBodyParams.lagrangeSystemName = "Sun-Mars";
                activeLagrangeSystem = &sunMarsLagrange;
            }
            else if (bodyName == "Moon")
            {
                selectedBodyParams.isPlanet = true;
                selectedBodyParams.lagrangeSystemName = "Earth-Moon";
                activeLagrangeSystem = &earthMoonLagrange;
            }
            else if (bodyName == "Mercury" || bodyName == "Venus" || bodyName == "Saturn" || bodyName == "Uranus" ||
                     bodyName == "Neptune" || bodyName == "Pluto")
            {
                // Planets without dedicated Lagrange systems - show as missing
                selectedBodyParams.isPlanet = true;
                selectedBodyParams.lagrangeSystemName = "Sun-" + bodyName;
                activeLagrangeSystem = nullptr;
            }

            // Populate Lagrange point info
            const char *lpLabels[] = {"L1", "L2", "L3", "L4", "L5"};
            for (int i = 0; i < 5; i++)
            {
                selectedBodyParams.lagrangePoints[i].label = lpLabels[i];
                if (activeLagrangeSystem)
                {
                    selectedBodyParams.lagrangePoints[i].available = true;
                    auto points = activeLagrangeSystem->getAllPoints();
                    selectedBodyParams.lagrangePoints[i].position = points[i]->position;
                    selectedBodyParams.lagrangePoints[i].displayRadius = points[i]->displayRadius;
                }
                else
                {
                    selectedBodyParams.lagrangePoints[i].available = false;
                    selectedBodyParams.lagrangePoints[i].position = glm::vec3(0.0f);
                    selectedBodyParams.lagrangePoints[i].displayRadius = 1.0f;
                }
            }

            // Populate moons for planets that have them
            if (bodyName == "Earth")
            {
                selectedBodyParams.moons.push_back({&luna, "Moon"});
            }
            else if (bodyName == "Jupiter")
            {
                selectedBodyParams.moons.push_back({&io, "Io"});
                selectedBodyParams.moons.push_back({&europa, "Europa"});
                selectedBodyParams.moons.push_back({&ganymede, "Ganymede"});
                selectedBodyParams.moons.push_back({&callisto, "Callisto"});
            }
            else if (bodyName == "Saturn")
            {
                selectedBodyParams.moons.push_back({&titan, "Titan"});
            }
            else if (bodyName == "Neptune")
            {
                selectedBodyParams.moons.push_back({&triton, "Triton"});
            }
            else if (bodyName == "Pluto")
            {
                selectedBodyParams.moons.push_back({&charon, "Charon"});
            }
        }

        // Build context menu params
        ContextMenuParams contextMenu;
        contextMenu.isOpen = camera.contextMenuOpen;
        contextMenu.targetBody = camera.contextMenuBody;
        contextMenu.menuX = camera.contextMenuX;
        contextMenu.menuY = camera.contextMenuY;
        // Trail toggle is handled via contextMenuGhostingClicked
        contextMenu.followMode = camera.getFollowMode();
        // Show follow mode toggle only if focused on this specific body
        contextMenu.isFocusedOnBody =
            camera.isFocused && !camera.focusIsLagrangePoint && camera.selectedBody == camera.contextMenuBody;
        // Check if in surface view mode on this body
        contextMenu.isInSurfaceView = camera.isInSurfaceView() && camera.selectedBody == camera.contextMenuBody;

        // Draw UI and get interaction results
        UIInteraction uiResult = DrawUserInterface(screenWidth,
                                                   screenHeight,
                                                   fps,
                                                   allBodies,
                                                   timeParams,
                                                   mouseX,
                                                   mouseY,
                                                   window,
                                                   &tooltip,
                                                   camera.selectedBody ? &selectedBodyParams : nullptr,
                                                   &contextMenu);

        // Handle context menu interactions
        if (uiResult.contextMenuGhostingClicked && camera.contextMenuBody)
        {
            camera.contextMenuBody->toggleTrail();
            std::cout << "Trail " << (camera.contextMenuBody->trailEnabled ? "enabled" : "disabled")
                      << " for: " << camera.contextMenuBody->name << "\n";
        }
        // Handle follow mode toggle (before closing context menu)
        if (uiResult.followModeToggled && camera.contextMenuBody)
        {
            camera.toggleFollowMode();
            std::cout << "Camera follow mode: "
                      << (camera.getFollowMode() == CameraFollowMode::Geostationary ? "Geostationary" : "Fixed")
                      << "\n";
        }

        // Handle surface view toggle (before closing context menu)
        if (uiResult.surfaceViewToggled && camera.contextMenuBody)
        {
            if (camera.isInSurfaceView())
            {
                camera.exitSurfaceView();
            }
            else
            {
                camera.enterSurfaceView(camera.contextMenuBody);
            }
        }

        // Close context menu AFTER handling toggles
        if (uiResult.contextMenuShouldClose)
        {
            camera.contextMenuOpen = false;
            camera.contextMenuBody = nullptr;
        }

        // Handle Lagrange point click (focus on the point)
        if (uiResult.clickedLagrangeIndex >= 0 && uiResult.clickedLagrangeIndex < 5 && activeLagrangeSystem)
        {
            auto points = activeLagrangeSystem->getAllPoints();
            LagrangePoint *lp = points[uiResult.clickedLagrangeIndex];

            // Use camera controller's focus method for proper follow behavior
            camera.focusOnLagrangePoint(lp->position, lp->displayRadius, lp->name);
            std::cout << "Focused on: " << lp->name << "\n";
        }

        // Handle moon click from details panel (select and focus)
        if (uiResult.clickedMoon)
        {
            camera.selectedBody = uiResult.clickedMoon;
            camera.focusOnBody(uiResult.clickedMoon);
            std::cout << "Focused on moon: " << uiResult.clickedMoon->name << "\n";
        }

        // Handle orbiting body button click (focus on Sun for planets, parent for moons)
        if (uiResult.focusOnOrbitingBody)
        {
            camera.selectedBody = uiResult.focusOnOrbitingBody;
            camera.focusOnBody(uiResult.focusOnOrbitingBody);
            std::cout << "Focused on orbiting body: " << uiResult.focusOnOrbitingBody->name << "\n";
        }

        // Handle UI interactions
        if (uiResult.clickedBody)
        {
            camera.selectedBody = uiResult.clickedBody;
            std::cout << "Selected: " << uiResult.clickedBody->name << "\n";
        }
        if (uiResult.doubleClickedBody)
        {
            camera.selectedBody = uiResult.doubleClickedBody;
            camera.focusOnBody(uiResult.doubleClickedBody);
            std::cout << "Focused on: " << uiResult.doubleClickedBody->name << "\n";
        }

        // Handle pause/resume toggle
        if (uiResult.pauseToggled)
        {
            g_timePaused = !g_timePaused;
            std::cout << "Time " << (g_timePaused ? "paused" : "resumed") << "\n";
        }

        // Handle visibility toggles
        if (uiResult.orbitsToggled)
        {
            g_showOrbits = !g_showOrbits;
            std::cout << "Orbit lines " << (g_showOrbits ? "shown" : "hidden") << "\n";
        }
        if (uiResult.axesToggled)
        {
            g_showRotationAxes = !g_showRotationAxes;
            std::cout << "Rotation axes " << (g_showRotationAxes ? "shown" : "hidden") << "\n";
        }
        if (uiResult.barycentersToggled)
        {
            g_showBarycenters = !g_showBarycenters;
            std::cout << "Barycenters " << (g_showBarycenters ? "shown" : "hidden") << "\n";
        }
        if (uiResult.lagrangePointsToggled)
        {
            g_showLagrangePoints = !g_showLagrangePoints;
            std::cout << "Lagrange points " << (g_showLagrangePoints ? "shown" : "hidden") << "\n";
        }
        if (uiResult.coordGridsToggled)
        {
            g_showCoordinateGrids = !g_showCoordinateGrids;
            std::cout << "Coordinate grids " << (g_showCoordinateGrids ? "shown" : "hidden") << "\n";
        }
        if (uiResult.magneticFieldsToggled)
        {
            g_showMagneticFields = !g_showMagneticFields;
            std::cout << "Magnetic fields " << (g_showMagneticFields ? "shown" : "hidden") << "\n";
        }
        if (uiResult.gravityGridToggled)
        {
            g_showGravityGrid = !g_showGravityGrid;
            std::cout << "Gravity grid " << (g_showGravityGrid ? "shown" : "hidden") << "\n";
        }
        if (uiResult.constellationsToggled)
        {
            g_showConstellations = !g_showConstellations;
            std::cout << "Constellations " << (g_showConstellations ? "shown" : "hidden") << "\n";
        }
        if (uiResult.forceVectorsToggled)
        {
            g_showForceVectors = !g_showForceVectors;
            std::cout << "Force vectors " << (g_showForceVectors ? "shown" : "hidden") << "\n";
        }
        if (uiResult.atmosphereLayersToggled)
        {
            g_showAtmosphereLayers = !g_showAtmosphereLayers;
            g_earthMaterial.setShowAtmosphereLayers(g_showAtmosphereLayers);
            std::cout << "Atmosphere layers " << (g_showAtmosphereLayers ? "shown" : "hidden") << "\n";
        }
        if (uiResult.sunSpotToggled)
        {
            g_showSunSpot = !g_showSunSpot;
            std::cout << "Sun spot " << (g_showSunSpot ? "shown" : "hidden") << "\n";
        }
        if (uiResult.enableAtmosphereToggled)
        {
            g_enableAtmosphere = !g_enableAtmosphere;
            g_earthMaterial.setEnableAtmosphere(g_enableAtmosphere);
            std::cout << "Atmosphere rendering " << (g_enableAtmosphere ? "enabled" : "disabled") << "\n";
        }
        if (uiResult.useAtmosphereLUTToggled)
        {
            g_useAtmosphereLUT = !g_useAtmosphereLUT;
            std::cout << "Atmosphere transmittance LUT " << (g_useAtmosphereLUT ? "enabled" : "disabled") << "\n";
        }
        if (uiResult.useMultiscatterLUTToggled)
        {
            g_useMultiscatterLUT = !g_useMultiscatterLUT;
            std::cout << "Atmosphere multiscatter LUT " << (g_useMultiscatterLUT ? "enabled" : "disabled") << "\n";
        }
        if (uiResult.heightmapToggled)
        {
            g_earthMaterial.setUseHeightmap(!g_earthMaterial.getUseHeightmap());
            std::cout << "Heightmap effect " << (g_earthMaterial.getUseHeightmap() ? "enabled" : "disabled") << "\n";
        }
        if (uiResult.normalMapToggled)
        {
            g_earthMaterial.setUseNormalMap(!g_earthMaterial.getUseNormalMap());
            std::cout << "Normal map effect " << (g_earthMaterial.getUseNormalMap() ? "enabled" : "disabled") << "\n";
        }
        if (uiResult.roughnessToggled)
        {
            g_earthMaterial.setUseSpecular(!g_earthMaterial.getUseSpecular());
            std::cout << "Roughness/Specular effect " << (g_earthMaterial.getUseSpecular() ? "enabled" : "disabled")
                      << "\n";
        }
        // Sync atmosphere enable flag
        g_earthMaterial.setEnableAtmosphere(g_enableAtmosphere);
        if (uiResult.newGravityGridResolution >= 0)
        {
            g_gravityGridResolution = uiResult.newGravityGridResolution;
        }
        if (uiResult.newGravityWarpStrength >= 0)
        {
            g_gravityWarpStrength = uiResult.newGravityWarpStrength;
        }
        if (uiResult.newFOV >= 0)
        {
            camera.fov = uiResult.newFOV;
        }

        // Handle fullscreen toggle from UI button
        // IMPORTANT: Do this FIRST and skip other UI interactions this frame
        // to prevent mouse coordinates from changing mid-frame and triggering
        // unintended UI interactions (like atmosphere layers toggle)
        bool fullscreenJustToggled = false;
        if (uiResult.fullscreenToggled)
        {
            ToggleFullscreen(window);
            fullscreenJustToggled = true;
        }

        // Skip processing other UI interactions if fullscreen was just toggled
        // The window size changed, so mouse coordinates are now relative to new window size
        // and would cause incorrect hit detection
        if (!fullscreenJustToggled)
        {

            // Handle texture resolution change from settings UI
            if (uiResult.newTextureResolution >= 0)
            {
                TextureResolution newRes = static_cast<TextureResolution>(uiResult.newTextureResolution);
                Settings::setTextureResolution(newRes);
                // Note: Restart is required for the change to take effect
                // The UI will show a restart warning when Settings::needsRestart() returns true
            }

            // Block camera input while UI sliders are being dragged
            camera.setInputBlocked(uiResult.uiSliderDragging);

            // Process pending deselect (cancelled if UI consumed the click)
            camera.processPendingDeselect(uiResult.uiConsumedClick);
        } // End of if (!fullscreenJustToggled)

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // Cleanup
    SpiceEphemeris::cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// ============================================================================
// GLFW and OpenGL Setup
// ============================================================================

GLFWwindow *StartGLFW()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << "\n";
        return nullptr;
    }

    GLFWwindow *window = glfwCreateWindow(screenWidth, screenHeight, "Von Neumann Toy", nullptr, nullptr);

    if (!window)
    {
        std::cerr << "Failed to create GLFW window" << "\n";
        glfwTerminate();
        return nullptr;
    }

    return window;
}

void DrawSphere(const glm::vec3 &center, float radius, const glm::vec3 &color, int slices, int stacks)
{
    glPushMatrix();
    glTranslatef(center.x, center.y, center.z);
    glColor3f(color.r, color.g, color.b);

    for (int i = 0; i < stacks; ++i)
    {
        float phi1 = static_cast<float>(PI) * (-0.5f + static_cast<float>(i) / stacks);
        float phi2 = static_cast<float>(PI) * (-0.5f + static_cast<float>(i + 1) / stacks);

        float y1 = radius * sin(phi1);
        float y2 = radius * sin(phi2);
        float r1 = radius * cos(phi1);
        float r2 = radius * cos(phi2);

        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; ++j)
        {
            float theta = 2.0f * static_cast<float>(PI) * static_cast<float>(j) / slices;
            float cosTheta = cos(theta);
            float sinTheta = sin(theta);

            float x1 = r1 * cosTheta;
            float z1 = r1 * sinTheta;
            glm::vec3 n1 = glm::normalize(glm::vec3(x1, y1, z1));
            glNormal3f(n1.x, n1.y, n1.z);
            glVertex3f(x1, y1, z1);

            float x2 = r2 * cosTheta;
            float z2 = r2 * sinTheta;
            glm::vec3 n2 = glm::normalize(glm::vec3(x2, y2, z2));
            glNormal3f(n2.x, n2.y, n2.z);
            glVertex3f(x2, y2, z2);
        }
        glEnd();
    }

    glPopMatrix();
}

// Draw an orbit circle that passes through the body's current position
// The orbit plane is computed to include the body's actual position
void DrawOrbit(const glm::vec3 &center,
               const glm::vec3 &bodyPosition,
               float lineWidth,
               const glm::vec3 &color,
               int segments)
{
    // Vector from center to body
    glm::vec3 toBody = bodyPosition - center;
    float orbitRadius = glm::length(toBody);

    if (orbitRadius < 0.001f)
        return; // Skip if too close

    // Normalize the direction to body
    glm::vec3 radialDir = glm::normalize(toBody);

    // Compute orbital plane basis vectors
    // We want the orbit to be roughly in the ecliptic but tilted to pass through the body
    glm::vec3 eclipticNormal(0.0f, 1.0f, 0.0f); // Y is up

    // If the body is not in the XZ plane, compute a tilted orbital plane
    // The plane normal should be perpendicular to the radial direction
    glm::vec3 orbitNormal;

    // Cross product of radial direction with a reference to get tangent
    glm::vec3 tangent = glm::cross(eclipticNormal, radialDir);

    if (glm::length(tangent) < 0.001f)
    {
        // Body is directly above/below center, use X axis as reference
        tangent = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), radialDir);
    }
    tangent = glm::normalize(tangent);

    // The orbit normal is perpendicular to both radial and tangent
    orbitNormal = glm::cross(radialDir, tangent);
    orbitNormal = glm::normalize(orbitNormal);

    // Recompute tangent to ensure orthogonality
    tangent = glm::cross(orbitNormal, radialDir);
    tangent = glm::normalize(tangent);

    glPushMatrix();
    glTranslatef(center.x, center.y, center.z);

    // Disable lighting for line rendering
    glDisable(GL_LIGHTING);

    // Enable blending for slight transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glLineWidth(lineWidth);
    glColor4f(color.r, color.g, color.b, 0.6f); // Slightly transparent

    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; ++i)
    {
        float theta = 2.0f * static_cast<float>(PI) * static_cast<float>(i) / segments;
        float cosT = cos(theta);
        float sinT = sin(theta);

        // Point on orbit = center + radius * (cos(theta) * radialDir + sin(theta) * tangent)
        glm::vec3 point = orbitRadius * (cosT * radialDir + sinT * tangent);
        glVertex3f(point.x, point.y, point.z);
    }
    glEnd();

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glLineWidth(1.0f); // Reset line width

    glPopMatrix();
}
