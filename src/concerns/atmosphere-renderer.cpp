// ============================================================================
// Atmosphere Renderer Implementation
// ============================================================================

#include "atmosphere-renderer.h"
#include "../materials/helpers/gl.h"
#include "../materials/helpers/shader-loader.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <stb_image.h>

// Atmosphere renderer state
static bool g_atmosphereInitialized = false;
static GLuint g_shaderProgram = 0;
static GLuint g_transmittanceLUT = 0;
static GLuint g_scatteringLUT = 0;
static bool g_lutsLoaded = false;

// Uniform locations
static GLint g_uniformCameraPos = -1;
static GLint g_uniformCameraDir = -1;
static GLint g_uniformCameraRight = -1;
static GLint g_uniformCameraUp = -1;
static GLint g_uniformCameraFOV = -1;
static GLint g_uniformAspectRatio = -1;
static GLint g_uniformNearPlane = -1;
static GLint g_uniformPlanetCenter = -1;
static GLint g_uniformPlanetRadius = -1;
static GLint g_uniformAtmosphereRadius = -1;
static GLint g_uniformSunDir = -1;
static GLint g_uniformSunColor = -1;
static GLint g_uniformTransmittanceLUT = -1;
static GLint g_uniformScatteringLUT = -1;
static GLint g_uniformDebugMode = -1;

// Fullscreen quad vertices (NDC space: -1 to 1)
static const float g_fullscreenQuad[] = {
    // x, y, u, v
    -1.0f, -1.0f, 0.0f, 0.0f, // Bottom-left
     1.0f, -1.0f, 1.0f, 0.0f, // Bottom-right
     1.0f,  1.0f, 1.0f, 1.0f, // Top-right
    -1.0f,  1.0f, 0.0f, 1.0f  // Top-left
};

static const unsigned int g_fullscreenQuadIndices[] = {
    0, 1, 2,
    0, 2, 3
};

// Compile and link atmosphere shader
static bool compileAtmosphereShader()
{
    if (glCreateShader == nullptr)
    {
        std::cerr << "Atmosphere: OpenGL shader extensions not loaded" << std::endl;
        return false;
    }

    // Load vertex shader
    std::vector<std::string> vertexPaths = {
        "shaders/atmosphere-vertex.glsl",
        "src/concerns/shaders/atmosphere-vertex.glsl",
        "../src/concerns/shaders/atmosphere-vertex.glsl",
        "../../src/concerns/shaders/atmosphere-vertex.glsl"
    };
    
    std::string vertexSource;
    std::string vertexPath;
    for (const auto& path : vertexPaths)
    {
        if (std::filesystem::exists(path))
        {
            vertexPath = path;
            vertexSource = loadShaderFile(path);
            if (!vertexSource.empty())
                break;
        }
    }
    
    if (vertexSource.empty())
    {
        std::cerr << "Atmosphere: Failed to load vertex shader (tried: ";
        for (size_t i = 0; i < vertexPaths.size(); ++i)
        {
            std::cerr << vertexPaths[i];
            if (i < vertexPaths.size() - 1) std::cerr << ", ";
        }
        std::cerr << ")" << std::endl;
        return false;
    }

    // Load fragment shader
    std::vector<std::string> fragmentPaths = {
        "shaders/atmosphere-fragment.glsl",
        "src/concerns/shaders/atmosphere-fragment.glsl",
        "../src/concerns/shaders/atmosphere-fragment.glsl",
        "../../src/concerns/shaders/atmosphere-fragment.glsl"
    };
    
    std::string fragmentSource;
    std::string fragmentPath;
    for (const auto& path : fragmentPaths)
    {
        if (std::filesystem::exists(path))
        {
            fragmentPath = path;
            fragmentSource = loadShaderFile(path);
            if (!fragmentSource.empty())
                break;
        }
    }
    
    if (fragmentSource.empty())
    {
        std::cerr << "Atmosphere: Failed to load fragment shader (tried: ";
        for (size_t i = 0; i < fragmentPaths.size(); ++i)
        {
            std::cerr << fragmentPaths[i];
            if (i < fragmentPaths.size() - 1) std::cerr << ", ";
        }
        std::cerr << ")" << std::endl;
        return false;
    }

    // Compile vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    if (vertexShader == 0)
    {
        std::cerr << "Atmosphere: Failed to create vertex shader" << std::endl;
        return false;
    }

    const char* vertexSourcePtr = vertexSource.c_str();
    glShaderSource(vertexShader, 1, &vertexSourcePtr, nullptr);
    glCompileShader(vertexShader);

    GLint success = 0;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (success == 0)
    {
        GLint logLength = 0;
        glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(logLength);
        glGetShaderInfoLog(vertexShader, logLength, nullptr, log.data());
        std::cerr << "Atmosphere: Vertex shader compilation failed:\n" << log.data() << std::endl;
        glDeleteShader(vertexShader);
        return false;
    }

    // Compile fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    if (fragmentShader == 0)
    {
        std::cerr << "Atmosphere: Failed to create fragment shader" << std::endl;
        glDeleteShader(vertexShader);
        return false;
    }

    const char* fragmentSourcePtr = fragmentSource.c_str();
    glShaderSource(fragmentShader, 1, &fragmentSourcePtr, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (success == 0)
    {
        GLint logLength = 0;
        glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(logLength);
        glGetShaderInfoLog(fragmentShader, logLength, nullptr, log.data());
        std::cerr << "Atmosphere: Fragment shader compilation failed:\n" << log.data() << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    // Link program
    g_shaderProgram = glCreateProgram();
    if (g_shaderProgram == 0)
    {
        std::cerr << "Atmosphere: Failed to create shader program" << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    glAttachShader(g_shaderProgram, vertexShader);
    glAttachShader(g_shaderProgram, fragmentShader);
    glLinkProgram(g_shaderProgram);

    glGetProgramiv(g_shaderProgram, GL_LINK_STATUS, &success);
    if (success == 0)
    {
        GLint logLength = 0;
        glGetProgramiv(g_shaderProgram, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(logLength);
        glGetProgramInfoLog(g_shaderProgram, logLength, nullptr, log.data());
        std::cerr << "Atmosphere: Shader program linking failed:\n" << log.data() << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(g_shaderProgram);
        g_shaderProgram = 0;
        return false;
    }

    // Clean up shaders (no longer needed after linking)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Get uniform locations
    g_uniformCameraPos = glGetUniformLocation(g_shaderProgram, "uCameraPos");
    g_uniformCameraDir = glGetUniformLocation(g_shaderProgram, "uCameraDir");
    g_uniformCameraRight = glGetUniformLocation(g_shaderProgram, "uCameraRight");
    g_uniformCameraUp = glGetUniformLocation(g_shaderProgram, "uCameraUp");
    g_uniformCameraFOV = glGetUniformLocation(g_shaderProgram, "uCameraFOV");
    g_uniformAspectRatio = glGetUniformLocation(g_shaderProgram, "uAspectRatio");
    g_uniformNearPlane = glGetUniformLocation(g_shaderProgram, "uNearPlane");
    g_uniformPlanetCenter = glGetUniformLocation(g_shaderProgram, "uPlanetCenter");
    g_uniformPlanetRadius = glGetUniformLocation(g_shaderProgram, "uPlanetRadius");
    g_uniformAtmosphereRadius = glGetUniformLocation(g_shaderProgram, "uAtmosphereRadius");
    g_uniformSunDir = glGetUniformLocation(g_shaderProgram, "uSunDir");
    g_uniformSunColor = glGetUniformLocation(g_shaderProgram, "uSunColor");
    g_uniformTransmittanceLUT = glGetUniformLocation(g_shaderProgram, "uTransmittanceLUT");
    g_uniformScatteringLUT = glGetUniformLocation(g_shaderProgram, "uScatteringLUT");
    g_uniformDebugMode = glGetUniformLocation(g_shaderProgram, "uDebugMode");
    
    if (g_uniformDebugMode < 0)
    {
        std::cerr << "Atmosphere: WARNING - uDebugMode uniform not found in shader!" << std::endl;
    }
    else
    {
        std::cout << "Atmosphere: uDebugMode uniform found at location " << g_uniformDebugMode << std::endl;
    }
    
    // Test setting the uniform immediately to verify it works
    if (g_uniformDebugMode >= 0)
    {
        glUseProgram(g_shaderProgram);
        glUniform1f(g_uniformDebugMode, 1.0f); // Set to debug mode 1 for testing
        glUseProgram(0);
        std::cout << "Atmosphere: Test-set uDebugMode to 1.0" << std::endl;
    }

    return true;
}

// Load HDR texture
static GLuint loadHDRTexture(const std::string &filepath)
{
    if (!std::filesystem::exists(filepath))
    {
        return 0;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    float *hdrData = stbi_loadf(filepath.c_str(), &width, &height, &channels, 3);

    if (hdrData == nullptr || width <= 0 || height <= 0 || channels < 3)
    {
        if (hdrData != nullptr)
        {
            stbi_image_free(hdrData);
        }
        return 0;
    }

    GLuint textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0)
    {
        stbi_image_free(hdrData);
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, hdrData);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(hdrData);
    return textureId;
}

bool InitAtmosphereRenderer()
{
    if (g_atmosphereInitialized)
    {
        return true;
    }

    if (!loadGLExtensions())
    {
        std::cerr << "Atmosphere: OpenGL extensions not available" << std::endl;
        return false;
    }

    if (!compileAtmosphereShader())
    {
        std::cerr << "Atmosphere: Failed to compile shader" << std::endl;
        return false;
    }

    g_atmosphereInitialized = true;
    return true;
}

void CleanupAtmosphereRenderer()
{
    if (g_transmittanceLUT != 0)
    {
        glDeleteTextures(1, &g_transmittanceLUT);
        g_transmittanceLUT = 0;
    }

    if (g_scatteringLUT != 0)
    {
        glDeleteTextures(1, &g_scatteringLUT);
        g_scatteringLUT = 0;
    }

    if (g_shaderProgram != 0 && glDeleteProgram != nullptr)
    {
        glDeleteProgram(g_shaderProgram);
        g_shaderProgram = 0;
    }

    g_lutsLoaded = false;
    g_atmosphereInitialized = false;
}

bool LoadAtmosphereLUTs(const std::string &transmittancePath, const std::string &scatteringPath)
{
    if (!g_atmosphereInitialized)
    {
        std::cerr << "Atmosphere: Renderer not initialized" << std::endl;
        return false;
    }

    // Load transmittance LUT
    g_transmittanceLUT = loadHDRTexture(transmittancePath);
    if (g_transmittanceLUT == 0)
    {
        std::cerr << "Atmosphere: Failed to load transmittance LUT: " << transmittancePath << std::endl;
        return false;
    }

    // Load scattering LUT
    g_scatteringLUT = loadHDRTexture(scatteringPath);
    if (g_scatteringLUT == 0)
    {
        std::cerr << "Atmosphere: Failed to load scattering LUT: " << scatteringPath << std::endl;
        glDeleteTextures(1, &g_transmittanceLUT);
        g_transmittanceLUT = 0;
        return false;
    }

    g_lutsLoaded = true;
    std::cout << "Atmosphere: LUTs loaded successfully" << std::endl;
    return true;
}

bool IsAtmosphereRendererReady()
{
    return g_atmosphereInitialized && g_lutsLoaded && g_shaderProgram != 0;
}

// Debug flag - 0 = normal, 1 = solid color test, 2 = debug march visualization
static int g_debugMode = 0; // Debug mode: 0 = normal rendering, 1 = solid color, 2 = debug march

void RenderAtmosphere(const glm::vec3 &cameraPos,
                      const glm::vec3 &cameraDir,
                      const glm::vec3 &cameraRight,
                      const glm::vec3 &cameraUp,
                      float fovRadians,
                      float aspectRatio,
                      float nearPlane,
                      const glm::vec3 &planetCenter,
                      float planetRadius,
                      float atmosphereRadius,
                      const glm::vec3 &sunDir,
                      const glm::vec3 &sunColor)
{
    static int renderCount = 0;
    renderCount++;
    if (renderCount % 60 == 0) // Print every 60 frames
    {
        std::cout << "DEBUG: RenderAtmosphere called (frame " << renderCount << ")" << std::endl;
        std::cout << "  Camera pos: " << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << std::endl;
        std::cout << "  Planet center: " << planetCenter.x << ", " << planetCenter.y << ", " << planetCenter.z << std::endl;
        std::cout << "  Planet radius: " << planetRadius << ", Atmosphere radius: " << atmosphereRadius << std::endl;
    }

    if (!IsAtmosphereRendererReady())
    {
        if (renderCount % 60 == 0)
        {
            std::cout << "DEBUG: Atmosphere renderer not ready!" << std::endl;
        }
        return;
    }

    // Save current OpenGL state
    GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLint currentDepthMask;
    glGetIntegerv(GL_DEPTH_WRITEMASK, &currentDepthMask);
    GLint currentBlendSrc, currentBlendDst;
    glGetIntegerv(GL_BLEND_SRC, &currentBlendSrc);
    glGetIntegerv(GL_BLEND_DST, &currentBlendDst);

    // Enable additive blending for emissive atmospheric scattering
    // This makes the scattered light add to the scene (glowing effect)
    if (g_debugMode == 0)
    {
        glEnable(GL_BLEND);
        // Additive blending: RGB values add to the scene, alpha controls intensity
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive blending for emissive effect
    }
    else
    {
        // Disable blending in debug mode so colors are fully visible
        glDisable(GL_BLEND);
    }

    // Disable depth test and depth write for fullscreen overlay
    // This ensures the atmosphere always renders on top
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // Use atmosphere shader
    glUseProgram(g_shaderProgram);

    // Set debug mode uniform (0 = normal, 1 = solid color test, 2 = debug march)
    if (g_uniformDebugMode >= 0)
    {
        glUniform1f(g_uniformDebugMode, static_cast<float>(g_debugMode));
        if (renderCount % 60 == 0)
        {
            std::cout << "DEBUG: Setting uDebugMode to " << g_debugMode << " (as float: " << static_cast<float>(g_debugMode) << ", uniform location: " << g_uniformDebugMode << ")" << std::endl;
        }
    }
    else
    {
        if (renderCount % 60 == 0)
        {
            std::cout << "DEBUG: WARNING - uDebugMode uniform not found (location: " << g_uniformDebugMode << ")" << std::endl;
        }
    }

    // Set uniforms
    if (g_uniformCameraPos >= 0)
        glUniform3f(g_uniformCameraPos, cameraPos.x, cameraPos.y, cameraPos.z);
    if (g_uniformCameraDir >= 0)
        glUniform3f(g_uniformCameraDir, cameraDir.x, cameraDir.y, cameraDir.z);
    if (g_uniformCameraRight >= 0)
        glUniform3f(g_uniformCameraRight, cameraRight.x, cameraRight.y, cameraRight.z);
    if (g_uniformCameraUp >= 0)
        glUniform3f(g_uniformCameraUp, cameraUp.x, cameraUp.y, cameraUp.z);
    if (g_uniformCameraFOV >= 0)
        glUniform1f(g_uniformCameraFOV, fovRadians);
    if (g_uniformAspectRatio >= 0)
        glUniform1f(g_uniformAspectRatio, aspectRatio);
    if (g_uniformNearPlane >= 0)
        glUniform1f(g_uniformNearPlane, nearPlane);
    if (g_uniformPlanetCenter >= 0)
        glUniform3f(g_uniformPlanetCenter, planetCenter.x, planetCenter.y, planetCenter.z);
    if (g_uniformPlanetRadius >= 0)
        glUniform1f(g_uniformPlanetRadius, planetRadius);
    if (g_uniformAtmosphereRadius >= 0)
        glUniform1f(g_uniformAtmosphereRadius, atmosphereRadius);
    if (g_uniformSunDir >= 0)
        glUniform3f(g_uniformSunDir, sunDir.x, sunDir.y, sunDir.z);
    if (g_uniformSunColor >= 0)
        glUniform3f(g_uniformSunColor, sunColor.x, sunColor.y, sunColor.z);

    // Bind LUT textures
    if (glActiveTexture_ptr)
    {
        glActiveTexture_ptr(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_transmittanceLUT);
        if (g_uniformTransmittanceLUT >= 0)
            glUniform1i(g_uniformTransmittanceLUT, 0);

        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_scatteringLUT);
        if (g_uniformScatteringLUT >= 0)
            glUniform1i(g_uniformScatteringLUT, 1);
    }

    // Render fullscreen quad
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    
    glVertexPointer(2, GL_FLOAT, 4 * sizeof(float), g_fullscreenQuad);
    glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(float), g_fullscreenQuad + 2);
    
    GLenum error = glGetError();
    if (error != GL_NO_ERROR && renderCount % 60 == 0)
    {
        std::cout << "DEBUG: OpenGL error before draw: " << error << std::endl;
    }
    
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, g_fullscreenQuadIndices);
    
    error = glGetError();
    if (error != GL_NO_ERROR)
    {
        std::cout << "DEBUG: OpenGL error after draw: " << error << " (frame " << renderCount << ")" << std::endl;
    }
    else if (renderCount % 60 == 0)
    {
        std::cout << "DEBUG: No OpenGL errors, draw completed successfully" << std::endl;
    }
    
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    // Restore state
    glUseProgram(0);
    if (glActiveTexture_ptr)
    {
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture_ptr(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Restore depth test state
    if (depthTestEnabled)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
    glDepthMask(currentDepthMask ? GL_TRUE : GL_FALSE);

    // Restore blend state (only if we were in normal mode)
    if (g_debugMode == 0)
    {
        if (blendEnabled)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
        glBlendFunc(currentBlendSrc, currentBlendDst);
    }
    else
    {
        // Restore original blend state
        if (blendEnabled)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
        glBlendFunc(currentBlendSrc, currentBlendDst);
    }
}
