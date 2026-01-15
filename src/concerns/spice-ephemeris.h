#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

// ==================================
// SPICE Ephemeris Module
// ==================================
// Provides high-precision solar system ephemeris data using NASA/NAIF SPICE kernels
// All positions are relative to the Solar System Barycenter (SSB)
// Time is in Barycentric Dynamical Time (TDB)

namespace SpiceEphemeris {

// ==================================
// NAIF Body IDs
// ==================================
// Standard NAIF integer codes for solar system bodies
// Using barycenters (1-9) for outer planets as de440.bsp has full coverage
// Planet centers (X99) require additional satellite kernels with limited dates
constexpr int NAIF_SSB     = 0;       // Solar System Barycenter
constexpr int NAIF_SUN     = 10;      // Sun
constexpr int NAIF_MERCURY = 1;       // Mercury Barycenter (≈ planet center)
constexpr int NAIF_VENUS   = 2;       // Venus Barycenter (≈ planet center)
constexpr int NAIF_EARTH   = 399;     // Earth (planet center, needed for Moon offset)
constexpr int NAIF_MOON    = 301;     // Moon
constexpr int NAIF_MARS    = 4;       // Mars Barycenter
constexpr int NAIF_JUPITER = 5;       // Jupiter Barycenter
constexpr int NAIF_SATURN  = 6;       // Saturn Barycenter
constexpr int NAIF_URANUS  = 7;       // Uranus Barycenter
constexpr int NAIF_NEPTUNE = 8;       // Neptune Barycenter
constexpr int NAIF_PLUTO   = 9;       // Pluto Barycenter

// Major moons
constexpr int NAIF_IO       = 501;    // Io
constexpr int NAIF_EUROPA   = 502;    // Europa
constexpr int NAIF_GANYMEDE = 503;    // Ganymede
constexpr int NAIF_CALLISTO = 504;    // Callisto
constexpr int NAIF_TITAN    = 606;    // Titan
constexpr int NAIF_TRITON   = 801;    // Triton
constexpr int NAIF_CHARON   = 901;    // Charon

// ==================================
// Initialization
// ==================================

// Initialize the SPICE system and load kernels from the specified directory
// kernelDir: path to directory containing SPICE kernel files
// Returns true if at least one SPK kernel was loaded successfully
bool initialize(const std::string& kernelDir);

// Check if SPICE is initialized
bool isInitialized();

// Cleanup and unload all kernels
void cleanup();

// ==================================
// Time Functions
// ==================================

// Get the time coverage of loaded SPK kernels for a specific body
// Returns start and end times as TDB Julian Dates
// Returns false if no coverage found for the body
bool getTimeCoverage(int naifId, double& startJD, double& endJD);

// Get the latest time available across all major planets
// Returns the minimum of all end times (so all bodies have valid data)
double getLatestAvailableTime();

// Get the earliest time available across all major planets
double getEarliestAvailableTime();

// Convert UTC calendar date to TDB Julian Date
double utcToTdbJulian(int year, int month, int day, int hour, int minute, double second);

// Convert TDB Julian Date to ephemeris time (seconds past J2000 TDB)
double julianToEt(double jdTdb);

// Convert ephemeris time to TDB Julian Date
double etToJulian(double et);

// ==================================
// Position Functions
// ==================================

// Get position of a body relative to the Solar System Barycenter
// naifId: NAIF ID of the body (use constants above)
// jdTdb: Julian Date in TDB
// Returns position in AU (converted to display units by caller)
// Returns (0,0,0) if body not found or time out of range
glm::dvec3 getBodyPosition(int naifId, double jdTdb);

// Get position and velocity of a body relative to the Solar System Barycenter
// position: output position in AU
// velocity: output velocity in AU/day
// Returns false if body not found or time out of range
bool getBodyState(int naifId, double jdTdb, glm::dvec3& position, glm::dvec3& velocity);

// ==================================
// Rotation/Orientation Functions
// ==================================

// Get the rotation axis (north pole direction) for a body from PCK kernel
// naifId: NAIF ID of the body
// jdTdb: Julian Date in TDB (pole can precess over time)
// Returns normalized axis direction in J2000 ecliptic coordinates
// Returns (0,1,0) if data not available
glm::dvec3 getBodyPoleDirection(int naifId, double jdTdb);

// Get the prime meridian direction for a body (X-axis of body-fixed frame)
// naifId: NAIF ID of the body
// jdTdb: Julian Date in TDB
// Returns normalized direction in J2000 ecliptic coordinates
// Returns (1,0,0) if data not available
glm::dvec3 getBodyPrimeMeridian(int naifId, double jdTdb);

// Get the full body-fixed frame (pole + prime meridian + third axis)
// naifId: NAIF ID of the body
// jdTdb: Julian Date in TDB
// pole: output - north pole direction (Z-axis of body-fixed frame)
// primeMeridian: output - prime meridian direction (X-axis)
// Returns true if successful
bool getBodyFrame(int naifId, double jdTdb, glm::dvec3& pole, glm::dvec3& primeMeridian);

// Check if rotation data is available for a body
bool hasRotationData(int naifId);

// ==================================
// Physical Constants (from PCK kernel)
// ==================================

// Get body radii from PCK kernel
// Returns (equatorial_a, equatorial_b, polar) in km
// For spherical bodies, all three values are equal
// Returns (0,0,0) if not available
glm::dvec3 getBodyRadii(int naifId);

// Get mean radius (average of radii) in km
// Returns 0 if not available
double getBodyMeanRadius(int naifId);

// Get gravitational parameter (GM) in km³/s²
// This is mass × gravitational constant
// Returns 0 if not available
double getBodyGM(int naifId);

// Get body mass in kg (derived from GM / G)
// Returns 0 if not available
double getBodyMass(int naifId);

// ==================================
// Utility
// ==================================

// Get the last error message from SPICE (if any)
std::string getLastError();

// Check if a specific body ID has data available
bool hasBodyData(int naifId);

} // namespace SpiceEphemeris
