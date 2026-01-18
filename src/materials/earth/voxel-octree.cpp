// ============================================================================
// Voxel Octree Implementation
// ============================================================================

#include "voxel-octree.h"
#include "../../concerns/constants.h"
#include "helpers/coordinate-conversion.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>


namespace EarthVoxelOctree
{

PlanetOctree::PlanetOctree(float baseRadius, float maxRadius, int maxDepth)
    : baseRadius_(baseRadius), maxRadius_(maxRadius), maxDepth_(maxDepth), heightmapData_(nullptr), heightmapWidth_(0),
      heightmapHeight_(0), landmassMask_(nullptr), averageRadius_(baseRadius)
{
    // Create root node centered at origin with size = maxRadius (half-extent)
    // This creates a cubic bounding box from -maxRadius to +maxRadius in each axis
    // The spherical bounding volume (radius = maxRadius) fits inside this cube
    // Base size is 4, max depth is 4, giving us 4^4 = 256 leaf nodes maximum per branch
    root_ = std::make_unique<OctreeNode>(glm::vec3(0.0f), maxRadius, 0);
}

PlanetOctree::~PlanetOctree() = default;

void PlanetOctree::buildFromHeightmap(const unsigned char *heightmapData,
                                      int heightmapWidth,
                                      int heightmapHeight,
                                      const unsigned char *landmassMask,
                                      float averageRadius)
{
    heightmapData_ = heightmapData;
    heightmapWidth_ = heightmapWidth;
    heightmapHeight_ = heightmapHeight;
    landmassMask_ = landmassMask;
    averageRadius_ = averageRadius;

    // Build octree recursively with parallelization
    // The root level and first few levels will be built in parallel for speed
    std::cout << "  Building octree in parallel (max depth: " << maxDepth_ << ")..." << "\n";
    buildOctreeRecursive(root_.get());
    std::cout << "  Octree build complete." << "\n";
}

float PlanetOctree::sampleHeightmap(const glm::vec3 &worldPos) const
{
    if (!heightmapData_ || heightmapWidth_ == 0 || heightmapHeight_ == 0)
    {
        return 0.0f; // No heightmap data
    }

    // Convert world position to direction from center
    float dist = glm::length(worldPos);
    if (dist < 0.001f)
    {
        return 0.0f;
    }
    glm::vec3 dir = worldPos / dist;

    // Convert direction to equirectangular UV
    // For now, use simple spherical coordinates
    // TODO: Use proper coordinate system with pole and prime meridian
    float latitude = std::asin(glm::clamp(dir.y, -1.0f, 1.0f));
    float longitude = std::atan2(dir.z, dir.x);

    // Convert to UV coordinates (equirectangular)
    const float PI_F = 3.14159265359f;
    float u = (longitude / PI_F + 1.0f) * 0.5f;
    float v = 0.5f - (latitude / PI_F);

    // Clamp to valid range
    u = glm::clamp(u, 0.0f, 1.0f);
    v = glm::clamp(v, 0.0f, 1.0f);

    // Convert to sinusoidal UV (matching texture format)
    // TODO: Use proper coordinate conversion helper
    float lon = (u - 0.5f) * 2.0f * PI_F;
    float lat = (0.5f - v) * PI_F;
    float cosLat = std::cos(lat);
    float absCosLat = std::abs(cosLat);

    float u_sinu;
    if (absCosLat < 0.01f)
    {
        u_sinu = 0.5f;
    }
    else
    {
        float x_sinu = lon * cosLat;
        u_sinu = x_sinu / (2.0f * PI_F) + 0.5f;
        float uMin = 0.5f - 0.5f * absCosLat;
        float uMax = 0.5f + 0.5f * absCosLat;
        u_sinu = glm::clamp(u_sinu, uMin, uMax);
    }

    float v_sinu = 0.5f + lat / PI_F;
    v_sinu = glm::clamp(v_sinu, 0.0f, 1.0f);

    // Sample heightmap using bilinear interpolation
    float x = u_sinu * (heightmapWidth_ - 1);
    float y = v_sinu * (heightmapHeight_ - 1);

    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int x1 = std::min(x0 + 1, heightmapWidth_ - 1);
    int y1 = std::min(y0 + 1, heightmapHeight_ - 1);

    float fx = x - x0;
    float fy = y - y0;

    // Bilinear interpolation
    float h00 = static_cast<float>(heightmapData_[y0 * heightmapWidth_ + x0]);
    float h10 = static_cast<float>(heightmapData_[y0 * heightmapWidth_ + x1]);
    float h01 = static_cast<float>(heightmapData_[y1 * heightmapWidth_ + x0]);
    float h11 = static_cast<float>(heightmapData_[y1 * heightmapWidth_ + x1]);

    float heightValue =
        h00 * (1.0f - fx) * (1.0f - fy) + h10 * fx * (1.0f - fy) + h01 * (1.0f - fx) * fy + h11 * fx * fy;

    // Convert heightmap value [0,255] to elevation in meters
    // Heightmap encoding: 128 (0.5) = sea level (0m), 255 (1.0) = Mt. Everest (~8848m)
    float normalizedHeight = heightValue / 255.0f;
    float elevationMeters = 0.0f;
    if (normalizedHeight >= 0.5f)
    {
        // Above sea level: map 0.5 -> 0m, 1.0 -> 8848m
        elevationMeters = (normalizedHeight - 0.5f) / 0.5f * 8848.0f;
    }
    else
    {
        // Below sea level: map 0.0 -> -11000m (deepest trench), 0.5 -> 0m
        elevationMeters = (normalizedHeight - 0.5f) / 0.5f * 11000.0f;
    }

    return elevationMeters;
}

float PlanetOctree::getSurfaceRadius(const glm::vec3 &worldPos) const
{
    float heightOffsetMeters = sampleHeightmap(worldPos);

    // Convert elevation from meters to display units
    // Earth radius in real units: RADIUS_EARTH_KM * 1000 meters
    // Display radius: averageRadius_ (display units)
    // Conversion: 1 meter = averageRadius_ / (RADIUS_EARTH_KM * 1000) display units
    const float RADIUS_EARTH_M = 6371000.0f; // Earth radius in meters
    float heightOffsetDisplay = heightOffsetMeters * (averageRadius_ / RADIUS_EARTH_M);

    return averageRadius_ + heightOffsetDisplay;
}

bool PlanetOctree::isVoxelSolid(const glm::vec3 &voxelCenter, float voxelSize) const
{
    // Sample heightmap at voxel center
    float distFromCenter = glm::length(voxelCenter);
    float surfaceRadius = getSurfaceRadius(voxelCenter);

    // Voxel is solid if its center is below the surface
    return distFromCenter < surfaceRadius;
}

bool PlanetOctree::nodeIntersectsSphere(const glm::vec3 &nodeCenter, float nodeSize) const
{
    // Check if node's bounding box intersects the spherical bounding volume
    // For a cube centered at nodeCenter with half-extent nodeSize, check if it intersects sphere of radius maxRadius_

    // Find closest point on cube to origin
    glm::vec3 closestPoint;
    closestPoint.x = glm::clamp(0.0f, nodeCenter.x - nodeSize, nodeCenter.x + nodeSize);
    closestPoint.y = glm::clamp(0.0f, nodeCenter.y - nodeSize, nodeCenter.y + nodeSize);
    closestPoint.z = glm::clamp(0.0f, nodeCenter.z - nodeSize, nodeCenter.z + nodeSize);

    float distToClosest = glm::length(closestPoint);

    // Find farthest point on cube from origin
    glm::vec3 farthestPoint;
    farthestPoint.x = (std::abs(nodeCenter.x + nodeSize) > std::abs(nodeCenter.x - nodeSize)) ? nodeCenter.x + nodeSize
                                                                                              : nodeCenter.x - nodeSize;
    farthestPoint.y = (std::abs(nodeCenter.y + nodeSize) > std::abs(nodeCenter.y - nodeSize)) ? nodeCenter.y + nodeSize
                                                                                              : nodeCenter.y - nodeSize;
    farthestPoint.z = (std::abs(nodeCenter.z + nodeSize) > std::abs(nodeCenter.z - nodeSize)) ? nodeCenter.z + nodeSize
                                                                                              : nodeCenter.z - nodeSize;

    float distToFarthest = glm::length(farthestPoint);

    // Node intersects sphere if closest point is inside or farthest point is outside
    // Also check if node intersects the surface region (between min and max surface radius)
    float minSurfaceRadius = averageRadius_ - 11000.0f; // Deepest trench
    float maxSurfaceRadius = averageRadius_ + 8848.0f;  // Mt. Everest

    return (distToClosest <= maxRadius_) && (distToFarthest >= minSurfaceRadius);
}

void PlanetOctree::buildOctreeRecursive(OctreeNode *node)
{
    // Check if node intersects the spherical bounding volume
    if (!nodeIntersectsSphere(node->center, node->size))
    {
        // Node is completely outside spherical bounds - mark as empty and don't subdivide
        node->isSolid = false;
        return;
    }

    // Check if node is near the surface (needs subdivision)
    // Sample surface radius at node corners to determine if subdivision is needed
    float distFromCenter = glm::length(node->center);
    float surfaceRadius = getSurfaceRadius(node->center);

    // Calculate node's actual distance range from origin
    // Find closest and farthest points on the cube from origin
    glm::vec3 closestPoint;
    closestPoint.x = glm::clamp(0.0f, node->center.x - node->size, node->center.x + node->size);
    closestPoint.y = glm::clamp(0.0f, node->center.y - node->size, node->center.y + node->size);
    closestPoint.z = glm::clamp(0.0f, node->center.z - node->size, node->center.z + node->size);
    float nodeMinDist = glm::length(closestPoint);

    glm::vec3 farthestPoint;
    farthestPoint.x = (std::abs(node->center.x + node->size) > std::abs(node->center.x - node->size))
                          ? node->center.x + node->size
                          : node->center.x - node->size;
    farthestPoint.y = (std::abs(node->center.y + node->size) > std::abs(node->center.y - node->size))
                          ? node->center.y + node->size
                          : node->center.y - node->size;
    farthestPoint.z = (std::abs(node->center.z + node->size) > std::abs(node->center.z - node->size))
                          ? node->center.z + node->size
                          : node->center.z - node->size;
    float nodeMaxDist = glm::length(farthestPoint);

    // Margin for height variations (20km)
    const float HEIGHT_MARGIN = 20000.0f;

    // If node is completely outside surface region, mark as empty leaf
    if (nodeMinDist > surfaceRadius + HEIGHT_MARGIN)
    {
        node->isSolid = false;
        node->isLeaf = true; // Explicitly mark as leaf
        return;
    }

    // If node is completely inside planet (well below surface), mark as solid leaf
    if (nodeMaxDist < surfaceRadius - HEIGHT_MARGIN)
    {
        node->isSolid = true;
        node->isLeaf = true; // Explicitly mark as leaf
        return;
    }

    // Check if we've reached max depth - if so, make this a leaf and store voxel bits
    if (node->depth >= maxDepth_)
    {
        // Max depth reached - store voxel bits for this node
        // For now, we'll store a simple 2x2x2 grid (8 voxels = 1 byte)
        // This can be expanded to higher resolution grids later
        storeVoxelBits(node);
        node->isLeaf = true; // Explicitly mark as leaf
        return;
    }

    // Node intersects surface region - subdivide for better detail
    node->isLeaf = false;
    float childSize = node->size * 0.5f;
    int childDepth = node->depth + 1;

    // Parallelize child node construction for better performance
    // Only parallelize up to a certain depth to avoid thread explosion
    // After depth 3, use sequential building to reduce overhead
    const int MAX_PARALLEL_DEPTH = 3;
    bool useParallel = (node->depth < MAX_PARALLEL_DEPTH);

    if (useParallel)
    {
        // Build children in parallel using threads
        std::vector<std::thread> threads;
        std::vector<int> childIndices;

        // First pass: create child nodes and collect indices for parallel building
        for (int i = 0; i < 8; i++)
        {
            glm::vec3 offset;
            offset.x = (i & 1) ? childSize : -childSize;
            offset.y = (i & 2) ? childSize : -childSize;
            offset.z = (i & 4) ? childSize : -childSize;

            glm::vec3 childCenter = node->center + offset;

            // Only create child if it intersects the sphere
            if (nodeIntersectsSphere(childCenter, childSize))
            {
                node->children[i] = std::make_unique<OctreeNode>(childCenter, childSize, childDepth);
                childIndices.push_back(i);
            }
        }

        // Second pass: build children in parallel
        for (int childIdx : childIndices)
        {
            threads.emplace_back([this, childIdx, node]() {
                buildOctreeRecursive(node->children[childIdx].get());
            });
        }

        // Wait for all threads to complete
        for (auto &thread : threads)
        {
            thread.join();
        }
    }
    else
    {
        // Sequential building for deeper nodes (reduces thread overhead)
        for (int i = 0; i < 8; i++)
        {
            glm::vec3 offset;
            offset.x = (i & 1) ? childSize : -childSize;
            offset.y = (i & 2) ? childSize : -childSize;
            offset.z = (i & 4) ? childSize : -childSize;

            glm::vec3 childCenter = node->center + offset;

            // Only create child if it intersects the sphere
            if (nodeIntersectsSphere(childCenter, childSize))
            {
                node->children[i] = std::make_unique<OctreeNode>(childCenter, childSize, childDepth);
                buildOctreeRecursive(node->children[i].get());
            }
        }
    }
}

void PlanetOctree::subdivideForProximity(const glm::vec3 &referencePoint, float maxSubdivisionDistance, int maxNodesToProcess)
{
    if (!root_)
    {
        return;
    }

    // Recursively subdivide nodes near the reference point
    static int callCount = 0;
    callCount++;
    if (callCount % 60 == 0) // Print every 60 frames to avoid spam
    {
        std::cout << "DEBUG: subdivideForProximity called - referencePoint=(" << referencePoint.x << ","
                  << referencePoint.y << "," << referencePoint.z << "), maxDistance=" << maxSubdivisionDistance << "\n";
    }

    // Collect nodes that need subdivision first (for parallel processing)
    std::vector<OctreeNode*> nodesToSubdivide;
    collectNodesForSubdivision(root_.get(), referencePoint, maxSubdivisionDistance, nodesToSubdivide, maxNodesToProcess);

    // Parallelize subdivision of collected nodes
    if (!nodesToSubdivide.empty())
    {
        const unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
        const size_t nodesPerThread = std::max(size_t(1), nodesToSubdivide.size() / numThreads);
        
        std::vector<std::thread> threads;
        std::atomic<size_t> processedCount(0);

        for (unsigned int t = 0; t < numThreads && t * nodesPerThread < nodesToSubdivide.size(); ++t)
        {
            threads.emplace_back([&, t]() {
                size_t startIdx = t * nodesPerThread;
                size_t endIdx = std::min(startIdx + nodesPerThread, nodesToSubdivide.size());
                
                for (size_t i = startIdx; i < endIdx; ++i)
                {
                    OctreeNode* node = nodesToSubdivide[i];
                    if (node)
                    {
                        subdivideNode(node, referencePoint, maxSubdivisionDistance);
                        processedCount++;
                    }
                }
            });
        }

        // Wait for all threads
        for (auto& thread : threads)
        {
            thread.join();
        }

        // Now recursively process children of subdivided nodes
        for (OctreeNode* node : nodesToSubdivide)
        {
            if (node && !node->isLeaf)
            {
                for (int i = 0; i < 8; i++)
                {
                    if (node->children[i])
                    {
                        subdivideForProximityRecursive(node->children[i].get(), referencePoint, maxSubdivisionDistance);
                    }
                }
            }
        }
    }
    else
    {
        // Fallback: use recursive approach if no nodes collected
        subdivideForProximityRecursive(root_.get(), referencePoint, maxSubdivisionDistance);
    }
}

void PlanetOctree::subdivideForProximityRecursive(OctreeNode *node,
                                                  const glm::vec3 &referencePoint,
                                                  float maxSubdivisionDistance)
{
    if (!node)
    {
        return;
    }

    // Calculate distance from reference point to node center
    float distToNode = glm::length(node->center - referencePoint);

    // Calculate the closest point on the node's bounding box to the reference point
    glm::vec3 closestPointOnNode;
    closestPointOnNode.x = glm::clamp(referencePoint.x, node->center.x - node->size, node->center.x + node->size);
    closestPointOnNode.y = glm::clamp(referencePoint.y, node->center.y - node->size, node->center.y + node->size);
    closestPointOnNode.z = glm::clamp(referencePoint.z, node->center.z - node->size, node->center.z + node->size);
    float minDistToNode = glm::length(closestPointOnNode - referencePoint);

    // If node is too far away, don't subdivide (and don't recurse into children)
    if (minDistToNode > maxSubdivisionDistance)
    {
        return;
    }

    // If node is a leaf and close enough, subdivide it (if not at max depth)
    if (node->isLeaf)
    {
        // Check if we can subdivide further
        if (node->depth >= maxDepth_)
        {
            // Already at max depth, can't subdivide more
            return;
        }

        // Check if node intersects the surface (only subdivide surface nodes)
        if (!nodeIntersectsSphere(node->center, node->size))
        {
            // Node is outside sphere, don't subdivide
            return;
        }

        // Check if node is near surface (similar to buildOctreeRecursive logic)
        float distFromCenter = glm::length(node->center);
        float surfaceRadius = getSurfaceRadius(node->center);

        // Calculate node's distance range from origin
        glm::vec3 closestPoint;
        closestPoint.x = glm::clamp(0.0f, node->center.x - node->size, node->center.x + node->size);
        closestPoint.y = glm::clamp(0.0f, node->center.y - node->size, node->center.y + node->size);
        closestPoint.z = glm::clamp(0.0f, node->center.z - node->size, node->center.z + node->size);
        float nodeMinDist = glm::length(closestPoint);

        glm::vec3 farthestPoint;
        farthestPoint.x = (std::abs(node->center.x + node->size) > std::abs(node->center.x - node->size))
                              ? node->center.x + node->size
                              : node->center.x - node->size;
        farthestPoint.y = (std::abs(node->center.y + node->size) > std::abs(node->center.y - node->size))
                              ? node->center.y + node->size
                              : node->center.y - node->size;
        farthestPoint.z = (std::abs(node->center.z + node->size) > std::abs(node->center.z - node->size))
                              ? node->center.z + node->size
                              : node->center.z - node->size;
        float nodeMaxDist = glm::length(farthestPoint);

        const float HEIGHT_MARGIN = 20000.0f;

        // Only subdivide if node intersects surface region
        if (nodeMinDist > surfaceRadius + HEIGHT_MARGIN || nodeMaxDist < surfaceRadius - HEIGHT_MARGIN)
        {
            // Node is completely outside or inside surface region, don't subdivide
            return;
        }

        // Subdivide this leaf node
        static int subdivisionCount = 0;
        subdivisionCount++;
        if (subdivisionCount % 100 == 0) // Print every 100 subdivisions to avoid spam
        {
            std::cout << "DEBUG: Subdividing node at depth " << node->depth << ", center=(" << node->center.x << ","
                      << node->center.y << "," << node->center.z << "), distance=" << minDistToNode << "\n";
        }

        node->isLeaf = false;
        float childSize = node->size * 0.5f;
        int childDepth = node->depth + 1;

        // Create 8 child nodes
        for (int i = 0; i < 8; i++)
        {
            glm::vec3 offset;
            offset.x = (i & 1) ? childSize : -childSize;
            offset.y = (i & 2) ? childSize : -childSize;
            offset.z = (i & 4) ? childSize : -childSize;

            glm::vec3 childCenter = node->center + offset;

            // Only create child if it intersects the sphere
            if (nodeIntersectsSphere(childCenter, childSize))
            {
                node->children[i] = std::make_unique<OctreeNode>(childCenter, childSize, childDepth);

                // Determine child's solidity (similar to buildOctreeRecursive)
                float childDistFromCenter = glm::length(childCenter);
                float childSurfaceRadius = getSurfaceRadius(childCenter);

                // Calculate child's distance range
                glm::vec3 childClosestPoint;
                childClosestPoint.x = glm::clamp(0.0f, childCenter.x - childSize, childCenter.x + childSize);
                childClosestPoint.y = glm::clamp(0.0f, childCenter.y - childSize, childCenter.y + childSize);
                childClosestPoint.z = glm::clamp(0.0f, childCenter.z - childSize, childCenter.z + childSize);
                float childMinDist = glm::length(childClosestPoint);

                glm::vec3 childFarthestPoint;
                childFarthestPoint.x = (std::abs(childCenter.x + childSize) > std::abs(childCenter.x - childSize))
                                           ? childCenter.x + childSize
                                           : childCenter.x - childSize;
                childFarthestPoint.y = (std::abs(childCenter.y + childSize) > std::abs(childCenter.y - childSize))
                                           ? childCenter.y + childSize
                                           : childCenter.y - childSize;
                childFarthestPoint.z = (std::abs(childCenter.z + childSize) > std::abs(childCenter.z - childSize))
                                           ? childCenter.z + childSize
                                           : childCenter.z - childSize;
                float childMaxDist = glm::length(childFarthestPoint);

                // Mark child as leaf initially (will be subdivided further if needed)
                node->children[i]->isLeaf = true;

                if (childMinDist > childSurfaceRadius + HEIGHT_MARGIN)
                {
                    node->children[i]->isSolid = false;
                }
                else if (childMaxDist < childSurfaceRadius - HEIGHT_MARGIN)
                {
                    node->children[i]->isSolid = true;
                }
                else
                {
                    // Child intersects surface - will be processed recursively
                    node->children[i]->isSolid = isVoxelSolid(childCenter, childSize);
                }
            }
        }
    }

    // Recursively process children (whether they existed before or were just created)
    for (int i = 0; i < 8; i++)
    {
        if (node->children[i])
        {
            subdivideForProximityRecursive(node->children[i].get(), referencePoint, maxSubdivisionDistance);
        }
    }
}

void PlanetOctree::collectNodesForSubdivision(OctreeNode *node,
                                              const glm::vec3 &referencePoint,
                                              float maxSubdivisionDistance,
                                              std::vector<OctreeNode*> &nodesToSubdivide,
                                              int maxNodesToProcess)
{
    if (!node || (maxNodesToProcess >= 0 && static_cast<int>(nodesToSubdivide.size()) >= maxNodesToProcess))
    {
        return;
    }

    // Calculate the closest point on the node's bounding box to the reference point
    glm::vec3 closestPointOnNode;
    closestPointOnNode.x = glm::clamp(referencePoint.x, node->center.x - node->size, node->center.x + node->size);
    closestPointOnNode.y = glm::clamp(referencePoint.y, node->center.y - node->size, node->center.y + node->size);
    closestPointOnNode.z = glm::clamp(referencePoint.z, node->center.z - node->size, node->center.z + node->size);
    float minDistToNode = glm::length(closestPointOnNode - referencePoint);

    // If node is too far away, don't collect (and don't recurse into children)
    if (minDistToNode > maxSubdivisionDistance)
    {
        return;
    }

    // If node is a leaf and close enough, collect it for subdivision
    if (node->isLeaf)
    {
        // Check if we can subdivide further
        if (node->depth >= maxDepth_)
        {
            // Already at max depth, can't subdivide more
            return;
        }

        // Check if node intersects the surface (only subdivide surface nodes)
        if (!nodeIntersectsSphere(node->center, node->size))
        {
            // Node is outside sphere, don't subdivide
            return;
        }

        // Check if node is near surface
        float distFromCenter = glm::length(node->center);
        float surfaceRadius = getSurfaceRadius(node->center);

        glm::vec3 closestPoint;
        closestPoint.x = glm::clamp(0.0f, node->center.x - node->size, node->center.x + node->size);
        closestPoint.y = glm::clamp(0.0f, node->center.y - node->size, node->center.y + node->size);
        closestPoint.z = glm::clamp(0.0f, node->center.z - node->size, node->center.z + node->size);
        float nodeMinDist = glm::length(closestPoint);

        glm::vec3 farthestPoint;
        farthestPoint.x = (std::abs(node->center.x + node->size) > std::abs(node->center.x - node->size))
                              ? node->center.x + node->size
                              : node->center.x - node->size;
        farthestPoint.y = (std::abs(node->center.y + node->size) > std::abs(node->center.y - node->size))
                              ? node->center.y + node->size
                              : node->center.y - node->size;
        farthestPoint.z = (std::abs(node->center.z + node->size) > std::abs(node->center.z - node->size))
                              ? node->center.z + node->size
                              : node->center.z - node->size;
        float nodeMaxDist = glm::length(farthestPoint);

        const float HEIGHT_MARGIN = 20000.0f;

        // Only collect if node intersects surface region
        if (nodeMinDist <= surfaceRadius + HEIGHT_MARGIN && nodeMaxDist >= surfaceRadius - HEIGHT_MARGIN)
        {
            nodesToSubdivide.push_back(node);
        }
    }
    else
    {
        // Recurse into children
        for (int i = 0; i < 8; i++)
        {
            if (node->children[i] && (maxNodesToProcess < 0 || static_cast<int>(nodesToSubdivide.size()) < maxNodesToProcess))
            {
                collectNodesForSubdivision(node->children[i].get(), referencePoint, maxSubdivisionDistance, nodesToSubdivide, maxNodesToProcess);
            }
        }
    }
}

void PlanetOctree::subdivideNode(OctreeNode *node, const glm::vec3 &referencePoint, float maxSubdivisionDistance)
{
    if (!node || !node->isLeaf || node->depth >= maxDepth_)
    {
        return;
    }

    // Subdivide this leaf node (same logic as in subdivideForProximityRecursive)
    node->isLeaf = false;
    float childSize = node->size * 0.5f;
    int childDepth = node->depth + 1;

    // Create 8 child nodes
    for (int i = 0; i < 8; i++)
    {
        glm::vec3 offset;
        offset.x = (i & 1) ? childSize : -childSize;
        offset.y = (i & 2) ? childSize : -childSize;
        offset.z = (i & 4) ? childSize : -childSize;

        glm::vec3 childCenter = node->center + offset;

        // Only create child if it intersects the sphere
        if (nodeIntersectsSphere(childCenter, childSize))
        {
            node->children[i] = std::make_unique<OctreeNode>(childCenter, childSize, childDepth);

            // Determine child's solidity
            float childDistFromCenter = glm::length(childCenter);
            float childSurfaceRadius = getSurfaceRadius(childCenter);

            glm::vec3 childClosestPoint;
            childClosestPoint.x = glm::clamp(0.0f, childCenter.x - childSize, childCenter.x + childSize);
            childClosestPoint.y = glm::clamp(0.0f, childCenter.y - childSize, childCenter.y + childSize);
            childClosestPoint.z = glm::clamp(0.0f, childCenter.z - childSize, childCenter.z + childSize);
            float childMinDist = glm::length(childClosestPoint);

            glm::vec3 childFarthestPoint;
            childFarthestPoint.x = (std::abs(childCenter.x + childSize) > std::abs(childCenter.x - childSize))
                                       ? childCenter.x + childSize
                                       : childCenter.x - childSize;
            childFarthestPoint.y = (std::abs(childCenter.y + childSize) > std::abs(childCenter.y - childSize))
                                       ? childCenter.y + childSize
                                       : childCenter.y - childSize;
            childFarthestPoint.z = (std::abs(childCenter.z + childSize) > std::abs(childCenter.z - childSize))
                                       ? childCenter.z + childSize
                                       : childCenter.z - childSize;
            float childMaxDist = glm::length(childFarthestPoint);

            const float HEIGHT_MARGIN = 20000.0f;

            node->children[i]->isLeaf = true;

            if (childMinDist > childSurfaceRadius + HEIGHT_MARGIN)
            {
                node->children[i]->isSolid = false;
            }
            else if (childMaxDist < childSurfaceRadius - HEIGHT_MARGIN)
            {
                node->children[i]->isSolid = true;
            }
            else
            {
                node->children[i]->isSolid = isVoxelSolid(childCenter, childSize);
            }
        }
    }
}

void PlanetOctree::extractSurfaceMesh(std::vector<MeshVertex> &vertices, std::vector<unsigned int> &indices)
{
    // Use greedy meshing for runtime mesh generation
    extractGreedyMesh(vertices, indices);
}

void PlanetOctree::extractSurfaceMesh(const glm::vec3 &referencePoint,
                                      float maxSubdivisionDistance,
                                      std::vector<MeshVertex> &vertices,
                                      std::vector<unsigned int> &indices)
{
    // Subdivide based on proximity first
    subdivideForProximity(referencePoint, maxSubdivisionDistance);

    // Extract mesh with distance filtering - only extract nodes within maxSubdivisionDistance
    // This ensures we get low resolution for far areas and high resolution only near camera
    vertices.clear();
    indices.clear();

    if (!root_)
    {
        std::cerr << "WARNING: PlanetOctree::extractSurfaceMesh() - Root node is null!" << "\n";
        return;
    }

    // Collect voxel data with distance filtering
    std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> voxelNodes;
    collectVoxelDataWithDistance(voxelNodes, referencePoint, maxSubdivisionDistance);

    if (voxelNodes.empty())
    {
        // Fallback: if no nodes within distance, extract at base level (very low detail)
        // This happens when viewing from very far away
        collectVoxelDataAtDepth(voxelNodes, 0); // Only root-level nodes
    }

    if (voxelNodes.empty())
    {
        std::cerr << "WARNING: PlanetOctree::extractSurfaceMesh() - No voxel data found!" << "\n";
        return;
    }

    unsigned int baseIndex = 0;

    // Greedy mesh along each axis (X, Y, Z)
    // This generates faces only on exposed surfaces
    for (int axis = 0; axis < 3; axis++)
    {
        greedyMeshAxis(voxelNodes, axis, vertices, indices, baseIndex);
    }
}

float PlanetOctree::sampleDensity(const glm::vec3 &pos) const
{
    // Returns: < 0 if inside planet, > 0 if outside, 0 at surface
    float distFromCenter = glm::length(pos);
    float surfaceRadius = getSurfaceRadius(pos);
    return distFromCenter - surfaceRadius;
}

// Calculate surface normal from density gradient (proper method for Marching Cubes)
// Uses finite differences to approximate the gradient of the density function
glm::vec3 PlanetOctree::calculateSurfaceNormal(const glm::vec3 &pos, float epsilon) const
{
    // Sample density at nearby points to compute gradient
    float d0 = sampleDensity(pos);
    float dx = sampleDensity(pos + glm::vec3(epsilon, 0.0f, 0.0f));
    float dy = sampleDensity(pos + glm::vec3(0.0f, epsilon, 0.0f));
    float dz = sampleDensity(pos + glm::vec3(0.0f, 0.0f, epsilon));

    // Compute gradient using finite differences
    glm::vec3 gradient = glm::vec3((dx - d0) / epsilon, (dy - d0) / epsilon, (dz - d0) / epsilon);

    // Normalize gradient to get surface normal
    float len = glm::length(gradient);
    glm::vec3 normal;

    if (len > 0.0001f)
    {
        normal = glm::normalize(gradient);
    }
    else
    {
        // Fallback: use radial direction (normalized position vector)
        float dist = glm::length(pos);
        if (dist > 0.001f)
        {
            normal = pos / dist; // Normalized radial direction (outward)
        }
        else
        {
            normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default fallback
        }
    }

    // CRITICAL: Ensure normal points outward (away from planet center)
    // The gradient points in direction of increasing density
    // For our density: negative inside planet, positive outside
    // So gradient should point outward, but verify and flip if needed

    // Get radial direction (from center to position)
    float distFromCenter = glm::length(pos);
    if (distFromCenter > 0.001f)
    {
        glm::vec3 radialDir = pos / distFromCenter;

        // Check if normal points inward (toward center)
        // If dot product is negative, normal points inward - flip it
        float dotProduct = glm::dot(normal, radialDir);
        if (dotProduct < 0.0f)
        {
            normal = -normal; // Flip to point outward
        }
    }

    return normal;
}

// Marching cubes edge table (12-bit mask for each of 256 cube configurations)
// Each bit indicates if an edge is intersected by the surface
static const int edgeTable[256] = {
    0x0,   0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c, 0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
    0x190, 0x99,  0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c, 0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
    0x230, 0x339, 0x33,  0x13a, 0x636, 0x73f, 0x35,  0x43c, 0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
    0x3a0, 0x2a9, 0x1a3, 0xaa,  0x7a6, 0x6af, 0x5a5, 0x4ac, 0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
    0x460, 0x569, 0x663, 0x76a, 0x66,  0x16f, 0x265, 0x36c, 0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
    0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff,  0x3f5, 0x2fc, 0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
    0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55,  0x15c, 0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
    0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc,  0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
    0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc, 0xcc,  0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
    0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c, 0x15c, 0x55,  0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
    0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc, 0x2fc, 0x3f5, 0xff,  0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
    0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c, 0x36c, 0x265, 0x16f, 0x66,  0x76a, 0x663, 0x569, 0x460,
    0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac, 0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa,  0x1a3, 0x2a9, 0x3a0,
    0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c, 0x43c, 0x35,  0x73f, 0x636, 0x13a, 0x33,  0x339, 0x230,
    0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c, 0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99,  0x190,
    0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c, 0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0};

// Marching cubes triangle table (up to 5 triangles per cube, 3 vertices each + sentinel -1)
static const int triTable[256][16] = {{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
                                      {2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1},
                                      {8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1},
                                      {3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1},
                                      {4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1},
                                      {4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
                                      {5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1},
                                      {2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1},
                                      {9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1},
                                      {2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1},
                                      {10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1},
                                      {5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1},
                                      {5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1},
                                      {10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1},
                                      {8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1},
                                      {2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1},
                                      {7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1},
                                      {2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1},
                                      {11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1},
                                      {5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1},
                                      {11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1},
                                      {11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1},
                                      {5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1},
                                      {2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1},
                                      {5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1},
                                      {6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1},
                                      {3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1},
                                      {6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1},
                                      {5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1},
                                      {10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1},
                                      {6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1},
                                      {8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1},
                                      {7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1},
                                      {3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1},
                                      {5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1},
                                      {0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1},
                                      {9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1},
                                      {8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1},
                                      {5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1},
                                      {0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1},
                                      {6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1},
                                      {10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1},
                                      {10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1},
                                      {10, 6, 1, 1, 6, 4, 1, 4, 8, 1, 8, 3, -1, -1, -1, -1},
                                      {1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1},
                                      {0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1},
                                      {10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1},
                                      {3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1},
                                      {6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1},
                                      {9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1},
                                      {8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1},
                                      {3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1},
                                      {6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1},
                                      {10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1},
                                      {10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1},
                                      {2, 6, 9, 2, 9, 1, 6, 7, 9, 8, 9, 3, 7, 3, 9, -1},
                                      {7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1},
                                      {7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1},
                                      {2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1},
                                      {1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1},
                                      {11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1},
                                      {8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1},
                                      {0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1},
                                      {7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
                                      {10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
                                      {2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1},
                                      {6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1},
                                      {7, 2, 3, 7, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1, -1},
                                      {2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1},
                                      {10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1},
                                      {10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1},
                                      {0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1},
                                      {7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1},
                                      {6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1},
                                      {8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1},
                                      {6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1},
                                      {4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1},
                                      {10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1},
                                      {8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1},
                                      {1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1},
                                      {8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1},
                                      {10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1},
                                      {10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1},
                                      {5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
                                      {11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1},
                                      {9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1},
                                      {6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1},
                                      {7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1},
                                      {3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1},
                                      {7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1},
                                      {3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1},
                                      {6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1},
                                      {9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1},
                                      {1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1},
                                      {4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1},
                                      {7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1},
                                      {6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1},
                                      {0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1},
                                      {6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1},
                                      {0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1},
                                      {11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1},
                                      {6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1},
                                      {5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1},
                                      {9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1},
                                      {1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1},
                                      {10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1},
                                      {0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1},
                                      {5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1},
                                      {10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1},
                                      {11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1},
                                      {9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1},
                                      {7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1},
                                      {2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1},
                                      {8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1},
                                      {9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1},
                                      {9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1},
                                      {1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1},
                                      {5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1},
                                      {0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1, -1},
                                      {10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1},
                                      {2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1},
                                      {0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1},
                                      {0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1},
                                      {9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1},
                                      {5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1},
                                      {5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1},
                                      {8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1},
                                      {9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1},
                                      {1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1},
                                      {3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1},
                                      {4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1},
                                      {9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1},
                                      {11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1},
                                      {11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1},
                                      {2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1},
                                      {9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1},
                                      {3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1},
                                      {1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1, -1},
                                      {4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1},
                                      {0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {2, 3, 10, 10, 3, 8, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1},
                                      {9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1},
                                      {1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {1, 3, 9, 9, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
                                      {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};

// Helper function to interpolate vertex position along an edge
// Standard marching cubes interpolation: finds point where density = isoValue (0.0)
// Formula: t = (isoValue - d1) / (d2 - d1), then P = P1 + t * (P2 - P1)
// Since isoValue = 0: t = -d1 / (d2 - d1)
//
// CRITICAL for watertight manifold: Adjacent cubes sharing an edge will compute
// the same vertex position because they use the same corner positions and densities.
// This ensures the mesh is watertight with no gaps.
static glm::vec3 interpolateEdge(const glm::vec3 &v1, const glm::vec3 &v2, float d1, float d2)
{
    const float isoValue = 0.0f; // Surface is at density = 0

    // Handle degenerate case (both corners have same density)
    if (std::abs(d2 - d1) < 0.0001f)
    {
        // If both are at iso-value, return midpoint
        if (std::abs(d1 - isoValue) < 0.0001f)
        {
            return (v1 + v2) * 0.5f;
        }
        // Otherwise return the corner closer to iso-value
        return (std::abs(d1 - isoValue) < std::abs(d2 - isoValue)) ? v1 : v2;
    }

    // Standard linear interpolation: t = (isoValue - d1) / (d2 - d1)
    float t = (isoValue - d1) / (d2 - d1);

    // Clamp t to [0, 1] for safety (should be in range if edge is intersected)
    t = glm::clamp(t, 0.0f, 1.0f);

    return v1 + t * (v2 - v1);
}

// Helper function to calculate normal using triangle face normal (for fallback)
static glm::vec3 calculateTriangleNormal(const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2)
{
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 normal = glm::cross(edge1, edge2);
    float len = glm::length(normal);
    if (len > 0.0001f)
    {
        return glm::normalize(normal);
    }
    return glm::normalize(v0); // Fallback to radial direction
}

void PlanetOctree::generateTrianglesForCube(const glm::vec3 &cubeCenter,
                                            float cubeSize,
                                            std::vector<MeshVertex> &vertices,
                                            std::vector<unsigned int> &indices,
                                            unsigned int &baseIndex) const
{
    // Marching cubes: sample density at 8 cube corners
    const float halfSize = cubeSize * 0.5f;
    glm::vec3 corners[8];
    float densities[8];

    // Define cube corners relative to center (standard MC ordering)
    corners[0] = cubeCenter + glm::vec3(-halfSize, -halfSize, -halfSize);
    corners[1] = cubeCenter + glm::vec3(halfSize, -halfSize, -halfSize);
    corners[2] = cubeCenter + glm::vec3(halfSize, halfSize, -halfSize);
    corners[3] = cubeCenter + glm::vec3(-halfSize, halfSize, -halfSize);
    corners[4] = cubeCenter + glm::vec3(-halfSize, -halfSize, halfSize);
    corners[5] = cubeCenter + glm::vec3(halfSize, -halfSize, halfSize);
    corners[6] = cubeCenter + glm::vec3(halfSize, halfSize, halfSize);
    corners[7] = cubeCenter + glm::vec3(-halfSize, halfSize, halfSize);

    // Sample density at each corner
    for (int i = 0; i < 8; i++)
    {
        densities[i] = sampleDensity(corners[i]);
    }

    // Determine cube configuration (which corners are inside/outside)
    int cubeIndex = 0;
    for (int i = 0; i < 8; i++)
    {
        if (densities[i] < 0.0f) // Inside planet
        {
            cubeIndex |= (1 << i);
        }
    }

    // If cube is completely outside (all corners outside), no surface
    if (cubeIndex == 0)
    {
        return;
    }

    // If cube is completely inside (all corners inside), no surface passes through
    // Standard Marching Cubes: only generate triangles where isosurface crosses the cube
    if (cubeIndex == 255)
    {
        return; // No surface intersection, skip this cube
    }

    // Get edge table entry for this cube configuration
    int edgeBits = edgeTable[cubeIndex];
    if (edgeBits == 0)
    {
        return; // No edges intersected
    }

    // Interpolate edge vertices where surface crosses edges
    // Initialize to zero - only edges marked in edgeBits will be computed
    glm::vec3 edgeVertices[12] = {glm::vec3(0.0f)};

    // Standard Marching Cubes edge ordering:
    // Edges 0-3: bottom face (z = -halfSize), edges 4-7: top face (z = +halfSize)
    // Edges 8-11: vertical edges connecting bottom to top
    // Edge 0: between corners 0 and 1
    if (edgeBits & 0x001)
    {
        edgeVertices[0] = interpolateEdge(corners[0], corners[1], densities[0], densities[1]);
    }
    // Edge 1: between corners 1 and 2
    if (edgeBits & 0x002)
    {
        edgeVertices[1] = interpolateEdge(corners[1], corners[2], densities[1], densities[2]);
    }
    // Edge 2: between corners 2 and 3
    if (edgeBits & 0x004)
    {
        edgeVertices[2] = interpolateEdge(corners[2], corners[3], densities[2], densities[3]);
    }
    // Edge 3: between corners 3 and 0
    if (edgeBits & 0x008)
    {
        edgeVertices[3] = interpolateEdge(corners[3], corners[0], densities[3], densities[0]);
    }
    // Edge 4: between corners 4 and 5
    if (edgeBits & 0x010)
    {
        edgeVertices[4] = interpolateEdge(corners[4], corners[5], densities[4], densities[5]);
    }
    // Edge 5: between corners 5 and 6
    if (edgeBits & 0x020)
    {
        edgeVertices[5] = interpolateEdge(corners[5], corners[6], densities[5], densities[6]);
    }
    // Edge 6: between corners 6 and 7
    if (edgeBits & 0x040)
    {
        edgeVertices[6] = interpolateEdge(corners[6], corners[7], densities[6], densities[7]);
    }
    // Edge 7: between corners 7 and 4
    if (edgeBits & 0x080)
    {
        edgeVertices[7] = interpolateEdge(corners[7], corners[4], densities[7], densities[4]);
    }
    // Edge 8: between corners 0 and 4
    if (edgeBits & 0x100)
    {
        edgeVertices[8] = interpolateEdge(corners[0], corners[4], densities[0], densities[4]);
    }
    // Edge 9: between corners 1 and 5
    if (edgeBits & 0x200)
    {
        edgeVertices[9] = interpolateEdge(corners[1], corners[5], densities[1], densities[5]);
    }
    // Edge 10: between corners 2 and 6
    if (edgeBits & 0x400)
    {
        edgeVertices[10] = interpolateEdge(corners[2], corners[6], densities[2], densities[6]);
    }
    // Edge 11: between corners 3 and 7
    if (edgeBits & 0x800)
    {
        edgeVertices[11] = interpolateEdge(corners[3], corners[7], densities[3], densities[7]);
    }

    // Generate triangles using triangle table
    // The triangle table references edges that should have been computed above
    const int *triangles = triTable[cubeIndex];
    for (int i = 0; triangles[i] != -1; i += 3)
    {
        int idx0 = triangles[i];
        int idx1 = triangles[i + 1];
        int idx2 = triangles[i + 2];

        // Validate edge indices
        if (idx0 < 0 || idx0 >= 12 || idx1 < 0 || idx1 >= 12 || idx2 < 0 || idx2 >= 12)
        {
            continue; // Invalid edge index
        }

        // Verify these edges were actually intersected (safety check)
        // Edge table uses bit flags: 0x001=edge0, 0x002=edge1, 0x004=edge2, etc.
        int edgeMask0 = (1 << idx0);
        int edgeMask1 = (1 << idx1);
        int edgeMask2 = (1 << idx2);
        if (!(edgeBits & edgeMask0) || !(edgeBits & edgeMask1) || !(edgeBits & edgeMask2))
        {
            // Edge not intersected - this shouldn't happen with correct tables
            // but skip to avoid using uninitialized vertices
            continue;
        }

        glm::vec3 v0 = edgeVertices[idx0];
        glm::vec3 v1 = edgeVertices[idx1];
        glm::vec3 v2 = edgeVertices[idx2];

        // Validate vertices are not degenerate (all zeros would indicate uninitialized)
        if (glm::length(v0) < 0.001f || glm::length(v1) < 0.001f || glm::length(v2) < 0.001f)
        {
            continue; // Skip degenerate triangle
        }

        // Calculate surface normals using density gradient (proper method for Marching Cubes)
        // This gives accurate normals for curved surfaces like planets
        // Use a small epsilon based on cube size for gradient calculation
        float epsilon = cubeSize * 0.01f;  // 1% of cube size
        epsilon = glm::max(epsilon, 0.1f); // Minimum epsilon for numerical stability

        // Calculate normals - calculateSurfaceNormal already ensures they point outward
        glm::vec3 normal0 = calculateSurfaceNormal(v0, epsilon);
        glm::vec3 normal1 = calculateSurfaceNormal(v1, epsilon);
        glm::vec3 normal2 = calculateSurfaceNormal(v2, epsilon);

        // CRITICAL: Ensure triangle winding order is correct for outward-facing normals
        // Compute face normal from triangle vertices to verify winding
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 faceNormal = glm::cross(edge1, edge2);
        float faceNormalLen = glm::length(faceNormal);

        if (faceNormalLen > 0.0001f)
        {
            faceNormal = glm::normalize(faceNormal);

            // Check if face normal points outward (should match vertex normals)
            float distFromCenter = glm::length(v0);
            if (distFromCenter > 0.001f)
            {
                glm::vec3 radialDir = v0 / distFromCenter;
                float dotFaceRadial = glm::dot(faceNormal, radialDir);

                // If face normal points inward, reverse triangle winding order
                if (dotFaceRadial < 0.0f)
                {
                    // Swap v1 and v2 to reverse winding
                    std::swap(v1, v2);
                    std::swap(normal1, normal2);
                }
            }
        }

        // Calculate UV coordinates
        const float PI_F = 3.14159265359f;
        auto calcUV = [PI_F](const glm::vec3 &pos) -> glm::vec2 {
            glm::vec3 dir = glm::normalize(pos);
            float latitude = std::asin(glm::clamp(dir.y, -1.0f, 1.0f));
            float longitude = std::atan2(dir.z, dir.x);
            float u = (longitude / PI_F + 1.0f) * 0.5f;
            float v = 0.5f - (latitude / PI_F);
            return glm::vec2(u, v);
        };

        // Create vertices
        MeshVertex vert0, vert1, vert2;
        vert0.position = v0;
        vert0.normal = normal0;
        vert0.uv = calcUV(v0);

        vert1.position = v1;
        vert1.normal = normal1;
        vert1.uv = calcUV(v1);

        vert2.position = v2;
        vert2.normal = normal2;
        vert2.uv = calcUV(v2);

        // Add triangle
        vertices.push_back(vert0);
        vertices.push_back(vert1);
        vertices.push_back(vert2);

        indices.push_back(baseIndex++);
        indices.push_back(baseIndex++);
        indices.push_back(baseIndex++);
    }
}

void PlanetOctree::extractMeshFromNode(const OctreeNode *node,
                                       std::vector<MeshVertex> &vertices,
                                       std::vector<unsigned int> &indices,
                                       unsigned int &baseIndex) const
{
    if (!node)
    {
        return;
    }

    if (node->isLeaf)
    {
        // For leaf nodes, use marching cubes to generate surface mesh
        // Generate mesh if node crosses the surface boundary (has both inside and outside corners)

        // Check if node crosses surface by sampling density at corners
        // If any corner is inside and any corner is outside, the node crosses the surface
        const float halfSize = node->size;
        glm::vec3 corners[8];
        corners[0] = node->center + glm::vec3(-halfSize, -halfSize, -halfSize);
        corners[1] = node->center + glm::vec3(halfSize, -halfSize, -halfSize);
        corners[2] = node->center + glm::vec3(halfSize, halfSize, -halfSize);
        corners[3] = node->center + glm::vec3(-halfSize, halfSize, -halfSize);
        corners[4] = node->center + glm::vec3(-halfSize, -halfSize, halfSize);
        corners[5] = node->center + glm::vec3(halfSize, -halfSize, halfSize);
        corners[6] = node->center + glm::vec3(halfSize, halfSize, halfSize);
        corners[7] = node->center + glm::vec3(-halfSize, halfSize, halfSize);

        // Sample density at corners
        // For watertight manifold: only process voxels where surface passes through
        // (has both inside and outside corners)
        bool hasInside = false;
        bool hasOutside = false;

        for (int i = 0; i < 8; i++)
        {
            float density = sampleDensity(corners[i]);
            if (density < 0.0f) // Inside planet (below surface radius)
            {
                hasInside = true;
            }
            else // Outside planet (at or above surface radius)
            {
                hasOutside = true;
            }

            // Early exit optimization: if we've found both, we know surface passes through
            if (hasInside && hasOutside)
            {
                break;
            }
        }

        // CRITICAL for watertight manifold: only generate triangles for voxels where
        // the surface actually passes through (has both inside and outside corners)
        // This ensures adjacent voxels will share edge vertices properly
        if (hasInside && hasOutside)
        {
            // node->size is half-extent, but generateTrianglesForCube expects full cube size
            generateTrianglesForCube(node->center, node->size * 2.0f, vertices, indices, baseIndex);
        }
    }
    else
    {
        // Recurse into children
        for (const auto &child : node->children)
        {
            if (child)
            {
                extractMeshFromNode(child.get(), vertices, indices, baseIndex);
            }
        }
    }
}

glm::vec2 PlanetOctree::worldToEquirectUV(const glm::vec3 &worldPos,
                                          const glm::vec3 &poleDir,
                                          const glm::vec3 &primeDir) const
{
    // TODO: Implement proper coordinate conversion
    // For now, use simple spherical coordinates
    float dist = glm::length(worldPos);
    if (dist < 0.001f)
    {
        return glm::vec2(0.5f, 0.5f);
    }

    glm::vec3 dir = worldPos / dist;
    const float PI_F = 3.14159265359f;
    float latitude = std::asin(glm::clamp(dir.y, -1.0f, 1.0f));
    float longitude = std::atan2(dir.z, dir.x);

    float u = (longitude / PI_F + 1.0f) * 0.5f;
    float v = 0.5f - (latitude / PI_F);

    return glm::vec2(u, v);
}

// Chunked mesh generation implementation
void PlanetOctree::extractChunkedSurfaceMesh(std::vector<MeshVertex> &vertices,
                                             std::vector<unsigned int> &indices,
                                             int numChunksX,
                                             int numChunksY)
{
    vertices.clear();
    indices.clear();

    if (!root_)
    {
        std::cerr << "WARNING: PlanetOctree::extractChunkedSurfaceMesh() - Root node is null!" << "\n";
        return;
    }

    // Generate chunks in parallel
    std::vector<ChunkMesh> chunks(numChunksX * numChunksY);
    std::vector<std::thread> threads;
    std::mutex chunksMutex;
    std::atomic<int> completedChunks(0);

    // Launch threads to generate chunks
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0)
    {
        numThreads = 4; // Fallback to 4 threads
    }

    std::atomic<int> nextChunkIndex(0);
    int totalChunks = numChunksX * numChunksY;

    // Worker function for parallel chunk generation
    auto worker = [&]() {
        while (true)
        {
            int chunkIdx = nextChunkIndex.fetch_add(1);
            if (chunkIdx >= totalChunks)
            {
                break;
            }

            int chunkX = chunkIdx % numChunksX;
            int chunkY = chunkIdx / numChunksX;

            chunks[chunkIdx] = generateChunkMesh(chunkX, chunkY, numChunksX, numChunksY);
            completedChunks.fetch_add(1);
        }
    };

    // Launch worker threads
    for (unsigned int i = 0; i < numThreads; i++)
    {
        threads.emplace_back(worker);
    }

    // Wait for all threads to complete
    for (auto &thread : threads)
    {
        thread.join();
    }

    std::cout << "  Generated " << completedChunks.load() << " chunks in parallel" << "\n";

    // Stitch chunks together
    stitchChunks(chunks, vertices, indices);

    std::cout << "  Stitched chunks: " << vertices.size() << " vertices, " << indices.size() << " indices" << "\n";
}

void PlanetOctree::extractChunkedSurfaceMesh(const glm::vec3 &referencePoint,
                                             float maxSubdivisionDistance,
                                             std::vector<MeshVertex> &vertices,
                                             std::vector<unsigned int> &indices,
                                             int numChunksX,
                                             int numChunksY)
{
    // Subdivide based on proximity first
    subdivideForProximity(referencePoint, maxSubdivisionDistance);

    // Then extract mesh normally
    extractChunkedSurfaceMesh(vertices, indices, numChunksX, numChunksY);
}

ChunkMesh PlanetOctree::generateChunkMesh(int chunkX, int chunkY, int numChunksX, int numChunksY) const
{
    ChunkMesh chunk;
    chunk.chunkX = chunkX;
    chunk.chunkY = chunkY;
    chunk.isValid = false;

    if (!root_)
    {
        return chunk;
    }

    // Calculate chunk boundaries in spherical coordinates
    const float PI_F = 3.14159265359f;
    float chunkWidth = 2.0f * PI_F / numChunksX; // Longitude range per chunk
    float chunkHeight = PI_F / numChunksY;       // Latitude range per chunk

    float minLon = -PI_F + chunkX * chunkWidth;
    float maxLon = -PI_F + (chunkX + 1) * chunkWidth;
    float minLat = -PI_F / 2.0f + chunkY * chunkHeight;
    float maxLat = -PI_F / 2.0f + (chunkY + 1) * chunkHeight;

    // Convert to world space bounding box
    // Create bounding box that encompasses the chunk region on the sphere surface
    float minRadius = averageRadius_ - 11000.0f; // Deepest trench
    float maxRadius = averageRadius_ + 8848.0f;  // Mt. Everest

    // Create bounding box corners
    glm::vec3 chunkMin(-maxRadius * 1.1f, -maxRadius * 1.1f, -maxRadius * 1.1f);
    glm::vec3 chunkMax(maxRadius * 1.1f, maxRadius * 1.1f, maxRadius * 1.1f);

    // Refine bounding box based on spherical coordinates
    // This is approximate - we'll use the full sphere and let the octree cull
    // For now, use full sphere bounds and let surface detection handle it

    unsigned int baseIndex = 0;
    extractMeshFromNodeChunked(root_.get(),
                               chunkMin,
                               chunkMax,
                               chunk.vertices,
                               chunk.indices,
                               chunk.edgeVertices,
                               chunkX,
                               chunkY,
                               baseIndex);

    chunk.isValid = !chunk.vertices.empty();
    return chunk;
}

void PlanetOctree::extractMeshFromNodeChunked(const OctreeNode *node,
                                              const glm::vec3 &chunkMin,
                                              const glm::vec3 &chunkMax,
                                              std::vector<MeshVertex> &vertices,
                                              std::vector<unsigned int> &indices,
                                              std::vector<EdgeVertex> &edgeVertices,
                                              int chunkX,
                                              int chunkY,
                                              unsigned int &baseIndex) const
{
    if (!node)
    {
        return;
    }

    // Check if node intersects chunk bounds (simplified - check if any corner is in bounds)
    const float halfSize = node->size;
    glm::vec3 nodeMin = node->center - glm::vec3(halfSize);
    glm::vec3 nodeMax = node->center + glm::vec3(halfSize);

    // Skip if node is completely outside chunk bounds
    if (nodeMax.x < chunkMin.x || nodeMin.x > chunkMax.x || nodeMax.y < chunkMin.y || nodeMin.y > chunkMax.y ||
        nodeMax.z < chunkMin.z || nodeMin.z > chunkMax.z)
    {
        return;
    }

    if (node->isLeaf)
    {
        // Check if node crosses surface
        glm::vec3 corners[8];
        corners[0] = node->center + glm::vec3(-halfSize, -halfSize, -halfSize);
        corners[1] = node->center + glm::vec3(halfSize, -halfSize, -halfSize);
        corners[2] = node->center + glm::vec3(halfSize, halfSize, -halfSize);
        corners[3] = node->center + glm::vec3(-halfSize, halfSize, -halfSize);
        corners[4] = node->center + glm::vec3(-halfSize, -halfSize, halfSize);
        corners[5] = node->center + glm::vec3(halfSize, -halfSize, halfSize);
        corners[6] = node->center + glm::vec3(halfSize, halfSize, halfSize);
        corners[7] = node->center + glm::vec3(-halfSize, halfSize, halfSize);

        bool hasInside = false;
        bool hasOutside = false;
        for (int i = 0; i < 8; i++)
        {
            float density = sampleDensity(corners[i]);
            if (density < 0.0f)
            {
                hasInside = true;
            }
            else
            {
                hasOutside = true;
            }
        }

        if (hasInside && hasOutside)
        {
            // Generate triangles and track edge vertices
            size_t startVertexCount = vertices.size();
            generateTrianglesForCube(node->center, node->size, vertices, indices, baseIndex);

            // Check which vertices are on chunk edges
            const float EDGE_EPSILON = node->size * 0.01f;
            for (size_t i = startVertexCount; i < vertices.size(); i++)
            {
                if (isOnChunkEdge(vertices[i].position, chunkMin, chunkMax, EDGE_EPSILON))
                {
                    EdgeVertex edgeVert;
                    edgeVert.position = vertices[i].position;
                    edgeVert.vertexIndex = static_cast<unsigned int>(i);
                    edgeVert.chunkX = chunkX;
                    edgeVert.chunkY = chunkY;
                    // Determine which edge side (simplified - would need more precise detection)
                    edgeVert.edgeSide = 0; // Placeholder
                    edgeVertices.push_back(edgeVert);
                }
            }
        }
    }
    else
    {
        // Recurse into children
        for (const auto &child : node->children)
        {
            if (child)
            {
                extractMeshFromNodeChunked(child.get(),
                                           chunkMin,
                                           chunkMax,
                                           vertices,
                                           indices,
                                           edgeVertices,
                                           chunkX,
                                           chunkY,
                                           baseIndex);
            }
        }
    }
}

bool PlanetOctree::isOnChunkEdge(const glm::vec3 &pos,
                                 const glm::vec3 &chunkMin,
                                 const glm::vec3 &chunkMax,
                                 float epsilon) const
{
    // Check if position is near any chunk boundary
    return (std::abs(pos.x - chunkMin.x) < epsilon || std::abs(pos.x - chunkMax.x) < epsilon ||
            std::abs(pos.y - chunkMin.y) < epsilon || std::abs(pos.y - chunkMax.y) < epsilon ||
            std::abs(pos.z - chunkMin.z) < epsilon || std::abs(pos.z - chunkMax.z) < epsilon);
}

void PlanetOctree::stitchChunks(const std::vector<ChunkMesh> &chunks,
                                std::vector<MeshVertex> &finalVertices,
                                std::vector<unsigned int> &finalIndices) const
{
    // Build lookup tables for edge vertices
    // Table 1: Position -> vertex index mapping for fast lookup
    std::unordered_map<size_t, unsigned int> positionToIndex;

    // Table 2: Edge vertex groups (vertices that should be merged)
    std::vector<std::vector<unsigned int>> vertexGroups;

    // First pass: collect all vertices and build position hash map
    unsigned int globalIndex = 0;
    for (const auto &chunk : chunks)
    {
        if (!chunk.isValid)
        {
            continue;
        }

        for (const auto &vertex : chunk.vertices)
        {
            // Hash position for lookup (simple hash)
            size_t posHash = std::hash<float>{}(vertex.position.x) ^ (std::hash<float>{}(vertex.position.y) << 1) ^
                             (std::hash<float>{}(vertex.position.z) << 2);

            // Check if we've seen this position before (within epsilon)
            const float MERGE_EPSILON = 0.1f;
            bool found = false;
            for (auto it = positionToIndex.begin(); it != positionToIndex.end(); ++it)
            {
                unsigned int existingIdx = it->second;
                if (existingIdx < finalVertices.size())
                {
                    float dist = glm::length(finalVertices[existingIdx].position - vertex.position);
                    if (dist < MERGE_EPSILON)
                    {
                        // Use existing vertex
                        positionToIndex[posHash] = existingIdx;
                        found = true;
                        break;
                    }
                }
            }

            if (!found)
            {
                finalVertices.push_back(vertex);
                positionToIndex[posHash] = globalIndex++;
            }
        }
    }

    // Second pass: remap indices using lookup table
    for (const auto &chunk : chunks)
    {
        if (!chunk.isValid)
        {
            continue;
        }

        unsigned int chunkVertexOffset = 0;
        for (size_t i = 0; i < chunk.indices.size(); i += 3)
        {
            if (i + 2 < chunk.indices.size())
            {
                unsigned int idx0 = chunk.indices[i];
                unsigned int idx1 = chunk.indices[i + 1];
                unsigned int idx2 = chunk.indices[i + 2];

                if (idx0 < chunk.vertices.size() && idx1 < chunk.vertices.size() && idx2 < chunk.vertices.size())
                {
                    // Find global indices for these vertices
                    const auto &v0 = chunk.vertices[idx0];
                    const auto &v1 = chunk.vertices[idx1];
                    const auto &v2 = chunk.vertices[idx2];

                    size_t hash0 = std::hash<float>{}(v0.position.x) ^ (std::hash<float>{}(v0.position.y) << 1) ^
                                   (std::hash<float>{}(v0.position.z) << 2);
                    size_t hash1 = std::hash<float>{}(v1.position.x) ^ (std::hash<float>{}(v1.position.y) << 1) ^
                                   (std::hash<float>{}(v1.position.z) << 2);
                    size_t hash2 = std::hash<float>{}(v2.position.x) ^ (std::hash<float>{}(v2.position.y) << 1) ^
                                   (std::hash<float>{}(v2.position.z) << 2);

                    // Find matching vertices in final mesh
                    unsigned int globalIdx0 = positionToIndex[hash0];
                    unsigned int globalIdx1 = positionToIndex[hash1];
                    unsigned int globalIdx2 = positionToIndex[hash2];

                    // Find actual matching vertices by position
                    for (size_t j = 0; j < finalVertices.size(); j++)
                    {
                        if (glm::length(finalVertices[j].position - v0.position) < 0.1f)
                        {
                            globalIdx0 = static_cast<unsigned int>(j);
                            break;
                        }
                    }
                    for (size_t j = 0; j < finalVertices.size(); j++)
                    {
                        if (glm::length(finalVertices[j].position - v1.position) < 0.1f)
                        {
                            globalIdx1 = static_cast<unsigned int>(j);
                            break;
                        }
                    }
                    for (size_t j = 0; j < finalVertices.size(); j++)
                    {
                        if (glm::length(finalVertices[j].position - v2.position) < 0.1f)
                        {
                            globalIdx2 = static_cast<unsigned int>(j);
                            break;
                        }
                    }

                    finalIndices.push_back(globalIdx0);
                    finalIndices.push_back(globalIdx1);
                    finalIndices.push_back(globalIdx2);
                }
            }
        }
    }
}

// Debug: Extract voxel wireframe edges for visualization
void PlanetOctree::extractVoxelWireframes(std::vector<glm::vec3> &edgeVertices) const
{
    edgeVertices.clear();
    extractVoxelWireframesFromNode(root_.get(), edgeVertices);
}

void PlanetOctree::extractVoxelWireframesFromNode(const OctreeNode *node, std::vector<glm::vec3> &edgeVertices) const
{
    if (!node)
    {
        return;
    }

    // Only extract wireframes from leaf nodes that cross the surface
    if (node->isLeaf)
    {
        // Check if this node crosses the surface (has both inside and outside corners)
        const float halfSize = node->size;
        glm::vec3 corners[8];
        corners[0] = node->center + glm::vec3(-halfSize, -halfSize, -halfSize);
        corners[1] = node->center + glm::vec3(halfSize, -halfSize, -halfSize);
        corners[2] = node->center + glm::vec3(halfSize, halfSize, -halfSize);
        corners[3] = node->center + glm::vec3(-halfSize, halfSize, -halfSize);
        corners[4] = node->center + glm::vec3(-halfSize, -halfSize, halfSize);
        corners[5] = node->center + glm::vec3(halfSize, -halfSize, halfSize);
        corners[6] = node->center + glm::vec3(halfSize, halfSize, halfSize);
        corners[7] = node->center + glm::vec3(-halfSize, halfSize, halfSize);

        bool hasInside = false;
        bool hasOutside = false;
        for (int i = 0; i < 8; i++)
        {
            float density = sampleDensity(corners[i]);
            if (density < 0.0f)
            {
                hasInside = true;
            }
            else
            {
                hasOutside = true;
            }
        }

        // Only draw wireframe if node crosses the surface
        if (hasInside && hasOutside)
        {
            // Draw all 12 edges of the cube
            // Bottom face edges (z = -halfSize)
            edgeVertices.push_back(corners[0]);
            edgeVertices.push_back(corners[1]); // Edge 0
            edgeVertices.push_back(corners[1]);
            edgeVertices.push_back(corners[2]); // Edge 1
            edgeVertices.push_back(corners[2]);
            edgeVertices.push_back(corners[3]); // Edge 2
            edgeVertices.push_back(corners[3]);
            edgeVertices.push_back(corners[0]); // Edge 3

            // Top face edges (z = +halfSize)
            edgeVertices.push_back(corners[4]);
            edgeVertices.push_back(corners[5]); // Edge 4
            edgeVertices.push_back(corners[5]);
            edgeVertices.push_back(corners[6]); // Edge 5
            edgeVertices.push_back(corners[6]);
            edgeVertices.push_back(corners[7]); // Edge 6
            edgeVertices.push_back(corners[7]);
            edgeVertices.push_back(corners[4]); // Edge 7

            // Vertical edges connecting bottom to top
            edgeVertices.push_back(corners[0]);
            edgeVertices.push_back(corners[4]); // Edge 8
            edgeVertices.push_back(corners[1]);
            edgeVertices.push_back(corners[5]); // Edge 9
            edgeVertices.push_back(corners[2]);
            edgeVertices.push_back(corners[6]); // Edge 10
            edgeVertices.push_back(corners[3]);
            edgeVertices.push_back(corners[7]); // Edge 11
        }
    }
    else
    {
        // Recurse into children
        for (const auto &child : node->children)
        {
            if (child)
            {
                extractVoxelWireframesFromNode(child.get(), edgeVertices);
            }
        }
    }
}

void PlanetOctree::storeVoxelBits(OctreeNode *node)
{
    if (!node)
    {
        return;
    }

    // For a leaf node at max depth, store voxel bits for a 32x32x32 grid
    // Storage: 32 rows (y) × 32 uint32_t per row (z), each uint32_t has 32 bits (x)
    // Total: 32 × 32 × 4 bytes = 4 KB per leaf node
    const int gridSize = 32; // 32x32x32 grid
    const float voxelSize = node->size / static_cast<float>(gridSize);
    const float halfVoxelSize = voxelSize * 0.5f;

    // Initialize 2D array: 32 rows × 32 uint32_t per row
    node->voxelGrid.clear();
    node->voxelGrid.resize(gridSize);
    for (int y = 0; y < gridSize; y++)
    {
        node->voxelGrid[y].resize(gridSize, 0);
    }

    bool hasSolidVoxel = false;
    
    // Sample 32x32x32 voxels, storing row by row
    // Each row (y) contains 32 uint32_t (z), each uint32_t has 32 bits (x)
    for (int y = 0; y < gridSize; y++)
    {
        for (int z = 0; z < gridSize; z++)
        {
            uint32_t rowBits = 0;
            
            for (int x = 0; x < gridSize; x++)
            {
                // Calculate voxel center position
                glm::vec3 offset;
                offset.x = (x + 0.5f) * voxelSize - node->size;
                offset.y = (y + 0.5f) * voxelSize - node->size;
                offset.z = (z + 0.5f) * voxelSize - node->size;

                glm::vec3 voxelCenter = node->center + offset;
                
                // Check if this voxel is solid
                bool solid = isVoxelSolid(voxelCenter, voxelSize);
                
                // Set bit at x position (0-31) in the uint32_t
                if (solid)
                {
                    rowBits |= (1U << x);
                    hasSolidVoxel = true;
                }
            }
            
            // Store the uint32_t for this (y, z) position
            node->voxelGrid[y][z] = rowBits;
        }
    }
    
    // Set isSolid based on whether any voxels are solid
    node->isSolid = hasSolidVoxel;
}

bool PlanetOctree::queryVoxelBits(const OctreeNode *node, const glm::vec3 &localPos) const
{
    if (!node || node->voxelGrid.empty())
    {
        // Fallback to isSolid if no grid stored
        return node ? node->isSolid : false;
    }

    // For 32x32x32 grid, determine which voxel this position falls into
    const int gridSize = 32;
    const float halfSize = node->size;

    // Convert local position to grid coordinates (0-31 range)
    // Position relative to node center, normalized to [0, 1] then scaled to [0, gridSize-1]
    glm::vec3 normalizedPos = (localPos + glm::vec3(halfSize)) / (node->size * 2.0f);
    
    // Clamp to [0, 1] range and convert to integer grid coordinates
    normalizedPos.x = glm::clamp(normalizedPos.x, 0.0f, 1.0f);
    normalizedPos.y = glm::clamp(normalizedPos.y, 0.0f, 1.0f);
    normalizedPos.z = glm::clamp(normalizedPos.z, 0.0f, 1.0f);
    
    // Convert to grid coordinates (0-31 for each axis)
    int gridX = static_cast<int>(normalizedPos.x * gridSize);
    int gridY = static_cast<int>(normalizedPos.y * gridSize);
    int gridZ = static_cast<int>(normalizedPos.z * gridSize);
    
    // Clamp to valid grid range
    gridX = std::max(0, std::min(gridX, gridSize - 1));
    gridY = std::max(0, std::min(gridY, gridSize - 1));
    gridZ = std::max(0, std::min(gridZ, gridSize - 1));

    // Check the bit at (x, y, z) position
    // voxelGrid[y][z] contains uint32_t with bits for x=0..31
    return isVoxelSolidBitwise(node->voxelGrid, gridX, gridY, gridZ);
}

bool PlanetOctree::queryVoxelRecursive(const OctreeNode *node, const glm::vec3 &pos) const
{
    if (!node)
    {
        return false;
    }

    // Check if position is within this node's bounds
    const float halfSize = node->size;
    glm::vec3 nodeMin = node->center - glm::vec3(halfSize);
    glm::vec3 nodeMax = node->center + glm::vec3(halfSize);

    if (pos.x < nodeMin.x || pos.x > nodeMax.x ||
        pos.y < nodeMin.y || pos.y > nodeMax.y ||
        pos.z < nodeMin.z || pos.z > nodeMax.z)
    {
        return false; // Position outside this node
    }

    if (node->isLeaf)
    {
        // Leaf node - query voxel bits
        glm::vec3 localPos = pos - node->center;
        return queryVoxelBits(node, localPos);
    }
    else
    {
        // Internal node - recurse into appropriate child
        const float childSize = node->size * 0.5f;
        glm::vec3 localPos = pos - node->center;
        
        // Determine which child contains this position
        int childIndex = 0;
        if (localPos.x >= 0.0f) childIndex |= 1;
        if (localPos.y >= 0.0f) childIndex |= 2;
        if (localPos.z >= 0.0f) childIndex |= 4;

        if (node->children[childIndex])
        {
            return queryVoxelRecursive(node->children[childIndex].get(), pos);
        }
        else
        {
            // Child doesn't exist - return false (empty)
            return false;
        }
    }
}

bool PlanetOctree::queryVoxel(const glm::vec3 &pos) const
{
    if (!root_)
    {
        return false;
    }
    return queryVoxelRecursive(root_.get(), pos);
}

size_t PlanetOctree::getVoxelDataSize() const
{
    size_t totalSize = 0;
    std::function<void(const OctreeNode *)> countSize = [&](const OctreeNode *node) {
        if (!node)
        {
            return;
        }
        // Count size of voxel grid: 32 rows × 32 uint32_t × 4 bytes = 4 KB per leaf node
        if (!node->voxelGrid.empty())
        {
            totalSize += node->voxelGrid.size() * node->voxelGrid[0].size() * sizeof(uint32_t);
        }
        if (!node->isLeaf)
        {
            for (const auto &child : node->children)
            {
                if (child)
                {
                    countSize(child.get());
                }
            }
        }
    };
    countSize(root_.get());
    return totalSize;
}

void PlanetOctree::serializeNode(std::ostream &out, const OctreeNode *node) const
{
    // Write node data
    out.write(reinterpret_cast<const char *>(&node->center), sizeof(glm::vec3));
    out.write(reinterpret_cast<const char *>(&node->size), sizeof(float));
    out.write(reinterpret_cast<const char *>(&node->depth), sizeof(int));
    out.write(reinterpret_cast<const char *>(&node->isLeaf), sizeof(bool));
    out.write(reinterpret_cast<const char *>(&node->isSolid), sizeof(bool));

    // Write voxel grid (32x32 array of uint32_t = 4 KB)
    size_t gridRows = node->voxelGrid.size();
    out.write(reinterpret_cast<const char *>(&gridRows), sizeof(size_t));
    for (size_t y = 0; y < gridRows; y++)
    {
        size_t rowSize = node->voxelGrid[y].size();
        out.write(reinterpret_cast<const char *>(&rowSize), sizeof(size_t));
        if (!node->voxelGrid[y].empty())
        {
            out.write(reinterpret_cast<const char *>(node->voxelGrid[y].data()), rowSize * sizeof(uint32_t));
        }
    }

    // Write children recursively
    for (int i = 0; i < 8; i++)
    {
        bool childExists = (node->children[i] != nullptr);
        out.write(reinterpret_cast<const char *>(&childExists), sizeof(bool));
        if (childExists)
        {
            serializeNode(out, node->children[i].get());
        }
    }
}

void PlanetOctree::deserializeNode(std::istream &in, OctreeNode *node)
{
    // Read node data
    in.read(reinterpret_cast<char *>(&node->center), sizeof(glm::vec3));
    in.read(reinterpret_cast<char *>(&node->size), sizeof(float));
    in.read(reinterpret_cast<char *>(&node->depth), sizeof(int));
    in.read(reinterpret_cast<char *>(&node->isLeaf), sizeof(bool));
    in.read(reinterpret_cast<char *>(&node->isSolid), sizeof(bool));

    // Read voxel grid (32x32 array of uint32_t = 4 KB)
    size_t gridRows = 0;
    in.read(reinterpret_cast<char *>(&gridRows), sizeof(size_t));
    node->voxelGrid.clear();
    node->voxelGrid.resize(gridRows);
    for (size_t y = 0; y < gridRows; y++)
    {
        size_t rowSize = 0;
        in.read(reinterpret_cast<char *>(&rowSize), sizeof(size_t));
        node->voxelGrid[y].resize(rowSize);
        if (!node->voxelGrid[y].empty())
        {
            in.read(reinterpret_cast<char *>(node->voxelGrid[y].data()), rowSize * sizeof(uint32_t));
        }
    }

    // Read children recursively
    for (int i = 0; i < 8; i++)
    {
        // Check if child exists
        bool childExists = false;
        in.read(reinterpret_cast<char *>(&childExists), sizeof(bool));
        if (childExists)
        {
            // Create child node (we'll read its data in the recursive call)
            glm::vec3 childCenter;
            float childSize;
            int childDepth;
            in.read(reinterpret_cast<char *>(&childCenter), sizeof(glm::vec3));
            in.read(reinterpret_cast<char *>(&childSize), sizeof(float));
            in.read(reinterpret_cast<char *>(&childDepth), sizeof(int));

            node->children[i] = std::make_unique<OctreeNode>(childCenter, childSize, childDepth);
            deserializeNode(in, node->children[i].get());
        }
    }
}

bool PlanetOctree::serializeToFile(const std::string &filepath) const
{
    try
    {
        std::ofstream out(filepath, std::ios::binary);
        if (!out.is_open())
        {
            std::cerr << "ERROR: Failed to open file for writing: " << filepath << "\n";
            return false;
        }

        // Write header: version, baseRadius, maxRadius, maxDepth
        const uint32_t VERSION = 1;
        out.write(reinterpret_cast<const char *>(&VERSION), sizeof(uint32_t));
        out.write(reinterpret_cast<const char *>(&baseRadius_), sizeof(float));
        out.write(reinterpret_cast<const char *>(&maxRadius_), sizeof(float));
        out.write(reinterpret_cast<const char *>(&maxDepth_), sizeof(int));

        // Serialize root node (includes center, size, depth, isLeaf, isSolid, voxelBits, children)
        if (root_)
        {
            serializeNode(out, root_.get());
        }

        out.close();
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: Exception during serialization: " << e.what() << "\n";
        return false;
    }
}

bool PlanetOctree::deserializeFromFile(const std::string &filepath)
{
    try
    {
        std::ifstream in(filepath, std::ios::binary);
        if (!in.is_open())
        {
            std::cerr << "ERROR: Failed to open file for reading: " << filepath << "\n";
            return false;
        }

        // Read header
        uint32_t version = 0;
        in.read(reinterpret_cast<char *>(&version), sizeof(uint32_t));
        if (version != 1)
        {
            std::cerr << "ERROR: Unsupported octree file version: " << version << "\n";
            return false;
        }

        in.read(reinterpret_cast<char *>(&baseRadius_), sizeof(float));
        in.read(reinterpret_cast<char *>(&maxRadius_), sizeof(float));
        in.read(reinterpret_cast<char *>(&maxDepth_), sizeof(int));

        // Deserialize root node
        if (!root_)
        {
            // Read root node center, size, depth first
            glm::vec3 rootCenter;
            float rootSize;
            int rootDepth;
            in.read(reinterpret_cast<char *>(&rootCenter), sizeof(glm::vec3));
            in.read(reinterpret_cast<char *>(&rootSize), sizeof(float));
            in.read(reinterpret_cast<char *>(&rootDepth), sizeof(int));

            root_ = std::make_unique<OctreeNode>(rootCenter, rootSize, rootDepth);
        }
        // Now deserialize the rest of the root node (isLeaf, isSolid, voxelBits, children)
        deserializeNode(in, root_.get());

        in.close();
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR: Exception during deserialization: " << e.what() << "\n";
        return false;
    }
}

void PlanetOctree::extractGreedyMesh(std::vector<MeshVertex> &vertices, std::vector<unsigned int> &indices)
{
    vertices.clear();
    indices.clear();

    if (!root_)
    {
        std::cerr << "WARNING: PlanetOctree::extractGreedyMesh() - Root node is null!" << "\n";
        return;
    }

    // Collect all leaf nodes with voxel data
    std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> voxelNodes;
    collectVoxelData(voxelNodes);

    if (voxelNodes.empty())
    {
        std::cerr << "WARNING: PlanetOctree::extractGreedyMesh() - No voxel data found!" << "\n";
        return;
    }

    // Greedy mesh along each axis (X, Y, Z) - parallelize across axes
    // This generates faces only on exposed surfaces
    std::vector<std::vector<MeshVertex>> axisVertices(3);
    std::vector<std::vector<unsigned int>> axisIndices(3);
    std::vector<unsigned int> axisBaseIndices(3, 0);

    // Process each axis in parallel
    const unsigned int numThreads = std::min(3u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    
    for (int axis = 0; axis < 3; axis++)
    {
        threads.emplace_back([&, axis]() {
            greedyMeshAxis(voxelNodes, axis, axisVertices[axis], axisIndices[axis], axisBaseIndices[axis]);
        });
    }

    // Wait for all threads
    for (auto& thread : threads)
    {
        thread.join();
    }

    // Combine results from all axes
    for (int axis = 0; axis < 3; axis++)
    {
        // Adjust indices to account for previous vertices
        for (unsigned int& idx : axisIndices[axis])
        {
            idx += static_cast<unsigned int>(vertices.size());
        }
        
        vertices.insert(vertices.end(), axisVertices[axis].begin(), axisVertices[axis].end());
        indices.insert(indices.end(), axisIndices[axis].begin(), axisIndices[axis].end());
    }
}

void PlanetOctree::collectVoxelData(std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes) const
{
    voxelNodes.clear();
    collectVoxelDataRecursive(root_.get(), voxelNodes);
}

void PlanetOctree::collectVoxelDataRecursive(const OctreeNode *node,
                                             std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes) const
{
    if (!node)
    {
        return;
    }

    if (node->isLeaf && node->depth == maxDepth_ && !node->voxelGrid.empty())
    {
        // This is a leaf node at max depth with voxel grid
        // Store the node center and pointer to voxel grid for greedy meshing
        voxelNodes.push_back({node->center, &node->voxelGrid});
    }
    else if (!node->isLeaf)
    {
        // Recurse into children
        for (const auto &child : node->children)
        {
            if (child)
            {
                collectVoxelDataRecursive(child.get(), voxelNodes);
            }
        }
    }
}

void PlanetOctree::collectVoxelDataWithDistance(std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                                                const glm::vec3 &referencePoint,
                                                float maxDistance) const
{
    voxelNodes.clear();
    if (root_)
    {
        collectVoxelDataWithDistanceRecursive(root_.get(), voxelNodes, referencePoint, maxDistance);
    }
}

void PlanetOctree::collectVoxelDataWithDistanceRecursive(const OctreeNode *node,
                                                         std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                                                         const glm::vec3 &referencePoint,
                                                         float maxDistance) const
{
    if (!node)
    {
        return;
    }

    // Calculate distance from reference point to node center
    float distanceToNode = glm::length(node->center - referencePoint);
    
    // Calculate node bounding sphere radius (half diagonal of cube)
    float nodeRadius = node->size * 0.866f; // sqrt(3) * size/2 ≈ 0.866 * size
    
    // Check if node is within extraction distance (with some margin for node size)
    if (distanceToNode - nodeRadius > maxDistance)
    {
        // Node is completely outside extraction distance, skip it
        return;
    }

    if (node->isLeaf && !node->voxelGrid.empty())
    {
        // This is a leaf node with voxel grid
        // Only include if within distance
        if (distanceToNode <= maxDistance + nodeRadius)
        {
            voxelNodes.push_back({node->center, &node->voxelGrid});
        }
    }
    else if (!node->isLeaf)
    {
        // Recurse into children
        for (const auto &child : node->children)
        {
            if (child)
            {
                collectVoxelDataWithDistanceRecursive(child.get(), voxelNodes, referencePoint, maxDistance);
            }
        }
    }
}

void PlanetOctree::collectVoxelDataAtDepth(std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                                           int targetDepth) const
{
    voxelNodes.clear();
    if (root_)
    {
        collectVoxelDataAtDepthRecursive(root_.get(), voxelNodes, targetDepth);
    }
}

void PlanetOctree::collectVoxelDataAtDepthRecursive(const OctreeNode *node,
                                                    std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                                                    int targetDepth) const
{
    if (!node)
    {
        return;
    }

    if (node->depth == targetDepth && node->isLeaf && !node->voxelGrid.empty())
    {
        // This is a node at target depth with voxel grid
        voxelNodes.push_back({node->center, &node->voxelGrid});
    }
    else if (!node->isLeaf)
    {
        // Recurse into children
        for (const auto &child : node->children)
        {
            if (child)
            {
                collectVoxelDataAtDepthRecursive(child.get(), voxelNodes, targetDepth);
            }
        }
    }
}

bool PlanetOctree::isVoxelSolidBitwise(const std::vector<std::vector<uint32_t>> &voxelGrid, int x, int y, int z) const
{
    // Check bit at (x, y, z) position
    // voxelGrid[y][z] contains uint32_t with bits for x=0..31
    // Clamp coordinates to valid range
    if (y < 0 || y >= static_cast<int>(voxelGrid.size()))
    {
        return false;
    }
    if (z < 0 || z >= static_cast<int>(voxelGrid[y].size()))
    {
        return false;
    }
    if (x < 0 || x >= 32)
    {
        return false;
    }
    
    uint32_t rowBits = voxelGrid[y][z];
    return (rowBits & (1U << x)) != 0;
}

bool PlanetOctree::getNeighborVoxel(const std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                                     const glm::vec3 &pos,
                                     int axis,
                                     int direction,
                                     float voxelSize) const
{
    // Calculate neighbor position
    glm::vec3 neighborPos = pos;
    neighborPos[axis] += direction * voxelSize;

    // Find voxel node at neighbor position (within tolerance)
    const float tolerance = voxelSize * 0.1f;
    for (const auto &voxelNode : voxelNodes)
    {
        glm::vec3 diff = neighborPos - voxelNode.first;
        if (glm::length(diff) < tolerance)
        {
            // Found the node, now check if the specific voxel in the 32x32x32 grid is solid
            // Convert position relative to node center to grid coordinates
            const float nodeSize = voxelSize * 32.0f; // Node contains 32x32x32 voxels
            const float halfSize = nodeSize;
            glm::vec3 localPos = neighborPos - voxelNode.first;
            
            // Normalize to [0, 1] range
            glm::vec3 normalizedPos = (localPos + glm::vec3(halfSize)) / (nodeSize * 2.0f);
            normalizedPos = glm::clamp(normalizedPos, 0.0f, 1.0f);
            
            // Convert to grid coordinates (0-31)
            int gridX = static_cast<int>(normalizedPos.x * 32);
            int gridY = static_cast<int>(normalizedPos.y * 32);
            int gridZ = static_cast<int>(normalizedPos.z * 32);
            gridX = std::max(0, std::min(gridX, 31));
            gridY = std::max(0, std::min(gridY, 31));
            gridZ = std::max(0, std::min(gridZ, 31));
            
            return isVoxelSolidBitwise(*voxelNode.second, gridX, gridY, gridZ);
        }
    }
    
    // Neighbor not found, assume empty
    return false;
}

void PlanetOctree::greedyMeshAxis(const std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                                  int axis,
                                  std::vector<MeshVertex> &vertices,
                                  std::vector<unsigned int> &indices,
                                  unsigned int &baseIndex) const
{
    if (voxelNodes.empty())
    {
        return;
    }

    // Calculate voxel size from first node (assuming uniform size)
    float voxelSize = 0.0f;
    if (!voxelNodes.empty())
    {
        // Estimate voxel size from node spacing or use a fixed size based on depth
        // For now, use a heuristic: size decreases with depth
        // Each node contains 32x32x32 voxels
        voxelSize = root_->size / static_cast<float>(1 << maxDepth_) / 32.0f;
    }

    // For each voxel node, check each voxel in its 32x32x32 grid
    for (const auto &voxelNode : voxelNodes)
    {
        const glm::vec3 &nodeCenter = voxelNode.first;
        const std::vector<std::vector<uint32_t>> &voxelGrid = *voxelNode.second;
        const float nodeSize = voxelSize * 32.0f; // Node contains 32x32x32 voxels

        // Check each voxel in the 32x32x32 grid
        for (int z = 0; z < 32; z++)
        {
            for (int y = 0; y < 32; y++)
            {
                for (int x = 0; x < 32; x++)
                {
                    // Check if this voxel is solid using bitwise operation
                    if (!isVoxelSolidBitwise(voxelGrid, x, y, z))
                    {
                        continue; // Skip empty voxels
                    }

                    // Calculate voxel position
                    glm::vec3 offset;
                    offset.x = (x + 0.5f) * voxelSize - nodeSize;
                    offset.y = (y + 0.5f) * voxelSize - nodeSize;
                    offset.z = (z + 0.5f) * voxelSize - nodeSize;
                    glm::vec3 voxelPos = nodeCenter + offset;

                    // Check neighbors along the current axis
                    // Generate face if neighbor is empty
                    for (int direction = -1; direction <= 1; direction += 2)
                    {
                        glm::vec3 neighborPos = voxelPos;
                        neighborPos[axis] += direction * voxelSize * 0.5f;

                        // Check if neighbor is solid
                        bool neighborSolid = getNeighborVoxel(voxelNodes, neighborPos, axis, direction, voxelSize);

                        if (!neighborSolid)
                        {
                            // Generate quad face on this side
                            // Create quad perpendicular to the axis
                            glm::vec3 faceCenter = voxelPos;
                            faceCenter[axis] += direction * voxelSize * 0.5f;

                            // Calculate quad corners
                            int axis1 = (axis + 1) % 3;
                            int axis2 = (axis + 2) % 3;

                            glm::vec3 corners[4];
                            float halfQuadSize = voxelSize * 0.5f;

                            corners[0] = faceCenter;
                            corners[0][axis1] -= halfQuadSize;
                            corners[0][axis2] -= halfQuadSize;

                            corners[1] = faceCenter;
                            corners[1][axis1] += halfQuadSize;
                            corners[1][axis2] -= halfQuadSize;

                            corners[2] = faceCenter;
                            corners[2][axis1] += halfQuadSize;
                            corners[2][axis2] += halfQuadSize;

                            corners[3] = faceCenter;
                            corners[3][axis1] -= halfQuadSize;
                            corners[3][axis2] += halfQuadSize;

                            // Generate two triangles for the quad
                            MeshVertex vert0, vert1, vert2, vert3;
                            vert0.position = corners[0];
                            vert1.position = corners[1];
                            vert2.position = corners[2];
                            vert3.position = corners[3];

                            // Calculate normals (pointing outward from solid voxel)
                            glm::vec3 normal(0.0f);
                            normal[axis] = static_cast<float>(direction);
                            vert0.normal = normal;
                            vert1.normal = normal;
                            vert2.normal = normal;
                            vert3.normal = normal;

                            // Calculate UV coordinates (simple projection)
                            vert0.uv = glm::vec2(0.0f, 0.0f);
                            vert1.uv = glm::vec2(1.0f, 0.0f);
                            vert2.uv = glm::vec2(1.0f, 1.0f);
                            vert3.uv = glm::vec2(0.0f, 1.0f);

                            // Add vertices and indices
                            vertices.push_back(vert0);
                            vertices.push_back(vert1);
                            vertices.push_back(vert2);
                            vertices.push_back(vert3);

                            // First triangle
                            indices.push_back(baseIndex);
                            indices.push_back(baseIndex + 1);
                            indices.push_back(baseIndex + 2);

                            // Second triangle
                            indices.push_back(baseIndex);
                            indices.push_back(baseIndex + 2);
                            indices.push_back(baseIndex + 3);

                            baseIndex += 4;
                        }
                    }
                }
            }
        }
    }
}

} // namespace EarthVoxelOctree
