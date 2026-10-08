// The host of every program in this folder (see ../CMakeLists.txt): it runs a game
// that tries to make the generated C or the engine do something undefined,
// through everything a session does with a world. The game says what it found
// with Debug.Log, and the host says "safety: ran to the end" once everything
// is let go, which is what the test waits for.
//
// A player's hand is made up too, the same every run: a press in the middle of
// the screen that's dragged far off it, then keys and typing, so widgets under
// it are pressed, dragged, focused and typed into.
//
// What a game's frames play goes through the code they name, its `audio`
// declarations, as the mixer would run it (run_effect).
//
//     tidec_safety_<name> [ticks] [gpu]
//
// With `gpu`, on desktop, each frame's list also goes through the renderer, in
// a window nobody sees. There the GUI measures text with the platform's font
// too, which needs no window; elsewhere a program that names the renderer
// would need a page or an app to run in.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __wasi__
#include <signal.h>
#endif

#include "game.h"
#include "tide/audio.h"
#include "tide/draw.h"
#include "tide/events.h"
#include "tide/gui.h"

#ifdef TIDE_SAFETY_THREADS
#include "tide/platform.h"
#endif

#ifndef __wasi__
// abort() ends the program by a signal on Linux and macOS, which CTest counts
// as a crash whatever the program printed: end it with an exit code instead.
static void aborted(const int sig)
{
    (void)sig;
    _Exit(3);
}
#endif

static tide_world world;    // The match
static tide_world before;   // ...as it was a tick ago, sharing its pages, as a client's snapshots do
static tide_world verified; // ...and two ticks ago, standing in for what a server confirmed
static tide_world unpacked;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;
static tide_event_log events;
static bool in_match;
#ifdef TIDE_SAFETY_RENDERER
static bool renders;
#endif
static uint32_t frames;

#define WIDTH 640.0f
#define HEIGHT 360.0f

#ifdef TIDE_HAS_AUDIO
// What a frame plays, the host runs as the mixer would (tide/audio.h): each
// voice's and each bus's effect on a block, with state of the bytes the mixer
// has for one, zeros as the code starts and kept from frame to frame, and a
// copy of the knobs the frame gave, in the bytes the mixer has for those. The
// block and each state are between bytes that have to stay as they were made:
// in the mixer, what's past a voice's state is the next voice. The knobs are
// only read, so theirs is an allocation of its own, which a sanitizer watches.
#define GUARD 256u
#define GUARDED 0xA5u
#define KEPT 16u
#define BLOCK_BYTES (TIDE_AUDIO_SAMPLES * (uint32_t)sizeof(float))

typedef struct kept_state {
    uint64_t voice;  // The voice's id, or for a bus, its number from the last id down
    uint64_t code;   // The effect's id: other code starts the state over
    uint32_t played; // The frame it last played in: a voice that stopped starts over too
    _Alignas(16) uint8_t bytes[GUARD + TIDE_AUDIO_STATE_BYTES + GUARD]; // Aligned for whatever a state holds
} kept_state;

static tide_audio_frame audio;
static kept_state kept[KEPT];
static uint32_t kept_count;
static uint32_t effects_run;

static void guard(uint8_t *bytes, const uint32_t size)
{
    memset(bytes, GUARDED, GUARD);
    memset(bytes + GUARD + size, GUARDED, GUARD);
}

static void guarded(const uint8_t *bytes, const uint32_t size, const char *what)
{
    for (uint32_t i = 0; i < GUARD; i++) {
        if (bytes[i] == GUARDED && bytes[GUARD + size + i] == GUARDED) continue;
        fprintf(stderr, "safety: an effect wrote outside its %s\n", what);
        exit(1);
    }
}

static void run_effect(const uint64_t voice, const tide_audio_effect *effect, const uint32_t knobs_at)
{
    if (!effect->fn) return;
    kept_state *k = NULL;
    for (uint32_t i = 0; i < kept_count && !k; i++) {
        if (kept[i].voice == voice) k = &kept[i];
    }
    if (!k) {
        if (kept_count == KEPT) return;
        k = &kept[kept_count++];
        k->voice = voice;
        k->code = 0;
    }
    if (k->code != effect->id || k->played + 1u != audio.frame) {
        guard(k->bytes, TIDE_AUDIO_STATE_BYTES);
        memset(k->bytes + GUARD, 0, TIDE_AUDIO_STATE_BYTES);
        k->code = effect->id;
    }
    k->played = audio.frame;
    uint8_t *knobs = calloc(1, TIDE_AUDIO_KNOBS_BYTES);
    static _Alignas(16) uint8_t block[GUARD + BLOCK_BYTES + GUARD];
    if (!knobs) exit(1);
    if (effect->knobs_size) memcpy(knobs, tide_audio_knobs(&audio, knobs_at, effect->knobs_size), effect->knobs_size);
    guard(block, BLOCK_BYTES);
    float *samples = (float *)(void *)(block + GUARD);
    for (uint32_t i = 0; i < TIDE_AUDIO_SAMPLES; i++) samples[i] = 0.25f;
    effect->fn(samples, k->bytes + GUARD, knobs);
    guarded(block, BLOCK_BYTES, "block");
    guarded(k->bytes, TIDE_AUDIO_STATE_BYTES, "state");
    free(knobs);
    effects_run++;
}

static void play(void)
{
    for (uint32_t i = 0; i < audio.voice_count; i++) {
        const tide_voice *v = &audio.voices[i];
        if (v->id && v->playing) run_effect(v->id, &v->effect, v->knobs_at);
    }
    for (uint32_t b = 0; b < TIDE_AUDIO_BUSES; b++) run_effect(UINT64_MAX - b, &audio.bus_effect[b], audio.bus_knobs_at[b]);
}
#endif

// What the player's hand does on frame `n`, round and round
static void hand(const uint32_t n)
{
    const uint32_t step = n % 16u;
    devices.mouse.position = tide_f2(step == 4u || step == 5u ? 1e9f : step == 6u ? -1e9f : WIDTH * 0.5f, HEIGHT * 0.5f);
    tide_button_set(&devices.mouse.left, step >= 2u && step <= 6u);
    tide_button_set(&devices.keyboard.tab, step == 8u);
    tide_button_set(&devices.keyboard.rightArrow, step == 9u);
    tide_button_set(&devices.keyboard.leftArrow, step == 10u);
    tide_button_set(&devices.keyboard.enter, step == 11u || step == 14u);
    tide_button_set(&devices.keyboard.backspace, step == 13u);
    devices.keyboard.text.count = 0;
    if (step == 12u) { // More digits than a number holds, and a letter past ASCII
        const char *typed = "-99999999999999999999.5e99";
        for (uint32_t i = 0; typed[i] && i + 1u < TIDE_TEXT_MAX; i++) devices.keyboard.text.chars[devices.keyboard.text.count++] = (uint8_t)typed[i];
        devices.keyboard.text.chars[devices.keyboard.text.count++] = 0x1F600u;
    }
}

// What local code asked of the session: a match it starts starts, and one it
// leaves or ends is over. The rest needs other machines.
static void requests(void)
{
    tide_session_request request;
    tide_start start;
    while (tide_local_take_request(&local, &request, &start)) {
        if (request.kind == TIDE_REQUEST_START) {
            tide_world_start(&world, 1.0f / 60.0f, &start);
            tide_world_copy(&before, &world);
            tide_world_copy(&verified, &world);
            in_match = true;
            tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
            tide_local_connected(&local);
        } else if (request.kind == TIDE_REQUEST_LEAVE || request.kind == TIDE_REQUEST_END) {
            if (in_match) tide_local_disconnected(&local, TIDE_DISCONNECT_LEFT, NULL);
            in_match = false;
        }
    }
}

static void frame(const float alpha)
{
    tide_measure_fn measure = NULL;
#ifdef TIDE_SAFETY_RENDERER
    measure = tide_platform_measure_text; // The font's own, which needs no window
#endif
    hand(frames++);
    tide_draw_reset(&draw);
    tide_pointer_poll(&devices, true);
    tide_gui_begin(&gui, &devices, tide_f2(WIDTH, HEIGHT), measure);
    tide_local_frame_time(&local, 1.0f / 60.0f);
    tide_local_events(&local, &events);
#ifdef TIDE_HAS_AUDIO
    tide_audio_reset(&audio);
    tide_local_audio(&local, &audio);
#endif
    tide_frame(in_match ? &world : NULL, in_match ? &before : NULL, alpha, in_match ? &verified : NULL, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
#ifdef TIDE_HAS_AUDIO
    play();
#endif
#ifdef TIDE_SAFETY_RENDERER
    if (renders) {
        uint32_t pixel = 0;
        tide_platform_read_pixels(&draw, &(tide_float2){WIDTH * 0.5f, HEIGHT * 0.5f}, 1, &pixel);
    }
#endif
    requests();
#ifndef TIDE_HAS_INPUT
    tide_devices_consume(&devices); // No tick samples them: what was pressed is this frame's, as the standard host has it
#endif
}

int main(const int argc, char **argv)
{
#ifdef _WIN32
    // Stopping without the debug C runtime's dialog, which would wait for a click
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifndef __wasi__
    signal(SIGABRT, aborted);
#endif
    const int ticks = argc > 1 && strcmp(argv[1], "gpu") != 0 ? atoi(argv[1]) : 6;
    const tide_jobs *jobs = NULL;
#ifdef TIDE_SAFETY_THREADS
    jobs = tide_platform_jobs();
#endif
#ifdef TIDE_SAFETY_RENDERER
    renders = strcmp(argv[argc - 1], "gpu") == 0;
    if (renders) tide_platform_open(&(tide_window_desc){.title = "safety", .width = (int)WIDTH, .height = (int)HEIGHT, .hidden = true});
#endif

    tide_local_init(&local);
#ifndef TIDE_MAIN_IS_LOCAL
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_copy(&before, &world);
    tide_world_copy(&verified, &world);
    in_match = true;
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_player_from_index(0), 0, true, false, "");
    tide_local_connected(&local);
#endif
    frame(1.0f);

    bool joined = false;
    for (int tick = 0; tick < ticks; tick++) {
        if (in_match) {
            if (!joined) tide_world_player_joined(&world, tide_player_from_index(0));
            joined = true;
#ifdef TIDE_HAS_INPUT
            tide_devices sampled = devices;
            tide_gui_hide(&gui, &sampled);
            tide_world_set_input(&world, tide_player_from_index(0), tide_input_sample(&sampled, &local));
            tide_devices_consume(&devices);
#endif
            tide_world_copy(&verified, &before);
            tide_world_copy(&before, &world);
            tide_world_tick_events(&world, jobs, &events);
            (void)tide_world_hash(&world);
            if (tide_world_ended(&world)) in_match = false;
        }
        frame(0.25f);
        frame(1.0f);
    }

    // The world as bytes and back, as a player who joins gets it
    if (in_match) {
        const uint32_t size = tide_world_pack(&world, NULL, 0);
        uint8_t *bytes = malloc(size ? size : 1u);
        if (!bytes || tide_world_pack(&world, bytes, size) != size || !tide_world_unpack(&unpacked, bytes, size)
            || tide_world_hash(&unpacked) != tide_world_hash(&world)) {
            fprintf(stderr, "safety: the world didn't come back from its bytes\n");
            return 1;
        }
        free(bytes);
        // ...which goes on as the world does
        tide_world_tick(&unpacked);
        tide_world_tick(&world);
        if (tide_world_hash(&unpacked) != tide_world_hash(&world)) {
            fprintf(stderr, "safety: the world from bytes went another way\n");
            return 1;
        }
    }

    tide_world_free(&unpacked);
    tide_world_free(&verified);
    tide_world_free(&before);
    tide_world_free(&world);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_event_log_free(&events);
#ifdef TIDE_HAS_AUDIO
    tide_audio_free(&audio);
    if (effects_run) fprintf(stderr, "safety: effects kept to their state\n");
#endif
    fprintf(stderr, "safety: ran to the end\n");
    return 0;
}
