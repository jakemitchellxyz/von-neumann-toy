#include "camera-controller.h"
#include "../materials/earth/economy/earth-economy.h"
#include "../types/celestial-body.h"
#include "app-state.h"
#include "constants.h"
#include "solar-lighting.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>


// ==================================
// Constructor
// ==================================
CameraController::CameraController()
    : moveSpeed(150.0f), rotateSpeed(0.15f), rollSpeed(1.0f),
      panSpeed(15.0f), scrollSpeed(500.0f), orbitSpeed(0.005f),
      maxRayDistance(static_cast<float>(PLUTO_SMA_AU * UNITS_PER_AU)), hoveredBody(nullptr), selectedBody(nullptr),
      contextMenuOpen(false), contextMenuBody(nullptr), contextMenuX(0.0), contextMenuY(0.0), isFocused(false),
      focusIsLagrangePoint(false), focusOffset(0.0f), followMode(CameraFollowMode::Fixed), lastJulianDate(0.0),
      pendingDeselect(false), focusedLagrangePosition(0.0f), focusedLagrangeRadius(1.0f), focusedLagrangeName(""),
      leftMousePressed(false), rightMousePressed(false), altKeyPressed(false), lastMouseX(0.0), lastMouseY(0.0),
      currentMouseX(0.0), currentMouseY(0.0), lastClickTime(0.0), defaultCursor(nullptr), pointerCursor(nullptr),
      screenWidth(1280.0f), screenHeight(720.0f), initialized(false), inputBlocked(false), surfaceLatitude(0.0f),
      surfaceLongitude(0.0f), surfaceAltitude(4.7e-7f) // ~2 meters above surface for Earth scale
      ,
      surfaceMoveSpeed(0.02f) // Radians per frame (about 1 degree)
      ,
      surfaceNormal(0.0f, 1.0f, 0.0f), surfaceNorth(0.0f, 0.0f, -1.0f), surfaceEast(1.0f, 0.0f, 0.0f),
      surfaceLocalYaw(0.0f), surfaceLocalPitch(90.0f) // Start looking straight up (along surface normal)
{
    // Camera state (position, yaw, pitch, roll, fov) is stored in APP_STATE.worldState.camera
    // and initialized in AppState constructor
}

// ==================================
// Destructor
// ==================================
CameraController::~CameraController()
{
    if (defaultCursor)
    {
        glfwDestroyCursor(defaultCursor);
        defaultCursor = nullptr;
    }
    if (pointerCursor)
    {
        glfwDestroyCursor(pointerCursor);
        pointerCursor = nullptr;
    }
}

// ==================================
// Initialize Camera for Earth View
// ==================================
void CameraController::initializeForEarth(const glm::vec3 &earthPos, float earthDisplayRadius)
{
    // Position camera as if focused on Earth (2x radius away, looking at it)
    float viewDistance = earthDisplayRadius * 2.0f;

    // Use a default viewing direction (slightly elevated, from positive X)
    glm::vec3 cameraDir = glm::normalize(glm::vec3(1.0f, 0.3f, 0.5f));

    // Position camera at the focus distance from Earth
    camera().position = earthPos + cameraDir * viewDistance;

    // Point camera directly at Earth
    glm::vec3 toEarth = glm::normalize(earthPos - camera().position);
    camera().yaw = glm::degrees(atan2(toEarth.z, toEarth.x));
    camera().pitch = glm::degrees(asin(toEarth.y));

    initialized = true;

    std::cout << "Camera positioned at: (" << camera().position.x << ", " << camera().position.y << ", " << camera().position.z << ")\n";
}

// ==================================
// Camera Direction Vectors
// ==================================
// These delegate to the CameraState methods which handle roll properly
glm::vec3 CameraController::getFront() const
{
    return camera().getFront();
}

glm::vec3 CameraController::getUp() const
{
    return camera().getUp();
}

glm::vec3 CameraController::getRight() const
{
    return camera().getRight();
}

glm::mat4 CameraController::getViewMatrix() const
{
    return camera().getViewMatrix();
}

// ==================================
// Focus Camera on Body
// ==================================
void CameraController::focusOnBody(const CelestialBody *body)
{
    if (!body)
        return;

    // Calculate view distance based on body radius
    // Use 2x radius as base distance, with minimum of 3x radius for very small bodies
    float viewDistance = body->displayRadius * 2.0f;
    viewDistance = std::max(viewDistance, body->displayRadius * 3.0f);

    glm::vec3 cameraDir;

    // For planets (non-sun bodies), position camera between body and sun
    // This ensures the sun is visible behind the planet, creating a nice lighting effect
    // The camera distance from the body is always based on the body's radius
    if (body->name != "Sun" && !body->isEmissive)
    {
        glm::vec3 sunPos = SolarLighting::getSunPosition();
        glm::vec3 sunToBody = body->position - sunPos;
        float sunToBodyDist = glm::length(sunToBody);

        // If sun and body are too close or degenerate, fall back to default
        if (sunToBodyDist > 0.001f)
        {
            // Direction from sun to body (normalized)
            glm::vec3 sunToBodyDir = sunToBody / sunToBodyDist;

            // Position camera along the line from sun to body, at the radius-based distance from the body
            // Camera direction points from body toward sun (negated), so camera is between sun and body
            // This means: Sun --- Camera --- Body, with camera at viewDistance from Body
            cameraDir = -sunToBodyDir; // Negative because we want to be on the opposite side from sun
        }
        else
        {
            // Fallback: use default direction if sun and body are too close
            cameraDir = glm::vec3(1.0f, 0.3f, 0.0f);
            cameraDir = glm::normalize(cameraDir);
        }
    }
    else
    {
        // For the Sun or emissive bodies, use current camera direction or default
        cameraDir = glm::normalize(camera().position - body->position);

        // If camera is too close or direction is degenerate, use a default direction
        if (glm::length(camera().position - body->position) < 0.01f || glm::any(glm::isnan(cameraDir)))
        {
            cameraDir = glm::vec3(1.0f, 0.3f, 0.0f); // Default viewing angle
            cameraDir = glm::normalize(cameraDir);
        }
    }

    // Position camera at the radius-based distance from the body
    // This ensures consistent viewing distance regardless of sun position
    camera().position = body->position + cameraDir * viewDistance;

    // Point camera at the body
    glm::vec3 toBody = glm::normalize(body->position - camera().position);
    camera().yaw = glm::degrees(atan2(toBody.z, toBody.x));
    camera().pitch = glm::degrees(asin(toBody.y));

    // Enable focus tracking - camera will follow this body
    isFocused = true;
    focusIsLagrangePoint = false;
    // Store fixed offset from body to camera (camera always at body + offset)
    focusOffset = camera().position - body->position;
}

// ==================================
// Focus on Lagrange Point
// ==================================
void CameraController::focusOnLagrangePoint(const glm::vec3 &pos, float displayRadius, const std::string &name)
{
    // Calculate view distance (Lagrange points are small, need good viewing distance)
    float viewDistance = std::max(displayRadius * 15.0f, 10.0f);

    // Use current camera direction to determine where to place camera
    glm::vec3 cameraDir = glm::normalize(camera().position - pos);

    // If camera is too close or direction is degenerate, use a default direction
    if (glm::length(camera().position - pos) < 0.01f || glm::any(glm::isnan(cameraDir)))
    {
        cameraDir = glm::vec3(1.0f, 0.3f, 0.0f);
        cameraDir = glm::normalize(cameraDir);
    }

    // Position camera at the calculated distance
    camera().position = pos + cameraDir * viewDistance;

    // Point camera at the Lagrange point
    glm::vec3 toPoint = glm::normalize(pos - camera().position);
    camera().yaw = glm::degrees(atan2(toPoint.z, toPoint.x));
    camera().pitch = glm::degrees(asin(toPoint.y));

    // Enable focus tracking for Lagrange point
    isFocused = true;
    focusIsLagrangePoint = true;
    focusedLagrangePosition = pos;
    focusedLagrangeRadius = displayRadius;
    focusedLagrangeName = name;
    // Store fixed offset from Lagrange point to camera
    focusOffset = camera().position - pos;
}

// ==================================
// Update Focused Lagrange Position
// ==================================
void CameraController::updateFocusedLagrangePosition(const glm::vec3 &newPosition)
{
    focusedLagrangePosition = newPosition;
}

// ==================================
// Clear Focus State
// ==================================
void CameraController::clearFocus()
{
    isFocused = false;
    focusIsLagrangePoint = false;
    focusedLagrangeName = "";
}

// ==================================
// Process Pending Deselect
// ==================================
void CameraController::processPendingDeselect(bool uiConsumedClick)
{
    if (!pendingDeselect)
        return;

    pendingDeselect = false;

    if (uiConsumedClick)
    {
        // UI handled the click, don't deselect
        return;
    }

    // Perform the deselection
    selectedBody = nullptr;
    clearFocus();
}

// ==================================
// Update Camera to Follow Body
// ==================================
void CameraController::updateFollowTarget(double currentJD)
{
    // Only follow if focused
    if (!isFocused)
    {
        lastJulianDate = currentJD;
        return;
    }

    if (focusIsLagrangePoint)
    {
        // Following a Lagrange point - camera = point + offset
        camera().position = focusedLagrangePosition + focusOffset;
    }
    else if (selectedBody != nullptr)
    {
        // Following a celestial body

        if (followMode == CameraFollowMode::Surface)
        {
            // Surface view mode: camera is on the surface, looking outward
            // Position is computed from lat/lon on body's surface

            // Get body-fixed coordinate frame
            glm::vec3 pole = glm::normalize(selectedBody->poleDirection);
            glm::vec3 primeMeridian = glm::normalize(selectedBody->primeMeridianDirection);
            glm::vec3 bodyEast = glm::normalize(glm::cross(pole, primeMeridian));

            // Compute surface position in body-fixed coordinates
            float cosLat = cos(surfaceLatitude);
            float sinLat = sin(surfaceLatitude);
            float cosLon = cos(surfaceLongitude);
            float sinLon = sin(surfaceLongitude);

            // Direction from center to surface point (this is also the surface normal)
            surfaceNormal = cosLat * (cosLon * primeMeridian + sinLon * bodyEast) + sinLat * pole;
            surfaceNormal = glm::normalize(surfaceNormal);

            // Update local tangent frame for the new surface position
            // surfaceNorth: direction toward the pole along the surface
            glm::vec3 poleProjection = pole - glm::dot(pole, surfaceNormal) * surfaceNormal;
            if (glm::length(poleProjection) > 0.001f)
            {
                surfaceNorth = glm::normalize(poleProjection);
            }
            else
            {
                // At poles, use prime meridian direction
                surfaceNorth = glm::normalize(primeMeridian - glm::dot(primeMeridian, surfaceNormal) * surfaceNormal);
            }
            // surfaceEast: perpendicular to normal and north
            surfaceEast = glm::normalize(glm::cross(surfaceNormal, surfaceNorth));

            // Camera position: on surface + absolute altitude offset
            float distanceFromCenter = selectedBody->displayRadius + surfaceAltitude;
            camera().position = selectedBody->position + surfaceNormal * distanceFromCenter;

            // Store offset for consistency with other modes
            focusOffset = camera().position - selectedBody->position;

            // Update world orientation from local surface coordinates
            // This keeps the camera looking in the same local direction as the planet rotates
            updateWorldOrientationFromSurface();
        }
        else if (followMode == CameraFollowMode::Geostationary && lastJulianDate > 0.0)
        {
            // Geostationary mode: rotate the offset around the body's pole
            double deltaJD = currentJD - lastJulianDate;

            if (std::abs(deltaJD) > 0.0 && selectedBody->rotationPeriod > 0.0)
            {
                // Calculate rotation angle (rotationPeriod in hours, deltaJD in days)
                double deltaHours = deltaJD * 24.0;
                float rotationAngle = static_cast<float>((deltaHours / selectedBody->rotationPeriod) * 2.0 * PI);

                // Rotate offset around pole axis using Rodrigues' formula
                glm::vec3 axis = glm::normalize(selectedBody->poleDirection);
                float cosA = cos(rotationAngle);
                float sinA = sin(rotationAngle);

                focusOffset = focusOffset * cosA + glm::cross(axis, focusOffset) * sinA +
                              axis * glm::dot(axis, focusOffset) * (1.0f - cosA);

                // Also rotate camera view direction
                glm::vec3 front = getFront();
                glm::vec3 rotatedFront =
                    front * cosA + glm::cross(axis, front) * sinA + axis * glm::dot(axis, front) * (1.0f - cosA);
                rotatedFront = glm::normalize(rotatedFront);
                camera().yaw = glm::degrees(atan2(rotatedFront.z, rotatedFront.x));
                camera().pitch = glm::degrees(asin(glm::clamp(rotatedFront.y, -1.0f, 1.0f)));
            }

            // Set camera position from offset
            camera().position = selectedBody->position + focusOffset;
        }
        else
        {
            // Fixed mode: just follow without rotation
            camera().position = selectedBody->position + focusOffset;
        }
    }

    lastJulianDate = currentJD;
}

// ==================================
// Toggle Follow Mode
// ==================================
void CameraController::toggleFollowMode()
{
    if (followMode == CameraFollowMode::Fixed)
    {
        followMode = CameraFollowMode::Geostationary;
    }
    else if (followMode == CameraFollowMode::Geostationary)
    {
        followMode = CameraFollowMode::Fixed;
    }
    // Surface mode is toggled separately via enterSurfaceView/exitSurfaceView
}

// ==================================
// Enter Surface View Mode
// ==================================
void CameraController::enterSurfaceView(CelestialBody *body, float latitude, float longitude)
{
    if (!body)
        return;

    // Select and focus on the body
    selectedBody = body;
    isFocused = true;
    focusIsLagrangePoint = false;

    // Set to surface mode
    followMode = CameraFollowMode::Surface;

    // ============================================================
    // Cast ray from camera to planet center to find surface point
    // ============================================================
    glm::vec3 cameraToCenter = body->position - camera().position;
    glm::vec3 rayDir = glm::normalize(cameraToCenter);

    // Ray-sphere intersection: find where ray hits planet surface
    // Ray: P(t) = position + t * rayDir
    // Sphere: |P - body->position|^2 = displayRadius^2
    glm::vec3 oc = camera().position - body->position;
    float a = glm::dot(rayDir, rayDir); // Should be 1.0 (normalized)
    float b = 2.0f * glm::dot(oc, rayDir);
    float c = glm::dot(oc, oc) - body->displayRadius * body->displayRadius;
    float discriminant = b * b - 4.0f * a * c;

    glm::vec3 surfacePoint;
    if (discriminant >= 0.0f)
    {
        // Ray intersects sphere - use the closer intersection point
        float t = (-b - sqrt(discriminant)) / (2.0f * a);
        surfacePoint = camera().position + t * rayDir;

        // Surface normal points outward from center
        surfaceNormal = glm::normalize(surfacePoint - body->position);
    }
    else
    {
        // Ray doesn't intersect (camera might be inside or very far)
        // Fall back to using lat/lon if provided, otherwise use direction from center
        if (latitude != 0.0f || longitude != 0.0f)
        {
            // Use provided lat/lon
            surfaceLatitude = latitude;
            surfaceLongitude = longitude;

            glm::vec3 pole = glm::normalize(body->poleDirection);
            glm::vec3 primeMeridian = glm::normalize(body->primeMeridianDirection);
            glm::vec3 bodyEast = glm::normalize(glm::cross(pole, primeMeridian));

            float cosLat = cos(surfaceLatitude);
            float sinLat = sin(surfaceLatitude);
            float cosLon = cos(surfaceLongitude);
            float sinLon = sin(surfaceLongitude);

            surfaceNormal = cosLat * (cosLon * primeMeridian + sinLon * bodyEast) + sinLat * pole;
            surfaceNormal = glm::normalize(surfaceNormal);
            surfacePoint = body->position + surfaceNormal * body->displayRadius;
        }
        else
        {
            // Use direction from center to camera as fallback
            surfaceNormal = glm::normalize(camera().position - body->position);
            surfacePoint = body->position + surfaceNormal * body->displayRadius;
        }
    }

    // ============================================================
    // Compute local coordinate frame at surface point
    // ============================================================
    glm::vec3 pole = glm::normalize(body->poleDirection);
    glm::vec3 primeMeridian = glm::normalize(body->primeMeridianDirection);

    // Local "north" direction: project pole onto tangent plane
    glm::vec3 poleProjection = pole - glm::dot(pole, surfaceNormal) * surfaceNormal;
    if (glm::length(poleProjection) > 0.001f)
    {
        surfaceNorth = glm::normalize(poleProjection);
    }
    else
    {
        // At poles, use prime meridian direction as "north"
        surfaceNorth = glm::normalize(primeMeridian - glm::dot(primeMeridian, surfaceNormal) * surfaceNormal);
    }

    // Local "east" direction: perpendicular to both normal and north
    // This points 90 degrees eastward (towards the horizon)
    surfaceEast = glm::normalize(glm::cross(surfaceNormal, surfaceNorth));

    // ============================================================
    // Position camera 2 meters above surface point
    // ============================================================
    float distanceFromCenter = body->displayRadius + surfaceAltitude; // surfaceAltitude is ~2 meters
    camera().position = body->position + surfaceNormal * distanceFromCenter;
    focusOffset = camera().position - body->position;

    // ============================================================
    // Calculate lat/lon for the surface point (for display)
    // ============================================================
    // Convert surface normal to lat/lon in body-fixed coordinates
    glm::vec3 bodyEastGlobal = glm::normalize(glm::cross(pole, primeMeridian));
    float dotPole = glm::dot(surfaceNormal, pole);
    surfaceLatitude = asin(glm::clamp(dotPole, -1.0f, 1.0f));

    glm::vec3 projToEquator = surfaceNormal - dotPole * pole;
    float projLen = glm::length(projToEquator);
    if (projLen > 0.001f)
    {
        projToEquator /= projLen;
        float dotPrimeMeridian = glm::dot(projToEquator, primeMeridian);
        float dotBodyEast = glm::dot(projToEquator, bodyEastGlobal);
        surfaceLongitude = atan2(dotBodyEast, dotPrimeMeridian);
    }
    else
    {
        surfaceLongitude = 0.0f; // At pole, longitude is undefined
    }

    std::cout << "Surface view: ray-cast from camera to center, hit surface at lat=" << glm::degrees(surfaceLatitude)
              << "°, lon=" << glm::degrees(surfaceLongitude) << "°" << std::endl;
    std::cout << "  Camera positioned 2m above surface, facing east (towards horizon)" << std::endl;

    // ============================================================
    // Orient camera to look 90 degrees eastward (towards horizon)
    // ============================================================
    // surfaceEast points eastward along the horizon
    // We want to look in that direction, with pitch at minimum to see horizon
    surfaceLocalYaw = 90.0f; // 90 degrees = eastward
    // Set pitch to minimum allowed (FOV/2) so horizon is visible at bottom of screen
    // This prevents frustum from clipping through planet while still showing horizon
    surfaceLocalPitch = camera().fov / 2.0f;

    // Clamp to valid range and convert to world coordinates
    clampSurfaceOrientation();
    updateWorldOrientationFromSurface();
    camera().roll = 0.0f;

    std::cout << "Entered surface view at lat=" << glm::degrees(surfaceLatitude)
              << "°, lon=" << glm::degrees(surfaceLongitude) << "°"
              << " looking toward horizon" << std::endl;
}

// ==================================
// Exit Surface View Mode
// ==================================
void CameraController::exitSurfaceView()
{
    if (followMode != CameraFollowMode::Surface)
        return;

    // Switch back to geostationary (stay over same point but elevated)
    followMode = CameraFollowMode::Geostationary;

    if (selectedBody)
    {
        // Position camera above the surface point at 2x radius
        float viewDistance = selectedBody->displayRadius * 2.0f;
        focusOffset = glm::normalize(focusOffset) * viewDistance;

        // Point camera at the body
        glm::vec3 toBody = glm::normalize(-focusOffset);
        camera().yaw = glm::degrees(atan2(toBody.z, toBody.x));
        camera().pitch = glm::degrees(asin(toBody.y));
    }

    std::cout << "Exited surface view" << std::endl;
}

// ==================================
// Update World Orientation from Surface Local Coordinates
// ==================================
// Converts surfaceLocalYaw and surfaceLocalPitch to world yaw/pitch
// Local coordinate system:
//   - surfaceNormal: local "up" (away from planet center)
//   - surfaceNorth: local "north" (toward pole along surface)
//   - surfaceEast: local "east" (perpendicular to north, right-hand rule)
// Local angles:
//   - surfaceLocalYaw: 0° = north, 90° = east, 180° = south, 270° = west
//   - surfaceLocalPitch: 0° = horizon, 90° = zenith (straight up)
void CameraController::updateWorldOrientationFromSurface()
{
    // Convert local yaw/pitch to a look direction in world space
    float yawRad = glm::radians(surfaceLocalYaw);
    float pitchRad = glm::radians(surfaceLocalPitch);

    // Horizontal direction on the surface (in the tangent plane)
    glm::vec3 horizontalDir = cosf(yawRad) * surfaceNorth + sinf(yawRad) * surfaceEast;

    // Final look direction: blend between horizontal and up based on pitch
    // pitch 0° = looking at horizon (horizontalDir)
    // pitch 90° = looking straight up (surfaceNormal)
    glm::vec3 lookDir = cosf(pitchRad) * horizontalDir + sinf(pitchRad) * surfaceNormal;
    lookDir = glm::normalize(lookDir);

    // Convert look direction to world yaw/pitch
    camera().yaw = glm::degrees(atan2(lookDir.z, lookDir.x));
    camera().pitch = glm::degrees(asin(glm::clamp(lookDir.y, -1.0f, 1.0f)));
}

// ==================================
// Clamp Surface View Orientation
// ==================================
// Prevents the camera from looking below the horizon (which would clip through the planet)
// Accounts for FOV so the frustum never penetrates the surface
void CameraController::clampSurfaceOrientation()
{
    // Minimum pitch: just above horizon, accounting for FOV
    // The bottom of the frustum is at (pitch - FOV/2)
    // To keep bottom above horizon (0°): pitch - FOV/2 >= 0, so pitch >= FOV/2
    float minPitch = camera().fov / 2.0f;

    // Maximum pitch: straight up (90°)
    float maxPitch = 90.0f;

    // Clamp local pitch
    surfaceLocalPitch = glm::clamp(surfaceLocalPitch, minPitch, maxPitch);

    // Local yaw: full 360° rotation allowed (no clamping needed for yaw)
    // Wrap yaw to [0, 360)
    while (surfaceLocalYaw < 0.0f)
        surfaceLocalYaw += 360.0f;
    while (surfaceLocalYaw >= 360.0f)
        surfaceLocalYaw -= 360.0f;
}

// ==================================
// Proximity-Based Speed Multiplier
// ==================================
// Slows camera movement when close to a focused body's surface
// Prevents camera from clipping through the body
//
// Near plane is dynamically computed based on altitude above surface.
// This allows the camera to get as close as human head height (6 feet) from planets.

// Scale factors for dynamic near plane computation
// Earth's display radius (1.5) corresponds to 6,371 km real radius
// So 1 display unit ≈ 4,247 km for Earth
// 1 meter ≈ 2.35e-7 display units for Earth
// 2 meters ≈ 4.7e-7 display units for Earth
static constexpr float DEFAULT_NEAR_PLANE = 0.1f; // Used when far from any surface
static constexpr float MIN_NEAR_PLANE = 4.7e-7f;  // ~2 meters in Earth scale (allows ground-level viewing)
static constexpr float MIN_ALTITUDE = 1.2e-4f;    // ~0.5 km minimum altitude from surface
static constexpr float NEAR_PLANE_ALTITUDE_RATIO =
    0.05f; // Near plane = 5% of altitude (more stable for very close views)

float CameraController::getProximitySpeedMultiplier(float *outMinDistance) const
{
    // Only apply proximity slowdown when focused on a celestial body
    if (!isFocused || focusIsLagrangePoint || selectedBody == nullptr)
    {
        if (outMinDistance)
            *outMinDistance = 0.0f;
        return 1.0f;
    }

    float bodyRadius = selectedBody->displayRadius;
    float distanceToCenter = glm::length(camera().position - selectedBody->position);
    float altitude = distanceToCenter - bodyRadius;

    // Minimum distance from center: 0.5km above the surface
    float minDistanceFromCenter = bodyRadius + MIN_ALTITUDE;
    if (outMinDistance)
        *outMinDistance = minDistanceFromCenter;

    // Slowdown zone extends from surface to 3x radius from center
    float slowdownRadius = bodyRadius * 3.0f;

    // If outside slowdown zone, full speed
    if (distanceToCenter >= slowdownRadius)
    {
        return 1.0f;
    }

    // Distance from the minimum allowed position
    float distanceFromMinimum = distanceToCenter - minDistanceFromCenter;

    // If at or below minimum, use minimum speed (allows still scrolling near surface)
    if (distanceFromMinimum <= 0.0f)
    {
        return 0.05f; // Minimum 5% speed - still allows slow zooming
    }

    // Linear interpolation: at minimum = 0.05, at 3x radius = 1.0
    float slowdownRange = slowdownRadius - minDistanceFromCenter;
    float speedMultiplier = distanceFromMinimum / slowdownRange;

    // Clamp to reasonable range (min 5% speed to allow zooming, max 1.0)
    return glm::clamp(speedMultiplier, 0.05f, 1.0f);
}

// ==================================
// Dynamic Near Plane Calculation
// ==================================
float CameraController::getDynamicNearPlane() const
{
    // If not focused on a body, use default near plane
    if (!isFocused || focusIsLagrangePoint || selectedBody == nullptr)
    {
        return DEFAULT_NEAR_PLANE;
    }

    float bodyRadius = selectedBody->displayRadius;
    float distanceToCenter = glm::length(camera().position - selectedBody->position);
    float altitude = distanceToCenter - bodyRadius;

    // If far from surface (altitude > default near plane), use default
    if (altitude > DEFAULT_NEAR_PLANE * 10.0f)
    {
        return DEFAULT_NEAR_PLANE;
    }

    // Near plane scales with altitude - allows rendering close to ground
    // Use 5% of altitude as near plane (more stable for very close views), with absolute minimum
    float nearPlane = altitude * NEAR_PLANE_ALTITUDE_RATIO;

    // Clamp between minimum (for numerical stability) and default
    return glm::clamp(nearPlane, MIN_NEAR_PLANE, DEFAULT_NEAR_PLANE);
}

bool CameraController::clampToSurface()
{
    if (!isFocused || focusIsLagrangePoint || selectedBody == nullptr)
    {
        return false;
    }

    float bodyRadius = selectedBody->displayRadius;

    // Minimum distance: 0.5km above the surface (MIN_ALTITUDE)
    float minDistance = bodyRadius + MIN_ALTITUDE;

    glm::vec3 toCamera = camera().position - selectedBody->position;
    float distanceToCenter = glm::length(toCamera);

    if (distanceToCenter < minDistance)
    {
        // Push camera out to minimum distance
        if (distanceToCenter > 0.001f)
        {
            glm::vec3 direction = toCamera / distanceToCenter;
            camera().position = selectedBody->position + direction * minDistance;
        }
        else
        {
            // Camera is at body center (degenerate case) - push out along arbitrary direction
            camera().position = selectedBody->position + glm::vec3(0.0f, 1.0f, 0.0f) * minDistance;
        }
        // Update focusOffset to match the clamped position
        focusOffset = camera().position - selectedBody->position;
        return true;
    }
    return false;
}

// ==================================
// Relative Speed Calculations
// ==================================
float CameraController::getRelativeSpeed() const
{
    if (selectedBody != nullptr)
    {
        // Speed is proportional to body's radius (move ~5% of radius per frame)
        float baseSpeed = selectedBody->displayRadius * 0.05f;
        // Apply proximity slowdown
        return baseSpeed * getProximitySpeedMultiplier();
    }
    return moveSpeed; // Default speed if no body selected
}

float CameraController::getRelativePanSpeed() const
{
    if (selectedBody != nullptr)
    {
        float baseSpeed = selectedBody->displayRadius * 0.02f; // 2% of radius per pixel
        // Apply proximity slowdown
        return baseSpeed * getProximitySpeedMultiplier();
    }
    return panSpeed;
}

float CameraController::getRelativeScrollSpeed() const
{
    if (selectedBody != nullptr)
    {
        float baseSpeed = selectedBody->displayRadius * 0.5f; // Half radius per scroll notch
        // Apply proximity slowdown
        float speed = baseSpeed * getProximitySpeedMultiplier();
        // Minimum scroll speed to allow zooming even when very close
        // ~50 meters per scroll notch at minimum (for Earth scale)
        float minScrollSpeed = MIN_ALTITUDE * 0.5f;
        return std::max(speed, minScrollSpeed);
    }
    return scrollSpeed;
}

// ==================================
// Keyboard Input Processing
// ==================================
void CameraController::processKeyboard(GLFWwindow *window)
{
    // Surface view mode: WASD moves across the surface using lat/lon
    // Space/Ctrl are disabled - you can't fly up from the surface
    if (followMode == CameraFollowMode::Surface && selectedBody != nullptr)
    {

        // W: Move north (increase latitude), capped at north pole
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        {
            surfaceLatitude += surfaceMoveSpeed;
        }
        // S: Move south (decrease latitude), capped at south pole
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        {
            surfaceLatitude -= surfaceMoveSpeed;
        }

        // Hard clamp latitude to poles - can't go past north or south pole
        static const float POLE_LIMIT = static_cast<float>(PI * 0.5) - 0.001f; // Just shy of exactly ±90°
        surfaceLatitude = glm::clamp(surfaceLatitude, -POLE_LIMIT, POLE_LIMIT);

        // A/D: Move west/east (change longitude) - wraps around, no cap
        // At higher latitudes, longitude changes faster to maintain consistent ground speed
        float latScale = 1.0f / std::max(0.1f, std::cos(surfaceLatitude));
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        {
            surfaceLongitude -= surfaceMoveSpeed * latScale;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        {
            surfaceLongitude += surfaceMoveSpeed * latScale;
        }

        // Wrap longitude continuously - can loop around the equator forever
        while (surfaceLongitude > PI)
            surfaceLongitude -= 2.0f * static_cast<float>(PI);
        while (surfaceLongitude < -PI)
            surfaceLongitude += 2.0f * static_cast<float>(PI);

        // Q/E: Roll camera around forward axis (works in surface mode too)
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        {
            camera().roll -= rollSpeed;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        {
            camera().roll += rollSpeed;
        }
        while (camera().roll > 180.0f)
            camera().roll -= 360.0f;
        while (camera().roll < -180.0f)
            camera().roll += 360.0f;

        // Space/Ctrl: Disabled in surface mode - explicitly ignored
        // (You can't fly up from the surface)

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, true);
        }

        return; // Don't process normal movement in surface mode
    }

    // Normal movement mode
    float speed = getRelativeSpeed();

    // Calculate camera's local axes
    glm::vec3 front = getFront();
    glm::vec3 right = getRight();
    glm::vec3 up = getUp();

    bool moved = false;

    // W/S: Move along camera's forward/backward axis
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    {
        camera().position += front * speed;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    {
        camera().position -= front * speed;
        moved = true;
    }

    // A/D: Move along camera's left/right axis
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
    {
        camera().position -= right * speed;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
    {
        camera().position += right * speed;
        moved = true;
    }

    // Space/Ctrl: Move along camera's up/down axis
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
    {
        camera().position += up * speed;
        moved = true;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
    {
        camera().position -= up * speed;
        moved = true;
    }

    // Q/E: Roll camera around forward axis
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
    {
        camera().roll -= rollSpeed; // Counter-clockwise roll (negative)
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
    {
        camera().roll += rollSpeed; // Clockwise roll (positive)
    }
    // Keep roll in reasonable range (-180 to 180)
    while (camera().roll > 180.0f)
        camera().roll -= 360.0f;
    while (camera().roll < -180.0f)
        camera().roll += 360.0f;

    // Ensure camera doesn't clip through body surface
    if (moved)
    {
        clampToSurface();
    }

    // If user manually moved camera, break focus tracking
    if (moved && isFocused)
    {
        isFocused = false;
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, true);
    }
}

// ==================================
// Raycasting
// ==================================
glm::vec3 CameraController::getMouseRayDirection() const
{
    // Convert screen coordinates to normalized device coordinates (-1 to 1)
    float ndcX = (2.0f * static_cast<float>(currentMouseX) / screenWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * static_cast<float>(currentMouseY) / screenHeight); // Flip Y

    // Calculate the ray direction based on FOV
    float aspect = screenWidth / screenHeight;
    float tanHalfFov = tan(glm::radians(camera().fov / 2.0f));

    // Get camera basis vectors
    glm::vec3 front = getFront();
    glm::vec3 right = getRight();
    glm::vec3 up = getUp();

    // Calculate ray direction: offset from forward based on NDC and FOV
    glm::vec3 rayDir = front + right * (ndcX * tanHalfFov * aspect) + up * (ndcY * tanHalfFov);

    return glm::normalize(rayDir);
}

float CameraController::raySphereIntersection(const glm::vec3 &rayOrigin,
                                              const glm::vec3 &rayDir,
                                              const glm::vec3 &sphereCenter,
                                              float sphereRadius)
{
    glm::vec3 oc = rayOrigin - sphereCenter;

    // Quadratic formula coefficients: at^2 + bt + c = 0
    float a = glm::dot(rayDir, rayDir);
    float b = 2.0f * glm::dot(oc, rayDir);
    float c = glm::dot(oc, oc) - sphereRadius * sphereRadius;

    float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f)
    {
        return -1.0f; // No intersection
    }

    float sqrtDisc = sqrt(discriminant);
    float t1 = (-b - sqrtDisc) / (2.0f * a);
    float t2 = (-b + sqrtDisc) / (2.0f * a);

    // Return closest positive intersection
    if (t1 > 0.0f)
        return t1;
    if (t2 > 0.0f)
        return t2;
    return -1.0f; // Both behind camera
}

void CameraController::updateRaycast(const std::vector<CelestialBody *> &bodies,
                                     GLFWwindow *window,
                                     bool skipIfMouseOverUI)
{
    // Check if we should skip raycasting (mouse over UI)
    if (skipIfMouseOverUI)
    {
        // Clear hovered body and set default cursor
        hoveredBody = nullptr;
        hoveredCityName.clear();
        glfwSetCursor(window, defaultCursor);
        return;
    }

    glm::vec3 rayDir = getMouseRayDirection();

    float closestDistance = -1.0f;
    CelestialBody *newHoveredBody = nullptr;
    std::string newHoveredCityName;

    for (CelestialBody *body : bodies)
    {
        float distance = raySphereIntersection(camera().position, rayDir, body->position, body->displayRadius);
        // Only count hits within max ray distance (Pluto's orbit)
        if (distance > 0.0f && distance <= maxRayDistance)
        {
            if (closestDistance < 0.0f || distance < closestDistance)
            {
                closestDistance = distance;
                newHoveredBody = body;

                // If hovering over Earth, check for city at intersection point
                if (body->name == "Earth")
                {
                    // Compute surface intersection point
                    glm::vec3 intersectionPoint = camera().position + rayDir * distance;
                    glm::vec3 surfacePoint =
                        body->position + glm::normalize(intersectionPoint - body->position) * body->displayRadius;

                    // Query for city at this location (relative to Earth's center)
                    glm::vec3 relativePos = surfacePoint - body->position;
                    if (g_earthEconomy.isInitialized())
                    {
                        newHoveredCityName = g_earthEconomy.getCityName(relativePos);
                    }
                }
            }
        }
    }

    // Store hovered body and city name for tooltip display
    hoveredBody = newHoveredBody;
    hoveredCityName = newHoveredCityName;

    if (hoveredBody != nullptr)
    {
        glfwSetCursor(window, pointerCursor);
    }
    else
    {
        glfwSetCursor(window, defaultCursor);
    }
}

// ==================================
// Screen Size Update
// ==================================
void CameraController::updateScreenSize(int width, int height)
{
    screenWidth = static_cast<float>(width);
    screenHeight = static_cast<float>(height);
}

// ==================================
// GLFW Callback Installation
// ==================================
void CameraController::initCallbacks(GLFWwindow *window)
{
    // Create cursors
    defaultCursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    pointerCursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);

    // Get screen dimensions
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    screenWidth = static_cast<float>(width);
    screenHeight = static_cast<float>(height);

    // Store this controller in the window for callback access
    glfwSetWindowUserPointer(window, this);

    // Set GLFW callbacks
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
}

// ==================================
// Static GLFW Callbacks
// ==================================
void CameraController::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    CameraController *controller = static_cast<CameraController *>(glfwGetWindowUserPointer(window));
    if (controller)
    {
        controller->handleMouseButton(window, button, action, mods);
    }
}

void CameraController::cursorPosCallback(GLFWwindow *window, double xpos, double ypos)
{
    CameraController *controller = static_cast<CameraController *>(glfwGetWindowUserPointer(window));
    if (controller)
    {
        controller->handleCursorPos(window, xpos, ypos);
    }
}

void CameraController::scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
    CameraController *controller = static_cast<CameraController *>(glfwGetWindowUserPointer(window));
    if (controller)
    {
        controller->handleScroll(window, xoffset, yoffset);
    }
}

// ==================================
// Mouse Button Handler
// ==================================
void CameraController::handleMouseButton(GLFWwindow *window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if (action == GLFW_PRESS)
        {
            leftMousePressed = true;
            glfwGetCursorPos(window, &lastMouseX, &lastMouseY);

            // If context menu is open, close it (UI will handle menu clicks separately)
            if (contextMenuOpen)
            {
                // Don't close here - let UI handle the click first
                // Context menu will be closed by UI after processing
            }
            // Handle body selection and double-click focus
            else if (hoveredBody != nullptr)
            {
                double currentTime = glfwGetTime();
                double timeSinceLastClick = currentTime - lastClickTime;
                bool isDoubleClick = (timeSinceLastClick <= DOUBLE_CLICK_THRESHOLD);

                if (isDoubleClick && selectedBody == hoveredBody)
                {
                    // Double-click on same body - focus camera on it
                    focusOnBody(hoveredBody);
                }
                else if (isDoubleClick && hoveredBody != selectedBody)
                {
                    // Double-click on different body - select and focus it
                    selectedBody = hoveredBody;
                    focusOnBody(hoveredBody);
                    std::cout << "Selected and focused: " << selectedBody->name << std::endl;
                }
                else if (!isFocused || hoveredBody == selectedBody)
                {
                    // Single click when NOT focused, or clicking on already-selected body
                    // Allow selection change
                    selectedBody = hoveredBody;
                    std::cout << "Selected: " << selectedBody->name << std::endl;
                }
                // else: Single click on different body while focused - do nothing
                // (prevents accidental selection change during orbit rotation)

                lastClickTime = currentTime;
            }
            else
            {
                // Clicked on empty space - check if Alt key is NOT pressed (allow Alt+drag to orbit)
                if (!altKeyPressed)
                {
                    // Mark pending deselect - will be confirmed or cancelled after UI check
                    pendingDeselect = true;
                }
            }
        }
        else if (action == GLFW_RELEASE)
        {
            leftMousePressed = false;
        }
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        if (action == GLFW_PRESS)
        {
            // Check if we should open a context menu instead of panning
            if (hoveredBody != nullptr && !contextMenuOpen)
            {
                // Open context menu for the hovered body
                contextMenuOpen = true;
                contextMenuBody = hoveredBody;
                glfwGetCursorPos(window, &contextMenuX, &contextMenuY);
                // Don't start panning
                rightMousePressed = false;
            }
            else
            {
                // Close any open context menu and start panning
                contextMenuOpen = false;
                contextMenuBody = nullptr;
                rightMousePressed = true;
                glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
            }
        }
        else if (action == GLFW_RELEASE)
        {
            rightMousePressed = false;
        }
    }
}

// ==================================
// Cursor Position Handler
// ==================================
void CameraController::handleCursorPos(GLFWwindow *window, double xpos, double ypos)
{
    // Always track current mouse position for raycasting
    currentMouseX = xpos;
    currentMouseY = ypos;

    // If input is blocked (UI slider dragging), don't process camera movement
    if (inputBlocked)
    {
        lastMouseX = xpos;
        lastMouseY = ypos;
        return;
    }

    double deltaX = xpos - lastMouseX;
    double deltaY = ypos - lastMouseY;

    // Check if Alt key is pressed
    altKeyPressed =
        (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS);

    if (leftMousePressed)
    {
        if (followMode == CameraFollowMode::Surface && selectedBody != nullptr)
        {
            // Surface view mode: use local yaw/pitch with clamping
            // Mouse X changes local yaw (azimuth around surface normal)
            // Mouse Y changes local pitch (angle above horizon)
            surfaceLocalYaw += static_cast<float>(deltaX) * rotateSpeed;
            surfaceLocalPitch += static_cast<float>(deltaY) * rotateSpeed;

            // Clamp to prevent looking below horizon (into the planet)
            clampSurfaceOrientation();

            // Convert local orientation to world yaw/pitch
            updateWorldOrientationFromSurface();
        }
        else if (altKeyPressed && selectedBody != nullptr)
        {
            // Orbit mode: rotate camera around selected body
            glm::vec3 toCamera = camera().position - selectedBody->position;
            float distance = glm::length(toCamera);

            // Calculate spherical coordinates relative to the body
            float theta = atan2(toCamera.z, toCamera.x);                      // Horizontal angle
            float phi = asin(glm::clamp(toCamera.y / distance, -1.0f, 1.0f)); // Vertical angle

            // Apply mouse movement to angles (inverted for natural feel)
            theta += static_cast<float>(deltaX) * orbitSpeed;
            phi += static_cast<float>(deltaY) * orbitSpeed;

            // Clamp vertical angle to prevent flipping
            phi = glm::clamp(phi, -1.5f, 1.5f); // ~86 degrees

            // Convert back to Cartesian coordinates
            float cosPhi = cos(phi);
            camera().position = selectedBody->position +
                       glm::vec3(distance * cosPhi * cos(theta), distance * sin(phi), distance * cosPhi * sin(theta));

            // Point camera at the body
            glm::vec3 toBody = glm::normalize(selectedBody->position - camera().position);
            camera().yaw = glm::degrees(atan2(toBody.z, toBody.x));
            camera().pitch = glm::degrees(asin(toBody.y));

            // Maintain focus tracking during orbit (update offset for new position)
            isFocused = true;
            focusOffset = camera().position - selectedBody->position;
        }
        else
        {
            // Normal rotation mode
            camera().yaw += static_cast<float>(deltaX) * rotateSpeed;
            camera().pitch -= static_cast<float>(deltaY) * rotateSpeed;
            if (camera().pitch > 89.0f)
                camera().pitch = 89.0f;
            if (camera().pitch < -89.0f)
                camera().pitch = -89.0f;
        }
    }

    if (rightMousePressed)
    {
        glm::vec3 front = getFront();
        glm::vec3 right = getRight();
        glm::vec3 up = getUp();

        // Pan speed relative to selected body's radius
        float currentPanSpeed = getRelativePanSpeed();

        camera().position -= right * static_cast<float>(deltaX) * currentPanSpeed;
        camera().position += up * static_cast<float>(deltaY) * currentPanSpeed;

        // Ensure camera doesn't clip through body surface
        clampToSurface();

        // Panning breaks focus tracking
        if (isFocused)
        {
            isFocused = false;
        }
    }

    if (leftMousePressed || rightMousePressed)
    {
        lastMouseX = xpos;
        lastMouseY = ypos;
    }
}

// ==================================
// Scroll Handler
// ==================================
void CameraController::handleScroll(GLFWwindow *window, double xoffset, double yoffset)
{
    glm::vec3 front = getFront();

    // Speed relative to selected body's radius (or default if none selected)
    float speed = getRelativeScrollSpeed();

    glm::vec3 movement = front * static_cast<float>(yoffset) * speed;
    camera().position += movement;

    // If focused, update the offset so we maintain the new distance
    if (isFocused && !focusIsLagrangePoint && selectedBody != nullptr)
    {
        focusOffset += movement;
    }
    else if (isFocused && focusIsLagrangePoint)
    {
        focusOffset += movement;
    }

    // Ensure camera doesn't clip through body surface
    clampToSurface();

    // Note: Scrolling does NOT break focus - it just adjusts viewing distance
}
