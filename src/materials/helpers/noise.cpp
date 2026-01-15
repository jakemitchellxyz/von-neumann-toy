// ============================================================================
// Procedural Noise Texture Generation for City Light Flickering
// ============================================================================
// Generates tileable Perlin noise textures in sinusoidal projection
// These textures are sampled with time-offset UVs for animation

#include "noise.h"
#include <cmath>

namespace
{
// Perlin noise hash function constants
constexpr int HASH_PRIME_MULTIPLIER = 57;
constexpr int HASH_BIT_SHIFT = 13;
constexpr int HASH_COEFFICIENT_1 = 15731;
constexpr int HASH_COEFFICIENT_2 = 789221;
constexpr int HASH_COEFFICIENT_3 = 1376312589;
constexpr unsigned int HASH_MASK = 0x7fffffffU;
constexpr float HASH_NORMALIZATION = 1073741824.0F;

// Perlin noise fade curve polynomial coefficients (5th order smoothstep)
constexpr float FADE_COEFFICIENT_1 = 6.0F;
constexpr float FADE_COEFFICIENT_2 = 15.0F;
constexpr float FADE_COEFFICIENT_3 = 10.0F;

// Fractional Brownian Motion (FBM) constants
constexpr float INITIAL_AMPLITUDE = 1.0F;
constexpr float INITIAL_FREQUENCY = 1.0F;
constexpr float FREQUENCY_MULTIPLIER = 2.0F;
constexpr float INITIAL_TOTAL = 0.0F;
constexpr float INITIAL_MAX_VALUE = 0.0F;

// Grid cell offset for corner hashing
constexpr int GRID_CELL_OFFSET = 1;

// Simple CPU-side Perlin noise for texture generation
float perlinHash(int gridX, int gridY)
{
    int hashValue = gridX + (gridY * HASH_PRIME_MULTIPLIER);
    hashValue = (hashValue << HASH_BIT_SHIFT) ^ hashValue;
    const int maskedHash =
        (hashValue * ((hashValue * hashValue * HASH_COEFFICIENT_1) + HASH_COEFFICIENT_2) + HASH_COEFFICIENT_3) &
        static_cast<int>(HASH_MASK);
    return 1.0F - (static_cast<float>(maskedHash) / HASH_NORMALIZATION);
}

float perlinSmooth(float xCoord, float yCoord)
{
    int gridXInteger = static_cast<int>(std::floor(xCoord));
    int gridYInteger = static_cast<int>(std::floor(yCoord));
    float xFractional = xCoord - static_cast<float>(gridXInteger);
    float yFractional = yCoord - static_cast<float>(gridYInteger);

    // Fade curves using 5th order smoothstep polynomial
    float xFade = xFractional * xFractional * xFractional *
                  (xFractional * ((xFractional * FADE_COEFFICIENT_1) - FADE_COEFFICIENT_2) + FADE_COEFFICIENT_3);
    float yFade = yFractional * yFractional * yFractional *
                  (yFractional * ((yFractional * FADE_COEFFICIENT_1) - FADE_COEFFICIENT_2) + FADE_COEFFICIENT_3);

    // Hash values at the four corners of the grid cell
    float cornerBottomLeft = perlinHash(gridXInteger, gridYInteger);
    float cornerTopLeft = perlinHash(gridXInteger, gridYInteger + GRID_CELL_OFFSET);
    float cornerBottomRight = perlinHash(gridXInteger + GRID_CELL_OFFSET, gridYInteger);
    float cornerTopRight = perlinHash(gridXInteger + GRID_CELL_OFFSET, gridYInteger + GRID_CELL_OFFSET);

    // Bilinear interpolation
    float bottomInterpolation = cornerBottomLeft + (xFade * (cornerBottomRight - cornerBottomLeft));
    float topInterpolation = cornerTopLeft + (xFade * (cornerTopRight - cornerTopLeft));
    return bottomInterpolation + (yFade * (topInterpolation - bottomInterpolation));
}
} // namespace

float perlinFBM(float xCoord, float yCoord, int octaves, float persistence)
{
    float total = INITIAL_TOTAL;
    float amplitude = INITIAL_AMPLITUDE;
    float maxValue = INITIAL_MAX_VALUE;
    float frequency = INITIAL_FREQUENCY;

    for (int octaveIndex = 0; octaveIndex < octaves; octaveIndex++)
    {
        total += perlinSmooth(xCoord * frequency, yCoord * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= FREQUENCY_MULTIPLIER;
    }

    return total / maxValue;
}
