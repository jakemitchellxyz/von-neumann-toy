#pragma once

#include <string>

// ==================================
// Application Settings
// ==================================
// Persisted to settings.json5 in the application directory

// Texture resolution presets
enum class TextureResolution {
    Low,      // 1024x512
    Medium,   // 4096x2048 (default)
    High,     // 8192x4096
    Ultra     // 16384x8192 (16K), lossless PNG
};

// Get string name for resolution
const char* getResolutionName(TextureResolution res);

// Get resolution from string name
TextureResolution getResolutionFromName(const std::string& name);

// Get output dimensions for a resolution preset
void getResolutionDimensions(TextureResolution res, int& width, int& height);

// Get folder name for resolution preset
const char* getResolutionFolderName(TextureResolution res);

// ==================================
// Settings Manager
// ==================================

class Settings {
public:
    // Load settings from file (creates default if not exists)
    static bool load(const std::string& filepath = "settings.json5");
    
    // Save current settings to file
    static bool save(const std::string& filepath = "settings.json5");
    
    // Get/Set texture resolution
    static TextureResolution getTextureResolution();
    static void setTextureResolution(TextureResolution res);
    
    // Check if settings have changed since last save
    static bool hasUnsavedChanges();
    
    // Check if restart is needed (resolution changed from running value)
    static bool needsRestart();
    
    // Mark current resolution as the "running" resolution
    static void markAsRunning();
    
private:
    static TextureResolution s_textureResolution;
    static TextureResolution s_runningResolution;
    static bool s_hasUnsavedChanges;
    static bool s_loaded;
};
