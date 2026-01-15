// ============================================================================
// Atmosphere Shader Initialization
// ============================================================================

#include "../../helpers/gl.h"
#include "../../helpers/shader-loader.h"
#include "../earth-material.h"


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// stb_image for loading HDR textures (implementation already in setup.cpp)
#include <stb_image.h>

// OpenXLSX for loading atmosphere data from xlsx
#ifdef HAS_OPENXLSX
#include <OpenXLSX.hpp>
#endif

bool EarthMaterial::initializeAtmosphereShader()
{
    // Early return if shader is already compiled
    if (atmosphereAvailable_ && atmosphereProgram_ != 0)
    {
        return true;
    }

    // Load shaders from files
    std::string vertexShaderPath = getShaderPath("atmosphere-vertex.glsl");
    std::string vertexShaderSource = loadShaderFile(vertexShaderPath);
    if (vertexShaderSource.empty())
    {
        std::cerr
            << "ERROR: EarthMaterial::initializeAtmosphereShader() - Could not load atmosphere-vertex.glsl from file"
            << "\n";
        std::cerr << "  Tried path: " << vertexShaderPath << "\n";
        std::cerr << "  Atmosphere shader is required. Cannot continue." << "\n";
        std::exit(1);
    }

    std::string fragmentShaderPath = getShaderPath("atmosphere-fragment.glsl");
    std::string fragmentShaderSource = loadShaderFile(fragmentShaderPath);
    if (fragmentShaderSource.empty())
    {
        std::cerr
            << "ERROR: EarthMaterial::initializeAtmosphereShader() - Could not load atmosphere-fragment.glsl from file"
            << "\n";
        std::cerr << "  Tried path: " << fragmentShaderPath << "\n";
        std::cerr << "  Atmosphere shader is required. Cannot continue." << "\n";
        std::exit(1);
    }

    // Compile vertex shader
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource.c_str());
    if (vertexShader == 0)
    {
        std::cerr << "ERROR: EarthMaterial::initializeAtmosphereShader() - Vertex shader compilation failed" << "\n";
        std::cerr << "  Check console output above for shader compilation errors." << "\n";
        std::cerr << "  Atmosphere shader is required. Cannot continue." << "\n";
        std::exit(1);
    }

    // Compile fragment shader
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource.c_str());
    if (fragmentShader == 0)
    {
        glDeleteShader(vertexShader);
        std::cerr << "ERROR: EarthMaterial::initializeAtmosphereShader() - Fragment shader compilation failed" << "\n";
        std::cerr << "  Check console output above for shader compilation errors." << "\n";
        std::cerr << "  Atmosphere shader is required. Cannot continue." << "\n";
        std::exit(1);
    }

    // Link program
    atmosphereProgram_ = linkProgram(vertexShader, fragmentShader);

    // Shaders can be deleted after linking
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (atmosphereProgram_ == 0)
    {
        std::cerr << "ERROR: EarthMaterial::initializeAtmosphereShader() - Shader program linking failed" << "\n";
        std::cerr << "  Check console output above for shader linking errors." << "\n";
        std::cerr << "  Atmosphere shader is required. Cannot continue." << "\n";
        std::exit(1);
    }

    // IMPORTANT: Activate shader program before getting uniform locations
    // Some drivers require the program to be active when querying uniform
    // locations
    glUseProgram(atmosphereProgram_);

    // Get uniform locations (fullscreen ray march approach)
    uniformAtmoInvViewProj_ = glGetUniformLocation(atmosphereProgram_, "uInvViewProj");
    uniformAtmoCameraPos_ = glGetUniformLocation(atmosphereProgram_, "uCameraPos");
    uniformAtmoSunDir_ = glGetUniformLocation(atmosphereProgram_, "uSunDir");
    uniformAtmoPlanetPos_ = glGetUniformLocation(atmosphereProgram_, "uPlanetPos");
    uniformAtmoPlanetRadius_ = glGetUniformLocation(atmosphereProgram_, "uPlanetRadius");
    uniformAtmoAtmosphereRadius_ = glGetUniformLocation(atmosphereProgram_, "uAtmosphereRadius");
    uniformAtmoDensityTex_ = glGetUniformLocation(atmosphereProgram_, "uDensityLUT");
    uniformAtmoMaxAltitude_ = glGetUniformLocation(atmosphereProgram_, "uMaxAltitude");

    // Restore program state (we activated it earlier to get uniform locations)
    glUseProgram(0);

    // Debug: verify uniform locations
    std::cout << "  Atmosphere shader uniforms (fullscreen ray march):" << "\n";
    std::cout << "    uInvViewProj: " << uniformAtmoInvViewProj_ << "\n";
    std::cout << "    uCameraPos: " << uniformAtmoCameraPos_ << "\n";
    std::cout << "    uSunDir: " << uniformAtmoSunDir_ << "\n";
    std::cout << "    uPlanetPos: " << uniformAtmoPlanetPos_ << "\n";
    std::cout << "    uPlanetRadius: " << uniformAtmoPlanetRadius_ << "\n";
    std::cout << "    uAtmosphereRadius: " << uniformAtmoAtmosphereRadius_ << "\n";
    std::cout << "    uDensityLUT: " << uniformAtmoDensityTex_ << "\n";
    std::cout << "    uMaxAltitude: " << uniformAtmoMaxAltitude_ << "\n";

    // Validate critical uniforms (warn if missing, but don't fail - some may be optional)
    if (uniformAtmoInvViewProj_ < 0)
    {
        std::cerr << "WARNING: uInvViewProj uniform not found in atmosphere shader" << "\n";
    }
    if (uniformAtmoCameraPos_ < 0)
    {
        std::cerr << "WARNING: uCameraPos uniform not found in atmosphere shader" << "\n";
    }
    if (uniformAtmoSunDir_ < 0)
    {
        std::cerr << "WARNING: uSunDir uniform not found in atmosphere shader" << "\n";
    }
    if (uniformAtmoPlanetPos_ < 0)
    {
        std::cerr << "WARNING: uPlanetPos uniform not found in atmosphere shader" << "\n";
    }
    if (uniformAtmoPlanetRadius_ < 0)
    {
        std::cerr << "WARNING: uPlanetRadius uniform not found in atmosphere shader" << "\n";
    }
    if (uniformAtmoAtmosphereRadius_ < 0)
    {
        std::cerr << "WARNING: uAtmosphereRadius uniform not found in atmosphere shader" << "\n";
    }

    // Debug: Dump US Standard Atmosphere 1976 Layers
    std::cout << "\n=== US Standard Atmosphere 1976 Layers ===" << "\n";
    std::cout << "ID | Altitude (km) | Temp (K) | Pressure (Pa) | Name" << "\n";
    std::cout << "---|---------------|----------|---------------|-----------------" << "\n";

    struct AtmoLayer
    {
        float h;
        float T;
        float P;
        const char *n;
    };
    const AtmoLayer layers[] = {{0.0f, 288.15f, 101325.0f, "Troposphere"},
                                {11.0f, 216.65f, 22632.0f, "Tropopause"},
                                {20.0f, 216.65f, 5474.9f, "Stratosphere 1"},
                                {32.0f, 228.65f, 868.0f, "Stratosphere 2"},
                                {47.0f, 270.65f, 110.9f, "Stratopause"},
                                {51.0f, 270.65f, 66.9f, "Mesosphere"},
                                {71.0f, 214.65f, 3.9f, "Mesopause"}};

    for (int i = 0; i < 7; i++)
    {
        printf("%2d | %13.1f | %8.2f | %13.1f | %s\n", i, layers[i].h, layers[i].T, layers[i].P, layers[i].n);
    }
    std::cout << "==========================================\n" << "\n";

    // Try to load real atmosphere data from xlsx
    std::string atmosphereXlsxPath = "defaults/earth-surface/atmosphere/USStandardAtmosphere.xlsm";
    if (std::filesystem::exists(atmosphereXlsxPath))
    {
        if (loadAtmosphereData(atmosphereXlsxPath))
        {
            std::cout << "Using real USSA data from xlsx for atmospheric scattering" << "\n";
        }
        else
        {
            std::cout << "Falling back to analytical USSA76 model" << "\n";
        }
    }
    else
    {
        std::cout << "Atmosphere xlsx not found, using analytical USSA76 model" << "\n";
    }

    // Try to load precomputed transmittance LUT (from luts folder)
    std::string lutPath = "earth-textures/luts/earth_atmosphere_transmittance_lut.hdr";
    if (std::filesystem::exists(lutPath))
    {
        if (loadAtmosphereTransmittanceLUT(lutPath))
        {
            std::cout << "Atmosphere transmittance LUT loaded successfully" << "\n";
        }
        else
        {
            std::cout << "Failed to load atmosphere transmittance LUT, using ray marching" << "\n";
        }
    }
    else
    {
        std::cout << "Atmosphere transmittance LUT not found, using ray marching" << "\n";
        std::cout << "  Run preprocessing to generate: earth-textures/luts/earth_atmosphere_transmittance_lut.hdr"
                  << "\n";
    }

    // Try to load precomputed multiscatter LUT (from luts folder)
    std::string multiscatterPath = "earth-textures/luts/earth_atmosphere_multiscatter_lut.hdr";
    if (std::filesystem::exists(multiscatterPath))
    {
        if (loadAtmosphereMultiscatterLUT(multiscatterPath))
        {
            std::cout << "Atmosphere multiscatter LUT loaded successfully" << "\n";
        }
        else
        {
            std::cout << "Failed to load atmosphere multiscatter LUT, using fallback" << "\n";
        }
    }
    else
    {
        std::cout << "Atmosphere multiscatter LUT not found, using fallback multiscattering" << "\n";
        std::cout << "  Run preprocessing to generate: earth-textures/luts/earth_atmosphere_multiscatter_lut.hdr"
                  << "\n";
    }

    atmosphereAvailable_ = true;
    return true;
}

// ============================================================================
// Load Atmosphere Data from xlsx
// ============================================================================
// Parses US Standard Atmosphere data from xlsx file and creates a 1D lookup
// texture for density vs altitude. This provides more accurate atmospheric
// scattering than analytical approximations.

#ifdef HAS_OPENXLSX
bool EarthMaterial::loadAtmosphereData(const std::string &xlsxPath)
{
    try
    {
        std::cout << "Loading US Standard Atmosphere data from: " << xlsxPath << "\n";

        OpenXLSX::XLDocument doc;
        doc.open(xlsxPath);

        // Get the first worksheet
        auto worksheetNames = doc.workbook().worksheetNames();
        if (worksheetNames.empty())
        {
            std::cerr << "No worksheets found in atmosphere xlsx" << "\n";
            return false;
        }

        auto wks = doc.workbook().worksheet(worksheetNames.front());
        std::cout << "  Reading worksheet: " << worksheetNames.front() << "\n";

        // Parse the data - expect columns: Altitude(m), Temperature(K),
        // Pressure(Pa), Density(kg/m3) Or similar format - we're primarily
        // interested in altitude vs density
        std::vector<std::pair<float, float>> altitudeDensityData;

        // Find column indices by header names (row 1)
        int altCol = -1, densityCol = -1, tempCol = -1, pressCol = -1;
        uint32_t rowCount = wks.rowCount();
        uint32_t colCount = wks.columnCount();

        std::cout << "  Worksheet size: " << rowCount << " rows x " << colCount << " cols" << "\n";

        // Check headers in row 1
        for (uint32_t col = 1; col <= std::min(colCount, 20u); col++)
        {
            try
            {
                auto cell = wks.cell(1, col);
                std::string header = cell.value().get<std::string>();
                // Convert to lowercase for comparison
                std::string headerLower = header;
                std::transform(headerLower.begin(), headerLower.end(), headerLower.begin(), ::tolower);

                if (headerLower.find("altitude") != std::string::npos ||
                    headerLower.find("height") != std::string::npos || headerLower.find("z") == 0)
                {
                    altCol = col;
                    std::cout << "  Found altitude column at " << col << ": " << header << "\n";
                }
                if (headerLower.find("density") != std::string::npos || headerLower.find("rho") != std::string::npos)
                {
                    densityCol = col;
                    std::cout << "  Found density column at " << col << ": " << header << "\n";
                }
                if (headerLower.find("temp") != std::string::npos || headerLower.find("t ") != std::string::npos)
                {
                    tempCol = col;
                    std::cout << "  Found temperature column at " << col << ": " << header << "\n";
                }
                if (headerLower.find("press") != std::string::npos || headerLower.find("p ") != std::string::npos)
                {
                    pressCol = col;
                    std::cout << "  Found pressure column at " << col << ": " << header << "\n";
                }
            }
            catch (...)
            {
                continue;
            }
        }

        // If we didn't find headers, assume standard column layout
        if (altCol == -1)
            altCol = 1;
        if (densityCol == -1)
        {
            // If no density column, we might need to compute from T and P
            if (tempCol != -1 && pressCol != -1)
            {
                std::cout << "  Will compute density from T and P" << "\n";
            }
            else
            {
                densityCol = 4; // Assume 4th column is density
            }
        }

        // Read data rows (starting from row 2, assuming row 1 is headers)
        float maxAlt = 0.0f;
        float seaLevelDensity = 1.225f; // Default

        for (uint32_t row = 2; row <= rowCount; row++)
        {
            try
            {
                auto altCell = wks.cell(row, altCol);
                float altitude = static_cast<float>(altCell.value().get<double>());

                float density;
                if (densityCol != -1)
                {
                    auto densCell = wks.cell(row, densityCol);
                    density = static_cast<float>(densCell.value().get<double>());
                }
                else if (tempCol != -1 && pressCol != -1)
                {
                    // Compute density from ideal gas law: rho = P / (R * T)
                    auto tempCell = wks.cell(row, tempCol);
                    auto pressCell = wks.cell(row, pressCol);
                    float T = static_cast<float>(tempCell.value().get<double>());
                    float P = static_cast<float>(pressCell.value().get<double>());
                    const float R_gas = 287.05287f; // J/(kg·K) for air
                    density = P / (R_gas * T);
                }
                else
                {
                    continue;
                }

                // Skip invalid data
                if (altitude < 0 || density <= 0)
                    continue;

                // Convert altitude to meters if it appears to be in km
                if (altitude < 1000 && row > 10)
                {
                    altitude *= 1000.0f; // Likely in km
                }

                altitudeDensityData.push_back({altitude, density});
                maxAlt = std::max(maxAlt, altitude);

                if (altitude < 100.0f)
                {
                    seaLevelDensity = density;
                }
            }
            catch (...)
            {
                continue;
            }
        }

        doc.close();

        if (altitudeDensityData.empty())
        {
            std::cerr << "No valid atmosphere data found in xlsx" << "\n";
            return false;
        }

        std::cout << "  Loaded " << altitudeDensityData.size() << " data points" << "\n";
        std::cout << "  Altitude range: 0 to " << maxAlt << " m" << "\n";
        std::cout << "  Sea level density: " << seaLevelDensity << " kg/m^3" << "\n";

        // Sort by altitude
        std::sort(altitudeDensityData.begin(), altitudeDensityData.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });

        // Extend to 100km (exosphere) if data doesn't reach that high
        if (maxAlt < 100000.0f)
        {
            // Add exponential decay extension
            float lastAlt = altitudeDensityData.back().first;
            float lastDensity = altitudeDensityData.back().second;
            float scaleHeight = 8500.0f; // ~8.5km scale height for upper atmosphere

            for (float alt = lastAlt + 1000.0f; alt <= 100000.0f; alt += 1000.0f)
            {
                float density = lastDensity * std::exp(-(alt - lastAlt) / scaleHeight);
                altitudeDensityData.push_back({alt, density});
            }
            maxAlt = 100000.0f;
            std::cout << "  Extended data to 100km using exponential decay" << "\n";
        }

        atmosphereMaxAltitude_ = maxAlt;

        // Create 1D texture (1024 samples from 0 to maxAlt)
        const int texSize = 1024;
        std::vector<float> densityLUT(texSize);

        for (int i = 0; i < texSize; i++)
        {
            float targetAlt = (static_cast<float>(i) / (texSize - 1)) * maxAlt;

            // Interpolate from data
            float density = 0.0f;
            for (size_t j = 0; j < altitudeDensityData.size() - 1; j++)
            {
                if (altitudeDensityData[j].first <= targetAlt && altitudeDensityData[j + 1].first >= targetAlt)
                {
                    float t = (targetAlt - altitudeDensityData[j].first) /
                              (altitudeDensityData[j + 1].first - altitudeDensityData[j].first);
                    // Log interpolation for exponential data
                    float logD0 = std::log(std::max(altitudeDensityData[j].second, 1e-20f));
                    float logD1 = std::log(std::max(altitudeDensityData[j + 1].second, 1e-20f));
                    density = std::exp(logD0 + t * (logD1 - logD0));
                    break;
                }
            }

            // Normalize to sea level (store as ratio)
            densityLUT[i] = density / seaLevelDensity;
        }

        // Create OpenGL texture
        glGenTextures(1, &atmosphereDensityTexture_);
        glBindTexture(GL_TEXTURE_1D, atmosphereDensityTexture_);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

        // Upload as R32F (single channel float)
        glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, texSize, 0, GL_RED, GL_FLOAT, densityLUT.data());

        glBindTexture(GL_TEXTURE_1D, 0);

        atmosphereDataLoaded_ = true;
        std::cout << "  Created atmosphere density LUT texture (" << texSize << " samples)" << "\n";
        std::cout << "  Density at 0m: " << densityLUT[0] << " (normalized)" << "\n";
        std::cout << "  Density at 11km: " << densityLUT[int(11000.0f / maxAlt * texSize)] << "\n";
        std::cout << "  Density at 50km: " << densityLUT[int(50000.0f / maxAlt * texSize)] << "\n";

        return true;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error loading atmosphere xlsx: " << e.what() << "\n";
        return false;
    }
}
#else
bool EarthMaterial::loadAtmosphereData(const std::string &xlsxPath)
{
    std::cerr << "OpenXLSX not available - cannot load " << xlsxPath << "\n";
    std::cerr << "Using hardcoded USSA76 atmosphere model" << "\n";
    return false;
}
#endif

// ============================================================================
// Load Atmosphere Transmittance LUT
// ============================================================================
// Loads precomputed 2D transmittance LUT (altitude vs sun zenith angle)
// This avoids computing transmittance via ray marching every frame

bool EarthMaterial::loadAtmosphereTransmittanceLUT(const std::string &lutPath)
{
    if (!std::filesystem::exists(lutPath))
    {
        std::cerr << "Atmosphere transmittance LUT not found: " << lutPath << "\n";
        return false;
    }

    std::cout << "Loading atmosphere transmittance LUT from: " << lutPath << "\n";

    // Load HDR image using stb_image
    int width, height, channels;
    float *data = stbi_loadf(lutPath.c_str(), &width, &height, &channels, 3); // Force RGB

    if (!data)
    {
        std::cerr << "Failed to load atmosphere transmittance LUT: " << lutPath << "\n";
        std::cerr << "  Error: " << stbi_failure_reason() << "\n";
        return false;
    }

    if (channels < 3)
    {
        std::cerr << "Atmosphere transmittance LUT must have at least 3 channels (RGB)" << "\n";
        stbi_image_free(data);
        return false;
    }

    std::cout << "  Loaded LUT: " << width << "x" << height << " (RGB)" << "\n";

    // Create OpenGL texture
    glGenTextures(1, &atmosphereTransmittanceLUT_);
    glBindTexture(GL_TEXTURE_2D, atmosphereTransmittanceLUT_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload as RGB32F (3-channel float)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, data);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    // Get uniform locations
    uniformAtmoTransmittanceLUT_ = glGetUniformLocation(atmosphereProgram_, "uTransmittanceLUT");
    uniformAtmoUseTransmittanceLUT_ = glGetUniformLocation(atmosphereProgram_, "uUseTransmittanceLUT");

    if (uniformAtmoTransmittanceLUT_ < 0 || uniformAtmoUseTransmittanceLUT_ < 0)
    {
        std::cerr << "WARNING: Transmittance LUT uniforms not found in atmosphere shader" << "\n";
        std::cerr << "  Shader will use ray marching instead of LUT" << "\n";
    }

    atmosphereTransmittanceLUTLoaded_ = true;
    return true;
}

// Load Atmosphere Multiscatter LUT
// Loads precomputed 2D multiscatter LUT (Hillaire iterative energy redistribution)
bool EarthMaterial::loadAtmosphereMultiscatterLUT(const std::string &lutPath)
{
    if (!std::filesystem::exists(lutPath))
    {
        std::cerr << "Atmosphere multiscatter LUT not found: " << lutPath << "\n";
        return false;
    }

    std::cout << "Loading atmosphere multiscatter LUT from: " << lutPath << "\n";

    // Load HDR image using stb_image
    int width, height, channels;
    float *data = stbi_loadf(lutPath.c_str(), &width, &height, &channels, 3); // Force RGB

    if (!data)
    {
        std::cerr << "Failed to load atmosphere multiscatter LUT: " << lutPath << "\n";
        std::cerr << "  Error: " << stbi_failure_reason() << "\n";
        return false;
    }

    if (channels < 3)
    {
        std::cerr << "Atmosphere multiscatter LUT must have at least 3 channels (RGB)" << "\n";
        stbi_image_free(data);
        return false;
    }

    std::cout << "  Loaded multiscatter LUT: " << width << "x" << height << " (RGB)" << "\n";

    // Create OpenGL texture
    glGenTextures(1, &atmosphereMultiscatterLUT_);
    glBindTexture(GL_TEXTURE_2D, atmosphereMultiscatterLUT_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload as RGB32F (3-channel float)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, data);

    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    // Get uniform locations
    uniformAtmoMultiscatterLUT_ = glGetUniformLocation(atmosphereProgram_, "uMultiscatterLUT");
    uniformAtmoUseMultiscatterLUT_ = glGetUniformLocation(atmosphereProgram_, "uUseMultiscatterLUT");

    if (uniformAtmoMultiscatterLUT_ < 0 || uniformAtmoUseMultiscatterLUT_ < 0)
    {
        std::cerr << "WARNING: Multiscatter LUT uniforms not found in atmosphere shader" << "\n";
        std::cerr << "  Shader will use fallback multiscattering" << "\n";
    }

    atmosphereMultiscatterLUTLoaded_ = true;
    return true;
}