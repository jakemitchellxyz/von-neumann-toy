// ============================================================================
// Skybox Texture Preprocessing
// ============================================================================
// Preprocesses celestial skybox textures from source files:
// - TIF files: constellation_figures_32k.tif, celestial_grid_32k.tif, constellation_bounds_32k.tif (32k versions)
// - EXR files: hiptyc_2020_16k.exr, milkyway_2020_16k.exr (16k versions - smaller files)
//
// All textures are resized to 2x the user's selected resolution and saved
// to the output directory for use during rendering.

#include "../../concerns/constants.h"
#include "../settings.h"
#include "../stars-dynamic-skybox.h"
#include <filesystem>
#include <iostream>
#include <string>

#include <stb_image.h>
#include <stb_image_write.h>

// libtiff for loading TIF files
#include <tiffio.h>

// tinyexr for loading EXR files
// miniz is required by tinyexr for compression support (installed via vcpkg)
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

// Helper function for resizing images (from stars-dynamic-skybox.cpp)
static void resizeImage(const unsigned char *src,
                        int srcW,
                        int srcH,
                        unsigned char *dst,
                        int dstW,
                        int dstH,
                        int channels)
{
    float xRatio = static_cast<float>(srcW) / dstW;
    float yRatio = static_cast<float>(srcH) / dstH;

    for (int y = 0; y < dstH; y++)
    {
        float srcY = y * yRatio;
        int y0 = static_cast<int>(srcY);
        int y1 = std::min(y0 + 1, srcH - 1);
        float yFrac = srcY - y0;

        for (int x = 0; x < dstW; x++)
        {
            float srcX = x * xRatio;
            int x0 = static_cast<int>(srcX);
            int x1 = std::min(x0 + 1, srcW - 1);
            float xFrac = srcX - x0;

            for (int c = 0; c < channels; c++)
            {
                // Bilinear interpolation
                float v00 = src[(y0 * srcW + x0) * channels + c];
                float v10 = src[(y0 * srcW + x1) * channels + c];
                float v01 = src[(y1 * srcW + x0) * channels + c];
                float v11 = src[(y1 * srcW + x1) * channels + c];

                float v0 = v00 * (1 - xFrac) + v10 * xFrac;
                float v1 = v01 * (1 - xFrac) + v11 * xFrac;
                float value = v0 * (1 - yFrac) + v1 * yFrac;

                dst[(y * dstW + x) * channels + c] = static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f));
            }
        }
    }
}

// Helper function for resizing EXR/HDR images (float format)
static void resizeImageFloat(const float *src, int srcW, int srcH, float *dst, int dstW, int dstH, int channels)
{
    float xRatio = static_cast<float>(srcW) / dstW;
    float yRatio = static_cast<float>(srcH) / dstH;

    for (int y = 0; y < dstH; y++)
    {
        float srcY = y * yRatio;
        int y0 = static_cast<int>(srcY);
        int y1 = std::min(y0 + 1, srcH - 1);
        float yFrac = srcY - y0;

        for (int x = 0; x < dstW; x++)
        {
            float srcX = x * xRatio;
            int x0 = static_cast<int>(srcX);
            int x1 = std::min(x0 + 1, srcW - 1);
            float xFrac = srcX - x0;

            for (int c = 0; c < channels; c++)
            {
                // Bilinear interpolation
                float v00 = src[(y0 * srcW + x0) * channels + c];
                float v10 = src[(y0 * srcW + x1) * channels + c];
                float v01 = src[(y1 * srcW + x0) * channels + c];
                float v11 = src[(y1 * srcW + x1) * channels + c];

                float v0 = v00 * (1 - xFrac) + v10 * xFrac;
                float v1 = v01 * (1 - xFrac) + v11 * xFrac;
                float value = v0 * (1 - yFrac) + v1 * yFrac;

                dst[(y * dstW + x) * channels + c] = value;
            }
        }
    }
}

// ==================================
// Equirectangular to Cubemap Conversion
// ==================================
// Converts equirectangular (lat/long) projection to cubemap format
// Cubemap faces are stored in a vertical strip: +X, -X, +Y, -Y, +Z, -Z
// Each face is faceSize x faceSize pixels

// Cubemap face indices (matches Vulkan VK_IMAGE_VIEW_TYPE_CUBE order)
enum CubemapFace
{
    FACE_POSITIVE_X = 0, // Right
    FACE_NEGATIVE_X = 1, // Left
    FACE_POSITIVE_Y = 2, // Top
    FACE_NEGATIVE_Y = 3, // Bottom
    FACE_POSITIVE_Z = 4, // Front
    FACE_NEGATIVE_Z = 5  // Back
};

// Convert cubemap face pixel coordinates to 3D direction vector
static void cubemapPixelToDirection(int face, int x, int y, int faceSize, float &dirX, float &dirY, float &dirZ)
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
    }

    // Normalize direction
    float len = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    dirX /= len;
    dirY /= len;
    dirZ /= len;
}

// Convert 3D direction to equirectangular UV coordinates
static void directionToEquirectangularUV(float dirX, float dirY, float dirZ, float &u, float &v)
{
    // Convert direction to spherical coordinates
    // longitude (theta) = atan2(z, x), range [-π, π]
    // latitude (phi) = asin(y), range [-π/2, π/2]
    float theta = std::atan2(dirZ, dirX);
    float phi = std::asin(std::clamp(dirY, -1.0f, 1.0f));

    // Convert to UV coordinates
    // U: longitude maps to [0, 1], with 0 at -π and 1 at +π
    // V: latitude maps to [0, 1], with 0 at +π/2 (top) and 1 at -π/2 (bottom)
    u = (theta + static_cast<float>(PI)) / (2.0f * static_cast<float>(PI));
    v = 0.5f - phi / static_cast<float>(PI);

    // Wrap U to [0, 1]
    if (u < 0.0f)
        u += 1.0f;
    if (u >= 1.0f)
        u -= 1.0f;

    // Clamp V to [0, 1]
    v = std::clamp(v, 0.0f, 1.0f);
}

// Sample equirectangular image with bilinear interpolation (float version)
static void sampleEquirectangularFloat(const float *src,
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

// Sample equirectangular image with bilinear interpolation (unsigned char version)
static void sampleEquirectangularUChar(const unsigned char *src,
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

// Convert equirectangular HDR image to cubemap format (vertical strip)
// Returns cubemap data as float array: 6 faces * faceSize * faceSize * channels
// Faces are arranged vertically: +X, -X, +Y, -Y, +Z, -Z
static float *convertEquirectangularToCubemapFloat(const float *equirectData,
                                                   int equirectW,
                                                   int equirectH,
                                                   int channels,
                                                   int faceSize)
{
    size_t cubemapSize = static_cast<size_t>(6) * faceSize * faceSize * channels;
    float *cubemapData = new (std::nothrow) float[cubemapSize];
    if (!cubemapData)
    {
        std::cerr << "    ERROR: Failed to allocate memory for cubemap (" << cubemapSize * sizeof(float) << " bytes)"
                  << std::endl;
        return nullptr;
    }

    std::cout << "    Converting equirectangular to cubemap..." << std::endl;
    std::cout << "      Source: " << equirectW << "x" << equirectH << std::endl;
    std::cout << "      Cubemap face size: " << faceSize << "x" << faceSize << std::endl;
    std::cout << "      Output: " << faceSize << "x" << (faceSize * 6) << " (vertical strip)" << std::endl;

    // Convert each face
    for (int face = 0; face < 6; face++)
    {
        size_t faceOffset = static_cast<size_t>(face) * faceSize * faceSize * channels;

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

                // Sample equirectangular image
                size_t pixelOffset = faceOffset + (static_cast<size_t>(y) * faceSize + x) * channels;
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

// Convert equirectangular unsigned char image to cubemap format (vertical strip)
static unsigned char *convertEquirectangularToCubemapUChar(const unsigned char *equirectData,
                                                           int equirectW,
                                                           int equirectH,
                                                           int channels,
                                                           int faceSize)
{
    size_t cubemapSize = static_cast<size_t>(6) * faceSize * faceSize * channels;
    unsigned char *cubemapData = new (std::nothrow) unsigned char[cubemapSize];
    if (!cubemapData)
    {
        std::cerr << "    ERROR: Failed to allocate memory for cubemap (" << cubemapSize << " bytes)" << std::endl;
        return nullptr;
    }

    std::cout << "    Converting equirectangular to cubemap..." << std::endl;
    std::cout << "      Source: " << equirectW << "x" << equirectH << std::endl;
    std::cout << "      Cubemap face size: " << faceSize << "x" << faceSize << std::endl;
    std::cout << "      Output: " << faceSize << "x" << (faceSize * 6) << " (vertical strip)" << std::endl;

    // Convert each face
    for (int face = 0; face < 6; face++)
    {
        size_t faceOffset = static_cast<size_t>(face) * faceSize * faceSize * channels;

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

                // Sample equirectangular image
                size_t pixelOffset = faceOffset + (static_cast<size_t>(y) * faceSize + x) * channels;
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

// Load TIF file using libtiff and convert to RGB unsigned char array
static unsigned char *loadTIFAsRGB(const std::string &filepath, int &width, int &height, int &channels)
{
    TIFF *tif = TIFFOpen(filepath.c_str(), "r");
    if (!tif)
    {
        std::cerr << "    ERROR: Failed to open TIF file: " << filepath << std::endl;
        return nullptr;
    }

    uint32_t w, h;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);

    width = static_cast<int>(w);
    height = static_cast<int>(h);

    uint16_t bitsPerSample = 8;
    uint16_t sampleFormat = SAMPLEFORMAT_UINT;
    uint16_t samplesPerPixel = 1;
    uint16_t photometric = PHOTOMETRIC_MINISBLACK;

    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
    TIFFGetFieldDefaulted(tif, TIFFTAG_PHOTOMETRIC, &photometric);

    // Force RGB output (3 channels)
    channels = 3;

    // Allocate output buffer (RGB)
    size_t bufferSize = static_cast<size_t>(width) * height * channels;
    unsigned char *data = new (std::nothrow) unsigned char[bufferSize];
    if (!data)
    {
        std::cerr << "    ERROR: Failed to allocate memory for TIF image" << std::endl;
        TIFFClose(tif);
        return nullptr;
    }

    // Read scanline-based image
    tsize_t scanlineSize = TIFFScanlineSize(tif);
    void *scanlineBuffer = _TIFFmalloc(scanlineSize);
    if (!scanlineBuffer)
    {
        std::cerr << "    ERROR: Failed to allocate scanline buffer" << std::endl;
        delete[] data;
        TIFFClose(tif);
        return nullptr;
    }

    // Read each scanline
    for (int y = 0; y < height; y++)
    {
        if (TIFFReadScanline(tif, scanlineBuffer, y, 0) < 0)
        {
            std::cerr << "    ERROR: Failed to read scanline " << y << std::endl;
            _TIFFfree(scanlineBuffer);
            delete[] data;
            TIFFClose(tif);
            return nullptr;
        }

        // Convert scanline to RGB
        for (int x = 0; x < width; x++)
        {
            unsigned char r = 0, g = 0, b = 0;

            if (samplesPerPixel == 1)
            {
                // Grayscale - convert to RGB
                unsigned char gray = 0;
                if (bitsPerSample == 8)
                {
                    gray = reinterpret_cast<unsigned char *>(scanlineBuffer)[x];
                }
                else if (bitsPerSample == 16)
                {
                    uint16_t val = reinterpret_cast<uint16_t *>(scanlineBuffer)[x];
                    gray = static_cast<unsigned char>(val / 256); // Scale 16-bit to 8-bit
                }
                r = g = b = gray;
            }
            else if (samplesPerPixel >= 3)
            {
                // RGB or RGBA
                if (bitsPerSample == 8)
                {
                    unsigned char *ptr = reinterpret_cast<unsigned char *>(scanlineBuffer);
                    r = ptr[x * samplesPerPixel + 0];
                    g = ptr[x * samplesPerPixel + 1];
                    b = ptr[x * samplesPerPixel + 2];
                }
                else if (bitsPerSample == 16)
                {
                    uint16_t *ptr = reinterpret_cast<uint16_t *>(scanlineBuffer);
                    r = static_cast<unsigned char>(ptr[x * samplesPerPixel + 0] / 256);
                    g = static_cast<unsigned char>(ptr[x * samplesPerPixel + 1] / 256);
                    b = static_cast<unsigned char>(ptr[x * samplesPerPixel + 2] / 256);
                }
            }

            int idx = (y * width + x) * channels;
            data[idx + 0] = r;
            data[idx + 1] = g;
            data[idx + 2] = b;
        }
    }

    _TIFFfree(scanlineBuffer);
    TIFFClose(tif);

    return data;
}

// Preprocess a single TIF texture file
// sourceFile: path to source file in defaults/celestial-skybox/
// outputFile: path to output file in celestial-skybox/[resolution]/ (cache location)
// useTransparency: if true, save as PNG with alpha channel (black pixels become transparent)
static bool preprocessTIFTexture(const std::string &sourceFile,
                                 const std::string &outputFile,
                                 int targetWidth,
                                 int targetHeight,
                                 const std::string &textureName,
                                 bool useTransparency = false)
{
    // Check if already processed (check output/cache directory, not source)
    if (std::filesystem::exists(outputFile))
    {
        std::cout << "  " << textureName << " texture already exists (cached): " << outputFile << std::endl;
        return true;
    }

    if (!std::filesystem::exists(sourceFile))
    {
        std::cerr << "  WARNING: " << textureName << " source file not found: " << sourceFile << std::endl;
        return false;
    }

    std::cout << "  Processing " << textureName << "..." << std::endl;
    std::cout << "    Source: " << sourceFile << std::endl;
    std::cout << "    Target: " << targetWidth << "x" << targetHeight << std::endl;
    std::cout << "    Output: " << outputFile << std::endl;

    // Load source image using libtiff
    int srcWidth, srcHeight, srcChannels;
    unsigned char *srcData = loadTIFAsRGB(sourceFile, srcWidth, srcHeight, srcChannels);

    if (!srcData)
    {
        std::cerr << "    ERROR: Failed to load " << textureName << " source image: " << sourceFile << std::endl;
        return false;
    }

    std::cout << "    Source image: " << srcWidth << "x" << srcHeight << " (" << srcChannels << " channels)"
              << std::endl;

    // Determine output format and channels
    int outputChannels = useTransparency ? 4 : 3; // RGBA for PNG with transparency, RGB for JPG
    bool needsResize = (srcWidth != targetWidth || srcHeight != targetHeight);
    bool needsAlphaConversion = useTransparency && srcChannels == 3;

    unsigned char *processedData = srcData;
    int processedWidth = srcWidth;
    int processedHeight = srcHeight;

    // Resize if needed
    if (needsResize)
    {
        size_t dstSize = static_cast<size_t>(targetWidth) * targetHeight * srcChannels;
        unsigned char *dstData = new (std::nothrow) unsigned char[dstSize];

        if (!dstData)
        {
            std::cerr << "    ERROR: Failed to allocate memory for resized " << textureName << " texture" << std::endl;
            delete[] srcData;
            return false;
        }

        resizeImage(srcData, srcWidth, srcHeight, dstData, targetWidth, targetHeight, srcChannels);
        delete[] srcData;
        processedData = dstData;
        processedWidth = targetWidth;
        processedHeight = targetHeight;
    }

    // Convert to RGBA with transparency if needed (black pixels become transparent)
    unsigned char *finalData = processedData;
    if (needsAlphaConversion)
    {
        size_t rgbaSize = static_cast<size_t>(processedWidth) * processedHeight * 4;
        unsigned char *rgbaData = new (std::nothrow) unsigned char[rgbaSize];

        if (!rgbaData)
        {
            std::cerr << "    ERROR: Failed to allocate memory for RGBA conversion" << std::endl;
            if (needsResize)
                delete[] processedData;
            else
                delete[] srcData;
            return false;
        }

        // Convert RGB to RGBA, making black pixels transparent
        for (int i = 0; i < processedWidth * processedHeight; i++)
        {
            unsigned char r = processedData[i * 3 + 0];
            unsigned char g = processedData[i * 3 + 1];
            unsigned char b = processedData[i * 3 + 2];

            rgbaData[i * 4 + 0] = r;
            rgbaData[i * 4 + 1] = g;
            rgbaData[i * 4 + 2] = b;

            // Black pixels (or very dark pixels) become transparent
            // Use a threshold to handle near-black pixels
            const unsigned char blackThreshold = 5; // Pixels darker than this become transparent
            if (r <= blackThreshold && g <= blackThreshold && b <= blackThreshold)
            {
                rgbaData[i * 4 + 3] = 0; // Fully transparent
            }
            else
            {
                rgbaData[i * 4 + 3] = 255; // Fully opaque
            }
        }

        if (needsResize)
            delete[] processedData;
        finalData = rgbaData;
        outputChannels = 4;
    }

    // Convert to cubemap format for seamless skybox rendering
    // Face size is typically half the height of the equirectangular image
    int faceSize = processedHeight / 2;
    std::cout << "    Converting to cubemap format (face size: " << faceSize << "x" << faceSize << ")..." << std::endl;

    unsigned char *cubemapData =
        convertEquirectangularToCubemapUChar(finalData, processedWidth, processedHeight, outputChannels, faceSize);

    // Clean up original data
    if (needsAlphaConversion)
        delete[] finalData;
    else if (needsResize)
        delete[] processedData;
    else
        delete[] srcData;

    if (!cubemapData)
    {
        std::cerr << "    ERROR: Failed to convert to cubemap format" << std::endl;
        return false;
    }

    // Cubemap dimensions: faceSize x (faceSize * 6) as vertical strip
    int cubemapWidth = faceSize;
    int cubemapHeight = faceSize * 6;

    // Save as PNG (with alpha) or JPG (without alpha)
    int result = 0;
    if (useTransparency)
    {
        // Save as PNG with alpha channel
        result = stbi_write_png(outputFile.c_str(),
                                cubemapWidth,
                                cubemapHeight,
                                outputChannels,
                                cubemapData,
                                cubemapWidth * outputChannels);
    }
    else
    {
        // Save as JPG (no alpha)
        result = stbi_write_jpg(outputFile.c_str(), cubemapWidth, cubemapHeight, outputChannels, cubemapData, 95);
    }

    delete[] cubemapData;

    if (result)
    {
        std::cout << "    " << textureName << " cubemap saved successfully as "
                  << (useTransparency ? "PNG (with transparency)" : "JPG") << std::endl;
        std::cout << "      Output dimensions: " << cubemapWidth << "x" << cubemapHeight << " (6 faces vertical strip)"
                  << std::endl;
        return true;
    }
    else
    {
        std::cerr << "    ERROR: Failed to save " << textureName << " cubemap: " << outputFile << std::endl;
        return false;
    }
}

// Combine two EXR/HDR textures additively
// Loads both source files, resizes to target resolution, adds them pixel-by-pixel, saves as HDR
static bool combineEXRTexturesAdditive(const std::string &sourceFile1,
                                       const std::string &sourceFile2,
                                       const std::string &outputFile,
                                       int targetWidth,
                                       int targetHeight,
                                       const std::string &textureName)
{
    // Check if already processed - but don't skip if we're being forced to regenerate
    // Always check if file actually exists and is valid
    if (std::filesystem::exists(outputFile))
    {
        // Verify file is not empty (corrupted cache)
        auto fileSize = std::filesystem::file_size(outputFile);
        if (fileSize > 0)
        {
            std::cout << "  " << textureName << " texture already exists (cached): " << outputFile << std::endl;
            std::cout << "  Skipping regeneration. Delete this file to force regeneration." << std::endl;
            return true;
        }
        else
        {
            std::cerr << "  WARNING: Cached file exists but is empty (corrupted). Regenerating..." << std::endl;
            std::filesystem::remove(outputFile);
        }
    }

    std::cout << "  " << textureName << " texture not found, will generate: " << outputFile << std::endl;

    if (!std::filesystem::exists(sourceFile1))
    {
        std::cerr << "  WARNING: " << textureName << " source file 1 not found: " << sourceFile1 << std::endl;
        return false;
    }

    if (!std::filesystem::exists(sourceFile2))
    {
        std::cerr << "  WARNING: " << textureName << " source file 2 not found: " << sourceFile2 << std::endl;
        return false;
    }

    std::cout << "  Processing " << textureName << " (combining two HDR files additively)..." << std::endl;
    std::cout << "    Source 1: " << sourceFile1 << std::endl;
    std::cout << "    Source 2: " << sourceFile2 << std::endl;
    std::cout << "    Target: " << targetWidth << "x" << targetHeight << std::endl;
    std::cout << "    Output: " << outputFile << std::endl;

    // Load first EXR image
    float *srcData1 = nullptr;
    int srcWidth1 = 0;
    int srcHeight1 = 0;
    const char *err1 = nullptr;

    int ret1 = LoadEXR(&srcData1, &srcWidth1, &srcHeight1, sourceFile1.c_str(), &err1);
    if (ret1 != TINYEXR_SUCCESS)
    {
        if (err1)
        {
            std::cerr << "    ERROR: Failed to load first EXR file: " << sourceFile1 << std::endl;
            std::cerr << "    tinyexr error: " << err1 << std::endl;
            FreeEXRErrorMessage(err1);
        }
        return false;
    }

    // Load second EXR image
    float *srcData2 = nullptr;
    int srcWidth2 = 0;
    int srcHeight2 = 0;
    const char *err2 = nullptr;

    int ret2 = LoadEXR(&srcData2, &srcWidth2, &srcHeight2, sourceFile2.c_str(), &err2);
    if (ret2 != TINYEXR_SUCCESS)
    {
        if (err2)
        {
            std::cerr << "    ERROR: Failed to load second EXR file: " << sourceFile2 << std::endl;
            std::cerr << "    tinyexr error: " << err2 << std::endl;
            FreeEXRErrorMessage(err2);
        }
        free(srcData1);
        return false;
    }

    std::cout << "    Source 1: " << srcWidth1 << "x" << srcHeight1 << " (RGBA)" << std::endl;
    std::cout << "    Source 2: " << srcWidth2 << "x" << srcHeight2 << " (RGBA)" << std::endl;

    // Convert both to RGB (drop alpha)
    int srcChannels = 3;
    size_t rgbSize1 = static_cast<size_t>(srcWidth1) * srcHeight1 * 3;
    size_t rgbSize2 = static_cast<size_t>(srcWidth2) * srcHeight2 * 3;

    float *rgbData1 = new (std::nothrow) float[rgbSize1];
    float *rgbData2 = new (std::nothrow) float[rgbSize2];

    if (!rgbData1 || !rgbData2)
    {
        std::cerr << "    ERROR: Failed to allocate memory for RGB conversion" << std::endl;
        free(srcData1);
        free(srcData2);
        if (rgbData1)
            delete[] rgbData1;
        if (rgbData2)
            delete[] rgbData2;
        return false;
    }

    // Convert RGBA to RGB for both images
    for (int i = 0; i < srcWidth1 * srcHeight1; i++)
    {
        rgbData1[i * 3 + 0] = srcData1[i * 4 + 0];
        rgbData1[i * 3 + 1] = srcData1[i * 4 + 1];
        rgbData1[i * 3 + 2] = srcData1[i * 4 + 2];
    }

    for (int i = 0; i < srcWidth2 * srcHeight2; i++)
    {
        rgbData2[i * 3 + 0] = srcData2[i * 4 + 0];
        rgbData2[i * 3 + 1] = srcData2[i * 4 + 1];
        rgbData2[i * 3 + 2] = srcData2[i * 4 + 2];
    }

    free(srcData1);
    free(srcData2);

    // Resize both images to target resolution if needed
    float *resizedData1 = nullptr;
    float *resizedData2 = nullptr;

    if (srcWidth1 != targetWidth || srcHeight1 != targetHeight)
    {
        size_t dstSize = static_cast<size_t>(targetWidth) * targetHeight * srcChannels;
        resizedData1 = new (std::nothrow) float[dstSize];
        if (!resizedData1)
        {
            std::cerr << "    ERROR: Failed to allocate memory for resizing first image" << std::endl;
            delete[] rgbData1;
            delete[] rgbData2;
            return false;
        }
        resizeImageFloat(rgbData1, srcWidth1, srcHeight1, resizedData1, targetWidth, targetHeight, srcChannels);
        delete[] rgbData1;
        rgbData1 = resizedData1;
    }

    if (srcWidth2 != targetWidth || srcHeight2 != targetHeight)
    {
        size_t dstSize = static_cast<size_t>(targetWidth) * targetHeight * srcChannels;
        resizedData2 = new (std::nothrow) float[dstSize];
        if (!resizedData2)
        {
            std::cerr << "    ERROR: Failed to allocate memory for resizing second image" << std::endl;
            delete[] rgbData1;
            delete[] rgbData2;
            return false;
        }
        resizeImageFloat(rgbData2, srcWidth2, srcHeight2, resizedData2, targetWidth, targetHeight, srcChannels);
        delete[] rgbData2;
        rgbData2 = resizedData2;
    }

    // Combine additively: result = image1 (milkyway/base) + image2 (hiptyc/stars)
    // Pure addition: black pixels (0,0,0) in hiptyc add nothing, preserving milkyway
    size_t combinedSize = static_cast<size_t>(targetWidth) * targetHeight * srcChannels;
    float *combinedData = new (std::nothrow) float[combinedSize];
    if (!combinedData)
    {
        std::cerr << "    ERROR: Failed to allocate memory for combined image" << std::endl;
        delete[] rgbData1;
        delete[] rgbData2;
        return false;
    }

    // Pure addition - no clamping, no normalization, just add the float values
    // milkyway (rgbData1) is the base layer, hiptyc (rgbData2) is added on top
    // Black pixels in hiptyc should be exactly 0.0 and add nothing

    size_t blackPixelCount = 0;
    size_t nonBlackPixelCount = 0;
    float minVal1 = rgbData1[0], maxVal1 = rgbData1[0];
    float minVal2 = rgbData2[0], maxVal2 = rgbData2[0];
    float minValCombined = 0.0f, maxValCombined = 0.0f;

    for (size_t i = 0; i < combinedSize; i += 3) // Check RGB triplets
    {
        // Check if hiptyc pixel is black (all channels near 0)
        float r2 = rgbData2[i + 0];
        float g2 = rgbData2[i + 1];
        float b2 = rgbData2[i + 2];
        bool isBlack = (r2 < 0.001f && g2 < 0.001f && b2 < 0.001f);

        if (isBlack)
            blackPixelCount++;
        else
            nonBlackPixelCount++;

        // Pure addition: combined = milkyway + hiptyc
        // If hiptyc pixel is black (0,0,0), it adds nothing: milkyway + 0 = milkyway
        combinedData[i + 0] = rgbData1[i + 0] + rgbData2[i + 0];
        combinedData[i + 1] = rgbData1[i + 1] + rgbData2[i + 1];
        combinedData[i + 2] = rgbData1[i + 2] + rgbData2[i + 2];

        // Track value ranges for debugging (sample first 100 pixels)
        if (i < 300) // 100 pixels * 3 channels
        {
            if (rgbData1[i] < minVal1)
                minVal1 = rgbData1[i];
            if (rgbData1[i] > maxVal1)
                maxVal1 = rgbData1[i];
            if (rgbData2[i] < minVal2)
                minVal2 = rgbData2[i];
            if (rgbData2[i] > maxVal2)
                maxVal2 = rgbData2[i];
            if (combinedData[i] < minValCombined)
                minValCombined = combinedData[i];
            if (combinedData[i] > maxValCombined)
                maxValCombined = combinedData[i];
        }
    }

    std::cout << "    Value ranges (sample):" << std::endl;
    std::cout << "      Milkyway: [" << minVal1 << ", " << maxVal1 << "]" << std::endl;
    std::cout << "      Hiptyc: [" << minVal2 << ", " << maxVal2 << "]" << std::endl;
    std::cout << "      Combined: [" << minValCombined << ", " << maxValCombined << "]" << std::endl;
    std::cout << "    Pixel statistics:" << std::endl;
    std::cout << "      Black pixels in hiptyc: " << blackPixelCount << " (should add nothing)" << std::endl;
    std::cout << "      Non-black pixels in hiptyc: " << nonBlackPixelCount << " (will add to milkyway)" << std::endl;

    delete[] rgbData1;
    delete[] rgbData2;

    // Convert equirectangular data to cubemap format
    // Cubemaps have no horizontal seam - each face tiles seamlessly with adjacent faces
    // Face size is typically half the height (or quarter the width) of the equirectangular image
    int faceSize = targetHeight / 2; // For a 2:1 aspect ratio equirectangular, this gives good quality
    std::cout << "    Converting to cubemap format (face size: " << faceSize << "x" << faceSize << ")..." << std::endl;

    float *cubemapData =
        convertEquirectangularToCubemapFloat(combinedData, targetWidth, targetHeight, srcChannels, faceSize);
    delete[] combinedData;

    if (!cubemapData)
    {
        std::cerr << "    ERROR: Failed to convert to cubemap format" << std::endl;
        return false;
    }

    // Save cubemap as vertical strip HDR (6 faces stacked vertically)
    // Dimensions: faceSize x (faceSize * 6)
    int cubemapWidth = faceSize;
    int cubemapHeight = faceSize * 6;
    int result = stbi_write_hdr(outputFile.c_str(), cubemapWidth, cubemapHeight, srcChannels, cubemapData);
    delete[] cubemapData;

    if (result)
    {
        std::cout << "    " << textureName << " cubemap saved successfully" << std::endl;
        std::cout << "      Output dimensions: " << cubemapWidth << "x" << cubemapHeight << " (6 faces vertical strip)"
                  << std::endl;
        return true;
    }
    else
    {
        std::cerr << "    ERROR: Failed to save " << textureName << " cubemap: " << outputFile << std::endl;
        return false;
    }
}

// Preprocess a single EXR texture file
// sourceFile: path to source file in defaults/celestial-skybox/
// outputFile: path to output file in celestial-skybox/[resolution]/ (cache location)
static bool preprocessEXRTexture(const std::string &sourceFile,
                                 const std::string &outputFile,
                                 int targetWidth,
                                 int targetHeight,
                                 const std::string &textureName)
{
    // Check if already processed (check output/cache directory, not source)
    if (std::filesystem::exists(outputFile))
    {
        std::cout << "  " << textureName << " texture already exists (cached): " << outputFile << std::endl;
        return true;
    }

    if (!std::filesystem::exists(sourceFile))
    {
        std::cerr << "  WARNING: " << textureName << " source file not found: " << sourceFile << std::endl;
        return false;
    }

    std::cout << "  Processing " << textureName << "..." << std::endl;
    std::cout << "    Source: " << sourceFile << std::endl;
    std::cout << "    Target: " << targetWidth << "x" << targetHeight << std::endl;
    std::cout << "    Output: " << outputFile << std::endl;

    // Load source EXR image using tinyexr
    float *srcData = nullptr;
    int srcWidth = 0;
    int srcHeight = 0;
    const char *err = nullptr;

    int ret = LoadEXR(&srcData, &srcWidth, &srcHeight, sourceFile.c_str(), &err);
    if (ret != TINYEXR_SUCCESS)
    {
        if (err)
        {
            std::cerr << "    ERROR: Failed to load " << textureName << " EXR file: " << sourceFile << std::endl;
            std::cerr << "    tinyexr error: " << err << std::endl;
            FreeEXRErrorMessage(err);
        }
        else
        {
            std::cerr << "    ERROR: Failed to load " << textureName << " EXR file: " << sourceFile << std::endl;
        }
        return false;
    }

    int srcChannels = 4; // EXR typically has RGBA, but we'll use RGB
    std::cout << "    Source image: " << srcWidth << "x" << srcHeight << " (4 channels RGBA, using RGB)" << std::endl;

    // Convert RGBA to RGB if needed (tinyexr loads as RGBA by default)
    float *rgbData = nullptr;
    if (srcChannels == 4)
    {
        size_t rgbSize = static_cast<size_t>(srcWidth) * srcHeight * 3;
        rgbData = new (std::nothrow) float[rgbSize];
        if (!rgbData)
        {
            std::cerr << "    ERROR: Failed to allocate memory for RGB conversion" << std::endl;
            free(srcData);
            return false;
        }

        // Convert RGBA to RGB (drop alpha)
        for (int i = 0; i < srcWidth * srcHeight; i++)
        {
            rgbData[i * 3 + 0] = srcData[i * 4 + 0];
            rgbData[i * 3 + 1] = srcData[i * 4 + 1];
            rgbData[i * 3 + 2] = srcData[i * 4 + 2];
        }
        free(srcData);
        srcData = rgbData;
        srcChannels = 3;
    }

    // If source is already target size, just copy (save as HDR)
    if (srcWidth == targetWidth && srcHeight == targetHeight)
    {
        // Save as HDR (stb_image_write doesn't support EXR, but HDR is similar)
        int result = stbi_write_hdr(outputFile.c_str(), targetWidth, targetHeight, srcChannels, srcData);
        free(srcData);

        if (result)
        {
            std::cout << "    " << textureName << " texture saved successfully (no resize needed)" << std::endl;
            return true;
        }
        else
        {
            std::cerr << "    ERROR: Failed to save " << textureName << " texture: " << outputFile << std::endl;
            return false;
        }
    }

    // Resize to target dimensions
    size_t dstSize = static_cast<size_t>(targetWidth) * targetHeight * srcChannels;
    float *dstData = new (std::nothrow) float[dstSize];

    if (!dstData)
    {
        std::cerr << "    ERROR: Failed to allocate memory for resized " << textureName << " texture" << std::endl;
        free(srcData);
        return false;
    }

    resizeImageFloat(srcData, srcWidth, srcHeight, dstData, targetWidth, targetHeight, srcChannels);

    // Free source data
    free(srcData);

    // Save as HDR (stb_image_write doesn't support EXR, but HDR is similar)
    int result = stbi_write_hdr(outputFile.c_str(), targetWidth, targetHeight, srcChannels, dstData);

    delete[] dstData;

    if (result)
    {
        std::cout << "    " << textureName << " texture saved successfully" << std::endl;
        return true;
    }
    else
    {
        std::cerr << "    ERROR: Failed to save " << textureName << " texture: " << outputFile << std::endl;
        return false;
    }
}

// Main preprocessing function for all skybox textures
// Source files are read from: defaultsPath/celestial-skybox/ (e.g., defaults/celestial-skybox/)
// Processed files are written to: outputPath/[resolution]/ (e.g., celestial-skybox/medium/)
// The initialization function reads from the outputPath, not the source directory
bool PreprocessSkyboxTextures(const std::string &defaultsPath,
                              const std::string &outputPath,
                              TextureResolution resolution)
{
    // Immediate output to verify function is being called
    std::cerr << "[DEBUG] PreprocessSkyboxTextures called with defaultsPath=" << defaultsPath
              << ", outputPath=" << outputPath << std::endl;
    std::cerr.flush();

    std::cout << "=== Skybox Texture Preprocessing ===" << std::endl;
    std::cout.flush();

    // Get resolution dimensions and calculate 2x target size
    int baseWidth, baseHeight;
    getResolutionDimensions(resolution, baseWidth, baseHeight);

    // Output is 2x the user's selected resolution
    int targetWidth = baseWidth * 2;
    int targetHeight = baseHeight * 2;

    // Output directory: where processed/cached textures are saved (e.g., celestial-skybox/medium/)
    std::string outputDir = outputPath + "/" + getResolutionFolderName(resolution);
    std::filesystem::create_directories(outputDir);

    // Source directory: where original source files are located (e.g., defaults/celestial-skybox/)
    std::string sourceDir = defaultsPath + "/celestial-skybox";

    std::cout << "Resolution: " << getResolutionName(resolution) << " (" << baseWidth << "x" << baseHeight << ")"
              << std::endl;
    std::cout << "Target resolution: " << targetWidth << "x" << targetHeight << " (2x)" << std::endl;
    std::cout << "Source directory: " << sourceDir << std::endl;
    std::cout << "  Absolute path: " << std::filesystem::absolute(sourceDir).string() << std::endl;
    std::cout << "Output directory: " << outputDir << std::endl;
    std::cout << "  Absolute path: " << std::filesystem::absolute(outputDir).string() << std::endl;
    std::cout << std::endl;
    std::cout.flush();

    // Check if source directory exists
    if (!std::filesystem::exists(sourceDir))
    {
        std::cerr << "ERROR: Source directory does not exist: " << sourceDir << std::endl;
        std::cerr << "  Absolute path: " << std::filesystem::absolute(sourceDir).string() << std::endl;
        std::cerr << "  Please ensure the celestial-skybox directory exists in the defaults folder." << std::endl;
        std::cout << "===================================" << std::endl;
        return false;
    }

    if (!std::filesystem::is_directory(sourceDir))
    {
        std::cerr << "ERROR: Source path exists but is not a directory: " << sourceDir << std::endl;
        std::cout << "===================================" << std::endl;
        return false;
    }

    bool allSuccess = true;

    std::cout << "Starting texture preprocessing..." << std::endl;
    std::cout.flush();

    // Check if source files exist before attempting to process
    // TIF files use 32k versions, EXR files use 16k versions (smaller)
    std::vector<std::pair<std::string, std::string>> sourceFiles = {
        {sourceDir + "/constellation_figures_32k.tif", "Constellation Figures (32k)"},
        {sourceDir + "/celestial_grid_32k.tif", "Celestial Grid (32k)"},
        {sourceDir + "/constellation_bounds_32k.tif", "Constellation Bounds (32k)"},
        {sourceDir + "/milkyway_2020_16k.exr", "Milky Way (16k)"},
        {sourceDir + "/hiptyc_2020_16k.exr", "Hiptyc Stars (16k)"}};

    std::cout << "\nChecking source files..." << std::endl;
    bool allSourcesExist = true;
    for (const auto &file : sourceFiles)
    {
        if (std::filesystem::exists(file.first))
        {
            std::cout << "  ✓ Found: " << file.second << " (" << file.first << ")" << std::endl;
        }
        else
        {
            std::cerr << "  ✗ Missing: " << file.second << " (" << file.first << ")" << std::endl;
            std::cerr << "    Absolute path: " << std::filesystem::absolute(file.first).string() << std::endl;
            allSourcesExist = false;
        }
    }

    if (!allSourcesExist)
    {
        std::cerr << "\nERROR: Some source files are missing. Cannot preprocess skybox textures." << std::endl;
        std::cerr << "Please ensure all source files exist in: " << sourceDir << std::endl;
        std::cout << "===================================" << std::endl;
        return false;
    }

    // Process TIF textures (using 32k source files)
    // Grid, bounds, and figures use PNG with transparency (black pixels become transparent)
    std::cout << "\n[1/4] Processing TIF textures (32k sources)..." << std::endl;
    bool tif1 = preprocessTIFTexture(sourceDir + "/constellation_figures_32k.tif",
                                     outputDir + "/constellation_figures.png",
                                     targetWidth,
                                     targetHeight,
                                     "Constellation Figures",
                                     true); // Use transparency
    allSuccess &= tif1;
    if (!tif1)
        std::cerr << "  ERROR: Failed to process Constellation Figures" << std::endl;

    bool tif2 = preprocessTIFTexture(sourceDir + "/celestial_grid_32k.tif",
                                     outputDir + "/celestial_grid.png",
                                     targetWidth,
                                     targetHeight,
                                     "Celestial Grid",
                                     true); // Use transparency
    allSuccess &= tif2;
    if (!tif2)
        std::cerr << "  ERROR: Failed to process Celestial Grid" << std::endl;

    bool tif3 = preprocessTIFTexture(sourceDir + "/constellation_bounds_32k.tif",
                                     outputDir + "/constellation_bounds.png",
                                     targetWidth,
                                     targetHeight,
                                     "Constellation Bounds",
                                     true); // Use transparency
    allSuccess &= tif3;
    if (!tif3)
        std::cerr << "  ERROR: Failed to process Constellation Bounds" << std::endl;

    // Combine milkyway and hiptyc HDR files additively into a single combined HDR (using 16k source files)
    // This avoids runtime blending issues and is more efficient
    // Pure addition: milkyway (base) + hiptyc (stars on top)
    // Black pixels in hiptyc add nothing, preserving milkyway beneath
    std::cout << "\n[2/4] Combining HDR textures additively (16k sources)..." << std::endl;
    bool hdrCombined = combineEXRTexturesAdditive(sourceDir + "/milkyway_2020_16k.exr",
                                                  sourceDir + "/hiptyc_2020_16k.exr",
                                                  outputDir + "/milkyway_combined.hdr",
                                                  targetWidth,
                                                  targetHeight,
                                                  "Milky Way + Hiptyc Stars");
    allSuccess &= hdrCombined;
    if (!hdrCombined)
        std::cerr << "  ERROR: Failed to combine HDR textures" << std::endl;

    std::cout << std::endl;

    // Check what files were actually created
    std::cout << "Verifying generated files..." << std::endl;
    std::vector<std::pair<std::string, std::string>> outputFiles = {
        {outputDir + "/constellation_figures.png", "Constellation Figures"},
        {outputDir + "/celestial_grid.png", "Celestial Grid"},
        {outputDir + "/constellation_bounds.png", "Constellation Bounds"},
        {outputDir + "/milkyway_combined.hdr", "Milky Way + Hiptyc Combined"}};

    int filesCreated = 0;
    for (const auto &file : outputFiles)
    {
        if (std::filesystem::exists(file.first))
        {
            std::cout << "  ✓ Created: " << file.second << std::endl;
            filesCreated++;
        }
        else
        {
            std::cerr << "  ✗ Missing: " << file.second << " (" << file.first << ")" << std::endl;
        }
    }

    if (allSuccess && filesCreated == outputFiles.size())
    {
        std::cout << "\nSkybox texture preprocessing completed successfully (" << filesCreated << "/"
                  << outputFiles.size() << " files)" << std::endl;
    }
    else
    {
        std::cerr << "\nWARNING: Skybox texture preprocessing had issues (" << filesCreated << "/" << outputFiles.size()
                  << " files created)" << std::endl;
        if (filesCreated == 0)
        {
            std::cerr << "  No files were generated. Check source files and error messages above." << std::endl;
        }
    }
    std::cout << "===================================" << std::endl;

    // Return true if at least the combined HDR was created (most important file)
    // This allows the skybox to render even if some PNG layers are missing
    bool criticalFileExists = std::filesystem::exists(outputDir + "/milkyway_combined.hdr");
    return criticalFileExists;
}
