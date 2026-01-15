#include "constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// ==================================
// Physics Constants
// ==================================
const double G = 6.6743e-11; // Gravitational constant (m^3 / (kg * s^2))
const double PI = 3.141592653589793;
const double AU_IN_METERS = 1.495978707e11; // 1 AU in meters
const double DAY_IN_SECONDS = 86400.0;

// ==================================
// Celestial Body Masses (kg)
// ==================================
// Planets
const double MASS_SUN = 1.989e30;
const double MASS_MERCURY = 3.30e23;
const double MASS_VENUS = 4.87e24;
const double MASS_EARTH = 5.972e24;
const double MASS_MARS = 6.42e23;
const double MASS_JUPITER = 1.898e27;
const double MASS_SATURN = 5.683e26;
const double MASS_URANUS = 8.681e25;
const double MASS_NEPTUNE = 1.024e26;
const double MASS_PLUTO = 1.31e22;

// Moons
const double MASS_MOON = 7.35e22;
const double MASS_IO = 8.93e22;
const double MASS_EUROPA = 4.80e22;
const double MASS_GANYMEDE = 1.48e23;
const double MASS_CALLISTO = 1.08e23;
const double MASS_TITAN = 1.35e23;
const double MASS_TRITON = 2.14e22;
const double MASS_CHARON = 1.59e21;

// ==================================
// Celestial Body Radii (km)
// ==================================
// Planets
const double RADIUS_SUN_KM = 696340.0;
const double RADIUS_MERCURY_KM = 2439.7;
const double RADIUS_VENUS_KM = 6051.8;
const double RADIUS_EARTH_KM = 6371.0;
const double RADIUS_MARS_KM = 3389.5;
const double RADIUS_JUPITER_KM = 69911.0;
const double RADIUS_SATURN_KM = 58232.0;
const double RADIUS_URANUS_KM = 25362.0;
const double RADIUS_NEPTUNE_KM = 24622.0;
const double RADIUS_PLUTO_KM = 1188.3;

// Moons
const double RADIUS_MOON_KM = 1737.4;
const double RADIUS_IO_KM = 1821.6;
const double RADIUS_EUROPA_KM = 1560.8;
const double RADIUS_GANYMEDE_KM = 2634.1;
const double RADIUS_CALLISTO_KM = 2410.3;
const double RADIUS_TITAN_KM = 2574.7;
const double RADIUS_TRITON_KM = 1353.4;
const double RADIUS_CHARON_KM = 606.0;

// ==================================
// Moon Orbital Parameters
// ==================================
// Semi-major axes (converted from km to AU)
const double IO_SMA_AU = 421800.0 / AU_IN_METERS * 1000.0;
const double EUROPA_SMA_AU = 671100.0 / AU_IN_METERS * 1000.0;
const double GANYMEDE_SMA_AU = 1070400.0 / AU_IN_METERS * 1000.0;
const double CALLISTO_SMA_AU = 1882700.0 / AU_IN_METERS * 1000.0;
const double TITAN_SMA_AU = 1221870.0 / AU_IN_METERS * 1000.0;
const double TRITON_SMA_AU = 354800.0 / AU_IN_METERS * 1000.0;
const double CHARON_SMA_AU = 19591.0 / AU_IN_METERS * 1000.0;
const double LUNA_SMA_AU = 384400.0 / AU_IN_METERS * 1000.0;

// Orbital periods (days)
const double IO_PERIOD = 1.769;
const double EUROPA_PERIOD = 3.551;
const double GANYMEDE_PERIOD = 7.155;
const double CALLISTO_PERIOD = 16.69;
const double TITAN_PERIOD = 15.95;
const double TRITON_PERIOD = 5.877; // Retrograde, but we'll use positive
const double CHARON_PERIOD = 6.387;
const double LUNA_PERIOD = 27.322;

// ==================================
// Planetary Orbital Semi-major Axes (AU)
// ==================================
const double MERCURY_SMA_AU = 0.387;
const double VENUS_SMA_AU = 0.723;
const double EARTH_SMA_AU = 1.000;
const double MARS_SMA_AU = 1.524;
const double JUPITER_SMA_AU = 5.203;
const double SATURN_SMA_AU = 9.537;
const double URANUS_SMA_AU = 19.19;
const double NEPTUNE_SMA_AU = 30.07;
const double PLUTO_SMA_AU = 39.48;
const double PLUTO_PERIOD_DAYS = 90560.0; // ~248 years

// ==================================
// Planetary Axial Tilts (degrees from ecliptic normal)
// ==================================
// These are the obliquity angles - tilt of rotation axis from perpendicular to orbital plane
const float MERCURY_AXIAL_TILT = 0.034f; // Nearly perpendicular
const float VENUS_AXIAL_TILT = 177.4f;   // Retrograde rotation (nearly upside down)
const float EARTH_AXIAL_TILT = 23.44f;   // Causes seasons
const float MARS_AXIAL_TILT = 25.19f;    // Similar to Earth
const float JUPITER_AXIAL_TILT = 3.13f;  // Nearly upright
const float SATURN_AXIAL_TILT = 26.73f;  // Notable tilt
const float URANUS_AXIAL_TILT = 97.77f;  // Extreme - rotates on its side
const float NEPTUNE_AXIAL_TILT = 28.32f; // Similar to Saturn
const float PLUTO_AXIAL_TILT = 122.53f;  // Retrograde, highly tilted

// ==================================
// Rotation Periods (sidereal day in hours)
// ==================================
const double SUN_ROTATION_HOURS = 609.12;     // ~25.38 days at equator
const double MERCURY_ROTATION_HOURS = 1407.6; // ~58.6 days
const double VENUS_ROTATION_HOURS = 5832.5;   // ~243 days (retrograde)
const double EARTH_ROTATION_HOURS = 23.9345;  // ~24 hours (sidereal day)
const double MARS_ROTATION_HOURS = 24.6229;   // ~24.6 hours
const double JUPITER_ROTATION_HOURS = 9.925;  // ~10 hours (fastest planet)
const double SATURN_ROTATION_HOURS = 10.656;  // ~10.7 hours
const double URANUS_ROTATION_HOURS = 17.24;   // ~17.2 hours (retrograde)
const double NEPTUNE_ROTATION_HOURS = 16.11;  // ~16.1 hours
const double PLUTO_ROTATION_HOURS = 153.3;    // ~6.4 days (retrograde)
const double MOON_ROTATION_HOURS = 655.7;     // ~27.3 days (tidally locked)

// ==================================
// Time Constants
// ==================================
const double JD_J2000 = 2451545.0;              // J2000.0 = January 1, 2000, 12:00 TT
const double DAYS_PER_TROPICAL_YEAR = 365.2425; // Days per tropical year (for year fraction calculations)
// MONTHS_PER_YEAR is now defined as constexpr in constants.h

// ==================================
// Earth Atmospheric Constants
// ==================================
const double KARMAN_LINE_KM = 100.0;           // Kármán line: boundary of space (100 km above Earth's surface)
const double SCATTERING_ATMOSPHERE_KM = 100.0; // Optically significant atmosphere height (same as Kármán line, 100 km)

// ==================================
// Visualization Scale Factors
// ==================================
// UNITS_PER_AU must be large enough that Sun's radius (164 units) < Mercury perihelion (0.307 AU)
// At 600 units/AU: Mercury perihelion = 184 units, safely outside Sun's 164 unit radius
const float UNITS_PER_AU = 600.0f;
const float MOON_DISTANCE_SCALE = 50.0f; // Exaggerate moon distances for visibility
const float EARTH_DISPLAY_RADIUS = 1.5f; // Earth = 1.5 display units baseline
const float MIN_DISPLAY_RADIUS = 0.15f;  // Minimum size so tiny moons are visible
const float SKYBOX_RADIUS = 50000.0f;    // Large sphere encompassing the solar system

// ==================================
// Render Settings
// ==================================
bool g_showOrbits = true;
bool g_showRotationAxes = true;
bool g_showBarycenters = false;
bool g_showLagrangePoints = false;
bool g_showCoordinateGrids = false;
bool g_showMagneticFields = false;
bool g_showGravityGrid = false;
int g_gravityGridResolution = 25;
float g_gravityWarpStrength = 1.0f;
bool g_showConstellations = false;
bool g_showForceVectors = false;
bool g_showAtmosphereLayers = false;
bool g_showSunSpot = true;
bool g_enableAtmosphere = true;   // Default disabled
bool g_useAtmosphereLUT = true;   // Default enabled (use LUT for better performance)
bool g_useMultiscatterLUT = true; // Default enabled (use multiscatter LUT for better quality)

// ==================================
// Helper Functions
// ==================================
float getDisplayRadius(double realRadiusKm)
{
    float ratio = static_cast<float>(realRadiusKm / RADIUS_EARTH_KM);
    float displayRadius;

    // Use sub-linear scaling for large bodies to keep them proportional to distances
    // Without this, Sun (109x Earth) would appear 164 units when 1 AU = 600 units
    // That makes it appear 3.7x its radius away instead of 215x
    //
    // Threshold: bodies larger than 10x Earth use sqrt scaling
    // This keeps terrestrial planets accurate while shrinking giants
    const float LARGE_BODY_THRESHOLD = 10.0f;

    if (ratio > LARGE_BODY_THRESHOLD)
    {
        // sqrt scaling: Sun (109x) -> 10 + sqrt(99) ≈ 20x Earth instead of 109x
        // This makes Sun ~30 units instead of 164, more proportional to 600 AU distance
        float excess = ratio - LARGE_BODY_THRESHOLD;
        float scaledExcess = std::sqrt(excess);
        displayRadius = EARTH_DISPLAY_RADIUS * (LARGE_BODY_THRESHOLD + scaledExcess);
    }
    else
    {
        // Linear scaling for terrestrial planets and moons
        displayRadius = EARTH_DISPLAY_RADIUS * ratio;
    }

    return std::max(displayRadius, MIN_DISPLAY_RADIUS);
}

// ==================================
// Star Catalog - Bright Stars for Constellations (J2000 coordinates)
// ==================================
const std::vector<Star> BRIGHT_STARS = {
    // Ursa Major (Big Dipper)
    {11.062f, 61.75f, 1.79f, "Dubhe"},
    {11.031f, 56.38f, 2.37f, "Merak"},
    {11.897f, 53.69f, 2.44f, "Phecda"},
    {12.257f, 57.03f, 3.31f, "Megrez"},
    {12.900f, 55.96f, 1.77f, "Alioth"},
    {13.399f, 54.93f, 2.27f, "Mizar"},
    {13.792f, 49.31f, 1.86f, "Alkaid"},

    // Orion
    {5.919f, 7.41f, 0.50f, "Betelgeuse"},
    {5.242f, -8.20f, 0.12f, "Rigel"},
    {5.679f, -1.94f, 2.09f, "Alnitak"},
    {5.603f, -1.20f, 1.70f, "Alnilam"},
    {5.533f, -0.30f, 2.23f, "Mintaka"},
    {5.418f, 6.35f, 1.64f, "Bellatrix"},
    {5.796f, -9.67f, 2.06f, "Saiph"},

    // Cassiopeia
    {0.675f, 56.54f, 2.23f, "Schedar"},
    {0.153f, 59.15f, 2.27f, "Caph"},
    {0.945f, 60.72f, 2.47f, "Gamma Cas"},
    {1.430f, 60.24f, 2.68f, "Ruchbah"},
    {1.907f, 63.67f, 3.38f, "Segin"},

    // Cygnus (Northern Cross)
    {20.690f, 45.28f, 1.25f, "Deneb"},
    {19.512f, 27.96f, 2.20f, "Sadr"},
    {20.370f, 40.26f, 2.87f, "Gienah"},
    {19.749f, 45.13f, 3.20f, "Delta Cyg"},
    {21.216f, 30.23f, 2.46f, "Albireo"},

    // Leo
    {10.139f, 11.97f, 1.35f, "Regulus"},
    {11.235f, 20.52f, 2.14f, "Algieba"},
    {11.818f, 14.57f, 2.01f, "Denebola"},
    {10.333f, 19.84f, 2.98f, "Zosma"},

    // Scorpius
    {16.490f, -26.43f, 0.96f, "Antares"},
    {17.622f, -43.00f, 1.63f, "Shaula"},
    {16.006f, -22.62f, 2.32f, "Dschubba"},
    {16.353f, -25.59f, 2.29f, "Acrab"},
    {17.708f, -37.10f, 2.69f, "Sargas"},

    // Lyra
    {18.616f, 38.78f, 0.03f, "Vega"},
    {18.982f, 32.69f, 3.24f, "Sheliak"},
    {18.746f, 37.60f, 3.52f, "Sulafat"},

    // Aquila
    {19.846f, 8.87f, 0.77f, "Altair"},
    {19.771f, 10.61f, 2.72f, "Alshain"},
    {19.922f, 6.41f, 3.23f, "Tarazed"},

    // Gemini
    {7.577f, 31.89f, 1.14f, "Pollux"},
    {7.755f, 28.03f, 1.58f, "Castor"},
    {6.629f, 16.40f, 1.93f, "Alhena"},

    // Taurus
    {4.599f, 16.51f, 0.85f, "Aldebaran"},
    {5.438f, 28.61f, 1.65f, "Elnath"},

    // Canis Major
    {6.752f, -16.72f, -1.46f, "Sirius"},
    {7.140f, -26.39f, 1.50f, "Adhara"},
    {6.378f, -17.96f, 1.98f, "Mirzam"},

    // Canis Minor
    {7.655f, 5.23f, 0.34f, "Procyon"},

    // Virgo
    {13.420f, -11.16f, 0.97f, "Spica"},
    {12.694f, -1.45f, 2.83f, "Porrima"},

    // Bootes
    {14.261f, 19.18f, -0.04f, "Arcturus"},

    // Centaurus
    {14.660f, -60.84f, -0.27f, "Alpha Centauri"},
    {14.064f, -60.37f, 0.61f, "Hadar"},

    // Crux (Southern Cross)
    {12.443f, -63.10f, 0.76f, "Acrux"},
    {12.795f, -59.69f, 1.25f, "Mimosa"},
    {12.252f, -57.11f, 1.63f, "Gacrux"},

    // Perseus
    {3.405f, 49.86f, 1.79f, "Mirfak"},
    {3.136f, 40.96f, 2.12f, "Algol"},

    // Andromeda
    {0.140f, 29.09f, 2.06f, "Alpheratz"},
    {1.162f, 35.62f, 2.06f, "Mirach"},
    {2.065f, 42.33f, 2.26f, "Almach"},

    // Pegasus
    {21.736f, 9.88f, 2.49f, "Enif"},
    {23.063f, 15.21f, 2.42f, "Markab"},
    {23.079f, 28.08f, 2.83f, "Scheat"},
    {0.220f, 15.18f, 2.49f, "Algenib"},

    // Auriga
    {5.278f, 45.99f, 0.08f, "Capella"},
    {5.995f, 44.95f, 2.62f, "Menkalinan"},

    // Draco
    {17.943f, 51.49f, 2.24f, "Eltanin"},
    {19.209f, 67.66f, 3.07f, "Rastaban"},

    // Polaris (North Star)
    {2.530f, 89.26f, 1.98f, "Polaris"},

    // Corona Borealis
    {15.578f, 26.71f, 2.23f, "Alphecca"},

    // Additional bright stars
    {22.960f, -29.62f, 1.16f, "Fomalhaut"},
    {5.278f, -34.07f, -0.72f, "Canopus"},
    {6.399f, -52.70f, 0.72f, "Miaplacidus"},
};

// ==================================
// Helper: Find star by name
// ==================================
const Star *findStarByName(const char *name)
{
    for (const Star &star : BRIGHT_STARS)
    {
        if (strcmp(star.name, name) == 0)
        {
            return &star;
        }
    }
    return nullptr;
}

// ==================================
// Constellation Line Definitions
// ==================================
// Defines which stars to connect with lines for each constellation
const std::vector<Constellation> CONSTELLATIONS = {
    // Ursa Major (Big Dipper) - the famous "dipper" shape
    {"Ursa Major",
     {
         {"Dubhe", "Merak"},   // Bowl bottom
         {"Merak", "Phecda"},  // Bowl left side
         {"Phecda", "Megrez"}, // Bowl top
         {"Megrez", "Dubhe"},  // Bowl right side
         {"Megrez", "Alioth"}, // Handle start
         {"Alioth", "Mizar"},  // Handle middle
         {"Mizar", "Alkaid"},  // Handle end
     }},

    // Orion - the hunter
    {"Orion",
     {
         {"Betelgeuse", "Bellatrix"}, // Shoulders
         {"Betelgeuse", "Alnitak"},   // Left side down
         {"Bellatrix", "Mintaka"},    // Right side down
         {"Alnitak", "Alnilam"},      // Belt left
         {"Alnilam", "Mintaka"},      // Belt right
         {"Alnitak", "Saiph"},        // Left leg
         {"Mintaka", "Rigel"},        // Right leg
     }},

    // Cassiopeia - the W shape
    {"Cassiopeia",
     {
         {"Caph", "Schedar"},
         {"Schedar", "Gamma Cas"},
         {"Gamma Cas", "Ruchbah"},
         {"Ruchbah", "Segin"},
     }},

    // Cygnus (Northern Cross)
    {"Cygnus",
     {
         {"Deneb", "Sadr"},     // Tail to center
         {"Sadr", "Albireo"},   // Center to head
         {"Sadr", "Gienah"},    // Center to right wing
         {"Sadr", "Delta Cyg"}, // Center to left wing
     }},

    // Leo - the lion
    {"Leo",
     {
         {"Regulus", "Algieba"},
         {"Algieba", "Zosma"},
         {"Zosma", "Denebola"},
     }},

    // Scorpius - the scorpion
    {"Scorpius",
     {
         {"Acrab", "Dschubba"},
         {"Dschubba", "Antares"},
         {"Antares", "Sargas"},
         {"Sargas", "Shaula"},
     }},

    // Lyra - the lyre
    {"Lyra",
     {
         {"Vega", "Sheliak"},
         {"Vega", "Sulafat"},
         {"Sheliak", "Sulafat"},
     }},

    // Aquila - the eagle
    {"Aquila",
     {
         {"Altair", "Alshain"},
         {"Altair", "Tarazed"},
     }},

    // Gemini - the twins
    {"Gemini",
     {
         {"Pollux", "Castor"},
         {"Pollux", "Alhena"},
     }},

    // Taurus - the bull (partial, just bright stars)
    {"Taurus",
     {
         {"Aldebaran", "Elnath"},
     }},

    // Canis Major - the great dog
    {"Canis Major",
     {
         {"Sirius", "Mirzam"},
         {"Sirius", "Adhara"},
     }},

    // Crux (Southern Cross)
    {"Crux",
     {
         {"Acrux", "Gacrux"},  // Vertical beam
         {"Mimosa", "Gacrux"}, // Left to top (simplified)
     }},

    // Andromeda
    {"Andromeda",
     {
         {"Alpheratz", "Mirach"},
         {"Mirach", "Almach"},
     }},

    // Pegasus (Great Square, sharing Alpheratz with Andromeda)
    {"Pegasus",
     {
         {"Markab", "Scheat"},
         {"Scheat", "Alpheratz"},
         {"Alpheratz", "Algenib"},
         {"Algenib", "Markab"},
     }},

    // Auriga - the charioteer
    {"Auriga",
     {
         {"Capella", "Menkalinan"},
     }},

    // Perseus
    {"Perseus",
     {
         {"Mirfak", "Algol"},
     }},

    // Centaurus
    {"Centaurus",
     {
         {"Alpha Centauri", "Hadar"},
     }},

    // Draco - the dragon (partial)
    {"Draco",
     {
         {"Eltanin", "Rastaban"},
     }},
};
