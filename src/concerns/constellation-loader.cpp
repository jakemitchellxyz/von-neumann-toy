#include "constellation-loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <regex>
#include <algorithm>

namespace fs = std::filesystem;

// ==================================
// Simple JSON5 Value Extraction
// ==================================
// These helpers extract values from JSON5 without a full parser

// Extract a string value: "key": "value"
std::string extractStringValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "";
    
    // Find the colon after the key
    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) return "";
    
    // Find the opening quote of the value
    size_t startQuote = json.find('"', colonPos + 1);
    if (startQuote == std::string::npos) return "";
    
    // Find the closing quote
    size_t endQuote = json.find('"', startQuote + 1);
    if (endQuote == std::string::npos) return "";
    
    return json.substr(startQuote + 1, endQuote - startQuote - 1);
}

// Parse RA string like "5h" or "23h 25m" to hours
float parseRA(const std::string& raStr) {
    float hours = 0.0f;
    float minutes = 0.0f;
    
    // Try to find hours
    size_t hPos = raStr.find('h');
    if (hPos != std::string::npos) {
        std::string hoursPart = raStr.substr(0, hPos);
        // Remove non-numeric prefix
        size_t numStart = hoursPart.find_first_of("0123456789");
        if (numStart != std::string::npos) {
            hours = std::stof(hoursPart.substr(numStart));
        }
    }
    
    // Try to find minutes
    size_t mPos = raStr.find('m');
    if (mPos != std::string::npos) {
        size_t mStart = raStr.find_last_of("0123456789", mPos);
        if (mStart != std::string::npos) {
            // Find start of minutes number
            size_t numStart = mStart;
            while (numStart > 0 && (isdigit(raStr[numStart-1]) || raStr[numStart-1] == '.')) {
                numStart--;
            }
            minutes = std::stof(raStr.substr(numStart, mPos - numStart));
        }
    }
    
    return hours + minutes / 60.0f;
}

// Parse Dec string like "+5°" or "-26.43°" to degrees
float parseDec(const std::string& decStr) {
    float degrees = 0.0f;
    bool negative = decStr.find('-') != std::string::npos;
    
    // Find the degree symbol or 'd'
    size_t degPos = decStr.find("°");
    if (degPos == std::string::npos) {
        degPos = decStr.find("deg");
    }
    
    // Extract number before degree symbol
    std::string numStr;
    for (char c : decStr) {
        if (isdigit(c) || c == '.' || c == '-' || c == '+') {
            numStr += c;
        }
    }
    
    if (!numStr.empty()) {
        try {
            degrees = std::stof(numStr);
        } catch (...) {
            degrees = 0.0f;
        }
    }
    
    return degrees;
}

// Extract graph star positions from the graph section
std::vector<GraphStar> parseGraph(const std::string& json) {
    std::vector<GraphStar> stars;
    
    // Find the graph section
    size_t graphStart = json.find("\"graph\"");
    if (graphStart == std::string::npos) return stars;
    
    size_t braceStart = json.find('{', graphStart);
    if (braceStart == std::string::npos) return stars;
    
    // Find matching closing brace
    int braceCount = 1;
    size_t pos = braceStart + 1;
    size_t graphEnd = json.length();
    
    while (pos < json.length() && braceCount > 0) {
        if (json[pos] == '{') braceCount++;
        else if (json[pos] == '}') braceCount--;
        if (braceCount == 0) {
            graphEnd = pos;
        }
        pos++;
    }
    
    std::string graphSection = json.substr(braceStart, graphEnd - braceStart + 1);
    
    // Parse each line (line1 through line10)
    for (int lineNum = 1; lineNum <= 10; lineNum++) {
        std::string lineKey = "\"line" + std::to_string(lineNum) + "\"";
        size_t linePos = graphSection.find(lineKey);
        if (linePos == std::string::npos) continue;
        
        // Find the object for this line
        size_t lineObjStart = graphSection.find('{', linePos);
        size_t lineObjEnd = graphSection.find('}', lineObjStart);
        if (lineObjStart == std::string::npos || lineObjEnd == std::string::npos) continue;
        
        std::string lineObj = graphSection.substr(lineObjStart, lineObjEnd - lineObjStart + 1);
        
        // Find all column positions (numbers in quotes)
        std::regex colRegex("\"(\\d+)\"\\s*:\\s*\"");
        std::smatch match;
        std::string::const_iterator searchStart = lineObj.cbegin();
        
        while (std::regex_search(searchStart, lineObj.cend(), match, colRegex)) {
            int col = std::stoi(match[1].str());
            stars.push_back({lineNum - 1, col});  // 0-indexed row
            searchStart = match.suffix().first;
        }
    }
    
    return stars;
}

// ==================================
// File Loading Functions
// ==================================

bool loadConstellationFile(const std::string& filePath, LoadedConstellation& outConstellation) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open constellation file: " << filePath << std::endl;
        return false;
    }
    
    // Read entire file
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    
    // Extract values
    outConstellation.name = extractStringValue(json, "name");
    outConstellation.title = extractStringValue(json, "title");
    outConstellation.quadrant = extractStringValue(json, "quadrant");
    
    std::string raStr = extractStringValue(json, "right ascension");
    std::string decStr = extractStringValue(json, "declination");
    std::string mainStarsStr = extractStringValue(json, "main stars");
    
    outConstellation.centerRA = parseRA(raStr);
    outConstellation.centerDec = parseDec(decStr);
    
    // Parse main stars count
    try {
        outConstellation.mainStars = std::stoi(mainStarsStr);
    } catch (...) {
        outConstellation.mainStars = 0;
    }
    
    // Parse graph
    outConstellation.graphStars = parseGraph(json);
    
    return !outConstellation.name.empty();
}

std::vector<LoadedConstellation> loadConstellationsFromDirectory(const std::string& directoryPath) {
    std::vector<LoadedConstellation> constellations;
    
    try {
        if (!fs::exists(directoryPath)) {
            std::cerr << "Constellation directory not found: " << directoryPath << std::endl;
            return constellations;
        }
        
        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".json5" || ext == ".json") {
                    LoadedConstellation constellation;
                    if (loadConstellationFile(entry.path().string(), constellation)) {
                        constellations.push_back(std::move(constellation));
                    }
                }
            }
        }
        
        std::cout << "Loaded " << constellations.size() << " constellations from " << directoryPath << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading constellations: " << e.what() << std::endl;
    }
    
    return constellations;
}

// ==================================
// Coordinate Conversion
// ==================================

void graphToRaDec(const LoadedConstellation& constellation, int graphRow, int graphCol,
                  float& outRA, float& outDec) {
    // Graph is typically 10 rows x 20 columns
    // Map to a region around the constellation center
    // Each row/col represents roughly 1-2 degrees
    
    const float graphWidth = 20.0f;   // columns
    const float graphHeight = 10.0f;  // rows
    const float raSpan = 2.0f;        // hours of RA covered by graph
    const float decSpan = 20.0f;      // degrees of Dec covered by graph
    
    // Normalize to -0.5 to 0.5
    float normalizedCol = (graphCol / graphWidth) - 0.5f;
    float normalizedRow = (graphRow / graphHeight) - 0.5f;
    
    // Convert to RA/Dec offset from center
    outRA = constellation.centerRA + normalizedCol * raSpan;
    outDec = constellation.centerDec - normalizedRow * decSpan;  // Row increases downward
    
    // Wrap RA to 0-24 range
    if (outRA < 0) outRA += 24.0f;
    if (outRA >= 24.0f) outRA -= 24.0f;
    
    // Clamp Dec to -90 to +90
    if (outDec < -90.0f) outDec = -90.0f;
    if (outDec > 90.0f) outDec = 90.0f;
}

// ==================================
// Path Helper
// ==================================

std::string getDefaultsPath() {
    // Try to find defaults directory relative to executable
    // This assumes the executable is in a build/Release or build/Debug folder
    
    std::vector<std::string> possiblePaths = {
        "defaults",
        "../defaults",
        "../../defaults",
        "./defaults/constellations/.."
    };
    
    for (const auto& path : possiblePaths) {
        if (fs::exists(path + "/constellations")) {
            return path;
        }
    }
    
    // Fallback to current directory
    return "defaults";
}
