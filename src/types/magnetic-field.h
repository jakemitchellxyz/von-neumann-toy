#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

// Forward declaration - FieldLine is defined in celestial-body.h
struct FieldLine;

// ==================================
// Magnetic Field Model Interface
// ==================================
// Abstract base class for planetary magnetic field models

class MagneticFieldModel {
public:
    virtual ~MagneticFieldModel() = default;
    
    // Compute the magnetic field vector at a given position
    // position: in body-centered coordinates (relative to body center, in km)
    // Returns: magnetic field vector in nanoTesla (nT)
    virtual glm::dvec3 computeField(const glm::dvec3& position, double yearFraction) const = 0;
    
    // Get the reference radius of the model (usually the planet's mean radius in km)
    virtual double getReferenceRadius() const = 0;
    
    // Get the model name
    virtual std::string getModelName() const = 0;
    
    // Check if model is valid for a given year
    virtual bool isValidForYear(double year) const = 0;
};

// ==================================
// IGRF (International Geomagnetic Reference Field) Model
// ==================================
// Implements the spherical harmonic expansion for Earth's magnetic field

class IGRFModel : public MagneticFieldModel {
public:
    // Default maximum degree for traditional IGRF (can be overridden for WMMHR)
    static constexpr int DEFAULT_MAX_DEGREE = 13;
    
    // Maximum degree of spherical harmonic expansion (dynamic based on loaded file)
    int maxDegree = DEFAULT_MAX_DEGREE;
    
    // Earth's reference radius in km (as used by IGRF/WMM)
    static constexpr double EARTH_RADIUS_KM = 6371.2;
    
    // Load IGRF coefficients from traditional IGRF file format
    // Returns nullptr if loading fails
    static std::unique_ptr<IGRFModel> loadFromFile(const std::string& filepath);
    
    // Load WMM/WMMHR coefficients from .COF file format
    // This is the simpler format: epoch, model, date on line 1
    // then: n m gnm hnm dgnm dhnm per line
    // Returns nullptr if loading fails
    static std::unique_ptr<IGRFModel> loadFromCOF(const std::string& filepath);
    
    // Compute the magnetic field at a position
    // position: in Earth-centered coordinates (km), with +Z toward north pole
    // yearFraction: decimal year (e.g., 2025.5 for mid-2025)
    // Returns: magnetic field in nanoTesla (Bx, By, Bz in geocentric coords)
    glm::dvec3 computeField(const glm::dvec3& position, double yearFraction) const override;
    
    double getReferenceRadius() const override { return EARTH_RADIUS_KM; }
    bool isValidForYear(double year) const override { return year >= 1900.0 && year <= 2035.0; }
    
private:
    IGRFModel() = default;
    
    // Gauss coefficients g(n,m) and h(n,m) for each epoch
    struct Epoch {
        double year;
        std::vector<std::vector<double>> g;  // g[n][m]
        std::vector<std::vector<double>> h;  // h[n][m]
    };
    
    std::vector<Epoch> epochs;
    
    // Secular variation coefficients (for extrapolation beyond last definitive epoch)
    std::vector<std::vector<double>> sv_g;
    std::vector<std::vector<double>> sv_h;
    double sv_base_year = 2025.0;
    
    // Interpolate/extrapolate coefficients for a given year
    void getCoefficients(double year, 
                         std::vector<std::vector<double>>& g_out,
                         std::vector<std::vector<double>>& h_out) const;
    
    // Associated Legendre polynomial P(n,m) and derivative
    static double associatedLegendre(int n, int m, double x, double& dPdx);
    
    // Schmidt semi-normalization factor
    static double schmidtFactor(int n, int m);
    
    // Model name (can be IGRF-14 or WMMHR-2025 etc)
    std::string modelNameStr = "IGRF-14";
    
public:
    std::string getModelName() const override { return modelNameStr; }
};

// ==================================
// Simple Dipole Model
// ==================================
// A basic magnetic dipole model for bodies without detailed field data

// ==================================
// Mars Magnetic Field Model (Crustal Anomalies)
// ==================================
// Mars has no active global dynamo, but has strong crustal magnetic anomalies
// Based on MGS (Mars Global Surveyor) data, model by Purucker 2008
class MarsMagneticModel : public MagneticFieldModel {
public:
    // Mars mean radius in km
    static constexpr double MARS_RADIUS_KM = 3393.5;
    static constexpr int MAX_DEGREE = 51;  // High resolution for crustal anomalies
    
    MarsMagneticModel() = default;
    
    // Load coefficients from file
    static std::unique_ptr<MarsMagneticModel> loadFromFile(const std::string& filepath);
    
    glm::dvec3 computeField(const glm::dvec3& position, double yearFraction) const override;
    double getReferenceRadius() const override { return MARS_RADIUS_KM; }
    std::string getModelName() const override { return "Mars-MGS-Purucker2008"; }
    bool isValidForYear(double) const override { return true; }  // Crustal field is static
    
private:
    // Gauss coefficients in nT
    std::vector<std::vector<double>> g;
    std::vector<std::vector<double>> h;
    
    void computeSchmidtLegendre(double cosTheta, double sinTheta,
                                 std::vector<std::vector<double>>& P,
                                 std::vector<std::vector<double>>& dP) const;
};

// ==================================
// Jupiter Magnetic Field Model (Juno/JRM33)
// ==================================
// High-resolution spherical harmonic model from Juno mission data
// Coefficients up to degree 30
class JupiterMagneticModel : public MagneticFieldModel {
public:
    // Jupiter's mean radius in km
    static constexpr double JUPITER_RADIUS_KM = 71492.0;
    static constexpr int MAX_DEGREE = 30;
    
    JupiterMagneticModel() = default;
    
    // Load coefficients from file
    static std::unique_ptr<JupiterMagneticModel> loadFromFile(const std::string& filepath);
    
    glm::dvec3 computeField(const glm::dvec3& position, double yearFraction) const override;
    double getReferenceRadius() const override { return JUPITER_RADIUS_KM; }
    std::string getModelName() const override { return "Jupiter-JRM33"; }
    bool isValidForYear(double) const override { return true; }  // Time-independent model
    
private:
    // Gauss coefficients in nT
    // g[n][m] and h[n][m] where n is degree (1-30), m is order (0-n)
    std::vector<std::vector<double>> g;
    std::vector<std::vector<double>> h;
    
    // Schmidt semi-normalized associated Legendre functions
    void computeSchmidtLegendre(double cosTheta, double sinTheta,
                                 std::vector<std::vector<double>>& P,
                                 std::vector<std::vector<double>>& dP) const;
};

// ==================================
// Saturn Magnetic Field Model (Cassini)
// ==================================
// Based on Cassini mission data
// Saturn's field is highly axisymmetric (nearly aligned with rotation axis)
class SaturnMagneticModel : public MagneticFieldModel {
public:
    // Saturn's mean radius in km
    static constexpr double SATURN_RADIUS_KM = 58232.0;
    static constexpr int MAX_DEGREE = 12;  // Support up to degree 12
    
    SaturnMagneticModel() = default;
    
    // Load coefficients from xlsx file (requires OpenXLSX)
    static std::unique_ptr<SaturnMagneticModel> loadFromXlsx(const std::string& filepath);
    
    // Load coefficients from text file (same format as Jupiter)
    static std::unique_ptr<SaturnMagneticModel> loadFromFile(const std::string& filepath);
    
    // Create with default Cao et al. 2012 coefficients
    static std::unique_ptr<SaturnMagneticModel> createDefault();
    
    glm::dvec3 computeField(const glm::dvec3& position, double yearFraction) const override;
    double getReferenceRadius() const override { return SATURN_RADIUS_KM; }
    std::string getModelName() const override { return "Saturn-Cassini"; }
    bool isValidForYear(double) const override { return true; }  // Time-independent model
    
private:
    // Gauss coefficients in nT
    // g[n][m] and h[n][m] where n is degree, m is order
    std::vector<std::vector<double>> g;
    std::vector<std::vector<double>> h;
    
    // Initialize coefficient arrays
    void initCoefficients();
    
    // Schmidt semi-normalized associated Legendre functions
    void computeSchmidtLegendre(double cosTheta, double sinTheta,
                                 std::vector<std::vector<double>>& P,
                                 std::vector<std::vector<double>>& dP) const;
};

// ==================================
// Simple Dipole Model
// ==================================
class DipoleMagneticModel : public MagneticFieldModel {
public:
    // Create a dipole model
    // dipoleMoment: magnetic dipole moment in nT * km^3
    // poleDirection: unit vector toward magnetic north pole (in body coords)
    // referenceRadius: planet radius in km
    DipoleMagneticModel(double dipoleMoment, const glm::dvec3& poleDirection, double referenceRadius);
    
    glm::dvec3 computeField(const glm::dvec3& position, double yearFraction) const override;
    double getReferenceRadius() const override { return refRadius; }
    std::string getModelName() const override { return "Dipole"; }
    bool isValidForYear(double) const override { return true; }
    
private:
    double moment;
    glm::dvec3 poleDir;
    double refRadius;
};

// ==================================
// Magnetic Field Line Tracer
// ==================================
// Traces field lines for visualization
// Note: FieldLine struct is defined in celestial-body.h

// Trace magnetic field lines from a starting point
// model: the magnetic field model to use
// startPos: starting position in body coordinates (km)
// yearFraction: time for field computation
// maxSteps: maximum integration steps
// stepSize: integration step size (km)
// Returns: traced field line
FieldLine traceFieldLine(const MagneticFieldModel& model,
                         const glm::dvec3& startPos,
                         double yearFraction,
                         int maxSteps = 1000,
                         double stepSize = 100.0);

// Generate a set of field lines around the body for visualization
// model: the magnetic field model
// yearFraction: time for field computation
// numLatitudes: number of starting latitude bands
// numLongitudes: number of starting points per latitude
// altitude: starting altitude above reference radius (km)
// maxExtentKm: maximum distance from center to trace field lines (defaults to 8x radius)
//              Set to L1 distance for realistic magnetopause boundary
std::vector<FieldLine> generateFieldLines(const MagneticFieldModel& model,
                                           double yearFraction,
                                           int numLatitudes = 6,
                                           int numLongitudes = 8,
                                           double altitude = 100.0,
                                           double maxExtentKm = 0.0);
