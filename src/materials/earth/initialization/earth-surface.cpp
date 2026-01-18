// ============================================================================
// Initialization - Load Combined Textures into OpenGL
// ============================================================================
// All textures are in sinusoidal projection (orange peel layout)

#include "../../../concerns/helpers/gl.h"
#include "../../../concerns/helpers/shader-loader.h"
#include "../../../concerns/helpers/vulkan.h"
#include "../../../concerns/settings.h"
#include "../../helpers/noise.h"
#include "../earth-material.h"
#include "../voxel-octree.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib> // For std::exit
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// stb_image for loading textures
// Note: STB_IMAGE_IMPLEMENTATION is defined in setup.cpp, so we just include the header here
#include <stb_image.h>


// Forward declaration - VulkanContext needs to be accessible
extern VulkanContext *g_vulkanContext;

GLuint EarthMaterial::loadTexture(const std::string &filepath)
{
    if (!g_vulkanContext)
    {
        std::cerr << "Vulkan context not initialized!" << "\n";
        return 0;
    }

    int width, height, channels;
    stbi_set_flip_vertically_on_load(false); // Vulkan expects top-to-bottom

    unsigned char *data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);

    if (!data)
    {
        std::cerr << "Failed to load texture: " << filepath << "\n";
        return 0;
    }

    // Convert to Vulkan format
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    size_t pixelSize = 4;
    if (channels == 1)
    {
        format = VK_FORMAT_R8_UNORM;
        pixelSize = 1;
    }
    else if (channels == 2)
    {
        // 2-channel RG format (for wind textures: R=u, G=v)
        format = VK_FORMAT_R8G8_UNORM;
        pixelSize = 2;
    }
    else if (channels == 3)
    {
        format = VK_FORMAT_R8G8B8_UNORM;
        pixelSize = 3;
        // Convert RGB to RGBA for Vulkan (many formats prefer RGBA)
        unsigned char *rgbaData = new unsigned char[width * height * 4];
        for (int i = 0; i < width * height; i++)
        {
            rgbaData[i * 4 + 0] = data[i * 3 + 0];
            rgbaData[i * 4 + 1] = data[i * 3 + 1];
            rgbaData[i * 4 + 2] = data[i * 3 + 2];
            rgbaData[i * 4 + 3] = 255;
        }
        stbi_image_free(data);
        data = rgbaData;
        format = VK_FORMAT_R8G8B8A8_UNORM;
        pixelSize = 4;
    }
    else if (channels == 4)
    {
        format = VK_FORMAT_R8G8B8A8_UNORM;
        pixelSize = 4;
    }

    // Create Vulkan texture
    VulkanTexture texture = createTexture2D(*g_vulkanContext,
                                            static_cast<uint32_t>(width),
                                            static_cast<uint32_t>(height),
                                            format,
                                            data,
                                            VK_FILTER_LINEAR,
                                            VK_FILTER_LINEAR,
                                            VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    stbi_image_free(data);

    // Register texture and return handle
    if (!g_textureRegistry)
    {
        std::cerr << "Texture registry not initialized!" << "\n";
        destroyTexture(*g_vulkanContext, texture);
        return 0;
    }

    return g_textureRegistry->registerTexture(texture);
}

// Specialized loader for wind textures (2-channel RG format)
// Ensures proper 2-channel texture loading for wind force vectors
GLuint EarthMaterial::loadWindTexture(const std::string &filepath)
{
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true); // OpenGL expects bottom-to-top

    // Force load as 2 channels (RG format: R=u wind, G=v wind)
    unsigned char *data = stbi_load(filepath.c_str(), &width, &height, &channels, 2);

    if (!data)
    {
        std::cerr << "Failed to load wind texture: " << filepath << "\n";
        return 0;
    }

    if (channels != 2)
    {
        std::cerr << "WARNING: Wind texture has " << channels << " channels, expected 2 (RG format)" << "\n";
        std::cerr << "  This may cause incorrect wind data sampling" << "\n";
    }

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    // Try to use GL_RG format (OpenGL 3.0+) for proper 2-channel storage
    // Fall back to GL_LUMINANCE_ALPHA if GL_RG is not available (OpenGL 2.1)
    GLenum internalFormat = GL_LUMINANCE_ALPHA;
    GLenum format = GL_LUMINANCE_ALPHA;

    // Check if GL_RG is available (requires OpenGL 3.0+ or ARB_texture_rg extension)
    // For now, we'll use GL_LUMINANCE_ALPHA and sample with .ra in the shader
    // GL_LUMINANCE_ALPHA stores: first channel -> LUMINANCE (replicated to RGB), second channel -> ALPHA
    // So sampling gives (L, L, L, A), and we use .ra to get (u wind, v wind)
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    std::cout << "  Wind texture loaded: " << width << "x" << height << " (2 channels: RG)" << "\n";

    return textureId;
}

void EarthMaterial::generateNoiseTextures()
{
    if (noiseTexturesGenerated_)
        return;

    std::cout << "Generating noise textures for city light flickering..." << "\n";

    // Micro noise: Fine-grained (2048x1024) - ~20km per pixel
    // Higher resolution for fine detail where cities flicker independently
    const int microWidth = 2048;
    const int microHeight = 1024;

    // Hourly noise: Coarser (512x256) - ~80km per pixel
    // Lower resolution for regional variation
    const int hourlyWidth = 512;
    const int hourlyHeight = 256;

    // Generate micro noise texture
    {
        std::vector<unsigned char> microData(microWidth * microHeight);

        // Scale factor determines noise "grain size"
        // Higher = more peaks across the texture = finer detail
        float scale = 50.0f; // ~40 noise peaks across width

        for (int y = 0; y < microHeight; y++)
        {
            for (int x = 0; x < microWidth; x++)
            {
                // Map to UV coordinates
                float u = static_cast<float>(x) / microWidth;
                float v = static_cast<float>(y) / microHeight;

                // Generate noise (FBM for more natural appearance)
                float noise = perlinFBM(u * scale, v * scale * 0.5f, 4, 0.5f);

                // Map from [-1,1] to [0,255]
                int value = static_cast<int>((noise + 1.0f) * 127.5f);
                value = std::max(0, std::min(255, value));

                microData[y * microWidth + x] = static_cast<unsigned char>(value);
            }
        }

        glGenTextures(1, &microNoiseTexture_);
        glBindTexture(GL_TEXTURE_2D, microNoiseTexture_);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     microWidth,
                     microHeight,
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     microData.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_REPEAT); // Tileable
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_REPEAT); // Tileable
        glBindTexture(GL_TEXTURE_2D, 0);

        // Also create Vulkan texture and register it
        extern TextureRegistry *g_textureRegistry;
        if (g_vulkanContext && g_textureRegistry)
        {
            VulkanTexture vkTexture = createTexture2D(*g_vulkanContext,
                                                      microWidth,
                                                      microHeight,
                                                      VK_FORMAT_R8_UNORM, // Single channel (luminance)
                                                      microData.data(),
                                                      VK_FILTER_LINEAR,
                                                      VK_FILTER_LINEAR,
                                                      VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                      VK_SAMPLER_ADDRESS_MODE_REPEAT);
            // Register and update the handle to point to Vulkan texture
            microNoiseTexture_ = g_textureRegistry->registerTexture(vkTexture);
        }

        std::cout << "  Micro noise: " << microWidth << "x" << microHeight << " (fine flicker)" << "\n";
    }

    // Generate hourly noise texture
    {
        std::vector<unsigned char> hourlyData(hourlyWidth * hourlyHeight);

        // Coarser scale for regional variation
        float scale = 15.0f; // ~15 noise peaks across width

        for (int y = 0; y < hourlyHeight; y++)
        {
            for (int x = 0; x < hourlyWidth; x++)
            {
                float u = static_cast<float>(x) / hourlyWidth;
                float v = static_cast<float>(y) / hourlyHeight;

                // Use different seed offset to make it visually different from
                // micro
                float noise = perlinFBM(u * scale + 100.0f, v * scale * 0.5f + 100.0f, 4, 0.5f);

                int value = static_cast<int>((noise + 1.0f) * 127.5f);
                value = std::max(0, std::min(255, value));

                hourlyData[y * hourlyWidth + x] = static_cast<unsigned char>(value);
            }
        }

        glGenTextures(1, &hourlyNoiseTexture_);
        glBindTexture(GL_TEXTURE_2D, hourlyNoiseTexture_);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE,
                     hourlyWidth,
                     hourlyHeight,
                     0,
                     GL_LUMINANCE,
                     GL_UNSIGNED_BYTE,
                     hourlyData.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                        GL_REPEAT); // Tileable
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                        GL_REPEAT); // Tileable
        glBindTexture(GL_TEXTURE_2D, 0);

        // Also create Vulkan texture and register it
        extern TextureRegistry *g_textureRegistry;
        if (g_vulkanContext && g_textureRegistry)
        {
            VulkanTexture vkTexture = createTexture2D(*g_vulkanContext,
                                                      hourlyWidth,
                                                      hourlyHeight,
                                                      VK_FORMAT_R8_UNORM, // Single channel (luminance)
                                                      hourlyData.data(),
                                                      VK_FILTER_LINEAR,
                                                      VK_FILTER_LINEAR,
                                                      VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                      VK_SAMPLER_ADDRESS_MODE_REPEAT);
            // Register and update the handle to point to Vulkan texture
            hourlyNoiseTexture_ = g_textureRegistry->registerTexture(vkTexture);
        }

        std::cout << "  Hourly noise: " << hourlyWidth << "x" << hourlyHeight << " (regional variation)" << "\n";
    }

    noiseTexturesGenerated_ = true;
    std::cout << "Noise textures generated successfully" << "\n";
}

// ============================================================================
// Surface Shader Initialization
// ============================================================================

bool EarthMaterial::initializeSurfaceShader()
{
    extern VulkanContext *g_vulkanContext;
    if (!g_vulkanContext)
    {
        std::cerr << "ERROR: Vulkan context not initialized!" << '\n';
        return false;
    }

    // Early return if pipeline is already created
    if (graphicsPipeline_ != VK_NULL_HANDLE)
    {
        shaderAvailable_ = true;
        return true;
    }

    // Load shaders from files
    std::string vertexShaderPath = getShaderPath("earth-vertex.glsl");
    std::string vertexShaderSource = loadShaderFile(vertexShaderPath);
    if (vertexShaderSource.empty())
    {
        std::cerr << "ERROR: EarthMaterial::initializeSurfaceShader() - Could not load earth-vertex.glsl from file"
                  << '\n';
        std::cerr << "  Tried path: " << vertexShaderPath << '\n';
        std::cerr << "  Shader-based rendering is required. Cannot continue." << '\n';
        std::exit(1);
    }

    std::string fragmentShaderPath = getShaderPath("earth-fragment.glsl");
    std::string fragmentShaderSource = loadShaderFile(fragmentShaderPath);
    if (fragmentShaderSource.empty())
    {
        std::cerr << "ERROR: EarthMaterial::initializeSurfaceShader() - Could not load earth-fragment.glsl from file"
                  << '\n';
        std::cerr << "  Tried path: " << fragmentShaderPath << '\n';
        std::cerr << "  Shader-based rendering is required. Cannot continue." << '\n';
        std::exit(1);
    }

    // Create Vulkan shader modules
    VulkanShader vertexShader = createShaderModule(*g_vulkanContext, vertexShaderSource, VK_SHADER_STAGE_VERTEX_BIT);
    VulkanShader fragmentShader =
        createShaderModule(*g_vulkanContext, fragmentShaderSource, VK_SHADER_STAGE_FRAGMENT_BIT);

    if (vertexShader.module == VK_NULL_HANDLE || fragmentShader.module == VK_NULL_HANDLE)
    {
        std::cerr << "ERROR: Failed to create Vulkan shader modules!" << '\n';
        if (vertexShader.module != VK_NULL_HANDLE)
            destroyShaderModule(*g_vulkanContext, vertexShader);
        if (fragmentShader.module != VK_NULL_HANDLE)
            destroyShaderModule(*g_vulkanContext, fragmentShader);
        return false;
    }

    vertexShaderModule_ = vertexShader.module;
    fragmentShaderModule_ = fragmentShader.module;

    // Create descriptor set layout for uniforms and textures
    // Vertex shader: binding 0-1 (samplers), binding 2 (Uniforms with MVP)
    // Fragment shader: binding 0-15 (samplers), binding 16 (Uniforms)
    // Note: Vertex and fragment shaders share some bindings (0-1 conflict!)
    // We'll create bindings for all used slots:
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    // Textures (binding 0-15) - used by both vertex (0-1) and fragment (0-15)
    // Texture bindings: 0-15 for textures
    // Note: Binding 2 is used for uNormalMap texture, so we skip it in the loop and add it separately
    for (uint32_t i = 0; i < 16; i++)
    {
        // Skip binding 2 - it will be added as a texture binding below
        if (i == 2)
            continue;

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = i;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        // Binding 0-1 used by vertex shader, all used by fragment shader
        binding.stageFlags =
            (i < 2) ? (VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT) : VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(binding);
    }

    // Binding 2: uNormalMap texture (must be after the loop to avoid conflict with uniform buffer)
    VkDescriptorSetLayoutBinding normalMapBinding{};
    normalMapBinding.binding = 2;
    normalMapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalMapBinding.descriptorCount = 1;
    normalMapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(normalMapBinding);

    // Vertex shader uniform buffer (binding 17 - moved from 2 to avoid conflict)
    VkDescriptorSetLayoutBinding vertexUniformBinding{};
    vertexUniformBinding.binding = 17;
    vertexUniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    vertexUniformBinding.descriptorCount = 1;
    vertexUniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings.push_back(vertexUniformBinding);

    // Fragment shader uniform buffer (binding 16)
    VkDescriptorSetLayoutBinding fragmentUniformBinding{};
    fragmentUniformBinding.binding = 16;
    fragmentUniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    fragmentUniformBinding.descriptorCount = 1;
    fragmentUniformBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(fragmentUniformBinding);

    descriptorSetLayout_ = createDescriptorSetLayout(*g_vulkanContext, bindings);

    // Create descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes;
    VkDescriptorPoolSize texturePoolSize{};
    texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturePoolSize.descriptorCount = 16 * 2; // 16 textures * 2 frames in flight
    poolSizes.push_back(texturePoolSize);

    VkDescriptorPoolSize uniformPoolSize{};
    uniformPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // Need 2 uniform buffers per descriptor set (bindings 16 and 17) × 2 frames in flight = 4 total
    uniformPoolSize.descriptorCount = 4; // 2 uniform buffers (vertex + fragment) × 2 frames in flight
    poolSizes.push_back(uniformPoolSize);

    descriptorPool_ = createDescriptorPool(*g_vulkanContext, 2, poolSizes); // 2 frames in flight

    // Allocate descriptor sets (one per frame in flight)
    descriptorSets_ = allocateDescriptorSets(*g_vulkanContext, descriptorPool_, descriptorSetLayout_, 2);

    // Create uniform buffers for vertex and fragment shaders
    // Vertex shader Uniforms (binding 2): float + int + vec3*2 + float + vec3*4 + int + float + mat4 = ~128 bytes
    // Fragment shader Uniforms (binding 16): float*3 + vec2 + vec3*5 + float + int*3 + ... = ~200 bytes
    // Round up to 256 bytes each for alignment
    size_t vertexUniformBufferSize = 256;
    size_t fragmentUniformBufferSize = 256;

    VulkanBuffer vertexUniformBuffer =
        createBuffer(*g_vulkanContext,
                     vertexUniformBufferSize,
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VulkanBuffer fragmentUniformBuffer =
        createBuffer(*g_vulkanContext,
                     fragmentUniformBufferSize,
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Store uniform buffers
    vertexUniformBuffer_ = vertexUniformBuffer;
    fragmentUniformBuffer_ = fragmentUniformBuffer;

    // Update descriptor sets with uniform buffers
    for (size_t i = 0; i < descriptorSets_.size(); i++)
    {
        updateDescriptorSetUniformBuffer(
            *g_vulkanContext,
            descriptorSets_[i],
            17, // Vertex shader uniform binding (changed from 2 to avoid conflict with uNormalMap texture)
            vertexUniformBuffer.buffer,
            0,
            vertexUniformBufferSize);

        updateDescriptorSetUniformBuffer(*g_vulkanContext,
                                         descriptorSets_[i],
                                         16, // Fragment shader uniform binding
                                         fragmentUniformBuffer.buffer,
                                         0,
                                         fragmentUniformBufferSize);
    }

    // Create graphics pipeline
    PipelineCreateInfo pipelineInfo;
    pipelineInfo.vertexShader = vertexShaderModule_;
    pipelineInfo.fragmentShader = fragmentShaderModule_;

    // Vertex input (matches EarthVoxelOctree::MeshVertex)
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(EarthVoxelOctree::MeshVertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    pipelineInfo.vertexBindings.push_back(bindingDescription);

    // Vertex attributes
    // layout(location = 0) in vec3 aPosition;
    VkVertexInputAttributeDescription posAttr{};
    posAttr.location = 0;
    posAttr.binding = 0;
    posAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
    posAttr.offset = offsetof(EarthVoxelOctree::MeshVertex, position);
    pipelineInfo.vertexAttributes.push_back(posAttr);

    // layout(location = 1) in vec2 aTexCoord;
    VkVertexInputAttributeDescription texAttr{};
    texAttr.location = 1;
    texAttr.binding = 0;
    texAttr.format = VK_FORMAT_R32G32_SFLOAT;
    texAttr.offset = offsetof(EarthVoxelOctree::MeshVertex, uv);
    pipelineInfo.vertexAttributes.push_back(texAttr);

    // layout(location = 2) in vec3 aNormal;
    VkVertexInputAttributeDescription normalAttr{};
    normalAttr.location = 2;
    normalAttr.binding = 0;
    normalAttr.format = VK_FORMAT_R32G32B32_SFLOAT;
    normalAttr.offset = offsetof(EarthVoxelOctree::MeshVertex, normal);
    pipelineInfo.vertexAttributes.push_back(normalAttr);

    pipelineInfo.descriptorSetLayouts.push_back(descriptorSetLayout_);
    pipelineInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInfo.depthTest = true;
    pipelineInfo.depthWrite = true;
    pipelineInfo.depthCompareOp = VK_COMPARE_OP_LESS;
    // Enable backface culling (cull faces facing away from camera)
    pipelineInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    pipelineInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // CCW vertices are front-facing

    graphicsPipeline_ = createGraphicsPipeline(*g_vulkanContext, pipelineInfo, pipelineLayout_);

    if (graphicsPipeline_ == VK_NULL_HANDLE)
    {
        std::cerr << "ERROR: Failed to create graphics pipeline!" << '\n';
        return false;
    }

    shaderAvailable_ = true;
    shaderProgram_ = 1; // Legacy compatibility

    // Restore program state (we activated it earlier to get uniform
    // locations)
    glUseProgram(0);

    uniformNightlights_ = glGetUniformLocation(shaderProgram_, "uNightlights");
    uniformWindTexture1_ = glGetUniformLocation(shaderProgram_, "uWindTexture1");
    uniformWindTexture2_ = glGetUniformLocation(shaderProgram_, "uWindTexture2");
    uniformWindBlendFactor_ = glGetUniformLocation(shaderProgram_, "uWindBlendFactor");
    uniformWindTextureSize_ = glGetUniformLocation(shaderProgram_, "uWindTextureSize");
    uniformTime_ = glGetUniformLocation(shaderProgram_, "uTime");
    uniformMicroNoise_ = glGetUniformLocation(shaderProgram_, "uMicroNoise");
    uniformHourlyNoise_ = glGetUniformLocation(shaderProgram_, "uHourlyNoise");
    uniformSpecular_ = glGetUniformLocation(shaderProgram_, "uSpecular");
    uniformIceMask_ = glGetUniformLocation(shaderProgram_, "uIceMask");
    uniformIceMask2_ = glGetUniformLocation(shaderProgram_, "uIceMask2");
    uniformIceBlendFactor_ = glGetUniformLocation(shaderProgram_, "uIceBlendFactor");
    uniformLandmassMask_ = glGetUniformLocation(shaderProgram_, "uLandmassMask");
    uniformCameraPos_ = glGetUniformLocation(shaderProgram_, "uCameraPos");
    uniformCameraDir_ = glGetUniformLocation(shaderProgram_, "uCameraDir");
    uniformCameraFOV_ = glGetUniformLocation(shaderProgram_, "uCameraFOV");
    uniformPrimeMeridianDir_ = glGetUniformLocation(shaderProgram_, "uPrimeMeridianDir");
    uniformBathymetryDepth_ = glGetUniformLocation(shaderProgram_, "uBathymetryDepth");
    uniformBathymetryNormal_ = glGetUniformLocation(shaderProgram_, "uBathymetryNormal");
    uniformCombinedNormal_ = glGetUniformLocation(shaderProgram_, "uCombinedNormal");
    uniformWindTexture1_ = glGetUniformLocation(shaderProgram_, "uWindTexture1");
    uniformWindTexture2_ = glGetUniformLocation(shaderProgram_, "uWindTexture2");
    uniformWindBlendFactor_ = glGetUniformLocation(shaderProgram_, "uWindBlendFactor");
    uniformWindTextureSize_ = glGetUniformLocation(shaderProgram_, "uWindTextureSize");
    uniformPlanetRadius_ = glGetUniformLocation(shaderProgram_, "uPlanetRadius");
    uniformFlatCircleMode_ = glGetUniformLocation(shaderProgram_, "uFlatCircleMode");
    uniformSphereCenter_ = glGetUniformLocation(shaderProgram_, "uSphereCenter");
    uniformSphereRadius_ = glGetUniformLocation(shaderProgram_, "uSphereRadius");
    uniformBillboardCenter_ = glGetUniformLocation(shaderProgram_, "uBillboardCenter");
    uniformDisplacementScale_ = glGetUniformLocation(shaderProgram_, "uDisplacementScale");
    uniformShowWireframe_ = glGetUniformLocation(shaderProgram_, "uShowWireframe");
    uniformMVP_ = glGetUniformLocation(shaderProgram_, "uMVP");

    shaderAvailable_ = true;
    return true;
}

bool EarthMaterial::initialize(const std::string &combinedBasePath, TextureResolution resolution)
{
    if (initialized_)
    {
        return true;
    }

    std::string combinedPath = combinedBasePath + "/" + getResolutionFolderName(resolution);
    bool lossless = (resolution == TextureResolution::Ultra);
    const char *ext = lossless ? ".png" : ".jpg";

    // Store base path for octree mesh generation
    textureBasePath_ = combinedPath;

    std::cout << "Loading Earth textures from: " << combinedPath << "\n";

    int loadedCount = 0;

    for (int month = 1; month <= 12; month++)
    {
        // Load Blue Marble equirectangular textures
        char bmName[64];
        snprintf(bmName, sizeof(bmName), "earth_month_%02d%s", month, ext);
        std::string filepath = combinedPath + "/" + bmName;

        if (!std::filesystem::exists(filepath))
        {
            std::cout << "  Month " << month << ": not found" << "\n";
            continue;
        }

        GLuint texId = loadTexture(filepath);
        if (texId != 0)
        {
            monthlyTextures_[month - 1] = texId;
            textureLoaded_[month - 1] = true;
            loadedCount++;
            std::cout << "  Month " << month << ": loaded" << "\n";
        }
        else
        {
            std::cout << "  Month " << month << ": failed to load" << "\n";
        }
    }

    std::cout << "Earth material: " << loadedCount << "/12 textures loaded" << "\n";

    // Load heightmap and normal map for bump mapping (equirectangular)
    std::string heightmapPath = combinedPath + "/earth_landmass_heightmap.png";
    std::string normalMapPath = combinedPath + "/earth_landmass_normal.png";

    if (std::filesystem::exists(heightmapPath))
    {
        heightmapTexture_ = loadTexture(heightmapPath);
        if (heightmapTexture_ != 0)
        {
            std::cout << "  Heightmap: loaded" << "\n";
        }
    }

    if (std::filesystem::exists(normalMapPath))
    {
        normalMapTexture_ = loadTexture(normalMapPath);
        if (normalMapTexture_ != 0)
        {
            elevationLoaded_ = true;
            std::cout << "  Normal map: loaded" << "\n";
        }
    }

    // Load nightlights texture (VIIRS Black Marble city lights)
    std::string nightlightsPath = combinedPath + "/earth_nightlights.png";
    if (std::filesystem::exists(nightlightsPath))
    {
        nightlightsTexture_ = loadTexture(nightlightsPath);
        if (nightlightsTexture_ != 0)
        {
            nightlightsLoaded_ = true;
            std::cout << "  Nightlights: loaded (city lights enabled)" << "\n";
        }
    }
    else
    {
        std::cout << "  Nightlights: not found (run preprocessNightlights first)" << "\n";
    }

    // Load wind textures (12 separate 2D textures, one per month)
    // Each JPG file: width x height, RGB format (R=u, G=v, B=0)
    int windTexturesLoadedCount = 0;
    for (int month = 1; month <= 12; month++)
    {
        char filename[64];
        snprintf(filename, sizeof(filename), "earth_wind_%02d.jpg", month);
        std::string windFile = combinedPath + "/" + filename;

        if (!std::filesystem::exists(windFile))
        {
            windTextures_[month - 1] = 0;
            windTexturesLoaded_[month - 1] = false;
            continue;
        }

        // Load JPG file using stb_image
        int jpgWidth, jpgHeight, jpgChannels;
        unsigned char *jpgData = stbi_load(windFile.c_str(), &jpgWidth, &jpgHeight, &jpgChannels, 3);

        if (!jpgData)
        {
            std::cerr << "  ERROR: Failed to load wind texture file: " << windFile << "\n";
            std::cerr << "  Error: " << stbi_failure_reason() << "\n";
            windTextures_[month - 1] = 0;
            windTexturesLoaded_[month - 1] = false;
            continue;
        }

        if (jpgChannels < 3)
        {
            std::cerr << "  ERROR: Wind texture has " << jpgChannels << " channels, expected 3 (RGB format)" << "\n";
            stbi_image_free(jpgData);
            windTextures_[month - 1] = 0;
            windTexturesLoaded_[month - 1] = false;
            continue;
        }

        // Create 2D texture
        GLuint textureId = 0;
        glGenTextures(1, &textureId);
        if (textureId == 0)
        {
            std::cerr << "  ERROR: Failed to generate texture for month " << month << "\n";
            stbi_image_free(jpgData);
            windTextures_[month - 1] = 0;
            windTexturesLoaded_[month - 1] = false;
            continue;
        }

        glBindTexture(GL_TEXTURE_2D, textureId);

        // Extract R and G channels from RGB JPG data to create LUMINANCE_ALPHA texture
        size_t pixelCount = static_cast<size_t>(jpgWidth) * jpgHeight;
        std::vector<unsigned char> twoChannelData(pixelCount * 2);
        for (size_t i = 0; i < pixelCount; i++)
        {
            twoChannelData[i * 2 + 0] = jpgData[i * 3 + 0]; // R -> LUMINANCE (u component)
            twoChannelData[i * 2 + 1] = jpgData[i * 3 + 1]; // G -> ALPHA (v component)
        }

        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_LUMINANCE_ALPHA,
                     jpgWidth,
                     jpgHeight,
                     0,
                     GL_LUMINANCE_ALPHA,
                     GL_UNSIGNED_BYTE,
                     twoChannelData.data());

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_2D, 0);

        stbi_image_free(jpgData);

        windTextures_[month - 1] = textureId;
        windTexturesLoaded_[month - 1] = true;
        windTexturesLoadedCount++;
    }

    if (windTexturesLoadedCount > 0)
    {
        std::cout << "  Wind textures: " << windTexturesLoadedCount << "/12 loaded" << "\n";
    }
    else
    {
        std::cout << "  Wind textures: not found (run preprocessWindData first)" << "\n";
    }

    // Load specular/roughness texture (surface reflectivity from MODIS green channel)
    std::string specularPath = combinedPath + "/earth_specular.png";
    if (std::filesystem::exists(specularPath))
    {
        specularTexture_ = loadTexture(specularPath);
        if (specularTexture_ != 0)
        {
            specularLoaded_ = true;
            std::cout << "  Specular: loaded (surface roughness enabled)" << "\n";
        }
    }
    else
    {
        std::cout << "  Specular: not found (run preprocessSpecular first)" << "\n";
    }

    // Load ice mask textures (12 monthly masks for seasonal ice coverage)
    int iceMasksLoadedCount = 0;
    for (int month = 1; month <= 12; month++)
    {
        char maskFilename[64];
        snprintf(maskFilename, sizeof(maskFilename), "earth_ice_mask_%02d.png", month);
        std::string iceMaskPath = combinedPath + "/" + maskFilename;

        if (std::filesystem::exists(iceMaskPath))
        {
            iceMaskTextures_[month - 1] = loadTexture(iceMaskPath);
            if (iceMaskTextures_[month - 1] != 0)
            {
                iceMasksLoaded_[month - 1] = true;
                iceMasksLoadedCount++;
            }
        }
        else
        {
            iceMaskTextures_[month - 1] = 0;
            iceMasksLoaded_[month - 1] = false;
        }
    }
    std::cout << "  Ice masks: " << iceMasksLoadedCount << "/12 loaded (seasonal ice enabled)" << "\n";

    // Load landmass mask texture (for ocean detection)
    std::string landmassMaskPath = combinedPath + "/earth_landmass_mask.png";
    if (std::filesystem::exists(landmassMaskPath))
    {
        landmassMaskTexture_ = loadTexture(landmassMaskPath);
        if (landmassMaskTexture_ != 0)
        {
            landmassMaskLoaded_ = true;
            std::cout << "  Landmass mask: loaded (ocean effects enabled)" << "\n";
        }
    }
    else
    {
        std::cout << "  Landmass mask: not found (ocean effects disabled)" << "\n";
    }

    // Load bathymetry textures (ocean floor depth and normal)
    std::string bathymetryDepthPath = combinedPath + "/earth_bathymetry_heightmap.png";
    std::string bathymetryNormalPath = combinedPath + "/earth_bathymetry_normal.png";

    if (std::filesystem::exists(bathymetryDepthPath) && std::filesystem::exists(bathymetryNormalPath))
    {
        bathymetryDepthTexture_ = loadTexture(bathymetryDepthPath);
        bathymetryNormalTexture_ = loadTexture(bathymetryNormalPath);

        if (bathymetryDepthTexture_ != 0 && bathymetryNormalTexture_ != 0)
        {
            bathymetryLoaded_ = true;
            std::cout << "  Bathymetry: loaded (ocean depth-based scattering enabled)" << "\n";
        }
    }
    else
    {
        std::cout << "  Bathymetry: not found (using fallback depth estimation)" << "\n";
    }

    // Load combined normal map (landmass + bathymetry) for shadows
    std::string combinedNormalPath = combinedPath + "/earth_combined_normal.png";
    if (std::filesystem::exists(combinedNormalPath))
    {
        combinedNormalTexture_ = loadTexture(combinedNormalPath);
        if (combinedNormalTexture_ != 0)
        {
            combinedNormalLoaded_ = true;
            std::cout << "  Combined normal map: loaded (for ocean floor shadows)" << "\n";
        }
    }
    else
    {
        std::cout << "  Combined normal map: not found (shadows will use fallback)" << "\n";
    }

    // Initialize shaders for per-pixel normal mapping
    // MANDATORY: Shader initialization (will abort on failure)
    // This initializes both surface shader and atmosphere shader
    initializeShaders();
    std::cout << "  Shader: initialized (per-pixel normal mapping enabled)" << "\n";

    // Generate noise textures for city light flickering (requires GL context)
    generateNoiseTextures();

    // Generate octree mesh from heightmap (requires heightmap to be loaded)
    // This will be called later when we have display radius, but we can prepare here
    // For now, we'll generate it on first draw call with actual radius

    // MANDATORY: Check required textures are loaded
    if (!elevationLoaded_ || normalMapTexture_ == 0)
    {
        std::cerr << "ERROR: EarthMaterial::initialize() - Normal map is required!" << "\n";
        std::cerr << "  elevationLoaded_ = " << elevationLoaded_ << "\n";
        std::cerr << "  normalMapTexture_ = " << normalMapTexture_ << "\n";
        std::exit(1);
    }

    if (loadedCount == 0)
    {
        std::cerr << "ERROR: EarthMaterial::initialize() - No monthly color "
                     "textures loaded!"
                  << "\n";
        std::cerr << "  Expected 12 monthly textures, found 0" << "\n";
        std::exit(1);
    }

    initialized_ = true;
    return true;
}