#pragma once

#include <GLFW/glfw3.h>
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
typedef void(APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLenum(APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void(APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint *framebuffers);
typedef void(APIENTRY *PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint *renderbuffers);
typedef void(APIENTRY *PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);
typedef void(APIENTRY *PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void(APIENTRY *PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void(APIENTRY *PFNGLDELETERENDERBUFFERSPROC)(GLsizei n, const GLuint *renderbuffers);

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
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Load OpenGL shader extension functions (must be called after GL context
// creation)
bool loadGLExtensions();
