#include "font-rendering.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <glm/glm.hpp>
#include <map>
#include <vector>


// ==================================
// Font Data Structures
// ==================================
// CharSegment is now defined in font-rendering.h

// Character width lookup (proportional spacing)
std::map<char, float> CHAR_WIDTHS = {
    {'A', 0.8f}, {'B', 0.7f}, {'C', 0.7f},  {'D', 0.7f},  {'E', 0.6f}, {'F', 0.6f},  {'G', 0.8f}, {'H', 0.7f},
    {'I', 0.3f}, {'J', 0.5f}, {'K', 0.7f},  {'L', 0.6f},  {'M', 0.9f}, {'N', 0.7f},  {'O', 0.8f}, {'P', 0.7f},
    {'Q', 0.8f}, {'R', 0.7f}, {'S', 0.7f},  {'T', 0.7f},  {'U', 0.7f}, {'V', 0.8f},  {'W', 1.0f}, {'X', 0.7f},
    {'Y', 0.7f}, {'Z', 0.7f}, {'a', 0.6f},  {'b', 0.6f},  {'c', 0.5f}, {'d', 0.6f},  {'e', 0.6f}, {'f', 0.4f},
    {'g', 0.6f}, {'h', 0.6f}, {'i', 0.25f}, {'j', 0.3f},  {'k', 0.6f}, {'l', 0.25f}, {'m', 0.9f}, {'n', 0.6f},
    {'o', 0.6f}, {'p', 0.6f}, {'q', 0.6f},  {'r', 0.4f},  {'s', 0.5f}, {'t', 0.4f},  {'u', 0.6f}, {'v', 0.6f},
    {'w', 0.9f}, {'x', 0.6f}, {'y', 0.6f},  {'z', 0.5f},  {'0', 0.6f}, {'1', 0.4f},  {'2', 0.6f}, {'3', 0.6f},
    {'4', 0.6f}, {'5', 0.6f}, {'6', 0.6f},  {'7', 0.6f},  {'8', 0.6f}, {'9', 0.6f},  {' ', 0.4f}, {'-', 0.4f},
    {'_', 0.5f}, {'.', 0.2f}, {',', 0.2f},  {':', 0.25f}, {'/', 0.4f}, {'<', 0.5f},  {'>', 0.5f}, {'(', 0.3f},
    {')', 0.3f},
};

// Simplified character segment definitions
std::map<char, std::vector<CharSegment>> CHAR_SEGMENTS = {
    // Uppercase letters
    {'A', {{0.0f, 1.0f, 0.4f, 0.0f}, {0.4f, 0.0f, 0.8f, 1.0f}, {0.15f, 0.6f, 0.65f, 0.6f}}},
    {'B',
     {{0.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 0.5f, 0.0f},
      {0.5f, 0.0f, 0.6f, 0.15f},
      {0.6f, 0.15f, 0.6f, 0.35f},
      {0.6f, 0.35f, 0.5f, 0.5f},
      {0.0f, 0.5f, 0.5f, 0.5f},
      {0.5f, 0.5f, 0.7f, 0.65f},
      {0.7f, 0.65f, 0.7f, 0.85f},
      {0.7f, 0.85f, 0.5f, 1.0f},
      {0.0f, 1.0f, 0.5f, 1.0f}}},
    {'C',
     {{0.7f, 0.15f, 0.5f, 0.0f},
      {0.5f, 0.0f, 0.2f, 0.0f},
      {0.2f, 0.0f, 0.0f, 0.2f},
      {0.0f, 0.2f, 0.0f, 0.8f},
      {0.0f, 0.8f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.5f, 1.0f},
      {0.5f, 1.0f, 0.7f, 0.85f}}},
    {'D',
     {{0.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 0.4f, 0.0f},
      {0.4f, 0.0f, 0.7f, 0.2f},
      {0.7f, 0.2f, 0.7f, 0.8f},
      {0.7f, 0.8f, 0.4f, 1.0f},
      {0.4f, 1.0f, 0.0f, 1.0f}}},
    {'E', {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.6f, 0.0f}, {0.0f, 0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.6f, 1.0f}}},
    {'F', {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.6f, 0.0f}, {0.0f, 0.5f, 0.5f, 0.5f}}},
    {'G',
     {{0.7f, 0.15f, 0.5f, 0.0f},
      {0.5f, 0.0f, 0.2f, 0.0f},
      {0.2f, 0.0f, 0.0f, 0.2f},
      {0.0f, 0.2f, 0.0f, 0.8f},
      {0.0f, 0.8f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.5f, 1.0f},
      {0.5f, 1.0f, 0.7f, 0.8f},
      {0.7f, 0.8f, 0.7f, 0.5f},
      {0.7f, 0.5f, 0.4f, 0.5f}}},
    {'H', {{0.0f, 0.0f, 0.0f, 1.0f}, {0.7f, 0.0f, 0.7f, 1.0f}, {0.0f, 0.5f, 0.7f, 0.5f}}},
    {'I', {{0.15f, 0.0f, 0.15f, 1.0f}}},
    {'J',
     {{0.5f, 0.0f, 0.5f, 0.8f}, {0.5f, 0.8f, 0.35f, 1.0f}, {0.35f, 1.0f, 0.15f, 1.0f}, {0.15f, 1.0f, 0.0f, 0.85f}}},
    {'K', {{0.0f, 0.0f, 0.0f, 1.0f}, {0.6f, 0.0f, 0.0f, 0.5f}, {0.0f, 0.5f, 0.7f, 1.0f}}},
    {'L', {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.6f, 1.0f}}},
    {'M', {{0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.45f, 0.5f}, {0.45f, 0.5f, 0.9f, 0.0f}, {0.9f, 0.0f, 0.9f, 1.0f}}},
    {'N', {{0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.7f, 1.0f}, {0.7f, 1.0f, 0.7f, 0.0f}}},
    {'O',
     {{0.2f, 0.0f, 0.6f, 0.0f},
      {0.6f, 0.0f, 0.8f, 0.2f},
      {0.8f, 0.2f, 0.8f, 0.8f},
      {0.8f, 0.8f, 0.6f, 1.0f},
      {0.6f, 1.0f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.0f, 0.8f},
      {0.0f, 0.8f, 0.0f, 0.2f},
      {0.0f, 0.2f, 0.2f, 0.0f}}},
    {'P',
     {{0.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 0.5f, 0.0f},
      {0.5f, 0.0f, 0.7f, 0.15f},
      {0.7f, 0.15f, 0.7f, 0.35f},
      {0.7f, 0.35f, 0.5f, 0.5f},
      {0.5f, 0.5f, 0.0f, 0.5f}}},
    {'Q',
     {{0.2f, 0.0f, 0.6f, 0.0f},
      {0.6f, 0.0f, 0.8f, 0.2f},
      {0.8f, 0.2f, 0.8f, 0.8f},
      {0.8f, 0.8f, 0.6f, 1.0f},
      {0.6f, 1.0f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.0f, 0.8f},
      {0.0f, 0.8f, 0.0f, 0.2f},
      {0.0f, 0.2f, 0.2f, 0.0f},
      {0.5f, 0.7f, 0.8f, 1.0f}}},
    {'R',
     {{0.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 0.5f, 0.0f},
      {0.5f, 0.0f, 0.7f, 0.15f},
      {0.7f, 0.15f, 0.7f, 0.35f},
      {0.7f, 0.35f, 0.5f, 0.5f},
      {0.5f, 0.5f, 0.0f, 0.5f},
      {0.3f, 0.5f, 0.7f, 1.0f}}},
    {'S',
     {{0.7f, 0.15f, 0.5f, 0.0f},
      {0.5f, 0.0f, 0.2f, 0.0f},
      {0.2f, 0.0f, 0.0f, 0.15f},
      {0.0f, 0.15f, 0.0f, 0.35f},
      {0.0f, 0.35f, 0.2f, 0.5f},
      {0.2f, 0.5f, 0.5f, 0.5f},
      {0.5f, 0.5f, 0.7f, 0.65f},
      {0.7f, 0.65f, 0.7f, 0.85f},
      {0.7f, 0.85f, 0.5f, 1.0f},
      {0.5f, 1.0f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.0f, 0.85f}}},
    {'T', {{0.0f, 0.0f, 0.7f, 0.0f}, {0.35f, 0.0f, 0.35f, 1.0f}}},
    {'U',
     {{0.0f, 0.0f, 0.0f, 0.8f},
      {0.0f, 0.8f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.5f, 1.0f},
      {0.5f, 1.0f, 0.7f, 0.8f},
      {0.7f, 0.8f, 0.7f, 0.0f}}},
    {'V', {{0.0f, 0.0f, 0.4f, 1.0f}, {0.4f, 1.0f, 0.8f, 0.0f}}},
    {'W', {{0.0f, 0.0f, 0.2f, 1.0f}, {0.2f, 1.0f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.8f, 1.0f}, {0.8f, 1.0f, 1.0f, 0.0f}}},
    {'X', {{0.0f, 0.0f, 0.7f, 1.0f}, {0.7f, 0.0f, 0.0f, 1.0f}}},
    {'Y', {{0.0f, 0.0f, 0.35f, 0.5f}, {0.7f, 0.0f, 0.35f, 0.5f}, {0.35f, 0.5f, 0.35f, 1.0f}}},
    {'Z', {{0.0f, 0.0f, 0.7f, 0.0f}, {0.7f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.7f, 1.0f}}},
    // Lowercase
    {'a',
     {{0.1f, 0.3f, 0.5f, 0.3f},
      {0.5f, 0.3f, 0.6f, 0.4f},
      {0.6f, 0.4f, 0.6f, 1.0f},
      {0.6f, 0.6f, 0.1f, 0.6f},
      {0.1f, 0.6f, 0.0f, 0.7f},
      {0.0f, 0.7f, 0.0f, 0.9f},
      {0.0f, 0.9f, 0.1f, 1.0f},
      {0.1f, 1.0f, 0.6f, 1.0f}}},
    {'b',
     {{0.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.4f, 0.4f, 0.4f},
      {0.4f, 0.4f, 0.6f, 0.55f},
      {0.6f, 0.55f, 0.6f, 0.85f},
      {0.6f, 0.85f, 0.4f, 1.0f},
      {0.4f, 1.0f, 0.0f, 1.0f}}},
    {'c',
     {{0.5f, 0.4f, 0.2f, 0.4f},
      {0.2f, 0.4f, 0.0f, 0.55f},
      {0.0f, 0.55f, 0.0f, 0.85f},
      {0.0f, 0.85f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.5f, 1.0f}}},
    {'d',
     {{0.6f, 0.0f, 0.6f, 1.0f},
      {0.6f, 0.4f, 0.2f, 0.4f},
      {0.2f, 0.4f, 0.0f, 0.55f},
      {0.0f, 0.55f, 0.0f, 0.85f},
      {0.0f, 0.85f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.6f, 1.0f}}},
    {'e',
     {{0.0f, 0.7f, 0.6f, 0.7f},
      {0.6f, 0.7f, 0.6f, 0.5f},
      {0.6f, 0.5f, 0.4f, 0.4f},
      {0.4f, 0.4f, 0.2f, 0.4f},
      {0.2f, 0.4f, 0.0f, 0.55f},
      {0.0f, 0.55f, 0.0f, 0.85f},
      {0.0f, 0.85f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.5f, 1.0f}}},
    {'f',
     {{0.4f, 0.15f, 0.3f, 0.0f}, {0.3f, 0.0f, 0.15f, 0.0f}, {0.15f, 0.0f, 0.15f, 1.0f}, {0.0f, 0.4f, 0.35f, 0.4f}}},
    {'g',
     {{0.6f, 0.4f, 0.2f, 0.4f},
      {0.2f, 0.4f, 0.0f, 0.55f},
      {0.0f, 0.55f, 0.0f, 0.7f},
      {0.0f, 0.7f, 0.2f, 0.85f},
      {0.2f, 0.85f, 0.6f, 0.85f},
      {0.6f, 0.4f, 0.6f, 1.1f},
      {0.6f, 1.1f, 0.4f, 1.2f},
      {0.4f, 1.2f, 0.1f, 1.2f}}},
    {'h', {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.6f, 0.55f}, {0.6f, 0.55f, 0.6f, 1.0f}}},
    {'i', {{0.12f, 0.4f, 0.12f, 1.0f}, {0.12f, 0.15f, 0.12f, 0.2f}}},
    {'j', {{0.2f, 0.4f, 0.2f, 1.1f}, {0.2f, 1.1f, 0.1f, 1.2f}, {0.1f, 1.2f, 0.0f, 1.2f}, {0.2f, 0.15f, 0.2f, 0.2f}}},
    {'k', {{0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 0.4f, 0.0f, 0.7f}, {0.0f, 0.7f, 0.6f, 1.0f}}},
    {'l', {{0.12f, 0.0f, 0.12f, 1.0f}}},
    {'m',
     {{0.0f, 0.4f, 0.0f, 1.0f},
      {0.0f, 0.4f, 0.3f, 0.4f},
      {0.3f, 0.4f, 0.4f, 0.5f},
      {0.4f, 0.5f, 0.4f, 1.0f},
      {0.4f, 0.4f, 0.7f, 0.4f},
      {0.7f, 0.4f, 0.9f, 0.5f},
      {0.9f, 0.5f, 0.9f, 1.0f}}},
    {'n', {{0.0f, 0.4f, 0.0f, 1.0f}, {0.0f, 0.4f, 0.4f, 0.4f}, {0.4f, 0.4f, 0.6f, 0.55f}, {0.6f, 0.55f, 0.6f, 1.0f}}},
    {'o',
     {{0.2f, 0.4f, 0.4f, 0.4f},
      {0.4f, 0.4f, 0.6f, 0.55f},
      {0.6f, 0.55f, 0.6f, 0.85f},
      {0.6f, 0.85f, 0.4f, 1.0f},
      {0.4f, 1.0f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.0f, 0.85f},
      {0.0f, 0.85f, 0.0f, 0.55f},
      {0.0f, 0.55f, 0.2f, 0.4f}}},
    {'p',
     {{0.0f, 0.4f, 0.0f, 1.2f},
      {0.0f, 0.4f, 0.4f, 0.4f},
      {0.4f, 0.4f, 0.6f, 0.55f},
      {0.6f, 0.55f, 0.6f, 0.85f},
      {0.6f, 0.85f, 0.4f, 1.0f},
      {0.4f, 1.0f, 0.0f, 1.0f}}},
    {'q',
     {{0.6f, 0.4f, 0.6f, 1.2f},
      {0.6f, 0.4f, 0.2f, 0.4f},
      {0.2f, 0.4f, 0.0f, 0.55f},
      {0.0f, 0.55f, 0.0f, 0.85f},
      {0.0f, 0.85f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.6f, 1.0f}}},
    {'r', {{0.0f, 0.4f, 0.0f, 1.0f}, {0.0f, 0.5f, 0.2f, 0.4f}, {0.2f, 0.4f, 0.4f, 0.4f}}},
    {'s',
     {{0.5f, 0.45f, 0.2f, 0.4f},
      {0.2f, 0.4f, 0.0f, 0.5f},
      {0.0f, 0.5f, 0.2f, 0.65f},
      {0.2f, 0.65f, 0.4f, 0.7f},
      {0.4f, 0.7f, 0.5f, 0.8f},
      {0.5f, 0.8f, 0.4f, 0.95f},
      {0.4f, 0.95f, 0.1f, 1.0f}}},
    {'t', {{0.2f, 0.0f, 0.2f, 0.9f}, {0.2f, 0.9f, 0.35f, 1.0f}, {0.0f, 0.4f, 0.4f, 0.4f}}},
    {'u', {{0.0f, 0.4f, 0.0f, 0.85f}, {0.0f, 0.85f, 0.2f, 1.0f}, {0.2f, 1.0f, 0.6f, 1.0f}, {0.6f, 0.4f, 0.6f, 1.0f}}},
    {'v', {{0.0f, 0.4f, 0.3f, 1.0f}, {0.3f, 1.0f, 0.6f, 0.4f}}},
    {'w',
     {{0.0f, 0.4f, 0.15f, 1.0f}, {0.15f, 1.0f, 0.45f, 0.6f}, {0.45f, 0.6f, 0.75f, 1.0f}, {0.75f, 1.0f, 0.9f, 0.4f}}},
    {'x', {{0.0f, 0.4f, 0.6f, 1.0f}, {0.6f, 0.4f, 0.0f, 1.0f}}},
    {'y',
     {{0.0f, 0.4f, 0.0f, 0.7f},
      {0.0f, 0.7f, 0.3f, 1.0f},
      {0.6f, 0.4f, 0.6f, 1.1f},
      {0.6f, 1.1f, 0.4f, 1.2f},
      {0.4f, 1.2f, 0.1f, 1.2f}}},
    {'z', {{0.0f, 0.4f, 0.5f, 0.4f}, {0.5f, 0.4f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.5f, 1.0f}}},
    // Numbers
    {'0',
     {{0.2f, 0.0f, 0.4f, 0.0f},
      {0.4f, 0.0f, 0.6f, 0.15f},
      {0.6f, 0.15f, 0.6f, 0.85f},
      {0.6f, 0.85f, 0.4f, 1.0f},
      {0.4f, 1.0f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.0f, 0.85f},
      {0.0f, 0.85f, 0.0f, 0.15f},
      {0.0f, 0.15f, 0.2f, 0.0f}}},
    {'1', {{0.3f, 0.0f, 0.3f, 1.0f}, {0.1f, 0.2f, 0.3f, 0.0f}}},
    {'2',
     {{0.0f, 0.15f, 0.2f, 0.0f},
      {0.2f, 0.0f, 0.4f, 0.0f},
      {0.4f, 0.0f, 0.6f, 0.15f},
      {0.6f, 0.15f, 0.6f, 0.4f},
      {0.6f, 0.4f, 0.0f, 1.0f},
      {0.0f, 1.0f, 0.6f, 1.0f}}},
    {'3',
     {{0.0f, 0.15f, 0.2f, 0.0f},
      {0.2f, 0.0f, 0.4f, 0.0f},
      {0.4f, 0.0f, 0.6f, 0.15f},
      {0.6f, 0.15f, 0.6f, 0.4f},
      {0.6f, 0.4f, 0.4f, 0.5f},
      {0.4f, 0.5f, 0.2f, 0.5f},
      {0.4f, 0.5f, 0.6f, 0.6f},
      {0.6f, 0.6f, 0.6f, 0.85f},
      {0.6f, 0.85f, 0.4f, 1.0f},
      {0.4f, 1.0f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.0f, 0.85f}}},
    {'4', {{0.5f, 0.0f, 0.5f, 1.0f}, {0.5f, 0.6f, 0.0f, 0.6f}, {0.0f, 0.6f, 0.0f, 0.0f}}},
    {'5',
     {{0.6f, 0.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f, 0.45f},
      {0.0f, 0.45f, 0.4f, 0.45f},
      {0.4f, 0.45f, 0.6f, 0.6f},
      {0.6f, 0.6f, 0.6f, 0.85f},
      {0.6f, 0.85f, 0.4f, 1.0f},
      {0.4f, 1.0f, 0.1f, 1.0f},
      {0.1f, 1.0f, 0.0f, 0.9f}}},
    {'6',
     {{0.5f, 0.0f, 0.2f, 0.0f},
      {0.2f, 0.0f, 0.0f, 0.2f},
      {0.0f, 0.2f, 0.0f, 0.85f},
      {0.0f, 0.85f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.4f, 1.0f},
      {0.4f, 1.0f, 0.6f, 0.85f},
      {0.6f, 0.85f, 0.6f, 0.6f},
      {0.6f, 0.6f, 0.4f, 0.45f},
      {0.4f, 0.45f, 0.0f, 0.45f}}},
    {'7', {{0.0f, 0.0f, 0.6f, 0.0f}, {0.6f, 0.0f, 0.2f, 1.0f}}},
    {'8',
     {{0.2f, 0.0f, 0.4f, 0.0f},
      {0.4f, 0.0f, 0.55f, 0.1f},
      {0.55f, 0.1f, 0.55f, 0.4f},
      {0.55f, 0.4f, 0.3f, 0.5f},
      {0.3f, 0.5f, 0.05f, 0.4f},
      {0.05f, 0.4f, 0.05f, 0.1f},
      {0.05f, 0.1f, 0.2f, 0.0f},
      {0.3f, 0.5f, 0.6f, 0.6f},
      {0.6f, 0.6f, 0.6f, 0.9f},
      {0.6f, 0.9f, 0.4f, 1.0f},
      {0.4f, 1.0f, 0.2f, 1.0f},
      {0.2f, 1.0f, 0.0f, 0.9f},
      {0.0f, 0.9f, 0.0f, 0.6f},
      {0.0f, 0.6f, 0.3f, 0.5f}}},
    {'9',
     {{0.1f, 1.0f, 0.4f, 1.0f},
      {0.4f, 1.0f, 0.6f, 0.8f},
      {0.6f, 0.8f, 0.6f, 0.15f},
      {0.6f, 0.15f, 0.4f, 0.0f},
      {0.4f, 0.0f, 0.2f, 0.0f},
      {0.2f, 0.0f, 0.0f, 0.15f},
      {0.0f, 0.15f, 0.0f, 0.4f},
      {0.0f, 0.4f, 0.2f, 0.55f},
      {0.2f, 0.55f, 0.6f, 0.55f}}},
    // Punctuation
    {' ', {}},
    {'-', {{0.1f, 0.5f, 0.3f, 0.5f}}},
    {'_', {{0.0f, 1.0f, 0.5f, 1.0f}}},
    {'.', {{0.1f, 0.9f, 0.1f, 1.0f}}},
    {',', {{0.1f, 0.9f, 0.0f, 1.1f}}},
    {':', {{0.1f, 0.3f, 0.1f, 0.35f}, {0.1f, 0.7f, 0.1f, 0.75f}}},
    {'/', {{0.0f, 1.0f, 0.4f, 0.0f}}},
    {'<', {{0.4f, 0.2f, 0.0f, 0.5f}, {0.0f, 0.5f, 0.4f, 0.8f}}},
    {'>', {{0.0f, 0.2f, 0.4f, 0.5f}, {0.4f, 0.5f, 0.0f, 0.8f}}},
    {'(', {{0.25f, 0.0f, 0.1f, 0.2f}, {0.1f, 0.2f, 0.1f, 0.8f}, {0.1f, 0.8f, 0.25f, 1.0f}}},
    {')', {{0.05f, 0.0f, 0.2f, 0.2f}, {0.2f, 0.2f, 0.2f, 0.8f}, {0.2f, 0.8f, 0.05f, 1.0f}}},
};

// ==================================
// 2D Text Rendering Implementation
// ==================================

void DrawText(float x, float y, const std::string &text, float scale, float r, float g, float b)
{
    glColor3f(r, g, b);
    glLineWidth(1.5f);

    float charHeight = 12.0f * scale;
    float currentX = x;

    for (char c : text)
    {
        auto segIt = CHAR_SEGMENTS.find(c);
        auto widthIt = CHAR_WIDTHS.find(c);

        float charWidth = (widthIt != CHAR_WIDTHS.end()) ? widthIt->second * charHeight : 0.5f * charHeight;

        if (segIt != CHAR_SEGMENTS.end())
        {
            glBegin(GL_LINES);
            for (const auto &seg : segIt->second)
            {
                glVertex2f(currentX + seg.x1 * charWidth, y + seg.y1 * charHeight);
                glVertex2f(currentX + seg.x2 * charWidth, y + seg.y2 * charHeight);
            }
            glEnd();
        }

        currentX += charWidth + 2.0f * scale;
    }
}

float GetTextWidth(const std::string &text, float scale)
{
    float charHeight = 12.0f * scale;
    float width = 0;

    for (char c : text)
    {
        auto widthIt = CHAR_WIDTHS.find(c);
        float charWidth = (widthIt != CHAR_WIDTHS.end()) ? widthIt->second * charHeight : 0.5f * charHeight;
        width += charWidth + 2.0f * scale;
    }

    return width;
}

void DrawNumber(float x, float y, int number, float scale, float r, float g, float b)
{
    DrawText(x, y, std::to_string(number), scale, r, g, b);
}

// ==================================
// 3D Text Rendering Implementation
// ==================================

void DrawBillboardText3D(const glm::vec3 &pos,
                         const std::string &text,
                         const glm::vec3 &cameraPos,
                         float targetPixelSize)
{
    // Calculate billboard basis vectors - text should face the camera
    glm::vec3 toCamera = glm::normalize(cameraPos - pos);

    // Handle the degenerate case when camera is directly above/below
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(toCamera, worldUp)) > 0.99f)
    {
        worldUp = glm::vec3(0.0f, 0.0f, 1.0f); // Use Z as up if looking straight down
    }

    // Right vector (in screen space) - perpendicular to camera look direction
    glm::vec3 right = glm::normalize(glm::cross(worldUp, toCamera));
    // Up vector (in screen space) - perpendicular to both
    glm::vec3 up = glm::normalize(glm::cross(toCamera, right));

    // Scale character height to achieve target pixel size on screen
    // Formula: charHeight = dist * targetPixels * (FOV_radians / screenHeight)
    // Assuming ~60° FOV (1.047 rad) and ~1080p screen: factor ≈ 0.00097 per pixel
    // For 12px: 0.00097 * 12 ≈ 0.0116
    float dist = glm::length(cameraPos - pos);
    float charHeight = dist * targetPixelSize * 0.001f; // ~12px default = 0.012

    // Calculate total text width for centering
    float totalWidth = 0.0f;
    for (char c : text)
    {
        auto widthIt = CHAR_WIDTHS.find(c);
        float charWidth = (widthIt != CHAR_WIDTHS.end()) ? widthIt->second * charHeight : 0.5f * charHeight;
        totalWidth += charWidth + charHeight * 0.15f; // Add spacing
    }

    // Start position (centered horizontally)
    float currentX = -totalWidth * 0.5f;

    glLineWidth(1.5f);
    glBegin(GL_LINES);

    for (char c : text)
    {
        auto segIt = CHAR_SEGMENTS.find(c);
        auto widthIt = CHAR_WIDTHS.find(c);

        float charWidth = (widthIt != CHAR_WIDTHS.end()) ? widthIt->second * charHeight : 0.5f * charHeight;

        if (segIt != CHAR_SEGMENTS.end())
        {
            // Draw each segment of the character
            for (const auto &seg : segIt->second)
            {
                // Transform 2D segment to 3D using billboard basis
                // Character segments use Y=0 at top, Y=1 at bottom (screen coords)
                // We need to flip Y so text appears right-side-up: use (1-y)
                float y1_flipped = 1.0f - seg.y1;
                float y2_flipped = 1.0f - seg.y2;

                glm::vec3 p1 = pos + right * (currentX + seg.x1 * charWidth) + up * (y1_flipped * charHeight);
                glm::vec3 p2 = pos + right * (currentX + seg.x2 * charWidth) + up * (y2_flipped * charHeight);

                glVertex3f(p1.x, p1.y, p1.z);
                glVertex3f(p2.x, p2.y, p2.z);
            }
        }

        currentX += charWidth + charHeight * 0.15f; // Advance cursor
    }

    glEnd();
}
