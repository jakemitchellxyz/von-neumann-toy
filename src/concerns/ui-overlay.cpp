#include "ui-overlay.h"
#include "../materials/earth/earth-material.h"
#include "../materials/earth/economy/economy-renderer.h"
#include "../materials/earth/helpers/coordinate-conversion.h"
#include "../types/celestial-body.h"
#include "camera-controller.h"
#include "constants.h"
#include "font-rendering.h"
#include "settings.h"
#include "ui-controls.h"
#include "ui-icons.h"
#include "ui-primitives.h"
#include <GLFW/glfw3.h>
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

static GLFWcursor *g_defaultCursor = nullptr;

// UI visibility state
static bool g_uiVisible = true;
static GLFWcursor *g_pointerCursor = nullptr;

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

// Slider dragging state
static bool g_isDraggingSlider = false;

// Accordion state - which nodes are expanded
static std::set<std::string> g_expandedNodes;

// ==================================
// Tree Node Structure
// ==================================
struct TreeNode
{
    std::string name;
    std::string id;      // Unique ID for expansion state
    CelestialBody *body; // nullptr if this is a folder
    std::vector<TreeNode> children;
    bool isFolder;

    TreeNode(const std::string &n, const std::string &nodeId, CelestialBody *b = nullptr)
        : name(n), id(nodeId), body(b), isFolder(b == nullptr)
    {
    }
};

// ==================================
// Helper Functions
// ==================================

// Convert Julian Date to UTC calendar date string
static std::string jdToUtcString(double jd)
{
    double z = std::floor(jd + 0.5);
    double f = (jd + 0.5) - z;

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

// Find body by name in bodies vector
static CelestialBody *findBodyByName(const std::vector<CelestialBody *> &bodies, const std::string &name)
{
    for (CelestialBody *body : bodies)
    {
        if (body->name == name)
            return body;
    }
    return nullptr;
}

// Build the hierarchical tree structure
static TreeNode buildSolarSystemTree(const std::vector<CelestialBody *> &bodies)
{
    // Root node is "Solar System" folder
    TreeNode root("Solar System", "solar_system");

    // Stars category (contains only the Sun)
    TreeNode stars("Stars", "stars");
    stars.children.push_back(TreeNode("Sun", "sun", findBodyByName(bodies, "Sun")));
    root.children.push_back(stars);

    // Planets category
    TreeNode planets("Planets", "planets");

    // Mercury (no moons)
    planets.children.push_back(TreeNode("Mercury", "mercury", findBodyByName(bodies, "Mercury")));

    // Venus (no moons)
    planets.children.push_back(TreeNode("Venus", "venus", findBodyByName(bodies, "Venus")));

    // Earth with Moon
    TreeNode earth("Earth", "earth", findBodyByName(bodies, "Earth"));
    TreeNode earthMoons("Moons", "earth_moons");
    earthMoons.children.push_back(TreeNode("Moon", "moon", findBodyByName(bodies, "Moon")));
    earth.children.push_back(earthMoons);
    planets.children.push_back(earth);

    // Mars (no major moons in our list)
    planets.children.push_back(TreeNode("Mars", "mars", findBodyByName(bodies, "Mars")));

    // Jupiter with Galilean moons
    TreeNode jupiter("Jupiter", "jupiter", findBodyByName(bodies, "Jupiter"));
    TreeNode jupiterMoons("Moons", "jupiter_moons");
    jupiterMoons.children.push_back(TreeNode("Io", "io", findBodyByName(bodies, "Io")));
    jupiterMoons.children.push_back(TreeNode("Europa", "europa", findBodyByName(bodies, "Europa")));
    jupiterMoons.children.push_back(TreeNode("Ganymede", "ganymede", findBodyByName(bodies, "Ganymede")));
    jupiterMoons.children.push_back(TreeNode("Callisto", "callisto", findBodyByName(bodies, "Callisto")));
    jupiter.children.push_back(jupiterMoons);
    planets.children.push_back(jupiter);

    // Saturn with Titan
    TreeNode saturn("Saturn", "saturn", findBodyByName(bodies, "Saturn"));
    TreeNode saturnMoons("Moons", "saturn_moons");
    saturnMoons.children.push_back(TreeNode("Titan", "titan", findBodyByName(bodies, "Titan")));
    saturn.children.push_back(saturnMoons);
    planets.children.push_back(saturn);

    // Uranus (no major moons in our list)
    planets.children.push_back(TreeNode("Uranus", "uranus", findBodyByName(bodies, "Uranus")));

    // Neptune with Triton
    TreeNode neptune("Neptune", "neptune", findBodyByName(bodies, "Neptune"));
    TreeNode neptuneMoons("Moons", "neptune_moons");
    neptuneMoons.children.push_back(TreeNode("Triton", "triton", findBodyByName(bodies, "Triton")));
    neptune.children.push_back(neptuneMoons);
    planets.children.push_back(neptune);

    // Pluto with Charon
    TreeNode pluto("Pluto", "pluto", findBodyByName(bodies, "Pluto"));
    TreeNode plutoMoons("Moons", "pluto_moons");
    plutoMoons.children.push_back(TreeNode("Charon", "charon", findBodyByName(bodies, "Charon")));
    pluto.children.push_back(plutoMoons);
    planets.children.push_back(pluto);

    root.children.push_back(planets);

    // Comets category (empty for now)
    TreeNode comets("Comets", "comets");
    root.children.push_back(comets);

    // Swarms category (empty for now - for asteroid belts etc)
    TreeNode swarms("Swarms", "swarms");
    root.children.push_back(swarms);

    return root;
}

// ==================================
// Initialization
// ==================================
void InitUI()
{
    g_lastFPSTime = glfwGetTime();
    g_frameCount = 0;
    g_currentFPS = 0;
    g_lastClickTime = 0.0;
    g_lastClickedBody = nullptr;
    g_isDraggingSlider = false;

    g_defaultCursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    g_pointerCursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

    // Expand solar system tree by default (root, sun, and planets)
    g_expandedNodes.insert("solar_system");
    g_expandedNodes.insert("sun");
    g_expandedNodes.insert("planets");
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
    glBegin(mode);
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
    glEnd();
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
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}

// ==================================
// Drawing Functions
// ==================================
// DrawText, GetTextWidth, and DrawNumber are now in font-rendering.cpp
// DrawRoundedRect, DrawArrow, DrawFolderIcon, DrawPlayIcon, DrawPauseIcon,
// DrawHandIcon, DrawMeasureIcon, DrawShootIcon, DrawCrosshair are now in ui-icons.cpp and ui-primitives.cpp

// DrawSlider and DrawTooltip are now in ui-controls.cpp and ui-primitives.cpp

// ==================================
// Tree Drawing with Interaction
// ==================================
struct TreeDrawResult
{
    float totalHeight;
    CelestialBody *hoveredBody;
    CelestialBody *clickedBody;
    CelestialBody *doubleClickedBody;
    bool arrowClicked;
};

static TreeDrawResult DrawTreeNode(const TreeNode &node,
                                   float x,
                                   float y,
                                   float panelWidth,
                                   int depth,
                                   double mouseX,
                                   double mouseY,
                                   bool mouseClicked,
                                   GLFWwindow *window)
{
    TreeDrawResult result = {0, nullptr, nullptr, nullptr, false};

    float indent = depth * INDENT_WIDTH;
    float itemX = x + indent;
    float itemWidth = panelWidth - indent - PANEL_PADDING;
    float itemHeight = ITEM_HEIGHT - 2;
    float currentY = y;

    bool hasChildren = !node.children.empty();
    bool isExpanded = g_expandedNodes.count(node.id) > 0;

    // Check hover state
    bool isHovered =
        (mouseX >= itemX && mouseX <= itemX + itemWidth && mouseY >= currentY && mouseY <= currentY + itemHeight);

    // Arrow area
    float arrowX = itemX;
    float arrowY = currentY + (itemHeight - ARROW_SIZE) / 2;
    bool isHoveringArrow = hasChildren && (mouseX >= arrowX && mouseX <= arrowX + ARROW_SIZE + 4 &&
                                           mouseY >= currentY && mouseY <= currentY + itemHeight);

    // Draw hover background
    if (isHovered && node.body)
    {
        DrawRoundedRect(itemX, currentY, itemWidth, itemHeight, 4.0f, 0.25f, 0.28f, 0.35f, 0.9f);
    }

    // Draw arrow if has children
    float textStartX = itemX + 4;
    if (hasChildren)
    {
        DrawArrow(arrowX, arrowY, ARROW_SIZE, isExpanded, 0.6f, 0.6f, 0.65f);
        textStartX = itemX + ARROW_SIZE + 6;
    }

    // Draw folder icon for folders
    if (node.isFolder && !node.body)
    {
        DrawFolderIcon(textStartX, currentY + 3, ITEM_HEIGHT - 8, 0.7f, 0.6f, 0.4f);
        textStartX += ITEM_HEIGHT - 4;
    }

    // Draw text
    float textColor = isHovered ? 1.0f : (node.body ? 0.85f : 0.7f);
    float textScale = node.body ? 0.85f : 0.75f;
    DrawText(textStartX, currentY + 5, node.name, textScale, textColor, textColor, textColor);

    // Handle click
    if (isHovered && mouseClicked && !g_isDraggingSlider)
    {
        if (isHoveringArrow && hasChildren)
        {
            // Toggle expansion
            if (isExpanded)
            {
                g_expandedNodes.erase(node.id);
            }
            else
            {
                g_expandedNodes.insert(node.id);
            }
            result.arrowClicked = true;
        }
        else if (node.body)
        {
            // Click on body
            double currentTime = glfwGetTime();
            double timeSinceLastClick = currentTime - g_lastClickTime;

            if (timeSinceLastClick <= DOUBLE_CLICK_THRESHOLD && g_lastClickedBody == node.body)
            {
                result.doubleClickedBody = node.body;
                g_lastClickedBody = nullptr;
            }
            else
            {
                result.clickedBody = node.body;
                g_lastClickedBody = node.body;
            }
            g_lastClickTime = currentTime;
        }
        else if (hasChildren)
        {
            // Click on folder name also toggles
            if (isExpanded)
            {
                g_expandedNodes.erase(node.id);
            }
            else
            {
                g_expandedNodes.insert(node.id);
            }
            result.arrowClicked = true;
        }
    }

    if (isHovered && node.body)
    {
        result.hoveredBody = node.body;
    }

    currentY += ITEM_HEIGHT;
    result.totalHeight += ITEM_HEIGHT;

    // Draw children if expanded
    if (hasChildren && isExpanded)
    {
        for (const auto &child : node.children)
        {
            TreeDrawResult childResult = DrawTreeNode(child,
                                                      x,
                                                      currentY,
                                                      panelWidth,
                                                      depth + 1,
                                                      mouseX,
                                                      mouseY,
                                                      mouseClicked && !result.arrowClicked,
                                                      window);
            currentY += childResult.totalHeight;
            result.totalHeight += childResult.totalHeight;

            if (childResult.hoveredBody)
                result.hoveredBody = childResult.hoveredBody;
            if (childResult.clickedBody)
                result.clickedBody = childResult.clickedBody;
            if (childResult.doubleClickedBody)
                result.doubleClickedBody = childResult.doubleClickedBody;
        }
    }

    return result;
}

// Calculate tree height for panel sizing
static float CalculateTreeHeight(const TreeNode &node, int depth = 0)
{
    float height = ITEM_HEIGHT;

    bool isExpanded = g_expandedNodes.count(node.id) > 0;
    if (!node.children.empty() && isExpanded)
    {
        for (const auto &child : node.children)
        {
            height += CalculateTreeHeight(child, depth + 1);
        }
    }

    return height;
}

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

// Lagrange accordion expansion state
static bool g_lagrangeAccordionExpanded = true;
// Moons accordion expansion state
static bool g_moonsAccordionExpanded = true;

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
        if (g_lagrangeAccordionExpanded)
        {
            lagrangeHeight += 5 * buttonHeight + sectionPadding; // 5 Lagrange points
        }
    }

    // Add Moons section height if this planet has moons
    float moonsHeight = 0.0f;
    if (!selected->moons.empty())
    {
        moonsHeight = lineHeight + sectionPadding; // Accordion header + separator
        if (g_moonsAccordionExpanded)
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
        titleBgR = glm::min(1.0f, titleBgR + 0.15f);
        titleBgG = glm::min(1.0f, titleBgG + 0.15f);
        titleBgB = glm::min(1.0f, titleBgB + 0.15f);
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

        // Separator
        glColor4f(0.3f, 0.3f, 0.35f, 0.8f);
        glBegin(GL_LINES);
        glVertex2f(panelX + PANEL_PADDING, currentY);
        glVertex2f(panelX + panelWidth - PANEL_PADDING, currentY);
        glEnd();
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
                                g_lagrangeAccordionExpanded,
                                mouseX,
                                mouseY,
                                mouseClicked))
        {
            g_lagrangeAccordionExpanded = !g_lagrangeAccordionExpanded;
        }

        currentY += headerHeight;

        // Draw Lagrange points if expanded
        if (g_lagrangeAccordionExpanded)
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

        // Separator
        glColor4f(0.3f, 0.3f, 0.35f, 0.8f);
        glBegin(GL_LINES);
        glVertex2f(panelX + PANEL_PADDING, currentY);
        glVertex2f(panelX + panelWidth - PANEL_PADDING, currentY);
        glEnd();
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
                                g_moonsAccordionExpanded,
                                mouseX,
                                mouseY,
                                mouseClicked))
        {
            g_moonsAccordionExpanded = !g_moonsAccordionExpanded;
        }

        currentY += headerHeight;

        // Draw moons if expanded
        if (g_moonsAccordionExpanded)
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
        false,   // fullscreenToggled
        -1,      // newTextureResolution
        false,   // followModeToggled
        false,   // surfaceViewToggled
        false    // uiHideToggled
    };

    BeginUI(screenWidth, screenHeight);

    // Track mouse click state (needed for button interactions throughout the UI)
    static bool wasMousePressedEarly = false;
    bool isMousePressedEarly = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool mouseClicked = !isMousePressedEarly && wasMousePressedEarly;
    wasMousePressedEarly = isMousePressedEarly;

    bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    // ==================================
    // Time Control Panel (Top Left)
    // ==================================
    // Calculate panel dimensions with fixed widths to prevent layout shifts
    std::string currentEpoch = jdToUtcString(timeParams.currentJD);
    std::string dilationStr = formatTimeDilation(*timeParams.timeDilation);

    // Use fixed widths based on maximum expected string lengths
    // Date format: "YYYY-MM-DD HH:MM" - use worst case "9999-12-31 23:59"
    float dateWidth = GetTextWidth("9999-12-31 23:59", 0.85f);
    float dilationLabelWidth = GetTextWidth("Time Speed: ", 0.75f);
    // Time dilation format varies, use a reasonable maximum like "100.0 day/s"
    float dilationValueWidth = GetTextWidth("100.0 day/s", 0.75f);

    float sliderWidth = 200.0f;        // Fixed slider width
    float playPauseBtnSize = 24.0f;    // Size of play/pause button
    float interactionsBtnSize = 24.0f; // Size of interactions button
    float timePanelPadding = 12.0f;
    float timePanelHeight = 32.0f;
    float timePanelWidth = dateWidth + timePanelPadding * 2 + dilationLabelWidth + sliderWidth + dilationValueWidth +
                           timePanelPadding + playPauseBtnSize + timePanelPadding + interactionsBtnSize +
                           timePanelPadding;

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

    glColor3f(0.95f, 0.95f, 0.95f);
    glLineWidth(2.0f);

    if (g_uiVisible)
    {
        // Left arrow (<)
        glBegin(GL_LINES);
        glVertex2f(arrowX + arrowSize * 0.7f, arrowY);
        glVertex2f(arrowX + arrowSize * 0.3f, arrowY + arrowSize * 0.5f);
        glVertex2f(arrowX + arrowSize * 0.3f, arrowY + arrowSize * 0.5f);
        glVertex2f(arrowX + arrowSize * 0.7f, arrowY + arrowSize);
        glEnd();
    }
    else
    {
        // Right arrow (>)
        glBegin(GL_LINES);
        glVertex2f(arrowX + arrowSize * 0.3f, arrowY);
        glVertex2f(arrowX + arrowSize * 0.7f, arrowY + arrowSize * 0.5f);
        glVertex2f(arrowX + arrowSize * 0.7f, arrowY + arrowSize * 0.5f);
        glVertex2f(arrowX + arrowSize * 0.3f, arrowY + arrowSize);
        glEnd();
    }
    glLineWidth(1.0f);

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

        // Draw time dilation section (using fixed date width, so position is stable)
        float dilationStartX = dateX + dateWidth + timePanelPadding * 2;
        float dilationY = timePanelY + (timePanelHeight - 16.0f) / 2.0f;

        // Label
        DrawText(dilationStartX, dilationY + 2, "Time Speed: ", 0.75f, 0.7f, 0.7f, 0.75f);

        // Slider
        float sliderX = dilationStartX + dilationLabelWidth + 6.0f;
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
        float valueX = sliderX + sliderWidth + 6.0f;
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

            // Draw border
            glColor4f(0.4f, 0.4f, 0.5f, 0.9f);
            glLineWidth(1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(popupX + 6, popupY);
            glVertex2f(popupX + popupWidth - 6, popupY);
            glVertex2f(popupX + popupWidth, popupY + 6);
            glVertex2f(popupX + popupWidth, popupY + popupHeight - 6);
            glVertex2f(popupX + popupWidth - 6, popupY + popupHeight);
            glVertex2f(popupX + 6, popupY + popupHeight);
            glVertex2f(popupX, popupY + popupHeight - 6);
            glVertex2f(popupX, popupY + 6);
            glEnd();

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

            // Draw border
            glColor4f(0.4f, 0.4f, 0.5f, 0.9f);
            glLineWidth(1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(popupX + 6, popupY);
            glVertex2f(popupX + popupWidth - 6, popupY);
            glVertex2f(popupX + popupWidth, popupY + 6);
            glVertex2f(popupX + popupWidth, popupY + popupHeight - 6);
            glVertex2f(popupX + popupWidth - 6, popupY + popupHeight);
            glVertex2f(popupX + 6, popupY + popupHeight);
            glVertex2f(popupX, popupY + popupHeight - 6);
            glVertex2f(popupX, popupY + 6);
            glEnd();

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

        // Settings accordion state (includes texture resolution dropdown + FOV slider + texture toggles + atmosphere toggle)
        static bool g_settingsAccordionExpanded = false;
        float settingsContentHeight =
            g_settingsAccordionExpanded
                ? (dropdownHeight + restartWarningHeight + fovSliderHeight + checkboxHeight * 3 + PANEL_PADDING * 5)
                : 0.0f; // 3 checkboxes: heightmap, normalMap, roughness
        float settingsSectionHeight = accordionHeaderHeight + settingsContentHeight + PANEL_PADDING;

        // Visualizations accordion state (closed by default)
        static bool g_controlsAccordionExpanded = false;
        // 13 checkboxes: orbits, axes, barycenters, lagrange, coord grids, magnetic fields, constellations, celestial grid, constellation figures, constellation bounds, force vectors, gravity grid, sun spot, wireframe (atmosphere moved to Settings)
        float controlsContentHeight = g_controlsAccordionExpanded ? (checkboxHeight * 13 + PANEL_PADDING * 2) : 0.0f;
        float controlsSectionHeight = accordionHeaderHeight + controlsContentHeight + PANEL_PADDING;

        float treeHeight = CalculateTreeHeight(solarSystemTree);
        float totalHeight = fullscreenBtnHeight + fpsHeight + settingsSectionHeight + controlsSectionHeight +
                            treeHeight + PANEL_PADDING * 6;

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

        // Separator
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

        // Separator
        glColor4f(0.3f, 0.3f, 0.35f, 0.8f);
        glBegin(GL_LINES);
        glVertex2f(panelX + PANEL_PADDING, currentY);
        glVertex2f(panelX + panelWidth - PANEL_PADDING, currentY);
        glEnd();
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
                                g_settingsAccordionExpanded,
                                mouseX,
                                mouseY,
                                mouseClicked))
        {
            g_settingsAccordionExpanded = !g_settingsAccordionExpanded;
        }

        currentY += accordionHeaderHeight;

        // Draw settings content if expanded
        if (g_settingsAccordionExpanded)
        {
            float settingsX = panelX + PANEL_PADDING + 8;
            float settingsW = panelWidth - PANEL_PADDING * 2 - 16;

            // Texture Resolution label
            DrawText(settingsX, currentY + 2, "Texture Resolution", 0.7f, 0.7f, 0.7f, 0.75f);
            currentY += 14;

            // Dropdown for texture resolution
            static bool g_resolutionDropdownOpen = false;
            float dropBtnY = currentY;
            float dropBtnH = dropdownHeight - 4;

            // Get current resolution name
            const char *currentResName = getResolutionName(timeParams.textureResolution);

            // Dropdown button background
            bool isDropdownHovering = (mouseX >= settingsX && mouseX <= settingsX + settingsW && mouseY >= dropBtnY &&
                                       mouseY <= dropBtnY + dropBtnH);

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

            // Dropdown arrow
            float dropArrowX = settingsX + settingsW - 14;
            float dropArrowY = dropBtnY + dropBtnH / 2;
            glColor3f(0.6f, 0.6f, 0.7f);
            glBegin(GL_TRIANGLES);
            glVertex2f(dropArrowX, dropArrowY - 3);
            glVertex2f(dropArrowX + 8, dropArrowY - 3);
            glVertex2f(dropArrowX + 4, dropArrowY + 3);
            glEnd();

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

            // Slider track
            float fovSliderX = settingsX;
            float fovSliderY = currentY;
            float fovSliderW = settingsW;
            float fovSliderH = 14.0f;
            float fovTrackH = 4.0f;
            float fovTrackY = fovSliderY + (fovSliderH - fovTrackH) / 2;

            // Track background
            DrawRoundedRect(fovSliderX, fovTrackY, fovSliderW, fovTrackH, 2.0f, 0.25f, 0.25f, 0.3f, 0.9f);

            // Calculate thumb position (5-120 degree range, increments of 5)
            const float MIN_FOV = 5.0f;
            const float MAX_FOV = 120.0f;
            const float FOV_INCREMENT = 5.0f;
            float fovNormalized = (timeParams.currentFOV - MIN_FOV) / (MAX_FOV - MIN_FOV);
            fovNormalized = glm::clamp(fovNormalized, 0.0f, 1.0f);
            float fovThumbRadius = 7.0f;
            float fovThumbX = fovSliderX + fovNormalized * (fovSliderW - fovThumbRadius * 2) + fovThumbRadius;
            float fovThumbY = fovSliderY + fovSliderH / 2;

            // Check if hovering over slider area
            bool isFovSliderHovering = (mouseX >= fovSliderX && mouseX <= fovSliderX + fovSliderW &&
                                        mouseY >= fovSliderY && mouseY <= fovSliderY + fovSliderH);

            // Handle slider dragging
            if (isFovSliderHovering && mouseDown && !g_fovSliderDragging)
            {
                g_fovSliderDragging = true;
            }
            if (!mouseDown)
            {
                g_fovSliderDragging = false;
            }

            if (g_fovSliderDragging)
            {
                float newNorm =
                    (static_cast<float>(mouseX) - fovSliderX - fovThumbRadius) / (fovSliderW - fovThumbRadius * 2);
                newNorm = glm::clamp(newNorm, 0.0f, 1.0f);
                float rawFOV = MIN_FOV + newNorm * (MAX_FOV - MIN_FOV);
                // Snap to increments of 5 and clamp to valid range
                result.newFOV = glm::clamp(round(rawFOV / FOV_INCREMENT) * FOV_INCREMENT, MIN_FOV, MAX_FOV);

                // Update thumb position for immediate visual feedback
                fovThumbX = fovSliderX + newNorm * (fovSliderW - fovThumbRadius * 2) + fovThumbRadius;
            }

            // Draw filled portion of track
            float fovFilledWidth = fovThumbX - fovSliderX;
            if (fovFilledWidth > 0)
            {
                DrawRoundedRect(fovSliderX, fovTrackY, fovFilledWidth, fovTrackH, 2.0f, 0.4f, 0.5f, 0.7f, 0.9f);
            }

            // Draw thumb
            glColor4f(isFovSliderHovering || g_fovSliderDragging ? 0.95f : 0.85f,
                      isFovSliderHovering || g_fovSliderDragging ? 0.95f : 0.85f,
                      isFovSliderHovering || g_fovSliderDragging ? 0.98f : 0.88f,
                      1.0f);
            glBegin(GL_TRIANGLE_FAN);
            glVertex2f(fovThumbX, fovThumbY);
            for (int i = 0; i <= 16; ++i)
            {
                float angle = 2.0f * PI * static_cast<float>(i) / 16.0f;
                glVertex2f(fovThumbX + cos(angle) * fovThumbRadius, fovThumbY + sin(angle) * fovThumbRadius);
            }
            glEnd();

            currentY += fovSliderH + PANEL_PADDING / 2;

            currentY += PANEL_PADDING;

            // Texture Effect Toggles
            float cbX = panelX + PANEL_PADDING + 8;
            float cbSize = 14.0f;
            float cbItemW = panelWidth - PANEL_PADDING * 2 - 8;

            // Wireframe Mode Checkbox
            float wireframeY = currentY;
            float wireframeBoxY = wireframeY + (checkboxHeight - cbSize) / 2;
            bool isWireframeHovering = (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= wireframeY &&
                                        mouseY <= wireframeY + checkboxHeight);

            // Checkbox box
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, wireframeBoxY);
            glVertex2f(cbX + cbSize, wireframeBoxY);
            glVertex2f(cbX + cbSize, wireframeBoxY + cbSize);
            glVertex2f(cbX, wireframeBoxY + cbSize);
            glEnd();
            glColor4f(isWireframeHovering ? 0.6f : 0.4f,
                      isWireframeHovering ? 0.6f : 0.4f,
                      isWireframeHovering ? 0.65f : 0.45f,
                      0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, wireframeBoxY);
            glVertex2f(cbX + cbSize, wireframeBoxY);
            glVertex2f(cbX + cbSize, wireframeBoxY + cbSize);
            glVertex2f(cbX, wireframeBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showWireframe)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, wireframeBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, wireframeBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, wireframeBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, wireframeBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     wireframeY + 4,
                     "Wireframe",
                     0.75f,
                     isWireframeHovering ? 0.95f : 0.8f,
                     isWireframeHovering ? 0.95f : 0.8f,
                     isWireframeHovering ? 0.95f : 0.8f);

            if (isWireframeHovering && mouseClicked)
            {
                result.wireframeToggled = true;
            }
            currentY += checkboxHeight;

            // FXAA Antialiasing Checkbox
            float fxaaY = currentY;
            float fxaaBoxY = fxaaY + (checkboxHeight - cbSize) / 2;
            bool isFXAAHovering =
                (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= fxaaY && mouseY <= fxaaY + checkboxHeight);

            // Checkbox box
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, fxaaBoxY);
            glVertex2f(cbX + cbSize, fxaaBoxY);
            glVertex2f(cbX + cbSize, fxaaBoxY + cbSize);
            glVertex2f(cbX, fxaaBoxY + cbSize);
            glEnd();
            glColor4f(isFXAAHovering ? 0.6f : 0.4f, isFXAAHovering ? 0.6f : 0.4f, isFXAAHovering ? 0.65f : 0.45f, 0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, fxaaBoxY);
            glVertex2f(cbX + cbSize, fxaaBoxY);
            glVertex2f(cbX + cbSize, fxaaBoxY + cbSize);
            glVertex2f(cbX, fxaaBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.fxaaEnabled)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, fxaaBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, fxaaBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, fxaaBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, fxaaBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     fxaaY + 4,
                     "FXAA Antialiasing",
                     0.75f,
                     isFXAAHovering ? 0.95f : 0.8f,
                     isFXAAHovering ? 0.95f : 0.8f,
                     isFXAAHovering ? 0.95f : 0.8f);

            if (isFXAAHovering && mouseClicked)
            {
                result.fxaaToggled = true;
            }
            currentY += checkboxHeight;

            // VSync Checkbox
            float vsyncY = currentY;
            float vsyncBoxY = vsyncY + (checkboxHeight - cbSize) / 2;
            bool isVSyncHovering =
                (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= vsyncY && mouseY <= vsyncY + checkboxHeight);

            // Checkbox box
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, vsyncBoxY);
            glVertex2f(cbX + cbSize, vsyncBoxY);
            glVertex2f(cbX + cbSize, vsyncBoxY + cbSize);
            glVertex2f(cbX, vsyncBoxY + cbSize);
            glEnd();
            glColor4f(isVSyncHovering ? 0.6f : 0.4f,
                      isVSyncHovering ? 0.6f : 0.4f,
                      isVSyncHovering ? 0.65f : 0.45f,
                      0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, vsyncBoxY);
            glVertex2f(cbX + cbSize, vsyncBoxY);
            glVertex2f(cbX + cbSize, vsyncBoxY + cbSize);
            glVertex2f(cbX, vsyncBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.vsyncEnabled)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, vsyncBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, vsyncBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, vsyncBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, vsyncBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     vsyncY + 4,
                     "VSync (Uncap FPS)",
                     0.75f,
                     isVSyncHovering ? 0.95f : 0.8f,
                     isVSyncHovering ? 0.95f : 0.8f,
                     isVSyncHovering ? 0.95f : 0.8f);

            if (isVSyncHovering && mouseClicked)
            {
                result.vsyncToggled = true;
            }
            currentY += checkboxHeight;

            // Heightmap toggle
            float heightmapY = currentY;
            bool isHeightmapHovering = (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= heightmapY &&
                                        mouseY <= heightmapY + checkboxHeight);
            float heightmapBoxY = heightmapY + (checkboxHeight - cbSize) / 2;
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, heightmapBoxY);
            glVertex2f(cbX + cbSize, heightmapBoxY);
            glVertex2f(cbX + cbSize, heightmapBoxY + cbSize);
            glVertex2f(cbX, heightmapBoxY + cbSize);
            glEnd();
            glColor4f(isHeightmapHovering ? 0.6f : 0.4f,
                      isHeightmapHovering ? 0.6f : 0.4f,
                      isHeightmapHovering ? 0.65f : 0.45f,
                      0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, heightmapBoxY);
            glVertex2f(cbX + cbSize, heightmapBoxY);
            glVertex2f(cbX + cbSize, heightmapBoxY + cbSize);
            glVertex2f(cbX, heightmapBoxY + cbSize);
            glEnd();
            extern EarthMaterial g_earthMaterial;
            if (g_earthMaterial.getUseHeightmap())
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, heightmapBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, heightmapBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, heightmapBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, heightmapBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }
            DrawText(cbX + cbSize + 6,
                     heightmapY + 4,
                     "Height Map",
                     0.75f,
                     isHeightmapHovering ? 0.95f : 0.8f,
                     isHeightmapHovering ? 0.95f : 0.8f,
                     isHeightmapHovering ? 0.95f : 0.8f);
            if (isHeightmapHovering && mouseClicked)
            {
                result.heightmapToggled = true;
            }
            currentY += checkboxHeight;

            // Normal Map toggle
            float normalMapY = currentY;
            bool isNormalMapHovering = (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= normalMapY &&
                                        mouseY <= normalMapY + checkboxHeight);
            float normalMapBoxY = normalMapY + (checkboxHeight - cbSize) / 2;
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, normalMapBoxY);
            glVertex2f(cbX + cbSize, normalMapBoxY);
            glVertex2f(cbX + cbSize, normalMapBoxY + cbSize);
            glVertex2f(cbX, normalMapBoxY + cbSize);
            glEnd();
            glColor4f(isNormalMapHovering ? 0.6f : 0.4f,
                      isNormalMapHovering ? 0.6f : 0.4f,
                      isNormalMapHovering ? 0.65f : 0.45f,
                      0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, normalMapBoxY);
            glVertex2f(cbX + cbSize, normalMapBoxY);
            glVertex2f(cbX + cbSize, normalMapBoxY + cbSize);
            glVertex2f(cbX, normalMapBoxY + cbSize);
            glEnd();
            if (g_earthMaterial.getUseNormalMap())
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, normalMapBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, normalMapBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, normalMapBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, normalMapBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }
            DrawText(cbX + cbSize + 6,
                     normalMapY + 4,
                     "Normal Map",
                     0.75f,
                     isNormalMapHovering ? 0.95f : 0.8f,
                     isNormalMapHovering ? 0.95f : 0.8f,
                     isNormalMapHovering ? 0.95f : 0.8f);
            if (isNormalMapHovering && mouseClicked)
            {
                result.normalMapToggled = true;
            }
            currentY += checkboxHeight;

            // Roughness toggle
            float roughnessY = currentY;
            bool isRoughnessHovering = (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= roughnessY &&
                                        mouseY <= roughnessY + checkboxHeight);
            float roughnessBoxY = roughnessY + (checkboxHeight - cbSize) / 2;
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, roughnessBoxY);
            glVertex2f(cbX + cbSize, roughnessBoxY);
            glVertex2f(cbX + cbSize, roughnessBoxY + cbSize);
            glVertex2f(cbX, roughnessBoxY + cbSize);
            glEnd();
            glColor4f(isRoughnessHovering ? 0.6f : 0.4f,
                      isRoughnessHovering ? 0.6f : 0.4f,
                      isRoughnessHovering ? 0.65f : 0.45f,
                      0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, roughnessBoxY);
            glVertex2f(cbX + cbSize, roughnessBoxY);
            glVertex2f(cbX + cbSize, roughnessBoxY + cbSize);
            glVertex2f(cbX, roughnessBoxY + cbSize);
            glEnd();
            if (g_earthMaterial.getUseSpecular())
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, roughnessBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, roughnessBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, roughnessBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, roughnessBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }
            DrawText(cbX + cbSize + 6,
                     roughnessY + 4,
                     "Roughness",
                     0.75f,
                     isRoughnessHovering ? 0.95f : 0.8f,
                     isRoughnessHovering ? 0.95f : 0.8f,
                     isRoughnessHovering ? 0.95f : 0.8f);
            if (isRoughnessHovering && mouseClicked)
            {
                result.roughnessToggled = true;
            }
            currentY += checkboxHeight;
        }

        // ==================================
        // Visualizations Accordion Section
        // ==================================
        currentY += PANEL_PADDING / 2;

        // Separator
        glColor4f(0.3f, 0.3f, 0.35f, 0.8f);
        glBegin(GL_LINES);
        glVertex2f(panelX + PANEL_PADDING, currentY);
        glVertex2f(panelX + panelWidth - PANEL_PADDING, currentY);
        glEnd();
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
                                g_controlsAccordionExpanded,
                                mouseX,
                                mouseY,
                                mouseClicked))
        {
            g_controlsAccordionExpanded = !g_controlsAccordionExpanded;
        }

        currentY += accordionHeaderHeight;

        // Draw visualizations checkboxes if expanded
        if (g_controlsAccordionExpanded)
        {
            float cbX = panelX + PANEL_PADDING + 8;
            float cbSize = 14.0f;
            float cbItemW = panelWidth - PANEL_PADDING * 2 - 8;

            // ==================================
            // Orbit Lines Checkbox
            // ==================================
            float orbitsY = currentY;
            bool isOrbitsHovering =
                (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= orbitsY && mouseY <= orbitsY + checkboxHeight);

            // Checkbox box
            float orbitsBoxY = orbitsY + (checkboxHeight - cbSize) / 2;
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, orbitsBoxY);
            glVertex2f(cbX + cbSize, orbitsBoxY);
            glVertex2f(cbX + cbSize, orbitsBoxY + cbSize);
            glVertex2f(cbX, orbitsBoxY + cbSize);
            glEnd();

            // Checkbox border
            glColor4f(isOrbitsHovering ? 0.6f : 0.4f,
                      isOrbitsHovering ? 0.6f : 0.4f,
                      isOrbitsHovering ? 0.65f : 0.45f,
                      0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, orbitsBoxY);
            glVertex2f(cbX + cbSize, orbitsBoxY);
            glVertex2f(cbX + cbSize, orbitsBoxY + cbSize);
            glVertex2f(cbX, orbitsBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showOrbits)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, orbitsBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, orbitsBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, orbitsBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, orbitsBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     orbitsY + 4,
                     "Orbit Lines",
                     0.75f,
                     isOrbitsHovering ? 0.95f : 0.8f,
                     isOrbitsHovering ? 0.95f : 0.8f,
                     isOrbitsHovering ? 0.95f : 0.8f);

            if (isOrbitsHovering && mouseClicked)
            {
                result.orbitsToggled = true;
            }

            currentY += checkboxHeight;

            // ==================================
            // Rotation Axes Checkbox
            // ==================================
            float axesY = currentY;
            bool isAxesHovering =
                (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= axesY && mouseY <= axesY + checkboxHeight);

            // Checkbox box
            float axesBoxY = axesY + (checkboxHeight - cbSize) / 2;
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, axesBoxY);
            glVertex2f(cbX + cbSize, axesBoxY);
            glVertex2f(cbX + cbSize, axesBoxY + cbSize);
            glVertex2f(cbX, axesBoxY + cbSize);
            glEnd();

            // Checkbox border
            glColor4f(isAxesHovering ? 0.6f : 0.4f, isAxesHovering ? 0.6f : 0.4f, isAxesHovering ? 0.65f : 0.45f, 0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, axesBoxY);
            glVertex2f(cbX + cbSize, axesBoxY);
            glVertex2f(cbX + cbSize, axesBoxY + cbSize);
            glVertex2f(cbX, axesBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showRotationAxes)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, axesBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, axesBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, axesBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, axesBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     axesY + 4,
                     "Rotation Axes",
                     0.75f,
                     isAxesHovering ? 0.95f : 0.8f,
                     isAxesHovering ? 0.95f : 0.8f,
                     isAxesHovering ? 0.95f : 0.8f);

            if (isAxesHovering && mouseClicked)
            {
                result.axesToggled = true;
            }

            currentY += checkboxHeight;

            // ==================================
            // Barycenters Checkbox
            // ==================================
            float baryY = currentY;
            bool isBaryHovering =
                (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= baryY && mouseY <= baryY + checkboxHeight);

            // Checkbox box
            float baryBoxY = baryY + (checkboxHeight - cbSize) / 2;
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, baryBoxY);
            glVertex2f(cbX + cbSize, baryBoxY);
            glVertex2f(cbX + cbSize, baryBoxY + cbSize);
            glVertex2f(cbX, baryBoxY + cbSize);
            glEnd();

            // Checkbox border
            glColor4f(isBaryHovering ? 0.6f : 0.4f, isBaryHovering ? 0.6f : 0.4f, isBaryHovering ? 0.65f : 0.45f, 0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, baryBoxY);
            glVertex2f(cbX + cbSize, baryBoxY);
            glVertex2f(cbX + cbSize, baryBoxY + cbSize);
            glVertex2f(cbX, baryBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showBarycenters)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, baryBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, baryBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, baryBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, baryBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     baryY + 4,
                     "Barycenters",
                     0.75f,
                     isBaryHovering ? 0.95f : 0.8f,
                     isBaryHovering ? 0.95f : 0.8f,
                     isBaryHovering ? 0.95f : 0.8f);

            if (isBaryHovering && mouseClicked)
            {
                result.barycentersToggled = true;
            }

            currentY += checkboxHeight;

            // ==================================
            // Lagrange Points Checkbox
            // ==================================
            float lagrangeY = currentY;
            bool isLagrangeHovering = (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= lagrangeY &&
                                       mouseY <= lagrangeY + checkboxHeight);

            // Checkbox box
            float lagrangeBoxY = lagrangeY + (checkboxHeight - cbSize) / 2;
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, lagrangeBoxY);
            glVertex2f(cbX + cbSize, lagrangeBoxY);
            glVertex2f(cbX + cbSize, lagrangeBoxY + cbSize);
            glVertex2f(cbX, lagrangeBoxY + cbSize);
            glEnd();

            // Checkbox border
            glColor4f(isLagrangeHovering ? 0.6f : 0.4f,
                      isLagrangeHovering ? 0.6f : 0.4f,
                      isLagrangeHovering ? 0.65f : 0.45f,
                      0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, lagrangeBoxY);
            glVertex2f(cbX + cbSize, lagrangeBoxY);
            glVertex2f(cbX + cbSize, lagrangeBoxY + cbSize);
            glVertex2f(cbX, lagrangeBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showLagrangePoints)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, lagrangeBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, lagrangeBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, lagrangeBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, lagrangeBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     lagrangeY + 4,
                     "Lagrange Points",
                     0.75f,
                     isLagrangeHovering ? 0.95f : 0.8f,
                     isLagrangeHovering ? 0.95f : 0.8f,
                     isLagrangeHovering ? 0.95f : 0.8f);

            if (isLagrangeHovering && mouseClicked)
            {
                result.lagrangePointsToggled = true;
            }

            currentY += checkboxHeight + PANEL_PADDING / 2;

            // ==================================
            // Coordinate Grids Checkbox
            // ==================================
            float coordGridY = currentY;
            float coordGridBoxY = coordGridY + (checkboxHeight - cbSize) / 2;
            bool isCoordGridHovering = (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= coordGridY &&
                                        mouseY <= coordGridY + checkboxHeight);

            // Checkbox box
            glColor3f(isCoordGridHovering ? 0.5f : 0.4f,
                      isCoordGridHovering ? 0.5f : 0.4f,
                      isCoordGridHovering ? 0.6f : 0.5f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, coordGridBoxY);
            glVertex2f(cbX + cbSize, coordGridBoxY);
            glVertex2f(cbX + cbSize, coordGridBoxY + cbSize);
            glVertex2f(cbX, coordGridBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showCoordinateGrids)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, coordGridBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, coordGridBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, coordGridBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, coordGridBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     coordGridY + 4,
                     "Coord Grids",
                     0.75f,
                     isCoordGridHovering ? 0.95f : 0.8f,
                     isCoordGridHovering ? 0.95f : 0.8f,
                     isCoordGridHovering ? 0.95f : 0.8f);

            if (isCoordGridHovering && mouseClicked)
            {
                result.coordGridsToggled = true;
            }

            currentY += checkboxHeight + PANEL_PADDING / 2;

            // ==================================
            // Magnetic Fields Checkbox
            // ==================================
            float magFieldY = currentY;
            float magFieldBoxY = magFieldY + (checkboxHeight - cbSize) / 2;
            bool isMagFieldHovering = (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= magFieldY &&
                                       mouseY <= magFieldY + checkboxHeight);

            // Checkbox box
            glColor3f(isMagFieldHovering ? 0.5f : 0.4f,
                      isMagFieldHovering ? 0.5f : 0.4f,
                      isMagFieldHovering ? 0.6f : 0.5f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, magFieldBoxY);
            glVertex2f(cbX + cbSize, magFieldBoxY);
            glVertex2f(cbX + cbSize, magFieldBoxY + cbSize);
            glVertex2f(cbX, magFieldBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showMagneticFields)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, magFieldBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, magFieldBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, magFieldBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, magFieldBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     magFieldY + 4,
                     "Magnetic Fields",
                     0.75f,
                     isMagFieldHovering ? 0.95f : 0.8f,
                     isMagFieldHovering ? 0.95f : 0.8f,
                     isMagFieldHovering ? 0.95f : 0.8f);

            if (isMagFieldHovering && mouseClicked)
            {
                result.magneticFieldsToggled = true;
            }

            currentY += checkboxHeight + PANEL_PADDING / 2;

            // ==================================
            // Constellations Checkbox
            // ==================================
            float constellY = currentY;
            float constellBoxY = constellY + (checkboxHeight - cbSize) / 2;
            bool isConstellHovering = (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= constellY &&
                                       mouseY <= constellY + checkboxHeight);

            // Checkbox box
            glColor3f(isConstellHovering ? 0.5f : 0.4f,
                      isConstellHovering ? 0.5f : 0.4f,
                      isConstellHovering ? 0.6f : 0.5f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, constellBoxY);
            glVertex2f(cbX + cbSize, constellBoxY);
            glVertex2f(cbX + cbSize, constellBoxY + cbSize);
            glVertex2f(cbX, constellBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showConstellations)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, constellBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, constellBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, constellBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, constellBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     constellY + 4,
                     "Constellations",
                     0.75f,
                     isConstellHovering ? 0.95f : 0.8f,
                     isConstellHovering ? 0.95f : 0.8f,
                     isConstellHovering ? 0.95f : 0.8f);

            if (isConstellHovering && mouseClicked)
            {
                result.constellationsToggled = true;
            }

            currentY += checkboxHeight + PANEL_PADDING / 2;

            // ==================================
            // Celestial Grid Checkbox
            // ==================================
            float gridY = currentY;
            float gridBoxY = gridY + (checkboxHeight - cbSize) / 2;
            bool isGridHovering =
                (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= gridY && mouseY <= gridY + checkboxHeight);

            // Checkbox box
            glColor3f(isGridHovering ? 0.5f : 0.4f, isGridHovering ? 0.5f : 0.4f, isGridHovering ? 0.6f : 0.5f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, gridBoxY);
            glVertex2f(cbX + cbSize, gridBoxY);
            glVertex2f(cbX + cbSize, gridBoxY + cbSize);
            glVertex2f(cbX, gridBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (g_showCelestialGrid)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, gridBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, gridBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, gridBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, gridBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     gridY + 4,
                     "Celestial Grid",
                     0.75f,
                     isGridHovering ? 0.95f : 0.8f,
                     isGridHovering ? 0.95f : 0.8f,
                     isGridHovering ? 0.95f : 0.8f);

            if (isGridHovering && mouseClicked)
            {
                result.constellationGridToggled = true;
            }

            currentY += checkboxHeight + PANEL_PADDING / 2;

            // ==================================
            // Constellation Figures Checkbox
            // ==================================
            float figuresY = currentY;
            float figuresBoxY = figuresY + (checkboxHeight - cbSize) / 2;
            bool isFiguresHovering =
                (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= figuresY && mouseY <= figuresY + checkboxHeight);

            // Checkbox box
            glColor3f(isFiguresHovering ? 0.5f : 0.4f,
                      isFiguresHovering ? 0.5f : 0.4f,
                      isFiguresHovering ? 0.6f : 0.5f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, figuresBoxY);
            glVertex2f(cbX + cbSize, figuresBoxY);
            glVertex2f(cbX + cbSize, figuresBoxY + cbSize);
            glVertex2f(cbX, figuresBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (g_showConstellationFigures)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, figuresBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, figuresBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, figuresBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, figuresBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     figuresY + 4,
                     "Constellation Figures",
                     0.75f,
                     isFiguresHovering ? 0.95f : 0.8f,
                     isFiguresHovering ? 0.95f : 0.8f,
                     isFiguresHovering ? 0.95f : 0.8f);

            if (isFiguresHovering && mouseClicked)
            {
                result.constellationFiguresToggled = true;
            }

            currentY += checkboxHeight + PANEL_PADDING / 2;

            // ==================================
            // Constellation Bounds Checkbox
            // ==================================
            float boundsY = currentY;
            float boundsBoxY = boundsY + (checkboxHeight - cbSize) / 2;
            bool isBoundsHovering =
                (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= boundsY && mouseY <= boundsY + checkboxHeight);

            // Checkbox box
            glColor3f(isBoundsHovering ? 0.5f : 0.4f, isBoundsHovering ? 0.5f : 0.4f, isBoundsHovering ? 0.6f : 0.5f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, boundsBoxY);
            glVertex2f(cbX + cbSize, boundsBoxY);
            glVertex2f(cbX + cbSize, boundsBoxY + cbSize);
            glVertex2f(cbX, boundsBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (g_showConstellationBounds)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, boundsBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, boundsBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, boundsBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, boundsBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     boundsY + 4,
                     "Constellation Bounds",
                     0.75f,
                     isBoundsHovering ? 0.95f : 0.8f,
                     isBoundsHovering ? 0.95f : 0.8f,
                     isBoundsHovering ? 0.95f : 0.8f);

            if (isBoundsHovering && mouseClicked)
            {
                result.constellationBoundsToggled = true;
            }

            currentY += checkboxHeight + PANEL_PADDING / 2;

            // ==================================
            // Force Vectors Checkbox
            // ==================================
            float forceVecY = currentY;
            float forceVecBoxY = forceVecY + (checkboxHeight - cbSize) / 2;
            bool isForceVecHovering = (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= forceVecY &&
                                       mouseY <= forceVecY + checkboxHeight);

            // Checkbox box
            glColor3f(isForceVecHovering ? 0.5f : 0.4f,
                      isForceVecHovering ? 0.5f : 0.4f,
                      isForceVecHovering ? 0.6f : 0.5f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, forceVecBoxY);
            glVertex2f(cbX + cbSize, forceVecBoxY);
            glVertex2f(cbX + cbSize, forceVecBoxY + cbSize);
            glVertex2f(cbX, forceVecBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showForceVectors)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, forceVecBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, forceVecBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, forceVecBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, forceVecBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     forceVecY + 4,
                     "Force Vectors",
                     0.75f,
                     isForceVecHovering ? 0.95f : 0.8f,
                     isForceVecHovering ? 0.95f : 0.8f,
                     isForceVecHovering ? 0.95f : 0.8f);

            if (isForceVecHovering && mouseClicked)
            {
                result.forceVectorsToggled = true;
            }

            currentY += checkboxHeight + PANEL_PADDING / 2;


            // ==================================
            // Gravity Grid Checkbox
            // ==================================
            float gravGridY = currentY;
            float gravGridBoxY = gravGridY + (checkboxHeight - cbSize) / 2;
            bool isGravGridHovering = (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= gravGridY &&
                                       mouseY <= gravGridY + checkboxHeight);

            // Checkbox box
            glColor3f(isGravGridHovering ? 0.5f : 0.4f,
                      isGravGridHovering ? 0.5f : 0.4f,
                      isGravGridHovering ? 0.6f : 0.5f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, gravGridBoxY);
            glVertex2f(cbX + cbSize, gravGridBoxY);
            glVertex2f(cbX + cbSize, gravGridBoxY + cbSize);
            glVertex2f(cbX, gravGridBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showGravityGrid)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, gravGridBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, gravGridBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, gravGridBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, gravGridBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     gravGridY + 4,
                     "Gravity Grid",
                     0.75f,
                     isGravGridHovering ? 0.95f : 0.8f,
                     isGravGridHovering ? 0.95f : 0.8f,
                     isGravGridHovering ? 0.95f : 0.8f);

            if (isGravGridHovering && mouseClicked)
            {
                result.gravityGridToggled = true;
            }

            currentY += checkboxHeight + PANEL_PADDING / 2;

            // ==================================
            // Sun Spot Checkbox
            // ==================================
            float sunSpotY = currentY;
            float sunSpotBoxY = sunSpotY + (checkboxHeight - cbSize) / 2;
            bool isSunSpotHovering =
                (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= sunSpotY && mouseY <= sunSpotY + checkboxHeight);

            // Checkbox box
            glColor3f(isSunSpotHovering ? 0.5f : 0.4f,
                      isSunSpotHovering ? 0.5f : 0.4f,
                      isSunSpotHovering ? 0.6f : 0.5f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, sunSpotBoxY);
            glVertex2f(cbX + cbSize, sunSpotBoxY);
            glVertex2f(cbX + cbSize, sunSpotBoxY + cbSize);
            glVertex2f(cbX, sunSpotBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (timeParams.showSunSpot)
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, sunSpotBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, sunSpotBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, sunSpotBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, sunSpotBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     sunSpotY + 4,
                     "Sun Spot",
                     0.75f,
                     isSunSpotHovering ? 0.95f : 0.8f,
                     isSunSpotHovering ? 0.95f : 0.8f,
                     isSunSpotHovering ? 0.95f : 0.8f);

            if (isSunSpotHovering && mouseClicked)
            {
                result.sunSpotToggled = true;
            }

            currentY += checkboxHeight + PANEL_PADDING / 2;

            // ==================================
            // Cities Checkbox
            // ==================================
            float citiesY = currentY;
            bool isCitiesHovering =
                (mouseX >= cbX && mouseX <= cbX + cbItemW && mouseY >= citiesY && mouseY <= citiesY + checkboxHeight);

            // Checkbox box
            float citiesBoxY = citiesY + (checkboxHeight - cbSize) / 2;
            glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
            glBegin(GL_QUADS);
            glVertex2f(cbX, citiesBoxY);
            glVertex2f(cbX + cbSize, citiesBoxY);
            glVertex2f(cbX + cbSize, citiesBoxY + cbSize);
            glVertex2f(cbX, citiesBoxY + cbSize);
            glEnd();

            // Checkbox border
            glColor4f(isCitiesHovering ? 0.6f : 0.4f,
                      isCitiesHovering ? 0.6f : 0.4f,
                      isCitiesHovering ? 0.65f : 0.45f,
                      0.9f);
            glLineWidth(1.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(cbX, citiesBoxY);
            glVertex2f(cbX + cbSize, citiesBoxY);
            glVertex2f(cbX + cbSize, citiesBoxY + cbSize);
            glVertex2f(cbX, citiesBoxY + cbSize);
            glEnd();

            // Checkmark if enabled
            if (g_economyRenderer.getShowCityLabels())
            {
                glColor3f(0.3f, 0.9f, 0.4f);
                glLineWidth(2.0f);
                glBegin(GL_LINES);
                glVertex2f(cbX + 3, citiesBoxY + cbSize * 0.5f);
                glVertex2f(cbX + cbSize * 0.4f, citiesBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize * 0.4f, citiesBoxY + cbSize - 3);
                glVertex2f(cbX + cbSize - 2, citiesBoxY + 2);
                glEnd();
                glLineWidth(1.0f);
            }

            DrawText(cbX + cbSize + 6,
                     citiesY + 4,
                     "Cities",
                     0.75f,
                     isCitiesHovering ? 0.95f : 0.8f,
                     isCitiesHovering ? 0.95f : 0.8f,
                     isCitiesHovering ? 0.95f : 0.8f);

            if (isCitiesHovering && mouseClicked)
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

        // Separator
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
        // Cursor Update (skip in shoot mode - cursor is hidden)
        // ==================================
        if (!g_shootModeActive)
        {
            bool isOverSlider = (mouseX >= panelX + PANEL_PADDING && mouseX <= panelX + panelWidth - PANEL_PADDING &&
                                 mouseY >= panelY + fpsHeight + PANEL_PADDING + 28 &&
                                 mouseY <= panelY + fpsHeight + PANEL_PADDING + 44);

            if (result.hoveredBody != nullptr || isOverSlider || g_isDraggingSlider)
            {
                glfwSetCursor(window, g_pointerCursor);
            }
            else
            {
                glfwSetCursor(window, g_defaultCursor);
            }
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
    bool mouseDownForContext = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    DrawContextMenu(contextMenu,
                    screenWidth,
                    screenHeight,
                    mouseX,
                    mouseY,
                    mouseClicked,
                    mouseDownForContext,
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
        if (g_shootModeContextMenuOpen && (mouseClicked || mouseDownForContext))
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
    else if (mouseClicked || mouseDownForContext)
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
        float hudWidth = std::max(coordTextWidth, locationTextWidth) + 32.0f;
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
        // Handle right-click for context menu
        bool rightMouseClicked = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        static bool wasRightMousePressed = false;
        bool rightClick = !rightMouseClicked && wasRightMousePressed;
        wasRightMousePressed = rightMouseClicked;

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
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            // Draw crosshair at fixed position (where menu was opened)
            float crosshairSize = 32.0f;
            DrawCrosshair(g_shootModeCrosshairX, g_shootModeCrosshairY, crosshairSize);
        }
        else
        {
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
                        sinuUV.x = std::max(0.0f, std::min(1.0f, sinuUV.x));
                        sinuUV.y = std::max(0.0f, std::min(1.0f, sinuUV.y));

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
