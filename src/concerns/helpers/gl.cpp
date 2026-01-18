// Prevent Windows OpenGL headers from being included
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef GL_NO_PROTOTYPES
#define GL_NO_PROTOTYPES
#endif
// Prevent GL/gl.h from being included by defining its guard
// This prevents Windows OpenGL headers from declaring these functions
#ifndef __gl_h_
#define __gl_h_
// Define empty stubs for OpenGL functions to prevent dllimport declarations
// We'll provide our own implementations below
#define WINGDIAPI
#define APIENTRY
#endif

#include "gl.h"
#include "vulkan.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <unordered_map>


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
PFNGLUNIFORM2FPROC glUniform2f = nullptr;
PFNGLUNIFORM3FPROC glUniform3f = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLACTIVETEXTUREPROC glActiveTexture_ptr = nullptr;
PFNGLTEXIMAGE3DPROC glTexImage3D = nullptr;
PFNGLTEXSUBIMAGE3DPROC glTexSubImage3D = nullptr;
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;
PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers = nullptr;
PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer = nullptr;
PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers = nullptr;
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// OpenGL functions removed - migrate to Vulkan instead
// All OpenGL calls must be replaced with Vulkan equivalents

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

// Vulkan-based implementations of OpenGL functions
// These replace the OpenGL function loading with Vulkan implementations

namespace
{
// State tracking
struct VulkanGLState
{
    std::unordered_map<GLuint, std::string> shaderSources;
    std::unordered_map<GLuint, VkShaderModule> shaderModules;
    std::unordered_map<GLuint, VkPipeline> pipelines;
    std::unordered_map<GLuint, VulkanBuffer> buffers;
    std::unordered_map<GLuint, uint32_t> vertexArrays;
    GLuint currentProgram = 0;
    GLuint currentVAO = 0;
    GLuint currentArrayBuffer = 0;
    GLuint currentElementArrayBuffer = 0;
    GLenum activeTextureUnit = GL_TEXTURE0;
};

VulkanGLState g_vkGLState;
GLuint g_nextHandle = 1;

GLuint allocateHandle()
{
    return g_nextHandle++;
}
} // namespace

// Vulkan implementations
static GLuint vkCreateShader(GLenum type)
{
    return allocateHandle();
}

static void vkShaderSource(GLuint shader, GLsizei count, const char **string, const GLint *length)
{
    std::string source;
    if (length == nullptr)
    {
        for (GLsizei i = 0; i < count; i++)
        {
            source += string[i];
        }
    }
    else
    {
        for (GLsizei i = 0; i < count; i++)
        {
            if (length[i] < 0)
            {
                source += string[i];
            }
            else
            {
                source.append(string[i], length[i]);
            }
        }
    }
    g_vkGLState.shaderSources[shader] = source;
}

static void vkCompileShader(GLuint shader)
{
    // Shader compilation happens during pipeline creation in Vulkan
    // This is a placeholder - actual compilation happens in linkProgram
}

static void vkGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
    if (pname == GL_COMPILE_STATUS)
    {
        *params = g_vkGLState.shaderSources.find(shader) != g_vkGLState.shaderSources.end() ? 1 : 0;
    }
    else if (pname == GL_INFO_LOG_LENGTH)
    {
        *params = 0; // No log for now
    }
}

static void vkGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, char *infoLog)
{
    if (length)
        *length = 0;
    if (infoLog && maxLength > 0)
        infoLog[0] = '\0';
}

static GLuint vkCreateProgram()
{
    return allocateHandle();
}

static void vkAttachShader(GLuint program, GLuint shader)
{
    // Shader attachment is handled during pipeline creation
}

static void vkLinkProgram(GLuint program)
{
    // In Vulkan, linking means creating a pipeline
    // This requires vertex and fragment shaders to be compiled
    // For now, this is a placeholder
}

static void vkGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
    if (pname == GL_LINK_STATUS)
    {
        *params = g_vkGLState.pipelines.find(program) != g_vkGLState.pipelines.end() ? 1 : 0;
    }
}

static void vkGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, char *infoLog)
{
    if (length)
        *length = 0;
    if (infoLog && maxLength > 0)
        infoLog[0] = '\0';
}

static void vkUseProgram(GLuint program)
{
    g_vkGLState.currentProgram = program;
}

static void vkDeleteShader(GLuint shader)
{
    auto it = g_vkGLState.shaderModules.find(shader);
    if (it != g_vkGLState.shaderModules.end() && g_vulkanContext)
    {
        vkDestroyShaderModule(g_vulkanContext->device, it->second, nullptr);
        g_vkGLState.shaderModules.erase(it);
    }
    g_vkGLState.shaderSources.erase(shader);
}

static void vkDeleteProgram(GLuint program)
{
    auto it = g_vkGLState.pipelines.find(program);
    if (it != g_vkGLState.pipelines.end() && g_vulkanContext)
    {
        vkDestroyPipeline(g_vulkanContext->device, it->second, nullptr);
        g_vkGLState.pipelines.erase(it);
    }
}

static GLint vkGetUniformLocation(GLuint program, const char *name)
{
    // Return a handle for the uniform
    // Actual binding happens through descriptor sets in Vulkan
    return static_cast<GLint>(allocateHandle());
}

static void vkUniform1i(GLint location, GLint v0)
{
    // Uniform updates require descriptor set updates in Vulkan
    // This is a placeholder
}

static void vkUniform1f(GLint location, GLfloat v0)
{
    // Uniform updates require descriptor set updates in Vulkan
}

static void vkUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
    // Uniform updates require descriptor set updates in Vulkan
}

static void vkUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
    // Uniform updates require descriptor set updates in Vulkan
}

static void vkUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    // Uniform updates require descriptor set updates in Vulkan
}

static void vkActiveTexture(GLenum texture)
{
    g_vkGLState.activeTextureUnit = texture;
}

static void vkGenBuffers(GLsizei n, GLuint *buffers)
{
    for (GLsizei i = 0; i < n; i++)
    {
        buffers[i] = allocateHandle();
    }
}

static void vkBindBuffer(GLenum target, GLuint buffer)
{
    if (target == GL_ARRAY_BUFFER)
    {
        g_vkGLState.currentArrayBuffer = buffer;
    }
    else if (target == GL_ELEMENT_ARRAY_BUFFER)
    {
        g_vkGLState.currentElementArrayBuffer = buffer;
    }
}

static void vkBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    if (!g_vulkanContext)
        return;

    GLuint bufferHandle = (target == GL_ARRAY_BUFFER)           ? g_vkGLState.currentArrayBuffer
                          : (target == GL_ELEMENT_ARRAY_BUFFER) ? g_vkGLState.currentElementArrayBuffer
                                                                : 0;
    if (bufferHandle == 0)
        return;

    VkBufferUsageFlags vkUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (target == GL_ELEMENT_ARRAY_BUFFER)
    {
        vkUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }

    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (usage == GL_DYNAMIC_DRAW)
    {
        properties |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    }
    else
    {
        properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    VulkanBuffer buffer = createBuffer(*g_vulkanContext, size, vkUsage, properties, data);
    g_vkGLState.buffers[bufferHandle] = buffer;
}

static void vkDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    if (!g_vulkanContext)
        return;
    for (GLsizei i = 0; i < n; i++)
    {
        auto it = g_vkGLState.buffers.find(buffers[i]);
        if (it != g_vkGLState.buffers.end())
        {
            destroyBuffer(*g_vulkanContext, it->second);
            g_vkGLState.buffers.erase(it);
        }
    }
}

// Cleanup function for GL compatibility layer buffers (called from cleanupVulkan)
void cleanupGLBuffers()
{
    extern VulkanContext *g_vulkanContext;
    if (!g_vulkanContext)
        return;

    std::cerr << "Cleaning up " << g_vkGLState.buffers.size() << " GL compatibility buffers..." << std::endl;
    for (auto &pair : g_vkGLState.buffers)
    {
        if (pair.second.buffer != VK_NULL_HANDLE || pair.second.allocation != VK_NULL_HANDLE)
        {
            destroyBuffer(*g_vulkanContext, pair.second);
        }
    }
    g_vkGLState.buffers.clear();
}

static void vkGenVertexArrays(GLsizei n, GLuint *arrays)
{
    for (GLsizei i = 0; i < n; i++)
    {
        arrays[i] = allocateHandle();
        g_vkGLState.vertexArrays[arrays[i]] = 0;
    }
}

static void vkBindVertexArray(GLuint array)
{
    g_vkGLState.currentVAO = array;
}

static void vkDeleteVertexArrays(GLsizei n, const GLuint *arrays)
{
    for (GLsizei i = 0; i < n; i++)
    {
        g_vkGLState.vertexArrays.erase(arrays[i]);
    }
}

static void vkVertexAttribPointer(GLuint index,
                                  GLint size,
                                  GLenum type,
                                  GLboolean normalized,
                                  GLsizei stride,
                                  const void *pointer)
{
    // Vertex attributes are defined in pipeline creation in Vulkan
    // This is stored for later use during pipeline creation
}

static void vkEnableVertexAttribArray(GLuint index)
{
    // Vertex attributes are always enabled in Vulkan pipelines
}

static void vkDisableVertexAttribArray(GLuint index)
{
    // Vertex attributes are always enabled in Vulkan pipelines
}

// Framebuffer functions (placeholders for now)
static void vkGenFramebuffers(GLsizei n, GLuint *framebuffers)
{
    for (GLsizei i = 0; i < n; i++)
    {
        framebuffers[i] = allocateHandle();
    }
}

static void vkBindFramebuffer(GLenum target, GLuint framebuffer)
{
    // Framebuffer binding in Vulkan is done through render passes
}

static void vkFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    // Framebuffer attachments are defined in render pass creation
}

static GLenum vkCheckFramebufferStatus(GLenum target)
{
    return GL_FRAMEBUFFER_COMPLETE; // Assume complete for now
}

static void vkDeleteFramebuffers(GLsizei n, const GLuint *framebuffers)
{
    // Cleanup handled by Vulkan context
}

// Renderbuffer functions (placeholders)
static void vkGenRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
    for (GLsizei i = 0; i < n; i++)
    {
        renderbuffers[i] = allocateHandle();
    }
}

static void vkBindRenderbuffer(GLenum target, GLuint renderbuffer)
{
    // Renderbuffers not directly used in Vulkan
}

static void vkRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height)
{
    // Renderbuffers not directly used in Vulkan
}

static void vkFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
    // Renderbuffers not directly used in Vulkan
}

static void vkDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers)
{
    // Cleanup handled by Vulkan context
}

// 3D texture functions
static void vkTexImage3D(GLenum target,
                         GLint level,
                         GLint internalformat,
                         GLsizei width,
                         GLsizei height,
                         GLsizei depth,
                         GLint border,
                         GLenum format,
                         GLenum type,
                         const void *pixels)
{
    // 3D textures require VkImage creation with 3D extent
    // Placeholder for now
}

static void vkTexSubImage3D(GLenum target,
                            GLint level,
                            GLint xoffset,
                            GLint yoffset,
                            GLint zoffset,
                            GLsizei width,
                            GLsizei height,
                            GLsizei depth,
                            GLenum format,
                            GLenum type,
                            const void *pixels)
{
    // 3D texture updates require image copy operations
    // Placeholder for now
}

// Legacy OpenGL function stubs - these are temporary placeholders during Vulkan migration
// TODO: Replace all calls to these functions with Vulkan equivalents

static void vkBlendFunc(GLenum sfactor, GLenum dfactor)
{
    // Blend state is set in Vulkan pipeline creation
}

static void vkColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    // Color is set via uniform buffers in Vulkan
}

static void vkColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    // Color is set via uniform buffers in Vulkan
}

static void vkDepthFunc(GLenum func)
{
    // Depth compare function is set in Vulkan pipeline creation
}

static void vkDepthMask(GLboolean flag)
{
    // Depth write enable is set in Vulkan pipeline creation
}

static void vkDisable(GLenum cap)
{
    // State is set in Vulkan pipeline creation
}

static void vkEnable(GLenum cap)
{
    // State is set in Vulkan pipeline creation
}

static GLenum vkGetError(void)
{
    return GL_NO_ERROR; // Vulkan errors are checked via VkResult
}

static void vkMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    // Material properties are set via uniform buffers in Vulkan
}

static void vkMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    // Material properties are set via uniform buffers in Vulkan
}

static void vkMatrixMode(GLenum mode)
{
    // Matrices are managed via uniform buffers in Vulkan
}

static void vkPolygonMode(GLenum face, GLenum mode)
{
    // Polygon mode is set in Vulkan pipeline creation (rasterization state)
}

static void vkPopMatrix(void)
{
    // Matrices are managed via uniform buffers in Vulkan
}

static void vkPushMatrix(void)
{
    // Matrices are managed via uniform buffers in Vulkan
}

// Function pointers for OpenGL matrix functions (loaded at runtime if OpenGL context available)
static PFNGLLOADIDENTITYPROC glLoadIdentity_real = nullptr;
static PFNGLORTHOPROC glOrtho_real = nullptr;
static bool glMatrixFunctionsLoaded = false;

static void loadGLMatrixFunctions()
{
    if (glMatrixFunctionsLoaded)
    {
        return;
    }
    // Try to load OpenGL functions for UI rendering
    // These are part of OpenGL 1.0, so they should be available if OpenGL context exists
    glLoadIdentity_real = glfwGetProcAddressCast<PFNGLLOADIDENTITYPROC>("glLoadIdentity");
    glOrtho_real = glfwGetProcAddressCast<PFNGLORTHOPROC>("glOrtho");
    glMatrixFunctionsLoaded = true;
}

static void vkLoadIdentity(void)
{
    loadGLMatrixFunctions();
    // For UI rendering, call real OpenGL function if available
    if (glLoadIdentity_real)
    {
        glLoadIdentity_real();
    }
    // Otherwise, matrices are managed via uniform buffers in Vulkan (no-op)
}

static void vkOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearVal, GLdouble farVal)
{
    loadGLMatrixFunctions();
    // For UI rendering, call real OpenGL function if available
    if (glOrtho_real)
    {
        glOrtho_real(left, right, bottom, top, nearVal, farVal);
    }
    // Otherwise, orthographic projection is managed via uniform buffers in Vulkan (no-op)
}

static void vkReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)
{
    // Use vkCmdCopyImageToBuffer in Vulkan
    // This is a placeholder - actual implementation requires command buffer recording
}

static void vkTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    // Translation is handled via uniform buffers in Vulkan
}

static void vkBindTexture(GLenum target, GLuint texture)
{
    // Texture binding is done via descriptor sets in Vulkan
}

static void vkDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    // Use vkCmdDrawIndexed in Vulkan
    // This is a placeholder - actual drawing requires command buffer recording
}

static void vkGenTextures(GLsizei n, GLuint *textures)
{
    for (GLsizei i = 0; i < n; i++)
    {
        textures[i] = allocateHandle();
    }
}

static void vkTexImage2D(GLenum target,
                         GLint level,
                         GLint internalformat,
                         GLsizei width,
                         GLsizei height,
                         GLint border,
                         GLenum format,
                         GLenum type,
                         const void *pixels)
{
    // Texture creation requires VkImage creation in Vulkan
    // This is a placeholder
}

static void vkTexParameteri(GLenum target, GLenum pname, GLint param)
{
    // Texture parameters are set via VkSampler in Vulkan
}

static void vkDeleteTextures(GLsizei n, const GLuint *textures)
{
    // Texture cleanup is handled by Vulkan context
}

static void vkBegin(GLenum mode)
{
    // Immediate mode rendering is not supported in Vulkan
    // Use vertex buffers and vkCmdDraw* instead
}

static void vkEnd(void)
{
    // Immediate mode rendering is not supported in Vulkan
}

static void vkLineWidth(GLfloat width)
{
    // Line width is set in Vulkan pipeline creation (rasterization state)
}

static void vkVertex2f(GLfloat x, GLfloat y)
{
    // Immediate mode rendering is not supported in Vulkan
    // Use vertex buffers instead
}

static void vkVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    // Immediate mode rendering is not supported in Vulkan
    // Use vertex buffers instead
}

// All OpenGL stub implementations removed - migrate to Vulkan

// Load GL extension functions - now assigns Vulkan implementations or stubs
bool loadGLExtensions()
{
    if (glExtensionsLoaded)
    {
        return true;
    }

    // Assign Vulkan implementations instead of loading OpenGL functions
    glCreateShader = vkCreateShader;
    glShaderSource = vkShaderSource;
    glCompileShader = vkCompileShader;
    glGetShaderiv = vkGetShaderiv;
    glGetShaderInfoLog = vkGetShaderInfoLog;
    glCreateProgram = vkCreateProgram;
    glAttachShader = vkAttachShader;
    glLinkProgram = vkLinkProgram;
    glGetProgramiv = vkGetProgramiv;
    glGetProgramInfoLog = vkGetProgramInfoLog;
    glUseProgram = vkUseProgram;
    glDeleteShader = vkDeleteShader;
    glDeleteProgram = vkDeleteProgram;
    glGetUniformLocation = vkGetUniformLocation;
    glUniform1i = vkUniform1i;
    glUniform1f = vkUniform1f;
    glUniform2f = vkUniform2f;
    glUniform3f = vkUniform3f;
    glUniformMatrix4fv = vkUniformMatrix4fv;
    glActiveTexture_ptr = vkActiveTexture;
    glTexImage3D = vkTexImage3D;
    glTexSubImage3D = vkTexSubImage3D;
    glGenFramebuffers = vkGenFramebuffers;
    glBindFramebuffer = vkBindFramebuffer;
    glFramebufferTexture2D = vkFramebufferTexture2D;
    glCheckFramebufferStatus = vkCheckFramebufferStatus;
    glDeleteFramebuffers = vkDeleteFramebuffers;
    glGenRenderbuffers = vkGenRenderbuffers;
    glBindRenderbuffer = vkBindRenderbuffer;
    glRenderbufferStorage = vkRenderbufferStorage;
    glFramebufferRenderbuffer = vkFramebufferRenderbuffer;
    glDeleteRenderbuffers = vkDeleteRenderbuffers;
    glGenVertexArrays = vkGenVertexArrays;
    glBindVertexArray = vkBindVertexArray;
    glDeleteVertexArrays = vkDeleteVertexArrays;
    glGenBuffers = vkGenBuffers;
    glBindBuffer = vkBindBuffer;
    glBufferData = vkBufferData;
    glDeleteBuffers = vkDeleteBuffers;
    glVertexAttribPointer = vkVertexAttribPointer;
    glEnableVertexAttribArray = vkEnableVertexAttribArray;
    glDisableVertexAttribArray = vkDisableVertexAttribArray;

    // Note: All OpenGL functions must now be migrated to Vulkan.
    // The functions that were previously stubbed (glGetIntegerv, glEnable, etc.)
    // must be replaced with Vulkan equivalents in the calling code.

    glExtensionsLoaded = true;
    return true;
}

// Legacy OpenGL function implementations (not function pointers)
// These are actual functions that Windows expects to be linked
// They're stubs during Vulkan migration

// On Windows, use __declspec(dllexport) to override __declspec(dllimport) from GL/gl.h
#ifdef _WIN32
#define GL_STUB_EXPORT __declspec(dllexport)
#else
#define GL_STUB_EXPORT
#endif

extern "C"
{
    GL_STUB_EXPORT void glBlendFunc(GLenum sfactor, GLenum dfactor)
    {
        vkBlendFunc(sfactor, dfactor);
    }

    GL_STUB_EXPORT void glColor3f(GLfloat red, GLfloat green, GLfloat blue)
    {
        vkColor3f(red, green, blue);
    }

    GL_STUB_EXPORT void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
    {
        vkColor4f(red, green, blue, alpha);
    }

    GL_STUB_EXPORT void glDepthFunc(GLenum func)
    {
        vkDepthFunc(func);
    }

    GL_STUB_EXPORT void glDepthMask(GLboolean flag)
    {
        vkDepthMask(flag);
    }

    GL_STUB_EXPORT void glDisable(GLenum cap)
    {
        vkDisable(cap);
    }

    GL_STUB_EXPORT void glEnable(GLenum cap)
    {
        vkEnable(cap);
    }

    GL_STUB_EXPORT GLenum glGetError(void)
    {
        return vkGetError();
    }

    GL_STUB_EXPORT void glMaterialf(GLenum face, GLenum pname, GLfloat param)
    {
        vkMaterialf(face, pname, param);
    }

    GL_STUB_EXPORT void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
    {
        vkMaterialfv(face, pname, params);
    }

    GL_STUB_EXPORT void glMatrixMode(GLenum mode)
    {
        vkMatrixMode(mode);
    }

    GL_STUB_EXPORT void glPolygonMode(GLenum face, GLenum mode)
    {
        vkPolygonMode(face, mode);
    }

    GL_STUB_EXPORT void glPopMatrix(void)
    {
        vkPopMatrix();
    }

    GL_STUB_EXPORT void glPushMatrix(void)
    {
        vkPushMatrix();
    }

    GL_STUB_EXPORT void glLoadIdentity(void)
    {
        vkLoadIdentity();
    }

    GL_STUB_EXPORT void glOrtho(GLdouble left,
                                GLdouble right,
                                GLdouble bottom,
                                GLdouble top,
                                GLdouble nearVal,
                                GLdouble farVal)
    {
        vkOrtho(left, right, bottom, top, nearVal, farVal);
    }

    GL_STUB_EXPORT void glReadPixels(GLint x,
                                     GLint y,
                                     GLsizei width,
                                     GLsizei height,
                                     GLenum format,
                                     GLenum type,
                                     void *pixels)
    {
        vkReadPixels(x, y, width, height, format, type, pixels);
    }

    GL_STUB_EXPORT void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
    {
        vkTranslatef(x, y, z);
    }

    GL_STUB_EXPORT void glBindTexture(GLenum target, GLuint texture)
    {
        vkBindTexture(target, texture);
    }

    GL_STUB_EXPORT void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
    {
        vkDrawElements(mode, count, type, indices);
    }

    GL_STUB_EXPORT void glGenTextures(GLsizei n, GLuint *textures)
    {
        vkGenTextures(n, textures);
    }

    GL_STUB_EXPORT void glTexImage2D(GLenum target,
                                     GLint level,
                                     GLint internalformat,
                                     GLsizei width,
                                     GLsizei height,
                                     GLint border,
                                     GLenum format,
                                     GLenum type,
                                     const void *pixels)
    {
        vkTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    }

    GL_STUB_EXPORT void glTexParameteri(GLenum target, GLenum pname, GLint param)
    {
        vkTexParameteri(target, pname, param);
    }

    GL_STUB_EXPORT void glDeleteTextures(GLsizei n, const GLuint *textures)
    {
        vkDeleteTextures(n, textures);
    }

    GL_STUB_EXPORT void glBegin(GLenum mode)
    {
        vkBegin(mode);
    }

    GL_STUB_EXPORT void glEnd(void)
    {
        vkEnd();
    }

    GL_STUB_EXPORT void glLineWidth(GLfloat width)
    {
        vkLineWidth(width);
    }

    GL_STUB_EXPORT void glVertex2f(GLfloat x, GLfloat y)
    {
        vkVertex2f(x, y);
    }

    GL_STUB_EXPORT void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
    {
        vkVertex3f(x, y, z);
    }

} // extern "C"

#undef GL_STUB_EXPORT
