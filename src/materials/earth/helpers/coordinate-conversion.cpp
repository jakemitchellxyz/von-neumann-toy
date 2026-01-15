#include "coordinate-conversion.h"
#include "../../../concerns/constants.h"
#include <cmath>

namespace EarthCoordinateConversion
{

glm::vec3 latLonToPosition(double latitude, double longitude, float radius)
{
    // Convert to 3D position on unit sphere
    // Y is up (north pole), so latitude affects Y component
    // Longitude rotates around Y axis

    double cosLat = cos(latitude);
    double sinLat = sin(latitude);
    double cosLon = cos(longitude);
    double sinLon = sin(longitude);

    // X points toward prime meridian (0° longitude) at equator
    // Z points toward 90°E longitude at equator
    // Y points toward north pole
    float x = static_cast<float>(cosLat * cosLon) * radius;
    float y = static_cast<float>(sinLat) * radius;
    float z = static_cast<float>(cosLat * sinLon) * radius;

    return glm::vec3(x, y, z);
}

std::pair<double, double> positionToLatLon(const glm::vec3 &position)
{
    glm::vec3 normalized = glm::normalize(position);

    // Latitude: angle from equator (XZ plane) to Y axis
    double latitude = asin(glm::clamp(static_cast<double>(normalized.y), -1.0, 1.0));

    // Longitude: angle around Y axis from X axis (prime meridian)
    double longitude = atan2(static_cast<double>(normalized.z), static_cast<double>(normalized.x));

    return std::make_pair(latitude, longitude);
}

std::pair<double, double> uvToLatLon(const glm::vec2 &uv)
{
    // Equirectangular mapping:
    // u: 0-1 maps to longitude -180° to +180° (or -π to +π)
    // v: 0-1 maps to latitude +90° to -90° (or +π/2 to -π/2)

    double longitude = (uv.x * 2.0 - 1.0) * PI; // -π to +π
    double latitude = (0.5 - uv.y) * PI;        // +π/2 to -π/2

    return std::make_pair(latitude, longitude);
}

glm::vec2 latLonToUV(double latitude, double longitude)
{
    // Inverse of uvToLatLon
    double u = (longitude / PI + 1.0) * 0.5; // 0 to 1
    double v = 0.5 - (latitude / PI);        // 0 to 1

    return glm::vec2(static_cast<float>(u), static_cast<float>(v));
}

glm::vec2 equirectToSinusoidal(const glm::vec2 &equirectUV)
{
    // Convert equirectangular UV to sinusoidal UV
    // Sinusoidal projection: x = lon * cos(lat), y = lat
    // This matches the texture projection used by Earth material

    auto [lat, lon] = uvToLatLon(equirectUV);

    // Sinusoidal projection
    double x = lon * cos(lat); // Longitude scaled by cos(latitude)
    double y = lat;            // Latitude unchanged

    // Normalize to 0-1 range
    // x: -π to +π -> 0 to 1
    // y: -π/2 to +π/2 -> 0 to 1
    double u = (x / PI + 1.0) * 0.5;
    double v = (y / PI + 0.5);

    return glm::vec2(static_cast<float>(u), static_cast<float>(v));
}

glm::vec2 sinusoidalToEquirect(const glm::vec2 &sinuUV)
{
    // Convert sinusoidal UV to equirectangular UV
    // Inverse of equirectToSinusoidal

    // Denormalize from 0-1 range
    double x = (sinuUV.x * 2.0 - 1.0) * PI; // -π to +π
    double y = (sinuUV.y - 0.5) * PI;       // -π/2 to +π/2

    // Inverse sinusoidal projection
    // x = lon * cos(lat), so lon = x / cos(lat)
    // y = lat
    double lat = y;
    double lon = x / cos(lat);

    // Clamp longitude to valid range
    lon = fmod(lon + PI, 2.0 * PI) - PI;

    return latLonToUV(lat, lon);
}

} // namespace EarthCoordinateConversion
