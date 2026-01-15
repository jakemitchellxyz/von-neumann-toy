#include "gl.h"
#include "../earth/earth-material.h"
#include <iostream>
#include <vector>

// GL extension function pointers (loaded at runtime)
// Definitions (declarations are in gl.h)
// These cannot be const because they are assigned values at runtime via glfwGetProcAddress
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUNIFORM1IPROC glUniform1i = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLUNIFORM3FPROC glUniform3f = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLACTIVETEXTUREPROC glActiveTexture_ptr = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

namespace
{
// Flag to track if GL extensions are loaded
// Cannot be const because it's modified at runtime when extensions are loaded
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
bool glExtensionsLoaded = false;
} // namespace

// Helper function to safely cast OpenGL function pointers
// OpenGL extension loading requires reinterpret_cast to convert from void* to function pointers
template <typename T>
T glfwGetProcAddressCast(const char *name)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<T>(glfwGetProcAddress(name));
}

// Load GL extension functions
bool loadGLExtensions()
{
    if (glExtensionsLoaded)
    {
        return true;
    }

    glCreateShader = glfwGetProcAddressCast<PFNGLCREATESHADERPROC>("glCreateShader");
    glShaderSource = glfwGetProcAddressCast<PFNGLSHADERSOURCEPROC>("glShaderSource");
    glCompileShader = glfwGetProcAddressCast<PFNGLCOMPILESHADERPROC>("glCompileShader");
    glGetShaderiv = glfwGetProcAddressCast<PFNGLGETSHADERIVPROC>("glGetShaderiv");
    glGetShaderInfoLog = glfwGetProcAddressCast<PFNGLGETSHADERINFOLOGPROC>("glGetShaderInfoLog");
    glCreateProgram = glfwGetProcAddressCast<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
    glAttachShader = glfwGetProcAddressCast<PFNGLATTACHSHADERPROC>("glAttachShader");
    glLinkProgram = glfwGetProcAddressCast<PFNGLLINKPROGRAMPROC>("glLinkProgram");
    glGetProgramiv = glfwGetProcAddressCast<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
    glGetProgramInfoLog = glfwGetProcAddressCast<PFNGLGETPROGRAMINFOLOGPROC>("glGetProgramInfoLog");
    glUseProgram = glfwGetProcAddressCast<PFNGLUSEPROGRAMPROC>("glUseProgram");
    glDeleteShader = glfwGetProcAddressCast<PFNGLDELETESHADERPROC>("glDeleteShader");
    glDeleteProgram = glfwGetProcAddressCast<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
    glGetUniformLocation = glfwGetProcAddressCast<PFNGLGETUNIFORMLOCATIONPROC>("glGetUniformLocation");
    glUniform1i = glfwGetProcAddressCast<PFNGLUNIFORM1IPROC>("glUniform1i");
    glUniform1f = glfwGetProcAddressCast<PFNGLUNIFORM1FPROC>("glUniform1f");
    glUniform3f = glfwGetProcAddressCast<PFNGLUNIFORM3FPROC>("glUniform3f");
    glUniformMatrix4fv = glfwGetProcAddressCast<PFNGLUNIFORMMATRIX4FVPROC>("glUniformMatrix4fv");
    glActiveTexture_ptr = glfwGetProcAddressCast<PFNGLACTIVETEXTUREPROC>("glActiveTexture");

    glExtensionsLoaded = (glCreateShader != nullptr && glShaderSource != nullptr && glCompileShader != nullptr &&
                          glGetShaderiv != nullptr && glCreateProgram != nullptr && glAttachShader != nullptr &&
                          glLinkProgram != nullptr && glUseProgram != nullptr && glGetUniformLocation != nullptr &&
                          glUniform1i != nullptr && glUniform3f != nullptr && glUniformMatrix4fv != nullptr &&
                          glActiveTexture_ptr != nullptr);

    if (!glExtensionsLoaded)
    {
        std::cerr << "Failed to load OpenGL shader extensions" << '\n';
    }

    return glExtensionsLoaded;
}

GLuint EarthMaterial::compileShader(GLenum type, const char *source)
{
    if (glCreateShader == nullptr)
    {
        std::cerr << "Failed to load OpenGL shader extensions" << '\n';
        return 0;
    }

    GLuint shader = glCreateShader(type);
    if (shader == 0)
    {
        std::cerr << "Failed to create shader" << '\n';
        return 0;
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == 0)
    {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(logLength);
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        std::cerr << "Shader compilation failed:\n" << log.data() << '\n';
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint EarthMaterial::linkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    if (glCreateProgram == nullptr)
    {
        std::cerr << "Failed to load OpenGL shader extensions" << '\n';
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0)
    {
        std::cerr << "Failed to create shader program" << '\n';
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == 0)
    {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(logLength);
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        std::cerr << "Shader linking failed:\n" << log.data() << '\n';
        glDeleteProgram(program);
        return 0;
    }

    return program;
}
