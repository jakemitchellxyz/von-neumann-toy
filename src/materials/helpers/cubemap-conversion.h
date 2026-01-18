#pragma once

// ============================================================================
// Shared Cubemap Conversion Utilities
// ============================================================================
// Functions for converting between equirectangular and cubemap formats.
// Used by both skybox and earth texture preprocessing.
//
// Cubemap format: 3x2 grid layout for better cache coherency
// Layout:
//   +X  -X  +Y   (row 0)
//   -Y  +Z  -Z   (row 1)
// Face order: +X, -X, +Y, -Y, +Z, -Z (matches Vulkan VK_IMAGE_VIEW_TYPE_CUBE)
// Grid dimensions: width = faceSize * 3, height = faceSize * 2

#include <cstddef>

// Cubemap face indices (matches Vulkan VK_IMAGE_VIEW_TYPE_CUBE order)
enum CubemapFace
{
    FACE_POSITIVE_X = 0, // Right  - grid position (0, 0)
    FACE_NEGATIVE_X = 1, // Left   - grid position (1, 0)
    FACE_POSITIVE_Y = 2, // Top    - grid position (2, 0)
    FACE_NEGATIVE_Y = 3, // Bottom - grid position (0, 1)
    FACE_POSITIVE_Z = 4, // Front  - grid position (1, 1)
    FACE_NEGATIVE_Z = 5  // Back   - grid position (2, 1)
};

// Get the grid position (column, row) for a cubemap face in 3x2 layout
inline void getCubemapFaceGridPosition(int face, int &col, int &row)
{
    // Layout: +X -X +Y (row 0), -Y +Z -Z (row 1)
    static const int faceCol[6] = {0, 1, 2, 0, 1, 2};
    static const int faceRow[6] = {0, 0, 0, 1, 1, 1};
    col = faceCol[face];
    row = faceRow[face];
}

// Convert cubemap face pixel coordinates to 3D direction vector
// face: cubemap face index (0-5)
// x, y: pixel coordinates within the face
// faceSize: size of each face in pixels
// dirX, dirY, dirZ: output normalized direction vector
void cubemapPixelToDirection(int face, int x, int y, int faceSize, float &dirX, float &dirY, float &dirZ);

// Convert 3D direction to equirectangular UV coordinates
// Uses standard geographic convention: Y=up (north), XZ=equatorial plane
// Longitude (U) measured from +Z direction towards +X (matches GLSL atan(x,z))
// dirX, dirY, dirZ: input direction vector (will be normalized internally)
// u, v: output UV coordinates in [0, 1] range
//       u=0.5 corresponds to +Z direction, u=0 and u=1 wrap at -Z direction
//       v=0 is north pole (Y=+1), v=1 is south pole (Y=-1)
void directionToEquirectangularUV(float dirX, float dirY, float dirZ, float &u, float &v);

// Convert equirectangular UV coordinates to 3D direction (inverse of above)
// u, v: UV coordinates in [0, 1] range
// dirX, dirY, dirZ: output normalized direction vector
void equirectangularUVToDirection(float u, float v, float &dirX, float &dirY, float &dirZ);

// Get cubemap face and face UV from a 3D direction
// dir: input direction vector (does not need to be normalized)
// face: output face index (0-5)
// faceU, faceV: output UV coordinates within the face [0, 1]
void directionToCubemapFaceUV(float dirX, float dirY, float dirZ, int &face, float &faceU, float &faceV);

// Sample equirectangular image with bilinear interpolation (unsigned char version)
// src: source image data (equirectangular projection)
// srcW, srcH: source image dimensions
// channels: number of color channels (1, 3, or 4)
// u, v: UV coordinates in [0, 1] range
// outColor: output color values (array of size channels)
void sampleEquirectangularUChar(const unsigned char *src,
                                int srcW,
                                int srcH,
                                int channels,
                                float u,
                                float v,
                                unsigned char *outColor);

// Sample equirectangular image with bilinear interpolation (float version)
// Same parameters as above but for HDR float data
void sampleEquirectangularFloat(const float *src,
                                int srcW,
                                int srcH,
                                int channels,
                                float u,
                                float v,
                                float *outColor);

// Sample cubemap grid image with bilinear interpolation (unsigned char version)
// Uses 3D direction to determine face and UV coordinates
// cubemapData: source cubemap grid data (3x2 layout)
// faceSize: size of each face in pixels
// channels: number of color channels (1, 3, or 4)
// dirX, dirY, dirZ: direction vector to sample
// outColor: output color values (array of size channels)
void sampleCubemapGridUChar(const unsigned char *cubemapData,
                            int faceSize,
                            int channels,
                            float dirX,
                            float dirY,
                            float dirZ,
                            unsigned char *outColor);

// Legacy alias for backward compatibility (deprecated - use sampleCubemapGridUChar)
void sampleCubemapStripUChar(const unsigned char *cubemapData,
                             int faceSize,
                             int channels,
                             float dirX,
                             float dirY,
                             float dirZ,
                             unsigned char *outColor);

// Convert equirectangular unsigned char image to cubemap format (3x2 grid)
// Returns cubemap data as unsigned char array: (faceSize * 3) x (faceSize * 2) x channels
// Faces are arranged in 3x2 grid: +X -X +Y (row 0), -Y +Z -Z (row 1)
// Caller must delete[] the returned array
// Returns nullptr on allocation failure
unsigned char *convertEquirectangularToCubemapUChar(const unsigned char *equirectData,
                                                    int equirectW,
                                                    int equirectH,
                                                    int channels,
                                                    int faceSize);

// Convert equirectangular HDR image to cubemap format (3x2 grid)
// Returns cubemap data as float array: (faceSize * 3) x (faceSize * 2) x channels
// Faces are arranged in 3x2 grid: +X -X +Y (row 0), -Y +Z -Z (row 1)
// Caller must delete[] the returned array
// Returns nullptr on allocation failure
float *convertEquirectangularToCubemapFloat(const float *equirectData,
                                            int equirectW,
                                            int equirectH,
                                            int channels,
                                            int faceSize);

// Convert cubemap grid image to equirectangular format (unsigned char version)
// This is the inverse of convertEquirectangularToCubemapUChar
// cubemapData: source cubemap grid data (3x2 layout)
// faceSize: size of each face in pixels
// channels: number of color channels (1, 3, or 4)
// equirectW, equirectH: output equirectangular dimensions
// Returns equirectangular data as unsigned char array
// Caller must delete[] the returned array
// Returns nullptr on allocation failure
unsigned char *convertCubemapToEquirectangularUChar(const unsigned char *cubemapData,
                                                    int faceSize,
                                                    int channels,
                                                    int equirectW,
                                                    int equirectH);

// Helper to calculate recommended face size for a given equirectangular image
// For 2:1 aspect ratio images, returns height/2 which gives good quality
inline int calculateCubemapFaceSize(int equirectWidth, int equirectHeight)
{
    // For equirectangular 2:1 aspect ratio, face size = height/2 gives good quality
    // This ensures each face has roughly the same pixel density as the source
    (void)equirectWidth; // Unused, but kept for API clarity
    return equirectHeight / 2;
}

// Get cubemap grid dimensions from face size (3x2 layout)
// gridWidth = faceSize * 3
// gridHeight = faceSize * 2
inline void getCubemapGridDimensions(int faceSize, int &gridWidth, int &gridHeight)
{
    gridWidth = faceSize * 3;
    gridHeight = faceSize * 2;
}

// Check if image dimensions indicate a cubemap 3x2 grid (width = 1.5 * height, width divisible by 3)
inline bool isCubemapGridDimensions(int width, int height)
{
    return (width == height * 3 / 2) && (width % 3 == 0);
}

// Get face size from cubemap grid dimensions
inline int getFaceSizeFromGridDimensions(int gridWidth, int gridHeight)
{
    (void)gridHeight; // Unused, but kept for API clarity
    return gridWidth / 3;
}

// Legacy aliases for backward compatibility (deprecated - use Grid versions)
inline void getCubemapStripDimensions(int faceSize, int &width, int &height)
{
    getCubemapGridDimensions(faceSize, width, height);
}

inline bool isCubemapStripDimensions(int width, int height)
{
    return isCubemapGridDimensions(width, height);
}

inline int getFaceSizeFromStripDimensions(int width, int height)
{
    return getFaceSizeFromGridDimensions(width, height);
}
