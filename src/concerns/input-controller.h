#pragma once

#include <cstdint>

// Forward declarations
struct GLFWwindow;

// ==================================
// InputPushConstants - GPU Push Constants for Input (16 bytes)
// ==================================
// Mouse state passed to shaders for hover effects
struct InputPushConstants
{
    float mouseX;       // 4 bytes - normalized mouse X (0-1)
    float mouseY;       // 4 bytes - normalized mouse Y (0-1)
    uint32_t mouseDown; // 4 bytes - mouse button state (bit flags)
    float padding;      // 4 bytes - alignment padding
};

// ==================================
// MouseButton enum
// ==================================
enum class MouseButton : uint32_t
{
    Left = 0,
    Right = 1,
    Middle = 2,
    Count = 3
};

// ==================================
// CursorType enum
// ==================================
enum class CursorType
{
    Arrow,    // Default arrow cursor
    Pointer,  // Pointing hand for clickable elements (links, buttons)
    Hand,     // Open hand for drag handles
    Grabbing, // Closed hand while dragging (falls back to Hand if unavailable)
    Text,     // I-beam for text input
    Crosshair // Crosshair for precision selection
};

// ==================================
// InputState - Full CPU-side input state
// ==================================
struct InputState
{
    // Mouse position in pixels
    double mouseX = 0.0;
    double mouseY = 0.0;

    // Mouse position normalized (0-1)
    float mouseNormX = 0.0f;
    float mouseNormY = 0.0f;

    // Mouse button states (current frame)
    bool mouseButtonDown[3] = {false, false, false};     // Currently pressed
    bool mouseButtonPressed[3] = {false, false, false};  // Just pressed this frame
    bool mouseButtonReleased[3] = {false, false, false}; // Just released this frame

    // Mouse click detection (press + release on same element)
    bool mouseClicked = false; // Left button clicked this frame

    // Drag state
    bool isDragging = false;
    double dragStartX = 0.0;
    double dragStartY = 0.0;
    double dragDeltaX = 0.0;
    double dragDeltaY = 0.0;

    // Scroll wheel
    double scrollX = 0.0;
    double scrollY = 0.0;

    // Window dimensions (for normalization)
    int windowWidth = 1;
    int windowHeight = 1;

    // Convert to GPU push constants
    InputPushConstants toPushConstants() const
    {
        InputPushConstants pc{};
        pc.mouseX = mouseNormX;
        pc.mouseY = mouseNormY;
        pc.mouseDown = (mouseButtonDown[0] ? 1u : 0u) | (mouseButtonDown[1] ? 2u : 0u) | (mouseButtonDown[2] ? 4u : 0u);
        pc.padding = 0.0f;
        return pc;
    }
};

// ==================================
// InputController Singleton
// ==================================
// Centralized input state management
// Handles GLFW callbacks and provides clean access to input state
class InputController
{
public:
    // Get singleton instance
    static InputController &instance();

    // Delete copy/move constructors
    InputController(const InputController &) = delete;
    InputController &operator=(const InputController &) = delete;
    InputController(InputController &&) = delete;
    InputController &operator=(InputController &&) = delete;

    // Initialize with GLFW window (sets up callbacks)
    void initialize(GLFWwindow *window);

    // Call at start of each frame to update state
    void beginFrame();

    // Call at end of each frame to clear per-frame events
    void endFrame();

    // Get current input state (read-only)
    const InputState &getState() const
    {
        return m_state;
    }

    // Check if mouse is within a rectangle (in pixel coordinates)
    bool isMouseInRect(float x, float y, float width, float height) const;

    // Check if a rect was clicked this frame
    bool wasRectClicked(float x, float y, float width, float height) const;

    // Check if mouse button is currently down
    bool isMouseButtonDown(MouseButton button) const;

    // Check if mouse button was just pressed this frame
    bool wasMouseButtonPressed(MouseButton button) const;

    // Check if mouse button was just released this frame
    bool wasMouseButtonReleased(MouseButton button) const;

    // Start/stop dragging
    void startDrag();
    void stopDrag();
    bool isDragging() const
    {
        return m_state.isDragging;
    }

    // Get drag delta since last frame
    double getDragDeltaX() const
    {
        return m_state.dragDeltaX;
    }
    double getDragDeltaY() const
    {
        return m_state.dragDeltaY;
    }

    // Cursor management
    // Call setCursor() when hovering interactive elements
    // The cursor resets to Arrow at the start of each frame
    void setCursor(CursorType type);
    CursorType getCursor() const
    {
        return m_currentCursor;
    }

    // Apply the current cursor to the window (called automatically in endFrame)
    void applyCursor();

    // GLFW callback handlers (called from static callbacks)
    void onMouseMove(double xpos, double ypos);
    void onMouseButton(int button, int action, int mods);
    void onScroll(double xoffset, double yoffset);
    void onWindowResize(int width, int height);

private:
    InputController();
    ~InputController();

    void createCursors();
    void destroyCursors();

    InputState m_state;
    InputState m_prevState; // Previous frame state for edge detection

    // Track press position for click detection
    double m_pressX = 0.0;
    double m_pressY = 0.0;
    bool m_wasPressed = false;

    GLFWwindow *m_window = nullptr;

    // Cursor management
    CursorType m_currentCursor = CursorType::Arrow;
    CursorType m_appliedCursor = CursorType::Arrow;

    // GLFW cursor handles (created once, reused)
    struct GLFWcursor *m_cursorArrow = nullptr;
    struct GLFWcursor *m_cursorPointer = nullptr;
    struct GLFWcursor *m_cursorHand = nullptr;
    struct GLFWcursor *m_cursorText = nullptr;
    struct GLFWcursor *m_cursorCrosshair = nullptr;
};

// Convenience macro for accessing InputController
// Note: Cannot use "INPUT" as it conflicts with Windows winuser.h INPUT type
#define g_input InputController::instance()
