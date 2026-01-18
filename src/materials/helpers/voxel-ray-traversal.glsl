#version 330 core

// ============================================================
// Fast Voxel Ray Traversal (Amanatides-Woo DDA Algorithm)
// ============================================================
// Optimized for GLSL 3.30 core profile
// Efficiently traverses voxel grids using DDA (Digital Differential Analyzer)
// Matches Rust implementation logic for consistency

// ============================================================
// 1. Data Structures
// ============================================================

struct Ray {
    vec3 origin;
    vec3 dir;
    vec3 invDir;  // Pre-computed 1.0 / dir (handles infinity correctly)
};

struct VoxelHit {
    bool hit;
    vec3 position;
    ivec3 voxelCoord;
    vec3 normal;
    float distance;
    vec4 voxelData;
};

// ============================================================
// 2. Ray-AABB Intersection
// ============================================================
// Calculates entry (tMin) and exit (tMax) points of ray against AABB
// Returns true if ray intersects the bounding box

bool intersectAABB(Ray ray, vec3 boxMin, vec3 boxMax, out float tMin, out float tMax) {
    vec3 t1 = (boxMin - ray.origin) * ray.invDir;
    vec3 t2 = (boxMax - ray.origin) * ray.invDir;
    
    vec3 tNear = min(t1, t2);
    vec3 tFar = max(t1, t2);
    
    tMin = max(max(tNear.x, tNear.y), tNear.z);
    tMax = min(min(tFar.x, tFar.y), tFar.z);
    
    // Ray intersects if tMax >= tMin and tMax > 0 (box is in front of ray)
    return tMax >= tMin && tMax > 0.0;
}

// ============================================================
// 3. Fast Voxel Traversal (DDA Algorithm)
// ============================================================
// Traverses voxel grid efficiently without missing any voxels
// Returns hit information if a solid voxel is found

VoxelHit traceVoxels(Ray ray, sampler3D voxelTexture, vec3 gridMin, vec3 gridMax, ivec3 gridRes, float maxDistance) {
    VoxelHit result;
    result.hit = false;
    result.distance = maxDistance;
    result.voxelData = vec4(0.0);
    
    // First, check if ray intersects the grid's bounding box
    float tNear, tFar;
    if (!intersectAABB(ray, gridMin, gridMax, tNear, tFar)) {
        return result; // No intersection with grid
    }
    
    // Clamp to positive distances only
    float t = max(tNear, 0.0);
    if (t > maxDistance) {
        return result; // Entry point is beyond max distance
    }
    
    // Start the ray at the entry point of the AABB
    vec3 rayStart = ray.origin + ray.dir * t;
    
    // Map world position to integer voxel coordinates
    vec3 gridSize = gridMax - gridMin;
    vec3 pos = (rayStart - gridMin) / gridSize * vec3(gridRes);
    
    // Clamp to valid voxel coordinates (avoid out-of-bounds)
    ivec3 coord = ivec3(clamp(pos, vec3(0.0), vec3(gridRes) - 1.0));
    
    // DDA Setup
    // step: direction to step in each axis (-1, 0, or +1)
    ivec3 step = ivec3(sign(ray.dir));
    
    // deltaT: distance along ray to move one full voxel in each axis
    vec3 voxelSize = gridSize / vec3(gridRes);
    vec3 deltaT = abs(ray.invDir * voxelSize);
    
    // Calculate initial sideDist (distance to next voxel boundary)
    vec3 sideDist;
    vec3 voxelPos = (vec3(coord) / vec3(gridRes)) * gridSize + gridMin;
    
    // Calculate initial sideDist for each axis
    for (int i = 0; i < 3; i++) {
        if (ray.dir[i] > 0.0) {
            // Moving in positive direction: distance to next boundary
            float nextBoundary = voxelPos[i] + voxelSize[i];
            sideDist[i] = (nextBoundary - rayStart[i]) * ray.invDir[i];
        } else if (ray.dir[i] < 0.0) {
            // Moving in negative direction: distance to previous boundary
            sideDist[i] = (voxelPos[i] - rayStart[i]) * ray.invDir[i];
        } else {
            // Ray is parallel to this axis: set to infinity
            sideDist[i] = 1e30; // Large value instead of infinity for compatibility
        }
    }
    
    // Adjust sideDist to be relative to ray start (add initial t)
    sideDist += t;
    
    // Traversal Loop
    // Maximum steps: diagonal of grid (safety limit)
    int maxSteps = gridRes.x + gridRes.y + gridRes.z;
    maxSteps = min(maxSteps, 512); // Cap at reasonable limit
    
    for (int i = 0; i < maxSteps; i++) {
        // Check if we've left the grid bounds
        if (coord.x < 0 || coord.x >= gridRes.x ||
            coord.y < 0 || coord.y >= gridRes.y ||
            coord.z < 0 || coord.z >= gridRes.z) {
            break; // Exited grid
        }
        
        // Check current voxel using texelFetch (integer coordinates, no filtering)
        vec4 voxel = texelFetch(voxelTexture, coord, 0);
        
        // Check if voxel is solid (alpha > 0.5 or custom threshold)
        if (voxel.a > 0.5) {
            // Hit! Calculate hit information
            result.hit = true;
            result.voxelCoord = coord;
            result.voxelData = voxel;
            
            // Calculate hit position (back up to voxel surface)
            // Use the minimum sideDist to determine which face was hit
            float minDist = min(min(sideDist.x, sideDist.y), sideDist.z);
            result.distance = minDist;
            result.position = ray.origin + ray.dir * minDist;
            
            // Calculate surface normal (points away from the face that was hit)
            if (sideDist.x < sideDist.y && sideDist.x < sideDist.z) {
                result.normal = vec3(float(-step.x), 0.0, 0.0);
            } else if (sideDist.y < sideDist.z) {
                result.normal = vec3(0.0, float(-step.y), 0.0);
            } else {
                result.normal = vec3(0.0, 0.0, float(-step.z));
            }
            
            return result;
        }
        
        // Step to next voxel (choose axis with minimum sideDist)
        if (sideDist.x < sideDist.y) {
            if (sideDist.x < sideDist.z) {
                // Step in X direction
                sideDist.x += deltaT.x;
                coord.x += step.x;
            } else {
                // Step in Z direction
                sideDist.z += deltaT.z;
                coord.z += step.z;
            }
        } else {
            if (sideDist.y < sideDist.z) {
                // Step in Y direction
                sideDist.y += deltaT.y;
                coord.y += step.y;
            } else {
                // Step in Z direction
                sideDist.z += deltaT.z;
                coord.z += step.z;
            }
        }
        
        // Early exit if we've traveled beyond max distance
        float currentDist = min(min(sideDist.x, sideDist.y), sideDist.z);
        if (currentDist > maxDistance) {
            break;
        }
    }
    
    // No hit found
    return result;
}

// ============================================================
// 4. Helper Functions
// ============================================================

// Create a ray from origin and direction (pre-computes invDir)
Ray createRay(vec3 origin, vec3 direction) {
    Ray ray;
    ray.origin = origin;
    ray.dir = normalize(direction);
    ray.invDir = vec3(
        ray.dir.x != 0.0 ? 1.0 / ray.dir.x : 1e30,
        ray.dir.y != 0.0 ? 1.0 / ray.dir.y : 1e30,
        ray.dir.z != 0.0 ? 1.0 / ray.dir.z : 1e30
    );
    return ray;
}

// Check if a voxel coordinate is within grid bounds
bool isValidVoxelCoord(ivec3 coord, ivec3 gridRes) {
    return coord.x >= 0 && coord.x < gridRes.x &&
           coord.y >= 0 && coord.y < gridRes.y &&
           coord.z >= 0 && coord.z < gridRes.z;
}

// Sample voxel at integer coordinates (with bounds checking)
vec4 sampleVoxelSafe(sampler3D voxelTexture, ivec3 coord, ivec3 gridRes) {
    if (!isValidVoxelCoord(coord, gridRes)) {
        return vec4(0.0); // Out of bounds = air
    }
    return texelFetch(voxelTexture, coord, 0);
}

// ============================================================
// 5. Octree-Aware Traversal (for hierarchical voxel grids)
// ============================================================
// This version can be extended to traverse octree structures
// For now, it works with uniform 3D textures

// Traverse multiple voxel grids (for octree leaf nodes)
// Each grid has its own bounds and resolution
VoxelHit traceVoxelsMultiGrid(Ray ray, sampler3D voxelTexture, vec3 gridMin, vec3 gridMax, ivec3 gridRes, float maxDistance) {
    // For now, delegate to single grid traversal
    // In a full octree implementation, you'd:
    // 1. Traverse octree nodes to find leaf nodes
    // 2. For each leaf node, call traceVoxels with that node's bounds
    // 3. Return the closest hit
    
    return traceVoxels(ray, voxelTexture, gridMin, gridMax, gridRes, maxDistance);
}

// ============================================================
// 6. Surface Normal Calculation
// ============================================================
// Calculate surface normal at hit point using neighbor sampling
// This provides smoother normals than face normals

vec3 calculateVoxelNormal(ivec3 coord, sampler3D voxelTexture, ivec3 gridRes, float voxelSize) {
    // Sample 6 neighbors (one in each direction)
    vec3 normal = vec3(0.0);
    
    // X axis
    vec4 left = sampleVoxelSafe(voxelTexture, coord + ivec3(-1, 0, 0), gridRes);
    vec4 right = sampleVoxelSafe(voxelTexture, coord + ivec3(1, 0, 0), gridRes);
    normal.x = left.a - right.a;
    
    // Y axis
    vec4 bottom = sampleVoxelSafe(voxelTexture, coord + ivec3(0, -1, 0), gridRes);
    vec4 top = sampleVoxelSafe(voxelTexture, coord + ivec3(0, 1, 0), gridRes);
    normal.y = bottom.a - top.a;
    
    // Z axis
    vec4 back = sampleVoxelSafe(voxelTexture, coord + ivec3(0, 0, -1), gridRes);
    vec4 front = sampleVoxelSafe(voxelTexture, coord + ivec3(0, 0, 1), gridRes);
    normal.z = back.a - front.a;
    
    // Normalize
    float len = length(normal);
    if (len > 0.001) {
        return normalize(normal);
    }
    
    // Fallback: return up vector if gradient is too small
    return vec3(0.0, 1.0, 0.0);
}
