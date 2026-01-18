#pragma once

// ==================================
// UI Icon Drawing Functions
// ==================================
// These functions draw simple icons for UI elements

// Draw an arrow (for tree expand/collapse, accordions)
// x, y: position
// size: icon size
// expanded: true for down arrow (V), false for right arrow (>)
// r, g, b: color
void DrawArrow(float x, float y, float size, bool expanded, float r, float g, float b);

// Draw a left arrow (<) for UI hide button
void DrawLeftArrow(float x, float y, float size, float r, float g, float b);

// Draw an up arrow (^) for open dropdowns
void DrawUpArrow(float x, float y, float size, float r, float g, float b);

// Draw a down arrow (V) for closed dropdowns
void DrawDownArrow(float x, float y, float size, float r, float g, float b);

// Draw folder icon
void DrawFolderIcon(float x, float y, float size, float r, float g, float b);

// Draw play icon (triangle pointing right)
void DrawPlayIcon(float x, float y, float size, float r, float g, float b);

// Draw pause icon (two vertical bars)
void DrawPauseIcon(float x, float y, float size, float r, float g, float b);

// Draw hand icon (pointing/index finger)
void DrawHandIcon(float x, float y, float size, float r, float g, float b);

// Draw measure icon (ruler/measuring tool)
void DrawMeasureIcon(float x, float y, float size, float r, float g, float b);

// Draw shoot icon (crosshair/target)
void DrawShootIcon(float x, float y, float size, float r, float g, float b);

// Draw eye icon (for color picker)
void DrawEyeIcon(float x, float y, float size, float r, float g, float b);

// Draw crosshair (for shoot mode)
void DrawCrosshair(float x, float y, float size);
