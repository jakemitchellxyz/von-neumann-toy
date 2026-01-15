#include "../../../concerns/constants.h"
#include "../earth-material.h"


#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>


int EarthMaterial::preprocessSpecular(const std::string &defaultsPath,
                                      const std::string &outputBasePath,
                                      TextureResolution resolution)
{
    std::string sourcePath = defaultsPath + "/earth-surface/albedo";
    std::string outputPath = outputBasePath + "/" + getResolutionFolderName(resolution);

    std::cout << "=== Earth Specular/Roughness Processing ===" << '\n';
    std::cout << "Processing Terra MODIS 3-6-7 Corrected Reflectance data" << '\n';
    std::cout << "Extracting relative green (green - red, clamped) for surface specular/roughness (landmass only)"
              << '\n';

    // Check source directory exists
    if (!std::filesystem::exists(sourcePath))
    {
        std::cout << "Specular source directory not found: " << sourcePath << '\n';
        std::cout << "===============================" << '\n';
        return 0;
    }

    // Check if output already exists
    std::filesystem::create_directories(outputPath);
    std::string outFile = outputPath + "/earth_specular.png";

    if (std::filesystem::exists(outFile))
    {
        std::cout << "Specular texture already exists: " << outFile << '\n';
        std::cout << "===============================" << '\n';
        return 1;
    }

    // Get output dimensions
    int outWidth, outHeight;
    getResolutionDimensions(resolution, outWidth, outHeight);
    std::cout << "Output dimensions: " << outWidth << "x" << outHeight << " (sinusoidal)" << '\n';

    // =========================================================================
    // Step 1: Load landmass mask (sinusoidal projection)
    // =========================================================================
    // The landmass mask tells us which pixels are land vs ocean
    // We only want specular data for land pixels; ocean will be black

    std::string landmaskPath = outputPath + "/earth_landmass_mask.png";
    std::vector<unsigned char> landmask;
    int maskW = 0, maskH = 0;

    if (std::filesystem::exists(landmaskPath))
    {
        int maskC;
        unsigned char *maskData = stbi_load(landmaskPath.c_str(), &maskW, &maskH, &maskC, 1);
        if (maskData)
        {
            landmask.assign(maskData, maskData + maskW * maskH);
            stbi_image_free(maskData);
            std::cout << "Loaded landmass mask: " << maskW << "x" << maskH << '\n';
        }
    }

    if (landmask.empty())
    {
        std::cout << "WARNING: Landmass mask not found. Run preprocessNightlights first." << '\n';
        std::cout << "         Will process without mask (ocean will have specular data)" << '\n';
    }

    // =========================================================================
    // Step 2: Find all source files (Terra MODIS satellite imagery)
    // =========================================================================
    // Look for JPG/PNG files that contain satellite reflectance data

    std::vector<std::string> sourceFiles;
    for (const auto &entry : std::filesystem::directory_iterator(sourcePath))
    {
        std::string ext = entry.path().extension().string();
        std::string filename = entry.path().filename().string();

        // Accept common image formats
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".tif" || ext == ".tiff")
        {
            sourceFiles.push_back(entry.path().string());
            std::cout << "  Found: " << filename << '\n';
        }
    }

    if (sourceFiles.empty())
    {
        std::cout << "No specular source files found in " << sourcePath << '\n';
        std::cout << "Run download-albedo.sh to download MODIS data from NASA GIBS" << '\n';
        std::cout << "===============================" << '\n';
        return 0;
    }

    std::cout << "Processing " << sourceFiles.size() << " source file(s)..." << '\n';

    // =========================================================================
    // Step 3: Process each source file - extract relative green
    // =========================================================================
    // Terra MODIS 3-6-7 band combination uses:
    //   - Red channel: Band 3 (459-479nm, blue-violet)
    //   - Green channel: Band 6 (1628-1652nm, SWIR - vegetation/surface)
    //   - Blue channel: Band 7 (2105-2155nm, SWIR - moisture/cloud)
    //
    // The dataset combines red and green into reflectance. To extract the
    // relative green signal, we compute: max(0, green - red) for each pixel.
    // This filters out white signals and focuses on vegetation/surface features.

    std::vector<std::vector<float>> processedImages;
    int workWidth = outWidth;
    int workHeight = outHeight;

    for (size_t i = 0; i < sourceFiles.size(); i++)
    {
        std::cout << "  Processing " << (i + 1) << "/" << sourceFiles.size() << "..." << '\n';

        int srcW, srcH, srcC;
        unsigned char *srcData = stbi_load(sourceFiles[i].c_str(), &srcW, &srcH, &srcC, 0);

        if (!srcData)
        {
            std::cerr << "    Failed to load: " << sourceFiles[i] << '\n';
            continue;
        }

        if (srcC < 2)
        {
            std::cerr << "    Not enough channels (need at least 2 for red and green): " << srcC << '\n';
            stbi_image_free(srcData);
            continue;
        }

        std::cout << "    Source: " << srcW << "x" << srcH << " (" << srcC << " channels)" << '\n';

        // Resize to working resolution
        std::vector<unsigned char> resizedData(workWidth * workHeight * srcC);
        resizeImage(srcData, srcW, srcH, resizedData.data(), workWidth, workHeight, srcC);
        stbi_image_free(srcData);

        // Extract relative green (green - red, clamped to 0) as float [0, 1]
        std::vector<float> relativeGreenChannel(workWidth * workHeight);

        for (int y = 0; y < workHeight; y++)
        {
            for (int x = 0; x < workWidth; x++)
            {
                int idx = y * workWidth + x;
                // Red is channel index 0, Green is channel index 1 (R=0, G=1, B=2)
                float red = resizedData[idx * srcC + 0] / 255.0f;
                float green = resizedData[idx * srcC + 1] / 255.0f;
                // Compute relative green: max(0, green - red)
                float relativeGreen = std::fmax(0.0f, green - red);
                relativeGreenChannel[idx] = relativeGreen;
            }
        }

        processedImages.push_back(std::move(relativeGreenChannel));
    }

    if (processedImages.empty())
    {
        std::cerr << "ERROR: No images could be processed" << '\n';
        std::cout << "===============================" << '\n';
        return 0;
    }

    // =========================================================================
    // Step 4: Composite images (average if multiple)
    // =========================================================================

    std::vector<float> combined(workWidth * workHeight, 0.0f);

    if (processedImages.size() == 1)
    {
        combined = std::move(processedImages[0]);
        std::cout << "Using single source image" << '\n';
    }
    else
    {
        std::cout << "Averaging " << processedImages.size() << " images..." << '\n';

        for (int i = 0; i < workWidth * workHeight; i++)
        {
            float sum = 0.0f;
            int count = 0;

            for (const auto &img : processedImages)
            {
                // Only count non-black pixels (valid data)
                if (img[i] > 0.01f)
                {
                    sum += img[i];
                    count++;
                }
            }

            combined[i] = (count > 0) ? (sum / count) : 0.0f;
        }
    }

    processedImages.clear(); // Free memory

    // =========================================================================
    // Step 5: Convert equirectangular to sinusoidal projection
    // =========================================================================
    // Source images from NASA GIBS are in equirectangular projection
    // We need to convert to sinusoidal to match our other textures

    std::cout << "Converting to sinusoidal projection..." << '\n';

    std::vector<float> sinusoidal(outWidth * outHeight, 0.0f);

    for (int y = 0; y < outHeight; y++)
    {
        float v = static_cast<float>(y) / (outHeight - 1);
        float lat = (0.5f - v) * static_cast<float>(PI);
        float cosLat = std::cos(lat);

        // Sinusoidal bounds at this latitude
        float uMin = 0.5f - 0.5f * std::abs(cosLat);
        float uMax = 0.5f + 0.5f * std::abs(cosLat);

        for (int x = 0; x < outWidth; x++)
        {
            float u = static_cast<float>(x) / (outWidth - 1);
            int dstIdx = y * outWidth + x;

            // Outside sinusoidal bounds = black
            if (u < uMin || u > uMax)
            {
                sinusoidal[dstIdx] = 0.0f;
                continue;
            }

            // Inverse sinusoidal to equirectangular
            float x_sinu = (u - 0.5f) * 2.0f * static_cast<float>(PI);
            float lon = (std::abs(cosLat) > 0.001f) ? (x_sinu / cosLat) : 0.0f;

            float u_equirect = lon / (2.0f * static_cast<float>(PI)) + 0.5f;
            float v_equirect = v;

            u_equirect = std::max(0.0f, std::min(1.0f, u_equirect));
            v_equirect = std::max(0.0f, std::min(1.0f, v_equirect));

            // Bilinear sample from combined (equirectangular)
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

            sinusoidal[dstIdx] = value;
        }
    }

    combined.clear(); // Free equirectangular data

    // =========================================================================
    // Step 5.5: Increase brightness uniformly before masking
    // =========================================================================
    // Clamp to [0, 1] to prevent overflow
    std::cout << "Normalizing values..." << '\n';
    for (int i = 0; i < outWidth * outHeight; i++)
    {
        sinusoidal[i] = std::min(sinusoidal[i], 1.0f);
    }

    // =========================================================================
    // Step 6: Invert values so lighter = less rough, darker = rougher
    // =========================================================================
    // Roughness: 0 = smooth/shiny, 1 = rough/matte
    // The MODIS data has higher values for more reflective (smoother) surfaces
    // So we invert: roughness = 1.0 - reflectivity
    std::cout << "Inverting values (lighter = less rough, darker = rougher)..." << '\n';
    for (int i = 0; i < outWidth * outHeight; i++)
    {
        // Invert: higher reflectance (lighter) -> lower roughness (smoother)
        sinusoidal[i] = 1.0f - sinusoidal[i];
        // Clamp to [0, 1]
        sinusoidal[i] = std::max(0.0f, std::min(1.0f, sinusoidal[i]));
    }

    // =========================================================================
    // Step 7: Apply landmass mask - multiply final roughness image by mask
    // =========================================================================
    // This sets all non-land pixels to 0 (black)
    // Landmass mask: white (255) = land, black (0) = ocean
    if (!landmask.empty())
    {
        std::cout << "Applying landmass mask (multiply final image by mask)..." << '\n';

        for (int y = 0; y < outHeight; y++)
        {
            for (int x = 0; x < outWidth; x++)
            {
                int idx = y * outWidth + x;

                // Sample landmask at corresponding position (same sinusoidal space)
                int mx = static_cast<int>(static_cast<float>(x) / (outWidth - 1) * (maskW - 1));
                int my = static_cast<int>(static_cast<float>(y) / (outHeight - 1) * (maskH - 1));
                mx = std::min(mx, maskW - 1);
                my = std::min(my, maskH - 1);

                unsigned char maskVal = landmask[my * maskW + mx];
                // Normalize mask to [0, 1] and multiply with final roughness image
                // Ocean (mask=0) -> 0, Land (mask=255) -> keeps roughness value
                float maskFactor = static_cast<float>(maskVal) / 255.0f;
                sinusoidal[idx] *= maskFactor;
            }
        }

        std::cout << "  Applied landmass mask: ocean set to black, land preserved" << '\n';
    }

    // =========================================================================
    // Step 8: Save as grayscale PNG (sinusoidal projection)
    // =========================================================================

    std::vector<unsigned char> output(outWidth * outHeight);

    for (int i = 0; i < outWidth * outHeight; i++)
    {
        output[i] = static_cast<unsigned char>(sinusoidal[i] * 255.0f);
    }

    // Save grayscale PNG
    std::cout << "Saving: " << outFile << '\n';
    if (!stbi_write_png(outFile.c_str(), outWidth, outHeight, 1, output.data(), outWidth))
    {
        std::cerr << "ERROR: Failed to save specular texture" << '\n';
        std::cout << "===============================" << '\n';
        return 0;
    }

    std::cout << "SUCCESS: Generated specular/roughness texture (relative green, sinusoidal, "
                 "landmass only)"
              << '\n';
    std::cout << "===============================" << '\n';
    return 1;
}
