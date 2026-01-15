// ============================================================================
// Settings Implementation
// ============================================================================

#include "settings.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <regex>

// Static member initialization
TextureResolution Settings::s_textureResolution = TextureResolution::Medium;
TextureResolution Settings::s_runningResolution = TextureResolution::Medium;
bool Settings::s_hasUnsavedChanges = false;
bool Settings::s_loaded = false;

// ==================================
// Resolution Helpers
// ==================================

const char* getResolutionName(TextureResolution res) {
    switch (res) {
        case TextureResolution::Low:    return "Low";
        case TextureResolution::Medium: return "Medium";
        case TextureResolution::High:   return "High";
        case TextureResolution::Ultra:  return "Ultra";
        default: return "Medium";
    }
}

TextureResolution getResolutionFromName(const std::string& name) {
    if (name == "Low" || name == "low") return TextureResolution::Low;
    if (name == "High" || name == "high") return TextureResolution::High;
    if (name == "Ultra" || name == "ultra") return TextureResolution::Ultra;
    return TextureResolution::Medium;  // Default
}

void getResolutionDimensions(TextureResolution res, int& width, int& height) {
    switch (res) {
        case TextureResolution::Low:
            width = 1024;
            height = 512;
            break;
        case TextureResolution::Medium:
            width = 4096;
            height = 2048;
            break;
        case TextureResolution::High:
            width = 8192;
            height = 4096;
            break;
        case TextureResolution::Ultra:
            // Maximum practical resolution: 16K
            width = 16384;
            height = 8192;
            break;
        default:
            width = 4096;
            height = 2048;
            break;
    }
}

const char* getResolutionFolderName(TextureResolution res) {
    switch (res) {
        case TextureResolution::Low:    return "low";
        case TextureResolution::Medium: return "medium";
        case TextureResolution::High:   return "high";
        case TextureResolution::Ultra:  return "ultra";
        default: return "medium";
    }
}

// ==================================
// Settings Load/Save
// ==================================

bool Settings::load(const std::string& filepath) {
    s_loaded = true;
    
    if (!std::filesystem::exists(filepath)) {
        // Create default settings file
        std::cout << "Creating default settings file: " << filepath << std::endl;
        s_textureResolution = TextureResolution::Medium;
        s_runningResolution = TextureResolution::Medium;
        save(filepath);
        return true;
    }
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open settings file: " << filepath << std::endl;
        return false;
    }
    
    // Read entire file
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();
    
    // Parse textureResolution setting
    // Look for: "textureResolution": "Medium"
    std::regex resRegex("\"textureResolution\"\\s*:\\s*\"(\\w+)\"");
    std::smatch match;
    
    if (std::regex_search(content, match, resRegex)) {
        std::string resName = match[1].str();
        s_textureResolution = getResolutionFromName(resName);
        std::cout << "Loaded texture resolution setting: " << getResolutionName(s_textureResolution) << std::endl;
    } else {
        s_textureResolution = TextureResolution::Medium;
    }
    
    s_runningResolution = s_textureResolution;
    s_hasUnsavedChanges = false;
    
    return true;
}

bool Settings::save(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not write settings file: " << filepath << std::endl;
        return false;
    }
    
    // Write JSON5 format
    file << "// Von Neumann Toy Settings\n";
    file << "// This file is auto-generated. Edit with care.\n";
    file << "{\n";
    file << "    // Texture resolution for Earth surface\n";
    file << "    // Options: \"Low\" (1024x512), \"Medium\" (4096x2048), \"High\" (8192x4096), \"Ultra\" (16384x8192)\n";
    file << "    \"textureResolution\": \"" << getResolutionName(s_textureResolution) << "\"\n";
    file << "}\n";
    
    file.close();
    s_hasUnsavedChanges = false;
    
    std::cout << "Saved settings to: " << filepath << std::endl;
    return true;
}

// ==================================
// Getters/Setters
// ==================================

TextureResolution Settings::getTextureResolution() {
    return s_textureResolution;
}

void Settings::setTextureResolution(TextureResolution res) {
    if (s_textureResolution != res) {
        s_textureResolution = res;
        s_hasUnsavedChanges = true;
        save();  // Auto-save on change
    }
}

bool Settings::hasUnsavedChanges() {
    return s_hasUnsavedChanges;
}

bool Settings::needsRestart() {
    return s_textureResolution != s_runningResolution;
}

void Settings::markAsRunning() {
    s_runningResolution = s_textureResolution;
}
