#include "../../../concerns/constants.h"
#include "../../helpers/cubemap-conversion.h"
#include "../earth-material.h"


#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

#include <tiffio.h>

// ============================================================================
// Elevation Data Processing (Heightmap and Normal Map Generation)
// ============================================================================

float *EarthMaterial::loadGeoTiffElevation(const std::string &filepath, int &width, int &height)
{
    std::cout << "Opening GeoTIFF: " << filepath << '\n';

    TIFF *tif = TIFFOpen(filepath.c_str(), "r");
    if (!tif)
    {
        std::cerr << "ERROR: Failed to open GeoTIFF: " << filepath << '\n';
        std::cerr << "  Make sure the file exists and libtiff is properly linked." << '\n';
        return nullptr;
    }

    std::cout << "  GeoTIFF opened successfully" << '\n';

    uint32_t w, h;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);

    width = static_cast<int>(w);
    height = static_cast<int>(h);

    // Get bits per sample and sample format
    uint16_t bitsPerSample = 8;
    uint16_t sampleFormat = SAMPLEFORMAT_UINT;
    uint16_t samplesPerPixel = 1;

    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);

    // Check if tiled
    uint32_t tileWidth = 0, tileHeight = 0;
    int isTiled = TIFFIsTiled(tif);
    if (isTiled)
    {
        TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tileWidth);
        TIFFGetField(tif, TIFFTAG_TILELENGTH, &tileHeight);
    }

    std::cout << "  GeoTIFF: " << width << "x" << height << ", " << bitsPerSample << " bits, " << samplesPerPixel
              << " samples"
              << (isTiled ? ", TILED (" + std::to_string(tileWidth) + "x" + std::to_string(tileHeight) + ")"
                          : ", SCANLINE")
              << '\n';

    // Allocate output buffer
    float *elevation = new (std::nothrow) float[static_cast<size_t>(width) * height];
    if (!elevation)
    {
        std::cerr << "Failed to allocate elevation buffer" << '\n';
        TIFFClose(tif);
        return nullptr;
    }

    // Initialize to zero
    std::fill(elevation, elevation + static_cast<size_t>(width) * height, 0.0f);

    if (isTiled)
    {
        // Read tiled image
        tsize_t tileSize = TIFFTileSize(tif);
        void *tileBuffer = _TIFFmalloc(tileSize);
        if (!tileBuffer)
        {
            std::cerr << "Failed to allocate tile buffer" << '\n';
            delete[] elevation;
            TIFFClose(tif);
            return nullptr;
        }

        int tilesX = (width + tileWidth - 1) / tileWidth;
        int tilesY = (height + tileHeight - 1) / tileHeight;
        int totalTiles = tilesX * tilesY;
        int tilesRead = 0;

        std::cout << "  Reading " << totalTiles << " tiles..." << '\n';
        std::cout.flush();

        for (uint32_t ty = 0; ty < static_cast<uint32_t>(height); ty += tileHeight)
        {
            for (uint32_t tx = 0; tx < static_cast<uint32_t>(width); tx += tileWidth)
            {
                if (TIFFReadTile(tif, tileBuffer, tx, ty, 0, 0) < 0)
                {
                    std::cerr << "Failed to read tile at (" << tx << ", " << ty << ")" << '\n';
                    _TIFFfree(tileBuffer);
                    delete[] elevation;
                    TIFFClose(tif);
                    return nullptr;
                }

                // Copy tile data to elevation buffer
                uint32_t copyWidth = std::min(tileWidth, w - tx);
                uint32_t copyHeight = std::min(tileHeight, h - ty);

                for (uint32_t py = 0; py < copyHeight; py++)
                {
                    for (uint32_t px = 0; px < copyWidth; px++)
                    {
                        uint32_t tileIdx = py * tileWidth + px;
                        uint32_t imgX = tx + px;
                        uint32_t imgY = ty + py;
                        uint32_t imgIdx = imgY * width + imgX;

                        float value = 0.0f;

                        if (bitsPerSample == 32)
                        {
                            if (sampleFormat == SAMPLEFORMAT_IEEEFP)
                            {
                                float *ptr = reinterpret_cast<float *>(tileBuffer);
                                value = ptr[tileIdx];
                            }
                            else if (sampleFormat == SAMPLEFORMAT_INT)
                            {
                                int32_t *ptr = reinterpret_cast<int32_t *>(tileBuffer);
                                value = static_cast<float>(ptr[tileIdx]);
                            }
                            else
                            {
                                uint32_t *ptr = reinterpret_cast<uint32_t *>(tileBuffer);
                                value = static_cast<float>(ptr[tileIdx]);
                            }
                        }
                        else if (bitsPerSample == 16)
                        {
                            if (sampleFormat == SAMPLEFORMAT_INT)
                            {
                                int16_t *ptr = reinterpret_cast<int16_t *>(tileBuffer);
                                value = static_cast<float>(ptr[tileIdx]);
                            }
                            else
                            {
                                uint16_t *ptr = reinterpret_cast<uint16_t *>(tileBuffer);
                                value = static_cast<float>(ptr[tileIdx]);
                            }
                        }
                        else if (bitsPerSample == 8)
                        {
                            uint8_t *ptr = reinterpret_cast<uint8_t *>(tileBuffer);
                            value = static_cast<float>(ptr[tileIdx]);
                        }

                        elevation[imgIdx] = value;
                    }
                }

                tilesRead++;
                if (tilesRead % 100 == 0)
                {
                    std::cout << "\r  Reading tiles: " << tilesRead << "/" << totalTiles << " ("
                              << (tilesRead * 100 / totalTiles) << "%)" << std::flush;
                }
            }
        }

        std::cout << "\r  Reading tiles: " << totalTiles << "/" << totalTiles << " (100%)" << '\n';
        _TIFFfree(tileBuffer);
    }
    else
    {
        // Read scanline-based image
        tsize_t scanlineSize = TIFFScanlineSize(tif);
        void *scanlineBuffer = _TIFFmalloc(scanlineSize);
        if (!scanlineBuffer)
        {
            std::cerr << "Failed to allocate scanline buffer" << '\n';
            delete[] elevation;
            TIFFClose(tif);
            return nullptr;
        }

        std::cout << "  Reading " << height << " scanlines..." << '\n';

        for (int y = 0; y < height; y++)
        {
            if (TIFFReadScanline(tif, scanlineBuffer, y, 0) < 0)
            {
                std::cerr << "Failed to read scanline " << y << '\n';
                _TIFFfree(scanlineBuffer);
                delete[] elevation;
                TIFFClose(tif);
                return nullptr;
            }

            for (int x = 0; x < width; x++)
            {
                float value = 0.0f;

                if (bitsPerSample == 32)
                {
                    if (sampleFormat == SAMPLEFORMAT_IEEEFP)
                    {
                        float *ptr = reinterpret_cast<float *>(scanlineBuffer);
                        value = ptr[x];
                    }
                    else if (sampleFormat == SAMPLEFORMAT_INT)
                    {
                        int32_t *ptr = reinterpret_cast<int32_t *>(scanlineBuffer);
                        value = static_cast<float>(ptr[x]);
                    }
                    else
                    {
                        uint32_t *ptr = reinterpret_cast<uint32_t *>(scanlineBuffer);
                        value = static_cast<float>(ptr[x]);
                    }
                }
                else if (bitsPerSample == 16)
                {
                    if (sampleFormat == SAMPLEFORMAT_INT)
                    {
                        int16_t *ptr = reinterpret_cast<int16_t *>(scanlineBuffer);
                        value = static_cast<float>(ptr[x]);
                    }
                    else
                    {
                        uint16_t *ptr = reinterpret_cast<uint16_t *>(scanlineBuffer);
                        value = static_cast<float>(ptr[x]);
                    }
                }
                else if (bitsPerSample == 8)
                {
                    uint8_t *ptr = reinterpret_cast<uint8_t *>(scanlineBuffer);
                    value = static_cast<float>(ptr[x]);
                }

                elevation[y * width + x] = value;
            }

            if (y % 1000 == 0)
            {
                std::cout << "\r  Reading scanlines: " << y << "/" << height << " (" << (y * 100 / height) << "%)"
                          << std::flush;
            }
        }

        std::cout << "\r  Reading scanlines: " << height << "/" << height << " (100%)" << '\n';
        _TIFFfree(scanlineBuffer);
    }

    TIFFClose(tif);
    std::cout << "  GeoTIFF loaded successfully" << '\n';

    return elevation;
}

unsigned char *EarthMaterial::generateHeightmap(const float *elevation,
                                                int srcWidth,
                                                int srcHeight,
                                                int dstWidth,
                                                int dstHeight)
{
    // Find min/max elevation for normalization
    float minElev = elevation[0];
    float maxElev = elevation[0];

    for (int i = 0; i < srcWidth * srcHeight; i++)
    {
        float v = elevation[i];
        // Skip NODATA values (typically very negative like -32768)
        if (v > -10000.0f)
        {
            minElev = std::min(minElev, v);
            maxElev = std::max(maxElev, v);
        }
    }

    std::cout << "  Elevation range: " << minElev << "m to " << maxElev << "m" << '\n';

    float range = maxElev - minElev;
    if (range < 1.0f)
        range = 1.0f; // Prevent division by zero

    // Allocate output buffer
    unsigned char *heightmap = new (std::nothrow) unsigned char[static_cast<size_t>(dstWidth) * dstHeight];
    if (!heightmap)
    {
        std::cerr << "Failed to allocate heightmap buffer" << '\n';
        return nullptr;
    }

    // Resample and normalize
    float xRatio = static_cast<float>(srcWidth) / dstWidth;
    float yRatio = static_cast<float>(srcHeight) / dstHeight;

    for (int y = 0; y < dstHeight; y++)
    {
        float srcY = y * yRatio;
        int y0 = static_cast<int>(srcY);
        int y1 = std::min(y0 + 1, srcHeight - 1);
        float yFrac = srcY - y0;

        for (int x = 0; x < dstWidth; x++)
        {
            float srcX = x * xRatio;
            int x0 = static_cast<int>(srcX);
            int x1 = std::min(x0 + 1, srcWidth - 1);
            float xFrac = srcX - x0;

            // Bilinear interpolation
            float v00 = elevation[y0 * srcWidth + x0];
            float v10 = elevation[y0 * srcWidth + x1];
            float v01 = elevation[y1 * srcWidth + x0];
            float v11 = elevation[y1 * srcWidth + x1];

            // Handle NODATA values
            if (v00 < -10000.0f)
                v00 = 0.0f;
            if (v10 < -10000.0f)
                v10 = 0.0f;
            if (v01 < -10000.0f)
                v01 = 0.0f;
            if (v11 < -10000.0f)
                v11 = 0.0f;

            float v0 = v00 * (1 - xFrac) + v10 * xFrac;
            float v1 = v01 * (1 - xFrac) + v11 * xFrac;
            float value = v0 * (1 - yFrac) + v1 * yFrac;

            // Normalize to 0-255
            float normalized = (value - minElev) / range;
            heightmap[y * dstWidth + x] = static_cast<unsigned char>(std::clamp(normalized * 255.0f, 0.0f, 255.0f));
        }
    }

    return heightmap;
}

// Generate normal map from equirectangular heightmap (legacy function, kept for compatibility)
unsigned char *EarthMaterial::generateNormalMap(const unsigned char *heightmap,
                                                int width,
                                                int height,
                                                float heightScale)
{
    // Allocate RGB output buffer
    unsigned char *normalMap = new (std::nothrow) unsigned char[static_cast<size_t>(width) * height * 3];
    if (!normalMap)
    {
        std::cerr << "Failed to allocate normal map buffer" << '\n';
        return nullptr;
    }

    // Generate normals using Sobel operator
    // Must account for equirectangular projection: pixels near poles represent
    // much smaller surface areas than at equator (meridians converge at poles)

    for (int y = 0; y < height; y++)
    {
        // Calculate latitude for this row
        // Row 0 = North Pole (90°N), Row height-1 = South Pole (90°S)
        float latitude =
            static_cast<float>(PI) / 2.0f - (static_cast<float>(y) / (height - 1)) * static_cast<float>(PI);

        // Scale factor for horizontal gradient to account for meridian convergence
        // At the poles, cos(lat) approaches 0, so we clamp to prevent extreme
        // values
        float cosLat = std::cos(latitude);
        float latitudeScale = 1.0f / std::max(cosLat, 0.1f); // Clamp to avoid infinity at poles

        for (int x = 0; x < width; x++)
        {
            // Sample neighboring heights (with wrapping for longitude, clamping for
            // latitude)
            auto getHeight = [&](int px, int py) -> float {
                // Wrap horizontally (longitude)
                if (px < 0)
                    px += width;
                if (px >= width)
                    px -= width;
                // Clamp vertically (latitude)
                py = std::clamp(py, 0, height - 1);
                return static_cast<float>(heightmap[py * width + px]) / 255.0f * heightScale;
            };

            // Sobel operator for gradient
            float hL = getHeight(x - 1, y);
            float hR = getHeight(x + 1, y);
            float hU = getHeight(x, y - 1);
            float hD = getHeight(x, y + 1);

            // Gradient in X and Y
            // dX needs to be scaled by latitude because 1 pixel of longitude
            // represents less surface distance near the poles
            float dX = (hR - hL) * 0.5f * latitudeScale;
            float dY = (hD - hU) * 0.5f;

            // Construct normal from gradient
            // Normal direction is (-df/dx, -df/dy, 1)
            // In tangent space: +X = east, +Y = north
            // A north-facing slope has dY < 0, and ny = -dY > 0 (tilts north)
            // An east-facing slope has dX < 0, and nx = -dX > 0 (tilts east)
            float nx = -dX; // Negate X for normal direction
            float ny = -dY; // Negate Y for normal direction
            float nz = 1.0f;

            // Normalize
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 0.0001f)
            {
                nx /= len;
                ny /= len;
                nz /= len;
            }
            else
            {
                nx = 0.0f;
                ny = 0.0f;
                nz = 1.0f;
            }

            // Convert from [-1,1] to [0,255]
            // Standard normal map encoding: R=X, G=Y, B=Z
            // CRITICAL: Flip Y component (north/south direction) to match shader convention
            int idx = (y * width + x) * 3;
            normalMap[idx + 0] = static_cast<unsigned char>((nx * 0.5f + 0.5f) * 255.0f);
            normalMap[idx + 1] = static_cast<unsigned char>((-ny * 0.5f + 0.5f) * 255.0f); // Flip Y
            normalMap[idx + 2] = static_cast<unsigned char>((nz * 0.5f + 0.5f) * 255.0f);
        }
    }

    return normalMap;
}

// Generate normal map directly from sinusoidal heightmap
// CRITICAL: This accounts for sinusoidal projection distortion
// In sinusoidal: U direction (east-west) is warped by cos(lat), V direction (north-south) is uniform
// When wrapped around sphere, U maps to east-west and V maps to north-south in world space
void EarthMaterial::generateNormalMapSinusoidal(const unsigned char *heightmapSinu,
                                                unsigned char *normalMapSinu,
                                                int width,
                                                int height,
                                                float heightScale)
{
    for (int y = 0; y < height; y++)
    {
        // Calculate latitude for this row in sinusoidal space
        // v = y / (height - 1), lat = (0.5 - v) * π
        float v = static_cast<float>(y) / (height - 1);
        float lat = (0.5f - v) * static_cast<float>(PI);
        float cosLat = std::cos(lat);

        // Valid U range at this latitude
        float uMin = 0.5f - 0.5f * std::abs(cosLat);
        float uMax = 0.5f + 0.5f * std::abs(cosLat);

        for (int x = 0; x < width; x++)
        {
            float u_sinu = static_cast<float>(x) / (width - 1);
            int dstIdx = y * width + x;

            // Check if within valid sinusoidal bounds
            if (u_sinu < uMin || u_sinu > uMax)
            {
                // Outside valid region - flat normal
                normalMapSinu[dstIdx * 3 + 0] = 128; // X = 0
                normalMapSinu[dstIdx * 3 + 1] = 128; // Y = 0
                normalMapSinu[dstIdx * 3 + 2] = 255; // Z = 1 (up)
                continue;
            }

            // Sample neighboring heights in sinusoidal space
            // CRITICAL: Must account for sinusoidal distortion when computing gradients
            auto getHeight = [&](int px, int py) -> float {
                // Wrap horizontally (longitude wraps around)
                if (px < 0)
                    px += width;
                if (px >= width)
                    px -= width;
                // Clamp vertically (latitude)
                py = std::clamp(py, 0, height - 1);

                // Check if neighbor is within valid sinusoidal bounds
                float u_neighbor = static_cast<float>(px) / (width - 1);
                float v_neighbor = static_cast<float>(py) / (height - 1);
                float lat_neighbor = (0.5f - v_neighbor) * static_cast<float>(PI);
                float cosLat_neighbor = std::cos(lat_neighbor);
                float uMin_neighbor = 0.5f - 0.5f * std::abs(cosLat_neighbor);
                float uMax_neighbor = 0.5f + 0.5f * std::abs(cosLat_neighbor);

                if (u_neighbor < uMin_neighbor || u_neighbor > uMax_neighbor)
                {
                    // Neighbor is outside valid region - use current height
                    return static_cast<float>(heightmapSinu[dstIdx]) / 255.0f * heightScale;
                }

                int idx = py * width + px;
                return static_cast<float>(heightmapSinu[idx]) / 255.0f * heightScale;
            };

            // Sobel operator for gradient
            float hL = getHeight(x - 1, y);
            float hR = getHeight(x + 1, y);
            float hU = getHeight(x, y - 1);
            float hD = getHeight(x, y + 1);

            // Account for sinusoidal projection distortion
            // In sinusoidal: x = lon * cos(lat), so dU/dlon = cos(lat)
            // This means 1 pixel in U represents cos(lat) * (2π/width) radians of longitude
            // At equator (cos(lat)=1): U is stretched, so gradient needs to be scaled DOWN
            // At poles (cos(lat)≈0): U is compressed, so gradient needs to be scaled UP
            // Scale factor: 1/cos(lat) to account for the stretching
            float uScale = 1.0f / std::max(std::abs(cosLat), 0.1f); // Clamp to avoid infinity at poles

            // Gradient in U and V directions (sinusoidal space)
            // Treat U and V normally: U = east-west, V = north-south (with sides being poles)
            float dU = (hR - hL) * 0.5f * uScale;
            float dV = (hD - hU) * 0.5f;

            // Construct normal from gradient
            // Normal direction is (-df/dU, -df/dV, 1) in tangent space
            // In sinusoidal tangent space: +U = east, +V = north (sides are poles)
            float nU = -dU; // Negate U for normal direction (east-west)
            float nV = -dV; // Negate V for normal direction (north-south)
            float nZ = 1.0f;

            // Normalize
            float len = std::sqrt(nU * nU + nV * nV + nZ * nZ);
            if (len > 0.0001f)
            {
                nU /= len;
                nV /= len;
                nZ /= len;
            }
            else
            {
                nU = 0.0f;
                nV = 0.0f;
                nZ = 1.0f;
            }

            // Convert from [-1,1] to [0,255]
            // Standard normal map encoding: R=X (U/east), G=Y (V/north), B=Z (up)
            // Flip V component (north/south direction) to match shader convention
            normalMapSinu[dstIdx * 3 + 0] = static_cast<unsigned char>((nU * 0.5f + 0.5f) * 255.0f);
            normalMapSinu[dstIdx * 3 + 1] = static_cast<unsigned char>((-nV * 0.5f + 0.5f) * 255.0f); // Flip V
            normalMapSinu[dstIdx * 3 + 2] = static_cast<unsigned char>((nZ * 0.5f + 0.5f) * 255.0f);
        }
    }
}

bool EarthMaterial::preprocessElevation(const std::string &defaultsPath,
                                        const std::string &outputBasePath,
                                        TextureResolution resolution)
{
    std::string sourcePath = defaultsPath + "/earth-surface/elevation";
    std::string outputPath = outputBasePath + "/" + getResolutionFolderName(resolution);

    int outWidth, outHeight;
    getResolutionDimensions(resolution, outWidth, outHeight);

    std::cout << "=== Earth Elevation Processing ===" << '\n';
    std::cout << "Resolution:  " << getResolutionName(resolution) << " (" << outWidth << "x" << outHeight << ")"
              << '\n';
    std::cout << "Source path: " << sourcePath << '\n';
    std::cout << "Output path: " << outputPath << '\n';
    std::cout.flush();

    // Check if source directory exists
    std::cout << "Checking source directory..." << '\n';
    std::cout.flush();

    if (!std::filesystem::exists(sourcePath))
    {
        std::cout << "ERROR: Source directory does not exist: " << sourcePath << '\n';
        std::cout << "Absolute path: " << std::filesystem::absolute(sourcePath).string() << '\n';
        std::cout << "===================================" << '\n';
        return false;
    }

    std::cout << "Source directory exists." << '\n';
    std::cout.flush();

    // Create output directory
    std::filesystem::create_directories(outputPath);

    // Output files (cubemap vertical strip format)
    std::string heightmapPath = outputPath + "/earth_landmass_heightmap.png";
    std::string normalMapPath = outputPath + "/earth_landmass_normal.png";

    // Print absolute paths for debugging
    std::cout << "Heightmap will be: " << std::filesystem::absolute(heightmapPath).string() << " (cubemap)" << '\n';
    std::cout << "Normalmap will be: " << std::filesystem::absolute(normalMapPath).string() << " (cubemap)" << '\n';

    if (std::filesystem::exists(heightmapPath) && std::filesystem::exists(normalMapPath))
    {
        std::cout << "Elevation textures already exist, skipping." << '\n';
        std::cout << "===================================" << '\n';
        return true;
    }

    // Find the ETOPO GeoTIFF file
    std::string tiffPath;
    std::cout << "Searching for GeoTIFF files..." << '\n';
    std::cout.flush();

    try
    {
        for (const auto &entry : std::filesystem::directory_iterator(sourcePath))
        {
            std::string ext = entry.path().extension().string();
            // Convert to lowercase for case-insensitive comparison
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            std::cout << "  Found: " << entry.path().filename().string() << " (ext: " << ext << ")" << '\n';
            std::cout.flush();

            if (ext == ".tif" || ext == ".tiff")
            {
                tiffPath = entry.path().string();
                std::cout << "  -> Selected as elevation source" << '\n';
                std::cout.flush();
                break;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR iterating directory: " << e.what() << '\n';
        std::cout << "===================================" << '\n';
        return false;
    }

    if (tiffPath.empty())
    {
        std::cout << "No GeoTIFF elevation file found in " << sourcePath << '\n';
        std::cout << "===================================" << '\n';
        return false;
    }

    std::cout << "Loading: " << std::filesystem::path(tiffPath).filename().string() << '\n';

    auto startTime = std::chrono::high_resolution_clock::now();

    // Load elevation data
    int srcWidth, srcHeight;
    float *elevation = loadGeoTiffElevation(tiffPath, srcWidth, srcHeight);
    if (!elevation)
    {
        std::cout << "Failed to load elevation data" << '\n';
        std::cout << "===================================" << '\n';
        return false;
    }

    std::cout << "Generating heightmap (equirectangular intermediate)..." << '\n';

    // Generate heightmap in equirectangular (intermediate)
    unsigned char *heightmapEquirect = generateHeightmap(elevation, srcWidth, srcHeight, outWidth, outHeight);
    if (!heightmapEquirect)
    {
        delete[] elevation;
        std::cout << "Failed to generate heightmap" << '\n';
        std::cout << "===================================" << '\n';
        return false;
    }

    // Free elevation data (no longer needed)
    delete[] elevation;
    elevation = nullptr;

    // =========================================================================
    // Generate Normal Map from Equirectangular Heightmap
    // =========================================================================
    std::cout << "Generating normal map from equirectangular heightmap..." << '\n';

    float heightScale = 50.0f;
    unsigned char *normalMapEquirect = generateNormalMap(heightmapEquirect, outWidth, outHeight, heightScale);
    if (!normalMapEquirect)
    {
        delete[] heightmapEquirect;
        std::cout << "Failed to generate normal map" << '\n';
        std::cout << "===================================" << '\n';
        return false;
    }

    // =========================================================================
    // Create Bathymetry Maps (Ocean Floor Depth and Normals)
    // =========================================================================
    // ETOPO data includes bathymetry (ocean floor elevation)
    // We extract this BEFORE masking the terrain to create ocean depth maps
    // These are used for subsurface scattering calculations

    std::cout << "Extracting bathymetry data..." << '\n';

    // Bathymetry output files
    std::string bathymetryHeightPath = outputPath + "/earth_bathymetry_heightmap.png";
    std::string bathymetryNormalPath = outputPath + "/earth_bathymetry_normal.png";

    // Combined normal map output file (for shadows - includes both landmass and bathymetry)
    std::string combinedNormalPath = outputPath + "/earth_combined_normal.png";

    // CRITICAL: Save original heightmap BEFORE masking for combined normal map generation
    // This preserves relative curves between land and ocean floor
    std::vector<unsigned char> originalHeightmap(outWidth * outHeight);
    for (int i = 0; i < outWidth * outHeight; i++)
    {
        originalHeightmap[i] = heightmapEquirect[i];
    }

    // Create bathymetry buffers (in equirectangular)
    std::vector<unsigned char> bathymetryHeight(outWidth * outHeight, 128); // Sea level default
    std::vector<unsigned char> bathymetryNormal(outWidth * outHeight * 3);

    // Initialize normals to flat (pointing up)
    for (int i = 0; i < outWidth * outHeight; i++)
    {
        bathymetryNormal[i * 3 + 0] = 128;
        bathymetryNormal[i * 3 + 1] = 128;
        bathymetryNormal[i * 3 + 2] = 255;
    }

    // =========================================================================
    // Apply landmass mask to flatten ocean areas (terrain) AND extract bathymetry
    // =========================================================================
    // Load landmass mask file (white=land, black=ocean) to detect oceans
    // Note: The landmass mask may be in cubemap format, so we sample it accordingly

    std::cout << "Applying landmass mask and generating bathymetry..." << '\n';

    // Load or generate landmass mask
    std::string landmaskPath = outputPath + "/earth_landmass_mask.png";
    if (!std::filesystem::exists(landmaskPath))
    {
        std::cout << "  Landmass mask not found, generating it..." << '\n';
        if (!preprocessLandmassMask(defaultsPath, outputBasePath, resolution))
        {
            std::cout << "  ERROR: Failed to generate landmass mask" << '\n';
            delete[] heightmapEquirect;
            delete[] normalMapEquirect;
            return false;
        }
    }

    // Load the landmass mask (will be cubemap format after preprocessing)
    int maskW, maskH, maskC;
    unsigned char *maskData = stbi_load(landmaskPath.c_str(), &maskW, &maskH, &maskC, 1);

    if (!maskData)
    {
        std::cout << "  ERROR: Failed to load landmass mask: " << landmaskPath << '\n';
        delete[] heightmapEquirect;
        delete[] normalMapEquirect;
        return false;
    }

    // Determine if mask is cubemap (height = 6 * width) or equirectangular (height = width / 2)
    bool maskIsCubemap = isCubemapGridDimensions(maskW, maskH);
    int maskFaceSize = maskIsCubemap ? getFaceSizeFromGridDimensions(maskW, maskH) : 0;

    std::cout << "  Loaded landmass mask: " << maskW << "x" << maskH
              << (maskIsCubemap ? " (cubemap)" : " (equirectangular/sinusoidal)") << '\n';

    int oceanPixels = 0;
    int landPixels = 0;

    // First pass: identify water pixels using mask and compute statistics
    std::vector<bool> isOcean(outWidth * outHeight, false);
    float minOceanDepth = 255.0f, maxOceanDepth = 0.0f;

    for (int y = 0; y < outHeight; y++)
    {
        for (int x = 0; x < outWidth; x++)
        {
            unsigned char maskVal = 255; // Default to land

            if (maskIsCubemap)
            {
                // Convert equirectangular UV to direction, then sample cubemap mask
                float u = static_cast<float>(x) / (outWidth - 1);
                float v = static_cast<float>(y) / (outHeight - 1);
                float dirX, dirY, dirZ;
                equirectangularUVToDirection(u, v, dirX, dirY, dirZ);

                // Sample cubemap mask using the direction
                sampleCubemapStripUChar(maskData, maskFaceSize, 1, dirX, dirY, dirZ, &maskVal);
            }
            else
            {
                // Sample from mask (sinusoidal or equirectangular space)
                int mx = static_cast<int>(static_cast<float>(x) / (outWidth - 1) * (maskW - 1));
                int my = static_cast<int>(static_cast<float>(y) / (outHeight - 1) * (maskH - 1));
                mx = std::min(mx, maskW - 1);
                my = std::min(my, maskH - 1);
                maskVal = maskData[my * maskW + mx];
            }

            // Mask: 0 = ocean (black), 255 = land (white)
            bool isWater = (maskVal == 0);

            int dstIdx = y * outWidth + x;
            isOcean[dstIdx] = isWater;

            if (isWater)
            {
                oceanPixels++;
                float depth = heightmapEquirect[dstIdx];
                minOceanDepth = std::min(minOceanDepth, depth);
                maxOceanDepth = std::max(maxOceanDepth, depth);
            }
            else
            {
                landPixels++;
            }
        }
    }

    stbi_image_free(maskData);

    std::cout << "  Ocean pixels: " << oceanPixels << ", Land pixels: " << landPixels << '\n';
    std::cout << "  Raw depth range: " << minOceanDepth << " - " << maxOceanDepth << '\n';

    // Second pass: create terrain and bathymetry maps in equirectangular
    for (int y = 0; y < outHeight; y++)
    {
        for (int x = 0; x < outWidth; x++)
        {
            int dstIdx = y * outWidth + x;

            if (isOcean[dstIdx])
            {
                // === BATHYMETRY: Keep raw ocean floor data ===
                unsigned char rawHeight = heightmapEquirect[dstIdx];

                // Invert and normalize depth for shader
                float normalizedDepth = 0.0f;
                if (rawHeight < 128)
                {
                    normalizedDepth = (128.0f - rawHeight) / 128.0f;
                }
                bathymetryHeight[dstIdx] = static_cast<unsigned char>(normalizedDepth * 255.0f);

                // Copy raw normal data
                bathymetryNormal[dstIdx * 3 + 0] = normalMapEquirect[dstIdx * 3 + 0];
                bathymetryNormal[dstIdx * 3 + 1] = normalMapEquirect[dstIdx * 3 + 1];
                bathymetryNormal[dstIdx * 3 + 2] = normalMapEquirect[dstIdx * 3 + 2];

                // === TERRAIN: Flatten ocean to sea level ===
                heightmapEquirect[dstIdx] = 128;
                normalMapEquirect[dstIdx * 3 + 0] = 128;
                normalMapEquirect[dstIdx * 3 + 1] = 128;
                normalMapEquirect[dstIdx * 3 + 2] = 255;
            }
            else
            {
                // === BATHYMETRY: Land areas masked to sea level ===
                bathymetryHeight[dstIdx] = 0;
            }
        }
    }

    float oceanPercent = 100.0f * oceanPixels / (outWidth * outHeight);
    std::cout << "  Processed " << oceanPixels << " ocean pixels (" << oceanPercent << "%)" << '\n';

    // =========================================================================
    // Generate Bathymetry Normal Map from Bathymetry Heightmap
    // =========================================================================
    std::cout << "Generating bathymetry normal map..." << '\n';

    const float bathymetryHeightScale = 11000.0f / 255.0f;
    unsigned char *bathymetryNormalTemp = generateNormalMap(bathymetryHeight.data(), outWidth, outHeight, bathymetryHeightScale);
    if (bathymetryNormalTemp)
    {
        // Overwrite land areas with flat normals
        for (int y = 0; y < outHeight; y++)
        {
            for (int x = 0; x < outWidth; x++)
            {
                int dstIdx = y * outWidth + x;
                if (!isOcean[dstIdx])
                {
                    bathymetryNormalTemp[dstIdx * 3 + 0] = 128;
                    bathymetryNormalTemp[dstIdx * 3 + 1] = 128;
                    bathymetryNormalTemp[dstIdx * 3 + 2] = 255;
                }
            }
        }
        memcpy(bathymetryNormal.data(), bathymetryNormalTemp, outWidth * outHeight * 3);
        delete[] bathymetryNormalTemp;
    }

    // =========================================================================
    // Generate Combined Normal Map (Landmass + Bathymetry) for Shadows
    // =========================================================================
    std::cout << "Generating combined normal map (landmass + bathymetry) for shadows..." << '\n';

    float combinedHeightScale = 50.0f;
    unsigned char *combinedNormalMap = generateNormalMap(originalHeightmap.data(), outWidth, outHeight, combinedHeightScale);

    // =========================================================================
    // Convert all textures to cubemap format
    // =========================================================================
    std::cout << "Converting all textures to cubemap format..." << '\n';

    int faceSize = calculateCubemapFaceSize(outWidth, outHeight);
    int cubemapWidth, cubemapHeight;
    getCubemapStripDimensions(faceSize, cubemapWidth, cubemapHeight);

    // Convert heightmap to cubemap
    unsigned char *heightmapCubemap = convertEquirectangularToCubemapUChar(heightmapEquirect, outWidth, outHeight, 1, faceSize);
    delete[] heightmapEquirect;

    // Convert normal map to cubemap
    unsigned char *normalMapCubemap = convertEquirectangularToCubemapUChar(normalMapEquirect, outWidth, outHeight, 3, faceSize);
    delete[] normalMapEquirect;

    // Convert bathymetry heightmap to cubemap
    unsigned char *bathymetryCubemap = convertEquirectangularToCubemapUChar(bathymetryHeight.data(), outWidth, outHeight, 1, faceSize);

    // Convert bathymetry normal map to cubemap
    unsigned char *bathymetryNormalCubemap = convertEquirectangularToCubemapUChar(bathymetryNormal.data(), outWidth, outHeight, 3, faceSize);

    // Convert combined normal map to cubemap
    unsigned char *combinedNormalCubemap = nullptr;
    if (combinedNormalMap)
    {
        combinedNormalCubemap = convertEquirectangularToCubemapUChar(combinedNormalMap, outWidth, outHeight, 3, faceSize);
        delete[] combinedNormalMap;
    }

    // =========================================================================
    // Save all cubemap textures
    // =========================================================================
    std::cout << "Saving cubemap textures..." << '\n';

    // Save bathymetry heightmap cubemap
    if (bathymetryCubemap)
    {
        std::cout << "Saving bathymetry depth cubemap: " << bathymetryHeightPath << '\n';
        if (!stbi_write_png(bathymetryHeightPath.c_str(), cubemapWidth, cubemapHeight, 1, bathymetryCubemap, cubemapWidth))
        {
            std::cerr << "  WARNING: Failed to save bathymetry heightmap" << '\n';
        }
        delete[] bathymetryCubemap;
    }

    // Save bathymetry normal map cubemap
    if (bathymetryNormalCubemap)
    {
        std::cout << "Saving bathymetry normal cubemap: " << bathymetryNormalPath << '\n';
        if (!stbi_write_png(bathymetryNormalPath.c_str(), cubemapWidth, cubemapHeight, 3, bathymetryNormalCubemap, cubemapWidth * 3))
        {
            std::cerr << "  WARNING: Failed to save bathymetry normal map" << '\n';
        }
        delete[] bathymetryNormalCubemap;
    }

    // Save combined normal map cubemap
    if (combinedNormalCubemap)
    {
        std::cout << "Saving combined normal cubemap: " << combinedNormalPath << '\n';
        if (!stbi_write_png(combinedNormalPath.c_str(), cubemapWidth, cubemapHeight, 3, combinedNormalCubemap, cubemapWidth * 3))
        {
            std::cerr << "  WARNING: Failed to save combined normal map" << '\n';
        }
        delete[] combinedNormalCubemap;
    }

    // Save heightmap cubemap
    if (heightmapCubemap)
    {
        std::cout << "Saving heightmap cubemap: " << heightmapPath << '\n';
        if (!stbi_write_png(heightmapPath.c_str(), cubemapWidth, cubemapHeight, 1, heightmapCubemap, cubemapWidth))
        {
            std::cerr << "Failed to save heightmap" << '\n';
            delete[] heightmapCubemap;
            if (normalMapCubemap) delete[] normalMapCubemap;
            std::cout << "===================================" << '\n';
            return false;
        }
        delete[] heightmapCubemap;
    }

    // Save normal map cubemap
    if (normalMapCubemap)
    {
        std::cout << "Saving normal map cubemap: " << normalMapPath << '\n';
        if (!stbi_write_png(normalMapPath.c_str(), cubemapWidth, cubemapHeight, 3, normalMapCubemap, cubemapWidth * 3))
        {
            std::cerr << "Failed to save normal map" << '\n';
            delete[] normalMapCubemap;
            std::cout << "===================================" << '\n';
            return false;
        }
        delete[] normalMapCubemap;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "Elevation processing complete in " << (duration.count() / 1000.0) << "s" << '\n';
    std::cout << "===================================" << '\n';

    return true;
}
