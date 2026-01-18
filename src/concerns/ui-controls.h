#pragma once

#include <string>

// ==================================
// UI Interactive Controls
// ==================================
// Reusable interactive UI components

// Draw a horizontal slider (logarithmic scale)
// Returns true if value changed
// x, y: position
// width, height: dimensions
// value: pointer to current value (will be modified)
// minVal, maxVal: value range (logarithmic scale)
// mouseX, mouseY: current mouse position
// mouseDown: whether mouse button is currently pressed
// isDragging: reference to dragging state (persists between calls)
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
                bool &isDragging);

// Draw a horizontal slider (linear scale with snapping)
// Returns true if value changed
// x, y: position
// width, height: dimensions
// value: pointer to current value (will be modified)
// minVal, maxVal: value range (linear scale)
// snapIncrement: value to snap to (0 for no snapping)
// mouseX, mouseY: current mouse position
// mouseDown: whether mouse button is currently pressed
// isDragging: reference to dragging state (persists between calls)
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
                      bool &isDragging);

// Draw a checkbox
// Returns true if clicked this frame
// x, y: position
// width, height: dimensions
// checked: whether checkbox is checked
// label: text label
// mouseX, mouseY: current mouse position
// mouseClicked: whether mouse was clicked this frame
bool DrawCheckbox(float x,
                  float y,
                  float width,
                  float height,
                  bool checked,
                  const std::string &label,
                  double mouseX,
                  double mouseY,
                  bool mouseClicked);

// Draw a button
// Returns true if clicked this frame
// x, y: position
// width, height: dimensions
// text: button text
// mouseX, mouseY: current mouse position
// mouseClicked: whether mouse was clicked this frame
// bgR, bgG, bgB, bgA: background color
// hoverBgR, hoverBgG, hoverBgB, hoverBgA: hover background color
// textR, textG, textB: text color
bool DrawButton(float x,
                float y,
                float width,
                float height,
                const std::string &text,
                double mouseX,
                double mouseY,
                bool mouseClicked,
                float bgR = 0.2f,
                float bgG = 0.2f,
                float bgB = 0.25f,
                float bgA = 0.9f,
                float hoverBgR = 0.3f,
                float hoverBgG = 0.3f,
                float hoverBgB = 0.35f,
                float hoverBgA = 0.95f,
                float textR = 0.9f,
                float textG = 0.9f,
                float textB = 0.95f);

// Draw an accordion header
// Returns true if clicked this frame
// x, y: position
// width, height: dimensions
// label: header text
// expanded: whether accordion is expanded
// mouseX, mouseY: current mouse position
// mouseClicked: whether mouse was clicked this frame
bool DrawAccordionHeader(float x,
                         float y,
                         float width,
                         float height,
                         const std::string &label,
                         bool expanded,
                         double mouseX,
                         double mouseY,
                         bool mouseClicked);
