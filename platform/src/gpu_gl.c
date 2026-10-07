// OpenGL under the renderer (see gpu.h): OpenGL 3.3 on desktop, OpenGL ES 3 on
// Android and WebGL 2 on the web, through gl.h. Its buffers' and textures'
// ids are OpenGL's own names for them.

#include "gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "gl.h"
#include "window.h"

// One GLSL for all three, but for its first lines. OpenGL ES asks for floats
// and ints of 32 bits, which its fragment shaders don't have otherwise.
#if defined(__wasm__)
#define GL_NAME "WebGL 2"
#define GLSL_VERSION "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#elif defined(__ANDROID__)
#define GL_NAME "OpenGL ES 3"
#define GLSL_VERSION "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#else
#define GL_NAME "OpenGL 3.3"
#define GLSL_VERSION "#version 330\n"
#endif

typedef struct gl_pipeline {
    GLuint program, vao;         // No program: a place that's free
    tide_gpu_pipeline_desc desc; // Its slots and attributes, how it blends and tests depth and stencil, and which side it draws
    GLint target_height;         // 0 on the window, its height when drawing top-first into a texture
} gl_pipeline;

typedef struct gl_target {
    GLuint framebuffer, depth; // No framebuffer: a place that's free
    GLuint colors[TIDE_GPU_MAX_COLORS]; // Its color images, in order: none for a target of depth alone
    uint32_t color_count;
    uint32_t drawn; // How many of them the framebuffer's draw buffers name (see draw_into)
    bool bytes;     // Its first color image is 4 bytes a pixel: what's read back
    tide_gpu_target_format format; // What it was made to hold, which the pipelines that draw into it are made for
    int width, height;
} gl_target;

// What's known of a buffer or a texture, by its name.
typedef struct gl_buffer {
    uint8_t kind; // tide_gpu_buffer_kind: WebGL binds a buffer as one kind only, ever
} gl_buffer;

typedef struct gl_texture {
    uint8_t format; // tide_gpu_format
    int32_t width, height;
} gl_texture;

static gl_pipeline *pipelines; // A pipeline's id is 1 + its place, and a target's
static uint32_t pipeline_count;
static gl_target *targets;
static uint32_t target_count;
static gl_buffer *buffers; // By name
static uint32_t buffer_names;
static gl_texture *textures;
static uint32_t texture_names;

static GLuint upload_vao;     // What index buffers are bound in while they're written, in no pipeline's place
static GLuint uniform_buffer; // Every pipeline's uniform block, as the last draw's was set
// How a unit samples the texture it has: the nearest pixel or blended, by
// each tide_gpu_wrap (sampler_for), and, for a target's depth, compared with
// the shader's number and those blended. A texture's own way would be the
// same in every unit that has it.
enum { WRAPS = TIDE_GPU_WRAP_MIRROR + 1, SAMPLE_COMPARED = 2 * WRAPS, SAMPLERS };
static GLuint samplers[SAMPLERS];

static GLuint sampler_for(const bool blended, const tide_gpu_wrap wrap)
{
    return samplers[(blended ? WRAPS : 0) + ((uint32_t)wrap < WRAPS ? (int)wrap : TIDE_GPU_WRAP_CLAMP)];
}
static const gl_pipeline *current;
static GLenum index_type;
static int pass_height; // The pass's target's, in its pixels
static bool pass_texture; // Its rows go from the top, as uploaded textures' do
static gl_target *pass_target; // What the pass draws into: NULL for the window

bool tide_gpu_gl_no_floats;
static GLint pass_stencil; // The number its pipelines' stencil tests compare with (gl_stencil)

// A pipeline's stencil test and what it stores, as OpenGL has them, with the
// pass's number. OpenGL keeps the number with the test, so this is called for
// a new pipeline and for a new number.
static void set_stencil(const gl_pipeline *p)
{
    static const GLenum tests[] = { // By tide_gpu_stencil_test
        GL_ALWAYS, GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL};
    static const GLenum ops[] = {GL_KEEP, GL_REPLACE, GL_INCR, GL_DECR, GL_ZERO, GL_INVERT}; // By tide_gpu_stencil_op
    const uint32_t test = (uint32_t)p->desc.stencil_test, op = (uint32_t)p->desc.stencil_op;
    if (test == TIDE_GPU_STENCIL_ALWAYS && op == TIDE_GPU_STENCIL_KEEP) {
        glDisable(GL_STENCIL_TEST);
        return;
    }
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(tests[test < sizeof tests / sizeof tests[0] ? test : 0], pass_stencil, 0xFFu);
    // Only where the draw draws: a pixel that fails either test keeps its number
    glStencilOp(GL_KEEP, GL_KEEP, ops[op < sizeof ops / sizeof ops[0] ? op : 0]);
}

#if !defined(__wasm__) && !defined(__ANDROID__)
enum { GL_TIME_SAMPLES = 4 };
static struct {
    GLuint queries[GL_TIME_SAMPLES];
    unsigned first, count;
    bool checked, supported, active;
} timing;
#endif

static void gl_time_begin(void)
{
#if !defined(__wasm__) && !defined(__ANDROID__)
    if (!timing.checked) {
        timing.checked = true;
        if (!tide_gl.BeginQuery || !tide_gl.EndQuery || !tide_gl.GenQueries || !tide_gl.DeleteQueries ||
            !tide_gl.GetQueryiv || !tide_gl.GetQueryObjectiv || !tide_gl.GetQueryObjectui64v) return;
        GLint bits = 0;
        glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
        if (!bits) return; // A core query can have no useful timer counter.
        glGenQueries(GL_TIME_SAMPLES, timing.queries);
        timing.supported = true;
    }
    // Never reuse a query whose result hasn't been read: a busy GPU simply
    // skips this frame's sample instead of holding up the CPU.
    if (!timing.supported || timing.active || timing.count == GL_TIME_SAMPLES) return;
    glBeginQuery(GL_TIME_ELAPSED, timing.queries[(timing.first + timing.count) % GL_TIME_SAMPLES]);
    timing.active = true;
#endif
}

static void gl_time_end(void)
{
#if !defined(__wasm__) && !defined(__ANDROID__)
    if (!timing.active) return;
    glEndQuery(GL_TIME_ELAPSED);
    timing.active = false;
    timing.count++;
#endif
}

static double gl_time_read(void)
{
#if !defined(__wasm__) && !defined(__ANDROID__)
    if (!timing.count) return -1.0;
    const GLuint query = timing.queries[timing.first];
    GLint available = GL_FALSE;
    glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) return -1.0;
    GLuint64 nanoseconds = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
    timing.first = (timing.first + 1u) % GL_TIME_SAMPLES;
    timing.count--;
    return (double)nanoseconds / 1000000.0;
#else
    return -1.0;
#endif
}

static void gl_time_reset(void)
{
#if !defined(__wasm__) && !defined(__ANDROID__)
    if (timing.active) glEndQuery(GL_TIME_ELAPSED);
    if (timing.supported) glDeleteQueries(GL_TIME_SAMPLES, timing.queries);
    memset(&timing, 0, sizeof timing);
#endif
}

// Room for names up to `name` in an array of `size`-byte elements, zeroed.
static void *room(void *array, uint32_t *count, const uint32_t name, const size_t size)
{
    if (name < *count) return array;
    uint32_t grown = *count ? *count : 64u;
    while (grown <= name) grown *= 2u;
    array = tide_realloc(array, *count * size, grown * size);
    memset((char *)array + *count * size, 0, (grown - *count) * size);
    *count = grown;
    return array;
}

// Binds a buffer where its kind goes, and says where that is.
static GLenum bind_buffer(const GLuint buffer)
{
    if (buffers[buffer].kind == TIDE_GPU_VERTICES) {
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        return GL_ARRAY_BUFFER;
    }
    // A vertex array keeps its index buffer: not a pipeline's
    if (!upload_vao) glGenVertexArrays(1, &upload_vao);
    glBindVertexArray(upload_vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
    return GL_ELEMENT_ARRAY_BUFFER;
}

static tide_gpu_id gl_buffer_make(const tide_gpu_buffer_kind kind, const size_t size, const void *data)
{
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    buffers = room(buffers, &buffer_names, buffer, sizeof(gl_buffer));
    buffers[buffer].kind = (uint8_t)kind;
    glBufferData(bind_buffer(buffer), (GLsizeiptr)size, data, data ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
    return buffer;
}

static void gl_buffer_write(const tide_gpu_id buffer, const size_t offset, const void *data, const size_t size)
{
    glBufferSubData(bind_buffer(buffer), (GLintptr)offset, (GLsizeiptr)size, data);
}

static void gl_buffer_free(const tide_gpu_id buffer)
{
    glDeleteBuffers(1, &buffer);
}

static void texture_pixels(const gl_texture *t, const void *pixels, const bool again)
{
    const bool red = t->format != TIDE_GPU_RGBA8, halves = t->format == TIDE_GPU_R16F;
    const GLenum type = halves ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // Rows of any width, one after the other
    if (again) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t->width, t->height, red ? GL_RED : GL_RGBA, type, pixels);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, halves ? GL_R16F : red ? GL_R8 : GL_RGBA8, t->width, t->height, 0,
                     red ? GL_RED : GL_RGBA, type, pixels);
    }
}

// Binds a texture to write it, in a unit past the ones pipelines sample, so
// the textures a draw was given stay in theirs.
static void bind_texture(const GLuint texture)
{
    glActiveTexture(GL_TEXTURE0 + TIDE_GPU_MAX_TEXTURES);
    glBindTexture(GL_TEXTURE_2D, texture);
}

static tide_gpu_id gl_texture_make(const tide_gpu_format format, const int width, const int height, const void *pixels)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    textures = room(textures, &texture_names, texture, sizeof(gl_texture));
    textures[texture] = (gl_texture){.format = (uint8_t)format, .width = width, .height = height};
    bind_texture(texture);
    texture_pixels(&textures[texture], pixels, false);
    // Its own way of being sampled, which a unit's sampler stands in for (gl_sample)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return texture;
}

static void gl_texture_write(const tide_gpu_id texture, const void *pixels)
{
    bind_texture(texture);
    texture_pixels(&textures[texture], pixels, true);
}

static void gl_texture_free(const tide_gpu_id texture)
{
    glDeleteTextures(1, &texture);
}

static GLuint compile(const char *name, const GLenum kind, const char *source)
{
    const GLuint shader = glCreateShader(kind);
    // OpenGL rasterizes from the bottom. A target's texture must instead have
    // its top row at v = 0, just like an uploaded image. Flip only the final
    // vertex position there, and give fragment code its original coordinates
    // back, so Pixel.position and shaders using gl_FragCoord keep working.
    const bool vertex = kind == GL_VERTEX_SHADER;
    char *fragment = NULL;
    if (!vertex) {
        const char *from = "gl_FragCoord", *to = "tide_gpu_coord";
        size_t count = 0;
        for (const char *at = source; (at = strstr(at, from)); at += strlen(from)) count++;
        fragment = tide_alloc(strlen(source) + count * (strlen(to) - strlen(from)) + 1u);
        char *out = fragment;
        const char *at = source, *next;
        while ((next = strstr(at, from))) {
            const size_t bytes = (size_t)(next - at);
            memcpy(out, at, bytes);
            out += bytes;
            memcpy(out, to, strlen(to));
            out += strlen(to);
            at = next + strlen(from);
        }
        strcpy(out, at);
    }
    const char *const sources[] = {
        GLSL_VERSION,
        vertex ? "uniform int tide_gpu_height;\n#define main tide_gpu_main\n"
               : "uniform int tide_gpu_height;\nvec4 tide_gpu_coord;\n#define main tide_gpu_main\n",
        vertex ? source : fragment,
        vertex ? "\n#undef main\nvoid main() { tide_gpu_main(); if (tide_gpu_height > 0) gl_Position.y = -gl_Position.y; }\n"
               : "\n#undef main\nvoid main() { tide_gpu_coord = gl_FragCoord; "
                 "if (tide_gpu_height > 0) tide_gpu_coord.y = float(tide_gpu_height) - tide_gpu_coord.y; tide_gpu_main(); }\n",
    };
    glShaderSource(shader, 4, sources, NULL);
    free(fragment);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        fprintf(stderr, "tide: the %s shader of %s didn't compile:\n%s\n",
                kind == GL_VERTEX_SHADER ? "vertex" : "fragment", name, log);
        abort();
    }
    return shader;
}

static tide_gpu_id gl_pipeline_make(const tide_gpu_pipeline_desc *desc)
{
    const GLuint vertex = compile(desc->name, GL_VERTEX_SHADER, desc->glsl_vertex);
    const GLuint fragment = compile(desc->name, GL_FRAGMENT_SHADER, desc->glsl_fragment);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetProgramInfoLog(program, sizeof log, NULL, log);
        fprintf(stderr, "tide: the shaders of %s didn't link:\n%s\n", desc->name, log);
        abort();
    }
    glDeleteShader(vertex); // The program keeps them
    glDeleteShader(fragment);
    // Its uniforms are in the uniform buffer, and each of its textures in the unit that's its place
    const GLuint block = glGetUniformBlockIndex(program, "uniforms");
    if (block != GL_INVALID_INDEX) glUniformBlockBinding(program, block, 0);
    if (desc->texture_count) glUseProgram(program);
    for (uint32_t i = 0; i < desc->texture_count; i++) {
        char name[16];
        snprintf(name, sizeof name, "pixels%u", (unsigned)i);
        glUniform1i(glGetUniformLocation(program, name), (GLint)i); // Nowhere, for one its shaders don't sample
    }

    uint32_t place = 0;
    while (place < pipeline_count && pipelines[place].program) place++;
    if (place == pipeline_count) {
        pipelines = tide_realloc(pipelines, pipeline_count * sizeof(gl_pipeline),
                                 (pipeline_count + 1u) * sizeof(gl_pipeline));
        pipeline_count++;
    }
    gl_pipeline *p = &pipelines[place];
    *p = (gl_pipeline){.program = program, .desc = *desc,
                      .target_height = glGetUniformLocation(program, "tide_gpu_height")};
    glGenVertexArrays(1, &p->vao);
    glBindVertexArray(p->vao);
    for (uint32_t i = 0; i < desc->attribute_count; i++) {
        glEnableVertexAttribArray(i);
        if (desc->slots[desc->attributes[i].slot].per_instance) glVertexAttribDivisor(i, 1);
    }
    current = NULL; // The pipelines may have moved
    return place + 1u;
}

static void gl_pipeline_free(const tide_gpu_id pipeline)
{
    gl_pipeline *p = &pipelines[pipeline - 1u];
    glDeleteProgram(p->program);
    glDeleteVertexArrays(1, &p->vao);
    *p = (gl_pipeline){0};
    current = NULL;
}

// A texture of four floats of 16 bits a pixel, for a target to draw into: no
// pixels of ours go into one.
static GLuint float_texture(const int width, const int height)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    textures = room(textures, &texture_names, texture, sizeof(gl_texture));
    textures[texture] = (gl_texture){.format = TIDE_GPU_RGBA8, .width = width, .height = height};
    bind_texture(texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return texture;
}

// Whether this OpenGL draws into floats. OpenGL 3.3 does, and OpenGL ES 3.2;
// an older OpenGL ES 3 and WebGL 2 do where they have it as an extension,
// which adds no function (the page asks WebGL for it as it makes the
// context), so it's found out by making a framebuffer of them, once. Such a
// texture is blended between pixels on all of them.
static bool gl_float_targets(void)
{
    static int found; // 0 until it's known, then 1 for floats and -1 for none
    if (tide_gpu_gl_no_floats) return false;
    if (!found) {
        GLuint framebuffer = 0;
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        const GLuint texture = float_texture(1, 1);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        found = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? 1 : -1;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture);
    }
    return found > 0;
}

// Which of the bound target's color images the draws that follow write: its
// first `count`, in order. A fragment shader's output with no image in its
// place goes nowhere, but an image with no output in its place would get
// anything at all (and WebGL refuses the draw), so the images past a
// pipeline's outputs are taken out of the draw buffers.
static void draw_into(gl_target *t, const uint32_t count)
{
    if (t->drawn == count) return;
    GLenum buffers[TIDE_GPU_MAX_COLORS];
    for (uint32_t i = 0; i < t->color_count; i++) buffers[i] = i < count ? GL_COLOR_ATTACHMENT0 + i : GL_NONE;
    if (t->color_count) glDrawBuffers((GLsizei)t->color_count, buffers);
    t->drawn = count;
}

static tide_gpu_id gl_target_make(const tide_gpu_target_format format, const int width, const int height)
{
    uint32_t place = 0;
    while (place < target_count && targets[place].framebuffer) place++;
    if (place == target_count) {
        targets = tide_realloc(targets, target_count * sizeof(gl_target), (target_count + 1u) * sizeof(gl_target));
        target_count++;
    }
    const uint32_t colors = tide_gpu_target_colors(format);
    bool floats = false;
    for (uint32_t i = 0; i < colors; i++) floats |= tide_gpu_target_color(format, i) == TIDE_GPU_COLOR_FLOAT;
    if (floats && !gl_float_targets()) {
        static bool said;
        if (!said) {
            fprintf(stderr, "tide: this device's " GL_NAME " can't draw into float images: a render target's "
                            "Format.Rgba16f image is 4 bytes a pixel here, and keeps what's drawn into it from 0 to 1\n");
        }
        said = true;
        floats = false;
    }
    gl_target *t = &targets[place]; // After the float images were looked for, which makes textures of its own
    *t = (gl_target){.width = width, .height = height, .format = format, .color_count = colors,
                     .drawn = colors ? 1u : 0u,
                     .bytes = colors && !(floats && tide_gpu_target_color(format, 0) == TIDE_GPU_COLOR_FLOAT)};
    glGenFramebuffers(1, &t->framebuffer);
    for (uint32_t i = 0; i < colors; i++) {
        t->colors[i] = floats && tide_gpu_target_color(format, i) == TIDE_GPU_COLOR_FLOAT
            ? float_texture(width, height) : gl_texture_make(TIDE_GPU_RGBA8, width, height, NULL);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);
    for (uint32_t i = 0; i < colors; i++) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, t->colors[i], 0);
    }
    if (!colors) { // Nothing to draw colors into, or to read them from: OpenGL 3.3 wants that said
        glDrawBuffers(1, (const GLenum[]){GL_NONE});
        glReadBuffer(GL_NONE);
    }
    // Its depth is a texture too, which shaders read, with 8 bits of stencil beside it
    glGenTextures(1, &t->depth);
    bind_texture(t->depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, t->depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "tide: no framebuffer to render offscreen in\n");
        abort();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return place + 1u;
}

// A target is textures of its size.
static int gl_target_limit(void)
{
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    return limit > 0 ? limit : 2048; // What every OpenGL ES 3 has, at least
}

static void gl_target_free(const tide_gpu_id target)
{
    gl_target *t = &targets[target - 1u];
    glDeleteFramebuffers(1, &t->framebuffer);
    for (uint32_t i = 0; i < t->color_count; i++) gl_texture_free(t->colors[i]);
    glDeleteTextures(1, &t->depth);
    *t = (gl_target){0};
}

static tide_gpu_id gl_target_texture(const tide_gpu_id target, const uint32_t color)
{
    const gl_target *t = &targets[target - 1u];
    return color < t->color_count ? t->colors[color] : 0;
}

static tide_gpu_id gl_target_depth(const tide_gpu_id target)
{
    return targets[target - 1u].depth;
}

static void gl_target_read(const tide_gpu_id target, uint8_t *rgba)
{
    const gl_target *t = &targets[target - 1u];
    if (!t->bytes) { // Depth alone, or floats first: no bytes to read
        memset(rgba, 0, (size_t)t->width * (size_t)t->height * 4u);
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, t->width, t->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // The vertex wrapper draws the top row into OpenGL's first row.
}

static void gl_begin(const tide_gpu_id target, const bool clear)
{
    int width, height;
    bool color = true;
    pass_target = target ? &targets[target - 1u] : NULL;
    if (target) {
        gl_target *t = pass_target;
        glBindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);
        width = t->width, height = t->height;
        color = t->color_count != 0;
        if (clear) draw_into(t, t->color_count); // A clear reaches the images the draw buffers name: all of them
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        tide_window_pixel_size(&width, &height);
    }
    pass_height = height;
    pass_texture = target != 0;
    // What a pass draws with is set here, whatever drew before: another
    // target's pass or, on the web, the program this one took over from.
    glViewport(0, 0, width, height);
    glDisable(GL_CULL_FACE); // Either side of a triangle draws, until a pipeline says otherwise (gl_use)
    glDisable(GL_STENCIL_TEST); // ...and no stencil test
    pass_stencil = 0;
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, width, height);
    if (!uniform_buffer) { // The first pass: what every pipeline draws with
        glGenBuffers(1, &uniform_buffer);
        glBindBuffer(GL_UNIFORM_BUFFER, uniform_buffer);
        glBufferData(GL_UNIFORM_BUFFER, TIDE_GPU_MAX_UNIFORMS, NULL, GL_DYNAMIC_DRAW);
        static const GLint wraps[WRAPS] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT}; // By tide_gpu_wrap
        glGenSamplers(SAMPLERS, samplers);
        for (int blended = 0; blended < 2; blended++) {
            const GLint filter = blended ? GL_LINEAR : GL_NEAREST;
            for (int wrap = 0; wrap < WRAPS; wrap++) {
                const GLuint sampler = sampler_for(blended != 0, (tide_gpu_wrap)wrap);
                glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, filter);
                glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, filter);
                glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, wraps[wrap]);
                glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, wraps[wrap]);
            }
        }
        // 1 where the depth that's stored isn't nearer than the shader's number, the test a mesh passes to draw
        // there: what that gives at the pixels around is blended, and past the edges it's the pixels at the edge
        glSamplerParameteri(samplers[SAMPLE_COMPARED], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(samplers[SAMPLE_COMPARED], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(samplers[SAMPLE_COMPARED], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(samplers[SAMPLE_COMPARED], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(samplers[SAMPLE_COMPARED], GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(samplers[SAMPLE_COMPARED], GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniform_buffer);
    current = NULL;
    if (clear) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glDepthMask(GL_TRUE); // A clear only reaches the depth that's written: the last pipeline may have read it alone
        glClear(color ? GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT
                      : GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
}

static void gl_end(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    pass_target = NULL;
}

static void gl_scissor(const int x, const int y, const int width, const int height)
{
    glScissor(x, pass_texture ? y : pass_height - (y + height), width, height);
}

static void gl_use(const tide_gpu_id pipeline)
{
    current = &pipelines[pipeline - 1u];
    // A pipeline is for one kind of target, as WebGPU has it: OpenGL would draw all the same, with the
    // colors of one kind into another's images
    const tide_gpu_target_format kind = pass_target ? pass_target->format : TIDE_GPU_TARGET_COLOR;
    if (current->desc.into != kind) {
        fprintf(stderr, "tide: the pipeline of %s is for targets that hold something else than the one it draws into "
                        "(tide_gpu_pipeline_desc.into)\n", current->desc.name ? current->desc.name : "a shader");
        abort();
    }
    glUseProgram(current->program);
    glUniform1i(current->target_height, pass_texture ? pass_height : 0);
    glBindVertexArray(current->vao);
    // Its outputs go to the target's color images in order, and the images past them are left alone
    if (pass_target && pass_target->color_count > 1) {
        const uint32_t outputs = current->desc.outputs ? current->desc.outputs : 1u;
        draw_into(pass_target, outputs < pass_target->color_count ? outputs : pass_target->color_count);
    }
    switch (current->desc.blend) {
    case TIDE_GPU_BLEND_ALPHA:
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case TIDE_GPU_BLEND_ADD:
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
        break;
    case TIDE_GPU_BLEND_MULTIPLY:
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
        break;
    default:
        glDisable(GL_BLEND);
        break;
    }
    // OpenGL only writes depth where it tests it
    if (current->desc.depth == TIDE_GPU_DEPTH_NONE) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(current->desc.depth == TIDE_GPU_DEPTH_RESET ? GL_ALWAYS : GL_LEQUAL);
        glDepthMask(current->desc.depth == TIDE_GPU_DEPTH_READ ? GL_FALSE : GL_TRUE);
    }
    if (current->desc.cull == TIDE_GPU_CULL_NONE) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(current->desc.cull == TIDE_GPU_CULL_BACK ? GL_BACK : GL_FRONT);
        // A triangle's front goes clockwise as it's seen. A target's pass is
        // drawn the other way up (compile), where the same triangle goes
        // counterclockwise for OpenGL
        glFrontFace(pass_texture ? GL_CCW : GL_CW);
    }
    set_stencil(current);
}

static void gl_stencil(const uint32_t value)
{
    pass_stencil = (GLint)(value < 255u ? value : 255u);
    if (current) set_stencil(current);
}

static void gl_uniforms(const void *data, const size_t size)
{
    glBindBuffer(GL_UNIFORM_BUFFER, uniform_buffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)size, data);
}

static void gl_sample(const uint32_t slot, const tide_gpu_id texture, const bool blended, const tide_gpu_wrap wrap)
{
    // A target's depth is whole only where its nearest pixel is read, unless it's compared
    const uint8_t read = current->desc.reads[slot];
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindSampler(slot, read == TIDE_GPU_READ_COMPARE ? samplers[SAMPLE_COMPARED] :
                        read == TIDE_GPU_READ_DEPTH ? sampler_for(false, TIDE_GPU_WRAP_CLAMP) : sampler_for(blended, wrap));
}

static void gl_vertices(const uint32_t slot, const tide_gpu_id buffer, const size_t offset)
{
    static const GLint counts[] = {1, 2, 3, 4, 4, 1, 2, 3, 4};
    const tide_gpu_pipeline_desc *d = &current->desc;
    glBindVertexArray(current->vao); // Writing an index buffer binds another
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    for (uint32_t i = 0; i < d->attribute_count; i++) {
        if (d->attributes[i].slot != slot) continue;
        const tide_gpu_attribute_format format = d->attributes[i].format;
        const GLsizei stride = (GLsizei)d->slots[slot].stride;
        const void *at = (const void *)(offset + d->attributes[i].offset);
        if (format >= TIDE_GPU_INT) { // Ints stay ints: the other call would hand the shader floats
            glVertexAttribIPointer(i, counts[format], GL_INT, stride, at);
        } else {
            const bool bytes = format == TIDE_GPU_BYTES4;
            glVertexAttribPointer(i, counts[format], bytes ? GL_UNSIGNED_BYTE : GL_FLOAT, bytes, stride, at);
        }
    }
}

static void gl_indices(const tide_gpu_id buffer, const bool wide)
{
    glBindVertexArray(current->vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
    index_type = wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
}

static void gl_draw(const uint32_t first, const uint32_t count, const uint32_t instances)
{
    glBindVertexArray(current->vao);
    glDrawArraysInstanced(GL_TRIANGLES, (GLint)first, (GLsizei)count, (GLsizei)instances);
}

static void gl_draw_indexed(const uint32_t count, const uint32_t instances)
{
    glBindVertexArray(current->vao);
    glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)count, index_type, NULL, (GLsizei)instances);
}

// The window's context, which stays.
static uint32_t gl_epoch(void)
{
    return 1;
}

const tide_gpu tide_gpu_gl = {
    .name = GL_NAME,
    .epoch = gl_epoch,
    .time_begin = gl_time_begin,
    .time_end = gl_time_end,
    .time_read = gl_time_read,
    .time_reset = gl_time_reset,
    .buffer = gl_buffer_make,
    .buffer_write = gl_buffer_write,
    .buffer_free = gl_buffer_free,
    .texture = gl_texture_make,
    .texture_write = gl_texture_write,
    .texture_free = gl_texture_free,
    .pipeline = gl_pipeline_make,
    .pipeline_free = gl_pipeline_free,
    .target = gl_target_make,
    .target_limit = gl_target_limit,
    .float_targets = gl_float_targets,
    .target_texture = gl_target_texture,
    .target_depth = gl_target_depth,
    .target_free = gl_target_free,
    .target_read = gl_target_read,
    .begin = gl_begin,
    .end = gl_end,
    .scissor = gl_scissor,
    .use = gl_use,
    .stencil = gl_stencil,
    .uniforms = gl_uniforms,
    .sample = gl_sample,
    .vertices = gl_vertices,
    .indices = gl_indices,
    .draw = gl_draw,
    .draw_indexed = gl_draw_indexed,
};
