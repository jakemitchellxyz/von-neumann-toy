// ============================================================================
// Economy Renderer Drawing
// ============================================================================
// Renders city labels and economy visualizations

#include "../../../concerns/font-rendering.h"
#include "../../../concerns/helpers/gl.h"
#include "../earth-material.h"
#include "../helpers/coordinate-conversion.h"
#include "economy-renderer.h"


#include <algorithm>
#include <cmath>
#include <iostream>

void EconomyRenderer::drawCityLabels(const glm::vec3 &earthPosition,
                                     float earthRadius,
                                     const glm::vec3 &cameraPos,
                                     const glm::vec3 &cameraFront,
                                     const glm::vec3 &cameraUp,
                                     const glm::vec3 &poleDirection,
                                     const glm::vec3 &primeMeridianDirection,
                                     float maxDistance)
{
    if (!initialized_ || !showCityLabels_)
    {
        return;
    }

    // Get city data from economy system
    if (!g_earthEconomy.isInitialized())
    {
        return;
    }

    const auto &cities = g_earthEconomy.getAllCities();
    if (cities.empty())
    {
        // Debug: check if cities are loaded
        static bool warnedOnce = false;
        if (!warnedOnce)
        {
            std::cout << "EconomyRenderer: No cities loaded (city count: " << g_earthEconomy.getCityCount() << ")\n";
            warnedOnce = true;
        }
        return;
    }

    // Calculate distance from camera to Earth center
    float cameraToEarthDist = glm::length(cameraPos - earthPosition);

    // Filter and sort cities by distance from camera
    struct CityRenderInfo
    {
        const CityData *city;
        glm::vec3 worldPos;
        float distanceToCamera;
    };

    std::vector<CityRenderInfo> visibleCities;
    visibleCities.reserve(cities.size());

    for (const auto &city : cities)
    {
        // DEBUG: Filter to only cities with more than 1 million population
        const float DEBUG_MIN_POPULATION = 1000000.0f;
        if (city.population < DEBUG_MIN_POPULATION)
        {
            continue; // Skip cities with less than 1 million population for debugging
        }

        // Filter by minimum population
        if (city.population < minPopulation_)
        {
            continue;
        }

        // Calculate world position of city on Earth's surface
        // Use the same rotating coordinate system as the coordinate grid
        // This ensures cities align with the grid lines
        glm::vec3 north = glm::normalize(poleDirection);
        glm::vec3 east = glm::normalize(primeMeridianDirection);
        // Ensure east is perpendicular to north (numerical stability)
        east = glm::normalize(east - glm::dot(east, north) * north);
        // Y-axis of body frame (90° East longitude at equator)
        glm::vec3 equatorY = glm::normalize(glm::cross(north, east));

        // Convert lat/lon to position using the rotating coordinate system
        float lat = static_cast<float>(city.latitude);
        float lon = static_cast<float>(city.longitude);
        float cosLat = std::cos(lat);
        float sinLat = std::sin(lat);
        float cosLon = std::cos(lon);
        float sinLon = std::sin(lon);

        // Same calculation as coordinate grid: position + north * height + east * (radius * cos(lon)) + equatorY * (radius * sin(lon))
        float height = earthRadius * sinLat;
        float circleRadius = earthRadius * cosLat;
        glm::vec3 cityWorldPos =
            earthPosition + north * height + east * (circleRadius * cosLon) + equatorY * (circleRadius * sinLon);

        // Calculate distance from camera
        float distToCamera = glm::length(cameraPos - cityWorldPos);

        // Cull cities that are too far
        if (distToCamera > maxDistance)
        {
            continue;
        }

        // Check if city is visible from camera
        glm::vec3 toCity = cityWorldPos - cameraPos;
        glm::vec3 toCityNorm = glm::normalize(toCity);

        // Check if city is in front of camera (dot product with camera front should be positive)
        float dotWithCameraFront = glm::dot(toCityNorm, cameraFront);
        if (dotWithCameraFront < 0.0f)
        {
            // City is behind camera
            continue;
        }

        // Check if city is on the visible side of Earth (not on the back)
        // Surface normal points outward from Earth center
        // Vector from city to camera should have positive dot with surface normal
        glm::vec3 surfaceNormal = glm::normalize(cityWorldPos - earthPosition);
        glm::vec3 toCameraFromCity = cameraPos - cityWorldPos;
        float dotNormalToCamera = glm::dot(surfaceNormal, glm::normalize(toCameraFromCity));

        // If dot product is negative, the camera is looking at the back side of Earth
        // We want to show cities where the surface normal points generally toward camera
        if (dotNormalToCamera < 0.3f) // Allow some tolerance (0.3 = ~72 degrees)
        {
            // City is on the back side of Earth or at a very oblique angle
            continue;
        }

        visibleCities.push_back({&city, cityWorldPos, distToCamera});
    }

    // Sort by distance (render closest first for proper depth)
    std::sort(visibleCities.begin(), visibleCities.end(), [](const CityRenderInfo &a, const CityRenderInfo &b) {
        return a.distanceToCamera < b.distanceToCamera;
    });

    // Limit number of labels to avoid clutter (show closest N cities)
    constexpr size_t MAX_VISIBLE_LABELS = 50;
    if (visibleCities.size() > MAX_VISIBLE_LABELS)
    {
        visibleCities.resize(MAX_VISIBLE_LABELS);
    }

    // Debug output (only once per frame, throttled)
    static int debugFrameCounter = 0;
    static bool debugPrinted = false;
    if (!debugPrinted || debugFrameCounter++ % 300 == 0) // Print every 5 seconds at 60fps
    {
        std::cout << "EconomyRenderer: " << visibleCities.size() << " visible cities (total: " << cities.size()
                  << ", minPop: " << minPopulation_ << ", maxDist: " << maxDistance << ")\n";
        if (!visibleCities.empty())
        {
            std::cout << "  First city: " << visibleCities[0].city->name << " at distance "
                      << visibleCities[0].distanceToCamera << "\n";
        }
        else if (cities.size() > 0)
        {
            std::cout << "  No cities visible (check culling logic)\n";
        }
        debugPrinted = true;
    }

    if (visibleCities.empty())
    {
        return; // Nothing to render
    }

    // TODO: Migrate economy rendering to Vulkan
    // Set up rendering state
    // glDisable(GL_LIGHTING); // REMOVED - migrate to Vulkan (lighting in shaders)
    // glEnable(GL_DEPTH_TEST); // REMOVED - migrate to Vulkan pipeline depth state (depthTest=true)
    // glDepthFunc(GL_LEQUAL); // REMOVED - migrate to Vulkan pipeline depth state (depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL)

    // TODO: Migrate economy rendering to Vulkan
    // Enable depth bias to push labels slightly closer to camera
    // This ensures labels always render on top of Earth surface
    // glEnable(GL_POLYGON_OFFSET_FILL); // REMOVED - migrate to Vulkan pipeline depth bias state
    // glPolygonOffset(-1.0f, -1.0f); // REMOVED - migrate to Vulkan pipeline depth bias state

    // Calculate label size based on distance (closer = larger)
    // Base size: 12 pixels, scales with distance
    const float BASE_PIXEL_SIZE = 16.0f; // Increased base size for better visibility

    // Elevation range for heightmap (ETOPO data: approximately -11000m to 9000m)
    const float MIN_ELEVATION_METERS = -11000.0f;
    const float MAX_ELEVATION_METERS = 9000.0f;
    const float ELEVATION_RANGE = MAX_ELEVATION_METERS - MIN_ELEVATION_METERS;

    // Minimum label elevation above surface (10 meters)
    const float MIN_LABEL_ELEVATION_METERS = 10.0f;

    // Earth radius in meters (for converting elevation to display units)
    const float EARTH_RADIUS_METERS = 6371000.0f; // ~6371 km

    // Helper function to sample heightmap elevation at lat/lon
    // Uses OpenGL 2.x compatible approach: render to a small viewport and read pixels
    auto sampleHeightmapElevation = [&](double lat, double lon) -> float {
        // Check if heightmap is available
        if (!g_earthMaterial.isInitialized() || !g_earthMaterial.getElevationLoaded())
        {
            return 0.0f; // No heightmap data, return sea level
        }

        GLuint heightmapTexture = g_earthMaterial.getHeightmapTexture();
        if (heightmapTexture == 0)
        {
            return 0.0f;
        }

        // Convert lat/lon to sinusoidal UV (heightmap uses sinusoidal projection)
        glm::vec2 equirectUV = EarthCoordinateConversion::latLonToUV(lat, lon);
        glm::vec2 sinuUV = EarthCoordinateConversion::equirectToSinusoidal(equirectUV);

        // Clamp UV to valid range
        sinuUV.x = std::max(0.0f, std::min(1.0f, sinuUV.x));
        sinuUV.y = std::max(0.0f, std::min(1.0f, sinuUV.y));

        // TODO: Migrate heightmap sampling to Vulkan
        // Save current state
        GLint viewport[4] = {0, 0, 0, 0}; // Default values - state queries removed
        // glGetIntegerv(GL_VIEWPORT, viewport); // REMOVED - migrate to Vulkan
        GLint currentTexture = 0;
        // glGetIntegerv(GL_TEXTURE_BINDING_2D, &currentTexture); // REMOVED - migrate to Vulkan
        GLboolean depthTest = false; // Default - state queries removed
        // glIsEnabled(GL_DEPTH_TEST); // REMOVED - migrate to Vulkan
        GLboolean lighting = false; // Default
        // glIsEnabled(GL_LIGHTING); // REMOVED - migrate to Vulkan
        GLboolean texture2d = false; // Default
        // glIsEnabled(GL_TEXTURE_2D); // REMOVED - migrate to Vulkan
        GLint matrixMode = 0;
        // glGetIntegerv(GL_MATRIX_MODE, &matrixMode); // REMOVED - migrate to Vulkan

        // Set up a 1x1 viewport in the corner for sampling
        // glViewport(0, 0, 1, 1); // REMOVED - migrate to Vulkan (use setViewport)

        // Bind heightmap texture
        glBindTexture(GL_TEXTURE_2D, heightmapTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Save current color and clear color
        GLfloat clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // Default - state queries removed
        // glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor); // REMOVED - migrate to Vulkan

        // TODO: Migrate texture sampling to Vulkan (use compute shader or texture sampling API)
        // Set up ortho projection for texture sampling
        // glMatrixMode(GL_PROJECTION); // REMOVED - migrate to Vulkan (matrices in uniform buffers)
        // glPushMatrix(); // REMOVED - migrate to Vulkan
        // glLoadIdentity(); // REMOVED - migrate to Vulkan
        // glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0); // REMOVED - migrate to Vulkan

        // glMatrixMode(GL_MODELVIEW); // REMOVED - migrate to Vulkan
        // glPushMatrix(); // REMOVED - migrate to Vulkan
        // glLoadIdentity(); // REMOVED - migrate to Vulkan

        // Draw a quad with the texture at the UV coordinates
        // We'll draw it off-screen in the corner where we can read it
        // glDisable(GL_DEPTH_TEST); // REMOVED - migrate to Vulkan pipeline state
        // glDisable(GL_LIGHTING); // REMOVED - migrate to Vulkan (lighting in shaders)
        // glEnable(GL_TEXTURE_2D); // REMOVED - migrate to Vulkan
        // glColor3f(1.0f, 1.0f, 1.0f); // REMOVED - migrate to Vulkan uniform buffer

        // glBegin(GL_QUADS); // REMOVED - migrate to Vulkan
        // glTexCoord2f(sinuUV.x, sinuUV.y); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex2f(0.0f, 0.0f); // REMOVED - migrate to Vulkan vertex buffer
        // glTexCoord2f(sinuUV.x, sinuUV.y); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex2f(1.0f, 0.0f); // REMOVED - migrate to Vulkan vertex buffer
        // glTexCoord2f(sinuUV.x, sinuUV.y); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex2f(1.0f, 1.0f); // REMOVED - migrate to Vulkan vertex buffer
        // glTexCoord2f(sinuUV.x, sinuUV.y); // REMOVED - migrate to Vulkan vertex buffer
        // glVertex2f(0.0f, 1.0f); // REMOVED - migrate to Vulkan vertex buffer
        // glEnd(); // REMOVED - migrate to Vulkan

        // Read pixel value from the default framebuffer (at corner of viewport)
        unsigned char pixel[4] = {0, 0, 0, 255}; // Default - texture sampling removed
        // glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel); // REMOVED - migrate to Vulkan (use texture sampling API)

        // Restore clear color
        // glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]); // REMOVED - migrate to Vulkan render pass clear values

        // Restore state
        // glPopMatrix(); // REMOVED - migrate to Vulkan
        // glMatrixMode(GL_PROJECTION); // REMOVED - migrate to Vulkan
        // glPopMatrix(); // REMOVED - migrate to Vulkan
        // glMatrixMode(matrixMode); // REMOVED - migrate to Vulkan

        // Restore viewport
        // glViewport(viewport[0], viewport[1], viewport[2], viewport[3]); // REMOVED - migrate to Vulkan (use setViewport)

        // TODO: Migrate state restoration to Vulkan
        // if (depthTest)
        //     glEnable(GL_DEPTH_TEST); // REMOVED - migrate to Vulkan pipeline state
        // if (lighting)
        //     glEnable(GL_LIGHTING); // REMOVED - migrate to Vulkan (lighting in shaders)
        // if (!texture2d)
        //     glDisable(GL_TEXTURE_2D); // REMOVED - migrate to Vulkan

        // glBindTexture(GL_TEXTURE_2D, currentTexture); // REMOVED - migrate to Vulkan (textures via descriptor sets)

        // Convert normalized value (0-255) back to meters
        float normalizedElevation = static_cast<float>(pixel[0]) / 255.0f;
        float elevationMeters = MIN_ELEVATION_METERS + normalizedElevation * ELEVATION_RANGE;

        return elevationMeters;
    };

    // Draw each city label
    for (const auto &info : visibleCities)
    {
        // Use a simpler scaling: larger pixel size for closer cities
        float pixelSize = BASE_PIXEL_SIZE;
        if (info.distanceToCamera < 5000.0f)
        {
            pixelSize = BASE_PIXEL_SIZE * 2.0f; // Larger when close
        }
        else if (info.distanceToCamera < 20000.0f)
        {
            pixelSize = BASE_PIXEL_SIZE * 1.5f;
        }

        // Calculate surface normal
        glm::vec3 surfaceNormal = glm::normalize(info.worldPos - earthPosition);

        // Sample heightmap elevation at city location
        float heightmapElevationMeters = sampleHeightmapElevation(info.city->latitude, info.city->longitude);

        // Calculate total elevation offset (minimum 10m + heightmap elevation)
        float totalElevationMeters =
            std::max(MIN_LABEL_ELEVATION_METERS, heightmapElevationMeters + MIN_LABEL_ELEVATION_METERS);

        // Convert elevation from meters to display units
        // Display units are scaled by UNITS_PER_AU, but Earth radius is already in display units
        // So we need to convert meters to a fraction of Earth radius
        float elevationInDisplayUnits = (totalElevationMeters / EARTH_RADIUS_METERS) * earthRadius;

        // Offset label above city position (along surface normal)
        glm::vec3 labelPos = info.worldPos + surfaceNormal * elevationInDisplayUnits;

        // Calculate direction to camera
        glm::vec3 toCamera = glm::normalize(cameraPos - labelPos);

        // Clamp the angle between text normal and surface normal
        // This prevents text from tilting too far toward the planet surface
        // We want the text to face the camera, but never tilt more than ~80 degrees from vertical
        float dotWithSurface = glm::dot(toCamera, surfaceNormal);
        const float MIN_DOT = cos(glm::radians(80.0f)); // Minimum 80 degrees from surface normal

        if (dotWithSurface < MIN_DOT)
        {
            // Clamp the direction to camera so it doesn't point too far down
            // Project toCamera onto the plane perpendicular to surfaceNormal, then add MIN_DOT component
            glm::vec3 tangentComponent = toCamera - surfaceNormal * dotWithSurface;
            float tangentLength = glm::length(tangentComponent);

            if (tangentLength > 0.001f)
            {
                // Normalize tangent component
                tangentComponent = glm::normalize(tangentComponent);

                // Reconstruct clamped direction: MIN_DOT along normal + remaining along tangent
                float remainingComponent = sqrt(1.0f - MIN_DOT * MIN_DOT);
                toCamera = surfaceNormal * MIN_DOT + tangentComponent * remainingComponent;
            }
            else
            {
                // Camera is directly above/below, use surface normal direction
                toCamera = surfaceNormal;
            }
        }

        // Use the original function but with a modified camera position
        // that would result in the clamped direction (prevents text from clipping into planet)
        float dist = glm::length(cameraPos - labelPos);
        glm::vec3 clampedCameraPos = labelPos + toCamera * dist;

        // Helper function to clamp a vertex to sphere surface
        // This samples the surface normal at each vertex position on the sphere
        auto clampVertexToSphere = [&](const glm::vec3 &vertex) -> glm::vec3 {
            glm::vec3 toVertex = vertex - earthPosition;
            float distToCenter = glm::length(toVertex);

            // If vertex is below sphere surface, project it onto surface
            if (distToCenter < earthRadius)
            {
                // Surface normal at this point is the normalized direction from center
                // This is the geometric surface normal of the sphere at this position
                glm::vec3 surfaceNormalAtVertex = glm::normalize(toVertex);
                // Project vertex onto sphere surface along the surface normal
                return earthPosition + surfaceNormalAtVertex * earthRadius;
            }

            return vertex;
        };

        // Calculate billboard basis vectors - text should face the clamped camera
        glm::vec3 toCameraBillboard = glm::normalize(clampedCameraPos - labelPos);

        // Handle the degenerate case when camera is directly above/below
        glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(toCameraBillboard, worldUp)) > 0.99f)
        {
            worldUp = glm::vec3(0.0f, 0.0f, 1.0f); // Use Z as up if looking straight down
        }

        // Right vector (in screen space) - perpendicular to camera look direction
        glm::vec3 right = glm::normalize(glm::cross(worldUp, toCameraBillboard));
        // Up vector (in screen space) - perpendicular to both
        glm::vec3 up = glm::normalize(glm::cross(toCameraBillboard, right));

        // Scale character height to achieve target pixel size on screen
        float charHeight = dist * pixelSize * 0.001f;

        // Calculate total text width for centering
        float totalWidth = 0.0f;
        for (char c : info.city->name)
        {
            auto widthIt = CHAR_WIDTHS.find(c);
            float charWidth = (widthIt != CHAR_WIDTHS.end()) ? widthIt->second * charHeight : 0.5f * charHeight;
            totalWidth += charWidth + charHeight * 0.15f; // Add spacing
        }

        // Start position (centered horizontally)
        float currentX = -totalWidth * 0.5f;

        // Draw text with vertex clamping to prevent clipping through planet
        glColor3f(1.0f, 1.0f, 0.5f); // Bright yellow for high visibility
        glLineWidth(3.0f);           // Make text thicker for visibility
        glBegin(GL_LINES);

        for (char c : info.city->name)
        {
            auto segIt = CHAR_SEGMENTS.find(c);
            auto widthIt = CHAR_WIDTHS.find(c);

            float charWidth = (widthIt != CHAR_WIDTHS.end()) ? widthIt->second * charHeight : 0.5f * charHeight;

            if (segIt != CHAR_SEGMENTS.end())
            {
                // Draw each segment of the character
                for (const auto &seg : segIt->second)
                {
                    // Transform 2D segment to 3D using billboard basis
                    // Character segments use Y=0 at top, Y=1 at bottom (screen coords)
                    // We need to flip Y so text appears right-side-up: use (1-y)
                    float y1_flipped = 1.0f - seg.y1;
                    float y2_flipped = 1.0f - seg.y2;

                    glm::vec3 p1 = labelPos + right * (currentX + seg.x1 * charWidth) + up * (y1_flipped * charHeight);
                    glm::vec3 p2 = labelPos + right * (currentX + seg.x2 * charWidth) + up * (y2_flipped * charHeight);

                    // Clamp each vertex to sphere surface to prevent clipping
                    p1 = clampVertexToSphere(p1);
                    p2 = clampVertexToSphere(p2);

                    // glVertex3f(p1.x, p1.y, p1.z); // REMOVED - migrate to Vulkan vertex buffer
                    // glVertex3f(p2.x, p2.y, p2.z); // REMOVED - migrate to Vulkan vertex buffer
                }
            }

            currentX += charWidth + charHeight * 0.15f; // Advance cursor
        }

        // glEnd(); // REMOVED - migrate to Vulkan
        // glLineWidth(1.0f); // REMOVED - migrate to Vulkan pipeline state
    }

    // Restore depth bias
    glDisable(GL_POLYGON_OFFSET_FILL);

    // Restore rendering state
    glEnable(GL_LIGHTING);
}
