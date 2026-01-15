#pragma once

// ============================================================================
// Procedural Noise Texture Generation for City Light Flickering
// ============================================================================
// Generates tileable Perlin noise textures in sinusoidal projection
// These textures are sampled with time-offset UVs for animation

// Fractal Brownian Motion (FBM) Perlin noise
float perlinFBM(float xCoord, float yCoord, int octaves, float persistence);
