// Tide inside another program's frame loop (tide/embed.h): the standard
// host's loop (tide/host.h), called once a frame, with what that program's
// window read standing in for a window of Tide's own (embedded.h).

#include "tide/embed.h"

#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "tide/host.h"
#include "embedded.h"

struct tide_embed {
    int unused;
};

static tide_embed the_embed;
static bool running;

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

tide_embed *tide_embed_create(const tide_embed_desc *desc)
{
    if (running || !desc || !desc->game) return NULL;
    running = true;
    tide_run_game = desc->game;
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
    tide_run_begin();
    return &the_embed;
}

void tide_embed_destroy(tide_embed *e)
{
    if (e != &the_embed || !running) return;
    tide_run_end();
    if (tide_run_game->unload) tide_run_game->unload();
    free(tide_run_pack);
    tide_run_pack = NULL;
    tide_run_pack_size = 0;
    tide_platform_set_relay(NULL);
    tide_run_embedded = false;
    running = false;
}

double tide_embed_frame(tide_embed *e, const float seconds, const tide_embed_input *input, const float width, const float height,
                        const float scale)
{
    if (e != &the_embed || !running) return 0.0;
    tide_platform_embed_frame(input, tide_f2(width, height), scale);
    // The session's clock is the frames' sum: a length that's none (NaN too) adds nothing
    tide_run_frame(NULL, seconds >= 0.0f && seconds <= 3600.0f ? seconds : 0.0f);
    return tide_session_until_tick(tide_run_session);
}

void tide_embed_status_of(const tide_embed *e, tide_embed_status *out)
{
    *out = (tide_embed_status){0};
    if (e != &the_embed || !running) return;
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
    if (e != &the_embed || !running) return false;
    tide_session_request request = {.kind = kind, .port = port};
    switch (kind) {
    case TIDE_REQUEST_START:
        if (text) return false; // Main's match: a scene by its name comes with the game's schema
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

void tide_embed_background(tide_embed *e, const bool background)
{
    if (e != &the_embed || !running || !background) return;
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    tide_run_request(&(tide_session_request){.kind = TIDE_REQUEST_LEAVE}, NULL);
#endif
}
