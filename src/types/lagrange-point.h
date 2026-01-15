#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

// ==================================
// Lagrange Point Structure
// ==================================
// Represents one of the five Lagrange points in a two-body system

enum class LagrangeType {
    L1,  // Between the two bodies
    L2,  // Beyond the smaller body, away from the larger
    L3,  // Beyond the larger body, opposite the smaller
    L4,  // 60° ahead of smaller body in orbit (leading)
    L5   // 60° behind smaller body in orbit (trailing)
};

struct LagrangePoint {
    std::string name;           // e.g., "Sun-Earth L1"
    LagrangeType type;
    glm::vec3 position;
    float displayRadius;
    
    LagrangePoint(const std::string& n, LagrangeType t, float radius = 1.0f)
        : name(n), type(t), position(0.0f), displayRadius(radius) {}
    
    // Render the Lagrange point as a green sphere
    void draw() const;
};

// ==================================
// Lagrange Point System
// ==================================
// Represents all 5 Lagrange points for a two-body system

struct LagrangeSystem {
    std::string primaryName;    // e.g., "Sun"
    std::string secondaryName;  // e.g., "Earth"
    double primaryMass;
    double secondaryMass;
    
    LagrangePoint L1;
    LagrangePoint L2;
    LagrangePoint L3;
    LagrangePoint L4;
    LagrangePoint L5;
    
    LagrangeSystem(const std::string& primary, const std::string& secondary,
                   double m1, double m2, float displayRadius);
    
    // Update all Lagrange point positions given current body positions
    void update(const glm::vec3& primaryPos, const glm::vec3& secondaryPos);
    
    // Draw all Lagrange points
    void draw() const;
    
    // Get all points as a vector for iteration
    std::vector<LagrangePoint*> getAllPoints();
};

// ==================================
// Lagrange Point Calculation
// ==================================

// Calculate approximate distance from secondary body to L1/L2 points
// Uses the Hill sphere approximation: r ≈ R * (m2 / (3 * m1))^(1/3)
double calculateL1L2Distance(double separation, double primaryMass, double secondaryMass);

// Calculate all 5 Lagrange points for a two-body system
void calculateLagrangePoints(
    const glm::vec3& primaryPos,    // Position of larger body (e.g., Sun)
    const glm::vec3& secondaryPos,  // Position of smaller body (e.g., Earth)
    double primaryMass,
    double secondaryMass,
    glm::vec3& outL1,
    glm::vec3& outL2,
    glm::vec3& outL3,
    glm::vec3& outL4,
    glm::vec3& outL5
);
