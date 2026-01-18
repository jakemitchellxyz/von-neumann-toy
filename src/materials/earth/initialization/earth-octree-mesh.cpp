// ============================================================================
// Octree Mesh Generation
// ============================================================================
// Generate voxel octree mesh from heightmap data

#include "../../../concerns/constants.h"
#include "../earth-material.h"
#include "../voxel-octree.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>


// stb_image for loading heightmap
#include <stb_image.h>

void EarthMaterial::generateOctreeMesh(float displayRadius, float maxRadius)
{
    if (meshGenerated_)
    {
        return; // Already generated
    }

    if (!elevationLoaded_ || heightmapTexture_ == 0)
    {
        std::cerr << "ERROR: EarthMaterial::generateOctreeMesh() - Heightmap not loaded!" << "\n";
        std::cerr << "  elevationLoaded_: " << elevationLoaded_ << "\n";
        std::cerr << "  heightmapTexture_: " << heightmapTexture_ << "\n";
        std::cerr << "  Octree voxel generation is required. Cannot continue." << "\n";
        std::exit(1);
    }

    std::cout << "  Building octree voxels..." << "\n";

    // Load heightmap data from disk (needed for octree construction)
    // Use stored texture base path
    std::string heightmapPath = textureBasePath_ + "/earth_landmass_heightmap.png";

    if (!std::filesystem::exists(heightmapPath))
    {
        std::cerr << "ERROR: EarthMaterial::generateOctreeMesh() - Heightmap file not found!" << "\n";
        std::cerr << "  Expected path: " << heightmapPath << "\n";
        std::cerr << "  Octree mesh generation is required. Cannot continue." << "\n";
        std::exit(1);
    }

    // Load heightmap image
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char *heightmapData = stbi_load(heightmapPath.c_str(), &width, &height, &channels, 1); // Force grayscale

    if (!heightmapData)
    {
        std::cerr << "ERROR: EarthMaterial::generateOctreeMesh() - Failed to load heightmap data!" << "\n";
        std::cerr << "  File path: " << heightmapPath << "\n";
        std::cerr << "  Octree mesh generation is required. Cannot continue." << "\n";
        std::exit(1);
    }

    // Load landmass mask (for determining ocean vs land)
    std::string landmassMaskPath = textureBasePath_ + "/earth_landmass_mask.png";
    unsigned char *landmassMaskData = nullptr;
    int maskWidth = 0, maskHeight = 0, maskChannels = 0;

    if (std::filesystem::exists(landmassMaskPath))
    {
        landmassMaskData = stbi_load(landmassMaskPath.c_str(), &maskWidth, &maskHeight, &maskChannels, 1);
    }

    // Create octree with spherical bounding volume
    // baseRadius: Earth's average radius (in display units)
    // maxRadius: Exosphere radius (spherical bounding volume)
    const float EARTH_RADIUS_KM = static_cast<float>(RADIUS_EARTH_KM);
    const float EXOSPHERE_HEIGHT_KM = 10000.0f; // 10,000 km exosphere
    float baseRadiusDisplay = displayRadius;
    float maxRadiusDisplay = displayRadius * (1.0f + EXOSPHERE_HEIGHT_KM / EARTH_RADIUS_KM);

    // Create octree with base size 4 and max depth 2 (reduced for lower base resolution)
    // Each leaf node stores a 32x32x32 voxel grid (4 KB per node)
    // Base size 4 means 4^3 = 64 children per node
    // Max depth 2 gives us up to 4^2 = 16 leaf nodes per branch (lower base resolution)
    // Maximum memory: reduced for better performance
    // Higher detail is achieved through proximity-based subdivision at render time
    const int MAX_DEPTH = 2;

    // Check for cached octree file first
    std::string cachePath = textureBasePath_ + "/earth_octree_cache.bin";
    bool cacheExists = std::filesystem::exists(cachePath);

    // Check if cache exists and has the correct maxDepth
    bool cacheValid = false;
    if (cacheExists)
    {
        // Read the maxDepth from cache file to check if it matches
        std::ifstream cacheCheck(cachePath, std::ios::binary);
        if (cacheCheck.is_open())
        {
            uint32_t version = 0;
            float cachedBaseRadius = 0.0f;
            float cachedMaxRadius = 0.0f;
            int cachedMaxDepth = 0;

            cacheCheck.read(reinterpret_cast<char *>(&version), sizeof(uint32_t));
            if (version == 1)
            {
                cacheCheck.read(reinterpret_cast<char *>(&cachedBaseRadius), sizeof(float));
                cacheCheck.read(reinterpret_cast<char *>(&cachedMaxRadius), sizeof(float));
                cacheCheck.read(reinterpret_cast<char *>(&cachedMaxDepth), sizeof(int));

                // Check if cached maxDepth matches desired MAX_DEPTH
                if (cachedMaxDepth == MAX_DEPTH)
                {
                    cacheValid = true;
                }
                else
                {
                    std::cout << "  Cache has maxDepth=" << cachedMaxDepth << ", but we need MAX_DEPTH=" << MAX_DEPTH
                              << ", will rebuild..." << "\n";
                }
            }
            cacheCheck.close();
        }
    }

    octreeMesh_ = std::make_unique<EarthVoxelOctree::PlanetOctree>(baseRadiusDisplay, maxRadiusDisplay, MAX_DEPTH);

    if (cacheValid)
    {
        // Try to load from cache
        std::cout << "  Loading octree from cache: " << cachePath << "\n";
        if (octreeMesh_->deserializeFromFile(cachePath))
        {
            size_t voxelDataSize = octreeMesh_->getVoxelDataSize();
            std::cout << "  Octree voxels: loaded from cache (" << voxelDataSize << " bytes of voxel data)" << "\n";
            meshGenerated_ = true;

            // Free loaded image data (not needed if loaded from cache)
            stbi_image_free(heightmapData);
            if (landmassMaskData)
            {
                stbi_image_free(landmassMaskData);
            }
            return;
        }
        else
        {
            std::cout << "  Cache load failed, rebuilding octree..." << "\n";
        }
    }
    else if (cacheExists)
    {
        std::cout << "  Cache exists but has wrong maxDepth, rebuilding octree..." << "\n";
    }

    // Build octree from heightmap (stores voxels as bits, no mesh generation)
    octreeMesh_->buildFromHeightmap(heightmapData, width, height, landmassMaskData, baseRadiusDisplay);

    // Skip mesh generation - we're using voxels directly now
    // The octree now stores voxels as bits (1 bit per voxel) for high-resolution grids
    // Mesh generation is skipped to allow for many layers of subdivision

    // Get voxel data size for reporting
    size_t voxelDataSize = octreeMesh_->getVoxelDataSize();
    std::cout << "  Octree voxels: built (" << voxelDataSize << " bytes of voxel data)" << "\n";

    // Save to cache for fast loading next time
    std::cout << "  Saving octree to cache: " << cachePath << "\n";
    if (octreeMesh_->serializeToFile(cachePath))
    {
        std::cout << "  Cache saved successfully" << "\n";
    }
    else
    {
        std::cerr << "  WARNING: Failed to save cache file" << "\n";
    }

    // Free loaded image data
    stbi_image_free(heightmapData);
    if (landmassMaskData)
    {
        stbi_image_free(landmassMaskData);
    }

    meshGenerated_ = true; // Mark as generated (even though we're not generating a mesh)
}

void EarthMaterial::updateOctreeMeshForProximity(const glm::vec3 &cameraPosWorld,
                                                 const glm::vec3 &planetPosition,
                                                 float displayRadius,
                                                 float maxSubdivisionDistance)
{
    if (!octreeMesh_)
    {
        // Can't update if octree hasn't been built yet
        return;
    }

    // Convert camera position from world space to local space (relative to planet center)
    // The octree is built in local space with planet center at origin
    glm::vec3 cameraPosLocal = cameraPosWorld - planetPosition;

    // Subdivide nodes near the camera for higher resolution voxels
    // This updates the octree structure and stores new voxel bits
    // Limit nodes processed per frame for chunked processing (prevents frame drops)
    const int MAX_NODES_PER_FRAME = 100; // Process up to 100 nodes per frame
    octreeMesh_->subdivideForProximity(cameraPosLocal, maxSubdivisionDistance, MAX_NODES_PER_FRAME);
}
