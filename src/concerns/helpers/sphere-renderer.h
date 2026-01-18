#pragma once

#include "vulkan.h"
#include <glm/glm.hpp>

// Draw a simple colored sphere using Vulkan
// This replaces the old OpenGL DrawSphere function
// center: center position of the sphere in world space
// radius: radius of the sphere
// color: RGB color (0.0-1.0 range)
// slices: number of longitude divisions
// stacks: number of latitude divisions
void DrawSphereVulkan(VkCommandBuffer cmd,
                      VulkanContext& context,
                      const glm::vec3& center,
                      float radius,
                      const glm::vec3& color,
                      int slices,
                      int stacks);

// Compatibility wrapper for old DrawSphere calls (uses global Vulkan context)
void DrawSphere(const glm::vec3& center, float radius, const glm::vec3& color, int slices, int stacks);

// Initialize sphere renderer (creates shared geometry buffers)
bool InitSphereRenderer(VulkanContext& context);

// Cleanup sphere renderer
void CleanupSphereRenderer(VulkanContext& context);
