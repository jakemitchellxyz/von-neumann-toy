// ============================================================================
// FXAA Implementation
// ============================================================================

#include "fxaa.h"
#include "../materials/helpers/gl.h"
#include "../materials/helpers/shader-loader.h"
#include "settings.h"
#include <iostream>
#include <vector>

// FXAA state
static bool g_fxaaInitialized = false;
static bool g_fxaaEnabled = true;
static GLuint g_framebuffer = 0;
static GLuint g_colorTexture = 0;
static GLuint g_depthRenderbuffer = 0;
static GLuint g_shaderProgram = 0;
static GLint g_uniformSourceTexture = -1;
static GLint g_uniformInvScreenSize = -1;
static int g_framebufferWidth = 0;
static int g_framebufferHeight = 0;

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

// Compile and link FXAA shader
static bool compileFXAAShader()
{
    if (glCreateShader == nullptr)
    {
        std::cerr << "FXAA: OpenGL shader extensions not loaded" << std::endl;
        return false;
    }

    // Load vertex shader
    std::string vertexPath = "src/concerns/shaders/fxaa-vertex.glsl";
    std::vector<std::string> vertexPaths = {
        "shaders/fxaa-vertex.glsl",
        "src/concerns/shaders/fxaa-vertex.glsl",
        "../src/concerns/shaders/fxaa-vertex.glsl",
        "../../src/concerns/shaders/fxaa-vertex.glsl"
    };
    
    std::string vertexSource;
    for (const auto& path : vertexPaths)
    {
        vertexSource = loadShaderFile(path);
        if (!vertexSource.empty())
            break;
    }
    
    if (vertexSource.empty())
    {
        std::cerr << "FXAA: Failed to load vertex shader" << std::endl;
        return false;
    }

    // Load fragment shader
    std::vector<std::string> fragmentPaths = {
        "shaders/fxaa-fragment.glsl",
        "src/concerns/shaders/fxaa-fragment.glsl",
        "../src/concerns/shaders/fxaa-fragment.glsl",
        "../../src/concerns/shaders/fxaa-fragment.glsl"
    };
    
    std::string fragmentSource;
    for (const auto& path : fragmentPaths)
    {
        fragmentSource = loadShaderFile(path);
        if (!fragmentSource.empty())
            break;
    }
    
    if (fragmentSource.empty())
    {
        std::cerr << "FXAA: Failed to load fragment shader" << std::endl;
        return false;
    }

    // Compile vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    if (vertexShader == 0)
    {
        std::cerr << "FXAA: Failed to create vertex shader" << std::endl;
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
        std::cerr << "FXAA: Vertex shader compilation failed:\n" << log.data() << std::endl;
        glDeleteShader(vertexShader);
        return false;
    }

    // Compile fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    if (fragmentShader == 0)
    {
        std::cerr << "FXAA: Failed to create fragment shader" << std::endl;
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
        std::cerr << "FXAA: Fragment shader compilation failed:\n" << log.data() << std::endl;
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    // Link program
    g_shaderProgram = glCreateProgram();
    if (g_shaderProgram == 0)
    {
        std::cerr << "FXAA: Failed to create shader program" << std::endl;
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
        std::cerr << "FXAA: Shader program linking failed:\n" << log.data() << std::endl;
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
    g_uniformSourceTexture = glGetUniformLocation(g_shaderProgram, "uSourceTexture");
    g_uniformInvScreenSize = glGetUniformLocation(g_shaderProgram, "uInvScreenSize");

    return true;
}

// Create framebuffer and texture
static bool createFramebuffer(int width, int height)
{
    if (glGenFramebuffers == nullptr)
    {
        std::cerr << "FXAA: Framebuffer extensions not available" << std::endl;
        return false;
    }

    // Delete existing framebuffer if it exists
    if (g_framebuffer != 0)
    {
        if (glDeleteFramebuffers)
        {
            glDeleteFramebuffers(1, &g_framebuffer);
        }
        g_framebuffer = 0;
    }

    if (g_colorTexture != 0)
    {
        glDeleteTextures(1, &g_colorTexture);
        g_colorTexture = 0;
    }

    if (g_depthRenderbuffer != 0 && glDeleteRenderbuffers)
    {
        glDeleteRenderbuffers(1, &g_depthRenderbuffer);
        g_depthRenderbuffer = 0;
    }

    // Create framebuffer
    glGenFramebuffers(1, &g_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);

    // Create color texture
    glGenTextures(1, &g_colorTexture);
    glBindTexture(GL_TEXTURE_2D, g_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Attach color texture to framebuffer
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_colorTexture, 0);

    // Create depth renderbuffer
    glGenRenderbuffers(1, &g_depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, g_depthRenderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_depthRenderbuffer);

    // Check framebuffer completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cerr << "FXAA: Framebuffer incomplete: " << status << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // Unbind framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    g_framebufferWidth = width;
    g_framebufferHeight = height;

    return true;
}

bool InitFXAA()
{
    if (g_fxaaInitialized)
    {
        return true;
    }

    // Load GL extensions
    if (!loadGLExtensions())
    {
        std::cerr << "FXAA: Failed to load OpenGL extensions" << std::endl;
        return false;
    }

    // Compile shader
    if (!compileFXAAShader())
    {
        std::cerr << "FXAA: Failed to compile shader" << std::endl;
        return false;
    }

    // Load FXAA enabled state from settings
    g_fxaaEnabled = Settings::getFXAAEnabled();

    g_fxaaInitialized = true;
    std::cout << "FXAA initialized successfully" << std::endl;
    return true;
}

void CleanupFXAA()
{
    if (!g_fxaaInitialized)
    {
        return;
    }

    if (g_shaderProgram != 0 && glDeleteProgram)
    {
        glDeleteProgram(g_shaderProgram);
        g_shaderProgram = 0;
    }

    if (g_colorTexture != 0)
    {
        glDeleteTextures(1, &g_colorTexture);
        g_colorTexture = 0;
    }

    if (g_depthRenderbuffer != 0 && glDeleteRenderbuffers)
    {
        glDeleteRenderbuffers(1, &g_depthRenderbuffer);
        g_depthRenderbuffer = 0;
    }

    if (g_framebuffer != 0 && glDeleteFramebuffers)
    {
        glDeleteFramebuffers(1, &g_framebuffer);
        g_framebuffer = 0;
    }

    g_fxaaInitialized = false;
}

void ResizeFXAA(int width, int height)
{
    if (!g_fxaaInitialized || width <= 0 || height <= 0)
    {
        return;
    }

    if (width == g_framebufferWidth && height == g_framebufferHeight)
    {
        return; // No resize needed
    }

    createFramebuffer(width, height);
}

bool BeginFXAA()
{
    if (!g_fxaaInitialized || !g_fxaaEnabled)
    {
        return false;
    }

    if (g_framebuffer == 0)
    {
        return false;
    }

    // Bind framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, g_framebuffer);
    
    // Set viewport to framebuffer size
    glViewport(0, 0, g_framebufferWidth, g_framebufferHeight);

    return true;
}

void EndFXAA()
{
    if (!g_fxaaInitialized || !g_fxaaEnabled)
    {
        return;
    }

    if (g_framebuffer == 0 || g_shaderProgram == 0)
    {
        return;
    }

    // Unbind framebuffer (render to screen)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Get current viewport size (screen size)
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    int screenWidth = viewport[2];
    int screenHeight = viewport[3];

    // Set viewport to full screen
    glViewport(0, 0, screenWidth, screenHeight);

    // Use FXAA shader
    glUseProgram(g_shaderProgram);

    // Set uniforms
    if (glActiveTexture_ptr)
    {
        glActiveTexture_ptr(GL_TEXTURE0);
    }
    glBindTexture(GL_TEXTURE_2D, g_colorTexture);
    if (g_uniformSourceTexture >= 0)
    {
        glUniform1i(g_uniformSourceTexture, 0);
    }
    if (g_uniformInvScreenSize >= 0)
    {
        glUniform2f(g_uniformInvScreenSize, 1.0f / static_cast<float>(screenWidth), 1.0f / static_cast<float>(screenHeight));
    }

    // Disable depth test for fullscreen quad
    glDisable(GL_DEPTH_TEST);

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
    
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, g_fullscreenQuadIndices);
    
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    // Re-enable depth test
    glEnable(GL_DEPTH_TEST);

    // Unbind shader
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool IsFXAAEnabled()
{
    return g_fxaaEnabled;
}

void SetFXAAEnabled(bool enabled)
{
    g_fxaaEnabled = enabled;
    Settings::setFXAAEnabled(enabled);
}
