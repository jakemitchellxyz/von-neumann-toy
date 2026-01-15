#include "stars-dynamic-skybox.h"
#include "constants.h"
#include "constellation-loader.h"
#include "settings.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

// stb_image for loading/saving textures (implementation in earth-material.cpp)
#include <stb_image.h>
#include <stb_image_write.h>

// GL_CLAMP_TO_EDGE may not be defined in basic Windows OpenGL headers
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// GL_GENERATE_MIPMAP may not be defined
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif

// ==================================
// Hipparcos Star Data
// ==================================
struct HipStar {
    int hipId;
    double raRad;       // Right Ascension at J1991.25 (radians)
    double decRad;      // Declination at J1991.25 (radians)
    double parallax;    // Parallax in mas
    double pmRA;        // Proper motion in RA (mas/yr)
    double pmDec;       // Proper motion in Dec (mas/yr)
    float magnitude;    // Hp magnitude
};

// ==================================
// Global State
// ==================================
static std::vector<HipStar> g_hipStars;
static std::vector<LoadedConstellation> g_loadedConstellations;
static bool g_skyboxInitialized = false;
static double g_referenceJD = 0.0;  // Reference epoch for star positions

// Star texture state
static GLuint g_starTexture = 0;
static bool g_starTextureReady = false;
static int g_starTextureWidth = 0;
static int g_starTextureHeight = 0;

// Hipparcos reference epoch: J1991.25 = JD 2448349.0625
static constexpr double HIP_EPOCH_JD = 2448349.0625;

// Maximum magnitude to display (lower = brighter, 6.5 is naked eye limit)
static constexpr float MAX_DISPLAY_MAGNITUDE = 7.0f;

// ==================================
// Hipparcos Catalog Loading
// ==================================

static bool loadHipparcosCatalog(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open Hipparcos catalog: " << filepath << std::endl;
        return false;
    }
    
    g_hipStars.clear();
    std::string line;
    int loadedCount = 0;
    int brightCount = 0;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::istringstream iss(line);
        
        // hip2.dat format (space-separated):
        // Col 1: HIP number
        // Col 2-4: Solution type flags  
        // Col 5: RA in radians (J1991.25)
        // Col 6: Dec in radians (J1991.25)
        // Col 7: Parallax (mas)
        // Col 8: PM in RA*cos(dec) (mas/yr)
        // Col 9: PM in Dec (mas/yr)
        // ... more columns ...
        // Col 21: Hp magnitude
        
        HipStar star;
        int dummy1, dummy2, dummy3;
        double dummy4, dummy5, dummy6, dummy7, dummy8, dummy9, dummy10;
        double dummy11, dummy12, dummy13;
        
        // Parse the space-separated format
        // hip2.dat columns:
        // 1: HIP, 2-4: flags, 5: RA, 6: Dec, 7: parallax, 8: pmRA, 9: pmDec
        // 10-14: errors, 15: Ntr, 16: F2, 17: var, 18: epoch, 19: ?, 20: Hp mag
        if (!(iss >> star.hipId >> dummy1 >> dummy2 >> dummy3 
                  >> star.raRad >> star.decRad >> star.parallax 
                  >> star.pmRA >> star.pmDec
                  >> dummy4 >> dummy5 >> dummy6 >> dummy7 >> dummy8  // cols 10-14 (errors)
                  >> dummy9 >> dummy10 >> dummy11 >> dummy12         // cols 15-18
                  >> dummy13                                         // col 19
                  >> star.magnitude)) {                              // col 20 = Hp magnitude
            continue;  // Skip malformed lines
        }
        
        loadedCount++;
        
        // Only keep stars bright enough to display
        if (star.magnitude <= MAX_DISPLAY_MAGNITUDE) {
            g_hipStars.push_back(star);
            brightCount++;
        }
    }
    
    // Count stars by magnitude range for verification
    int veryBright = 0, bright = 0, medium = 0, dim = 0;
    float minMag = 100.0f, maxMag = -100.0f;
    for (const auto& s : g_hipStars) {
        if (s.magnitude < minMag) minMag = s.magnitude;
        if (s.magnitude > maxMag) maxMag = s.magnitude;
        if (s.magnitude < 0) veryBright++;
        else if (s.magnitude < 2) bright++;
        else if (s.magnitude < 4) medium++;
        else dim++;
    }
    
    std::cout << "Hipparcos catalog: loaded " << brightCount << " bright stars (mag <= " 
              << MAX_DISPLAY_MAGNITUDE << ") from " << loadedCount << " total entries\n";
    std::cout << "  Magnitude range: " << minMag << " to " << maxMag << "\n";
    std::cout << "  Distribution: " << veryBright << " very bright (mag<0), " 
              << bright << " bright (0-2), " << medium << " medium (2-4), " 
              << dim << " dim (4+)" << std::endl;
    
    return !g_hipStars.empty();
}

// Propagate star position from J1991.25 to target Julian Date using proper motion
static void propagateStarPosition(const HipStar& star, double targetJD, double& raOut, double& decOut) {
    // Calculate time difference in years
    double deltaYears = (targetJD - HIP_EPOCH_JD) / 365.25;
    
    // Convert proper motion from mas/yr to radians/yr
    double pmRARad = (star.pmRA / 1000.0) * (PI / 180.0) / 3600.0;
    double pmDecRad = (star.pmDec / 1000.0) * (PI / 180.0) / 3600.0;
    
    // Apply proper motion (simplified, ignores cos(dec) correction already in pmRA)
    raOut = star.raRad + pmRARad * deltaYears / cos(star.decRad);
    decOut = star.decRad + pmDecRad * deltaYears;
    
    // Normalize RA to 0-2π
    while (raOut < 0) raOut += 2.0 * PI;
    while (raOut >= 2.0 * PI) raOut -= 2.0 * PI;
    
    // Clamp Dec to valid range
    if (decOut > PI / 2.0) decOut = PI / 2.0;
    if (decOut < -PI / 2.0) decOut = -PI / 2.0;
}

// ==================================
// Initialization
// ==================================

void InitializeSkybox(const std::string& defaultsPath) {
    if (g_skyboxInitialized) {
        return;
    }
    
    // Load Hipparcos catalog
    std::string catalogPath = defaultsPath + "/catalogues/hip2.dat";
    if (!loadHipparcosCatalog(catalogPath)) {
        std::cerr << "Warning: Failed to load Hipparcos catalog" << std::endl;
    }
    
    // Load constellation patterns from JSON5 files
    std::string constellationsPath = defaultsPath + "/constellations";
    g_loadedConstellations = loadConstellationsFromDirectory(constellationsPath);
    
    if (g_loadedConstellations.empty()) {
        std::cerr << "Warning: No constellations loaded from " << constellationsPath << std::endl;
    } else {
        std::cout << "Loaded " << g_loadedConstellations.size() << " constellation patterns" << std::endl;
    }
    
    g_skyboxInitialized = true;
}

bool IsSkyboxInitialized() {
    return g_skyboxInitialized;
}

// ==================================
// Helper Functions
// ==================================

// IAU 2006 obliquity of the ecliptic at J2000.0
// This is the angle between Earth's equator and the ecliptic plane
// Value: 84381.406 arcseconds = 23.4392911 degrees = 0.4090928042 radians
static const double OBLIQUITY_J2000_DEG = 23.4392911;
static const double OBLIQUITY_J2000_RAD = 0.4090928042223289;

// Convert Right Ascension and Declination (J2000 equatorial) to 3D Cartesian 
// coordinates in our ecliptic-aligned display system
// ra: in radians (0 to 2π)
// dec: in radians (-π/2 to π/2)
glm::vec3 raDecToCartesian(float ra, float dec, float radius) {
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
    float y_disp = static_cast<float>(z_ecl) * radius;   // Ecliptic Z -> Display Y (up)
    float z_disp = static_cast<float>(-y_ecl) * radius;  // Ecliptic Y -> Display -Z (right-handed)
    
    return glm::vec3(x_disp, y_disp, z_disp);
}

// Overload for hours/degrees (used by constellation loader)
glm::vec3 raDecToCartesianHours(float raHours, float decDeg, float radius) {
    float raRad = raHours * (2.0f * static_cast<float>(PI) / 24.0f);
    float decRad = glm::radians(decDeg);
    return raDecToCartesian(raRad, decRad, radius);
}

// Calculate Earth's rotation angle for the current Julian Date
float getEarthRotationAngle(double jd) {
    double T = (jd - JD_J2000) / 36525.0;
    double gmst = 280.46061837 + 360.98564736629 * (jd - JD_J2000) 
                  + 0.000387933 * T * T;
    gmst = fmod(gmst, 360.0);
    if (gmst < 0) gmst += 360.0;
    return static_cast<float>(gmst);
}

// ==================================
// Billboard Text Character Definitions
// ==================================

struct CharSegment {
    float x1, y1, x2, y2;
};

static const std::vector<CharSegment>& getCharSegments(char c) {
    static std::map<char, std::vector<CharSegment>> chars;
    static bool initialized = false;
    
    if (!initialized) {
        chars['A'] = {{0,0, 0.5f,1}, {0.5f,1, 1,0}, {0.2f,0.4f, 0.8f,0.4f}};
        chars['B'] = {{0,0, 0,1}, {0,1, 0.7f,1}, {0.7f,1, 0.7f,0.55f}, {0.7f,0.55f, 0,0.5f}, {0,0.5f, 0.7f,0.5f}, {0.7f,0.5f, 0.7f,0}, {0.7f,0, 0,0}};
        chars['C'] = {{1,0.2f, 0.3f,0}, {0.3f,0, 0,0.3f}, {0,0.3f, 0,0.7f}, {0,0.7f, 0.3f,1}, {0.3f,1, 1,0.8f}};
        chars['D'] = {{0,0, 0,1}, {0,1, 0.6f,1}, {0.6f,1, 1,0.7f}, {1,0.7f, 1,0.3f}, {1,0.3f, 0.6f,0}, {0.6f,0, 0,0}};
        chars['E'] = {{1,0, 0,0}, {0,0, 0,1}, {0,1, 1,1}, {0,0.5f, 0.7f,0.5f}};
        chars['F'] = {{0,0, 0,1}, {0,1, 1,1}, {0,0.5f, 0.7f,0.5f}};
        chars['G'] = {{1,0.8f, 0.3f,1}, {0.3f,1, 0,0.7f}, {0,0.7f, 0,0.3f}, {0,0.3f, 0.3f,0}, {0.3f,0, 1,0.2f}, {1,0.2f, 1,0.5f}, {1,0.5f, 0.5f,0.5f}};
        chars['H'] = {{0,0, 0,1}, {1,0, 1,1}, {0,0.5f, 1,0.5f}};
        chars['I'] = {{0.3f,0, 0.7f,0}, {0.5f,0, 0.5f,1}, {0.3f,1, 0.7f,1}};
        chars['J'] = {{0.2f,1, 0.8f,1}, {0.5f,1, 0.5f,0.2f}, {0.5f,0.2f, 0.3f,0}, {0.3f,0, 0,0.2f}};
        chars['K'] = {{0,0, 0,1}, {0,0.5f, 1,1}, {0.3f,0.65f, 1,0}};
        chars['L'] = {{0,1, 0,0}, {0,0, 1,0}};
        chars['M'] = {{0,0, 0,1}, {0,1, 0.5f,0.5f}, {0.5f,0.5f, 1,1}, {1,1, 1,0}};
        chars['N'] = {{0,0, 0,1}, {0,1, 1,0}, {1,0, 1,1}};
        chars['O'] = {{0.3f,0, 0,0.3f}, {0,0.3f, 0,0.7f}, {0,0.7f, 0.3f,1}, {0.3f,1, 0.7f,1}, {0.7f,1, 1,0.7f}, {1,0.7f, 1,0.3f}, {1,0.3f, 0.7f,0}, {0.7f,0, 0.3f,0}};
        chars['P'] = {{0,0, 0,1}, {0,1, 0.7f,1}, {0.7f,1, 1,0.75f}, {1,0.75f, 1,0.55f}, {1,0.55f, 0.7f,0.5f}, {0.7f,0.5f, 0,0.5f}};
        chars['Q'] = {{0.3f,0, 0,0.3f}, {0,0.3f, 0,0.7f}, {0,0.7f, 0.3f,1}, {0.3f,1, 0.7f,1}, {0.7f,1, 1,0.7f}, {1,0.7f, 1,0.3f}, {1,0.3f, 0.7f,0}, {0.7f,0, 0.3f,0}, {0.6f,0.3f, 1,0}};
        chars['R'] = {{0,0, 0,1}, {0,1, 0.7f,1}, {0.7f,1, 1,0.75f}, {1,0.75f, 1,0.55f}, {1,0.55f, 0.7f,0.5f}, {0.7f,0.5f, 0,0.5f}, {0.5f,0.5f, 1,0}};
        chars['S'] = {{1,0.8f, 0.3f,1}, {0.3f,1, 0,0.75f}, {0,0.75f, 0.3f,0.5f}, {0.3f,0.5f, 0.7f,0.5f}, {0.7f,0.5f, 1,0.25f}, {1,0.25f, 0.7f,0}, {0.7f,0, 0,0.2f}};
        chars['T'] = {{0,1, 1,1}, {0.5f,1, 0.5f,0}};
        chars['U'] = {{0,1, 0,0.3f}, {0,0.3f, 0.3f,0}, {0.3f,0, 0.7f,0}, {0.7f,0, 1,0.3f}, {1,0.3f, 1,1}};
        chars['V'] = {{0,1, 0.5f,0}, {0.5f,0, 1,1}};
        chars['W'] = {{0,1, 0.25f,0}, {0.25f,0, 0.5f,0.5f}, {0.5f,0.5f, 0.75f,0}, {0.75f,0, 1,1}};
        chars['X'] = {{0,0, 1,1}, {0,1, 1,0}};
        chars['Y'] = {{0,1, 0.5f,0.5f}, {1,1, 0.5f,0.5f}, {0.5f,0.5f, 0.5f,0}};
        chars['Z'] = {{0,1, 1,1}, {1,1, 0,0}, {0,0, 1,0}};
        chars[' '] = {};
        chars['-'] = {{0.2f,0.5f, 0.8f,0.5f}};
        chars['_'] = {{0,0, 1,0}};
        
        initialized = true;
    }
    
    static std::vector<CharSegment> empty;
    char upper = std::toupper(c);
    auto it = chars.find(upper);
    return (it != chars.end()) ? it->second : empty;
}

static void DrawBillboardText(const glm::vec3& pos, const std::string& text,
                              float size, const glm::vec3& right, const glm::vec3& up) {
    if (text.empty()) return;
    
    float charWidth = size * 0.7f;
    float charSpacing = size * 0.2f;
    float totalWidth = text.length() * (charWidth + charSpacing) - charSpacing;
    
    glm::vec3 startPos = pos - right * (totalWidth * 0.5f);
    
    glLineWidth(1.5f);
    glColor4f(0.8f, 0.8f, 0.85f, 0.7f);
    
    glBegin(GL_LINES);
    
    float currentX = 0.0f;
    for (char c : text) {
        const auto& segments = getCharSegments(c);
        glm::vec3 charOrigin = startPos + right * currentX;
        
        for (const auto& seg : segments) {
            glm::vec3 p1 = charOrigin + right * (seg.x1 * charWidth) + up * (seg.y1 * size);
            glm::vec3 p2 = charOrigin + right * (seg.x2 * charWidth) + up * (seg.y2 * size);
            glVertex3f(p1.x, p1.y, p1.z);
            glVertex3f(p2.x, p2.y, p2.z);
        }
        
        currentX += charWidth + charSpacing;
    }
    
    glEnd();
}

static glm::vec3 calculateConstellationCenter(const std::vector<glm::vec3>& starPositions) {
    if (starPositions.empty()) return glm::vec3(0.0f);
    
    glm::vec3 minPos = starPositions[0];
    glm::vec3 maxPos = starPositions[0];
    
    for (const auto& pos : starPositions) {
        minPos = glm::min(minPos, pos);
        maxPos = glm::max(maxPos, pos);
    }
    
    return (minPos + maxPos) * 0.5f;
}

static std::string formatConstellationName(const std::string& name) {
    std::string result;
    for (char c : name) {
        if (c == '_') {
            result += ' ';
        } else {
            result += std::toupper(c);
        }
    }
    return result;
}

// ==================================
// Skybox Rendering
// ==================================

void DrawSkybox(const glm::vec3& cameraPos, double jd,
                const glm::vec3& cameraFront, const glm::vec3& cameraUp) {
    if (!g_skyboxInitialized) {
        InitializeSkybox(getDefaultsPath());
    }
    
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    
    glPushMatrix();
    
    // Center skybox on camera
    glTranslatef(cameraPos.x, cameraPos.y, cameraPos.z);
    
    // Stars are now properly transformed from J2000 equatorial to ecliptic coordinates
    // in raDecToCartesian(), so no additional rotation is needed here.
    // The stars form a fixed inertial reference frame for the solar system.
    
    // ==================================
    // Draw stars from Hipparcos catalog
    // ==================================
    // Astronomical magnitude is logarithmic: each magnitude step = 2.512x brightness change
    // flux_ratio = 10^((reference_mag - star_mag) / 2.5)
    // We use mag 7.0 as our dimmest reference
    
    // Reference magnitude for brightness calculations
    const float REF_MAG = 7.0f;  // Dimmest star we show at minimum brightness
    const float MIN_BRIGHTNESS = 0.12f;  // Minimum color intensity
    const float MAX_BRIGHTNESS = 1.0f;   // Maximum color intensity
    
    // Calculate brightness from magnitude using proper logarithmic scale
    auto getMagnitudeBrightness = [](float mag) -> float {
        // Astronomical flux ratio: 10^((REF_MAG - mag) / 2.5)
        // Sirius (mag -1.46) vs mag 7 star: ~2500x brighter
        // We compress this logarithmically for display
        float fluxRatio = std::pow(10.0f, (7.0f - mag) / 2.5f);
        // Apply perceptual compression (cube root approximates human brightness perception)
        float brightness = std::pow(fluxRatio, 0.33f) / std::pow(2512.0f, 0.33f);
        return glm::clamp(0.12f + brightness * 0.88f, 0.12f, 1.0f);
    };
    
    // Get color temperature based on magnitude (simplified - real depends on spectral type)
    auto getStarColor = [&getMagnitudeBrightness](float mag, float& r, float& g, float& b) {
        float brightness = getMagnitudeBrightness(mag);
        
        if (mag < -0.5f) {
            // Very bright stars - blue-white (like Sirius, Vega)
            r = brightness * 0.92f;
            g = brightness * 0.96f;
            b = brightness;
        } else if (mag < 1.5f) {
            // Bright stars - white
            r = brightness;
            g = brightness * 0.98f;
            b = brightness * 0.95f;
        } else if (mag < 3.5f) {
            // Medium stars - slightly warm
            r = brightness;
            g = brightness * 0.95f;
            b = brightness * 0.88f;
        } else {
            // Dim stars - warmer orange tint
            r = brightness;
            g = brightness * 0.90f;
            b = brightness * 0.78f;
        }
    };
    
    glEnable(GL_POINT_SMOOTH);
    glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
    
    // Batch stars by magnitude ranges for efficient rendering
    // Each batch uses a single point size
    struct MagRange { float min, max, pointSize; };
    std::vector<MagRange> ranges = {
        {-2.0f, 0.0f, 6.0f},   // Very bright (Sirius, Canopus, etc.)
        {0.0f, 1.5f, 4.5f},    // Bright (Vega, Arcturus, etc.)
        {1.5f, 3.0f, 3.0f},    // Medium-bright
        {3.0f, 4.5f, 2.2f},    // Medium
        {4.5f, 5.5f, 1.6f},    // Dim
        {5.5f, 7.5f, 1.2f}     // Very dim
    };
    
    for (const auto& range : ranges) {
        glPointSize(range.pointSize);
        glBegin(GL_POINTS);
        
        for (const HipStar& star : g_hipStars) {
            if (star.magnitude >= range.min && star.magnitude < range.max) {
                float r, g, b;
                getStarColor(star.magnitude, r, g, b);
                glColor3f(r, g, b);
                
                double ra, dec;
                propagateStarPosition(star, jd, ra, dec);
                glm::vec3 pos = raDecToCartesian(static_cast<float>(ra), static_cast<float>(dec), SKYBOX_RADIUS);
                glVertex3f(pos.x, pos.y, pos.z);
            }
        }
        
        glEnd();
    }
    
    glDisable(GL_POINT_SMOOTH);
    
    // ==================================
    // Draw constellation lines and labels (if enabled)
    // ==================================
    if (g_showConstellations) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(1.0f);
        glColor4f(0.3f, 0.5f, 0.7f, 0.5f);
        
        glBegin(GL_LINES);
        
        for (const LoadedConstellation& constellation : g_loadedConstellations) {
            const auto& stars = constellation.graphStars;
            
            for (size_t i = 0; i < stars.size(); i++) {
                for (size_t j = i + 1; j < stars.size(); j++) {
                    int rowDiff = std::abs(stars[i].row - stars[j].row);
                    int colDiff = std::abs(stars[i].col - stars[j].col);
                    
                    if (rowDiff <= 2 && colDiff <= 6) {
                        float ra1, dec1, ra2, dec2;
                        graphToRaDec(constellation, stars[i].row, stars[i].col, ra1, dec1);
                        graphToRaDec(constellation, stars[j].row, stars[j].col, ra2, dec2);
                        
                        glm::vec3 pos1 = raDecToCartesianHours(ra1, dec1, SKYBOX_RADIUS * 0.997f);
                        glm::vec3 pos2 = raDecToCartesianHours(ra2, dec2, SKYBOX_RADIUS * 0.997f);
                        
                        glVertex3f(pos1.x, pos1.y, pos1.z);
                        glVertex3f(pos2.x, pos2.y, pos2.z);
                    }
                }
            }
        }
        
        glEnd();
        
        // ==================================
        // Draw constellation labels
        // ==================================
        // Since we no longer apply rotation transforms, use camera vectors directly
        glm::vec3 localCameraRight = glm::normalize(glm::cross(cameraFront, cameraUp));
        glm::vec3 localCameraUp = glm::normalize(glm::cross(localCameraRight, cameraFront));
        
        float labelSize = SKYBOX_RADIUS * 0.012f;
        
        for (const LoadedConstellation& constellation : g_loadedConstellations) {
            if (constellation.graphStars.empty()) continue;
            
            std::vector<glm::vec3> starPositions;
            for (const GraphStar& graphStar : constellation.graphStars) {
                float ra, dec;
                graphToRaDec(constellation, graphStar.row, graphStar.col, ra, dec);
                glm::vec3 pos = raDecToCartesianHours(ra, dec, SKYBOX_RADIUS * 0.99f);
                starPositions.push_back(pos);
            }
            
            glm::vec3 center = calculateConstellationCenter(starPositions);
            center = glm::normalize(center) * (SKYBOX_RADIUS * 0.995f);
            center += localCameraUp * labelSize * 0.5f;
            
            std::string displayName = formatConstellationName(constellation.name);
            DrawBillboardText(center, displayName, labelSize, localCameraRight, localCameraUp);
        }
        
        glDisable(GL_BLEND);
    }
    
    glPopMatrix();
    
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

// ==================================
// Star Texture Generation
// ==================================

// Always use 8K for star accuracy (8192x4096)
static const int STAR_TEXTURE_WIDTH = 8192;
static const int STAR_TEXTURE_HEIGHT = 4096;

// Convert ecliptic longitude/latitude to UV coordinates for equirectangular projection
static glm::vec2 eclipticToUV(double lon, double lat) {
    // Longitude: 0 to 2π maps to U: 0 to 1
    // Latitude: -π/2 to π/2 maps to V: 1 to 0 (flipped for image coordinates)
    double u = lon / (2.0 * PI);
    double v = 0.5 - lat / PI;  // Map [-π/2, π/2] to [1, 0]
    return glm::vec2(static_cast<float>(u), static_cast<float>(v));
}

// Draw a star at the given ecliptic coordinates into the texture buffer
// Accounts for equirectangular projection distortion (cos(latitude) factor)
static void drawStar(std::vector<float>& buffer, int width, int height,
                     double eclLon, double eclLat, float brightness, int magnitudeClass) {
    // Convert to UV
    glm::vec2 uv = eclipticToUV(eclLon, eclLat);
    
    // Convert UV to pixel coordinates
    int centerX = static_cast<int>(uv.x * width) % width;
    int centerY = static_cast<int>(uv.y * height);
    
    // Clamp Y to valid range
    if (centerY < 0) centerY = 0;
    if (centerY >= height) centerY = height - 1;
    
    // Calculate the cos(latitude) distortion factor for equirectangular
    // Near poles, horizontal distances are stretched, so we need to compress our drawing
    double cosLat = std::cos(eclLat);
    if (cosLat < 0.01) cosLat = 0.01;  // Avoid division by zero at poles
    
    // Star rendering: real stars are point sources
    // Only the very brightest stars (mag < 0) get a tiny halo
    // Most stars are single pixels
    
    // Determine star size based on magnitude class:
    // 0 = very bright (mag < 0): 2 pixel radius with subtle halo
    // 1 = bright (mag 0-2): 1-2 pixel radius
    // 2 = medium (mag 2-4): 1 pixel
    // 3 = dim (mag 4+): 1 pixel, dimmer
    
    int baseRadius;
    switch (magnitudeClass) {
        case 0: baseRadius = 2; break;  // Very bright
        case 1: baseRadius = 1; break;  // Bright
        default: baseRadius = 0; break; // Medium and dim are single pixels
    }
    
    // For single pixel stars (baseRadius = 0), just set the pixel
    if (baseRadius == 0) {
        int idx = (centerY * width + centerX) * 3;
        buffer[idx + 0] += brightness;
        buffer[idx + 1] += brightness;
        buffer[idx + 2] += brightness;
        return;
    }
    
    // For larger stars, draw with projection-corrected extent
    // The horizontal extent needs to be divided by cos(lat) to appear circular on the sphere
    int radiusY = baseRadius;
    int radiusX = static_cast<int>(std::ceil(baseRadius / cosLat));
    radiusX = std::min(radiusX, 50);  // Cap to avoid extreme values at poles
    
    // Draw with Gaussian PSF (Point Spread Function)
    float sigma = static_cast<float>(baseRadius) * 0.6f;
    
    for (int dy = -radiusY; dy <= radiusY; ++dy) {
        for (int dx = -radiusX; dx <= radiusX; ++dx) {
            int px = (centerX + dx + width) % width;  // Wrap horizontally
            int py = centerY + dy;
            
            if (py < 0 || py >= height) continue;
            
            // Calculate angular distance accounting for projection
            // dx pixels = dx * cos(lat) in angular terms
            float angularDx = dx * static_cast<float>(cosLat);
            float dist = std::sqrt(angularDx * angularDx + static_cast<float>(dy * dy));
            
            // Gaussian PSF
            float falloff = std::exp(-(dist * dist) / (2.0f * sigma * sigma));
            
            int idx = (py * width + px) * 3;
            float intensity = brightness * falloff;
            buffer[idx + 0] += intensity;
            buffer[idx + 1] += intensity;
            buffer[idx + 2] += intensity;
        }
    }
}

int GenerateStarTexture(const std::string& defaultsPath,
                        const std::string& outputPath,
                        TextureResolution resolution,
                        double jd) {
    // Initialize skybox first to load star catalog
    if (!g_skyboxInitialized) {
        InitializeSkybox(defaultsPath);
    }
    
    if (g_hipStars.empty()) {
        std::cerr << "No stars loaded, cannot generate star texture" << std::endl;
        return -1;
    }
    
    // Always use 8K for accurate star rendering
    const int width = STAR_TEXTURE_WIDTH;
    const int height = STAR_TEXTURE_HEIGHT;
    
    std::cout << "=== Star Texture Generation ===" << std::endl;
    std::cout << "Resolution: " << width << "x" << height << " (8K fixed for accuracy)" << std::endl;
    std::cout << "Stars to render: " << g_hipStars.size() << std::endl;
    
    // Create output directory - always use "8k" folder
    std::string outputDir = outputPath + "/8k";
    std::filesystem::create_directories(outputDir);
    
    std::string outputFile = outputDir + "/stars.png";  // PNG for quality
    
    // Check if already exists
    if (std::filesystem::exists(outputFile)) {
        std::cout << "Star texture already exists: " << outputFile << std::endl;
        return static_cast<int>(g_hipStars.size());
    }
    
    // Create HDR buffer (float for accumulation - allows multiple stars to add up)
    std::vector<float> hdrBuffer(width * height * 3, 0.0f);
    
    // Astronomical magnitude reference: 
    // Sirius = -1.46 mag (brightest star)
    // Limit of naked eye = ~6.5 mag
    // Hipparcos catalog goes to ~7 mag
    const float REF_MAG = 7.0f;  // Reference (dimmest we show)
    
    // Render each star
    int starsRendered = 0;
    for (const HipStar& star : g_hipStars) {
        // Propagate star position to target date
        double ra, dec;
        propagateStarPosition(star, jd, ra, dec);
        
        // Transform J2000 equatorial -> J2000 ecliptic coordinates
        double x_eq = cos(dec) * cos(ra);
        double y_eq = cos(dec) * sin(ra);
        double z_eq = sin(dec);
        
        double cosObl = cos(OBLIQUITY_J2000_RAD);
        double sinObl = sin(OBLIQUITY_J2000_RAD);
        
        double x_ecl = x_eq;
        double y_ecl = cosObl * y_eq + sinObl * z_eq;
        double z_ecl = -sinObl * y_eq + cosObl * z_eq;
        
        // Convert ecliptic Cartesian back to spherical (longitude, latitude)
        double eclLon = std::atan2(y_ecl, x_ecl);  // -π to π
        double eclLat = std::asin(z_ecl);           // -π/2 to π/2
        
        // Normalize longitude to 0-2π
        if (eclLon < 0) eclLon += 2.0 * PI;
        
        // Calculate brightness from magnitude using astronomical formula
        // Flux ratio = 10^((ref_mag - star_mag) / 2.5)
        // Sirius (-1.46) vs mag 7: ratio = 10^(8.46/2.5) = ~2400x
        float fluxRatio = std::pow(10.0f, (REF_MAG - star.magnitude) / 2.5f);
        
        // Apply perceptual compression - human eye responds logarithmically
        // Use fourth root for reasonable dynamic range in output
        float brightness = std::pow(fluxRatio, 0.25f);  
        brightness = brightness * 0.3f;  // Scale down so dim stars are ~0.3, bright ~3.0
        
        // Determine magnitude class for star size
        int magClass;
        if (star.magnitude < 0) {
            magClass = 0;       // Very bright (Sirius, Canopus, etc.) - 2px radius
        } else if (star.magnitude < 2) {
            magClass = 1;       // Bright (Vega, Arcturus, etc.) - 1px radius  
        } else if (star.magnitude < 4) {
            magClass = 2;       // Medium - single pixel
        } else {
            magClass = 3;       // Dim - single pixel, dimmer
        }
        
        // Draw the star with proper projection correction
        drawStar(hdrBuffer, width, height, eclLon, eclLat, brightness, magClass);
        starsRendered++;
    }
    
    std::cout << "Stars rendered: " << starsRendered << std::endl;
    
    // Tone map HDR buffer to LDR (0-255)
    std::vector<unsigned char> ldrBuffer(width * height * 3);
    
    // Find max value for normalization reference
    float maxVal = 0.0f;
    for (float v : hdrBuffer) maxVal = std::max(maxVal, v);
    std::cout << "Max HDR value: " << maxVal << std::endl;
    
    // Apply simple tone mapping: clamp with slight boost for visibility
    for (int i = 0; i < width * height * 3; ++i) {
        float hdr = hdrBuffer[i];
        // Simple S-curve for better contrast
        float ldr = hdr / (0.5f + hdr);  // Soft knee at 0.5
        // Boost overall brightness slightly
        ldr *= 1.5f;
        // Clamp
        ldr = std::min(ldr, 1.0f);
        ldrBuffer[i] = static_cast<unsigned char>(std::min(255.0f, ldr * 255.0f));
    }
    
    // Save as PNG (lossless for star quality)
    int result = stbi_write_png(outputFile.c_str(), width, height, 3, ldrBuffer.data(), width * 3);
    
    if (result) {
        std::cout << "Star texture saved: " << outputFile << std::endl;
    } else {
        std::cerr << "Failed to save star texture: " << outputFile << std::endl;
        return -1;
    }
    
    return starsRendered;
}

bool InitializeStarTextureMaterial(const std::string& texturePath, TextureResolution /* resolution */) {
    if (g_starTextureReady) {
        return true;
    }
    
    // Always use 8K star texture for accuracy
    std::string filepath = texturePath + "/8k/stars.png";
    
    std::cout << "Loading star texture: " << filepath << std::endl;
    
    // Load the texture
    int width, height, channels;
    unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 3);
    
    if (!data) {
        std::cerr << "Failed to load star texture: " << filepath << std::endl;
        return false;
    }
    
    // Generate OpenGL texture
    glGenTextures(1, &g_starTexture);
    glBindTexture(GL_TEXTURE_2D, g_starTexture);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Upload texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    
    stbi_image_free(data);
    
    g_starTextureWidth = width;
    g_starTextureHeight = height;
    g_starTextureReady = true;
    
    std::cout << "Star texture loaded: " << width << "x" << height << std::endl;
    
    return true;
}

bool IsStarTextureReady() {
    return g_starTextureReady;
}

void DrawSkyboxTextured(const glm::vec3& cameraPos) {
    if (!g_starTextureReady) {
        return;
    }
    
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glEnable(GL_TEXTURE_2D);
    
    glBindTexture(GL_TEXTURE_2D, g_starTexture);
    
    // Set emissive material (self-luminous, not affected by lighting)
    GLfloat emission[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    GLfloat black[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, emission);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, black);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, black);
    
    glColor3f(1.0f, 1.0f, 1.0f);
    
    glPushMatrix();
    glTranslatef(cameraPos.x, cameraPos.y, cameraPos.z);
    
    // Draw textured sphere (inside-out for skybox)
    // High resolution for smooth rendering of 8K texture
    const int slices = 128;
    const int stacks = 64;
    const float radius = SKYBOX_RADIUS;
    
    // UV mapping must match texture generation (eclipticToUV):
    // U = eclLon / 2π  (longitude 0 to 2π maps to U 0 to 1)
    // V = 0.5 - eclLat / π  (north pole V=0, equator V=0.5, south pole V=1)
    
    for (int i = 0; i < stacks; ++i) {
        // phi is latitude from -π/2 (south) to +π/2 (north)
        float phi1 = static_cast<float>(PI) * (-0.5f + static_cast<float>(i) / stacks);
        float phi2 = static_cast<float>(PI) * (-0.5f + static_cast<float>(i + 1) / stacks);
        
        float y1 = radius * sin(phi1);
        float y2 = radius * sin(phi2);
        float r1 = radius * cos(phi1);
        float r2 = radius * cos(phi2);
        
        // V coordinates - must match eclipticToUV: v = 0.5 - lat/π
        // At phi = -π/2 (south): v = 0.5 + 0.5 = 1
        // At phi = +π/2 (north): v = 0.5 - 0.5 = 0
        float v1 = 0.5f - phi1 / static_cast<float>(PI);
        float v2 = 0.5f - phi2 / static_cast<float>(PI);
        
        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; ++j) {
            // theta is longitude from 0 to 2π
            float theta = 2.0f * static_cast<float>(PI) * static_cast<float>(j) / slices;
            float cosTheta = cos(theta);
            float sinTheta = sin(theta);
            
            // U coordinate - must match eclipticToUV: u = eclLon / 2π
            // Theta=0 should map to U=0 (vernal equinox direction: +X)
            // For inside view, we need to reverse since we're looking inward
            float u = 1.0f - static_cast<float>(j) / slices;
            
            // Convert from spherical to Cartesian:
            // In ecliptic: X = r*cos(lat)*cos(lon), Y = r*cos(lat)*sin(lon), Z = r*sin(lat)
            // In display (Y-up): X_disp = X_ecl, Y_disp = Z_ecl, Z_disp = -Y_ecl
            
            // First vertex (at phi1)
            float x1 = r1 * cosTheta;                  // ecliptic X
            float z1_ecl = r1 * sinTheta;              // ecliptic Y
            glTexCoord2f(u, v1);
            glNormal3f(-cosTheta * cos(phi1), -sin(phi1), sinTheta * cos(phi1));  // Inward normal
            glVertex3f(x1, y1, -z1_ecl);               // Y_disp = Y_ecl_z = y1, Z_disp = -Y_ecl
            
            // Second vertex (at phi2)
            float x2 = r2 * cosTheta;
            float z2_ecl = r2 * sinTheta;
            glTexCoord2f(u, v2);
            glNormal3f(-cosTheta * cos(phi2), -sin(phi2), sinTheta * cos(phi2));
            glVertex3f(x2, y2, -z2_ecl);
        }
        glEnd();
    }
    
    glPopMatrix();
    
    // Reset material
    GLfloat noEmission[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, noEmission);
    
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}
