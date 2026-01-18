#include "ui-tree.h"
#include "font-rendering.h"
#include "ui-icons.h"
#include "ui-primitives.h"
#include <GLFW/glfw3.h>
#include <cmath>

// Undefine Windows.h macros that conflict with our functions
#ifdef DrawText
#undef DrawText
#endif

// Constants (shared with ui-overlay.cpp)
static const float ITEM_HEIGHT = 22.0f;
static const float PANEL_PADDING = 8.0f;
static const float INDENT_WIDTH = 16.0f;
static const float ARROW_SIZE = 8.0f;
static const double DOUBLE_CLICK_THRESHOLD = 0.3;

// State (shared with ui-overlay.cpp)
static std::set<std::string> g_expandedNodes;
static double g_lastClickTime = 0.0;
static CelestialBody *g_lastClickedBody = nullptr;

// External reference to slider dragging state (defined in ui-overlay.cpp)
extern bool g_isDraggingSlider;

// ==================================
// Helper Functions
// ==================================

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

// ==================================
// Tree Building
// ==================================

TreeNode buildSolarSystemTree(const std::vector<CelestialBody *> &bodies)
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
// Tree Drawing
// ==================================

TreeDrawResult DrawTreeNode(const TreeNode &node,
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

float CalculateTreeHeight(const TreeNode &node, int depth)
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

std::set<std::string> &GetExpandedNodes()
{
    return g_expandedNodes;
}
