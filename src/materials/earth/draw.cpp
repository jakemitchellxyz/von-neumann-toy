// ============================================================================
// Drawing
// ============================================================================

#include "../../concerns/constants.h"
#include "../../concerns/font-rendering.h"
#include "../helpers/gl.h"
#include "earth-material.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace
{
// OpenGL 4x4 matrix size (4 rows × 4 columns = 16 elements)
constexpr size_t OPENGL_MATRIX_SIZE = 16;
} // namespace

void EarthMaterial::draw(const glm::vec3 &position,
                         float displayRadius,
                         const glm::vec3 &poleDirection,
                         const glm::vec3 &primeMeridianDirection,
                         double julianDate,
                         const glm::vec3 &cameraPos,
                         const glm::vec3 &sunDirection,
                         const glm::vec3 &moonDirection)
{
    if (!initialized_)
    {
        return;
    }

    // Calculate fractional month index for blending
    // J2000 is Jan 1.5, 2000.
    double daysSinceJ2000 = julianDate - JD_J2000;

    // Day of year (approximate, ignoring leap year variations for visual
    // blending)
    double yearFraction = std::fmod(daysSinceJ2000, DAYS_PER_TROPICAL_YEAR) / DAYS_PER_TROPICAL_YEAR;
    if (yearFraction < 0)
    {
        yearFraction += 1.0;
    }

    // Map year fraction (0.0-1.0) to month index (0.0-MONTHS_PER_YEAR.0)
    // We shift by -0.5 so that index X.0 corresponds to the MIDDLE of month
    // X+1 e.g., 0.0 = Mid-Jan, 1.0 = Mid-Feb. This ensures that at the middle
    // of the month, we are 100% on that texture.
    double monthPos = (yearFraction * static_cast<double>(MONTHS_PER_YEAR)) - 0.5;
    if (monthPos < 0)
    {
        monthPos += static_cast<double>(MONTHS_PER_YEAR);
    }

    int idx1 = static_cast<int>(std::floor(monthPos));
    int idx2 = (idx1 + 1) % MONTHS_PER_YEAR;
    float blendFactor = static_cast<float>(monthPos - idx1);

    // Handle wrap-around for idx1 (e.g. -0.5 floor is -1 -> 11)
    if (idx1 < 0)
    {
        idx1 = (idx1 % MONTHS_PER_YEAR + MONTHS_PER_YEAR) % MONTHS_PER_YEAR;
    }

    // MANDATORY: Required textures must be loaded - no fallbacks
    if (!textureLoaded_[idx1] || monthlyTextures_[idx1] == 0)
    {
        std::cerr << "ERROR: EarthMaterial::draw() - Color texture for month " << (idx1 + 1) << " is missing!"
                  << "\n";
        std::cerr << "  Month index: " << idx1 << " (0-based)" << "\n";
        std::exit(1);
    }

    if (!textureLoaded_[idx2] || monthlyTextures_[idx2] == 0)
    {
        std::cerr << "ERROR: EarthMaterial::draw() - Color texture for month " << (idx2 + 1) << " is missing!"
                  << "\n";
        std::cerr << "  Month index: " << idx2 << " (0-based)" << "\n";
        std::exit(1);
    }

    GLuint tex1 = monthlyTextures_[idx1];
    GLuint tex2 = monthlyTextures_[idx2];

    if (!shaderAvailable_)
    {
        std::cerr << "ERROR: EarthMaterial::draw() - Shader not available!" << "\n";
        std::cerr << "  Shader compilation or linking failed. Check console "
                     "for shader errors."
                  << "\n";
        std::exit(1);
    }

    if (!elevationLoaded_)
    {
        std::cerr << "ERROR: EarthMaterial::draw() - Elevation data not loaded!" << "\n";
        std::cerr << "  Normal map is required for shader-based rendering." << "\n";
        std::exit(1);
    }

    if (normalMapTexture_ == 0)
    {
        std::cerr << "ERROR: EarthMaterial::draw() - Normal map texture is missing!" << "\n";
        std::cerr << "  Normal map texture ID is 0. Check texture loading." << "\n";
        std::exit(1);
    }

    // Use shader-based rendering (MANDATORY - no fallback)
    {
        // Get current matrices from OpenGL fixed-function state
        std::array<GLfloat, OPENGL_MATRIX_SIZE> modelviewMatrix{};
        std::array<GLfloat, OPENGL_MATRIX_SIZE> projectionMatrix{};
        glGetFloatv(GL_MODELVIEW_MATRIX, modelviewMatrix.data());
        glGetFloatv(GL_PROJECTION_MATRIX, projectionMatrix.data());

        // Use the sunDirection passed as parameter - this is the direction FROM
        // Earth TO Sun computed correctly in celestial-body.cpp as
        // normalize(sunPos - earthPos)
        glm::vec3 lightDir = sunDirection;

        // Use shader
        glUseProgram(shaderProgram_);

        // Set matrices - use identity for model since we'll transform vertices
        // directly
        glm::mat4 identity = glm::mat4(1.0f);
        glUniformMatrix4fv(uniformModelMatrix_, 1, GL_FALSE, glm::value_ptr(identity));
        glUniformMatrix4fv(uniformViewMatrix_, 1, GL_FALSE, modelviewMatrix.data());
        glUniformMatrix4fv(uniformProjectionMatrix_, 1, GL_FALSE, projectionMatrix.data());

        // Set textures
        // Unit 0: Color Texture 1
        glActiveTexture_ptr(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex1);
        glUniform1i(uniformColorTexture_, 0);

        // Unit 1: Color Texture 2
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex2);
        glUniform1i(uniformColorTexture2_, 1);

        // Blend Factor
        glUniform1f(uniformBlendFactor_, blendFactor);

// Unit 2: Normal Map (only bind if enabled and loaded)
#ifndef GL_TEXTURE2
#define GL_TEXTURE2 0x84C2
#endif

        glActiveTexture_ptr(GL_TEXTURE2);
        if (useNormalMap_ && elevationLoaded_ && normalMapTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, normalMapTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (uniformNormalMap_ >= 0)
        {
            glUniform1i(uniformNormalMap_, 2);
        }

// Unit 12: Heightmap (landmass elevation) (only bind if enabled and loaded)
#ifndef GL_TEXTURE12
#define GL_TEXTURE12 0x84CC
#endif
        glActiveTexture_ptr(GL_TEXTURE12);
        if (useHeightmap_ && elevationLoaded_ && heightmapTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, heightmapTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (uniformHeightmap_ >= 0)
        {
            glUniform1i(uniformHeightmap_, 12);
        }

// Unit 3: Nightlights (city lights)
#ifndef GL_TEXTURE3
#define GL_TEXTURE3 0x84C3
#endif

        glActiveTexture_ptr(GL_TEXTURE3);
        if (nightlightsLoaded_ && nightlightsTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, nightlightsTexture_);
        }
        else
        {
            // Bind a black texture (no lights) if nightlights not available
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformNightlights_, 3);

// Unit 4: Micro noise texture (fine-grained flicker)
#ifndef GL_TEXTURE4
#define GL_TEXTURE4 0x84C4
#endif
        glActiveTexture_ptr(GL_TEXTURE4);
        if (noiseTexturesGenerated_ && microNoiseTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, microNoiseTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformMicroNoise_, 4);

// Unit 5: Hourly noise texture (regional variation)
#ifndef GL_TEXTURE5
#define GL_TEXTURE5 0x84C5
#endif
        glActiveTexture_ptr(GL_TEXTURE5);
        if (noiseTexturesGenerated_ && hourlyNoiseTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, hourlyNoiseTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformHourlyNoise_, 5);

// Unit 6: Specular/Roughness texture (surface reflectivity) (only bind if enabled and
// loaded)
#ifndef GL_TEXTURE6
#define GL_TEXTURE6 0x84C6
#endif
        glActiveTexture_ptr(GL_TEXTURE6);
        if (useSpecular_ && specularLoaded_ && specularTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, specularTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (uniformSpecular_ >= 0)
        {
            glUniform1i(uniformSpecular_, 6);
        }

        // Calculate ice mask month indices based on Julian date (same as color
        // blending)
        float monthPos =
            static_cast<float>(std::fmod((julianDate - JD_J2000) / 30.4375, static_cast<double>(MONTHS_PER_YEAR)));
        if (monthPos < 0)
        {
            monthPos += static_cast<float>(MONTHS_PER_YEAR);
        }
        int iceIdx1 = static_cast<int>(monthPos);
        int iceIdx2 = (iceIdx1 + 1) % MONTHS_PER_YEAR;
        float iceBlendFactor = monthPos - static_cast<float>(iceIdx1);

// Unit 7: Ice mask texture (current month)
#ifndef GL_TEXTURE7
#define GL_TEXTURE7 0x84C7
#endif
        glActiveTexture_ptr(GL_TEXTURE7);
        if (iceMasksLoaded_[iceIdx1] && iceMaskTextures_[iceIdx1] != 0)
        {
            glBindTexture(GL_TEXTURE_2D, iceMaskTextures_[iceIdx1]);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformIceMask_, 7);

// Unit 8: Ice mask texture (next month for blending)
#ifndef GL_TEXTURE8
#define GL_TEXTURE8 0x84C8
#endif
        glActiveTexture_ptr(GL_TEXTURE8);
        if (iceMasksLoaded_[iceIdx2] && iceMaskTextures_[iceIdx2] != 0)
        {
            glBindTexture(GL_TEXTURE_2D, iceMaskTextures_[iceIdx2]);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformIceMask2_, 8);

        // Ice blend factor (same as color blend factor for consistent monthly
        // transition)
        glUniform1f(uniformIceBlendFactor_, iceBlendFactor);

// Unit 9: Landmass mask texture (for ocean detection)
#ifndef GL_TEXTURE9
#define GL_TEXTURE9 0x84C9
#endif
        glActiveTexture_ptr(GL_TEXTURE9);
        if (landmassMaskLoaded_ && landmassMaskTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, landmassMaskTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformLandmassMask_, 9);

// Unit 10: Bathymetry depth texture (ocean floor depth)
#ifndef GL_TEXTURE10
#define GL_TEXTURE10 0x84CA
#endif
        glActiveTexture_ptr(GL_TEXTURE10);
        if (bathymetryLoaded_ && bathymetryDepthTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, bathymetryDepthTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformBathymetryDepth_, 10);

// Unit 11: Bathymetry normal texture (ocean floor terrain)
#ifndef GL_TEXTURE11
#define GL_TEXTURE11 0x84CB
#endif
        glActiveTexture_ptr(GL_TEXTURE11);
        if (bathymetryLoaded_ && bathymetryNormalTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, bathymetryNormalTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformBathymetryNormal_, 11);

// Unit 12: Combined normal map (landmass + bathymetry) for shadows
#ifndef GL_TEXTURE12
#define GL_TEXTURE12 0x84CC
#endif
        glActiveTexture_ptr(GL_TEXTURE12);
        if (combinedNormalLoaded_ && combinedNormalTexture_ != 0)
        {
            glBindTexture(GL_TEXTURE_2D, combinedNormalTexture_);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glUniform1i(uniformCombinedNormal_, 12);

// Unit 13-15: Water scattering LUTs (T_water, S1_water, Sm_water)
#ifndef GL_TEXTURE13
#define GL_TEXTURE13 0x84CD
#endif
#ifndef GL_TEXTURE14
#define GL_TEXTURE14 0x84CE
#endif
#ifndef GL_TEXTURE15
#define GL_TEXTURE15 0x84CF
#endif
        bool waterLUTsAvailable = waterTransmittanceLUTLoaded_ && waterTransmittanceLUT_ != 0 &&
                                  waterSingleScatterLUTLoaded_ && waterSingleScatterLUT_ != 0 &&
                                  waterMultiscatterLUTLoaded_ && waterMultiscatterLUT_ != 0;

        // Unit 13: Water transmittance LUT (T_water)
        glActiveTexture_ptr(GL_TEXTURE13);
        if (waterLUTsAvailable)
        {
            glBindTexture(GL_TEXTURE_2D, waterTransmittanceLUT_);
            if (uniformWaterTransmittanceLUT_ >= 0)
            {
                glUniform1i(uniformWaterTransmittanceLUT_, 13);
            }
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // Unit 14: Water single scatter LUT (S1_water)
        glActiveTexture_ptr(GL_TEXTURE14);
        if (waterLUTsAvailable)
        {
            glBindTexture(GL_TEXTURE_2D, waterSingleScatterLUT_);
            if (uniformWaterSingleScatterLUT_ >= 0)
            {
                glUniform1i(uniformWaterSingleScatterLUT_, 14);
            }
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // Unit 15: Water multiscatter LUT (Sm_water)
        glActiveTexture_ptr(GL_TEXTURE15);
        if (waterLUTsAvailable)
        {
            glBindTexture(GL_TEXTURE_2D, waterMultiscatterLUT_);
            if (uniformWaterMultiscatterLUT_ >= 0)
            {
                glUniform1i(uniformWaterMultiscatterLUT_, 15);
            }
            glUniform1i(uniformUseWaterScatteringLUT_, 1); // Enable LUT usage
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, 0);
            if (uniformUseWaterScatteringLUT_ >= 0)
            {
                glUniform1i(uniformUseWaterScatteringLUT_, 0); // Disable LUT, use ray-marching
            }
        }

        // Set lighting uniforms
        glUniform3f(uniformLightDir_, lightDir.x, lightDir.y, lightDir.z);
        glUniform3f(uniformLightColor_, 1.0f, 1.0f, 1.0f); // White sunlight
        glUniform3f(uniformAmbientColor_, 0.0f, 0.0f,
                    0.0f); // No ambient - Sun is exclusive light source

        // =========================================================================
        // Moonlight calculation
        // =========================================================================
        // Moonlight is reflected sunlight. Physical properties:
        // - Moon's albedo: ~0.12 (reflects 12% of incident sunlight)
        // - Full moon illuminance: ~0.1-0.25 lux (vs sun's ~100,000 lux)
        // - For visual purposes, we use a visible but realistic ratio
        //
        // Moon phase affects intensity:
        // - Full moon: sun and moon on opposite sides of Earth
        // - New moon: sun and moon on same side (moon not visible at night)
        // We approximate phase by dot(sunDir, moonDir):
        //   -1 = full moon (opposite), +1 = new moon (same side)

        glm::vec3 moonDir = glm::normalize(moonDirection);
        float sunMoonDot = glm::dot(lightDir, moonDir);

        // Moon phase factor: 0 at new moon, 1 at full moon
        // This is simplified - real phase depends on viewing angle
        float moonPhase = 0.5f - (0.5f * sunMoonDot);

        // Moonlight intensity: base intensity * phase
        // Using ~0.03 as base (visible but not overwhelming sun)
        // Full moon: 0.03, half moon: 0.015, new moon: ~0
        float moonIntensity = 0.03f * moonPhase;

        // Moonlight color: slightly warm/gray (reflected from gray lunar
        // surface) Slight blue-shift from Earth's atmosphere at night
        glm::vec3 moonColor = glm::vec3(0.8f, 0.85f, 1.0f) * moonIntensity;

        glUniform3f(uniformMoonDir_, moonDir.x, moonDir.y, moonDir.z);
        glUniform3f(uniformMoonColor_, moonColor.r, moonColor.g, moonColor.b);

        // Camera position for view direction calculations
        glUniform3f(uniformCameraPos_, cameraPos.x, cameraPos.y, cameraPos.z);

        // Pass pole direction for tangent frame calculation
        glm::vec3 poleNorm = glm::normalize(poleDirection);
        glUniform3f(uniformPoleDir_, poleNorm.x, poleNorm.y, poleNorm.z);

        // Pass time (Julian date fraction) for animated noise
        // Use fractional part so noise cycles smoothly
        float timeFrac = static_cast<float>(std::fmod(julianDate, 1.0));
        glUniform1f(uniformTime_, timeFrac);

        // Pass planet radius for WGS 84 oblateness calculation
        if (uniformPlanetRadius_ >= 0)
        {
            glUniform1f(uniformPlanetRadius_, displayRadius);
        }

        // Set toggle uniforms
        if (uniformUseNormalMap_ >= 0)
        {
            glUniform1i(uniformUseNormalMap_, useNormalMap_ ? 1 : 0);
        }
        if (uniformUseHeightmap_ >= 0)
        {
            glUniform1i(uniformUseHeightmap_, useHeightmap_ ? 1 : 0);
        }
        if (uniformUseSpecular_ >= 0)
        {
            glUniform1i(uniformUseSpecular_, useSpecular_ ? 1 : 0);
        }

        // Draw sphere with shader (moderate tessellation is fine with per-pixel
        // normals)
        drawTexturedSphere(position,
                           displayRadius,
                           poleDirection,
                           primeMeridianDirection,
                           SPHERE_BASE_SLICES,
                           SPHERE_BASE_STACKS);

        // Restore state - unbind all texture units we used
        glUseProgram(0);
        glActiveTexture_ptr(GL_TEXTURE11);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE10);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE9);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE8);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Draw atmosphere (fullscreen ray march pass)
    if (atmosphereAvailable_ && enableAtmosphere_)
    {
        // For fullscreen ray marching, the atmosphere bounds extend to the
        // exosphere
        // Extended to exosphere height (~10,000km) for proper light refraction at high altitudes.
        // The shader will handle the density falloff using the USSA76 model and LUT,
        // so we want to include all potentially visible atmosphere for light refraction.
        // 99% of atmospheric mass is below 30km, 99.9% below 50km, but the exosphere
        // extends much higher and affects light refraction even at very low densities.
        constexpr float EXOSPHERE_HEIGHT_KM = 10000.0f; // 10,000km exosphere height
        float atmosphereRadius =
            displayRadius * static_cast<float>((RADIUS_EARTH_KM + EXOSPHERE_HEIGHT_KM) / RADIUS_EARTH_KM);
        drawAtmosphere(position, displayRadius, atmosphereRadius, cameraPos, sunDirection);
    }

    // Draw visual debugging for atmospheric layers
    if (showAtmosphereLayers_)
    {
        drawAtmosphereDebug(position, displayRadius, cameraPos);
    }
}

// ============================================================================
// Textured Sphere Rendering
// ============================================================================
// Draws a textured sphere using immediate mode OpenGL.
// When shaders are active, the fragment shader handles per-pixel normal
// mapping. The geometry just needs to provide position, normal (for TBN), and
// UV coords.

void EarthMaterial::drawTexturedSphere(const glm::vec3 &position,
                                       float radius,
                                       const glm::vec3 &poleDir,
                                       const glm::vec3 &primeDir,
                                       int slices,
                                       int stacks)
{
    glm::vec3 north = glm::normalize(poleDir);

    glm::vec3 east = primeDir - glm::dot(primeDir, north) * north;
    if (glm::length(east) < 0.001f)
    {
        if (std::abs(north.y) < 0.9f)
        {
            east = glm::normalize(glm::cross(north, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        else
        {
            east = glm::normalize(glm::cross(north, glm::vec3(1.0f, 0.0f, 0.0f)));
        }
    }
    else
    {
        east = glm::normalize(east);
    }

    glm::vec3 south90 = glm::normalize(glm::cross(north, east));

    glPushMatrix();
    glTranslatef(position.x, position.y, position.z);

    for (int i = 0; i < stacks; i++)
    {
        float phi1 = static_cast<float>(PI) * (static_cast<float>(i) / static_cast<float>(stacks) - 0.5f);
        float phi2 = static_cast<float>(PI) * (static_cast<float>(i + 1) / static_cast<float>(stacks) - 0.5f);

        float vTexCoord1 = static_cast<float>(i) / static_cast<float>(stacks);
        float vTexCoord2 = static_cast<float>(i + 1) / static_cast<float>(stacks);

        float cosPhi1 = cos(phi1);
        float sinPhi1 = sin(phi1);
        float cosPhi2 = cos(phi2);
        float sinPhi2 = sin(phi2);

        glBegin(GL_QUAD_STRIP);

        for (int j = 0; j <= slices; j++)
        {
            float theta = 2.0f * static_cast<float>(PI) * static_cast<float>(j) / static_cast<float>(slices);
            float uCoord = static_cast<float>(j) / static_cast<float>(slices);
            float thetaShifted = theta - static_cast<float>(PI);

            float cosTheta = cos(thetaShifted);
            float sinTheta = sin(thetaShifted);

            // First vertex
            glm::vec3 localDir1 = cosPhi1 * (cosTheta * east + sinTheta * south90) + sinPhi1 * north;
            glm::vec3 worldPos1 = radius * localDir1;

            // Normal is just the outward direction (shader uses this for TBN)
            glTexCoord2f(uCoord, vTexCoord1);
            glNormal3f(localDir1.x, localDir1.y, localDir1.z);
            glVertex3f(worldPos1.x, worldPos1.y, worldPos1.z);

            // Second vertex
            glm::vec3 localDir2 = cosPhi2 * (cosTheta * east + sinTheta * south90) + sinPhi2 * north;
            glm::vec3 worldPos2 = radius * localDir2;

            glTexCoord2f(uCoord, vTexCoord2);
            glNormal3f(localDir2.x, localDir2.y, localDir2.z);
            glVertex3f(worldPos2.x, worldPos2.y, worldPos2.z);
        }

        glEnd();
    }

    glPopMatrix();
}

// ============================================================================
// Atmospheric Scattering Rendering - Fullscreen Ray March
// ============================================================================
// Draws the atmosphere as a fullscreen quad pass with ray marching.
// This is the CORRECT approach: the atmosphere is an analytic volume,
// not geometry. Each pixel constructs a view ray and marches through
// the atmosphere volume, accumulating Rayleigh/Mie scattering.
// The planet surface is just an occluder - rays terminate at the surface.

void EarthMaterial::drawAtmosphere(const glm::vec3 &planetPos,
                                   float planetRadius,
                                   float atmosphereRadius,
                                   const glm::vec3 &cameraPos,
                                   const glm::vec3 &sunDir) const
{
    if (!atmosphereAvailable_ || atmosphereProgram_ == 0)
    {
        // Debug: Log why we're not rendering
        static bool loggedOnce = false;
        if (!loggedOnce)
        {
            std::cerr << "WARNING: Atmosphere NOT rendering:" << "\n";
            std::cerr << "  atmosphereAvailable_ = " << (atmosphereAvailable_ ? "true" : "false") << "\n";
            std::cerr << "  atmosphereProgram_ = " << atmosphereProgram_ << "\n";
            std::cerr << "  enableAtmosphere_ = " << (enableAtmosphere_ ? "true" : "false") << "\n";
            if (!atmosphereAvailable_)
            {
                std::cerr << "  -> Atmosphere shader initialization failed or was not called" << "\n";
            }
            if (atmosphereProgram_ == 0)
            {
                std::cerr << "  -> Atmosphere shader program is invalid (0)" << "\n";
            }
            loggedOnce = true;
        }
        return;
    }

    // Debug: Log that we ARE rendering (once)
    static bool loggedRender = false;
    if (!loggedRender)
    {
        std::cout << "Atmosphere rendering (fullscreen ray march):" << "\n";
        std::cout << "  planetPos = (" << planetPos.x << ", " << planetPos.y << ", " << planetPos.z << ")" << "\n";
        std::cout << "  cameraPos = (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")" << "\n";
        std::cout << "  sunDir = (" << sunDir.x << ", " << sunDir.y << ", " << sunDir.z << ")" << "\n";
        std::cout << "  planetRadius = " << planetRadius << "\n";
        std::cout << "  atmosphereRadius = " << atmosphereRadius << " (ratio=" << (atmosphereRadius / planetRadius)
                  << "x)" << "\n";
        std::cout << "  camera distance from planet = " << glm::length(cameraPos - planetPos) << "\n";
        loggedRender = true;
    }

    // Get current OpenGL matrices for ray reconstruction
    std::array<GLfloat, OPENGL_MATRIX_SIZE> modelviewMatrix{};
    std::array<GLfloat, OPENGL_MATRIX_SIZE> projectionMatrix{};
    glGetFloatv(GL_MODELVIEW_MATRIX, modelviewMatrix.data());
    glGetFloatv(GL_PROJECTION_MATRIX, projectionMatrix.data());

    glm::mat4 viewMatrix = glm::make_mat4(modelviewMatrix.data());
    glm::mat4 projMatrix = glm::make_mat4(projectionMatrix.data());

    // Compute inverse view-projection matrix for ray reconstruction
    glm::mat4 viewProj = projMatrix * viewMatrix;
    glm::mat4 invViewProj = glm::inverse(viewProj);

    // Save current state
    GLboolean depthMask = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLint blendSrc = 0;
    GLint blendDst = 0;
    glGetIntegerv(GL_BLEND_SRC, &blendSrc);
    glGetIntegerv(GL_BLEND_DST, &blendDst);
    GLboolean lighting = glIsEnabled(GL_LIGHTING);
    GLboolean texture2d = glIsEnabled(GL_TEXTURE_2D);
    GLboolean cullFace = glIsEnabled(GL_CULL_FACE);

    // Save matrix modes
    GLint matrixMode = 0;
    glGetIntegerv(GL_MATRIX_MODE, &matrixMode);

    // Set up state for fullscreen quad rendering
    // Don't use depth test - we handle occlusion analytically in the shader
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE); // Don't write to depth buffer
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,
                GL_ONE_MINUS_SRC_ALPHA); // Standard alpha blending
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE); // Draw both sides of the quad

    // Keep perspective projection - don't switch to orthographic
    // The fullscreen quad will be rendered in perspective space
    // The shader reconstructs rays from the perspective projection matrix

    // Use atmosphere shader
    glUseProgram(atmosphereProgram_);

    // Set uniforms for ray marching
    glUniformMatrix4fv(uniformAtmoInvViewProj_, 1, GL_FALSE, glm::value_ptr(invViewProj));
    // uCameraPos is optional - only set if uniform exists (may be optimized away if unused)
    if (uniformAtmoCameraPos_ >= 0)
    {
        glUniform3f(uniformAtmoCameraPos_, cameraPos.x, cameraPos.y, cameraPos.z);
    }
    glUniform3f(uniformAtmoSunDir_, sunDir.x, sunDir.y, sunDir.z);
    glUniform3f(uniformAtmoPlanetPos_, planetPos.x, planetPos.y, planetPos.z);
    glUniform1f(uniformAtmoPlanetRadius_, planetRadius);
    glUniform1f(uniformAtmoAtmosphereRadius_, atmosphereRadius);

    // Bind density lookup texture if available
    if (atmosphereDataLoaded_ && atmosphereDensityTexture_ != 0)
    {
        glActiveTexture_ptr(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_1D, atmosphereDensityTexture_);
        glUniform1i(uniformAtmoDensityTex_, 0);
        glUniform1f(uniformAtmoMaxAltitude_, atmosphereMaxAltitude_);
    }
    else
    {
        // Signal to shader to use analytical fallback (uMaxAltitude = 0)
        glUniform1f(uniformAtmoMaxAltitude_, 0.0f);
    }

    // Bind transmittance LUT if available and enabled
    // Check both that LUT is loaded AND that the user wants to use it (g_useAtmosphereLUT)
    bool shouldUseLUT = g_useAtmosphereLUT && atmosphereTransmittanceLUTLoaded_ && atmosphereTransmittanceLUT_ != 0 &&
                        uniformAtmoTransmittanceLUT_ >= 0;

    if (shouldUseLUT)
    {
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, atmosphereTransmittanceLUT_);
        glUniform1i(uniformAtmoTransmittanceLUT_, 1);
        glUniform1i(uniformAtmoUseTransmittanceLUT_, 1); // Enable LUT usage
    }
    else
    {
        if (uniformAtmoUseTransmittanceLUT_ >= 0)
        {
            glUniform1i(uniformAtmoUseTransmittanceLUT_, 0); // Disable LUT, use ray marching
        }
    }

    // Bind multiscatter LUT if available and enabled
    bool shouldUseMultiscatterLUT = g_useMultiscatterLUT && atmosphereMultiscatterLUTLoaded_ &&
                                    atmosphereMultiscatterLUT_ != 0 && uniformAtmoMultiscatterLUT_ >= 0;

    if (shouldUseMultiscatterLUT)
    {
        glActiveTexture_ptr(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, atmosphereMultiscatterLUT_);
        glUniform1i(uniformAtmoMultiscatterLUT_, 2);
        glUniform1i(uniformAtmoUseMultiscatterLUT_, 1); // Enable multiscatter LUT usage
    }
    else
    {
        if (uniformAtmoUseMultiscatterLUT_ >= 0)
        {
            glUniform1i(uniformAtmoUseMultiscatterLUT_, 0); // Disable multiscatter LUT, use fallback
        }
    }

    // Draw fullscreen quad in NDC coordinates [-1, 1]
    // The vertex shader passes these through directly to clip space
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f(1.0f, -1.0f);
    glVertex2f(1.0f, 1.0f);
    glVertex2f(-1.0f, 1.0f);
    glEnd();

    // Restore shader state
    glUseProgram(0);

    // Restore render state
    glDepthMask(depthMask);
    if (depthTest)
    {
        glEnable(GL_DEPTH_TEST);
    }
    if (!blendEnabled)
    {
        glDisable(GL_BLEND);
    }
    glBlendFunc(blendSrc, blendDst);
    if (lighting)
    {
        glEnable(GL_LIGHTING);
    }
    if (texture2d)
    {
        glEnable(GL_TEXTURE_2D);
    }
    if (cullFace)
    {
        glEnable(GL_CULL_FACE);
    }
}

// ============================================================================
// Atmosphere Debugging
// ============================================================================
// Uses centralized font rendering from font-rendering.h

void EarthMaterial::drawDebugRing(const glm::vec3 &center, float radius, const glm::vec3 &color)
{
    glColor3f(color.x, color.y, color.z);

    // Draw 3 rings to visualize the sphere
    glBegin(GL_LINE_LOOP);
    const int segments = 64;
    // XZ Plane (Equator)
    for (int i = 0; i < segments; i++)
    {
        float theta = 2.0f * 3.14159f * float(i) / float(segments);
        glVertex3f(center.x + (radius * cos(theta)), center.y, center.z + (radius * sin(theta)));
    }
    glEnd();

    glBegin(GL_LINE_LOOP);
    // XY Plane
    for (int i = 0; i < segments; i++)
    {
        float theta = 2.0f * 3.14159f * float(i) / float(segments);
        glVertex3f(center.x + (radius * cos(theta)), center.y + (radius * sin(theta)), center.z);
    }
    glEnd();
}

void EarthMaterial::drawBillboardText(const glm::vec3 &pos,
                                      const std::string &text,
                                      const glm::vec3 &cameraPos,
                                      float targetPixelSize)
{
    // Use centralized 3D text rendering from font-rendering module
    DrawBillboardText3D(pos, text, cameraPos, targetPixelSize);
}

void EarthMaterial::drawAtmosphereDebug(const glm::vec3 &planetPos, float planetRadius, const glm::vec3 &cameraPos)
{
    // US Standard Atmosphere 1976 Layers + Kármán Line
    // Heights are in kilometers above Earth's surface
    struct AtmoLayerInfo
    {
        float h; // Altitude in km
        const char *name;
        float r, g, b; // Color
        bool isKarman; // Special handling for Kármán line
    };

    const AtmoLayerInfo layers[] = {
        {11.0f, "Tropopause", 0.6f, 0.85f, 0.2f, false},                             // Yellow-green
        {20.0f, "Stratosphere 1", 0.7f, 0.7f, 0.2f, false},                          // Olive
        {32.0f, "Stratosphere 2", 0.8f, 0.6f, 0.2f, false},                          // Orange-ish
        {47.0f, "Stratopause", 0.85f, 0.5f, 0.2f, false},                            // Orange
        {51.0f, "Mesosphere", 0.9f, 0.4f, 0.2f, false},                              // Red-orange
        {71.0f, "Mesopause", 0.95f, 0.3f, 0.2f, false},                              // Red
        {static_cast<float>(KARMAN_LINE_KM), "Karman Line", 0.0f, 0.8f, 1.0f, true}, // Cyan (boundary of space!)
        {400.0f, "ISS Orbit", 0.4f, 0.4f, 0.9f, false},                              // Blue (for reference)
        {600.0f, "Thermopause", 0.9f, 0.2f, 0.5f, false},                            // Magenta (base of exosphere)
        {10000.0f, "Exosphere Edge", 0.3f, 0.9f, 0.3f, false},                       // Green (157% of Earth radius!)
    };
    const int numLayers = sizeof(layers) / sizeof(layers[0]);

    // Debug: Log scale calculations once
    static bool loggedScales = false;
    if (!loggedScales)
    {
        std::cout << "\n=== Atmosphere Layer Scale Verification ===" << "\n";
        std::cout << "Earth radius: " << RADIUS_EARTH_KM << " km" << "\n";
        std::cout << "Planet display radius: " << planetRadius << " display units" << "\n";
        std::cout << "Scale: 1 display unit = " << (static_cast<float>(RADIUS_EARTH_KM) / planetRadius) << " km"
                  << "\n";
        std::cout << "\n";
        for (int i = 0; i < numLayers; i++)
        {
            float h = layers[i].h;
            float scale = static_cast<float>((RADIUS_EARTH_KM + static_cast<double>(h)) / RADIUS_EARTH_KM);
            float ringRadius = planetRadius * scale;
            float heightInDisplayUnits = ringRadius - planetRadius;
            std::cout << "  " << layers[i].name << ": " << h << " km" << "\n";
            std::cout << "    Scale factor: " << scale << "\n";
            std::cout << "    Ring radius: " << ringRadius << " display units" << "\n";
            std::cout << "    Height above surface: " << heightInDisplayUnits << " display units" << "\n";
            std::cout << "    Percentage of Earth radius: " << (h / static_cast<float>(RADIUS_EARTH_KM) * 100.0f) << "%"
                      << "\n";
        }
        std::cout << "===========================================" << "\n";
        std::cout << "NOTE: Atmosphere IS thin! Karman line (100km) is only "
                     "1.57% of Earth's radius."
                  << "\n";
        std::cout << "===========================================" << "\n" << "\n";
        loggedScales = true;
    }

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    // Keep depth testing enabled so Earth sphere occludes backside of rings

    for (int i = 0; i < numLayers; i++)
    {
        float h = layers[i].h;
        // Calculate radius in display units
        // This correctly maps real-world km to display units:
        // ringRadius = planetRadius * (RADIUS_EARTH_KM + altitude_km) / RADIUS_EARTH_KM
        float scale = static_cast<float>((RADIUS_EARTH_KM + static_cast<double>(h)) / RADIUS_EARTH_KM);
        float r = planetRadius * scale;

        // Set color and line width
        glColor3f(layers[i].r, layers[i].g, layers[i].b);

        // Kármán line gets special treatment - thicker and more visible
        if (layers[i].isKarman)
        {
            glLineWidth(2.5f);
        }
        else
        {
            glLineWidth(1.0f);
        }

        drawDebugRing(planetPos, r, glm::vec3(layers[i].r, layers[i].g, layers[i].b));

        // Position label at the intersection of the two perpendicular rings
        // The rings are in XZ plane (equator) and XY plane - they intersect on
        // the X-axis Pick the intersection point closer to the camera
        glm::vec3 posX = planetPos + glm::vec3(r, 0.0f, 0.0f);
        glm::vec3 negX = planetPos + glm::vec3(-r, 0.0f, 0.0f);
        float distPosX = glm::length(cameraPos - posX);
        float distNegX = glm::length(cameraPos - negX);
        glm::vec3 labelPos = (distPosX < distNegX) ? posX : negX;

        char buf[48];
        if (layers[i].isKarman)
        {
            snprintf(buf, sizeof(buf), ">>> %s %.0fkm <<<", layers[i].name, h);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%s %.0fkm", layers[i].name, h);
        }

        glColor3f(layers[i].r, layers[i].g, layers[i].b);
        drawBillboardText(labelPos, std::string(buf), cameraPos);
    }

    glLineWidth(1.0f); // Reset
}
