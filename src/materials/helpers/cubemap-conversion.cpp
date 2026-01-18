// ============================================================================
// Shared Cubemap Conversion Utilities - Implementation
// ============================================================================

#include "cubemap-conversion.h"
#include "../../concerns/constants.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <new>

// Convert cubemap face pixel coordinates to 3D direction vector
void cubemapPixelToDirection(int face, int x, int y, int faceSize, float &dirX, float &dirY, float &dirZ)
{
    // Map pixel coordinates to [-1, 1] range
    // Add 0.5 to sample at pixel center
    float u = (2.0f * (x + 0.5f) / faceSize) - 1.0f;
    float v = (2.0f * (y + 0.5f) / faceSize) - 1.0f;

    // Convert to 3D direction based on face
    // Note: v is inverted for some faces to match texture coordinate conventions
    switch (face)
    {
    case FACE_POSITIVE_X: // +X (right)
        dirX = 1.0f;
        dirY = -v;
        dirZ = -u;
        break;
    case FACE_NEGATIVE_X: // -X (left)
        dirX = -1.0f;
        dirY = -v;
        dirZ = u;
        break;
    case FACE_POSITIVE_Y: // +Y (top)
        dirX = u;
        dirY = 1.0f;
        dirZ = v;
        break;
    case FACE_NEGATIVE_Y: // -Y (bottom)
        dirX = u;
        dirY = -1.0f;
        dirZ = -v;
        break;
    case FACE_POSITIVE_Z: // +Z (front)
        dirX = u;
        dirY = -v;
        dirZ = 1.0f;
        break;
    case FACE_NEGATIVE_Z: // -Z (back)
        dirX = -u;
        dirY = -v;
        dirZ = -1.0f;
        break;
    default:
        dirX = 0.0f;
        dirY = 0.0f;
        dirZ = 1.0f;
        break;
    }

    // Normalize direction
    float len = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    dirX /= len;
    dirY /= len;
    dirZ /= len;
}

// Convert 3D direction to equirectangular UV coordinates
// Matches the shader convention: longitude = atan(x, z), latitude = asin(y)
void directionToEquirectangularUV(float dirX, float dirY, float dirZ, float &u, float &v)
{
    // Convert direction to spherical coordinates
    // longitude (theta) = atan2(x, z), range [-π, π] - angle from +Z towards +X
    // latitude (phi) = asin(y), range [-π/2, π/2]
    // NOTE: This matches the GLSL atan(x, z) convention used in the shader
    float theta = std::atan2(dirX, dirZ);
    float phi = std::asin(std::clamp(dirY, -1.0f, 1.0f));

    // Convert to UV coordinates
    // U: longitude maps to [0, 1], with center (U=0.5) at +Z direction
    // V: latitude maps to [0, 1], with 0 at north pole and 1 at south pole
    u = theta / (2.0f * static_cast<float>(PI)) + 0.5f;
    v = 0.5f - phi / static_cast<float>(PI);

    // Wrap U to [0, 1]
    if (u < 0.0f)
        u += 1.0f;
    if (u >= 1.0f)
        u -= 1.0f;

    // Clamp V to [0, 1]
    v = std::clamp(v, 0.0f, 1.0f);
}

// Sample equirectangular image with bilinear interpolation (unsigned char version)
void sampleEquirectangularUChar(const unsigned char *src,
                                int srcW,
                                int srcH,
                                int channels,
                                float u,
                                float v,
                                unsigned char *outColor)
{
    // Convert UV to pixel coordinates
    float srcX = u * srcW - 0.5f;
    float srcY = v * srcH - 0.5f;

    // Handle wrapping for X (horizontal)
    int x0 = static_cast<int>(std::floor(srcX));
    int x1 = x0 + 1;
    float xFrac = srcX - x0;

    // Wrap x0 and x1
    x0 = ((x0 % srcW) + srcW) % srcW;
    x1 = ((x1 % srcW) + srcW) % srcW;

    // Clamp Y (vertical)
    int y0 = static_cast<int>(std::floor(srcY));
    int y1 = y0 + 1;
    float yFrac = srcY - y0;

    y0 = std::clamp(y0, 0, srcH - 1);
    y1 = std::clamp(y1, 0, srcH - 1);

    // Bilinear interpolation
    for (int c = 0; c < channels; c++)
    {
        float v00 = src[(y0 * srcW + x0) * channels + c];
        float v10 = src[(y0 * srcW + x1) * channels + c];
        float v01 = src[(y1 * srcW + x0) * channels + c];
        float v11 = src[(y1 * srcW + x1) * channels + c];

        float v0 = v00 * (1.0f - xFrac) + v10 * xFrac;
        float v1 = v01 * (1.0f - xFrac) + v11 * xFrac;
        outColor[c] = static_cast<unsigned char>(std::clamp(v0 * (1.0f - yFrac) + v1 * yFrac, 0.0f, 255.0f));
    }
}

// Sample equirectangular image with bilinear interpolation (float version)
void sampleEquirectangularFloat(const float *src,
                                int srcW,
                                int srcH,
                                int channels,
                                float u,
                                float v,
                                float *outColor)
{
    // Convert UV to pixel coordinates
    float srcX = u * srcW - 0.5f;
    float srcY = v * srcH - 0.5f;

    // Handle wrapping for X (horizontal)
    int x0 = static_cast<int>(std::floor(srcX));
    int x1 = x0 + 1;
    float xFrac = srcX - x0;

    // Wrap x0 and x1
    x0 = ((x0 % srcW) + srcW) % srcW;
    x1 = ((x1 % srcW) + srcW) % srcW;

    // Clamp Y (vertical)
    int y0 = static_cast<int>(std::floor(srcY));
    int y1 = y0 + 1;
    float yFrac = srcY - y0;

    y0 = std::clamp(y0, 0, srcH - 1);
    y1 = std::clamp(y1, 0, srcH - 1);

    // Bilinear interpolation
    for (int c = 0; c < channels; c++)
    {
        float v00 = src[(y0 * srcW + x0) * channels + c];
        float v10 = src[(y0 * srcW + x1) * channels + c];
        float v01 = src[(y1 * srcW + x0) * channels + c];
        float v11 = src[(y1 * srcW + x1) * channels + c];

        float v0 = v00 * (1.0f - xFrac) + v10 * xFrac;
        float v1 = v01 * (1.0f - xFrac) + v11 * xFrac;
        outColor[c] = v0 * (1.0f - yFrac) + v1 * yFrac;
    }
}

// Convert equirectangular UV coordinates to 3D direction (inverse of directionToEquirectangularUV)
void equirectangularUVToDirection(float u, float v, float &dirX, float &dirY, float &dirZ)
{
    // Convert UV to spherical coordinates
    // U=0.5 corresponds to longitude=0 (+Z direction)
    // V=0 is north pole, V=1 is south pole
    float theta = (u - 0.5f) * 2.0f * static_cast<float>(PI); // longitude: [-π, π]
    float phi = (0.5f - v) * static_cast<float>(PI);          // latitude: [-π/2, π/2]

    // Convert spherical to Cartesian
    float cosPhi = std::cos(phi);
    dirX = std::sin(theta) * cosPhi;
    dirY = std::sin(phi);
    dirZ = std::cos(theta) * cosPhi;
}

// Get cubemap face and face UV from a 3D direction
// This matches the GLSL getCubemapFaceUV function in the fragment shader
void directionToCubemapFaceUV(float dirX, float dirY, float dirZ, int &face, float &faceU, float &faceV)
{
    float absDirX = std::abs(dirX);
    float absDirY = std::abs(dirY);
    float absDirZ = std::abs(dirZ);
    float ma; // major axis value
    float uvX, uvY;

    if (absDirX >= absDirY && absDirX >= absDirZ)
    {
        // X is dominant
        ma = absDirX;
        if (dirX > 0.0f)
        {
            // +X face
            face = FACE_POSITIVE_X;
            uvX = -dirZ;
            uvY = -dirY;
        }
        else
        {
            // -X face
            face = FACE_NEGATIVE_X;
            uvX = dirZ;
            uvY = -dirY;
        }
    }
    else if (absDirY >= absDirX && absDirY >= absDirZ)
    {
        // Y is dominant
        ma = absDirY;
        if (dirY > 0.0f)
        {
            // +Y face
            face = FACE_POSITIVE_Y;
            uvX = dirX;
            uvY = dirZ;
        }
        else
        {
            // -Y face
            face = FACE_NEGATIVE_Y;
            uvX = dirX;
            uvY = -dirZ;
        }
    }
    else
    {
        // Z is dominant
        ma = absDirZ;
        if (dirZ > 0.0f)
        {
            // +Z face
            face = FACE_POSITIVE_Z;
            uvX = dirX;
            uvY = -dirY;
        }
        else
        {
            // -Z face
            face = FACE_NEGATIVE_Z;
            uvX = -dirX;
            uvY = -dirY;
        }
    }

    // Normalize UV to [-1, 1] then convert to [0, 1]
    faceU = (uvX / ma) * 0.5f + 0.5f;
    faceV = (uvY / ma) * 0.5f + 0.5f;
}

// Sample cubemap grid image with bilinear interpolation (unsigned char version)
// Uses 3x2 grid layout: +X -X +Y (row 0), -Y +Z -Z (row 1)
void sampleCubemapGridUChar(const unsigned char *cubemapData,
                            int faceSize,
                            int channels,
                            float dirX,
                            float dirY,
                            float dirZ,
                            unsigned char *outColor)
{
    // Get face and UV from direction
    int face;
    float faceU, faceV;
    directionToCubemapFaceUV(dirX, dirY, dirZ, face, faceU, faceV);

    // Get grid position for this face
    int col, row;
    getCubemapFaceGridPosition(face, col, row);

    // Grid dimensions
    int gridWidth = faceSize * 3;

    // Convert face UV to pixel coordinates within the grid
    float srcX = (col + faceU) * faceSize - 0.5f;
    float srcY = (row + faceV) * faceSize - 0.5f;

    // Get integer coordinates for bilinear interpolation
    int x0 = static_cast<int>(std::floor(srcX));
    int x1 = x0 + 1;
    float xFrac = srcX - x0;

    int y0 = static_cast<int>(std::floor(srcY));
    int y1 = y0 + 1;
    float yFrac = srcY - y0;

    // Clamp to valid range within the face's region
    int faceStartX = col * faceSize;
    int faceStartY = row * faceSize;
    x0 = std::clamp(x0, faceStartX, faceStartX + faceSize - 1);
    x1 = std::clamp(x1, faceStartX, faceStartX + faceSize - 1);
    y0 = std::clamp(y0, faceStartY, faceStartY + faceSize - 1);
    y1 = std::clamp(y1, faceStartY, faceStartY + faceSize - 1);

    // Bilinear interpolation
    for (int c = 0; c < channels; c++)
    {
        float v00 = cubemapData[(y0 * gridWidth + x0) * channels + c];
        float v10 = cubemapData[(y0 * gridWidth + x1) * channels + c];
        float v01 = cubemapData[(y1 * gridWidth + x0) * channels + c];
        float v11 = cubemapData[(y1 * gridWidth + x1) * channels + c];

        float v0 = v00 * (1.0f - xFrac) + v10 * xFrac;
        float v1 = v01 * (1.0f - xFrac) + v11 * xFrac;
        outColor[c] = static_cast<unsigned char>(std::clamp(v0 * (1.0f - yFrac) + v1 * yFrac, 0.0f, 255.0f));
    }
}

// Legacy alias for backward compatibility
void sampleCubemapStripUChar(const unsigned char *cubemapData,
                             int faceSize,
                             int channels,
                             float dirX,
                             float dirY,
                             float dirZ,
                             unsigned char *outColor)
{
    sampleCubemapGridUChar(cubemapData, faceSize, channels, dirX, dirY, dirZ, outColor);
}

// Convert equirectangular unsigned char image to cubemap format (3x2 grid)
unsigned char *convertEquirectangularToCubemapUChar(const unsigned char *equirectData,
                                                    int equirectW,
                                                    int equirectH,
                                                    int channels,
                                                    int faceSize)
{
    // 3x2 grid: width = faceSize * 3, height = faceSize * 2
    int gridWidth = faceSize * 3;
    int gridHeight = faceSize * 2;
    size_t cubemapSize = static_cast<size_t>(gridWidth) * gridHeight * channels;
    unsigned char *cubemapData = new (std::nothrow) unsigned char[cubemapSize];
    if (!cubemapData)
    {
        std::cerr << "    ERROR: Failed to allocate memory for cubemap (" << cubemapSize << " bytes)\n";
        return nullptr;
    }

    std::cout << "    Converting equirectangular to cubemap (3x2 grid)...\n";
    std::cout << "      Source: " << equirectW << "x" << equirectH << "\n";
    std::cout << "      Cubemap face size: " << faceSize << "x" << faceSize << "\n";
    std::cout << "      Output: " << gridWidth << "x" << gridHeight << " (3x2 grid)\n";

    // Convert each face
    for (int face = 0; face < 6; face++)
    {
        // Get grid position for this face
        int col, row;
        getCubemapFaceGridPosition(face, col, row);
        int faceStartX = col * faceSize;
        int faceStartY = row * faceSize;

        for (int y = 0; y < faceSize; y++)
        {
            for (int x = 0; x < faceSize; x++)
            {
                // Get 3D direction for this pixel
                float dirX, dirY, dirZ;
                cubemapPixelToDirection(face, x, y, faceSize, dirX, dirY, dirZ);

                // Convert direction to equirectangular UV
                float u, v;
                directionToEquirectangularUV(dirX, dirY, dirZ, u, v);

                // Calculate pixel position in 3x2 grid
                int gridX = faceStartX + x;
                int gridY = faceStartY + y;
                size_t pixelOffset = (static_cast<size_t>(gridY) * gridWidth + gridX) * channels;

                // Sample equirectangular image
                sampleEquirectangularUChar(equirectData,
                                           equirectW,
                                           equirectH,
                                           channels,
                                           u,
                                           v,
                                           &cubemapData[pixelOffset]);
            }
        }
    }

    return cubemapData;
}

// Convert equirectangular HDR image to cubemap format (3x2 grid)
float *convertEquirectangularToCubemapFloat(const float *equirectData,
                                            int equirectW,
                                            int equirectH,
                                            int channels,
                                            int faceSize)
{
    // 3x2 grid: width = faceSize * 3, height = faceSize * 2
    int gridWidth = faceSize * 3;
    int gridHeight = faceSize * 2;
    size_t cubemapSize = static_cast<size_t>(gridWidth) * gridHeight * channels;
    float *cubemapData = new (std::nothrow) float[cubemapSize];
    if (!cubemapData)
    {
        std::cerr << "    ERROR: Failed to allocate memory for cubemap (" << cubemapSize * sizeof(float) << " bytes)\n";
        return nullptr;
    }

    std::cout << "    Converting equirectangular to cubemap (3x2 grid)...\n";
    std::cout << "      Source: " << equirectW << "x" << equirectH << "\n";
    std::cout << "      Cubemap face size: " << faceSize << "x" << faceSize << "\n";
    std::cout << "      Output: " << gridWidth << "x" << gridHeight << " (3x2 grid)\n";

    // Convert each face
    for (int face = 0; face < 6; face++)
    {
        // Get grid position for this face
        int col, row;
        getCubemapFaceGridPosition(face, col, row);
        int faceStartX = col * faceSize;
        int faceStartY = row * faceSize;

        for (int y = 0; y < faceSize; y++)
        {
            for (int x = 0; x < faceSize; x++)
            {
                // Get 3D direction for this pixel
                float dirX, dirY, dirZ;
                cubemapPixelToDirection(face, x, y, faceSize, dirX, dirY, dirZ);

                // Convert direction to equirectangular UV
                float u, v;
                directionToEquirectangularUV(dirX, dirY, dirZ, u, v);

                // Calculate pixel position in 3x2 grid
                int gridX = faceStartX + x;
                int gridY = faceStartY + y;
                size_t pixelOffset = (static_cast<size_t>(gridY) * gridWidth + gridX) * channels;

                // Sample equirectangular image
                sampleEquirectangularFloat(equirectData,
                                           equirectW,
                                           equirectH,
                                           channels,
                                           u,
                                           v,
                                           &cubemapData[pixelOffset]);
            }
        }
    }

    return cubemapData;
}

// Convert cubemap grid image to equirectangular format (unsigned char version)
// This is the inverse of convertEquirectangularToCubemapUChar
unsigned char *convertCubemapToEquirectangularUChar(const unsigned char *cubemapData,
                                                    int faceSize,
                                                    int channels,
                                                    int equirectW,
                                                    int equirectH)
{
    size_t equirectSize = static_cast<size_t>(equirectW) * equirectH * channels;
    unsigned char *equirectData = new (std::nothrow) unsigned char[equirectSize];
    if (!equirectData)
    {
        std::cerr << "    ERROR: Failed to allocate memory for equirectangular (" << equirectSize << " bytes)\n";
        return nullptr;
    }

    std::cout << "    Converting cubemap (3x2 grid) to equirectangular...\n";
    std::cout << "      Cubemap face size: " << faceSize << "x" << faceSize << "\n";
    std::cout << "      Output: " << equirectW << "x" << equirectH << "\n";

    // For each pixel in the equirectangular output
    for (int y = 0; y < equirectH; y++)
    {
        for (int x = 0; x < equirectW; x++)
        {
            // Convert pixel position to UV
            float u = (x + 0.5f) / equirectW;
            float v = (y + 0.5f) / equirectH;

            // Convert UV to 3D direction
            float dirX, dirY, dirZ;
            equirectangularUVToDirection(u, v, dirX, dirY, dirZ);

            // Sample cubemap at this direction
            size_t pixelOffset = (static_cast<size_t>(y) * equirectW + x) * channels;
            sampleCubemapGridUChar(cubemapData, faceSize, channels, dirX, dirY, dirZ, &equirectData[pixelOffset]);
        }
    }

    return equirectData;
}
