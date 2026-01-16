#include "stars-dynamic-skybox.h"
#include "../materials/helpers/gl.h"
#include "../materials/helpers/shader-loader.h"
#include "constants.h"
#include "settings.h"
#include "ui-overlay.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>


// stb_image for loading/saving textures (implementation in earth-material.cpp)
#include <stb_image.h>
#include <stb_image_write.h>

// GL_CLAMP_TO_EDGE may not be defined in basic Windows OpenGL headers
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// GL_REPEAT may not be defined in basic Windows OpenGL headers
#ifndef GL_REPEAT
#define GL_REPEAT 0x2901
#endif

// GL_GENERATE_MIPMAP may not be defined
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

// ==================================
// Global State
// ==================================
static bool g_skyboxInitialized = false;

// Star texture state
static GLuint g_starTexture = 0;
static bool g_starTextureReady = false;
static int g_starTextureWidth = 0;
static int g_starTextureHeight = 0;

// Additional celestial skybox textures
static GLuint g_constellationFiguresTexture = 0;
static GLuint g_constellationGridTexture = 0;
static GLuint g_constellationBoundsTexture = 0;
static GLuint g_milkywayTexture = 0; // Milky Way EXR file
static GLuint g_hiptycTexture = 0;   // Hiptyc stars EXR file
static bool g_constellationFiguresReady = false;
static bool g_constellationGridReady = false;
static bool g_constellationBoundsReady = false;
static bool g_milkywayReady = false;
static bool g_hiptycReady = false;

// Skybox shader program
static GLuint g_skyboxShaderProgram = 0;
static GLint g_skyboxUniformTexture = -1;
static GLint g_skyboxUniformUseAdditive = -1;
static GLint g_skyboxUniformExposure = -1;
static bool g_skyboxShaderReady = false;

// ==================================
// Shader Helper Functions
// ==================================

static GLuint compileSkyboxShader(GLenum type, const char *source)
{
    if (glCreateShader == nullptr)
    {
        std::cerr << "Failed to load OpenGL shader extensions" << '\n';
        return 0;
    }

    GLuint shader = glCreateShader(type);
    if (shader == 0)
    {
        std::cerr << "Failed to create skybox shader" << '\n';
        return 0;
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == 0)
    {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(logLength);
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        std::cerr << "Skybox shader compilation failed:\n" << log.data() << '\n';
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint linkSkyboxProgram(GLuint vertexShader, GLuint fragmentShader)
{
    if (glCreateProgram == nullptr)
    {
        std::cerr << "Failed to load OpenGL shader extensions" << '\n';
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0)
    {
        std::cerr << "Failed to create skybox shader program" << '\n';
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == 0)
    {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(logLength);
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        std::cerr << "Skybox shader program linking failed:\n" << log.data() << '\n';
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

static std::string getSkyboxShaderPath(const std::string &filename)
{
    // Try multiple possible paths
    std::vector<std::string> paths = {
        "shaders/" + filename,
        "src/concerns/shaders/" + filename,
        "../src/concerns/shaders/" + filename,
        "../../src/concerns/shaders/" + filename,
    };

    for (const auto &path : paths)
    {
        std::ifstream test(path);
        if (test.good())
        {
            test.close();
            return path;
        }
    }

    // Return first path as default (will fail with error message)
    return paths[0];
}

static bool initializeSkyboxShader()
{
    if (g_skyboxShaderReady && g_skyboxShaderProgram != 0)
    {
        return true;
    }

    // Ensure GL extensions are loaded
    if (!loadGLExtensions())
    {
        std::cerr << "ERROR: Failed to load OpenGL extensions for skybox shader" << '\n';
        return false;
    }

    // Load vertex shader
    std::string vertexShaderPath = getSkyboxShaderPath("skybox-vertex.glsl");
    std::string vertexShaderSource = loadShaderFile(vertexShaderPath);
    if (vertexShaderSource.empty())
    {
        std::cerr << "ERROR: Could not load skybox-vertex.glsl from file" << '\n';
        std::cerr << "  Tried path: " << vertexShaderPath << '\n';
        return false;
    }

    // Load fragment shader
    std::string fragmentShaderPath = getSkyboxShaderPath("skybox-fragment.glsl");
    std::string fragmentShaderSource = loadShaderFile(fragmentShaderPath);
    if (fragmentShaderSource.empty())
    {
        std::cerr << "ERROR: Could not load skybox-fragment.glsl from file" << '\n';
        std::cerr << "  Tried path: " << fragmentShaderPath << '\n';
        return false;
    }

    // Compile shaders
    GLuint vertexShader = compileSkyboxShader(GL_VERTEX_SHADER, vertexShaderSource.c_str());
    if (vertexShader == 0)
    {
        return false;
    }

    GLuint fragmentShader = compileSkyboxShader(GL_FRAGMENT_SHADER, fragmentShaderSource.c_str());
    if (fragmentShader == 0)
    {
        glDeleteShader(vertexShader);
        return false;
    }

    // Link program
    g_skyboxShaderProgram = linkSkyboxProgram(vertexShader, fragmentShader);
    if (g_skyboxShaderProgram == 0)
    {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    // Get uniform locations
    g_skyboxUniformTexture = glGetUniformLocation(g_skyboxShaderProgram, "skyboxTexture");
    g_skyboxUniformUseAdditive = glGetUniformLocation(g_skyboxShaderProgram, "useAdditiveBlending");
    g_skyboxUniformExposure = glGetUniformLocation(g_skyboxShaderProgram, "exposure");

    // Clean up shader objects (they're linked into the program now)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    g_skyboxShaderReady = true;
    std::cout << "Skybox shader initialized successfully" << '\n';
    return true;
}

// ==================================
// Initialization
// ==================================

void InitializeSkybox(const std::string &defaultsPath)
{
    if (g_skyboxInitialized)
    {
        return;
    }

    g_skyboxInitialized = true;
}

bool IsSkyboxInitialized()
{
    return g_skyboxInitialized;
}

// ==================================
// Helper Functions
// ==================================
// Convert Right Ascension and Declination (J2000 equatorial) to 3D Cartesian
// coordinates in our ecliptic-aligned display system
// ra: in radians (0 to 2π)
// dec: in radians (-π/2 to π/2)
glm::vec3 raDecToCartesian(float ra, float dec, float radius)
{
    // Step 1: Convert RA/Dec to J2000 equatorial Cartesian
    // J2000 equatorial: X -> vernal equinox (0h RA), Y -> 90° RA (6h), Z -> celestial north pole
    double x_eq = cos(dec) * cos(ra);
    double y_eq = cos(dec) * sin(ra);
    double z_eq = sin(dec);

    // Step 2: Rotate from J2000 equatorial to J2000 ecliptic
    // This is a rotation around the X-axis by the obliquity ε
    // [ 1    0       0     ]   [ x_eq ]
    // [ 0  cos(ε)  sin(ε)  ] * [ y_eq ]
    // [ 0 -sin(ε)  cos(ε)  ]   [ z_eq ]
    double cosObl = cos(OBLIQUITY_J2000_RAD);
    double sinObl = sin(OBLIQUITY_J2000_RAD);

    double x_ecl = x_eq;
    double y_ecl = cosObl * y_eq + sinObl * z_eq;
    double z_ecl = -sinObl * y_eq + cosObl * z_eq;

    // Step 3: Convert to our display coordinates (Y-up, right-handed)
    // J2000 ecliptic: X -> vernal equinox, Y -> 90° ecl lon, Z -> ecliptic north pole
    // Display: X -> same, Y -> up (ecl Z), Z -> negated ecl Y (for right-handedness)
    float x_disp = static_cast<float>(x_ecl) * radius;
    float y_disp = static_cast<float>(z_ecl) * radius;  // Ecliptic Z -> Display Y (up)
    float z_disp = static_cast<float>(-y_ecl) * radius; // Ecliptic Y -> Display -Z (right-handed)

    return glm::vec3(x_disp, y_disp, z_disp);
}

// Overload for hours/degrees (used by constellation loader)
glm::vec3 raDecToCartesianHours(float raHours, float decDeg, float radius)
{
    float raRad = raHours * (2.0f * static_cast<float>(PI) / 24.0f);
    float decRad = glm::radians(decDeg);
    return raDecToCartesian(raRad, decRad, radius);
}

// Calculate Earth's rotation angle for the current Julian Date
float getEarthRotationAngle(double jd)
{
    double T = (jd - JD_J2000) / 36525.0;
    double gmst = 280.46061837 + 360.98564736629 * (jd - JD_J2000) + 0.000387933 * T * T;
    gmst = fmod(gmst, 360.0);
    if (gmst < 0)
        gmst += 360.0;
    return static_cast<float>(gmst);
}

// ==================================
// Billboard Text Character Definitions
// ==================================

struct CharSegment
{
    float x1, y1, x2, y2;
};

static const std::vector<CharSegment> &getCharSegments(char c)
{
    static std::map<char, std::vector<CharSegment>> chars;
    static bool initialized = false;

    if (!initialized)
    {
        chars['A'] = {{0, 0, 0.5f, 1}, {0.5f, 1, 1, 0}, {0.2f, 0.4f, 0.8f, 0.4f}};
        chars['B'] = {{0, 0, 0, 1},
                      {0, 1, 0.7f, 1},
                      {0.7f, 1, 0.7f, 0.55f},
                      {0.7f, 0.55f, 0, 0.5f},
                      {0, 0.5f, 0.7f, 0.5f},
                      {0.7f, 0.5f, 0.7f, 0},
                      {0.7f, 0, 0, 0}};
        chars['C'] = {{1, 0.2f, 0.3f, 0},
                      {0.3f, 0, 0, 0.3f},
                      {0, 0.3f, 0, 0.7f},
                      {0, 0.7f, 0.3f, 1},
                      {0.3f, 1, 1, 0.8f}};
        chars['D'] = {{0, 0, 0, 1},
                      {0, 1, 0.6f, 1},
                      {0.6f, 1, 1, 0.7f},
                      {1, 0.7f, 1, 0.3f},
                      {1, 0.3f, 0.6f, 0},
                      {0.6f, 0, 0, 0}};
        chars['E'] = {{1, 0, 0, 0}, {0, 0, 0, 1}, {0, 1, 1, 1}, {0, 0.5f, 0.7f, 0.5f}};
        chars['F'] = {{0, 0, 0, 1}, {0, 1, 1, 1}, {0, 0.5f, 0.7f, 0.5f}};
        chars['G'] = {{1, 0.8f, 0.3f, 1},
                      {0.3f, 1, 0, 0.7f},
                      {0, 0.7f, 0, 0.3f},
                      {0, 0.3f, 0.3f, 0},
                      {0.3f, 0, 1, 0.2f},
                      {1, 0.2f, 1, 0.5f},
                      {1, 0.5f, 0.5f, 0.5f}};
        chars['H'] = {{0, 0, 0, 1}, {1, 0, 1, 1}, {0, 0.5f, 1, 0.5f}};
        chars['I'] = {{0.3f, 0, 0.7f, 0}, {0.5f, 0, 0.5f, 1}, {0.3f, 1, 0.7f, 1}};
        chars['J'] = {{0.2f, 1, 0.8f, 1}, {0.5f, 1, 0.5f, 0.2f}, {0.5f, 0.2f, 0.3f, 0}, {0.3f, 0, 0, 0.2f}};
        chars['K'] = {{0, 0, 0, 1}, {0, 0.5f, 1, 1}, {0.3f, 0.65f, 1, 0}};
        chars['L'] = {{0, 1, 0, 0}, {0, 0, 1, 0}};
        chars['M'] = {{0, 0, 0, 1}, {0, 1, 0.5f, 0.5f}, {0.5f, 0.5f, 1, 1}, {1, 1, 1, 0}};
        chars['N'] = {{0, 0, 0, 1}, {0, 1, 1, 0}, {1, 0, 1, 1}};
        chars['O'] = {{0.3f, 0, 0, 0.3f},
                      {0, 0.3f, 0, 0.7f},
                      {0, 0.7f, 0.3f, 1},
                      {0.3f, 1, 0.7f, 1},
                      {0.7f, 1, 1, 0.7f},
                      {1, 0.7f, 1, 0.3f},
                      {1, 0.3f, 0.7f, 0},
                      {0.7f, 0, 0.3f, 0}};
        chars['P'] = {{0, 0, 0, 1},
                      {0, 1, 0.7f, 1},
                      {0.7f, 1, 1, 0.75f},
                      {1, 0.75f, 1, 0.55f},
                      {1, 0.55f, 0.7f, 0.5f},
                      {0.7f, 0.5f, 0, 0.5f}};
        chars['Q'] = {{0.3f, 0, 0, 0.3f},
                      {0, 0.3f, 0, 0.7f},
                      {0, 0.7f, 0.3f, 1},
                      {0.3f, 1, 0.7f, 1},
                      {0.7f, 1, 1, 0.7f},
                      {1, 0.7f, 1, 0.3f},
                      {1, 0.3f, 0.7f, 0},
                      {0.7f, 0, 0.3f, 0},
                      {0.6f, 0.3f, 1, 0}};
        chars['R'] = {{0, 0, 0, 1},
                      {0, 1, 0.7f, 1},
                      {0.7f, 1, 1, 0.75f},
                      {1, 0.75f, 1, 0.55f},
                      {1, 0.55f, 0.7f, 0.5f},
                      {0.7f, 0.5f, 0, 0.5f},
                      {0.5f, 0.5f, 1, 0}};
        chars['S'] = {{1, 0.8f, 0.3f, 1},
                      {0.3f, 1, 0, 0.75f},
                      {0, 0.75f, 0.3f, 0.5f},
                      {0.3f, 0.5f, 0.7f, 0.5f},
                      {0.7f, 0.5f, 1, 0.25f},
                      {1, 0.25f, 0.7f, 0},
                      {0.7f, 0, 0, 0.2f}};
        chars['T'] = {{0, 1, 1, 1}, {0.5f, 1, 0.5f, 0}};
        chars['U'] = {{0, 1, 0, 0.3f}, {0, 0.3f, 0.3f, 0}, {0.3f, 0, 0.7f, 0}, {0.7f, 0, 1, 0.3f}, {1, 0.3f, 1, 1}};
        chars['V'] = {{0, 1, 0.5f, 0}, {0.5f, 0, 1, 1}};
        chars['W'] = {{0, 1, 0.25f, 0}, {0.25f, 0, 0.5f, 0.5f}, {0.5f, 0.5f, 0.75f, 0}, {0.75f, 0, 1, 1}};
        chars['X'] = {{0, 0, 1, 1}, {0, 1, 1, 0}};
        chars['Y'] = {{0, 1, 0.5f, 0.5f}, {1, 1, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f, 0}};
        chars['Z'] = {{0, 1, 1, 1}, {1, 1, 0, 0}, {0, 0, 1, 0}};
        chars[' '] = {};
        chars['-'] = {{0.2f, 0.5f, 0.8f, 0.5f}};
        chars['_'] = {{0, 0, 1, 0}};

        initialized = true;
    }

    static std::vector<CharSegment> empty;
    char upper = std::toupper(c);
    auto it = chars.find(upper);
    return (it != chars.end()) ? it->second : empty;
}

static void DrawBillboardText(const glm::vec3 &pos,
                              const std::string &text,
                              float size,
                              const glm::vec3 &right,
                              const glm::vec3 &up)
{
    if (text.empty())
        return;

    float charWidth = size * 0.7f;
    float charSpacing = size * 0.2f;
    float totalWidth = text.length() * (charWidth + charSpacing) - charSpacing;

    glm::vec3 startPos = pos - right * (totalWidth * 0.5f);

    glLineWidth(1.5f);
    glColor4f(0.8f, 0.8f, 0.85f, 0.7f);

    glBegin(GL_LINES);

    float currentX = 0.0f;
    for (char c : text)
    {
        const auto &segments = getCharSegments(c);
        glm::vec3 charOrigin = startPos + right * currentX;

        for (const auto &seg : segments)
        {
            glm::vec3 p1 = charOrigin + right * (seg.x1 * charWidth) + up * (seg.y1 * size);
            glm::vec3 p2 = charOrigin + right * (seg.x2 * charWidth) + up * (seg.y2 * size);
            glVertex3f(p1.x, p1.y, p1.z);
            glVertex3f(p2.x, p2.y, p2.z);
        }

        currentX += charWidth + charSpacing;
    }

    glEnd();
}

static glm::vec3 calculateConstellationCenter(const std::vector<glm::vec3> &starPositions)
{
    if (starPositions.empty())
        return glm::vec3(0.0f);

    glm::vec3 minPos = starPositions[0];
    glm::vec3 maxPos = starPositions[0];

    for (const auto &pos : starPositions)
    {
        minPos = glm::min(minPos, pos);
        maxPos = glm::max(maxPos, pos);
    }

    return (minPos + maxPos) * 0.5f;
}

static std::string formatConstellationName(const std::string &name)
{
    std::string result;
    for (char c : name)
    {
        if (c == '_')
        {
            result += ' ';
        }
        else
        {
            result += std::toupper(c);
        }
    }
    return result;
}


// ==================================
// Constellation Texture Preprocessing
// ==================================

// Simple bilinear interpolation resize (similar to EarthMaterial::resizeImage)
static void resizeConstellationImage(const unsigned char *src,
                                     int srcW,
                                     int srcH,
                                     unsigned char *dst,
                                     int dstW,
                                     int dstH,
                                     int channels)
{
    float xRatio = static_cast<float>(srcW) / dstW;
    float yRatio = static_cast<float>(srcH) / dstH;

    for (int y = 0; y < dstH; y++)
    {
        float srcY = y * yRatio;
        int y0 = static_cast<int>(srcY);
        int y1 = std::min(y0 + 1, srcH - 1);
        float yFrac = srcY - y0;

        for (int x = 0; x < dstW; x++)
        {
            float srcX = x * xRatio;
            int x0 = static_cast<int>(srcX);
            int x1 = std::min(x0 + 1, srcW - 1);
            float xFrac = srcX - x0;

            for (int c = 0; c < channels; c++)
            {
                // Bilinear interpolation
                float v00 = src[(y0 * srcW + x0) * channels + c];
                float v10 = src[(y0 * srcW + x1) * channels + c];
                float v01 = src[(y1 * srcW + x0) * channels + c];
                float v11 = src[(y1 * srcW + x1) * channels + c];

                float v0 = v00 * (1 - xFrac) + v10 * xFrac;
                float v1 = v01 * (1 - xFrac) + v11 * xFrac;
                float value = v0 * (1 - yFrac) + v1 * yFrac;

                dst[(y * dstW + x) * channels + c] = static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f));
            }
        }
    }
}

// Legacy function - now calls the new preprocessing function
bool PreprocessConstellationTexture(const std::string &defaultsPath,
                                    const std::string &outputPath,
                                    TextureResolution resolution)
{
    return PreprocessSkyboxTextures(defaultsPath, outputPath, resolution);
}


// Helper function to load a texture (supports JPG, PNG, TIF)
// Handles both RGB (3 channels) and RGBA (4 channels) for PNG transparency
static GLuint loadTextureFile(const std::string &filepath, bool &success)
{
    success = false;

    if (!std::filesystem::exists(filepath))
    {
        return 0;
    }

    int width, height, channels;
    // Load with 0 channels to get the actual number of channels from the file
    // This allows PNG files with alpha to be loaded as RGBA
    unsigned char *data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);

    if (!data)
    {
        std::cerr << "Failed to load texture: " << filepath << " - " << stbi_failure_reason() << std::endl;
        return 0;
    }

    std::cout << "  Loaded texture: " << width << "x" << height << " (" << channels << " channels)" << std::endl;

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    // Use GL_REPEAT for S (U) coordinate to allow seamless horizontal wrapping
    // Keep GL_CLAMP_TO_EDGE for T (V) coordinate to prevent vertical wrapping
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Handle both RGB (3 channels) and RGBA (4 channels)
    if (channels == 4)
    {
        // RGBA texture with alpha channel
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    }
    else
    {
        // RGB texture (no alpha) - convert to RGBA if needed, or use RGB
        // For compatibility, we'll use RGB for 3-channel images
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    }

    stbi_image_free(data);
    success = true;

    return textureId;
}

// Helper function to load an EXR/HDR file (float format)
static GLuint loadEXRFile(const std::string &filepath, bool &success)
{
    success = false;

    if (!std::filesystem::exists(filepath))
    {
        return 0;
    }

    int width, height, channels;
    float *data = stbi_loadf(filepath.c_str(), &width, &height, &channels, 3);

    if (!data)
    {
        std::cerr << "Failed to load EXR/HDR file: " << filepath << " - " << stbi_failure_reason() << std::endl;
        std::cerr << "  Note: stbi_loadf may not support EXR format, only HDR" << std::endl;
        return 0;
    }

    std::cout << "  Loaded EXR/HDR: " << width << "x" << height << " (" << channels << " channels)" << std::endl;

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    // Use GL_REPEAT for S (U) coordinate to allow seamless horizontal wrapping
    // Keep GL_CLAMP_TO_EDGE for T (V) coordinate to prevent vertical wrapping
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload as RGB32F (3-channel float) for HDR/EXR data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, data);

    stbi_image_free(data);
    success = true;

    return textureId;
}

// Initialize the star texture material (load pre-generated texture into OpenGL)
// texturePath: path to the output/cache folder (e.g., "celestial-skybox")
//              This is where PreprocessSkyboxTextures writes processed files
//              NOT the source directory (defaults/celestial-skybox)
bool InitializeStarTextureMaterial(const std::string &texturePath, TextureResolution resolution)
{
    if (g_starTextureReady)
    {
        return true;
    }

    // Load from output/cache directory: celestial-skybox/[resolution]/
    // (not from defaults/celestial-skybox which is the source directory)
    std::string resolutionFolder = getResolutionFolderName(resolution);
    std::string basePath = texturePath + "/" + resolutionFolder;

    std::cout << "Loading celestial skybox textures from cache: " << basePath << std::endl;

    // Load combined Milky Way + Hiptyc HDR texture (pre-combined additively during preprocessing)
    std::string combinedPath = basePath + "/milkyway_combined.hdr";
    if (!std::filesystem::exists(combinedPath))
    {
        // Fallback to separate files if combined doesn't exist (backward compatibility)
        std::string milkywayPath = basePath + "/milkyway_2020.hdr";
        if (!std::filesystem::exists(milkywayPath))
        {
            milkywayPath = basePath + "/milkyway_2020.exr";
        }
        g_milkywayTexture = loadEXRFile(milkywayPath, g_milkywayReady);
        if (g_milkywayReady)
        {
            std::cout << "  Milky Way texture loaded: " << milkywayPath << std::endl;
            g_starTexture = g_milkywayTexture; // Use Milky Way as base texture
            g_starTextureReady = true;
        }

        // Load Hiptyc stars EXR texture (second layer)
        std::string hiptycPath = basePath + "/hiptyc_2020.hdr";
        if (!std::filesystem::exists(hiptycPath))
        {
            hiptycPath = basePath + "/hiptyc_2020.exr";
        }
        g_hiptycTexture = loadEXRFile(hiptycPath, g_hiptycReady);
        if (g_hiptycReady)
        {
            std::cout << "  Hiptyc stars texture loaded: " << hiptycPath << std::endl;
        }
    }
    else
    {
        // Load combined texture
        g_milkywayTexture = loadEXRFile(combinedPath, g_milkywayReady);
        if (g_milkywayReady)
        {
            std::cout << "  Combined Milky Way + Hiptyc texture loaded: " << combinedPath << std::endl;
            g_starTexture = g_milkywayTexture; // Use combined texture as base
            g_starTextureReady = true;
            // Set hiptyc as ready too so rendering code knows we have the combined texture
            g_hiptycReady = true;
            g_hiptycTexture = g_milkywayTexture; // Same texture
        }
    }

    // Load celestial grid texture (third layer) - PNG with transparency
    std::string gridPath = basePath + "/celestial_grid.png";
    if (!std::filesystem::exists(gridPath))
    {
        gridPath = basePath + "/celestial_grid.jpg"; // Fallback to JPG
    }
    if (!std::filesystem::exists(gridPath))
    {
        gridPath = basePath + "/grid.png"; // Alternative name
    }
    g_constellationGridTexture = loadTextureFile(gridPath, g_constellationGridReady);
    if (g_constellationGridReady)
    {
        std::cout << "  Celestial grid texture loaded: " << gridPath << std::endl;
    }

    // Load constellation figures texture (fourth layer) - PNG with transparency
    std::string figuresPath = basePath + "/constellation_figures.png";
    if (!std::filesystem::exists(figuresPath))
    {
        figuresPath = basePath + "/constellation_figures.jpg"; // Fallback to JPG
    }
    g_constellationFiguresTexture = loadTextureFile(figuresPath, g_constellationFiguresReady);
    if (g_constellationFiguresReady)
    {
        std::cout << "  Constellation figures texture loaded: " << figuresPath << std::endl;
    }

    // Load constellation bounds texture (top layer) - PNG with transparency
    std::string boundsPath = basePath + "/constellation_bounds.png";
    if (!std::filesystem::exists(boundsPath))
    {
        boundsPath = basePath + "/constellation_bounds.jpg"; // Fallback to JPG
    }
    if (!std::filesystem::exists(boundsPath))
    {
        boundsPath = basePath + "/bounds.png"; // Alternative name
    }
    g_constellationBoundsTexture = loadTextureFile(boundsPath, g_constellationBoundsReady);
    if (g_constellationBoundsReady)
    {
        std::cout << "  Constellation bounds texture loaded: " << boundsPath << std::endl;
    }

    // If we didn't load Milky Way, try to use Hiptyc as fallback
    if (!g_starTextureReady && g_hiptycReady)
    {
        g_starTexture = g_hiptycTexture;
        g_starTextureReady = true;
        std::cout << "  Using Hiptyc stars as base texture" << std::endl;
    }

    // If still no texture, try constellation figures as last resort
    if (!g_starTextureReady && g_constellationFiguresReady)
    {
        g_starTexture = g_constellationFiguresTexture;
        g_starTextureReady = true;
        std::cout << "  Using constellation figures as base texture" << std::endl;
    }

    if (g_starTextureReady)
    {
        std::cout << "Celestial skybox textures initialized successfully" << std::endl;
        std::cout << "  Base texture ID: " << g_starTexture << std::endl;
        std::cout << "  Milky Way ready: " << (g_milkywayReady ? "yes" : "no") << std::endl;
        std::cout << "  Hiptyc stars ready: " << (g_hiptycReady ? "yes" : "no") << std::endl;
        std::cout << "  Celestial grid ready: " << (g_constellationGridReady ? "yes" : "no") << std::endl;
        std::cout << "  Constellation figures ready: " << (g_constellationFiguresReady ? "yes" : "no") << std::endl;
        std::cout << "  Constellation bounds ready: " << (g_constellationBoundsReady ? "yes" : "no") << std::endl;

        // Initialize skybox shader for HDR rendering
        initializeSkyboxShader();

        return true;
    }
    else
    {
        std::cerr << "Failed to load any celestial skybox textures from: " << basePath << std::endl;
        std::cerr
            << "  Expected files: milkyway_combined.hdr (or milkyway_2020.hdr/exr + hiptyc_2020.hdr/exr), "
               "celestial_grid.png (or .jpg), constellation_figures.png (or .jpg), constellation_bounds.png (or .jpg)"
            << std::endl;
        return false;
    }
}

bool IsStarTextureReady()
{
    return g_starTextureReady;
}

// Helper function to draw the skybox sphere with a given texture
// This is called multiple times to layer textures
static void drawSkyboxSphere(const glm::vec3 &cameraPos, GLuint textureId)
{
    if (textureId == 0)
        return;

    glBindTexture(GL_TEXTURE_2D, textureId);

    glPushMatrix();
    glTranslatef(cameraPos.x, cameraPos.y, cameraPos.z);

    // Draw textured sphere (inside-out for skybox, normals face inward)
    // Very low resolution for skybox - it's very far away and fragment shader handles precision
    // Target: ~128 total triangles (8 slices × 8 stacks × 2 triangles per quad = 128 triangles)
    const int slices = 8;
    const int stacks = 8;
    const float radius = SKYBOX_RADIUS;

    // UV mapping for constellation texture (equatorial coordinates, plate carrée projection):
    // Texture is centered at 0h RA, R.A. increases to the left
    // U = 0.5 - (RA / 24h) where RA is in hours (0-24)
    // V = 0.5 - (Dec / 180°) where Dec is in degrees (-90 to +90)
    // The sphere is drawn in ecliptic coordinates, so we need to convert to equatorial for UV mapping

    for (int i = 0; i < stacks; ++i)
    {
        // phi is ecliptic latitude from -π/2 (south) to +π/2 (north)
        float phi1 = static_cast<float>(PI) * (-0.5f + static_cast<float>(i) / stacks);
        float phi2 = static_cast<float>(PI) * (-0.5f + static_cast<float>(i + 1) / stacks);

        float y1 = radius * sin(phi1);
        float y2 = radius * sin(phi2);
        float r1 = radius * cos(phi1);
        float r2 = radius * cos(phi2);

        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; ++j)
        {
            // theta is ecliptic longitude from 0 to 2π
            float theta = 2.0f * static_cast<float>(PI) * static_cast<float>(j) / slices;
            float cosTheta = cos(theta);
            float sinTheta = sin(theta);

            // Convert ecliptic coordinates to Cartesian (ecliptic frame)
            float x_ecl = cos(phi1) * cosTheta;
            float y_ecl = cos(phi1) * sinTheta;
            float z_ecl = sin(phi1);

            // Convert from ecliptic to equatorial coordinates (inverse of the transformation in raDecToCartesian)
            // Ecliptic -> Equatorial rotation around X-axis by -obliquity
            double cosObl = cos(OBLIQUITY_J2000_RAD);
            double sinObl = sin(OBLIQUITY_J2000_RAD);

            // Inverse rotation: [x_eq]   [ 1    0       0     ]   [x_ecl]
            //                  [y_eq] = [ 0  cos(ε) -sin(ε) ] * [y_ecl]
            //                  [z_eq]   [ 0  sin(ε)  cos(ε) ]   [z_ecl]
            double x_eq = x_ecl;
            double y_eq = cosObl * y_ecl - sinObl * z_ecl;
            double z_eq = sinObl * y_ecl + cosObl * z_ecl;

            // Convert equatorial Cartesian to RA/Dec
            double ra = std::atan2(y_eq, x_eq); // -π to π
            double dec = std::asin(z_eq);       // -π/2 to π/2

            // Normalize RA to 0-2π, then convert to hours (0-24)
            if (ra < 0)
                ra += 2.0 * PI;
            double raHours = ra * 12.0 / PI; // Convert radians to hours (2π rad = 24h)

            // Convert Dec from radians to degrees
            double decDeg = dec * 180.0 / PI;

            // Map to UV coordinates for constellation texture
            // U: center (0h RA) is at U=0.5, R.A. increases to the left
            float u = 0.5f - static_cast<float>(raHours / 24.0);
            // Don't clamp U - allow it to extend beyond [0,1] for seamless blending
            // The shader will detect when we're near the seam (within 0.05 of edges)
            // and blend between both sides. We allow U to go slightly beyond the blend zone
            // to ensure the shader can always sample both sides when needed
            // No wrapping here - let the shader handle the blending
            // U can be in range [-0.1, 1.1] to give shader room to blend

            // V: Dec -90° to +90° maps to V: 1 to 0
            // Keep V clamped since we don't want vertical wrapping
            float v1 = 0.5f - static_cast<float>(decDeg / 180.0);
            v1 = std::clamp(v1, 0.0f, 1.0f);

            // Calculate for phi2 as well
            float x_ecl2 = cos(phi2) * cosTheta;
            float y_ecl2 = cos(phi2) * sinTheta;
            float z_ecl2 = sin(phi2);

            double x_eq2 = x_ecl2;
            double y_eq2 = cosObl * y_ecl2 - sinObl * z_ecl2;
            double z_eq2 = sinObl * y_ecl2 + cosObl * z_ecl2;

            double ra2 = std::atan2(y_eq2, x_eq2);
            double dec2 = std::asin(z_eq2);

            if (ra2 < 0)
                ra2 += 2.0 * PI;
            double raHours2 = ra2 * 12.0 / PI;
            double decDeg2 = dec2 * 180.0 / PI;

            float u2 = 0.5f - static_cast<float>(raHours2 / 24.0);
            // Same seamless blending for u2 - shader handles the blending

            float v2 = 0.5f - static_cast<float>(decDeg2 / 180.0);
            v2 = std::clamp(v2, 0.0f, 1.0f);

            // Convert from spherical to Cartesian for display:
            // In ecliptic: X = r*cos(lat)*cos(lon), Y = r*cos(lat)*sin(lon), Z = r*sin(lat)
            // In display (Y-up): X_disp = X_ecl, Y_disp = Z_ecl, Z_disp = -Y_ecl

            // First vertex (at phi1)
            float x1 = r1 * cosTheta;     // ecliptic X
            float z1_ecl = r1 * sinTheta; // ecliptic Y
            glTexCoord2f(u, v1);
            // No normals needed - lighting is disabled for skybox
            // Setting normals can cause visual artifacts even when lighting is off
            glVertex3f(x1, y1, -z1_ecl); // Y_disp = Y_ecl_z = y1, Z_disp = -Y_ecl

            // Second vertex (at phi2)
            float x2 = r2 * cosTheta;
            float z2_ecl = r2 * sinTheta;
            glTexCoord2f(u2, v2);
            // No normals needed - lighting is disabled for skybox
            glVertex3f(x2, y2, -z2_ecl);
        }
        glEnd();

        // Count triangles: TRIANGLE_STRIP with (slices+1)*2 vertices = (slices+1)*2 - 2 triangles
        CountTriangles(GL_TRIANGLE_STRIP, (slices + 1) * 2);
    }

    glPopMatrix();
}

void DrawSkyboxTextured(const glm::vec3 &cameraPos)
{
    if (!g_starTextureReady)
    {
        // Try to draw with any available texture
        if (!g_milkywayReady && !g_hiptycReady && !g_constellationFiguresReady)
        {
            return; // No textures available
        }
    }

    // Use shader-based rendering for HDR textures
    // Fall back to fixed-function if shader not available
    bool useShader = g_skyboxShaderReady && g_skyboxShaderProgram != 0;

    // Save OpenGL state that we'll modify
    GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
    GLboolean lightingEnabled = glIsEnabled(GL_LIGHTING);
    GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean depthMaskEnabled;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskEnabled);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLboolean texture2DEnabled = glIsEnabled(GL_TEXTURE_2D);

    // Save blend function
    GLint blendSrc, blendDst;
    glGetIntegerv(GL_BLEND_SRC, &blendSrc);
    glGetIntegerv(GL_BLEND_DST, &blendDst);

    // Save current shader program
    GLint currentProgram = 0;
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);

    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);  // Disable culling for inside-out sphere
    glDisable(GL_DEPTH_TEST); // Disable depth testing so layers don't occlude each other
    glDepthMask(GL_FALSE);    // Don't write to depth buffer
    glEnable(GL_TEXTURE_2D);

    // Set white color for unlit texture (no material properties needed)
    glColor3f(1.0f, 1.0f, 1.0f);

    if (useShader)
    {
        // Use shader program for HDR texture support
        glUseProgram(g_skyboxShaderProgram);

        // Set texture unit (GL_TEXTURE0)
        if (g_skyboxUniformTexture >= 0)
        {
            glUniform1i(g_skyboxUniformTexture, 0);
        }

        // Set exposure to brighten HDR values for display
        // HDR files often have low values (< 1.0) that need scaling to be visible
        if (g_skyboxUniformExposure >= 0)
        {
            glUniform1f(g_skyboxUniformExposure, 5.0f); // No scaling - use HDR values as-is
        }

        glActiveTexture_ptr(GL_TEXTURE0);
    }
    else
    {
        // Fallback to fixed-function pipeline
        glUseProgram(0);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }

    // Layer textures in order (bottom to top):
    // 1. Combined Milky Way + Hiptyc (pre-combined additively during preprocessing)
    //    If combined texture exists, use it. Otherwise fall back to separate textures.
    if (g_milkywayReady && g_milkywayTexture != 0)
    {
        glDisable(GL_BLEND); // No blending needed for base layer
        if (useShader && g_skyboxUniformUseAdditive >= 0)
        {
            glUniform1i(g_skyboxUniformUseAdditive, 0); // false - not additive
        }
        drawSkyboxSphere(cameraPos, g_milkywayTexture);

        // Only render hiptyc separately if we don't have the combined texture
        // (check if hiptyc texture is different from milkyway texture)
        if (g_hiptycReady && g_hiptycTexture != 0 && g_hiptycTexture != g_milkywayTexture)
        {
            // Separate hiptyc texture exists - render additively on top
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE); // Additive blending for second layer
            if (useShader && g_skyboxUniformUseAdditive >= 0)
            {
                glUniform1i(g_skyboxUniformUseAdditive, 1); // true - additive blending
            }
            drawSkyboxSphere(cameraPos, g_hiptycTexture);
        }
    }

    // 3. Celestial grid (third layer) - PNG with alpha transparency
    // Use additive blending: black pixels add nothing, colored pixels add their color
    if (g_showCelestialGrid && g_constellationGridReady && g_constellationGridTexture != 0)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE); // Additive blending - shader multiplies by alpha
        if (useShader && g_skyboxUniformUseAdditive >= 0)
        {
            glUniform1i(g_skyboxUniformUseAdditive, 1); // true - additive blending with alpha
        }
        drawSkyboxSphere(cameraPos, g_constellationGridTexture);
    }

    // 4. Constellation figures (fourth layer) - PNG with alpha transparency
    if (g_showConstellationFigures && g_constellationFiguresReady && g_constellationFiguresTexture != 0)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE); // Additive blending - shader multiplies by alpha
        if (useShader && g_skyboxUniformUseAdditive >= 0)
        {
            glUniform1i(g_skyboxUniformUseAdditive, 1); // true - additive blending with alpha
        }
        drawSkyboxSphere(cameraPos, g_constellationFiguresTexture);
    }

    // 5. Constellation bounds (top layer) - PNG with alpha transparency
    if (g_showConstellationBounds && g_constellationBoundsReady && g_constellationBoundsTexture != 0)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE); // Additive blending - shader multiplies by alpha
        if (useShader && g_skyboxUniformUseAdditive >= 0)
        {
            glUniform1i(g_skyboxUniformUseAdditive, 1); // true - additive blending with alpha
        }
        drawSkyboxSphere(cameraPos, g_constellationBoundsTexture);
    }

    // Fallback: if no layered textures, use base texture
    if (!g_milkywayReady && !g_hiptycReady && g_starTextureReady && g_starTexture != 0)
    {
        if (useShader && g_skyboxUniformUseAdditive >= 0)
        {
            glUniform1i(g_skyboxUniformUseAdditive, 0); // false
        }
        drawSkyboxSphere(cameraPos, g_starTexture);
    }

    // Restore OpenGL state
    glBindTexture(GL_TEXTURE_2D, 0);

    // Restore previous shader program
    if (useShader)
    {
        glUseProgram(currentProgram);
    }
    else
    {
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // Restore default
    }

    // Restore previous state
    if (!texture2DEnabled)
        glDisable(GL_TEXTURE_2D);
    if (blendEnabled)
    {
        glBlendFunc(blendSrc, blendDst); // Restore previous blend function
    }
    else
    {
        glDisable(GL_BLEND);
    }
    glDepthMask(depthMaskEnabled ? GL_TRUE : GL_FALSE);
    if (depthTestEnabled)
        glEnable(GL_DEPTH_TEST);
    if (cullFaceEnabled)
        glEnable(GL_CULL_FACE);
    if (lightingEnabled)
        glEnable(GL_LIGHTING);
}

// Draw wireframe version of skybox (for wireframe overlay mode)
void DrawSkyboxWireframe(const glm::vec3 &cameraPos)
{
    if (!g_starTextureReady)
    {
        // Try to draw with any available texture
        if (!g_milkywayReady && !g_hiptycReady && !g_constellationFiguresReady)
        {
            return; // No textures available
        }
    }

    // Render the same geometry as DrawSkyboxTextured but without shaders
    // This allows glPolygonMode(GL_LINE) to work
    // Unbind shader (should already be unbound, but be safe)
    glUseProgram(0);

    // Draw the skybox sphere geometry in wireframe mode
    // Use the same tessellation as the filled version
    drawSkyboxSphere(cameraPos, 0); // Pass 0 for texture (we don't need it for wireframe)
}
