// ============================================================================
// Magnetic Field Model Implementation
// ============================================================================

#include "magnetic-field.h"
#include "celestial-body.h"  // For FieldLine struct

#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <iostream>

// ==================================
// IGRF Model Implementation
// ==================================

std::unique_ptr<IGRFModel> IGRFModel::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open IGRF coefficients file: " << filepath << std::endl;
        return nullptr;
    }
    
    auto model = std::unique_ptr<IGRFModel>(new IGRFModel());
    
    std::string line;
    std::vector<double> epochYears;
    bool headerParsed = false;
    int lineNum = 0;
    
    while (std::getline(file, line)) {
        lineNum++;
        
        // Skip comment lines
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream iss(line);
        
        // Parse header line to get epoch years
        // Look for a line starting with "g/h" which contains the epoch years
        if (!headerParsed) {
            std::string firstToken;
            iss >> firstToken;
            
            // The epoch year line starts with "g/h" (not "c/s" which is the category header)
            if (firstToken != "g/h") {
                continue;  // Skip category header lines
            }
            
            // Skip "n" and "m" tokens
            std::string token;
            iss >> token >> token;  // Skip "n" and "m"
            
            // Parse epoch years
            while (iss >> token) {
                // Skip the SV column header or date ranges like "2025-30"
                if (token == "SV" || token.find('-') != std::string::npos) {
                    break;
                }
                try {
                    double year = std::stod(token);
                    if (year > 1800 && year < 2200) {  // Sanity check
                        epochYears.push_back(year);
                    }
                } catch (...) {
                    // Not a number
                    break;
                }
            }
            
            std::cout << "IGRF: Found " << epochYears.size() << " epochs (";
            if (!epochYears.empty()) {
                std::cout << epochYears.front() << " to " << epochYears.back();
            }
            std::cout << ")" << std::endl;
            
            // Initialize epochs using default max degree for traditional IGRF
            const int maxDeg = IGRFModel::DEFAULT_MAX_DEGREE;
            model->maxDegree = maxDeg;
            
            for (double year : epochYears) {
                IGRFModel::Epoch epoch;
                epoch.year = year;
                epoch.g.resize(maxDeg + 1);
                epoch.h.resize(maxDeg + 1);
                for (int n = 0; n <= maxDeg; ++n) {
                    epoch.g[n].resize(n + 1, 0.0);
                    epoch.h[n].resize(n + 1, 0.0);
                }
                model->epochs.push_back(epoch);
            }
            
            // Initialize secular variation arrays
            model->sv_g.resize(maxDeg + 1);
            model->sv_h.resize(maxDeg + 1);
            for (int n = 0; n <= maxDeg; ++n) {
                model->sv_g[n].resize(n + 1, 0.0);
                model->sv_h[n].resize(n + 1, 0.0);
            }
            
            headerParsed = true;
            continue;
        }
        
        // Parse coefficient line
        char gh;
        int n, m;
        iss >> gh >> n >> m;
        
        if (n > model->maxDegree || m > n) continue;
        
        // Read coefficients for each epoch
        for (size_t i = 0; i < model->epochs.size(); ++i) {
            double coeff;
            if (iss >> coeff) {
                if (gh == 'g') {
                    model->epochs[i].g[n][m] = coeff;
                } else {
                    model->epochs[i].h[n][m] = coeff;
                }
            }
        }
        
        // Read secular variation (last column)
        double sv;
        if (iss >> sv) {
            if (gh == 'g') {
                model->sv_g[n][m] = sv;
            } else {
                model->sv_h[n][m] = sv;
            }
        }
    }
    
    if (model->epochs.empty()) {
        std::cerr << "No epochs found in IGRF file" << std::endl;
        return nullptr;
    }
    
    std::cout << "Loaded IGRF model with " << model->epochs.size() << " epochs" << std::endl;
    std::cout << "  Years: " << model->epochs.front().year << " to " << model->epochs.back().year << std::endl;
    
    // Debug: show main dipole coefficient (should be ~-29000 to -30000 nT)
    if (model->epochs.size() > 0) {
        const auto& lastEpoch = model->epochs.back();
        if (lastEpoch.g.size() > 1 && lastEpoch.g[1].size() > 0) {
            std::cout << "  g(1,0) = " << lastEpoch.g[1][0] << " nT (main dipole)" << std::endl;
        }
    }
    
    return model;
}

// ==================================
// Load from .COF (WMM/WMMHR) format
// ==================================
// Format:
//   Line 1: epoch model_name date
//   Lines 2+: n m gnm hnm dgnm dhnm
//   End: line of 9s (sentinel)

std::unique_ptr<IGRFModel> IGRFModel::loadFromCOF(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open COF coefficients file: " << filepath << std::endl;
        return nullptr;
    }
    
    auto model = std::unique_ptr<IGRFModel>(new IGRFModel());
    
    std::string line;
    int lineNum = 0;
    double epochYear = 2025.0;
    std::string modelName = "WMM";
    int maxDegreeFound = 0;
    
    // Temporary storage for coefficients (we don't know max degree yet)
    std::vector<std::tuple<int, int, double, double, double, double>> coeffData;
    
    while (std::getline(file, line)) {
        lineNum++;
        
        // Skip empty lines
        if (line.empty()) continue;
        
        // Check for sentinel line (all 9s)
        if (line.find("999999") != std::string::npos) break;
        
        std::istringstream iss(line);
        
        if (lineNum == 1) {
            // Parse header: epoch model_name date
            iss >> epochYear >> modelName;
            std::cout << "Loading " << modelName << " (epoch " << epochYear << ") from: " << filepath << std::endl;
            continue;
        }
        
        // Parse coefficient line: n m gnm hnm dgnm dhnm
        int n, m;
        double gnm, hnm, dgnm, dhnm;
        
        if (!(iss >> n >> m >> gnm >> hnm >> dgnm >> dhnm)) {
            // Try without secular variation (some files don't have it)
            std::istringstream iss2(line);
            if (!(iss2 >> n >> m >> gnm >> hnm)) {
                continue;  // Skip malformed line
            }
            dgnm = 0.0;
            dhnm = 0.0;
        }
        
        // Track max degree
        maxDegreeFound = std::max(maxDegreeFound, n);
        
        // Store coefficient
        coeffData.push_back({n, m, gnm, hnm, dgnm, dhnm});
    }
    
    file.close();
    
    if (coeffData.empty()) {
        std::cerr << "No coefficients found in COF file" << std::endl;
        return nullptr;
    }
    
    std::cout << "  Found coefficients up to degree " << maxDegreeFound << std::endl;
    std::cout << "  Total coefficients: " << coeffData.size() << std::endl;
    
    // Set max degree
    model->maxDegree = maxDegreeFound;
    model->modelNameStr = modelName;
    model->sv_base_year = epochYear;
    
    // Initialize a single epoch with the loaded coefficients
    IGRFModel::Epoch epoch;
    epoch.year = epochYear;
    epoch.g.resize(maxDegreeFound + 1);
    epoch.h.resize(maxDegreeFound + 1);
    for (int n = 0; n <= maxDegreeFound; ++n) {
        epoch.g[n].resize(n + 1, 0.0);
        epoch.h[n].resize(n + 1, 0.0);
    }
    
    // Initialize secular variation arrays
    model->sv_g.resize(maxDegreeFound + 1);
    model->sv_h.resize(maxDegreeFound + 1);
    for (int n = 0; n <= maxDegreeFound; ++n) {
        model->sv_g[n].resize(n + 1, 0.0);
        model->sv_h[n].resize(n + 1, 0.0);
    }
    
    // Fill in the coefficients
    for (const auto& [n, m, gnm, hnm, dgnm, dhnm] : coeffData) {
        if (n <= maxDegreeFound && m <= n) {
            epoch.g[n][m] = gnm;
            epoch.h[n][m] = hnm;
            model->sv_g[n][m] = dgnm;
            model->sv_h[n][m] = dhnm;
        }
    }
    
    model->epochs.push_back(epoch);
    
    // Debug output
    std::cout << "Loaded " << modelName << " magnetic field model" << std::endl;
    std::cout << "  Epoch: " << epochYear << std::endl;
    std::cout << "  Max degree: " << maxDegreeFound << std::endl;
    if (epoch.g.size() > 1 && epoch.g[1].size() > 0) {
        std::cout << "  g(1,0) = " << epoch.g[1][0] << " nT (main dipole)" << std::endl;
    }
    
    return model;
}

void IGRFModel::getCoefficients(double year,
                                 std::vector<std::vector<double>>& g_out,
                                 std::vector<std::vector<double>>& h_out) const {
    // Initialize output arrays (use dynamic maxDegree)
    g_out.resize(maxDegree + 1);
    h_out.resize(maxDegree + 1);
    for (int n = 0; n <= maxDegree; ++n) {
        g_out[n].resize(n + 1, 0.0);
        h_out[n].resize(n + 1, 0.0);
    }
    
    if (epochs.empty()) return;
    
    // Find bracketing epochs
    if (year <= epochs.front().year) {
        // Before first epoch - use first epoch
        g_out = epochs.front().g;
        h_out = epochs.front().h;
        return;
    }
    
    if (year >= epochs.back().year) {
        // After last epoch - extrapolate using secular variation
        double dt = year - sv_base_year;
        for (int n = 1; n <= maxDegree && n < static_cast<int>(sv_g.size()); ++n) {
            for (int m = 0; m <= n && m < static_cast<int>(sv_g[n].size()); ++m) {
                if (n < static_cast<int>(epochs.back().g.size()) && 
                    m < static_cast<int>(epochs.back().g[n].size())) {
                    g_out[n][m] = epochs.back().g[n][m] + sv_g[n][m] * dt;
                    h_out[n][m] = epochs.back().h[n][m] + sv_h[n][m] * dt;
                }
            }
        }
        return;
    }
    
    // Find bracketing epochs and interpolate
    for (size_t i = 0; i < epochs.size() - 1; ++i) {
        if (year >= epochs[i].year && year < epochs[i + 1].year) {
            double t = (year - epochs[i].year) / (epochs[i + 1].year - epochs[i].year);
            
            for (int n = 1; n <= maxDegree; ++n) {
                for (int m = 0; m <= n; ++m) {
                    if (n < static_cast<int>(epochs[i].g.size()) && 
                        m < static_cast<int>(epochs[i].g[n].size())) {
                        g_out[n][m] = epochs[i].g[n][m] + t * (epochs[i + 1].g[n][m] - epochs[i].g[n][m]);
                        h_out[n][m] = epochs[i].h[n][m] + t * (epochs[i + 1].h[n][m] - epochs[i].h[n][m]);
                    }
                }
            }
            return;
        }
    }
}

double IGRFModel::schmidtFactor(int n, int m) {
    // Compute Schmidt semi-normalization factor
    // S(n,m) = sqrt(2 * (n-m)! / (n+m)!) for m > 0
    // S(n,0) = 1
    if (m == 0) return 1.0;
    
    double factor = 2.0;
    for (int i = n - m + 1; i <= n + m; ++i) {
        factor /= static_cast<double>(i);
    }
    return std::sqrt(factor);
}

double IGRFModel::associatedLegendre(int n, int m, double x, double& dPdx) {
    // Compute associated Legendre polynomial P_n^m(x) and its derivative
    // Using recurrence relations
    
    double pmm = 1.0;  // P_m^m
    double somx2 = std::sqrt((1.0 - x) * (1.0 + x));
    
    // Compute P_m^m = (-1)^m * (2m-1)!! * (1-x^2)^(m/2)
    double fact = 1.0;
    for (int i = 1; i <= m; ++i) {
        pmm *= -fact * somx2;
        fact += 2.0;
    }
    
    if (n == m) {
        dPdx = m * x * pmm / (x * x - 1.0);
        return pmm;
    }
    
    // Compute P_{m+1}^m = x * (2m+1) * P_m^m
    double pmmp1 = x * (2.0 * m + 1.0) * pmm;
    
    if (n == m + 1) {
        dPdx = (static_cast<double>(n) * x * pmmp1 - static_cast<double>(n + m) * pmm) / (x * x - 1.0);
        return pmmp1;
    }
    
    // Use recurrence: (n-m)*P_n^m = x*(2n-1)*P_{n-1}^m - (n+m-1)*P_{n-2}^m
    double pnm = 0.0;
    double pnm_prev = pmm;
    double pnm_curr = pmmp1;
    
    for (int nn = m + 2; nn <= n; ++nn) {
        pnm = (x * (2.0 * nn - 1.0) * pnm_curr - static_cast<double>(nn + m - 1) * pnm_prev) 
              / static_cast<double>(nn - m);
        pnm_prev = pnm_curr;
        pnm_curr = pnm;
    }
    
    dPdx = (static_cast<double>(n) * x * pnm - static_cast<double>(n + m) * pnm_prev) / (x * x - 1.0);
    return pnm;
}

glm::dvec3 IGRFModel::computeField(const glm::dvec3& position, double yearFraction) const {
    // Convert Cartesian to spherical coordinates
    double r = glm::length(position);
    if (r < 1.0) r = 1.0;  // Avoid singularity at center
    
    double theta = std::acos(position.z / r);  // Colatitude (from north pole)
    double phi = std::atan2(position.y, position.x);  // Longitude
    
    double sinTheta = std::sin(theta);
    double cosTheta = std::cos(theta);
    
    // Get interpolated coefficients for the given year
    std::vector<std::vector<double>> g, h;
    getCoefficients(yearFraction, g, h);
    
    // Compute field components in spherical coordinates
    double Br = 0.0;      // Radial component
    double Btheta = 0.0;  // Colatitude component
    double Bphi = 0.0;    // Longitude component
    
    double ratio = EARTH_RADIUS_KM / r;
    double ratio_power = ratio * ratio;  // (a/r)^2
    
    // Use dynamic maxDegree (can be up to 133 for WMMHR)
    int effectiveMaxDegree = std::min(maxDegree, static_cast<int>(g.size()) - 1);
    
    for (int n = 1; n <= effectiveMaxDegree; ++n) {
        ratio_power *= ratio;  // (a/r)^(n+2)
        
        for (int m = 0; m <= n && m < static_cast<int>(g[n].size()); ++m) {
            double cos_m_phi = std::cos(m * phi);
            double sin_m_phi = std::sin(m * phi);
            
            // Compute associated Legendre polynomial and derivative
            double dPdTheta;
            double P_nm = associatedLegendre(n, m, cosTheta, dPdTheta);
            dPdTheta *= -sinTheta;  // Chain rule: dP/dtheta = dP/dx * dx/dtheta = dP/dx * (-sin(theta))
            
            // Schmidt normalization
            double S = schmidtFactor(n, m);
            P_nm *= S;
            dPdTheta *= S;
            
            // Field contributions from this term
            double g_nm = g[n][m];
            double h_nm = h[n][m];
            
            double coeff = g_nm * cos_m_phi + h_nm * sin_m_phi;
            double coeff_phi = m * (-g_nm * sin_m_phi + h_nm * cos_m_phi);
            
            Br += (n + 1) * ratio_power * coeff * P_nm;
            Btheta += -ratio_power * coeff * dPdTheta;
            
            if (sinTheta > 1e-10) {
                Bphi += -ratio_power * coeff_phi * P_nm / sinTheta;
            }
        }
    }
    
    // Convert from spherical to Cartesian coordinates
    double sinPhi = std::sin(phi);
    double cosPhi = std::cos(phi);
    
    double Bx = Br * sinTheta * cosPhi + Btheta * cosTheta * cosPhi - Bphi * sinPhi;
    double By = Br * sinTheta * sinPhi + Btheta * cosTheta * sinPhi + Bphi * cosPhi;
    double Bz = Br * cosTheta - Btheta * sinTheta;
    
    return glm::dvec3(Bx, By, Bz);
}

// ==================================
// Mars Magnetic Field Model Implementation
// ==================================

std::unique_ptr<MarsMagneticModel> MarsMagneticModel::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open Mars coefficients file: " << filepath << std::endl;
        return nullptr;
    }
    
    auto model = std::unique_ptr<MarsMagneticModel>(new MarsMagneticModel());
    
    // Initialize coefficient arrays
    model->g.resize(MAX_DEGREE + 1);
    model->h.resize(MAX_DEGREE + 1);
    for (int n = 0; n <= MAX_DEGREE; ++n) {
        model->g[n].resize(n + 1, 0.0);
        model->h[n].resize(n + 1, 0.0);
    }
    
    std::string line;
    int coeffCount = 0;
    bool headerPassed = false;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        // Skip header lines until we find the column header line
        if (line.find("gnm") != std::string::npos && line.find("hnm") != std::string::npos) {
            headerPassed = true;
            continue;
        }
        if (!headerPassed) continue;
        
        std::istringstream iss(line);
        int n, m;
        double gnm, hnm;
        
        // Format: n m gnm hnm [gdotnm hdotnm]
        if (iss >> n >> m >> gnm >> hnm) {
            if (n >= 1 && n <= MAX_DEGREE && m >= 0 && m <= n) {
                model->g[n][m] = gnm;
                model->h[n][m] = hnm;
                coeffCount++;
            }
        }
    }
    
    std::cout << "Loaded Mars magnetic field model with " << coeffCount << " coefficient pairs" << std::endl;
    std::cout << "  Reference radius: " << MARS_RADIUS_KM << " km" << std::endl;
    std::cout << "  Max degree: " << MAX_DEGREE << " (crustal anomaly resolution)" << std::endl;
    if (model->g[1][0] != 0.0) {
        std::cout << "  g(1,0) = " << model->g[1][0] << " nT (dipole term)" << std::endl;
    }
    
    return model;
}

void MarsMagneticModel::computeSchmidtLegendre(double cosTheta, double sinTheta,
                                                std::vector<std::vector<double>>& P,
                                                std::vector<std::vector<double>>& dP) const {
    int maxDeg = static_cast<int>(g.size()) - 1;
    P.resize(maxDeg + 1);
    dP.resize(maxDeg + 1);
    
    for (int n = 0; n <= maxDeg; ++n) {
        P[n].resize(n + 1, 0.0);
        dP[n].resize(n + 1, 0.0);
    }
    
    P[0][0] = 1.0;
    dP[0][0] = 0.0;
    
    if (maxDeg >= 1) {
        P[1][0] = cosTheta;
        P[1][1] = sinTheta;
        dP[1][0] = -sinTheta;
        dP[1][1] = cosTheta;
    }
    
    for (int n = 2; n <= maxDeg; ++n) {
        double factor = std::sqrt(static_cast<double>(2 * n - 1) / (2 * n));
        P[n][n] = factor * sinTheta * P[n-1][n-1];
        dP[n][n] = factor * (sinTheta * dP[n-1][n-1] + cosTheta * P[n-1][n-1]);
        
        P[n][n-1] = std::sqrt(static_cast<double>(2 * n - 1)) * cosTheta * P[n-1][n-1];
        dP[n][n-1] = std::sqrt(static_cast<double>(2 * n - 1)) * 
                     (cosTheta * dP[n-1][n-1] - sinTheta * P[n-1][n-1]);
        
        for (int m = 0; m < n - 1; ++m) {
            double Knm = static_cast<double>((n - 1) * (n - 1) - m * m) /
                         static_cast<double>((2 * n - 1) * (2 * n - 3));
            P[n][m] = cosTheta * P[n-1][m] - Knm * P[n-2][m];
            dP[n][m] = cosTheta * dP[n-1][m] - sinTheta * P[n-1][m] - Knm * dP[n-2][m];
        }
    }
}

glm::dvec3 MarsMagneticModel::computeField(const glm::dvec3& position, double) const {
    if (g.empty()) return glm::dvec3(0.0);
    
    double r = glm::length(position);
    if (r < 1.0) r = 1.0;
    
    double cosTheta = position.z / r;
    double sinTheta = std::sqrt(1.0 - cosTheta * cosTheta);
    if (sinTheta < 1e-10) sinTheta = 1e-10;
    
    double phi = std::atan2(position.y, position.x);
    
    std::vector<std::vector<double>> P, dP;
    computeSchmidtLegendre(cosTheta, sinTheta, P, dP);
    
    double Br = 0.0;
    double Btheta = 0.0;
    double Bphi = 0.0;
    
    double a_over_r = MARS_RADIUS_KM / r;
    double power = a_over_r * a_over_r;
    
    int maxDeg = static_cast<int>(g.size()) - 1;
    
    for (int n = 1; n <= maxDeg; ++n) {
        power *= a_over_r;
        
        for (int m = 0; m <= n && m < static_cast<int>(g[n].size()); ++m) {
            double cosM = std::cos(m * phi);
            double sinM = std::sin(m * phi);
            
            double gnm = g[n][m];
            double hnm = (m < static_cast<int>(h[n].size())) ? h[n][m] : 0.0;
            
            Br += static_cast<double>(n + 1) * power * (gnm * cosM + hnm * sinM) * P[n][m];
            Btheta += -power * (gnm * cosM + hnm * sinM) * dP[n][m];
            
            if (m > 0) {
                Bphi += power * static_cast<double>(m) * (-gnm * sinM + hnm * cosM) * P[n][m] / sinTheta;
            }
        }
    }
    
    double cosPhi = std::cos(phi);
    double sinPhi = std::sin(phi);
    
    double Bx = Br * sinTheta * cosPhi + Btheta * cosTheta * cosPhi - Bphi * sinPhi;
    double By = Br * sinTheta * sinPhi + Btheta * cosTheta * sinPhi + Bphi * cosPhi;
    double Bz = Br * cosTheta - Btheta * sinTheta;
    
    return glm::dvec3(Bx, By, Bz);
}

// ==================================
// Jupiter Magnetic Field Model Implementation
// ==================================

std::unique_ptr<JupiterMagneticModel> JupiterMagneticModel::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open Jupiter coefficients file: " << filepath << std::endl;
        return nullptr;
    }
    
    auto model = std::unique_ptr<JupiterMagneticModel>(new JupiterMagneticModel());
    
    // Initialize coefficient arrays (degree 0 to MAX_DEGREE)
    model->g.resize(MAX_DEGREE + 1);
    model->h.resize(MAX_DEGREE + 1);
    for (int n = 0; n <= MAX_DEGREE; ++n) {
        model->g[n].resize(n + 1, 0.0);
        model->h[n].resize(n + 1, 0.0);
    }
    
    std::string line;
    int coeffCount = 0;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::istringstream iss(line);
        char gh;
        int n, m;
        double coeff;
        
        if (iss >> gh >> n >> m >> coeff) {
            if (n >= 1 && n <= MAX_DEGREE && m >= 0 && m <= n) {
                if (gh == 'g') {
                    model->g[n][m] = coeff;
                } else if (gh == 'h') {
                    model->h[n][m] = coeff;
                }
                coeffCount++;
            }
        }
    }
    
    std::cout << "Loaded Jupiter magnetic field model with " << coeffCount << " coefficients" << std::endl;
    std::cout << "  g(1,0) = " << model->g[1][0] << " nT (main dipole)" << std::endl;
    std::cout << "  Max degree: " << MAX_DEGREE << std::endl;
    
    return model;
}

void JupiterMagneticModel::computeSchmidtLegendre(double cosTheta, double sinTheta,
                                                   std::vector<std::vector<double>>& P,
                                                   std::vector<std::vector<double>>& dP) const {
    // Compute Schmidt semi-normalized associated Legendre functions and their derivatives
    P.resize(MAX_DEGREE + 1);
    dP.resize(MAX_DEGREE + 1);
    
    for (int n = 0; n <= MAX_DEGREE; ++n) {
        P[n].resize(n + 1, 0.0);
        dP[n].resize(n + 1, 0.0);
    }
    
    // Initial values
    P[0][0] = 1.0;
    dP[0][0] = 0.0;
    
    if (MAX_DEGREE >= 1) {
        P[1][0] = cosTheta;
        P[1][1] = sinTheta;
        dP[1][0] = -sinTheta;
        dP[1][1] = cosTheta;
    }
    
    // Recurrence relations for P_n^m
    for (int n = 2; n <= MAX_DEGREE; ++n) {
        // Diagonal: P_n^n
        double factor = std::sqrt(static_cast<double>(2 * n - 1) / (2 * n));
        P[n][n] = factor * sinTheta * P[n-1][n-1];
        dP[n][n] = factor * (sinTheta * dP[n-1][n-1] + cosTheta * P[n-1][n-1]);
        
        // Sub-diagonal: P_n^{n-1}
        P[n][n-1] = std::sqrt(static_cast<double>(2 * n - 1)) * cosTheta * P[n-1][n-1];
        dP[n][n-1] = std::sqrt(static_cast<double>(2 * n - 1)) * 
                     (cosTheta * dP[n-1][n-1] - sinTheta * P[n-1][n-1]);
        
        // Lower orders
        for (int m = 0; m < n - 1; ++m) {
            double Knm = static_cast<double>((n - 1) * (n - 1) - m * m) /
                         static_cast<double>((2 * n - 1) * (2 * n - 3));
            P[n][m] = cosTheta * P[n-1][m] - Knm * P[n-2][m];
            dP[n][m] = cosTheta * dP[n-1][m] - sinTheta * P[n-1][m] - Knm * dP[n-2][m];
        }
    }
}

glm::dvec3 JupiterMagneticModel::computeField(const glm::dvec3& position, double) const {
    double r = glm::length(position);
    if (r < 1.0) r = 1.0;  // Avoid singularity
    
    double cosTheta = position.z / r;
    double sinTheta = std::sqrt(1.0 - cosTheta * cosTheta);
    if (sinTheta < 1e-10) sinTheta = 1e-10;  // Avoid division by zero at poles
    
    double phi = std::atan2(position.y, position.x);
    
    // Compute Schmidt semi-normalized Legendre functions
    std::vector<std::vector<double>> P, dP;
    computeSchmidtLegendre(cosTheta, sinTheta, P, dP);
    
    // Compute field components in spherical coordinates
    double Br = 0.0;
    double Btheta = 0.0;
    double Bphi = 0.0;
    
    double a_over_r = JUPITER_RADIUS_KM / r;
    double power = a_over_r * a_over_r;  // (a/r)^2
    
    for (int n = 1; n <= MAX_DEGREE; ++n) {
        power *= a_over_r;  // Now (a/r)^(n+2)
        
        for (int m = 0; m <= n; ++m) {
            double cosM = std::cos(m * phi);
            double sinM = std::sin(m * phi);
            
            double gnm = g[n][m];
            double hnm = h[n][m];
            
            // B_r = -dV/dr = sum (n+1) * (a/r)^(n+2) * (g*cos + h*sin) * P
            Br += static_cast<double>(n + 1) * power * (gnm * cosM + hnm * sinM) * P[n][m];
            
            // B_theta = -(1/r) * dV/dtheta = sum (a/r)^(n+2) * (g*cos + h*sin) * dP/dtheta
            Btheta += -power * (gnm * cosM + hnm * sinM) * dP[n][m];
            
            // B_phi = -(1/(r*sin(theta))) * dV/dphi = sum m * (a/r)^(n+2) * (-g*sin + h*cos) * P / sin(theta)
            if (m > 0) {
                Bphi += power * static_cast<double>(m) * (-gnm * sinM + hnm * cosM) * P[n][m] / sinTheta;
            }
        }
    }
    
    // Convert from spherical to Cartesian
    double cosPhi = std::cos(phi);
    double sinPhi = std::sin(phi);
    
    double Bx = Br * sinTheta * cosPhi + Btheta * cosTheta * cosPhi - Bphi * sinPhi;
    double By = Br * sinTheta * sinPhi + Btheta * cosTheta * sinPhi + Bphi * cosPhi;
    double Bz = Br * cosTheta - Btheta * sinTheta;
    
    return glm::dvec3(Bx, By, Bz);
}

// ==================================
// Saturn Magnetic Field Model Implementation
// ==================================

#ifdef HAS_OPENXLSX
#include <OpenXLSX.hpp>
#endif

void SaturnMagneticModel::initCoefficients() {
    g.resize(MAX_DEGREE + 1);
    h.resize(MAX_DEGREE + 1);
    for (int n = 0; n <= MAX_DEGREE; ++n) {
        g[n].resize(n + 1, 0.0);
        h[n].resize(n + 1, 0.0);
    }
}

std::unique_ptr<SaturnMagneticModel> SaturnMagneticModel::createDefault() {
    // Default Cao et al. 2012 coefficients (axisymmetric)
    auto model = std::unique_ptr<SaturnMagneticModel>(new SaturnMagneticModel());
    model->initCoefficients();
    
    // Only g(n,0) terms - Saturn's field is remarkably axisymmetric
    model->g[1][0] = 21191.0;  // Main dipole
    model->g[2][0] = 1586.0;   // Quadrupole
    model->g[3][0] = 2374.0;   // Octupole
    model->g[4][0] = 65.0;     // Hexadecapole
    model->g[5][0] = 185.0;    // 32-pole
    
    std::cout << "Saturn magnetic field model created with default Cao et al. 2012 coefficients" << std::endl;
    return model;
}

std::unique_ptr<SaturnMagneticModel> SaturnMagneticModel::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open Saturn coefficients file: " << filepath << std::endl;
        return nullptr;
    }
    
    auto model = std::unique_ptr<SaturnMagneticModel>(new SaturnMagneticModel());
    model->initCoefficients();
    
    std::string line;
    int coeffCount = 0;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::istringstream iss(line);
        char gh;
        int n, m;
        double coeff;
        
        if (iss >> gh >> n >> m >> coeff) {
            if (n >= 1 && n <= MAX_DEGREE && m >= 0 && m <= n) {
                if (gh == 'g') {
                    model->g[n][m] = coeff;
                } else if (gh == 'h') {
                    model->h[n][m] = coeff;
                }
                coeffCount++;
            }
        }
    }
    
    std::cout << "Loaded Saturn magnetic field model with " << coeffCount << " coefficients" << std::endl;
    std::cout << "  g(1,0) = " << model->g[1][0] << " nT (main dipole)" << std::endl;
    
    return model;
}

#ifdef HAS_OPENXLSX
std::unique_ptr<SaturnMagneticModel> SaturnMagneticModel::loadFromXlsx(const std::string& filepath) {
    try {
        OpenXLSX::XLDocument doc;
        doc.open(filepath);
        
        auto model = std::unique_ptr<SaturnMagneticModel>(new SaturnMagneticModel());
        model->initCoefficients();
        
        auto wks = doc.workbook().worksheet(doc.workbook().worksheetNames().front());
        int coeffCount = 0;
        
        // Iterate through rows - expect format: g/h, n, m, coefficient
        // Or: column headers in row 1, data starting row 2
        for (uint32_t row = 1; row <= wks.rowCount(); ++row) {
            try {
                auto cellA = wks.cell(row, 1);
                auto cellB = wks.cell(row, 2);
                auto cellC = wks.cell(row, 3);
                auto cellD = wks.cell(row, 4);
                
                // Try to parse as g/h n m coeff
                std::string ghStr = cellA.value().get<std::string>();
                if (ghStr.empty()) continue;
                
                char gh = ghStr[0];
                if (gh != 'g' && gh != 'h' && gh != 'G' && gh != 'H') continue;
                gh = (gh == 'G') ? 'g' : ((gh == 'H') ? 'h' : gh);
                
                int n = static_cast<int>(cellB.value().get<double>());
                int m = static_cast<int>(cellC.value().get<double>());
                double coeff = cellD.value().get<double>();
                
                if (n >= 1 && n <= MAX_DEGREE && m >= 0 && m <= n) {
                    if (gh == 'g') {
                        model->g[n][m] = coeff;
                    } else {
                        model->h[n][m] = coeff;
                    }
                    coeffCount++;
                }
            } catch (...) {
                // Skip rows that don't match expected format
                continue;
            }
        }
        
        doc.close();
        
        if (coeffCount == 0) {
            std::cerr << "No coefficients found in xlsx file: " << filepath << std::endl;
            return nullptr;
        }
        
        std::cout << "Loaded Saturn magnetic field model from xlsx with " << coeffCount << " coefficients" << std::endl;
        std::cout << "  g(1,0) = " << model->g[1][0] << " nT (main dipole)" << std::endl;
        
        return model;
        
    } catch (const std::exception& e) {
        std::cerr << "Error reading xlsx file: " << e.what() << std::endl;
        return nullptr;
    }
}
#else
std::unique_ptr<SaturnMagneticModel> SaturnMagneticModel::loadFromXlsx(const std::string& filepath) {
    std::cerr << "OpenXLSX not available - cannot load " << filepath << std::endl;
    std::cerr << "Falling back to default Saturn coefficients" << std::endl;
    return createDefault();
}
#endif

void SaturnMagneticModel::computeSchmidtLegendre(double cosTheta, double sinTheta,
                                                  std::vector<std::vector<double>>& P,
                                                  std::vector<std::vector<double>>& dP) const {
    int maxDeg = static_cast<int>(g.size()) - 1;
    P.resize(maxDeg + 1);
    dP.resize(maxDeg + 1);
    
    for (int n = 0; n <= maxDeg; ++n) {
        P[n].resize(n + 1, 0.0);
        dP[n].resize(n + 1, 0.0);
    }
    
    P[0][0] = 1.0;
    dP[0][0] = 0.0;
    
    if (maxDeg >= 1) {
        P[1][0] = cosTheta;
        P[1][1] = sinTheta;
        dP[1][0] = -sinTheta;
        dP[1][1] = cosTheta;
    }
    
    for (int n = 2; n <= maxDeg; ++n) {
        double factor = std::sqrt(static_cast<double>(2 * n - 1) / (2 * n));
        P[n][n] = factor * sinTheta * P[n-1][n-1];
        dP[n][n] = factor * (sinTheta * dP[n-1][n-1] + cosTheta * P[n-1][n-1]);
        
        P[n][n-1] = std::sqrt(static_cast<double>(2 * n - 1)) * cosTheta * P[n-1][n-1];
        dP[n][n-1] = std::sqrt(static_cast<double>(2 * n - 1)) * 
                     (cosTheta * dP[n-1][n-1] - sinTheta * P[n-1][n-1]);
        
        for (int m = 0; m < n - 1; ++m) {
            double Knm = static_cast<double>((n - 1) * (n - 1) - m * m) /
                         static_cast<double>((2 * n - 1) * (2 * n - 3));
            P[n][m] = cosTheta * P[n-1][m] - Knm * P[n-2][m];
            dP[n][m] = cosTheta * dP[n-1][m] - sinTheta * P[n-1][m] - Knm * dP[n-2][m];
        }
    }
}

glm::dvec3 SaturnMagneticModel::computeField(const glm::dvec3& position, double) const {
    if (g.empty()) return glm::dvec3(0.0);
    
    double r = glm::length(position);
    if (r < 1.0) r = 1.0;
    
    double cosTheta = position.z / r;
    double sinTheta = std::sqrt(1.0 - cosTheta * cosTheta);
    if (sinTheta < 1e-10) sinTheta = 1e-10;
    
    double phi = std::atan2(position.y, position.x);
    
    std::vector<std::vector<double>> P, dP;
    computeSchmidtLegendre(cosTheta, sinTheta, P, dP);
    
    double Br = 0.0;
    double Btheta = 0.0;
    double Bphi = 0.0;
    
    double a_over_r = SATURN_RADIUS_KM / r;
    double power = a_over_r * a_over_r;
    
    int maxDeg = static_cast<int>(g.size()) - 1;
    
    for (int n = 1; n <= maxDeg; ++n) {
        power *= a_over_r;
        
        for (int m = 0; m <= n && m < static_cast<int>(g[n].size()); ++m) {
            double cosM = std::cos(m * phi);
            double sinM = std::sin(m * phi);
            
            double gnm = g[n][m];
            double hnm = (m < static_cast<int>(h[n].size())) ? h[n][m] : 0.0;
            
            Br += static_cast<double>(n + 1) * power * (gnm * cosM + hnm * sinM) * P[n][m];
            Btheta += -power * (gnm * cosM + hnm * sinM) * dP[n][m];
            
            if (m > 0) {
                Bphi += power * static_cast<double>(m) * (-gnm * sinM + hnm * cosM) * P[n][m] / sinTheta;
            }
        }
    }
    
    double cosPhi = std::cos(phi);
    double sinPhi = std::sin(phi);
    
    double Bx = Br * sinTheta * cosPhi + Btheta * cosTheta * cosPhi - Bphi * sinPhi;
    double By = Br * sinTheta * sinPhi + Btheta * cosTheta * sinPhi + Bphi * cosPhi;
    double Bz = Br * cosTheta - Btheta * sinTheta;
    
    return glm::dvec3(Bx, By, Bz);
}

// ==================================
// Dipole Model Implementation
// ==================================

DipoleMagneticModel::DipoleMagneticModel(double dipoleMoment, const glm::dvec3& poleDirection, double referenceRadius)
    : moment(dipoleMoment)
    , poleDir(glm::normalize(poleDirection))
    , refRadius(referenceRadius)
{
}

glm::dvec3 DipoleMagneticModel::computeField(const glm::dvec3& position, double) const {
    double r = glm::length(position);
    if (r < 1.0) r = 1.0;
    
    glm::dvec3 rHat = position / r;
    
    // Magnetic dipole field: B = (mu0/4pi) * (1/r^3) * [3*(m·r)r - m]
    // Simplified for a dipole at origin with moment along poleDir
    double r3 = r * r * r;
    double mDotR = glm::dot(poleDir, rHat);
    
    glm::dvec3 B = (moment / r3) * (3.0 * mDotR * rHat - poleDir);
    
    return B;
}

// ==================================
// Field Line Tracing Implementation
// ==================================

// Trace a single direction along field lines
// maxExtentKm: maximum distance from center (0 = use default 8x radius)
static std::vector<glm::dvec3> traceOneDirection(const MagneticFieldModel& model,
                                                  const glm::dvec3& startPos,
                                                  double yearFraction,
                                                  double direction,  // +1 or -1
                                                  int maxSteps,
                                                  double stepSize,
                                                  double maxExtentKm) {
    std::vector<glm::dvec3> points;
    glm::dvec3 pos = startPos;
    double refRadius = model.getReferenceRadius();
    
    // Use provided max extent or default to 8x reference radius
    double maxRadius = (maxExtentKm > 0) ? maxExtentKm : (refRadius * 8.0);
    
    for (int step = 0; step < maxSteps; ++step) {
        points.push_back(pos);
        
        double r = glm::length(pos);
        
        // Stop conditions
        if (r > maxRadius) break;         // Beyond magnetopause (L1 distance)
        if (r < refRadius * 0.95) break;  // Hit surface
        
        // Get field at current position
        glm::dvec3 B = model.computeField(pos, yearFraction);
        double Bmag = glm::length(B);
        if (Bmag < 1.0) break;  // Field too weak (nT)
        
        // Step along field direction
        glm::dvec3 stepDir = (B / Bmag) * direction;
        pos += stepDir * stepSize;
    }
    
    return points;
}

// Forward declaration for use within this file
static FieldLine traceFieldLineInternal(const MagneticFieldModel& model,
                                        const glm::dvec3& startPos,
                                        double yearFraction,
                                        int maxSteps,
                                        double stepSize,
                                        double maxExtentKm);

FieldLine traceFieldLine(const MagneticFieldModel& model,
                         const glm::dvec3& startPos,
                         double yearFraction,
                         int maxSteps,
                         double stepSize) {
    // Call internal version with default max extent (0 = use 8x radius)
    return traceFieldLineInternal(model, startPos, yearFraction, maxSteps, stepSize, 0.0);
}

static FieldLine traceFieldLineInternal(const MagneticFieldModel& model,
                                        const glm::dvec3& startPos,
                                        double yearFraction,
                                        int maxSteps,
                                        double stepSize,
                                        double maxExtentKm) {
    FieldLine line;
    line.reachesOtherPole = false;
    line.startedFromNorth = (startPos.z >= 0);  // Z > 0 is north (positive) pole
    
    double startZ = startPos.z;  // Track which hemisphere we started in
    
    // Trace in both directions from starting point
    auto forwardPoints = traceOneDirection(model, startPos, yearFraction, 1.0, maxSteps / 2, stepSize, maxExtentKm);
    auto backwardPoints = traceOneDirection(model, startPos, yearFraction, -1.0, maxSteps / 2, stepSize, maxExtentKm);
    
    // Combine: reverse backward points and append forward points
    line.points.reserve(forwardPoints.size() + backwardPoints.size());
    
    // Add backward points in reverse order (excluding first which is startPos)
    for (int i = static_cast<int>(backwardPoints.size()) - 1; i > 0; --i) {
        line.points.push_back(backwardPoints[i]);
    }
    
    // Add all forward points
    for (const auto& pt : forwardPoints) {
        line.points.push_back(pt);
    }
    
    // Check if line reaches opposite hemisphere
    if (!line.points.empty()) {
        double endZ = line.points.back().z;
        double startEndZ = line.points.front().z;
        if ((startZ > 0 && (endZ < 0 || startEndZ < 0)) || 
            (startZ < 0 && (endZ > 0 || startEndZ > 0))) {
            line.reachesOtherPole = true;
        }
    }
    
    return line;
}

std::vector<FieldLine> generateFieldLines(const MagneticFieldModel& model,
                                           double yearFraction,
                                           int numLatitudes,
                                           int numLongitudes,
                                           double altitude,
                                           double maxExtentKm) {
    std::vector<FieldLine> lines;
    double refRadius = model.getReferenceRadius();
    double startRadius = refRadius + altitude;
    
    // Use provided max extent or default to 8x reference radius
    double effectiveMaxExtent = (maxExtentKm > 0) ? maxExtentKm : (refRadius * 8.0);
    
    std::cout << "Generating field lines: refRadius=" << refRadius 
              << " km, startAlt=" << altitude << " km, lats=" << numLatitudes 
              << ", lons=" << numLongitudes 
              << ", maxExtent=" << effectiveMaxExtent << " km (L1 boundary)" << std::endl;
    
    // Generate starting points at various latitudes in NORTHERN hemisphere only
    // Field lines naturally trace to the southern hemisphere for dipole fields
    // This creates cleaner, non-redundant line coverage
    for (int latIdx = 0; latIdx < numLatitudes; ++latIdx) {
        // Latitudes from near north pole toward equator (e.g., 75°, 60°, 45°, 30°)
        double lat = 75.0 - latIdx * (50.0 / std::max(numLatitudes - 1, 1));
        double latRad = glm::radians(lat);
        
        for (int lonIdx = 0; lonIdx < numLongitudes; ++lonIdx) {
            // Even spacing around the globe (0°, 45°, 90°, 135°, etc. for 8 longitudes)
            double lon = lonIdx * (360.0 / numLongitudes);
            double lonRad = glm::radians(lon);
            
            // Starting point in northern hemisphere
            glm::dvec3 startPos(
                startRadius * std::cos(latRad) * std::cos(lonRad),
                startRadius * std::cos(latRad) * std::sin(lonRad),
                startRadius * std::sin(latRad)
            );
            
            // Trace field line with L1 boundary (magnetopause)
            FieldLine line = traceFieldLineInternal(model, startPos, yearFraction, 1000, 100.0, effectiveMaxExtent);
            if (line.points.size() > 5) {
                lines.push_back(line);
            }
        }
    }
    
    std::cout << "Generated " << lines.size() << " field lines" << std::endl;
    if (!lines.empty()) {
        size_t totalPoints = 0;
        size_t closedLines = 0;
        for (const auto& l : lines) {
            totalPoints += l.points.size();
            if (l.reachesOtherPole) closedLines++;
        }
        std::cout << "  Total points: " << totalPoints << ", avg per line: " 
                  << (totalPoints / lines.size()) << std::endl;
        std::cout << "  Closed (connected) lines: " << closedLines 
                  << ", Open lines: " << (lines.size() - closedLines) << std::endl;
        // Debug: show sample field strength at start
        glm::dvec3 testPos(refRadius + 100, 0, 0);  // 100km above equator
        glm::dvec3 B = model.computeField(testPos, yearFraction);
        std::cout << "  Field at equator surface: " << glm::length(B) << " nT" << std::endl;
    }
    
    return lines;
}
