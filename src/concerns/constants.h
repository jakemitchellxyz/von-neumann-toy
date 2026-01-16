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
// Coordinate System Constants
// ==================================
extern const double OBLIQUITY_J2000_RAD; // Obliquity of the ecliptic at J2000.0 (radians) ≈ 23.4392911°

// ==================================
// Earth Atmospheric Constants
// ==================================
extern const double KARMAN_LINE_KM;           // Kármán line: boundary of space (100 km above Earth's surface)
extern const double SCATTERING_ATMOSPHERE_KM; // Optically significant atmosphere height (same as Kármán line, 100 km)

// ==================================
// Sphere Rendering Constants
// ==================================
constexpr int SPHERE_BASE_SLICES = 32; // Base number of longitude divisions (slices) for sphere tessellation
constexpr int SPHERE_BASE_STACKS = 32; // Base number of latitude divisions (stacks) for sphere tessellation
constexpr float TESSELATION_DISTANCE_THRESHOLD = 5.0f; // Distance threshold (in radii) for dynamic tessellation
constexpr int MAX_TESSELATION_MULTIPLIER = 4;          // Maximum tessellation multiplier when very close
constexpr float LOCAL_TESSELATION_RADIUS =
    0.5f; // Radius (in sphere radii) for local high-detail tessellation around closest point
constexpr int LOCAL_TESSELATION_MULTIPLIER = 8; // Additional multiplier for local high-detail region
constexpr int FAR_TRIANGLE_COUNT_MAX = 64;      // Maximum number of triangles for pie-style rendering when far away
constexpr int FAR_TRIANGLE_COUNT_MIN = 16;      // Minimum number of triangles for pie-style rendering at 5 radii

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
extern bool g_showOrbits;               // Show/hide orbital paths
extern bool g_showRotationAxes;         // Show/hide rotation axes and equators
extern bool g_showBarycenters;          // Show/hide barycenter markers
extern bool g_showLagrangePoints;       // Show/hide Lagrange points
extern bool g_showCoordinateGrids;      // Show/hide planet coordinate grids
extern bool g_showMagneticFields;       // Show/hide magnetic field lines
extern bool g_showGravityGrid;          // Show/hide gravity spacetime grid
extern bool g_showForceVectors;         // Show/hide gravity and momentum force vectors
extern bool g_showSunSpot;              // Show/hide sun spot visualization (circle + cross at overhead position)
extern int g_gravityGridResolution;     // Grid lines per axis (10-50)
extern float g_gravityWarpStrength;     // Warp strength multiplier (0.1-5.0)
extern bool g_showConstellations;       // Show/hide constellation stars
extern bool g_showCelestialGrid;        // Show/hide celestial grid overlay
extern bool g_showConstellationFigures; // Show/hide constellation figure lines
extern bool g_showConstellationBounds;  // Show/hide constellation boundary lines
extern bool g_showWireframe;            // Show/hide wireframe mode (triangle edges)

// ==================================
// Helper Functions
// ==================================
float getDisplayRadius(double realRadiusKm);
