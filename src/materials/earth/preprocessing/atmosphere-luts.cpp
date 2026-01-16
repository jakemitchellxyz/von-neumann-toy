#include "../../../concerns/constants.h"
#include "../earth-material.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <stb_image_write.h>

// ============================================================================
// Preprocess Atmosphere LUTs
// ============================================================================
// Generates transmittance and scattering lookup tables for atmosphere rendering
// These are simplified placeholder LUTs that can be replaced with proper
// atmospheric scattering calculations later.

bool EarthMaterial::preprocessAtmosphereLUTs(const std::string &outputBasePath)
{
    std::string outputPath = outputBasePath + "/luts";

    std::cout << "=== Atmosphere LUT Processing ===" << '\n';

    // Create output directory
    std::filesystem::create_directories(outputPath);

    std::string transmittanceFile = outputPath + "/earth_atmosphere_transmittance_lut.hdr";
    std::string scatteringFile = outputPath + "/earth_atmosphere_scattering_lut.hdr";

    // Check if already processed
    if (std::filesystem::exists(transmittanceFile) && std::filesystem::exists(scatteringFile))
    {
        std::cout << "Atmosphere LUTs already exist: " << outputPath << '\n';
        std::cout << "==============================" << '\n';
        return true;
    }

    // LUT dimensions (matching shader constants)
    const int TRANSMITTANCE_WIDTH = 256;
    const int TRANSMITTANCE_HEIGHT = 128;
    const int SCATTERING_WIDTH = 256;
    const int SCATTERING_HEIGHT = 128;

    std::cout << "Generating atmosphere LUTs..." << '\n';

    // ==========================================================================
    // Generate Transmittance LUT (256x128)
    // ==========================================================================
    // Format: mu_sun (x-axis, -1 to 1) x height (y-axis, surface to top)
    // Values: RGB = transmittance (how much light passes through)
    std::vector<float> transmittanceData(TRANSMITTANCE_WIDTH * TRANSMITTANCE_HEIGHT * 3);

    for (int y = 0; y < TRANSMITTANCE_HEIGHT; ++y)
    {
        float height = 1.0f - (float(y) + 0.5f) / TRANSMITTANCE_HEIGHT; // 0 = top, 1 = surface
        float heightNormalized = height; // [0, 1] where 0 = top of atmosphere, 1 = surface

        for (int x = 0; x < TRANSMITTANCE_WIDTH; ++x)
        {
            float muSun = (float(x) + 0.5f) / TRANSMITTANCE_WIDTH * 2.0f - 1.0f; // [-1, 1]

            int idx = (y * TRANSMITTANCE_WIDTH + x) * 3;

            // Simple transmittance model:
            // - Higher altitude = more transmittance (less atmosphere)
            // - Sun overhead (muSun = 1) = more transmittance (shorter path)
            // - Sun at horizon (muSun = -1) = less transmittance (longer path)
            float transmittance = 1.0f - (1.0f - heightNormalized) * 0.3f; // Base transmittance increases with height
            transmittance *= 0.5f + 0.5f * (muSun + 1.0f); // More transmittance when sun is overhead

            // Clamp and apply to RGB (atmospheric transmittance affects all wavelengths similarly)
            transmittance = std::max(0.0f, std::min(1.0f, transmittance));
            transmittanceData[idx + 0] = transmittance; // R
            transmittanceData[idx + 1] = transmittance; // G
            transmittanceData[idx + 2] = transmittance; // B
        }
    }

    // Save transmittance LUT as HDR
    if (!stbi_write_hdr(transmittanceFile.c_str(), TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT, 3, transmittanceData.data()))
    {
        std::cerr << "Failed to write transmittance LUT: " << transmittanceFile << '\n';
        std::cout << "==============================" << '\n';
        return false;
    }

    std::cout << "Generated transmittance LUT: " << transmittanceFile << '\n';

    // ==========================================================================
    // Generate Scattering LUT (256x128)
    // ==========================================================================
    // Format: mu_sun (x-axis, -1 to 1) x height (y-axis, surface to top)
    // RGB channels encode scattering at different angles:
    //   R = forward scattering (nu ≈ 1, sun behind viewer)
    //   G = side scattering (nu ≈ 0, sun perpendicular)
    //   B = backward scattering (nu ≈ -1, sun in front of viewer)
    std::vector<float> scatteringData(SCATTERING_WIDTH * SCATTERING_HEIGHT * 3);

    for (int y = 0; y < SCATTERING_HEIGHT; ++y)
    {
        float height = 1.0f - (float(y) + 0.5f) / SCATTERING_HEIGHT; // 0 = top, 1 = surface
        float heightNormalized = height; // [0, 1] where 0 = top of atmosphere, 1 = surface

        for (int x = 0; x < SCATTERING_WIDTH; ++x)
        {
            float muSun = (float(x) + 0.5f) / SCATTERING_WIDTH * 2.0f - 1.0f; // [-1, 1]

            int idx = (y * SCATTERING_WIDTH + x) * 3;

            // Simple scattering model:
            // - More scattering at lower altitudes (more atmosphere)
            // - Forward scattering (R) is stronger (Mie scattering)
            // - Side scattering (G) is moderate
            // - Backward scattering (B) is weaker

            // Density increases toward surface (heightNormalized: 0 = top, 1 = surface)
            // At surface: heightNormalized = 1.0, density = 1.0
            // At top: heightNormalized = 0.0, density = 0.0
            float density = heightNormalized; // More density at surface (FIXED: was backwards)
            
            // Increase base scattering significantly - these values need to be much larger
            // The LUT stores scattering coefficients that will be multiplied by transmittance
            // Higher values = more visible scattering
            float baseScattering = density * 2.0f; // Increased from 0.1f to 2.0f

            // Forward scattering (R) - strongest (Mie scattering dominates)
            float forwardScattering = baseScattering * 3.0f; // Increased multiplier
            
            // Side scattering (G) - moderate (Rayleigh scattering - blue sky)
            float sideScattering = baseScattering * 1.5f; // Increased multiplier
            
            // Backward scattering (B) - weakest
            float backwardScattering = baseScattering * 0.8f; // Increased multiplier

            // Apply sun angle influence (more scattering when sun is overhead)
            float sunFactor = 0.5f + 0.5f * (muSun + 1.0f);
            forwardScattering *= sunFactor;
            sideScattering *= sunFactor;
            backwardScattering *= sunFactor;

            // Clamp values (allow higher values for stronger scattering)
            scatteringData[idx + 0] = std::max(0.0f, std::min(20.0f, forwardScattering));   // R
            scatteringData[idx + 1] = std::max(0.0f, std::min(20.0f, sideScattering));      // G
            scatteringData[idx + 2] = std::max(0.0f, std::min(20.0f, backwardScattering)); // B
        }
    }

    // Save scattering LUT as HDR
    if (!stbi_write_hdr(scatteringFile.c_str(), SCATTERING_WIDTH, SCATTERING_HEIGHT, 3, scatteringData.data()))
    {
        std::cerr << "Failed to write scattering LUT: " << scatteringFile << '\n';
        std::cout << "==============================" << '\n';
        return false;
    }

    std::cout << "Generated scattering LUT: " << scatteringFile << '\n';
    std::cout << "==============================" << '\n';
    return true;
}
