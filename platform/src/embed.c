// Tide inside another program's frame loop (tide/embed.h): the standard
// host's loop (tide/host.h), called once a frame, with what that program's
// window read standing in for a window of Tide's own (embedded.h).

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__wasm__)
#define _DEFAULT_SOURCE // dlopen and readdir under strict C
#endif

#include "tide/embed.h"

#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "tide/embed_game.h"
#include "tide/host.h"
#include "tide/migrate.h"
#include "embedded.h"

#if !defined(__wasm__)
// Loading the game's libraries as tide run's host does, on this host's state
#include "reload_library.inc"
#if !defined(_WIN32)
#include <dirent.h>
#endif
#endif

struct tide_embed {
    int unused;
};

static tide_embed the_embed;
static bool running;
#if !defined(__wasm__)
static bool from_library; // The game is the libraries tide builds in a folder, swapped in as they come
#endif
static tide_packet packet;
// The draw packet (tide_embed_draw), made when it's asked for, from the frame's draw list
static uint8_t *drawn;
static size_t drawn_capacity;
static char *schema;
static uint32_t schema_size;
static const tide_host_game *schema_of; // The build `schema` describes

uint32_t tide_embed_abi(void)
{
    return TIDE_EMBED_ABI;
}

// --seed <n> and --data-dir <folder> from `desc`'s command line, as tide/run.h
// reads them, but a bad one is left out with a line saying so: the program Tide
// is inside of goes on.
static void embed_options(const tide_embed_desc *desc)
{
    for (int i = 1; i + 1 < desc->argc; i++) {
        const char *arg = desc->argv[i], *value = desc->argv[i + 1];
        if (strcmp(arg, "--seed") == 0) {
            char *end = NULL;
            const unsigned long seed = strtoul(value, &end, 10);
            if (end != value && !*end && value[0] >= '0' && value[0] <= '9' && seed <= 2147483647ul) {
                tide_run_seed = (uint32_t)seed;
                tide_run_seeded = true;
            } else {
                fprintf(stderr, "tide: --seed takes a whole number from 0 to 2147483647, not '%s': left out\n", value);
            }
            i++;
        } else if (strcmp(arg, "--data-dir") == 0) {
            if (value[0] && value[0] != '-') tide_run_data_directory = value;
            else fprintf(stderr, "tide: --data-dir takes a nonempty save directory, not '%s': left out\n", value);
            i++;
        }
    }
}

#if !defined(__wasm__)
// The highest n of a file `<prefix><n><suffix>` in `dir`, or 0 for none.
static uint32_t newest(const char *dir, const char *prefix, const char *suffix)
{
    uint32_t most = 0;
    const size_t prefix_size = strlen(prefix), suffix_size = strlen(suffix);
#ifdef _WIN32
    char pattern[4096];
    snprintf(pattern, sizeof pattern, "%s/%s*", dir, prefix);
    WIN32_FIND_DATAA found;
    const HANDLE find = FindFirstFileA(pattern, &found);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        const char *name = found.cFileName;
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    for (const struct dirent *entry; (entry = readdir(d)) != NULL;) {
        const char *name = entry->d_name;
#endif
        const size_t size = strlen(name);
        if (size <= prefix_size + suffix_size || strncmp(name, prefix, prefix_size) != 0
            || strcmp(name + size - suffix_size, suffix) != 0) continue;
        uint64_t n = 0;
        size_t i = prefix_size;
        for (; i < size - suffix_size && name[i] >= '0' && name[i] <= '9' && n <= UINT32_MAX; i++) n = n * 10u + (uint64_t)(name[i] - '0');
        if (i == size - suffix_size && n > most && n <= UINT32_MAX) most = (uint32_t)n;
#ifdef _WIN32
    } while (FindNextFileA(find, &found));
    FindClose(find);
#else
    }
    closedir(d);
#endif
    return most;
}

// The newest build tide made in `dir`, as reload.c loads one, with its pack: NULL after saying why.
static const tide_host_game *open_library(const char *dir)
{
    const uint32_t build = newest(dir, "game-", LIBRARY_SUFFIX);
    if (!build) {
        fprintf(stderr, "tide: no build of the game in %s yet: tide unity makes them\n", dir);
        return NULL;
    }
    const size_t size = strlen(dir) + 1;
    char *kept = tide_alloc(size);
    memcpy(kept, dir, size);
    reload.dir = kept;
    reload.running = reload.loaded = build;
    const tide_host_game *game = load(build, &reload.library, true);
    if (!game) return NULL;
    const uint32_t pack = newest(dir, "pack-", "");
    reload.pack = pack ? pack - 1u : 0u; // check_pack loads the newest from here on
    return game;
}
#endif

tide_embed *tide_embed_create(const tide_embed_desc *desc)
{
    if (running || !desc || (!desc->game && !desc->library_dir)) return NULL;
    const tide_host_game *game = desc->game;
#if !defined(__wasm__)
    from_library = false;
    tide_run_embedded = true; // Before loading: a library that won't load leaves the program going
    if (!game) {
        game = open_library(desc->library_dir);
        from_library = game != NULL;
    }
#endif
    if (!game) {
        tide_run_embedded = false;
        return NULL;
    }
    running = true;
    tide_run_game = game;
    tide_run_embedded = true;
    tide_platform_set_relay(desc->relay);
    tide_platform_embed_frame(NULL, tide_f2(960.0f, 540.0f), 1.0f); // A screen until the first frame says one
    // The host's own parsing ends the program at a flag that's wrong: it gets none of them
    tide_run_configure(&(tide_run_desc){.tick_rate = desc->tick_rate, .game_name = desc->game_name});
    tide_run_settings.argc = desc->argc; // What tide_run_arguments reads: --host, --join and --connect
    tide_run_settings.argv = desc->argv;
    embed_options(desc);
    if (desc->pack && desc->pack_size) {
        uint8_t *pack = tide_alloc(desc->pack_size);
        memcpy(pack, desc->pack, desc->pack_size);
        tide_run_assets_from(pack, desc->pack_size);
    }
#if !defined(__wasm__)
    if (from_library) check_pack(); // The pack tide wrote with the build, where the game has assets
#endif
    tide_run_begin();
    return &the_embed;
}

void tide_embed_destroy(tide_embed *e)
{
    if (e != &the_embed || !running) return;
    if (tide_run_game) {
        tide_run_end();
        if (tide_run_game->unload) tide_run_game->unload();
    }
#if !defined(__wasm__)
    if (from_library && reload.library) {
        library_close(reload.library);
        char path[4096];
        loaded_path(reload.loaded, path, sizeof path);
        remove(path);
    }
    if (from_library) {
        free((char *)(uintptr_t)reload.dir);
        memset(&reload, 0, sizeof reload);
    }
#endif
    free(tide_run_pack);
    tide_run_pack = NULL;
    tide_run_pack_size = 0;
    tide_run_game = NULL;
    tide_packet_free(&packet);
    free(drawn);
    drawn = NULL;
    drawn_capacity = 0;
    free(schema);
    schema = NULL;
    schema_of = NULL;
    tide_platform_set_relay(NULL);
    tide_run_embedded = false;
    running = false;
}

// The frame's view packet, from the worlds the frame's views drew.
static void write_view(void)
{
    const tide_embed_game *embed = tide_run_game ? tide_run_game->embed : NULL;
    if (!embed || !embed->view) {
        tide_packet_begin(&packet);
        tide_packet_end(&packet, tide_run_game ? tide_run_game->game->hash : 0, false, 0, 0, 1.0f, 0);
        return;
    }
    const tide_view_worlds view = tide_session_view(tide_run_session);
    const tide_session_status status = tide_session_status_of(tide_run_session);
    embed->view(view.current, view.previous, view.alpha, tide_run_local, status.client.verified_tick, &packet);
}

double tide_embed_frame(tide_embed *e, const float seconds, const tide_embed_input *input, const float width, const float height,
                        const float scale)
{
    if (e != &the_embed || !running) return 0.0;
    // The session's clock is the frames' sum: a length that's none (NaN too) adds nothing
    const float dt = seconds >= 0.0f && seconds <= 3600.0f ? seconds : 0.0f;
#if !defined(__wasm__)
    if (from_library) { // A new build, as tide run's host takes one: between two frames
        reload.since_check += dt;
        if (reload.since_check >= 0.1f) {
            reload.since_check = 0.0f;
            check();
        }
        if (reload.restart_requested && tide_run_game) {
            check();
            restart();
            reload.restart_requested = false;
        }
    }
#endif
    if (!tide_run_game) return 0.0; // A build that didn't load after a restart: it said why
    tide_platform_embed_frame(input, tide_f2(width, height), scale);
    tide_run_frame(NULL, dt);
#if !defined(__wasm__)
    if (from_library) rejoin(dt);
#endif
    write_view();
    return tide_session_until_tick(tide_run_session);
}

const uint8_t *tide_embed_view(const tide_embed *e, uint32_t *size)
{
    if (size) *size = 0;
    if (e != &the_embed || !running || !packet.size) return NULL;
    if (size) *size = packet.size;
    return packet.bytes;
}

static size_t draw_part(const size_t at, const uint64_t count, const size_t each, uint64_t *total)
{
    const size_t from = (at + 7u) & ~(size_t)7u;
    *total = (uint64_t)from + count * each;
    return from;
}

// The list as the packet has it: its quads (TIDE_DRAW_QUADS) as the meshes they were, a TIDE_DRAW_MESH command over
// their corners and triangles after the list's own, to the bit (tide_draw_quad_corner). A list with none is as it is.
static tide_draw_command *unquad_commands;
static tide_vertex *unquad_vertices;
static uint32_t *unquad_indices;
static size_t unquad_command_room, unquad_vertex_room, unquad_index_room;

static bool unquad_room(void **items, size_t *room, const size_t count, const size_t size)
{
    if (count <= *room) return true;
    void *grown = realloc(*items, count * size);
    if (!grown) return false;
    *items = grown;
    *room = count;
    return true;
}

static bool unquad(const tide_draw_list *d, tide_draw_list *out)
{
    *out = *d;
    if (!d->quad_count) return true;
    const size_t vertices = (size_t)d->vertex_count + 4u * (size_t)d->quad_count;
    const size_t indices = (size_t)d->index_count + 6u * (size_t)d->quad_count;
    if (!unquad_room((void **)&unquad_commands, &unquad_command_room, d->count, sizeof(tide_draw_command))
        || !unquad_room((void **)&unquad_vertices, &unquad_vertex_room, vertices, sizeof(tide_vertex))
        || !unquad_room((void **)&unquad_indices, &unquad_index_room, indices, sizeof(uint32_t))) return false;
    if (d->count) memcpy(unquad_commands, d->commands, d->count * sizeof(tide_draw_command));
    if (d->vertex_count) memcpy(unquad_vertices, d->vertices, d->vertex_count * sizeof(tide_vertex));
    if (d->index_count) memcpy(unquad_indices, d->indices, d->index_count * sizeof(uint32_t));
    uint32_t vertex_count = d->vertex_count, index_count = d->index_count;
    for (uint32_t i = 0; i < d->count; i++) {
        tide_draw_command *c = &unquad_commands[i];
        if (c->kind != TIDE_DRAW_QUADS) continue;
        const uint32_t from = c->mesh.first < d->quad_count ? c->mesh.first : d->quad_count;
        const uint32_t count = c->mesh.count < d->quad_count - from ? c->mesh.count : d->quad_count - from;
        c->kind = TIDE_DRAW_MESH;
        c->mesh.first = index_count;
        c->mesh.count = 6u * count;
        for (uint32_t k = 0; k < count; k++) {
            const tide_draw_quad *q = &d->quads[from + k];
            static const uint32_t corners[4] = {0, 1, 2, 5}; // p, q, r and s of p q r p r s
            static const uint32_t triangles[6] = {0, 1, 2, 0, 2, 3};
            for (uint32_t v = 0; v < 4u; v++) unquad_vertices[vertex_count + v] = tide_draw_quad_corner(q, corners[v]);
            for (uint32_t t = 0; t < 6u; t++) unquad_indices[index_count + t] = vertex_count + triangles[t];
            vertex_count += 4u;
            index_count += 6u;
        }
    }
    out->commands = unquad_commands;
    out->vertices = unquad_vertices;
    out->vertex_count = vertex_count;
    out->indices = unquad_indices;
    out->index_count = index_count;
    out->quads = NULL;
    out->quad_count = 0;
    return true;
}

const uint8_t *tide_embed_draw(const tide_embed *e, uint32_t *size)
{
    if (size) *size = 0;
    if (e != &the_embed || !running || !tide_run_game) return NULL;
    tide_draw_list shown;
    if (!unquad(&tide_run_draw, &shown)) return NULL;
    const tide_draw_list *d = &shown;
    const tide_float2 screen = tide_platform_screen_size();
    const tide_embed_draw_header header = {
        .magic = TIDE_EMBED_DRAW_MAGIC, .version = TIDE_EMBED_DRAW_VERSION,
        .commands = d->count, .shapes = d->shape_count, .vertices = d->vertex_count, .indices = d->index_count,
        .text = d->text_used, .matrices = d->matrix_count, .turns = d->turn_count,
        .command_size = (uint32_t)sizeof(tide_draw_command), .shape_size = (uint32_t)sizeof(tide_draw_shape_data),
        .vertex_size = (uint32_t)sizeof(tide_vertex),
        .screen_width = screen.x, .screen_height = screen.y, .screen_scale = tide_platform_screen_scale().x,
    };
    uint64_t end = sizeof header;
    const size_t commands = draw_part((size_t)end, header.commands, sizeof(tide_draw_command), &end);
    const size_t shapes = draw_part((size_t)end, header.shapes, sizeof(tide_draw_shape_data), &end);
    const size_t vertices = draw_part((size_t)end, header.vertices, sizeof(tide_vertex), &end);
    const size_t indices = draw_part((size_t)end, header.indices, sizeof(uint32_t), &end);
    const size_t text = draw_part((size_t)end, header.text, 1, &end);
    const size_t matrices = draw_part((size_t)end, header.matrices, sizeof(tide_float4x4), &end);
    const size_t turns = draw_part((size_t)end, header.turns, sizeof(tide_float2x2), &end);
    if (end > UINT32_MAX / 2u) return NULL; // No frame draws so much
    if (end > drawn_capacity) {
        uint8_t *grown = realloc(drawn, (size_t)end);
        if (!grown) return NULL;
        drawn = grown;
        drawn_capacity = (size_t)end;
    }
    memset(drawn, 0, (size_t)end); // Padding between the parts too
    memcpy(drawn, &header, sizeof header);
    if (header.commands) memcpy(drawn + commands, d->commands, header.commands * sizeof(tide_draw_command));
    if (header.shapes) memcpy(drawn + shapes, d->shapes, header.shapes * sizeof(tide_draw_shape_data));
    if (header.vertices) memcpy(drawn + vertices, d->vertices, header.vertices * sizeof(tide_vertex));
    if (header.indices) memcpy(drawn + indices, d->indices, header.indices * sizeof(uint32_t));
    if (header.text) memcpy(drawn + text, d->text, header.text);
    if (header.matrices) memcpy(drawn + matrices, d->matrices, header.matrices * sizeof(tide_float4x4));
    if (header.turns) memcpy(drawn + turns, d->turns, header.turns * sizeof(tide_float2x2));
    if (size) *size = (uint32_t)end;
    return drawn;
}

void tide_embed_status_of(const tide_embed *e, tide_embed_status *out)
{
    *out = (tide_embed_status){0};
    if (e != &the_embed || !running || !tide_run_session) return;
    const tide_session_status s = tide_session_status_of(tide_run_session);
    const tide_client_status *c = &s.client;
    *out = (tide_embed_status){
        .state = (uint32_t)c->state,
        .reason = (uint32_t)c->reason,
        .player = c->player.id,
        .ping_ms = c->ping_ms,
        .verified_tick = c->verified_tick,
        .predicted_tick = c->predicted_tick,
        .delay_ticks = c->delay_ticks,
        .resyncs = c->resyncs,
        .sent_bytes = s.sent_bytes,
        .received_bytes = s.received_bytes,
        .server = s.server,
        .open = s.open,
        .typing = tide_platform_embed_typing(),
        .cursor_locked = tide_platform_embed_cursor_lock(),
    };
    memcpy(out->room, tide_run_room, sizeof out->room);
    out->room[sizeof out->room - 1] = '\0';
    memcpy(out->message, c->message, sizeof out->message);
    out->message[sizeof out->message - 1] = '\0';
}

bool tide_embed_request(tide_embed *e, const tide_session_request_kind kind, const char *text, const uint32_t port)
{
    if (e != &the_embed || !running || !tide_run_game) return false;
    tide_session_request request = {.kind = kind, .port = port};
    switch (kind) {
    case TIDE_REQUEST_START: // Main's match, or a scene by its name
        if (text) {
            const tide_embed_game *embed = tide_run_game->embed;
            if (!embed || !embed->start || !embed->start(tide_run_start, text)) return false;
            tide_run_request(&request, tide_run_start);
            return true;
        }
        break;
    case TIDE_REQUEST_JOIN:
    case TIDE_REQUEST_CONNECT:
        if (!text || !text[0]) return false;
        break;
    case TIDE_REQUEST_OPEN:
    case TIDE_REQUEST_CLOSE:
    case TIDE_REQUEST_LEAVE:
    case TIDE_REQUEST_END:
    case TIDE_REQUEST_KICK_ALL:
        break;
    default:
        return false;
    }
    if (text) snprintf(request.text, sizeof request.text, "%s", text);
    tide_run_request(&request, NULL);
    return true;
}

bool tide_embed_command(tide_embed *e, const uint32_t type, const uint32_t target_index, const uint32_t target_generation,
                        const void *value, const uint32_t size)
{
    if (e != &the_embed || !running || !tide_run_game || !value || type > INT32_MAX) return false;
    const tide_embed_game *embed = tide_run_game->embed;
    return embed && embed->command
        && embed->command(tide_run_local, (int32_t)type, target_index, target_generation, value, size);
}

void tide_embed_background(tide_embed *e, const bool background)
{
    if (e != &the_embed || !running || !background || !tide_run_game) return;
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    tide_run_request(&(tide_session_request){.kind = TIDE_REQUEST_LEAVE}, NULL);
#endif
}

static void make_schema(void)
{
    const tide_host_game *game = tide_run_game;
    free(schema);
    schema = NULL;
    schema_size = 0;
    schema_of = game;
    if (!game || !game->embed || !game->embed->schema) return;
    const uint32_t size = game->embed->schema(NULL, 0);
    if (!size) return;
    schema = tide_alloc((size_t)size + 1u);
    schema_size = game->embed->schema(schema, size + 1u);
}

const char *tide_embed_schema(const tide_embed *e, uint32_t *size)
{
    if (size) *size = 0;
    if (e != &the_embed || !running || !tide_run_game) return NULL;
    if (schema_of != tide_run_game) make_schema(); // A new build came in
    if (size) *size = schema_size;
    return schema;
}
