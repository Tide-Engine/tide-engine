#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/color.h"
#include "tide/math.h"

// Immediate-mode drawing: Tide's Draw2D and Draw3D APIs record commands into a draw
// list, and a renderer plays them back (tide_platform_draw, on OpenGL or WebGPU). The list
// knows nothing about any renderer, so replacing one doesn't touch game code.
//
// Generated code calls the tide_draw_* functions below (and tide/mesh.h's),
// and so does a game's C that a view hands its list (a `DrawList` parameter).
//
// Positions and sizes are in world units, with y up. The camera decides where
// they land on screen. Commands draw in the order they were recorded. The
// window starts black each frame; owned targets keep their contents. After a TIDE_DRAW_SCREEN command, they're in the
// screen's pixels instead, from the top left with y down, as the GUI's are.
//
// 3D meshes (TIDE_DRAW_MESH_3D) go through a camera of their own
// (TIDE_DRAW_CAMERA_3D), and are the only commands that test and write depth:
// they hide each other by how far they are, whatever their order, and
// everything else draws over what's there, in order. A clear clears depth
// too. Each frame starts with a 3D camera at the origin, looking along +z
// with a field of view of 60 degrees.

typedef enum tide_draw_kind {
    TIDE_DRAW_CLEAR,       // color
    TIDE_DRAW_CAMERA,      // a = center, b.x = size
    TIDE_DRAW_CIRCLE,      // a = center, b.x = radius, color
    TIDE_DRAW_WIRE_CIRCLE, // a = center, b.x = radius, color
    TIDE_DRAW_RECT,        // a = center, b = size, color
    TIDE_DRAW_WIRE_RECT,   // a = center, b = size, color
    TIDE_DRAW_LINE,        // a = from, b = to, color
    TIDE_DRAW_TEXT,        // a = top left corner, b.x = font size, color, text
    TIDE_DRAW_SCREEN,      // The commands after it are in the screen's pixels
    TIDE_DRAW_MESH,        // mesh: its triangles, and the texture they sample
    TIDE_DRAW_CLIP,        // a = the corner where x and y are lowest, b = size
    TIDE_DRAW_NO_CLIP,
    TIDE_DRAW_CAMERA_3D, // camera: the 3D camera, for the 3D meshes after it
    TIDE_DRAW_MESH_3D,   // mesh3: a run of instances of a 3D mesh
    TIDE_DRAW_SHADED,    // shaded: a run of instances of a mesh, through a shader of the game's
    TIDE_DRAW_TARGET,    // target: destination for subsequent commands; 0 is the window
    TIDE_DRAW_BLIT,      // shaded: a fullscreen shaded run, replacing its destination
} tide_draw_kind;

typedef struct tide_draw_command {
    uint32_t kind; // tide_draw_kind
    uint32_t text; // TIDE_DRAW_TEXT: offset of the text in tide_draw_list.text
    union {
        uint32_t target; // TIDE_DRAW_TARGET: 1 + its place in tide_draw_list.targets, or 0
        struct {
            tide_float2 a;
            tide_float2 b;
        };
        struct {              // TIDE_DRAW_MESH
            uint32_t first;   // Its indices in tide_draw_list.indices, three a triangle...
            uint32_t count;   // ...and how many
            uint32_t texture; // 1 + its texture's place in tide_draw_list.textures, or 0 for none
            uint32_t filter;  // tide_filter
        } mesh;
        struct {             // TIDE_DRAW_CAMERA_3D
            uint32_t matrix; // Its place in tide_draw_list.matrices: from the world to clip space, as OpenGL's
            uint32_t fit;    // 1: it's for a square screen, and renderers divide x by the screen's width over its height
        } camera;
        struct {              // TIDE_DRAW_MESH_3D
            uint32_t mesh;    // 1 + its place in tide_draw_list.meshes, or 0 for none (see tide_draw_forget)
            uint32_t first;   // Its instances: transforms in tide_draw_list.matrices, from the mesh to the world...
            uint32_t count;   // ...and how many
            uint32_t texture; // As a mesh's
            uint32_t filter;
        } mesh3;
        struct {          // TIDE_DRAW_SHADED
            uint32_t run; // Its place in tide_draw_list.runs
        } shaded;
    };
    tide_color color;
} tide_draw_command;

// A rectangle: its corner and its size. The GUI's are from the top left.
typedef struct tide_rect {
    float x, y, width, height;
} tide_rect;

// A mesh's corner: where it is, where in the texture it samples ((0, 0) is the
// first pixel's outer corner, (1, 1) the last one's), and its color, which
// multiplies the texture's. Tide's `Vertex` is the same.
typedef struct tide_vertex {
    tide_float2 position;
    tide_float2 uv;
    tide_color color;
} tide_vertex;

// How a mesh samples its texture between pixels. Tide's `Filter` has the same
// values, which are Unity's FilterMode's.
typedef enum tide_filter {
    TIDE_FILTER_POINT,    // The nearest pixel: hard edges
    TIDE_FILTER_BILINEAR, // Blended with its neighbors
} tide_filter;

// Pixels to draw with, where the game keeps them. Nothing holds a texture on
// the GPU: a mesh names its pixels, the list copies them when it hasn't got
// them as they are, and the renderer uploads from that copy. So C can keep
// its pixels wherever it likes, and say when they changed.
typedef struct tide_texture {
    const void *pixels; // width * height of them: red, green, blue, alpha, a byte each, rows from the first
    int32_t width, height;
    uint64_t version; // Any number that's another whenever these pixels are (see tide_texture_version)
} tide_texture;

// Texture's common sampling reference: either CPU pixels or a rendered image.
// Copying this value never copies an image. The existing tide_texture remains
// the CPU pixel descriptor used by C drawing callers.
typedef struct tide_texture_ref {
    tide_texture image;
    uint64_t target; // A rendered image's identity; 0 for CPU pixels
} tide_texture_ref;

// Managed images share this state, including through a copied color reference.
// Disposal keeps a small tombstone until the build unloads, so aliases remain
// safe to inspect. The image attachments are released by the renderer.
typedef struct tide_render_target_state {
    struct tide_render_target_state *next;
    struct tide_render_target_state **owner;
    int32_t width, height;
    uint64_t version;
    uint64_t reload_id; // Temporary opaque identity while restoring another address space.
    bool alive;
} tide_render_target_state;

#define TIDE_RENDER_MANAGED (UINT64_C(1) << 63)
#define TIDE_RENDER_WINDOW UINT64_MAX
#define TIDE_RENDER_INVALID (UINT64_MAX - UINT64_C(1))

static inline bool tide_render_target_managed(const uint64_t id)
{
    return (id & TIDE_RENDER_MANAGED) && id != TIDE_RENDER_WINDOW && id != TIDE_RENDER_INVALID;
}

static inline tide_render_target_state *tide_render_target_state_of(const uint64_t id)
{
    return tide_render_target_managed(id) ? (tide_render_target_state *)(uintptr_t)(id & ~TIDE_RENDER_MANAGED) : NULL;
}

static inline bool tide_render_target_live(const uint64_t id)
{
    const tide_render_target_state *s = tide_render_target_state_of(id);
    return s ? s->alive : id && id != TIDE_RENDER_WINDOW && id != TIDE_RENDER_INVALID;
}

static inline uint64_t tide_render_target_version(const uint64_t id)
{
    const tide_render_target_state *s = tide_render_target_state_of(id);
    return s ? s->version : 0;
}

// A local destination reference. The window uses TIDE_RENDER_WINDOW. Borrowed
// C descriptors use a nonzero id without the managed bit; their owner supplies
// the dimensions. A zero-initialized reference is invalid.
typedef struct tide_render_target {
    uint64_t id;
    int32_t width, height;
} tide_render_target;

tide_render_target tide_render_target_make(int32_t width, int32_t height);
void tide_render_target_resize(tide_render_target target, int32_t width, int32_t height);
void tide_render_target_dispose(tide_render_target target);
void tide_render_target_adopt(tide_render_target target); // Native reload: move a live reference's bookkeeping.
tide_render_target tide_render_target_recreate(tide_render_target previous); // Web reload: previous.id is opaque.
void tide_render_targets_recreated(void); // Ends one web restoration's alias mapping.
void tide_render_targets_free(void); // Build unload: references must no longer be used.

static inline tide_render_target tide_render_target_resolve(tide_render_target target)
{
    const tide_render_target_state *s = tide_render_target_state_of(target.id);
    if (s) target.width = s->alive ? s->width : 0, target.height = s->alive ? s->height : 0;
    return target;
}

static inline tide_texture_ref tide_render_target_color(const tide_render_target target)
{
    const tide_render_target resolved = tide_render_target_resolve(target);
    return (tide_texture_ref){.image = {.width = resolved.width, .height = resolved.height},
                              .target = resolved.id && resolved.id != TIDE_RENDER_WINDOW ? resolved.id : TIDE_RENDER_INVALID};
}

// A texture the list has, for the commands that draw with it: a copy of the
// pixels as they were when a mesh last named them. One no command of a frame
// named is let go when the next frame starts.
typedef struct tide_draw_texture {
    uint64_t id;      // Whose pixels: C's address of them, or a grid's place in its world
    uint64_t version; // ...as they were then
    uint32_t space;   // What `id` is: TIDE_PIXELS_*
    int32_t width, height;
    uint32_t frame;    // The frame that last drew with it
    uint32_t epoch;    // The list's when it was copied (see tide_draw_forget)
    uint32_t capacity; // Bytes `pixels` has room for
    uint8_t *pixels;   // width * height * 4 bytes, rows from the first
} tide_draw_texture;

enum { TIDE_PIXELS_MEMORY, TIDE_PIXELS_GRID, TIDE_PIXELS_TARGET };

// A rendering destination named by recorded commands. Its identity is supplied
// by the resource owner; its size is captured when the command is recorded.
typedef struct tide_draw_target_data {
    uint64_t id;
    uint64_t version; // Resize revision, captured with its dimensions.
    int32_t width, height;
    uint32_t clipped;
    uint32_t camera_2d, camera_3d; // Command indexes, 1 + their places; 0 for the defaults.
} tide_draw_target_data;

// A 3D mesh's corner, as a mesh's but in 3D. Tide's `Vertex3` is the same.
typedef struct tide_vertex3 {
    tide_float3 position;
    tide_float2 uv;
    tide_color color;
} tide_vertex3;

// Triangles in 3D, where the game keeps them: three of `indices` each, places
// in `vertices`. As with a texture, nothing holds them on the GPU: the list
// copies them when it hasn't got them as they are, and the renderer uploads
// from that copy, so a mesh that doesn't change costs neither again.
typedef struct tide_mesh {
    const tide_vertex3 *vertices;
    uint32_t vertex_count;
    const uint32_t *indices;
    uint32_t index_count;
    uint64_t version; // Any number that's another whenever these are (see tide_mesh_version)
} tide_mesh;

// A 3D mesh the list has, for the commands that draw it: a copy of its
// vertices and triangles at a version. It never changes while it has its
// place: another version of the mesh takes a place of its own, so what a
// frame drew before it changed still shows. One no command of a frame named
// is let go when the next frame starts, and one that frames go on drawing
// keeps its place, so renderers can keep theirs by its place.
typedef struct tide_draw_mesh_data {
    uint64_t id, other; // Whose: C's addresses of its vertices and indices, or Tide's lists
    uint64_t version;
    uint32_t space;     // What the ids are: TIDE_MESH_*, or TIDE_MESH_NONE for a free place
    uint32_t frame;     // The frame that last drew it
    uint32_t epoch;     // The list's when it was copied (see tide_draw_forget)
    uint32_t next_free; // A free place's: 1 + the next free one, or 0
    union {
        tide_vertex3 *vertices; // Its corners, or for a TIDE_MESH_BYTES mesh...
        void *bytes;            // ...its vertices as its shader reads them
    };
    uint32_t vertex_size; // Bytes each
    uint32_t vertex_count;
    uint32_t vertex_capacity; // Bytes `vertices` has room for
    uint32_t *indices; // Three a triangle, each a place in `vertices`: what's past them is left out
    uint32_t index_count;
    uint32_t index_capacity;
} tide_draw_mesh_data;

// TIDE_MESH_BYTES: Tide's lists, their vertices as their struct has them (see
// tide_shader), where the others' are tide_vertex3s.
enum { TIDE_MESH_NONE, TIDE_MESH_MEMORY, TIDE_MESH_LISTS, TIDE_MESH_BYTES };

// ---------------------------------------------------------------------------
// Shaders: code of the game's that runs on the GPU, for each vertex of a mesh
// and each pixel it covers. tidec writes them from Tide's `vert` and `frag`
// declarations, one for each combination a mesh draw uses, in the generated
// C: nothing else makes one.

// What a vertex's field is in its list: 1 to 4 floats, or 1 to 4 ints.
typedef enum tide_shader_format {
    TIDE_SHADER_FLOAT,
    TIDE_SHADER_FLOAT2,
    TIDE_SHADER_FLOAT3,
    TIDE_SHADER_FLOAT4,
    TIDE_SHADER_INT,
    TIDE_SHADER_INT2,
    TIDE_SHADER_INT3,
    TIDE_SHADER_INT4,
} tide_shader_format;

// The most a shader takes: vertex attributes (its vertex's fields, and a
// float4 or int4 of its instance's each), and textures.
enum { TIDE_SHADER_ATTRIBUTES = 16, TIDE_SHADER_TEXTURES = 8 };

// The most bytes of the game's a shader's uniforms hold, after the engine's own.
#define TIDE_SHADER_UNIFORM_BYTES 4000u

// A vertex shader and a fragment shader, in each language a renderer takes,
// and what they read:
//
// - A mesh's vertices, `vertex_size` bytes each, whose fields are the
//   attributes at locations 0 on.
// - An instance for each time the mesh is drawn: `instance_floats` float4s,
//   then `instance_ints` int4s, the attributes at the locations after the
//   vertex's. A 3D shader's first four float4s are the instance's transform,
//   column by column.
// - Uniforms for each draw, one block. The engine's own come first:
//   `tide_camera`, a matrix from the mesh's space to clip space as OpenGL has
//   it (the world through the 3D camera for a 3D shader; for a 2D one, the
//   units the draw is in, through the 2D camera or the screen), then
//   `tide_screen`, a float4: the target's pixels in a logical one across and
//   down, and the target's height in its own pixels. After them,
//   `uniform_floats` float4s and `uniform_ints` int4s of the game's.
// - `texture_count` textures: `pixels0`, `pixels1`...
//
// GLSL is without its version line, its uniforms a std140 block named
// `uniforms`. WGSL has both stages, `vertex` and `fragment`, its uniforms at
// binding 0 of group 0, and texture i and its sampler at bindings 1 + 2i and
// 2 + 2i; it's NULL off the web.
typedef struct tide_shader {
    const char *name; // For messages: "Wave and Shade on BoxVertex"
    const char *glsl_vertex, *glsl_fragment;
    const char *wgsl;
    uint64_t id;   // A hash of all of the above and below: another shader, another id
    uint32_t dims; // 2: drawn in order, through the 2D camera or the screen; 3: through the 3D camera, with depth
    uint32_t vertex_size;
    uint32_t attribute_count; // The vertex's fields
    struct {
        uint32_t format; // tide_shader_format
        uint32_t offset; // In the vertex
    } attributes[TIDE_SHADER_ATTRIBUTES];
    uint32_t instance_floats, instance_ints;
    uint32_t uniform_floats, uniform_ints;
    uint32_t texture_count;
} tide_shader;

static inline uint32_t tide_shader_instance_size(const tide_shader *s)
{
    return (s->instance_floats + s->instance_ints) * 16u;
}

static inline uint32_t tide_shader_uniform_size(const tide_shader *s)
{
    return (s->uniform_floats + s->uniform_ints) * 16u;
}

// A run of instances of one mesh through one shader, which renderers draw at
// once: a TIDE_DRAW_SHADED command's.
typedef struct tide_draw_run {
    const tide_shader *shader; // NULL once forgotten (see tide_draw_forget)
    uint32_t mesh;     // 1 + its place in tide_draw_list.meshes: a TIDE_MESH_BYTES one
    uint32_t first;    // Its instances: where they start in tide_draw_list.instances, in bytes...
    uint32_t count;    // ...and how many, tide_shader_instance_size each
    uint32_t uniforms; // Its uniforms of the game's: where they start in tide_draw_list.uniforms, in bytes
    uint32_t blended;  // A bit for each texture: sampled blended with its neighbors, or the nearest
    uint32_t textures[TIDE_SHADER_TEXTURES]; // 1 + each one's place in tide_draw_list.textures, or 0 for none: white
} tide_draw_run;

// One frame's commands. Not simulation state: it lives outside the world. A
// zeroed list is ready to use, and it grows as it needs, with no limit but
// memory: running out ends the program, as it does for worlds.
typedef struct tide_draw_list {
    tide_draw_command *commands; // `count` of them, with room for `capacity`
    uint32_t count;
    uint32_t capacity;
    char *text; // The commands' text, `text_used` bytes, with room for `text_capacity`
    uint32_t text_used;
    uint32_t text_capacity;
    tide_vertex *vertices; // The meshes' corners
    uint32_t vertex_count;
    uint32_t vertex_capacity;
    uint32_t *indices; // ...and their triangles, each index a place in `vertices`
    uint32_t index_count;
    uint32_t index_capacity;
    tide_draw_texture *textures; // What the meshes draw with, kept from frame to frame while they do
    uint32_t texture_count;
    uint32_t texture_capacity;
    tide_draw_target_data *targets;
    uint32_t target_count, target_capacity;
    uint32_t target; // Current destination, 1 + its place above; 0 is the window
    uint32_t window_clipped;
    uint32_t window_camera_2d, window_camera_3d;
    tide_float4x4 *matrices; // The 3D cameras' and the instances' of 3D meshes
    uint32_t matrix_count;
    uint32_t matrix_capacity;
    tide_draw_mesh_data *meshes; // What 3D meshes draw, kept from frame to frame while they do
    uint32_t mesh_count;         // Places, free ones too
    uint32_t mesh_capacity;
    uint32_t mesh_free;           // 1 + the first free place, or 0
    uint32_t *mesh_index;         // The meshes by whose they are: 1 + a place, or 0 for none
    uint32_t mesh_index_capacity; // A power of two, or 0
    tide_draw_run *runs;          // The TIDE_DRAW_SHADED commands' runs
    uint32_t run_count;
    uint32_t run_capacity;
    uint8_t *instances;     // ...their instances, one after the other
    uint32_t instance_used; // Bytes
    uint32_t instance_capacity;
    uint8_t *uniforms; // ...and their uniforms
    uint32_t uniform_used;
    uint32_t uniform_capacity;
    uint32_t frame;   // Counts tide_draw_reset
    uint32_t clipped; // Whether a clip is on for the commands to come
    uint32_t camera_2d; // 1 + the current 2D camera or screen command, or 0 for the default
    uint32_t camera_3d; // 1 + the current 3D camera command, or 0 for the default
    uint32_t epoch;   // Counts tide_draw_forget
} tide_draw_list;

// Empties the list for a new frame. It keeps its memory for the next, and the
// textures the frame drew with.
void tide_draw_reset(tide_draw_list *d);

// Lets the list's memory go: it's empty, and ready to use again.
void tide_draw_free(tide_draw_list *d);

// Lets the list's textures go, and has it copy again whatever the commands to
// come draw with, and renderers upload it again: for a host whose game was
// swapped for another build (hot reloading), whose C names other pixels than
// the one before, maybe at the same address with the same version.
void tide_draw_forget(tide_draw_list *d);

// Selects the destination for commands recorded next. Re-selecting it records
// nothing, so adjacent compatible draws can still batch. Each destination has
// its own camera and clip for the frame. An id of 0 selects the window.
tide_draw_list *tide_draw_to(tide_draw_list *d, uint64_t id, int32_t width, int32_t height);

static inline tide_draw_list *tide_draw_render_target(tide_draw_list *d, const tide_render_target target)
{
    const tide_render_target resolved = tide_render_target_resolve(target);
    return tide_draw_to(d, resolved.id == TIDE_RENDER_WINDOW ? 0 : resolved.id ? resolved.id : TIDE_RENDER_INVALID,
                        resolved.width, resolved.height);
}

// A rendered destination's color as a texture slot, without a CPU copy.
uint32_t tide_draw_target_texture(tide_draw_list *d, uint64_t id, int32_t width, int32_t height);

// A CPU image or target color, copied or referenced as its source requires.
uint32_t tide_draw_texture_source(tide_draw_list *d, tide_texture_ref texture);

// Fills the whole screen, or what the clip leaves of it.
void tide_draw_clear(tide_draw_list *d, tide_color color);

// Sets the camera for the commands after it: `center` is the world position
// at the middle of the screen, and `size` is half the visible height, like
// Unity's orthographic size. Each frame starts with the camera at the origin
// and 1 world unit per pixel.
void tide_draw_camera(tide_draw_list *d, tide_float2 center, float size);

// The commands after it are in the screen's pixels: from the top left, with y
// down, until the next camera.
void tide_draw_screen(tide_draw_list *d);

void tide_draw_circle(tide_draw_list *d, tide_float2 center, float radius, tide_color color);
void tide_draw_wire_circle(tide_draw_list *d, tide_float2 center, float radius, tide_color color);
void tide_draw_rect(tide_draw_list *d, tide_float2 center, tide_float2 size, tide_color color);
void tide_draw_wire_rect(tide_draw_list *d, tide_float2 center, tide_float2 size, tide_color color);
void tide_draw_line(tide_draw_list *d, tide_float2 from, tide_float2 to, tide_color color);

// `position` is the text's top left corner and `size` its font size: the
// height of a line of it, with its capitals in the line's middle. A newline
// starts another line under it. The text is copied into the list.
void tide_draw_text(tide_draw_list *d, const char *text, tide_float2 position, float size, tide_color color);

// Triangles: three of `indices` each, places in `vertices`, both copied into
// the list. Each pixel is its corners' colors blended across the triangle,
// times `texture`'s at their uvs (NULL for none: the colors alone), sampled
// with `filter`. Either side of a triangle draws. A triangle with an index
// past the vertices is left out.
void tide_draw_mesh(tide_draw_list *d, const tide_vertex *vertices, uint32_t vertex_count, const uint32_t *indices,
                    uint32_t index_count, const tide_texture *texture, tide_filter filter);

// The same in two steps, for vertices several meshes share, like a GUI
// library's, whose every batch has a texture and a clip of its own:
// tide_draw_vertices copies them into the list and returns where the first
// is, and tide_draw_triangles draws triangles of the list's vertices from
// `base` on.
uint32_t tide_draw_vertices(tide_draw_list *d, const tide_vertex *vertices, uint32_t count);
void tide_draw_triangles(tide_draw_list *d, uint32_t base, const uint32_t *indices, uint32_t count,
                         const tide_texture *texture, tide_filter filter);

// A version for pixels that change rarely: a hash of them. C that knows when
// its pixels change can count instead.
uint64_t tide_texture_version(const void *pixels, int32_t width, int32_t height);

// Only what's inside `rect` draws, for the commands after it, clears too,
// until another clip or tide_draw_no_clip. The rect is in the units the
// commands are in (the world's under a camera, pixels on the screen), from
// (x, y) to (x + width, y + height), and stays where it is on the screen when
// the camera changes. Each frame starts with none.
void tide_draw_clip(tide_draw_list *d, tide_rect rect);
void tide_draw_no_clip(tide_draw_list *d);

// Adds `from`'s commands to the end of `d`, such as the GUI's over the world.
void tide_draw_append(tide_draw_list *d, const tide_draw_list *from);

// The texture `id` of `space` (TIDE_PIXELS_*) at `version`, for a mesh
// command: 1 + its place in the list's textures, or 0 when it has no size.
// When the list hasn't got its pixels as they are (it's new, or its version
// or size changed), *pixels is where to write them: width * height * 4 bytes.
// Otherwise it's NULL.
uint32_t tide_draw_texture_slot(tide_draw_list *d, uint32_t space, uint64_t id, uint64_t version, int32_t width,
                                int32_t height, uint8_t **pixels);

// tide_draw_triangles, with what tide_draw_texture_slot gave as the texture.
void tide_draw_triangles_with(tide_draw_list *d, uint32_t base, const uint32_t *indices, uint32_t count,
                              uint32_t texture, tide_filter filter);

// ---------------------------------------------------------------------------
// 3D

// Sets the 3D camera for the 3D meshes after it: where it is, which way it
// looks (along its rotation's +z, its up its +y), and how much it sees up
// and down, in degrees, like Unity's Camera.fieldOfView. It sees from 0.3
// units in front of it on, however far, and as much across as the screen's
// shape gives. Other commands keep the camera they have.
void tide_draw_camera_3d(tide_draw_list *d, tide_float3 position, tide_quaternion rotation, float field_of_view);

// What tide_draw_camera_3d records: from the world to clip space, for a
// square screen (TIDE_DRAW_CAMERA_3D's `fit`).
tide_float4x4 tide_draw_camera_3d_matrix(tide_float3 position, tide_quaternion rotation, float field_of_view);

// The same with any projection: `transform` places the camera in the world
// (it looks along its +z), and `projection` takes what it sees, looking down
// -z as OpenGL's and Unity's views do, to clip space (see
// tide_perspectivefov_f4x4 and tide_ortho_f4x4), the screen's shape included.
void tide_draw_camera_matrices(tide_draw_list *d, tide_float4x4 transform, tide_float4x4 projection);

// A 3D mesh, placed in the world by `transform`: each pixel is its corners'
// colors blended across the triangle, times `texture`'s at their uvs (NULL
// for none), sampled with `filter`. Either side of a triangle draws.
// Instances of one mesh drawn one after another, with one texture and filter,
// are one command, which renderers draw at once.
void tide_draw_mesh_3d(tide_draw_list *d, const tide_mesh *mesh, tide_float4x4 transform, const tide_texture *texture,
                       tide_filter filter);

// A version for vertices and indices that change rarely: a hash of them.
uint64_t tide_mesh_version(const tide_mesh *mesh);

// The same in steps, for meshes kept elsewhere, like Tide's lists: the mesh
// `id` and `other` of `space` (TIDE_MESH_*) at `version`, for a 3D mesh
// command: 1 + its place in the list's meshes. When the list hasn't got it
// at that version, *fill is true, and tide_draw_mesh_fill gives it its
// vertices and indices before anything draws it.
uint32_t tide_draw_mesh_slot(tide_draw_list *d, uint32_t space, uint64_t id, uint64_t other, uint64_t version,
                             bool *fill);
void tide_draw_mesh_fill(tide_draw_list *d, uint32_t mesh, const tide_vertex3 *vertices, uint32_t vertex_count,
                         const uint32_t *indices, uint32_t index_count);

// An instance of what tide_draw_mesh_slot gave, with what
// tide_draw_texture_slot gave as the texture.
void tide_draw_instance(tide_draw_list *d, uint32_t mesh, tide_float4x4 transform, uint32_t texture, tide_filter filter);

// The same for `count` transforms, copied in their order into one run.
// With no transforms, nothing is recorded.
void tide_draw_instances(tide_draw_list *d, uint32_t mesh, const tide_float4x4 *transforms, uint32_t count,
                         uint32_t texture, tide_filter filter);

// tide_draw_mesh_fill for a TIDE_MESH_BYTES mesh: vertices of `vertex_size`
// bytes each, as a shader reads them.
void tide_draw_mesh_fill_bytes(tide_draw_list *d, uint32_t mesh, const void *vertices, uint32_t vertex_size,
                               uint32_t vertex_count, const uint32_t *indices, uint32_t index_count);

// An instance of the TIDE_MESH_BYTES mesh tide_draw_mesh_slot gave, through
// `shader`: its instance's bytes (tide_shader_instance_size of them), the
// draw's uniforms of the game's (tide_shader_uniform_size), and what
// tide_draw_texture_slot gave for each of its textures, bit i of `blended`
// set for one sampled blended. The bytes are copied; a pointer can be NULL
// when the shader has none of its kind, and with none where it has some,
// nothing is drawn. Instances of one mesh drawn one after
// another through one shader, with the same uniforms and textures, are one
// command, which renderers draw at once.
void tide_draw_shaded(tide_draw_list *d, const tide_shader *shader, uint32_t mesh, const void *instance,
                      const void *uniforms, const uint32_t *textures, uint32_t blended);

// The same for `count` instances, tide_shader_instance_size bytes each,
// copied in their order into one run. A shader with no instance bytes may
// take NULL with a positive count. A count of zero records nothing.
void tide_draw_shaded_instances(tide_draw_list *d, const tide_shader *shader, uint32_t mesh, const void *instances,
                                uint32_t count, const void *uniforms, const uint32_t *textures, uint32_t blended);

// Runs a frag over the entire selected destination. The fixed Vertex mesh has
// clip-space positions, top-left uvs, and white color. The camera is identity,
// clipping is disabled for this draw alone, and output replaces destination
// color without changing its depth. Texture bindings and packed shader data
// are the same as tide_draw_shaded's. Sampling the destination is refused.
void tide_draw_blit(tide_draw_list *d, const tide_shader *shader, const void *instance,
                   const void *uniforms, const uint32_t *textures, uint32_t blended);
// ---------------------------------------------------------------------------
// 3D primitives: solid meshes, through the 3D camera and with depth, as
// tide_draw_mesh_3d. Every shape is centered on `center`; its geometry is
// kept by shape and color, and repeated instances draw at once. A size,
// radius or height that is not positive draws nothing.

// A cube's `size` is its full width, height and depth. The rotated form
// turns its local axes by `rotation` before placing it in the world.
void tide_draw_cube(tide_draw_list *d, tide_float3 center, tide_float3 size, tide_color color);
void tide_draw_cube_rotated(tide_draw_list *d, tide_float3 center, tide_float3 size, tide_quaternion rotation,
                             tide_color color);

void tide_draw_sphere(tide_draw_list *d, tide_float3 center, float radius, tide_color color);

// A cylinder's axis is local +y, with the full `height` around its center
// and a circular cap at each end.
void tide_draw_cylinder(tide_draw_list *d, tide_float3 center, float radius, float height, tide_color color);
void tide_draw_cylinder_rotated(tide_draw_list *d, tide_float3 center, float radius, float height,
                                 tide_quaternion rotation, tide_color color);

// A plane is in local XZ: `size.x` along x, `size.y` along z. Both sides
// draw, as for other meshes.
void tide_draw_plane(tide_draw_list *d, tide_float3 center, tide_float2 size, tide_color color);
void tide_draw_plane_rotated(tide_draw_list *d, tide_float3 center, tide_float2 size, tide_quaternion rotation,
                              tide_color color);

// ---------------------------------------------------------------------------
// Coordinates through the current camera, for the commands recorded next.
// `screen_size` is the window's width and height in logical pixels. When a
// target is selected, its dimensions are used instead. Screen
// positions are from the top left, with y down, as tide_draw_screen and the
// GUI's are. Device positions are from the bottom left: change their y to
// screen_size.y - y before passing them here.
//
// Each target keeps its own cameras. The 2D helpers use its most recent
// 2D camera or screen command, or the
// default centered camera at one world unit per pixel. In screen mode both
// are the identity. A 3D camera leaves them as they were, and 2D commands
// leave the 3D camera as it was. Neither needs a frame to have been rendered.
tide_float2 tide_draw_world_to_screen(const tide_draw_list *d, tide_float2 point, tide_float2 screen_size);
tide_float2 tide_draw_screen_to_world(const tide_draw_list *d, tide_float2 point, tide_float2 screen_size);

// The 3D helpers use the most recent 3D camera, or the default perspective
// camera. A screen position's z is depth: 0 at the near plane, 1 at the far
// plane, as the GPU's depth buffer, rather than distance from the camera.
// Coordinates and depth are not clamped to the viewport. With the standard
// infinite perspective camera, depth 1 has no finite world position.
tide_float3 tide_draw_world_to_screen_3d(const tide_draw_list *d, tide_float3 point, tide_float2 screen_size);
tide_float3 tide_draw_screen_to_world_3d(const tide_draw_list *d, tide_float3 point, tide_float2 screen_size);

// A ray through the screen point, starting at the camera's near plane and
// pointing into the scene. The direction is normalized; this works with
// perspective cameras and orthographic or other matrix projections.
tide_float3 tide_draw_screen_ray_origin(const tide_draw_list *d, tide_float2 point, tide_float2 screen_size);
tide_float3 tide_draw_screen_ray_direction(const tide_draw_list *d, tide_float2 point, tide_float2 screen_size);

// All coordinate helpers return a zero vector when an input is non-finite,
// screen size is non-positive, the camera is singular, or a homogeneous
// projection has no finite point. A point outside the view is still mapped.

// How big a renderer draws text: of `text` drawn `size` tall, the width of
// its widest line and the height of its lines, in the units `size` is in. One
// line is `size` high, and each newline adds what's from one line to the
// next. Text is fitted to the pixels it's drawn on, so where it's drawn is
// part of the answer: in the window, whose units are its logical pixels, or
// (`target`) in a render target, whose units are its own pixels. The platform
// measures with its font (tide_platform_measure_text).
typedef tide_float2 (*tide_measure_fn)(const char *text, float size, bool target);

// What stands in for a host's tide_measure_fn when it has none (NULL), as
// tests and servers do: a guess, on the wide side.
tide_float2 tide_draw_guess_text(const char *text, float size, bool target);

// The size of `text` as tide_draw_text would draw it now, at `size`, in the
// units the commands recorded next are in: the screen's pixels after
// tide_draw_screen, and world units through the current 2D camera, which
// draws text as many times bigger as it has pixels to a unit. `x` is its
// widest line's width and `y` the height of its lines, both from the
// position tide_draw_text takes, its top left corner. `screen_size` is as for
// the coordinate helpers above, and `measure` the renderer's, or NULL to
// guess. Nothing for a size that isn't above 0, as nothing is drawn.
tide_float2 tide_draw_measure_text(const tide_draw_list *d, const char *text, float size, tide_float2 screen_size,
                                   tide_measure_fn measure);
