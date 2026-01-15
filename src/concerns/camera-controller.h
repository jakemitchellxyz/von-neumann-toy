#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <vector>
#include <string>

// Forward declaration - CelestialBody defined in entrypoint
struct CelestialBody;

// Camera follow modes when focused on a body
enum class CameraFollowMode {
    Fixed,          // Camera follows body position but doesn't rotate with it
    Geostationary,  // Camera rotates with the body (stays over same surface point)
    Surface         // Camera is on the surface, looking outward, moves via lat/lon
};

class CameraController {
public:
    // ==================================
    // Camera State
    // ==================================
    glm::vec3 position;
    float yaw;    // Horizontal angle in degrees
    float pitch;  // Vertical angle in degrees
    float roll;   // Roll angle in degrees (rotation around forward axis)
    
    // ==================================
    // Configuration
    // ==================================
    float moveSpeed;
    float rotateSpeed;
    float rollSpeed;  // Roll speed in degrees per frame
    float panSpeed;
    float scrollSpeed;
    float orbitSpeed;
    float fov;
    
    // Max raycast distance (should be set to Pluto's orbital distance)
    float maxRayDistance;
    
    // ==================================
    // Selection State
    // ==================================
    CelestialBody* hoveredBody;
    CelestialBody* selectedBody;
    std::string hoveredCityName; // City name when hovering over Earth's surface
    
    // ==================================
    // Context Menu State
    // ==================================
    bool contextMenuOpen;           // True when context menu is displayed
    CelestialBody* contextMenuBody; // Body the context menu was opened on
    double contextMenuX;            // Screen X position where menu should appear
    double contextMenuY;            // Screen Y position where menu should appear
    
    // ==================================
    // Focus/Follow State
    // ==================================
    bool isFocused;              // True when camera is focused on/following something
    bool focusIsLagrangePoint;   // True if focused on a Lagrange point, false if body
    glm::vec3 focusOffset;       // Fixed offset from body to camera (set when focus starts)
    CameraFollowMode followMode; // How camera follows focused body (Fixed or Geostationary)
    double lastJulianDate;       // Previous Julian Date (for geostationary rotation calc)
    
    // ==================================
    // Surface View State
    // ==================================
    float surfaceLatitude;       // Current latitude in radians (-PI/2 to PI/2)
    float surfaceLongitude;      // Current longitude in radians (-PI to PI)
    float surfaceAltitude;       // Height above surface (small offset to avoid clipping)
    float surfaceMoveSpeed;      // Speed of lat/lon movement in radians per second
    glm::vec3 surfaceNormal;     // Outward normal at current surface position (updated each frame)
    glm::vec3 surfaceNorth;      // Local "north" direction on surface (tangent toward pole)
    glm::vec3 surfaceEast;       // Local "east" direction on surface (tangent perpendicular to north)
    float surfaceLocalYaw;       // Azimuth angle around surface normal (0° = north, 90° = east)
    float surfaceLocalPitch;     // Angle above horizon (0° = horizon, 90° = zenith/straight up)
    
    // ==================================
    // Pending Deselect (for UI click handling)
    // ==================================
    bool pendingDeselect;        // True when a click on empty space occurred, awaiting UI confirmation
    bool inputBlocked;           // True when UI slider is being dragged (block camera movement)
    
    // Lagrange point focus data (used when focusIsLagrangePoint is true)
    glm::vec3 focusedLagrangePosition;  // Current position of focused Lagrange point
    float focusedLagrangeRadius;        // Display radius for the focused Lagrange point
    std::string focusedLagrangeName;    // Name of the focused Lagrange point
    
    // ==================================
    // Public Methods
    // ==================================
    
    // Constructor - initializes with default values
    CameraController();
    
    // Destructor - cleans up cursors
    ~CameraController();
    
    // Initialize camera to view Earth with Sun in frame
    // earthPos: current position of Earth in world space
    // earthDisplayRadius: Earth's display radius for scaling
    void initializeForEarth(const glm::vec3& earthPos, float earthDisplayRadius);
    
    // Get camera direction vectors
    glm::vec3 getFront() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;
    
    // Get view matrix for rendering
    glm::mat4 getViewMatrix() const;
    
    // Focus camera on a specific body
    // Positions camera at radius^2 distance, looking at the body
    // Sets isFocused to true, focusIsLagrangePoint to false
    void focusOnBody(const CelestialBody* body);
    
    // Focus camera on a Lagrange point
    // Positions camera at appropriate distance, looking at the point
    // Sets isFocused to true, focusIsLagrangePoint to true
    void focusOnLagrangePoint(const glm::vec3& position, float displayRadius, const std::string& name);
    
    // Update the focused Lagrange point's position (call each frame after Lagrange update)
    void updateFocusedLagrangePosition(const glm::vec3& newPosition);
    
    // Update camera position to follow focused target (body or Lagrange point)
    // Call this each frame AFTER body/Lagrange positions have been updated
    // Displaces camera by the same amount the focused target moved
    // currentJD: current Julian Date (needed for geostationary rotation calculation)
    void updateFollowTarget(double currentJD);
    
    // Clear focus state (stop following)
    void clearFocus();
    
    // Toggle between Fixed and Geostationary follow modes
    void toggleFollowMode();
    
    // Get current follow mode
    CameraFollowMode getFollowMode() const { return followMode; }
    
    // Enter surface view mode - places camera on surface at given lat/lon
    // If no lat/lon specified, uses current view direction to determine location
    void enterSurfaceView(CelestialBody* body, float latitude = 0.0f, float longitude = 0.0f);
    
    // Exit surface view mode - returns to normal follow mode
    void exitSurfaceView();
    
    // Check if in surface view mode
    bool isInSurfaceView() const { return followMode == CameraFollowMode::Surface; }
    
    // Update world yaw/pitch from surface local coordinates
    void updateWorldOrientationFromSurface();
    
    // Clamp surface orientation to prevent looking below horizon
    void clampSurfaceOrientation();
    
    // Process pending deselect - call after UI interaction check
    // If uiConsumedClick is true, cancels the pending deselect
    // Otherwise performs the deselection
    void processPendingDeselect(bool uiConsumedClick);
    
    // Block camera input (e.g., when UI slider is being dragged)
    void setInputBlocked(bool blocked) { inputBlocked = blocked; }
    
    // Process keyboard input (WASD, Space, Ctrl, Escape)
    void processKeyboard(GLFWwindow* window);
    
    // Update raycasting - finds which body the mouse is hovering over
    // bodies: list of all celestial bodies to test against
    // window: GLFW window (for cursor updates)
    void updateRaycast(const std::vector<CelestialBody*>& bodies, GLFWwindow* window, bool skipIfMouseOverUI = false);
    
    // Initialize and install GLFW callbacks
    // Must be called after GLFW window creation
    void initCallbacks(GLFWwindow* window);
    
    // Get movement speed relative to selected body
    float getRelativeSpeed() const;
    
    // Get pan speed relative to selected body
    float getRelativePanSpeed() const;
    
    // Get scroll speed relative to selected body
    float getRelativeScrollSpeed() const;
    
    // Get proximity-based speed multiplier (slows down near body surface)
    // Returns 1.0 when far away, approaches 0 near surface
    // Also outputs the minimum allowed distance to maintain from body center
    float getProximitySpeedMultiplier(float* outMinDistance = nullptr) const;
    
    // Get dynamic near plane based on proximity to surfaces
    // Returns smaller values when close to a body's surface to allow viewing at ground level
    // Default far plane value when not near any surface
    float getDynamicNearPlane() const;
    
    // Clamp camera position to stay outside body surface
    // Returns true if position was clamped
    bool clampToSurface();
    
    // Update screen dimensions (call when window is resized)
    void updateScreenSize(int width, int height);
    
    // Get ray direction from current mouse position (public for measurement system)
    glm::vec3 getMouseRayDirection() const;
    
    // ==================================
    // GLFW Callback Handlers
    // ==================================
    // These are called by static callback functions
    void handleMouseButton(GLFWwindow* window, int button, int action, int mods);
    void handleCursorPos(GLFWwindow* window, double xpos, double ypos);
    void handleScroll(GLFWwindow* window, double xoffset, double yoffset);
    
private:
    // ==================================
    // Mouse State
    // ==================================
    bool leftMousePressed;
    bool rightMousePressed;
    bool altKeyPressed;
    double lastMouseX, lastMouseY;
    double currentMouseX, currentMouseY;
    
    // ==================================
    // Double-click Detection
    // ==================================
    double lastClickTime;
    static constexpr double DOUBLE_CLICK_THRESHOLD = 0.200;  // 200ms
    
    // ==================================
    // Cursors
    // ==================================
    GLFWcursor* defaultCursor;
    GLFWcursor* pointerCursor;
    
    // ==================================
    // Screen Dimensions (for raycasting)
    // ==================================
    float screenWidth;
    float screenHeight;
    
    // ==================================
    // Initialization flag
    // ==================================
    bool initialized;
    
    // ==================================
    // Private Methods
    // ==================================
    
    // Ray-sphere intersection test
    // Returns distance to intersection, or -1 if no hit
    static float raySphereIntersection(const glm::vec3& rayOrigin, 
                                       const glm::vec3& rayDir,
                                       const glm::vec3& sphereCenter, 
                                       float sphereRadius);
    
    // ==================================
    // Static GLFW Callbacks
    // ==================================
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};
