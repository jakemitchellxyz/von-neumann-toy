#include "earth-economy.h"
#include "../../../concerns/constants.h"
#include "../../../concerns/settings.h"
#include "../../helpers/gl.h"
#include "../helpers/coordinate-conversion.h"


#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
// Removed shared_mutex - no longer needed since we're just appending all cities
#include <string>
#include <thread>
#include <vector>

#ifdef HAS_OPENXLSX
#include <OpenXLSX.hpp>
#endif

#include <stb_image.h>
#include <stb_image_write.h>

#ifdef HAS_PROTOBUF
#include "cities.pb.h"
#include <fstream>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#endif

// ============================================================================
// Global Instance
// ============================================================================
EarthEconomy g_earthEconomy;

// ============================================================================
// Forward Declarations
// ============================================================================
#ifdef HAS_PROTOBUF
bool saveCityDatabaseToProtobuf(const std::string &dbPath,
                                const std::vector<CityData> &cities,
                                const std::string &sourceFile);
bool loadCityDatabaseFromProtobuf(const std::string &dbPath, std::vector<CityData> &cities);
#else
inline bool saveCityDatabaseToProtobuf(const std::string &, const std::vector<CityData> &, const std::string &)
{
    return false;
}
inline bool loadCityDatabaseFromProtobuf(const std::string &, std::vector<CityData> &)
{
    return false;
}
#endif

// ============================================================================
// Constructor / Destructor
// ============================================================================

EarthEconomy::EarthEconomy() : cityTexture_(0), initialized_(false)
{
    spatialGrid_.resize(SPATIAL_GRID_SIZE);
    for (auto &row : spatialGrid_)
    {
        row.resize(SPATIAL_GRID_SIZE);
    }
}

EarthEconomy::~EarthEconomy()
{
    cleanup();
}

// ============================================================================
// Preprocessing
// ============================================================================

bool EarthEconomy::preprocessCities(const std::string &xlsxPath,
                                    const std::string &outputBasePath,
                                    TextureResolution resolution)
{
    std::string outputPath = outputBasePath + "/" + getResolutionFolderName(resolution);
    std::filesystem::create_directories(outputPath);

    std::string outFile = outputPath + "/earth_cities.png";

    // Check if already processed
    if (std::filesystem::exists(outFile))
    {
        std::cout << "City texture already exists: " << outFile << "\n";
        return true;
    }

    std::cout << "=== City Data Preprocessing ===" << "\n";
    std::cout << "Source: " << xlsxPath << "\n";
    std::cout << "Output: " << outFile << "\n";

    // Load city data from Excel
    std::vector<CityData> cities;
    if (!loadCityDataFromExcel(xlsxPath, cities))
    {
        std::cout << "Failed to load city data from Excel" << "\n";
        return false;
    }

    std::cout << "Loaded " << cities.size() << " cities" << "\n";

    if (cities.empty())
    {
        std::cout << "No cities found in Excel file" << "\n";
        return false;
    }

    // Get resolution dimensions
    int width, height;
    getResolutionDimensions(resolution, width, height);

    std::cout << "Generating city texture: " << width << "x" << height << "\n";

    // Create texture data in sinusoidal projection (grayscale: 0 = no city, 255 = city present)
    // We'll build it in equirectangular first, then convert to sinusoidal (like other textures)
    std::vector<unsigned char> equirectData(width * height, 0);

    // First pass: Mark city locations in equirectangular projection
    for (const auto &city : cities)
    {
        // Convert lat/lon to equirectangular UV
        glm::vec2 equirectUV = EarthCoordinateConversion::latLonToUV(city.latitude, city.longitude);

        // Convert UV to pixel coordinates (equirectangular)
        int x = static_cast<int>(equirectUV.x * width);
        int y = static_cast<int>(equirectUV.y * height);

        // Clamp to valid range
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);

        // Mark city location (use 255 for city presence)
        equirectData[y * width + x] = 255;

        // Also mark neighboring pixels for better visibility (small radius)
        const int radius = 2;
        for (int dy = -radius; dy <= radius; dy++)
        {
            for (int dx = -radius; dx <= radius; dx++)
            {
                int nx = x + dx;
                int ny = y + dy;
                if (nx >= 0 && nx < width && ny >= 0 && ny < height)
                {
                    int idx = ny * width + nx;
                    // Use distance-based falloff for smoother appearance
                    float dist = sqrtf(static_cast<float>(dx * dx + dy * dy));
                    if (dist <= radius)
                    {
                        unsigned char value = static_cast<unsigned char>(255.0f * (1.0f - dist / radius));
                        equirectData[idx] = std::max(equirectData[idx], value);
                    }
                }
            }
        }
    }

    // Second pass: Convert equirectangular to sinusoidal projection
    std::vector<unsigned char> sinusoidalData(width * height, 0);

    for (int y = 0; y < height; y++)
    {
        // v in [0, 1], top to bottom
        float v = static_cast<float>(y) / (height - 1);

        // Latitude: v=0 → lat=π/2 (north pole), v=1 → lat=-π/2 (south pole)
        float lat = (0.5f - v) * static_cast<float>(PI);
        float cosLat = std::cos(lat);

        // Valid x range in sinusoidal: [-π*cos(lat), π*cos(lat)]
        // In UV: [0.5 - 0.5*cos(lat), 0.5 + 0.5*cos(lat)]
        float uMin = 0.5f - 0.5f * std::abs(cosLat);
        float uMax = 0.5f + 0.5f * std::abs(cosLat);

        for (int x = 0; x < width; x++)
        {
            // u in [0, 1], left to right
            float u = static_cast<float>(x) / (width - 1);

            int dstIdx = y * width + x;

            // Check if this pixel is within valid sinusoidal bounds
            if (u < uMin || u > uMax)
            {
                // Outside valid region - black pixel
                sinusoidalData[dstIdx] = 0;
                continue;
            }

            // Inverse sinusoidal: find longitude from sinusoidal x
            // x_sinu = (u - 0.5) * 2π, then lon = x_sinu / cos(lat)
            float x_sinu = (u - 0.5f) * 2.0f * static_cast<float>(PI);
            float lon = (std::abs(cosLat) > 0.001f) ? (x_sinu / cosLat) : 0.0f;

            // Convert longitude to equirectangular u
            float u_equirect = lon / (2.0f * static_cast<float>(PI)) + 0.5f;
            float v_equirect = v; // Same latitude mapping

            // Clamp to valid range
            u_equirect = std::max(0.0f, std::min(1.0f, u_equirect));
            v_equirect = std::max(0.0f, std::min(1.0f, v_equirect));

            // Bilinear sample from equirectangular buffer
            float srcX = u_equirect * (width - 1);
            float srcY = v_equirect * (height - 1);

            int x0 = static_cast<int>(srcX);
            int y0 = static_cast<int>(srcY);
            int x1 = std::min(x0 + 1, width - 1);
            int y1 = std::min(y0 + 1, height - 1);

            float fx = srcX - x0;
            float fy = srcY - y0;

            float p00 = static_cast<float>(equirectData[y0 * width + x0]);
            float p10 = static_cast<float>(equirectData[y0 * width + x1]);
            float p01 = static_cast<float>(equirectData[y1 * width + x0]);
            float p11 = static_cast<float>(equirectData[y1 * width + x1]);

            float top = p00 * (1.0f - fx) + p10 * fx;
            float bottom = p01 * (1.0f - fx) + p11 * fx;
            float value = top * (1.0f - fy) + bottom * fy;

            sinusoidalData[dstIdx] = static_cast<unsigned char>(value);
        }
    }

    // Save texture to disk (sinusoidal projection)
    // Note: stbi_write_png expects data top-to-bottom, which matches our coordinate system
    if (!stbi_write_png(outFile.c_str(), width, height, 1, sinusoidalData.data(), width))
    {
        std::cerr << "Failed to write city texture: " << outFile << "\n";
        return false;
    }

    std::cout << "City texture saved: " << outFile << "\n";

    // Save city database to protobuf file
    std::string dbFile = outputPath + "/earth_cities.pb";
    if (saveCityDatabaseToProtobuf(dbFile, cities, xlsxPath))
    {
        std::cout << "City database saved: " << dbFile << "\n";
    }
    else
    {
        std::cerr << "Warning: Failed to save city database to protobuf" << "\n";
    }

    std::cout << "===============================" << "\n";

    return true;
}

// ============================================================================
// Excel File Loading
// ============================================================================

#ifdef HAS_OPENXLSX
bool EarthEconomy::loadCityDataFromExcel(const std::string &xlsxPath, std::vector<CityData> &cities)
{
    try
    {
        if (!std::filesystem::exists(xlsxPath))
        {
            std::cerr << "Excel file not found: " << xlsxPath << "\n";
            return false;
        }

        std::cout << "Loading city data from: " << xlsxPath << "\n";

        OpenXLSX::XLDocument doc;
        doc.open(xlsxPath);

        // Get the first worksheet
        auto worksheetNames = doc.workbook().worksheetNames();
        if (worksheetNames.empty())
        {
            std::cerr << "No worksheets found in Excel file" << "\n";
            return false;
        }

        auto wks = doc.workbook().worksheet(worksheetNames.front());
        std::cout << "Reading worksheet: " << worksheetNames.front() << "\n";

        uint32_t rowCount = wks.rowCount();
        uint32_t colCount = wks.columnCount();

        std::cout << "Worksheet size: " << rowCount << " rows x " << colCount << " cols" << "\n";

        // Find column indices by header names (row 1)
        int nameCol = -1, countryCol = -1, latCol = -1, lonCol = -1, popCol = 11; // Hard-coded to column 11

        // DEBUG: Log all column headers
        std::cout << "DEBUG: All column headers in Excel file:\n";
        for (uint32_t col = 1; col <= colCount; col++)
        {
            try
            {
                auto cell = wks.cell(1, col);
                std::string header = cell.value().get<std::string>();
                std::cout << "  Column " << col << ": \"" << header << "\"\n";
            }
            catch (...)
            {
                try
                {
                    // Try as number
                    auto cell = wks.cell(1, col);
                    double numVal = cell.value().get<double>();
                    std::cout << "  Column " << col << ": (number) " << numVal << "\n";
                }
                catch (...)
                {
                    std::cout << "  Column " << col << ": (empty or error)\n";
                }
            }
        }

        std::cout << "\nDEBUG: Column matching process:\n";
        std::cout << "DEBUG: colCount = " << colCount << ", will check columns 1 to " << std::min(colCount, 20u)
                  << "\n";
        for (uint32_t col = 1; col <= std::min(colCount, 20u); col++)
        {
            try
            {
                auto cell = wks.cell(1, col);
                std::string header = cell.value().get<std::string>();
                std::string headerLower = header;
                std::transform(headerLower.begin(), headerLower.end(), headerLower.begin(), ::tolower);

                std::cout << "  Checking column " << col << ": \"" << header << "\" (lowercase: \"" << headerLower
                          << "\")\n";

                if (headerLower.find("city") != std::string::npos || headerLower.find("name") != std::string::npos)
                {
                    if (nameCol < 0)
                    {
                        nameCol = static_cast<int>(col);
                        std::cout << "    -> Matched as NAME column\n";
                    }
                }
                else if (headerLower.find("country") != std::string::npos ||
                         headerLower.find("iso") != std::string::npos)
                {
                    if (countryCol < 0)
                    {
                        countryCol = static_cast<int>(col);
                        std::cout << "    -> Matched as COUNTRY column\n";
                    }
                }
                else if (headerLower.find("lat") != std::string::npos)
                {
                    if (latCol < 0)
                    {
                        latCol = static_cast<int>(col);
                        std::cout << "    -> Matched as LATITUDE column\n";
                    }
                }
                else if (headerLower.find("lng") != std::string::npos || headerLower.find("lon") != std::string::npos ||
                         headerLower.find("longitude") != std::string::npos)
                {
                    if (lonCol < 0)
                    {
                        lonCol = static_cast<int>(col);
                        std::cout << "    -> Matched as LONGITUDE column\n";
                    }
                }
                // Population column is hard-coded to column 11, skip matching
                // else if (headerLower == "population" || headerLower.find("population") != std::string::npos)
                // {
                //     std::cout << "    -> Found 'population' in header\n";
                //     if (popCol < 0)
                //     {
                //         popCol = static_cast<int>(col);
                //         std::cout << "    -> Matched as POPULATION column (column " << col << ")\n";
                //     }
                //     else
                //     {
                //         std::cout << "    -> Population column already set to column " << popCol << ", skipping\n";
                //     }
                // }
                else
                {
                    // Debug: check if it contains "pop" (for debugging)
                    if (headerLower.find("pop") != std::string::npos)
                    {
                        std::cout << "    -> Contains 'pop' but not 'population' (find result: "
                                  << headerLower.find("pop") << ")\n";
                    }
                    else
                    {
                        std::cout << "    -> No match\n";
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cout << "  Column " << col << ": Exception reading cell: " << e.what() << "\n";
            }
            catch (...)
            {
                std::cout << "  Column " << col << ": Unknown exception reading cell\n";
            }
        }

        std::cout << "Column mapping: city=" << nameCol << ", country=" << countryCol << ", lat=" << latCol
                  << ", lon=" << lonCol << ", population=" << popCol << "\n";

        if (nameCol < 0 || latCol < 0 || lonCol < 0)
        {
            std::cerr << "Required columns not found (need: name, latitude, longitude)" << "\n";
            return false;
        }

        // Population column is hard-coded to column 11, so no need to check
        // if (popCol < 0)
        // {
        //     std::cerr << "Warning: Population column not found - cannot filter by size" << "\n";
        // }

        // Multithreaded loading: load all cities
        std::vector<CityData> allCities;
        allCities.reserve(rowCount); // Reserve space for all rows (will be less due to filtering)
        std::mutex citiesMutex;      // Mutex for thread-safe appending

        // Determine number of threads
        const unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
        std::cout << "Processing cities with " << numThreads << " threads, loading all cities" << "\n";

        // Process rows in parallel batches
        const uint32_t rowsPerThread = (rowCount - 1) / numThreads + 1; // -1 to skip header
        std::vector<std::thread> threads;
        std::atomic<uint32_t> processedRows(0);

        // DEBUG: Log first city entry (row 2, after header)
        static bool firstCityLogged = false;
        if (rowCount >= 2 && !firstCityLogged)
        {
            std::cout << "\nDEBUG: First city entry (row 2):\n";
            for (uint32_t col = 1; col <= colCount; col++)
            {
                try
                {
                    auto cell = wks.cell(2, col);
                    try
                    {
                        std::string strVal = cell.value().get<std::string>();
                        std::cout << "  Column " << col << ": \"" << strVal << "\"\n";
                    }
                    catch (...)
                    {
                        try
                        {
                            double numVal = cell.value().get<double>();
                            std::cout << "  Column " << col << ": " << numVal << "\n";
                        }
                        catch (...)
                        {
                            std::cout << "  Column " << col << ": (empty or error)\n";
                        }
                    }
                }
                catch (...)
                {
                    std::cout << "  Column " << col << ": (read error)\n";
                }
            }
            firstCityLogged = true;
        }

        auto processRowRange = [&](uint32_t startRow, uint32_t endRow) {
            for (uint32_t row = startRow; row <= endRow && row <= rowCount; row++)
            {
                try
                {
                    CityData city;

                    // Read name
                    if (nameCol > 0)
                    {
                        auto cell = wks.cell(row, nameCol);
                        city.name = cell.value().get<std::string>();
                    }

                    // Read country
                    if (countryCol > 0)
                    {
                        auto cell = wks.cell(row, countryCol);
                        city.country = cell.value().get<std::string>();
                    }

                    // Read latitude (in degrees)
                    if (latCol > 0)
                    {
                        auto cell = wks.cell(row, latCol);
                        double latDeg = cell.value().get<double>();
                        city.latitude = glm::radians(latDeg);
                    }

                    // Read longitude (in degrees)
                    if (lonCol > 0)
                    {
                        auto cell = wks.cell(row, lonCol);
                        double lonDeg = cell.value().get<double>();
                        city.longitude = glm::radians(lonDeg);
                    }

                    // Read population (optional)
                    if (popCol > 0)
                    {
                        try
                        {
                            auto cell = wks.cell(row, popCol);
                            city.population = static_cast<float>(cell.value().get<double>());
                        }
                        catch (...)
                        {
                            city.population = 0.0f;
                        }
                    }
                    else
                    {
                        city.population = 0.0f;
                    }

                    // Validate coordinates
                    if (city.latitude >= -PI / 2.0 && city.latitude <= PI / 2.0 && city.longitude >= -PI &&
                        city.longitude <= PI)
                    {
                        // Compute 3D position on Earth sphere
                        city.position =
                            EarthCoordinateConversion::latLonToPosition(city.latitude, city.longitude, 1.0f);

                        // Add city to list (thread-safe)
                        {
                            std::lock_guard<std::mutex> lock(citiesMutex);
                            allCities.push_back(city);
                        }
                    }

                    processedRows++;
                }
                catch (...)
                {
                    // Skip rows that fail to parse
                    processedRows++;
                }
            }
        };

        // Launch threads
        for (unsigned int i = 0; i < numThreads; i++)
        {
            uint32_t startRow = 2 + i * rowsPerThread; // Start at row 2 (skip header)
            uint32_t endRow = startRow + rowsPerThread - 1;
            threads.emplace_back(processRowRange, startRow, endRow);
        }

        // Wait for all threads to complete
        for (auto &thread : threads)
        {
            thread.join();
        }

        std::cout << "Processed " << processedRows << " rows, loaded " << allCities.size() << " cities" << "\n";

        // Copy all cities to output
        cities = std::move(allCities);

        doc.close();
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error loading Excel file: " << e.what() << "\n";
        return false;
    }
}
#else
bool EarthEconomy::loadCityDataFromExcel(const std::string &xlsxPath, std::vector<CityData> &cities)
{
    std::cerr << "OpenXLSX not available - cannot load " << xlsxPath << "\n";
    return false;
}
#endif

// ============================================================================
// Protobuf Database Functions
// ============================================================================

#ifdef HAS_PROTOBUF
bool saveCityDatabaseToProtobuf(const std::string &dbPath,
                                const std::vector<CityData> &cities,
                                const std::string &sourceFile)
{
    try
    {
        earth::economy::CityDatabase database;
        database.set_version(1);
        database.set_source_file(sourceFile);

        for (const auto &city : cities)
        {
            auto *pbCity = database.add_cities();
            pbCity->set_name(city.name);
            pbCity->set_country(city.country);
            pbCity->set_latitude(city.latitude);
            pbCity->set_longitude(city.longitude);
            pbCity->set_population(city.population);
            pbCity->set_density(0.0f); // Density not available in Excel, set to 0
            pbCity->set_position_x(city.position.x);
            pbCity->set_position_y(city.position.y);
            pbCity->set_position_z(city.position.z);
        }

        // Write to binary file
        std::ofstream output(dbPath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output.is_open())
        {
            std::cerr << "Failed to open protobuf file for writing: " << dbPath << "\n";
            return false;
        }

        if (!database.SerializeToOstream(&output))
        {
            std::cerr << "Failed to serialize city database to protobuf" << "\n";
            return false;
        }

        output.close();
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error saving protobuf database: " << e.what() << "\n";
        return false;
    }
}

bool loadCityDatabaseFromProtobuf(const std::string &dbPath, std::vector<CityData> &cities)
{
    try
    {
        if (!std::filesystem::exists(dbPath))
        {
            std::cerr << "Protobuf database not found: " << dbPath << "\n";
            return false;
        }

        earth::economy::CityDatabase database;
        std::ifstream input(dbPath, std::ios::in | std::ios::binary);
        if (!input.is_open())
        {
            std::cerr << "Failed to open protobuf file for reading: " << dbPath << "\n";
            return false;
        }

        if (!database.ParseFromIstream(&input))
        {
            std::cerr << "Failed to parse protobuf database" << "\n";
            return false;
        }

        input.close();

        // Convert protobuf cities to CityData
        cities.clear();
        cities.reserve(database.cities_size());

        for (const auto &pbCity : database.cities())
        {
            CityData city;
            city.name = pbCity.name();
            city.country = pbCity.country();
            city.latitude = pbCity.latitude();
            city.longitude = pbCity.longitude();
            city.population = pbCity.population();
            city.position = glm::vec3(pbCity.position_x(), pbCity.position_y(), pbCity.position_z());

            cities.push_back(city);
        }

        std::cout << "Loaded " << cities.size() << " cities from protobuf database (version " << database.version()
                  << ")" << "\n";
        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error loading protobuf database: " << e.what() << "\n";
        return false;
    }
}
#endif

// ============================================================================
// Initialization
// ============================================================================

bool EarthEconomy::initialize(const std::string &combinedBasePath, TextureResolution resolution)
{
    if (initialized_)
    {
        return true;
    }

    std::string combinedPath = combinedBasePath + "/" + getResolutionFolderName(resolution);
    std::string texturePath = combinedPath + "/earth_cities.png";
    std::string dbPath = combinedPath + "/earth_cities.pb";

    if (!std::filesystem::exists(texturePath))
    {
        std::cout << "City texture not found: " << texturePath << "\n";
        return false;
    }

    // Load city texture
    cityTexture_ = loadCityTexture(texturePath);
    if (cityTexture_ == 0)
    {
        std::cout << "Failed to load city texture" << "\n";
        return false;
    }

    // Load city database from protobuf (preferred method)
    if (std::filesystem::exists(dbPath))
    {
        if (loadCityDatabaseFromProtobuf(dbPath, cities_))
        {
            std::cout << "City database loaded from protobuf: " << cities_.size() << " cities" << "\n";
            buildSpatialIndex();
        }
        else
        {
            std::cerr << "Warning: Failed to load city database from protobuf, trying Excel fallback" << "\n";
            cities_.clear();
        }
    }

    // Fallback to Excel if protobuf not available
    if (cities_.empty())
    {
        std::string xlsxPath = combinedBasePath + "/../defaults/economy/worldcities.xlsx";
        if (!std::filesystem::exists(xlsxPath))
        {
            xlsxPath = "defaults/economy/worldcities.xlsx";
        }
        if (loadCityDataFromExcel(xlsxPath, cities_))
        {
            std::cout << "Loaded " << cities_.size() << " cities from Excel (fallback)" << "\n";
            buildSpatialIndex();
        }
        else
        {
            std::cout << "Warning: Could not load city data for queries (texture loaded but no city names available)"
                      << "\n";
        }
    }

    initialized_ = true;
    std::cout << "Earth economy system initialized" << "\n";
    return true;
}

GLuint EarthEconomy::loadCityTexture(const std::string &filepath)
{
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true); // OpenGL expects bottom-to-top

    unsigned char *data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);

    if (!data)
    {
        std::cerr << "Failed to load city texture: " << filepath << "\n";
        return 0;
    }

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    GLenum format = GL_LUMINANCE;
    if (channels == 1)
        format = GL_LUMINANCE;
    else if (channels == 3)
        format = GL_RGB;
    else if (channels == 4)
        format = GL_RGBA;

    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    return textureId;
}

void EarthEconomy::buildSpatialIndex()
{
    // Clear existing index
    for (auto &row : spatialGrid_)
    {
        for (auto &cell : row)
        {
            cell.cityIndices.clear();
        }
    }

    // Add cities to grid cells
    for (size_t i = 0; i < cities_.size(); i++)
    {
        const auto &city = cities_[i];

        // Convert lat/lon to grid coordinates
        // lat: -π/2 to +π/2 -> 0 to SPATIAL_GRID_SIZE-1
        // lon: -π to +π -> 0 to SPATIAL_GRID_SIZE-1
        int latIdx = static_cast<int>((city.latitude + PI / 2.0) / PI * (SPATIAL_GRID_SIZE - 1));
        int lonIdx = static_cast<int>((city.longitude + PI) / (2.0 * PI) * (SPATIAL_GRID_SIZE - 1));

        latIdx = std::clamp(latIdx, 0, SPATIAL_GRID_SIZE - 1);
        lonIdx = std::clamp(lonIdx, 0, SPATIAL_GRID_SIZE - 1);

        spatialGrid_[latIdx][lonIdx].cityIndices.push_back(i);
    }

    std::cout << "Built spatial index for " << cities_.size() << " cities" << "\n";
}

// ============================================================================
// Runtime Queries
// ============================================================================

const CityData *EarthEconomy::findNearestCity(const glm::vec3 &surfacePosition, double maxDistance) const
{
    auto results = findNearestCities(surfacePosition, 1, maxDistance);
    return results.empty() ? nullptr : results[0];
}

std::vector<const CityData *> EarthEconomy::findNearestCities(const glm::vec3 &surfacePosition,
                                                              size_t count,
                                                              double maxDistance) const
{
    std::vector<const CityData *> results;

    if (cities_.empty())
    {
        return results;
    }

    glm::vec3 normalizedPos = glm::normalize(surfacePosition);

    // Convert position to lat/lon
    auto [lat, lon] = EarthCoordinateConversion::positionToLatLon(normalizedPos);

    // Find grid cell
    int latIdx = static_cast<int>((lat + PI / 2.0) / PI * (SPATIAL_GRID_SIZE - 1));
    int lonIdx = static_cast<int>((lon + PI) / (2.0 * PI) * (SPATIAL_GRID_SIZE - 1));

    latIdx = std::clamp(latIdx, 0, SPATIAL_GRID_SIZE - 1);
    lonIdx = std::clamp(lonIdx, 0, SPATIAL_GRID_SIZE - 1);

    // Search nearby grid cells (simple approach: check 3x3 neighborhood)
    struct Candidate
    {
        size_t cityIdx;
        double distance;
    };
    std::vector<Candidate> candidates;

    const int searchRadius = 2; // Check 2 cells in each direction
    for (int dLat = -searchRadius; dLat <= searchRadius; dLat++)
    {
        for (int dLon = -searchRadius; dLon <= searchRadius; dLon++)
        {
            int checkLat = latIdx + dLat;
            int checkLon = lonIdx + dLon;

            if (checkLat < 0 || checkLat >= SPATIAL_GRID_SIZE || checkLon < 0 || checkLon >= SPATIAL_GRID_SIZE)
            {
                continue;
            }

            const auto &cell = spatialGrid_[checkLat][checkLon];
            for (size_t cityIdx : cell.cityIndices)
            {
                const auto &city = cities_[cityIdx];
                glm::vec3 cityPos = glm::normalize(city.position);

                // Compute angular distance (dot product of normalized vectors)
                double dot = glm::dot(normalizedPos, cityPos);
                double angularDist = acos(glm::clamp(dot, -1.0, 1.0));

                if (angularDist <= maxDistance)
                {
                    candidates.push_back({cityIdx, angularDist});
                }
            }
        }
    }

    // Sort by distance
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return a.distance < b.distance;
    });

    // Return top N results
    size_t resultCount = std::min(count, candidates.size());
    results.reserve(resultCount);
    for (size_t i = 0; i < resultCount; i++)
    {
        results.push_back(&cities_[candidates[i].cityIdx]);
    }

    return results;
}

std::string EarthEconomy::getCityName(const glm::vec3 &surfacePosition) const
{
    const CityData *city = findNearestCity(surfacePosition);
    return city ? city->name : std::string();
}

// ============================================================================
// Cleanup
// ============================================================================

void EarthEconomy::cleanup()
{
    if (cityTexture_ != 0)
    {
        glDeleteTextures(1, &cityTexture_);
        cityTexture_ = 0;
    }

    cities_.clear();
    initialized_ = false;
}
