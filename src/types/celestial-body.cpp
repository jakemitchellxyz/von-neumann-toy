// ============================================================================
// Celestial Body Implementation
// ============================================================================

#include "celestial-body.h"
#include "../concerns/constants.h"
#include "../concerns/solar-lighting.h"
#include "../concerns/spice-ephemeris.h"
#include "../materials/earth/earth-material.h"
#include "magnetic-field.h"


#include <GLFW/glfw3.h> // Includes OpenGL headers properly on all platforms
#include <algorithm>    // For std::sort
#include <cmath>
#include <glm/glm.hpp>


// Forward declaration for DrawSphere (defined in entrypoint.cpp) - kept for non-lit objects
void DrawSphere(const glm::vec3 &center, float radius, const glm::vec3 &color, int slices, int stacks);

// ============================================================================
// CelestialBody::draw()
// ============================================================================

void CelestialBody::draw(double julianDate, const glm::vec3 &cameraPos) const
{
    if (isEmissive)
    {
        // Sun is self-luminous - draw without lighting
        SolarLighting::drawEmissiveSphere(position, displayRadius, color, 32, 16);
    }
    else
    {
        // Determine which position to use for lighting calculation
        // Moons use their parent planet's position for consistent lighting
        glm::vec3 lightingPosition = parentBody ? parentBody->position : position;

        // Set up lighting for this body's position
        SolarLighting::setupLightingForBody(lightingPosition, UNITS_PER_AU);

        // Use textured material if enabled and available
        if (useTexturedMaterial && g_earthMaterial.isInitialized())
        {
            // Compute sun direction for atmosphere scattering
            glm::vec3 sunPos = SolarLighting::getSunPosition();
            glm::vec3 sunDir = glm::normalize(sunPos - position);

            // Compute moon direction for moonlight
            // Get moon position from SPICE and convert to display coordinates
            glm::dvec3 moonPosKm = SpiceEphemeris::getBodyPosition(SpiceEphemeris::NAIF_MOON, julianDate);
            glm::dvec3 earthPosKm = SpiceEphemeris::getBodyPosition(SpiceEphemeris::NAIF_EARTH, julianDate);

            // Convert km to AU, then to display units
            constexpr double KM_PER_AU = 149597870.7;
            glm::vec3 moonPos = glm::vec3((moonPosKm - earthPosKm) / KM_PER_AU * static_cast<double>(UNITS_PER_AU));
            glm::vec3 moonDir = glm::normalize(moonPos); // Direction from Earth to Moon

            g_earthMaterial.draw(position,
                                 displayRadius,
                                 poleDirection,
                                 primeMeridianDirection,
                                 julianDate,
                                 cameraPos,
                                 sunDir,
                                 moonDir);
        }
        else
        {
            // Draw with solar lighting and proper SPICE-derived orientation
            SolarLighting::drawOrientedLitSphere(position,
                                                 displayRadius,
                                                 color,
                                                 poleDirection,
                                                 primeMeridianDirection,
                                                 32,
                                                 16);
        }
    }
}

// ============================================================================
// CelestialBody::recordTrailPoint()
// ============================================================================

void CelestialBody::recordTrailPoint()
{
    if (!trailEnabled)
        return;
    trailPoints.push_back(position);
}

// ============================================================================
// CelestialBody::drawTrail()
// ============================================================================

void CelestialBody::drawTrail() const
{
    if (!trailEnabled || trailPoints.size() < 2)
        return;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE); // Don't write to depth buffer (transparent)
    glLineWidth(2.0f);

    // Draw trail as a line strip with fading alpha
    glBegin(GL_LINE_STRIP);

    size_t numPoints = trailPoints.size();
    for (size_t i = 0; i < numPoints; ++i)
    {
        // Alpha fades from 0 (oldest) to full (newest)
        float alpha = static_cast<float>(i) / static_cast<float>(numPoints - 1);
        alpha = alpha * alpha; // Quadratic falloff for smoother fade

        // Use body color with fading alpha
        glColor4f(color.r, color.g, color.b, alpha * 0.8f);
        glVertex3f(trailPoints[i].x, trailPoints[i].y, trailPoints[i].z);
    }

    glEnd();

    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ============================================================================
// CelestialBody::drawBarycenter()
// ============================================================================

void CelestialBody::drawBarycenter() const
{
    if (barycenter.has_value())
    {
        glm::vec3 barycenterColor(0.2f, 0.5f, 0.95f); // Blue color
        DrawSphere(barycenter.value(), barycenterDisplayRadius, barycenterColor, 16, 8);
    }
}

// ============================================================================
// CelestialBody::updatePoleDirection()
// ============================================================================

void CelestialBody::updatePoleDirection(double jdTdb)
{
    using namespace SpiceEphemeris;

    glm::dvec3 pole, pm;
    if (getBodyFrame(naifId, jdTdb, pole, pm))
    {
        // Convert from J2000 equatorial to our display coords (Y-up)
        // SPICE J2000 is equatorial, we swap Y and Z for display
        // We negate the Z component to maintain right-handedness (correct rotation direction)
        poleDirection = glm::vec3(static_cast<float>(pole.x),
                                  static_cast<float>(pole.z), // J2000 Z -> Display Y
                                  -static_cast<float>(pole.y) // J2000 Y -> Display -Z (negated for correct rotation)
        );
        poleDirection = glm::normalize(poleDirection);

        // Also get the prime meridian direction (includes time-dependent rotation!)
        primeMeridianDirection =
            glm::vec3(static_cast<float>(pm.x),
                      static_cast<float>(pm.z), // J2000 Z -> Display Y
                      -static_cast<float>(pm.y) // J2000 Y -> Display -Z (negated for correct rotation)
            );
        primeMeridianDirection = glm::normalize(primeMeridianDirection);
    }
    else
    {
        // Fallback: use hardcoded axial tilt
        // Note: tilt is toward +X to match SPICE convention after coordinate transform
        float tiltRad = glm::radians(axialTilt);
        poleDirection = glm::vec3(-sinf(tiltRad), // Tilt toward -X for correct handedness
                                  cosf(tiltRad),
                                  0.0f);
        poleDirection = glm::normalize(poleDirection);

        // Fallback prime meridian - perpendicular to pole, using right-hand rule
        // Cross with Y-up to get a vector in the XZ plane
        primeMeridianDirection = glm::normalize(glm::cross(poleDirection, glm::vec3(0.0f, 1.0f, 0.0f)));
        if (glm::length(primeMeridianDirection) < 0.01f)
        {
            primeMeridianDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        }
    }
}

// ============================================================================
// CelestialBody::drawRotationAxis()
// ============================================================================

void CelestialBody::drawRotationAxis() const
{
    // Line length is 2x the radius
    float axisLength = displayRadius * 2.0f;

    // Calculate endpoints using cached pole direction
    glm::vec3 northPole = position + poleDirection * axisLength;
    glm::vec3 southPole = position - poleDirection * axisLength;

    // Disable lighting for line rendering
    glDisable(GL_LIGHTING);
    glLineWidth(2.0f);

    // Draw north pole line (green)
    glBegin(GL_LINES);
    glColor3f(0.2f, 0.9f, 0.2f); // Green
    glVertex3f(position.x, position.y, position.z);
    glVertex3f(northPole.x, northPole.y, northPole.z);
    glEnd();

    // Draw south pole line (red)
    glBegin(GL_LINES);
    glColor3f(0.9f, 0.2f, 0.2f); // Red
    glVertex3f(position.x, position.y, position.z);
    glVertex3f(southPole.x, southPole.y, southPole.z);
    glEnd();

    // ==================================
    // Draw right-hand rule cone/arrow at north pole
    // ==================================
    // The cone points in the direction of pole, with a circular arrow around it
    // showing counter-clockwise rotation when viewed from above (right-hand rule)

    float coneHeight = displayRadius * 0.4f;
    float coneRadius = displayRadius * 0.2f;
    int coneSegments = 12;

    // Position cone slightly before the end of the north pole line
    glm::vec3 coneBase = northPole - poleDirection * coneHeight;
    glm::vec3 coneTip = northPole;

    // Get perpendicular vectors for the cone base
    glm::vec3 up = poleDirection;
    glm::vec3 arbitrary = (fabs(up.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 coneX = glm::normalize(glm::cross(up, arbitrary));
    glm::vec3 coneY = glm::normalize(glm::cross(up, coneX));

    // Draw cone surface (green)
    glColor3f(0.3f, 0.85f, 0.3f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(coneTip.x, coneTip.y, coneTip.z);
    for (int i = 0; i <= coneSegments; ++i)
    {
        float angle = 2.0f * static_cast<float>(PI) * static_cast<float>(i) / coneSegments;
        glm::vec3 point = coneBase + coneRadius * (cosf(angle) * coneX + sinf(angle) * coneY);
        glVertex3f(point.x, point.y, point.z);
    }
    glEnd();

    // Draw rotation direction arrows around the cone (curved arrow effect)
    // Three small arrows spaced around the cone showing rotation direction
    glColor3f(0.9f, 0.9f, 0.2f); // Yellow for rotation direction
    glLineWidth(2.5f);

    float arrowRadius = coneRadius * 1.3f;
    float arrowHeight = coneHeight * 0.5f;
    glm::vec3 arrowCenter = coneBase + poleDirection * arrowHeight;

    for (int a = 0; a < 3; ++a)
    {
        float baseAngle = 2.0f * static_cast<float>(PI) * static_cast<float>(a) / 3.0f;

        // Draw arc segment (counter-clockwise direction)
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= 6; ++i)
        {
            float angle = baseAngle + static_cast<float>(PI) * 0.3f * static_cast<float>(i) / 6.0f;
            glm::vec3 point = arrowCenter + arrowRadius * (cosf(angle) * coneX + sinf(angle) * coneY);
            glVertex3f(point.x, point.y, point.z);
        }
        glEnd();

        // Draw arrowhead at the end of the arc
        float endAngle = baseAngle + static_cast<float>(PI) * 0.3f;
        glm::vec3 arrowEnd = arrowCenter + arrowRadius * (cosf(endAngle) * coneX + sinf(endAngle) * coneY);

        // Tangent direction (counter-clockwise)
        glm::vec3 tangent = glm::normalize(-sinf(endAngle) * coneX + cosf(endAngle) * coneY);
        glm::vec3 arrowHead1 = arrowEnd - tangent * (displayRadius * 0.08f) + poleDirection * (displayRadius * 0.06f);
        glm::vec3 arrowHead2 = arrowEnd - tangent * (displayRadius * 0.08f) - poleDirection * (displayRadius * 0.06f);

        glBegin(GL_LINES);
        glVertex3f(arrowEnd.x, arrowEnd.y, arrowEnd.z);
        glVertex3f(arrowHead1.x, arrowHead1.y, arrowHead1.z);
        glVertex3f(arrowEnd.x, arrowEnd.y, arrowEnd.z);
        glVertex3f(arrowHead2.x, arrowHead2.y, arrowHead2.z);
        glEnd();
    }

    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

// ============================================================================
// CelestialBody::drawEquator()
// ============================================================================

void CelestialBody::drawEquator() const
{
    const int segments = 64;

    // The equator is perpendicular to the pole direction
    // Find two perpendicular vectors in the equatorial plane
    glm::vec3 up = poleDirection;
    glm::vec3 arbitrary = (fabs(up.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 equatorX = glm::normalize(glm::cross(up, arbitrary));
    glm::vec3 equatorY = glm::normalize(glm::cross(up, equatorX));

    // Equator radius is slightly larger than body for visibility
    float equatorRadius = displayRadius * 1.05f;

    glDisable(GL_LIGHTING);
    glLineWidth(1.5f);

    // Draw equator in a cyan/teal color
    glColor3f(0.3f, 0.8f, 0.8f);

    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segments; ++i)
    {
        float angle = 2.0f * static_cast<float>(PI) * static_cast<float>(i) / segments;
        glm::vec3 point = position + equatorRadius * (cosf(angle) * equatorX + sinf(angle) * equatorY);
        glVertex3f(point.x, point.y, point.z);
    }
    glEnd();

    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

// ============================================================================
// CelestialBody::drawForceVectors()
// ============================================================================
// Draws two force vectors:
// 1. Gravity acceleration vector (yellow/orange) - direction and magnitude of gravitational pull
// 2. Momentum/velocity vector (cyan) - direction and magnitude of current motion

void CelestialBody::drawForceVectors(const glm::vec3 &gravityAccel) const
{
    glDisable(GL_LIGHTING);
    glLineWidth(2.5f);

    // Scale factor for visualization - vectors should be visible relative to body size
    // We scale based on display radius so vectors are proportional to the body
    float baseScale = displayRadius * 5.0f;

    // ==================================
    // 1. Gravity acceleration vector (yellow/orange)
    // ==================================
    float gravMag = glm::length(gravityAccel);
    if (gravMag > 1e-10f)
    {
        glm::vec3 gravDir = gravityAccel / gravMag;

        // Scale gravity vector - use log scale to handle huge range
        // gravMag is in display units per day^2, we want visual length
        float gravLength = baseScale * std::log10(1.0f + gravMag * 1000.0f);
        gravLength = std::min(gravLength, displayRadius * 20.0f); // Cap length

        glm::vec3 gravEnd = position + gravDir * gravLength;

        // Draw gravity vector line (yellow/orange)
        glColor3f(1.0f, 0.7f, 0.2f);
        glBegin(GL_LINES);
        glVertex3f(position.x, position.y, position.z);
        glVertex3f(gravEnd.x, gravEnd.y, gravEnd.z);
        glEnd();

        // Draw arrowhead
        glm::vec3 perpX, perpY;
        if (fabs(gravDir.y) < 0.9f)
        {
            perpX = glm::normalize(glm::cross(gravDir, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        else
        {
            perpX = glm::normalize(glm::cross(gravDir, glm::vec3(1.0f, 0.0f, 0.0f)));
        }
        perpY = glm::normalize(glm::cross(gravDir, perpX));

        float arrowSize = gravLength * 0.15f;
        glm::vec3 arrowBase = gravEnd - gravDir * arrowSize;

        glBegin(GL_TRIANGLES);
        glVertex3f(gravEnd.x, gravEnd.y, gravEnd.z);
        glVertex3f(arrowBase.x + perpX.x * arrowSize * 0.4f,
                   arrowBase.y + perpX.y * arrowSize * 0.4f,
                   arrowBase.z + perpX.z * arrowSize * 0.4f);
        glVertex3f(arrowBase.x - perpX.x * arrowSize * 0.4f,
                   arrowBase.y - perpX.y * arrowSize * 0.4f,
                   arrowBase.z - perpX.z * arrowSize * 0.4f);

        glVertex3f(gravEnd.x, gravEnd.y, gravEnd.z);
        glVertex3f(arrowBase.x + perpY.x * arrowSize * 0.4f,
                   arrowBase.y + perpY.y * arrowSize * 0.4f,
                   arrowBase.z + perpY.z * arrowSize * 0.4f);
        glVertex3f(arrowBase.x - perpY.x * arrowSize * 0.4f,
                   arrowBase.y - perpY.y * arrowSize * 0.4f,
                   arrowBase.z - perpY.z * arrowSize * 0.4f);
        glEnd();
    }

    // ==================================
    // 2. Momentum/velocity vector (cyan)
    // ==================================
    float velMag = glm::length(velocity);
    if (velMag > 1e-10f)
    {
        glm::vec3 velDir = velocity / velMag;

        // Scale velocity vector - use log scale
        float velLength = baseScale * std::log10(1.0f + velMag * 10.0f);
        velLength = std::min(velLength, displayRadius * 20.0f); // Cap length

        glm::vec3 velEnd = position + velDir * velLength;

        // Draw velocity vector line (cyan)
        glColor3f(0.2f, 0.9f, 1.0f);
        glBegin(GL_LINES);
        glVertex3f(position.x, position.y, position.z);
        glVertex3f(velEnd.x, velEnd.y, velEnd.z);
        glEnd();

        // Draw arrowhead
        glm::vec3 perpX, perpY;
        if (fabs(velDir.y) < 0.9f)
        {
            perpX = glm::normalize(glm::cross(velDir, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        else
        {
            perpX = glm::normalize(glm::cross(velDir, glm::vec3(1.0f, 0.0f, 0.0f)));
        }
        perpY = glm::normalize(glm::cross(velDir, perpX));

        float arrowSize = velLength * 0.15f;
        glm::vec3 arrowBase = velEnd - velDir * arrowSize;

        glBegin(GL_TRIANGLES);
        glVertex3f(velEnd.x, velEnd.y, velEnd.z);
        glVertex3f(arrowBase.x + perpX.x * arrowSize * 0.4f,
                   arrowBase.y + perpX.y * arrowSize * 0.4f,
                   arrowBase.z + perpX.z * arrowSize * 0.4f);
        glVertex3f(arrowBase.x - perpX.x * arrowSize * 0.4f,
                   arrowBase.y - perpX.y * arrowSize * 0.4f,
                   arrowBase.z - perpX.z * arrowSize * 0.4f);

        glVertex3f(velEnd.x, velEnd.y, velEnd.z);
        glVertex3f(arrowBase.x + perpY.x * arrowSize * 0.4f,
                   arrowBase.y + perpY.y * arrowSize * 0.4f,
                   arrowBase.z + perpY.z * arrowSize * 0.4f);
        glVertex3f(arrowBase.x - perpY.x * arrowSize * 0.4f,
                   arrowBase.y - perpY.y * arrowSize * 0.4f,
                   arrowBase.z - perpY.z * arrowSize * 0.4f);
        glEnd();
    }

    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}


// ============================================================================
// Barycenter Calculation Functions
// ============================================================================

glm::vec3 computeBarycenter(const std::vector<CelestialBody *> &bodies)
{
    glm::dvec3 weightedSum(0.0);
    double totalMass = 0.0;

    for (const CelestialBody *body : bodies)
    {
        weightedSum += glm::dvec3(body->position) * body->mass;
        totalMass += body->mass;
    }

    if (totalMass > 0.0)
    {
        return glm::vec3(weightedSum / totalMass);
    }
    return glm::vec3(0.0f);
}

void computePlanetaryBarycenter(CelestialBody &primary, const std::vector<CelestialBody *> &moons)
{
    if (moons.empty())
    {
        primary.barycenter = std::nullopt;
        primary.barycenterDisplayRadius = 0.0f;
        return;
    }

    // Create list including primary and all moons
    std::vector<CelestialBody *> system;
    system.push_back(&primary);
    for (CelestialBody *moon : moons)
    {
        system.push_back(moon);
    }

    primary.barycenter = computeBarycenter(system);
    primary.barycenterDisplayRadius = primary.displayRadius * 0.5f; // Half the planet's radius
}

// ============================================================================
// Magnetic Field Methods
// ============================================================================

void CelestialBody::setMagneticFieldModel(std::shared_ptr<MagneticFieldModel> model)
{
    magneticField = model;
    cachedFieldLines.clear();
    fieldLinesYear = 0.0;
}

glm::dvec3 CelestialBody::computeMagneticField(const glm::vec3 &localPos, double yearFraction) const
{
    if (!magneticField)
    {
        return glm::dvec3(0.0);
    }

    // Convert from display units to km
    // displayRadius is in display units, we need to know the real radius in km
    // For Earth, reference radius is ~6371 km
    double refRadiusKm = magneticField->getReferenceRadius();
    double scale = refRadiusKm / static_cast<double>(displayRadius);

    // Convert local position to km in body-centered coords
    // Note: Our display coords have Y up, IGRF uses Z toward north pole
    // Display coords are right-handed with Z negated from J2000, so:
    // Display X -> IGRF X, Display Z -> IGRF -Y, Display Y -> IGRF Z
    glm::dvec3 posKm(static_cast<double>(localPos.x) * scale,
                     -static_cast<double>(localPos.z) * scale, // Display Z -> IGRF -Y (negated for right-handedness)
                     static_cast<double>(localPos.y) * scale   // Display Y -> IGRF Z (north)
    );

    return magneticField->computeField(posKm, yearFraction);
}

void CelestialBody::updateFieldLines(double yearFraction, int numLatitudes, int numLongitudes)
{
    if (!magneticField)
    {
        cachedFieldLines.clear();
        return;
    }

    // Only recompute if year changed significantly (more than 0.1 year)
    if (std::abs(yearFraction - fieldLinesYear) < 0.1 && !cachedFieldLines.empty())
    {
        return;
    }

    // Generate field lines using magnetosphere extent (L1 distance) as boundary
    // If magnetosphereExtentKm is 0, generateFieldLines will use a default
    cachedFieldLines =
        generateFieldLines(*magneticField, yearFraction, numLatitudes, numLongitudes, 100.0, magnetosphereExtentKm);
    fieldLinesYear = yearFraction;
}

void CelestialBody::drawMagneticFieldLines() const
{
    // Note: We don't check showMagneticField here - the caller should check
    // the global g_showMagneticFields flag before calling this method
    if (cachedFieldLines.empty() || !magneticField)
    {
        return;
    }

    // Scale factor from km to display units
    double refRadiusKm = magneticField->getReferenceRadius();
    float scale = displayRadius / static_cast<float>(refRadiusKm);

    glDisable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST); // Enable depth test so lines behind planet are occluded
    glDepthMask(GL_FALSE);   // Don't write to depth buffer (transparent lines)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.5f); // Thicker lines for visibility

    // Define colors for magnetic field visualization
    // Positive (north) pole color: warm red/orange
    const glm::vec3 positiveColor(1.0f, 0.3f, 0.1f);  // Red-orange
    const glm::vec3 positiveDark(0.6f, 0.15f, 0.05f); // Dark red (for gradient end)

    // Negative (south) pole color: cool blue/cyan
    const glm::vec3 negativeColor(0.1f, 0.5f, 1.0f);  // Blue
    const glm::vec3 negativeDark(0.05f, 0.25f, 0.6f); // Dark blue (for gradient end)

    for (const auto &line : cachedFieldLines)
    {
        if (line.points.size() < 2)
            continue;

        size_t numPoints = line.points.size();

        glBegin(GL_LINE_STRIP);
        for (size_t i = 0; i < numPoints; ++i)
        {
            const auto &pt = line.points[i];

            // Calculate normalized position along the line [0, 1]
            float t = static_cast<float>(i) / static_cast<float>(numPoints - 1);

            glm::vec3 color;
            float alpha = 0.85f;

            if (line.reachesOtherPole)
            {
                // Connected loop: gradient from positive to negative color
                // Line goes from one pole to the other
                if (line.startedFromNorth)
                {
                    // North to South: positive -> negative
                    color = glm::mix(positiveColor, negativeColor, t);
                }
                else
                {
                    // South to North: negative -> positive
                    color = glm::mix(negativeColor, positiveColor, t);
                }
                alpha = 0.9f; // Slightly more opaque for connected lines
            }
            else
            {
                // Disconnected line (open field line going to infinity)
                if (line.startedFromNorth)
                {
                    // Positive (north) disconnected: bright to dark positive
                    color = glm::mix(positiveColor, positiveDark, t);
                }
                else
                {
                    // Negative (south) disconnected: bright to dark negative
                    color = glm::mix(negativeColor, negativeDark, t);
                }
                alpha = 0.75f; // Slightly more transparent for open lines
            }

            glColor4f(color.r, color.g, color.b, alpha);

            // Convert from IGRF coords (Z north) to display coords (Y up)
            // IGRF X -> Display X, IGRF Z -> Display Y, IGRF Y -> Display -Z
            // And scale from km to display units
            float x = static_cast<float>(pt.x) * scale + position.x;
            float y = static_cast<float>(pt.z) * scale + position.y; // IGRF Z -> Display Y
            float z =
                -static_cast<float>(pt.y) * scale + position.z; // IGRF Y -> Display -Z (negated for right-handedness)

            glVertex3f(x, y, z);
        }
        glEnd();
    }

    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE); // Restore depth writing
    glEnable(GL_LIGHTING);
}

// ============================================================================
// Coordinate Grid Implementation
// ============================================================================

// Helper: Draw billboard text at a 3D position
static void DrawBillboardLabel(const glm::vec3 &worldPos,
                               const std::string &text,
                               const glm::vec3 &cameraPos,
                               const glm::vec3 &cameraFront,
                               const glm::vec3 &cameraUp,
                               float scale,
                               const glm::vec3 &textColor)
{
    // Calculate billboard orientation
    glm::vec3 toCamera = glm::normalize(cameraPos - worldPos);
    glm::vec3 right = glm::normalize(glm::cross(cameraUp, toCamera));
    glm::vec3 up = glm::normalize(glm::cross(toCamera, right));

    // Character dimensions
    float charWidth = scale * 0.6f;
    float charHeight = scale;
    float totalWidth = text.length() * charWidth;

    // Center the text
    glm::vec3 startPos = worldPos - right * (totalWidth * 0.5f);

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3f(textColor.r, textColor.g, textColor.b);

    // Draw each character as simple lines (basic font)
    for (size_t i = 0; i < text.length(); ++i)
    {
        glm::vec3 charPos = startPos + right * (i * charWidth + charWidth * 0.5f);
        char c = text[i];

        glLineWidth(1.5f);
        glBegin(GL_LINES);

        // Simple stroke font for digits and degree symbol
        float h = charHeight * 0.5f;
        float w = charWidth * 0.4f;

        if (c >= '0' && c <= '9')
        {
            // Draw digit outlines
            switch (c)
            {
            case '0':
                // Top
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                // Right
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                // Bottom
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                // Left
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                break;
            case '1':
                glVertex3f(charPos.x + h * up.x, charPos.y + h * up.y, charPos.z + h * up.z);
                glVertex3f(charPos.x - h * up.x, charPos.y - h * up.y, charPos.z - h * up.z);
                break;
            case '2':
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                break;
            case '3':
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                break;
            case '4':
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                break;
            case '5':
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                break;
            case '6':
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                break;
            case '7':
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                break;
            case '8':
                // Full 8 shape
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                break;
            case '9':
                glVertex3f(charPos.x - w * right.x - h * up.x,
                           charPos.y - w * right.y - h * up.y,
                           charPos.z - w * right.z - h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                glVertex3f(charPos.x + w * right.x - h * up.x,
                           charPos.y + w * right.y - h * up.y,
                           charPos.z + w * right.z - h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x + w * right.x + h * up.x,
                           charPos.y + w * right.y + h * up.y,
                           charPos.z + w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x + h * up.x,
                           charPos.y - w * right.y + h * up.y,
                           charPos.z - w * right.z + h * up.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
                glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
                break;
            }
        }
        else if (c == '-')
        {
            glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
            glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
        }
        else if (c == 'N' || c == 'n')
        {
            glVertex3f(charPos.x - w * right.x - h * up.x,
                       charPos.y - w * right.y - h * up.y,
                       charPos.z - w * right.z - h * up.z);
            glVertex3f(charPos.x - w * right.x + h * up.x,
                       charPos.y - w * right.y + h * up.y,
                       charPos.z - w * right.z + h * up.z);
            glVertex3f(charPos.x - w * right.x + h * up.x,
                       charPos.y - w * right.y + h * up.y,
                       charPos.z - w * right.z + h * up.z);
            glVertex3f(charPos.x + w * right.x - h * up.x,
                       charPos.y + w * right.y - h * up.y,
                       charPos.z + w * right.z - h * up.z);
            glVertex3f(charPos.x + w * right.x - h * up.x,
                       charPos.y + w * right.y - h * up.y,
                       charPos.z + w * right.z - h * up.z);
            glVertex3f(charPos.x + w * right.x + h * up.x,
                       charPos.y + w * right.y + h * up.y,
                       charPos.z + w * right.z + h * up.z);
        }
        else if (c == 'S' || c == 's')
        {
            // Same as 5
            glVertex3f(charPos.x + w * right.x + h * up.x,
                       charPos.y + w * right.y + h * up.y,
                       charPos.z + w * right.z + h * up.z);
            glVertex3f(charPos.x - w * right.x + h * up.x,
                       charPos.y - w * right.y + h * up.y,
                       charPos.z - w * right.z + h * up.z);
            glVertex3f(charPos.x - w * right.x + h * up.x,
                       charPos.y - w * right.y + h * up.y,
                       charPos.z - w * right.z + h * up.z);
            glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
            glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
            glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
            glVertex3f(charPos.x + w * right.x, charPos.y + w * right.y, charPos.z + w * right.z);
            glVertex3f(charPos.x + w * right.x - h * up.x,
                       charPos.y + w * right.y - h * up.y,
                       charPos.z + w * right.z - h * up.z);
            glVertex3f(charPos.x + w * right.x - h * up.x,
                       charPos.y + w * right.y - h * up.y,
                       charPos.z + w * right.z - h * up.z);
            glVertex3f(charPos.x - w * right.x - h * up.x,
                       charPos.y - w * right.y - h * up.y,
                       charPos.z - w * right.z - h * up.z);
        }
        else if (c == 'E' || c == 'e')
        {
            glVertex3f(charPos.x + w * right.x + h * up.x,
                       charPos.y + w * right.y + h * up.y,
                       charPos.z + w * right.z + h * up.z);
            glVertex3f(charPos.x - w * right.x + h * up.x,
                       charPos.y - w * right.y + h * up.y,
                       charPos.z - w * right.z + h * up.z);
            glVertex3f(charPos.x - w * right.x + h * up.x,
                       charPos.y - w * right.y + h * up.y,
                       charPos.z - w * right.z + h * up.z);
            glVertex3f(charPos.x - w * right.x - h * up.x,
                       charPos.y - w * right.y - h * up.y,
                       charPos.z - w * right.z - h * up.z);
            glVertex3f(charPos.x - w * right.x, charPos.y - w * right.y, charPos.z - w * right.z);
            glVertex3f(charPos.x + w * right.x * 0.5f, charPos.y + w * right.y * 0.5f, charPos.z + w * right.z * 0.5f);
            glVertex3f(charPos.x - w * right.x - h * up.x,
                       charPos.y - w * right.y - h * up.y,
                       charPos.z - w * right.z - h * up.z);
            glVertex3f(charPos.x + w * right.x - h * up.x,
                       charPos.y + w * right.y - h * up.y,
                       charPos.z + w * right.z - h * up.z);
        }
        else if (c == 'W' || c == 'w')
        {
            glVertex3f(charPos.x - w * right.x + h * up.x,
                       charPos.y - w * right.y + h * up.y,
                       charPos.z - w * right.z + h * up.z);
            glVertex3f(charPos.x - w * right.x * 0.5f - h * up.x,
                       charPos.y - w * right.y * 0.5f - h * up.y,
                       charPos.z - w * right.z * 0.5f - h * up.z);
            glVertex3f(charPos.x - w * right.x * 0.5f - h * up.x,
                       charPos.y - w * right.y * 0.5f - h * up.y,
                       charPos.z - w * right.z * 0.5f - h * up.z);
            glVertex3f(charPos.x, charPos.y, charPos.z);
            glVertex3f(charPos.x, charPos.y, charPos.z);
            glVertex3f(charPos.x + w * right.x * 0.5f - h * up.x,
                       charPos.y + w * right.y * 0.5f - h * up.y,
                       charPos.z + w * right.z * 0.5f - h * up.z);
            glVertex3f(charPos.x + w * right.x * 0.5f - h * up.x,
                       charPos.y + w * right.y * 0.5f - h * up.y,
                       charPos.z + w * right.z * 0.5f - h * up.z);
            glVertex3f(charPos.x + w * right.x + h * up.x,
                       charPos.y + w * right.y + h * up.y,
                       charPos.z + w * right.z + h * up.z);
        }

        glEnd();
    }

    glLineWidth(1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

// Structure to hold label info for sorting by distance
struct GridLabel
{
    glm::vec3 position;
    std::string text;
    float distanceToCamera;
};

void CelestialBody::drawCoordinateGrid(const glm::vec3 &cameraPos,
                                       const glm::vec3 &cameraFront,
                                       const glm::vec3 &cameraUp) const
{
    if (!showCoordinateGrid)
        return;

    // Get coordinate basis from pole and prime meridian directions
    // These are updated each frame from SPICE, so the grid rotates with the planet
    glm::vec3 north = glm::normalize(poleDirection);

    // Use the actual prime meridian direction from SPICE (rotates with planet)
    // This is the X-axis of the body-fixed frame, pointing to 0° longitude
    glm::vec3 east = glm::normalize(primeMeridianDirection);

    // Ensure east is perpendicular to north (numerical stability)
    east = glm::normalize(east - glm::dot(east, north) * north);

    // Y-axis of body frame (90° East longitude at equator)
    glm::vec3 equatorY = glm::normalize(glm::cross(north, east));

    // Grid radius slightly above surface
    float gridRadius = displayRadius * 1.02f;

    // Collect all label positions for distance sorting
    std::vector<GridLabel> labels;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ==================================
    // Draw Latitude Lines (parallels)
    // ==================================
    // Draw at -60, -30, 0, 30, 60 degrees
    const int latitudes[] = {-60, -30, 0, 30, 60};
    const int numLatitudes = 5;
    const int latSegments = 64;

    for (int latIdx = 0; latIdx < numLatitudes; ++latIdx)
    {
        int latDeg = latitudes[latIdx];
        float latRad = glm::radians(static_cast<float>(latDeg));
        float cosLat = std::cos(latRad);
        float sinLat = std::sin(latRad);

        // Latitude circle radius
        float circleRadius = gridRadius * cosLat;
        float height = gridRadius * sinLat;

        // Color: equator is brighter
        if (latDeg == 0)
        {
            glColor4f(1.0f, 0.8f, 0.2f, 0.8f); // Yellow for equator
            glLineWidth(2.5f);
        }
        else
        {
            glColor4f(0.6f, 0.8f, 0.6f, 0.5f); // Green for other latitudes
            glLineWidth(1.5f);
        }

        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < latSegments; ++i)
        {
            float angle = 2.0f * static_cast<float>(PI) * static_cast<float>(i) / latSegments;
            glm::vec3 point = position + north * height + east * (circleRadius * std::cos(angle)) +
                              equatorY * (circleRadius * std::sin(angle));
            glVertex3f(point.x, point.y, point.z);
        }
        glEnd();

        // Add label at longitude 0
        glm::vec3 labelPos = position + north * height + east * circleRadius * 1.05f;

        std::string latLabel;
        if (latDeg > 0)
            latLabel = std::to_string(latDeg) + "N";
        else if (latDeg < 0)
            latLabel = std::to_string(-latDeg) + "S";
        else
            latLabel = "0";

        GridLabel label;
        label.position = labelPos;
        label.text = latLabel;
        label.distanceToCamera = glm::distance(labelPos, cameraPos);
        labels.push_back(label);
    }

    // ==================================
    // Draw Longitude Lines (meridians)
    // ==================================
    // Draw every 30 degrees (12 meridians)
    const int lonSegments = 48;

    for (int lonDeg = 0; lonDeg < 360; lonDeg += 30)
    {
        float lonRad = glm::radians(static_cast<float>(lonDeg));

        // Prime meridian (0 degrees) is brighter
        if (lonDeg == 0)
        {
            glColor4f(1.0f, 0.4f, 0.4f, 0.8f); // Red for prime meridian
            glLineWidth(2.5f);
        }
        else
        {
            glColor4f(0.6f, 0.6f, 0.8f, 0.5f); // Blue for other meridians
            glLineWidth(1.5f);
        }

        // Direction on equatorial plane for this longitude
        glm::vec3 lonDir = east * std::cos(lonRad) + equatorY * std::sin(lonRad);

        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= lonSegments; ++i)
        {
            // Latitude from -90 to +90
            float lat = static_cast<float>(PI) * (static_cast<float>(i) / lonSegments - 0.5f);
            float cosLat = std::cos(lat);
            float sinLat = std::sin(lat);

            glm::vec3 point = position + gridRadius * (north * sinLat + lonDir * cosLat);
            glVertex3f(point.x, point.y, point.z);
        }
        glEnd();

        // Add label at equator
        glm::vec3 labelPos = position + lonDir * gridRadius * 1.05f;

        std::string lonLabel;
        if (lonDeg == 0)
            lonLabel = "0";
        else if (lonDeg <= 180)
            lonLabel = std::to_string(lonDeg) + "E";
        else
            lonLabel = std::to_string(360 - lonDeg) + "W";

        GridLabel label;
        label.position = labelPos;
        label.text = lonLabel;
        label.distanceToCamera = glm::distance(labelPos, cameraPos);
        labels.push_back(label);
    }

    glLineWidth(1.0f);
    glDisable(GL_BLEND);

    // ==================================
    // Draw only the 5 closest labels
    // ==================================
    // Sort labels by distance to camera
    std::sort(labels.begin(), labels.end(), [](const GridLabel &a, const GridLabel &b) {
        return a.distanceToCamera < b.distanceToCamera;
    });

    // Draw only the closest 5 labels
    int numLabelsToDraw = std::min(5, static_cast<int>(labels.size()));
    float labelScale = displayRadius * 0.15f; // Scale labels relative to body size
    glm::vec3 textColor(1.0f, 1.0f, 0.9f);    // Light yellow text

    for (int i = 0; i < numLabelsToDraw; ++i)
    {
        DrawBillboardLabel(labels[i].position, labels[i].text, cameraPos, cameraFront, cameraUp, labelScale, textColor);
    }

    glEnable(GL_LIGHTING);
}
