#include "../../../concerns/constants.h"
#include "../../helpers/cubemap-conversion.h"
#include "../earth-material.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

#include <cmath>

namespace
{
// Tile naming constants
constexpr std::array<const char *, 4> AREAS = {"A", "B", "C", "D"};
constexpr std::array<const char *, 2> HEMISPHERES = {"1", "2"};
} // namespace

// ==================================================
// Preprocessing Blue Marble - Combine Source Tiles
// ==================================================
// Uses multithreading to process all 12 months in parallel for faster startup.

int EarthMaterial::preprocessTiles(const std::string &defaultsPath,
                                   const std::string &outputBasePath,
                                   TextureResolution resolution)
{
    std::string sourcePath = defaultsPath + "/earth-surface/blue-marble";
    std::string outputPath = outputBasePath + "/" + getResolutionFolderName(resolution);

    int outWidth, outHeight;
    getResolutionDimensions(resolution, outWidth, outHeight);
    bool lossless = (resolution == TextureResolution::Ultra);

    std::cout << "=== Earth Texture Preprocessing ===" << '\n';
    std::cout << "Resolution:   " << getResolutionName(resolution) << " (" << outWidth << "x" << outHeight << ")"
              << '\n';
    std::cout << "Source tiles: " << sourcePath << '\n';
    std::cout << "Output path:  " << outputPath << '\n';

    // Create output directory if it doesn't exist
    std::filesystem::create_directories(outputPath);

    // Determine file extension based on format
    const char *ext = lossless ? ".png" : ".jpg";

    // First pass: determine which months need processing
    struct MonthTask
    {
        int month;
        std::string outputFilepath;
        bool needsProcessing;
        bool sourceExists;
    };

    std::vector<MonthTask> tasks(12);
    int skippedCount = 0;
    int missingCount = 0;

    for (int month = 1; month <= 12; month++)
    {
        MonthTask &task = tasks[month - 1];
        task.month = month;

        char outputFilename[64];
        snprintf(outputFilename, sizeof(outputFilename), "earth_month_%02d%s", month, ext);
        task.outputFilepath = outputPath + "/" + outputFilename;

        // Check if combined image already exists
        if (std::filesystem::exists(task.outputFilepath))
        {
            task.needsProcessing = false;
            task.sourceExists = true;
            skippedCount++;
            continue;
        }

        // Check if source tiles exist for this month
        char testFilename[128];
        snprintf(testFilename, sizeof(testFilename), "world.topo.2004%02d.3x21600x21600.A1.jpg", month);
        std::string testPath = sourcePath + "/" + testFilename;

        if (!std::filesystem::exists(testPath))
        {
            task.needsProcessing = false;
            task.sourceExists = false;
            missingCount++;
            continue;
        }

        task.needsProcessing = true;
        task.sourceExists = true;
    }

    // Count how many need processing
    int toProcessCount = 0;
    for (const auto &task : tasks)
    {
        if (task.needsProcessing)
            toProcessCount++;
    }

    if (toProcessCount == 0)
    {
        std::cout << "All " << skippedCount << " textures already exist, nothing to process." << '\n';
        if (missingCount > 0)
        {
            std::cout << "(" << missingCount << " months have no source tiles)" << '\n';
        }
        std::cout << "===================================" << '\n';
        return skippedCount;
    }

    // Get number of hardware threads
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0)
        numThreads = 4; // Fallback

    // For Ultra resolution, limit threads due to memory usage (~15GB per image)
    if (resolution == TextureResolution::Ultra)
    {
        numThreads = std::min(numThreads, 2u);
    }

    numThreads = std::min(numThreads, static_cast<unsigned int>(toProcessCount));

    std::cout << "Processing " << toProcessCount << " months using " << numThreads << " threads..." << '\n';
    if (resolution == TextureResolution::Ultra)
    {
        std::cout << "(Ultra resolution - this may take several minutes)" << '\n';
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Capture resolution parameters for worker threads
    const int capturedWidth = outWidth;
    const int capturedHeight = outHeight;
    const bool capturedLossless = lossless;

    // Atomic counters for progress tracking
    std::atomic<int> processedCount{0};
    std::atomic<int> failedCount{0};
    std::atomic<int> currentTask{0};
    std::mutex coutMutex;

    // Worker function
    auto worker = [&]() {
        while (true)
        {
            // Get next task
            int taskIndex = currentTask.fetch_add(1);
            if (taskIndex >= 12)
                break;

            const MonthTask &task = tasks[taskIndex];
            if (!task.needsProcessing)
                continue;

            // Process this month
            bool success = combineTilesForMonth(task.month,
                                                sourcePath,
                                                task.outputFilepath,
                                                capturedWidth,
                                                capturedHeight,
                                                capturedLossless);

            if (success)
            {
                processedCount++;
                std::lock_guard<std::mutex> lock(coutMutex);
                std::cout << "  Month " << task.month << ": done" << '\n';
            }
            else
            {
                failedCount++;
                std::lock_guard<std::mutex> lock(coutMutex);
                std::cout << "  Month " << task.month << ": FAILED" << '\n';
            }
        }
    };

    // Launch worker threads
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (unsigned int i = 0; i < numThreads; i++)
    {
        threads.emplace_back(worker);
    }

    // Wait for all threads to complete
    for (auto &t : threads)
    {
        t.join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "Preprocessing complete in " << (duration.count() / 1000.0) << "s: " << processedCount.load()
              << " processed";
    if (failedCount > 0)
    {
        std::cout << ", " << failedCount.load() << " failed";
    }
    if (skippedCount > 0)
    {
        std::cout << ", " << skippedCount << " already existed";
    }
    std::cout << '\n';
    std::cout << "===================================" << '\n';

    return processedCount.load() + skippedCount;
}

bool EarthMaterial::combineTilesForMonth(int month,
                                         const std::string &sourcePath,
                                         const std::string &outputPath,
                                         int outWidth,
                                         int outHeight,
                                         bool lossless)
{
    const int channels = 3; // RGB

    // Each tile's size in the intermediate equirectangular buffer (4 columns, 2
    // rows)
    const int tileWidth = outWidth / 4;
    const int tileHeight = outHeight / 2;

    // =========================================================================
    // Step 1: Build intermediate equirectangular buffer from source tiles
    // =========================================================================
    std::vector<unsigned char> equirect;
    try
    {
        equirect.resize(static_cast<size_t>(outWidth) * outHeight * channels, 0);
    }
    catch (const std::bad_alloc &)
    {
        std::cerr << "Failed to allocate " << (static_cast<size_t>(outWidth) * outHeight * channels / 1024 / 1024)
                  << " MB for equirectangular buffer" << '\n';
        return false;
    }

    // Process each of the 8 tiles into equirectangular buffer
    for (int col = 0; col < 4; col++)
    {
        for (int row = 0; row < 2; row++)
        {
            // Build source filename
            char filename[128];
            snprintf(filename,
                     sizeof(filename),
                     "world.topo.2004%02d.3x21600x21600.%s%s.jpg",
                     month,
                     AREAS[col],
                     HEMISPHERES[row]);
            std::string filepath = sourcePath + "/" + filename;

            // Load and resize tile
            int srcChannels;
            unsigned char *tileData = loadAndResizeTile(filepath, tileWidth, tileHeight, srcChannels);

            if (!tileData)
            {
                std::cerr << "\n    Failed to load tile: " << filename << '\n';
                return false;
            }

            // Copy tile into equirectangular buffer at correct position
            int startX = col * tileWidth;
            int startY = row * tileHeight;

            for (int y = 0; y < tileHeight; y++)
            {
                for (int x = 0; x < tileWidth; x++)
                {
                    int srcIdx = (y * tileWidth + x) * srcChannels;
                    int dstIdx = ((startY + y) * outWidth + (startX + x)) * channels;

                    if (srcChannels >= 3)
                    {
                        equirect[dstIdx + 0] = tileData[srcIdx + 0];
                        equirect[dstIdx + 1] = tileData[srcIdx + 1];
                        equirect[dstIdx + 2] = tileData[srcIdx + 2];
                    }
                    else
                    {
                        equirect[dstIdx + 0] = tileData[srcIdx];
                        equirect[dstIdx + 1] = tileData[srcIdx];
                        equirect[dstIdx + 2] = tileData[srcIdx];
                    }
                }
            }

            delete[] tileData;
        }
    }

    // =========================================================================
    // Step 2: Convert equirectangular to cubemap (vertical strip)
    // =========================================================================
    // Cubemap provides uniform sampling density and seamless rendering at all angles
    // Format: Vertical strip with 6 faces (+X, -X, +Y, -Y, +Z, -Z)

    int faceSize = calculateCubemapFaceSize(outWidth, outHeight);
    unsigned char *cubemapData =
        convertEquirectangularToCubemapUChar(equirect.data(), outWidth, outHeight, channels, faceSize);

    if (!cubemapData)
    {
        std::cerr << "Failed to convert equirectangular to cubemap" << '\n';
        return false;
    }

    // =========================================================================
    // Step 3: Save cubemap as vertical strip image
    // =========================================================================
    int cubemapWidth, cubemapHeight;
    getCubemapStripDimensions(faceSize, cubemapWidth, cubemapHeight);

    int result;
    if (lossless)
    {
        result = stbi_write_png(outputPath.c_str(),
                                cubemapWidth,
                                cubemapHeight,
                                channels,
                                cubemapData,
                                cubemapWidth * channels);
    }
    else
    {
        result = stbi_write_jpg(outputPath.c_str(), cubemapWidth, cubemapHeight, channels, cubemapData, 95);
    }

    delete[] cubemapData;
    return result != 0;
}

unsigned char *EarthMaterial::loadAndResizeTile(const std::string &filepath,
                                                int targetWidth,
                                                int targetHeight,
                                                int &channels)
{
    // Load source image
    int srcWidth, srcHeight;
    stbi_set_flip_vertically_on_load(false); // Keep top-to-bottom for processing

    unsigned char *srcData = stbi_load(filepath.c_str(), &srcWidth, &srcHeight, &channels, 0);

    if (!srcData)
    {
        return nullptr;
    }

    // If source is already target size, return as-is (copy to new buffer)
    if (srcWidth == targetWidth && srcHeight == targetHeight)
    {
        size_t size = static_cast<size_t>(targetWidth) * targetHeight * channels;
        unsigned char *result = new (std::nothrow) unsigned char[size];
        if (!result)
        {
            stbi_image_free(srcData);
            return nullptr;
        }
        memcpy(result, srcData, size);
        stbi_image_free(srcData);
        return result;
    }

    // Resize to target dimensions
    size_t dstSize = static_cast<size_t>(targetWidth) * targetHeight * channels;
    unsigned char *dstData = new (std::nothrow) unsigned char[dstSize];
    if (!dstData)
    {
        stbi_image_free(srcData);
        return nullptr;
    }

    resizeImage(srcData, srcWidth, srcHeight, dstData, targetWidth, targetHeight, channels);

    // Free source
    stbi_image_free(srcData);

    return dstData;
}

void EarthMaterial::resizeImage(const unsigned char *src,
                                int srcW,
                                int srcH,
                                unsigned char *dst,
                                int dstW,
                                int dstH,
                                int channels)
{
    // Simple bilinear interpolation resize
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
