#include "spice-ephemeris.h"
#include "constants.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <vector>


#ifdef HAS_CSPICE
// CSPICE headers (C library)
extern "C"
{
#include <SpiceUsr.h>
}
#endif

namespace fs = std::filesystem;

namespace SpiceEphemeris
{

// ==================================
// Module State
// ==================================
static bool g_initialized = false;
static std::string g_lastError;

// J2000 epoch in Julian Date (TDB)
static constexpr double J2000_JD = 2451545.0;

// Seconds per day
static constexpr double SECONDS_PER_DAY = 86400.0;

// Computed valid time range (intersection of all kernel coverages)
static double g_validStartET = -1e20;
static double g_validEndET = 1e20;
static double g_validStartJD = J2000_JD - 36525.0; // ~100 years before J2000
static double g_validEndJD = J2000_JD + 36525.0;   // ~100 years after J2000

// Track which bodies have data
static std::map<int, bool> g_bodyHasData;

// List of all bodies discovered with ephemeris data
static std::vector<BodyInfo> g_availableBodies;

#ifdef HAS_CSPICE

// List of loaded SPK files for coverage checking
static std::vector<std::string> g_loadedSpkFiles;

// ==================================
// Error Handling (CSPICE)
// ==================================

static void clearSpiceError()
{
    if (failed_c())
    {
        reset_c();
    }
}

static bool checkSpiceError(const char *context)
{
    if (failed_c())
    {
        char shortMsg[41];
        char longMsg[1841];
        getmsg_c("SHORT", 40, shortMsg);
        getmsg_c("LONG", 1840, longMsg);
        g_lastError = std::string(context) + ": " + shortMsg + " - " + longMsg;
        std::cerr << "SPICE Error: " << g_lastError << std::endl;
        reset_c();
        return true;
    }
    return false;
}

// Check coverage for a specific body across all loaded SPK files
static bool checkBodyCoverage(int naifId, double &startET, double &endET)
{
    SPICEDOUBLE_CELL(cover, 2000);

    startET = 1e20;
    endET = -1e20;
    bool found = false;

    for (const auto &spkFile : g_loadedSpkFiles)
    {
        scard_c(0, &cover);
        spkcov_c(spkFile.c_str(), naifId, &cover);

        if (failed_c())
        {
            reset_c();
            continue;
        }

        SpiceInt numIntervals = wncard_c(&cover);
        for (SpiceInt i = 0; i < numIntervals; i++)
        {
            SpiceDouble start, end;
            wnfetd_c(&cover, i, &start, &end);
            startET = std::min(startET, start);
            endET = std::max(endET, end);
            found = true;
        }
    }

    return found;
}

// Convert ET to calendar string for logging
static std::string etToDateString(double et)
{
    char dateStr[64];
    et2utc_c(et, "C", 0, 63, dateStr);
    if (failed_c())
    {
        reset_c();
        return "unknown";
    }
    return std::string(dateStr);
}

// ==================================
// Expected Kernels - these should all be present for full functionality
// ==================================
static const std::vector<std::string> g_expectedKernels = {
    "de440.bsp",    // Main planetary ephemeris (includes Moon)
    "jup365.bsp",   // Jupiter satellites
    "sat457.bsp",   // Saturn satellites
    "mar097s.bsp",  // Mars satellites (alternative name)
    "mar099s.bsp",  // Mars satellites
    "nep105.bsp",   // Neptune satellites
    "plu060.bsp",   // Pluto system
    "L1_de441.bsp", // Lagrange L1 point
    "L2_de441.bsp", // Lagrange L2 point
    "L4_de441.bsp", // Lagrange L4 point
    "L5_de441.bsp", // Lagrange L5 point
    "naif0012.tls", // Leap seconds
    "pck00010.tpc", // Planetary constants
    "pck00011.tpc", // Planetary constants (alternative)
};

// ==================================
// Initialization (CSPICE)
// ==================================

bool initialize(const std::string &kernelDir)
{
    if (g_initialized)
    {
        return true;
    }

    // Check if directory exists
    if (!fs::exists(kernelDir) || !fs::is_directory(kernelDir))
    {
        g_lastError = "Kernel directory not found: " + kernelDir;
        std::cerr << "SPICE: " << g_lastError << std::endl;
        return false;
    }

    std::cout << "SPICE: Loading kernels from " << kernelDir << "\n";
    std::cout << "  Absolute path: " << fs::absolute(kernelDir).string() << "\n";

    int kernelsLoaded = 0;
    bool hasSpk = false;
    bool hasLsk = false;
    g_loadedSpkFiles.clear();

    // Track which kernels we found
    std::vector<std::string> loadedKernelNames;

    // Iterate through directory and load kernel files
    for (const auto &entry : fs::directory_iterator(kernelDir))
    {
        if (!entry.is_regular_file())
            continue;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Load supported kernel types
        bool shouldLoad = (ext == ".bsp") || // SPK - Spacecraft/Planet Kernel
                          (ext == ".tls") || // LSK - Leap Seconds Kernel
                          (ext == ".tpc") || // PCK - Planetary Constants Kernel
                          (ext == ".tf") ||  // FK - Frame Kernel
                          (ext == ".pck");   // Binary PCK

        if (shouldLoad)
        {
            std::string pathStr = entry.path().string();
            std::string filename = entry.path().filename().string();

            furnsh_c(pathStr.c_str());

            if (checkSpiceError("furnsh_c"))
            {
                std::cerr << "SPICE: Failed to load kernel: " << pathStr << "\n";
                continue;
            }

            std::cout << "SPICE: Loaded " << filename << "\n";
            kernelsLoaded++;
            loadedKernelNames.push_back(filename);

            if (ext == ".bsp")
            {
                hasSpk = true;
                g_loadedSpkFiles.push_back(pathStr);
            }
            if (ext == ".tls")
                hasLsk = true;
        }
    }

    // Check for expected kernels and warn about missing ones
    std::cout << "\nSPICE: Verifying expected kernels...\n";
    std::vector<std::string> missingKernels;
    for (const auto &expected : g_expectedKernels)
    {
        bool found = false;
        for (const auto &loaded : loadedKernelNames)
        {
            // Case-insensitive comparison
            std::string loadedLower = loaded;
            std::string expectedLower = expected;
            std::transform(loadedLower.begin(), loadedLower.end(), loadedLower.begin(), ::tolower);
            std::transform(expectedLower.begin(), expectedLower.end(), expectedLower.begin(), ::tolower);
            if (loadedLower == expectedLower)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            missingKernels.push_back(expected);
        }
    }

    if (!missingKernels.empty())
    {
        std::cout << "SPICE: Note - Some optional kernels not found:\n";
        for (const auto &missing : missingKernels)
        {
            std::cout << "  - " << missing << "\n";
        }
    }
    else
    {
        std::cout << "SPICE: All expected kernels loaded successfully!\n";
    }

    if (!hasSpk)
    {
        g_lastError = "No SPK (ephemeris) kernels found in " + kernelDir;
        std::cerr << "SPICE: " << g_lastError << "\n";
        return false;
    }

    if (!hasLsk)
    {
        std::cerr << "SPICE: Warning - No LSK (leap seconds) kernel found.\n";
    }

    g_initialized = true;
    g_availableBodies.clear();
    std::cout << "SPICE: Initialized with " << kernelsLoaded << " kernel(s)\n";
    std::cout << "SPICE: Loaded " << g_loadedSpkFiles.size() << " SPK file(s) for ephemeris data\n";

    // ==================================
    // Discover all bodies with ephemeris data
    // ==================================
    std::cout << "\nSPICE: Discovering bodies with ephemeris data...\n";

    // Bodies to check (NAIF ID, name) - these are common solar system bodies
    struct BodyCheck
    {
        int naifId;
        const char *name;
    };

    BodyCheck bodiesToCheck[] = {
        // Sun
        {NAIF_SUN, "Sun"},
        // Planets (using barycenters for most, 399 for Earth to get Moon offset)
        {NAIF_MERCURY, "Mercury"},
        {NAIF_VENUS, "Venus"},
        {NAIF_EARTH, "Earth"},
        {NAIF_MARS, "Mars"},
        {NAIF_JUPITER, "Jupiter"},
        {NAIF_SATURN, "Saturn"},
        {NAIF_URANUS, "Uranus"},
        {NAIF_NEPTUNE, "Neptune"},
        {NAIF_PLUTO, "Pluto"},
        // Major moons
        {NAIF_MOON, "Moon"},
        {NAIF_IO, "Io"},
        {NAIF_EUROPA, "Europa"},
        {NAIF_GANYMEDE, "Ganymede"},
        {NAIF_CALLISTO, "Callisto"},
        {NAIF_TITAN, "Titan"},
        {NAIF_TRITON, "Triton"},
        {NAIF_CHARON, "Charon"},
    };

    double overallStart = -1e20;
    double overallEnd = 1e20;

    for (const auto &check : bodiesToCheck)
    {
        double startET, endET;
        if (checkBodyCoverage(check.naifId, startET, endET))
        {
            g_bodyHasData[check.naifId] = true;

            // Get radius from PCK if available
            double radiusKm = getBodyMeanRadius(check.naifId);

            // Add to available bodies list
            BodyInfo info;
            info.naifId = check.naifId;
            info.name = check.name;
            info.radiusKm = radiusKm;
            g_availableBodies.push_back(info);

            std::string startStr = etToDateString(startET);
            std::string endStr = etToDateString(endET);
            std::cout << "  " << check.name << " (ID " << check.naifId << "): " << startStr << " to " << endStr;
            if (radiusKm > 0)
            {
                std::cout << " [radius: " << radiusKm << " km]";
            }
            std::cout << "\n";

            // Compute intersection for planets (not moons) for valid time range
            if (check.naifId <= 10 || check.naifId == 399)
            {
                overallStart = std::max(overallStart, startET);
                overallEnd = std::min(overallEnd, endET);
            }
        }
        else
        {
            g_bodyHasData[check.naifId] = false;
            std::cout << "  " << check.name << " (ID " << check.naifId << "): NO COVERAGE\n";
        }
    }

    std::cout << "\nSPICE: Found " << g_availableBodies.size() << " bodies with ephemeris data\n";

    // ==================================
    // Verify Moon coverage specifically (common issue)
    // ==================================
    if (g_bodyHasData[NAIF_MOON])
    {
        std::cout << "\n=== MOON COVERAGE VERIFICATION ===\n";
        std::cout << "Moon (NAIF ID 301) has ephemeris data.\n";

        // Check which SPK files contain Moon data
        for (const auto &spkFile : g_loadedSpkFiles)
        {
            SPICEDOUBLE_CELL(cover, 2000);
            scard_c(0, &cover);
            spkcov_c(spkFile.c_str(), NAIF_MOON, &cover);

            if (!failed_c() && wncard_c(&cover) > 0)
            {
                std::string filename = fs::path(spkFile).filename().string();
                SpiceDouble start, end;
                wnfetd_c(&cover, 0, &start, &end);
                std::cout << "  Found in: " << filename << " (" << etToDateString(start) << " to "
                          << etToDateString(end) << ")\n";
            }
            else if (failed_c())
            {
                reset_c();
            }
        }
        std::cout << "==================================\n";
    }
    else
    {
        std::cerr << "\n=== WARNING: NO MOON COVERAGE! ===\n";
        std::cerr << "Moon (NAIF ID 301) has no ephemeris data!\n";
        std::cerr << "This will cause incorrect Moon positions.\n";
        std::cerr << "Make sure de440.bsp or similar kernel is loaded.\n";
        std::cerr << "==================================\n";
    }

    // Store computed valid range
    if (overallStart < overallEnd)
    {
        g_validStartET = overallStart;
        g_validEndET = overallEnd;
        g_validStartJD = etToJulian(overallStart);
        g_validEndJD = etToJulian(overallEnd);

        std::cout << "\n=== VALID TIME RANGE ===\n";
        std::cout << "Start: " << etToDateString(overallStart) << " (JD " << g_validStartJD << ")\n";
        std::cout << "End:   " << etToDateString(overallEnd) << " (JD " << g_validEndJD << ")\n";
        std::cout << "========================\n";
    }
    else
    {
        std::cerr << "SPICE: Warning - No common time range found!\n";
    }

    // ==================================
    // Test Moon position calculation
    // ==================================
    if (g_bodyHasData[NAIF_MOON] && g_bodyHasData[NAIF_EARTH])
    {
        std::cout << "\n=== MOON POSITION TEST (J2000.0) ===\n";

        // Test at J2000.0 epoch (January 1, 2000, 12:00 TT)
        double testJD = 2451545.0; // J2000.0

        glm::dvec3 moonPos, moonVel;
        glm::dvec3 earthPos, earthVel;

        if (getBodyState(NAIF_MOON, testJD, moonPos, moonVel) && getBodyState(NAIF_EARTH, testJD, earthPos, earthVel))
        {

            glm::dvec3 moonRelEarth = moonPos - earthPos;
            double distanceAU = glm::length(moonRelEarth);
            double distanceKm = distanceAU * 149597870.7;

            std::cout << "Moon position (AU from SSB): [" << moonPos.x << ", " << moonPos.y << ", " << moonPos.z
                      << "]\n";
            std::cout << "Earth position (AU from SSB): [" << earthPos.x << ", " << earthPos.y << ", " << earthPos.z
                      << "]\n";
            std::cout << "Moon-Earth distance: " << distanceKm << " km\n";
            std::cout << "  (Expected ~356,500 - 406,700 km, mean ~384,400 km)\n";

            // Sanity check - Moon should be roughly 384,400 km from Earth
            if (distanceKm < 300000 || distanceKm > 450000)
            {
                std::cerr << "WARNING: Moon distance seems incorrect!\n";
                std::cerr << "This may indicate a kernel loading issue.\n";
            }
            else
            {
                std::cout << "Moon distance is within expected range.\n";
            }
        }
        else
        {
            std::cerr << "WARNING: Failed to get Moon/Earth state at J2000.0!\n";
        }
        std::cout << "====================================\n\n";
    }

    return true;
}

void cleanup()
{
    if (g_initialized)
    {
        kclear_c();
        g_initialized = false;
        g_loadedSpkFiles.clear();
        g_bodyHasData.clear();
        g_availableBodies.clear();
    }
}

// ==================================
// Time Functions (CSPICE)
// ==================================

bool getTimeCoverage(int naifId, double &startJD, double &endJD)
{
    startJD = g_validStartJD;
    endJD = g_validEndJD;
    return g_initialized;
}

double getLatestAvailableTime()
{
    return g_validEndJD;
}

double getEarliestAvailableTime()
{
    return g_validStartJD;
}

double utcToTdbJulian(int year, int month, int day, int hour, int minute, double second)
{
    if (!g_initialized)
    {
        // Fallback to simple calculation without SPICE
        int a = (14 - month) / 12;
        int y = year + 4800 - a;
        int m = month + 12 * a - 3;
        double jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
        jd += (hour - 12) / 24.0 + minute / 1440.0 + second / 86400.0;
        return jd;
    }

    // Build UTC string
    char utcStr[64];
    snprintf(utcStr, sizeof(utcStr), "%04d-%02d-%02dT%02d:%02d:%06.3f", year, month, day, hour, minute, second);

    SpiceDouble et;
    str2et_c(utcStr, &et);

    if (checkSpiceError("str2et_c"))
    {
        // Fallback
        int a = (14 - month) / 12;
        int y = year + 4800 - a;
        int m = month + 12 * a - 3;
        double jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
        jd += (hour - 12) / 24.0 + minute / 1440.0 + second / 86400.0;
        return jd;
    }

    return etToJulian(et);
}

// ==================================
// Position Functions (CSPICE)
// ==================================

bool getBodyState(int naifId, double jdTdb, glm::dvec3 &position, glm::dvec3 &velocity)
{
    if (!g_initialized)
    {
        position = glm::dvec3(0.0);
        velocity = glm::dvec3(0.0);
        return false;
    }

    // Check if body has data
    auto it = g_bodyHasData.find(naifId);
    if (it != g_bodyHasData.end() && !it->second)
    {
        position = glm::dvec3(0.0);
        velocity = glm::dvec3(0.0);
        return false;
    }

    // Convert Julian Date to ephemeris time
    SpiceDouble et = julianToEt(jdTdb);

    // State vector: [x, y, z, vx, vy, vz] in km and km/s
    SpiceDouble state[6];
    SpiceDouble lt; // Light time (not used but required)

    // Get state relative to Solar System Barycenter in J2000 frame
    spkezr_c(std::to_string(naifId).c_str(), // Target body
             et,                             // Epoch (TDB)
             "J2000",                        // Reference frame
             "NONE",                         // Aberration correction (none for geometric)
             "0",                            // Observer: SSB (NAIF ID 0)
             state,                          // Output state
             &lt                             // Light time
    );

    if (failed_c())
    {
        // Don't spam errors - just return false
        reset_c();
        position = glm::dvec3(0.0);
        velocity = glm::dvec3(0.0);
        return false;
    }

    // Convert from km to AU
    constexpr double KM_PER_AU = 149597870.7;

    position.x = state[0] / KM_PER_AU;
    position.y = state[1] / KM_PER_AU;
    position.z = state[2] / KM_PER_AU;

    // Convert velocity from km/s to AU/day
    constexpr double KM_S_TO_AU_DAY = SECONDS_PER_DAY / KM_PER_AU;
    velocity.x = state[3] * KM_S_TO_AU_DAY;
    velocity.y = state[4] * KM_S_TO_AU_DAY;
    velocity.z = state[5] * KM_S_TO_AU_DAY;

    return true;
}

bool hasBodyData(int naifId)
{
    auto it = g_bodyHasData.find(naifId);
    return (it != g_bodyHasData.end() && it->second);
}

// ==================================
// Rotation/Orientation Functions (CSPICE)
// ==================================

glm::dvec3 getBodyPoleDirection(int naifId, double jdTdb)
{
    if (!g_initialized)
    {
        return glm::dvec3(0.0, 1.0, 0.0); // Default: ecliptic north
    }

    // Convert Julian Date to ephemeris time
    SpiceDouble et = julianToEt(jdTdb);

    // Get the transformation matrix from body-fixed frame to J2000
    // This gives us the orientation of the body
    SpiceDouble tipm[3][3];

    // Build the body-fixed frame name (e.g., "IAU_EARTH", "IAU_MARS")
    // For bodies, we use the body ID to get the frame
    char frameName[32];

    // Map NAIF IDs to IAU frame names
    const char *iauFrame = nullptr;
    switch (naifId)
    {
    case 10:
        iauFrame = "IAU_SUN";
        break;
    case 1:
    case 199:
        iauFrame = "IAU_MERCURY";
        break;
    case 2:
    case 299:
        iauFrame = "IAU_VENUS";
        break;
    case 3:
    case 399:
        iauFrame = "IAU_EARTH";
        break;
    case 301:
        iauFrame = "IAU_MOON";
        break;
    case 4:
    case 499:
        iauFrame = "IAU_MARS";
        break;
    case 5:
    case 599:
        iauFrame = "IAU_JUPITER";
        break;
    case 501:
        iauFrame = "IAU_IO";
        break;
    case 502:
        iauFrame = "IAU_EUROPA";
        break;
    case 503:
        iauFrame = "IAU_GANYMEDE";
        break;
    case 504:
        iauFrame = "IAU_CALLISTO";
        break;
    case 6:
    case 699:
        iauFrame = "IAU_SATURN";
        break;
    case 606:
        iauFrame = "IAU_TITAN";
        break;
    case 7:
    case 799:
        iauFrame = "IAU_URANUS";
        break;
    case 8:
    case 899:
        iauFrame = "IAU_NEPTUNE";
        break;
    case 801:
        iauFrame = "IAU_TRITON";
        break;
    case 9:
    case 999:
        iauFrame = "IAU_PLUTO";
        break;
    case 901:
        iauFrame = "IAU_CHARON";
        break;
    default:
        return glm::dvec3(0.0, 1.0, 0.0);
    }

    // Get the transformation from IAU body-fixed frame to J2000
    pxform_c(iauFrame, "J2000", et, tipm);

    if (failed_c())
    {
        reset_c();
        return glm::dvec3(0.0, 1.0, 0.0); // Fallback
    }

    // The Z-axis of the body-fixed frame is the rotation axis (north pole)
    // Transform [0, 0, 1] from body frame to J2000
    glm::dvec3 poleJ2000(tipm[0][2], // Z column of rotation matrix
                         tipm[1][2],
                         tipm[2][2]);

    return glm::normalize(poleJ2000);
}

// Helper function to get IAU frame name for a body
static const char *getIAUFrameName(int naifId)
{
    switch (naifId)
    {
    case 10:
        return "IAU_SUN";
    case 1:
    case 199:
        return "IAU_MERCURY";
    case 2:
    case 299:
        return "IAU_VENUS";
    case 3:
    case 399:
        return "IAU_EARTH";
    case 301:
        return "IAU_MOON";
    case 4:
    case 499:
        return "IAU_MARS";
    case 5:
    case 599:
        return "IAU_JUPITER";
    case 501:
        return "IAU_IO";
    case 502:
        return "IAU_EUROPA";
    case 503:
        return "IAU_GANYMEDE";
    case 504:
        return "IAU_CALLISTO";
    case 6:
    case 699:
        return "IAU_SATURN";
    case 606:
        return "IAU_TITAN";
    case 7:
    case 799:
        return "IAU_URANUS";
    case 8:
    case 899:
        return "IAU_NEPTUNE";
    case 801:
        return "IAU_TRITON";
    case 9:
    case 999:
        return "IAU_PLUTO";
    case 901:
        return "IAU_CHARON";
    default:
        return nullptr;
    }
}

glm::dvec3 getBodyPrimeMeridian(int naifId, double jdTdb)
{
    if (!g_initialized)
    {
        return glm::dvec3(1.0, 0.0, 0.0); // Fallback
    }

    double et = julianToEt(jdTdb);
    SpiceDouble tipm[3][3];

    const char *iauFrame = getIAUFrameName(naifId);
    if (!iauFrame)
    {
        return glm::dvec3(1.0, 0.0, 0.0);
    }

    pxform_c(iauFrame, "J2000", et, tipm);

    if (failed_c())
    {
        reset_c();
        return glm::dvec3(1.0, 0.0, 0.0);
    }

    // The X-axis of the body-fixed frame points to the prime meridian
    // Transform [1, 0, 0] from body frame to J2000
    glm::dvec3 pmJ2000(tipm[0][0], // X column of rotation matrix
                       tipm[1][0],
                       tipm[2][0]);

    return glm::normalize(pmJ2000);
}

bool getBodyFrame(int naifId, double jdTdb, glm::dvec3 &pole, glm::dvec3 &primeMeridian)
{
    if (!g_initialized)
    {
        pole = glm::dvec3(0.0, 1.0, 0.0);
        primeMeridian = glm::dvec3(1.0, 0.0, 0.0);
        return false;
    }

    double et = julianToEt(jdTdb);
    SpiceDouble tipm[3][3];

    const char *iauFrame = getIAUFrameName(naifId);
    if (!iauFrame)
    {
        pole = glm::dvec3(0.0, 1.0, 0.0);
        primeMeridian = glm::dvec3(1.0, 0.0, 0.0);
        return false;
    }

    pxform_c(iauFrame, "J2000", et, tipm);

    if (failed_c())
    {
        reset_c();
        pole = glm::dvec3(0.0, 1.0, 0.0);
        primeMeridian = glm::dvec3(1.0, 0.0, 0.0);
        return false;
    }

    // Z-axis = pole direction (north)
    pole = glm::normalize(glm::dvec3(tipm[0][2], tipm[1][2], tipm[2][2]));

    // X-axis = prime meridian direction
    primeMeridian = glm::normalize(glm::dvec3(tipm[0][0], tipm[1][0], tipm[2][0]));

    return true;
}

bool hasRotationData(int naifId)
{
    if (!g_initialized)
        return false;

    // Try to get pole data - if it succeeds, we have rotation data
    SpiceInt n;
    SpiceDouble values[3];

    // Map to body name for bodvrd
    const char *bodyName = nullptr;
    switch (naifId)
    {
    case 10:
        bodyName = "SUN";
        break;
    case 1:
    case 199:
        bodyName = "MERCURY";
        break;
    case 2:
    case 299:
        bodyName = "VENUS";
        break;
    case 3:
    case 399:
        bodyName = "EARTH";
        break;
    case 301:
        bodyName = "MOON";
        break;
    case 4:
    case 499:
        bodyName = "MARS";
        break;
    case 5:
    case 599:
        bodyName = "JUPITER";
        break;
    case 501:
        bodyName = "IO";
        break;
    case 502:
        bodyName = "EUROPA";
        break;
    case 503:
        bodyName = "GANYMEDE";
        break;
    case 504:
        bodyName = "CALLISTO";
        break;
    case 6:
    case 699:
        bodyName = "SATURN";
        break;
    case 606:
        bodyName = "TITAN";
        break;
    case 7:
    case 799:
        bodyName = "URANUS";
        break;
    case 8:
    case 899:
        bodyName = "NEPTUNE";
        break;
    case 801:
        bodyName = "TRITON";
        break;
    case 9:
    case 999:
        bodyName = "PLUTO";
        break;
    case 901:
        bodyName = "CHARON";
        break;
    default:
        return false;
    }

    bodvrd_c(bodyName, "POLE_RA", 3, &n, values);
    if (failed_c())
    {
        reset_c();
        return false;
    }
    return n > 0;
}

// ==================================
// Physical Constants from PCK
// ==================================

// Helper to get body name string for bodvrd
static const char *getBodyNameForId(int naifId)
{
    switch (naifId)
    {
    case NAIF_SUN:
        return "SUN";
    case 1:
    case 199:
        return "MERCURY";
    case 2:
    case 299:
        return "VENUS";
    case 3:
    case 399:
        return "EARTH";
    case NAIF_MOON:
        return "MOON";
    case 4:
    case 499:
        return "MARS";
    case 5:
    case 599:
        return "JUPITER";
    case 501:
        return "IO";
    case 502:
        return "EUROPA";
    case 503:
        return "GANYMEDE";
    case 504:
        return "CALLISTO";
    case 6:
    case 699:
        return "SATURN";
    case 606:
        return "TITAN";
    case 7:
    case 799:
        return "URANUS";
    case 8:
    case 899:
        return "NEPTUNE";
    case 801:
        return "TRITON";
    case 9:
    case 999:
        return "PLUTO";
    case 901:
        return "CHARON";
    default:
        return nullptr;
    }
}

glm::dvec3 getBodyRadii(int naifId)
{
    if (!g_initialized)
        return glm::dvec3(0.0);

    const char *bodyName = getBodyNameForId(naifId);
    if (!bodyName)
        return glm::dvec3(0.0);

    SpiceInt n;
    SpiceDouble radii[3];

    bodvrd_c(bodyName, "RADII", 3, &n, radii);
    if (failed_c())
    {
        reset_c();
        return glm::dvec3(0.0);
    }

    if (n >= 3)
    {
        // SPICE returns (equatorial_a, equatorial_b, polar)
        return glm::dvec3(radii[0], radii[1], radii[2]);
    }
    return glm::dvec3(0.0);
}

double getBodyMeanRadius(int naifId)
{
    glm::dvec3 radii = getBodyRadii(naifId);
    if (radii.x == 0.0)
        return 0.0;
    // Mean radius = (a + b + c) / 3
    return (radii.x + radii.y + radii.z) / 3.0;
}

double getBodyGM(int naifId)
{
    if (!g_initialized)
        return 0.0;

    const char *bodyName = getBodyNameForId(naifId);
    if (!bodyName)
        return 0.0;

    SpiceInt n;
    SpiceDouble gm[1];

    bodvrd_c(bodyName, "GM", 1, &n, gm);
    if (failed_c())
    {
        reset_c();
        return 0.0;
    }

    if (n >= 1)
    {
        return gm[0]; // km³/s²
    }
    return 0.0;
}

double getBodyMass(int naifId)
{
    double gm = getBodyGM(naifId);
    if (gm == 0.0)
        return 0.0;

    // G = 6.67430e-20 km³/(kg·s²)
    constexpr double G_KM3 = 6.67430e-20;
    return gm / G_KM3;
}

#else // !HAS_CSPICE

// ==================================
// Stub implementations when CSPICE is not available
// ==================================

bool initialize(const std::string &kernelDir)
{
    std::cout << "SPICE: Not available (compiled without CSPICE support)\n";
    return false;
}

void cleanup()
{
}

bool getTimeCoverage(int naifId, double &startJD, double &endJD)
{
    startJD = g_validStartJD;
    endJD = g_validEndJD;
    return false;
}

double getLatestAvailableTime()
{
    return g_validEndJD;
}
double getEarliestAvailableTime()
{
    return g_validStartJD;
}

double utcToTdbJulian(int year, int month, int day, int hour, int minute, double second)
{
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;
    double jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
    jd += (hour - 12) / 24.0 + minute / 1440.0 + second / 86400.0;
    return jd;
}

bool getBodyState(int naifId, double jdTdb, glm::dvec3 &position, glm::dvec3 &velocity)
{
    position = glm::dvec3(0.0);
    velocity = glm::dvec3(0.0);
    return false;
}

bool hasBodyData(int naifId)
{
    return false;
}

glm::dvec3 getBodyPoleDirection(int naifId, double jdTdb)
{
    return glm::dvec3(0.0, 1.0, 0.0); // Default: ecliptic north
}

bool hasRotationData(int naifId)
{
    return false;
}

glm::dvec3 getBodyRadii(int naifId)
{
    return glm::dvec3(0.0);
}
double getBodyMeanRadius(int naifId)
{
    return 0.0;
}
double getBodyGM(int naifId)
{
    return 0.0;
}
double getBodyMass(int naifId)
{
    return 0.0;
}

#endif // HAS_CSPICE

// ==================================
// Common Functions (always available)
// ==================================

bool isInitialized()
{
    return g_initialized;
}

double julianToEt(double jdTdb)
{
    return (jdTdb - J2000_JD) * SECONDS_PER_DAY;
}

double etToJulian(double et)
{
    return J2000_JD + et / SECONDS_PER_DAY;
}

glm::dvec3 getBodyPosition(int naifId, double jdTdb)
{
    glm::dvec3 pos, vel;
    if (getBodyState(naifId, jdTdb, pos, vel))
    {
        return pos;
    }
    return glm::dvec3(0.0);
}

std::string getLastError()
{
    return g_lastError;
}

std::vector<BodyInfo> getAvailableBodies()
{
    return g_availableBodies;
}

std::string getBodyName(int naifId)
{
    for (const auto &body : g_availableBodies)
    {
        if (body.naifId == naifId)
        {
            return body.name;
        }
    }
    // Fallback for well-known bodies not in g_availableBodies
    switch (naifId)
    {
    case NAIF_SSB:
        return "Solar System Barycenter";
    case NAIF_SUN:
        return "Sun";
    case NAIF_MERCURY:
        return "Mercury";
    case NAIF_VENUS:
        return "Venus";
    case NAIF_EARTH:
        return "Earth";
    case NAIF_MOON:
        return "Moon";
    case NAIF_MARS:
        return "Mars";
    case NAIF_JUPITER:
        return "Jupiter";
    case NAIF_SATURN:
        return "Saturn";
    case NAIF_URANUS:
        return "Uranus";
    case NAIF_NEPTUNE:
        return "Neptune";
    case NAIF_PLUTO:
        return "Pluto";
    case NAIF_IO:
        return "Io";
    case NAIF_EUROPA:
        return "Europa";
    case NAIF_GANYMEDE:
        return "Ganymede";
    case NAIF_CALLISTO:
        return "Callisto";
    case NAIF_TITAN:
        return "Titan";
    case NAIF_TRITON:
        return "Triton";
    case NAIF_CHARON:
        return "Charon";
    default:
        return "";
    }
}

} // namespace SpiceEphemeris
