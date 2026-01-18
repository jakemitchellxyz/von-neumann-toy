#include "vulkan.h"
#include "../app-state.h"
#include "../input-controller.h"
#include "../ui-overlay.h"
#include "../ui-primitives.h"
#include "shader-loader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

// stb_image for loading skybox textures (implementation defined elsewhere)
#include <stb_image.h>


#ifdef _WIN32
#include <process.h>
#include <windows.h>

#else
#include <sys/wait.h>
#include <unistd.h>

#endif

// Global Vulkan context pointer
VulkanContext *g_vulkanContext = nullptr;

// UI vertex buffer builder (global, used by UI rendering functions)
std::vector<UIVertex> g_uiVertexBuilder;
bool g_buildingUIVertices = false;
static int g_uiScreenWidth = 0;
static int g_uiScreenHeight = 0;

// Validation layer names
const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};

// Required device extensions
const std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

// Debug callback for validation layers
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                    VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                    void *pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::cerr << "Vulkan validation: " << pCallbackData->pMessage << "\n";
    }
    return VK_FALSE;
}

// Check if validation layers are available
bool checkValidationLayerSupport()
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char *layerName : validationLayers)
    {
        bool layerFound = false;
        for (const auto &layerProperties : availableLayers)
        {
            if (strcmp(layerName, layerProperties.layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }
        if (!layerFound)
        {
            return false;
        }
    }
    return true;
}

// Get required instance extensions (platform-specific extensions must be provided)
std::vector<const char *> getRequiredExtensions(const std::vector<const char *> &platformExtensions)
{
    std::vector<const char *> extensions = platformExtensions;

    if (enableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

// Create Vulkan instance (exposed for surface creation)
bool createInstance(VulkanContext &context, const std::vector<const char *> &requiredExtensions)
{
    if (enableValidationLayers && !checkValidationLayerSupport())
    {
        std::cerr << "Validation layers requested, but not available!" << "\n";
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Von Neumann Toy";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions(requiredExtensions);
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Initialize defaults
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = reinterpret_cast<VkDebugUtilsMessengerCreateInfoEXT *>(&debugCreateInfo);
    }

    if (vkCreateInstance(&createInfo, nullptr, &context.instance) != VK_SUCCESS)
    {
        std::cerr << "Failed to create Vulkan instance!" << "\n";
        return false;
    }

    // Create debug messenger
    if (enableValidationLayers)
    {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context.instance,
                                                                              "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            func(context.instance, &debugCreateInfo, nullptr, &context.debugMessenger);
        }
    }

    return true;
}

// Find queue families
void findQueueFamilies(VulkanContext &context, VkPhysicalDevice device)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            context.graphicsQueueFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, context.surface, &presentSupport);
        if (presentSupport != VK_FALSE)
        {
            context.presentQueueFamily = i;
        }

        if (context.graphicsQueueFamily != UINT32_MAX && context.presentQueueFamily != UINT32_MAX)
        {
            break;
        }
    }
}

// Check if device supports required extensions
bool checkDeviceExtensionSupport(VulkanContext &context, VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto &extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

// Check if swapchain is adequate
bool isSwapchainAdequate(VulkanContext &context, VkPhysicalDevice device)
{
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, context.surface, &formatCount, nullptr);

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, context.surface, &presentModeCount, nullptr);

    return formatCount > 0 && presentModeCount > 0;
}

// Check if device is suitable
bool isDeviceSuitable(VulkanContext &context, VkPhysicalDevice device)
{
    findQueueFamilies(context, device);

    bool extensionsSupported = checkDeviceExtensionSupport(context, device);
    bool swapchainAdequate = isSwapchainAdequate(context, device);

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    return context.graphicsQueueFamily != UINT32_MAX && context.presentQueueFamily != UINT32_MAX &&
           extensionsSupported && swapchainAdequate && (deviceFeatures.samplerAnisotropy != VK_FALSE);
}

// Select physical device
bool selectPhysicalDevice(VulkanContext &context)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(context.instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        std::cerr << "Failed to find GPUs with Vulkan support!" << "\n";
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(context.instance, &deviceCount, devices.data());

    for (const auto &device : devices)
    {
        if (isDeviceSuitable(context, device))
        {
            context.physicalDevice = device;
            break;
        }
    }

    if (context.physicalDevice == VK_NULL_HANDLE)
    {
        std::cerr << "Failed to find a suitable GPU!" << "\n";
        return false;
    }

    return true;
}

// Create logical device
bool createLogicalDevice(VulkanContext &context)
{
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {context.graphicsQueueFamily, context.presentQueueFamily};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (enableValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(context.physicalDevice, &createInfo, nullptr, &context.device) != VK_SUCCESS)
    {
        std::cerr << "Failed to create logical device!" << "\n";
        return false;
    }

    vkGetDeviceQueue(context.device, context.graphicsQueueFamily, 0, &context.graphicsQueue);
    vkGetDeviceQueue(context.device, context.presentQueueFamily, 0, &context.presentQueue);

    return true;
}

// Set surface (surface is created by platform-specific code)
bool setSurface(VulkanContext &context, VkSurfaceKHR surface)
{
    if (surface == VK_NULL_HANDLE)
    {
        std::cerr << "Invalid surface provided!" << "\n";
        return false;
    }
    context.surface = surface;
    return true;
}

// Choose swapchain surface format
VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats)
{
    for (const auto &availableFormat : availableFormats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

// Choose swapchain present mode
VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes)
{
    for (const auto &availablePresentMode : availablePresentModes)
    {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// Choose swapchain extent
VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities, uint32_t width, uint32_t height)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = {width, height};

    actualExtent.width =
        std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height =
        std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

// Create swapchain
bool createSwapchain(VulkanContext &context, uint32_t width, uint32_t height)
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physicalDevice, context.surface, &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, nullptr);

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, formats.data());

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physicalDevice, context.surface, &presentModeCount, nullptr);

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physicalDevice,
                                              context.surface,
                                              &presentModeCount,
                                              presentModes.data());

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(presentModes);
    VkExtent2D extent = chooseSwapExtent(capabilities, width, height);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = context.surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = {context.graphicsQueueFamily, context.presentQueueFamily};

    if (context.graphicsQueueFamily != context.presentQueueFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(context.device, &createInfo, nullptr, &context.swapchain) != VK_SUCCESS)
    {
        std::cerr << "Failed to create swap chain!" << "\n";
        return false;
    }

    vkGetSwapchainImagesKHR(context.device, context.swapchain, &imageCount, nullptr);
    context.swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(context.device, context.swapchain, &imageCount, context.swapchainImages.data());

    context.swapchainImageFormat = surfaceFormat.format;
    context.swapchainExtent = extent;

    // Create image views
    context.swapchainImageViews.resize(context.swapchainImages.size());
    for (size_t i = 0; i < context.swapchainImages.size(); i++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = context.swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = context.swapchainImageFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(context.device, &viewInfo, nullptr, &context.swapchainImageViews[i]) != VK_SUCCESS)
        {
            std::cerr << "Failed to create swapchain image view " << i << "!" << "\n";
            return false;
        }
    }

    return true;
}

// Create render pass with 2 subpasses: 3D scene (subpass 0) and UI overlay (subpass 1)
bool createRenderPass(VulkanContext &context)
{
    // Color attachment - shared between both subpasses
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = context.swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear at start of render pass
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Subpass 0: 3D scene (depth test on, depth write on, blending off)
    VkAttachmentReference colorAttachmentRef0{};
    colorAttachmentRef0.attachment = 0;
    colorAttachmentRef0.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass0{}; // 3D scene
    subpass0.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass0.colorAttachmentCount = 1;
    subpass0.pColorAttachments = &colorAttachmentRef0;

    // Subpass 1: 2D UI overlay (depth test off, blending on)
    // Uses the same color attachment, but with LOAD_OP_LOAD (preserves subpass 0 output)
    VkAttachmentReference colorAttachmentRef1{};
    colorAttachmentRef1.attachment = 0;
    colorAttachmentRef1.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass1{}; // UI overlay
    subpass1.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass1.colorAttachmentCount = 1;
    subpass1.pColorAttachments = &colorAttachmentRef1;

    // Subpass dependencies
    // Dependency 0: External -> Subpass 0 (beginning of render pass)
    VkSubpassDependency dependency0{};
    dependency0.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency0.dstSubpass = 0;
    dependency0.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency0.srcAccessMask = 0;
    dependency0.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency0.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Dependency 1: Subpass 0 -> Subpass 1 (UI overlay reads from 3D scene output)
    VkSubpassDependency dependency1{};
    dependency1.srcSubpass = 0;
    dependency1.dstSubpass = 1;
    dependency1.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency1.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency1.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency1.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Dependency 2: Subpass 1 -> External (end of render pass)
    VkSubpassDependency dependency2{};
    dependency2.srcSubpass = 1;
    dependency2.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency2.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency2.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency2.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency2.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    VkSubpassDescription subpasses[] = {subpass0, subpass1};
    VkSubpassDependency dependencies[] = {dependency0, dependency1, dependency2};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 2;
    renderPassInfo.pSubpasses = subpasses;
    renderPassInfo.dependencyCount = 3;
    renderPassInfo.pDependencies = dependencies;

    if (vkCreateRenderPass(context.device, &renderPassInfo, nullptr, &context.renderPass) != VK_SUCCESS)
    {
        std::cerr << "Failed to create render pass!" << "\n";
        return false;
    }

    return true;
}

// Get temporary directory path (with trailing separator)
static std::string getTempDirectory()
{
#ifdef _WIN32
    char tempPath[MAX_PATH];
    DWORD result = GetTempPathA(MAX_PATH, tempPath);
    if (result > 0 && result < MAX_PATH)
    {
        std::string path(tempPath);
        // Ensure trailing backslash
        if (!path.empty() && path.back() != '\\' && path.back() != '/')
        {
            path += '\\';
        }
        return path;
    }
    return std::string(".\\");
#else
    const char *tmpDir = std::getenv("TMPDIR");
    if (tmpDir != nullptr)
    {
        std::string path(tmpDir);
        if (!path.empty() && path.back() != '/')
            path += '/';
        return path;
    }
    tmpDir = std::getenv("TMP");
    if (tmpDir != nullptr)
    {
        std::string path(tmpDir);
        if (!path.empty() && path.back() != '/')
            path += '/';
        return path;
    }
    return std::string("/tmp/");
#endif
}

// Generate deterministic filename based on shader source hash
static std::string generateShaderFilename(const std::string &glslSource, const std::string &shaderStage)
{
    // Create hash of shader source + stage to ensure deterministic filename
    std::hash<std::string> hasher;
    size_t hash = hasher(glslSource + shaderStage);

    // Convert hash to hex string for filename
    std::stringstream ss;
    ss << std::hex << hash;
    std::string hashStr = ss.str();

    return "vnt_shader_" + hashStr + "_" + shaderStage;
}

// Compile GLSL to SPIR-V using glslangValidator (writes to temp directory, always overwrites same file)
// context: Vulkan context to track temp files for cleanup
std::vector<uint32_t> compileGLSLToSPIRV(VulkanContext &context,
                                         const std::string &glslSource,
                                         const std::string &shaderStage,
                                         const std::string &entryPoint)
{
    // Map shader stage to glslangValidator stage name
    std::string stageArg;
    if (shaderStage == "vertex")
        stageArg = "vert";
    else if (shaderStage == "fragment")
        stageArg = "frag";
    else if (shaderStage == "compute")
        stageArg = "comp";
    else
    {
        std::cerr << "Unknown shader stage: " << shaderStage << "\n";
        return {};
    }

    // Get temp directory
    std::string tempDir = getTempDirectory();

    // Generate deterministic filename based on shader content hash
    // This ensures the same shader always uses the same temp file (overwrites previous)
    std::string baseName = generateShaderFilename(glslSource, shaderStage);
    std::string tempInputFile = tempDir + baseName + ".glsl";
    std::string tempOutputFile = tempDir + baseName + ".spv";

    // Write GLSL source to temp file
    std::ofstream inputFile(tempInputFile);
    if (!inputFile.is_open())
    {
        std::cerr << "Failed to create temporary shader file in " << tempDir << "\n";
        return {};
    }
    inputFile << glslSource;
    inputFile.close();

    // Compile using glslangValidator
    std::string command = "glslangValidator -V -S " + stageArg + " -e " + entryPoint + " -o \"" + tempOutputFile +
                          "\" \"" + tempInputFile + "\"";

    int result = system(command.c_str());

    // Read SPIR-V binary
    std::vector<uint32_t> spirv;
    if (result == 0)
    {
        std::ifstream outputFile(tempOutputFile, std::ios::binary | std::ios::ate);
        if (outputFile.is_open())
        {
            size_t fileSize = outputFile.tellg();
            outputFile.seekg(0, std::ios::beg);
            spirv.resize(fileSize / sizeof(uint32_t));
            outputFile.read(reinterpret_cast<char *>(spirv.data()), static_cast<std::streamsize>(fileSize));
            outputFile.close();
        }
    }
    else
    {
        std::cerr << "Failed to compile shader using glslangValidator. Make sure glslangValidator is in your PATH."
                  << "\n";
        std::cerr << "Command: " << command << "\n";
    }

    // Track temp files for cleanup (only if compilation succeeded)
    if (result == 0 && !spirv.empty())
    {
        // Add to tracking list if not already present (same shader might be compiled multiple times)
        bool inputFound = false;
        bool outputFound = false;
        for (const auto &trackedFile : context.tempShaderFiles)
        {
            if (trackedFile == tempInputFile)
                inputFound = true;
            if (trackedFile == tempOutputFile)
                outputFound = true;
        }
        if (!inputFound)
            context.tempShaderFiles.push_back(tempInputFile);
        if (!outputFound)
            context.tempShaderFiles.push_back(tempOutputFile);
    }
    else
    {
        // Clean up temp files immediately if compilation failed
        std::remove(tempInputFile.c_str());
        std::remove(tempOutputFile.c_str());
    }

    return spirv;
}

// Create shader module
VulkanShader createShaderModule(VulkanContext &context, const std::string &glslSource, VkShaderStageFlagBits stage)
{
    VulkanShader shader;
    shader.stage = stage;

    // Determine shader stage name for compilation
    std::string stageName;
    if (stage == VK_SHADER_STAGE_VERTEX_BIT)
        stageName = "vertex";
    else if (stage == VK_SHADER_STAGE_FRAGMENT_BIT)
        stageName = "fragment";
    else if (stage == VK_SHADER_STAGE_COMPUTE_BIT)
        stageName = "compute";
    else
    {
        std::cerr << "Unsupported shader stage!" << "\n";
        return shader;
    }

    // Compile GLSL to SPIR-V
    std::vector<uint32_t> spirv = compileGLSLToSPIRV(context, glslSource, stageName, "main");
    if (spirv.empty())
    {
        std::cerr << "Failed to compile shader to SPIR-V!" << "\n";
        return shader;
    }

    // Create shader module
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();

    if (vkCreateShaderModule(context.device, &createInfo, nullptr, &shader.module) != VK_SUCCESS)
    {
        std::cerr << "Failed to create shader module!" << "\n";
        return shader;
    }

    return shader;
}

// Destroy shader module
void destroyShaderModule(VulkanContext &context, VulkanShader &shader)
{
    if (shader.module != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(context.device, shader.module, nullptr);
        shader.module = VK_NULL_HANDLE;
    }
}

// Create graphics pipeline
bool createGraphicsPipeline(VulkanContext &context)
{
    // Load shader sources
    std::vector<std::string> vertexPaths = {"src/materials/screen/single-pass-screen.vert",
                                            "../src/materials/screen/single-pass-screen.vert",
                                            "../../src/materials/screen/single-pass-screen.vert"};
    std::string vertexSource;
    for (const auto &path : vertexPaths)
    {
        if (std::ifstream(path).good())
        {
            vertexSource = loadShaderFile(path);
            if (!vertexSource.empty())
                break;
        }
    }

    std::vector<std::string> fragmentPaths = {"src/materials/screen/single-pass-screen.frag",
                                              "../src/materials/screen/single-pass-screen.frag",
                                              "../../src/materials/screen/single-pass-screen.frag"};
    std::string fragmentSource;
    for (const auto &path : fragmentPaths)
    {
        if (std::ifstream(path).good())
        {
            fragmentSource = loadShaderFile(path);
            if (!fragmentSource.empty())
                break;
        }
    }

    if (vertexSource.empty() || fragmentSource.empty())
    {
        std::cerr << "Failed to load shader files!" << "\n";
        return false;
    }

    // Create shader modules
    VulkanShader vertexShader = createShaderModule(context, vertexSource, VK_SHADER_STAGE_VERTEX_BIT);
    VulkanShader fragmentShader = createShaderModule(context, fragmentSource, VK_SHADER_STAGE_FRAGMENT_BIT);

    if (vertexShader.module == VK_NULL_HANDLE || fragmentShader.module == VK_NULL_HANDLE)
    {
        std::cerr << "Failed to create shader modules!" << "\n";
        destroyShaderModule(context, vertexShader);
        destroyShaderModule(context, fragmentShader);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertexShader.module;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragmentShader.module;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input (vec2 position in NDC space)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 2; // 2 floats (position)
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescription.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // Changed to match shared buffer
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    // Use Vulkan's natural coordinate system (Y=0 at top) for 3D rendering
    // Set as dynamic state so it updates on window resize
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = nullptr; // Dynamic state - set in command buffer
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr; // Dynamic state - set in command buffer

    // Dynamic state (viewport and scissor set dynamically each frame)
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Push constant ranges for WorldPushConstants, InputPushConstants, and CameraPushConstants
    // Range 0: WorldPushConstants (offset 0, 16 bytes)
    // Range 1: InputPushConstants (offset 16, 16 bytes)
    // Range 2: CameraPushConstants (offset 32, 144 bytes)
    std::array<VkPushConstantRange, 3> pushConstantRanges{};

    // World state (julian date, time dilation)
    pushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRanges[0].offset = 0;
    pushConstantRanges[0].size = sizeof(WorldPushConstants);

    // Input state (mouse position, button state)
    pushConstantRanges[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRanges[1].offset = sizeof(WorldPushConstants);
    pushConstantRanges[1].size = sizeof(InputPushConstants);

    // Camera state (view/projection matrices, position, FOV)
    pushConstantRanges[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRanges[2].offset = sizeof(WorldPushConstants) + sizeof(InputPushConstants);
    pushConstantRanges[2].size = sizeof(CameraPushConstants);

    // Pipeline layout with push constants and SSBO descriptor set
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    // Use SSBO descriptor set layout if available
    if (context.ssboDescriptorSetLayout != VK_NULL_HANDLE)
    {
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &context.ssboDescriptorSetLayout;
    }
    else
    {
        pipelineLayoutInfo.setLayoutCount = 0;
        pipelineLayoutInfo.pSetLayouts = nullptr;
    }

    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
    pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges.data();

    if (vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr, &context.pipelineLayout) != VK_SUCCESS)
    {
        std::cerr << "Failed to create pipeline layout!" << "\n";
        destroyShaderModule(context, vertexShader);
        destroyShaderModule(context, fragmentShader);
        return false;
    }

    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState; // Dynamic viewport/scissor
    pipelineInfo.layout = context.pipelineLayout;
    pipelineInfo.renderPass = context.renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &context.screenPipeline) !=
        VK_SUCCESS)
    {
        std::cerr << "Failed to create graphics pipeline!" << "\n";
        vkDestroyPipelineLayout(context.device, context.pipelineLayout, nullptr);
        destroyShaderModule(context, vertexShader);
        destroyShaderModule(context, fragmentShader);
        return false;
    }

    // Clean up shader modules (no longer needed after pipeline creation)
    destroyShaderModule(context, vertexShader);
    destroyShaderModule(context, fragmentShader);

    return true;
}

// Create UI pipeline (for subpass 1 - 2D UI overlay with blending)
bool createUIPipeline(VulkanContext &context)
{
    // Load shader sources
    std::vector<std::string> vertexPaths = {"src/materials/screen/ui-overlay.vert",
                                            "../src/materials/screen/ui-overlay.vert",
                                            "../../src/materials/screen/ui-overlay.vert"};
    std::string vertexSource;
    for (const auto &path : vertexPaths)
    {
        if (std::ifstream(path).good())
        {
            vertexSource = loadShaderFile(path);
            if (!vertexSource.empty())
                break;
        }
    }

    std::vector<std::string> fragmentPaths = {"src/materials/screen/ui-overlay.frag",
                                              "../src/materials/screen/ui-overlay.frag",
                                              "../../src/materials/screen/ui-overlay.frag"};
    std::string fragmentSource;
    for (const auto &path : fragmentPaths)
    {
        if (std::ifstream(path).good())
        {
            fragmentSource = loadShaderFile(path);
            if (!fragmentSource.empty())
                break;
        }
    }

    if (vertexSource.empty() || fragmentSource.empty())
    {
        std::cerr << "Failed to load UI shader files!" << "\n";
        return false;
    }

    // Create shader modules
    VulkanShader vertexShader = createShaderModule(context, vertexSource, VK_SHADER_STAGE_VERTEX_BIT);
    VulkanShader fragmentShader = createShaderModule(context, fragmentSource, VK_SHADER_STAGE_FRAGMENT_BIT);

    if (vertexShader.module == VK_NULL_HANDLE || fragmentShader.module == VK_NULL_HANDLE)
    {
        std::cerr << "Failed to create UI shader modules!" << "\n";
        destroyShaderModule(context, vertexShader);
        destroyShaderModule(context, fragmentShader);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertexShader.module;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragmentShader.module;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input (2D position + color)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 6; // 2 floats (position) + 4 floats (color)
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2]{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = 0; // Position at offset 0

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 2; // Color at offset 8

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    // Flip viewport Y for UI to match OpenGL-style coordinates (Y=0 at bottom)
    // This is a Vulkan-OpenGL compatibility issue - UI math expects Y=0 at bottom
    // Negative height flips the Y axis so NDC Y=1 maps to screen Y=0 (top)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = (float)context.swapchainExtent.height; // Start at bottom
    viewport.width = (float)context.swapchainExtent.width;
    viewport.height = -(float)context.swapchainExtent.height; // Negative height flips Y
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = context.swapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = nullptr; // Dynamic state - set in command buffer
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr; // Dynamic state - set in command buffer

    // Dynamic state (viewport and scissor set dynamically each frame)
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // No culling for 2D UI
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil (disabled for UI)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Color blending (enabled for alpha blending)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Push constant ranges (same as screen pipeline)
    // Range 0: WorldPushConstants (offset 0, 16 bytes)
    // Range 1: InputPushConstants (offset 16, 16 bytes)
    // Range 2: CameraPushConstants (offset 32, 144 bytes)
    std::array<VkPushConstantRange, 3> uiPushConstantRanges{};

    // World state (julian date, time dilation)
    uiPushConstantRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uiPushConstantRanges[0].offset = 0;
    uiPushConstantRanges[0].size = sizeof(WorldPushConstants);

    // Input state (mouse position, button state)
    uiPushConstantRanges[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uiPushConstantRanges[1].offset = sizeof(WorldPushConstants);
    uiPushConstantRanges[1].size = sizeof(InputPushConstants);

    // Camera state (view/projection matrices, position, FOV)
    uiPushConstantRanges[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uiPushConstantRanges[2].offset = sizeof(WorldPushConstants) + sizeof(InputPushConstants);
    uiPushConstantRanges[2].size = sizeof(CameraPushConstants);

    // Pipeline layout with push constants and SSBO descriptor set
    VkPipelineLayoutCreateInfo uiPipelineLayoutInfo{};
    uiPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    // Use SSBO descriptor set layout if available
    if (context.ssboDescriptorSetLayout != VK_NULL_HANDLE)
    {
        uiPipelineLayoutInfo.setLayoutCount = 1;
        uiPipelineLayoutInfo.pSetLayouts = &context.ssboDescriptorSetLayout;
    }
    else
    {
        uiPipelineLayoutInfo.setLayoutCount = 0;
        uiPipelineLayoutInfo.pSetLayouts = nullptr;
    }

    uiPipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(uiPushConstantRanges.size());
    uiPipelineLayoutInfo.pPushConstantRanges = uiPushConstantRanges.data();

    if (vkCreatePipelineLayout(context.device, &uiPipelineLayoutInfo, nullptr, &context.uiPipelineLayout) != VK_SUCCESS)
    {
        std::cerr << "Failed to create UI pipeline layout!" << "\n";
        destroyShaderModule(context, vertexShader);
        destroyShaderModule(context, fragmentShader);
        return false;
    }

    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState; // Dynamic viewport/scissor
    pipelineInfo.layout = context.uiPipelineLayout;
    pipelineInfo.renderPass = context.renderPass;
    pipelineInfo.subpass = 1; // UI is in subpass 1
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &context.uiPipeline) !=
        VK_SUCCESS)
    {
        std::cerr << "Failed to create UI graphics pipeline!" << "\n";
        vkDestroyPipelineLayout(context.device, context.uiPipelineLayout, nullptr);
        destroyShaderModule(context, vertexShader);
        destroyShaderModule(context, fragmentShader);
        return false;
    }

    // Clean up shader modules (no longer needed after pipeline creation)
    destroyShaderModule(context, vertexShader);
    destroyShaderModule(context, fragmentShader);

    return true;
}

// Create command pool
bool createCommandPool(VulkanContext &context)
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context.graphicsQueueFamily;

    if (vkCreateCommandPool(context.device, &poolInfo, nullptr, &context.commandPool) != VK_SUCCESS)
    {
        std::cerr << "Failed to create command pool!" << "\n";
        return false;
    }

    return true;
}

// Create framebuffers
bool createFramebuffers(VulkanContext &context)
{
    context.swapchainFramebuffers.resize(context.swapchainImageViews.size());

    for (size_t i = 0; i < context.swapchainImageViews.size(); i++)
    {
        VkImageView attachments[] = {context.swapchainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = context.renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = context.swapchainExtent.width;
        framebufferInfo.height = context.swapchainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(context.device, &framebufferInfo, nullptr, &context.swapchainFramebuffers[i]) !=
            VK_SUCCESS)
        {
            std::cerr << "Failed to create framebuffer " << i << "!" << "\n";
            return false;
        }
    }

    return true;
}

// Create command buffers
bool createCommandBuffers(VulkanContext &context)
{
    context.commandBuffers.resize(context.swapchainImages.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = context.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)context.commandBuffers.size();

    if (vkAllocateCommandBuffers(context.device, &allocInfo, context.commandBuffers.data()) != VK_SUCCESS)
    {
        std::cerr << "Failed to allocate command buffers!" << "\n";
        return false;
    }

    return true;
}

// Create synchronization objects
bool createSyncObjects(VulkanContext &context)
{
    context.imageAvailableSemaphores.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    context.renderFinishedSemaphores.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    context.inFlightFences.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(context.device, &semaphoreInfo, nullptr, &context.imageAvailableSemaphores[i]) !=
                VK_SUCCESS ||
            vkCreateSemaphore(context.device, &semaphoreInfo, nullptr, &context.renderFinishedSemaphores[i]) !=
                VK_SUCCESS ||
            vkCreateFence(context.device, &fenceInfo, nullptr, &context.inFlightFences[i]) != VK_SUCCESS)
        {
            std::cerr << "Failed to create synchronization objects for frame " << i << "!" << "\n";
            return false;
        }
    }

    return true;
}

// Initialize Vulkan (instance must already be created)
bool initVulkan(VulkanContext &context, VkSurfaceKHR surface, uint32_t width, uint32_t height)
{
    if (context.instance == VK_NULL_HANDLE)
    {
        std::cerr << "Vulkan instance must be created before calling initVulkan!" << "\n";
        return false;
    }

    if (!setSurface(context, surface))
    {
        cleanupVulkan(context);
        return false;
    }

    if (!selectPhysicalDevice(context))
    {
        cleanupVulkan(context);
        return false;
    }

    if (!createLogicalDevice(context))
    {
        cleanupVulkan(context);
        return false;
    }

    if (!createSwapchain(context, width, height))
    {
        cleanupVulkan(context);
        return false;
    }

    if (!createRenderPass(context))
    {
        cleanupVulkan(context);
        return false;
    }

    // Create SSBO resources (must be before pipelines to have descriptor set layout)
    if (!createSSBOResources(context))
    {
        cleanupVulkan(context);
        return false;
    }

    if (!createGraphicsPipeline(context))
    {
        cleanupVulkan(context);
        return false;
    }

    if (!createUIPipeline(context))
    {
        cleanupVulkan(context);
        return false;
    }

    // Create shared fullscreen quad vertex buffer (NDC space positions)
    // Used by both screen pipeline and UI pipeline
    // Fullscreen quad in NDC space: two triangles covering [-1, 1] x [-1, 1]
    struct FullscreenQuadVertex
    {
        float x, y; // NDC position
    };

    // Two triangles covering the fullscreen:
    // Triangle 1: bottom-left, bottom-right, top-right
    // Triangle 2: bottom-left, top-right, top-left
    FullscreenQuadVertex fullscreenQuadVertices[] = {
        {-1.0f, -1.0f}, // Bottom-left
        {1.0f, -1.0f},  // Bottom-right
        {1.0f, 1.0f},   // Top-right
        {-1.0f, -1.0f}, // Bottom-left (reused)
        {1.0f, 1.0f},   // Top-right (reused)
        {-1.0f, 1.0f}   // Top-left
    };

    context.fullscreenQuadBuffer =
        createBuffer(context,
                     sizeof(fullscreenQuadVertices),
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     fullscreenQuadVertices);
    context.fullscreenQuadVertexCount = 6; // 6 vertices = 2 triangles

    // Create test UI vertex buffer (simple colored quad in top-left corner)
    // Vertex format: [x, y, r, g, b, a] per vertex
    // Positions are in NDC space (-1 to 1) to match the shared fullscreen quad format
    struct UIVertex
    {
        float x, y;       // Position in NDC space (-1 to 1)
        float r, g, b, a; // Color (RGBA)
    };

    // Create a fullscreen test quad to verify UI pipeline renders correctly
    // With flipped viewport: NDC Y=1 maps to screen top, Y=-1 maps to screen bottom
    // Fullscreen quad: from (-1, -1) to (1, 1) covering entire screen
    UIVertex testQuad[] = {
        // Bottom-left
        {-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.5f}, // Red (semi-transparent)
        // Bottom-right
        {1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.5f}, // Green (semi-transparent)
        // Top-left
        {-1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f}, // Blue (semi-transparent)
        // Top-right
        {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.5f} // Yellow (semi-transparent)
    };

    // Two triangles with correct winding order for clockwise front face
    // Fullscreen quad: Bottom-left, Bottom-right, Top-left, Top-right
    // Triangle 1: Bottom-left, Bottom-right, Top-left (clockwise)
    // Triangle 2: Bottom-right, Top-right, Top-left (clockwise)
    UIVertex testVertices[] = {
        testQuad[0], // Bottom-left (-1, -1)
        testQuad[1], // Bottom-right (1, -1)
        testQuad[2], // Top-left (-1, 1)
        testQuad[1], // Bottom-right (1, -1)
        testQuad[3], // Top-right (1, 1)
        testQuad[2]  // Top-left (-1, 1)
    };

    context.testUIVertexBuffer =
        createBuffer(context,
                     sizeof(testVertices),
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     testVertices);
    context.testUIVertexCount = 6; // 6 vertices = 2 triangles

    if (!createFramebuffers(context))
    {
        cleanupVulkan(context);
        return false;
    }

    if (!createCommandPool(context))
    {
        cleanupVulkan(context);
        return false;
    }

    if (!createCommandBuffers(context))
    {
        cleanupVulkan(context);
        return false;
    }

    if (!createSyncObjects(context))
    {
        cleanupVulkan(context);
        return false;
    }

    return true;
}

// Cleanup Vulkan
void cleanupVulkan(VulkanContext &context)
{
    if (context.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(context.device);

        // Cleanup synchronization objects
        for (size_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (context.imageAvailableSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(context.device, context.imageAvailableSemaphores[i], nullptr);
            if (context.renderFinishedSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(context.device, context.renderFinishedSemaphores[i], nullptr);
            if (context.inFlightFences[i] != VK_NULL_HANDLE)
                vkDestroyFence(context.device, context.inFlightFences[i], nullptr);
        }

        // Cleanup command pool
        if (context.commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(context.device, context.commandPool, nullptr);
        }

        // Cleanup pipeline
        if (context.uiPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(context.device, context.uiPipeline, nullptr);
        }

        if (context.uiPipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(context.device, context.uiPipelineLayout, nullptr);
        }

        // Cleanup shared fullscreen quad buffer
        if (context.fullscreenQuadBuffer.buffer != VK_NULL_HANDLE)
        {
            destroyBuffer(context, context.fullscreenQuadBuffer);
        }

        // Cleanup test UI vertex buffer
        if (context.testUIVertexBuffer.buffer != VK_NULL_HANDLE)
        {
            destroyBuffer(context, context.testUIVertexBuffer);
        }

        // Cleanup UI vertex buffer
        if (context.uiVertexBuffer.buffer != VK_NULL_HANDLE)
        {
            destroyBuffer(context, context.uiVertexBuffer);
        }

        // Cleanup SSBO resources
        if (context.uiStateSSBO.buffer != VK_NULL_HANDLE)
        {
            destroyBuffer(context, context.uiStateSSBO);
        }

        if (context.hoverOutputSSBO.buffer != VK_NULL_HANDLE)
        {
            destroyBuffer(context, context.hoverOutputSSBO);
        }

        if (context.celestialObjectsSSBO.buffer != VK_NULL_HANDLE)
        {
            destroyBuffer(context, context.celestialObjectsSSBO);
        }

        // Cleanup skybox texture resources
        cleanupSkyboxTexture(context);

        // Cleanup Earth texture resources
        cleanupEarthTextures(context);

        if (context.ssboDescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(context.device, context.ssboDescriptorPool, nullptr);
            context.ssboDescriptorPool = VK_NULL_HANDLE;
        }

        if (context.ssboDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(context.device, context.ssboDescriptorSetLayout, nullptr);
            context.ssboDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if (context.screenPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(context.device, context.screenPipeline, nullptr);
        }

        // Cleanup pipeline layout
        if (context.pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(context.device, context.pipelineLayout, nullptr);
        }

        // Cleanup framebuffers
        for (auto framebuffer : context.swapchainFramebuffers)
        {
            if (framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(context.device, framebuffer, nullptr);
        }

        // Cleanup render pass
        if (context.renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(context.device, context.renderPass, nullptr);
        }

        // Cleanup swapchain image views
        for (auto imageView : context.swapchainImageViews)
        {
            if (imageView != VK_NULL_HANDLE)
                vkDestroyImageView(context.device, imageView, nullptr);
        }

        // Cleanup swapchain
        if (context.swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(context.device, context.swapchain, nullptr);
        }

        // Cleanup device
        vkDestroyDevice(context.device, nullptr);
        context.device = VK_NULL_HANDLE;
    }

    // Cleanup surface
    if (context.surface != VK_NULL_HANDLE && context.instance != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(context.instance, context.surface, nullptr);
        context.surface = VK_NULL_HANDLE;
    }

    // Cleanup debug messenger
    if (context.debugMessenger != VK_NULL_HANDLE && context.instance != VK_NULL_HANDLE)
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(context.instance,
                                                                               "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr)
        {
            func(context.instance, context.debugMessenger, nullptr);
        }
        context.debugMessenger = VK_NULL_HANDLE;
    }

    // Cleanup instance
    if (context.instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(context.instance, nullptr);
        context.instance = VK_NULL_HANDLE;
    }

    // Cleanup temp shader files
    for (const auto &tempFile : context.tempShaderFiles)
    {
        std::remove(tempFile.c_str());
    }
    context.tempShaderFiles.clear();
}

// Cleanup swapchain and framebuffers (for resize)
void cleanupSwapchain(VulkanContext &context)
{
    if (context.device == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(context.device);

    // Cleanup framebuffers
    for (auto framebuffer : context.swapchainFramebuffers)
    {
        if (framebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(context.device, framebuffer, nullptr);
    }
    context.swapchainFramebuffers.clear();

    // Cleanup swapchain image views
    for (auto imageView : context.swapchainImageViews)
    {
        if (imageView != VK_NULL_HANDLE)
            vkDestroyImageView(context.device, imageView, nullptr);
    }
    context.swapchainImageViews.clear();

    // Cleanup swapchain
    if (context.swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(context.device, context.swapchain, nullptr);
        context.swapchain = VK_NULL_HANDLE;
    }
}

// Recreate swapchain and framebuffers (for resize)
bool recreateSwapchain(VulkanContext &context, uint32_t width, uint32_t height)
{
    // Wait for device to be idle
    vkDeviceWaitIdle(context.device);

    // Cleanup old swapchain
    cleanupSwapchain(context);

    // Free old command buffers (they're tied to the old swapchain)
    if (context.commandPool != VK_NULL_HANDLE && !context.commandBuffers.empty())
    {
        vkFreeCommandBuffers(context.device,
                             context.commandPool,
                             static_cast<uint32_t>(context.commandBuffers.size()),
                             context.commandBuffers.data());
        context.commandBuffers.clear();
    }

    // Recreate swapchain
    if (!createSwapchain(context, width, height))
    {
        std::cerr << "Failed to recreate swapchain!" << "\n";
        return false;
    }

    // Recreate framebuffers
    if (!createFramebuffers(context))
    {
        std::cerr << "Failed to recreate framebuffers!" << "\n";
        return false;
    }

    // Recreate command buffers (number may have changed)
    if (!createCommandBuffers(context))
    {
        std::cerr << "Failed to recreate command buffers!" << "\n";
        return false;
    }

    return true;
}

// Begin frame
VkCommandBuffer beginFrame(VulkanContext &context)
{
    // Wait for fence
    vkWaitForFences(context.device, 1, &context.inFlightFences[context.currentFrame], VK_TRUE, UINT64_MAX);

    // Acquire swapchain image
    VkResult result = vkAcquireNextImageKHR(context.device,
                                            context.swapchain,
                                            UINT64_MAX,
                                            context.imageAvailableSemaphores[context.currentFrame],
                                            VK_NULL_HANDLE,
                                            &context.currentSwapchainImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        // Swapchain is out of date, skip this frame
        return VK_NULL_HANDLE;
    }
    else if (result != VK_SUCCESS)
    {
        std::cerr << "Failed to acquire swapchain image!" << "\n";
        return VK_NULL_HANDLE;
    }

    // Reset fence
    vkResetFences(context.device, 1, &context.inFlightFences[context.currentFrame]);

    // Begin command buffer
    VkCommandBuffer cmd = context.commandBuffers[context.currentSwapchainImageIndex];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = context.renderPass;
    renderPassInfo.framebuffer = context.swapchainFramebuffers[context.currentSwapchainImageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = context.swapchainExtent;

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    return cmd;
}

// End frame
void endFrame(VulkanContext &context)
{
    // This will be called after vkCmdEndRenderPass in RenderFrame
    // Get the command buffer that was used
    VkCommandBuffer cmd = context.commandBuffers[context.currentSwapchainImageIndex];

    // End command buffer recording
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        std::cerr << "Failed to record command buffer!" << "\n";
        return;
    }

    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {context.imageAvailableSemaphores[context.currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkSemaphore signalSemaphores[] = {context.renderFinishedSemaphores[context.currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitResult =
        vkQueueSubmit(context.graphicsQueue, 1, &submitInfo, context.inFlightFences[context.currentFrame]);
    if (submitResult != VK_SUCCESS)
    {
        std::cerr << "Failed to submit draw command buffer! VkResult: " << submitResult << "\n";
        // VK_ERROR_OUT_OF_HOST_MEMORY = -1, VK_ERROR_OUT_OF_DEVICE_MEMORY = -2, VK_ERROR_DEVICE_LOST = -4
        return;
    }

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = {context.swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &context.currentSwapchainImageIndex;

    vkQueuePresentKHR(context.presentQueue, &presentInfo);

    context.currentFrame = (context.currentFrame + 1) % VulkanContext::MAX_FRAMES_IN_FLIGHT;
}

// Helper function to find memory type
static uint32_t findMemoryType(VulkanContext &context, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(context.physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    std::cerr << "Failed to find suitable memory type!" << "\n";
    return UINT32_MAX;
}

// Create buffer
VulkanBuffer createBuffer(VulkanContext &context,
                          VkDeviceSize size,
                          VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          const void *data)
{
    VulkanBuffer buffer;
    buffer.size = size;

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(context.device, &bufferInfo, nullptr, &buffer.buffer) != VK_SUCCESS)
    {
        std::cerr << "Failed to create buffer!" << "\n";
        return buffer;
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(context.device, buffer.buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(context, memRequirements.memoryTypeBits, properties);

    if (allocInfo.memoryTypeIndex == UINT32_MAX)
    {
        vkDestroyBuffer(context.device, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
        return buffer;
    }

    if (vkAllocateMemory(context.device, &allocInfo, nullptr, &buffer.allocation) != VK_SUCCESS)
    {
        std::cerr << "Failed to allocate buffer memory!" << "\n";
        vkDestroyBuffer(context.device, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
        return buffer;
    }

    // Bind memory
    vkBindBufferMemory(context.device, buffer.buffer, buffer.allocation, 0);

    // Copy data if provided
    if (data != nullptr && (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
    {
        void *mapped;
        vkMapMemory(context.device, buffer.allocation, 0, size, 0, &mapped);
        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(context.device, buffer.allocation);
    }

    return buffer;
}

// Destroy buffer
void destroyBuffer(VulkanContext &context, VulkanBuffer &buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(context.device, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
    }
    if (buffer.allocation != VK_NULL_HANDLE)
    {
        vkFreeMemory(context.device, buffer.allocation, nullptr);
        buffer.allocation = VK_NULL_HANDLE;
    }
    buffer.size = 0;
}

// ==================================
// SSBO and Push Constants Implementation
// ==================================

// Create SSBO descriptor set layout for UIState, HoverOutput, CelestialObjects, Skybox, and Earth textures
bool createSSBODescriptorSetLayout(VulkanContext &context)
{
    // Eight bindings: UIState (0), HoverOutput (1), CelestialObjects (2), SkyboxCubemap (3),
    // EarthColor (4), EarthNormal (5), EarthNightlights (6), EarthSpecular (7)
    std::array<VkDescriptorSetLayoutBinding, 8> bindings{};

    // Binding 0: UIState SSBO (read by vertex/fragment shaders)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // Binding 1: HoverOutput SSBO (written by fragment shader for hover detection)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    // Binding 2: CelestialObjects SSBO (read by fragment shader for ray marching)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    // Binding 3: SkyboxCubemap texture (read by fragment shader for skybox sampling)
    // Stored as vertical strip with 6 faces - sampled as regular 2D texture
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[3].pImmutableSamplers = nullptr;

    // Binding 4: Earth Color texture (monthly Blue Marble)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[4].pImmutableSamplers = nullptr;

    // Binding 5: Earth Normal map
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[5].pImmutableSamplers = nullptr;

    // Binding 6: Earth Nightlights texture
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[6].pImmutableSamplers = nullptr;

    // Binding 7: Earth Specular/Roughness texture
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[7].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &context.ssboDescriptorSetLayout) !=
        VK_SUCCESS)
    {
        std::cerr << "Failed to create SSBO descriptor set layout!" << "\n";
        return false;
    }

    return true;
}

// Hover output struct (matches shader layout)
struct HoverOutput
{
    uint32_t hitMaterialID; // 0 = no hit, >0 = material ID of hit object
};

// Create SSBO buffer and descriptor set
bool createSSBOResources(VulkanContext &context)
{
    // Create descriptor set layout first
    if (!createSSBODescriptorSetLayout(context))
    {
        return false;
    }

    // Create descriptor pool (need 3 SSBOs + 5 combined image samplers for skybox + earth textures)
    std::array<VkDescriptorPoolSize, 2> poolSizes{};

    // Storage buffers: UIState + HoverOutput + CelestialObjects
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 3;

    // Combined image samplers: SkyboxCubemap (1) + Earth textures (4)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 5;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &context.ssboDescriptorPool) != VK_SUCCESS)
    {
        std::cerr << "Failed to create SSBO descriptor pool!" << "\n";
        return false;
    }

    // Create SSBO buffer for UIState (binding 0)
    context.uiStateSSBO = createBuffer(context,
                                       sizeof(UIState),
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       nullptr);

    if (context.uiStateSSBO.buffer == VK_NULL_HANDLE)
    {
        std::cerr << "Failed to create UIState SSBO buffer!" << "\n";
        return false;
    }

    // Create SSBO buffer for HoverOutput (binding 1)
    context.hoverOutputSSBO = createBuffer(context,
                                           sizeof(HoverOutput),
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                           nullptr);

    if (context.hoverOutputSSBO.buffer == VK_NULL_HANDLE)
    {
        std::cerr << "Failed to create HoverOutput SSBO buffer!" << "\n";
        return false;
    }

    // Create SSBO buffer for CelestialObjects (binding 2)
    // Size: 4 bytes (count) + MAX_CELESTIAL_OBJECTS * sizeof(CelestialObject)
    // CelestialObject is 32 bytes (2 vec4s): position(12) + radius(4) + color(12) + naifId(4)
    constexpr size_t CELESTIAL_OBJECT_SIZE = 32; // 2 * sizeof(vec4)
    constexpr size_t MAX_CELESTIAL_OBJECTS = 32;
    constexpr size_t CELESTIAL_SSBO_SIZE =
        16 + MAX_CELESTIAL_OBJECTS * CELESTIAL_OBJECT_SIZE; // 16 byte header for count + padding

    context.celestialObjectsSSBO =
        createBuffer(context,
                     CELESTIAL_SSBO_SIZE,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     nullptr);

    if (context.celestialObjectsSSBO.buffer == VK_NULL_HANDLE)
    {
        std::cerr << "Failed to create CelestialObjects SSBO buffer!" << "\n";
        return false;
    }

    // Initialize celestial objects count to 0
    context.celestialObjectCount = 0;
    uint32_t zeroCount = 0;
    void *mapped;
    vkMapMemory(context.device, context.celestialObjectsSSBO.allocation, 0, sizeof(uint32_t), 0, &mapped);
    std::memcpy(mapped, &zeroCount, sizeof(uint32_t));
    vkUnmapMemory(context.device, context.celestialObjectsSSBO.allocation);

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = context.ssboDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &context.ssboDescriptorSetLayout;

    if (vkAllocateDescriptorSets(context.device, &allocInfo, &context.ssboDescriptorSet) != VK_SUCCESS)
    {
        std::cerr << "Failed to allocate SSBO descriptor set!" << "\n";
        return false;
    }

    // Update descriptor set with all three bindings
    std::array<VkDescriptorBufferInfo, 3> bufferInfos{};

    // Binding 0: UIState
    bufferInfos[0].buffer = context.uiStateSSBO.buffer;
    bufferInfos[0].offset = 0;
    bufferInfos[0].range = sizeof(UIState);

    // Binding 1: HoverOutput
    bufferInfos[1].buffer = context.hoverOutputSSBO.buffer;
    bufferInfos[1].offset = 0;
    bufferInfos[1].range = sizeof(HoverOutput);

    // Binding 2: CelestialObjects
    bufferInfos[2].buffer = context.celestialObjectsSSBO.buffer;
    bufferInfos[2].offset = 0;
    bufferInfos[2].range = CELESTIAL_SSBO_SIZE;

    std::array<VkWriteDescriptorSet, 3> descriptorWrites{};

    // Write for binding 0 (UIState)
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = context.ssboDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfos[0];

    // Write for binding 1 (HoverOutput)
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = context.ssboDescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &bufferInfos[1];

    // Write for binding 2 (CelestialObjects)
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = context.ssboDescriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pBufferInfo = &bufferInfos[2];

    vkUpdateDescriptorSets(context.device,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(),
                           0,
                           nullptr);

    // Initialize UIState with default from AppState
    updateSSBOBuffer(context, APP_STATE.uiState);

    // Initialize HoverOutput to 0 (no hit)
    resetHoverOutput(context);

    std::cout << "SSBO resources created successfully (UIState: " << sizeof(UIState)
              << " bytes, HoverOutput: " << sizeof(HoverOutput) << " bytes, CelestialObjects: " << CELESTIAL_SSBO_SIZE
              << " bytes)" << "\n";
    return true;
}

// Update SSBO buffer with current UIState
void updateSSBOBuffer(VulkanContext &context, const UIState &state)
{
    if (context.uiStateSSBO.buffer == VK_NULL_HANDLE)
    {
        return;
    }

    void *mapped;
    vkMapMemory(context.device, context.uiStateSSBO.allocation, 0, sizeof(UIState), 0, &mapped);
    std::memcpy(mapped, &state, sizeof(UIState));
    vkUnmapMemory(context.device, context.uiStateSSBO.allocation);
}

// Read hover output from SSBO (returns material ID at mouse position, 0 = no hit)
uint32_t readHoverOutput(VulkanContext &context)
{
    if (context.hoverOutputSSBO.buffer == VK_NULL_HANDLE)
    {
        return 0;
    }

    void *mapped;
    vkMapMemory(context.device, context.hoverOutputSSBO.allocation, 0, sizeof(HoverOutput), 0, &mapped);
    uint32_t result = reinterpret_cast<HoverOutput *>(mapped)->hitMaterialID;
    vkUnmapMemory(context.device, context.hoverOutputSSBO.allocation);

    return result;
}

// Reset hover output SSBO to 0 (call before rendering)
void resetHoverOutput(VulkanContext &context)
{
    if (context.hoverOutputSSBO.buffer == VK_NULL_HANDLE)
    {
        return;
    }

    HoverOutput reset{};
    reset.hitMaterialID = 0;

    void *mapped;
    vkMapMemory(context.device, context.hoverOutputSSBO.allocation, 0, sizeof(HoverOutput), 0, &mapped);
    std::memcpy(mapped, &reset, sizeof(HoverOutput));
    vkUnmapMemory(context.device, context.hoverOutputSSBO.allocation);
}

// Wait for the current frame's fence (ensures previous frame's GPU work is complete)
void waitForCurrentFrameFence(VulkanContext &context)
{
    vkWaitForFences(context.device, 1, &context.inFlightFences[context.currentFrame], VK_TRUE, UINT64_MAX);
}

// Push world constants to command buffer
void pushWorldConstants(VkCommandBuffer cmd, VkPipelineLayout layout, const WorldPushConstants &constants)
{
    vkCmdPushConstants(cmd,
                       layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(WorldPushConstants),
                       &constants);
}

// Push input constants to command buffer (offset after WorldPushConstants)
void pushInputConstants(VkCommandBuffer cmd, VkPipelineLayout layout, const InputPushConstants &constants)
{
    vkCmdPushConstants(cmd,
                       layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       sizeof(WorldPushConstants), // Offset after world constants
                       sizeof(InputPushConstants),
                       &constants);
}

// Push camera constants to command buffer (offset after WorldPushConstants and InputPushConstants)
void pushCameraConstants(VkCommandBuffer cmd, VkPipelineLayout layout, const CameraPushConstants &constants)
{
    vkCmdPushConstants(cmd,
                       layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       sizeof(WorldPushConstants) +
                           sizeof(InputPushConstants), // Offset after world and input constants
                       sizeof(CameraPushConstants),
                       &constants);
}

// ==================================
// Frustum Culling for Celestial Objects
// ==================================

// Frustum plane structure (ax + by + cz + d = 0)
struct FrustumPlane
{
    float a, b, c, d;

    // Normalize the plane
    void normalize()
    {
        float len = std::sqrt(a * a + b * b + c * c);
        if (len > 0.0f)
        {
            a /= len;
            b /= len;
            c /= len;
            d /= len;
        }
    }

    // Distance from point to plane (positive = in front, negative = behind)
    float distanceToPoint(float x, float y, float z) const
    {
        return a * x + b * y + c * z + d;
    }
};

// Extract frustum planes from view-projection matrix
// Uses the Gribb/Hartmann method
static void extractFrustumPlanes(const glm::mat4 &viewProj, FrustumPlane planes[6])
{
    // Left plane
    planes[0].a = viewProj[0][3] + viewProj[0][0];
    planes[0].b = viewProj[1][3] + viewProj[1][0];
    planes[0].c = viewProj[2][3] + viewProj[2][0];
    planes[0].d = viewProj[3][3] + viewProj[3][0];

    // Right plane
    planes[1].a = viewProj[0][3] - viewProj[0][0];
    planes[1].b = viewProj[1][3] - viewProj[1][0];
    planes[1].c = viewProj[2][3] - viewProj[2][0];
    planes[1].d = viewProj[3][3] - viewProj[3][0];

    // Bottom plane
    planes[2].a = viewProj[0][3] + viewProj[0][1];
    planes[2].b = viewProj[1][3] + viewProj[1][1];
    planes[2].c = viewProj[2][3] + viewProj[2][1];
    planes[2].d = viewProj[3][3] + viewProj[3][1];

    // Top plane
    planes[3].a = viewProj[0][3] - viewProj[0][1];
    planes[3].b = viewProj[1][3] - viewProj[1][1];
    planes[3].c = viewProj[2][3] - viewProj[2][1];
    planes[3].d = viewProj[3][3] - viewProj[3][1];

    // Near plane
    planes[4].a = viewProj[0][3] + viewProj[0][2];
    planes[4].b = viewProj[1][3] + viewProj[1][2];
    planes[4].c = viewProj[2][3] + viewProj[2][2];
    planes[4].d = viewProj[3][3] + viewProj[3][2];

    // Far plane
    planes[5].a = viewProj[0][3] - viewProj[0][2];
    planes[5].b = viewProj[1][3] - viewProj[1][2];
    planes[5].c = viewProj[2][3] - viewProj[2][2];
    planes[5].d = viewProj[3][3] - viewProj[3][2];

    // Normalize all planes
    for (int i = 0; i < 6; ++i)
    {
        planes[i].normalize();
    }
}

// Test if a sphere is visible (intersects or inside frustum)
static bool isSphereInFrustum(const FrustumPlane planes[6], float x, float y, float z, float radius)
{
    for (int i = 0; i < 6; ++i)
    {
        float dist = planes[i].distanceToPoint(x, y, z);
        if (dist < -radius)
        {
            return false; // Sphere is completely behind this plane
        }
    }
    return true; // Sphere is at least partially visible
}

// Update celestial objects SSBO with frustum-culled objects
void updateCelestialObjectsSSBO(VulkanContext &context,
                                const std::vector<CelestialObject> &objects,
                                const glm::mat4 &viewMatrix,
                                const glm::mat4 &projMatrix)
{
    if (context.celestialObjectsSSBO.buffer == VK_NULL_HANDLE)
    {
        return;
    }

    // Extract frustum planes from view-projection matrix
    glm::mat4 viewProj = projMatrix * viewMatrix;
    FrustumPlane frustumPlanes[6];
    extractFrustumPlanes(viewProj, frustumPlanes);

    // GPU struct layout (must match shader):
    // struct CelestialObjectGPU {
    //     vec3 position;  // 12 bytes
    //     float radius;   // 4 bytes
    //     vec3 color;     // 12 bytes
    //     int naifId;     // 4 bytes
    // }; // Total: 32 bytes (2 vec4s)

    constexpr size_t CELESTIAL_OBJECT_SIZE = 32;
    constexpr size_t MAX_CELESTIAL_OBJECTS = 32;
    constexpr size_t HEADER_SIZE = 16;

    // Filter objects by frustum visibility
    std::vector<const CelestialObject *> visibleObjects;
    visibleObjects.reserve(objects.size());

    for (const auto &obj : objects)
    {
        if (isSphereInFrustum(frustumPlanes, obj.position.x, obj.position.y, obj.position.z, obj.radius))
        {
            visibleObjects.push_back(&obj);
            if (visibleObjects.size() >= MAX_CELESTIAL_OBJECTS)
            {
                break;
            }
        }
    }

    uint32_t objectCount = static_cast<uint32_t>(visibleObjects.size());
    size_t dataSize = HEADER_SIZE + objectCount * CELESTIAL_OBJECT_SIZE;

    void *mapped;
    vkMapMemory(context.device, context.celestialObjectsSSBO.allocation, 0, dataSize, 0, &mapped);

    // Write header
    auto *header = reinterpret_cast<uint32_t *>(mapped);
    header[0] = objectCount;
    header[1] = 0;
    header[2] = 0;
    header[3] = 0;

    // Write visible objects
    auto *objectData = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(mapped) + HEADER_SIZE);

    for (uint32_t i = 0; i < objectCount; ++i)
    {
        const CelestialObject &obj = *visibleObjects[i];
        size_t offset = i * 8;

        objectData[offset + 0] = obj.position.x;
        objectData[offset + 1] = obj.position.y;
        objectData[offset + 2] = obj.position.z;
        objectData[offset + 3] = obj.radius;

        objectData[offset + 4] = obj.color.x;
        objectData[offset + 5] = obj.color.y;
        objectData[offset + 6] = obj.color.z;
        auto *intPtr = reinterpret_cast<int32_t *>(&objectData[offset + 7]);
        *intPtr = obj.naifId;
    }

    vkUnmapMemory(context.device, context.celestialObjectsSSBO.allocation);
    context.celestialObjectCount = objectCount;
}

// Helper function to convert screen-space coordinates to NDC
// Screen space: (0, 0) = top-left, (width, height) = bottom-right
// NDC space: (-1, -1) = bottom-left, (1, 1) = top-right (with flipped viewport)
static void screenToNDC(float screenX, float screenY, int screenWidth, int screenHeight, float &ndcX, float &ndcY)
{
    // Convert to NDC: screen (0-width) -> NDC (-1 to 1)
    ndcX = (screenX / screenWidth) * 2.0f - 1.0f;
    // With flipped viewport: screen (0-height) -> NDC (1 to -1) so Y=0 maps to NDC Y=1
    ndcY = 1.0f - (screenY / screenHeight) * 2.0f;
}

// Begin building UI vertices (clears the builder and stores screen dimensions)
void BeginUIVertexBuffer(int screenWidth, int screenHeight)
{
    g_uiVertexBuilder.clear();
    g_buildingUIVertices = true;
    g_uiScreenWidth = screenWidth;
    g_uiScreenHeight = screenHeight;
}

// Add a vertex to the UI vertex buffer builder (uses stored screen dimensions)
void AddUIVertex(float x, float y, float r, float g, float b, float a)
{
    if (!g_buildingUIVertices || g_uiScreenWidth <= 0 || g_uiScreenHeight <= 0)
    {
        return; // Not building vertices or invalid screen dimensions
    }

    float ndcX, ndcY;
    screenToNDC(x, y, g_uiScreenWidth, g_uiScreenHeight, ndcX, ndcY);
    g_uiVertexBuilder.push_back({ndcX, ndcY, r, g, b, a});
}

// End building UI vertices and create/update the vertex buffer
uint32_t EndUIVertexBuffer(VulkanContext &context)
{
    g_buildingUIVertices = false;

    if (g_uiVertexBuilder.empty())
    {
        context.uiVertexCount = 0;
        return 0;
    }

    // Create or update vertex buffer
    VkDeviceSize bufferSize = g_uiVertexBuilder.size() * sizeof(UIVertex);
    if (context.uiVertexBuffer.buffer == VK_NULL_HANDLE || context.uiVertexBufferSize < bufferSize)
    {
        // Destroy old buffer if it exists
        if (context.uiVertexBuffer.buffer != VK_NULL_HANDLE)
        {
            destroyBuffer(context, context.uiVertexBuffer);
        }

        // Create new buffer (round up to reasonable size for dynamic updates)
        // Use parentheses to avoid Windows.h max macro conflict
        VkDeviceSize minSize = VkDeviceSize(256 * 1024); // At least 256KB for UI
        VkDeviceSize allocSize = bufferSize > minSize ? bufferSize : minSize;
        // Create buffer without data first, then copy only the actual data size
        context.uiVertexBuffer =
            createBuffer(context,
                         allocSize,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         nullptr); // Don't copy data during creation
        context.uiVertexBufferSize = allocSize;

        // Copy only the actual vertex data
        if (context.uiVertexBuffer.buffer != VK_NULL_HANDLE)
        {
            void *mapped;
            vkMapMemory(context.device, context.uiVertexBuffer.allocation, 0, bufferSize, 0, &mapped);
            std::memcpy(mapped, g_uiVertexBuilder.data(), static_cast<size_t>(bufferSize));
            vkUnmapMemory(context.device, context.uiVertexBuffer.allocation);
        }
    }
    else
    {
        // Update existing buffer
        void *mapped;
        vkMapMemory(context.device, context.uiVertexBuffer.allocation, 0, bufferSize, 0, &mapped);
        std::memcpy(mapped, g_uiVertexBuilder.data(), static_cast<size_t>(bufferSize));
        vkUnmapMemory(context.device, context.uiVertexBuffer.allocation);
    }

    context.uiVertexCount = static_cast<uint32_t>(g_uiVertexBuilder.size());
    return context.uiVertexCount;
}

// Build UI vertex buffer from UI rendering calls
// This function should be called each frame before rendering to build the UI geometry
uint32_t buildUIVertexBuffer(VulkanContext &context, int screenWidth, int screenHeight)
{
    // Begin building vertices (stores screen dimensions for AddUIVertex)
    BeginUIVertexBuffer(screenWidth, screenHeight);

    // Call DrawUserInterface to build the full UI
    // Using stub values for parameters not yet wired up
    std::vector<CelestialBody *> bodies; // Empty for now
    TimeControlParams timeParams{};

    // Use AppState for time parameters - sync both directions
    timeParams.currentJD = APP_STATE.worldState.julianDate;
    timeParams.minJD = 2451545.0;                                          // J2000 epoch
    timeParams.maxJD = 2488070.0;                                          // ~2100 AD
    static double timeDilation = 1.0;                                      // Local storage for slider
    timeDilation = static_cast<double>(APP_STATE.worldState.timeDilation); // Read from AppState each frame
    timeParams.timeDilation = &timeDilation;
    timeParams.isPaused = APP_STATE.worldState.isPaused;

    // Populate visualization toggle states from AppState.uiState
    timeParams.showOrbits = APP_STATE.uiState.showOrbits != 0u;
    timeParams.showRotationAxes = APP_STATE.uiState.showRotationAxes != 0u;
    timeParams.showBarycenters = APP_STATE.uiState.showBarycenters != 0u;
    timeParams.showLagrangePoints = APP_STATE.uiState.showLagrangePoints != 0u;
    timeParams.showCoordinateGrids = APP_STATE.uiState.showCoordinateGrids != 0u;
    timeParams.showMagneticFields = APP_STATE.uiState.showMagneticFields != 0u;
    timeParams.showGravityGrid = APP_STATE.uiState.showGravityGrid != 0u;
    timeParams.showConstellations = APP_STATE.uiState.showConstellations != 0u;
    timeParams.showForceVectors = APP_STATE.uiState.showForceVectors != 0u;
    timeParams.showSunSpot = APP_STATE.uiState.showSunSpot != 0u;
    timeParams.showWireframe = APP_STATE.uiState.showWireframe != 0u;
    timeParams.showVoxelWireframes = APP_STATE.uiState.showVoxelWireframes != 0u;
    timeParams.showAtmosphereLayers = APP_STATE.uiState.showAtmosphereLayers != 0u;

    // Populate render settings from AppState.uiState
    timeParams.fxaaEnabled = APP_STATE.uiState.fxaaEnabled != 0u;
    timeParams.vsyncEnabled = APP_STATE.uiState.vsyncEnabled != 0u;
    timeParams.gravityGridResolution = APP_STATE.uiState.gravityGridResolution;
    timeParams.gravityWarpStrength = APP_STATE.uiState.gravityWarpStrength;
    timeParams.currentFOV = APP_STATE.worldState.camera.fov;
    timeParams.isFullscreen = APP_STATE.uiState.isFullscreen != 0u;
    timeParams.textureResolution = static_cast<TextureResolution>(APP_STATE.uiState.textureResolution);

    // Get mouse position from InputController
    const InputState &input = INPUT.getState();
    double mouseX = input.mouseX;
    double mouseY = input.mouseY;

    // Get current FPS
    int currentFPS = UpdateFPS();

    // Use world triangle count from previous frame (only 3D geometry, not UI)
    int triangleCount = static_cast<int>(context.worldTriangleCount);

    UIInteraction interaction = DrawUserInterface(screenWidth,
                                                  screenHeight,
                                                  currentFPS,
                                                  triangleCount,
                                                  bodies,
                                                  timeParams,
                                                  mouseX,
                                                  mouseY,
                                                  nullptr);

    // Write time dilation back to AppState if changed by slider
    APP_STATE.worldState.timeDilation = static_cast<float>(timeDilation);

    // Handle UI toggle interactions - flip the corresponding state when toggled
    if (interaction.pauseToggled)
    {
        APP_STATE.worldState.isPaused = !APP_STATE.worldState.isPaused;
    }
    if (interaction.orbitsToggled)
    {
        APP_STATE.uiState.showOrbits = APP_STATE.uiState.showOrbits != 0u ? 0u : 1u;
    }
    if (interaction.axesToggled)
    {
        APP_STATE.uiState.showRotationAxes = APP_STATE.uiState.showRotationAxes != 0u ? 0u : 1u;
    }
    if (interaction.barycentersToggled)
    {
        APP_STATE.uiState.showBarycenters = APP_STATE.uiState.showBarycenters != 0u ? 0u : 1u;
    }
    if (interaction.lagrangePointsToggled)
    {
        APP_STATE.uiState.showLagrangePoints = APP_STATE.uiState.showLagrangePoints != 0u ? 0u : 1u;
    }
    if (interaction.coordGridsToggled)
    {
        APP_STATE.uiState.showCoordinateGrids = APP_STATE.uiState.showCoordinateGrids != 0u ? 0u : 1u;
    }
    if (interaction.magneticFieldsToggled)
    {
        APP_STATE.uiState.showMagneticFields = APP_STATE.uiState.showMagneticFields != 0u ? 0u : 1u;
    }
    if (interaction.gravityGridToggled)
    {
        APP_STATE.uiState.showGravityGrid = APP_STATE.uiState.showGravityGrid != 0u ? 0u : 1u;
    }
    if (interaction.constellationsToggled)
    {
        APP_STATE.uiState.showConstellations = APP_STATE.uiState.showConstellations != 0u ? 0u : 1u;
    }
    if (interaction.constellationGridToggled)
    {
        APP_STATE.uiState.showCelestialGrid = APP_STATE.uiState.showCelestialGrid != 0u ? 0u : 1u;
    }
    if (interaction.constellationFiguresToggled)
    {
        APP_STATE.uiState.showConstellationFigures = APP_STATE.uiState.showConstellationFigures != 0u ? 0u : 1u;
    }
    if (interaction.constellationBoundsToggled)
    {
        APP_STATE.uiState.showConstellationBounds = APP_STATE.uiState.showConstellationBounds != 0u ? 0u : 1u;
    }
    if (interaction.forceVectorsToggled)
    {
        APP_STATE.uiState.showForceVectors = APP_STATE.uiState.showForceVectors != 0u ? 0u : 1u;
    }
    if (interaction.sunSpotToggled)
    {
        APP_STATE.uiState.showSunSpot = APP_STATE.uiState.showSunSpot != 0u ? 0u : 1u;
    }
    if (interaction.wireframeToggled)
    {
        APP_STATE.uiState.showWireframe = APP_STATE.uiState.showWireframe != 0u ? 0u : 1u;
    }
    if (interaction.voxelWireframeToggled)
    {
        APP_STATE.uiState.showVoxelWireframes = APP_STATE.uiState.showVoxelWireframes != 0u ? 0u : 1u;
    }
    if (interaction.atmosphereLayersToggled)
    {
        APP_STATE.uiState.showAtmosphereLayers = APP_STATE.uiState.showAtmosphereLayers != 0u ? 0u : 1u;
    }
    if (interaction.fxaaToggled)
    {
        APP_STATE.uiState.fxaaEnabled = APP_STATE.uiState.fxaaEnabled != 0u ? 0u : 1u;
    }
    if (interaction.vsyncToggled)
    {
        APP_STATE.uiState.vsyncEnabled = APP_STATE.uiState.vsyncEnabled != 0u ? 0u : 1u;
    }
    if (interaction.citiesToggled)
    {
        APP_STATE.uiState.citiesEnabled = APP_STATE.uiState.citiesEnabled != 0u ? 0u : 1u;
    }
    if (interaction.heightmapToggled)
    {
        APP_STATE.uiState.heightmapEnabled = APP_STATE.uiState.heightmapEnabled != 0u ? 0u : 1u;
    }
    if (interaction.normalMapToggled)
    {
        APP_STATE.uiState.normalMapEnabled = APP_STATE.uiState.normalMapEnabled != 0u ? 0u : 1u;
    }
    if (interaction.roughnessToggled)
    {
        APP_STATE.uiState.roughnessEnabled = APP_STATE.uiState.roughnessEnabled != 0u ? 0u : 1u;
    }
    if (interaction.newGravityGridResolution >= 0)
    {
        APP_STATE.uiState.gravityGridResolution = interaction.newGravityGridResolution;
    }
    if (interaction.newGravityWarpStrength >= 0.0f)
    {
        APP_STATE.uiState.gravityWarpStrength = interaction.newGravityWarpStrength;
    }
    // Track FOV slider dragging state for save-on-release
    static bool wasFovSliderDragging = false;
    static float fovBeforeDrag = 60.0f;

    if (interaction.newFOV >= 0.0f)
    {
        // If we just started dragging, remember the initial value
        if (!wasFovSliderDragging && interaction.fovSliderDragging)
        {
            fovBeforeDrag = APP_STATE.worldState.camera.fov;
        }
        // Clamp FOV to safe range (5-120 degrees) to prevent shader issues
        float clampedFOV = std::max(5.0f, std::min(120.0f, interaction.newFOV));
        // Update camera state FOV (primary) and UIState (for backward compatibility)
        APP_STATE.worldState.camera.fov = clampedFOV;
        APP_STATE.uiState.currentFOV = clampedFOV;
    }

    // Save when FOV slider drag ends and value changed
    if (wasFovSliderDragging && !interaction.fovSliderDragging)
    {
        if (std::abs(APP_STATE.worldState.camera.fov - fovBeforeDrag) > 0.01f)
        {
            APP_STATE.saveToSettings();
        }
    }
    wasFovSliderDragging = interaction.fovSliderDragging;
    if (interaction.newTextureResolution >= 0)
    {
        APP_STATE.uiState.textureResolution = interaction.newTextureResolution;
        APP_STATE.saveToSettings();
    }
    if (interaction.fullscreenToggled)
    {
        APP_STATE.uiState.isFullscreen = APP_STATE.uiState.isFullscreen != 0u ? 0u : 1u;
    }

    // End building and create buffer
    uint32_t uiVertexCount = EndUIVertexBuffer(context);

    // Update triangle counts for the NEXT frame
    // World triangles: fullscreen quad = 6 vertices = 2 triangles (displayed in UI)
    context.worldTriangleCount = context.fullscreenQuadVertexCount / 3;
    // UI triangles: each 3 vertices = 1 triangle (tracked but not displayed)
    context.uiTriangleCount = uiVertexCount / 3;
    // Total triangles (world + UI, for internal tracking)
    context.totalTriangleCount = context.worldTriangleCount + context.uiTriangleCount;

    return uiVertexCount;
}

// ==================================
// Skybox Texture Implementation
// ==================================

// Helper function to find memory type for image
static uint32_t findImageMemoryType(VulkanContext &context, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(context.physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    std::cerr << "Failed to find suitable memory type for image!" << "\n";
    return UINT32_MAX;
}

// Load skybox cubemap texture from file (vertical strip format: 6 faces stacked)
bool loadSkyboxTexture(VulkanContext &context, const std::string &filepath)
{
    // Check if file exists
    std::ifstream file(filepath);
    if (!file.good())
    {
        std::cerr << "Skybox texture file not found: " << filepath << "\n";
        return false;
    }
    file.close();

    // Load HDR image using stb_image
    // Note: stbi is already included via other files, but we need stbi_loadf for HDR
    int width, height, channels;
    bool isHDR = filepath.find(".hdr") != std::string::npos || filepath.find(".HDR") != std::string::npos;

    void *pixelData = nullptr;
    VkFormat format;
    VkDeviceSize imageSize;

    if (isHDR)
    {
        // Load as float data for HDR
        float *hdrData = stbi_loadf(filepath.c_str(), &width, &height, &channels, 4); // Force RGBA
        if (!hdrData)
        {
            std::cerr << "Failed to load HDR skybox texture: " << filepath << "\n";
            return false;
        }
        pixelData = hdrData;
        format = VK_FORMAT_R32G32B32A32_SFLOAT;
        imageSize = static_cast<VkDeviceSize>(width) * height * 4 * sizeof(float);
    }
    else
    {
        // Load as unsigned char data for PNG/JPG
        unsigned char *imgData = stbi_load(filepath.c_str(), &width, &height, &channels, 4); // Force RGBA
        if (!imgData)
        {
            std::cerr << "Failed to load skybox texture: " << filepath << "\n";
            return false;
        }
        pixelData = imgData;
        format = VK_FORMAT_R8G8B8A8_UNORM;
        imageSize = static_cast<VkDeviceSize>(width) * height * 4;
    }

    std::cout << "Loading skybox cubemap texture: " << width << "x" << height << " (" << (isHDR ? "HDR" : "LDR") << ")"
              << "\n";

    // Create staging buffer
    VulkanBuffer stagingBuffer =
        createBuffer(context,
                     imageSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Copy pixel data to staging buffer
    void *mapped;
    vkMapMemory(context.device, stagingBuffer.allocation, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, pixelData, imageSize);
    vkUnmapMemory(context.device, stagingBuffer.allocation);

    // Free pixel data
    stbi_image_free(pixelData);

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    if (vkCreateImage(context.device, &imageInfo, nullptr, &context.skyboxImage) != VK_SUCCESS)
    {
        std::cerr << "Failed to create skybox image!" << "\n";
        destroyBuffer(context, stagingBuffer);
        return false;
    }

    // Allocate memory for image
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(context.device, context.skyboxImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findImageMemoryType(context, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (allocInfo.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(context.device, &allocInfo, nullptr, &context.skyboxImageMemory) != VK_SUCCESS)
    {
        std::cerr << "Failed to allocate skybox image memory!" << "\n";
        vkDestroyImage(context.device, context.skyboxImage, nullptr);
        destroyBuffer(context, stagingBuffer);
        return false;
    }

    vkBindImageMemory(context.device, context.skyboxImage, context.skyboxImageMemory, 0);

    // Transition image layout and copy data
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = context.commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    vkAllocateCommandBuffers(context.device, &cmdAllocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Transition to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = context.skyboxImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    vkCmdCopyBufferToImage(commandBuffer,
                           stagingBuffer.buffer,
                           context.skyboxImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    vkEndCommandBuffer(commandBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(context.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(context.graphicsQueue);

    vkFreeCommandBuffers(context.device, context.commandPool, 1, &commandBuffer);
    destroyBuffer(context, stagingBuffer);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = context.skyboxImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context.device, &viewInfo, nullptr, &context.skyboxImageView) != VK_SUCCESS)
    {
        std::cerr << "Failed to create skybox image view!" << "\n";
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(context.device, &samplerInfo, nullptr, &context.skyboxSampler) != VK_SUCCESS)
    {
        std::cerr << "Failed to create skybox sampler!" << "\n";
        return false;
    }

    context.skyboxTextureReady = true;
    std::cout << "Skybox cubemap texture loaded successfully" << "\n";
    return true;
}

// Update skybox descriptor set binding (call after loading texture)
void updateSkyboxDescriptorSet(VulkanContext &context)
{
    if (!context.skyboxTextureReady || context.ssboDescriptorSet == VK_NULL_HANDLE)
    {
        return;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = context.skyboxImageView;
    imageInfo.sampler = context.skyboxSampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = context.ssboDescriptorSet;
    descriptorWrite.dstBinding = 3;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(context.device, 1, &descriptorWrite, 0, nullptr);
    std::cout << "Skybox descriptor set updated" << "\n";
}

// Cleanup skybox texture resources
void cleanupSkyboxTexture(VulkanContext &context)
{
    if (context.skyboxSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(context.device, context.skyboxSampler, nullptr);
        context.skyboxSampler = VK_NULL_HANDLE;
    }

    if (context.skyboxImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(context.device, context.skyboxImageView, nullptr);
        context.skyboxImageView = VK_NULL_HANDLE;
    }

    if (context.skyboxImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(context.device, context.skyboxImage, nullptr);
        context.skyboxImage = VK_NULL_HANDLE;
    }

    if (context.skyboxImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(context.device, context.skyboxImageMemory, nullptr);
        context.skyboxImageMemory = VK_NULL_HANDLE;
    }

    context.skyboxTextureReady = false;
}

// ==================================
// Earth Material Texture Functions
// ==================================

// Helper function to load a single texture into Vulkan image/view/sampler
// Returns true on success, outputs to image, imageMemory, imageView, sampler
static bool loadTextureHelper(VulkanContext &context,
                              const std::string &filepath,
                              VkImage &image,
                              VkDeviceMemory &imageMemory,
                              VkImageView &imageView,
                              VkSampler &sampler,
                              VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                              VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
{
    // Check if file exists
    std::ifstream file(filepath);
    if (!file.good())
    {
        std::cerr << "Texture file not found: " << filepath << "\n";
        return false;
    }
    file.close();

    // Load image using stb_image
    int width, height, channels;
    unsigned char *imgData = stbi_load(filepath.c_str(), &width, &height, &channels, 4); // Force RGBA
    if (!imgData)
    {
        std::cerr << "Failed to load texture: " << filepath << "\n";
        return false;
    }

    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    std::cout << "Loading texture: " << filepath << " (" << width << "x" << height << ")\n";

    // Create staging buffer
    VulkanBuffer stagingBuffer =
        createBuffer(context,
                     imageSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Copy pixel data to staging buffer
    void *mapped;
    vkMapMemory(context.device, stagingBuffer.allocation, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, imgData, imageSize);
    vkUnmapMemory(context.device, stagingBuffer.allocation);

    stbi_image_free(imgData);

    // Create image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    if (vkCreateImage(context.device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        std::cerr << "Failed to create image for: " << filepath << "\n";
        destroyBuffer(context, stagingBuffer);
        return false;
    }

    // Allocate memory for image
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(context.device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findImageMemoryType(context, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (allocInfo.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(context.device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
    {
        std::cerr << "Failed to allocate image memory for: " << filepath << "\n";
        vkDestroyImage(context.device, image, nullptr);
        destroyBuffer(context, stagingBuffer);
        return false;
    }

    vkBindImageMemory(context.device, image, imageMemory, 0);

    // Transition image layout and copy data
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = context.commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    vkAllocateCommandBuffers(context.device, &cmdAllocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Transition to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    vkCmdCopyBufferToImage(commandBuffer,
                           stagingBuffer.buffer,
                           image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    vkEndCommandBuffer(commandBuffer);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(context.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(context.graphicsQueue);

    vkFreeCommandBuffers(context.device, context.commandPool, 1, &commandBuffer);
    destroyBuffer(context, stagingBuffer);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context.device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        std::cerr << "Failed to create image view for: " << filepath << "\n";
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = addressModeU;
    samplerInfo.addressModeV = addressModeV;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(context.device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
    {
        std::cerr << "Failed to create sampler for: " << filepath << "\n";
        return false;
    }

    return true;
}

// Load Earth material textures for NAIF ID 399
bool loadEarthTextures(VulkanContext &context,
                       const std::string &basePath,
                       const std::string &resolutionFolder,
                       int currentMonth)
{
    // Clean up any existing Earth textures first
    cleanupEarthTextures(context);

    bool allLoaded = true;
    std::string resFolderPath = basePath + "/" + resolutionFolder;

    // Load Earth color texture (monthly Blue Marble) - Binding 4
    // Month is 1-12, texture files are earth_month_01.png, earth_month_02.png, etc.
    char monthStr[3];
    std::snprintf(monthStr, sizeof(monthStr), "%02d", currentMonth);
    std::string colorPath = resFolderPath + "/earth_month_" + monthStr + ".png";

    // Try PNG first, then JPG
    if (!std::ifstream(colorPath).good())
    {
        colorPath = resFolderPath + "/earth_month_" + monthStr + ".jpg";
    }

    if (!loadTextureHelper(context,
                           colorPath,
                           context.earthColorImage,
                           context.earthColorImageMemory,
                           context.earthColorImageView,
                           context.earthColorSampler))
    {
        std::cerr << "Failed to load Earth color texture: " << colorPath << "\n";
        allLoaded = false;
    }
    else
    {
        std::cout << "Loaded Earth color texture: " << colorPath << "\n";
    }

    // Load Earth normal map - Binding 5
    std::string normalPath = resFolderPath + "/earth_normal.png";
    if (!loadTextureHelper(context,
                           normalPath,
                           context.earthNormalImage,
                           context.earthNormalImageMemory,
                           context.earthNormalImageView,
                           context.earthNormalSampler))
    {
        std::cerr << "Warning: Failed to load Earth normal map (optional): " << normalPath << "\n";
        // Normal map is optional - don't fail overall
    }

    // Load Earth nightlights texture - Binding 6
    std::string nightlightsPath = resFolderPath + "/earth_nightlights.png";
    if (!loadTextureHelper(context,
                           nightlightsPath,
                           context.earthNightlightsImage,
                           context.earthNightlightsImageMemory,
                           context.earthNightlightsImageView,
                           context.earthNightlightsSampler))
    {
        std::cerr << "Warning: Failed to load Earth nightlights (optional): " << nightlightsPath << "\n";
        // Nightlights is optional - don't fail overall
    }

    // Load Earth specular/roughness texture - Binding 7
    std::string specularPath = resFolderPath + "/earth_specular.png";
    if (!loadTextureHelper(context,
                           specularPath,
                           context.earthSpecularImage,
                           context.earthSpecularImageMemory,
                           context.earthSpecularImageView,
                           context.earthSpecularSampler))
    {
        std::cerr << "Warning: Failed to load Earth specular (optional): " << specularPath << "\n";
        // Specular is optional - don't fail overall
    }

    // Mark as ready if at least the color texture loaded
    context.earthTexturesReady = (context.earthColorImage != VK_NULL_HANDLE);

    if (context.earthTexturesReady)
    {
        std::cout << "Earth textures loaded successfully (NAIF ID 399)\n";
    }

    return context.earthTexturesReady;
}

// Update Earth texture descriptor set bindings (call after loading textures)
void updateEarthDescriptorSet(VulkanContext &context)
{
    if (context.ssboDescriptorSet == VK_NULL_HANDLE)
    {
        return;
    }

    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorImageInfo> imageInfos(4); // Store image infos to keep them alive

    // Binding 4: Earth Color texture
    if (context.earthColorImage != VK_NULL_HANDLE)
    {
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[0].imageView = context.earthColorImageView;
        imageInfos[0].sampler = context.earthColorSampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = context.ssboDescriptorSet;
        write.dstBinding = 4;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos[0];
        writes.push_back(write);
    }

    // Binding 5: Earth Normal map
    if (context.earthNormalImage != VK_NULL_HANDLE)
    {
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].imageView = context.earthNormalImageView;
        imageInfos[1].sampler = context.earthNormalSampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = context.ssboDescriptorSet;
        write.dstBinding = 5;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos[1];
        writes.push_back(write);
    }

    // Binding 6: Earth Nightlights
    if (context.earthNightlightsImage != VK_NULL_HANDLE)
    {
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[2].imageView = context.earthNightlightsImageView;
        imageInfos[2].sampler = context.earthNightlightsSampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = context.ssboDescriptorSet;
        write.dstBinding = 6;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos[2];
        writes.push_back(write);
    }

    // Binding 7: Earth Specular
    if (context.earthSpecularImage != VK_NULL_HANDLE)
    {
        imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[3].imageView = context.earthSpecularImageView;
        imageInfos[3].sampler = context.earthSpecularSampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = context.ssboDescriptorSet;
        write.dstBinding = 7;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos[3];
        writes.push_back(write);
    }

    if (!writes.empty())
    {
        vkUpdateDescriptorSets(context.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        std::cout << "Earth descriptor set updated (" << writes.size() << " bindings)\n";
    }
}

// Helper to cleanup a single texture
static void cleanupTextureHelper(VulkanContext &context,
                                 VkImage &image,
                                 VkDeviceMemory &imageMemory,
                                 VkImageView &imageView,
                                 VkSampler &sampler)
{
    if (sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(context.device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    if (imageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(context.device, imageView, nullptr);
        imageView = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE)
    {
        vkDestroyImage(context.device, image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (imageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(context.device, imageMemory, nullptr);
        imageMemory = VK_NULL_HANDLE;
    }
}

// Cleanup Earth texture resources
void cleanupEarthTextures(VulkanContext &context)
{
    cleanupTextureHelper(context,
                         context.earthColorImage,
                         context.earthColorImageMemory,
                         context.earthColorImageView,
                         context.earthColorSampler);

    cleanupTextureHelper(context,
                         context.earthNormalImage,
                         context.earthNormalImageMemory,
                         context.earthNormalImageView,
                         context.earthNormalSampler);

    cleanupTextureHelper(context,
                         context.earthNightlightsImage,
                         context.earthNightlightsImageMemory,
                         context.earthNightlightsImageView,
                         context.earthNightlightsSampler);

    cleanupTextureHelper(context,
                         context.earthSpecularImage,
                         context.earthSpecularImageMemory,
                         context.earthSpecularImageView,
                         context.earthSpecularSampler);

    context.earthTexturesReady = false;
}
