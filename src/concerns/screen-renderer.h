#pragma once

#include "constants.h"
#include "settings.h" // TextureResolution enum
#include "vulkan-renderer.h"
#include <GLFW/glfw3.h>

struct ScreenRendererState
{
    GLFWwindow *window = nullptr;
    GLFWwindow *openglContextWindow = nullptr; // Hidden window for OpenGL context (UI rendering)
    VulkanRendererState vulkanRenderer;
    int width = DEFAULT_WINDOW_WIDTH;
    int height = DEFAULT_WINDOW_HEIGHT;
    bool initialized = false;
    bool shouldExit = false; // Set to true on Ctrl+C

    // Fullscreen state tracking
    bool isCurrentlyFullscreen = false;         // Track actual window state
    int windowedX = 100;                        // Windowed position X (for restore)
    int windowedY = 100;                        // Windowed position Y (for restore)
    int windowedWidth = DEFAULT_WINDOW_WIDTH;   // Windowed size (for restore)
    int windowedHeight = DEFAULT_WINDOW_HEIGHT; // Windowed size (for restore)

    // VSync frame rate limiting
    int monitorRefreshRate = 60; // Display refresh rate (Hz)
    double lastFrameTime = 0.0;  // Time of last frame render (from glfwGetTime)
};

// Initialize screen renderer (handles GLFW, Vulkan, and OpenGL setup)
// Also loads the skybox cubemap texture for implicit ray-miss background
bool InitScreenRenderer(ScreenRendererState &state,
                        int width,
                        int height,
                        const char *title,
                        TextureResolution textureRes);

// Cleanup screen renderer
void CleanupScreenRenderer(ScreenRendererState &state);

// Render a frame (Vulkan scene + OpenGL UI overlay)
void RenderFrame(ScreenRendererState &state);

// Check if window should close
bool ShouldClose(ScreenRendererState &state);

// Poll events
void PollEvents(ScreenRendererState &state);

// Get OpenGL context window for UI rendering (makes context current)
// Returns the OpenGL context window, or nullptr if not available
// Call this before UI rendering to ensure OpenGL context is current
GLFWwindow *GetOpenGLContextForUI(ScreenRendererState &state);
