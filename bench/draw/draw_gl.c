// versus_draw's workloads by hand in OpenGL 3.3 core (see versus_draw.h): a
// framebuffer of the target's size, a static quad and cube, one instance
// buffer the CPU maps and writes each frame (see gl_instances), and a program
// and a vertex array for each workload, set up once. A frame binds
// those, clears, draws once and reads a pixel back.
//
// Its shaders do what Tide's renderer's do for these shapes (platform/src/
// shaders.h), so the pixels are the same: colors go out times their alpha and
// blend over what's there, a circle's edge is smooth over a pixel, a round
// rect's is by its distance from the edge, and frames are drawn upside down
// (y flipped in clip space) so that their first row is their top, as Tide
// draws its targets, which turns the cubes' front faces counterclockwise.

#include "versus_draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

enum {
    GL_FALSE = 0,
    GL_TRUE = 1,
    GL_ONE = 1,
    GL_TRIANGLES = 0x0004,
    GL_DEPTH_BUFFER_BIT = 0x0100,
    GL_COLOR_BUFFER_BIT = 0x4000,
    GL_LEQUAL = 0x0203,
    GL_ONE_MINUS_SRC_ALPHA = 0x0303,
    GL_BACK = 0x0405,
    GL_CCW = 0x0901,
    GL_CULL_FACE = 0x0B44,
    GL_DEPTH_TEST = 0x0B71,
    GL_BLEND = 0x0BE2,
    GL_PACK_ALIGNMENT = 0x0D05,
    GL_UNPACK_ALIGNMENT = 0x0CF5,
    GL_TEXTURE_2D = 0x0DE1,
    GL_UNSIGNED_BYTE = 0x1401,
    GL_UNSIGNED_SHORT = 0x1403,
    GL_FLOAT = 0x1406,
    GL_VENDOR = 0x1F00,
    GL_RENDERER = 0x1F01,
    GL_VERSION = 0x1F02,
    GL_RGBA = 0x1908,
    GL_NEAREST = 0x2600,
    GL_TEXTURE_MAG_FILTER = 0x2800,
    GL_TEXTURE_MIN_FILTER = 0x2801,
    GL_TEXTURE_WRAP_S = 0x2802,
    GL_TEXTURE_WRAP_T = 0x2803,
    GL_RGBA8 = 0x8058,
    GL_CLAMP_TO_EDGE = 0x812F,
    GL_DEPTH24_STENCIL8 = 0x88F0,
    GL_TEXTURE0 = 0x84C0,
    GL_ARRAY_BUFFER = 0x8892,
    GL_ELEMENT_ARRAY_BUFFER = 0x8893,
    GL_STREAM_DRAW = 0x88E0,
    GL_STATIC_DRAW = 0x88E4,
    GL_FRAGMENT_SHADER = 0x8B30,
    GL_VERTEX_SHADER = 0x8B31,
    GL_COMPILE_STATUS = 0x8B81,
    GL_LINK_STATUS = 0x8B82,
    GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
    GL_COLOR_ATTACHMENT0 = 0x8CE0,
    GL_DEPTH_STENCIL_ATTACHMENT = 0x821A,
    GL_FRAMEBUFFER = 0x8D40,
    GL_RENDERBUFFER = 0x8D41,
    GL_MAP_WRITE_BIT = 0x0002,
    GL_MAP_UNSYNCHRONIZED_BIT = 0x0020,
};

#ifdef _WIN32
#define GL_CALL __stdcall // Windows' OpenGL is stdcall, which only 32-bit x86 tells from the default
#else
#define GL_CALL
#endif

#define GL_FUNCTIONS(X)                                                                                                \
    X(void, ActiveTexture, (GLenum texture))                                                                           \
    X(void, AttachShader, (GLuint program, GLuint shader))                                                             \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                                                \
    X(void, BindFramebuffer, (GLenum target, GLuint framebuffer))                                                      \
    X(void, BindRenderbuffer, (GLenum target, GLuint renderbuffer))                                                    \
    X(void, BindTexture, (GLenum target, GLuint texture))                                                              \
    X(void, BindVertexArray, (GLuint array))                                                                           \
    X(void, BlendFunc, (GLenum source, GLenum destination))                                                            \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage))                               \
    X(GLenum, CheckFramebufferStatus, (GLenum target))                                                                 \
    X(void, Clear, (GLbitfield mask))                                                                                  \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                                  \
    X(void, CompileShader, (GLuint shader))                                                                            \
    X(GLuint, CreateProgram, (void))                                                                                   \
    X(GLuint, CreateShader, (GLenum kind))                                                                             \
    X(void, CullFace, (GLenum mode))                                                                                   \
    X(void, DeleteBuffers, (GLsizei n, const GLuint *buffers))                                                         \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint *framebuffers))                                               \
    X(void, DeleteProgram, (GLuint program))                                                                           \
    X(void, DeleteRenderbuffers, (GLsizei n, const GLuint *renderbuffers))                                             \
    X(void, DeleteShader, (GLuint shader))                                                                             \
    X(void, DeleteTextures, (GLsizei n, const GLuint *textures))                                                       \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint *arrays))                                                     \
    X(void, DepthFunc, (GLenum func))                                                                                  \
    X(void, Disable, (GLenum cap))                                                                                     \
    X(void, DrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei instances))                         \
    X(void, DrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instances))   \
    X(void, Enable, (GLenum cap))                                                                                      \
    X(void, EnableVertexAttribArray, (GLuint index))                                                                   \
    X(void, FramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rbtarget, GLuint renderbuffer))         \
    X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level))    \
    X(void, FrontFace, (GLenum mode))                                                                                  \
    X(void, GenBuffers, (GLsizei n, GLuint *buffers))                                                                  \
    X(void, GenFramebuffers, (GLsizei n, GLuint *framebuffers))                                                        \
    X(void, GenRenderbuffers, (GLsizei n, GLuint *renderbuffers))                                                      \
    X(void, GenTextures, (GLsizei n, GLuint *textures))                                                                \
    X(void, GenVertexArrays, (GLsizei n, GLuint *arrays))                                                              \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei size, GLsizei *length, GLchar *log))                           \
    X(void, GetProgramiv, (GLuint program, GLenum name, GLint *value))                                                 \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei size, GLsizei *length, GLchar *log))                             \
    X(void, GetShaderiv, (GLuint shader, GLenum name, GLint *value))                                                   \
    X(const GLubyte *, GetString, (GLenum name))                                                                       \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar *name))                                                 \
    X(void, LinkProgram, (GLuint program))                                                                             \
    X(void *, MapBufferRange, (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access))                  \
    X(void, PixelStorei, (GLenum name, GLint value))                                                                   \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels))            \
    X(void, RenderbufferStorage, (GLenum target, GLenum format, GLsizei width, GLsizei height))                        \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths))          \
    X(void, TexImage2D, (GLenum target, GLint level, GLint inner, GLsizei w, GLsizei h, GLint border, GLenum format,   \
                         GLenum type, const void *pixels))                                                             \
    X(void, TexParameteri, (GLenum target, GLenum name, GLint value))                                                  \
    X(void, Uniform1i, (GLint location, GLint value))                                                                  \
    X(void, Uniform4f, (GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w))                                   \
    X(void, UniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))              \
    X(GLboolean, UnmapBuffer, (GLenum target))                                                                         \
    X(void, UseProgram, (GLuint program))                                                                              \
    X(void, VertexAttribDivisor, (GLuint index, GLuint divisor))                                                       \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,         \
                                  const void *offset))                                                                 \
    X(void, Viewport, (GLint x, GLint y, GLsizei width, GLsizei height))

#define GL_DECLARE(type, name, parameters) static type(GL_CALL *gl##name) parameters;
GL_FUNCTIONS(GL_DECLARE)
#undef GL_DECLARE

static GLFWwindow *window; // Hidden, for its context alone: frames go into `framebuffer`
static GLuint framebuffer, color, depth, texture;
static GLuint quad, cube_vertices, cube_indices, instances;
static size_t instance_bytes;

typedef struct pass {
    GLuint program, vao;
} pass;
static pass passes[VD_KINDS];

// ---------------------------------------------------------------------------
// Shaders

#define VERSION "#version 330 core\n"

// Logical pixels from the top left to clip space, upside down: x * screen.xy + screen.zw.
#define SCREEN "uniform vec4 screen;\n"

// A 2D shape's quad, its edge `ROOM` pixels further out for what's smooth.
#define SHAPE_VERTEX(ROOM, MORE_IN, MORE_OUT, MORE)                                                                     \
    VERSION "layout(location = 0) in vec2 corner;\n"                                                                   \
            "layout(location = 1) in vec4 shape;\n"                                                                    \
            "layout(location = 2) in vec4 color;\n" MORE_IN SCREEN "out vec2 local;\n"                                 \
            "out vec2 extent;\n"                                                                                       \
            "out vec4 tint;\n" MORE_OUT "void main() {\n"                                                              \
            "    vec2 c = corner * 2.0 - 1.0;\n"                                                                       \
            "    extent = abs(shape.zw);\n"                                                                            \
            "    local = c * (extent + " ROOM ");\n"                                                                   \
            "    tint = color;\n" MORE "    gl_Position = vec4((shape.xy + local) * screen.xy + screen.zw, 0.0, 1.0);\n" \
            "}\n"

static const char rect_vertex[] = SHAPE_VERTEX("0.0", "", "", "");
static const char rect_fragment[] = VERSION "in vec4 tint;\n"
                                            "out vec4 pixel;\n"
                                            "void main() { pixel = vec4(tint.rgb * tint.a, tint.a); }\n";

static const char circle_vertex[] = SHAPE_VERTEX("1.0", "", "", "");
static const char circle_fragment[] = VERSION "in vec2 local;\n"
                                              "in vec2 extent;\n"
                                              "in vec4 tint;\n"
                                              "out vec4 pixel;\n"
                                              "void main() {\n"
                                              "    float cover = clamp(extent.x - length(local) + 0.5, 0.0, 1.0);\n"
                                              "    if (cover <= 0.0) discard;\n"
                                              "    pixel = vec4(tint.rgb * (tint.a * cover), tint.a * cover);\n"
                                              "}\n";

static const char quad_vertex[] = SHAPE_VERTEX("0.0", "", "out vec2 uv;\n", "    uv = corner;\n");
static const char quad_fragment[] = VERSION "in vec2 uv;\n"
                                            "in vec4 tint;\n"
                                            "uniform sampler2D pixels;\n"
                                            "out vec4 pixel;\n"
                                            "void main() { pixel = texture(pixels, uv) * vec4(tint.rgb * tint.a, tint.a); }\n";

// The radius is interpolated as Tide's is, which makes the same pixels
static const char round_vertex[] =
    SHAPE_VERTEX("1.0", "layout(location = 3) in float radius;\n", "out float bend;\n", "    bend = radius;\n");
static const char round_fragment[] = VERSION "in vec2 local;\n"
                                             "in vec2 extent;\n"
                                             "in vec4 tint;\n"
                                             "in float bend;\n"
                                             "out vec4 pixel;\n"
                                             "void main() {\n"
                                             "    vec2 q = abs(local) - extent + bend;\n"
                                             "    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - bend;\n"
                                             "    float cover = clamp(0.5 - d, 0.0, 1.0);\n"
                                             "    if (cover <= 0.0) discard;\n"
                                             "    pixel = vec4(tint.rgb * tint.a, tint.a) * cover;\n"
                                             "}\n";

static const char cube_vertex[] = VERSION "layout(location = 0) in vec3 position;\n"
                                          "layout(location = 1) in vec4 color;\n"
                                          "layout(location = 2) in vec3 place;\n"
                                          "uniform mat4 camera;\n"
                                          "out vec4 tint;\n"
                                          "void main() {\n"
                                          "    tint = color;\n"
                                          "    gl_Position = camera * vec4(position + place, 1.0);\n"
                                          "}\n";
static const char cube_matrix_vertex[] = VERSION "layout(location = 0) in vec3 position;\n"
                                                 "layout(location = 1) in vec4 color;\n"
                                                 "layout(location = 2) in mat4 model;\n"
                                                 "uniform mat4 camera;\n"
                                                 "out vec4 tint;\n"
                                                 "void main() {\n"
                                                 "    tint = color;\n"
                                                 "    gl_Position = camera * (model * vec4(position, 1.0));\n"
                                                 "}\n";
#define cube_fragment rect_fragment

static GLuint compile(const GLenum kind, const char *source)
{
    const GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        fprintf(stderr, "versus_draw: a shader didn't compile:\n%s\n%s\n", log, source);
        abort();
    }
    return shader;
}

static GLuint program(const char *vertex, const char *fragment)
{
    const GLuint p = glCreateProgram(), v = compile(GL_VERTEX_SHADER, vertex), f = compile(GL_FRAGMENT_SHADER, fragment);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "versus_draw: shaders didn't link:\n%s\n", log);
        abort();
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// ---------------------------------------------------------------------------

static void attribute(const GLuint index, const GLint size, const GLenum type, const bool normalized, const size_t stride,
                      const size_t offset, const bool per_instance)
{
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, size, type, normalized ? GL_TRUE : GL_FALSE, (GLsizei)stride, (const void *)offset);
    if (per_instance) glVertexAttribDivisor(index, 1);
}

static bool gl_open(const float camera[16], const uint8_t pixels[2 * 2 * 4], char *device, const size_t device_size)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // Never shown: frames go into a framebuffer of its own
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    window = glfwCreateWindow(VD_WIDTH, VD_HEIGHT, "versus_draw", NULL, NULL);
    if (!window) return false;
    glfwMakeContextCurrent(window);
    bool found = true;
#define GL_LOAD(type, name, parameters)                                                                                \
    gl##name = (type(GL_CALL *) parameters)glfwGetProcAddress("gl" #name);                                             \
    if (!gl##name) {                                                                                                   \
        fprintf(stderr, "versus_draw: this OpenGL has no gl" #name "\n");                                              \
        found = false;                                                                                                 \
    }
    GL_FUNCTIONS(GL_LOAD)
#undef GL_LOAD
    if (!found) {
        glfwDestroyWindow(window);
        window = NULL;
        return false;
    }
    snprintf(device, device_size, "%s, OpenGL %s", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));

    // The target: color, and depth for the cubes, as Tide's targets have it
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, VD_WIDTH, VD_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, VD_WIDTH, VD_HEIGHT);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "versus_draw: no framebuffer to draw in\n");
        return false;
    }

    // The quads' texture, in unit 0, which only the quads' program samples
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Meshes: a quad's two triangles, its top left corner first, and the cube Tide's benchmark draws
    static const float corners[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    static const struct {
        float position[3];
        uint8_t color[4];
    } cube[8] = {
        {{-0.5f, -0.5f, -0.5f}, {255, 0, 0, 255}},   {{0.5f, -0.5f, -0.5f}, {0, 255, 0, 255}},
        {{0.5f, 0.5f, -0.5f}, {0, 0, 255, 255}},     {{-0.5f, 0.5f, -0.5f}, {255, 255, 0, 255}},
        {{-0.5f, -0.5f, 0.5f}, {255, 0, 255, 255}},  {{0.5f, -0.5f, 0.5f}, {0, 255, 255, 255}},
        {{0.5f, 0.5f, 0.5f}, {255, 255, 255, 255}},  {{-0.5f, 0.5f, 0.5f}, {128, 128, 128, 255}},
    };
    static const uint16_t triangles[36] = {0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                           3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    glGenBuffers(1, &quad);
    glBindBuffer(GL_ARRAY_BUFFER, quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
    glGenBuffers(1, &cube_vertices);
    glBindBuffer(GL_ARRAY_BUFFER, cube_vertices);
    glBufferData(GL_ARRAY_BUFFER, sizeof cube, cube, GL_STATIC_DRAW);
    // The instances: room for the biggest workload's
    instance_bytes = (size_t)VD_COUNT * vd_instance_size(VD_CUBE_MATRICES);
    glGenBuffers(1, &instances);
    glBindBuffer(GL_ARRAY_BUFFER, instances);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)instance_bytes, NULL, GL_STREAM_DRAW);

    // Each workload's program and what it reads, which a vertex array keeps
    static const struct {
        const char *vertex, *fragment;
    } shaders[VD_KINDS] = {
        [VD_RECTS] = {rect_vertex, rect_fragment},     [VD_CIRCLES] = {circle_vertex, circle_fragment},
        [VD_QUADS] = {quad_vertex, quad_fragment},     [VD_CUBES] = {cube_vertex, cube_fragment},
        [VD_ROUND_RECTS] = {round_vertex, round_fragment}, [VD_CUBE_MATRICES] = {cube_matrix_vertex, cube_fragment},
    };
    float flipped[16]; // The camera, upside down as the frames are
    memcpy(flipped, camera, sizeof flipped);
    for (int column = 0; column < 4; column++) flipped[column * 4 + 1] = -flipped[column * 4 + 1];
    for (int k = VD_RECTS; k < VD_KINDS; k++) {
        pass *p = &passes[k];
        p->program = program(shaders[k].vertex, shaders[k].fragment);
        glUseProgram(p->program);
        const GLint screen = glGetUniformLocation(p->program, "screen");
        if (screen >= 0) glUniform4f(screen, 2.0f / VD_WIDTH, 2.0f / VD_HEIGHT, -1.0f, -1.0f);
        const GLint at = glGetUniformLocation(p->program, "camera");
        if (at >= 0) glUniformMatrix4fv(at, 1, GL_FALSE, flipped);
        const GLint sampled = glGetUniformLocation(p->program, "pixels");
        if (sampled >= 0) glUniform1i(sampled, 0);
        glGenVertexArrays(1, &p->vao);
        glBindVertexArray(p->vao);
        const size_t stride = vd_instance_size((vd_kind)k);
        if (k == VD_CUBES || k == VD_CUBE_MATRICES) {
            glBindBuffer(GL_ARRAY_BUFFER, cube_vertices);
            attribute(0, 3, GL_FLOAT, false, sizeof cube[0], 0, false);
            attribute(1, 4, GL_UNSIGNED_BYTE, true, sizeof cube[0], 3 * sizeof(float), false);
            glBindBuffer(GL_ARRAY_BUFFER, instances);
            if (k == VD_CUBES) attribute(2, 3, GL_FLOAT, false, stride, 0, true);
            else for (GLuint c = 0; c < 4; c++) attribute(2 + c, 4, GL_FLOAT, false, stride, c * 4u * sizeof(float), true);
            if (!cube_indices) {
                glGenBuffers(1, &cube_indices);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_indices);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof triangles, triangles, GL_STATIC_DRAW);
            } else {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_indices);
            }
        } else {
            glBindBuffer(GL_ARRAY_BUFFER, quad);
            attribute(0, 2, GL_FLOAT, false, 2 * sizeof(float), 0, false);
            glBindBuffer(GL_ARRAY_BUFFER, instances);
            attribute(1, 4, GL_FLOAT, false, stride, 0, true);
            if (k == VD_ROUND_RECTS) {
                attribute(3, 1, GL_FLOAT, false, stride, offsetof(vd_round, radius), true);
                attribute(2, 4, GL_UNSIGNED_BYTE, true, stride, offsetof(vd_round, rgba), true);
            } else {
                attribute(2, 4, GL_UNSIGNED_BYTE, true, stride, offsetof(vd_shape, rgba), true);
            }
        }
    }
    glBindVertexArray(0);

    // What never changes: how colors blend, how depth is tested, which faces are culled
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glViewport(0, 0, VD_WIDTH, VD_HEIGHT);
    return true;
}

// The instance buffer, mapped for writing without waiting for the GPU
// (unsynchronized): each frame waits for its own pixel to be read back, so the
// GPU is done with the last frame's instances by then. (A renderer whose
// frames overlap keeps a ring of these regions and a fence for each.) Orphaning
// the buffer instead (GL_MAP_INVALIDATE_BUFFER_BIT) had NVIDIA's driver hand
// out fresh memory: writing 65,536 rects took 0.33 ms a frame that way here,
// against 0.05 ms unsynchronized (persistent mapping, OpenGL 4.4's, matched it).
static void *gl_instances(const vd_kind kind, const size_t bytes)
{
    (void)kind;
    glBindBuffer(GL_ARRAY_BUFFER, instances);
    return glMapBufferRange(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
}

static void gl_written(const vd_kind kind, const size_t bytes)
{
    (void)kind, (void)bytes;
    glUnmapBuffer(GL_ARRAY_BUFFER);
}

static uint32_t gl_render(const vd_kind kind, const uint32_t count)
{
    const bool deep = kind == VD_CUBES || kind == VD_CUBE_MATRICES;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    if (deep) {
        glDisable(GL_BLEND); // Opaque
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    } else {
        glEnable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
    }
    glClear(deep ? GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT);
    if (count) {
        glUseProgram(passes[kind].program);
        glBindVertexArray(passes[kind].vao);
        if (deep) glDrawElementsInstanced(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, NULL, (GLsizei)count);
        else glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)count);
    }
    uint8_t p[4];
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p); // Its first row is the frame's top
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static bool gl_frame(uint8_t *rgba)
{
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glReadPixels(0, 0, VD_WIDTH, VD_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return true;
}

static void gl_close(void)
{
    for (int k = 0; k < VD_KINDS; k++) {
        if (passes[k].program) glDeleteProgram(passes[k].program);
        if (passes[k].vao) glDeleteVertexArrays(1, &passes[k].vao);
    }
    const GLuint buffers[4] = {quad, cube_vertices, cube_indices, instances};
    glDeleteBuffers(4, buffers);
    glDeleteTextures(1, &texture);
    glDeleteTextures(1, &color);
    glDeleteRenderbuffers(1, &depth);
    glDeleteFramebuffers(1, &framebuffer);
    glfwDestroyWindow(window);
    window = NULL;
}

const vd_api vd_gl = {
    .name = "OpenGL 3.3",
    .api = "OpenGL 3.3 core",
    .open = gl_open,
    .instances = gl_instances,
    .written = gl_written,
    .render = gl_render,
    .frame = gl_frame,
    .close = gl_close,
};
