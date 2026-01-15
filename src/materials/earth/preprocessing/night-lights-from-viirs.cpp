#include "../../../concerns/constants.h"
#include "../earth-material.h"


#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

// ============================================================================
// Preprocess NASA Nightlights Snapshots
// ============================================================================
// Combines multiple satellite nightlight images to reduce banding artifacts
// caused by cloud cover and atmospheric Mie scattering.
// Uses max-blend to fill gaps and averaging to reduce noise/banding.
// Source images are assumed to be in equirectangular projection.

bool EarthMaterial::preprocessNightlights(const std::string &defaultsPath,
                                          const std::string &outputBasePath,
                                          TextureResolution resolution)
{
    std::string sourcePath = defaultsPath + "/earth-surface/human-lights";
    std::string outputPath = outputBasePath + "/" + getResolutionFolderName(resolution);

    std::cout << "=== Nightlights Processing ===" << '\n';

    // Check source directory exists
    if (!std::filesystem::exists(sourcePath))
    {
        std::cout << "Nightlights source directory not found: " << sourcePath << '\n';
        std::cout << "==============================" << '\n';
        return false;
    }

    // Create output directory
    std::filesystem::create_directories(outputPath);
    std::string outFile = outputPath + "/earth_nightlights.png";

    // Check if already processed
    if (std::filesystem::exists(outFile))
    {
        std::cout << "Nightlights texture already exists: " << outFile << '\n';
        std::cout << "==============================" << '\n';
        return true;
    }

    // Collect all image files
    std::vector<std::string> sourceFiles;
    for (const auto &entry : std::filesystem::directory_iterator(sourcePath))
    {
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png")
        {
            sourceFiles.push_back(std::filesystem::absolute(entry.path()).string());
            std::cout << "Found: " << entry.path().filename().string() << '\n';
        }
    }

    if (sourceFiles.empty())
    {
        std::cout << "No nightlights images found in " << sourcePath << '\n';
        std::cout << "==============================" << '\n';
        return false;
    }

    std::cout << "Processing " << sourceFiles.size() << " source image(s)..." << '\n';

    // ==========================================================================
    // VIIRS-style Nighttime Lights Processing
    // ==========================================================================
    // Industry standard VIIRS processing uses:
    // 1. Local adaptive thresholding (detect lights above local background)
    // 2. Spatial filtering (lights are small/point-like, clouds are
    // large/diffuse)
    // 3. Multi-temporal compositing (median rejects outliers like
    // fires/lightning)
    // 4. Outlier removal (values far from median are transient, not lights)
    //
    // Since we don't have quality flags, we approximate this with:
    // - Local contrast detection (bright relative to surroundings = light)
    // - Size-based filtering (small bright spots = lights, large bright areas =
    // clouds)
    // - Median compositing across multiple images

    // Working at the largest source resolution for best detail
    int workWidth = 0, workHeight = 0;

    // First pass: determine max resolution
    for (const auto &sourceFile : sourceFiles)
    {
        int w, h, channels;
        if (stbi_info(sourceFile.c_str(), &w, &h, &channels))
        {
            if (w * h > workWidth * workHeight)
            {
                workWidth = w;
                workHeight = h;
            }
        }
    }

    if (workWidth == 0 || workHeight == 0)
    {
        std::cerr << "ERROR: Could not determine image dimensions" << '\n';
        std::cout << "==============================" << '\n';
        return false;
    }

    std::cout << "Working resolution: " << workWidth << "x" << workHeight << '\n';

    // Store all processed images for median compositing
    std::vector<std::vector<float>> processedImages;

    // Local window size for background estimation (in pixels)
    // Larger = better cloud rejection but may miss small towns
    const int WINDOW_RADIUS = 15;

    // Threshold: how much brighter than local background to be considered a light
    const float LOCAL_CONTRAST_THRESHOLD = 0.08f; // 8% above local background

    // Minimum absolute brightness to be considered (rejects dim noise)
    const float MIN_ABSOLUTE_BRIGHTNESS = 0.05f;

    int imagesProcessed = 0;

    for (const auto &sourceFile : sourceFiles)
    {
        std::cout << "  Processing: " << std::filesystem::path(sourceFile).filename().string() << '\n';

        int w, h, channels;
        stbi_set_flip_vertically_on_load(false);
        unsigned char *data = stbi_load(sourceFile.c_str(), &w, &h, &channels, 0);

        if (!data)
        {
            std::cerr << "    WARNING: Failed to load" << '\n';
            continue;
        }

        // Convert to grayscale at working resolution
        std::vector<float> gray(workWidth * workHeight);

        for (int y = 0; y < workHeight; y++)
        {
            for (int x = 0; x < workWidth; x++)
            {
                float srcXf = static_cast<float>(x) / (workWidth - 1) * (w - 1);
                float srcYf = static_cast<float>(y) / (workHeight - 1) * (h - 1);

                int sx = std::min(static_cast<int>(srcXf), w - 1);
                int sy = std::min(static_cast<int>(srcYf), h - 1);
                int srcIdx = sy * w + sx;

                float lum;
                if (channels >= 3)
                {
                    float r = data[srcIdx * channels + 0] / 255.0f;
                    float g = data[srcIdx * channels + 1] / 255.0f;
                    float b = data[srcIdx * channels + 2] / 255.0f;
                    lum = 0.299f * r + 0.587f * g + 0.114f * b;
                }
                else
                {
                    lum = data[srcIdx * channels] / 255.0f;
                }

                gray[y * workWidth + x] = lum;
            }
        }

        stbi_image_free(data);

        // =====================================================================
        // STEP 0: Cross-track Mie scattering correction (swath edge vignette)
        // =====================================================================
        // VIIRS scans in swaths as satellite orbits pole-to-pole
        // At swath edges, viewing angle is oblique → more atmospheric path
        // This causes Mie scattering artifacts (brighter edges)
        //
        // We detect swath boundaries by looking for sudden vertical brightness
        // changes, then apply a vignette (darkening) near those edges

        std::cout << "    Applying cross-track correction..." << '\n';

        // Compute horizontal gradient to detect vertical seams (swath edges)
        // Swaths run roughly N-S, so edges appear as vertical brightness changes
        std::vector<float> hGradient(workWidth * workHeight, 0.0f);

        const int GRADIENT_RADIUS = 5; // Look 5 pixels left/right for gradient

        for (int y = 0; y < workHeight; y++)
        {
            for (int x = GRADIENT_RADIUS; x < workWidth - GRADIENT_RADIUS; x++)
            {
                // Compute difference between left and right neighborhoods
                float leftSum = 0.0f, rightSum = 0.0f;
                for (int dx = 1; dx <= GRADIENT_RADIUS; dx++)
                {
                    leftSum += gray[y * workWidth + (x - dx)];
                    rightSum += gray[y * workWidth + (x + dx)];
                }
                float leftAvg = leftSum / GRADIENT_RADIUS;
                float rightAvg = rightSum / GRADIENT_RADIUS;

                // Gradient magnitude (absolute difference)
                hGradient[y * workWidth + x] = std::abs(rightAvg - leftAvg);
            }
        }

        // Find swath edges: columns with consistently high gradient (vertical
        // lines) Sum gradient vertically to find "seam" columns
        std::vector<float> columnGradient(workWidth, 0.0f);
        for (int x = 0; x < workWidth; x++)
        {
            float sum = 0.0f;
            for (int y = 0; y < workHeight; y++)
            {
                sum += hGradient[y * workWidth + x];
            }
            columnGradient[x] = sum / workHeight;
        }

        // Find peaks in column gradient (swath boundaries)
        // Apply gentle vignette around detected edges to suppress scatter
        const float SEAM_THRESHOLD = 0.015f; // Gradient threshold for seam detection
        const int VIGNETTE_RADIUS = 40;      // How far the darkening extends from seam

        std::vector<float> vignetteMap(workWidth, 1.0f); // 1.0 = no darkening

        for (int x = 0; x < workWidth; x++)
        {
            // Check if this column is near a seam
            float nearestSeamDistance = static_cast<float>(workWidth); // Far away

            // Look for seams within vignette radius
            for (int sx = std::max(0, x - VIGNETTE_RADIUS); sx < std::min(workWidth, x + VIGNETTE_RADIUS); sx++)
            {
                if (columnGradient[sx] > SEAM_THRESHOLD)
                {
                    float dist = static_cast<float>(std::abs(x - sx));
                    nearestSeamDistance = std::min(nearestSeamDistance, dist);
                }
            }

            // Apply gentle vignette based on distance to nearest seam
            // Only suppress (not erase) - preserve city lights at edges
            if (nearestSeamDistance < VIGNETTE_RADIUS)
            {
                // Smooth falloff: mild darkening at seam, none at vignette edge
                float t = nearestSeamDistance / VIGNETTE_RADIUS;
                // Cosine falloff for smooth transition
                // 0.65 at seam (35% reduction) → 1.0 at edge (no effect)
                float darken = 0.65f + 0.35f * (0.5f - 0.5f * std::cos(t * 3.14159f));
                vignetteMap[x] = darken;
            }
        }

        // Apply vignette to the grayscale image
        for (int y = 0; y < workHeight; y++)
        {
            for (int x = 0; x < workWidth; x++)
            {
                gray[y * workWidth + x] *= vignetteMap[x];
            }
        }

        // NOTE: Removed periodic swath vignette - it assumed fixed swath positions
        // but satellite passes occur at different times, so swaths shift between
        // images. The gradient-based vignette above detects actual edges per-image.

        // =====================================================================
        // STEP 1: Compute local background using box blur (approximates median)
        // =====================================================================
        // This estimates the "background" level around each pixel
        // Clouds create elevated backgrounds; clear sky is dark

        std::vector<float> background(workWidth * workHeight, 0.0f);

        // Horizontal pass of box blur
        std::vector<float> tempBlur(workWidth * workHeight, 0.0f);
        for (int y = 0; y < workHeight; y++)
        {
            float sum = 0.0f;
            int count = 0;

            // Initialize window
            for (int x = 0; x <= WINDOW_RADIUS && x < workWidth; x++)
            {
                sum += gray[y * workWidth + x];
                count++;
            }

            for (int x = 0; x < workWidth; x++)
            {
                tempBlur[y * workWidth + x] = sum / count;

                // Slide window
                int removeX = x - WINDOW_RADIUS;
                int addX = x + WINDOW_RADIUS + 1;

                if (removeX >= 0)
                {
                    sum -= gray[y * workWidth + removeX];
                    count--;
                }
                if (addX < workWidth)
                {
                    sum += gray[y * workWidth + addX];
                    count++;
                }
            }
        }

        // Vertical pass of box blur
        for (int x = 0; x < workWidth; x++)
        {
            float sum = 0.0f;
            int count = 0;

            for (int y = 0; y <= WINDOW_RADIUS && y < workHeight; y++)
            {
                sum += tempBlur[y * workWidth + x];
                count++;
            }

            for (int y = 0; y < workHeight; y++)
            {
                background[y * workWidth + x] = sum / count;

                int removeY = y - WINDOW_RADIUS;
                int addY = y + WINDOW_RADIUS + 1;

                if (removeY >= 0)
                {
                    sum -= tempBlur[removeY * workWidth + x];
                    count--;
                }
                if (addY < workHeight)
                {
                    sum += tempBlur[addY * workWidth + x];
                    count++;
                }
            }
        }

        // =====================================================================
        // STEP 2: Extract lights using local contrast
        // =====================================================================
        // A pixel is a "light" if it's significantly brighter than its local
        // background This naturally rejects clouds (which raise the whole local
        // area)

        std::vector<float> lights(workWidth * workHeight, 0.0f);

        for (int i = 0; i < workWidth * workHeight; i++)
        {
            float pixel = gray[i];
            float bg = background[i];

            // Local contrast: how much brighter is this pixel vs background?
            float contrast = pixel - bg;

            // Must exceed local background by threshold AND meet minimum brightness
            if (contrast > LOCAL_CONTRAST_THRESHOLD && pixel > MIN_ABSOLUTE_BRIGHTNESS)
            {
                // Normalize the excess brightness
                // Brighter lights get higher values
                float intensity = (contrast - LOCAL_CONTRAST_THRESHOLD) / (1.0f - LOCAL_CONTRAST_THRESHOLD);
                intensity = std::max(0.0f, std::min(1.0f, intensity));

                // Apply gamma to boost dim lights
                lights[i] = std::pow(intensity, 0.5f);
            }
            else
            {
                lights[i] = 0.0f;
            }
        }

        processedImages.push_back(std::move(lights));
        imagesProcessed++;
        std::cout << "    Extracted lights (" << imagesProcessed << "/" << sourceFiles.size() << ")" << '\n';
    }

    if (imagesProcessed == 0)
    {
        std::cerr << "ERROR: No images could be processed" << '\n';
        std::cout << "==============================" << '\n';
        return false;
    }

    // =========================================================================
    // STEP 3: Consistency-filtered median composite
    // =========================================================================
    // Only keep lights that appear in ALL (or nearly all) source images.
    // This "forgets" edge artifacts and transient lights that only appear
    // in 1-2 datasets, dramatically improving signal quality.
    //
    // Pipeline:
    // 1. Count how many images have non-zero data for each pixel
    // 2. Require pixel to appear in at least (n-1) images (allow 1 gap for
    // clouds)
    // 3. For qualifying pixels, use median of non-zero values

    std::cout << "Creating consistency-filtered composite from " << imagesProcessed << " images..." << '\n';

    // Minimum required occurrences: ~50-60% of source images
    // Use floor to be lenient - allow cloud gaps in nearly half the images
    // For 2 images: require at least 1
    // For 3 images: require at least 2 (allows 1 gap)
    // For 4 images: require at least 2 (allows 2 gaps)
    // For 5 images: require at least 3 (allows 2 gaps)
    // For 6 images: require at least 3 (allows 3 gaps)
    size_t minOccurrences = std::max(size_t(1), static_cast<size_t>(std::floor(imagesProcessed * 0.5 + 0.5)));
    std::cout << "  Requiring data in at least " << minOccurrences << " of " << imagesProcessed << " images (~50%)"
              << '\n';

    std::vector<float> combined(workWidth * workHeight, 0.0f);
    std::vector<float> nonZeroValues;
    nonZeroValues.reserve(processedImages.size());

    size_t keptPixels = 0;
    size_t rejectedPixels = 0;

    for (int i = 0; i < workWidth * workHeight; i++)
    {
        nonZeroValues.clear();

        // Collect only non-zero values and count occurrences
        for (const auto &img : processedImages)
        {
            if (img[i] > 0.001f)
            { // Non-zero threshold
                nonZeroValues.push_back(img[i]);
            }
        }

        size_t occurrences = nonZeroValues.size();

        // Only keep pixel if it appears in enough images
        if (occurrences >= minOccurrences)
        {
            // Sort non-zero values to find median
            std::sort(nonZeroValues.begin(), nonZeroValues.end());

            // Use median of non-zero values
            size_t n = nonZeroValues.size();
            float median;
            if (n == 1)
            {
                median = nonZeroValues[0];
            }
            else if (n % 2 == 0)
            {
                median = (nonZeroValues[n / 2 - 1] + nonZeroValues[n / 2]) / 2.0f;
            }
            else
            {
                median = nonZeroValues[n / 2];
            }

            combined[i] = median;
            keptPixels++;
        }
        else
        {
            // Pixel doesn't appear consistently - reject it
            combined[i] = 0.0f;
            if (occurrences > 0)
                rejectedPixels++;
        }
    }

    processedImages.clear(); // Free memory

    std::cout << "  Kept " << keptPixels << " consistent pixels" << '\n';
    std::cout << "  Rejected " << rejectedPixels << " inconsistent pixels (edge artifacts)" << '\n';
    std::cout << "Consistency-filtered composite complete" << '\n';

    // Get output dimensions
    int outWidth, outHeight;
    getResolutionDimensions(resolution, outWidth, outHeight);

    std::cout << "Converting to sinusoidal projection (" << outWidth << "x" << outHeight << ")..." << '\n';

    // Convert equirectangular combined to sinusoidal output
    std::vector<unsigned char> sinusoidal(outWidth * outHeight, 0);

    for (int y = 0; y < outHeight; y++)
    {
        float v = static_cast<float>(y) / (outHeight - 1);
        float lat = (0.5f - v) * static_cast<float>(PI);
        float cosLat = std::cos(lat);

        float uMin = 0.5f - 0.5f * std::abs(cosLat);
        float uMax = 0.5f + 0.5f * std::abs(cosLat);

        for (int x = 0; x < outWidth; x++)
        {
            float u = static_cast<float>(x) / (outWidth - 1);
            int dstIdx = y * outWidth + x;

            if (u < uMin || u > uMax)
            {
                sinusoidal[dstIdx] = 0;
                continue;
            }

            // Inverse sinusoidal to equirectangular
            float x_sinu = (u - 0.5f) * 2.0f * static_cast<float>(PI);
            float lon = (std::abs(cosLat) > 0.001f) ? (x_sinu / cosLat) : 0.0f;

            float u_equirect = lon / (2.0f * static_cast<float>(PI)) + 0.5f;
            float v_equirect = v;

            u_equirect = std::max(0.0f, std::min(1.0f, u_equirect));
            v_equirect = std::max(0.0f, std::min(1.0f, v_equirect));

            // Bilinear sample from combined
            float srcX = u_equirect * (workWidth - 1);
            float srcY = v_equirect * (workHeight - 1);

            int x0 = static_cast<int>(srcX);
            int y0 = static_cast<int>(srcY);
            int x1 = std::min(x0 + 1, workWidth - 1);
            int y1 = std::min(y0 + 1, workHeight - 1);

            float fx = srcX - x0;
            float fy = srcY - y0;

            float p00 = combined[y0 * workWidth + x0];
            float p10 = combined[y0 * workWidth + x1];
            float p01 = combined[y1 * workWidth + x0];
            float p11 = combined[y1 * workWidth + x1];

            float top = p00 * (1 - fx) + p10 * fx;
            float bottom = p01 * (1 - fx) + p11 * fx;
            float value = top * (1 - fy) + bottom * fy;

            sinusoidal[dstIdx] = static_cast<unsigned char>(value * 255.0f);
        }
    }

    // =========================================================================
    // STEP 4: Apply landmass mask to filter ocean artifacts
    // =========================================================================
    // Both nightlights and Blue Marble are now in SINUSOIDAL projection
    // Generate landmass mask if it doesn't exist, then apply it

    std::cout << "Applying landmass mask from Blue Marble color..." << '\n';

    // Generate landmass mask if it doesn't exist
    std::string landmaskPath = outputPath + "/earth_landmass_mask.png";
    if (!std::filesystem::exists(landmaskPath))
    {
        if (!preprocessLandmassMask(defaultsPath, outputBasePath, resolution))
        {
            std::cout << "  WARNING: Failed to generate landmass mask, skipping ocean masking" << '\n';
        }
    }

    // Load and apply the mask
    if (std::filesystem::exists(landmaskPath))
    {
        int maskW, maskH, maskC;
        unsigned char *maskData = stbi_load(landmaskPath.c_str(), &maskW, &maskH, &maskC, 1);

        if (maskData)
        {
            std::cout << "  Loaded landmass mask: " << maskW << "x" << maskH << " (sinusoidal)" << '\n';

            int maskedPixels = 0;

            // Both are in sinusoidal - sample directly
            for (int y = 0; y < outHeight; y++)
            {
                for (int x = 0; x < outWidth; x++)
                {
                    int dstIdx = y * outWidth + x;

                    // Sample from mask (same sinusoidal space)
                    int mx = static_cast<int>(static_cast<float>(x) / (outWidth - 1) * (maskW - 1));
                    int my = static_cast<int>(static_cast<float>(y) / (outHeight - 1) * (maskH - 1));
                    mx = std::min(mx, maskW - 1);
                    my = std::min(my, maskH - 1);

                    unsigned char maskVal = maskData[my * maskW + mx];

                    // Mask ocean pixels (black in mask) to black in nightlights
                    if (maskVal == 0) // Ocean
                    {
                        sinusoidal[dstIdx] = 0;
                        maskedPixels++;
                    }
                }
            }

            stbi_image_free(maskData);

            float maskPercent = 100.0f * maskedPixels / (outWidth * outHeight);
            std::cout << "  Masked " << maskedPixels << " ocean pixels (" << maskPercent << "%)" << '\n';
        }
        else
        {
            std::cout << "  WARNING: Failed to load landmass mask, skipping ocean masking" << '\n';
        }
    }
    else
    {
        std::cout << "  WARNING: Landmass mask not found, skipping ocean masking" << '\n';
    }

    // Save grayscale PNG
    std::cout << "Saving: " << outFile << '\n';
    if (!stbi_write_png(outFile.c_str(), outWidth, outHeight, 1, sinusoidal.data(), outWidth))
    {
        std::cerr << "ERROR: Failed to save nightlights texture" << '\n';
        std::cout << "==============================" << '\n';
        return false;
    }

    std::cout << "SUCCESS: Generated nightlights texture" << '\n';
    std::cout << "==============================" << '\n';
    return true;
}