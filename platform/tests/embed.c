// Tide inside another program's frame (tide/embed.h), as that program runs
// it: platform/tests/embed.tide, its frames' lengths and input made up here.

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "tide/embed.h"
#include "tide/run.h"
#include "tide_test.h"


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
    TIDE_CHECK(strstr(schema, "{\"name\":\"Bump\",\"kind\":\"command\"") != NULL);
    TIDE_CHECK(strstr(schema, "\"scenes\":[\"Main\",\"Arena\"]") != NULL || strstr(schema, "\"Arena\"") != NULL);
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
    TIDE_CHECK(!tide_embed_command(e, bump + 1u, 0, 0, &value, sizeof value)); // No command
    frames(e, 10, NULL);
    uint32_t size = 0, count = 0;
    const uint8_t *p = tide_embed_view(e, &size);
    const uint8_t *tally = section(p, size, TIDE_PACKET_SINGLETON, type_named("Tally"), &count, NULL);
    TIDE_CHECK(tally != NULL);
    int32_t bumps;
    memcpy(&bumps, tally + offsetof(Tally, bumps), sizeof bumps);
    TIDE_CHECK(bumps == 7);
    TIDE_CHECK(!tide_embed_request(e, TIDE_REQUEST_START, "Nowhere", 0));
    TIDE_CHECK(tide_embed_request(e, TIDE_REQUEST_START, "Arena", 0));
    frames(e, 10, NULL);
    tide_embed_status status;
    tide_embed_status_of(e, &status);
    TIDE_CHECK(status.state == TIDE_SESSION_CONNECTED);
    tide_embed_destroy(e);
}
