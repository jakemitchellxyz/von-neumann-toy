#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

// Forward declarations for app state
struct WorldState;
struct WorldPushConstants;
struct UIState;
struct InputPushConstants;
struct CameraPushConstants;
struct CelestialObject;

// Vulkan shader module wrapper
struct VulkanShader
{
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
};

// Vulkan buffer wrapper
struct VulkanBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

// Main Vulkan context structure
struct VulkanContext
{
    // Core Vulkan objects
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily = UINT32_MAX;
    uint32_t presentQueueFamily = UINT32_MAX;

    // Surface and swapchain
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent = {0, 0};
    uint32_t currentSwapchainImageIndex = 0;

    // Render pass and pipelines
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline screenPipeline = VK_NULL_HANDLE; // Subpass 0: 3D scene
    VkPipeline uiPipeline = VK_NULL_HANDLE;     // Subpass 1: 2D UI overlay
    VkPipelineLayout uiPipelineLayout = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchainFramebuffers;

    // Shared fullscreen quad vertex buffer (used by both screen and UI pipelines)
    VulkanBuffer fullscreenQuadBuffer = {};
    uint32_t fullscreenQuadVertexCount = 6; // 2 triangles = 6 vertices

    // Test UI vertex buffer (temporary for testing)
    VulkanBuffer testUIVertexBuffer = {};
    uint32_t testUIVertexCount = 0;

    // Actual UI vertex buffer (built each frame from UI rendering calls)
    VulkanBuffer uiVertexBuffer = {};
    uint32_t uiVertexCount = 0;
    VkDeviceSize uiVertexBufferSize = 0; // Current allocated size

    // Triangle count tracking (for UI display)
    uint32_t worldTriangleCount = 0; // Triangles in 3D world geometry
    uint32_t uiTriangleCount = 0;    // Triangles in UI geometry
    uint32_t totalTriangleCount = 0; // Total triangles rendered this frame

    // Command buffers
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    // Synchronization
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 1;

    // Debug messenger (for validation layers)
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    // Track temp shader files for cleanup
    std::vector<std::string> tempShaderFiles;

    // ==================================
    // SSBO for UIState (shader settings)
    // ==================================
    VulkanBuffer uiStateSSBO = {};     // SSBO buffer containing UIState (binding 0)
    VulkanBuffer hoverOutputSSBO = {}; // SSBO buffer for hover detection output (binding 1)
    VulkanBuffer minDistanceSSBO = {}; // SSBO buffer for min surface distance readback (binding 9)
    VkDescriptorSetLayout ssboDescriptorSetLayout = VK_NULL_HANDLE; // Descriptor set layout for SSBOs
    VkDescriptorPool ssboDescriptorPool = VK_NULL_HANDLE;           // Descriptor pool for SSBOs
    VkDescriptorSet ssboDescriptorSet = VK_NULL_HANDLE;             // Descriptor set for SSBO bindings

    // ==================================
    // SSBO for Celestial Objects (binding 2)
    // ==================================
    VulkanBuffer celestialObjectsSSBO = {}; // SSBO buffer containing celestial objects array
    uint32_t celestialObjectCount = 0;      // Number of celestial objects currently in buffer

    // ==================================
    // Skybox Cubemap Texture (binding 3)
    // ==================================
    // Stored as vertical strip: 6 faces (faceSize x faceSize each)
    // Used for seamless skybox rendering when ray misses all objects
    VkImage skyboxImage = VK_NULL_HANDLE;
    VkDeviceMemory skyboxImageMemory = VK_NULL_HANDLE;
    VkImageView skyboxImageView = VK_NULL_HANDLE;
    VkSampler skyboxSampler = VK_NULL_HANDLE;
    bool skyboxTextureReady = false;

    // ==================================
    // Earth Material Textures (bindings 4-7)
    // ==================================
    // Used when ray hits Earth (NAIF ID 399) in single-pass-screen.frag
    // Binding 4: Earth color texture (monthly Blue Marble)
    VkImage earthColorImage = VK_NULL_HANDLE;
    VkDeviceMemory earthColorImageMemory = VK_NULL_HANDLE;
    VkImageView earthColorImageView = VK_NULL_HANDLE;
    VkSampler earthColorSampler = VK_NULL_HANDLE;
    // Binding 5: Earth normal map
    VkImage earthNormalImage = VK_NULL_HANDLE;
    VkDeviceMemory earthNormalImageMemory = VK_NULL_HANDLE;
    VkImageView earthNormalImageView = VK_NULL_HANDLE;
    VkSampler earthNormalSampler = VK_NULL_HANDLE;
    // Binding 6: Earth nightlights texture
    VkImage earthNightlightsImage = VK_NULL_HANDLE;
    VkDeviceMemory earthNightlightsImageMemory = VK_NULL_HANDLE;
    VkImageView earthNightlightsImageView = VK_NULL_HANDLE;
    VkSampler earthNightlightsSampler = VK_NULL_HANDLE;
    // Binding 7: Earth specular/roughness texture
    VkImage earthSpecularImage = VK_NULL_HANDLE;
    VkDeviceMemory earthSpecularImageMemory = VK_NULL_HANDLE;
    VkImageView earthSpecularImageView = VK_NULL_HANDLE;
    VkSampler earthSpecularSampler = VK_NULL_HANDLE;
    // Binding 8: Earth heightmap texture (for parallax/displacement)
    VkImage earthHeightmapImage = VK_NULL_HANDLE;
    VkDeviceMemory earthHeightmapImageMemory = VK_NULL_HANDLE;
    VkImageView earthHeightmapImageView = VK_NULL_HANDLE;
    VkSampler earthHeightmapSampler = VK_NULL_HANDLE;
    bool earthTexturesReady = false;
};

// Global Vulkan context pointer (set during initialization)
extern VulkanContext *g_vulkanContext;

// Create Vulkan instance (must be called before creating surface)
// requiredExtensions: Platform-specific instance extensions (e.g., from GLFW)
bool createInstance(VulkanContext &context, const std::vector<const char *> &requiredExtensions);

// Initialize Vulkan context (after instance and surface are created)
// surface: Pre-created VkSurfaceKHR (created by platform-specific code)
// width, height: Window dimensions for swapchain creation
bool initVulkan(VulkanContext &context, VkSurfaceKHR surface, uint32_t width, uint32_t height);

// Cleanup Vulkan context
void cleanupVulkan(VulkanContext &context);

// Create shader module from GLSL source code
VulkanShader createShaderModule(VulkanContext &context, const std::string &glslSource, VkShaderStageFlagBits stage);

// Destroy shader module
void destroyShaderModule(VulkanContext &context, VulkanShader &shader);

// Create buffer
VulkanBuffer createBuffer(VulkanContext &context,
                          VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          const void *data = nullptr);

// Destroy buffer
void destroyBuffer(VulkanContext &context, VulkanBuffer &buffer);

// Begin frame - acquire swapchain image and begin command buffer
VkCommandBuffer beginFrame(VulkanContext &context);

// End frame - end render pass and submit frame
void endFrame(VulkanContext &context);

// Cleanup swapchain and framebuffers (for resize)
void cleanupSwapchain(VulkanContext &context);

// Recreate swapchain and framebuffers (for resize)
bool recreateSwapchain(VulkanContext &context, uint32_t width, uint32_t height);

// Create UI pipeline (for subpass 1 - 2D UI overlay)
bool createUIPipeline(VulkanContext &context);

// UI vertex structure (for building UI geometry)
struct UIVertex
{
    float x, y;       // Position in NDC space (-1 to 1)
    float r, g, b, a; // Color (RGBA)
};

// UI vertex buffer builder (global, used by UI rendering functions)
// Call BeginUIVertexBuffer before UI rendering, AddUIVertex during rendering, EndUIVertexBuffer after
extern std::vector<UIVertex> g_uiVertexBuilder;
extern bool g_buildingUIVertices;

// Begin building UI vertices (clears the builder and stores screen dimensions)
void BeginUIVertexBuffer(int screenWidth, int screenHeight);

// Add a vertex to the UI vertex buffer builder (uses stored screen dimensions)
// x, y: Position in screen space (pixels)
// r, g, b, a: Color (RGBA, 0-1)
void AddUIVertex(float x, float y, float r, float g, float b, float a);

// End building UI vertices and create/update the vertex buffer
// Returns the number of vertices added
uint32_t EndUIVertexBuffer(VulkanContext &context);

// Build UI vertex buffer from UI rendering calls
// This function should be called each frame before rendering to build the UI geometry
// Returns the number of vertices added
uint32_t buildUIVertexBuffer(VulkanContext &context, int screenWidth, int screenHeight);

// Compile GLSL shader source to SPIR-V using glslangValidator
// context: Vulkan context to track temp files for cleanup
std::vector<uint32_t> compileGLSLToSPIRV(VulkanContext &context,
                                         const std::string &glslSource,
                                         const std::string &shaderStage,
                                         const std::string &entryPoint = "main");

// ==================================
// SSBO and Push Constants
// ==================================

// Create SSBO descriptor set layout for UIState
bool createSSBODescriptorSetLayout(VulkanContext &context);

// Create SSBO buffer and descriptor set
bool createSSBOResources(VulkanContext &context);

// Update SSBO buffer with current UIState
void updateSSBOBuffer(VulkanContext &context, const UIState &state);

// Read hover output from SSBO (returns material ID at mouse position, 0 = no hit)
uint32_t readHoverOutput(VulkanContext &context);

// Read minimum surface distance from SSBO (for camera step limiting)
float readMinSurfaceDistance(VulkanContext &context);

// Wait for the current frame's fence (ensures previous frame's GPU work is complete)
// Call this before accessing buffers written by the GPU
void waitForCurrentFrameFence(VulkanContext &context);

// Reset hover output SSBO to 0 (call before rendering)
void resetHoverOutput(VulkanContext &context);

// Reset min distance SSBO to large value (call before rendering)
void resetMinDistanceOutput(VulkanContext &context);

// Push world state constants to command buffer
// Takes WorldPushConstants directly (use WorldState::toPushConstants() to convert)
void pushWorldConstants(VkCommandBuffer cmd, VkPipelineLayout layout, const WorldPushConstants &constants);

// Push input state constants to command buffer
// Takes InputPushConstants directly (use InputState::toPushConstants() to convert)
// Offset is sizeof(WorldPushConstants) = 16 bytes
void pushInputConstants(VkCommandBuffer cmd, VkPipelineLayout layout, const InputPushConstants &constants);

// Push camera state constants to command buffer
// Takes CameraPushConstants directly (use WorldState::toCameraPushConstants() to convert)
// Offset is sizeof(WorldPushConstants) + sizeof(InputPushConstants) = 32 bytes
void pushCameraConstants(VkCommandBuffer cmd, VkPipelineLayout layout, const CameraPushConstants &constants);

// Update celestial objects SSBO with frustum-culled objects
// objects: vector of CelestialObject structs
// viewMatrix, projMatrix: camera matrices for frustum culling
// selectedNaifId: NAIF ID of selected body (always included, never culled)
// Only objects visible in the frustum (plus selected) are sent to the GPU
void updateCelestialObjectsSSBO(VulkanContext &context,
                                const std::vector<CelestialObject> &objects,
                                const glm::mat4 &viewMatrix,
                                const glm::mat4 &projMatrix,
                                int32_t selectedNaifId = 0);

// ==================================
// Skybox Texture Functions
// ==================================

// Load skybox cubemap texture from file (vertical strip format: 6 faces stacked)
// filepath: path to HDR or PNG file with cubemap faces in vertical strip format
// Returns true on success
bool loadSkyboxTexture(VulkanContext &context, const std::string &filepath);

// Update skybox descriptor set binding (call after loading texture)
void updateSkyboxDescriptorSet(VulkanContext &context);

// Cleanup skybox texture resources
void cleanupSkyboxTexture(VulkanContext &context);

// ==================================
// Earth Material Texture Functions
// ==================================

// Load Earth material textures for NAIF ID 399
// basePath: path to earth-textures folder (e.g., "earth-textures")
// resolutionFolder: resolution subfolder (e.g., "medium", "high")
// currentMonth: 1-12 for monthly color texture selection
// Returns true on success
bool loadEarthTextures(VulkanContext &context,
                       const std::string &basePath,
                       const std::string &resolutionFolder,
                       int currentMonth);

// Update Earth texture descriptor set bindings (call after loading textures)
void updateEarthDescriptorSet(VulkanContext &context);

// Cleanup Earth texture resources
void cleanupEarthTextures(VulkanContext &context);
