#pragma once

#include "../types/celestial-body.h"
#include <GLFW/glfw3.h>
#include <set>
#include <string>
#include <vector>

// Forward declarations
struct CelestialBody;

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
// Tree Drawing Result
// ==================================
struct TreeDrawResult
{
    float totalHeight;
    CelestialBody *hoveredBody;
    CelestialBody *clickedBody;
    CelestialBody *doubleClickedBody;
    bool arrowClicked;
};

// ==================================
// Tree Drawing Functions
// ==================================

// Build the hierarchical tree structure from bodies
TreeNode buildSolarSystemTree(const std::vector<CelestialBody *> &bodies);

// Draw a tree node and its children recursively
// Returns interaction results (hovered, clicked, double-clicked bodies)
TreeDrawResult DrawTreeNode(const TreeNode &node,
                            float x,
                            float y,
                            float panelWidth,
                            int depth,
                            double mouseX,
                            double mouseY,
                            bool mouseClicked,
                            GLFWwindow *window);

// Calculate tree height for panel sizing
float CalculateTreeHeight(const TreeNode &node, int depth = 0);

// Get expanded nodes set (for external access if needed)
std::set<std::string> &GetExpandedNodes();
