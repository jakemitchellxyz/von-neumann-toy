#pragma once

// Protect from Windows.h macros that conflict with DrawText
#ifdef DrawText
#undef DrawText
#endif
#ifdef DrawTextA
#undef DrawTextA
#endif
#ifdef DrawTextW
#undef DrawTextW
#endif

#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>


// Character segment structure
struct CharSegment
{
    float x1, y1, x2, y2;
};

// Character data (exported for custom rendering)
extern std::map<char, std::vector<CharSegment>> CHAR_SEGMENTS;
extern std::map<char, float> CHAR_WIDTHS;

// ==================================
// 2D Text Rendering (for UI)
// ==================================

// Draw text at 2D screen coordinates (for UI overlay)
// x, y: screen coordinates (pixels)
// text: string to render
// scale: size multiplier
// r, g, b: color (0.0-1.0)
void DrawText(float x, float y, const std::string &text, float scale, float r, float g, float b);

// Get the width of text in pixels (for layout calculations)
float GetTextWidth(const std::string &text, float scale);

// Draw a number as text
void DrawNumber(float x, float y, int number, float scale, float r, float g, float b);

// ==================================
// 3D Text Rendering (for 3D space)
// ==================================

// Draw billboarded text in 3D space (always faces camera)
// pos: 3D world position
// text: string to render
// cameraPos: camera position in world space (for billboarding)
// targetPixelSize: desired height in screen pixels (default 12px)
void DrawBillboardText3D(const glm::vec3 &pos,
                         const std::string &text,
                         const glm::vec3 &cameraPos,
                         float targetPixelSize = 12.0f);
