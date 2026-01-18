#include "screen-renderer.h"
#include "app-state.h"
#include "helpers/vulkan.h"
#include "input-controller.h"
#include "ui-overlay.h"
#include "vulkan-renderer.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <iostream>
#include <thread>

// Initialize screen renderer (handles GLFW, Vulkan, and OpenGL setup)
// Also loads the skybox cubemap texture for implicit ray-miss background
bool InitScreenRenderer(ScreenRendererState &state,
                        int width,
                        int height,
                        const char *title,
                        TextureResolution textureRes)
{
    // Initialize GLFW
    if (glfwInit() == 0)
    {
        std::cerr << "Failed to initialize GLFW" << "\n";
        return false;
    }

    // Create a window with Vulkan (no OpenGL context)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    state.window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (state.window == nullptr)
    {
        std::cerr << "Failed to create GLFW window" << "\n";
        glfwTerminate();
        return false;
    }

    // Initialize Vulkan renderer with the created window
    // This will handle all Vulkan initialization (instance, surface, device, etc.)
    if (!InitVulkanRenderer(state.vulkanRenderer, state.window, width, height))
    {
        std::cerr << "Failed to initialize Vulkan renderer!" << "\n";
        glfwDestroyWindow(state.window);
        glfwTerminate();
        return false;
    }

    // ========================================================================
    // Load skybox cubemap texture for implicit ray-miss background
    // ========================================================================
    // The skybox is not drawn explicitly - it's used as the fallback color
    // in single-pass-screen.frag when a ray doesn't hit any object.
    // This provides an implicit infinite-distance background of the celestial sky.
    if (g_vulkanContext != nullptr)
    {
        std::string resolutionFolder = getResolutionFolderName(textureRes);
        std::string skyboxPath = "celestial-skybox/" + resolutionFolder + "/milkyway_combined.hdr";

        if (loadSkyboxTexture(*g_vulkanContext, skyboxPath))
        {
            updateSkyboxDescriptorSet(*g_vulkanContext);
            std::cout << "Skybox cubemap loaded for ray-miss background\n";
        }
        else
        {
            std::cerr << "Warning: Failed to load skybox texture, ray-miss will show black\n";
        }

        // ========================================================================
        // Load Earth material textures for NAIF ID 399
        // ========================================================================
        // When a ray hits Earth in single-pass-screen.frag, these textures are
        // sampled to provide realistic Earth rendering with proper colors,
        // normal mapping, nightlights, and specular effects.
        std::string earthTexturePath = "earth-textures";
        int currentMonth = 1; // TODO: Get current month from Julian date

        if (loadEarthTextures(*g_vulkanContext, earthTexturePath, resolutionFolder, currentMonth))
        {
            updateEarthDescriptorSet(*g_vulkanContext);
            std::cout << "Earth textures loaded for NAIF ID 399\n";
        }
        else
        {
            std::cerr << "Warning: Failed to load Earth textures, Earth will render with default color\n";
        }
    }

    // Update resize callback to update screen renderer state as well
    glfwSetWindowUserPointer(state.window, &state);
    glfwSetFramebufferSizeCallback(state.window, [](GLFWwindow *window, int newWidth, int newHeight) {
        auto *screenState = static_cast<ScreenRendererState *>(glfwGetWindowUserPointer(window));
        if (screenState != nullptr)
        {
            screenState->vulkanRenderer.framebufferResized = true;
            screenState->vulkanRenderer.width = newWidth;
            screenState->vulkanRenderer.height = newHeight;
            screenState->width = newWidth;
            screenState->height = newHeight;
        }
        // Update InputController with new window size
        INPUT.onWindowResize(newWidth, newHeight);
    });

    // Initialize input controller with the window
    INPUT.initialize(state.window);

    // Create a hidden OpenGL context window for UI rendering
    // This allows us to use OpenGL for UI while using Vulkan for the main rendering
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Hidden window
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE); // Compatibility profile for old OpenGL functions
    state.openglContextWindow = glfwCreateWindow(width, height, "", nullptr, nullptr);
    if (state.openglContextWindow == nullptr)
    {
        std::cerr << "Failed to create OpenGL context for UI rendering!" << "\n";
        // Continue anyway - UI just won't render
    }
    else
    {
        // Make the OpenGL context current to load OpenGL functions
        glfwMakeContextCurrent(state.openglContextWindow);

        // Store the OpenGL context window globally for UI rendering
        extern void SetOpenGLContextWindow(GLFWwindow * window);
        SetOpenGLContextWindow(state.openglContextWindow);

        // Load OpenGL extensions (needed for UI rendering)
        extern bool loadGLExtensions();
        if (!loadGLExtensions())
        {
            std::cerr << "Warning: Failed to load some OpenGL extensions for UI rendering" << "\n";
        }

        // Switch back to the Vulkan window (no context needed for Vulkan)
        glfwMakeContextCurrent(nullptr);
    }

    // Initialize UI system
    InitUI();

    state.width = width;
    state.height = height;
    state.initialized = true;
    state.shouldExit = false;

    // Store initial windowed position and size for fullscreen restore
    glfwGetWindowPos(state.window, &state.windowedX, &state.windowedY);
    state.windowedWidth = width;
    state.windowedHeight = height;
    state.isCurrentlyFullscreen = false;

    // Get monitor refresh rate for VSync frame limiting
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    if (monitor != nullptr)
    {
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        if (mode != nullptr)
        {
            state.monitorRefreshRate = mode->refreshRate;
            std::cout << "Monitor refresh rate: " << state.monitorRefreshRate << " Hz\n";
        }
    }

    // Initialize frame timing
    state.lastFrameTime = glfwGetTime();

    std::cout << "Screen renderer initialized successfully" << "\n";
    return true;
}

// Cleanup screen renderer
void CleanupScreenRenderer(ScreenRendererState &state)
{
    if (!state.initialized)
    {
        return;
    }

    // Cleanup Vulkan renderer (this handles Vulkan cleanup)
    CleanupVulkanRenderer(state.vulkanRenderer);

    // Cleanup OpenGL context window
    if (state.openglContextWindow != nullptr)
    {
        glfwDestroyWindow(state.openglContextWindow);
        state.openglContextWindow = nullptr;
    }

    // Destroy Vulkan window
    if (state.window != nullptr)
    {
        glfwDestroyWindow(state.window);
        state.window = nullptr;
    }

    // Terminate GLFW (this will clean up all windows and contexts)
    glfwTerminate();

    state.vulkanRenderer.initialized = false;
    state.initialized = false;

    std::cout << "Screen renderer cleaned up" << "\n";
}

// Render a frame (Vulkan scene + OpenGL UI overlay)
void RenderFrame(ScreenRendererState &state)
{
    if (!state.initialized)
    {
        return;
    }

    // VSync frame rate limiting
    // If VSync is enabled, wait until enough time has passed for the next frame
    if (APP_STATE.uiState.vsyncEnabled != 0u)
    {
        double targetFrameTime = 1.0 / static_cast<double>(state.monitorRefreshRate);
        double currentTime = glfwGetTime();
        double elapsedTime = currentTime - state.lastFrameTime;

        // If we haven't reached the target frame time, sleep to limit frame rate
        if (elapsedTime < targetFrameTime)
        {
            double sleepTime = targetFrameTime - elapsedTime;
            // Use high-precision sleep for accurate frame timing
            // Sleep for slightly less than needed, then spin-wait for precision
            if (sleepTime > 0.001)
            {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(static_cast<long long>((sleepTime - 0.001) * 1000000.0)));
            }
            // Spin-wait for the remaining time for higher precision
            while (glfwGetTime() - state.lastFrameTime < targetFrameTime)
            {
                // Spin wait
            }
        }
    }

    // Update last frame time
    state.lastFrameTime = glfwGetTime();

    // First, render the Vulkan frame
    RenderFrame(state.vulkanRenderer);

    // Render OpenGL UI overlay on top of Vulkan output
    // Note: This renders to the OpenGL context window, which is hidden.
    // For proper compositing, we would need to render UI to a texture and composite in Vulkan.
    // For now, this at least allows the UI rendering code to execute without crashing.
    if (state.openglContextWindow != nullptr)
    {
        // Make OpenGL context current for UI rendering
        glfwMakeContextCurrent(state.openglContextWindow);

        // TODO: Call DrawUserInterface here once we have the required parameters (bodies, timeParams, etc.)
        // For now, the UI rendering functions will be called from the main game loop when those parameters are available.
        // The OpenGL context is now set up and ready for UI rendering.

        // Note: We don't swap buffers here because the window is hidden.
        // When DrawUserInterface is called from the game loop, it will handle its own rendering.

        // Switch back to no context (Vulkan doesn't need a context)
        glfwMakeContextCurrent(nullptr);
    }

    // End input frame - clear per-frame events
    INPUT.endFrame();
}

// Check if window should close
bool ShouldClose(ScreenRendererState &state)
{
    // Check if Ctrl+C was pressed
    if (state.shouldExit)
    {
        return true;
    }

    if (!state.initialized || state.window == nullptr)
    {
        return true;
    }

    return glfwWindowShouldClose(state.window) != 0;
}

// Update fullscreen state based on APP_STATE.uiState.isFullscreen
static void UpdateFullscreenState(ScreenRendererState &state)
{
    bool wantFullscreen = (APP_STATE.uiState.isFullscreen != 0u);

    // Only act if the desired state differs from the actual state
    if (wantFullscreen == state.isCurrentlyFullscreen)
    {
        return;
    }

    if (wantFullscreen)
    {
        // Save windowed position and size before going fullscreen
        glfwGetWindowPos(state.window, &state.windowedX, &state.windowedY);
        glfwGetWindowSize(state.window, &state.windowedWidth, &state.windowedHeight);

        // Get the primary monitor and its video mode
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);

        // Switch to fullscreen
        glfwSetWindowMonitor(state.window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);

        // Update monitor refresh rate for VSync frame limiting
        state.monitorRefreshRate = mode->refreshRate;

        state.isCurrentlyFullscreen = true;
        std::cout << "Entered fullscreen mode (" << mode->width << "x" << mode->height << " @ " << mode->refreshRate
                  << " Hz)\n";
    }
    else
    {
        // Restore windowed mode with saved position and size
        glfwSetWindowMonitor(state.window,
                             nullptr,
                             state.windowedX,
                             state.windowedY,
                             state.windowedWidth,
                             state.windowedHeight,
                             GLFW_DONT_CARE);

        // Get current monitor refresh rate for VSync (use primary monitor)
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        if (monitor != nullptr)
        {
            const GLFWvidmode *mode = glfwGetVideoMode(monitor);
            if (mode != nullptr)
            {
                state.monitorRefreshRate = mode->refreshRate;
            }
        }

        state.isCurrentlyFullscreen = false;
        std::cout << "Exited fullscreen mode (" << state.windowedWidth << "x" << state.windowedHeight << ")\n";
    }
}

// Poll events
void PollEvents(ScreenRendererState &state)
{
    if (state.initialized && state.window != nullptr)
    {
        // Begin input frame BEFORE polling events
        // This clears per-frame state, then callbacks set new values
        INPUT.beginFrame();

        // Poll events - triggers GLFW callbacks that update input state
        glfwPollEvents();

        // Check for F11 key to toggle fullscreen (with debounce)
        static bool f11WasPressed = false;
        if (glfwGetKey(state.window, GLFW_KEY_F11) == GLFW_PRESS)
        {
            if (!f11WasPressed)
            {
                // Toggle fullscreen in AppState
                APP_STATE.uiState.isFullscreen = APP_STATE.uiState.isFullscreen != 0u ? 0u : 1u;
                f11WasPressed = true;
            }
        }
        else
        {
            f11WasPressed = false;
        }

        // Update fullscreen state based on AppState
        UpdateFullscreenState(state);
    }
}

// Get OpenGL context window for UI rendering (makes context current)
GLFWwindow *GetOpenGLContextForUI(ScreenRendererState &state)
{
    if (state.openglContextWindow != nullptr && state.initialized)
    {
        glfwMakeContextCurrent(state.openglContextWindow);
        return state.openglContextWindow;
    }
    return nullptr;
}
