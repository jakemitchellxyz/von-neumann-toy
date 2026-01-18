#include "lagrange-point.h"
#include "../concerns/constants.h"
#include "../concerns/helpers/sphere-renderer.h"
#include <cmath>
#include <GLFW/glfw3.h>

// ==================================
// Lagrange Point Drawing
// ==================================

void LagrangePoint::draw() const {
    // Green color for Lagrange points
    glm::vec3 lagrangeColor(0.2f, 0.9f, 0.3f);
    DrawSphere(position, displayRadius, lagrangeColor, 12, 6);
}

// ==================================
// Lagrange System Implementation
// ==================================

LagrangeSystem::LagrangeSystem(const std::string& primary, const std::string& secondary,
                               double m1, double m2, float displayRadius)
    : primaryName(primary)
    , secondaryName(secondary)
    , primaryMass(m1)
    , secondaryMass(m2)
    , L1(primary + "-" + secondary + " L1", LagrangeType::L1, displayRadius)
    , L2(primary + "-" + secondary + " L2", LagrangeType::L2, displayRadius)
    , L3(primary + "-" + secondary + " L3", LagrangeType::L3, displayRadius)
    , L4(primary + "-" + secondary + " L4", LagrangeType::L4, displayRadius)
    , L5(primary + "-" + secondary + " L5", LagrangeType::L5, displayRadius)
{
}

void LagrangeSystem::update(const glm::vec3& primaryPos, const glm::vec3& secondaryPos) {
    calculateLagrangePoints(
        primaryPos, secondaryPos,
        primaryMass, secondaryMass,
        L1.position, L2.position, L3.position, L4.position, L5.position
    );
}

void LagrangeSystem::draw() const {
    L1.draw();
    L2.draw();
    L3.draw();
    L4.draw();
    L5.draw();
}

std::vector<LagrangePoint*> LagrangeSystem::getAllPoints() {
    return { &L1, &L2, &L3, &L4, &L5 };
}

// ==================================
// Lagrange Point Calculation
// ==================================

double calculateL1L2Distance(double separation, double primaryMass, double secondaryMass) {
    // Hill sphere radius approximation
    // r_Hill ≈ a * (m2 / (3 * m1))^(1/3)
    // where a is the semi-major axis (separation)
    
    if (primaryMass <= 0 || secondaryMass <= 0 || separation <= 0) {
        return 0.0;
    }
    
    double massRatio = secondaryMass / (3.0 * primaryMass);
    double hillRadius = separation * std::cbrt(massRatio);
    
    return hillRadius;
}

void calculateLagrangePoints(
    const glm::vec3& primaryPos,
    const glm::vec3& secondaryPos,
    double primaryMass,
    double secondaryMass,
    glm::vec3& outL1,
    glm::vec3& outL2,
    glm::vec3& outL3,
    glm::vec3& outL4,
    glm::vec3& outL5
) {
    // Vector from primary to secondary
    glm::vec3 toSecondary = secondaryPos - primaryPos;
    float separation = glm::length(toSecondary);
    
    if (separation < 0.0001f) {
        // Bodies too close, can't calculate meaningful Lagrange points
        outL1 = outL2 = outL3 = outL4 = outL5 = primaryPos;
        return;
    }
    
    // Normalized direction from primary to secondary
    glm::vec3 dir = toSecondary / separation;
    
    // Calculate Hill sphere distance for L1/L2
    double hillDist = calculateL1L2Distance(separation, primaryMass, secondaryMass);
    
    // L1: Between the two bodies, closer to secondary
    // Located at distance (separation - hillDist) from primary
    outL1 = secondaryPos - dir * static_cast<float>(hillDist);
    
    // L2: Beyond secondary, away from primary
    // Located at distance (separation + hillDist) from primary
    outL2 = secondaryPos + dir * static_cast<float>(hillDist);
    
    // L3: On opposite side of primary from secondary
    // Approximately at the same distance as secondary but on opposite side
    // More precisely: r_L3 ≈ R * (1 + 5*m2/(12*m1))
    double l3Factor = 1.0 + (5.0 * secondaryMass) / (12.0 * primaryMass);
    outL3 = primaryPos - dir * static_cast<float>(separation * l3Factor);
    
    // L4 and L5: At vertices of equilateral triangles
    // L4 is 60° ahead (leading) in the orbit
    // L5 is 60° behind (trailing) in the orbit
    
    // We need to determine the orbital plane normal
    // For simplicity, assume orbits are roughly in a plane
    // Use cross product with "up" direction to get perpendicular
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    glm::vec3 perpendicular = glm::cross(dir, up);
    
    // If dir is parallel to up, use a different reference
    if (glm::length(perpendicular) < 0.001f) {
        perpendicular = glm::cross(dir, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    perpendicular = glm::normalize(perpendicular);
    
    // L4 and L5 are at 60° angles from the primary-secondary line
    // Position is at same distance from primary as secondary
    // Rotated ±60° in the orbital plane
    
    // cos(60°) = 0.5, sin(60°) = √3/2 ≈ 0.866
    const float cos60 = 0.5f;
    const float sin60 = 0.866025f;
    
    // L4: 60° ahead (counterclockwise when viewed from above)
    glm::vec3 l4Dir = dir * cos60 + perpendicular * sin60;
    outL4 = primaryPos + l4Dir * separation;
    
    // L5: 60° behind (clockwise when viewed from above)
    glm::vec3 l5Dir = dir * cos60 - perpendicular * sin60;
    outL5 = primaryPos + l5Dir * separation;
}
