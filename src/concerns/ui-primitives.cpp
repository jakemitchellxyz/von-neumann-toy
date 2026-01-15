#include "ui-primitives.h"
#include "font-rendering.h"
#include "constants.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

// Draw a rounded rectangle
void DrawRoundedRect(float x, float y, float width, float height, float radius, float r, float g, float b, float a)
{
    const int cornerSegments = 8;
    const float PI_VAL = static_cast<float>(PI);

    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);

    float cx = x + width / 2;
    float cy = y + height / 2;
    glVertex2f(cx, cy);

    for (int i = 0; i <= cornerSegments; i++)
    {
        float angle = PI_VAL + (PI_VAL / 2) * i / cornerSegments;
        glVertex2f(x + radius + radius * cos(angle), y + radius + radius * sin(angle));
    }
    for (int i = 0; i <= cornerSegments; i++)
    {
        float angle = 3 * PI_VAL / 2 + (PI_VAL / 2) * i / cornerSegments;
        glVertex2f(x + width - radius + radius * cos(angle), y + radius + radius * sin(angle));
    }
    for (int i = 0; i <= cornerSegments; i++)
    {
        float angle = 0 + (PI_VAL / 2) * i / cornerSegments;
        glVertex2f(x + width - radius + radius * cos(angle), y + height - radius + radius * sin(angle));
    }
    for (int i = 0; i <= cornerSegments; i++)
    {
        float angle = PI_VAL / 2 + (PI_VAL / 2) * i / cornerSegments;
        glVertex2f(x + radius + radius * cos(angle), y + height - radius + radius * sin(angle));
    }
    glVertex2f(x + radius + radius * cos(PI_VAL), y + radius + radius * sin(PI_VAL));

    glEnd();
}

// Draw tooltip
void DrawTooltip(float mouseX, float mouseY, const std::string &text, int screenWidth, int screenHeight)
{
    float padding = 6.0f;
    float textWidth = GetTextWidth(text, 0.85f);
    float tooltipWidth = textWidth + padding * 2;
    float tooltipHeight = 20.0f;

    // Position above cursor
    float tooltipX = mouseX - tooltipWidth / 2;
    float tooltipY = mouseY - tooltipHeight - 10.0f;

    // Keep on screen
    if (tooltipX < 5)
        tooltipX = 5;
    if (tooltipX + tooltipWidth > screenWidth - 5)
        tooltipX = screenWidth - 5 - tooltipWidth;
    if (tooltipY < 5)
        tooltipY = mouseY + 20.0f; // Show below if no room above

    // Background
    DrawRoundedRect(tooltipX, tooltipY, tooltipWidth, tooltipHeight, 4.0f, 0.15f, 0.15f, 0.18f, 0.95f);

    // Text
    DrawText(tooltipX + padding, tooltipY + 4.0f, text, 0.85f, 0.95f, 0.95f, 0.95f);
}
