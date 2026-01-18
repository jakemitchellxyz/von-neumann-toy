#include "ui-icons.h"
#include "constants.h"
#include "helpers/vulkan.h"
#include <GLFW/glfw3.h>
#include <cmath>

// Helper to draw a line as a thin quad
static void DrawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a, float width = 1.5f)
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
static void DrawQuad(float x, float y, float w, float h, float r, float g, float b, float a)
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

// Draw an arrow (for tree expand/collapse)
void DrawArrow(float x, float y, float size, bool expanded, float r, float g, float b)
{
    if (!g_buildingUIVertices)
        return;

    if (expanded)
    {
        // Down arrow (V shape)
        DrawLine(x, y + size * 0.3f, x + size * 0.5f, y + size * 0.7f, r, g, b, 1.0f);
        DrawLine(x + size * 0.5f, y + size * 0.7f, x + size, y + size * 0.3f, r, g, b, 1.0f);
    }
    else
    {
        // Right arrow (> shape)
        DrawLine(x + size * 0.3f, y, x + size * 0.7f, y + size * 0.5f, r, g, b, 1.0f);
        DrawLine(x + size * 0.7f, y + size * 0.5f, x + size * 0.3f, y + size, r, g, b, 1.0f);
    }
}

// Draw a left arrow (<) for UI hide button
void DrawLeftArrow(float x, float y, float size, float r, float g, float b)
{
    if (!g_buildingUIVertices)
        return;

    // Left arrow (< shape)
    DrawLine(x + size * 0.7f, y, x + size * 0.3f, y + size * 0.5f, r, g, b, 1.0f);
    DrawLine(x + size * 0.3f, y + size * 0.5f, x + size * 0.7f, y + size, r, g, b, 1.0f);
}

// Draw an up arrow (^) for open dropdowns
void DrawUpArrow(float x, float y, float size, float r, float g, float b)
{
    if (!g_buildingUIVertices)
        return;

    // Up arrow (^ shape)
    DrawLine(x, y + size * 0.7f, x + size * 0.5f, y + size * 0.3f, r, g, b, 1.0f);
    DrawLine(x + size * 0.5f, y + size * 0.3f, x + size, y + size * 0.7f, r, g, b, 1.0f);
}

// Draw a down arrow (V) for closed dropdowns
void DrawDownArrow(float x, float y, float size, float r, float g, float b)
{
    if (!g_buildingUIVertices)
        return;

    // Down arrow (V shape)
    DrawLine(x, y + size * 0.3f, x + size * 0.5f, y + size * 0.7f, r, g, b, 1.0f);
    DrawLine(x + size * 0.5f, y + size * 0.7f, x + size, y + size * 0.3f, r, g, b, 1.0f);
}

// Draw folder icon
void DrawFolderIcon(float x, float y, float size, float r, float g, float b)
{
    if (!g_buildingUIVertices)
        return;

    // Folder shape as lines
    DrawLine(x, y + size * 0.2f, x + size * 0.35f, y + size * 0.2f, r, g, b, 1.0f);
    DrawLine(x + size * 0.35f, y + size * 0.2f, x + size * 0.45f, y, r, g, b, 1.0f);
    DrawLine(x + size * 0.45f, y, x + size, y, r, g, b, 1.0f);
    DrawLine(x + size, y, x + size, y + size, r, g, b, 1.0f);
    DrawLine(x + size, y + size, x, y + size, r, g, b, 1.0f);
    DrawLine(x, y + size, x, y + size * 0.2f, r, g, b, 1.0f);
}

// Draw play icon (triangle pointing right)
void DrawPlayIcon(float x, float y, float size, float r, float g, float b)
{
    if (!g_buildingUIVertices)
        return;

    AddUIVertex(x, y, r, g, b, 1.0f);
    AddUIVertex(x + size, y + size * 0.5f, r, g, b, 1.0f);
    AddUIVertex(x, y + size, r, g, b, 1.0f);
}

// Draw pause icon (two vertical bars)
void DrawPauseIcon(float x, float y, float size, float r, float g, float b)
{
    float barWidth = size * 0.25f;
    float gap = size * 0.2f;

    DrawQuad(x, y, barWidth, size, r, g, b, 1.0f);
    DrawQuad(x + barWidth + gap, y, barWidth, size, r, g, b, 1.0f);
}

// Draw hand icon (pointing/index finger)
void DrawHandIcon(float x, float y, float size, float r, float g, float b)
{
    if (!g_buildingUIVertices)
        return;

    float palmHeight = size * 0.4f;
    float palmWidth = size * 0.5f;
    float palmX = x + (size - palmWidth) / 2.0f;
    float palmY = y + size - palmHeight;

    // Palm outline
    DrawLine(palmX, palmY, palmX + palmWidth, palmY, r, g, b, 1.0f, 2.0f);
    DrawLine(palmX + palmWidth, palmY, palmX + palmWidth, palmY + palmHeight, r, g, b, 1.0f, 2.0f);
    DrawLine(palmX + palmWidth, palmY + palmHeight, palmX, palmY + palmHeight, r, g, b, 1.0f, 2.0f);
    DrawLine(palmX, palmY + palmHeight, palmX, palmY, r, g, b, 1.0f, 2.0f);

    // Index finger
    float fingerX = x + size * 0.5f;
    float fingerTipX = fingerX + size * 0.15f;
    float fingerTipY = y;
    DrawLine(fingerX, palmY, fingerTipX, fingerTipY, r, g, b, 1.0f, 2.0f);
}

// Draw measure icon (ruler/measuring tool)
void DrawMeasureIcon(float x, float y, float size, float r, float g, float b)
{
    if (!g_buildingUIVertices)
        return;

    float rulerY = y + size * 0.5f;
    DrawLine(x, rulerY, x + size, rulerY, r, g, b, 1.0f);

    float tickHeight = size * 0.15f;
    for (int i = 0; i <= 4; i++)
    {
        float tickX = x + (size * i) / 4.0f;
        DrawLine(tickX, rulerY - tickHeight / 2.0f, tickX, rulerY + tickHeight / 2.0f, r, g, b, 1.0f);
    }
}

// Draw shoot icon (crosshair/target)
void DrawShootIcon(float x, float y, float size, float r, float g, float b)
{
    if (!g_buildingUIVertices)
        return;

    float centerX = x + size * 0.5f;
    float centerY = y + size * 0.5f;
    float crosshairSize = size * 0.4f;
    float lineLength = crosshairSize * 0.5f;
    float gap = crosshairSize * 0.15f;

    DrawLine(centerX, centerY - gap, centerX, centerY - lineLength, r, g, b, 1.0f, 2.0f);
    DrawLine(centerX, centerY + gap, centerX, centerY + lineLength, r, g, b, 1.0f, 2.0f);
    DrawLine(centerX - gap, centerY, centerX - lineLength, centerY, r, g, b, 1.0f, 2.0f);
    DrawLine(centerX + gap, centerY, centerX + lineLength, centerY, r, g, b, 1.0f, 2.0f);

    // Center circle
    const float PI_VAL = static_cast<float>(PI);
    float circleRadius = size * 0.08f;
    for (int i = 0; i < 16; i++)
    {
        float a1 = 2.0f * PI_VAL * i / 16.0f;
        float a2 = 2.0f * PI_VAL * (i + 1) / 16.0f;
        DrawLine(centerX + cosf(a1) * circleRadius, centerY + sinf(a1) * circleRadius,
                 centerX + cosf(a2) * circleRadius, centerY + sinf(a2) * circleRadius, r, g, b, 1.0f, 2.0f);
    }
}

// Draw eye icon (for color picker)
void DrawEyeIcon(float x, float y, float size, float r, float g, float b)
{
    if (!g_buildingUIVertices)
        return;

    float centerX = x + size * 0.5f;
    float centerY = y + size * 0.5f;
    float eyeWidth = size * 0.6f;
    float eyeHeight = size * 0.4f;
    float pupilSize = size * 0.15f;

    // Eye outline (ellipse)
    int segments = 16;
    for (int i = 0; i < segments; i++)
    {
        float a1 = (i / static_cast<float>(segments)) * 2.0f * 3.14159f;
        float a2 = ((i + 1) / static_cast<float>(segments)) * 2.0f * 3.14159f;
        DrawLine(centerX + cosf(a1) * eyeWidth * 0.5f, centerY + sinf(a1) * eyeHeight * 0.5f,
                 centerX + cosf(a2) * eyeWidth * 0.5f, centerY + sinf(a2) * eyeHeight * 0.5f, r, g, b, 1.0f, 2.0f);
    }

    // Pupil (filled circle)
    for (int i = 0; i < segments; i++)
    {
        float a1 = (i / static_cast<float>(segments)) * 2.0f * 3.14159f;
        float a2 = ((i + 1) / static_cast<float>(segments)) * 2.0f * 3.14159f;
        AddUIVertex(centerX, centerY, r, g, b, 1.0f);
        AddUIVertex(centerX + cosf(a1) * pupilSize * 0.5f, centerY + sinf(a1) * pupilSize * 0.5f, r, g, b, 1.0f);
        AddUIVertex(centerX + cosf(a2) * pupilSize * 0.5f, centerY + sinf(a2) * pupilSize * 0.5f, r, g, b, 1.0f);
    }
}

// Draw crosshair (for shoot mode)
void DrawCrosshair(float x, float y, float size)
{
    if (!g_buildingUIVertices)
        return;

    float lineLength = size * 0.5f;
    float gap = size * 0.15f;
    float r = 1.0f, g = 1.0f, b = 1.0f;

    DrawLine(x, y - gap, x, y - lineLength, r, g, b, 0.9f, 2.0f);
    DrawLine(x, y + gap, x, y + lineLength, r, g, b, 0.9f, 2.0f);
    DrawLine(x - gap, y, x - lineLength, y, r, g, b, 0.9f, 2.0f);
    DrawLine(x + gap, y, x + lineLength, y, r, g, b, 0.9f, 2.0f);

    // Center dot
    const float PI_VAL = static_cast<float>(PI);
    float dotRadius = 2.0f;
    for (int i = 0; i < 8; i++)
    {
        float a1 = 2.0f * PI_VAL * i / 8.0f;
        float a2 = 2.0f * PI_VAL * (i + 1) / 8.0f;
        AddUIVertex(x, y, r, g, b, 0.9f);
        AddUIVertex(x + cosf(a1) * dotRadius, y + sinf(a1) * dotRadius, r, g, b, 0.9f);
        AddUIVertex(x + cosf(a2) * dotRadius, y + sinf(a2) * dotRadius, r, g, b, 0.9f);
    }
}
