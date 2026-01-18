#pragma once

#include "settings.h"

// Preprocess all application data (Earth textures, skybox, cities, wind, atmosphere LUTs)
// This function handles all preprocessing that needs to happen before the window is created.
// Returns true if all critical preprocessing succeeded, false otherwise.
bool PreprocessAllData(TextureResolution textureRes);
