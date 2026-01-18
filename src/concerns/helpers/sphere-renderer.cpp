#include "sphere-renderer.h"
#include "vulkan.h"
#include "../../materials/earth/earth-material.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>
#include <iostream>

// Sphere geometry buffers (shared across all sphere draws)
static VulkanBuffer g_sphereVertexBuffer = {};
static VulkanBuffer g_sphereIndexBuffer = {};
static uint32_t g_sphereIndexCount = 0;
static bool g_sphereRendererInitialized = false;

// Simple shader sources for sphere rendering
static const char* SPHERE_VERTEX_SHADER = R"(
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 color;
} ubo;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    fragColor = ubo.color;
    fragNormal = mat3(transpose(inverse(ubo.model))) * inNormal;
}
)";

static const char* SPHERE_FRAGMENT_SHADER = R"(
#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    // Simple flat shading with slight directional lighting
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float ndotl = max(dot(normalize(fragNormal), lightDir), 0.3);
    outColor = vec4(fragColor * ndotl, 1.0);
}
)";

// Generate sphere geometry
static void generateSphereGeometry(int slices, int stacks, 
                                   std::vector<float>& vertices, 
                                   std::vector<uint32_t>& indices)
{
    vertices.clear();
    indices.clear();
    
    const float PI = 3.14159265359f;
    
    // Generate vertices
    for (int i = 0; i <= stacks; ++i)
    {
        float phi = PI * (-0.5f + static_cast<float>(i) / stacks);
        float y = sin(phi);
        float r = cos(phi);
        
        for (int j = 0; j <= slices; ++j)
        {
            float theta = 2.0f * PI * static_cast<float>(j) / slices;
            float x = r * cos(theta);
            float z = r * sin(theta);
            
            // Position (normalized, will be scaled by radius)
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            
            // Normal (same as position for unit sphere)
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
        }
    }
    
    // Generate indices
    for (int i = 0; i < stacks; ++i)
    {
        int k1 = i * (slices + 1);
        int k2 = k1 + slices + 1;
        
        for (int j = 0; j < slices; ++j, ++k1, ++k2)
        {
            if (i != 0)
            {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }
            
            if (i != (stacks - 1))
            {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }
}

// Uniform buffer for sphere rendering
struct SphereUniformBuffer {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 color;
    float padding; // For alignment
};

static VulkanPipeline g_spherePipeline = {};
static VulkanBuffer g_sphereUniformBuffer = {};
static VkDescriptorSet g_sphereDescriptorSet = VK_NULL_HANDLE;
static VkDescriptorSetLayout g_sphereDescriptorSetLayout = VK_NULL_HANDLE;
static VkDescriptorPool g_sphereDescriptorPool = VK_NULL_HANDLE;

bool InitSphereRenderer(VulkanContext& context)
{
    if (g_sphereRendererInitialized)
        return true;
    
    // Generate default sphere geometry (16 slices, 8 stacks - matches typical usage)
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    generateSphereGeometry(16, 8, vertices, indices);
    g_sphereIndexCount = static_cast<uint32_t>(indices.size());
    
    // Create vertex buffer
    g_sphereVertexBuffer = createBuffer(context,
                                       vertices.size() * sizeof(float),
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                       vertices.data());
    
    // Create index buffer
    g_sphereIndexBuffer = createBuffer(context,
                                      indices.size() * sizeof(uint32_t),
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      indices.data());
    
    // Create uniform buffer
    g_sphereUniformBuffer = createBuffer(context,
                                        sizeof(SphereUniformBuffer),
                                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                        nullptr);
    
    // Create descriptor set layout
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    
    if (vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &g_sphereDescriptorSetLayout) != VK_SUCCESS)
    {
        std::cerr << "Failed to create sphere descriptor set layout!" << std::endl;
        return false;
    }
    
    // Create descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    
    if (vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &g_sphereDescriptorPool) != VK_SUCCESS)
    {
        std::cerr << "Failed to create sphere descriptor pool!" << std::endl;
        return false;
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = g_sphereDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &g_sphereDescriptorSetLayout;
    
    if (vkAllocateDescriptorSets(context.device, &allocInfo, &g_sphereDescriptorSet) != VK_SUCCESS)
    {
        std::cerr << "Failed to allocate sphere descriptor set!" << std::endl;
        return false;
    }
    
    // Update descriptor set
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = g_sphereUniformBuffer.buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(SphereUniformBuffer);
    
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = g_sphereDescriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;
    
    vkUpdateDescriptorSets(context.device, 1, &descriptorWrite, 0, nullptr);
    
    // Create shaders
    VulkanShader vertexShader = createShaderModule(context, SPHERE_VERTEX_SHADER, VK_SHADER_STAGE_VERTEX_BIT);
    VulkanShader fragmentShader = createShaderModule(context, SPHERE_FRAGMENT_SHADER, VK_SHADER_STAGE_FRAGMENT_BIT);
    
    if (vertexShader.module == VK_NULL_HANDLE || fragmentShader.module == VK_NULL_HANDLE)
    {
        std::cerr << "Failed to create sphere shaders!" << std::endl;
        return false;
    }
    
    // Vertex input description
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = 6 * sizeof(float); // 3 position + 3 normal
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions(2);
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = 0;
    
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = 3 * sizeof(float);
    
    // Create graphics pipeline
    PipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertexShader = vertexShader.module;
    pipelineInfo.fragmentShader = fragmentShader.module;
    pipelineInfo.vertexBindings.push_back(bindingDescription);
    pipelineInfo.vertexAttributes = attributeDescriptions;
    pipelineInfo.descriptorSetLayouts.push_back(g_sphereDescriptorSetLayout);
    pipelineInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInfo.depthTest = true;
    pipelineInfo.depthWrite = true;
    pipelineInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    
    g_spherePipeline.pipeline = createGraphicsPipeline(context, pipelineInfo, g_spherePipeline.layout);
    
    destroyShaderModule(context, vertexShader);
    destroyShaderModule(context, fragmentShader);
    
    if (g_spherePipeline.pipeline == VK_NULL_HANDLE)
    {
        std::cerr << "Failed to create sphere pipeline!" << std::endl;
        return false;
    }
    
    g_sphereRendererInitialized = true;
    return true;
}

void CleanupSphereRenderer(VulkanContext& context)
{
    if (!g_sphereRendererInitialized)
        return;
    
    if (g_spherePipeline.pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(context.device, g_spherePipeline.pipeline, nullptr);
        g_spherePipeline.pipeline = VK_NULL_HANDLE;
    }
    
    if (g_spherePipeline.layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(context.device, g_spherePipeline.layout, nullptr);
        g_spherePipeline.layout = VK_NULL_HANDLE;
    }
    
    if (g_sphereDescriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(context.device, g_sphereDescriptorPool, nullptr);
        g_sphereDescriptorPool = VK_NULL_HANDLE;
    }
    
    if (g_sphereDescriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(context.device, g_sphereDescriptorSetLayout, nullptr);
        g_sphereDescriptorSetLayout = VK_NULL_HANDLE;
    }
    
    destroyBuffer(context, g_sphereUniformBuffer);
    destroyBuffer(context, g_sphereIndexBuffer);
    destroyBuffer(context, g_sphereVertexBuffer);
    
    g_sphereRendererInitialized = false;
}

void DrawSphereVulkan(VkCommandBuffer cmd,
                      VulkanContext& context,
                      const glm::vec3& center,
                      float radius,
                      const glm::vec3& color,
                      int slices,
                      int stacks)
{
    if (!g_sphereRendererInitialized)
    {
        if (!InitSphereRenderer(context))
        {
            std::cerr << "Failed to initialize sphere renderer!" << std::endl;
            return;
        }
    }
    
    // Create model matrix (translate and scale)
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, center);
    model = glm::scale(model, glm::vec3(radius));
    
    // Get view and projection matrices from EarthMaterial
    glm::mat4 viewMatrix = EarthMaterial::getViewMatrix();
    glm::mat4 projectionMatrix = EarthMaterial::getProjectionMatrix();
    
    // Update uniform buffer
    SphereUniformBuffer ubo{};
    ubo.model = model;
    ubo.view = viewMatrix;
    ubo.proj = projectionMatrix;
    ubo.color = color;
    
    updateUniformBuffer(context, g_sphereUniformBuffer, &ubo, sizeof(ubo));
    
    // Bind pipeline and descriptor sets
    bindPipelineAndDescriptors(cmd, g_spherePipeline.pipeline, g_spherePipeline.layout, {g_sphereDescriptorSet});
    
    // Bind vertex and index buffers
    VkBuffer vertexBuffers[] = {g_sphereVertexBuffer.buffer};
    VkDeviceSize offsets[] = {0};
    recordBindVertexBuffers(cmd, 0, {vertexBuffers[0]}, {offsets[0]});
    recordBindIndexBuffer(cmd, g_sphereIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    
    // Draw
    recordDrawIndexed(cmd, g_sphereIndexCount);
}

// Compatibility wrapper for old DrawSphere calls
void DrawSphere(const glm::vec3& center, float radius, const glm::vec3& color, int slices, int stacks)
{
    extern VulkanContext* g_vulkanContext;
    if (!g_vulkanContext)
        return;
    
    // Get current command buffer from context
    VkCommandBuffer cmd = g_vulkanContext->currentCommandBuffer;
    if (cmd == VK_NULL_HANDLE)
        return;
    
    DrawSphereVulkan(cmd, *g_vulkanContext, center, radius, color, slices, stacks);
}
