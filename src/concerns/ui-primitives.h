#pragma once

#include <string>

// ==================================
// UI Primitive Drawing Functions
// ==================================
// Basic shapes and utilities for UI rendering

// Draw a rounded rectangle
// x, y: top-left position
// width, height: dimensions
// radius: corner radius
// r, g, b, a: color (0.0-1.0)
void DrawRoundedRect(float x, float y, float width, float height, float radius, float r, float g, float b, float a);

// Draw tooltip
// mouseX, mouseY: mouse position
// text: tooltip text
// screenWidth, screenHeight: screen dimensions (for clamping)
void DrawTooltip(float mouseX, float mouseY, const std::string &text, int screenWidth, int screenHeight);
