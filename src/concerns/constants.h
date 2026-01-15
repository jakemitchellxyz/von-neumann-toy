#pragma once

#include <string>
#include <vector>


// ==================================
// Physics Constants
// ==================================
extern const double G; // Gravitational constant (m^3 / (kg * s^2))
extern const double PI;
extern const double AU_IN_METERS; // 1 AU in meters
extern const double DAY_IN_SECONDS;

// ==================================
// Celestial Body Masses (kg)
// ==================================
// Planets
extern const double MASS_SUN;
extern const double MASS_MERCURY;
extern const double MASS_VENUS;
extern const double MASS_EARTH;
extern const double MASS_MARS;
extern const double MASS_JUPITER;
extern const double MASS_SATURN;
extern const double MASS_URANUS;
extern const double MASS_NEPTUNE;
extern const double MASS_PLUTO;

// Moons
extern const double MASS_MOON;
extern const double MASS_IO;
extern const double MASS_EUROPA;
extern const double MASS_GANYMEDE;
extern const double MASS_CALLISTO;
extern const double MASS_TITAN;
extern const double MASS_TRITON;
extern const double MASS_CHARON;

// ==================================
// Celestial Body Radii (km)
// ==================================
// Planets
extern const double RADIUS_SUN_KM;
extern const double RADIUS_MERCURY_KM;
extern const double RADIUS_VENUS_KM;
extern const double RADIUS_EARTH_KM;
extern const double RADIUS_MARS_KM;
extern const double RADIUS_JUPITER_KM;
extern const double RADIUS_SATURN_KM;
extern const double RADIUS_URANUS_KM;
extern const double RADIUS_NEPTUNE_KM;
extern const double RADIUS_PLUTO_KM;

// Moons
extern const double RADIUS_MOON_KM;
extern const double RADIUS_IO_KM;
extern const double RADIUS_EUROPA_KM;
extern const double RADIUS_GANYMEDE_KM;
extern const double RADIUS_CALLISTO_KM;
extern const double RADIUS_TITAN_KM;
extern const double RADIUS_TRITON_KM;
extern const double RADIUS_CHARON_KM;

// ==================================
// Moon Orbital Parameters
// ==================================
// Semi-major axes (AU)
extern const double IO_SMA_AU;
extern const double EUROPA_SMA_AU;
extern const double GANYMEDE_SMA_AU;
extern const double CALLISTO_SMA_AU;
extern const double TITAN_SMA_AU;
extern const double TRITON_SMA_AU;
extern const double CHARON_SMA_AU;
extern const double LUNA_SMA_AU;

// Orbital periods (days)
extern const double IO_PERIOD;
extern const double EUROPA_PERIOD;
extern const double GANYMEDE_PERIOD;
extern const double CALLISTO_PERIOD;
extern const double TITAN_PERIOD;
extern const double TRITON_PERIOD;
extern const double CHARON_PERIOD;
extern const double LUNA_PERIOD;

// ==================================
// Planetary Orbital Semi-major Axes (AU)
// ==================================
extern const double MERCURY_SMA_AU;
extern const double VENUS_SMA_AU;
extern const double EARTH_SMA_AU;
extern const double MARS_SMA_AU;
extern const double JUPITER_SMA_AU;
extern const double SATURN_SMA_AU;
extern const double URANUS_SMA_AU;
extern const double NEPTUNE_SMA_AU;
extern const double PLUTO_SMA_AU;
extern const double PLUTO_PERIOD_DAYS;

// ==================================
// Planetary Axial Tilts (degrees from ecliptic normal)
// ==================================
// Obliquity: angle between rotation axis and orbital axis (perpendicular to ecliptic)
extern const float MERCURY_AXIAL_TILT;
extern const float VENUS_AXIAL_TILT;
extern const float EARTH_AXIAL_TILT;
extern const float MARS_AXIAL_TILT;
extern const float JUPITER_AXIAL_TILT;
extern const float SATURN_AXIAL_TILT;
extern const float URANUS_AXIAL_TILT;
extern const float NEPTUNE_AXIAL_TILT;
extern const float PLUTO_AXIAL_TILT;

// ==================================
// Rotation Periods (sidereal day in hours)
// ==================================
extern const double SUN_ROTATION_HOURS;
extern const double MERCURY_ROTATION_HOURS;
extern const double VENUS_ROTATION_HOURS;
extern const double EARTH_ROTATION_HOURS;
extern const double MARS_ROTATION_HOURS;
extern const double JUPITER_ROTATION_HOURS;
extern const double SATURN_ROTATION_HOURS;
extern const double URANUS_ROTATION_HOURS;
extern const double NEPTUNE_ROTATION_HOURS;
extern const double PLUTO_ROTATION_HOURS;
extern const double MOON_ROTATION_HOURS;

// ==================================
// Time Constants
// ==================================
extern const double JD_J2000;               // Julian Date for J2000.0 epoch
extern const double DAYS_PER_TROPICAL_YEAR; // Days per tropical year (365.2425)
constexpr int MONTHS_PER_YEAR = 12;         // Number of months in a year (for monthly texture arrays)

// ==================================
// Earth Atmospheric Constants
// ==================================
extern const double KARMAN_LINE_KM;           // Kármán line: boundary of space (100 km above Earth's surface)
extern const double SCATTERING_ATMOSPHERE_KM; // Optically significant atmosphere height (same as Kármán line, 100 km)

// ==================================
// Sphere Rendering Constants
// ==================================
constexpr int SPHERE_BASE_SLICES = 64; // Base number of longitude divisions (slices) for sphere tessellation
constexpr int SPHERE_BASE_STACKS = 32; // Base number of latitude divisions (stacks) for sphere tessellation

// ==================================
// Visualization Scale Factors
// ==================================
extern const float UNITS_PER_AU;
extern const float MOON_DISTANCE_SCALE;
extern const float EARTH_DISPLAY_RADIUS;
extern const float MIN_DISPLAY_RADIUS;
extern const float SKYBOX_RADIUS;

// ==================================
// Render Settings (mutable at runtime)
// ==================================
extern bool g_showOrbits;           // Show/hide orbital paths
extern bool g_showRotationAxes;     // Show/hide rotation axes and equators
extern bool g_showBarycenters;      // Show/hide barycenter markers
extern bool g_showLagrangePoints;   // Show/hide Lagrange points
extern bool g_showCoordinateGrids;  // Show/hide planet coordinate grids
extern bool g_showMagneticFields;   // Show/hide magnetic field lines
extern bool g_showGravityGrid;      // Show/hide gravity spacetime grid
extern bool g_showConstellations;   // Show/hide constellation lines and labels
extern bool g_showForceVectors;     // Show/hide gravity and momentum force vectors
extern bool g_showAtmosphereLayers; // Show/hide atmosphere layer rings
extern bool g_showSunSpot;          // Show/hide sun spot visualization (circle + cross at overhead position)
extern bool g_enableAtmosphere;     // Enable/disable atmosphere rendering (fullscreen ray march)
extern bool g_useAtmosphereLUT;     // Use precomputed transmittance LUT instead of ray marching (faster)
extern bool g_useMultiscatterLUT;   // Use precomputed multiscatter LUT instead of fallback (faster, better quality)
extern int g_gravityGridResolution; // Grid lines per axis (10-50)
extern float g_gravityWarpStrength; // Warp strength multiplier (0.1-5.0)

// ==================================
// Star Data
// ==================================
struct Star
{
    float ra;  // Right Ascension in hours (0-24)
    float dec; // Declination in degrees (-90 to +90)
    float mag; // Apparent magnitude (lower = brighter)
    const char *name;
};

extern const std::vector<Star> BRIGHT_STARS;

// ==================================
// Constellation Data
// ==================================
// A constellation is defined by its name and a list of line segments
// Each line segment connects two stars by name
struct ConstellationLine
{
    const char *star1;
    const char *star2;
};

struct Constellation
{
    const char *name;
    std::vector<ConstellationLine> lines;
};

extern const std::vector<Constellation> CONSTELLATIONS;

// Find a star by name in BRIGHT_STARS (returns nullptr if not found)
const Star *findStarByName(const char *name);

// ==================================
// Helper Functions
// ==================================
float getDisplayRadius(double realRadiusKm);
