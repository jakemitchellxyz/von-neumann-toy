#include "vulkan-renderer.h"
#include "app-state.h"
#include "helpers/vulkan.h"
#include "input-controller.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <iterator>

// Initialize Vulkan renderer with an existing GLFW window
// The window should already be created with GLFW_NO_API
bool InitVulkanRenderer(VulkanRendererState &state, GLFWwindow *window, int width, int height)
{
    if (window == nullptr)
    {
        std::cerr << "Invalid GLFW window provided" << "\n";
        return false;
    }

    state.window = window;

    // Get required instance extensions from GLFW
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char *> requiredExtensions;
    requiredExtensions.reserve(glfwExtensionCount);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) - GLFW C API requires pointer arithmetic
    std::copy(glfwExtensions, glfwExtensions + glfwExtensionCount, std::back_inserter(requiredExtensions));

    // Create Vulkan instance first
    if (!createInstance(state.context, requiredExtensions))
    {
        std::cerr << "Failed to create Vulkan instance!" << "\n";
        return false;
    }

    // Create Vulkan surface from GLFW window
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(state.context.instance, state.window, nullptr, &surface) != VK_SUCCESS)
    {
        std::cerr << "Failed to create Vulkan surface!" << "\n";
        cleanupVulkan(state.context);
        return false;
    }

    // Initialize Vulkan (instance and surface must be created first)
    if (!initVulkan(state.context, surface, static_cast<uint32_t>(width), static_cast<uint32_t>(height)))
    {
        std::cerr << "Failed to initialize Vulkan!" << "\n";
        vkDestroySurfaceKHR(state.context.instance, surface, nullptr);
        cleanupVulkan(state.context);
        return false;
    }

    // Make Vulkan context globally accessible
    g_vulkanContext = &state.context;

    state.width = width;
    state.height = height;
    state.initialized = true;
    state.framebufferResized = false;
    state.shouldExit = false;

    // Set resize callback
    glfwSetWindowUserPointer(state.window, &state);
    glfwSetFramebufferSizeCallback(state.window, [](GLFWwindow *window, int width, int height) {
        auto *state = static_cast<VulkanRendererState *>(glfwGetWindowUserPointer(window));
        if (state != nullptr)
        {
            state->framebufferResized = true;
            state->width = width;
            state->height = height;
        }
    });

    std::cout << "Vulkan renderer initialized successfully" << "\n";
    return true;
}

// Cleanup Vulkan renderer
void CleanupVulkanRenderer(VulkanRendererState &state)
{
    if (!state.initialized)
    {
        return;
    }

    cleanupVulkan(state.context);

    // Note: Window cleanup is handled by screen-renderer
    state.window = nullptr;
    state.initialized = false;

    std::cout << "Vulkan renderer cleaned up" << "\n";
}

// Render a frame (black screen using screen shaders)
void RenderFrame(VulkanRendererState &state)
{
    if (!state.initialized)
    {
        return;
    }

    // Handle framebuffer resize
    if (state.framebufferResized)
    {
        state.framebufferResized = false;
        if (!recreateSwapchain(state.context, static_cast<uint32_t>(state.width), static_cast<uint32_t>(state.height)))
        {
            std::cerr << "Failed to recreate swapchain on resize!" << "\n";
            return;
        }
    }

    // Wait for ALL GPU work to complete before modifying shared buffers
    // This is necessary because SSBOs are shared across all frames-in-flight
    // (waitForCurrentFrameFence only waits for the frame at current index,
    // but the previous frame with a different index might still be using the SSBO)
    vkDeviceWaitIdle(state.context.device);

    // Now safe to read hover output from the *previous* frame
    // Use debouncing: only change confirmed state after N consistent frames
    uint32_t hoverMaterialID = readHoverOutput(state.context);

    if (hoverMaterialID == state.pendingHoverMaterialID)
    {
        // Same as pending - increment counter
        state.pendingHoverFrameCount++;
    }
    else
    {
        // Different value - start tracking new candidate
        state.pendingHoverMaterialID = hoverMaterialID;
        state.pendingHoverFrameCount = 1;
    }

    // Only update confirmed state if pending has been consistent for enough frames
    if (state.pendingHoverFrameCount >= VulkanRendererState::HOVER_DEBOUNCE_FRAMES &&
        state.pendingHoverMaterialID != state.confirmedHoverMaterialID)
    {
        state.confirmedHoverMaterialID = state.pendingHoverMaterialID;
    }

    // Set cursor based on confirmed hover state (do this every frame since beginFrame resets to Arrow)
    if (state.confirmedHoverMaterialID > 0)
    {
        INPUT.setCursor(CursorType::Pointer);
    }

    // Reset hover output before rendering (will be set by fragment shader if mouse hits something)
    resetHoverOutput(state.context);

    // Update SSBO buffer with current UIState from AppState
    updateSSBOBuffer(state.context, APP_STATE.uiState);

    // Update celestial objects SSBO with frustum-culled objects
    if (!APP_STATE.worldState.celestialObjects.empty())
    {
        // Get camera matrices for frustum culling
        float aspectRatio = static_cast<float>(state.width) / static_cast<float>(state.height);
        constexpr float nearPlane = 0.1f;
        constexpr float farPlane = 100000.0f;
        CameraPushConstants camConst = APP_STATE.worldState.toCameraPushConstants(aspectRatio, nearPlane, farPlane);

        updateCelestialObjectsSSBO(state.context,
                                   APP_STATE.worldState.celestialObjects,
                                   camConst.viewMatrix,
                                   camConst.projectionMatrix);
    }

    // Build UI vertex buffer from UI rendering calls
    // This should be called before beginning the frame so we have the UI geometry ready
    buildUIVertexBuffer(state.context, state.width, state.height);

    // Begin frame - acquire swapchain image and begin command buffer
    // (fence wait inside will be instant since we already waited above)
    VkCommandBuffer cmd = beginFrame(state.context);
    if (cmd == VK_NULL_HANDLE)
    {
        // Swapchain might be out of date, try to recreate it
        if (!recreateSwapchain(state.context, static_cast<uint32_t>(state.width), static_cast<uint32_t>(state.height)))
        {
            std::cerr << "Failed to recreate swapchain!" << "\n";
        }
        return;
    }

    // Set viewport and scissor dynamically (updated each frame for window resizing)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)state.context.swapchainExtent.width;
    viewport.height = (float)state.context.swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = state.context.swapchainExtent;

    // Subpass 0: Render 3D scene
    if (state.context.screenPipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.context.screenPipeline);
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Push world constants (converted from WorldState to GPU-specific struct)
        pushWorldConstants(cmd, state.context.pipelineLayout, APP_STATE.worldState.toPushConstants());

        // Push input constants (mouse position and button state)
        pushInputConstants(cmd, state.context.pipelineLayout, INPUT.getState().toPushConstants());

        // Push camera constants (view/projection matrices, position, FOV)
        float aspectRatio = static_cast<float>(state.width) / static_cast<float>(state.height);
        constexpr float nearPlane = 0.1f;     // Default near plane
        constexpr float farPlane = 100000.0f; // Far enough for solar system scale
        pushCameraConstants(cmd,
                            state.context.pipelineLayout,
                            APP_STATE.worldState.toCameraPushConstants(aspectRatio, nearPlane, farPlane));

        // Bind SSBO descriptor set (UIState)
        if (state.context.ssboDescriptorSet != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    state.context.pipelineLayout,
                                    0,
                                    1,
                                    &state.context.ssboDescriptorSet,
                                    0,
                                    nullptr);
        }

        // Bind shared fullscreen quad vertex buffer and draw
        if (state.context.fullscreenQuadBuffer.buffer != VK_NULL_HANDLE && state.context.fullscreenQuadVertexCount > 0)
        {
            VkBuffer vertexBuffers[] = {state.context.fullscreenQuadBuffer.buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdDraw(cmd, state.context.fullscreenQuadVertexCount, 1, 0, 0);
        }
    }

    // Move to subpass 1: UI overlay
    vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);

    // Subpass 1: Render UI overlay
    if (state.context.uiPipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state.context.uiPipeline);

        // Set viewport for UI (flipped Y for OpenGL compatibility)
        VkViewport uiViewport{};
        uiViewport.x = 0.0f;
        uiViewport.y = (float)state.context.swapchainExtent.height; // Start at bottom
        uiViewport.width = (float)state.context.swapchainExtent.width;
        uiViewport.height = -(float)state.context.swapchainExtent.height; // Negative height flips Y
        uiViewport.minDepth = 0.0f;
        uiViewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &uiViewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Push world constants (using UI pipeline layout)
        pushWorldConstants(cmd, state.context.uiPipelineLayout, APP_STATE.worldState.toPushConstants());

        // Push input constants (mouse position and button state)
        pushInputConstants(cmd, state.context.uiPipelineLayout, INPUT.getState().toPushConstants());

        // Bind SSBO descriptor set (UIState)
        if (state.context.ssboDescriptorSet != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    state.context.uiPipelineLayout,
                                    0,
                                    1,
                                    &state.context.ssboDescriptorSet,
                                    0,
                                    nullptr);
        }

        // Draw UI vertex buffer (built from actual UI rendering)
        if (state.context.uiVertexBuffer.buffer != VK_NULL_HANDLE && state.context.uiVertexCount > 0)
        {
            VkBuffer vertexBuffers[] = {state.context.uiVertexBuffer.buffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdDraw(cmd, state.context.uiVertexCount, 1, 0, 0);
        }
    }

    // End render pass and submit frame
    vkCmdEndRenderPass(cmd);
    endFrame(state.context);
}

// Check if window should close
bool ShouldClose(VulkanRendererState &state)
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

// Poll events
// Note: This is kept for backward compatibility, but screen-renderer should handle event polling
void PollEvents(VulkanRendererState &state)
{
    if (state.initialized && state.window != nullptr)
    {
        glfwPollEvents();
    }
}
