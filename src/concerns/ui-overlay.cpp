#define NOMINMAX
#include "ui-overlay.h"
#include "../materials/earth/earth-material.h"
#include "../materials/earth/economy/economy-renderer.h"
#include "../materials/earth/helpers/coordinate-conversion.h"
#include "../types/celestial-body.h"
#include "app-state.h"
#include "camera-controller.h"
#include "constants.h"
#include "helpers/vulkan.h"
#include "input-controller.h"
#include "settings.h"
#include "ui-controls.h"
#include "ui-icons.h"
#include "ui-primitives.h"
#include "ui-tree.h"
#include <GLFW/glfw3.h>
// Undefine Windows macros that conflict with our functions
#ifdef DrawText
#undef DrawText
#endif
#ifdef DrawTextA
#undef DrawTextA
#endif
#ifdef DrawTextW
#undef DrawTextW
#endif
// Include font-rendering.h AFTER undefining Windows macros to prevent conflicts
#include "font-rendering.h"
#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <map>
#include <set>
#include <vector>

// ==================================
// Constants
// ==================================
static const float UI_PADDING = 10.0f;
static const float ITEM_HEIGHT = 22.0f;
static const float ITEM_PADDING = 6.0f;
static const float PANEL_PADDING = 8.0f;
static const float INDENT_WIDTH = 16.0f;
static const float ARROW_SIZE = 8.0f;

// Time dilation range (logarithmic scale)
// MIN = real-time (1 second per second = 1/86400 days per second)
// MAX = 100 days per second
static const double MIN_TIME_DILATION = 1.0 / 86400.0; // Real-time: ~0.0000116 days/sec
static const double MAX_TIME_DILATION = 100.0;

// J2000 epoch for conversions
static const double J2000_JD = 2451545.0;

// ==================================
// Character Definitions for Text Rendering
// ==================================
#include "font-rendering.h"

// ==================================
// State
// ==================================
static double g_lastFPSTime = 0.0;
static int g_frameCount = 0;
static int g_currentFPS = 0;

// Triangle counting state - manual counting for immediate mode OpenGL
static int g_currentTriangleCount = 0;
static int g_frameTriangleCount = 0;
static bool g_countingTriangles = false;
static GLenum g_currentPrimitiveType = 0;
static int g_currentPrimitiveVertexCount = 0;

static double g_lastClickTime = 0.0;
static CelestialBody *g_lastClickedBody = nullptr;
static const double DOUBLE_CLICK_THRESHOLD = 0.3;

// UI visibility state
static bool g_uiVisible = true;

// OpenGL context window for UI rendering (set by screen-renderer)
static GLFWwindow *g_openglContextWindow = nullptr;

// Interactions popup menu state
static bool g_interactionsPopupOpen = false;

// Measurement mode state
static MeasurementMode g_measurementMode = MeasurementMode::None;
static bool g_measurePopupOpen = false; // Second popup for measurement types

// Measurement result (updated each frame)
static MeasurementResult g_measurementResult =
    {false, glm::vec3(0), nullptr, 0.0, 0.0, 0.0f, false, 0.0f, 0.0f, 0.0f, 0, 0, 0};

// Shoot mode state
static bool g_shootModeActive = false;
static bool g_shootModeContextMenuOpen = false;
static float g_shootModeCrosshairX = 0.0f; // Fixed crosshair position when menu is open
static float g_shootModeCrosshairY = 0.0f;
static float g_shootModeMenuX = 0.0f; // Fixed menu position
static float g_shootModeMenuY = 0.0f;

// Timezone selector state
static int g_selectedTimezoneIndex = 7; // 7 = CST (default)
static bool g_timezoneDropdownOpen = false;

// Timezone definitions (offset in hours from UTC)
struct TimezoneInfo
{
    const char *name;   // Display name
    const char *abbrev; // Short abbreviation
    float offsetHours;  // Offset from UTC in hours
};

static const TimezoneInfo g_timezones[] = {
    {"UTC", "UTC", 0.0f},
    {"UTC-12 (Baker Island)", "UTC-12", -12.0f},
    {"UTC-11 (Samoa)", "UTC-11", -11.0f},
    {"UTC-10 (Hawaii)", "HST", -10.0f},
    {"UTC-9 (Alaska)", "AKST", -9.0f},
    {"UTC-8 (Pacific)", "PST", -8.0f},
    {"UTC-7 (Mountain)", "MST", -7.0f},
    {"UTC-6 (Central)", "CST", -6.0f},
    {"UTC-5 (Eastern)", "EST", -5.0f},
    {"UTC-4 (Atlantic)", "AST", -4.0f},
    {"UTC-3 (Buenos Aires)", "ART", -3.0f},
    {"UTC-2 (Mid-Atlantic)", "UTC-2", -2.0f},
    {"UTC-1 (Azores)", "AZOT", -1.0f},
    {"UTC+1 (Central Europe)", "CET", 1.0f},
    {"UTC+2 (Eastern Europe)", "EET", 2.0f},
    {"UTC+3 (Moscow)", "MSK", 3.0f},
    {"UTC+4 (Dubai)", "GST", 4.0f},
    {"UTC+5 (Pakistan)", "PKT", 5.0f},
    {"UTC+5:30 (India)", "IST", 5.5f},
    {"UTC+6 (Bangladesh)", "BST", 6.0f},
    {"UTC+7 (Thailand)", "ICT", 7.0f},
    {"UTC+8 (China/Singapore)", "CST", 8.0f},
    {"UTC+9 (Japan/Korea)", "JST", 9.0f},
    {"UTC+10 (Sydney)", "AEST", 10.0f},
    {"UTC+11 (Solomon Islands)", "SBT", 11.0f},
    {"UTC+12 (New Zealand)", "NZST", 12.0f},
};
static const int g_timezoneCount = sizeof(g_timezones) / sizeof(g_timezones[0]);

// ==================================
// Check if mouse is over UI
// ==================================
bool IsMouseOverUI(int screenWidth, int screenHeight, double mouseX, double mouseY, bool uiVisible)
{
    // In shoot mode, always return false (UI interactions disabled except for shoot mode context menu)
    if (g_shootModeActive)
    {
        // Only return true if mouse is over the shoot mode context menu (using fixed position)
        if (g_shootModeContextMenuOpen)
        {
            float contextMenuWidth = 160.0f;
            float contextMenuHeight = 44.0f; // Approximate
            float contextMenuX = g_shootModeMenuX;
            float contextMenuY = g_shootModeMenuY;
            if (mouseX >= contextMenuX && mouseX <= contextMenuX + contextMenuWidth && mouseY >= contextMenuY &&
                mouseY <= contextMenuY + contextMenuHeight)
            {
                return true;
            }
        }
        return false;
    }

    if (!uiVisible)
    {
        // UI is hidden, but check if mouse is over the "Show UI" button
        float hideUIButtonWidth = 80.0f;
        float hideUIButtonHeight = 28.0f;
        float hideUIButtonX = UI_PADDING;
        float hideUIButtonY = UI_PADDING;

        if (mouseX >= hideUIButtonX && mouseX <= hideUIButtonX + hideUIButtonWidth && mouseY >= hideUIButtonY &&
            mouseY <= hideUIButtonY + hideUIButtonHeight)
        {
            return true;
        }
        return false;
    }

    // Check hide UI button (top left, arrow button)
    float hideUIButtonSizeCheck = 28.0f;
    float timePanelHeightCheck = 32.0f;
    float hideUIButtonXCheck = UI_PADDING;
    float hideUIButtonYCheck = UI_PADDING;
    if (mouseX >= hideUIButtonXCheck && mouseX <= hideUIButtonXCheck + hideUIButtonSizeCheck &&
        mouseY >= hideUIButtonYCheck && mouseY <= hideUIButtonYCheck + hideUIButtonSizeCheck)
    {
        return true;
    }

    // Check time control panel (top left, approximate bounds) - only if UI is visible
    if (uiVisible)
    {
        float timePanelWidth = 650.0f; // Approximate width (includes interactions button)
        float hideUIButtonSpacing = 8.0f;
        float timePanelX = UI_PADDING + hideUIButtonSizeCheck + hideUIButtonSpacing;
        float timePanelY = UI_PADDING;
        if (mouseX >= timePanelX && mouseX <= timePanelX + timePanelWidth && mouseY >= timePanelY &&
            mouseY <= timePanelY + timePanelHeightCheck)
        {
            return true;
        }
    }

    // Check interactions popup if open (approximate bounds) - only if UI is visible
    if (uiVisible && g_interactionsPopupOpen)
    {
        float timePanelWidth = 650.0f;
        float hideUIButtonSpacing = 8.0f;
        float timePanelX = UI_PADDING + hideUIButtonSizeCheck + hideUIButtonSpacing;
        float timePanelY = UI_PADDING;
        float popupWidth = 180.0f;
        float popupHeight = 120.0f; // Approximate
        float popupX = timePanelX + timePanelWidth - popupWidth / 2.0f;
        float popupY = timePanelY + timePanelHeightCheck + 8.0f; // Below time panel
        if (mouseX >= popupX && mouseX <= popupX + popupWidth && mouseY >= popupY && mouseY <= popupY + popupHeight)
        {
            return true;
        }
    }

    // Check measure popup if open (approximate bounds) - only if UI is visible
    if (uiVisible && g_measurePopupOpen)
    {
        float timePanelWidth = 650.0f;
        float hideUIButtonSpacing = 8.0f;
        float timePanelX = UI_PADDING + hideUIButtonSizeCheck + hideUIButtonSpacing;
        float timePanelY = UI_PADDING;
        float popupWidth = 180.0f;
        float popupHeight = 120.0f; // Approximate
        float popupX = timePanelX + timePanelWidth - popupWidth / 2.0f;
        float popupY = timePanelY + timePanelHeightCheck + 8.0f; // Below time panel
        if (mouseX >= popupX && mouseX <= popupX + popupWidth && mouseY >= popupY && mouseY <= popupY + popupHeight)
        {
            return true;
        }
    }

    // Check left panel (approximate bounds - matches DrawUserInterface logic) - only if UI is visible
    if (uiVisible)
    {
        float panelX = UI_PADDING;
        // Position below time control panel
        float timePanelHeight = 32.0f;
        float panelY = UI_PADDING + timePanelHeight + UI_PADDING;
        float panelWidth = 220.0f;
        float maxPanelHeight = screenHeight - UI_PADDING * 2;

        if (mouseX >= panelX && mouseX <= panelX + panelWidth && mouseY >= panelY && mouseY <= panelY + maxPanelHeight)
        {
            return true;
        }

        // Check right details panel (if visible, approximate bounds)
        // Note: We don't know exact height without selected body, so use reasonable estimate
        float detailsPanelWidth = 200.0f;
        float detailsPanelX = screenWidth - UI_PADDING - detailsPanelWidth;
        float detailsPanelY = UI_PADDING;
        float detailsPanelHeight = 400.0f; // Reasonable estimate

        if (mouseX >= detailsPanelX && mouseX <= detailsPanelX + detailsPanelWidth && mouseY >= detailsPanelY &&
            mouseY <= detailsPanelY + detailsPanelHeight)
        {
            return true;
        }
    }

    return false;
}

// Get current UI visibility state
bool IsUIVisible()
{
    return g_uiVisible;
}

// Slider dragging state (externally accessible for ui-tree.cpp)
bool g_isDraggingSlider = false;

// Accordion state - which nodes are expanded (now managed in ui-tree.cpp)

// ==================================
// Helper Functions
// ==================================

// Convert Julian Date to calendar date string with timezone offset
static std::string jdToTimezoneString(double jd, float timezoneOffsetHours)
{
    // Apply timezone offset (convert hours to days)
    double adjustedJd = jd + (timezoneOffsetHours / 24.0);

    double z = std::floor(adjustedJd + 0.5);
    double f = (adjustedJd + 0.5) - z;

    double a;
    if (z < 2299161)
    {
        a = z;
    }
    else
    {
        double alpha = std::floor((z - 1867216.25) / 36524.25);
        a = z + 1 + alpha - std::floor(alpha / 4);
    }

    double b = a + 1524;
    double c = std::floor((b - 122.1) / 365.25);
    double d = std::floor(365.25 * c);
    double e = std::floor((b - d) / 30.6001);

    int day = static_cast<int>(b - d - std::floor(30.6001 * e));
    int month = (e < 14) ? static_cast<int>(e - 1) : static_cast<int>(e - 13);
    int year = (month > 2) ? static_cast<int>(c - 4716) : static_cast<int>(c - 4715);

    double hours = f * 24.0;
    int hour = static_cast<int>(hours);
    int minute = static_cast<int>((hours - hour) * 60);

    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    return std::string(buf);
}

// Convert Julian Date to UTC calendar date string (convenience wrapper)
static std::string jdToUtcString(double jd)
{
    return jdToTimezoneString(jd, 0.0f);
}

// Format time dilation as human-readable string
static std::string formatTimeDilation(double dilation)
{
    char buf[64];

    // Real-time threshold (within 1% of 1/86400)
    const double REALTIME = 1.0 / 86400.0;
    if (std::abs(dilation - REALTIME) / REALTIME < 0.01)
    {
        return "Real-time";
    }

    // Convert to more intuitive units
    double secondsPerSecond = dilation * 86400.0; // Convert days/sec to seconds/sec

    if (secondsPerSecond < 60.0)
    {
        // Less than 1 minute per second
        snprintf(buf, sizeof(buf), "%.0f sec/s", secondsPerSecond);
    }
    else if (secondsPerSecond < 3600.0)
    {
        // Less than 1 hour per second
        snprintf(buf, sizeof(buf), "%.1f min/s", secondsPerSecond / 60.0);
    }
    else if (secondsPerSecond < 86400.0)
    {
        // Less than 1 day per second
        snprintf(buf, sizeof(buf), "%.1f hr/s", secondsPerSecond / 3600.0);
    }
    else
    {
        // 1 or more days per second
        snprintf(buf, sizeof(buf), "%.1f day/s", dilation);
    }
    return std::string(buf);
}

// Tree building and drawing functions are now in ui-tree.cpp

// ==================================
// Initialization
// ==================================
void SetOpenGLContextWindow(GLFWwindow *window)
{
    g_openglContextWindow = window;
}

void InitUI()
{
    g_lastFPSTime = glfwGetTime();
    g_frameCount = 0;
    g_currentFPS = 0;
    g_lastClickTime = 0.0;
    g_lastClickedBody = nullptr;
    g_isDraggingSlider = false;

    // Cursors are now managed by InputController

    // Expand solar system tree by default (root, sun, and planets)
    GetExpandedNodes().insert("solar_system");
    GetExpandedNodes().insert("sun");
    GetExpandedNodes().insert("planets");
}

// ==================================
// FPS Calculation
// ==================================
int UpdateFPS()
{
    g_frameCount++;
    double currentTime = glfwGetTime();
    double elapsed = currentTime - g_lastFPSTime;

    if (elapsed >= 1.0)
    {
        g_currentFPS = static_cast<int>(g_frameCount / elapsed);
        g_frameCount = 0;
        g_lastFPSTime = currentTime;
    }

    return g_currentFPS;
}

// ==================================
// Triangle Count Functions
// ==================================
// Manual triangle counting by hooking into OpenGL calls
// We use function pointers to intercept glBegin/glEnd/glVertex calls

// Function pointer types for OpenGL functions
typedef void(APIENTRY *PFNGLBEGINPROC)(GLenum mode);
typedef void(APIENTRY *PFNGLENDPROC)(void);
typedef void(APIENTRY *PFNGLVERTEX3FPROC)(GLfloat x, GLfloat y, GLfloat z);

// Original OpenGL function pointers (will be set to actual OpenGL functions)
static PFNGLBEGINPROC glBegin_real = (PFNGLBEGINPROC)glfwGetProcAddress("glBegin");
static PFNGLENDPROC glEnd_real = (PFNGLENDPROC)glfwGetProcAddress("glEnd");
static PFNGLVERTEX3FPROC glVertex3f_real = (PFNGLVERTEX3FPROC)glfwGetProcAddress("glVertex3f");

// Helper function to calculate triangles from primitive type and vertex count
static int CalculateTriangles(GLenum primitiveType, int vertexCount)
{
    switch (primitiveType)
    {
    case GL_TRIANGLES:
        return vertexCount / 3;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        return vertexCount >= 3 ? vertexCount - 2 : 0;
    case GL_QUADS:
        return (vertexCount / 4) * 2;
    case GL_QUAD_STRIP:
        return vertexCount >= 4 ? vertexCount - 2 : 0;
    default:
        return 0;
    }
}

// Wrapper functions that count triangles
static void glBegin_counting(GLenum mode)
{
    if (g_countingTriangles)
    {
        g_currentPrimitiveType = mode;
        g_currentPrimitiveVertexCount = 0;
    }
    // TODO: Migrate UI rendering to Vulkan
    // glBegin(mode); // REMOVED - migrate to Vulkan
}

static void glVertex3f_counting(GLfloat x, GLfloat y, GLfloat z)
{
    if (g_countingTriangles)
    {
        g_currentPrimitiveVertexCount++;
    }
    glVertex3f(x, y, z);
}

static void glEnd_counting(void)
{
    if (g_countingTriangles && g_currentPrimitiveType != 0)
    {
        int triangles = CalculateTriangles(g_currentPrimitiveType, g_currentPrimitiveVertexCount);
        g_frameTriangleCount += triangles;
        g_currentPrimitiveType = 0;
        g_currentPrimitiveVertexCount = 0;
    }
    // glEnd(); // REMOVED - migrate to Vulkan
}

void StartTriangleCountQuery()
{
    // Reset counter for this frame
    g_frameTriangleCount = 0;
    g_countingTriangles = true;
}

void EndTriangleCountQuery()
{
    // Stop counting and store the result
    g_countingTriangles = false;
    g_currentTriangleCount = g_frameTriangleCount;
}

int UpdateTriangleCount()
{
    // Return the count from the last completed frame
    return g_currentTriangleCount;
}

void CountTriangles(GLenum primitiveType, int vertexCount)
{
    if (!g_countingTriangles)
    {
        return;
    }

    int triangles = 0;
    switch (primitiveType)
    {
    case GL_TRIANGLES:
        triangles = vertexCount / 3;
        break;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        triangles = vertexCount >= 3 ? vertexCount - 2 : 0;
        break;
    case GL_QUADS:
        triangles = (vertexCount / 4) * 2;
        break;
    case GL_QUAD_STRIP:
        triangles = vertexCount >= 4 ? vertexCount - 2 : 0;
        break;
    default:
        triangles = 0;
        break;
    }

    g_frameTriangleCount += triangles;
}

// ==================================
// 2D Rendering Mode
// ==================================
void BeginUI(int screenWidth, int screenHeight)
{
    // Skip OpenGL setup when building Vulkan vertices
    if (g_buildingUIVertices)
    {
        return;
    }

    // Make OpenGL context current if available
    if (g_openglContextWindow != nullptr)
    {
        glfwMakeContextCurrent(g_openglContextWindow);
    }

    // Ensure we're starting from a clean state
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Set up orthographic projection for 2D UI rendering
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, screenWidth, screenHeight, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
}

void EndUI()
{
    // Skip OpenGL cleanup when building Vulkan vertices
    if (g_buildingUIVertices)
    {
        return;
    }

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

// ==================================
// Drawing Functions
// ==================================
// DrawText, GetTextWidth, and DrawNumber are now in font-rendering.cpp
// DrawRoundedRect, DrawArrow, DrawFolderIcon, DrawPlayIcon, DrawPauseIcon,
// DrawHandIcon, DrawMeasureIcon, DrawShootIcon, DrawCrosshair are now in ui-icons.cpp and ui-primitives.cpp

// DrawSlider and DrawTooltip are now in ui-controls.cpp and ui-primitives.cpp

// Tree drawing functions are now in ui-tree.cpp

// ==================================
// Number Formatting Helpers
// ==================================
static std::string formatScientific(double value, int precision = 2)
{
    if (value == 0.0)
        return "0";

    int exponent = static_cast<int>(std::floor(std::log10(std::abs(value))));
    double mantissa = value / std::pow(10.0, exponent);

    char buf[64];
    if (exponent == 0)
    {
        snprintf(buf, sizeof(buf), "%.*f", precision, value);
    }
    else if (std::abs(exponent) <= 3)
    {
        // For small exponents, just show the number
        snprintf(buf, sizeof(buf), "%.*f", precision, value);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%.*fe%d", precision, mantissa, exponent);
    }
    return std::string(buf);
}

static std::string formatWithUnit(double value, const std::string &unit, int precision = 2)
{
    return formatScientific(value, precision) + " " + unit;
}

// Lagrange accordion expansion state - now stored in AppState
// Accessor helper macros for cleaner code
#define LAGRANGE_ACCORDION_EXPANDED (APP_STATE.uiState.lagrangeExpanded != 0)
#define MOONS_ACCORDION_EXPANDED (APP_STATE.uiState.moonsExpanded != 0)
#define SETTINGS_ACCORDION_EXPANDED (APP_STATE.uiState.settingsExpanded != 0)
#define CONTROLS_ACCORDION_EXPANDED (APP_STATE.uiState.controlsExpanded != 0)

// ==================================
// Draw Details Panel (Right Side)
// ==================================
// Returns the index of clicked Lagrange point (0-4), or -1 if none clicked
// Sets clickedMoon to the moon that was clicked (nullptr if none)
// Sets focusOnOrbitingBody to the orbiting body that was clicked (nullptr if none)
// Sets titleClicked to true if the title was clicked (to focus on selected body)
static int DrawDetailsPanel(int screenWidth,
                            int screenHeight,
                            const SelectedBodyParams *selected,
                            const std::vector<CelestialBody *> &bodies,
                            double mouseX,
                            double mouseY,
                            bool mouseClicked,
                            CelestialBody *&clickedMoon,
                            CelestialBody *&focusOnOrbitingBody,
                            bool &titleClicked)
{
    clickedMoon = nullptr;
    focusOnOrbitingBody = nullptr;
    titleClicked = false;
    if (!selected || !selected->body)
        return -1;

    const CelestialBody *body = selected->body;

    float panelWidth = 200.0f;
    float panelX = screenWidth - UI_PADDING - panelWidth;
    float panelY = UI_PADDING;

    // Calculate panel height based on content
    float lineHeight = 18.0f;
    float titleHeight = 28.0f;
    float sectionPadding = 8.0f;
    float buttonHeight = 22.0f;
    int numLines = 5; // Tilt, Rotation, Velocity, Mass, Barycenter
    if (!body->barycenter.has_value())
        numLines--;

    // Add height for orbiting body button (always shown if there's an orbiting body)
    float orbitingBodyButtonHeight = 0.0f;
    CelestialBody *orbitingBody = nullptr;
    if (body->parentBody != nullptr)
    {
        // Moon: orbiting body is parent planet
        orbitingBody = body->parentBody;
        orbitingBodyButtonHeight = buttonHeight + sectionPadding;
    }
    else
    {
        // Planet: orbiting body is the Sun
        for (CelestialBody *b : bodies)
        {
            if (b->name == "Sun")
            {
                orbitingBody = b;
                orbitingBodyButtonHeight = buttonHeight + sectionPadding;
                break;
            }
        }
    }

    float contentHeight = titleHeight + numLines * lineHeight + sectionPadding * 2 + orbitingBodyButtonHeight;

    // Add Lagrange section height if this is a planet
    float lagrangeHeight = 0.0f;
    if (selected->isPlanet)
    {
        lagrangeHeight = lineHeight; // Accordion header
        if (LAGRANGE_ACCORDION_EXPANDED)
        {
            lagrangeHeight += 5 * buttonHeight + sectionPadding; // 5 Lagrange points
        }
    }

    // Add Moons section height if this planet has moons
    float moonsHeight = 0.0f;
    if (!selected->moons.empty())
    {
        moonsHeight = lineHeight + sectionPadding; // Accordion header + separator
        if (MOONS_ACCORDION_EXPANDED)
        {
            moonsHeight += static_cast<float>(selected->moons.size()) * buttonHeight + sectionPadding;
        }
    }

    float totalHeight = contentHeight + lagrangeHeight + moonsHeight + PANEL_PADDING * 2;

    // Draw panel background
    DrawRoundedRect(panelX, panelY, panelWidth, totalHeight, 8.0f, 0.12f, 0.12f, 0.14f, 0.85f);

    float currentY = panelY + PANEL_PADDING;
    float labelX = panelX + PANEL_PADDING;

    // ==================================
    // Title (Body Name) - Clickable Button
    // ==================================
    float titleX = panelX + PANEL_PADDING;
    float titleY = currentY;
    float titleW = panelWidth - PANEL_PADDING * 2;
    float titleH = titleHeight - 4;

    bool isTitleHovering =
        (mouseX >= titleX && mouseX <= titleX + titleW && mouseY >= titleY && mouseY <= titleY + titleH);

    // Draw button background with hover effect
    float titleBgR = body->color.r * 0.4f + 0.1f;
    float titleBgG = body->color.g * 0.4f + 0.1f;
    float titleBgB = body->color.b * 0.4f + 0.1f;
    if (isTitleHovering)
    {
        titleBgR = (glm::min)(1.0f, titleBgR + 0.15f);
        titleBgG = (glm::min)(1.0f, titleBgG + 0.15f);
        titleBgB = (glm::min)(1.0f, titleBgB + 0.15f);
    }

    DrawRoundedRect(titleX, titleY, titleW, titleH, 4.0f, titleBgR, titleBgG, titleBgB, 0.9f);

    float titleTextWidth = GetTextWidth(body->name, 1.0f);
    float titleTextX = panelX + (panelWidth - titleTextWidth) / 2;
    DrawText(titleTextX, titleY + 6, body->name, 1.0f, 0.95f, 0.95f, 0.95f);

    // Handle title click - focus on selected body
    if (isTitleHovering && mouseClicked)
    {
        titleClicked = true;
    }

    currentY += titleHeight + sectionPadding;

    // ==================================
    // Orbiting Body Button
    // ==================================
    if (orbitingBody != nullptr)
    {
        float orbitBtnX = labelX;
        float orbitBtnY = currentY;
        float orbitBtnW = panelWidth - PANEL_PADDING * 2;
        float orbitBtnH = buttonHeight - 2;

        bool isOrbitBtnHovering = (mouseX >= orbitBtnX && mouseX <= orbitBtnX + orbitBtnW && mouseY >= orbitBtnY &&
                                   mouseY <= orbitBtnY + orbitBtnH);

        // Button background
        glm::vec3 orbitColor = orbitingBody->color;
        float orbitBgR = isOrbitBtnHovering ? orbitColor.r * 0.4f + 0.2f : orbitColor.r * 0.2f + 0.1f;
        float orbitBgG = isOrbitBtnHovering ? orbitColor.g * 0.4f + 0.2f : orbitColor.g * 0.2f + 0.1f;
        float orbitBgB = isOrbitBtnHovering ? orbitColor.b * 0.4f + 0.2f : orbitColor.b * 0.2f + 0.1f;
        DrawRoundedRect(orbitBtnX, orbitBtnY, orbitBtnW, orbitBtnH, 3.0f, orbitBgR, orbitBgG, orbitBgB, 0.9f);

        // Button text - show orbiting body name
        std::string orbitText = "Focus on " + orbitingBody->name;
        float orbitTextWidth = GetTextWidth(orbitText, 0.75f);
        float orbitTextX = orbitBtnX + (orbitBtnW - orbitTextWidth) / 2;
        DrawText(orbitTextX, orbitBtnY + 4, orbitText, 0.75f, 0.9f, 0.9f, 0.95f);

        // Handle click
        if (isOrbitBtnHovering && mouseClicked)
        {
            focusOnOrbitingBody = orbitingBody;
        }

        currentY += buttonHeight + sectionPadding;
    }

    // ==================================
    // Axial Tilt
    // ==================================
    DrawText(labelX, currentY, "Axial Tilt:", 0.75f, 0.6f, 0.6f, 0.65f);
    char tiltBuf[32];
    snprintf(tiltBuf, sizeof(tiltBuf), "%.2f deg", selected->axialTiltDegrees);
    float tiltWidth = GetTextWidth(tiltBuf, 0.75f);
    DrawText(panelX + panelWidth - PANEL_PADDING - tiltWidth, currentY, tiltBuf, 0.75f, 0.9f, 0.9f, 0.95f);
    currentY += lineHeight;

    // ==================================
    // Rotation Period
    // ==================================
    DrawText(labelX, currentY, "Rotation:", 0.75f, 0.6f, 0.6f, 0.65f);
    char rotBuf[32];
    if (selected->rotationPeriodHours < 24.0)
    {
        snprintf(rotBuf, sizeof(rotBuf), "%.2f hrs", selected->rotationPeriodHours);
    }
    else
    {
        snprintf(rotBuf, sizeof(rotBuf), "%.2f days", selected->rotationPeriodHours / 24.0);
    }
    float rotWidth = GetTextWidth(rotBuf, 0.75f);
    DrawText(panelX + panelWidth - PANEL_PADDING - rotWidth, currentY, rotBuf, 0.75f, 0.9f, 0.9f, 0.95f);
    currentY += lineHeight;

    // ==================================
    // Orbital Velocity
    // ==================================
    DrawText(labelX, currentY, "Velocity:", 0.75f, 0.6f, 0.6f, 0.65f);
    char velBuf[32];
    snprintf(velBuf, sizeof(velBuf), "%.2f km/s", selected->orbitalVelocityKmS);
    float velWidth = GetTextWidth(velBuf, 0.75f);
    DrawText(panelX + panelWidth - PANEL_PADDING - velWidth, currentY, velBuf, 0.75f, 0.9f, 0.9f, 0.95f);
    currentY += lineHeight;

    // ==================================
    // Mass
    // ==================================
    DrawText(labelX, currentY, "Mass:", 0.75f, 0.6f, 0.6f, 0.65f);
    std::string massStr = formatWithUnit(body->mass, "kg");
    float massWidth = GetTextWidth(massStr, 0.7f);
    DrawText(panelX + panelWidth - PANEL_PADDING - massWidth, currentY, massStr, 0.7f, 0.9f, 0.9f, 0.95f);
    currentY += lineHeight;

    // ==================================
    // Barycenter Distance (if available)
    // ==================================
    if (body->barycenter.has_value())
    {
        DrawText(labelX, currentY, "Barycenter:", 0.75f, 0.6f, 0.6f, 0.65f);
        float baryDist = glm::length(body->barycenter.value() - body->position);
        char baryBuf[32];
        if (baryDist < 0.01f)
        {
            snprintf(baryBuf, sizeof(baryBuf), "%.4f units", baryDist);
        }
        else
        {
            snprintf(baryBuf, sizeof(baryBuf), "%.2f units", baryDist);
        }
        float baryWidth = GetTextWidth(baryBuf, 0.75f);
        DrawText(panelX + panelWidth - PANEL_PADDING - baryWidth, currentY, baryBuf, 0.75f, 0.9f, 0.9f, 0.95f);
        currentY += lineHeight;
    }

    // ==================================
    // Lagrange Points Accordion (for planets only)
    // ==================================
    int clickedLagrange = -1;

    if (selected->isPlanet)
    {
        currentY += sectionPadding / 2;

        // TODO: Migrate UI rendering to Vulkan
        // Separator
        // glColor4f(0.3f, 0.3f, 0.35f, 0.8f); // REMOVED - migrate to Vulkan uniform buffer
        // glBegin(GL_LINES); // REMOVED - migrate to Vulkan
        // glVertex2f(panelX + PANEL_PADDING, currentY); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex2f(panelX + panelWidth - PANEL_PADDING, currentY); // REMOVED - migrate to Vulkan vertex buffer
        // glEnd(); // REMOVED - migrate to Vulkan
        currentY += sectionPadding / 2;

        // Accordion header
        float headerY = currentY;
        float headerHeight = lineHeight;
        bool isHoveringHeader = (mouseX >= labelX && mouseX <= panelX + panelWidth - PANEL_PADDING &&
                                 mouseY >= headerY && mouseY <= headerY + headerHeight);

        // Draw accordion header
        if (DrawAccordionHeader(labelX,
                                headerY,
                                panelX + panelWidth - PANEL_PADDING - labelX,
                                headerHeight,
                                "Lagrange Points",
                                LAGRANGE_ACCORDION_EXPANDED,
                                mouseX,
                                mouseY,
                                mouseClicked))
        {
            APP_STATE.uiState.lagrangeExpanded = APP_STATE.uiState.lagrangeExpanded ? 0 : 1;
        }

        currentY += headerHeight;

        // Draw Lagrange points if expanded
        if (LAGRANGE_ACCORDION_EXPANDED)
        {
            const char *lagrangeLabels[] = {"L1", "L2", "L3", "L4", "L5"};

            for (int i = 0; i < 5; i++)
            {
                float itemX = labelX + 8;
                float itemY = currentY;
                float itemWidth = panelWidth - PANEL_PADDING * 2 - 8;
                float itemHeight = buttonHeight - 2;

                const LagrangePointInfo &lp = selected->lagrangePoints[i];

                bool isHovering =
                    (mouseX >= itemX && mouseX <= itemX + itemWidth && mouseY >= itemY && mouseY <= itemY + itemHeight);

                if (lp.available)
                {
                    // Draw clickable button
                    float bgBrightness = isHovering ? 0.28f : 0.2f;
                    DrawRoundedRect(itemX, itemY, itemWidth, itemHeight, 3.0f, 0.15f, bgBrightness, 0.15f, 0.9f);

                    // Label
                    DrawText(itemX + 6, itemY + 4, lp.label, 0.75f, 0.3f, 0.9f, 0.3f);

                    // "Go" indicator on the right
                    if (isHovering)
                    {
                        float goWidth = GetTextWidth(">", 0.75f);
                        DrawText(itemX + itemWidth - goWidth - 6, itemY + 4, ">", 0.75f, 0.5f, 1.0f, 0.5f);
                    }

                    // Handle click
                    if (isHovering && mouseClicked)
                    {
                        clickedLagrange = i;
                    }
                }
                else
                {
                    // Draw disabled state with "missing" text
                    DrawText(itemX + 6, itemY + 4, lp.label, 0.75f, 0.4f, 0.4f, 0.45f);

                    float missingWidth = GetTextWidth("missing", 0.65f);
                    DrawText(itemX + itemWidth - missingWidth - 6, itemY + 5, "missing", 0.65f, 0.5f, 0.4f, 0.4f);
                }

                currentY += buttonHeight;
            }
        }
    }

    // ==================================
    // Moons Accordion (for planets with moons)
    // ==================================
    if (!selected->moons.empty())
    {
        currentY += sectionPadding / 2;

        // TODO: Migrate UI rendering to Vulkan
        // Separator
        // glColor4f(0.3f, 0.3f, 0.35f, 0.8f); // REMOVED - migrate to Vulkan uniform buffer
        // glBegin(GL_LINES); // REMOVED - migrate to Vulkan
        // glVertex2f(panelX + PANEL_PADDING, currentY); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex2f(panelX + panelWidth - PANEL_PADDING, currentY); // REMOVED - migrate to Vulkan vertex buffer
        // glEnd(); // REMOVED - migrate to Vulkan
        currentY += sectionPadding / 2;

        // Accordion header
        float headerY = currentY;
        float headerHeight = lineHeight;
        bool isHoveringHeader = (mouseX >= labelX && mouseX <= panelX + panelWidth - PANEL_PADDING &&
                                 mouseY >= headerY && mouseY <= headerY + headerHeight);

        // Draw accordion header with moon count
        std::string moonHeader = "Moons (" + std::to_string(selected->moons.size()) + ")";
        if (DrawAccordionHeader(labelX,
                                headerY,
                                panelX + panelWidth - PANEL_PADDING - labelX,
                                headerHeight,
                                moonHeader,
                                MOONS_ACCORDION_EXPANDED,
                                mouseX,
                                mouseY,
                                mouseClicked))
        {
            APP_STATE.uiState.moonsExpanded = APP_STATE.uiState.moonsExpanded ? 0 : 1;
        }

        currentY += headerHeight;

        // Draw moons if expanded
        if (MOONS_ACCORDION_EXPANDED)
        {
            for (const MoonInfo &moon : selected->moons)
            {
                float itemX = labelX + 8;
                float itemY = currentY;
                float itemWidth = panelWidth - PANEL_PADDING * 2 - 8;
                float itemHeight = buttonHeight - 2;

                bool isHovering =
                    (mouseX >= itemX && mouseX <= itemX + itemWidth && mouseY >= itemY && mouseY <= itemY + itemHeight);

                // Draw clickable button with moon's color
                glm::vec3 moonColor = moon.body->color;
                float bgR = isHovering ? moonColor.r * 0.4f + 0.15f : moonColor.r * 0.2f + 0.1f;
                float bgG = isHovering ? moonColor.g * 0.4f + 0.15f : moonColor.g * 0.2f + 0.1f;
                float bgB = isHovering ? moonColor.b * 0.4f + 0.15f : moonColor.b * 0.2f + 0.1f;
                DrawRoundedRect(itemX, itemY, itemWidth, itemHeight, 3.0f, bgR, bgG, bgB, 0.9f);

                // Moon name
                DrawText(itemX + 6, itemY + 4, moon.name, 0.75f, 0.9f, 0.9f, 0.95f);

                // "Go" indicator on the right
                if (isHovering)
                {
                    float goWidth = GetTextWidth(">", 0.75f);
                    DrawText(itemX + itemWidth - goWidth - 6, itemY + 4, ">", 0.75f, 0.5f, 1.0f, 0.5f);
                }

                // Handle click - focus on moon
                if (isHovering && mouseClicked)
                {
                    clickedMoon = moon.body;
                }

                currentY += buttonHeight;
            }
        }
    }

    return clickedLagrange;
}

// ==================================
// Draw Context Menu
// ==================================
// Slider dragging states (file-level for UI interaction tracking)
static bool g_contextMenuSliderDragging = false;
static bool g_fovSliderDragging = false;
static bool g_gridResSliderDragging = false;
static bool g_warpStrengthSliderDragging = false;

static void DrawContextMenu(const ContextMenuParams *contextMenu,
                            int screenWidth,
                            int screenHeight,
                            double mouseX,
                            double mouseY,
                            bool mouseClicked,
                            bool mouseDown,
                            bool &trailToggled,
                            bool &shouldClose,
                            bool &followModeToggled,
                            bool &surfaceViewToggled)
{
    trailToggled = false;
    shouldClose = false;
    followModeToggled = false;
    surfaceViewToggled = false;

    if (!contextMenu || !contextMenu->isOpen || !contextMenu->targetBody)
    {
        g_contextMenuSliderDragging = false;
        return;
    }

    const float menuWidth = 180.0f;
    const float buttonHeight = 28.0f;
    const float sliderHeight = 44.0f;
    const float padding = 6.0f;

    // Calculate menu height - add extra button if focused on this body
    float menuHeight = buttonHeight + sliderHeight + padding * 3;
    if (contextMenu->isFocusedOnBody)
    {
        menuHeight += buttonHeight + padding; // Add space for follow mode toggle
    }

    // Position menu to the right of click point, clamped to screen
    float menuPosX = static_cast<float>(contextMenu->menuX) + 10.0f;
    float menuPosY = static_cast<float>(contextMenu->menuY) - menuHeight / 2.0f;

    // Clamp to screen bounds
    if (menuPosX + menuWidth > screenWidth - 10)
    {
        menuPosX = static_cast<float>(contextMenu->menuX) - menuWidth - 10.0f;
    }
    if (menuPosY < 10)
        menuPosY = 10;
    if (menuPosY + menuHeight > screenHeight - 10)
    {
        menuPosY = screenHeight - menuHeight - 10;
    }

    // Draw menu background
    DrawRoundedRect(menuPosX, menuPosY, menuWidth, menuHeight, 6.0f, 0.18f, 0.18f, 0.22f, 0.95f);

    // Draw border
    // TODO: Migrate UI rendering to Vulkan
    // Temporarily restored OpenGL calls so UI renders
    glColor4f(0.4f, 0.4f, 0.5f, 0.9f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(menuPosX + 6, menuPosY);
    glVertex2f(menuPosX + menuWidth - 6, menuPosY);
    glVertex2f(menuPosX + menuWidth, menuPosY + 6);
    glVertex2f(menuPosX + menuWidth, menuPosY + menuHeight - 6);
    glVertex2f(menuPosX + menuWidth - 6, menuPosY + menuHeight);
    glVertex2f(menuPosX + 6, menuPosY + menuHeight);
    glVertex2f(menuPosX, menuPosY + menuHeight - 6);
    glVertex2f(menuPosX, menuPosY + 6);
    glEnd();

    float currentY = menuPosY + padding;

    // ==================================
    // Toggle Trail button
    // ==================================
    float buttonX = menuPosX + padding;
    float buttonY = currentY;
    float buttonW = menuWidth - padding * 2;

    bool isButtonHovering =
        (mouseX >= buttonX && mouseX <= buttonX + buttonW && mouseY >= buttonY && mouseY <= buttonY + buttonHeight);

    // Button background - different color if trail is already enabled
    bool trailIsEnabled = contextMenu->targetBody->trailEnabled;
    if (isButtonHovering)
    {
        DrawRoundedRect(buttonX,
                        buttonY,
                        buttonW,
                        buttonHeight,
                        4.0f,
                        trailIsEnabled ? 0.4f : 0.2f,
                        trailIsEnabled ? 0.25f : 0.35f,
                        trailIsEnabled ? 0.2f : 0.2f,
                        0.9f);
    }
    else
    {
        DrawRoundedRect(buttonX,
                        buttonY,
                        buttonW,
                        buttonHeight,
                        4.0f,
                        trailIsEnabled ? 0.3f : 0.15f,
                        trailIsEnabled ? 0.2f : 0.25f,
                        trailIsEnabled ? 0.15f : 0.15f,
                        0.85f);
    }

    // Button text
    std::string buttonText = trailIsEnabled ? "Disable Trail" : "Enable Trail";
    float textWidth = GetTextWidth(buttonText, 0.8f);
    float textX = buttonX + (buttonW - textWidth) / 2;
    DrawText(textX,
             buttonY + 6,
             buttonText,
             0.8f,
             trailIsEnabled ? 1.0f : 0.6f,
             trailIsEnabled ? 0.7f : 0.9f,
             trailIsEnabled ? 0.6f : 0.6f);

    // Handle button click
    if (isButtonHovering && mouseClicked)
    {
        trailToggled = true;
        // Don't close menu on trail toggle
    }

    currentY += buttonHeight + padding;

    // ==================================
    // Follow Mode Toggle (only shown when focused on this body, not in surface view)
    // ==================================
    if (contextMenu->isFocusedOnBody && !contextMenu->isInSurfaceView)
    {
        float followBtnY = currentY;

        bool isFollowBtnHovering = (mouseX >= buttonX && mouseX <= buttonX + buttonW && mouseY >= followBtnY &&
                                    mouseY <= followBtnY + buttonHeight);

        // Check if currently in geostationary mode
        bool isGeostationary = (contextMenu->followMode == CameraFollowMode::Geostationary);

        // Button background - blue tint for geostationary
        if (isFollowBtnHovering)
        {
            DrawRoundedRect(buttonX,
                            followBtnY,
                            buttonW,
                            buttonHeight,
                            4.0f,
                            isGeostationary ? 0.2f : 0.25f,
                            isGeostationary ? 0.35f : 0.25f,
                            isGeostationary ? 0.45f : 0.35f,
                            0.9f);
        }
        else
        {
            DrawRoundedRect(buttonX,
                            followBtnY,
                            buttonW,
                            buttonHeight,
                            4.0f,
                            isGeostationary ? 0.15f : 0.2f,
                            isGeostationary ? 0.25f : 0.2f,
                            isGeostationary ? 0.35f : 0.25f,
                            0.85f);
        }

        // Button text
        std::string followText = isGeostationary ? "Geostationary" : "Fixed";
        float followTextWidth = GetTextWidth(followText, 0.8f);
        float followTextX = buttonX + (buttonW - followTextWidth) / 2;
        DrawText(followTextX,
                 followBtnY + 6,
                 followText,
                 0.8f,
                 isGeostationary ? 0.6f : 0.9f,
                 isGeostationary ? 0.85f : 0.9f,
                 isGeostationary ? 1.0f : 0.9f);

        // Handle button click
        if (isFollowBtnHovering && mouseClicked)
        {
            followModeToggled = true;
            // Don't close menu on toggle
        }

        currentY += buttonHeight + padding;
    }

    // ==================================
    // Surface View Toggle
    // ==================================
    {
        float surfaceBtnY = currentY;

        bool isSurfaceBtnHovering = (mouseX >= buttonX && mouseX <= buttonX + buttonW && mouseY >= surfaceBtnY &&
                                     mouseY <= surfaceBtnY + buttonHeight);

        bool isInSurfaceView = contextMenu->isInSurfaceView;

        // Button background - green tint when in surface view
        if (isSurfaceBtnHovering)
        {
            DrawRoundedRect(buttonX,
                            surfaceBtnY,
                            buttonW,
                            buttonHeight,
                            4.0f,
                            isInSurfaceView ? 0.15f : 0.25f,
                            isInSurfaceView ? 0.35f : 0.25f,
                            isInSurfaceView ? 0.2f : 0.35f,
                            0.9f);
        }
        else
        {
            DrawRoundedRect(buttonX,
                            surfaceBtnY,
                            buttonW,
                            buttonHeight,
                            4.0f,
                            isInSurfaceView ? 0.1f : 0.2f,
                            isInSurfaceView ? 0.3f : 0.2f,
                            isInSurfaceView ? 0.15f : 0.25f,
                            0.85f);
        }

        // Button text
        std::string surfaceText = isInSurfaceView ? "Exit Surface" : "View from Surface";
        float surfaceTextWidth = GetTextWidth(surfaceText, 0.8f);
        float surfaceTextX = buttonX + (buttonW - surfaceTextWidth) / 2;
        DrawText(surfaceTextX,
                 surfaceBtnY + 6,
                 surfaceText,
                 0.8f,
                 isInSurfaceView ? 0.6f : 0.9f,
                 isInSurfaceView ? 1.0f : 0.9f,
                 isInSurfaceView ? 0.7f : 0.9f);

        // Handle button click
        if (isSurfaceBtnHovering && mouseClicked)
        {
            surfaceViewToggled = true;
            shouldClose = true; // Close menu when entering/exiting surface view
        }

        currentY += buttonHeight + padding;
    }

    // ==================================
    // Click outside menu closes it (but not while dragging slider)
    // ==================================
    bool clickedOutside =
        mouseClicked && !g_contextMenuSliderDragging &&
        (mouseX < menuPosX || mouseX > menuPosX + menuWidth || mouseY < menuPosY || mouseY > menuPosY + menuHeight);
    if (clickedOutside)
    {
        shouldClose = true;
    }
}

// ==================================
// Main UI Drawing Function
// ==================================
UIInteraction DrawUserInterface(int screenWidth,
                                int screenHeight,
                                int fps,
                                int triangleCount,
                                const std::vector<CelestialBody *> &bodies,
                                const TimeControlParams &timeParams,
                                double mouseX,
                                double mouseY,
                                GLFWwindow *window,
                                const TooltipParams *tooltip,
                                const SelectedBodyParams *selectedBody,
                                const ContextMenuParams *contextMenu)
{
    UIInteraction result = {
        nullptr, // clickedBody
        nullptr, // doubleClickedBody
        nullptr, // hoveredBody
        -1,      // clickedLagrangeIndex
        nullptr, // clickedMoon
        nullptr, // focusOnOrbitingBody
        false,   // contextMenuGhostingClicked (now trail toggle)
        false,   // contextMenuShouldClose
        false,   // pauseToggled
        false,   // orbitsToggled
        false,   // axesToggled
        false,   // barycentersToggled
        false,   // lagrangePointsToggled
        false,   // coordGridsToggled
        false,   // magneticFieldsToggled
        false,   // gravityGridToggled
        false,   // constellationsToggled
        false,   // constellationGridToggled
        false,   // constellationFiguresToggled
        false,   // constellationBoundsToggled
        false,   // forceVectorsToggled
        false,   // sunSpotToggled
        false,   // wireframeToggled
        false,   // voxelWireframeToggled
        false,   // atmosphereLayersToggled
        false,   // fxaaToggled
        false,   // vsyncToggled
        false,   // citiesToggled
        false,   // heightmapToggled
        false,   // normalMapToggled
        false,   // roughnessToggled
        -1,      // newGravityGridResolution
        -1.0f,   // newGravityWarpStrength
        -1.0f,   // newFOV
        false,   // uiConsumedClick
        false,   // uiSliderDragging
        false,   // fovSliderDragging
        false,   // fullscreenToggled
        -1,      // newTextureResolution
        false,   // followModeToggled
        false,   // surfaceViewToggled
        false    // uiHideToggled
    };

    BeginUI(screenWidth, screenHeight);

    // Get mouse state from InputController
    const InputState &inputState = g_input.getState();
    bool mouseDown = inputState.mouseButtonDown[0]; // Left button
    bool mouseClicked = inputState.mouseClicked;    // Left button click (press + release)

    // ==================================
    // Time Control Panel (Top Left)
    // ==================================
    // Calculate panel dimensions with fixed widths to prevent layout shifts
    const TimezoneInfo &selectedTz = g_timezones[g_selectedTimezoneIndex];
    std::string currentEpoch = jdToTimezoneString(timeParams.currentJD, selectedTz.offsetHours);
    std::string dilationStr = formatTimeDilation(*timeParams.timeDilation);

    // Use fixed widths based on maximum expected string lengths
    // Date format: "YYYY-MM-DD HH:MM" - use worst case "9999-12-31 23:59"
    float dateWidth = GetTextWidth("9999-12-31 23:59", 0.85f);
    // Timezone dropdown width (fixed width for abbreviation like "UTC-12")
    float tzDropdownWidth = 60.0f;
    float tzDropdownGap = 6.0f;
    float dilationLabelWidth = GetTextWidth("Time Speed: ", 0.75f);
    // Time dilation format varies, use a reasonable maximum like "100.0 day/s"
    float dilationValueWidth = GetTextWidth("100.0 day/s", 0.75f);

    float sliderWidth = 200.0f;        // Fixed slider width
    float playPauseBtnSize = 24.0f;    // Size of play/pause button
    float interactionsBtnSize = 24.0f; // Size of interactions button
    float timePanelPadding = 12.0f;
    float sliderGap = 6.0f; // Gap before/after slider
    float timePanelHeight = 32.0f;
    float timePanelWidth = dateWidth + tzDropdownGap + tzDropdownWidth + timePanelPadding * 2 + dilationLabelWidth +
                           sliderGap + sliderWidth + sliderGap + dilationValueWidth + timePanelPadding +
                           playPauseBtnSize + timePanelPadding + interactionsBtnSize + timePanelPadding;

    // Hide UI button (arrow) - positioned to the left of time panel at top
    float hideUIButtonSize = 28.0f;                                         // Square button
    float hideUIButtonSpacing = 8.0f;                                       // Space between button and time panel
    float timePanelX = UI_PADDING + hideUIButtonSize + hideUIButtonSpacing; // Position after hide button
    float timePanelY = UI_PADDING;                                          // Top of screen

    float hideUIButtonX = UI_PADDING;
    float hideUIButtonY = UI_PADDING; // Top-left corner

    // ==================================
    // Hide UI Button (arrow icon)
    // ==================================
    bool isHideUIHovering = (mouseX >= hideUIButtonX && mouseX <= hideUIButtonX + hideUIButtonSize &&
                             mouseY >= hideUIButtonY && mouseY <= hideUIButtonY + hideUIButtonSize);

    // Button background
    if (isHideUIHovering)
    {
        DrawRoundedRect(hideUIButtonX,
                        hideUIButtonY,
                        hideUIButtonSize,
                        hideUIButtonSize,
                        4.0f,
                        0.35f,
                        0.45f,
                        0.6f,
                        0.95f);
    }
    else
    {
        DrawRoundedRect(hideUIButtonX,
                        hideUIButtonY,
                        hideUIButtonSize,
                        hideUIButtonSize,
                        4.0f,
                        0.25f,
                        0.3f,
                        0.4f,
                        0.9f);
    }

    // Draw arrow icon (left arrow when UI visible, right arrow when hidden)
    float arrowSize = hideUIButtonSize * 0.5f;
    float arrowX = hideUIButtonX + (hideUIButtonSize - arrowSize) / 2.0f;
    float arrowY = hideUIButtonY + (hideUIButtonSize - arrowSize) / 2.0f;

    if (g_uiVisible)
    {
        // Left arrow (<) - click to hide UI
        DrawLeftArrow(arrowX, arrowY, arrowSize, 0.95f, 0.95f, 0.95f);
    }
    else
    {
        // Right arrow (>) - click to show UI
        DrawArrow(arrowX, arrowY, arrowSize, false, 0.95f, 0.95f, 0.95f);
    }

    // Handle button click
    if (mouseClicked && isHideUIHovering)
    {
        g_uiVisible = !g_uiVisible;
        result.uiHideToggled = true;
    }

    // Only draw time controls if UI is visible
    if (!g_uiVisible)
    {
        // UI is hidden - only the arrow button is shown (already drawn above)
        // Continue to draw shoot mode and other overlays that should work when UI is hidden
    }
    else
    {
        // Draw time control panel when UI is visible

        // Draw panel background
        DrawRoundedRect(timePanelX, timePanelY, timePanelWidth, timePanelHeight, 6.0f, 0.12f, 0.12f, 0.14f, 0.85f);

        // Draw date on the left (using fixed width, so position is stable)
        float dateX = timePanelX + timePanelPadding;
        float dateY = timePanelY + (timePanelHeight - 20.0f) / 2.0f;
        DrawText(dateX, dateY, currentEpoch, 0.85f, 0.9f, 0.9f, 0.95f);

        // ==================================
        // Timezone Dropdown (next to date)
        // ==================================
        float tzDropdownX = dateX + dateWidth + tzDropdownGap;
        float tzDropdownY = timePanelY + (timePanelHeight - 20.0f) / 2.0f;
        float tzDropdownH = 20.0f;

        bool isTzDropdownHovering = (mouseX >= tzDropdownX && mouseX <= tzDropdownX + tzDropdownWidth &&
                                     mouseY >= tzDropdownY && mouseY <= tzDropdownY + tzDropdownH);

        if (isTzDropdownHovering)
        {
            g_input.setCursor(CursorType::Pointer);
        }

        // Dropdown button background
        DrawRoundedRect(tzDropdownX,
                        tzDropdownY,
                        tzDropdownWidth,
                        tzDropdownH,
                        3.0f,
                        isTzDropdownHovering ? 0.22f : 0.18f,
                        isTzDropdownHovering ? 0.22f : 0.18f,
                        isTzDropdownHovering ? 0.27f : 0.22f,
                        0.95f);

        // Dropdown text (timezone abbreviation)
        DrawText(tzDropdownX + 5, tzDropdownY + 2, selectedTz.abbrev, 0.75f, 0.85f, 0.85f, 0.9f);

        // Dropdown arrow
        float tzArrowSize = 8.0f;
        float tzArrowX = tzDropdownX + tzDropdownWidth - tzArrowSize - 5;
        float tzArrowY = tzDropdownY + (tzDropdownH - tzArrowSize) / 2;
        if (g_timezoneDropdownOpen)
        {
            DrawUpArrow(tzArrowX, tzArrowY, tzArrowSize, 0.6f, 0.6f, 0.7f);
        }
        else
        {
            DrawDownArrow(tzArrowX, tzArrowY, tzArrowSize, 0.6f, 0.6f, 0.7f);
        }

        // Toggle dropdown on click
        if (isTzDropdownHovering && mouseClicked)
        {
            g_timezoneDropdownOpen = !g_timezoneDropdownOpen;
        }

        // Draw dropdown options if open
        if (g_timezoneDropdownOpen)
        {
            float tzOptionH = 20.0f;
            float tzOptionsY = tzDropdownY + tzDropdownH + 2;
            float tzOptionsWidth = 180.0f; // Wider to fit full timezone names
            float tzOptionsHeight = tzOptionH * g_timezoneCount + 4;

            // Dropdown background (drawn above other elements)
            DrawRoundedRect(tzDropdownX, tzOptionsY, tzOptionsWidth, tzOptionsHeight, 3.0f, 0.12f, 0.12f, 0.15f, 0.98f);

            for (int i = 0; i < g_timezoneCount; i++)
            {
                float optY = tzOptionsY + 2 + i * tzOptionH;
                bool isOptionHovering = (mouseX >= tzDropdownX && mouseX <= tzDropdownX + tzOptionsWidth &&
                                         mouseY >= optY && mouseY <= optY + tzOptionH - 2);

                if (isOptionHovering)
                {
                    g_input.setCursor(CursorType::Pointer);
                }

                // Highlight current selection or hover
                bool isSelected = (i == g_selectedTimezoneIndex);
                if (isOptionHovering || isSelected)
                {
                    DrawRoundedRect(tzDropdownX + 2,
                                    optY,
                                    tzOptionsWidth - 4,
                                    tzOptionH - 2,
                                    2.0f,
                                    isOptionHovering ? 0.28f : 0.2f,
                                    isOptionHovering ? 0.32f : 0.23f,
                                    isOptionHovering ? 0.42f : 0.32f,
                                    0.9f);
                }

                DrawText(tzDropdownX + 6, optY + 3, g_timezones[i].name, 0.65f, 0.85f, 0.85f, 0.9f);

                if (isOptionHovering && mouseClicked)
                {
                    g_selectedTimezoneIndex = i;
                    g_timezoneDropdownOpen = false;
                }
            }

            // Close dropdown if clicked outside
            if (mouseClicked && !isTzDropdownHovering &&
                !(mouseX >= tzDropdownX && mouseX <= tzDropdownX + tzOptionsWidth && mouseY >= tzOptionsY &&
                  mouseY <= tzOptionsY + tzOptionsHeight))
            {
                g_timezoneDropdownOpen = false;
            }
        }

        // Draw time dilation section (using fixed date + timezone dropdown width, so position is stable)
        float dilationStartX = tzDropdownX + tzDropdownWidth + timePanelPadding * 2;
        float dilationY = timePanelY + (timePanelHeight - 16.0f) / 2.0f;

        // Label
        DrawText(dilationStartX, dilationY + 2, "Time Speed: ", 0.75f, 0.7f, 0.7f, 0.75f);

        // Slider
        float sliderX = dilationStartX + dilationLabelWidth + sliderGap;
        float sliderY = dilationY;
        DrawSlider(sliderX,
                   sliderY,
                   sliderWidth,
                   16.0f,
                   timeParams.timeDilation,
                   MIN_TIME_DILATION,
                   MAX_TIME_DILATION,
                   mouseX,
                   mouseY,
                   mouseDown,
                   g_isDraggingSlider);

        // Value
        float valueX = sliderX + sliderWidth + sliderGap;
        DrawText(valueX, dilationY + 2, dilationStr, 0.75f, 0.8f, 0.85f, 0.9f);

        // Play/Pause button on the right
        float playPauseBtnX = valueX + dilationValueWidth + timePanelPadding;
        float playPauseBtnY = timePanelY + (timePanelHeight - playPauseBtnSize) / 2.0f;
        bool isPlayPauseHovering = (mouseX >= playPauseBtnX && mouseX <= playPauseBtnX + playPauseBtnSize &&
                                    mouseY >= playPauseBtnY && mouseY <= playPauseBtnY + playPauseBtnSize);

        // Button background
        if (timeParams.isPaused)
        {
            // Green when paused (ready to resume)
            DrawRoundedRect(playPauseBtnX,
                            playPauseBtnY,
                            playPauseBtnSize,
                            playPauseBtnSize,
                            4.0f,
                            isPlayPauseHovering ? 0.25f : 0.2f,
                            isPlayPauseHovering ? 0.55f : 0.45f,
                            isPlayPauseHovering ? 0.25f : 0.2f,
                            0.95f);
        }
        else
        {
            // Orange when running (ready to pause)
            DrawRoundedRect(playPauseBtnX,
                            playPauseBtnY,
                            playPauseBtnSize,
                            playPauseBtnSize,
                            4.0f,
                            isPlayPauseHovering ? 0.55f : 0.45f,
                            isPlayPauseHovering ? 0.35f : 0.28f,
                            isPlayPauseHovering ? 0.15f : 0.1f,
                            0.95f);
        }

        // Draw icon (play triangle when paused, pause bars when running)
        float iconSize = playPauseBtnSize * 0.6f;
        float iconX = playPauseBtnX + (playPauseBtnSize - iconSize) / 2.0f;
        float iconY = playPauseBtnY + (playPauseBtnSize - iconSize) / 2.0f;

        if (timeParams.isPaused)
        {
            // Draw play triangle (pointing right)
            DrawPlayIcon(iconX, iconY, iconSize, 0.95f, 0.95f, 0.95f);
        }
        else
        {
            // Draw pause bars
            DrawPauseIcon(iconX, iconY, iconSize, 0.95f, 0.95f, 0.95f);
        }

        // Handle button click
        if (isPlayPauseHovering && mouseClicked)
        {
            result.pauseToggled = true;
        }

        // Interactions button (hand icon) on the right of play/pause button
        float interactionsBtnX = playPauseBtnX + playPauseBtnSize + timePanelPadding;
        float interactionsBtnY = timePanelY + (timePanelHeight - interactionsBtnSize) / 2.0f;
        bool isInteractionsHovering = (mouseX >= interactionsBtnX && mouseX <= interactionsBtnX + interactionsBtnSize &&
                                       mouseY >= interactionsBtnY && mouseY <= interactionsBtnY + interactionsBtnSize);

        // Button background
        DrawRoundedRect(interactionsBtnX,
                        interactionsBtnY,
                        interactionsBtnSize,
                        interactionsBtnSize,
                        4.0f,
                        isInteractionsHovering ? 0.35f : 0.25f,
                        isInteractionsHovering ? 0.35f : 0.25f,
                        isInteractionsHovering ? 0.45f : 0.35f,
                        0.95f);

        // Draw hand icon
        float handIconSize = interactionsBtnSize * 0.6f;
        float handIconX = interactionsBtnX + (interactionsBtnSize - handIconSize) / 2.0f;
        float handIconY = interactionsBtnY + (interactionsBtnSize - handIconSize) / 2.0f;
        DrawHandIcon(handIconX, handIconY, handIconSize, 0.95f, 0.95f, 0.95f);

        // Handle interactions button click - toggle popup
        if (isInteractionsHovering && mouseClicked)
        {
            g_interactionsPopupOpen = !g_interactionsPopupOpen;
        }

        // Draw interactions popup menu if open
        if (g_interactionsPopupOpen)
        {
            float popupWidth = 180.0f;
            float popupButtonHeight = 32.0f;
            float popupPadding = 8.0f;
            float popupTitleHeight = 24.0f;
            float popupHeight = popupTitleHeight + popupPadding + popupButtonHeight * 3 + popupPadding * 3;
            float popupX = interactionsBtnX + interactionsBtnSize / 2.0f - popupWidth / 2.0f;
            float popupY = interactionsBtnY + interactionsBtnSize + 8.0f; // Below the button

            // Clamp to screen bounds
            if (popupX < UI_PADDING)
                popupX = UI_PADDING;
            if (popupX + popupWidth > screenWidth - UI_PADDING)
                popupX = screenWidth - UI_PADDING - popupWidth;
            if (popupY + popupHeight > screenHeight - UI_PADDING)
                popupY = interactionsBtnY - popupHeight - 8.0f; // Show above if no room below

            // Draw popup background
            DrawRoundedRect(popupX, popupY, popupWidth, popupHeight, 6.0f, 0.18f, 0.18f, 0.22f, 0.95f);

            // TODO: Migrate UI rendering to Vulkan
            // Draw border
            // glColor4f(0.4f, 0.4f, 0.5f, 0.9f); // REMOVED - migrate to Vulkan uniform buffer
            // glLineWidth(1.0f); // REMOVED - migrate to Vulkan pipeline state
            // glBegin(GL_LINE_LOOP); // REMOVED - migrate to Vulkan
            // glVertex2f(popupX + 6, popupY); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX + popupWidth - 6, popupY); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX + popupWidth, popupY + 6); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX + popupWidth, popupY + popupHeight - 6); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX + popupWidth - 6, popupY + popupHeight); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX + 6, popupY + popupHeight); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX, popupY + popupHeight - 6); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX, popupY + 6); // REMOVED - migrate to Vulkan vertex buffer
            // glEnd(); // REMOVED - migrate to Vulkan

            float currentPopupY = popupY + popupPadding;

            // Title
            float titleTextWidth = GetTextWidth("Interactions", 0.85f);
            float titleTextX = popupX + (popupWidth - titleTextWidth) / 2.0f;
            DrawText(titleTextX, currentPopupY + 4, "Interactions", 0.85f, 0.95f, 0.95f, 0.95f);
            currentPopupY += popupTitleHeight + popupPadding;

            // Measure button
            float measureBtnX = popupX + popupPadding;
            float measureBtnY = currentPopupY;
            float measureBtnW = popupWidth - popupPadding * 2;
            bool isMeasureHovering = (mouseX >= measureBtnX && mouseX <= measureBtnX + measureBtnW &&
                                      mouseY >= measureBtnY && mouseY <= measureBtnY + popupButtonHeight);

            DrawRoundedRect(measureBtnX,
                            measureBtnY,
                            measureBtnW,
                            popupButtonHeight,
                            4.0f,
                            isMeasureHovering ? 0.3f : 0.2f,
                            isMeasureHovering ? 0.3f : 0.2f,
                            isMeasureHovering ? 0.4f : 0.3f,
                            0.9f);

            // Measure icon
            float measureIconSize = popupButtonHeight * 0.5f;
            float measureIconX = measureBtnX + 8.0f;
            float measureIconY = measureBtnY + (popupButtonHeight - measureIconSize) / 2.0f;
            DrawMeasureIcon(measureIconX, measureIconY, measureIconSize, 0.9f, 0.9f, 0.95f);

            // Measure label
            DrawText(measureBtnX + measureIconSize + 16.0f, measureBtnY + 8, "Measure", 0.75f, 0.9f, 0.9f, 0.95f);

            // Handle measure button click - open measure submenu
            if (isMeasureHovering && mouseClicked)
            {
                g_measurePopupOpen = !g_measurePopupOpen;
            }

            currentPopupY += popupButtonHeight + popupPadding / 2;

            // Color Picker button
            float colorPickerBtnX = popupX + popupPadding;
            float colorPickerBtnY = currentPopupY;
            bool isColorPickerHovering = (mouseX >= colorPickerBtnX && mouseX <= colorPickerBtnX + measureBtnW &&
                                          mouseY >= colorPickerBtnY && mouseY <= colorPickerBtnY + popupButtonHeight);

            DrawRoundedRect(colorPickerBtnX,
                            colorPickerBtnY,
                            measureBtnW,
                            popupButtonHeight,
                            4.0f,
                            isColorPickerHovering ? 0.3f : 0.2f,
                            isColorPickerHovering ? 0.3f : 0.2f,
                            isColorPickerHovering ? 0.4f : 0.3f,
                            0.9f);

            // Eye icon
            float eyeIconSize = popupButtonHeight * 0.5f;
            float eyeIconX = colorPickerBtnX + 8.0f;
            float eyeIconY = colorPickerBtnY + (popupButtonHeight - eyeIconSize) / 2.0f;
            DrawEyeIcon(eyeIconX, eyeIconY, eyeIconSize, 0.9f, 0.9f, 0.95f);

            // Color Picker label
            DrawText(colorPickerBtnX + eyeIconSize + 16.0f,
                     colorPickerBtnY + 8,
                     "Color Picker",
                     0.75f,
                     0.9f,
                     0.9f,
                     0.95f);

            // Handle color picker button click - toggle color picker mode
            if (isColorPickerHovering && mouseClicked)
            {
                if (g_measurementMode == MeasurementMode::ColorPicker)
                {
                    g_measurementMode = MeasurementMode::None; // Toggle off
                }
                else
                {
                    g_measurementMode = MeasurementMode::ColorPicker;
                }
                g_interactionsPopupOpen = false; // Close interactions popup
            }

            currentPopupY += popupButtonHeight + popupPadding / 2;

            // Shoot button
            float shootBtnX = popupX + popupPadding;
            float shootBtnY = currentPopupY;
            bool isShootHovering = (mouseX >= shootBtnX && mouseX <= shootBtnX + measureBtnW && mouseY >= shootBtnY &&
                                    mouseY <= shootBtnY + popupButtonHeight);

            DrawRoundedRect(shootBtnX,
                            shootBtnY,
                            measureBtnW,
                            popupButtonHeight,
                            4.0f,
                            isShootHovering ? 0.3f : 0.2f,
                            isShootHovering ? 0.3f : 0.2f,
                            isShootHovering ? 0.4f : 0.3f,
                            0.9f);

            // Shoot icon
            float shootIconSize = popupButtonHeight * 0.5f;
            float shootIconX = shootBtnX + 8.0f;
            float shootIconY = shootBtnY + (popupButtonHeight - shootIconSize) / 2.0f;
            DrawShootIcon(shootIconX, shootIconY, shootIconSize, 0.9f, 0.9f, 0.95f);

            // Shoot label
            DrawText(shootBtnX + shootIconSize + 16.0f, shootBtnY + 8, "Shoot", 0.75f, 0.9f, 0.9f, 0.95f);

            // Handle shoot button click - enter shoot mode
            if (isShootHovering && mouseClicked)
            {
                g_shootModeActive = true;
                g_interactionsPopupOpen = false; // Close interactions popup
            }

            // Close popup if clicking outside (but not if clicking on measure button or measure submenu)
            // Don't close if clicking on measure button (it's inside the popup, so this shouldn't happen, but be safe)
            bool clickedOnMeasureButton = isMeasureHovering;

            // Check if clicking on measure submenu
            bool clickedOnMeasureSubmenu = false;
            if (g_measurePopupOpen)
            {
                float measurePopupWidth = 200.0f;
                float measurePopupHeight = 120.0f;
                float measurePopupX = popupX + popupWidth + 8.0f; // To the right of interactions popup
                float measurePopupY = popupY;
                if (measurePopupX + measurePopupWidth > screenWidth - UI_PADDING)
                    measurePopupX = popupX - measurePopupWidth - 8.0f; // Show to the left if no room on right
                clickedOnMeasureSubmenu = (mouseX >= measurePopupX && mouseX <= measurePopupX + measurePopupWidth &&
                                           mouseY >= measurePopupY && mouseY <= measurePopupY + measurePopupHeight);
            }

            bool clickedOutsidePopup = mouseClicked &&
                                       !(mouseX >= popupX && mouseX <= popupX + popupWidth && mouseY >= popupY &&
                                         mouseY <= popupY + popupHeight) &&
                                       !isInteractionsHovering && !clickedOnMeasureButton && !clickedOnMeasureSubmenu;

            if (clickedOutsidePopup)
            {
                g_interactionsPopupOpen = false;
                g_measurePopupOpen = false; // Also close measure popup
            }
        }

        // Draw measure submenu popup if open
        if (g_measurePopupOpen)
        {
            float popupWidth = 200.0f;
            float popupButtonHeight = 32.0f;
            float popupPadding = 8.0f;
            float popupTitleHeight = 24.0f;
            float popupHeight = popupTitleHeight + popupPadding + popupButtonHeight * 2 + popupPadding * 2;

            // Position next to interactions popup
            float interactionsPopupX = interactionsBtnX + interactionsBtnSize / 2.0f - 180.0f / 2.0f;
            float interactionsPopupY = interactionsBtnY + interactionsBtnSize + 8.0f;
            float popupX = interactionsPopupX + 180.0f + 8.0f; // To the right of interactions popup
            float popupY = interactionsPopupY;

            // Clamp to screen bounds
            if (popupX + popupWidth > screenWidth - UI_PADDING)
                popupX = interactionsPopupX - popupWidth - 8.0f; // Show to the left if no room on right
            if (popupY + popupHeight > screenHeight - UI_PADDING)
                popupY = screenHeight - popupHeight - UI_PADDING;

            // Draw popup background
            DrawRoundedRect(popupX, popupY, popupWidth, popupHeight, 6.0f, 0.18f, 0.18f, 0.22f, 0.95f);

            // TODO: Migrate UI rendering to Vulkan
            // Draw border
            // glColor4f(0.4f, 0.4f, 0.5f, 0.9f); // REMOVED - migrate to Vulkan uniform buffer
            // glLineWidth(1.0f); // REMOVED - migrate to Vulkan pipeline state
            // glBegin(GL_LINE_LOOP); // REMOVED - migrate to Vulkan
            // glVertex2f(popupX + 6, popupY); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX + popupWidth - 6, popupY); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX + popupWidth, popupY + 6); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX + popupWidth, popupY + popupHeight - 6); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX + popupWidth - 6, popupY + popupHeight); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX + 6, popupY + popupHeight); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX, popupY + popupHeight - 6); // REMOVED - migrate to Vulkan vertex buffer
            // glVertex2f(popupX, popupY + 6); // REMOVED - migrate to Vulkan vertex buffer
            // glEnd(); // REMOVED - migrate to Vulkan

            float currentPopupY = popupY + popupPadding;

            // Title
            float titleTextWidth = GetTextWidth("Measurement", 0.85f);
            float titleTextX = popupX + (popupWidth - titleTextWidth) / 2.0f;
            DrawText(titleTextX, currentPopupY + 4, "Measurement", 0.85f, 0.95f, 0.95f, 0.95f);
            currentPopupY += popupTitleHeight + popupPadding;

            // Longitude/Latitude button
            float latLonBtnX = popupX + popupPadding;
            float latLonBtnY = currentPopupY;
            float latLonBtnW = popupWidth - popupPadding * 2;
            bool isLatLonHovering = (mouseX >= latLonBtnX && mouseX <= latLonBtnX + latLonBtnW &&
                                     mouseY >= latLonBtnY && mouseY <= latLonBtnY + popupButtonHeight);

            bool isLatLonActive = (g_measurementMode == MeasurementMode::LongitudeLatitude);
            DrawRoundedRect(latLonBtnX,
                            latLonBtnY,
                            latLonBtnW,
                            popupButtonHeight,
                            4.0f,
                            isLatLonActive ? 0.3f : (isLatLonHovering ? 0.3f : 0.2f),
                            isLatLonActive ? 0.4f : (isLatLonHovering ? 0.3f : 0.2f),
                            isLatLonActive ? 0.5f : (isLatLonHovering ? 0.4f : 0.3f),
                            0.9f);

            DrawText(latLonBtnX + 8.0f, latLonBtnY + 8, "Longitude/Latitude", 0.75f, 0.9f, 0.9f, 0.95f);

            // Handle lat/lon button click
            if (isLatLonHovering && mouseClicked)
            {
                if (g_measurementMode == MeasurementMode::LongitudeLatitude)
                {
                    g_measurementMode = MeasurementMode::None; // Toggle off
                }
                else
                {
                    g_measurementMode = MeasurementMode::LongitudeLatitude;
                }
                g_measurePopupOpen = false; // Close popup after selection
            }

            currentPopupY += popupButtonHeight + popupPadding / 2;

            // Altitude/Depth button
            float altDepthBtnX = popupX + popupPadding;
            float altDepthBtnY = currentPopupY;
            float altDepthBtnW = popupWidth - popupPadding * 2;
            bool isAltDepthHovering = (mouseX >= altDepthBtnX && mouseX <= altDepthBtnX + altDepthBtnW &&
                                       mouseY >= altDepthBtnY && mouseY <= altDepthBtnY + popupButtonHeight);

            bool isAltDepthActive = (g_measurementMode == MeasurementMode::AltitudeDepth);
            DrawRoundedRect(altDepthBtnX,
                            altDepthBtnY,
                            altDepthBtnW,
                            popupButtonHeight,
                            4.0f,
                            isAltDepthActive ? 0.3f : (isAltDepthHovering ? 0.3f : 0.2f),
                            isAltDepthActive ? 0.4f : (isAltDepthHovering ? 0.3f : 0.2f),
                            isAltDepthActive ? 0.5f : (isAltDepthHovering ? 0.4f : 0.3f),
                            0.9f);

            DrawText(altDepthBtnX + 8.0f, altDepthBtnY + 8, "Altitude/Depth", 0.75f, 0.9f, 0.9f, 0.95f);

            // Handle altitude/depth button click
            if (isAltDepthHovering && mouseClicked)
            {
                if (g_measurementMode == MeasurementMode::AltitudeDepth)
                {
                    g_measurementMode = MeasurementMode::None; // Toggle off
                }
                else
                {
                    g_measurementMode = MeasurementMode::AltitudeDepth;
                }
                g_measurePopupOpen = false; // Close popup after selection
            }

            // Close measure popup if clicking outside (but keep interactions popup open)
            bool clickedOutsideMeasurePopup = mouseClicked && !(mouseX >= popupX && mouseX <= popupX + popupWidth &&
                                                                mouseY >= popupY && mouseY <= popupY + popupHeight);

            // Also check if clicking on interactions popup (shouldn't close measure popup in that case)
            bool clickedOnInteractionsPopup = false;
            if (g_interactionsPopupOpen)
            {
                float interactionsPopupX = interactionsBtnX + interactionsBtnSize / 2.0f - 180.0f / 2.0f;
                float interactionsPopupY = interactionsBtnY + interactionsBtnSize + 8.0f;
                float interactionsPopupWidth = 180.0f;
                float interactionsPopupHeight = 120.0f;
                clickedOnInteractionsPopup =
                    (mouseX >= interactionsPopupX && mouseX <= interactionsPopupX + interactionsPopupWidth &&
                     mouseY >= interactionsPopupY && mouseY <= interactionsPopupY + interactionsPopupHeight);
            }

            if (clickedOutsideMeasurePopup && !clickedOnInteractionsPopup)
            {
                g_measurePopupOpen = false;
            }
        }
    } // End of "if UI is visible" block for time controls

    // Only draw rest of UI panels if UI is visible
    if (!g_uiVisible)
    {
        // UI is hidden - skip drawing panels, but continue to shoot mode and other overlays
    }
    else
    {
        // Build tree structure
        TreeNode solarSystemTree = buildSolarSystemTree(bodies);

        float panelX = UI_PADDING;
        // Position below time control panel (time panel height + padding)
        float timePanelHeight = 32.0f;
        float panelY = UI_PADDING + timePanelHeight + UI_PADDING; // Below time control panel
        float panelWidth = 220.0f;

        // Section heights
        float fullscreenBtnHeight = 28.0f; // At the very top
        float fpsHeight = 48.0f;           // Increased to accommodate FPS and triangle count (2 lines)
        float accordionHeaderHeight = 22.0f;
        float checkboxHeight = 22.0f;
        float dropdownHeight = 24.0f;
        float restartWarningHeight = 20.0f;
        float fovSliderHeight = 32.0f; // Now in settings

        // Dropdown state (declared here so it can be used in height calculation)
        static bool g_resolutionDropdownOpen = false;
        float dropdownOptionsHeight = g_resolutionDropdownOpen ? ((dropdownHeight - 4) * 4 + 4) : 0.0f;

        // Settings accordion state (includes texture resolution dropdown + FOV slider + texture toggles + atmosphere toggle)
        // State is stored in AppState.uiState.settingsExpanded
        float settingsContentHeight = SETTINGS_ACCORDION_EXPANDED
                                          ? (dropdownHeight + dropdownOptionsHeight + restartWarningHeight +
                                             fovSliderHeight + checkboxHeight * 3 + PANEL_PADDING * 5)
                                          : 0.0f; // 3 checkboxes: heightmap, normalMap, roughness
        float settingsSectionHeight = accordionHeaderHeight + settingsContentHeight + PANEL_PADDING;

        // Visualizations accordion state (closed by default)
        // State is stored in AppState.uiState.controlsExpanded
        // 17 checkboxes: orbits, axes, barycenters, lagrange, coord grids, magnetic fields, constellations,
        // celestial grid, constellation figures, constellation bounds, force vectors, gravity grid, sun spot,
        // wireframe, voxel wireframes, atmosphere layers, cities
        // Plus conditional sliders when gravity grid is enabled (Grid Resolution + Warp Strength)
        float numCheckboxes = 17.0f;
        float checkboxTotalHeight = numCheckboxes * (checkboxHeight + PANEL_PADDING / 2);
        // Each slider: label (14) + slider (14) + padding
        float gravitySliderHeight = timeParams.showGravityGrid ? (2.0f * (14.0f + 14.0f + PANEL_PADDING / 2)) : 0.0f;
        float controlsContentHeight =
            CONTROLS_ACCORDION_EXPANDED ? (checkboxTotalHeight + gravitySliderHeight + PANEL_PADDING * 2) : 0.0f;
        float controlsSectionHeight = accordionHeaderHeight + controlsContentHeight + PANEL_PADDING;

        float treeHeight = CalculateTreeHeight(solarSystemTree);
        float totalHeight = fullscreenBtnHeight + fpsHeight + settingsSectionHeight + controlsSectionHeight +
                            treeHeight + PANEL_PADDING * 8;

        // Cap max height for scrolling (future feature)
        float maxPanelHeight = screenHeight - UI_PADDING * 2;
        if (totalHeight > maxPanelHeight)
            totalHeight = maxPanelHeight;

        // Draw main panel background
        DrawRoundedRect(panelX, panelY, panelWidth, totalHeight, 8.0f, 0.12f, 0.12f, 0.14f, 0.85f);

        // Check if mouse is within left panel bounds (for click consumption)
        bool mouseInLeftPanel =
            (mouseX >= panelX && mouseX <= panelX + panelWidth && mouseY >= panelY && mouseY <= panelY + totalHeight);

        // ==================================
        // Fullscreen Button (at the very top)
        // ==================================
        float currentY = panelY + PANEL_PADDING;
        float topFullscreenBtnW = panelWidth - PANEL_PADDING * 2;
        float topFullscreenBtnH = fullscreenBtnHeight - 4;

        bool isTopFullscreenHovering =
            (mouseX >= panelX + PANEL_PADDING && mouseX <= panelX + PANEL_PADDING + topFullscreenBtnW &&
             mouseY >= currentY && mouseY <= currentY + topFullscreenBtnH);

        // Button background with accent color
        if (isTopFullscreenHovering)
        {
            DrawRoundedRect(panelX + PANEL_PADDING,
                            currentY,
                            topFullscreenBtnW,
                            topFullscreenBtnH,
                            4.0f,
                            0.35f,
                            0.45f,
                            0.6f,
                            0.95f);
        }
        else
        {
            DrawRoundedRect(panelX + PANEL_PADDING,
                            currentY,
                            topFullscreenBtnW,
                            topFullscreenBtnH,
                            4.0f,
                            0.25f,
                            0.3f,
                            0.4f,
                            0.9f);
        }

        // Button text
        std::string topFullscreenText = timeParams.isFullscreen ? "Exit Fullscreen (F11)" : "Fullscreen (F11)";
        float topFsTextWidth = GetTextWidth(topFullscreenText, 0.75f);
        DrawText(panelX + PANEL_PADDING + (topFullscreenBtnW - topFsTextWidth) / 2,
                 currentY + 5,
                 topFullscreenText,
                 0.75f,
                 isTopFullscreenHovering ? 0.98f : 0.9f,
                 isTopFullscreenHovering ? 0.98f : 0.9f,
                 isTopFullscreenHovering ? 1.0f : 0.95f);

        if (isTopFullscreenHovering && mouseClicked)
        {
            result.fullscreenToggled = true;
        }

        currentY += fullscreenBtnHeight;

        // ==================================
        // FPS and Triangle Count Section
        // ==================================
        DrawRoundedRect(panelX + PANEL_PADDING,
                        currentY,
                        panelWidth - PANEL_PADDING * 2,
                        fpsHeight - 4,
                        4.0f,
                        0.95f,
                        0.95f,
                        0.93f,
                        0.95f);
        DrawText(panelX + PANEL_PADDING + 6, currentY + 6, "FPS: " + std::to_string(fps), 1.0f, 0.1f, 0.45f, 0.2f);

        // Format triangle count with thousand separators for readability
        std::string triangleStr = std::to_string(triangleCount);
        if (triangleCount >= 1000)
        {
            // Add comma separators (e.g., 1,234,567)
            std::string formatted;
            int count = 0;
            for (int i = static_cast<int>(triangleStr.length()) - 1; i >= 0; i--)
            {
                if (count > 0 && count % 3 == 0)
                {
                    formatted = "," + formatted;
                }
                formatted = triangleStr[i] + formatted;
                count++;
            }
            triangleStr = formatted;
        }
        DrawText(panelX + PANEL_PADDING + 6, currentY + 20, "Triangles: " + triangleStr, 1.0f, 0.1f, 0.45f, 0.2f);
        currentY += fpsHeight;

        // TODO: Migrate UI rendering to Vulkan
        // Separator
        // Temporarily restored OpenGL calls so UI renders
        glColor4f(0.3f, 0.3f, 0.35f, 0.8f);
        glBegin(GL_LINES);
        glVertex2f(panelX + PANEL_PADDING, currentY);
        glVertex2f(panelX + panelWidth - PANEL_PADDING, currentY);
        glEnd();
        currentY += PANEL_PADDING;

        // ==================================
        // Settings Accordion Section
        // ==================================
        currentY += PANEL_PADDING / 2;

        // TODO: Migrate UI rendering to Vulkan
        // Separator
        // glColor4f(0.3f, 0.3f, 0.35f, 0.8f); // REMOVED - migrate to Vulkan uniform buffer
        // glBegin(GL_LINES); // REMOVED - migrate to Vulkan
        // glVertex2f(panelX + PANEL_PADDING, currentY); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex2f(panelX + panelWidth - PANEL_PADDING, currentY); // REMOVED - migrate to Vulkan vertex buffer
        // glEnd(); // REMOVED - migrate to Vulkan
        currentY += PANEL_PADDING / 2;

        // Settings accordion header
        float settingsHeaderY = currentY;
        bool isSettingsHeaderHovering =
            (mouseX >= panelX + PANEL_PADDING && mouseX <= panelX + panelWidth - PANEL_PADDING &&
             mouseY >= settingsHeaderY && mouseY <= settingsHeaderY + accordionHeaderHeight);

        // Draw accordion header for settings
        if (DrawAccordionHeader(panelX + PANEL_PADDING,
                                settingsHeaderY,
                                panelWidth - PANEL_PADDING * 2,
                                accordionHeaderHeight,
                                "Settings",
                                SETTINGS_ACCORDION_EXPANDED,
                                mouseX,
                                mouseY,
                                mouseClicked))
        {
            APP_STATE.uiState.settingsExpanded = APP_STATE.uiState.settingsExpanded ? 0 : 1;
        }

        currentY += accordionHeaderHeight;

        // Draw settings content if expanded
        if (SETTINGS_ACCORDION_EXPANDED)
        {
            float settingsX = panelX + PANEL_PADDING + 8;
            float settingsW = panelWidth - PANEL_PADDING * 2 - 16;

            // Texture Resolution label
            DrawText(settingsX, currentY + 2, "Texture Resolution", 0.7f, 0.7f, 0.7f, 0.75f);
            currentY += 14;

            // Dropdown for texture resolution
            float dropBtnY = currentY;
            float dropBtnH = dropdownHeight - 4;

            // Get current resolution name
            const char *currentResName = getResolutionName(timeParams.textureResolution);

            // Dropdown button background
            bool isDropdownHovering = (mouseX >= settingsX && mouseX <= settingsX + settingsW && mouseY >= dropBtnY &&
                                       mouseY <= dropBtnY + dropBtnH);

            if (isDropdownHovering)
            {
                g_input.setCursor(CursorType::Pointer);
            }

            DrawRoundedRect(settingsX,
                            dropBtnY,
                            settingsW,
                            dropBtnH,
                            3.0f,
                            isDropdownHovering ? 0.25f : 0.2f,
                            isDropdownHovering ? 0.25f : 0.2f,
                            isDropdownHovering ? 0.3f : 0.25f,
                            0.95f);

            // Dropdown text
            DrawText(settingsX + 6, dropBtnY + 3, currentResName, 0.75f, 0.9f, 0.9f, 0.95f);

            // Dropdown arrow (up when open, down when closed)
            float dropArrowSize = 10.0f;
            float dropArrowX = settingsX + settingsW - dropArrowSize - 6;
            float dropArrowY = dropBtnY + (dropBtnH - dropArrowSize) / 2;
            if (g_resolutionDropdownOpen)
            {
                DrawUpArrow(dropArrowX, dropArrowY, dropArrowSize, 0.7f, 0.7f, 0.8f);
            }
            else
            {
                DrawDownArrow(dropArrowX, dropArrowY, dropArrowSize, 0.7f, 0.7f, 0.8f);
            }

            // Toggle dropdown on click
            if (isDropdownHovering && mouseClicked)
            {
                g_resolutionDropdownOpen = !g_resolutionDropdownOpen;
            }

            currentY += dropdownHeight;

            // Draw dropdown options if open
            if (g_resolutionDropdownOpen)
            {
                float optionY = dropBtnY + dropBtnH + 2;
                const char *options[] = {"Low", "Medium", "High", "Ultra"};
                const char *descriptions[] = {"1024x512", "4096x2048", "8192x4096", "16384x8192"};

                // Dropdown background
                DrawRoundedRect(settingsX, optionY, settingsW, dropBtnH * 4 + 4, 3.0f, 0.15f, 0.15f, 0.18f, 0.98f);

                for (int i = 0; i < 4; i++)
                {
                    float optY = optionY + 2 + i * dropBtnH;
                    bool isOptionHovering = (mouseX >= settingsX && mouseX <= settingsX + settingsW && mouseY >= optY &&
                                             mouseY <= optY + dropBtnH - 2);

                    if (isOptionHovering)
                    {
                        g_input.setCursor(CursorType::Pointer);
                    }

                    // Highlight current selection or hover
                    bool isSelected = (i == static_cast<int>(timeParams.textureResolution));
                    if (isOptionHovering || isSelected)
                    {
                        DrawRoundedRect(settingsX + 2,
                                        optY,
                                        settingsW - 4,
                                        dropBtnH - 2,
                                        2.0f,
                                        isOptionHovering ? 0.3f : 0.22f,
                                        isOptionHovering ? 0.35f : 0.25f,
                                        isOptionHovering ? 0.45f : 0.35f,
                                        0.9f);
                    }

                    DrawText(settingsX + 8, optY + 3, options[i], 0.7f, 0.9f, 0.9f, 0.95f);
                    float descWidth = GetTextWidth(descriptions[i], 0.6f);
                    DrawText(settingsX + settingsW - descWidth - 8, optY + 4, descriptions[i], 0.6f, 0.6f, 0.6f, 0.7f);

                    if (isOptionHovering && mouseClicked)
                    {
                        result.newTextureResolution = i;
                        g_resolutionDropdownOpen = false;
                    }
                }

                // Close dropdown if clicked outside
                if (mouseClicked && !isDropdownHovering &&
                    !(mouseX >= settingsX && mouseX <= settingsX + settingsW && mouseY >= optionY &&
                      mouseY <= optionY + dropBtnH * 4 + 4))
                {
                    g_resolutionDropdownOpen = false;
                }

                // Add dropdown options height to layout
                currentY += dropdownOptionsHeight;
            }

            // Restart warning if settings changed
            if (Settings::needsRestart())
            {
                DrawText(settingsX, currentY + 2, "Restart required to apply", 0.65f, 0.95f, 0.7f, 0.3f);
            }
            currentY += restartWarningHeight;

            // ==================================
            // FOV Slider (in Settings)
            // ==================================
            // Label with current value
            char fovLabel[32];
            snprintf(fovLabel, sizeof(fovLabel), "FOV: %.0f deg", timeParams.currentFOV);
            DrawText(settingsX, currentY, fovLabel, 0.7f, 0.7f, 0.7f, 0.75f);
            currentY += 14;

            // FOV slider (5-120 degrees, snapping to 5 degree increments)
            float fovValue = timeParams.currentFOV;
            if (DrawLinearSlider(settingsX,
                                 currentY,
                                 settingsW,
                                 16.0f,
                                 &fovValue,
                                 5.0f,
                                 120.0f,
                                 5.0f,
                                 mouseX,
                                 mouseY,
                                 mouseDown,
                                 g_fovSliderDragging))
            {
                result.newFOV = fovValue;
            }

            currentY += 16.0f + PANEL_PADDING / 2;

            currentY += PANEL_PADDING;

            // Texture Effect Toggles
            float cbX = panelX + PANEL_PADDING + 8;
            float cbItemW = panelWidth - PANEL_PADDING * 2 - 8;

            // FXAA Antialiasing
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.fxaaEnabled,
                             "FXAA Antialiasing",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.fxaaToggled = true;
            }
            currentY += checkboxHeight;

            // VSync
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.vsyncEnabled,
                             "VSync (Uncap FPS)",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.vsyncToggled = true;
            }
            currentY += checkboxHeight;

            // Heightmap toggle - read from AppState
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             APP_STATE.uiState.heightmapEnabled != 0u,
                             "Height Map",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.heightmapToggled = true;
            }
            currentY += checkboxHeight;

            // Normal Map toggle - read from AppState
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             APP_STATE.uiState.normalMapEnabled != 0u,
                             "Normal Map",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.normalMapToggled = true;
            }
            currentY += checkboxHeight;

            // Roughness toggle - read from AppState
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             APP_STATE.uiState.roughnessEnabled != 0u,
                             "Roughness",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.roughnessToggled = true;
            }
            currentY += checkboxHeight;
        }

        // ==================================
        // Visualizations Accordion Section
        // ==================================
        currentY += PANEL_PADDING / 2;

        // TODO: Migrate UI rendering to Vulkan
        // Separator
        // glColor4f(0.3f, 0.3f, 0.35f, 0.8f); // REMOVED - migrate to Vulkan uniform buffer
        // glBegin(GL_LINES); // REMOVED - migrate to Vulkan
        // glVertex2f(panelX + PANEL_PADDING, currentY); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex2f(panelX + panelWidth - PANEL_PADDING, currentY); // REMOVED - migrate to Vulkan vertex buffer
        // glEnd(); // REMOVED - migrate to Vulkan
        currentY += PANEL_PADDING / 2;

        // Visualizations accordion header
        float ctrlHeaderY = currentY;
        bool isCtrlHeaderHovering =
            (mouseX >= panelX + PANEL_PADDING && mouseX <= panelX + panelWidth - PANEL_PADDING &&
             mouseY >= ctrlHeaderY && mouseY <= ctrlHeaderY + accordionHeaderHeight);

        // Draw accordion header for visualizations
        if (DrawAccordionHeader(panelX + PANEL_PADDING,
                                ctrlHeaderY,
                                panelWidth - PANEL_PADDING * 2,
                                accordionHeaderHeight,
                                "Visualizations",
                                CONTROLS_ACCORDION_EXPANDED,
                                mouseX,
                                mouseY,
                                mouseClicked))
        {
            APP_STATE.uiState.controlsExpanded = APP_STATE.uiState.controlsExpanded ? 0 : 1;
        }

        currentY += accordionHeaderHeight;

        // Draw visualizations checkboxes if expanded
        if (CONTROLS_ACCORDION_EXPANDED)
        {
            float cbX = panelX + PANEL_PADDING + 8;
            float cbItemW = panelWidth - PANEL_PADDING * 2 - 8;

            // Orbit Lines
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showOrbits,
                             "Orbit Lines",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.orbitsToggled = true;
            }
            currentY += checkboxHeight;

            // Rotation Axes
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showRotationAxes,
                             "Rotation Axes",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.axesToggled = true;
            }
            currentY += checkboxHeight;

            // Barycenters
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showBarycenters,
                             "Barycenters",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.barycentersToggled = true;
            }
            currentY += checkboxHeight;

            // Lagrange Points
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showLagrangePoints,
                             "Lagrange Points",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.lagrangePointsToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Coord Grids
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showCoordinateGrids,
                             "Coord Grids",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.coordGridsToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Magnetic Fields
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showMagneticFields,
                             "Magnetic Fields",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.magneticFieldsToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Constellations
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showConstellations,
                             "Constellations",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.constellationsToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Celestial Grid
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             g_showCelestialGrid,
                             "Celestial Grid",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.constellationGridToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Constellation Figures
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             g_showConstellationFigures,
                             "Constellation Figures",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.constellationFiguresToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Constellation Bounds
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             g_showConstellationBounds,
                             "Constellation Bounds",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.constellationBoundsToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Force Vectors
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showForceVectors,
                             "Force Vectors",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.forceVectorsToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Gravity Grid
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showGravityGrid,
                             "Gravity Grid",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.gravityGridToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Sun Spot
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showSunSpot,
                             "Sun Spot",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.sunSpotToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Wireframe
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showWireframe,
                             "Wireframe",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.wireframeToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Voxel Wireframes
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showVoxelWireframes,
                             "Voxel Wireframes",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.voxelWireframeToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Atmosphere Layers
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             timeParams.showAtmosphereLayers,
                             "Atmosphere Layers",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.atmosphereLayersToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // Cities
            if (DrawCheckbox(cbX,
                             currentY,
                             cbItemW,
                             checkboxHeight,
                             g_economyRenderer.getShowCityLabels(),
                             "Cities",
                             mouseX,
                             mouseY,
                             mouseClicked))
            {
                result.citiesToggled = true;
            }
            currentY += checkboxHeight + PANEL_PADDING / 2;

            // ==================================
            // Grid Resolution Slider (only show if gravity grid is enabled)
            // ==================================
            if (timeParams.showGravityGrid)
            {
                // Label with current value
                char gridResLabel[32];
                snprintf(gridResLabel, sizeof(gridResLabel), "Grid Lines: %d", timeParams.gravityGridResolution);
                DrawText(cbX, currentY, gridResLabel, 0.7f, 0.7f, 0.7f, 0.75f);
                currentY += 14;

                // Slider track
                float gridResSliderX = cbX;
                float gridResSliderY = currentY;
                float gridResSliderW = cbItemW;
                float gridResSliderH = 14.0f;
                float gridResTrackH = 4.0f;
                float gridResTrackY = gridResSliderY + (gridResSliderH - gridResTrackH) / 2;

                // Track background
                DrawRoundedRect(gridResSliderX,
                                gridResTrackY,
                                gridResSliderW,
                                gridResTrackH,
                                2.0f,
                                0.25f,
                                0.25f,
                                0.3f,
                                0.9f);

                // Calculate thumb position (10-50 range)
                const int MIN_GRID_RES = 10;
                const int MAX_GRID_RES = 50;
                float gridResNormalized = static_cast<float>(timeParams.gravityGridResolution - MIN_GRID_RES) /
                                          static_cast<float>(MAX_GRID_RES - MIN_GRID_RES);
                gridResNormalized = glm::clamp(gridResNormalized, 0.0f, 1.0f);
                float gridResThumbRadius = 7.0f;
                float gridResThumbX =
                    gridResSliderX + gridResNormalized * (gridResSliderW - gridResThumbRadius * 2) + gridResThumbRadius;
                float gridResThumbY = gridResSliderY + gridResSliderH / 2;

                // Check if hovering over slider area
                bool isGridResSliderHovering = (mouseX >= gridResSliderX && mouseX <= gridResSliderX + gridResSliderW &&
                                                mouseY >= gridResSliderY && mouseY <= gridResSliderY + gridResSliderH);

                // Handle slider dragging
                if (isGridResSliderHovering && mouseDown && !g_gridResSliderDragging)
                {
                    g_gridResSliderDragging = true;
                }
                if (!mouseDown)
                {
                    g_gridResSliderDragging = false;
                }

                if (g_gridResSliderDragging)
                {
                    float newNorm = (static_cast<float>(mouseX) - gridResSliderX - gridResThumbRadius) /
                                    (gridResSliderW - gridResThumbRadius * 2);
                    newNorm = glm::clamp(newNorm, 0.0f, 1.0f);
                    result.newGravityGridResolution =
                        MIN_GRID_RES + static_cast<int>(newNorm * (MAX_GRID_RES - MIN_GRID_RES));

                    // Update thumb position for immediate visual feedback
                    gridResThumbX =
                        gridResSliderX + newNorm * (gridResSliderW - gridResThumbRadius * 2) + gridResThumbRadius;
                }

                // Draw filled portion of track
                float gridResFilledWidth = gridResThumbX - gridResSliderX;
                if (gridResFilledWidth > 0)
                {
                    DrawRoundedRect(gridResSliderX,
                                    gridResTrackY,
                                    gridResFilledWidth,
                                    gridResTrackH,
                                    2.0f,
                                    0.5f,
                                    0.5f,
                                    0.6f,
                                    0.9f);
                }

                // Draw thumb
                glColor4f(isGridResSliderHovering || g_gridResSliderDragging ? 0.95f : 0.85f,
                          isGridResSliderHovering || g_gridResSliderDragging ? 0.95f : 0.85f,
                          isGridResSliderHovering || g_gridResSliderDragging ? 0.98f : 0.88f,
                          1.0f);
                glBegin(GL_TRIANGLE_FAN);
                glVertex2f(gridResThumbX, gridResThumbY);
                for (int i = 0; i <= 16; ++i)
                {
                    float angle = 2.0f * PI * static_cast<float>(i) / 16.0f;
                    glVertex2f(gridResThumbX + cos(angle) * gridResThumbRadius,
                               gridResThumbY + sin(angle) * gridResThumbRadius);
                }
                glEnd();

                currentY += gridResSliderH + PANEL_PADDING / 2;

                // ==================================
                // Warp Strength Slider
                // ==================================
                // Label with current value
                char warpLabel[32];
                snprintf(warpLabel, sizeof(warpLabel), "Warp Strength: %.1fx", timeParams.gravityWarpStrength);
                DrawText(cbX, currentY, warpLabel, 0.7f, 0.7f, 0.7f, 0.75f);
                currentY += 14;

                // Slider track
                float warpSliderX = cbX;
                float warpSliderY = currentY;
                float warpSliderW = cbItemW;
                float warpSliderH = 14.0f;
                float warpTrackH = 4.0f;
                float warpTrackY = warpSliderY + (warpSliderH - warpTrackH) / 2;

                // Track background
                DrawRoundedRect(warpSliderX, warpTrackY, warpSliderW, warpTrackH, 2.0f, 0.25f, 0.25f, 0.3f, 0.9f);

                // Calculate thumb position (0.1-5.0 range)
                const float MIN_WARP = 0.1f;
                const float MAX_WARP = 5.0f;
                float warpNormalized = (timeParams.gravityWarpStrength - MIN_WARP) / (MAX_WARP - MIN_WARP);
                warpNormalized = glm::clamp(warpNormalized, 0.0f, 1.0f);
                float warpThumbRadius = 7.0f;
                float warpThumbX = warpSliderX + warpNormalized * (warpSliderW - warpThumbRadius * 2) + warpThumbRadius;
                float warpThumbY = warpSliderY + warpSliderH / 2;

                // Check if hovering over slider area
                bool isWarpSliderHovering = (mouseX >= warpSliderX && mouseX <= warpSliderX + warpSliderW &&
                                             mouseY >= warpSliderY && mouseY <= warpSliderY + warpSliderH);

                // Handle slider dragging
                if (isWarpSliderHovering && mouseDown && !g_warpStrengthSliderDragging)
                {
                    g_warpStrengthSliderDragging = true;
                }
                if (!mouseDown)
                {
                    g_warpStrengthSliderDragging = false;
                }

                if (g_warpStrengthSliderDragging)
                {
                    float newNorm = (static_cast<float>(mouseX) - warpSliderX - warpThumbRadius) /
                                    (warpSliderW - warpThumbRadius * 2);
                    newNorm = glm::clamp(newNorm, 0.0f, 1.0f);
                    result.newGravityWarpStrength = MIN_WARP + newNorm * (MAX_WARP - MIN_WARP);

                    // Update thumb position for immediate visual feedback
                    warpThumbX = warpSliderX + newNorm * (warpSliderW - warpThumbRadius * 2) + warpThumbRadius;
                }

                // Draw filled portion of track
                float warpFilledWidth = warpThumbX - warpSliderX;
                if (warpFilledWidth > 0)
                {
                    DrawRoundedRect(warpSliderX, warpTrackY, warpFilledWidth, warpTrackH, 2.0f, 0.5f, 0.5f, 0.6f, 0.9f);
                }

                // Draw thumb
                glColor4f(isWarpSliderHovering || g_warpStrengthSliderDragging ? 0.95f : 0.85f,
                          isWarpSliderHovering || g_warpStrengthSliderDragging ? 0.95f : 0.85f,
                          isWarpSliderHovering || g_warpStrengthSliderDragging ? 0.98f : 0.88f,
                          1.0f);
                glBegin(GL_TRIANGLE_FAN);
                glVertex2f(warpThumbX, warpThumbY);
                for (int i = 0; i <= 16; ++i)
                {
                    float angle = 2.0f * PI * static_cast<float>(i) / 16.0f;
                    glVertex2f(warpThumbX + cos(angle) * warpThumbRadius, warpThumbY + sin(angle) * warpThumbRadius);
                }
                glEnd();

                currentY += warpSliderH + PANEL_PADDING / 2;
            }
        }

        // TODO: Migrate UI rendering to Vulkan
        // Separator
        // Temporarily restored OpenGL calls so UI renders
        glColor4f(0.3f, 0.3f, 0.35f, 0.8f);
        glBegin(GL_LINES);
        glVertex2f(panelX + PANEL_PADDING, currentY);
        glVertex2f(panelX + panelWidth - PANEL_PADDING, currentY);
        glEnd();
        currentY += PANEL_PADDING;

        // ==================================
        // Tree View Section
        // ==================================
        // mouseClicked is already declared at the start of DrawUserInterface
        TreeDrawResult treeResult = DrawTreeNode(solarSystemTree,
                                                 panelX + PANEL_PADDING,
                                                 currentY,
                                                 panelWidth - PANEL_PADDING,
                                                 0,
                                                 mouseX,
                                                 mouseY,
                                                 mouseClicked,
                                                 window);

        result.hoveredBody = treeResult.hoveredBody;
        result.clickedBody = treeResult.clickedBody;
        result.doubleClickedBody = treeResult.doubleClickedBody;

        // ==================================
        // Cursor Update for hovered bodies (skip in shoot mode - cursor is hidden)
        // ==================================
        // Note: Sliders, buttons, checkboxes, and accordions set their own cursors in ui-controls.cpp
        if (!g_shootModeActive && result.hoveredBody != nullptr)
        {
            g_input.setCursor(CursorType::Pointer);
        }

        // ==================================
        // Draw Details Panel (Right Side)
        // ==================================
        bool titleClicked = false;
        result.clickedLagrangeIndex = DrawDetailsPanel(screenWidth,
                                                       screenHeight,
                                                       selectedBody,
                                                       bodies,
                                                       mouseX,
                                                       mouseY,
                                                       mouseClicked,
                                                       result.clickedMoon,
                                                       result.focusOnOrbitingBody,
                                                       titleClicked);

        // Handle title click - if title was clicked, focus on selected body
        if (titleClicked && selectedBody && selectedBody->body)
        {
            result.doubleClickedBody = const_cast<CelestialBody *>(selectedBody->body);
        }
    } // End of "if UI is visible" block for left/right panels

    // ==================================
    // Draw Tooltip (for 3D hover)
    // ==================================
    if (tooltip && tooltip->show && !tooltip->text.empty())
    {
        DrawTooltip(static_cast<float>(tooltip->mouseX),
                    static_cast<float>(tooltip->mouseY),
                    tooltip->text,
                    screenWidth,
                    screenHeight);
    }

    // ==================================
    // Draw Context Menu (for right-click)
    // ==================================
    DrawContextMenu(contextMenu,
                    screenWidth,
                    screenHeight,
                    mouseX,
                    mouseY,
                    mouseClicked,
                    mouseDown, // Use mouseDown from InputController
                    result.contextMenuGhostingClicked,
                    result.contextMenuShouldClose,
                    result.followModeToggled,
                    result.surfaceViewToggled);

    // ==================================
    // Check if UI consumed the click
    // ==================================
    // This prevents camera deselection when clicking on UI elements
    // In shoot mode, only consume clicks for the shoot mode context menu
    if (g_shootModeActive)
    {
        // Only check shoot mode context menu (using fixed position)
        if (g_shootModeContextMenuOpen && (mouseClicked || mouseDown))
        {
            float contextMenuWidth = 160.0f;
            float contextMenuHeight = 44.0f;
            float contextMenuX = g_shootModeMenuX;
            float contextMenuY = g_shootModeMenuY;
            if (mouseX >= contextMenuX && mouseX <= contextMenuX + contextMenuWidth && mouseY >= contextMenuY &&
                mouseY <= contextMenuY + contextMenuHeight)
            {
                result.uiConsumedClick = true;
            }
        }
    }
    else if (mouseClicked || mouseDown)
    {
        // Check hide UI button (bottom left, arrow button)
        float hideUIButtonSizeCheck = 28.0f;
        float hideUIButtonSpacingCheck = 8.0f;
        float timePanelHeightCheck = 32.0f;
        float hideUIButtonXCheck = UI_PADDING;
        float hideUIButtonYCheck =
            screenHeight - timePanelHeightCheck - UI_PADDING + (timePanelHeightCheck - hideUIButtonSizeCheck) / 2.0f;
        if (mouseX >= hideUIButtonXCheck && mouseX <= hideUIButtonXCheck + hideUIButtonSizeCheck &&
            mouseY >= hideUIButtonYCheck && mouseY <= hideUIButtonYCheck + hideUIButtonSizeCheck)
        {
            result.uiConsumedClick = true;
        }

        // Check time control panel (bottom left) - use fixed widths to match drawing code
        if (g_uiVisible)
        {
            float dateWidthCheck = GetTextWidth("9999-12-31 23:59", 0.85f);
            float dilationLabelWidthCheck = GetTextWidth("Time Speed: ", 0.75f);
            float dilationValueWidthCheck = GetTextWidth("100.0 day/s", 0.75f);
            float sliderWidthCheck = 200.0f;
            float playPauseBtnSizeCheck = 24.0f;
            float interactionsBtnSizeCheck = 24.0f;
            float timePanelPaddingCheck = 12.0f;
            float timePanelWidthCheck = dateWidthCheck + timePanelPaddingCheck * 2 + dilationLabelWidthCheck +
                                        sliderWidthCheck + dilationValueWidthCheck + timePanelPaddingCheck +
                                        playPauseBtnSizeCheck + timePanelPaddingCheck + interactionsBtnSizeCheck +
                                        timePanelPaddingCheck;
            float timePanelXCheck = UI_PADDING + hideUIButtonSizeCheck + hideUIButtonSpacingCheck;
            float timePanelYCheck = screenHeight - timePanelHeightCheck - UI_PADDING;
            if (mouseX >= timePanelXCheck && mouseX <= timePanelXCheck + timePanelWidthCheck &&
                mouseY >= timePanelYCheck && mouseY <= timePanelYCheck + timePanelHeightCheck)
            {
                result.uiConsumedClick = true;
            }

            // Check interactions popup if open
            if (g_interactionsPopupOpen)
            {
                float popupWidthCheck = 180.0f;
                float popupHeightCheck = 120.0f; // Approximate
                float popupXCheck = timePanelXCheck + timePanelWidthCheck - popupWidthCheck / 2.0f;
                float popupYCheck = timePanelYCheck + timePanelHeightCheck + 8.0f; // Below time panel
                if (mouseX >= popupXCheck && mouseX <= popupXCheck + popupWidthCheck && mouseY >= popupYCheck &&
                    mouseY <= popupYCheck + popupHeightCheck)
                {
                    result.uiConsumedClick = true;
                }
            }

            // Check measure popup if open
            if (g_measurePopupOpen)
            {
                float popupWidthCheck = 200.0f;
                float popupHeightCheck = 120.0f; // Approximate
                float interactionsPopupXCheck = timePanelXCheck + timePanelWidthCheck - 180.0f / 2.0f;
                float interactionsPopupYCheck = timePanelYCheck + timePanelHeightCheck + 8.0f;
                float popupXCheck = interactionsPopupXCheck + 180.0f + 8.0f;
                float popupYCheck = interactionsPopupYCheck;
                if (mouseX >= popupXCheck && mouseX <= popupXCheck + popupWidthCheck && mouseY >= popupYCheck &&
                    mouseY <= popupYCheck + popupHeightCheck)
                {
                    result.uiConsumedClick = true;
                }
            }
        }

        // Check left panel (only if UI is visible)
        bool mouseInLeftPanel = false;
        if (g_uiVisible)
        {
            float panelX = UI_PADDING;
            // Position below time control panel
            float timePanelHeight = 32.0f;
            float panelY = UI_PADDING + timePanelHeight + UI_PADDING;
            float panelWidth = 220.0f;
            float maxPanelHeight = screenHeight - UI_PADDING * 2;
            mouseInLeftPanel = (mouseX >= panelX && mouseX <= panelX + panelWidth && mouseY >= panelY &&
                                mouseY <= panelY + maxPanelHeight);
        }
        if (g_uiVisible && mouseInLeftPanel)
        {
            result.uiConsumedClick = true;
        }

        // Check right details panel (if visible and UI is visible)
        if (g_uiVisible && selectedBody && selectedBody->body)
        {
            float detailsPanelWidth = 240.0f;
            float detailsPanelX = screenWidth - UI_PADDING - detailsPanelWidth;
            float detailsPanelY = UI_PADDING;
            // Estimate max panel height (we can't know exact without redrawing, use reasonable estimate)
            float detailsPanelHeight = 400.0f;
            if (mouseX >= detailsPanelX && mouseX <= detailsPanelX + detailsPanelWidth && mouseY >= detailsPanelY &&
                mouseY <= detailsPanelY + detailsPanelHeight)
            {
                result.uiConsumedClick = true;
            }
        }

        // Check context menu (if open and UI is visible)
        if (g_uiVisible && contextMenu && contextMenu->isOpen)
        {
            float contextMenuWidth = 160.0f;
            float contextMenuHeight = 80.0f;
            if (mouseX >= contextMenu->menuX && mouseX <= contextMenu->menuX + contextMenuWidth &&
                mouseY >= contextMenu->menuY && mouseY <= contextMenu->menuY + contextMenuHeight)
            {
                result.uiConsumedClick = true;
            }
        }
    }

    // Track if any slider is being dragged (to block camera input)
    result.uiSliderDragging = g_isDraggingSlider || g_contextMenuSliderDragging || g_fovSliderDragging ||
                              g_gridResSliderDragging || g_warpStrengthSliderDragging;
    result.fovSliderDragging = g_fovSliderDragging;

    // ==================================
    // Surface View Coordinate HUD
    // ==================================
    if (timeParams.isInSurfaceView)
    {
        // Format coordinates with cardinal directions
        float lat = timeParams.surfaceLatitude;
        float lon = timeParams.surfaceLongitude;

        // Normalize longitude to -180 to 180
        while (lon > 180.0f)
            lon -= 360.0f;
        while (lon < -180.0f)
            lon += 360.0f;

        char latStr[32], lonStr[32];
        snprintf(latStr, sizeof(latStr), "%.4f° %c", std::abs(lat), lat >= 0 ? 'N' : 'S');
        snprintf(lonStr, sizeof(lonStr), "%.4f° %c", std::abs(lon), lon >= 0 ? 'E' : 'W');

        std::string coordText = std::string(latStr) + "  " + std::string(lonStr);
        std::string locationText = "Surface of " + timeParams.surfaceBodyName;

        // Calculate panel dimensions
        float coordTextWidth = GetTextWidth(coordText, 1.0f);
        float locationTextWidth = GetTextWidth(locationText, 0.7f);
        float hudWidth = (glm::max)(coordTextWidth, locationTextWidth) + 32.0f;
        float hudHeight = 52.0f;
        float hudX = (screenWidth - hudWidth) / 2.0f;
        float hudY = screenHeight - hudHeight - 20.0f;

        // Semi-transparent background panel
        DrawRoundedRect(hudX, hudY, hudWidth, hudHeight, 8.0f, 0.08f, 0.08f, 0.1f, 0.85f);

        // Border glow
        glColor4f(0.3f, 0.5f, 0.7f, 0.6f);
        glLineWidth(1.5f);
        glBegin(GL_LINE_LOOP);
        float r = 8.0f;
        // Draw rounded rectangle outline
        for (int i = 0; i <= 8; i++)
        {
            float angle = PI / 2.0f + (PI / 2.0f) * (i / 8.0f);
            glVertex2f(hudX + r + cos(angle) * r, hudY + r + sin(angle) * r);
        }
        for (int i = 0; i <= 8; i++)
        {
            float angle = PI + (PI / 2.0f) * (i / 8.0f);
            glVertex2f(hudX + r + cos(angle) * r, hudY + hudHeight - r + sin(angle) * r);
        }
        for (int i = 0; i <= 8; i++)
        {
            float angle = 3 * PI / 2.0f + (PI / 2.0f) * (i / 8.0f);
            glVertex2f(hudX + hudWidth - r + cos(angle) * r, hudY + hudHeight - r + sin(angle) * r);
        }
        for (int i = 0; i <= 8; i++)
        {
            float angle = (PI / 2.0f) * (i / 8.0f);
            glVertex2f(hudX + hudWidth - r + cos(angle) * r, hudY + r + sin(angle) * r);
        }
        glEnd();
        glLineWidth(1.0f);

        // Location name (smaller, above coordinates)
        float locX = hudX + (hudWidth - locationTextWidth) / 2.0f;
        DrawText(locX, hudY + 8, locationText, 0.7f, 0.6f, 0.7f, 0.8f);

        // Coordinates (larger, centered)
        float coordX = hudX + (hudWidth - coordTextWidth) / 2.0f;
        DrawText(coordX, hudY + 26, coordText, 1.0f, 0.95f, 0.95f, 0.98f);
    }

    // ==================================
    // Shoot Mode (crosshair and context menu)
    // ==================================
    if (g_shootModeActive)
    {
        // Handle right-click for context menu using InputController
        bool rightMouseDown = inputState.mouseButtonDown[1]; // Right button
        static bool wasRightMousePressed = false;
        bool rightClick = !rightMouseDown && wasRightMousePressed;
        wasRightMousePressed = rightMouseDown;

        if (rightClick)
        {
            g_shootModeContextMenuOpen = !g_shootModeContextMenuOpen;
            if (g_shootModeContextMenuOpen)
            {
                // Store crosshair and menu positions when opening menu
                g_shootModeCrosshairX = static_cast<float>(mouseX);
                g_shootModeCrosshairY = static_cast<float>(mouseY);

                float contextMenuWidth = 160.0f;
                float contextMenuButtonHeight = 28.0f;
                float contextMenuPadding = 8.0f;
                float contextMenuHeight = contextMenuButtonHeight + contextMenuPadding * 2;

                // Calculate menu position
                g_shootModeMenuX = g_shootModeCrosshairX - contextMenuWidth / 2.0f;
                g_shootModeMenuY = g_shootModeCrosshairY - contextMenuHeight - 10.0f;

                // Clamp to screen bounds
                if (g_shootModeMenuX < UI_PADDING)
                    g_shootModeMenuX = UI_PADDING;
                if (g_shootModeMenuX + contextMenuWidth > screenWidth - UI_PADDING)
                    g_shootModeMenuX = screenWidth - UI_PADDING - contextMenuWidth;
                if (g_shootModeMenuY < UI_PADDING)
                    g_shootModeMenuY = g_shootModeCrosshairY + 10.0f; // Show below if no room above
            }
        }

        // Show cursor when context menu is open, hide otherwise
        if (g_shootModeContextMenuOpen)
        {
            if (window)
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            // Draw crosshair at fixed position (where menu was opened)
            float crosshairSize = 32.0f;
            DrawCrosshair(g_shootModeCrosshairX, g_shootModeCrosshairY, crosshairSize);
        }
        else
        {
            if (window)
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
            // Draw crosshair at mouse position (following cursor)
            float crosshairSize = 32.0f;
            DrawCrosshair(static_cast<float>(mouseX), static_cast<float>(mouseY), crosshairSize);
        }

        // Draw shoot mode context menu if open
        if (g_shootModeContextMenuOpen)
        {
            float contextMenuWidth = 160.0f;
            float contextMenuButtonHeight = 28.0f;
            float contextMenuPadding = 8.0f;
            float contextMenuHeight = contextMenuButtonHeight + contextMenuPadding * 2;

            // Use stored menu position (fixed)
            float contextMenuX = g_shootModeMenuX;
            float contextMenuY = g_shootModeMenuY;

            // Draw menu background
            DrawRoundedRect(contextMenuX,
                            contextMenuY,
                            contextMenuWidth,
                            contextMenuHeight,
                            6.0f,
                            0.18f,
                            0.18f,
                            0.22f,
                            0.95f);

            // Draw border
            glColor4f(0.4f, 0.4f, 0.5f, 0.9f);
            glLineWidth(1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(contextMenuX + 6, contextMenuY);
            glVertex2f(contextMenuX + contextMenuWidth - 6, contextMenuY);
            glVertex2f(contextMenuX + contextMenuWidth, contextMenuY + 6);
            glVertex2f(contextMenuX + contextMenuWidth, contextMenuY + contextMenuHeight - 6);
            glVertex2f(contextMenuX + contextMenuWidth - 6, contextMenuY + contextMenuHeight);
            glVertex2f(contextMenuX + 6, contextMenuY + contextMenuHeight);
            glVertex2f(contextMenuX, contextMenuY + contextMenuHeight - 6);
            glVertex2f(contextMenuX, contextMenuY + 6);
            glEnd();

            // Exit Shoot Mode button
            float exitBtnX = contextMenuX + contextMenuPadding;
            float exitBtnY = contextMenuY + contextMenuPadding;
            float exitBtnW = contextMenuWidth - contextMenuPadding * 2;
            bool isExitHovering = (mouseX >= exitBtnX && mouseX <= exitBtnX + exitBtnW && mouseY >= exitBtnY &&
                                   mouseY <= exitBtnY + contextMenuButtonHeight);

            DrawRoundedRect(exitBtnX,
                            exitBtnY,
                            exitBtnW,
                            contextMenuButtonHeight,
                            4.0f,
                            isExitHovering ? 0.4f : 0.3f,
                            isExitHovering ? 0.25f : 0.2f,
                            isExitHovering ? 0.25f : 0.2f,
                            0.9f);

            std::string exitText = "Exit Shoot Mode";
            float exitTextWidth = GetTextWidth(exitText, 0.8f);
            float exitTextX = exitBtnX + (exitBtnW - exitTextWidth) / 2.0f;
            DrawText(exitTextX, exitBtnY + 6, exitText, 0.8f, 0.9f, 0.9f, 0.95f);

            // Handle exit button click
            if (isExitHovering && mouseClicked)
            {
                g_shootModeActive = false;
                g_shootModeContextMenuOpen = false;
                g_shootModeCrosshairX = 0.0f;
                g_shootModeCrosshairY = 0.0f;
                g_shootModeMenuX = 0.0f;
                g_shootModeMenuY = 0.0f;
                if (window)
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL); // Restore cursor
            }

            // Close menu if clicking outside
            bool clickedOutsideContextMenu =
                mouseClicked && !(mouseX >= contextMenuX && mouseX <= contextMenuX + contextMenuWidth &&
                                  mouseY >= contextMenuY && mouseY <= contextMenuY + contextMenuHeight);
            if (clickedOutsideContextMenu)
            {
                g_shootModeContextMenuOpen = false;
            }
        }
        else
        {
            // If context menu is closed, restore cursor visibility (it will be hidden again next frame if still in shoot mode)
            // Actually, keep it hidden - we'll restore it when exiting shoot mode
        }
    }
    else
    {
        // Not in shoot mode - ensure cursor is visible and reset state
        if (window)
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        g_shootModeContextMenuOpen = false; // Reset context menu state
        g_shootModeCrosshairX = 0.0f;
        g_shootModeCrosshairY = 0.0f;
        g_shootModeMenuX = 0.0f;
        g_shootModeMenuY = 0.0f;
    }

    EndUI();

    return result;
}

// ==================================
// Measurement Functions
// ==================================

MeasurementMode GetMeasurementMode()
{
    return g_measurementMode;
}

void SetMeasurementMode(MeasurementMode mode)
{
    g_measurementMode = mode;
}

const MeasurementResult &GetMeasurementResult()
{
    return g_measurementResult;
}

// ==================================
// Update Measurement Result
// ==================================
// This should be called from entrypoint.cpp after raycasting
// to update the measurement result based on current mouse position
void UpdateMeasurementResult(const glm::vec3 &cameraPos,
                             const glm::vec3 &rayDir,
                             const std::vector<CelestialBody *> &bodies,
                             float maxRayDistance)
{
    // Reset result
    g_measurementResult.hasHit = false;
    g_measurementResult.hitPoint = glm::vec3(0);
    g_measurementResult.hitBody = nullptr;
    g_measurementResult.latitude = 0.0;
    g_measurementResult.longitude = 0.0;
    g_measurementResult.elevation = 0.0f;
    g_measurementResult.hasColor = false;
    g_measurementResult.colorR = 0.0f;
    g_measurementResult.colorG = 0.0f;
    g_measurementResult.colorB = 0.0f;
    g_measurementResult.colorRInt = 0;
    g_measurementResult.colorGInt = 0;
    g_measurementResult.colorBInt = 0;

    if (g_measurementMode == MeasurementMode::None)
    {
        return; // No measurement active
    }

    // Handle color picker mode separately (reads pixel color from framebuffer)
    if (g_measurementMode == MeasurementMode::ColorPicker)
    {
        // Color picker doesn't need raycasting, it reads directly from framebuffer
        // This will be handled in entrypoint.cpp where we have access to mouse coordinates
        return;
    }

    // Find closest intersection with celestial bodies
    float closestDistance = -1.0f;
    CelestialBody *hitBody = nullptr;
    glm::vec3 hitPoint(0);

    for (CelestialBody *body : bodies)
    {
        // Ray-sphere intersection
        glm::vec3 oc = cameraPos - body->position;
        float a = glm::dot(rayDir, rayDir);
        float b = 2.0f * glm::dot(oc, rayDir);
        float c = glm::dot(oc, oc) - body->displayRadius * body->displayRadius;
        float discriminant = b * b - 4.0f * a * c;

        if (discriminant >= 0.0f)
        {
            float sqrtDisc = sqrt(discriminant);
            float t1 = (-b - sqrtDisc) / (2.0f * a);
            float t2 = (-b + sqrtDisc) / (2.0f * a);

            float t = -1.0f;
            if (t1 > 0.0f)
                t = t1;
            else if (t2 > 0.0f)
                t = t2;

            if (t > 0.0f && t <= maxRayDistance)
            {
                if (closestDistance < 0.0f || t < closestDistance)
                {
                    closestDistance = t;
                    hitBody = body;
                    hitPoint = cameraPos + rayDir * t;
                }
            }
        }
    }

    if (hitBody != nullptr)
    {
        g_measurementResult.hasHit = true;
        g_measurementResult.hitPoint = hitPoint;
        g_measurementResult.hitBody = hitBody;

        // Calculate lat/lon if body has coordinate system (poleDirection and primeMeridianDirection)
        if (hitBody->poleDirection != glm::vec3(0) && hitBody->primeMeridianDirection != glm::vec3(0))
        {
            // Convert hit point to body-local coordinates
            glm::vec3 relativePos = hitPoint - hitBody->position;
            glm::vec3 normalized = glm::normalize(relativePos);

            // Get body's coordinate frame
            glm::vec3 pole = glm::normalize(hitBody->poleDirection);
            glm::vec3 primeMeridian = glm::normalize(hitBody->primeMeridianDirection);
            glm::vec3 bodyEast = glm::normalize(glm::cross(pole, primeMeridian));

            // Calculate latitude (angle from equator to pole)
            g_measurementResult.latitude = asin(glm::clamp(glm::dot(normalized, pole), -1.0f, 1.0f));

            // Calculate longitude (angle around pole axis)
            glm::vec3 equatorProj = normalized - pole * glm::dot(normalized, pole);
            float equatorProjLen = glm::length(equatorProj);
            if (equatorProjLen > 0.001f)
            {
                equatorProj = glm::normalize(equatorProj);
                float cosLon = glm::dot(equatorProj, primeMeridian);
                float sinLon = glm::dot(equatorProj, bodyEast);
                g_measurementResult.longitude = atan2(sinLon, cosLon);
            }
            else
            {
                // At pole, longitude is undefined
                g_measurementResult.longitude = 0.0;
            }

            // Calculate elevation if body is Earth and has heightmap
            if (hitBody->name == "Earth" && g_measurementMode == MeasurementMode::AltitudeDepth)
            {
                // Sample heightmap elevation (similar to economy renderer)
                extern EarthMaterial g_earthMaterial;
                if (g_earthMaterial.isInitialized() && g_earthMaterial.getElevationLoaded())
                {
                    GLuint heightmapTexture = g_earthMaterial.getHeightmapTexture();
                    if (heightmapTexture != 0)
                    {
                        // Convert lat/lon to sinusoidal UV
                        glm::vec2 equirectUV = EarthCoordinateConversion::latLonToUV(g_measurementResult.latitude,
                                                                                     g_measurementResult.longitude);
                        glm::vec2 sinuUV = EarthCoordinateConversion::equirectToSinusoidal(equirectUV);

                        // Flip V coordinate
                        sinuUV.y = 1.0f - sinuUV.y;

                        // Clamp UV
                        sinuUV.x = (glm::max)(0.0f, (glm::min)(1.0f, sinuUV.x));
                        sinuUV.y = (glm::max)(0.0f, (glm::min)(1.0f, sinuUV.y));

                        // Sample texture (simplified - we'll use a more efficient method)
                        // For now, we'll set elevation to 0 and calculate it properly in entrypoint.cpp
                        // where we have better access to the texture sampling
                        g_measurementResult.elevation = 0.0f; // Will be updated in entrypoint.cpp
                    }
                }
            }
        }
    }
}
