#pragma once

// ==================================
// FXAA (Fast Approximate Anti-Aliasing)
// ==================================
// Post-processing antialiasing effect applied to the final rendered scene

// Initialize FXAA system (call once at startup, after OpenGL context creation)
// Returns true if initialization succeeded
bool InitFXAA();

// Cleanup FXAA resources (call on shutdown)
void CleanupFXAA();

// Resize FXAA framebuffer (call when window is resized)
// width, height: new framebuffer dimensions
void ResizeFXAA(int width, int height);

// Begin rendering to FXAA framebuffer (call before rendering scene)
// Returns true if FXAA is enabled and framebuffer is ready
bool BeginFXAA();

// End FXAA rendering and apply FXAA post-processing (call after rendering scene)
// This will render the FXAA-processed result to the screen
void EndFXAA();

// Check if FXAA is currently enabled
bool IsFXAAEnabled();

// Enable or disable FXAA (updates immediately)
void SetFXAAEnabled(bool enabled);
