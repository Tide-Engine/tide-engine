// Several players of one game in one program, each as a host runs it: a
// local state, a session, and what the session asks of the game's own
// network (tide/network.h), through the functions the standard host uses
// (tide/host.h). Nothing here knows which network the game registered: its
// Tide asks Session.Open() and Session.Join(text), and this serves them as a
// host does.
//
// Include after the game's generated header. A game without rooms here: with
// no network registered, an Open or a Join does nothing.
#pragma once

#include <stdio.h>
#include <string.h>

#include "tide/network.h"
#include "tide/session.h"

#define PLAYER_TICKS 64u // Verified ticks whose hashes a player keeps

typedef struct player {
    const char *name;
    tide_local local;
    tide_gui gui;
    tide_devices devices;
    tide_draw_list draw;
    tide_session *session;
    tide_network_match network;
    tide_start start;
    tide_session_status status;
    char room[TIDE_NETWORK_ADDRESS_BYTES]; // Session.room, as the game was last told
    int connected;                         // Connected and Disconnected events so far
    int disconnected;
    uint32_t reason;                       // ...and the last one's reason
    uint32_t ticks[PLAYER_TICKS];          // The last verified ticks, and its world's hash after each
    uint64_t hashes[PLAYER_TICKS];
    // Before anything of this player runs: for a game whose C keeps who's
    // playing, where one program is one player (NULL for none)
    void (*enter)(struct player *p);
} player;

static void player_sample(void *user, const uint32_t tick, void *input)
{
    (void)tick;
#ifdef TIDE_HAS_INPUT
    player *p = user;
    *(tide_input *)input = tide_input_sample(&p->devices, &p->local);
#else
    (void)user;
    (void)input;
#endif
}

// A player as its program starts: in no match where Main is local, and else
// in one of its own.
static void player_init(player *p, const char *name)
{
    p->name = name;
    if (p->enter) p->enter(p);
    tide_local_init(&p->local);
    p->session = tide_session_create(&(tide_session_desc){
        .game = &tide_game_api, .tick_rate = 60, .sample = player_sample, .user = p});
#ifndef TIDE_MAIN_IS_LOCAL
    tide_session_start(p->session, NULL, 0.0); // Main is the match's: one of this machine's own, from the start
#endif
}

static void player_free(player *p)
{
    if (p->enter) p->enter(p);
    tide_session_destroy(p->session);
    p->session = NULL;
    tide_network_match_leave(&p->network);
    tide_local_free(&p->local);
    tide_draw_free(&p->draw);
    tide_draw_free(&p->gui.list);
}

// What local code asked of the session, as tide_run_request serves it.
static void player_request(player *p, const tide_session_request *request, const double now)
{
    tide_session *s = p->session;
    const tide_network *own = tide_network_get();
    tide_transport network;
    tide_address server;
    switch (request->kind) {
    case TIDE_REQUEST_START:
        tide_network_match_leave(&p->network);
        tide_session_start(s, &p->start, now);
        break;
    case TIDE_REQUEST_OPEN:
        if (!tide_session_status_of(s).server || tide_session_open(s, (tide_transport){0})) break;
        if (own && tide_network_match_host(&p->network, own, &network)) tide_session_open(s, network);
        break;
    case TIDE_REQUEST_CLOSE:
        tide_session_close(s);
        break;
    case TIDE_REQUEST_JOIN:
        tide_network_match_leave(&p->network);
        if (!own || !tide_network_match_join(&p->network, own, request->text, &network, &server)) {
            tide_session_leave(s);
            tide_session_fail(s, TIDE_DISCONNECT_FAILED);
            break;
        }
        tide_session_join(s, network, server, now);
        break;
    case TIDE_REQUEST_LEAVE:
        tide_session_leave(s);
        tide_network_match_leave(&p->network);
        break;
    case TIDE_REQUEST_END:
        if (!tide_session_status_of(s).server) break;
        tide_session_end(s);
        tide_network_match_leave(&p->network);
        break;
    default:
        break;
    }
}

// One frame of a player's program, as tide_run_frame goes: the network's
// callbacks, the session, what it has to say, the views and the tasks, and
// what they asked of the session.
static void player_frame(player *p, const double now, const float seconds)
{
    if (p->enter) p->enter(p);
    tide_session *s = p->session;
    tide_local_frame_time(&p->local, seconds);
    tide_event_log *heard = tide_session_events(s);
    tide_network_match_update(&p->network, tide_network_get());
    tide_session_update(s, now);
    tide_network_match_after(&p->network, s, now);
    tide_session_event event;
    while (tide_session_next_event(s, &event)) {
        if (event.kind == TIDE_SESSION_CONNECTED_EVENT) {
            p->connected++;
            tide_local_connected(&p->local);
        } else {
            p->disconnected++;
            p->reason = event.reason;
            tide_local_disconnected(&p->local, event.reason, event.message);
        }
    }
    p->status = tide_session_status_of(s);
    p->room[0] = '\0';
    if (p->status.open || !p->status.server) tide_network_match_address(&p->network, p->room);
    tide_local_set_session(&p->local, p->status.client.state, p->status.client.player, p->status.client.ping_ms,
                           p->status.server, p->status.open, p->room);
    const tide_view_worlds view = tide_session_view(s);
    if (view.verified && p->status.client.state == TIDE_SESSION_CONNECTED) {
        const uint32_t slot = view.verified_tick % PLAYER_TICKS;
        p->ticks[slot] = view.verified_tick;
        p->hashes[slot] = tide_world_hash(view.verified);
    }
    tide_draw_reset(&p->draw);
    tide_touches_poll(&p->devices.touchscreen);
    tide_pointer_poll(&p->devices, true);
    tide_gui_begin(&p->gui, &p->devices, tide_f2(640, 480), NULL);
    tide_local_events(&p->local, heard);
    tide_frame(view.current, view.previous, view.alpha, view.verified, view.verified_previous, view.verified_alpha,
               &p->local, &p->draw, &p->gui);
    tide_session_mark_presented(s);
    tide_gui_end(&p->gui, &p->draw);
    if (p->status.client.state != TIDE_SESSION_CONNECTED) tide_devices_consume(&p->devices);
    tide_session_request request = {0};
    while (tide_local_take_request(&p->local, &request, &p->start)) player_request(p, &request, now);
}

static bool player_in(const player *p)
{
    return p->status.client.state == TIDE_SESSION_CONNECTED;
}

// The verified tick both players have a hash of, the newest, and whether
// their worlds were the same after it: 0 with no tick in common.
static uint32_t players_agree(const player *a, const player *b, bool *same)
{
    uint32_t newest = 0;
    for (uint32_t i = 0; i < PLAYER_TICKS; i++) {
        const uint32_t tick = a->ticks[i];
        if (!tick || tick <= newest || b->ticks[tick % PLAYER_TICKS] != tick) continue;
        newest = tick;
        *same = a->hashes[i] == b->hashes[tick % PLAYER_TICKS];
    }
    return newest;
}

static const char *player_text(player *p, const tide_text text, char *out, const size_t room)
{
    const tide_str read = tide_text_read(&p->local.heap, text);
    const size_t bytes = (size_t)read.bytes < room - 1 ? (size_t)read.bytes : room - 1;
    memcpy(out, read.ptr, bytes);
    out[bytes] = 0;
    return out;
}

static void player_set_text(player *p, tide_text *field, const char *value)
{
    tide_text_use(NULL, &p->local.heap);
    tide_text_set(field, tide_str_from_cstr(value), TIDE_IN_LOCAL);
}
