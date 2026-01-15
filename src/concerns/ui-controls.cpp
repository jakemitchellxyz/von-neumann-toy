#include "ui-controls.h"
#include "font-rendering.h"
#include "ui-icons.h"
#include "ui-primitives.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>


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

    if (mouseDown && (isDragging || isInTrack))
    {
        isDragging = true;

        float newNormalized = static_cast<float>((mouseX - x - thumbWidth / 2) / (width - thumbWidth));
        newNormalized = std::max(0.0f, std::min(1.0f, newNormalized));

        double newLogVal = logMin + newNormalized * (logMax - logMin);
        double newValue = std::pow(10.0, newLogVal);

        if (std::abs(newValue - *value) > 0.001)
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

    // Checkbox box background
    glColor4f(0.25f, 0.25f, 0.3f, 0.9f);
    glBegin(GL_QUADS);
    glVertex2f(x, cbBoxY);
    glVertex2f(x + cbSize, cbBoxY);
    glVertex2f(x + cbSize, cbBoxY + cbSize);
    glVertex2f(x, cbBoxY + cbSize);
    glEnd();

    // Checkbox border
    glColor4f(isHovering ? 0.6f : 0.4f, isHovering ? 0.6f : 0.4f, isHovering ? 0.65f : 0.45f, 0.9f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, cbBoxY);
    glVertex2f(x + cbSize, cbBoxY);
    glVertex2f(x + cbSize, cbBoxY + cbSize);
    glVertex2f(x, cbBoxY + cbSize);
    glEnd();

    // Checkmark if checked
    if (checked)
    {
        glColor3f(0.3f, 0.9f, 0.4f);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        glVertex2f(x + 3, cbBoxY + cbSize * 0.5f);
        glVertex2f(x + cbSize * 0.4f, cbBoxY + cbSize - 3);
        glVertex2f(x + cbSize * 0.4f, cbBoxY + cbSize - 3);
        glVertex2f(x + cbSize - 2, cbBoxY + 2);
        glEnd();
        glLineWidth(1.0f);
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
