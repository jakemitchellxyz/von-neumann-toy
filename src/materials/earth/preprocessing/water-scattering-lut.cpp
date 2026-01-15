#include "../../../concerns/constants.h"
#include "../earth-material.h"

#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

// Water optical properties
// Reference: Pope & Fry (1997) "Absorption spectrum (380-700 nm) of pure water"
constexpr glm::vec3 WATER_ABSORPTION = glm::vec3(0.45f,   // Red (680nm): strongly absorbed
                                                 0.03f,   // Green (550nm): weakly absorbed
                                                 0.015f); // Blue (440nm): weakly absorbed

// Scattering coefficient (per meter) - much smaller than absorption (σ_a ≫ σ_s)
// Typical values for clear ocean water
constexpr glm::vec3 WATER_SCATTERING = glm::vec3(0.001f, 0.001f, 0.001f); // RGB scattering

// Extinction coefficient: σ_t = σ_a + σ_s
constexpr glm::vec3 WATER_EXTINCTION = WATER_ABSORPTION + WATER_SCATTERING;

constexpr float WATER_IOR = 1.339f;   // Seawater index of refraction
constexpr float MAX_DEPTH = 11000.0f; // Maximum ocean depth (Mariana Trench, meters)

// Henyey-Greenstein phase function parameter (forward scattering)
constexpr float WATER_PHASE_G = 0.9f; // Strong forward scattering

// Henyey-Greenstein phase function
// Returns phase function value for given cosine of scattering angle
// g: asymmetry parameter (0 = isotropic, 1 = forward, -1 = backward)
float henyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    if (denom < 1e-6f)
    {
        return 0.0f;
    }
    return (1.0f - g2) / (denom * std::sqrt(denom));
}

// Compute transmittance T_water(z, μ)
// z: depth from sea level [0, MAX_DEPTH], where 0 = sea level, positive = downward
// mu: cos(zenith angle) [-1, 1], where 1 = straight down, -1 = straight up
// Returns: RGB transmittance exp(−∫ σ_t ds)
// Note: z=0 is sea level, valid range depends on bathymetry at each (x,y) coordinate
glm::vec3 computeWaterTransmittance(float z, float mu)
{
    // Clamp inputs
    // z must be >= 0 (below sea level) and <= MAX_DEPTH
    z = std::max(0.0f, std::min(z, MAX_DEPTH));
    mu = std::max(-1.0f, std::min(mu, 1.0f));

    // Path length through water at depth z with angle mu
    // mu = cos(zenith), where mu=1 means straight down
    // Path length = z / max(|mu|, 0.1) to avoid division by zero
    // For mu < 0 (looking up), path length is still positive (distance traveled)
    float pathLength = z / std::max(std::abs(mu), 0.1f);

    // Transmittance: exp(−σ_t · pathLength)
    glm::vec3 transmittance = glm::exp(-WATER_EXTINCTION * pathLength);

    return transmittance;
}

// Compute single scattering S1_water(z, μ, μ_s, ν)
// z: depth from sea level [0, MAX_DEPTH], where 0 = sea level, positive = downward
// mu: cos(view zenith angle) [-1, 1], where 1 = straight down, -1 = straight up
// mu_s: cos(sun zenith angle) [-1, 1], where 1 = sun overhead, -1 = sun below horizon
// nu: cos(angle between view and sun directions) [-1, 1]
//     This accounts for relative camera rotation relative to sunlight direction
// Returns: RGB single scattering integral ∫ T_view(s) · T_sun(s) · σ_s · Φ(ω_view, ω_sun) ds
// Note: z=0 is sea level, valid range depends on bathymetry at each (x,y) coordinate
// This integrates along the view ray path from surface to depth z
glm::vec3 computeWaterSingleScattering(float z, float mu, float mu_s, float nu)
{
    // Clamp inputs
    z = std::max(0.0f, std::min(z, MAX_DEPTH));
    mu = std::max(-1.0f, std::min(mu, 1.0f));
    mu_s = std::max(-1.0f, std::min(mu_s, 1.0f));
    nu = std::max(-1.0f, std::min(nu, 1.0f));

    // If mu_s < 0, sun is below horizon (not visible)
    if (mu_s < 0.0f)
    {
        // Sun below horizon - no direct sunlight, only ambient
        return glm::vec3(0.0f);
    }

    // Use nu directly as the scattering angle cosine
    // nu = cos(angle between view and sun directions) accounts for relative camera rotation
    float cosTheta = nu;
    cosTheta = std::max(-1.0f, std::min(1.0f, cosTheta));

    // Henyey-Greenstein phase function
    float phase = henyeyGreenstein(cosTheta, WATER_PHASE_G);

    // Integrate along view ray path from surface (z=0) to depth z
    // Use numerical integration with multiple steps
    const int numSteps = 32;
    float stepSize = z / static_cast<float>(numSteps);

    glm::vec3 S1 = glm::vec3(0.0f);

    for (int i = 0; i < numSteps; i++)
    {
        // Current depth along view ray
        float s = (static_cast<float>(i) + 0.5f) * stepSize; // Midpoint of step

        // Path length from surface to this point along view ray
        float viewPathToPoint = s / std::max(std::abs(mu), 0.1f);

        // Path length from surface to this point along sun ray (to reach same depth)
        float sunPathToPoint = s / std::max(std::abs(mu_s), 0.1f);

        // Transmittance from sun to scattering point
        glm::vec3 T_sun = glm::exp(-WATER_EXTINCTION * sunPathToPoint);

        // Transmittance from scattering point back to surface along view ray
        glm::vec3 T_view = glm::exp(-WATER_EXTINCTION * viewPathToPoint);

        // Single scattering contribution at this point
        // S1 += T_view(s) · T_sun(s) · σ_s · Φ · ds
        glm::vec3 contribution = T_view * T_sun * WATER_SCATTERING * phase * stepSize;
        S1 += contribution;
    }

    return S1;
}

// Compute multiple scattering Sm_water(z, μ) iteratively
// z: depth from sea level [0, MAX_DEPTH], where 0 = sea level, positive = downward
// mu: cos(view zenith angle) [-1, 1]
// Returns: RGB multiple scattering (isotropic approximation)
// Note: z=0 is sea level, valid range depends on bathymetry at each (x,y) coordinate
glm::vec3 computeWaterMultipleScattering(float z, float mu)
{
    // Clamp inputs
    z = std::max(0.0f, std::min(z, MAX_DEPTH));
    mu = std::max(-1.0f, std::min(mu, 1.0f));

    // Path length through water
    float pathLength = z / std::max(std::abs(mu), 0.1f);

    // Transmittance along view path
    glm::vec3 T_view = glm::exp(-WATER_EXTINCTION * pathLength);

    // Multiple scattering: isotropic approximation
    // Sm = ∫ T_view · σ_s · (S1_avg + Sm_prev) ds
    // For simplicity, use isotropic single scattering average
    // Iterate 2-4 times for convergence

    glm::vec3 Sm = glm::vec3(0.0f);
    const int iterations = 3;

    for (int i = 0; i < iterations; i++)
    {
        // Isotropic single scattering contribution (average over all sun angles)
        // Approximate as: T_view · σ_s · (S1_avg + Sm) · pathLength
        glm::vec3 S1_avg = WATER_SCATTERING * pathLength * 0.5f; // Average phase function ≈ 0.5
        glm::vec3 Sm_new = T_view * WATER_SCATTERING * (S1_avg + Sm) * pathLength;
        Sm = Sm_new;
    }

    return Sm;
}

// Generate T_water(z, μ) transmittance LUT (2D)
// z: depth [0, MAX_DEPTH], μ: cos(zenith) [-1, 1]
bool EarthMaterial::generateWaterTransmittanceLUT(const std::string &outputPath, int depthRes, int muRes)
{
    std::cout << "=== Generating Water Transmittance LUT (T_water) ===" << '\n';
    std::cout << "Resolution: depth=" << depthRes << ", mu=" << muRes << '\n';
    std::cout << "Output: " << outputPath << '\n';

    std::vector<float> lutData(depthRes * muRes * 3); // HDR format

    std::cout << "Generating transmittance LUT..." << '\n';

    for (int muIdx = 0; muIdx < muRes; muIdx++)
    {
        // mu: [-1, 1]
        float mu = (static_cast<float>(muIdx) / (muRes - 1.0f)) * 2.0f - 1.0f;

        for (int zIdx = 0; zIdx < depthRes; zIdx++)
        {
            // z: [0, MAX_DEPTH] with square root mapping for better shallow water resolution
            float u_normalized = static_cast<float>(zIdx) / (depthRes - 1.0f);
            float z = u_normalized * u_normalized * MAX_DEPTH;

            glm::vec3 transmittance = computeWaterTransmittance(z, mu);

            int idx = (muIdx * depthRes + zIdx) * 3;
            lutData[idx + 0] = transmittance.r;
            lutData[idx + 1] = transmittance.g;
            lutData[idx + 2] = transmittance.b;
        }

        if ((muIdx + 1) % 10 == 0)
        {
            std::cout << "\r  Progress: " << (muIdx + 1) << "/" << muRes << " (" << ((muIdx + 1) * 100 / muRes) << "%)"
                      << std::flush;
        }
    }

    std::cout << "\r  Progress: " << muRes << "/" << muRes << " (100%)" << '\n';

    // Save as HDR
    std::cout << "Saving transmittance LUT..." << '\n';
    if (!stbi_write_hdr(outputPath.c_str(), depthRes, muRes, 3, lutData.data()))
    {
        std::cerr << "ERROR: Failed to save water transmittance LUT" << '\n';
        return false;
    }

    std::cout << "Water transmittance LUT generated successfully" << '\n';
    return true;
}

// Generate S1_water(z, μ, μ_s, ν) single scattering LUT (4D packed as 2D)
// z: depth [0, MAX_DEPTH] from sea level (0 = sea level, positive = downward)
// μ: cos(view zenith) [-1, 1]
// μ_s: cos(sun zenith) [-1, 1]
// ν: cos(angle between view and sun directions) [-1, 1]
//     This accounts for relative camera rotation relative to sunlight direction
// Resolution parameters control LUT fidelity:
// - depthRes: Number of depth samples (higher = better depth resolution for bathymetry)
// - muRes: Number of view angle samples (higher = better view angle resolution)
// - muSunRes: Number of sun angle samples (higher = better sun angle resolution)
// - nuRes: Number of relative angle samples (higher = better scattering angle resolution)
bool EarthMaterial::generateWaterSingleScatteringLUT(const std::string &outputPath,
                                                     int depthRes,
                                                     int muRes,
                                                     int muSunRes,
                                                     int nuRes)
{
    std::cout << "=== Generating Water Single Scattering LUT (S1_water) ===" << '\n';
    std::cout << "Resolution: depth=" << depthRes << " samples, mu=" << muRes << " samples, mu_s=" << muSunRes
              << " samples, nu=" << nuRes << " samples" << '\n';
    std::cout << "Depth range: [0, " << MAX_DEPTH << "] meters (0 = sea level)" << '\n';
    std::cout << "Output: " << outputPath << '\n';

    // Pack 4D as 2D: width = depthRes * muSunRes * nuRes, height = muRes
    int width = depthRes * muSunRes * nuRes;
    int height = muRes;

    std::vector<float> lutData(width * height * 3); // HDR format

    std::cout << "Generating single scattering LUT..." << '\n';

    for (int muIdx = 0; muIdx < muRes; muIdx++)
    {
        // mu: [-1, 1]
        float mu = (static_cast<float>(muIdx) / (muRes - 1.0f)) * 2.0f - 1.0f;

        for (int muSunIdx = 0; muSunIdx < muSunRes; muSunIdx++)
        {
            // mu_s: [-1, 1]
            float mu_s = (static_cast<float>(muSunIdx) / (muSunRes - 1.0f)) * 2.0f - 1.0f;

            for (int nuIdx = 0; nuIdx < nuRes; nuIdx++)
            {
                // nu: [-1, 1] - cos(angle between view and sun directions)
                float nu = (static_cast<float>(nuIdx) / (nuRes - 1.0f)) * 2.0f - 1.0f;

                for (int zIdx = 0; zIdx < depthRes; zIdx++)
                {
                    // z: [0, MAX_DEPTH] with square root mapping
                    float u_normalized = static_cast<float>(zIdx) / (depthRes - 1.0f);
                    float z = u_normalized * u_normalized * MAX_DEPTH;

                    glm::vec3 S1 = computeWaterSingleScattering(z, mu, mu_s, nu);

                    // Pack: x = zIdx + muSunIdx * depthRes + nuIdx * (depthRes * muSunRes), y = muIdx
                    int x = zIdx + muSunIdx * depthRes + nuIdx * (depthRes * muSunRes);
                    int y = muIdx;
                    int idx = (y * width + x) * 3;

                    lutData[idx + 0] = S1.r;
                    lutData[idx + 1] = S1.g;
                    lutData[idx + 2] = S1.b;
                }
            }
        }

        if ((muIdx + 1) % 10 == 0)
        {
            std::cout << "\r  Progress: " << (muIdx + 1) << "/" << muRes << " (" << ((muIdx + 1) * 100 / muRes) << "%)"
                      << std::flush;
        }
    }

    std::cout << "\r  Progress: " << muRes << "/" << muRes << " (100%)" << '\n';

    // Save as HDR
    std::cout << "Saving single scattering LUT..." << '\n';
    if (!stbi_write_hdr(outputPath.c_str(), width, height, 3, lutData.data()))
    {
        std::cerr << "ERROR: Failed to save water single scattering LUT" << '\n';
        return false;
    }

    std::cout << "Water single scattering LUT generated successfully (" << width << "x" << height << ")" << '\n';
    return true;
}

// Generate Sm_water(z, μ) multiple scattering LUT (2D)
// z: depth [0, MAX_DEPTH] from sea level (0 = sea level, positive = downward)
// μ: cos(view zenith) [-1, 1]
// Resolution parameters control LUT fidelity:
// - depthRes: Number of depth samples (higher = better depth resolution for bathymetry)
// - muRes: Number of angular samples (higher = better angular resolution)
// Note: Multiple scattering is isotropic approximation, so lower resolution may be acceptable
bool EarthMaterial::generateWaterMultipleScatteringLUT(const std::string &outputPath, int depthRes, int muRes)
{
    std::cout << "=== Generating Water Multiple Scattering LUT (Sm_water) ===" << '\n';
    std::cout << "Resolution: depth=" << depthRes << " samples, mu=" << muRes << " samples" << '\n';
    std::cout << "Depth range: [0, " << MAX_DEPTH << "] meters (0 = sea level)" << '\n';
    std::cout << "Output: " << outputPath << '\n';

    std::vector<float> lutData(depthRes * muRes * 3); // HDR format

    std::cout << "Generating multiple scattering LUT..." << '\n';

    for (int muIdx = 0; muIdx < muRes; muIdx++)
    {
        // mu: [-1, 1]
        float mu = (static_cast<float>(muIdx) / (muRes - 1.0f)) * 2.0f - 1.0f;

        for (int zIdx = 0; zIdx < depthRes; zIdx++)
        {
            // z: [0, MAX_DEPTH] with square root mapping
            float u_normalized = static_cast<float>(zIdx) / (depthRes - 1.0f);
            float z = u_normalized * u_normalized * MAX_DEPTH;

            glm::vec3 Sm = computeWaterMultipleScattering(z, mu);

            int idx = (muIdx * depthRes + zIdx) * 3;
            lutData[idx + 0] = Sm.r;
            lutData[idx + 1] = Sm.g;
            lutData[idx + 2] = Sm.b;
        }

        if ((muIdx + 1) % 10 == 0)
        {
            std::cout << "\r  Progress: " << (muIdx + 1) << "/" << muRes << " (" << ((muIdx + 1) * 100 / muRes) << "%)"
                      << std::flush;
        }
    }

    std::cout << "\r  Progress: " << muRes << "/" << muRes << " (100%)" << '\n';

    // Save as HDR
    std::cout << "Saving multiple scattering LUT..." << '\n';
    if (!stbi_write_hdr(outputPath.c_str(), depthRes, muRes, 3, lutData.data()))
    {
        std::cerr << "ERROR: Failed to save water multiple scattering LUT" << '\n';
        return false;
    }

    std::cout << "Water multiple scattering LUT generated successfully (" << depthRes << "x" << muRes << ")" << '\n';
    return true;
}

// Generate all water LUTs (wrapper function for convenience)
bool EarthMaterial::generateWaterScatteringLUT(const std::string &outputBasePath,
                                               int depthRes,
                                               int muRes,
                                               int muSunRes,
                                               int nuRes)
{
    std::string transmittancePath = outputBasePath + "/earth_water_transmittance_lut.hdr";
    std::string singleScatterPath = outputBasePath + "/earth_water_single_scatter_lut.hdr";
    std::string multiscatterPath = outputBasePath + "/earth_water_multiscatter_lut.hdr";

    bool success = true;

    success &= generateWaterTransmittanceLUT(transmittancePath, depthRes, muRes);
    std::cout << '\n';

    success &= generateWaterSingleScatteringLUT(singleScatterPath, depthRes, muRes, muSunRes, nuRes);
    std::cout << '\n';

    success &= generateWaterMultipleScatteringLUT(multiscatterPath, depthRes, muRes);
    std::cout << '\n';

    if (success)
    {
        std::cout << "All water LUTs generated successfully" << '\n';
        std::cout << "===================================" << '\n';
    }

    return success;
}
