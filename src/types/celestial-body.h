#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <deque>
#include <optional>
#include <memory>

// Forward declarations for magnetic field (full types in magnetic-field.h)
class MagneticFieldModel;

// FieldLine structure for magnetic field line visualization
// Defined here to avoid circular includes (used by std::vector member)
struct FieldLine {
    std::vector<glm::dvec3> points;  // Points along the field line (in body coords, km)
    bool reachesOtherPole = false;   // True if line connects to opposite hemisphere
    bool startedFromNorth = true;    // True if line originated from northern (positive) hemisphere
};

// ==================================
// Celestial Body Structure
// ==================================
// Represents a celestial object (star, planet, moon) in the solar system simulation

struct CelestialBody {
    std::string name;
    int naifId;           // NAIF SPICE ID for looking up rotation data
    glm::vec3 position;
    glm::vec3 velocity;   // Velocity vector (in display units per day)
    glm::vec3 color;
    float displayRadius;
    double mass;
    float axialTilt;      // Fallback axial tilt in degrees (used if SPICE data unavailable)
    double rotationPeriod; // Rotation period in hours (sidereal day)
    
    // Cached pole direction (updated each frame from SPICE or fallback)
    glm::vec3 poleDirection;
    
    // Cached prime meridian direction (updated each frame from SPICE)
    // Points to where 0° longitude intersects the equator
    glm::vec3 primeMeridianDirection;
    
    // Optional barycenter position (for systems with moons or overall solar system)
    std::optional<glm::vec3> barycenter;
    float barycenterDisplayRadius;  // Radius for drawing the barycenter marker
    
    // ==================================
    // Trail effect (orbital path visualization)
    // ==================================
    bool trailEnabled;                     // Whether to record and draw trail
    std::vector<glm::vec3> trailPoints;    // Accumulated trail positions in world space
    
    // ==================================
    // Magnetic Field Model (optional)
    // ==================================
    std::shared_ptr<MagneticFieldModel> magneticField;  // Magnetic field model (nullptr if none)
    std::vector<FieldLine> cachedFieldLines;            // Cached field lines for rendering
    double fieldLinesYear = 0.0;                        // Year for which field lines were computed
    bool showMagneticField = false;                     // Whether to render magnetic field lines
    double magnetosphereExtentKm = 0.0;                 // L1 distance (magnetopause boundary) in km
    
    // ==================================
    // Coordinate Grid (lat/long lines)
    // ==================================
    bool showCoordinateGrid = false;                    // Whether to render lat/long grid
    
    // ==================================
    // Textured Rendering (for Earth)
    // ==================================
    bool useTexturedMaterial = false;                   // Whether to use textured rendering
    
    // ==================================
    // Lighting Properties
    // ==================================
    bool isEmissive = false;                            // True for self-luminous bodies (Sun)
    CelestialBody* parentBody = nullptr;                // Parent body for moons (shares same light config)
    
    CelestialBody(const std::string& n, int naif, glm::vec3 col, float r, double m, float tilt = 0.0f)
        : name(n), naifId(naif), position(0.0f), velocity(0.0f), color(col), displayRadius(r), mass(m), axialTilt(tilt),
          rotationPeriod(24.0), poleDirection(0.0f, 1.0f, 0.0f), primeMeridianDirection(1.0f, 0.0f, 0.0f),
          barycenter(std::nullopt), barycenterDisplayRadius(0.0f),
          trailEnabled(false), magneticField(nullptr), showMagneticField(false), showCoordinateGrid(false),
          useTexturedMaterial(false), isEmissive(false), parentBody(nullptr) {}
    
    // Record current position to trail (call each timestep while trail is enabled)
    void recordTrailPoint();
    
    // Clear all trail points
    void clearTrail() {
        trailPoints.clear();
    }
    
    // Toggle trail on/off
    void toggleTrail() {
        trailEnabled = !trailEnabled;
        if (!trailEnabled) {
            clearTrail();
        }
    }
    
    // Draw the trail as a line in world space
    void drawTrail() const;
    
    // Render the celestial body
    // julianDate: optional, needed for textured materials (e.g., Earth monthly textures)
    // cameraPos: camera position in world space (needed for atmosphere rendering)
    void draw(double julianDate, const glm::vec3& cameraPos) const;
    
    // Render the barycenter marker (if barycenter is set)
    void drawBarycenter() const;
    
    // Render the rotation axis (green = north pole, red = south pole)
    // Uses poleDirection (must be updated each frame)
    void drawRotationAxis() const;
    
    // Render an equatorial circle around the body
    // Uses poleDirection to determine the equatorial plane
    void drawEquator() const;
    
    // Draw force vectors: gravity acceleration (yellow) and momentum (cyan)
    // gravityAccel: pre-computed gravitational acceleration vector
    void drawForceVectors(const glm::vec3& gravityAccel) const;
    
    // Update the pole direction from SPICE or fallback
    void updatePoleDirection(double jdTdb);
    
    // ==================================
    // Magnetic Field Methods
    // ==================================
    
    // Set a magnetic field model for this body
    void setMagneticFieldModel(std::shared_ptr<MagneticFieldModel> model);
    
    // Check if this body has a magnetic field model
    bool hasMagneticField() const { return magneticField != nullptr; }
    
    // Compute magnetic field at a point relative to body center
    // localPos: position in body-centered coordinates (display units)
    // yearFraction: decimal year for time-dependent models
    // Returns: magnetic field vector in nanoTesla
    glm::dvec3 computeMagneticField(const glm::vec3& localPos, double yearFraction) const;
    
    // Update cached field lines (call when year changes significantly or first time)
    // Uses magnetosphereExtentKm as the maximum field extent (set from L1 distance)
    void updateFieldLines(double yearFraction, int numLatitudes = 6, int numLongitudes = 8);
    
    // Draw magnetic field lines (3D visualization)
    void drawMagneticFieldLines() const;
    
    // Toggle magnetic field visualization
    void toggleMagneticField() { showMagneticField = !showMagneticField; }
    
    // ==================================
    // Coordinate Grid Methods
    // ==================================
    
    // Toggle coordinate grid visualization
    void toggleCoordinateGrid() { showCoordinateGrid = !showCoordinateGrid; }
    
    // Draw the latitude/longitude coordinate grid
    // cameraPos: camera position for distance calculations (for label culling)
    // cameraFront: camera forward direction (for billboard orientation)
    // cameraUp: camera up direction (for billboard orientation)
    void drawCoordinateGrid(const glm::vec3& cameraPos, 
                            const glm::vec3& cameraFront, 
                            const glm::vec3& cameraUp) const;
};

// ==================================
// Barycenter Calculation
// ==================================

// Compute the barycenter (center of mass) for a system of bodies
// Returns the position where the weighted center of mass is located
glm::vec3 computeBarycenter(const std::vector<CelestialBody*>& bodies);

// Compute barycenter for a primary body and its moons
// Sets the barycenter field on the primary body
void computePlanetaryBarycenter(CelestialBody& primary, const std::vector<CelestialBody*>& moons);
