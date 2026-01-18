#include "../../../concerns/constants.h"
#include "../../helpers/cubemap-conversion.h"
#include "../earth-material.h"


#include <algorithm>
#include <cctype> // For std::tolower
#include <cmath>
#include <filesystem>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

// ============================================================================
// Helper functions for water detection using MNDWI algorithm
// ============================================================================
namespace
{
// ============================================================================
// Helper function: Sample elevation data at equirectangular coordinates
// ============================================================================
// Samples elevation data with bilinear interpolation.
// - x, y: Pixel coordinates in output space
// - width, height: Dimensions of output space
float sampleElevation(float *elevationData,
                      int elevationWidth,
                      int elevationHeight,
                      int x,
                      int y,
                      int width,
                      int height)
{
    // Convert to UV coordinates [0, 1]
    float u = static_cast<float>(x) / (width - 1);
    float v = static_cast<float>(y) / (height - 1);

    // Convert to pixel coordinates in elevation data
    float srcX = u * (elevationWidth - 1);
    float srcY = v * (elevationHeight - 1);

    // Bilinear interpolation
    int x0 = static_cast<int>(srcX);
    int y0 = static_cast<int>(srcY);
    int x1 = std::min(x0 + 1, elevationWidth - 1);
    int y1 = std::min(y0 + 1, elevationHeight - 1);

    float fx = srcX - x0;
    float fy = srcY - y0;

    float h00 = elevationData[y0 * elevationWidth + x0];
    float h10 = elevationData[y0 * elevationWidth + x1];
    float h01 = elevationData[y1 * elevationWidth + x0];
    float h11 = elevationData[y1 * elevationWidth + x1];

    float hVal = h00 * (1.0f - fx) * (1.0f - fy) + h10 * fx * (1.0f - fy) + h01 * (1.0f - fx) * fy + h11 * fx * fy;

    return hVal;
}

// ============================================================================
// Helper function: Convert RGB to HSV
// ============================================================================
void rgbToHsv(float r, float g, float b, float &h, float &s, float &v)
{
    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float delta = maxVal - minVal;

    v = maxVal;

    if (maxVal < 0.001f)
    {
        s = 0.0f;
        h = 0.0f;
        return;
    }

    s = delta / maxVal;

    if (delta < 0.001f)
    {
        h = 0.0f;
        return;
    }

    if (maxVal == r)
    {
        h = 60.0f * (((g - b) / delta));
        if (h < 0.0f)
            h += 360.0f;
    }
    else if (maxVal == g)
    {
        h = 60.0f * (((b - r) / delta) + 2.0f);
    }
    else
    {
        h = 60.0f * (((r - g) / delta) + 4.0f);
    }
}

// ============================================================================
// Helper function: Calculate Modified Normalized Difference Water Index (MNDWI)
// ============================================================================
// MNDWI = (Green - SWIR) / (Green + SWIR)
// Since we only have RGB, we use a modified version:
// Modified MNDWI (RGB) = (Green - Red) / (Green + Red)
// This works because water absorbs red more than green, similar to SWIR behavior
float calculateMNDWI_RGB(float r, float g, float b)
{
    float numerator = g - r;
    float denominator = g + r;

    // Avoid division by zero
    if (std::abs(denominator) < 0.001f)
        return 0.0f;

    return numerator / denominator;
}

// ============================================================================
// Helper function: Calculate NDWI-like index using visible bands
// ============================================================================
// NDWI = (Green - NIR) / (Green + NIR)
// Since we only have RGB, we approximate with:
// Visible NDWI = (Green - Red) / (Green + Red)
// Water typically has higher green reflectance and lower red reflectance
float calculateVisibleNDWI(float r, float g, float b)
{
    return calculateMNDWI_RGB(r, g, b);
}

// ============================================================================
// Helper function: Check if pixel is water using MNDWI + HSV analysis
// ============================================================================
// Uses multiple water detection methods:
// 1. MNDWI (Modified NDWI using RGB approximation) - but requires blue dominance
// 2. HSV color space analysis (hue in blue range, avoiding green/cyan)
// 3. Blue dominance ratios (blue must dominate both red AND green)
// 4. Shallow water detection (brighter blue-green colors)
// Returns true if pixel is likely water
bool isWaterPixel(float r, float g, float b)
{
    // Calculate water indices
    float mndwi = calculateMNDWI_RGB(r, g, b);

    // CRITICAL: Blue must dominate green to avoid vegetation false positives
    // Vegetation has green > blue, water has blue >= green
    float blueGreenRatio = b / std::max(g, 0.01f);
    bool blueDominatesGreen = blueGreenRatio >= 0.9f; // Blue at least 90% of green

    // Blue dominance over red
    float blueRedRatio = b / std::max(r, 0.01f);
    bool blueDominatesRed = blueRedRatio > 1.1f; // Blue significantly stronger than red

    // MNDWI threshold: water typically has MNDWI > 0.0, but we need higher threshold
    // to avoid green vegetation (which also has G > R)
    bool mndwiWater = mndwi > 0.1f;       // Raised threshold to avoid vegetation
    bool strongMndwiWater = mndwi > 0.2f; // Strong water signal

    // Convert to HSV for color space analysis
    float h, s, v;
    rgbToHsv(r, g, b, h, s, v);

    // HSV-based water detection:
    // - Hue: Water is in blue range (180-240 degrees)
    //   AVOID cyan/green range (0-150) to prevent vegetation false positives
    // - Saturation: Water has medium saturation
    // - Value: Can vary widely (deep water is dark, shallow is bright)

    bool hueInWaterRange = false;
    if (h >= 180.0f && h <= 240.0f) // Blue range only
    {
        hueInWaterRange = true;
    }
    else if (h >= 170.0f && h <= 190.0f) // Blue-green (coastal water) - narrow range
    {
        // Only accept if blue clearly dominates green
        if (blueGreenRatio >= 0.95f)
            hueInWaterRange = true;
    }

    // HSV water detection - require blue hue AND blue dominance
    bool hsvWater = hueInWaterRange && s > 0.12f && v > 0.03f && v < 0.95f && blueDominatesGreen;

    // Shallow water detection: brighter blue-green colors
    // Shallow water is often brighter and may have more green tint
    bool shallowWater = false;
    if (h >= 170.0f && h <= 200.0f && v > 0.15f && v < 0.8f && s > 0.1f)
    {
        // Shallow water: blue-green hue, but blue still dominates
        if (blueGreenRatio >= 0.85f && blueRedRatio > 1.05f)
        {
            shallowWater = true;
        }
    }

    // Very dark water (deep ocean) - lower saturation threshold
    bool deepWater = false;
    if (h >= 180.0f && h <= 240.0f && v < 0.3f && v > 0.02f && blueDominatesRed && blueDominatesGreen)
    {
        deepWater = true;
    }

    // Combine evidence with stricter requirements:
    // 1. Strong MNDWI + blue dominance = definitely water
    if (strongMndwiWater && blueDominatesGreen && blueDominatesRed)
        return true;

    // 2. MNDWI positive + HSV matches + blue dominance = water
    if (mndwiWater && hsvWater && blueDominatesRed)
        return true;

    // 3. Shallow water detection (brighter blue-green)
    if (shallowWater)
        return true;

    // 4. Deep water detection (dark blue)
    if (deepWater)
        return true;

    // 5. HSV matches + strong blue dominance (for edge cases)
    if (hsvWater && blueRedRatio > 1.2f && blueGreenRatio >= 0.95f)
        return true;

    return false;
}

// ============================================================================
// Helper function: Recursively expand water mask from perimeter
// ============================================================================
// Uses a queue-based flood fill starting from the water perimeter to
// recursively expand outward, collecting all water pixels until reaching
// continent edges. Uses raw elevation data to ensure we don't expand to elevated land.
// - elevationData: Optional raw elevation data in meters (nullptr if not available)
// - elevationWidth/Height: Dimensions of elevation data
// - elevationMin/Max: Min/max elevation values for normalization
// - seaLevel: Actual sea level in meters (calculated from elevation data)
void expandWaterMask(std::vector<unsigned char> &waterMask,
                     unsigned char *colorData,
                     int width,
                     int height,
                     int colorChannels,
                     int colorWidth,
                     int colorHeight,
                     float *elevationData = nullptr,
                     int elevationWidth = 0,
                     int elevationHeight = 0,
                     float elevationMin = 0.0f,
                     float elevationMax = 0.0f,
                     float seaLevel = 0.0f)
{
    const int neighborOffsets[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};

    std::cout << "  Expanding water mask recursively from perimeter..." << '\n';
    if (elevationData)
    {
        std::cout << "    Using elevation data constraint (sea level = " << seaLevel << "m)" << '\n';
    }
    else
    {
        std::cout << "    No elevation data available, using color-only detection" << '\n';
    }

    // Find all perimeter pixels (water pixels adjacent to land)
    std::queue<std::pair<int, int>> perimeterQueue;
    std::vector<bool> visited(width * height, false);

    const float SEA_LEVEL_METERS = seaLevel;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = y * width + x;

            // Skip if not water
            if (waterMask[idx] != 0)
                continue;

            // Check if this water pixel has at least one land neighbor (perimeter)
            bool isPerimeter = false;
            for (int n = 0; n < 8; n++)
            {
                int nx = x + neighborOffsets[n][0];
                int ny = y + neighborOffsets[n][1];

                if (nx < 0 || nx >= width || ny < 0 || ny >= height)
                    continue;

                int nIdx = ny * width + nx;
                if (waterMask[nIdx] != 0) // Neighbor is land
                {
                    isPerimeter = true;
                    break;
                }
            }

            if (isPerimeter)
            {
                perimeterQueue.push({x, y});
                visited[idx] = true;
            }
        }
    }

    std::cout << "    Found " << perimeterQueue.size() << " perimeter pixels to expand from" << '\n';

    // Recursively expand from perimeter
    int totalExpanded = 0;
    int iteration = 0;

    while (!perimeterQueue.empty())
    {
        auto [x, y] = perimeterQueue.front();
        perimeterQueue.pop();

        // Check all neighbors of this perimeter pixel
        for (int n = 0; n < 8; n++)
        {
            int nx = x + neighborOffsets[n][0];
            int ny = y + neighborOffsets[n][1];

            // Skip out of bounds
            if (nx < 0 || nx >= width || ny < 0 || ny >= height)
                continue;

            int nIdx = ny * width + nx;

            // Skip if already water or already visited
            if (waterMask[nIdx] == 0 || visited[nIdx])
                continue;

            // Sample color at neighbor pixel
            int cx = static_cast<int>(static_cast<float>(nx) / (width - 1) * (colorWidth - 1));
            int cy = static_cast<int>(static_cast<float>(ny) / (height - 1) * (colorHeight - 1));
            cx = std::min(cx, colorWidth - 1);
            cy = std::min(cy, colorHeight - 1);

            int colorIdx = cy * colorWidth + cx;
            float r = colorData[colorIdx * colorChannels + 0] / 255.0f;
            float g = colorData[colorIdx * colorChannels + 1] / 255.0f;
            float b = colorData[colorIdx * colorChannels + 2] / 255.0f;

            // Check elevation constraint: only expand to pixels at or below sea level
            bool elevationOk = true;
            if (elevationData && elevationWidth > 0 && elevationHeight > 0)
            {
                // Sample elevation data directly in equirectangular coordinates
                float elevationValue =
                    sampleElevation(elevationData, elevationWidth, elevationHeight, nx, ny, width, height);

                // Only allow expansion to pixels at or below sea level
                // Allow small tolerance (up to 5 meters) to account for noise and coastal variations
                elevationOk = (elevationValue <= seaLevel + 5.0f);
            }

            // Check if neighbor is water using MNDWI + HSV analysis
            if (elevationOk && isWaterPixel(r, g, b))
            {
                // Add to water mask and continue expanding from here
                waterMask[nIdx] = 0; // Mark as water
                visited[nIdx] = true;
                perimeterQueue.push({nx, ny});
                totalExpanded++;

                if (totalExpanded % 10000 == 0)
                {
                    std::cout << "    Expanded " << totalExpanded << " pixels..." << '\n';
                }
            }
        }

        iteration++;
    }

    std::cout << "    Total expanded: " << totalExpanded << " pixels in " << iteration << " iterations" << '\n';
}

// ============================================================================
// Helper function: Remove small isolated land islands
// ============================================================================
// Finds small isolated land regions (islands) that are only ~3 pixels in radius
// and converts them to water (ocean). This removes noise and small false positives.
// - maxRadius: Maximum radius in pixels for an island to be removed (default: 3)
void removeSmallLandIslands(std::vector<unsigned char> &waterMask, int width, int height, int maxRadius = 3)
{
    std::cout << "  Removing small land islands (radius <= " << maxRadius << " pixels)..." << '\n';

    const int neighborOffsets[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
    std::vector<bool> visited(width * height, false);
    int islandsRemoved = 0;
    int totalPixelsRemoved = 0;

    // Find all land pixels (value = 255) and check if they're part of small islands
    for (int startY = 0; startY < height; startY++)
    {
        for (int startX = 0; startX < width; startX++)
        {
            int startIdx = startY * width + startX;

            // Skip if already visited or not land
            if (visited[startIdx] || waterMask[startIdx] != 255)
                continue;

            // Flood fill to find connected land component
            std::queue<std::pair<int, int>> componentQueue;
            std::vector<std::pair<int, int>> componentPixels;

            componentQueue.push({startX, startY});
            visited[startIdx] = true;

            int minX = startX, maxX = startX;
            int minY = startY, maxY = startY;

            while (!componentQueue.empty())
            {
                auto [x, y] = componentQueue.front();
                componentQueue.pop();

                componentPixels.push_back({x, y});

                // Update bounding box
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);

                // Check all neighbors
                for (int n = 0; n < 8; n++)
                {
                    int nx = x + neighborOffsets[n][0];
                    int ny = y + neighborOffsets[n][1];

                    if (nx < 0 || nx >= width || ny < 0 || ny >= height)
                        continue;

                    int nIdx = ny * width + nx;

                    // Add unvisited land pixels to component
                    if (!visited[nIdx] && waterMask[nIdx] == 255)
                    {
                        visited[nIdx] = true;
                        componentQueue.push({nx, ny});
                    }
                }
            }

            // Calculate radius of component (half of diagonal of bounding box)
            int widthComponent = maxX - minX + 1;
            int heightComponent = maxY - minY + 1;
            float radius = std::sqrt(widthComponent * widthComponent + heightComponent * heightComponent) / 2.0f;

            // If component is small enough, convert to water
            if (radius <= static_cast<float>(maxRadius))
            {
                for (const auto &[x, y] : componentPixels)
                {
                    int idx = y * width + x;
                    waterMask[idx] = 0; // Mark as water
                }
                islandsRemoved++;
                totalPixelsRemoved += static_cast<int>(componentPixels.size());
            }
        }
    }

    std::cout << "    Removed " << islandsRemoved << " small land islands (" << totalPixelsRemoved
              << " pixels converted to water)" << '\n';
}

// ============================================================================
// Helper function: Erode edges by reducing white pixels near non-white neighbors
// ============================================================================
// Finds white pixels (land) that have non-white (water) nearby pixels and
// reduces their value based on the proportion of non-white neighbors.
// This "pulls the edge" closer to shorelines before denoising.
// - erosionRadius: Radius to check for non-white neighbors (default: 2)
void erodeEdges(std::vector<unsigned char> &landmask, int width, int height, int erosionRadius = 2)
{
    std::cout << "  Eroding edges to pull shorelines closer (radius: " << erosionRadius << ")..." << '\n';

    std::vector<unsigned char> result(landmask);

    const int neighborOffsets[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};
    int pixelsEroded = 0;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = y * width + x;
            unsigned char pixelValue = landmask[idx];

            // Only process white pixels (land)
            if (pixelValue != 255)
            {
                continue;
            }

            // Count non-white neighbors within radius
            int nonWhiteCount = 0;
            int totalNeighbors = 0;

            for (int dy = -erosionRadius; dy <= erosionRadius; dy++)
            {
                for (int dx = -erosionRadius; dx <= erosionRadius; dx++)
                {
                    // Skip center pixel
                    if (dx == 0 && dy == 0)
                        continue;

                    int nx = x + dx;
                    int ny = y + dy;

                    if (nx < 0 || nx >= width || ny < 0 || ny >= height)
                    {
                        // Out of bounds counts as non-white (edge of image)
                        nonWhiteCount++;
                        totalNeighbors++;
                        continue;
                    }

                    int nIdx = ny * width + nx;
                    totalNeighbors++;

                    // Check if neighbor is non-white (water or gray)
                    if (landmask[nIdx] != 255)
                    {
                        nonWhiteCount++;
                    }
                }
            }

            if (totalNeighbors == 0)
            {
                continue;
            }

            // Calculate proportion of non-white neighbors
            float nonWhiteRatio = static_cast<float>(nonWhiteCount) / static_cast<float>(totalNeighbors);

            // Reduce pixel value based on non-white neighbor ratio
            // More non-white neighbors = more reduction
            // Formula: newValue = 255 * (1 - nonWhiteRatio * reductionStrength)
            // reductionStrength controls how aggressive the erosion is (0.0 to 1.0)
            const float reductionStrength = 0.7f; // 70% reduction for fully surrounded pixels
            float reduction = nonWhiteRatio * reductionStrength;
            float newValue = 255.0f * (1.0f - reduction);

            // Only reduce if there are non-white neighbors
            if (nonWhiteCount > 0)
            {
                result[idx] = static_cast<unsigned char>(std::max(0.0f, std::min(255.0f, newValue)));
                pixelsEroded++;
            }
        }
    }

    // Copy result back
    landmask = result;

    std::cout << "    Eroded " << pixelsEroded << " edge pixels" << '\n';
}

// ============================================================================
// Helper function: Generate denoising mask using Gaussian blur + invert
// ============================================================================
// Applies Gaussian blur to the landmass mask, then inverts it to create
// a denoising application mask. This is much faster than distance-based
// gradient generation.
// - sigma: Gaussian blur radius (standard deviation in pixels, default: 8.0)
void generateDenoiseMask(const std::vector<unsigned char> &landmask,
                         std::vector<unsigned char> &denoiseMask,
                         int width,
                         int height,
                         float sigma = 8.0f)
{
    std::cout << "  Generating denoising mask using Gaussian blur (sigma: " << sigma << ")..." << '\n';

    denoiseMask.resize(width * height, 0);

    // Calculate kernel size (3*sigma on each side, rounded up)
    int kernelRadius = static_cast<int>(std::ceil(3.0f * sigma));
    int kernelSize = kernelRadius * 2 + 1;

    // Generate 1D Gaussian kernel
    std::vector<float> kernel(kernelSize);
    float sum = 0.0f;
    for (int i = 0; i < kernelSize; i++)
    {
        int offset = i - kernelRadius;
        float value = std::exp(-(offset * offset) / (2.0f * sigma * sigma));
        kernel[i] = value;
        sum += value;
    }
    // Normalize kernel
    for (int i = 0; i < kernelSize; i++)
    {
        kernel[i] /= sum;
    }

    // Temporary buffer for horizontal pass
    std::vector<float> tempBuffer(width * height, 0.0f);

    // Horizontal pass
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            float sum = 0.0f;
            for (int k = 0; k < kernelSize; k++)
            {
                int offset = k - kernelRadius;
                int sampleX = x + offset;
                // Clamp to edges
                sampleX = std::max(0, std::min(width - 1, sampleX));
                int idx = y * width + sampleX;
                sum += static_cast<float>(landmask[idx]) * kernel[k];
            }
            tempBuffer[y * width + x] = sum;
        }
    }

    // Vertical pass and invert
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = y * width + x;
            float sum = 0.0f;
            for (int k = 0; k < kernelSize; k++)
            {
                int offset = k - kernelRadius;
                int sampleY = y + offset;
                // Clamp to edges
                sampleY = std::max(0, std::min(height - 1, sampleY));
                int sampleIdx = sampleY * width + x;
                sum += tempBuffer[sampleIdx] * kernel[k];
            }
            // Invert: white (255) becomes black (0), black (0) becomes white (255)
            float inverted = 255.0f - sum;
            denoiseMask[idx] = static_cast<unsigned char>(std::max(0.0f, std::min(255.0f, inverted)));
        }
    }

    // Step 3: Set any pure white (255) pixels in the gradient to black (0)
    // This ensures no pure white pixels exist in the final denoising mask
    int whitePixelsRemoved = 0;
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = y * width + x;
            if (denoiseMask[idx] == 255) // Pure white in gradient
            {
                denoiseMask[idx] = 0; // Set to black
                whitePixelsRemoved++;
            }
        }
    }

    std::cout << "    Generated denoising mask (removed " << whitePixelsRemoved << " pure white pixels)" << '\n';
}

// ============================================================================
// Helper function: Apply denoising to mask using gradient mask
// ============================================================================
// Applies denoising to the landmass mask, weighted by the denoising mask.
// Uses a small kernel (3x3 or 5x5) suitable for single-pixel noise clusters.
// - kernelSize: Size of denoising kernel (3 or 5, default: 3 for single pixels)
void applyDenoising(std::vector<unsigned char> &landmask,
                    const std::vector<unsigned char> &denoiseMask,
                    int width,
                    int height,
                    int kernelSize = 3)
{
    std::cout << "  Applying denoising to landmass mask (kernel: " << kernelSize << "x" << kernelSize << ")..." << '\n';

    std::vector<unsigned char> result(width * height);
    int kernelRadius = kernelSize / 2;
    int pixelsDenoised = 0;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = y * width + x;
            unsigned char denoiseStrength = denoiseMask[idx];

            // If denoising strength is 0 (black), don't denoise
            if (denoiseStrength == 0)
            {
                result[idx] = landmask[idx];
                continue;
            }

            unsigned char originalValue = landmask[idx];

            // Only denoise white pixels (land), not black pixels (water)
            // Black pixels (water) remain unchanged
            if (originalValue == 0)
            {
                result[idx] = originalValue;
                continue;
            }

            // Collect neighboring pixels for median calculation
            std::vector<unsigned char> neighbors;
            neighbors.reserve(kernelSize * kernelSize);

            for (int dy = -kernelRadius; dy <= kernelRadius; dy++)
            {
                for (int dx = -kernelRadius; dx <= kernelRadius; dx++)
                {
                    int nx = x + dx;
                    int ny = y + dy;

                    if (nx < 0 || nx >= width || ny < 0 || ny >= height)
                        continue;

                    int nIdx = ny * width + nx;
                    neighbors.push_back(landmask[nIdx]);
                }
            }

            if (neighbors.empty())
            {
                result[idx] = originalValue;
                continue;
            }

            // Calculate median (better for noise removal than mean)
            std::sort(neighbors.begin(), neighbors.end());
            unsigned char median = neighbors[neighbors.size() / 2];

            // Blend original with denoised based on denoising mask strength
            // denoiseStrength: 0 = no denoising, 255 = full denoising
            // IMPORTANT: Only allow pushing white towards black, never black towards white
            float blendFactor = static_cast<float>(denoiseStrength) / 255.0f;
            float denoisedValue =
                static_cast<float>(originalValue) * (1.0f - blendFactor) + static_cast<float>(median) * blendFactor;

            // Clamp: only allow result to be darker (lower) than original, never brighter
            // This ensures we only remove white noise, never create false white pixels
            float clampedValue = std::min(denoisedValue, static_cast<float>(originalValue));
            result[idx] = static_cast<unsigned char>(std::max(0.0f, std::min(255.0f, clampedValue)));

            if (denoiseStrength > 0 && originalValue > 0)
            {
                pixelsDenoised++;
            }
        }
    }

    // Copy result back to landmask
    landmask = result;

    std::cout << "    Denoised " << pixelsDenoised << " pixels" << '\n';
}
} // namespace

// ============================================================================
// Preprocess Landmass Mask from Blue Marble Color Texture
// ============================================================================
// Creates a landmass mask (white=land, black=ocean) using MNDWI (Modified
// Normalized Difference Water Index) algorithm. Uses a modified MNDWI approach
// adapted for RGB data: (Green - Red) / (Green + Red), combined with HSV color
// space analysis and region growing to capture all water pixels including
// shallow coastal water, turbid zones, and push the mask to continent edges.
// - Input: Blue Marble monthly texture (earth_month_01.jpg/png) in cubemap format
// - Output: earth_landmass_mask.png (cubemap vertical strip format)
// - Used for: Filtering ocean pixels from other textures
// - Algorithm: MNDWI (RGB approximation) + HSV analysis + region growing

bool EarthMaterial::preprocessLandmassMask(const std::string &defaultsPath,
                                           const std::string &outputBasePath,
                                           TextureResolution resolution)
{
    std::string outputPath = outputBasePath + "/" + getResolutionFolderName(resolution);
    std::filesystem::create_directories(outputPath);

    std::string landmaskPath = outputPath + "/earth_landmass_mask.png";
    std::string colorPath = outputPath + "/earth_month_01.jpg";
    if (!std::filesystem::exists(colorPath))
    {
        colorPath = outputPath + "/earth_month_01.png";
    }

    // Check cache: mask exists and is newer than or equal to the color image
    if (std::filesystem::exists(landmaskPath))
    {
        bool colorExists = std::filesystem::exists(colorPath);

        if (colorExists)
        {
            // Check modification times - only rebuild if color image is newer
            auto maskTime = std::filesystem::last_write_time(landmaskPath);
            auto colorTime = std::filesystem::last_write_time(colorPath);

            // If mask is newer than or equal to color image, we're good
            if (maskTime >= colorTime)
            {
                std::cout << "Landmass mask already exists and is up-to-date: " << landmaskPath << '\n';
                return true;
            }
            else
            {
                std::cout << "Landmass mask exists but color image is newer, rebuilding..." << '\n';
            }
        }
        else
        {
            // Color image missing, but mask exists - keep it
            std::cout << "Landmass mask already exists (color image not found): " << landmaskPath << '\n';
            return true;
        }
    }
    // If mask doesn't exist, proceed to generation below

    std::cout << "=== Landmass Mask Generation ===" << '\n';

    // Get output dimensions
    int outWidth, outHeight;
    getResolutionDimensions(resolution, outWidth, outHeight);
    std::cout << "Output dimensions: " << outWidth << "x" << outHeight << " (will convert to cubemap)" << '\n';

    // Load color texture (Blue Marble) - required dependency
    if (!std::filesystem::exists(colorPath))
    {
        std::cout << "  ERROR: Color texture not found. Run preprocessTiles first." << '\n';
        return false;
    }

    int cw, ch, cc;
    unsigned char *colorData = stbi_load(colorPath.c_str(), &cw, &ch, &cc, 0);

    if (!colorData || cc < 3)
    {
        std::cout << "  ERROR: Failed to load color texture" << '\n';
        return false;
    }

    // Check if color texture is in cubemap format (height = 6 * width)
    bool colorIsCubemap = isCubemapStripDimensions(cw, ch);
    std::cout << "  Loaded color data: " << cw << "x" << ch
              << (colorIsCubemap ? " (cubemap)" : " (equirectangular)") << '\n';

    // If color is cubemap, convert to equirectangular for processing
    // This ensures all processing happens in consistent equirectangular space
    unsigned char *colorEquirect = nullptr;
    int colorEquirectW = 0, colorEquirectH = 0;

    if (colorIsCubemap)
    {
        int faceSize = getFaceSizeFromStripDimensions(cw, ch);
        // Use 2:1 aspect ratio for equirectangular (width = 2 * faceSize, height = faceSize)
        colorEquirectW = faceSize * 2;
        colorEquirectH = faceSize;
        std::cout << "  Converting cubemap to equirectangular for processing..." << '\n';
        colorEquirect = convertCubemapToEquirectangularUChar(colorData, faceSize, cc, colorEquirectW, colorEquirectH);
        if (!colorEquirect)
        {
            std::cout << "  ERROR: Failed to convert cubemap to equirectangular" << '\n';
            stbi_image_free(colorData);
            return false;
        }
        // Free original cubemap data, use equirectangular for processing
        stbi_image_free(colorData);
        colorData = colorEquirect;
        cw = colorEquirectW;
        ch = colorEquirectH;
    }

    // Find elevation GeoTIFF file for raw elevation data (optional - helps filter elevated areas)
    std::string elevationSourcePath = defaultsPath + "/earth-surface/elevation";
    std::string elevationTiffPath;

    try
    {
        if (std::filesystem::exists(elevationSourcePath))
        {
            for (const auto &entry : std::filesystem::directory_iterator(elevationSourcePath))
            {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".tif" || ext == ".tiff")
                {
                    elevationTiffPath = entry.path().string();
                    break;
                }
            }
        }
    }
    catch (const std::exception &)
    {
        // Elevation path doesn't exist or can't be accessed
    }

    // Load raw elevation data from GeoTIFF if available (optional - helps filter elevated areas)
    float *elevationData = nullptr;
    int elevationW = 0, elevationH = 0;
    float elevationMin = 0.0f, elevationMax = 0.0f;

    if (!elevationTiffPath.empty() && std::filesystem::exists(elevationTiffPath))
    {
        std::cout << "  Loading raw elevation data from: "
                  << std::filesystem::path(elevationTiffPath).filename().string() << '\n';
        elevationData = loadGeoTiffElevation(elevationTiffPath, elevationW, elevationH);

        if (elevationData)
        {
            // Find min/max elevation for reference
            elevationMin = elevationData[0];
            elevationMax = elevationData[0];
            for (int i = 0; i < elevationW * elevationH; i++)
            {
                float v = elevationData[i];
                if (v > -10000.0f) // Skip NODATA values
                {
                    elevationMin = std::min(elevationMin, v);
                    elevationMax = std::max(elevationMax, v);
                }
            }

            std::cout << "  Loaded elevation data: " << elevationW << "x" << elevationH << '\n';
            std::cout << "    Elevation range: " << elevationMin << "m to " << elevationMax << "m" << '\n';
        }
        else
        {
            std::cout << "  WARNING: Failed to load elevation data, proceeding with color-only detection" << '\n';
        }
    }
    else
    {
        std::cout << "  Elevation GeoTIFF not found, using color-only detection" << '\n';
        std::cout << "    Note: For best results, ensure elevation GeoTIFF is in " << elevationSourcePath << '\n';
    }

    std::vector<unsigned char> landmaskImg(outWidth * outHeight);

    // Calculate actual sea level from elevation data
    float SEA_LEVEL_METERS = 0.0f; // Default to 0 if no elevation data
    if (elevationData && elevationW > 0 && elevationH > 0)
    {
        // Find sea level by analyzing elevation values near 0
        // Use histogram approach: find the most common elevation value in range [-50m, +50m]
        const int HISTOGRAM_BINS = 200;
        const float RANGE_MIN = -50.0f;
        const float RANGE_MAX = 50.0f;
        const float BIN_SIZE = (RANGE_MAX - RANGE_MIN) / HISTOGRAM_BINS;

        std::vector<int> histogram(HISTOGRAM_BINS, 0);
        int validSamples = 0;

        for (int i = 0; i < elevationW * elevationH; i++)
        {
            float v = elevationData[i];
            if (v > -10000.0f && v >= RANGE_MIN && v <= RANGE_MAX) // Skip NODATA and out of range
            {
                int bin = static_cast<int>((v - RANGE_MIN) / BIN_SIZE);
                bin = std::max(0, std::min(HISTOGRAM_BINS - 1, bin));
                histogram[bin]++;
                validSamples++;
            }
        }

        if (validSamples > 0)
        {
            // Find bin with maximum count (mode)
            int maxBin = 0;
            int maxCount = histogram[0];
            for (int i = 1; i < HISTOGRAM_BINS; i++)
            {
                if (histogram[i] > maxCount)
                {
                    maxCount = histogram[i];
                    maxBin = i;
                }
            }

            // Calculate sea level as center of the peak bin
            SEA_LEVEL_METERS = RANGE_MIN + (maxBin + 0.5f) * BIN_SIZE;

            std::cout << "    Calculated sea level from elevation data: " << SEA_LEVEL_METERS << "m" << '\n';
            std::cout << "      (peak bin: " << maxBin << ", count: " << maxCount << " samples)" << '\n';
        }
        else
        {
            std::cout << "    Could not determine sea level from elevation data, using 0m" << '\n';
        }
    }
    else
    {
        std::cout << "    No elevation data available, using sea level = 0m" << '\n';
    }

    // Step 1: Create initial water mask using MNDWI algorithm + elevation constraint
    std::cout << "  Creating initial water mask using MNDWI (Modified NDWI)..." << '\n';
    int initialWaterPixels = 0;
    int initialLandPixels = 0;
    int elevationFilteredPixels = 0;

    for (int y = 0; y < outHeight; y++)
    {
        for (int x = 0; x < outWidth; x++)
        {
            int cx = static_cast<int>(static_cast<float>(x) / (outWidth - 1) * (cw - 1));
            int cy = static_cast<int>(static_cast<float>(y) / (outHeight - 1) * (ch - 1));
            cx = std::min(cx, cw - 1);
            cy = std::min(cy, ch - 1);

            int idx = cy * cw + cx;
            float r = colorData[idx * cc + 0] / 255.0f;
            float g = colorData[idx * cc + 1] / 255.0f;
            float b = colorData[idx * cc + 2] / 255.0f;

            // Use MNDWI-based water detection
            bool isWater = isWaterPixel(r, g, b);

            // Apply elevation constraint: reject water detection if pixel is above sea level
            if (isWater && elevationData && elevationW > 0 && elevationH > 0)
            {
                // Sample elevation data directly in equirectangular coordinates
                float elevationValue =
                    sampleElevation(elevationData, elevationW, elevationH, x, y, outWidth, outHeight);

                // If pixel is significantly above sea level, reject water detection
                // Allow small tolerance (up to 10 meters) for noise and coastal variations
                if (elevationValue > SEA_LEVEL_METERS + 10.0f)
                {
                    isWater = false;
                    elevationFilteredPixels++;
                }
            }

            // Water = 0, Land = 255 (inverted for expansion function)
            landmaskImg[y * outWidth + x] = isWater ? 0 : 255;

            if (isWater)
                initialWaterPixels++;
            else
                initialLandPixels++;
        }
    }

    float initialWaterPercent = 100.0f * initialWaterPixels / (outWidth * outHeight);
    std::cout << "    Initial detection: " << initialWaterPixels << " water pixels (" << initialWaterPercent << "%), "
              << initialLandPixels << " land pixels" << '\n';
    if (elevationFilteredPixels > 0)
    {
        std::cout << "    Elevation data filtered out " << elevationFilteredPixels << " elevated false positives"
                  << '\n';
    }

    // Step 2: Expand water mask from edges to capture all water pixels
    expandWaterMask(landmaskImg,
                    colorData,
                    outWidth,
                    outHeight,
                    cc,
                    cw,
                    ch,
                    elevationData,
                    elevationW,
                    elevationH,
                    elevationMin,
                    elevationMax,
                    SEA_LEVEL_METERS);

    // Step 3: Remove small isolated land islands (convert to water)
    removeSmallLandIslands(landmaskImg, outWidth, outHeight, 3);

    // Step 4: Erode edges to pull shorelines closer before denoising
    erodeEdges(landmaskImg, outWidth, outHeight, 2);

    // Step 5: Generate denoising mask using Gaussian blur + invert
    std::vector<unsigned char> denoiseMask;
    generateDenoiseMask(landmaskImg, denoiseMask, outWidth, outHeight, 32.0f);

    // Step 5: Apply denoising to landmass mask using the gradient mask
    // Step 6: Second denoising pass with larger kernel (9x9) for 8px clusters
    applyDenoising(landmaskImg, denoiseMask, outWidth, outHeight, 9);
    // Step 6: Second denoising pass with larger kernel (9x9) for 8px clusters
    applyDenoising(landmaskImg, denoiseMask, outWidth, outHeight, 6);
    // Step 6: Second denoising pass with larger kernel (9x9) for 8px clusters
    applyDenoising(landmaskImg, denoiseMask, outWidth, outHeight, 4);
    // First pass: Use small kernel (3x3) for single-pixel noise clusters
    applyDenoising(landmaskImg, denoiseMask, outWidth, outHeight, 3);


    // Free elevation data if loaded
    if (elevationData)
    {
        delete[] elevationData;
    }

    // Free color data - use delete[] if we converted from cubemap, stbi_image_free otherwise
    if (colorIsCubemap)
    {
        delete[] colorData; // Was allocated by convertCubemapToEquirectangularUChar
    }
    else
    {
        stbi_image_free(colorData); // Was allocated by stbi_load
    }

    // Convert landmass mask to cubemap format
    std::cout << "  Converting landmass mask to cubemap format..." << '\n';
    int faceSize = calculateCubemapFaceSize(outWidth, outHeight);
    unsigned char *maskCubemap = convertEquirectangularToCubemapUChar(landmaskImg.data(), outWidth, outHeight, 1, faceSize);

    if (!maskCubemap)
    {
        std::cerr << "  ERROR: Failed to convert landmass mask to cubemap" << '\n';
        return false;
    }

    // Get cubemap dimensions
    int cubemapWidth, cubemapHeight;
    getCubemapStripDimensions(faceSize, cubemapWidth, cubemapHeight);

    // Save landmass mask as cubemap
    if (!stbi_write_png(landmaskPath.c_str(), cubemapWidth, cubemapHeight, 1, maskCubemap, cubemapWidth))
    {
        std::cerr << "  ERROR: Failed to save landmass mask" << '\n';
        delete[] maskCubemap;
        return false;
    }

    delete[] maskCubemap;
    std::cout << "  Saved landmass mask cubemap: " << landmaskPath << " (" << cubemapWidth << "x" << cubemapHeight << ")" << '\n';

    // Convert and save denoising mask as cubemap
    std::string denoiseMaskPath = outputPath + "/earth_landmass_gradient.png";
    unsigned char *denoiseCubemap = convertEquirectangularToCubemapUChar(denoiseMask.data(), outWidth, outHeight, 1, faceSize);

    if (denoiseCubemap)
    {
        if (!stbi_write_png(denoiseMaskPath.c_str(), cubemapWidth, cubemapHeight, 1, denoiseCubemap, cubemapWidth))
        {
            std::cerr << "  WARNING: Failed to save denoising mask" << '\n';
        }
        else
        {
            std::cout << "  Saved denoising mask cubemap: " << denoiseMaskPath << '\n';
        }
        delete[] denoiseCubemap;
    }

    return true;
}
