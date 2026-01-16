#include "ui-icons.h"
#include "constants.h"
#include <GLFW/glfw3.h>
#include <cmath>

// Draw an arrow (for tree expand/collapse)
void DrawArrow(float x, float y, float size, bool expanded, float r, float g, float b)
{
    glColor3f(r, g, b);
    glLineWidth(1.5f);

    if (expanded)
    {
        // Down arrow (V shape)
        glBegin(GL_LINES);
        glVertex2f(x, y + size * 0.3f);
        glVertex2f(x + size * 0.5f, y + size * 0.7f);
        glVertex2f(x + size * 0.5f, y + size * 0.7f);
        glVertex2f(x + size, y + size * 0.3f);
        glEnd();
    }
    else
    {
        // Right arrow (> shape)
        glBegin(GL_LINES);
        glVertex2f(x + size * 0.3f, y);
        glVertex2f(x + size * 0.7f, y + size * 0.5f);
        glVertex2f(x + size * 0.7f, y + size * 0.5f);
        glVertex2f(x + size * 0.3f, y + size);
        glEnd();
    }
}

// Draw folder icon
void DrawFolderIcon(float x, float y, float size, float r, float g, float b)
{
    glColor3f(r, g, b);
    glLineWidth(1.5f);

    // Folder shape
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y + size * 0.2f);
    glVertex2f(x + size * 0.35f, y + size * 0.2f);
    glVertex2f(x + size * 0.45f, y);
    glVertex2f(x + size, y);
    glVertex2f(x + size, y + size);
    glVertex2f(x, y + size);
    glEnd();
}

// Draw play icon (triangle pointing right)
void DrawPlayIcon(float x, float y, float size, float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_TRIANGLES);
    glVertex2f(x, y);
    glVertex2f(x, y + size);
    glVertex2f(x + size, y + size * 0.5f);
    glEnd();
}

// Draw pause icon (two vertical bars)
void DrawPauseIcon(float x, float y, float size, float r, float g, float b)
{
    glColor3f(r, g, b);
    float barWidth = size * 0.25f;
    float gap = size * 0.2f;

    // Left bar
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + barWidth, y);
    glVertex2f(x + barWidth, y + size);
    glVertex2f(x, y + size);
    glEnd();

    // Right bar
    glBegin(GL_QUADS);
    glVertex2f(x + barWidth + gap, y);
    glVertex2f(x + barWidth + gap + barWidth, y);
    glVertex2f(x + barWidth + gap + barWidth, y + size);
    glVertex2f(x + barWidth + gap, y + size);
    glEnd();
}

// Draw hand icon (pointing/index finger)
void DrawHandIcon(float x, float y, float size, float r, float g, float b)
{
    glColor3f(r, g, b);
    glLineWidth(2.0f);

    // Palm (rounded rectangle at bottom)
    float palmHeight = size * 0.4f;
    float palmWidth = size * 0.5f;
    float palmX = x + (size - palmWidth) / 2.0f;
    float palmY = y + size - palmHeight;

    glBegin(GL_LINE_LOOP);
    glVertex2f(palmX, palmY);
    glVertex2f(palmX + palmWidth, palmY);
    glVertex2f(palmX + palmWidth, palmY + palmHeight);
    glVertex2f(palmX, palmY + palmHeight);
    glEnd();

    // Index finger (pointing up)
    float fingerX = x + size * 0.5f;
    float fingerY = y + size * 0.2f;
    float fingerTipX = fingerX + size * 0.15f;
    float fingerTipY = y;

    glBegin(GL_LINES);
    // Finger base to tip
    glVertex2f(fingerX, palmY);
    glVertex2f(fingerTipX, fingerTipY);
    // Finger side lines
    glVertex2f(fingerX - size * 0.08f, palmY + size * 0.1f);
    glVertex2f(fingerTipX - size * 0.05f, fingerTipY + size * 0.1f);
    glVertex2f(fingerX + size * 0.08f, palmY + size * 0.1f);
    glVertex2f(fingerTipX + size * 0.05f, fingerTipY + size * 0.1f);
    glEnd();

    glLineWidth(1.0f);
}

// Draw measure icon (ruler/measuring tool)
void DrawMeasureIcon(float x, float y, float size, float r, float g, float b)
{
    glColor3f(r, g, b);
    glLineWidth(1.5f);

    // Ruler body (horizontal line)
    float rulerY = y + size * 0.5f;
    glBegin(GL_LINES);
    glVertex2f(x, rulerY);
    glVertex2f(x + size, rulerY);
    glEnd();

    // Tick marks
    float tickHeight = size * 0.15f;
    for (int i = 0; i <= 4; i++)
    {
        float tickX = x + (size * i) / 4.0f;
        glBegin(GL_LINES);
        glVertex2f(tickX, rulerY - tickHeight / 2.0f);
        glVertex2f(tickX, rulerY + tickHeight / 2.0f);
        glEnd();
    }

    glLineWidth(1.0f);
}

// Draw shoot icon (crosshair/target)
void DrawShootIcon(float x, float y, float size, float r, float g, float b)
{
    glColor3f(r, g, b);
    glLineWidth(2.0f);

    float centerX = x + size * 0.5f;
    float centerY = y + size * 0.5f;
    float crosshairSize = size * 0.4f;
    float lineLength = crosshairSize * 0.5f;
    float gap = crosshairSize * 0.15f;

    // Draw crosshair lines
    glBegin(GL_LINES);
    // Top
    glVertex2f(centerX, centerY - gap);
    glVertex2f(centerX, centerY - lineLength);
    // Bottom
    glVertex2f(centerX, centerY + gap);
    glVertex2f(centerX, centerY + lineLength);
    // Left
    glVertex2f(centerX - gap, centerY);
    glVertex2f(centerX - lineLength, centerY);
    // Right
    glVertex2f(centerX + gap, centerY);
    glVertex2f(centerX + lineLength, centerY);
    glEnd();

    // Draw center circle
    const float PI_VAL = static_cast<float>(PI);
    float circleRadius = size * 0.08f;
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i <= 16; i++)
    {
        float angle = 2.0f * PI_VAL * i / 16.0f;
        glVertex2f(centerX + cos(angle) * circleRadius, centerY + sin(angle) * circleRadius);
    }
    glEnd();

    glLineWidth(1.0f);
}

// Draw eye icon (for color picker)
void DrawEyeIcon(float x, float y, float size, float r, float g, float b)
{
    glColor3f(r, g, b);
    glLineWidth(2.0f);

    float centerX = x + size * 0.5f;
    float centerY = y + size * 0.5f;
    float eyeWidth = size * 0.6f;
    float eyeHeight = size * 0.4f;
    float pupilSize = size * 0.15f;

    // Draw eye outline (ellipse)
    glBegin(GL_LINE_LOOP);
    int segments = 16;
    for (int i = 0; i < segments; i++)
    {
        float angle = (i / static_cast<float>(segments)) * 2.0f * 3.14159f;
        float px = centerX + cos(angle) * eyeWidth * 0.5f;
        float py = centerY + sin(angle) * eyeHeight * 0.5f;
        glVertex2f(px, py);
    }
    glEnd();

    // Draw pupil (filled circle in center)
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(centerX, centerY); // Center
    for (int i = 0; i <= segments; i++)
    {
        float angle = (i / static_cast<float>(segments)) * 2.0f * 3.14159f;
        float px = centerX + cos(angle) * pupilSize * 0.5f;
        float py = centerY + sin(angle) * pupilSize * 0.5f;
        glVertex2f(px, py);
    }
    glEnd();

    glLineWidth(1.0f);
}

// Draw crosshair (for shoot mode)
void DrawCrosshair(float x, float y, float size)
{
    glColor4f(1.0f, 1.0f, 1.0f, 0.9f);
    glLineWidth(2.0f);

    float lineLength = size * 0.5f;
    float gap = size * 0.15f;

    // Draw crosshair lines
    glBegin(GL_LINES);
    // Top
    glVertex2f(x, y - gap);
    glVertex2f(x, y - lineLength);
    // Bottom
    glVertex2f(x, y + gap);
    glVertex2f(x, y + lineLength);
    // Left
    glVertex2f(x - gap, y);
    glVertex2f(x - lineLength, y);
    // Right
    glVertex2f(x + gap, y);
    glVertex2f(x + lineLength, y);
    glEnd();

    // Draw center dot
    const float PI_VAL = static_cast<float>(PI);
    float dotRadius = 2.0f;
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= 8; i++)
    {
        float angle = 2.0f * PI_VAL * i / 8.0f;
        glVertex2f(x + cos(angle) * dotRadius, y + sin(angle) * dotRadius);
    }
    glEnd();

    glLineWidth(1.0f);
}
