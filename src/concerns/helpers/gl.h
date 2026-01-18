#pragma once

// Prevent Windows OpenGL headers from being included
// Define these before any Windows headers to prevent GL/gl.h inclusion
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Prevent OpenGL header inclusion by defining GL_NO_PROTOTYPES
// This prevents function declarations from GL/gl.h
#ifndef GL_NO_PROTOTYPES
#define GL_NO_PROTOTYPES
#endif

// Define GLFW_NO_API before including GLFW to prevent OpenGL header inclusion
#ifndef GLFW_NO_API
#define GLFW_NO_API
#endif
#include <GLFW/glfw3.h>
#include <cstddef> // for ptrdiff_t

// Define OpenGL types that might be needed
#ifndef GLenum
typedef unsigned int GLenum;
#endif
#ifndef GLboolean
typedef unsigned char GLboolean;
#endif
#ifndef GLint
typedef int GLint;
#endif
#ifndef GLuint
typedef unsigned int GLuint;
#endif
#ifndef GLfloat
typedef float GLfloat;
#endif
#ifndef GLdouble
typedef double GLdouble;
#endif
#ifndef GLsizei
typedef int GLsizei;
#endif
#ifndef GLsizeiptr
typedef ptrdiff_t GLsizeiptr;
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
// ============================================================================
// OpenGL Extension Loading
// ============================================================================

// OpenGL constants that may not be defined in basic Windows OpenGL headers
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#ifndef GL_R32F
#define GL_R32F 0x822E
#endif

#ifndef GL_RGB32F
#define GL_RGB32F 0x8815
#endif

#ifndef GL_RED
#define GL_RED 0x1903
#endif

#ifndef GL_TEXTURE_1D
#define GL_TEXTURE_1D 0x0DE0
#endif

#ifndef GL_TEXTURE_3D
#define GL_TEXTURE_3D 0x806F
#endif

#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif

// Buffer constants for VAO/VBO
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif

#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif

#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif

#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif

// Primitive types
#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif

#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif

// Additional OpenGL constants needed for stubs
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#endif
#ifndef GL_FALSE
#define GL_FALSE 0
#endif
#ifndef GL_TRUE
#define GL_TRUE 1
#endif
#ifndef GL_LIGHTING
#define GL_LIGHTING 0x0B50
#endif
#ifndef GL_LIGHT0
#define GL_LIGHT0 0x4000
#endif

// Size type for buffer operations
#ifndef GLsizeiptr
typedef ptrdiff_t GLsizeiptr;
#endif

// Framebuffer constants
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_RENDERBUFFER 0x8D41
#endif

// OpenGL shader constants (may not be in basic headers)
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#endif

#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE4 0x84C4
#define GL_TEXTURE5 0x84C5
#define GL_TEXTURE6 0x84C6
#define GL_TEXTURE7 0x84C7
#define GL_TEXTURE8 0x84C8
#define GL_TEXTURE9 0x84C9
#define GL_TEXTURE10 0x84CA
#define GL_TEXTURE11 0x84CB
#define GL_TEXTURE12 0x84CC
#define GL_TEXTURE13 0x84CD
#define GL_TEXTURE14 0x84CE
#define GL_TEXTURE15 0x84CF
#endif

// OpenGL function pointer types
typedef void(APIENTRY *PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef GLuint(APIENTRY *PFNGLCREATESHADERPROC)(GLenum type);
typedef void(APIENTRY *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const char **string, const GLint *length);
typedef void(APIENTRY *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void(APIENTRY *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void(APIENTRY *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei maxLength, GLsizei *length, char *infoLog);
typedef GLuint(APIENTRY *PFNGLCREATEPROGRAMPROC)(void);
typedef void(APIENTRY *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void(APIENTRY *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void(APIENTRY *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void(APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei maxLength, GLsizei *length, char *infoLog);
typedef void(APIENTRY *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void(APIENTRY *PFNGLDELETESHADERPROC)(GLuint shader);
typedef void(APIENTRY *PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint(APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const char *name);
typedef void(APIENTRY *PFNGLUNIFORM1IPROC)(GLint location, GLint v00);
typedef void(APIENTRY *PFNGLUNIFORM1FPROC)(GLint location, GLfloat v00);
typedef void(APIENTRY *PFNGLUNIFORM2FPROC)(GLint location, GLfloat v00, GLfloat v01);
typedef void(APIENTRY *PFNGLUNIFORM3FPROC)(GLint location, GLfloat v00, GLfloat v01, GLfloat v02);
typedef void(APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)(GLint location,
                                                  GLsizei count,
                                                  GLboolean transpose,
                                                  const GLfloat *value);
typedef void(APIENTRY *PFNGLTEXIMAGE3DPROC)(GLenum target,
                                            GLint level,
                                            GLint internalformat,
                                            GLsizei width,
                                            GLsizei height,
                                            GLsizei depth,
                                            GLint border,
                                            GLenum format,
                                            GLenum type,
                                            const void *pixels);
typedef void(APIENTRY *PFNGLTEXSUBIMAGE3DPROC)(GLenum target,
                                               GLint level,
                                               GLint xoffset,
                                               GLint yoffset,
                                               GLint zoffset,
                                               GLsizei width,
                                               GLsizei height,
                                               GLsizei depth,
                                               GLenum format,
                                               GLenum type,
                                               const void *pixels);
typedef void(APIENTRY *PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
typedef void(APIENTRY *PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void(APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target,
                                                      GLenum attachment,
                                                      GLenum textarget,
                                                      GLuint texture,
                                                      GLint level);
typedef GLenum(APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void(APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint *framebuffers);
typedef void(APIENTRY *PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint *renderbuffers);
typedef void(APIENTRY *PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);
typedef void(APIENTRY *PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target,
                                                     GLenum internalformat,
                                                     GLsizei width,
                                                     GLsizei height);
typedef void(APIENTRY *PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target,
                                                         GLenum attachment,
                                                         GLenum renderbuffertarget,
                                                         GLuint renderbuffer);
typedef void(APIENTRY *PFNGLDELETERENDERBUFFERSPROC)(GLsizei n, const GLuint *renderbuffers);
// VAO/VBO functions for OpenGL 3.3 core profile
typedef void(APIENTRY *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
typedef void(APIENTRY *PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void(APIENTRY *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint *arrays);
typedef void(APIENTRY *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void(APIENTRY *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void(APIENTRY *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void(APIENTRY *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);
typedef void(APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index,
                                                     GLint size,
                                                     GLenum type,
                                                     GLboolean normalized,
                                                     GLsizei stride,
                                                     const void *pointer);
typedef void(APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void(APIENTRY *PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint index);
// Additional OpenGL functions needed for migration (temporary stubs)
typedef void(APIENTRY *PFNGLGETINTEGERVPROC)(GLenum pname, GLint *params);
typedef GLboolean(APIENTRY *PFNGLISENABLEDPROC)(GLenum cap);
typedef void(APIENTRY *PFNGLTEXENVFPROC)(GLenum target, GLenum pname, GLfloat param);
typedef void(APIENTRY *PFNGLTEXIMAGE2DPROC)(GLenum target,
                                            GLint level,
                                            GLint internalformat,
                                            GLsizei width,
                                            GLsizei height,
                                            GLint border,
                                            GLenum format,
                                            GLenum type,
                                            const void *pixels);
typedef void(APIENTRY *PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void(APIENTRY *PFNGLLOADIDENTITYPROC)(void);
typedef void(APIENTRY *PFNGLORTHOPROC)(GLdouble left,
                                       GLdouble right,
                                       GLdouble bottom,
                                       GLdouble top,
                                       GLdouble zNear,
                                       GLdouble zFar);
typedef void(APIENTRY *PFNGLVERTEX2FPROC)(GLfloat x, GLfloat y);
typedef void(APIENTRY *PFNGLVERTEX3FPROC)(GLfloat x, GLfloat y, GLfloat z);
typedef void(APIENTRY *PFNGLCOLORMATERIALPROC)(GLenum face, GLenum mode);
typedef void(APIENTRY *PFNGLLIGHTMODELFVPROC)(GLenum pname, const GLfloat *params);
typedef void(APIENTRY *PFNGLLIGHTFPROC)(GLenum light, GLenum pname, GLfloat param);
typedef void(APIENTRY *PFNGLLIGHTFVPROC)(GLenum light, GLenum pname, const GLfloat *params);
typedef void(APIENTRY *PFNGLSHADEMODELPROC)(GLenum mode);
typedef void(APIENTRY *PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint *textures);
typedef void(APIENTRY *PFNGLTEXCOORD2FPROC)(GLfloat s, GLfloat t);
typedef void(APIENTRY *PFNGLCLEARCOLORPROC)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void(APIENTRY *PFNGLPOLYGONOFFSETPROC)(GLfloat factor, GLfloat units);
typedef void(APIENTRY *PFNGLENABLEPROC)(GLenum cap);
typedef void(APIENTRY *PFNGLDISABLEPROC)(GLenum cap);
typedef void(APIENTRY *PFNGLBEGINPROC)(GLenum mode);
typedef void(APIENTRY *PFNGLENDPROC)(void);
typedef GLenum(APIENTRY *PFNGLGETERRORPROC)(void);
// Additional OpenGL functions needed for migration
typedef void(APIENTRY *PFNGLPUSHMATRIXPROC)(void);
typedef void(APIENTRY *PFNGLPOPMATRIXPROC)(void);
typedef void(APIENTRY *PFNGLREADPIXELSPROC)(GLint x,
                                            GLint y,
                                            GLsizei width,
                                            GLsizei height,
                                            GLenum format,
                                            GLenum type,
                                            void *pixels);
typedef void(APIENTRY *PFNGLTRANSLATEFPROC)(GLfloat x, GLfloat y, GLfloat z);
typedef void(APIENTRY *PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void(APIENTRY *PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void(APIENTRY *PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count, GLenum type, const void *indices);
typedef void(APIENTRY *PFNGLGENTEXTURESPROC)(GLsizei n, GLuint *textures);
typedef void(APIENTRY *PFNGLGETBOOLEANVPROC)(GLenum pname, GLboolean *params);
typedef void(APIENTRY *PFNGLGETFLOATVPROC)(GLenum pname, GLfloat *params);

// Extern declarations for OpenGL function pointers (loaded at runtime)
// These cannot be const because they are assigned values at runtime via glfwGetProcAddress
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLDELETESHADERPROC glDeleteShader;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLUNIFORM1IPROC glUniform1i;
extern PFNGLUNIFORM1FPROC glUniform1f;
extern PFNGLUNIFORM2FPROC glUniform2f;
extern PFNGLUNIFORM3FPROC glUniform3f;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
extern PFNGLACTIVETEXTUREPROC glActiveTexture_ptr;
extern PFNGLTEXIMAGE3DPROC glTexImage3D;
extern PFNGLTEXSUBIMAGE3DPROC glTexSubImage3D;
extern PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
extern PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
extern PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer;
extern PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
extern PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers;
extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// OpenGL stub function declarations (implemented in gl.cpp)
// These are regular functions, not function pointers, to avoid dllimport issues
// On Windows, we use __declspec(dllexport) to override the __declspec(dllimport) from GL/gl.h
#ifdef _WIN32
#define GL_STUB_EXPORT __declspec(dllexport)
// Suppress warnings about inconsistent DLL linkage - we're intentionally overriding Windows OpenGL functions
#pragma warning(push)
#pragma warning(disable : 4273) // inconsistent dll linkage
#else
#define GL_STUB_EXPORT
#endif

extern "C"
{
    GL_STUB_EXPORT void glBlendFunc(GLenum sfactor, GLenum dfactor);
    GL_STUB_EXPORT void glColor3f(GLfloat red, GLfloat green, GLfloat blue);
    GL_STUB_EXPORT void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
    GL_STUB_EXPORT void glDepthFunc(GLenum func);
    GL_STUB_EXPORT void glDepthMask(GLboolean flag);
    GL_STUB_EXPORT void glEnable(GLenum cap);
    GL_STUB_EXPORT void glDisable(GLenum cap);
    GL_STUB_EXPORT GLenum glGetError(void);
    GL_STUB_EXPORT void glBegin(GLenum mode);
    GL_STUB_EXPORT void glEnd(void);
    GL_STUB_EXPORT void glLineWidth(GLfloat width);
    GL_STUB_EXPORT void glVertex2f(GLfloat x, GLfloat y);
    GL_STUB_EXPORT void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
    GL_STUB_EXPORT void glBindTexture(GLenum target, GLuint texture);
    GL_STUB_EXPORT void glGenTextures(GLsizei n, GLuint *textures);
    GL_STUB_EXPORT void glTexImage2D(GLenum target,
                                     GLint level,
                                     GLint internalformat,
                                     GLsizei width,
                                     GLsizei height,
                                     GLint border,
                                     GLenum format,
                                     GLenum type,
                                     const void *pixels);
    GL_STUB_EXPORT void glTexParameteri(GLenum target, GLenum pname, GLint param);
    GL_STUB_EXPORT void glDeleteTextures(GLsizei n, const GLuint *textures);
    GL_STUB_EXPORT void glPushMatrix(void);
    GL_STUB_EXPORT void glPopMatrix(void);
    GL_STUB_EXPORT void glLoadIdentity(void);
    GL_STUB_EXPORT void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble nearVal, GLdouble farVal);
    GL_STUB_EXPORT void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
    GL_STUB_EXPORT void glMatrixMode(GLenum mode);
    GL_STUB_EXPORT void glPolygonMode(GLenum face, GLenum mode);
    GL_STUB_EXPORT void glReadPixels(GLint x,
                                     GLint y,
                                     GLsizei width,
                                     GLsizei height,
                                     GLenum format,
                                     GLenum type,
                                     void *pixels);
    GL_STUB_EXPORT void glMaterialf(GLenum face, GLenum pname, GLfloat param);
    GL_STUB_EXPORT void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
    GL_STUB_EXPORT void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
}

#undef GL_STUB_EXPORT

#ifdef _WIN32
#pragma warning(pop) // Restore warning settings
#endif

// All OpenGL stub functions removed - migrate to Vulkan instead

// If Windows OpenGL headers define these as functions, redirect them to our stubs
// Check if they're defined as functions (not function pointers) and redirect
#ifdef _WIN32
// On Windows, if GL/gl.h was included, these will be functions
// We'll use linker redirection or define macros after the header
// For now, we'll handle this in gl.cpp by checking if they're already defined
#endif

// Load OpenGL shader extension functions (must be called after GL context
// creation)
bool loadGLExtensions();

// Cleanup GL compatibility layer buffers (called from cleanupVulkan)
void cleanupGLBuffers();
