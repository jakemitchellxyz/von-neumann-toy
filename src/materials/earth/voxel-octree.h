#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// Voxel Octree for Planet Surface Mesh
// ============================================================================
// An octree representation of a voxelized planet, used to generate a surface
// mesh that replaces the tessellated sphere. The octree is built from the
// heightmap, determining voxel occupancy based on whether points are above or
// below the average radius (with heightmap offsets).

namespace EarthVoxelOctree
{

// ============================================================================
// Z-Curve (Morton Order) Encoding/Decoding
// ============================================================================
// Space-filling curve that maps 3D coordinates to linear indices while
// preserving spatial locality. This improves cache performance for voxel queries.
//
// Benefits:
// - Nearby voxels in 3D space are stored near each other in memory
// - Reduces cache misses when querying spatially coherent regions
// - Enables efficient traversal of nearby voxels (e.g., ray marching)
// - Scales well to larger grids (4x4x4, 8x8x8, etc.) for higher resolution
//
// The Z-curve interleaves bits: zyx zyx zyx... (z in MSB, x in LSB)
// For a 2x2x2 grid, coordinates map as:
//   (0,0,0) -> 0, (1,0,0) -> 1, (0,1,0) -> 2, (1,1,0) -> 3,
//   (0,0,1) -> 4, (1,0,1) -> 5, (0,1,1) -> 6, (1,1,1) -> 7

// Encode 3D coordinates to Morton (Z-order) index
// Interleaves bits: zyx zyx zyx... (z in MSB, x in LSB)
inline uint32_t mortonEncode3D(uint32_t x, uint32_t y, uint32_t z)
{
    // Spread bits: x -> ...x...x...x, y -> ...y...y...y, z -> ...z...z...z
    // Then interleave: zyx zyx zyx...
    
    // For 10 bits per coordinate (supports up to 1024x1024x1024 grid)
    x &= 0x3FF; // 10 bits
    y &= 0x3FF;
    z &= 0x3FF;
    
    // Spread bits using bit manipulation
    x = (x | (x << 16)) & 0x030000FF;
    x = (x | (x << 8)) & 0x0300F00F;
    x = (x | (x << 4)) & 0x030C30C3;
    x = (x | (x << 2)) & 0x09249249;
    
    y = (y | (y << 16)) & 0x030000FF;
    y = (y | (y << 8)) & 0x0300F00F;
    y = (y | (y << 4)) & 0x030C30C3;
    y = (y | (y << 2)) & 0x09249249;
    
    z = (z | (z << 16)) & 0x030000FF;
    z = (z | (z << 8)) & 0x0300F00F;
    z = (z | (z << 4)) & 0x030C30C3;
    z = (z | (z << 2)) & 0x09249249;
    
    // Interleave: zyx zyx zyx...
    return (z << 2) | (y << 1) | x;
}

// Decode Morton (Z-order) index to 3D coordinates
inline void mortonDecode3D(uint32_t morton, uint32_t &x, uint32_t &y, uint32_t &z)
{
    // Extract and compact bits
    x = morton & 0x09249249;
    x = (x | (x >> 2)) & 0x030C30C3;
    x = (x | (x >> 4)) & 0x0300F00F;
    x = (x | (x >> 8)) & 0x030000FF;
    x = (x | (x >> 16)) & 0x000003FF;
    
    y = (morton >> 1) & 0x09249249;
    y = (y | (y >> 2)) & 0x030C30C3;
    y = (y | (y >> 4)) & 0x0300F00F;
    y = (y | (y >> 8)) & 0x030000FF;
    y = (y | (y >> 16)) & 0x000003FF;
    
    z = (morton >> 2) & 0x09249249;
    z = (z | (z >> 2)) & 0x030C30C3;
    z = (z | (z >> 4)) & 0x0300F00F;
    z = (z | (z >> 8)) & 0x030000FF;
    z = (z | (z >> 16)) & 0x000003FF;
}

// Convert 3D grid coordinates to Morton index (for 2x2x2, 4x4x4, 8x8x8 grids)
// gridSize: size of one dimension (2, 4, 8, etc.)
inline uint32_t gridToMorton(int x, int y, int z, int gridSize)
{
    // Clamp coordinates to valid range
    x = std::max(0, std::min(x, gridSize - 1));
    y = std::max(0, std::min(y, gridSize - 1));
    z = std::max(0, std::min(z, gridSize - 1));
    
    return mortonEncode3D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z));
}

// Convert Morton index to 3D grid coordinates
inline void mortonToGrid(uint32_t morton, int &x, int &y, int &z)
{
    uint32_t xu, yu, zu;
    mortonDecode3D(morton, xu, yu, zu);
    x = static_cast<int>(xu);
    y = static_cast<int>(yu);
    z = static_cast<int>(zu);
}

// Octree node structure
struct OctreeNode
{
    glm::vec3 center;                                    // Center of this node's bounding box
    float size;                                          // Size of this node's bounding box (half-extent)
    int depth;                                           // Depth in the octree (0 = root)
    bool isLeaf;                                         // True if this is a leaf node
    bool isSolid;                                        // True if this voxel is solid (inside planet)
    std::array<std::unique_ptr<OctreeNode>, 8> children; // 8 child nodes
    
    // Bit-packed voxel storage for leaf nodes at max depth
    // 32x32x32 voxel grid stored as 2D array: 32 rows × 32 uint32_t per row
    // Each uint32_t represents 32 voxels in a row (1 bit per voxel)
    // Total: 32 × 32 × 4 bytes = 4 KB per leaf node
    // Voxels are stored row by row: row[y][z] contains 32 bits for x=0..31
    std::vector<std::vector<uint32_t>> voxelGrid; // 2D array: [32 rows][32 uint32_t] = 4 KB

    OctreeNode(const glm::vec3 &center_, float size_, int depth_)
        : center(center_), size(size_), depth(depth_), isLeaf(true), isSolid(false)
    {
        // Initialize all children to nullptr (unique_ptr is move-only, can't use fill())
        for (int i = 0; i < 8; i++)
        {
            children[i] = nullptr;
        }
    }
};

// Surface mesh vertex
struct MeshVertex
{
    glm::vec3 position; // Local space position (relative to planet center at origin)
    glm::vec3 normal;   // Surface normal (in local space)
    glm::vec2 uv;       // Texture coordinates (equirectangular, computed from local position)

    bool operator==(const MeshVertex &other) const
    {
        const float EPSILON = 0.001f;
        return glm::length(position - other.position) < EPSILON;
    }
};

// Edge vertex for chunk stitching
// Tracks vertices on chunk boundaries for lookup table
struct EdgeVertex
{
    glm::vec3 position;       // Local space position (relative to planet center at origin)
    unsigned int vertexIndex; // Index in the chunk's vertex array
    int chunkX, chunkY;       // Chunk coordinates
    int edgeSide;             // Which side of chunk (0=-X, 1=+X, 2=-Y, 3=+Y, 4=-Z, 5=+Z)
};

// Chunk mesh data
struct ChunkMesh
{
    std::vector<MeshVertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<EdgeVertex> edgeVertices; // Vertices on chunk boundaries
    int chunkX, chunkY;                   // Chunk coordinates
    bool isValid;
};

// Octree for planet voxelization
// Uses spherical bounding volume optimized for planet surface mesh generation
class PlanetOctree
{
public:
    // Constructor
    // baseRadius: Earth's average radius (used as base for heightmap offsets)
    // maxRadius: Maximum radius including atmosphere/exosphere (spherical bounding volume)
    // maxDepth: Maximum octree depth (controls mesh resolution)
    PlanetOctree(float baseRadius, float maxRadius, int maxDepth = 6);
    ~PlanetOctree();

    // Build the octree from heightmap data
    // heightmapData: grayscale heightmap (0-255, sinusoidal projection)
    // heightmapWidth, heightmapHeight: dimensions of heightmap
    // landmassMask: binary mask (255=land, 0=ocean, sinusoidal projection)
    // averageRadius: average radius from center (used as base for heightmap offsets)
    void buildFromHeightmap(const unsigned char *heightmapData,
                            int heightmapWidth,
                            int heightmapHeight,
                            const unsigned char *landmassMask,
                            float averageRadius);

    // Extract surface mesh from the octree using greedy meshing with bitwise operations
    // Uses binary voxel data and bitwise operations for efficient mesh generation
    // Returns vertices and indices for rendering
    void extractSurfaceMesh(std::vector<MeshVertex> &vertices, std::vector<unsigned int> &indices);

    // Extract surface mesh using greedy meshing algorithm
    // Greedily combines adjacent voxels into quads for optimal mesh generation
    void extractGreedyMesh(std::vector<MeshVertex> &vertices, std::vector<unsigned int> &indices);

    // Extract surface mesh with proximity-based subdivision
    // Subdivides nodes near referencePoint before extracting mesh
    // referencePoint: World position in local space (relative to planet center)
    // maxSubdivisionDistance: Maximum distance from reference point to subdivide
    void extractSurfaceMesh(const glm::vec3 &referencePoint,
                            float maxSubdivisionDistance,
                            std::vector<MeshVertex> &vertices,
                            std::vector<unsigned int> &indices);

    // Chunked mesh generation with parallel processing
    // Divides planet surface into chunks, processes in parallel, and stitches together
    // numChunksX, numChunksY: Number of chunks in each direction (e.g., 8x4 = 32 chunks)
    void extractChunkedSurfaceMesh(std::vector<MeshVertex> &vertices,
                                   std::vector<unsigned int> &indices,
                                   int numChunksX = 8,
                                   int numChunksY = 4);

    // Chunked mesh generation with proximity-based subdivision
    // Subdivides nodes near referencePoint before extracting mesh
    void extractChunkedSurfaceMesh(const glm::vec3 &referencePoint,
                                   float maxSubdivisionDistance,
                                   std::vector<MeshVertex> &vertices,
                                   std::vector<unsigned int> &indices,
                                   int numChunksX = 8,
                                   int numChunksY = 4);

    // Get the root node (for debugging/inspection)
    const OctreeNode *getRoot() const
    {
        return root_.get();
    }

    // Debug: Extract voxel wireframe edges (for visualization)
    // Returns edges as pairs of vertices representing cube edges
    void extractVoxelWireframes(std::vector<glm::vec3> &edgeVertices) const;

    // Proximity-based dynamic subdivision
    // Subdivides nodes near the reference point to increase resolution
    // referencePoint: World position (typically camera position) in local space (relative to planet center)
    // maxSubdivisionDistance: Maximum distance from reference point to subdivide (in display units)
    //   Nodes closer than this distance will be subdivided if not already at max depth
    // maxNodesToProcess: Maximum number of nodes to process this frame (for chunked processing, -1 = unlimited)
    void subdivideForProximity(const glm::vec3 &referencePoint, float maxSubdivisionDistance, int maxNodesToProcess = -1);
    
    // Helper: Collect nodes that need subdivision (for parallel processing)
    void collectNodesForSubdivision(OctreeNode *node,
                                    const glm::vec3 &referencePoint,
                                    float maxSubdivisionDistance,
                                    std::vector<OctreeNode*> &nodesToSubdivide,
                                    int maxNodesToProcess);
    
    // Helper: Subdivide a single node (thread-safe, called in parallel)
    void subdivideNode(OctreeNode *node, const glm::vec3 &referencePoint, float maxSubdivisionDistance);

    // Query voxel at a specific position
    // Returns true if voxel is solid (inside planet), false if empty
    // pos: World position in local space (relative to planet center)
    bool queryVoxel(const glm::vec3 &pos) const;

    // Get voxel data size (for debugging/monitoring)
    size_t getVoxelDataSize() const;

    // Serialize octree to binary file for fast loading
    // filepath: Path to save the octree file
    // Returns true on success
    bool serializeToFile(const std::string &filepath) const;

    // Deserialize octree from binary file
    // filepath: Path to load the octree file from
    // Returns true on success
    bool deserializeFromFile(const std::string &filepath);

private:
    std::unique_ptr<OctreeNode> root_;
    float baseRadius_; // Earth's average radius
    float maxRadius_;  // Spherical bounding volume radius (exosphere)
    int maxDepth_;

    // Heightmap data (cached during build)
    const unsigned char *heightmapData_;
    int heightmapWidth_;
    int heightmapHeight_;
    const unsigned char *landmassMask_;
    float averageRadius_;

    // Check if a node intersects the spherical bounding volume
    // Returns true if the node's bounding box intersects the sphere
    bool nodeIntersectsSphere(const glm::vec3 &nodeCenter, float nodeSize) const;

    // Sample heightmap at a given world position
    // Returns height offset from average radius in meters
    float sampleHeightmap(const glm::vec3 &worldPos) const;

    // Determine if a voxel at the given position is solid (inside planet)
    // Uses heightmap to determine if the point is below the surface
    bool isVoxelSolid(const glm::vec3 &voxelCenter, float voxelSize) const;

    // Get surface radius at a given position (baseRadius + heightmap offset)
    float getSurfaceRadius(const glm::vec3 &worldPos) const;

    // Recursively build octree (spherical bounds optimization)
    void buildOctreeRecursive(OctreeNode *node);

    // Recursively subdivide nodes based on proximity to reference point
    // Subdivides nodes that are close to referencePoint and not at max depth
    void subdivideForProximityRecursive(OctreeNode *node,
                                        const glm::vec3 &referencePoint,
                                        float maxSubdivisionDistance);

    // Extract mesh from octree node using optimized marching cubes for spheres
    void extractMeshFromNode(const OctreeNode *node,
                             std::vector<MeshVertex> &vertices,
                             std::vector<unsigned int> &indices,
                             unsigned int &baseIndex) const;

    // Marching cubes: sample density at cube corner
    // Returns: < 0 if inside planet, > 0 if outside, 0 at surface
    float sampleDensity(const glm::vec3 &pos) const;

    // Calculate surface normal from density gradient (proper method for Marching Cubes)
    // Uses finite differences to approximate the gradient of the density function
    glm::vec3 calculateSurfaceNormal(const glm::vec3 &pos, float epsilon) const;

    // Generate triangles for a cube using marching cubes lookup table
    void generateTrianglesForCube(const glm::vec3 &cubeCenter,
                                  float cubeSize,
                                  std::vector<MeshVertex> &vertices,
                                  std::vector<unsigned int> &indices,
                                  unsigned int &baseIndex) const;

    // Convert world position to equirectangular UV coordinates
    glm::vec2 worldToEquirectUV(const glm::vec3 &worldPos, const glm::vec3 &poleDir, const glm::vec3 &primeDir) const;

    // Chunked mesh generation helpers
    // Generate mesh for a single chunk
    ChunkMesh generateChunkMesh(int chunkX, int chunkY, int numChunksX, int numChunksY) const;

    // Extract mesh from octree node within a chunk boundary
    void extractMeshFromNodeChunked(const OctreeNode *node,
                                    const glm::vec3 &chunkMin,
                                    const glm::vec3 &chunkMax,
                                    std::vector<MeshVertex> &vertices,
                                    std::vector<unsigned int> &indices,
                                    std::vector<EdgeVertex> &edgeVertices,
                                    int chunkX,
                                    int chunkY,
                                    unsigned int &baseIndex) const;

    // Check if a position is on a chunk edge
    bool isOnChunkEdge(const glm::vec3 &pos, const glm::vec3 &chunkMin, const glm::vec3 &chunkMax, float epsilon) const;

    // Stitch chunks together using lookup tables
    void stitchChunks(const std::vector<ChunkMesh> &chunks,
                      std::vector<MeshVertex> &finalVertices,
                      std::vector<unsigned int> &finalIndices) const;

    // Helper to extract wireframes from a node
    void extractVoxelWireframesFromNode(const OctreeNode *node, std::vector<glm::vec3> &edgeVertices) const;

    // Store voxel bits for a leaf node at max depth
    // For a 2x2x2 subgrid, stores 8 bits (1 byte) representing voxel occupancy
    void storeVoxelBits(OctreeNode *node);

    // Query voxel bits from a leaf node
    // Returns true if voxel at local position within node is solid
    bool queryVoxelBits(const OctreeNode *node, const glm::vec3 &localPos) const;

    // Recursively query voxel at position
    bool queryVoxelRecursive(const OctreeNode *node, const glm::vec3 &pos) const;

    // Greedy meshing helpers
    // Collect all leaf node voxel data for greedy meshing
    void collectVoxelData(std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes) const;
    void collectVoxelDataRecursive(const OctreeNode *node, std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes) const;
    
    // Collect voxel data with distance filtering - only nodes within maxDistance
    void collectVoxelDataWithDistance(std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                                      const glm::vec3 &referencePoint,
                                      float maxDistance) const;
    void collectVoxelDataWithDistanceRecursive(const OctreeNode *node,
                                                std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                                                const glm::vec3 &referencePoint,
                                                float maxDistance) const;
    
    // Collect voxel data only at a specific depth (for low-resolution base mesh)
    void collectVoxelDataAtDepth(std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                                 int targetDepth) const;
    void collectVoxelDataAtDepthRecursive(const OctreeNode *node,
                                         std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                                         int targetDepth) const;
    
    // Greedy mesh generation for a single axis
    // axis: 0=X, 1=Y, 2=Z
    void greedyMeshAxis(const std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                       int axis,
                       std::vector<MeshVertex> &vertices,
                       std::vector<unsigned int> &indices,
                       unsigned int &baseIndex) const;
    
    // Check if a voxel is solid using bitwise operations
    // x, y, z: grid coordinates (0-31)
    bool isVoxelSolidBitwise(const std::vector<std::vector<uint32_t>> &voxelGrid, int x, int y, int z) const;
    
    // Get neighbor voxel state for greedy meshing
    bool getNeighborVoxel(const std::vector<std::pair<glm::vec3, const std::vector<std::vector<uint32_t>>*>> &voxelNodes,
                          const glm::vec3 &pos,
                          int axis,
                          int direction,
                          float voxelSize) const;

    // Serialization helpers
    void serializeNode(std::ostream &out, const OctreeNode *node) const;
    void deserializeNode(std::istream &in, OctreeNode *node);
};

} // namespace EarthVoxelOctree
