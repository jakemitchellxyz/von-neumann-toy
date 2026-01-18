#include "preprocess-data.h"
#include "../materials/earth/earth-material.h"
#include "../materials/earth/economy/earth-economy.h"
#include "spice-ephemeris.h"
#include "stars-dynamic-skybox.h"
#include <filesystem>
#include <iostream>
#include <string>


#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

// Get the path to the defaults directory
// Checks next to the executable first, then falls back to current directory
static std::string getDefaultsPath()
{
    // Try to get executable directory
    std::filesystem::path exePath;

#ifdef _WIN32
    // On Windows, use GetModuleFileName
    char exePathBuf[1024];
    DWORD result = GetModuleFileNameA(nullptr, exePathBuf, sizeof(exePathBuf));
    if (result != 0 && result < sizeof(exePathBuf))
    {
        exePath = std::filesystem::path(exePathBuf).parent_path();
    }
#else
    // On Linux/Mac, try reading /proc/self/exe or use argv[0]
    char exePathBuf[1024];
    ssize_t len = readlink("/proc/self/exe", exePathBuf, sizeof(exePathBuf) - 1);
    if (len != -1 && len < static_cast<ssize_t>(sizeof(exePathBuf)))
    {
        exePathBuf[len] = '\0';
        exePath = std::filesystem::path(exePathBuf).parent_path();
    }
#endif

    // Check if defaults exists next to executable
    if (!exePath.empty())
    {
        std::filesystem::path defaultsPath = exePath / "defaults";
        if (std::filesystem::exists(defaultsPath) && std::filesystem::is_directory(defaultsPath))
        {
            return defaultsPath.string();
        }
    }

    // Fall back to "defaults" in current directory
    std::filesystem::path defaultsPath = std::filesystem::current_path() / "defaults";
    if (std::filesystem::exists(defaultsPath) && std::filesystem::is_directory(defaultsPath))
    {
        return defaultsPath.string();
    }

    // Last resort: return "defaults" relative to current directory
    return "defaults";
}

// Find the source defaults directory (where original files are, not runtime copy)
static std::string findSourceDefaultsPath()
{
    std::string runtimeDefaultsPath = getDefaultsPath();
    std::filesystem::path sourceDefaultsPath;

    // Try to find source defaults directory
    // Method 1: Check if ../../defaults exists relative to executable
    std::filesystem::path exePath;
#ifdef _WIN32
    char exePathBuf[1024];
    DWORD result = GetModuleFileNameA(nullptr, exePathBuf, sizeof(exePathBuf));
    if (result != 0 && result < sizeof(exePathBuf))
    {
        exePath = std::filesystem::path(exePathBuf).parent_path();
    }
#else
    char exePathBuf[1024];
    ssize_t len = readlink("/proc/self/exe", exePathBuf, sizeof(exePathBuf) - 1);
    if (len != -1 && len < static_cast<ssize_t>(sizeof(exePathBuf)))
    {
        exePathBuf[len] = '\0';
        exePath = std::filesystem::path(exePathBuf).parent_path();
    }
#endif

    if (!exePath.empty())
    {
        // Try ../../defaults (typical build structure: build/Release/vnt.exe -> ../../defaults)
        std::filesystem::path candidate = exePath.parent_path().parent_path() / "defaults";
        if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate))
        {
            sourceDefaultsPath = candidate;
        }
        else
        {
            // Try ../defaults (if executable is directly in build/)
            candidate = exePath.parent_path() / "defaults";
            if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate))
            {
                sourceDefaultsPath = candidate;
            }
            else
            {
                // Fall back to runtime defaults (might be the same if running from source)
                sourceDefaultsPath = runtimeDefaultsPath;
            }
        }
    }
    else
    {
        // Fall back to runtime defaults
        sourceDefaultsPath = runtimeDefaultsPath;
    }

    return sourceDefaultsPath.string();
}

bool PreprocessAllData(TextureResolution textureRes)
{
    // ========================================================================
    // Initialize SPICE Ephemeris (REQUIRED for celestial body positions)
    // ========================================================================
    // SPICE kernel files must be in defaults/kernels/
    // Required: at least one .bsp (ephemeris) file and one .tls (leap seconds) file
    std::string defaultsPath = getDefaultsPath();
    std::string kernelsPath = defaultsPath + "/kernels";

    std::cout << "\n=== SPICE Ephemeris Initialization ===\n";
    std::cout << "Looking for kernels in: " << kernelsPath << "\n";
    std::cout << "  Absolute: " << std::filesystem::absolute(kernelsPath).string() << "\n";

    if (!SpiceEphemeris::initialize(kernelsPath))
    {
        std::cerr << "\n=== FATAL ERROR: SPICE Initialization Failed! ===\n";
        std::cerr << "Could not find or load SPICE kernel files.\n";
        std::cerr << "Expected location: " << std::filesystem::absolute(kernelsPath).string() << "\n";
        std::cerr << "\nRequired files in kernels directory:\n";
        std::cerr << "  - *.bsp : SPK ephemeris file (e.g., de440.bsp)\n";
        std::cerr << "  - *.tls : Leap seconds kernel (e.g., naif0012.tls)\n";
        std::cerr << "\nDownload from: https://naif.jpl.nasa.gov/naif/data.html\n";
        std::cerr << "================================================\n";
        return false;
    }

    std::cout << "SPICE initialization successful!\n";
    std::cout << "===================================\n\n";

    // ========================================================================
    // Pre-window initialization: Process Earth textures
    // ========================================================================
    // Combine Blue Marble tiles into monthly textures at the configured resolution.
    // This runs before OpenGL is initialized, so textures are ready when needed.
    std::cout << "\n";
    int earthColorTexturesReady =
        EarthMaterial::preprocessTiles("defaults",       // Source tiles in defaults/earth-surface/blue-marble/
                                       "earth-textures", // Output combined images next to executable
                                       textureRes        // Use configured resolution
        );
    std::cout << "\n";

    // Process elevation data into heightmap and normal map textures.
    // This generates bump mapping textures from ETOPO GeoTIFF elevation data.
    bool earthElevationReady =
        EarthMaterial::preprocessElevation("defaults",       // Source elevation in defaults/earth-surface/elevation/
                                           "earth-textures", // Output next to color textures
                                           textureRes        // Use same resolution as color textures
        );
    std::cout << "\n";

    // Process MODIS reflectance data into specular/roughness texture.
    // This extracts relative green (green - red) for surface roughness mapping.
    int earthSpecularReady =
        EarthMaterial::preprocessSpecular("defaults",       // Source MODIS data in defaults/earth-surface/albedo/
                                          "earth-textures", // Output next to executable
                                          textureRes);
    std::cout << "\n";

    // Process VIIRS Black Marble nightlights for city lights at night
    // This converts HDF5 radiance data into grayscale emissive texture
    bool earthNightlightsReady =
        EarthMaterial::preprocessNightlights("defaults",       // Source in defaults/earth-surface/human-lights/
                                             "earth-textures", // Output next to executable
                                             textureRes);
    std::cout << "\n";

    // Generate ice masks from Blue Marble monthly textures
    // Creates 12 masks (one per month) for ice/snow coverage
    bool earthIceMasksReady = EarthMaterial::preprocessIceMasks("defaults", // Not used (reads from earth-textures)
                                                                "earth-textures", // Where monthly textures are
                                                                textureRes);
    (void)earthIceMasksReady; // Currently unused, prepared for future feature
    std::cout << "\n";

    // Preprocess city data from Excel file into texture
    // Loads worldcities.xlsx and generates city location texture (sinusoidal projection)
    std::string citiesXlsxPath = getDefaultsPath() + "/economy/worldcities.xlsx";
    bool citiesReady = EarthEconomy::preprocessCities(citiesXlsxPath, "earth-textures", textureRes);
    (void)citiesReady; // Prepared for runtime use
    std::cout << "\n";

    // Combined result: color textures + elevation textures + specular + nightlights
    int earthTexturesReady =
        earthColorTexturesReady + (earthElevationReady ? 1 : 0) + earthSpecularReady + (earthNightlightsReady ? 1 : 0);

    // ========================================================================
    // Preprocess skybox textures
    // Resizes TIF and EXR files from defaults/celestial-skybox/ to 2x the user's selected resolution
    // MANDATORY: This MUST succeed or the application cannot continue
    std::cout << "\n";

    // For preprocessing, we need the SOURCE defaults directory (where the original files are),
    // not the runtime defaults directory (which is copied to build/Release/defaults)
    // The source directory is typically ../../defaults relative to the executable
    std::string sourceDefaultsPath = findSourceDefaultsPath();
    std::string outputPath = "celestial-skybox";
    std::string outputDir = outputPath + "/" + getResolutionFolderName(textureRes);
    std::string criticalFile = outputDir + "/milkyway_combined.hdr";

    std::cout << "Using source defaults path: " << sourceDefaultsPath << "\n";
    std::cout << "  Absolute: " << std::filesystem::absolute(sourceDefaultsPath).string() << "\n";

    // Check if critical file exists
    bool needsPreprocessing = !std::filesystem::exists(criticalFile);

    if (needsPreprocessing)
    {
        std::cout << "Skybox textures not found. Running preprocessing..." << "\n";
        std::cout.flush();
    }
    else
    {
        std::cout << "Skybox textures found. Skipping preprocessing." << "\n";
        std::cout.flush();
    }

    // Always run preprocessing if critical file doesn't exist
    bool skyboxTexturesReady = PreprocessSkyboxTextures(sourceDefaultsPath, // Source in defaults/celestial-skybox/
                                                        outputPath,         // Output folder
                                                        textureRes);

    // Verify critical file was created
    if (!std::filesystem::exists(criticalFile))
    {
        std::cerr << "\n=== FATAL ERROR: Skybox preprocessing failed! ===" << "\n";
        std::cerr << "The critical skybox texture file was not created: " << criticalFile << "\n";
        std::cerr << "Source files should be in: " << sourceDefaultsPath << "/celestial-skybox/" << "\n";
        std::cerr << "  Required files:" << "\n";
        std::cerr << "    - constellation_figures_32k.tif" << "\n";
        std::cerr << "    - celestial_grid_32k.tif" << "\n";
        std::cerr << "    - constellation_bounds_32k.tif" << "\n";
        std::cerr << "    - milkyway_2020_16k.exr" << "\n";
        std::cerr << "    - hiptyc_2020_16k.exr" << "\n";
        std::cerr << "Output directory: " << std::filesystem::absolute(outputDir).string() << "\n";
        std::cerr << "================================================" << "\n";
        std::cerr << "Cannot continue without skybox textures. Exiting." << "\n";
        return false; // Exit application - cannot continue without skybox
    }

    std::cout << "Skybox preprocessing completed successfully." << "\n";
    std::cout << "\n";

    // ========================================================================
    // Preprocess wind data from NetCDF files
    // Processes 12 monthly NetCDF files and creates a static 3D LUT binary file
    std::cout << "\n";
    bool windDataReady = EarthMaterial::preprocessWindData("defaults",       // Source in defaults/wind-forces/
                                                           "earth-textures", // Output next to other earth textures
                                                           textureRes);
    (void)windDataReady; // Prepared for runtime use
    std::cout << "\n";

    // ========================================================================
    // Preprocess atmosphere LUTs
    // Generates transmittance and scattering lookup tables for atmosphere rendering
    std::cout << "\n";
    bool atmosphereLUTsReady = EarthMaterial::preprocessAtmosphereLUTs("earth-textures");
    (void)atmosphereLUTsReady; // Prepared for runtime use
    std::cout << "\n";

    return true;
}
