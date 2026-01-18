#include "../earth-material.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

// ============================================================================
// Preprocess Ice Masks from Blue Marble Monthly Textures
// ============================================================================
// Creates 12 ice masks (one per month) based on white/ice colors in the
// Blue Marble monthly images. Ice appears white/bright in satellite imagery.
// Masks are output in cubemap format (same as color textures).
// White = ice/snow, Black = everything else

namespace
{
// Image processing constants
constexpr unsigned char MAX_PIXEL_VALUE = 255;  // Maximum value for 8-bit grayscale pixel
constexpr float MAX_PIXEL_VALUE_FLOAT = 255.0F; // Maximum pixel value as float for normalization

// Ice detection thresholds
constexpr float BRIGHTNESS_THRESHOLD_FRESH_SNOW = 0.92F;   // Very bright threshold for fresh snow detection
constexpr float BRIGHTNESS_THRESHOLD_BRIGHT_WHITE = 0.85F; // Brightness threshold for bright white/near-white detection
constexpr float BRIGHTNESS_THRESHOLD_ICE_SNOW = 0.75F;     // Brightness threshold for ice/snow detection
constexpr float BRIGHTNESS_THRESHOLD_GLACIAL_ICE = 0.7F;   // Brightness threshold for glacial ice detection
constexpr float SATURATION_THRESHOLD_LOW = 0.15F;     // Low saturation threshold for bright white/near-white detection
constexpr float SATURATION_THRESHOLD_MEDIUM = 0.2F;   // Medium saturation threshold for glacial ice detection
constexpr float SATURATION_THRESHOLD_VERY_LOW = 0.1F; // Very low saturation threshold for ice/snow detection
constexpr float BLUE_CHANNEL_RATIO_THRESHOLD = 0.95F; // Blue channel ratio threshold for glacial ice detection
} // namespace

bool EarthMaterial::preprocessIceMasks(const std::string &defaultsPath,
                                       const std::string &outputBasePath,
                                       TextureResolution resolution)
{
    std::string outputPath = outputBasePath + "/" + getResolutionFolderName(resolution);

    std::cout << "=== Ice Mask Generation ===" << '\n';

    // Check if output directory exists
    if (!std::filesystem::exists(outputPath))
    {
        std::cout << "Output directory not found (Blue Marble not processed yet?): " << outputPath << '\n';
        std::cout << "===========================" << '\n';
        return false;
    }

    // Get resolution dimensions
    int outWidth = 0;
    int outHeight = 0;
    getResolutionDimensions(resolution, outWidth, outHeight);
    bool lossless = (resolution == TextureResolution::Ultra);
    const char *ext = lossless ? ".png" : ".jpg";

    int masksGenerated = 0;

    for (int month = 1; month <= MONTHS_PER_YEAR; month++)
    {
        // Check if ice mask already exists
        std::ostringstream maskFilenameStream;
        maskFilenameStream << "earth_ice_mask_" << std::setfill('0') << std::setw(2) << month << ".png";
        std::string maskFilename = maskFilenameStream.str();
        std::string maskPath = outputPath;
        maskPath += "/";
        maskPath += maskFilename;

        if (std::filesystem::exists(maskPath))
        {
            std::cout << "  Month " << month << ": ice mask exists (skipping)" << '\n';
            masksGenerated++;
            continue;
        }

        // Load the Blue Marble monthly texture (sinusoidal projection)
        std::ostringstream colorFilenameStream;
        colorFilenameStream << "earth_month_" << std::setfill('0') << std::setw(2) << month << ext;
        std::string colorFilename = colorFilenameStream.str();
        std::string colorPath = outputPath;
        colorPath += "/";
        colorPath += colorFilename;

        if (!std::filesystem::exists(colorPath))
        {
            std::cout << "  Month " << month << ": color texture not found (skipping)" << '\n';
            continue;
        }

        int colorWidth = 0;
        int colorHeight = 0;
        int colorChannels = 0;
        unsigned char *colorData = stbi_load(colorPath.c_str(), &colorWidth, &colorHeight, &colorChannels, 0);

        if (colorData == nullptr || colorChannels < 3)
        {
            std::cout << "  Month " << month << ": failed to load color texture" << '\n';
            if (colorData != nullptr)
            {
                stbi_image_free(colorData);
            }
            continue;
        }

        std::cout << "  Month " << month << ": generating ice mask from " << colorWidth << "x" << colorHeight
                  << " texture..." << '\n';

        // Create ice mask at the same resolution as the color texture
        std::vector<unsigned char> iceMask(static_cast<size_t>(colorWidth) * static_cast<size_t>(colorHeight), 0);

        int icePixels = 0;

        for (int rowY = 0; rowY < colorHeight; rowY++)
        {
            for (int colX = 0; colX < colorWidth; colX++)
            {
                int pixelIndex = (rowY * colorWidth) + colX;
                int colorIndex = pixelIndex * colorChannels;
                // stb_image returns raw pointer, pointer arithmetic is necessary for image processing
                // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                const unsigned char *pixelData = colorData + colorIndex;
                float redChannel = static_cast<float>(pixelData[0]) / MAX_PIXEL_VALUE_FLOAT;
                float greenChannel = static_cast<float>(pixelData[1]) / MAX_PIXEL_VALUE_FLOAT;
                float blueChannel = static_cast<float>(pixelData[2]) / MAX_PIXEL_VALUE_FLOAT;
                // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

                float brightness = (redChannel + greenChannel + blueChannel) / 3.0f;

                // Ice/snow detection heuristics:
                // - Very bright (high overall brightness)
                // - Low color saturation (close to white/gray)
                // - Not blue-shifted (to avoid mistaking water reflections)

                float maxChannel = std::max({redChannel, greenChannel, blueChannel});
                float minChannel = std::min({redChannel, greenChannel, blueChannel});
                float saturation = (maxChannel > 0.001f) ? (maxChannel - minChannel) / maxChannel : 0.0f;

                bool isIce = false;

                // Bright white/near-white: very high brightness, very low saturation
                if (brightness > BRIGHTNESS_THRESHOLD_BRIGHT_WHITE && saturation < SATURATION_THRESHOLD_LOW)
                {
                    isIce = true;
                }

                // Slightly less bright but still clearly ice/snow
                if (brightness > BRIGHTNESS_THRESHOLD_ICE_SNOW && saturation < SATURATION_THRESHOLD_VERY_LOW)
                {
                    isIce = true;
                }

                // Gray-white with slight blue tint (glacial ice)
                if (brightness > BRIGHTNESS_THRESHOLD_GLACIAL_ICE && saturation < SATURATION_THRESHOLD_MEDIUM &&
                    blueChannel >= redChannel * BLUE_CHANNEL_RATIO_THRESHOLD &&
                    blueChannel >= greenChannel * BLUE_CHANNEL_RATIO_THRESHOLD)
                {
                    isIce = true;
                }

                // Very bright regardless of saturation (fresh snow)
                if (brightness > BRIGHTNESS_THRESHOLD_FRESH_SNOW)
                {
                    isIce = true;
                }

                // Exclude obvious clouds (usually have slight texture/not pure white)
                // Note: This is imperfect as clouds and ice look similar in visible
                // light Future improvement: use multi-band data or cloud masks

                if (isIce)
                {
                    iceMask[pixelIndex] = MAX_PIXEL_VALUE;
                    icePixels++;
                }
            }
        }

        stbi_image_free(colorData);

        // Save ice mask
        if (stbi_write_png(maskPath.c_str(), colorWidth, colorHeight, 1, iceMask.data(), colorWidth) != 0)
        {
            float icePercentage =
                (static_cast<float>(icePixels) / static_cast<float>(colorWidth * colorHeight)) * 100.0F;
            std::cout << "    Saved: " << maskFilename << " (" << icePercentage << "% ice)" << '\n';
            masksGenerated++;
        }
        else
        {
            std::cerr << "    ERROR: Failed to save " << maskFilename << '\n';
        }
    }

    std::cout << "Generated " << masksGenerated << "/" << MONTHS_PER_YEAR << " ice masks" << '\n';
    std::cout << "===========================" << '\n';

    return masksGenerated > 0;
}
