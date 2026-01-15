#include "../../../concerns/constants.h"
#include "../../helpers/sin-distance-fields.h"
#include "../earth-material.h"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

// stb_image_write implementation already in setup.cpp
#include <stb_image_write.h>

// Physical constants for atmosphere scattering
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
constexpr float PI_F = static_cast<float>(M_PI);
constexpr float g0 = 9.80665f;          // m/s^2
constexpr float R_gas = 287.05287f;     // J/(kg·K) - specific gas constant for air
constexpr float rho_sea_level = 1.225f; // kg/m^3

// Rayleigh scattering coefficients at sea level (m^-1)
// Red: 680nm, Green: 550nm, Blue: 440nm
constexpr float BETA_R_RED = 5.802e-6f;
constexpr float BETA_R_GREEN = 13.558e-6f;
constexpr float BETA_R_BLUE = 33.100e-6f;

// Mie scattering coefficient at sea level (m^-1)
constexpr float BETA_M_SEA = 2.0e-5f;

// Maximum altitude for atmosphere (meters)
// Extended to exosphere height (~10,000km) for proper light refraction at high altitudes
constexpr float MAX_ALTITUDE = 10000000.0f; // 10,000km (exosphere)

// US Standard Atmosphere 1976 layer definitions
struct AtmoLayer
{
    float h0; // Base altitude (m)
    float T0; // Base temperature (K)
    float P0; // Base pressure (Pa)
    float L;  // Temperature lapse rate (K/m)
};

constexpr AtmoLayer LAYERS[] = {
    {0.0f, 288.15f, 101325.0f, -0.0065f},     // Troposphere
    {11000.0f, 216.65f, 22632.06f, 0.0f},     // Tropopause
    {20000.0f, 216.65f, 5474.889f, 0.001f},   // Stratosphere 1
    {32000.0f, 228.65f, 868.0187f, 0.0028f},  // Stratosphere 2
    {47000.0f, 270.65f, 110.9063f, 0.0f},     // Stratopause
    {51000.0f, 270.65f, 66.93887f, -0.0028f}, // Mesosphere
    {71000.0f, 214.65f, 3.956420f, -0.002f}   // Mesopause
};

constexpr int NUM_LAYERS = 7;
constexpr float H_SCALE_UPPER = 8500.0f; // Scale height for upper atmosphere

// Get atmospheric density at altitude using USSA76 model
float getAtmosphereDensity(float altitude_m)
{
    if (altitude_m < 0.0f)
        altitude_m = 0.0f;

    // Above mesopause: use exponential decay
    if (altitude_m > 84852.0f)
    {
        float rho_84km = 3.956420f / (R_gas * 214.65f);
        return rho_84km * std::exp(-(altitude_m - 84852.0f) / H_SCALE_UPPER);
    }

    // Find which layer we're in
    int layerIdx = 0;
    for (int i = 0; i < NUM_LAYERS - 1; i++)
    {
        if (altitude_m >= LAYERS[i].h0 && altitude_m < LAYERS[i + 1].h0)
        {
            layerIdx = i;
            break;
        }
        if (i == NUM_LAYERS - 2 && altitude_m >= LAYERS[i + 1].h0)
        {
            layerIdx = NUM_LAYERS - 1;
            break;
        }
    }

    const AtmoLayer &layer = LAYERS[layerIdx];
    float dh = altitude_m - layer.h0;

    float T, P;
    if (std::abs(layer.L) > 1e-6f)
    {
        // Non-isothermal layer
        T = layer.T0 + layer.L * dh;
        P = layer.P0 * std::pow(T / layer.T0, -g0 / (layer.L * R_gas));
    }
    else
    {
        // Isothermal layer
        T = layer.T0;
        P = layer.P0 * std::exp(-g0 * dh / (R_gas * T));
    }

    // Density from ideal gas law
    float rho = P / (R_gas * T);
    return rho / rho_sea_level; // Normalized to sea level
}

// ============================================================
// Bruneton-style Transmittance LUT
// ============================================================
// Parameterization: mu (cos(view zenith angle), mu_s (cos(sun zenith angle))
// This properly handles all viewing angles including grazing rays
//
// Reference: Bruneton & Neyret (2008) "Precomputed Atmospheric Scattering"

// Compute transmittance along a ray from point P to atmosphere boundary
// Uses Bruneton parameterization: mu = cos(zenith angle from vertical)
// Returns RGB transmittance (exp(-tau)) for Rayleigh wavelengths
glm::vec3 computeTransmittanceBruneton(float r, float mu, int numSteps = 128)
{
    // r: distance from planet center (in meters)
    // mu: cos(zenith angle) = dot(rayDir, vertical)
    //     mu = 1: ray pointing straight up
    //     mu = 0: ray horizontal
    //     mu = -1: ray pointing straight down

    const float planetRadius = 6371000.0f; // meters
    const float atmosphereRadius = planetRadius + MAX_ALTITUDE;

    // Clamp r to valid range
    r = std::max(planetRadius, std::min(r, atmosphereRadius));

    // Compute ray direction from mu
    // mu = cos(zenith), so we need to construct a ray from the point at radius r
    // For simplicity, we use a coordinate system where vertical is (0,1,0)
    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - mu * mu));
    glm::vec3 rayDir(0.0f, mu, sinTheta); // Ray direction in local frame

    // Point on sphere at radius r (on the equator, pointing up)
    glm::vec3 rayOrigin(0.0f, r, 0.0f);

    // Find intersection with atmosphere boundary using SDF
    // Note: rayOrigin is already at (0, r, 0) where r is distance from planet center
    // So the sphere center is at origin (0, 0, 0)
    glm::vec3 sphereCenter(0.0f, 0.0f, 0.0f);
    float t0, t1;
    if (!raySphereIntersect(rayOrigin, rayDir, sphereCenter, atmosphereRadius, t0, t1))
    {
        // Ray doesn't intersect atmosphere - return full transmittance
        return glm::vec3(1.0f);
    }

    // Use the exit point (t1) - distance to atmosphere boundary
    float pathLength = t1;
    if (pathLength <= 0.0f)
    {
        // Ray starts outside or at boundary
        return glm::vec3(1.0f);
    }

    // Check if ray hits planet surface using SDF
    float tPlanet0, tPlanet1;
    if (raySphereIntersect(rayOrigin, rayDir, sphereCenter, planetRadius, tPlanet0, tPlanet1))
    {
        if (tPlanet0 > 0.0f && tPlanet0 < pathLength)
        {
            // Ray hits planet - no transmittance
            return glm::vec3(0.0f);
        }
    }

    // Ray march from origin to atmosphere boundary
    float stepSize = pathLength / static_cast<float>(numSteps);

    float opticalDepthR = 0.0f;
    float opticalDepthM = 0.0f;

    for (int i = 0; i < numSteps; i++)
    {
        float t = (static_cast<float>(i) + 0.5f) * stepSize;
        glm::vec3 pos = rayOrigin + rayDir * t;

        // Altitude at this point using SDF for consistent calculation
        float sdfDist = sdSphere(pos, sphereCenter, planetRadius);
        float distanceFromCenter = glm::length(pos);
        float altitude = distanceFromCenter - planetRadius;

        // CRITICAL: Clamp to atmosphere bounds - never evaluate outside
        // Safety check - should not happen if intersection math is correct
        if (altitude < 0.0f || altitude >= MAX_ALTITUDE)
        {
            break; // Stop marching when outside bounds
        }

        // Get density
        float density = getAtmosphereDensity(altitude);

        // CRITICAL: Early-out when density is effectively zero
        // This prevents accumulating transmittance without scattering (black fog)
        const float MIN_DENSITY_THRESHOLD = 1e-6f;
        if (density < MIN_DENSITY_THRESHOLD)
        {
            break; // Stop marching when density is zero - no medium to attenuate through
        }

        // Accumulate optical depth (stepSize is already in meters)
        opticalDepthR += density * stepSize;
        opticalDepthM += density * stepSize;
    }

    // Compute transmittance for RGB wavelengths
    float tauRed = BETA_R_RED * opticalDepthR + BETA_M_SEA * opticalDepthM;
    float tauGreen = BETA_R_GREEN * opticalDepthR + BETA_M_SEA * opticalDepthM;
    float tauBlue = BETA_R_BLUE * opticalDepthR + BETA_M_SEA * opticalDepthM;

    return glm::vec3(std::exp(-tauRed), std::exp(-tauGreen), std::exp(-tauBlue));
}

// Helper: Get transmittance for a ray from point P to sun
// r: distance from planet center
// mu_s: cos(sun zenith angle) at point P
glm::vec3 getTransmittanceToSun(float r, float mu_s)
{
    return computeTransmittanceBruneton(r, mu_s);
}

// ============================================================
// Hillaire Multiscattering LUT
// ============================================================
// Uses iterative energy redistribution to compute multiscattering
// Reference: Hillaire (2015) "A Scalable and Production-Ready Sky and Atmosphere Rendering Technique"

// Compute single-scatter radiance (for multiscattering computation)
glm::vec3 computeSingleScatter(float r, float mu, float mu_s, float nu, int numSteps = 64)
{
    const float planetRadius = 6371000.0f;
    const float atmosphereRadius = planetRadius + MAX_ALTITUDE;

    r = std::max(planetRadius, std::min(r, atmosphereRadius));

    // Build coordinate system
    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - mu * mu));
    glm::vec3 viewDir(0.0f, mu, sinTheta);

    float sinThetaS = std::sqrt(std::max(0.0f, 1.0f - mu_s * mu_s));
    // nu = cos(angle between view and sun directions)
    // We need to construct sun direction such that dot(viewDir, sunDir) = nu
    float cosPhi = (nu - mu * mu_s) / (sinTheta * sinThetaS + 1e-6f);
    cosPhi = std::max(-1.0f, std::min(1.0f, cosPhi));
    float sinPhi = std::sqrt(std::max(0.0f, 1.0f - cosPhi * cosPhi));
    glm::vec3 sunDir(sinPhi * sinThetaS, mu_s, cosPhi * sinThetaS);

    glm::vec3 rayOrigin(0.0f, r, 0.0f);

    // Intersect view ray with atmosphere using SDF
    glm::vec3 sphereCenter(0.0f, 0.0f, 0.0f); // Planet center
    float t0, t1;
    if (!raySphereIntersect(rayOrigin, viewDir, sphereCenter, atmosphereRadius, t0, t1))
        return glm::vec3(0.0f);

    float pathLength = t1;
    if (pathLength <= 0.0f)
        return glm::vec3(0.0f);

    // Check planet intersection using SDF
    float tPlanet0, tPlanet1;
    if (raySphereIntersect(rayOrigin, viewDir, sphereCenter, planetRadius, tPlanet0, tPlanet1))
    {
        if (tPlanet0 > 0.0f && tPlanet0 < pathLength)
            pathLength = tPlanet0;
    }

    float stepSize = pathLength / static_cast<float>(numSteps);
    glm::vec3 scatterR = glm::vec3(0.0f);
    glm::vec3 scatterM = glm::vec3(0.0f);

    float opticalDepthR = 0.0f;
    float opticalDepthM = 0.0f;

    for (int i = 0; i < numSteps; i++)
    {
        float t = (static_cast<float>(i) + 0.5f) * stepSize;
        glm::vec3 pos = rayOrigin + viewDir * t;
        // Use SDF for consistent distance calculation
        float sdfDist = sdSphere(pos, sphereCenter, planetRadius);
        float distanceFromCenter = glm::length(pos);
        float altitude = distanceFromCenter - planetRadius;

        // CRITICAL: Clamp to atmosphere bounds - never evaluate outside
        if (altitude < 0.0f || altitude >= MAX_ALTITUDE)
            break;

        float density = getAtmosphereDensity(altitude);

        // CRITICAL: Early-out when density is effectively zero
        // This prevents accumulating transmittance without scattering
        const float MIN_DENSITY_THRESHOLD = 1e-6f;
        if (density < MIN_DENSITY_THRESHOLD)
            break;

        opticalDepthR += density * stepSize;
        opticalDepthM += density * stepSize;

        // Transmittance from sun to this point
        glm::vec3 posNormalized = glm::normalize(pos);
        glm::vec3 sunTransmittance = getTransmittanceToSun(distanceFromCenter, glm::dot(posNormalized, sunDir));

        // Phase functions
        float cosScatter = glm::dot(viewDir, sunDir);
        float phaseR = (3.0f / (16.0f * PI)) * (1.0f + cosScatter * cosScatter);
        float g = 0.76f;
        float g2 = g * g;
        float phaseM = (3.0f / (8.0f * PI)) * ((1.0f - g2) * (1.0f + cosScatter * cosScatter)) /
                       ((2.0f + g2) * std::pow(1.0f + g2 - 2.0f * g * cosScatter, 1.5f));

        // View transmittance to this point
        glm::vec3 viewTransmittance = computeTransmittanceBruneton(r, mu, 32);
        glm::vec3 transmittanceToPoint =
            computeTransmittanceBruneton(distanceFromCenter, glm::dot(posNormalized, viewDir), 32);
        glm::vec3 transmittance = viewTransmittance / (transmittanceToPoint + glm::vec3(1e-6f));

        scatterR += density * sunTransmittance * transmittance * phaseR * stepSize;
        scatterM += density * sunTransmittance * transmittance * phaseM * stepSize;
    }

    return BETA_R_RED * scatterR + BETA_M_SEA * scatterM;
}

// Hillaire iterative multiscattering computation
glm::vec3 computeMultiscatterHillaire(float r, float mu_s, int numIterations = 3)
{
    // CRITICAL: Ensure multiscatter LUT is zero at TOA
    // This prevents black fog from dark energy
    const float planetRadius = 6371000.0f;
    const float atmosphereRadius = planetRadius + MAX_ALTITUDE;

    // Early-out if outside atmosphere bounds
    if (r >= atmosphereRadius)
    {
        return glm::vec3(0.0f); // Exactly zero at and above TOA
    }

    // Start with single scatter
    glm::vec3 L = glm::vec3(0.0f);

    // Sample multiple view directions and accumulate scattered light
    const int numSamples = 16;
    for (int iter = 0; iter < numIterations; iter++)
    {
        glm::vec3 L_iter = glm::vec3(0.0f);

        for (int i = 0; i < numSamples; i++)
        {
            // Uniform sampling on hemisphere
            float u1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(numSamples);
            float mu = 1.0f - 2.0f * u1; // -1 to 1

            // Sample azimuth angle
            float u2 = static_cast<float>(i) / static_cast<float>(numSamples);
            float phi = 2.0f * PI_F * u2;
            float nu = mu * mu_s + std::sqrt(1.0f - mu * mu) * std::sqrt(1.0f - mu_s * mu_s) * std::cos(phi);

            // Single scatter for this direction
            glm::vec3 singleScatter = computeSingleScatter(r, mu, mu_s, nu, 32);

            // Add multiscatter from previous iteration (energy redistribution)
            if (iter > 0)
            {
                // Approximate: multiscatter comes from all directions
                // Use average of previous iteration's multiscatter
                glm::vec3 multiScatter = L / static_cast<float>(numSamples);
                singleScatter += multiScatter * 0.5f; // Redistribution factor
            }

            L_iter += singleScatter;
        }

        L = L_iter / static_cast<float>(numSamples);
    }

    return L;
}

bool EarthMaterial::preprocessAtmosphereTransmittanceLUT(const std::string &outputBasePath)
{
    // Create luts subdirectory
    std::string lutsPath = outputBasePath + "/luts";
    std::filesystem::create_directories(lutsPath);

    std::string outputPath = lutsPath + "/earth_atmosphere_transmittance_lut.hdr";
    std::string multiscatterPath = lutsPath + "/earth_atmosphere_multiscatter_lut.hdr";
    std::string singleScatterPath = lutsPath + "/earth_atmosphere_single_scatter_lut.hdr";

    // Check if already exists
    if (std::filesystem::exists(outputPath) && std::filesystem::exists(multiscatterPath) &&
        std::filesystem::exists(singleScatterPath))
    {
        std::cout << "Atmosphere transmittance LUTs already exist" << "\n";
        return true;
    }

    std::cout << "=== Generating Bruneton-style Atmosphere LUTs ===" << "\n";
    std::cout << "Transmittance LUT: " << outputPath << "\n";
    std::cout << "Single Scatter LUT: " << singleScatterPath << "\n";
    std::cout << "Multiscatter LUT: " << multiscatterPath << "\n";

    const float planetRadius = 6371000.0f;
    const float atmosphereRadius = planetRadius + MAX_ALTITUDE;

    // Bruneton LUT dimensions
    // R: distance from planet center (normalized: [R_ground, R_atmosphere])
    // Mu_s: cos(sun zenith angle) [-1, 1]
    const int transWidth = 256;  // R samples
    const int transHeight = 128; // mu_s samples

    std::vector<float> transLutData(transWidth * transHeight * 3); // RGB

    std::cout << "Computing transmittance LUT (" << transWidth << "x" << transHeight << ")..." << "\n";

    // Parallelize row processing
    const unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
    std::cout << "  Using " << numThreads << " threads" << "\n";

    std::atomic<int> completedRows(0);
    std::mutex progressMutex;

    auto processTransmittanceRow = [&](int startY, int endY) {
        for (int y = startY; y < endY; y++)
        {
            // mu_s: cos(sun zenith angle) from -1 (sun below) to 1 (sun overhead)
            float mu_s = -1.0f + 2.0f * (static_cast<float>(y) / static_cast<float>(transHeight - 1));

            for (int x = 0; x < transWidth; x++)
            {
                // R: distance from planet center, normalized [R_ground, R_atmosphere]
                float u = static_cast<float>(x) / static_cast<float>(transWidth - 1);
                // Use nonlinear mapping for better resolution near surface
                float r = planetRadius + u * u * (atmosphereRadius - planetRadius);

                // Compute transmittance to sun
                glm::vec3 transmittance = getTransmittanceToSun(r, mu_s);

                int idx = (y * transWidth + x) * 3;
                transLutData[idx + 0] = transmittance.r;
                transLutData[idx + 1] = transmittance.g;
                transLutData[idx + 2] = transmittance.b;
            }

            // Thread-safe progress reporting
            int completed = ++completedRows;
            if (completed % 16 == 0)
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                std::cout << "  Transmittance progress: " << completed << "/" << transHeight << " rows" << "\n";
            }
        }
    };

    // Launch threads
    std::vector<std::thread> threads;
    int rowsPerThread = static_cast<int>((transHeight + static_cast<int>(numThreads) - 1) /
                                         static_cast<int>(numThreads)); // Ceiling division

    for (unsigned int t = 0; t < numThreads; t++)
    {
        int startY = static_cast<int>(t) * rowsPerThread;
        int endY = std::min(startY + rowsPerThread, transHeight);
        if (startY < transHeight)
        {
            threads.emplace_back(processTransmittanceRow, startY, endY);
        }
    }

    // Wait for all threads to complete
    for (auto &thread : threads)
    {
        thread.join();
    }

    std::cout << "  Transmittance LUT computation complete: " << transHeight << "/" << transHeight << " rows" << "\n";

    // Generate multiscatter LUT (Hillaire method)
    std::cout << "Computing multiscatter LUT (" << transWidth << "x" << transHeight << ")..." << "\n";
    std::vector<float> multiLutData(transWidth * transHeight * 3); // RGB

    // Reset progress counter for multiscatter
    completedRows.store(0);

    auto processMultiscatterRow = [&](int startY, int endY) {
        for (int y = startY; y < endY; y++)
        {
            float mu_s = -1.0f + 2.0f * (static_cast<float>(y) / static_cast<float>(transHeight - 1));

            for (int x = 0; x < transWidth; x++)
            {
                float u = static_cast<float>(x) / static_cast<float>(transWidth - 1);
                float r = planetRadius + u * u * (atmosphereRadius - planetRadius);

                // Compute multiscattering using Hillaire iterative method
                glm::vec3 multiscatter = computeMultiscatterHillaire(r, mu_s, 3);

                int idx = (y * transWidth + x) * 3;
                multiLutData[idx + 0] = multiscatter.r;
                multiLutData[idx + 1] = multiscatter.g;
                multiLutData[idx + 2] = multiscatter.b;
            }

            // Thread-safe progress reporting
            int completed = ++completedRows;
            if (completed % 16 == 0)
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                std::cout << "  Multiscatter progress: " << completed << "/" << transHeight << " rows" << "\n";
            }
        }
    };

    // Launch threads for multiscatter
    threads.clear();
    for (unsigned int t = 0; t < numThreads; t++)
    {
        int startY = static_cast<int>(t) * rowsPerThread;
        int endY = std::min(startY + rowsPerThread, transHeight);
        if (startY < transHeight)
        {
            threads.emplace_back(processMultiscatterRow, startY, endY);
        }
    }

    // Wait for all threads to complete
    for (auto &thread : threads)
    {
        thread.join();
    }

    std::cout << "  Multiscatter LUT computation complete: " << transHeight << "/" << transHeight << " rows" << "\n";

    // Generate single-scatter LUT (3D: r, mu, mu_s)
    // Packed as 2D texture: width = R_samples * mu_samples, height = mu_s_samples
    const int singleRRes = 128;  // R samples
    const int singleMuRes = 64;  // mu (view zenith) samples
    const int singleMuSRes = 64; // mu_s (sun zenith) samples
    const int singleWidth = singleRRes * singleMuRes;
    const int singleHeight = singleMuSRes;

    std::cout << "Computing single-scatter LUT (3D packed as " << singleWidth << "x" << singleHeight << ")..." << "\n";
    std::cout << "  Resolution: R=" << singleRRes << ", mu=" << singleMuRes << ", mu_s=" << singleMuSRes << "\n";
    std::vector<float> singleLutData(singleWidth * singleHeight * 3); // RGB

    // Reset progress counter
    completedRows.store(0);
    std::atomic<int> completedSlices(0);

    auto processSingleScatterSlice = [&](int startMuS, int endMuS) {
        for (int muS_idx = startMuS; muS_idx < endMuS; muS_idx++)
        {
            // mu_s: cos(sun zenith angle) from -1 to 1
            float mu_s = -1.0f + 2.0f * (static_cast<float>(muS_idx) / static_cast<float>(singleMuSRes - 1));

            for (int mu_idx = 0; mu_idx < singleMuRes; mu_idx++)
            {
                // mu: cos(view zenith angle) from -1 to 1
                float mu = -1.0f + 2.0f * (static_cast<float>(mu_idx) / static_cast<float>(singleMuRes - 1));

                for (int r_idx = 0; r_idx < singleRRes; r_idx++)
                {
                    // R: distance from planet center
                    float u = static_cast<float>(r_idx) / static_cast<float>(singleRRes - 1);
                    float r = planetRadius + u * u * (atmosphereRadius - planetRadius);

                    // nu = cos(angle between view and sun)
                    // For single scatter LUT, we need to compute the scattering angle
                    // We'll use mu * mu_s as an approximation, or compute properly
                    // Actually, we need the full scattering angle nu
                    // For simplicity in the LUT, we can use mu * mu_s (which is correct when azimuth = 0)
                    // But for proper lookup, we'd need nu. Let's compute it properly:
                    // nu = dot(viewDir, sunDir) = mu * mu_s + sqrt(1-mu²) * sqrt(1-mu_s²) * cos(phi)
                    // For the LUT, we'll use phi=0 (same azimuth), so:
                    float nu = mu * mu_s + std::sqrt(std::max(0.0f, 1.0f - mu * mu)) *
                                               std::sqrt(std::max(0.0f, 1.0f - mu_s * mu_s));

                    // Compute single scatter
                    glm::vec3 singleScatter = computeSingleScatter(r, mu, mu_s, nu, 64);

                    // Pack into 2D texture: x = r_idx + mu_idx * singleRRes, y = muS_idx
                    int x = r_idx + mu_idx * singleRRes;
                    int y = muS_idx;
                    int idx = (y * singleWidth + x) * 3;
                    singleLutData[idx + 0] = singleScatter.r;
                    singleLutData[idx + 1] = singleScatter.g;
                    singleLutData[idx + 2] = singleScatter.b;
                }
            }

            // Thread-safe progress reporting
            int completed = ++completedSlices;
            if (completed % 8 == 0)
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                std::cout << "  Single-scatter progress: " << completed << "/" << singleMuSRes << " slices" << "\n";
            }
        }
    };

    // Launch threads for single scatter
    threads.clear();
    int slicesPerThread =
        static_cast<int>((singleMuSRes + static_cast<int>(numThreads) - 1) / static_cast<int>(numThreads));
    for (unsigned int t = 0; t < numThreads; t++)
    {
        int startMuS = static_cast<int>(t) * slicesPerThread;
        int endMuS = std::min(startMuS + slicesPerThread, singleMuSRes);
        if (startMuS < singleMuSRes)
        {
            threads.emplace_back(processSingleScatterSlice, startMuS, endMuS);
        }
    }

    // Wait for all threads to complete
    for (auto &thread : threads)
    {
        thread.join();
    }

    std::cout << "  Single-scatter LUT computation complete: " << singleMuSRes << "/" << singleMuSRes << " slices"
              << "\n";

    // Save transmittance LUT
    int result = stbi_write_hdr(outputPath.c_str(), transWidth, transHeight, 3, transLutData.data());
    if (result == 0)
    {
        std::cerr << "ERROR: Failed to save transmittance LUT to: " << outputPath << "\n";
        return false;
    }

    // Save multiscatter LUT
    result = stbi_write_hdr(multiscatterPath.c_str(), transWidth, transHeight, 3, multiLutData.data());
    if (result == 0)
    {
        std::cerr << "ERROR: Failed to save multiscatter LUT to: " << multiscatterPath << "\n";
        return false;
    }

    // Save single-scatter LUT
    result = stbi_write_hdr(singleScatterPath.c_str(), singleWidth, singleHeight, 3, singleLutData.data());
    if (result == 0)
    {
        std::cerr << "ERROR: Failed to save single-scatter LUT to: " << singleScatterPath << "\n";
        return false;
    }

    std::cout << "Atmosphere LUTs generated successfully" << "\n";
    std::cout << "  Transmittance LUT: " << transWidth << "x" << transHeight << "\n";
    std::cout << "  Single-scatter LUT: " << singleWidth << "x" << singleHeight << " (3D: R=" << singleRRes
              << ", mu=" << singleMuRes << ", mu_s=" << singleMuSRes << ")" << "\n";
    std::cout << "  Multiscatter LUT: " << transWidth << "x" << transHeight << "\n";
    std::cout << "  Format: HDR (RGB float)" << "\n";

    // Print some sample values
    int seaLevelIdx = (transHeight / 2 * transWidth + 0) * 3;
    int highAltIdx = ((transHeight - 1) * transWidth + (transWidth - 1)) * 3;
    std::cout << "  Transmittance samples:" << "\n";
    std::cout << "    Sea level, overhead sun: (" << transLutData[seaLevelIdx] << ", " << transLutData[seaLevelIdx + 1]
              << ", " << transLutData[seaLevelIdx + 2] << ")" << "\n";
    std::cout << "    High altitude, horizon sun: (" << transLutData[highAltIdx] << ", " << transLutData[highAltIdx + 1]
              << ", " << transLutData[highAltIdx + 2] << ")" << "\n";

    // Generate water scattering LUTs in the same luts folder
    // Resolution parameters: higher depthRes for better bathymetry fidelity
    // - depthRes=128: High depth resolution (important for complex bathymetry)
    // - muRes=64: High angular resolution for view angles
    // - muSunRes=32: Moderate sun angle resolution (single scatter only)
    // - nuRes=32: Relative angle resolution (scattering angle between view and sun)
    std::cout << "\n";
    bool waterLUTReady = EarthMaterial::generateWaterScatteringLUT(lutsPath, 128, 64, 32, 32);
    if (waterLUTReady)
    {
        std::cout << "All water scattering LUTs generated successfully" << "\n";
    }
    else
    {
        std::cerr << "WARNING: Failed to generate water scattering LUTs" << "\n";
    }

    return true;
}
