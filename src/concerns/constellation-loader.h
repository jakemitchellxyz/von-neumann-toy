#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

// ==================================
// Constellation Data Loaded from JSON5 Files
// ==================================

// A star position parsed from the JSON5 graph
struct GraphStar {
    int row;      // Line number (0-9 typically)
    int col;      // Column position
};

// A loaded constellation with its metadata and star positions
struct LoadedConstellation {
    std::string name;
    std::string title;
    std::string quadrant;
    float centerRA;        // Right ascension in hours
    float centerDec;       // Declination in degrees
    int mainStars;
    std::vector<GraphStar> graphStars;  // Star positions from graph
};

// ==================================
// Constellation Loader Functions
// ==================================

// Load all constellation files from a directory
// Returns vector of loaded constellations
std::vector<LoadedConstellation> loadConstellationsFromDirectory(const std::string& directoryPath);

// Load a single constellation from a JSON5 file
// Returns true if successful
bool loadConstellationFile(const std::string& filePath, LoadedConstellation& outConstellation);

// Convert graph coordinates to RA/Dec based on constellation center
// graphRow: 0-9 (top to bottom)
// graphCol: 0-20 (left to right)
// Returns position in RA (hours) and Dec (degrees)
void graphToRaDec(const LoadedConstellation& constellation, int graphRow, int graphCol,
                  float& outRA, float& outDec);

// Get the defaults directory path (relative to executable)
std::string getDefaultsPath();
