#include "../../../concerns/constants.h"
#include "../earth-material.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <stb_image_write.h>

// GDAL for reading NetCDF files
#include <cpl_conv.h>
#include <gdal_priv.h>
#include <gdal_utils.h>

// ============================================================================
// Wind Data Processing (CCMP Wind Analysis NetCDF files)
// ============================================================================
// Processes CCMP wind NetCDF files to extract wind direction vectors (u, v)
// and create 12 separate 2D textures (one per month) in sinusoidal projection
// Saves as JPG files (RGB format: R=u, G=v, B=0) for loading into OpenGL 2D textures

namespace
{
// Constants for invalid data filtering
constexpr float NO_DATA_VALUE = -9999.0f;
constexpr float MAX_VALID_WIND = 100.0f;
constexpr float MAX_WIND_SPEED = 50.0f; // Maximum expected wind speed (m/s) for normalization

// Process a single month's wind data
bool processWindMonth(int month,
                      const std::string &ncFilePath,
                      const std::string &outputFilePath,
                      int outWidth,
                      int outHeight)
{
    // Open NetCDF subdatasets
    std::string uSubdataset = "NETCDF:\"" + ncFilePath + "\":uwnd";
    std::string vSubdataset = "NETCDF:\"" + ncFilePath + "\":vwnd";

    GDALDataset *uDataset = static_cast<GDALDataset *>(GDALOpen(uSubdataset.c_str(), GA_ReadOnly));
    GDALDataset *vDataset = static_cast<GDALDataset *>(GDALOpen(vSubdataset.c_str(), GA_ReadOnly));

    if (!uDataset || !vDataset)
    {
        uSubdataset = "NETCDF:\"" + ncFilePath + "\":u";
        vSubdataset = "NETCDF:\"" + ncFilePath + "\":v";
        uDataset = static_cast<GDALDataset *>(GDALOpen(uSubdataset.c_str(), GA_ReadOnly));
        vDataset = static_cast<GDALDataset *>(GDALOpen(vSubdataset.c_str(), GA_ReadOnly));
    }

    if (!uDataset || !vDataset)
    {
        std::cerr << "ERROR: Failed to open NetCDF subdatasets for month " << month << "\n";
        if (uDataset)
            GDALClose(uDataset);
        if (vDataset)
            GDALClose(vDataset);
        return false;
    }

    GDALRasterBand *uBand = uDataset->GetRasterBand(1);
    GDALRasterBand *vBand = vDataset->GetRasterBand(1);

    if (!uBand || !vBand)
    {
        std::cerr << "ERROR: Could not get raster bands for month " << month << "\n";
        GDALClose(uDataset);
        GDALClose(vDataset);
        return false;
    }

    int srcWidth = uBand->GetXSize();
    int srcHeight = uBand->GetYSize();

    // Read u and v data
    std::vector<float> uData(static_cast<size_t>(srcWidth) * srcHeight);
    std::vector<float> vData(static_cast<size_t>(srcWidth) * srcHeight);

    CPLErr errU =
        uBand->RasterIO(GF_Read, 0, 0, srcWidth, srcHeight, uData.data(), srcWidth, srcHeight, GDT_Float32, 0, 0);
    CPLErr errV =
        vBand->RasterIO(GF_Read, 0, 0, srcWidth, srcHeight, vData.data(), srcWidth, srcHeight, GDT_Float32, 0, 0);

    GDALClose(uDataset);
    GDALClose(vDataset);

    if (errU != CE_None || errV != CE_None)
    {
        std::cerr << "ERROR: Failed to read wind data for month " << month << "\n";
        return false;
    }

    // Resample to equirectangular projection
    std::vector<float> uEquirect(static_cast<size_t>(outWidth) * outHeight, 0.0f);
    std::vector<float> vEquirect(static_cast<size_t>(outWidth) * outHeight, 0.0f);

    for (int y = 0; y < outHeight; y++)
    {
        float v = static_cast<float>(y) / (outHeight - 1);
        float lat = (0.5f - v) * static_cast<float>(PI);

        for (int x = 0; x < outWidth; x++)
        {
            float u = static_cast<float>(x) / (outWidth - 1);
            // Longitude mapping: u=0 → -180°, u=0.5 → 0° (Greenwich), u=1 → +180°
            float lon = (u * 2.0f - 1.0f) * static_cast<float>(PI);

            // Convert to source pixel coordinates
            // Source NetCDF typically has longitude 0-360° with 0° at left edge, 180° at center
            // We want Greenwich (0°) at center of output, so we need to shift:
            // Our lon -180° → Source lon 180° (center of source, which is srcWidth/2)
            // Our lon 0° → Source lon 0° (left edge) or 360° (right edge, wraps to 0°)
            // Our lon +180° → Source lon 180° (center of source, which is srcWidth/2)
            // Map: source_lon = (lon + 2π) mod 2π, which shifts -180° to 180°
            float lonSource = lon + 2.0f * static_cast<float>(PI);
            // Normalize to [0, 2π)
            if (lonSource >= 2.0f * static_cast<float>(PI))
                lonSource -= 2.0f * static_cast<float>(PI);
            float srcX = lonSource / (2.0f * static_cast<float>(PI)) * (srcWidth - 1);
            float srcY = (PI / 2.0f - lat) / static_cast<float>(PI) * (srcHeight - 1);

            // Bilinear sample from source
            // Handle longitude wrapping: 360° wraps to 0°
            int x0 = static_cast<int>(srcX);
            int y0 = static_cast<int>(srcY);
            int x1 = x0 + 1;
            // Wrap x1 if it goes beyond right edge (360° wraps to 0°)
            if (x1 >= srcWidth)
                x1 = 0;
            int y1 = std::min(y0 + 1, srcHeight - 1);

            float fx = srcX - x0;
            float fy = srcY - y0;

            int idx00 = y0 * srcWidth + x0;
            int idx10 = y0 * srcWidth + x1;
            int idx01 = y1 * srcWidth + x0;
            int idx11 = y1 * srcWidth + x1;

            float u00 = uData[idx00];
            float u10 = uData[idx10];
            float u01 = uData[idx01];
            float u11 = uData[idx11];

            // Filter invalid data
            if (u00 <= NO_DATA_VALUE || std::abs(u00) > MAX_VALID_WIND)
                u00 = 0.0f;
            if (u10 <= NO_DATA_VALUE || std::abs(u10) > MAX_VALID_WIND)
                u10 = 0.0f;
            if (u01 <= NO_DATA_VALUE || std::abs(u01) > MAX_VALID_WIND)
                u01 = 0.0f;
            if (u11 <= NO_DATA_VALUE || std::abs(u11) > MAX_VALID_WIND)
                u11 = 0.0f;

            float uVal =
                u00 * (1.0f - fx) * (1.0f - fy) + u10 * fx * (1.0f - fy) + u01 * (1.0f - fx) * fy + u11 * fx * fy;

            float v00 = vData[idx00];
            float v10 = vData[idx10];
            float v01 = vData[idx01];
            float v11 = vData[idx11];

            // Filter invalid data
            if (v00 <= NO_DATA_VALUE || std::abs(v00) > MAX_VALID_WIND)
                v00 = 0.0f;
            if (v10 <= NO_DATA_VALUE || std::abs(v10) > MAX_VALID_WIND)
                v10 = 0.0f;
            if (v01 <= NO_DATA_VALUE || std::abs(v01) > MAX_VALID_WIND)
                v01 = 0.0f;
            if (v11 <= NO_DATA_VALUE || std::abs(v11) > MAX_VALID_WIND)
                v11 = 0.0f;

            float vVal =
                v00 * (1.0f - fx) * (1.0f - fy) + v10 * fx * (1.0f - fy) + v01 * (1.0f - fx) * fy + v11 * fx * fy;

            uEquirect[y * outWidth + x] = uVal;
            vEquirect[y * outWidth + x] = vVal;
        }
    }

    // Convert equirectangular to sinusoidal projection and normalize
    // Store as float for HDR format (2 channels: RG = u, v wind components)
    std::vector<float> sinusoidalData(static_cast<size_t>(outWidth) * outHeight * 2);

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
            int dstIdx = (y * outWidth + x) * 2;

            if (u < uMin || u > uMax)
            {
                // Outside valid region - zero wind (0.5 = center of [-1, 1] -> [0, 1] range)
                sinusoidalData[dstIdx + 0] = 0.5f; // 0.0 in normalized space
                sinusoidalData[dstIdx + 1] = 0.5f;
                continue;
            }

            // Inverse sinusoidal: find longitude from sinusoidal x
            float x_sinu = (u - 0.5f) * 2.0f * static_cast<float>(PI);
            float lon = (std::abs(cosLat) > 0.001f) ? (x_sinu / cosLat) : 0.0f;

            // Convert longitude to equirectangular u
            float u_equirect = lon / (2.0f * static_cast<float>(PI)) + 0.5f;
            u_equirect = std::max(0.0f, std::min(1.0f, u_equirect));

            // Bilinear sample from equirectangular buffer
            float srcX = u_equirect * (outWidth - 1);
            float srcY = v * (outHeight - 1);

            int x0 = static_cast<int>(srcX);
            int y0 = static_cast<int>(srcY);
            int x1 = std::min(x0 + 1, outWidth - 1);
            int y1 = std::min(y0 + 1, outHeight - 1);

            float fx = srcX - x0;
            float fy = srcY - y0;

            float u00 = uEquirect[y0 * outWidth + x0];
            float u10 = uEquirect[y0 * outWidth + x1];
            float u01 = uEquirect[y1 * outWidth + x0];
            float u11 = uEquirect[y1 * outWidth + x1];

            // Filter invalid data
            if (std::abs(u00) > MAX_VALID_WIND)
                u00 = 0.0f;
            if (std::abs(u10) > MAX_VALID_WIND)
                u10 = 0.0f;
            if (std::abs(u01) > MAX_VALID_WIND)
                u01 = 0.0f;
            if (std::abs(u11) > MAX_VALID_WIND)
                u11 = 0.0f;

            float uVal =
                u00 * (1.0f - fx) * (1.0f - fy) + u10 * fx * (1.0f - fy) + u01 * (1.0f - fx) * fy + u11 * fx * fy;

            float v00 = vEquirect[y0 * outWidth + x0];
            float v10 = vEquirect[y0 * outWidth + x1];
            float v01 = vEquirect[y1 * outWidth + x0];
            float v11 = vEquirect[y1 * outWidth + x1];

            // Filter invalid data
            if (std::abs(v00) > MAX_VALID_WIND)
                v00 = 0.0f;
            if (std::abs(v10) > MAX_VALID_WIND)
                v10 = 0.0f;
            if (std::abs(v01) > MAX_VALID_WIND)
                v01 = 0.0f;
            if (std::abs(v11) > MAX_VALID_WIND)
                v11 = 0.0f;

            float vVal =
                v00 * (1.0f - fx) * (1.0f - fy) + v10 * fx * (1.0f - fy) + v01 * (1.0f - fx) * fy + v11 * fx * fy;

            // Normalize wind values: [-maxWindSpeed, maxWindSpeed] -> [-1, 1]
            // Store as float for HDR format (will be converted to [0, 1] range for HDR)
            float uNorm = std::max(-1.0f, std::min(1.0f, uVal / MAX_WIND_SPEED));
            float vNorm = std::max(-1.0f, std::min(1.0f, vVal / MAX_WIND_SPEED));

            // Convert [-1, 1] to [0, 1] for HDR storage (HDR format expects non-negative values)
            sinusoidalData[dstIdx + 0] = (uNorm + 1.0f) * 0.5f;
            sinusoidalData[dstIdx + 1] = (vNorm + 1.0f) * 0.5f;
        }
    }

    // Convert float [0, 1] to unsigned char [0, 255] and expand to RGB (R=u, G=v, B=0)
    size_t pixelCount = static_cast<size_t>(outWidth) * outHeight;
    std::vector<unsigned char> jpgData(pixelCount * 3);

    for (size_t i = 0; i < pixelCount; i++)
    {
        float uVal = sinusoidalData[i * 2 + 0]; // u component [0, 1]
        float vVal = sinusoidalData[i * 2 + 1]; // v component [0, 1]

        jpgData[i * 3 + 0] = static_cast<unsigned char>(uVal * 255.0f); // R = u
        jpgData[i * 3 + 1] = static_cast<unsigned char>(vVal * 255.0f); // G = v
        jpgData[i * 3 + 2] = 0;                                         // B = 0 (unused)
    }

    // Save as JPG with quality 95 (high quality)
    if (!stbi_write_jpg(outputFilePath.c_str(), outWidth, outHeight, 3, jpgData.data(), 95))
    {
        std::cerr << "ERROR: Failed to save wind texture file: " << outputFilePath << "\n";
        return false;
    }

    return true;
}

} // namespace

bool EarthMaterial::preprocessWindData(const std::string &defaultsPath,
                                       const std::string &outputBasePath,
                                       TextureResolution resolution)
{
    std::string windSourcePath = defaultsPath + "/wind-forces";
    std::string outputPath = outputBasePath + "/" + getResolutionFolderName(resolution);
    std::filesystem::create_directories(outputPath);

    // Output files: 12 separate JPG files (one per month)
    std::vector<std::string> outputFiles;
    bool allFilesExist = true;
    for (int month = 1; month <= 12; month++)
    {
        char filename[64];
        snprintf(filename, sizeof(filename), "earth_wind_%02d.jpg", month);
        std::string filePath = outputPath + "/" + filename;
        outputFiles.push_back(filePath);
        if (!std::filesystem::exists(filePath))
        {
            allFilesExist = false;
        }
    }

    // Check if already processed
    if (allFilesExist)
    {
        std::cout << "Wind textures already exist (12 files)" << "\n";
        return true;
    }

    std::cout << "=== Wind Data Preprocessing ===" << "\n";
    std::cout << "Source: " << windSourcePath << "\n";
    std::cout << "Output: " << outputPath << " (12 separate JPG files)" << "\n";

    // Find NetCDF files
    if (!std::filesystem::exists(windSourcePath) || !std::filesystem::is_directory(windSourcePath))
    {
        std::cerr << "ERROR: Wind source directory does not exist: " << windSourcePath << "\n";
        return false;
    }

    std::vector<std::string> ncFiles;
    for (const auto &entry : std::filesystem::directory_iterator(windSourcePath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".nc")
        {
            ncFiles.push_back(entry.path().string());
        }
    }

    if (ncFiles.empty())
    {
        std::cerr << "ERROR: No NetCDF files found in " << windSourcePath << "\n";
        return false;
    }

    // Sort files chronologically
    std::sort(ncFiles.begin(), ncFiles.end());

    if (ncFiles.size() != 12)
    {
        std::cerr << "WARNING: Expected 12 NetCDF files (one per month), found " << ncFiles.size() << "\n";
    }

    std::cout << "Found " << ncFiles.size() << " NetCDF files" << "\n";

    // Initialize GDAL (must be done before threading)
    GDALAllRegister();

    // Get output dimensions based on requested resolution
    int outWidth, outHeight;
    getResolutionDimensions(resolution, outWidth, outHeight);
    std::cout << "Wind texture resolution: " << getResolutionName(resolution) << " (" << outWidth << "x" << outHeight
              << ")" << "\n";

    // Create task list for months that need processing
    struct MonthTask
    {
        int month;
        std::string ncFilePath;
        std::string outputFilePath;
        bool needsProcessing;
    };

    std::vector<MonthTask> tasks;
    int skippedCount = 0;
    for (size_t monthIdx = 0; monthIdx < ncFiles.size() && monthIdx < 12; monthIdx++)
    {
        int month = static_cast<int>(monthIdx) + 1;
        MonthTask task;
        task.month = month;
        task.ncFilePath = ncFiles[monthIdx];
        task.outputFilePath = outputFiles[monthIdx];
        task.needsProcessing = !std::filesystem::exists(task.outputFilePath);
        if (!task.needsProcessing)
            skippedCount++;
        tasks.push_back(task);
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
        std::cout << "All " << skippedCount << " wind textures already exist, nothing to process." << "\n";
        std::cout << "===================================" << "\n";
        return true;
    }

    // Get number of hardware threads
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0)
        numThreads = 4; // Fallback

    // For Ultra resolution, limit threads due to memory usage
    if (resolution == TextureResolution::Ultra)
    {
        numThreads = std::min(numThreads, 2u);
    }

    numThreads = std::min(numThreads, static_cast<unsigned int>(toProcessCount));

    std::cout << "Processing " << toProcessCount << " months using " << numThreads << " threads..." << "\n";
    if (resolution == TextureResolution::Ultra)
    {
        std::cout << "(Ultra resolution - this may take several minutes)" << "\n";
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // Capture resolution parameters for worker threads
    const int capturedWidth = outWidth;
    const int capturedHeight = outHeight;

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
            if (taskIndex >= static_cast<int>(tasks.size()))
                break;

            const MonthTask &task = tasks[taskIndex];
            if (!task.needsProcessing)
                continue;

            // Process this month
            bool success =
                processWindMonth(task.month, task.ncFilePath, task.outputFilePath, capturedWidth, capturedHeight);

            if (success)
            {
                processedCount++;
                std::lock_guard<std::mutex> lock(coutMutex);
                std::cout << "  Month " << task.month << ": done" << "\n";
            }
            else
            {
                failedCount++;
                std::lock_guard<std::mutex> lock(coutMutex);
                std::cout << "  Month " << task.month << ": FAILED" << "\n";
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
    std::cout << "\n";
    std::cout << "\n=== Wind Data Preprocessing Complete ===" << "\n";
    std::cout << "Wind textures saved: 12 files in " << outputPath << "\n";
    std::cout << "  Each texture: " << outWidth << "x" << outHeight << " (JPG RGB format, R=u, G=v)" << "\n";
    std::cout << "===================================" << "\n";

    return failedCount.load() == 0;
}
