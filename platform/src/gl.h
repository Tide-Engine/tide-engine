#pragma once

// OpenGL as the platform layer uses it: the part OpenGL 3.3, OpenGL ES 3 and
// WebGL 2 share, plus core desktop timer queries, and only what the renderer
// calls (gpu_gl.c). No system
// header: Windows' stops at OpenGL 1.1, and each platform keeps its own
// elsewhere.
//
// On the web the functions are the page's (platform/web/tide.js gives each by
// its C name), and on Android libGLESv3's. On desktop they're looked up in the
// window's context once it's current (tide_gl_load, gl.c), and the names
// below call through what it found.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

enum {
    GL_NUM_COMPRESSED_TEXTURE_FORMATS = 0x86A2,
    GL_COMPRESSED_TEXTURE_FORMATS = 0x86A3,
    GL_COMPRESSED_RGB_S3TC_DXT1 = 0x83F0,
    GL_COMPRESSED_RGBA_S3TC_DXT5 = 0x83F3,
    GL_TEXTURE_MAX_LEVEL = 0x813D,
    GL_FALSE = 0,
    GL_TRUE = 1,
    GL_NONE = 0,
    GL_TRIANGLES = 0x0004,
    GL_DEPTH_BUFFER_BIT = 0x0100,
    GL_STENCIL_BUFFER_BIT = 0x0400,
    GL_COLOR_BUFFER_BIT = 0x4000,
    GL_NEVER = 0x0200,
    GL_LESS = 0x0201,
    GL_EQUAL = 0x0202,
    GL_LEQUAL = 0x0203,
    GL_GREATER = 0x0204,
    GL_NOTEQUAL = 0x0205,
    GL_GEQUAL = 0x0206,
    GL_ALWAYS = 0x0207,
    GL_ZERO = 0,
    GL_ONE = 1,
    GL_SRC_ALPHA = 0x0302,
    GL_ONE_MINUS_SRC_ALPHA = 0x0303,
    GL_ONE_MINUS_DST_ALPHA = 0x0305,
    GL_DST_COLOR = 0x0306,
    GL_FRONT = 0x0404,
    GL_BACK = 0x0405,
    GL_CW = 0x0900,
    GL_CCW = 0x0901,
    GL_CULL_FACE = 0x0B44,
    GL_DEPTH_TEST = 0x0B71,
    GL_STENCIL_TEST = 0x0B90,
    GL_BLEND = 0x0BE2,
    GL_SCISSOR_TEST = 0x0C11,
    GL_MAX_TEXTURE_SIZE = 0x0D33,
    GL_UNPACK_ROW_LENGTH = 0x0CF2,
    GL_UNPACK_ALIGNMENT = 0x0CF5,
    GL_PACK_ALIGNMENT = 0x0D05,
    GL_TEXTURE_2D = 0x0DE1,
    GL_TEXTURE_CUBE_MAP = 0x8513,
    GL_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515, // The five after it are -x, +y, -y, +z, -z
    GL_TEXTURE_CUBE_MAP_SEAMLESS = 0x884F, // OpenGL 3.3's to ask for: OpenGL ES 3 and WebGL 2 read across a cube's edges anyway
    GL_TEXTURE_WRAP_R = 0x8072,
    GL_UNSIGNED_BYTE = 0x1401,
    GL_UNSIGNED_SHORT = 0x1403,
    GL_INT = 0x1404,
    GL_UNSIGNED_INT = 0x1405,
    GL_FLOAT = 0x1406,
    GL_HALF_FLOAT = 0x140B,
    GL_INVERT = 0x150A,
    GL_RED = 0x1903,
    GL_RGBA = 0x1908,
    GL_KEEP = 0x1E00,
    GL_REPLACE = 0x1E01,
    GL_INCR = 0x1E02,
    GL_DECR = 0x1E03,
    GL_NEAREST = 0x2600,
    GL_LINEAR = 0x2601,
    GL_LINEAR_MIPMAP_LINEAR = 0x2703,
    GL_TEXTURE_MAG_FILTER = 0x2800,
    GL_TEXTURE_MIN_FILTER = 0x2801,
    GL_TEXTURE_WRAP_S = 0x2802,
    GL_TEXTURE_WRAP_T = 0x2803,
    GL_REPEAT = 0x2901,
    GL_SAMPLES = 0x80A9,
    GL_RGBA8 = 0x8058,
    GL_RGBA32I = 0x8D82,
    GL_RGBA_INTEGER = 0x8D99,
    GL_CLAMP_TO_EDGE = 0x812F,
    GL_DEPTH_STENCIL_ATTACHMENT = 0x821A,
    GL_MIRRORED_REPEAT = 0x8370,
    GL_R8 = 0x8229,
    GL_R16F = 0x822D,
    GL_TEXTURE0 = 0x84C0,
    GL_DEPTH_STENCIL = 0x84F9,
    GL_UNSIGNED_INT_24_8 = 0x84FA,
    GL_RGBA16F = 0x881A,
    GL_TEXTURE_COMPARE_MODE = 0x884C,
    GL_TEXTURE_COMPARE_FUNC = 0x884D,
    GL_COMPARE_REF_TO_TEXTURE = 0x884E,
    GL_DEPTH24_STENCIL8 = 0x88F0,
    GL_ARRAY_BUFFER = 0x8892,
    GL_ELEMENT_ARRAY_BUFFER = 0x8893,
    GL_STATIC_DRAW = 0x88E4,
    GL_DYNAMIC_DRAW = 0x88E8,
    GL_FRAGMENT_SHADER = 0x8B30,
    GL_VERTEX_SHADER = 0x8B31,
    GL_COMPILE_STATUS = 0x8B81,
    GL_LINK_STATUS = 0x8B82,
    GL_READ_FRAMEBUFFER = 0x8CA8,
    GL_DRAW_FRAMEBUFFER = 0x8CA9,
    GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
    GL_COLOR_ATTACHMENT0 = 0x8CE0,
    GL_FRAMEBUFFER = 0x8D40,
    GL_RENDERBUFFER = 0x8D41,
    GL_MAX_SAMPLES = 0x8D57,
    GL_UNIFORM_BUFFER = 0x8A11,
};

#define GL_INVALID_INDEX 0xFFFFFFFFu // A uniform block the program hasn't got

// Every function: V for those that return nothing, R for the others, each
// with its name after `gl`, its parameters, and those as arguments.
#define TIDE_GL_FUNCTIONS(V, R)                                                                                        \
    V(ActiveTexture, (GLenum texture), (texture))                                                                      \
    V(AttachShader, (GLuint program, GLuint shader), (program, shader))                                                \
    V(BindBuffer, (GLenum target, GLuint buffer), (target, buffer))                                                    \
    V(BindBufferBase, (GLenum target, GLuint index, GLuint buffer), (target, index, buffer))                           \
    V(BindFramebuffer, (GLenum target, GLuint framebuffer), (target, framebuffer))                                     \
    V(BindRenderbuffer, (GLenum target, GLuint renderbuffer), (target, renderbuffer))                                  \
    V(BindSampler, (GLuint unit, GLuint sampler), (unit, sampler))                                                     \
    V(BindTexture, (GLenum target, GLuint texture), (target, texture))                                                 \
    V(BindVertexArray, (GLuint array), (array))                                                                        \
    V(BlendFunc, (GLenum source, GLenum destination), (source, destination))                                           \
    V(BlitFramebuffer,                                                                                                 \
      (GLint x0, GLint y0, GLint x1, GLint y1, GLint to_x0, GLint to_y0, GLint to_x1, GLint to_y1, GLbitfield mask,    \
       GLenum filter),                                                                                                 \
      (x0, y0, x1, y1, to_x0, to_y0, to_x1, to_y1, mask, filter))                                                      \
    V(BlendFuncSeparate, (GLenum source, GLenum destination, GLenum source_alpha, GLenum destination_alpha),          \
      (source, destination, source_alpha, destination_alpha))                                                         \
    V(BufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage), (target, size, data, usage))       \
    V(BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data), (target, offset, size, data)) \
    R(GLenum, CheckFramebufferStatus, (GLenum target), (target))                                                       \
    V(Clear, (GLbitfield mask), (mask))                                                                                \
    V(ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a), (r, g, b, a))                                          \
    V(CompileShader, (GLuint shader), (shader))                                                                        \
    R(GLuint, CreateProgram, (void), ())                                                                               \
    R(GLuint, CreateShader, (GLenum type), (type))                                                                     \
    V(CullFace, (GLenum side), (side))                                                                                 \
    V(DeleteBuffers, (GLsizei count, const GLuint *buffers), (count, buffers))                                         \
    V(DeleteFramebuffers, (GLsizei count, const GLuint *framebuffers), (count, framebuffers))                          \
    V(DeleteProgram, (GLuint program), (program))                                                                      \
    V(DeleteRenderbuffers, (GLsizei count, const GLuint *renderbuffers), (count, renderbuffers))                       \
    V(DeleteShader, (GLuint shader), (shader))                                                                         \
    V(DeleteTextures, (GLsizei count, const GLuint *textures), (count, textures))                                      \
    V(DeleteVertexArrays, (GLsizei count, const GLuint *arrays), (count, arrays))                                      \
    V(DepthFunc, (GLenum function), (function))                                                                        \
    V(DepthMask, (GLboolean write), (write))                                                                           \
    V(Disable, (GLenum capability), (capability))                                                                      \
    V(DrawArrays, (GLenum mode, GLint first, GLsizei count), (mode, first, count))                                     \
    V(DrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei instances),                               \
      (mode, first, count, instances))                                                                                 \
    V(DrawBuffers, (GLsizei count, const GLenum *buffers), (count, buffers))                                           \
    V(DrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instances),        \
      (mode, count, type, indices, instances))                                                                         \
    V(Enable, (GLenum capability), (capability))                                                                       \
    V(EnableVertexAttribArray, (GLuint index), (index))                                                                \
    V(FramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum renderbuffer_target, GLuint renderbuffer),    \
      (target, attachment, renderbuffer_target, renderbuffer))                                                         \
    V(FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum texture_target, GLuint texture, GLint level),    \
      (target, attachment, texture_target, texture, level))                                                            \
    V(FrontFace, (GLenum winding), (winding))                                                                          \
    V(GenBuffers, (GLsizei count, GLuint *buffers), (count, buffers))                                                      V(GenerateMipmap, (GLenum target), (target))                                                                       \
    V(GenFramebuffers, (GLsizei count, GLuint *framebuffers), (count, framebuffers))                                   \
    V(GenRenderbuffers, (GLsizei count, GLuint *renderbuffers), (count, renderbuffers))                                \
    V(GenSamplers, (GLsizei count, GLuint *samplers), (count, samplers))                                               \
    V(GenTextures, (GLsizei count, GLuint *textures), (count, textures))                                               \
    V(GenVertexArrays, (GLsizei count, GLuint *arrays), (count, arrays))                                               \
    V(GetIntegerv, (GLenum name, GLint *out), (name, out))                                                             \
    V(GetProgramInfoLog, (GLuint program, GLsizei capacity, GLsizei *length, GLchar *log),                             \
      (program, capacity, length, log))                                                                                \
    V(GetProgramiv, (GLuint program, GLenum name, GLint *out), (program, name, out))                                   \
    V(GetShaderInfoLog, (GLuint shader, GLsizei capacity, GLsizei *length, GLchar *log),                               \
      (shader, capacity, length, log))                                                                                 \
    V(GetShaderiv, (GLuint shader, GLenum name, GLint *out), (shader, name, out))                                      \
    R(GLuint, GetUniformBlockIndex, (GLuint program, const GLchar *name), (program, name))                             \
    R(GLint, GetUniformLocation, (GLuint program, const GLchar *name), (program, name))                                \
    V(LinkProgram, (GLuint program), (program))                                                                        \
    V(PixelStorei, (GLenum name, GLint value), (name, value))                                                          \
    V(ReadBuffer, (GLenum source), (source))                                                                           \
    V(ReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels),         \
      (x, y, width, height, format, type, pixels))                                                                     \
    V(RenderbufferStorageMultisample,                                                                                  \
      (GLenum target, GLsizei samples, GLenum format, GLsizei width, GLsizei height),                                  \
      (target, samples, format, width, height))                                                                        \
    V(SamplerParameteri, (GLuint sampler, GLenum name, GLint value), (sampler, name, value))                           \
    V(Scissor, (GLint x, GLint y, GLsizei width, GLsizei height), (x, y, width, height))                               \
    V(ShaderSource, (GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths),                \
      (shader, count, strings, lengths))                                                                               \
    V(StencilFunc, (GLenum function, GLint reference, GLuint mask), (function, reference, mask))                           V(StencilOp, (GLenum failed, GLenum hidden, GLenum drawn), (failed, hidden, drawn))                                    V(TexImage2D,                                                                                                      \
      (GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height, GLint border, GLenum format,         \
       GLenum type, const void *pixels),                                                                               \
      (target, level, internal, width, height, border, format, type, pixels))                                          \
    V(CompressedTexImage2D, (GLenum target, GLint level, GLenum format, GLsizei width, GLsizei height, GLint border,   \
        GLsizei size, const void *pixels), (target, level, format, width, height, border, size, pixels))              \
    V(TexParameteri, (GLenum target, GLenum name, GLint value), (target, name, value))                                 \
    V(TexSubImage2D,                                                                                                   \
      (GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,        \
       const void *pixels),                                                                                            \
      (target, level, x, y, width, height, format, type, pixels))                                                      \
    V(Uniform1i, (GLint location, GLint value), (location, value))                                                     \
    V(UniformBlockBinding, (GLuint program, GLuint block, GLuint binding), (program, block, binding))                  \
    V(UseProgram, (GLuint program), (program))                                                                         \
    V(VertexAttribDivisor, (GLuint index, GLuint divisor), (index, divisor))                                           \
    V(VertexAttribIPointer, (GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer),              \
      (index, size, type, stride, pointer))                                                                            \
    V(VertexAttribPointer,                                                                                             \
      (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer),              \
      (index, size, type, normalized, stride, pointer))                                                                \
    V(Viewport, (GLint x, GLint y, GLsizei width, GLsizei height), (x, y, width, height))

#if defined(__wasm__) || defined(__ANDROID__)

#define TIDE_GL_DECLARE_V(name, parameters, arguments) void gl##name parameters;
#define TIDE_GL_DECLARE_R(type, name, parameters, arguments) type gl##name parameters;
TIDE_GL_FUNCTIONS(TIDE_GL_DECLARE_V, TIDE_GL_DECLARE_R)
#undef TIDE_GL_DECLARE_V
#undef TIDE_GL_DECLARE_R

// OpenGL ES 3's and WebGL 2's alone (desktop has it from OpenGL 4.2): what a
// renderbuffer of a format can be, of which the renderer asks its sample
// counts (GL_SAMPLES: the most first). OpenGL 3.3 has GL_MAX_SAMPLES for
// every format it draws into.
void glGetInternalformativ(GLenum target, GLenum format, GLenum name, GLsizei count, GLint *out);

#else

typedef uint64_t GLuint64;

enum {
    GL_QUERY_COUNTER_BITS = 0x8864,
    GL_QUERY_RESULT = 0x8866,
    GL_QUERY_RESULT_AVAILABLE = 0x8867,
    GL_TIME_ELAPSED = 0x88BF,
};

// Timer queries are core in OpenGL 3.3, but aren't in OpenGL ES 3 or WebGL 2.
// Keep their functions separate from the imports those two share.
#define TIDE_GL_TIMER_FUNCTIONS(V, R)                                                                                  \
    V(BeginQuery, (GLenum target, GLuint query), (target, query))                                                      \
    V(DeleteQueries, (GLsizei count, const GLuint *queries), (count, queries))                                        \
    V(EndQuery, (GLenum target), (target))                                                                             \
    V(GenQueries, (GLsizei count, GLuint *queries), (count, queries))                                                  \
    V(GetQueryiv, (GLenum target, GLenum name, GLint *out), (target, name, out))                                        \
    V(GetQueryObjectiv, (GLuint query, GLenum name, GLint *out), (query, name, out))                                   \
    V(GetQueryObjectui64v, (GLuint query, GLenum name, GLuint64 *out), (query, name, out))

// Windows' OpenGL functions are stdcall, which only 32-bit x86 tells from
// the default.
#ifdef _WIN32
#define TIDE_GL_CALL __stdcall
#else
#define TIDE_GL_CALL
#endif

#define TIDE_GL_MEMBER_V(name, parameters, arguments) void(TIDE_GL_CALL *name) parameters;
#define TIDE_GL_MEMBER_R(type, name, parameters, arguments) type(TIDE_GL_CALL *name) parameters;
typedef struct tide_gl_functions {
    TIDE_GL_FUNCTIONS(TIDE_GL_MEMBER_V, TIDE_GL_MEMBER_R)
    TIDE_GL_TIMER_FUNCTIONS(TIDE_GL_MEMBER_V, TIDE_GL_MEMBER_R)
} tide_gl_functions;
#undef TIDE_GL_MEMBER_V
#undef TIDE_GL_MEMBER_R

extern tide_gl_functions tide_gl;

// Looks every function up with `find` (glfwGetProcAddress), in the context
// that's current. False, having said which, if the context lacks one.
typedef void (*tide_gl_proc)(void);
bool tide_gl_load(tide_gl_proc (*find)(const char *name));

#define TIDE_GL_CALL_V(name, parameters, arguments)                                                                    \
    static inline void gl##name parameters                                                                             \
    {                                                                                                                  \
        tide_gl.name arguments;                                                                                        \
    }
#define TIDE_GL_CALL_R(type, name, parameters, arguments)                                                              \
    static inline type gl##name parameters                                                                             \
    {                                                                                                                  \
        return tide_gl.name arguments;                                                                                 \
    }
TIDE_GL_FUNCTIONS(TIDE_GL_CALL_V, TIDE_GL_CALL_R)
TIDE_GL_TIMER_FUNCTIONS(TIDE_GL_CALL_V, TIDE_GL_CALL_R)
#undef TIDE_GL_CALL_V
#undef TIDE_GL_CALL_R

#endif
