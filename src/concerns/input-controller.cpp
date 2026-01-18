#include "input-controller.h"
#include "app-state.h"
#include <GLFW/glfw3.h>
#include <cmath>

// ==================================
// Static callback functions for GLFW
// ==================================

static void glfwMouseMoveCallback(GLFWwindow * /*window*/, double xpos, double ypos)
{
    INPUT.onMouseMove(xpos, ypos);
}

static void glfwMouseButtonCallback(GLFWwindow * /*window*/, int button, int action, int mods)
{
    INPUT.onMouseButton(button, action, mods);
}

static void glfwScrollCallback(GLFWwindow * /*window*/, double xoffset, double yoffset)
{
    INPUT.onScroll(xoffset, yoffset);
}

// ==================================
// InputController Implementation
// ==================================

InputController &InputController::instance()
{
    static InputController instance;
    return instance;
}

InputController::InputController() = default;

InputController::~InputController()
{
    destroyCursors();
}

void InputController::createCursors()
{
    // Create standard GLFW cursors
    m_cursorArrow = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    m_cursorPointer = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    m_cursorText = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    m_cursorCrosshair = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);

    // GLFW doesn't have a built-in "open hand" or "grabbing" cursor
    // We use the hand cursor (pointing hand) for both Pointer and Hand types
    // For a true open/closed hand, you'd need custom cursor images
    m_cursorHand = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
}

void InputController::destroyCursors()
{
    if (m_cursorArrow != nullptr)
    {
        glfwDestroyCursor(m_cursorArrow);
        m_cursorArrow = nullptr;
    }
    if (m_cursorPointer != nullptr)
    {
        glfwDestroyCursor(m_cursorPointer);
        m_cursorPointer = nullptr;
    }
    if (m_cursorHand != nullptr)
    {
        glfwDestroyCursor(m_cursorHand);
        m_cursorHand = nullptr;
    }
    if (m_cursorText != nullptr)
    {
        glfwDestroyCursor(m_cursorText);
        m_cursorText = nullptr;
    }
    if (m_cursorCrosshair != nullptr)
    {
        glfwDestroyCursor(m_cursorCrosshair);
        m_cursorCrosshair = nullptr;
    }
}

void InputController::initialize(GLFWwindow *window)
{
    m_window = window;

    if (window == nullptr)
    {
        return;
    }

    // Create cursors
    createCursors();

    // Get initial window size
    glfwGetWindowSize(window, &m_state.windowWidth, &m_state.windowHeight);

    // Get initial mouse position
    glfwGetCursorPos(window, &m_state.mouseX, &m_state.mouseY);

    // Normalize mouse position
    if (m_state.windowWidth > 0 && m_state.windowHeight > 0)
    {
        m_state.mouseNormX = static_cast<float>(m_state.mouseX / m_state.windowWidth);
        m_state.mouseNormY = static_cast<float>(m_state.mouseY / m_state.windowHeight);
    }

    // Set up GLFW callbacks
    glfwSetCursorPosCallback(window, glfwMouseMoveCallback);
    glfwSetMouseButtonCallback(window, glfwMouseButtonCallback);
    glfwSetScrollCallback(window, glfwScrollCallback);
}

void InputController::beginFrame()
{
    // Store previous state for edge detection
    m_prevState = m_state;

    // Clear per-frame events
    m_state.mouseClicked = false;
    m_state.scrollX = 0.0;
    m_state.scrollY = 0.0;
    m_state.dragDeltaX = 0.0;
    m_state.dragDeltaY = 0.0;

    for (int i = 0; i < 3; ++i)
    {
        m_state.mouseButtonPressed[i] = false;
        m_state.mouseButtonReleased[i] = false;
    }

    // Reset cursor to default - UI code will set it if hovering something
    // But if we're dragging, keep the grabbing cursor
    if (m_state.isDragging)
    {
        m_currentCursor = CursorType::Grabbing;
    }
    else
    {
        m_currentCursor = CursorType::Arrow;
    }

    // Update window size (in case resize callback was missed)
    if (m_window != nullptr)
    {
        glfwGetWindowSize(m_window, &m_state.windowWidth, &m_state.windowHeight);
    }
}

void InputController::endFrame()
{
    // Apply cursor change if needed
    applyCursor();
}

void InputController::setCursor(CursorType type)
{
    // Don't override grabbing cursor while dragging
    if (m_state.isDragging && type != CursorType::Grabbing)
    {
        return;
    }
    m_currentCursor = type;
}

void InputController::applyCursor()
{
    if (m_window == nullptr)
    {
        return;
    }

    // Only update if cursor changed
    if (m_currentCursor == m_appliedCursor)
    {
        return;
    }

    GLFWcursor *cursor = nullptr;
    switch (m_currentCursor)
    {
    case CursorType::Arrow:
        cursor = m_cursorArrow;
        break;
    case CursorType::Pointer:
        cursor = m_cursorPointer;
        break;
    case CursorType::Hand:
        cursor = m_cursorHand;
        break;
    case CursorType::Grabbing:
        // Use hand cursor for grabbing since GLFW doesn't have a closed hand
        cursor = m_cursorHand;
        break;
    case CursorType::Text:
        cursor = m_cursorText;
        break;
    case CursorType::Crosshair:
        cursor = m_cursorCrosshair;
        break;
    }

    if (cursor != nullptr)
    {
        glfwSetCursor(m_window, cursor);
        m_appliedCursor = m_currentCursor;
    }
}

bool InputController::isMouseInRect(float rectX, float rectY, float width, float height) const
{
    return m_state.mouseX >= rectX && m_state.mouseX <= rectX + width && m_state.mouseY >= rectY &&
           m_state.mouseY <= rectY + height;
}

bool InputController::wasRectClicked(float rectX, float rectY, float width, float height) const
{
    return m_state.mouseClicked && isMouseInRect(rectX, rectY, width, height);
}

bool InputController::isMouseButtonDown(MouseButton button) const
{
    auto idx = static_cast<size_t>(button);
    if (idx < 3)
    {
        return m_state.mouseButtonDown[idx];
    }
    return false;
}

bool InputController::wasMouseButtonPressed(MouseButton button) const
{
    auto idx = static_cast<size_t>(button);
    if (idx < 3)
    {
        return m_state.mouseButtonPressed[idx];
    }
    return false;
}

bool InputController::wasMouseButtonReleased(MouseButton button) const
{
    auto idx = static_cast<size_t>(button);
    if (idx < 3)
    {
        return m_state.mouseButtonReleased[idx];
    }
    return false;
}

void InputController::startDrag()
{
    if (!m_state.isDragging)
    {
        m_state.isDragging = true;
        m_state.dragStartX = m_state.mouseX;
        m_state.dragStartY = m_state.mouseY;
    }
}

void InputController::stopDrag()
{
    m_state.isDragging = false;
}

void InputController::onMouseMove(double xpos, double ypos)
{
    // Calculate drag delta before updating position
    if (m_state.isDragging)
    {
        m_state.dragDeltaX = xpos - m_state.mouseX;
        m_state.dragDeltaY = ypos - m_state.mouseY;
    }

    // Left-click drag: rotate camera yaw/pitch
    if (m_state.mouseButtonDown[0] && !m_state.isDragging)
    {
        double deltaX = xpos - m_state.mouseX;
        double deltaY = ypos - m_state.mouseY;

        // Update camera orientation (sensitivity factor of 0.15)
        constexpr float rotateSpeed = 0.15f;
        APP_STATE.worldState.camera.yaw += static_cast<float>(deltaX) * rotateSpeed;
        APP_STATE.worldState.camera.pitch -= static_cast<float>(deltaY) * rotateSpeed;

        // Clamp pitch to prevent gimbal lock
        if (APP_STATE.worldState.camera.pitch > 89.0f)
        {
            APP_STATE.worldState.camera.pitch = 89.0f;
        }
        if (APP_STATE.worldState.camera.pitch < -89.0f)
        {
            APP_STATE.worldState.camera.pitch = -89.0f;
        }
    }

    m_state.mouseX = xpos;
    m_state.mouseY = ypos;

    // Update normalized coordinates
    if (m_state.windowWidth > 0 && m_state.windowHeight > 0)
    {
        m_state.mouseNormX = static_cast<float>(xpos / m_state.windowWidth);
        m_state.mouseNormY = static_cast<float>(ypos / m_state.windowHeight);
    }
}

void InputController::onMouseButton(int button, int action, int /*mods*/)
{
    if (button < 0 || button > 2)
    {
        return;
    }

    bool wasDown = m_state.mouseButtonDown[button];
    bool isDown = (action == GLFW_PRESS || action == GLFW_REPEAT);

    m_state.mouseButtonDown[button] = isDown;

    if (isDown && !wasDown)
    {
        // Button just pressed
        m_state.mouseButtonPressed[button] = true;

        // Track press position for click detection (left button only)
        if (button == 0)
        {
            m_pressX = m_state.mouseX;
            m_pressY = m_state.mouseY;
            m_wasPressed = true;
        }
    }
    else if (!isDown && wasDown)
    {
        // Button just released
        m_state.mouseButtonReleased[button] = true;

        // Check for click (left button only)
        // A click is a press followed by release within a small distance
        if (button == 0 && m_wasPressed)
        {
            constexpr double clickThreshold = 5.0; // pixels
            double dx = m_state.mouseX - m_pressX;
            double dy = m_state.mouseY - m_pressY;
            double distance = std::sqrt(dx * dx + dy * dy);

            if (distance <= clickThreshold)
            {
                m_state.mouseClicked = true;
            }

            m_wasPressed = false;
        }

        // Stop dragging on any button release
        if (m_state.isDragging)
        {
            stopDrag();
        }
    }
}

void InputController::onScroll(double xoffset, double yoffset)
{
    m_state.scrollX = xoffset;
    m_state.scrollY = yoffset;
}

void InputController::onWindowResize(int width, int height)
{
    m_state.windowWidth = width;
    m_state.windowHeight = height;

    // Update normalized coordinates
    if (width > 0 && height > 0)
    {
        m_state.mouseNormX = static_cast<float>(m_state.mouseX / width);
        m_state.mouseNormY = static_cast<float>(m_state.mouseY / height);
    }
}
