#include "ui-controls.h"
#include "font-rendering.h"
#include "helpers/vulkan.h"
#include "input-controller.h"

// Undefine Windows.h macros that conflict
#ifdef DrawText
#undef DrawText
#endif
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include "ui-icons.h"
#include "ui-primitives.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

// Helper to draw a line as a thin quad
static void DrawControlLine(float x1,
                            float y1,
                            float x2,
                            float y2,
                            float r,
                            float g,
                            float b,
                            float a,
                            float width = 1.5f)
{
    if (!g_buildingUIVertices)
        return;

    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
        return;

    float perpX = -dy / len * width * 0.5f;
    float perpY = dx / len * width * 0.5f;

    AddUIVertex(x1 + perpX, y1 + perpY, r, g, b, a);
    AddUIVertex(x2 + perpX, y2 + perpY, r, g, b, a);
    AddUIVertex(x1 - perpX, y1 - perpY, r, g, b, a);
    AddUIVertex(x2 + perpX, y2 + perpY, r, g, b, a);
    AddUIVertex(x2 - perpX, y2 - perpY, r, g, b, a);
    AddUIVertex(x1 - perpX, y1 - perpY, r, g, b, a);
}

// Helper to draw a filled quad
static void DrawControlQuad(float x, float y, float w, float h, float r, float g, float b, float a)
{
    if (!g_buildingUIVertices)
        return;

    AddUIVertex(x, y, r, g, b, a);
    AddUIVertex(x + w, y, r, g, b, a);
    AddUIVertex(x, y + h, r, g, b, a);
    AddUIVertex(x + w, y, r, g, b, a);
    AddUIVertex(x + w, y + h, r, g, b, a);
    AddUIVertex(x, y + h, r, g, b, a);
}


// Draw a horizontal slider
bool DrawSlider(float x,
                float y,
                float width,
                float height,
                double *value,
                double minVal,
                double maxVal,
                double mouseX,
                double mouseY,
                bool mouseDown,
                bool &isDragging)
{
    bool valueChanged = false;

    DrawRoundedRect(x, y + height / 2 - 2, width, 4, 2.0f, 0.3f, 0.3f, 0.35f, 0.9f);

    double logMin = std::log10(minVal);
    double logMax = std::log10(maxVal);
    double logVal = std::log10(*value);
    float normalizedPos = static_cast<float>((logVal - logMin) / (logMax - logMin));
    float thumbX = x + normalizedPos * (width - 12);

    float thumbWidth = 12.0f;
    float thumbHeight = height;
    bool isHoveringThumb =
        (mouseX >= thumbX && mouseX <= thumbX + thumbWidth && mouseY >= y && mouseY <= y + thumbHeight);

    if (mouseDown && isHoveringThumb && !isDragging)
    {
        isDragging = true;
    }

    bool isInTrack = (mouseX >= x && mouseX <= x + width && mouseY >= y && mouseY <= y + thumbHeight);

    // Set cursor based on state
    if (isDragging)
    {
        g_input.setCursor(CursorType::Grabbing);
    }
    else if (isHoveringThumb || isInTrack)
    {
        g_input.setCursor(CursorType::Hand);
    }

    if (mouseDown && (isDragging || isInTrack))
    {
        isDragging = true;

        float newNormalized = static_cast<float>((mouseX - x - thumbWidth / 2) / (width - thumbWidth));
        newNormalized = std::max(0.0f, std::min(1.0f, newNormalized));

        double newLogVal = logMin + newNormalized * (logMax - logMin);
        double newValue = std::pow(10.0, newLogVal);

        // Use relative epsilon for comparison since values span a huge logarithmic range
        // (from ~0.0000116 to 100, a ratio of ~8.6 million)
        double relativeEpsilon = std::abs(*value) * 0.001; // 0.1% of current value
        if (relativeEpsilon < 1e-10)
        {
            relativeEpsilon = 1e-10; // Minimum epsilon for very small values
        }

        if (std::abs(newValue - *value) > relativeEpsilon)
        {
            *value = newValue;
            valueChanged = true;
        }
    }

    if (!mouseDown)
    {
        isDragging = false;
    }

    float thumbColor = (isHoveringThumb || isDragging) ? 0.95f : 0.8f;
    DrawRoundedRect(thumbX, y, thumbWidth, thumbHeight, 3.0f, thumbColor, thumbColor, thumbColor, 1.0f);

    return valueChanged;
}

// Draw a linear slider with optional snapping
bool DrawLinearSlider(float x,
                      float y,
                      float width,
                      float height,
                      float *value,
                      float minVal,
                      float maxVal,
                      float snapIncrement,
                      double mouseX,
                      double mouseY,
                      bool mouseDown,
                      bool &isDragging)
{
    bool valueChanged = false;

    // Draw track background
    DrawRoundedRect(x, y + height / 2 - 2, width, 4, 2.0f, 0.3f, 0.3f, 0.35f, 0.9f);

    // Calculate normalized position
    float normalizedPos = (*value - minVal) / (maxVal - minVal);
    normalizedPos = std::max(0.0f, std::min(1.0f, normalizedPos));

    float thumbWidth = 12.0f;
    float thumbX = x + normalizedPos * (width - thumbWidth);
    float thumbHeight = height;

    bool isHoveringThumb =
        (mouseX >= thumbX && mouseX <= thumbX + thumbWidth && mouseY >= y && mouseY <= y + thumbHeight);

    if (mouseDown && isHoveringThumb && !isDragging)
    {
        isDragging = true;
    }

    bool isInTrack = (mouseX >= x && mouseX <= x + width && mouseY >= y && mouseY <= y + thumbHeight);

    // Set cursor based on state
    if (isDragging)
    {
        g_input.setCursor(CursorType::Grabbing);
    }
    else if (isHoveringThumb || isInTrack)
    {
        g_input.setCursor(CursorType::Hand);
    }

    if (mouseDown && (isDragging || isInTrack))
    {
        isDragging = true;

        float newNormalized = static_cast<float>((mouseX - x - thumbWidth / 2) / (width - thumbWidth));
        newNormalized = std::max(0.0f, std::min(1.0f, newNormalized));

        float newValue = minVal + newNormalized * (maxVal - minVal);

        // Apply snapping if increment is specified
        if (snapIncrement > 0)
        {
            newValue = std::round(newValue / snapIncrement) * snapIncrement;
        }

        // Clamp to range
        newValue = std::max(minVal, std::min(maxVal, newValue));

        if (std::abs(newValue - *value) > 0.001f)
        {
            *value = newValue;
            valueChanged = true;
        }
    }

    if (!mouseDown)
    {
        isDragging = false;
    }

    // Draw filled portion of track
    float filledWidth = thumbX - x + thumbWidth / 2;
    if (filledWidth > 0)
    {
        DrawRoundedRect(x, y + height / 2 - 2, filledWidth, 4, 2.0f, 0.4f, 0.5f, 0.7f, 0.9f);
    }

    float thumbColor = (isHoveringThumb || isDragging) ? 0.95f : 0.8f;
    DrawRoundedRect(thumbX, y, thumbWidth, thumbHeight, 3.0f, thumbColor, thumbColor, thumbColor, 1.0f);

    return valueChanged;
}

// Draw a checkbox
bool DrawCheckbox(float x,
                  float y,
                  float width,
                  float height,
                  bool checked,
                  const std::string &label,
                  double mouseX,
                  double mouseY,
                  bool mouseClicked)
{
    float cbSize = 14.0f;
    float cbBoxY = y + (height - cbSize) / 2;
    bool isHovering = (mouseX >= x && mouseX <= x + width && mouseY >= y && mouseY <= y + height);

    // Set pointer cursor when hovering
    if (isHovering)
    {
        g_input.setCursor(CursorType::Pointer);
    }

    // Checkbox box background
    DrawControlQuad(x, cbBoxY, cbSize, cbSize, 0.25f, 0.25f, 0.3f, 0.9f);

    // Checkbox border
    float borderC = isHovering ? 0.6f : 0.4f;
    DrawControlLine(x, cbBoxY, x + cbSize, cbBoxY, borderC, borderC, borderC + 0.05f, 0.9f);
    DrawControlLine(x + cbSize, cbBoxY, x + cbSize, cbBoxY + cbSize, borderC, borderC, borderC + 0.05f, 0.9f);
    DrawControlLine(x + cbSize, cbBoxY + cbSize, x, cbBoxY + cbSize, borderC, borderC, borderC + 0.05f, 0.9f);
    DrawControlLine(x, cbBoxY + cbSize, x, cbBoxY, borderC, borderC, borderC + 0.05f, 0.9f);

    // Checkmark if checked
    if (checked)
    {
        DrawControlLine(x + 3,
                        cbBoxY + cbSize * 0.5f,
                        x + cbSize * 0.4f,
                        cbBoxY + cbSize - 3,
                        0.3f,
                        0.9f,
                        0.4f,
                        1.0f,
                        2.0f);
        DrawControlLine(x + cbSize * 0.4f,
                        cbBoxY + cbSize - 3,
                        x + cbSize - 2,
                        cbBoxY + 2,
                        0.3f,
                        0.9f,
                        0.4f,
                        1.0f,
                        2.0f);
    }

    // Label
    DrawText(x + cbSize + 6,
             y + 4,
             label,
             0.75f,
             isHovering ? 0.95f : 0.8f,
             isHovering ? 0.95f : 0.8f,
             isHovering ? 0.95f : 0.8f);

    // Handle click
    if (isHovering && mouseClicked)
    {
        return true;
    }

    return false;
}

// Draw a button
bool DrawButton(float x,
                float y,
                float width,
                float height,
                const std::string &text,
                double mouseX,
                double mouseY,
                bool mouseClicked,
                float bgR,
                float bgG,
                float bgB,
                float bgA,
                float hoverBgR,
                float hoverBgG,
                float hoverBgB,
                float hoverBgA,
                float textR,
                float textG,
                float textB)
{
    bool isHovering = (mouseX >= x && mouseX <= x + width && mouseY >= y && mouseY <= y + height);

    // Set pointer cursor when hovering
    if (isHovering)
    {
        g_input.setCursor(CursorType::Pointer);
    }

    // Button background
    if (isHovering)
    {
        DrawRoundedRect(x, y, width, height, 4.0f, hoverBgR, hoverBgG, hoverBgB, hoverBgA);
    }
    else
    {
        DrawRoundedRect(x, y, width, height, 4.0f, bgR, bgG, bgB, bgA);
    }

    // Button text
    float textWidth = GetTextWidth(text, 0.8f);
    float textX = x + (width - textWidth) / 2.0f;
    DrawText(textX, y + 6, text, 0.8f, textR, textG, textB);

    // Handle click
    if (isHovering && mouseClicked)
    {
        return true;
    }

    return false;
}

// Draw an accordion header
bool DrawAccordionHeader(float x,
                         float y,
                         float width,
                         float height,
                         const std::string &label,
                         bool expanded,
                         double mouseX,
                         double mouseY,
                         bool mouseClicked)
{
    bool isHovering = (mouseX >= x && mouseX <= x + width && mouseY >= y && mouseY <= y + height);

    // Set pointer cursor when hovering
    if (isHovering)
    {
        g_input.setCursor(CursorType::Pointer);
    }

    // Draw arrow
    float arrowX = x;
    float arrowY = y + 3;
    float arrowSize = 10.0f;
    float textColor = isHovering ? 0.95f : 0.75f;
    DrawArrow(arrowX, arrowY, arrowSize, expanded, 0.6f, 0.6f, 0.65f);

    // Header text
    DrawText(x + arrowSize + 4, y + 2, label, 0.75f, textColor, textColor, textColor);

    // Handle click
    if (isHovering && mouseClicked)
    {
        return true;
    }

    return false;
}
