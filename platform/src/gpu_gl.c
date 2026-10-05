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
    tide_gpu_pipeline_desc desc; // Its slots and attributes, and how it blends and tests depth
    GLint target_height;         // 0 on the window, its height when drawing top-first into a texture
} gl_pipeline;

typedef struct gl_target {
    GLuint framebuffer, color, depth; // No framebuffer: a place that's free
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
// How a unit samples the texture it has: the nearest pixel, or blended. A
// texture's own way would be the same in every unit that has it.
static GLuint samplers[2];
static const gl_pipeline *current;
static GLenum index_type;
static int pass_height; // The pass's target's, in its pixels
static bool pass_texture; // Its rows go from the top, as uploaded textures' do

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
    const bool red = t->format == TIDE_GPU_R8;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // Rows of any width, one after the other
    if (again) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t->width, t->height, red ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, red ? GL_R8 : GL_RGBA8, t->width, t->height, 0, red ? GL_RED : GL_RGBA,
                     GL_UNSIGNED_BYTE, pixels);
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

static tide_gpu_id gl_target_make(const int width, const int height)
{
    uint32_t place = 0;
    while (place < target_count && targets[place].framebuffer) place++;
    if (place == target_count) {
        targets = tide_realloc(targets, target_count * sizeof(gl_target), (target_count + 1u) * sizeof(gl_target));
        target_count++;
    }
    gl_target *t = &targets[place];
    *t = (gl_target){.width = width, .height = height};
    glGenFramebuffers(1, &t->framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);
    t->color = gl_texture_make(TIDE_GPU_RGBA8, width, height, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->color, 0);
    glGenRenderbuffers(1, &t->depth);
    glBindRenderbuffer(GL_RENDERBUFFER, t->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t->depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "tide: no framebuffer to render offscreen in\n");
        abort();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return place + 1u;
}

// A target is a texture and a renderbuffer of its size: the smaller of the two limits.
static int gl_target_limit(void)
{
    GLint texture = 0, renderbuffer = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &renderbuffer);
    const GLint limit = texture < renderbuffer ? texture : renderbuffer;
    return limit > 0 ? limit : 2048; // What every OpenGL ES 3 has, at least
}

static void gl_target_free(const tide_gpu_id target)
{
    gl_target *t = &targets[target - 1u];
    glDeleteFramebuffers(1, &t->framebuffer);
    gl_texture_free(t->color);
    glDeleteRenderbuffers(1, &t->depth);
    *t = (gl_target){0};
}

static tide_gpu_id gl_target_texture(const tide_gpu_id target)
{
    return targets[target - 1u].color;
}

static void gl_target_read(const tide_gpu_id target, uint8_t *rgba)
{
    const gl_target *t = &targets[target - 1u];
    glBindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, t->width, t->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // The vertex wrapper draws the top row into OpenGL's first row.
}

static void gl_begin(const tide_gpu_id target, const bool clear)
{
    int width, height;
    if (target) {
        const gl_target *t = &targets[target - 1u];
        glBindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);
        width = t->width, height = t->height;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        tide_window_pixel_size(&width, &height);
    }
    pass_height = height;
    pass_texture = target != 0;
    // What a pass draws with is set here, whatever drew before: another
    // target's pass or, on the web, the program this one took over from.
    glViewport(0, 0, width, height);
    glDisable(GL_CULL_FACE); // Either side of a triangle draws
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, width, height);
    if (!uniform_buffer) { // The first pass: what every pipeline draws with
        glGenBuffers(1, &uniform_buffer);
        glBindBuffer(GL_UNIFORM_BUFFER, uniform_buffer);
        glBufferData(GL_UNIFORM_BUFFER, TIDE_GPU_MAX_UNIFORMS, NULL, GL_DYNAMIC_DRAW);
        glGenSamplers(2, samplers);
        for (int blended = 0; blended < 2; blended++) {
            const GLint filter = blended ? GL_LINEAR : GL_NEAREST;
            glSamplerParameteri(samplers[blended], GL_TEXTURE_MIN_FILTER, filter);
            glSamplerParameteri(samplers[blended], GL_TEXTURE_MAG_FILTER, filter);
            glSamplerParameteri(samplers[blended], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glSamplerParameteri(samplers[blended], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniform_buffer);
    current = NULL;
    if (clear) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

static void gl_end(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void gl_scissor(const int x, const int y, const int width, const int height)
{
    glScissor(x, pass_texture ? y : pass_height - (y + height), width, height);
}

static void gl_use(const tide_gpu_id pipeline)
{
    current = &pipelines[pipeline - 1u];
    glUseProgram(current->program);
    glUniform1i(current->target_height, pass_texture ? pass_height : 0);
    glBindVertexArray(current->vao);
    switch (current->desc.blend) {
    case TIDE_GPU_BLEND_ALPHA:
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case TIDE_GPU_BLEND_ADD:
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
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
        glDepthFunc(current->desc.depth == TIDE_GPU_DEPTH_TEST ? GL_LEQUAL : GL_ALWAYS);
    }
}

static void gl_uniforms(const void *data, const size_t size)
{
    glBindBuffer(GL_UNIFORM_BUFFER, uniform_buffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)size, data);
}

static void gl_sample(const uint32_t slot, const tide_gpu_id texture, const bool blended)
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindSampler(slot, samplers[blended]);
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
    .target_texture = gl_target_texture,
    .target_free = gl_target_free,
    .target_read = gl_target_read,
    .begin = gl_begin,
    .end = gl_end,
    .scissor = gl_scissor,
    .use = gl_use,
    .uniforms = gl_uniforms,
    .sample = gl_sample,
    .vertices = gl_vertices,
    .indices = gl_indices,
    .draw = gl_draw,
    .draw_indexed = gl_draw_indexed,
};
