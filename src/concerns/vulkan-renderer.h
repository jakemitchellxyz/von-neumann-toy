#pragma once

#include "helpers/vulkan.h"
#include <GLFW/glfw3.h>

struct VulkanRendererState
{
    GLFWwindow *window = nullptr;
    VulkanContext context;
    int width = 1280;
    int height = 720;
    bool initialized = false;
    bool framebufferResized = false;
    bool shouldExit = false; // Set to true on Ctrl+C

    // Hover detection state (debounced to avoid cursor jitter)
    uint32_t confirmedHoverMaterialID = 0;          // Stable hover state used for cursor
    uint32_t pendingHoverMaterialID = 0;            // Candidate value being tested
    int pendingHoverFrameCount = 0;                 // How many frames pending value has been consistent
    static constexpr int HOVER_DEBOUNCE_FRAMES = 2; // Frames required to confirm change
};

// Initialize Vulkan renderer with an existing GLFW window
// The window should already be created with GLFW_NO_API
bool InitVulkanRenderer(VulkanRendererState &state, GLFWwindow *window, int width, int height);

// Cleanup Vulkan renderer
void CleanupVulkanRenderer(VulkanRendererState &state);

// Render a frame (black scene)
void RenderFrame(VulkanRendererState &state);

// Check if window should close
bool ShouldClose(VulkanRendererState &state);

// Poll events
void PollEvents(VulkanRendererState &state);
