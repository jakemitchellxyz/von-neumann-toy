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
// Coordinate System Constants
// ==================================
const double OBLIQUITY_J2000_RAD = 0.4090926006005828; // Obliquity of the ecliptic at J2000.0 (radians) ≈ 23.4392911°

// ==================================
// Earth Atmospheric Constants
// ==================================
const double KARMAN_LINE_KM = 100.0;           // Kármán line: boundary of space (100 km above Earth's surface)
const double SCATTERING_ATMOSPHERE_KM = 100.0; // Optically significant atmosphere height (same as Kármán line, 100 km)

// ==================================
// Visualization Scale Factors
// ==================================
// UNITS_PER_AU converts AU to display units using consistent scaling for both positions and radii
// All celestial body sizes and distances use this same conversion factor
const float UNITS_PER_AU = 600.0f;
const float SKYBOX_RADIUS = 50000.0f; // Large sphere encompassing the solar system

// ==================================
// Render Settings
// ==================================
bool g_showOrbits = false;       // Default to disabled
bool g_showRotationAxes = false; // Default to disabled
bool g_showBarycenters = false;
bool g_showLagrangePoints = false;
bool g_showCoordinateGrids = false;
bool g_showMagneticFields = false;
bool g_showGravityGrid = false;
int g_gravityGridResolution = 25;
float g_gravityWarpStrength = 1.0f;
bool g_showConstellations = false;
bool g_showCelestialGrid = false;        // Default to hidden
bool g_showConstellationFigures = false; // Default to hidden
bool g_showConstellationBounds = false;  // Default to hidden
bool g_showForceVectors = false;
bool g_showSunSpot = false;          // Default to disabled
bool g_showWireframe = false;        // Default to hidden
bool g_showVoxelWireframes = false;  // Default to hidden
bool g_showAtmosphereLayers = false; // Default to hidden

// ==================================
// Helper Functions
// ==================================
float getDisplayRadius(double realRadiusKm)
{
    // Convert km to AU, then scale by UNITS_PER_AU (same conversion as positions)
    // This ensures proportionally correct sizes relative to orbital distances
    // No arbitrary scaling - everything derives from SPICE data
    constexpr double KM_PER_AU = 149597870.7;
    return static_cast<float>((realRadiusKm / KM_PER_AU) * UNITS_PER_AU);
}
