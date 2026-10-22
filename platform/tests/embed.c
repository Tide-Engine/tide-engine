// Tide inside another program's frame (tide/embed.h), as that program runs
// it: platform/tests/embed.tide, its frames' lengths and input made up here.

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/embed.h"
#include "tide/run.h"
#include "tide_test.h"
#if !defined(__wasm__)
#include "shaders_spirv.h"
#endif


// What the game's view saw at the last frame (embed.tide's Show).
static int seen_ticks = -1, seen_presses = -1;
static float seen_width, seen_height;

void Seen(const int ticks, const int presses, const float width, const float height)
{
    seen_ticks = ticks;
    seen_presses = presses;
    seen_width = width;
    seen_height = height;
}

static char program[] = "embed";

static tide_embed *start(int argc, char **argv)
{
    seen_ticks = seen_presses = -1;
    return tide_embed_create(&(tide_embed_desc){.game = &tide_host_game_api, .argc = argc, .argv = argv});
}

// `count` frames of a 60th of a second, with `input` in each.
static void frames(tide_embed *e, const int count, const tide_embed_input *input)
{
    for (int i = 0; i < count; i++) tide_embed_frame(e, 1.0f / 60.0f, input, 800.0f, 600.0f, 2.0f);
}

static void hold_key(tide_embed_input *input, const unsigned key, const bool held)
{
    if (held) input->keys[key / 8] |= (uint8_t)(1u << key % 8);
    else input->keys[key / 8] &= (uint8_t)~(1u << key % 8);
}

// Main's match starts on this machine, and ticks at its rate by the frames' lengths: a second of them is 30 ticks.
TIDE_TEST(embed_runs_main)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    TIDE_CHECK(start(1, argv) == NULL); // One at a time
    TIDE_CHECK(tide_embed_abi() == TIDE_EMBED_ABI);
    frames(e, 60, NULL);
    tide_embed_status status;
    tide_embed_status_of(e, &status);
    TIDE_CHECK(status.server);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(!status.open);
    TIDE_CHECK(status.room[0] == '\0'); // A closed match is in no room
    TIDE_CHECK(seen_ticks >= 27 && seen_ticks <= 30);
    TIDE_CHECK(seen_width == 800.0f && seen_height == 600.0f); // The screen, in logical pixels
    tide_embed_destroy(e);
}

// A key held for one frame between two ticks reaches the next tick's input once, as from a window.
TIDE_TEST(embed_input_reaches_ticks)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    tide_embed_input input = {0};
    frames(e, 10, &input);
    hold_key(&input, 36, true); // space: after the 26 letters and 10 digits, in tide_keyboard's order
    frames(e, 1, &input);
    hold_key(&input, 36, false);
    frames(e, 10, &input);
    TIDE_CHECK(seen_presses == 1);
    tide_embed_destroy(e);
}

// Requests go as local code's do: out of the match and into Main's again; a scene by name isn't taken yet.
TIDE_TEST(embed_requests)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 5, NULL);
    TIDE_CHECK(!tide_embed_request(e, TIDE_REQUEST_START, "Main", 0));
    TIDE_CHECK(!tide_embed_request(e, TIDE_REQUEST_JOIN, "", 0));
    TIDE_CHECK(!tide_embed_request(e, TIDE_REQUEST_COPY, "text", 0));
    TIDE_CHECK(tide_embed_request(e, TIDE_REQUEST_LEAVE, NULL, 0));
    frames(e, 2, NULL);
    tide_embed_status status;
    tide_embed_status_of(e, &status);
    TIDE_CHECK(status.state == TIDE_SESSION_OFFLINE);
    TIDE_CHECK(tide_embed_request(e, TIDE_REQUEST_START, NULL, 0));
    seen_ticks = -1;
    frames(e, 30, NULL);
    tide_embed_status_of(e, &status);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    TIDE_CHECK(seen_ticks >= 12 && seen_ticks <= 15); // A new match, counted from its start
    tide_embed_destroy(e);
}

// A flag that's wrong is left out, and the program goes on, where the standard host would end it.
TIDE_TEST(embed_bad_flags_left_out)
{
    char seed[] = "--seed", bad[] = "lots", dir[] = "--data-dir", dash[] = "-x";
    char *argv[] = {program, seed, bad, dir, dash};
    tide_embed *e = start(5, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 30, NULL);
    TIDE_CHECK(seen_ticks >= 12);
    tide_embed_destroy(e);
}

// Frames whose lengths make no time (NaN, below 0) add none; the screen falls back to the smallest.
TIDE_TEST(embed_bad_frames)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 30, NULL);
    const int ticks = seen_ticks;
    float nan_value;
    const uint32_t nan_bits = 0x7fc00000u;
    memcpy(&nan_value, &nan_bits, sizeof nan_value);
    tide_embed_frame(e, nan_value, NULL, nan_value, -5.0f, nan_value);
    tide_embed_frame(e, -1.0f, NULL, nan_value, -5.0f, 0.0f);
    TIDE_CHECK(seen_ticks == ticks);
    TIDE_CHECK(seen_width == 1.0f && seen_height == 1.0f);
    tide_embed_destroy(e);
}

// The layout's type named `name`, or -1.
static int32_t type_named(const char *name)
{
    for (uint32_t i = 0; i < tide_game_layout.type_count; i++) {
        if (strcmp(tide_game_layout.types[i].name, name) == 0) return (int32_t)i;
    }
    return -1;
}

static uint32_t u32_at(const uint8_t *p, const uint32_t at)
{
    uint32_t v;
    memcpy(&v, p + at, sizeof v);
    return v;
}

// The packet's section of `kind` for `type`: its count, its rows (or NULL) and values, as another language finds them.
static const uint8_t *section(const uint8_t *p, const uint32_t size, const uint32_t kind, const int32_t type, uint32_t *count,
                              const uint8_t **rows)
{
    const uint32_t entities = u32_at(p, 36), sections = u32_at(p, 40);
    uint32_t at = TIDE_PACKET_HEADER + entities * TIDE_PACKET_ENTITY;
    for (uint32_t s = 0; s < sections; s++) {
        at = (at + 7u) & ~7u;
        if (at + TIDE_PACKET_SECTION > size) return NULL;
        const uint32_t k = u32_at(p, at), t = u32_at(p, at + 4), n = u32_at(p, at + 8), bytes = u32_at(p, at + 16);
        const uint8_t *data = p + at + TIDE_PACKET_SECTION;
        if (k == kind && (int32_t)t == type) {
            *count = n;
            const bool has_rows = kind == TIDE_PACKET_COMPONENT || kind == TIDE_PACKET_COMPONENT_LOCAL;
            if (rows) *rows = has_rows ? data : NULL;
            return has_rows ? data + ((n * 4u + 7u) & ~7u) : data;
        }
        at += TIDE_PACKET_SECTION + bytes;
    }
    return NULL;
}

// The text or list a packet's handle names, in its STRINGS.
static const uint8_t *strings_entry(const uint8_t *p, const uint32_t size, const uint32_t handle, uint32_t *length)
{
    uint32_t count = 0;
    const uint8_t *entries = section(p, size, TIDE_PACKET_STRINGS, 0, &count, NULL);
    if (!entries || handle == 0 || handle > count) return NULL;
    *length = u32_at(entries, (handle - 1u) * 8u + 4u);
    return entries + count * 8u + u32_at(entries, (handle - 1u) * 8u);
}

// The frame's view packet holds the entity, its components blended as views see them, text and lists, and singletons.
TIDE_TEST(embed_view_packet)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 30, NULL);
    uint32_t size = 0;
    const uint8_t *p = tide_embed_view(e, &size);
    TIDE_CHECK(p != NULL && size >= TIDE_PACKET_HEADER);
    TIDE_CHECK(u32_at(p, 0) == TIDE_PACKET_MAGIC && u32_at(p, 4) == TIDE_PACKET_VERSION && u32_at(p, 16) == size);
    uint64_t hash;
    memcpy(&hash, p + 8, sizeof hash);
    TIDE_CHECK(hash == tide_game_api.hash);
    TIDE_CHECK(u32_at(p, 20) == 1u); // In a match
    TIDE_CHECK(u32_at(p, 36) == 2u); // Two entities: Main's scene, and this machine's player's counter
    uint32_t count = 0;
    const uint8_t *rows = NULL;
    const uint8_t *values = section(p, size, TIDE_PACKET_COMPONENT, type_named("Counter"), &count, &rows);
    TIDE_CHECK(values != NULL && count == 1u && u32_at(rows, 0) < 2u);
    const uint32_t entity = TIDE_PACKET_HEADER + u32_at(rows, 0) * TIDE_PACKET_ENTITY;
    TIDE_CHECK(u32_at(p, entity + 8) == 3u); // Owned, and by this machine's player
    TIDE_CHECK(u32_at(p, entity + 16) == TIDE_PACKET_NONE); // A root
    // What it was spawned as: known, as this machine spawned it, and the same frame after frame
    const uint32_t born = u32_at(p, entity + 20);
    TIDE_CHECK(born != 0u);
    Counter counter;
    memcpy(&counter, values, sizeof counter);
    TIDE_CHECK(counter.ticks == seen_ticks);
    uint32_t length = 0;
    const uint8_t *label = strings_entry(p, size, counter.label.at, &length);
    TIDE_CHECK(label && length == 7u && memcmp(label, "counted", 7) == 0);
    const uint8_t *marks = strings_entry(p, size, counter.marks.at, &length);
    int32_t mark[3];
    TIDE_CHECK(marks && length == sizeof mark);
    memcpy(mark, marks, sizeof mark);
    TIDE_CHECK(mark[0] == 4 && mark[1] == 5 && mark[2] == 6);
    const uint8_t *tally = section(p, size, TIDE_PACKET_SINGLETON, type_named("Tally"), &count, NULL);
    TIDE_CHECK(tally != NULL && count == 1u);
    float level;
    memcpy(&level, tally + offsetof(Tally, level), sizeof level);
    TIDE_CHECK(level > 0.3f && level < 0.6f); // About half a second of ticks, blended between the last two
    frames(e, 5, NULL);
    p = tide_embed_view(e, &size);
    TIDE_REQUIRE(p != NULL && u32_at(p, 36) == 2u);
    values = section(p, size, TIDE_PACKET_COMPONENT, type_named("Counter"), &count, &rows);
    TIDE_REQUIRE(values != NULL && count == 1u);
    TIDE_CHECK(u32_at(p, TIDE_PACKET_HEADER + u32_at(rows, 0) * TIDE_PACKET_ENTITY + 20) == born);
    tide_embed_destroy(e);
}

// What the views drew, for the other program to draw: the header says the parts, each from a multiple of 8, and
// Show's rect and text are among the commands, the text's words in the text part.
TIDE_TEST(embed_draw_packet)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 30, NULL); // The counter is there, and Show draws for it
    uint32_t size = 0;
    const uint8_t *p = tide_embed_draw(e, &size);
    TIDE_REQUIRE(p != NULL && size >= sizeof(tide_embed_draw_header));
    tide_embed_draw_header h;
    memcpy(&h, p, sizeof h);
    TIDE_CHECK(h.magic == TIDE_EMBED_DRAW_MAGIC && h.version == TIDE_EMBED_DRAW_VERSION);
    TIDE_CHECK(h.command_size == sizeof(tide_draw_command) && h.shape_size == sizeof(tide_draw_shape_data));
    TIDE_CHECK(h.vertex_size == sizeof(tide_vertex));
    TIDE_CHECK(h.screen_width == 800.0f && h.screen_height == 600.0f && h.screen_scale == 2.0f);
    const size_t commands = (sizeof h + 7u) & ~(size_t)7u;
    size_t at = commands + (size_t)h.commands * sizeof(tide_draw_command);
    at = (at + 7u) & ~(size_t)7u; // shapes
    at += (size_t)h.shapes * sizeof(tide_draw_shape_data);
    at = (at + 7u) & ~(size_t)7u; // vertices
    at += (size_t)h.vertices * sizeof(tide_vertex);
    at = (at + 7u) & ~(size_t)7u; // indices
    at += (size_t)h.indices * sizeof(uint32_t);
    const size_t text = (at + 7u) & ~(size_t)7u;
    TIDE_REQUIRE(text + h.text <= size);
    const size_t shapes = (commands + (size_t)h.commands * sizeof(tide_draw_command) + 7u) & ~(size_t)7u;
    const size_t vertices = (shapes + (size_t)h.shapes * sizeof(tide_draw_shape_data) + 7u) & ~(size_t)7u;
    const size_t indices = (vertices + (size_t)h.vertices * sizeof(tide_vertex) + 7u) & ~(size_t)7u;
    bool rect = false, words = false, quad = false, quads = false;
    for (uint32_t i = 0; i < h.commands; i++) {
        tide_draw_command c;
        memcpy(&c, p + commands + i * sizeof c, sizeof c);
        if (c.kind == TIDE_DRAW_RECT && c.a.x == 10.0f && c.a.y == 20.0f && c.b.x == 30.0f && c.b.y == 40.0f && c.color.r == 1.0f) {
            rect = true;
        }
        if (c.kind == TIDE_DRAW_TEXT && c.text < h.text && strcmp((const char *)p + text + c.text, "drawn") == 0) words = true;
        quads |= c.kind == TIDE_DRAW_QUADS; // The list's quads are meshes in the packet, as they always were
        if (c.kind == TIDE_DRAW_MESH && c.mesh.count == 6u && c.mesh.first + 6u <= h.indices) {
            uint32_t at_index[6];
            memcpy(at_index, p + indices + (size_t)c.mesh.first * sizeof(uint32_t), sizeof at_index);
            bool blue = true;
            float right = 0.0f;
            for (uint32_t k = 0; k < 6u; k++) {
                tide_vertex v;
                if (at_index[k] >= h.vertices) { blue = false; break; }
                memcpy(&v, p + vertices + (size_t)at_index[k] * sizeof v, sizeof v);
                blue &= v.color.b == 1.0f && v.color.r == 0.0f && (v.position.x == 50.0f || v.position.x == 70.0f);
                if (v.position.x > right) right = v.position.x;
            }
            quad |= blue && right == 70.0f;
        }
    }
    TIDE_CHECK(rect);
    TIDE_CHECK(words);
    TIDE_CHECK(quad && !quads);
    tide_embed_destroy(e);
    TIDE_CHECK(tide_embed_draw(e, &size) == NULL && size == 0); // Nothing once it's ended
}

// Where Show's yellow circle is in the draw packet: the point it projected, in screen pixels.
static bool projected(tide_embed *e, tide_float2 *at)
{
    uint32_t size = 0;
    const uint8_t *p = tide_embed_draw(e, &size);
    if (!p || size < sizeof(tide_embed_draw_header)) return false;
    tide_embed_draw_header h;
    memcpy(&h, p, sizeof h);
    const size_t commands = (sizeof h + 7u) & ~(size_t)7u;
    for (uint32_t i = 0; i < h.commands; i++) {
        tide_draw_command c;
        memcpy(&c, p + commands + i * sizeof c, sizeof c);
        if (c.kind == TIDE_DRAW_CIRCLE && c.b.x == 3.0f && c.color.r == 1.0f && c.color.g == 1.0f && c.color.b == 0.0f) {
            *at = c.a;
            return true;
        }
    }
    return false;
}

// The other program's camera is the one views start with (tide_embed_camera): Draw.WorldToScreen goes through it,
// and a projection that sees nothing goes back to Tide's first camera.
TIDE_TEST(embed_camera)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 30, NULL);
    tide_float2 at = {0};
    TIDE_REQUIRE(projected(e, &at));
    TIDE_CHECK(at.x == 400.0f && at.y == 300.0f); // Straight ahead of a camera at the origin: the 800 by 600 screen's middle
    // A step to the right, a quarter turn wide, as Unity's camera gives its matrices: 5 ahead and 1 to the left is 0.2
    // of the half width the field of view sees there, times 600 / 800 for the screen's shape
    const tide_float4x4 transform = tide_translate_f4x4(tide_f3(1.0f, 0.0f, 0.0f));
    const tide_float4x4 projection = tide_perspectivefov_f4x4(TIDE_PI_F * 0.5f, 800.0f / 600.0f, 0.3f, 1000.0f);
    tide_embed_camera(e, &transform.c0.x, &projection.c0.x);
    frames(e, 1, NULL);
    TIDE_REQUIRE(projected(e, &at));
    TIDE_CHECK(at.x > 339.9f && at.x < 340.1f && at.y > 299.9f && at.y < 300.1f);
    const tide_float4x4 none = {0};
    tide_embed_camera(e, &transform.c0.x, &none.c0.x);
    frames(e, 1, NULL);
    TIDE_REQUIRE(projected(e, &at));
    TIDE_CHECK(at.x == 400.0f && at.y == 300.0f);
    tide_embed_destroy(e);
}

#if !defined(__wasm__)
// What a frame's render commands hold, read as the other program reads them (tide_embed_render).
typedef struct rendered {
    uint32_t ops[TIDE_EMBED_RENDER_DRAW_INDEXED + 1];
    uint32_t used[16]; // The pipelines USE named, the first 16
    uint32_t used_count;
    uint32_t pipelines[32]; // ...and those made, by `keys`' places
    float camera[16];       // The first uniforms' first 16 floats
    uint64_t keys[32];
    uint32_t key_count;
    uint32_t first_pipeline; // The first pipeline's number
    bool window_cleared;     // A pass on the other program's own target asked to clear it
    bool whole;              // Each command's bytes were inside the stream
} rendered;

static rendered read_commands(const uint8_t *p, const uint32_t size)
{
    rendered r = {0};
    r.whole = p != NULL;
    for (uint32_t at = 0; r.whole && at < size;) {
        uint32_t head[2];
        if (size - at < 8u) {
            r.whole = false;
            break;
        }
        memcpy(head, p + at, 8u);
        at += 8u;
        if (head[1] % 4u || head[1] > size - at || head[0] == 0 || head[0] > TIDE_EMBED_RENDER_DRAW_INDEXED) {
            r.whole = false;
            break;
        }
        r.ops[head[0]]++;
        uint32_t words[4] = {0};
        memcpy(words, p + at, head[1] < 16u ? head[1] : 16u);
        if (head[0] == TIDE_EMBED_RENDER_PIPELINE && r.key_count < 32u) {
            if (!r.first_pipeline) r.first_pipeline = words[0];
            r.pipelines[r.key_count] = words[0];
            r.keys[r.key_count++] = (uint64_t)words[1] | (uint64_t)words[2] << 32;
        }
        if (head[0] == TIDE_EMBED_RENDER_USE && r.used_count < 16u) r.used[r.used_count++] = words[0];
        if (head[0] == TIDE_EMBED_RENDER_UNIFORMS && r.ops[head[0]] == 1u && head[1] >= 68u) memcpy(r.camera, p + at + 4, 64);
        if (head[0] == TIDE_EMBED_RENDER_BEGIN && words[0] == 0 && words[1]) r.window_cleared = true;
        at += head[1];
    }
    return r;
}

static rendered render(tide_embed *e)
{
    uint32_t size = 0;
    const uint8_t *p = tide_embed_render(e, &size);
    return read_commands(p, size);
}

static uint64_t fnv(const uint32_t *words, const uint32_t count)
{
    uint64_t h = UINT64_C(14695981039346656037);
    const uint8_t *b = (const uint8_t *)words;
    for (size_t i = 0; i < (size_t)count * 4u; i++) h = (h ^ b[i]) * UINT64_C(1099511628211);
    return h;
}

// The renderer draws the views' frame through the backend that writes it down: a pass on the other program's
// target, which asks to be cleared as a window's first pass does, pipelines named by their SPIR-V's hash (what
// tide unity names the shaders it writes by), the text's atlas, buffers and draws. The next frame makes nothing
// again, and after a reset everything is made again, numbered from 1.
TIDE_TEST(embed_render)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 10, NULL);
    const rendered first = render(e);
    TIDE_REQUIRE(first.whole);
    TIDE_CHECK(first.ops[TIDE_EMBED_RENDER_BEGIN] >= 1u && first.ops[TIDE_EMBED_RENDER_END] == first.ops[TIDE_EMBED_RENDER_BEGIN]);
    TIDE_CHECK(first.window_cleared);
    TIDE_CHECK(first.ops[TIDE_EMBED_RENDER_TEXTURE] >= 1u); // The text's atlas, and the white pixel
    TIDE_CHECK(first.ops[TIDE_EMBED_RENDER_BUFFER] >= 1u);
    TIDE_CHECK(first.ops[TIDE_EMBED_RENDER_DRAW] + first.ops[TIDE_EMBED_RENDER_DRAW_INDEXED] >= 3u);
    TIDE_REQUIRE(first.key_count >= 3u);
    const uint64_t known[] = {
#define KEY(name) fnv(tide_spirv_##name, tide_spirv_##name##_words),
        TIDE_BUILTIN_SPIRV(KEY)
#undef KEY
    };
    for (uint32_t i = 0; i < first.key_count; i++) {
        bool found = false;
        for (size_t k = 0; k < sizeof known / sizeof known[0]; k++) found = found || first.keys[i] == known[k];
        TIDE_CHECK(found);
    }
    frames(e, 1, NULL);
    const rendered second = render(e);
    TIDE_REQUIRE(second.whole);
    TIDE_CHECK(second.ops[TIDE_EMBED_RENDER_PIPELINE] == 0u);
    TIDE_CHECK(second.ops[TIDE_EMBED_RENDER_DRAW] + second.ops[TIDE_EMBED_RENDER_DRAW_INDEXED] >= 3u);
    // The frame's 3D again through another camera: the cube alone, through the 3D meshes' pipeline, with that
    // camera's matrix as its uniforms say, and none of the 2D
    uint64_t cube_key = 0, used_key = 0;
    for (uint32_t i = 0; i < first.key_count; i++) {
        if (first.keys[i] == fnv(tide_spirv_mesh_3d, tide_spirv_mesh_3d_words)) cube_key = first.keys[i];
    }
    TIDE_REQUIRE(cube_key != 0);
    const tide_float4x4 place = tide_translate_f4x4(tide_f3(0.0f, 2.0f, 0.0f));
    const tide_float4x4 projection = tide_perspectivefov_f4x4(1.0f, 2.0f, 0.5f, 100.0f);
    uint32_t view_size = 0;
    const uint8_t *view_commands = tide_embed_render_view(e, &place.c0.x, &projection.c0.x, 400, 200, &view_size);
    const rendered view = read_commands(view_commands, view_size);
    TIDE_REQUIRE(view.whole);
    TIDE_CHECK(view.ops[TIDE_EMBED_RENDER_DRAW_INDEXED] == 1u && view.ops[TIDE_EMBED_RENDER_DRAW] == 0u);
    TIDE_REQUIRE(view.used_count == 1u);
    for (uint32_t i = 0; i < first.key_count; i++) {
        if (first.pipelines[i] == view.used[0]) used_key = first.keys[i];
    }
    TIDE_CHECK(used_key == cube_key);
    tide_float4x4 seeing = tide_inverse_f4x4(place);
    seeing.c0.z = -seeing.c0.z, seeing.c1.z = -seeing.c1.z, seeing.c2.z = -seeing.c2.z, seeing.c3.z = -seeing.c3.z;
    const tide_float4x4 expected = tide_mul_f4x4(projection, seeing);
    TIDE_CHECK(memcmp(view.camera, &expected, sizeof expected) == 0);
    TIDE_CHECK(tide_embed_render_view(e, &place.c0.x, &projection.c0.x, 0, 200, &view_size) == NULL);
    tide_embed_render_reset(e);
    frames(e, 1, NULL);
    const rendered again = render(e);
    TIDE_REQUIRE(again.whole);
    TIDE_CHECK(again.ops[TIDE_EMBED_RENDER_PIPELINE] == first.ops[TIDE_EMBED_RENDER_PIPELINE]);
    TIDE_CHECK(again.ops[TIDE_EMBED_RENDER_TEXTURE] >= 1u);
    TIDE_CHECK(again.first_pipeline > 0u && again.first_pipeline <= first.first_pipeline);
    tide_embed_destroy(e);
}
#endif

// --stats shows the stats' drawer, open, in the draw packet the other program draws: the frame rate and the
// frame's work, and no draw calls, which are the other program's.
TIDE_TEST(embed_stats)
{
    char *argv[] = {program, "--stats"};
    tide_embed *e = start(2, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 30, NULL);
    uint32_t size = 0;
    const uint8_t *p = tide_embed_draw(e, &size);
    TIDE_REQUIRE(p != NULL && size >= sizeof(tide_embed_draw_header));
    tide_embed_draw_header h;
    memcpy(&h, p, sizeof h);
    const size_t commands = (sizeof h + 7u) & ~(size_t)7u;
    size_t at = commands + (size_t)h.commands * sizeof(tide_draw_command);
    at = ((at + 7u) & ~(size_t)7u) + (size_t)h.shapes * sizeof(tide_draw_shape_data);
    at = ((at + 7u) & ~(size_t)7u) + (size_t)h.vertices * sizeof(tide_vertex);
    at = ((at + 7u) & ~(size_t)7u) + (size_t)h.indices * sizeof(uint32_t);
    const size_t text = (at + 7u) & ~(size_t)7u;
    TIDE_REQUIRE(text + h.text <= size);
    bool fps = false, cpu = false, draw_calls = false;
    for (uint32_t i = 0; i < h.commands; i++) {
        tide_draw_command c;
        memcpy(&c, p + commands + i * sizeof c, sizeof c);
        if (c.kind != TIDE_DRAW_TEXT || c.text >= h.text) continue;
        const char *words = (const char *)p + text + c.text;
        const size_t n = strlen(words);
        fps |= n > 4 && strcmp(words + n - 4, " fps") == 0;
        cpu |= strcmp(words, "cpu") == 0;
        draw_calls |= strcmp(words, "draw calls") == 0;
    }
    TIDE_CHECK(fps && cpu && !draw_calls);
    tide_embed_destroy(e);
}

// The schema names the packet's types, what they are, and the game's commands and scenes.
TIDE_TEST(embed_schema)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    uint32_t size = 0;
    const char *schema = tide_embed_schema(e, &size);
    TIDE_CHECK(schema != NULL && size == strlen(schema));
    char counter[96];
    snprintf(counter, sizeof counter, "{\"name\":\"Counter\",\"kind\":\"component\",\"size\":%u", (unsigned)sizeof(Counter));
    TIDE_CHECK(strstr(schema, counter) != NULL);
    // A component's declared defaults, as its bytes: its text and lists empty
    Counter defaults;
    memset(&defaults, 0, sizeof defaults);
    defaults.rate = 2.5f;
    char hex[sizeof defaults * 2 + 16] = "\"defaults\":\"";
    for (size_t i = 0; i < sizeof defaults; i++) {
        snprintf(hex + strlen(hex), 3, "%02x", (unsigned)((const uint8_t *)&defaults)[i]);
    }
    TIDE_CHECK(strstr(schema, hex) != NULL);
    TIDE_CHECK(strstr(schema, "{\"name\":\"label\",\"type\":\"string\",\"kind\":\"text\"") != NULL);
    TIDE_CHECK(strstr(schema, "\"name\":\"marks\",\"type\":\"List<int>\",\"kind\":\"list\"") != NULL);
    TIDE_CHECK(strstr(schema, "{\"name\":\"Tally\",\"kind\":\"singleton\"") != NULL);
    // A component no entity has yet is a component still, with its defaults (69.0f's bytes)
    const char *unplaced = strstr(schema, "{\"name\":\"Unplaced\",\"kind\":\"component\"");
    TIDE_CHECK(unplaced != NULL && strstr(unplaced, "\"defaults\":\"00008a42\"") != NULL);
    TIDE_CHECK(strstr(schema, "{\"name\":\"Bump\",\"kind\":\"command\"") != NULL);
    // An event another program hears is after the commands, and listed
    TIDE_CHECK(strstr(schema, "{\"name\":\"Ticked\",\"kind\":\"event\"") != NULL);
    char events[64];
    snprintf(events, sizeof events, "\"events\":[{\"name\":\"Ticked\",\"type\":%u}]", (unsigned)tide_game_layout.type_count + 2u);
    TIDE_CHECK(strstr(schema, events) != NULL);
    TIDE_CHECK(strstr(schema, "\"scenes\":[\"Main\",\"Arena\"]") != NULL || strstr(schema, "\"Arena\"") != NULL);
    // A scene's tag says it's a scene, an ordinary component doesn't (before its fields)
    const char *main_tag = strstr(schema, "{\"name\":\"Main\",\"kind\":\"component\"");
    TIDE_CHECK(main_tag != NULL);
    const char *main_fields = main_tag ? strstr(main_tag, "\"fields\"") : NULL;
    const char *main_scene = main_tag ? strstr(main_tag, "\"scene\":true") : NULL;
    TIDE_CHECK(main_scene != NULL && main_scene < main_fields);
    const char *counter_tag = strstr(schema, counter);
    const char *counter_fields = counter_tag ? strstr(counter_tag, "\"fields\"") : NULL;
    const char *counter_scene = counter_tag ? strstr(counter_tag, "\"scene\":true") : NULL;
    TIDE_CHECK(counter_fields != NULL && (counter_scene == NULL || counter_scene > counter_fields));
    tide_embed_destroy(e);
}

// The match's events, as this machine's local handlers hear them: each frame's packet has those heard since the last
// one, once as predicted and once confirmed, with their text and the entity they were sent to.
TIDE_TEST(embed_heard_events)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    const uint32_t ticked = tide_game_layout.type_count + 2u; // After the commands, Bump and Rename
    int predicted = 0, verified = 0, last_tick = 0;
    bool targets = true, labels = true, ordered = true;
    for (int frame = 0; frame < 75; frame++) {
        frames(e, 1, NULL);
        uint32_t size = 0, count = 0;
        const uint8_t *p = tide_embed_view(e, &size);
        TIDE_REQUIRE(p != NULL);
        const uint8_t *records = section(p, size, TIDE_PACKET_EVENTS, 0, &count, NULL);
        if (!records) continue;
        uint32_t at = 0;
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t record = u32_at(records, at), type = u32_at(records, at + 4), flags = u32_at(records, at + 8);
            const uint32_t tick = u32_at(records, at + 12), place = u32_at(records, at + 24);
            TIDE_REQUIRE(record >= TIDE_PACKET_EVENT && record % 8u == 0u);
            if (type == ticked) {
                Ticked heard;
                memcpy(&heard, records + at + TIDE_PACKET_EVENT, sizeof heard);
                if (flags == 1u) {
                    predicted++;
                    ordered &= (int)tick > last_tick;
                    last_tick = (int)tick;
                } else if (flags == 2u) {
                    verified++;
                }
                // Sent to the counter, which is among the packet's entities, at its handle
                targets &= place < u32_at(p, 36)
                           && u32_at(p, TIDE_PACKET_HEADER + place * TIDE_PACKET_ENTITY) == u32_at(records, at + 16);
                uint32_t length = 0;
                const uint8_t *label = strings_entry(p, size, heard.label.at, &length);
                labels &= label && length == 7u && memcmp(label, "counted", 7) == 0 && heard.ticks % 10 == 0;
                // A list of text: its elements the packet's handles too
                const uint8_t *words = strings_entry(p, size, heard.words.at, &length);
                labels &= words && length == 2u * sizeof(tide_text);
                if (words) {
                    tide_text second;
                    memcpy(&second, words + sizeof second, sizeof second);
                    const uint8_t *b = strings_entry(p, size, second.at, &length);
                    labels &= b && length == 1u && b[0] == 'b';
                }
            }
            at += record;
        }
    }
    TIDE_CHECK(predicted >= 3 && predicted <= 4); // 75 frames are 37 ticks: the counter's 10th, 20th and 30th
    TIDE_CHECK(verified >= predicted - 1 && verified <= predicted);
    TIDE_CHECK(targets);
    TIDE_CHECK(labels);
    TIDE_CHECK(ordered); // Each once, as heard
    tide_embed_destroy(e);
}

// Lists, maps, sets and grids, what they hold the packet's too: a singleton's, as STRINGS lays them out.
TIDE_TEST(embed_containers)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 10, NULL);
    uint32_t size = 0, count = 0, length = 0;
    const uint8_t *p = tide_embed_view(e, &size);
    const uint8_t *at = section(p, size, TIDE_PACKET_SINGLETON, type_named("Shelf"), &count, NULL);
    TIDE_REQUIRE(at != NULL);
    Shelf shelf;
    memcpy(&shelf, at, sizeof shelf);
    // List<string>: handles of text
    const uint8_t *words = strings_entry(p, size, shelf.words.at, &length);
    TIDE_REQUIRE(words && length == 2u * sizeof(tide_text));
    tide_text word;
    memcpy(&word, words + sizeof word, sizeof word);
    const uint8_t *unity = strings_entry(p, size, word.at, &length);
    TIDE_CHECK(unity && length == 5u && memcmp(unity, "unity", 5) == 0);
    // List<Note>: structs whose text is a handle
    const uint8_t *notes = strings_entry(p, size, shelf.notes.at, &length);
    TIDE_REQUIRE(notes && length == sizeof(Note));
    Note note;
    memcpy(&note, notes, sizeof note);
    const uint8_t *first = strings_entry(p, size, note.text.at, &length);
    TIDE_CHECK(note.at == 3 && first && length == 5u && memcmp(first, "first", 5) == 0);
    // Map<string, int>: count, 0, keys (to 8 bytes), values, as they were added
    uint32_t handle;
    memcpy(&handle, &shelf.counts, sizeof handle);
    const uint8_t *counts = strings_entry(p, size, handle, &length);
    TIDE_REQUIRE(counts && length == 8u + 8u + 2u * 4u && u32_at(counts, 0) == 2u);
    tide_text key;
    memcpy(&key, counts + 8u + sizeof key, sizeof key);
    const uint8_t *pears = strings_entry(p, size, key.at, &length);
    TIDE_CHECK(pears && length == 5u && memcmp(pears, "pears", 5) == 0);
    TIDE_CHECK(u32_at(counts, 16) == 2u && u32_at(counts, 20) == 5u);
    // Set<int>: count, 0, keys
    memcpy(&handle, &shelf.ids, sizeof handle);
    const uint8_t *ids = strings_entry(p, size, handle, &length);
    TIDE_CHECK(ids && length == 16u && u32_at(ids, 0) == 2u && u32_at(ids, 8) == 7u && u32_at(ids, 12) == 9u);
    // Grid2<byte>: its shape and size, then the one chunk it has, holding the cell set
    const uint8_t *cells = strings_entry(p, size, shelf.cells.at, &length);
    TIDE_REQUIRE(cells && length >= 48u + 16u);
    TIDE_CHECK(u32_at(cells, 0) == 2u && u32_at(cells, 4) == 1u && u32_at(cells, 20) == 100u && u32_at(cells, 24) == 50u);
    TIDE_CHECK(u32_at(cells, 32) == 1u); // One chunk
    const uint32_t sx = u32_at(cells, 8), sy = u32_at(cells, 12);
    TIDE_CHECK((int32_t)u32_at(cells, 48) == 70 >> sx && (int32_t)u32_at(cells, 52) == 40 >> sy);
    const uint32_t place = (70u & ((1u << sx) - 1u)) | (40u & ((1u << sy) - 1u)) << sx;
    TIDE_CHECK(cells[48u + 16u + place] == 6u);
    tide_embed_destroy(e);
}

// A command from the other program reaches its handler as local code's would; a scene starts by its name.
TIDE_TEST(embed_commands_and_scenes)
{
    char *argv[] = {program};
    tide_embed *e = start(1, argv);
    TIDE_CHECK(e != NULL);
    frames(e, 5, NULL);
    const uint32_t bump = tide_game_layout.type_count; // The first type after the layout's: the game's one command
    const Bump value = {.by = 7};
    TIDE_CHECK(tide_embed_command(e, bump, 0, 0, &value, sizeof value));
    TIDE_CHECK(!tide_embed_command(e, bump, 0, 0, &value, sizeof value - 1u)); // Not its size
    TIDE_CHECK(!tide_embed_command(e, bump + 2u, 0, 0, &value, sizeof value)); // An event, no command
    frames(e, 10, NULL);
    uint32_t size = 0, count = 0;
    const uint8_t *p = tide_embed_view(e, &size);
    const uint8_t *tally = section(p, size, TIDE_PACKET_SINGLETON, type_named("Tally"), &count, NULL);
    TIDE_CHECK(tally != NULL);
    int32_t bumps;
    memcpy(&bumps, tally + offsetof(Tally, bumps), sizeof bumps);
    TIDE_CHECK(bumps == 7);
    // A command's text and list after its value: a count, the entries, their bytes
    const uint32_t rename = bump + 1u;
    uint8_t bytes[sizeof(Rename) + 4 + 16 + 16] = {0};
    Rename named = {0};
    named.name.at = 1;
    named.marks.at = 2;
    memcpy(bytes, &named, sizeof named);
    const uint8_t marks[3] = {7, 8, 9};
    const uint32_t tail[5] = {2, 0, 4, 4, sizeof marks};
    memcpy(bytes + sizeof named, tail, sizeof tail);
    memcpy(bytes + sizeof named + sizeof tail, "tidy", 4);
    memcpy(bytes + sizeof named + sizeof tail + 4, marks, sizeof marks);
    const uint32_t whole = (uint32_t)(sizeof named + sizeof tail + 4 + sizeof marks);
    TIDE_CHECK(!tide_embed_command(e, rename, 0, 0, bytes, whole - 1u)); // The list's bytes cut short
    named.name.at = 3; // No such entry
    memcpy(bytes, &named, sizeof named);
    TIDE_CHECK(!tide_embed_command(e, rename, 0, 0, bytes, whole));
    named.name.at = 1;
    memcpy(bytes, &named, sizeof named);
    bytes[sizeof named + sizeof tail] = 0xFF; // Not UTF-8
    TIDE_CHECK(!tide_embed_command(e, rename, 0, 0, bytes, whole));
    bytes[sizeof named + sizeof tail] = 't';
    TIDE_CHECK(tide_embed_command(e, rename, 0, 0, bytes, whole));
    frames(e, 10, NULL);
    p = tide_embed_view(e, &size);
    tally = section(p, size, TIDE_PACKET_SINGLETON, type_named("Tally"), &count, NULL);
    TIDE_REQUIRE(tally != NULL);
    Tally renamed;
    memcpy(&renamed, tally, sizeof renamed);
    uint32_t length = 0;
    const uint8_t *name = strings_entry(p, size, renamed.name.at, &length);
    TIDE_CHECK(name && length == 4u && memcmp(name, "tidy", 4) == 0);
    const uint8_t *kept = strings_entry(p, size, renamed.marks.at, &length);
    TIDE_CHECK(kept && length == sizeof marks && memcmp(kept, marks, sizeof marks) == 0);
    TIDE_CHECK(!tide_embed_request(e, TIDE_REQUEST_START, "Nowhere", 0));
    TIDE_CHECK(tide_embed_request(e, TIDE_REQUEST_START, "Arena", 0));
    frames(e, 10, NULL);
    tide_embed_status status;
    tide_embed_status_of(e, &status);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    tide_embed_destroy(e);
}
