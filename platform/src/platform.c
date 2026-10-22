#include "tide/platform.h"

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "tide/time.h"
#include "tide/wav.h"
#include "files.h"
#include "font.h"
#include "audio_in.h"
#include "audio_out.h"
#include "embedded.h"
#include "tide/vorbis.h"
#include "gpu.h"
#include "mixer.h"
#include "players.h"
#include "png.h"
#include "script.h"
#include "shaders.h"
#include "texture_residency.h"
#include "window.h"

// The renderer's shaders as SPIR-V, for Vulkan, where a build draws with it
#ifdef TIDE_SPIRV
#include "shaders_spirv.h"
#define SPIRV_OF(name) tide_spirv_##name
#define SPIRV_WORDS(name) tide_spirv_##name##_words
#else
#define SPIRV_OF(name) NULL
#define SPIRV_WORDS(name) 0u
#endif

uint8_t *tide_platform_pack(size_t *size)
{
    return tide_window_pack(size);
}

#ifdef __wasm__
#include "tide_web.h"
#endif

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

// Buttons are found by their offset in the device struct, in the order
// window.h has them, which is devices.h's.
#define BUTTON_AT(device, offset) ((tide_button *)((char *)(device) + (offset)))

#define KEY_OFFSET(name) offsetof(tide_keyboard, name),
#define MOUSE_OFFSET(name) offsetof(tide_mouse, name),
#define PAD_OFFSET(name) offsetof(tide_gamepad, name),
#define DPAD_OFFSET(name) offsetof(tide_gamepad, dpad.name),

static const size_t key_offsets[] = {TIDE_KEYBOARD_KEYS(KEY_OFFSET)};
static const size_t mouse_offsets[] = {TIDE_MOUSE_BUTTONS(MOUSE_OFFSET)};
static const size_t pad_offsets[] = {TIDE_GAMEPAD_BUTTONS(PAD_OFFSET) TIDE_DPAD_BUTTONS(DPAD_OFFSET)};

_Static_assert(COUNT_OF(key_offsets) == TIDE_KEY_COUNT, "a tide_key for every key");
_Static_assert(COUNT_OF(pad_offsets) == TIDE_PAD_COUNT, "a tide_pad_button for every gamepad button");

#ifdef __wasm__
static bool web_timer_frames; // Frames on timers instead of animation frames
#endif

static tide_frame_fn run_frame;
static void *run_user;
static uint64_t opened;    // When the window opened (tide_time_now_ns)
static double frame_start; // When the frame running, or the last one, started (now())
static double frame_due;   // When the next one comes while nobody sees the window
static bool due_asked;     // ...as the frame function asked (tide_platform_next_frame)

// The window's size as of the frame's start, in logical pixels, and in the
// pixels it renders.
static int screen_width = 1, screen_height = 1;
static int pixel_width = 1, pixel_height = 1;

// What draws in the window (see gpu.h), which it says once it's open.
static const tide_gpu *gpu;
// The game's cache folder, where the backend keeps the pipelines it compiled
// across runs (tide_gpu.pipeline_cache): known once the host named the game,
// and another for another game, as the backend was last told.
static char *gpu_cache_folder;
static bool gpu_timing;

// The last frames' lengths, for tide_platform_fps.
static float frame_seconds[32];
static unsigned frames_counted;

// A headless window (tide_platform_open_headless): frames draw into pixels of
// the platform's own, at the size and scale it was asked for, in made-up time,
// with made-up devices (script.h). The window under it is never shown, and
// only there for its graphics context.
static bool headless;
static tide_headless_desc headless_run;

// Inside another program's frame (tide/embed.h): what its window read this
// frame stands in for a window of Tide's own, as a headless run's script does,
// and what the game asks of the window is kept for it to do.
static bool embedded;
static tide_embed_input embedded_input;
static uint32_t embedded_text_at, embedded_touch_at; // The next of the frame's characters and touch events
static bool embedded_typing, embedded_lock;
static bool embedded_microphone; // The frame's audio has a voice of the microphone
#ifdef __wasm__
static bool embedded_heard; // The page's audio device is open for the program beside it
#endif
_Static_assert(TIDE_KEY_COUNT <= 8 * sizeof embedded_input.keys, "a bit for every key");

// Whether nobody hears this window: in a run nobody watches (TIDE_HIDDEN,
// tide/page.h) nobody listens either, so it opens no output device and no
// microphone, and its mixer is handed frames and goes unheard, as on a machine
// with neither. Desktop only, as the variable is: a page's sound is its
// browser's to mute.
static bool unheard;

// The microphone: open while frames name a voice of it, closed when none does
static struct {
    bool open;
    uint32_t retry;    // Frames until a device that wouldn't open is tried again
    tide_sound file;   // A headless run's --microphone file
    uint32_t file_at;  // The next frame of it to play
    double file_due;   // Frames of it owed this frame, with the remainder carried
} mic;

static uint32_t headless_frame; // The frame running, or the last one, counted from 1
// What a headless window's mixer played, as stereo floats: `played` frames so
// far, of which the ones from `captured` on haven't gone to a capture yet, and
// the buffer holds the ones from `kept` on. A frame is a 60th of a second,
// which at TIDE_AUDIO_RATE is HEADLESS_AUDIO_FRAMES of audio.
#define HEADLESS_AUDIO_FRAMES (TIDE_AUDIO_RATE / 60u)
static float *headless_audio;
static uint64_t played, captured, kept, audio_capacity;

static double now(void)
{
    return (double)(tide_time_now_ns() - opened) * 1e-9;
}

static void follow_window(void)
{
    if (headless) { // As it was asked for, whatever the display and the window under it are
        screen_width = headless_run.width;
        screen_height = headless_run.height;
        pixel_width = (int)((float)screen_width * headless_run.scale + 0.5f);
        pixel_height = (int)((float)screen_height * headless_run.scale + 0.5f);
        if (pixel_width < 1) pixel_width = 1;
        if (pixel_height < 1) pixel_height = 1;
        return;
    }
    tide_window_size(&screen_width, &screen_height);
    tide_window_pixel_size(&pixel_width, &pixel_height);
}

void tide_platform_open(const tide_window_desc *desc)
{
#if !defined(__wasm__) && !defined(__ANDROID__)
    // Nobody watches this run, so its window can't show, and one that isn't
    // hidden has nothing but its closing to end the run
    if (!desc->hidden && tide_hidden()) {
        fprintf(stderr, "tide: TIDE_HIDDEN is set, so no window shows, and this one's closing is all that would end its run: a "
                        "program that ends by itself opens its window hidden (tide_window_desc.hidden) or headless "
                        "(tide_platform_open_headless)\n");
        exit(2);
    }
    unheard = tide_hidden(); // ...and nobody listens to it either
#endif
    if (!tide_window_open(desc)) exit(1); // It said why
    gpu = tide_window_gpu();
    free(gpu_cache_folder); // Which this backend hasn't been told
    gpu_cache_folder = NULL;
    opened = tide_time_now_ns();
    follow_window();
#ifdef __wasm__
    web_timer_frames = desc->hidden;
#endif
    if (!unheard) tide_audio_out_open(); // With none, it said so, and the mixer goes unheard
}

void tide_platform_open_headless(const tide_headless_desc *desc)
{
#if defined(__wasm__) || defined(__ANDROID__)
    (void)desc;
    fprintf(stderr, "tide: a game runs without a window on desktop only\n");
    exit(2);
#else
    headless_run = *desc;
    if (headless_run.microphone) {
        FILE *f = fopen(headless_run.microphone, "rb");
        uint8_t *bytes = NULL;
        size_t size = 0;
        if (f) {
            fseek(f, 0, SEEK_END);
            const long length = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (length > 0) {
                bytes = tide_alloc((size_t)length);
                size = fread(bytes, 1, (size_t)length, f);
            }
            fclose(f);
        }
        if (!bytes) {
            fprintf(stderr, "tide: --microphone: '%s' can't be read\n", headless_run.microphone);
            exit(2);
        }
        if (!tide_wav_decode(bytes, size, &mic.file) && !tide_vorbis_decode(bytes, size, &mic.file)) {
            fprintf(stderr, "tide: --microphone: '%s' isn't a WAV or an Ogg Vorbis file that decodes\n", headless_run.microphone);
            exit(2);
        }
        free(bytes);
    }
    if (headless_run.width <= 0) headless_run.width = 960;
    if (headless_run.height <= 0) headless_run.height = 540;
    if (!(headless_run.scale > 0.0f)) headless_run.scale = 1.0f;
    const float wide = (float)headless_run.width * headless_run.scale, high = (float)headless_run.height * headless_run.scale;
    if (wide > (float)TIDE_PNG_MAX_SIDE || high > (float)TIDE_PNG_MAX_SIDE) {
        fprintf(stderr, "tide: %d by %d at scale %g is more than a capture's %d pixels a side\n", headless_run.width,
                headless_run.height, (double)headless_run.scale, TIDE_PNG_MAX_SIDE);
        exit(2);
    }
    // Which player of the run this is, if it's one of several: its script's lines and its files are its own
    tide_platform_players_configure(&headless_run);
    if (headless_run.capture) headless_run.capture = tide_platform_players_file(headless_run.capture);
    if (headless_run.capture_audio) headless_run.capture_audio = tide_platform_players_file(headless_run.capture_audio);
    // The script first: a line that's wrong needs no window to say so
    if (headless_run.script && !tide_script_load(headless_run.script)) exit(2);
    if (!headless_run.frames && !tide_script_quits()) {
        fprintf(stderr, "tide: nothing ends this run, and it has no window to close: give it --frames <n>, or its script a "
                        "`quit` line\n");
        exit(2);
    }
    // The other players' processes, started before this one has a window or a thread for them to take after
    tide_platform_players_start();
    if (!tide_window_open(&(tide_window_desc){.title = "", .width = 64, .height = 64, .hidden = true})) {
        exit(tide_platform_players_finish(1)); // It said why
    }
    headless = true;
    gpu = tide_window_gpu();
    free(gpu_cache_folder);
    gpu_cache_folder = NULL;
    opened = tide_time_now_ns();
    follow_window();
#endif
}

// A headless window's mixer renders a frame's worth of audio once the frame
// ran, as a device would have asked for it meanwhile, and keeps what a capture
// can still ask for: the whole run for --capture-audio, what played since the
// last capture-audio line while the script has another, and nothing otherwise.
// So a run that captures nothing holds a frame of it, however long it goes on.
static void headless_audio_frame(void)
{
    if (!headless_run.capture_audio) {
        if (!tide_script_captures_audio()) captured = played;
        if (captured > kept) { // The frames before `captured` go: a capture just took them, so nothing's rendered past it yet
            memmove(headless_audio, headless_audio + (captured - kept) * 2, (size_t)(played - captured) * 2 * sizeof *headless_audio);
            kept = captured;
        }
    }
    const uint64_t need = played - kept + HEADLESS_AUDIO_FRAMES;
    if (need > audio_capacity) {
        uint64_t c = audio_capacity ? audio_capacity : 16u * HEADLESS_AUDIO_FRAMES;
        while (c < need) c *= 2;
        headless_audio = tide_realloc(headless_audio, (size_t)audio_capacity * 2 * sizeof *headless_audio,
                                      (size_t)c * 2 * sizeof *headless_audio);
        audio_capacity = c;
    }
    tide_mixer_render(headless_audio + (played - kept) * 2, HEADLESS_AUDIO_FRAMES);
    played += HEADLESS_AUDIO_FRAMES;
}

// Writes what was played from frame `from` on, which the buffer still has, to
// a WAV at `path`.
static bool capture_audio(const char *path, const uint64_t from)
{
    const uint64_t frames = played - from;
    size_t size = 0;
    uint8_t *wav = frames <= 0xFFFFFFFFu && from >= kept ? tide_wav_encode(headless_audio + (from - kept) * 2, (uint32_t)frames, TIDE_AUDIO_RATE, &size) : NULL;
    errno = 0;
    FILE *file = wav ? fopen(path, "wb") : NULL;
    bool written = file && fwrite(wav, 1, size, file) == size;
    const int error = errno; // Before fclose says something else
    if (file && fclose(file) != 0) written = false;
    free(wav);
    if (!written) {
        fprintf(stderr, "tide: can't write %s: %s\n", path, error ? strerror(error) : "the file isn't whole");
        return false;
    }
    printf("tide: captured %s, %.3f seconds of audio\n", path, (double)frames / (double)TIDE_AUDIO_RATE);
    fflush(stdout);
    return true;
}

bool tide_platform_capture_audio(const char *path)
{
    if (!headless) {
        fprintf(stderr, "tide: no audio to capture: a headless window's is (tide_platform_open_headless)\n");
        return false;
    }
    const uint64_t from = captured;
    captured = played;
    return capture_audio(path, from);
}

static void headless_microphone(void)
{
    if (!mic.file.samples || !mic.file.frames) return;
    mic.file_due += (double)mic.file.rate / 60.0;
    uint32_t n = (uint32_t)mic.file_due;
    mic.file_due -= n;
    while (n) {
        uint32_t run = mic.file.frames - mic.file_at;
        if (run > n) run = n;
        tide_audio_in_push(mic.file.samples + (size_t)mic.file_at * mic.file.channels, run, mic.file.rate, mic.file.channels);
        mic.file_at += run;
        if (mic.file_at == mic.file.frames) mic.file_at = 0;
        n -= run;
    }
}

void tide_platform_audio(const tide_audio_frame *frame)
{
    tide_mixer_submit(frame);
    if (headless) {
        if (frame->microphone) headless_microphone();
        return;
    }
    if (embedded) {
        embedded_microphone = frame->microphone;
#ifndef __wasm__
        return; // The other program plays the mixer, and hears the microphone for it (tide_embed_audio, _microphone)
#else
        // Beside the other program on the page: Tide's program's own AudioWorklet, and the page's microphone
        if (!embedded_heard) {
            embedded_heard = true;
            tide_audio_out_open(); // With none, it said so, and the mixer goes unheard
        }
#endif
    }
    if (frame->microphone && !unheard) {
        if (mic.open) return;
        if (mic.retry) {
            mic.retry--;
            return;
        }
        mic.open = tide_audio_in_open();
        if (!mic.open) mic.retry = 60; // A second, for a player still being asked
    } else if (mic.open) {
        tide_audio_in_close();
        mic.open = false;
        mic.retry = 0;
    }
}

void tide_platform_audio_forget(void)
{
    tide_mixer_forget();
}

// A headless run's frame, once it drew: the pictures its script and its desc
// ask of it. TIDE_KEEP_RUNNING, or the run's exit code.
static int headless_drawn(void)
{
    const int code = tide_script_drawn(headless_frame);
    if (code != 0 && code != TIDE_KEEP_RUNNING) return code;
    const bool last = code == 0 || (headless_run.frames && headless_frame >= headless_run.frames);
    if (last && headless_run.capture && !tide_platform_capture(headless_run.capture)) return 1;
    if (last && headless_run.capture_audio && !capture_audio(headless_run.capture_audio, 0)) return 1;
    return last ? 0 : TIDE_KEEP_RUNNING;
}

static void write_used_keys(void);

_Noreturn static void finish(int code)
{
    code = tide_platform_players_finish(code); // A run of several players ends as one
    tide_platform_gpu_timing(false);
    write_used_keys(); // How the game's shaders were drawn, for its next run to make them as it starts
    if (mic.open) tide_audio_in_close();
    tide_audio_out_close();
#ifdef __wasm__
    tide_web_stop();
#endif
    tide_window_close();
    exit(code);
}

void tide_platform_next_frame(const double seconds)
{
    const double due = frame_start + (seconds > 0.0 ? seconds : 0.0);
    if (!due_asked || due < frame_due) frame_due = due;
    due_asked = true;
}

static int step(void)
{
    const double start = now();
    // A headless window's time is made up: the same on every machine, every run
    const float seconds = headless ? 1.0f / 60.0f : (float)(start - frame_start);
    frame_start = start;
    frame_due = start + 1.0 / 60.0;
    due_asked = false;
    frame_seconds[frames_counted++ % COUNT_OF(frame_seconds)] = seconds;
    follow_window();
    if (headless) tide_script_input(++headless_frame); // In the devices at this frame's poll
    int code = run_frame(run_user, seconds);
    if (headless) headless_audio_frame();
    if (headless && code == TIDE_KEEP_RUNNING) code = headless_drawn();
    // In a run of several players: what this one sent in the frame goes, and what the others sent comes
    if (headless && code == TIDE_KEEP_RUNNING) code = tide_platform_players_step(headless_frame);
    tide_window_present(); // Also takes the system's input
#ifndef __wasm__
    // A window nobody sees (minimized, or an app in the background) has no
    // display's pace to wait for: the next frame comes when the frame function
    // asked, and draws nothing (see tide_platform_draw). It waits on the
    // system, so a window restored or closed meanwhile goes on at once.
    while (tide_window_unseen() && !tide_window_should_close()) {
        const double left = frame_due - now();
        if (left <= 0.0) break;
        tide_window_wait(left);
    }
#endif
    return code;
}

#ifdef __wasm__
// The page calls it for every frame, once tide_platform_run starts the loop:
// seconds until the next one, which the page waits while it's hidden.
__attribute__((export_name("tide_web_frame"))) double tide_web_frame(void)
{
    const int code = step();
    if (code != TIDE_KEEP_RUNNING) finish(code);
    return frame_due - now();
}
#endif

void tide_platform_run(const tide_frame_fn frame, void *user)
{
    run_frame = frame;
    run_user = user;
    frame_start = now();
#ifdef __wasm__
    // Frames on requestAnimationFrame, or as fast as timers allow when hidden.
    // Doesn't return: it unwinds main's stack back to the browser.
    tide_web_run(web_timer_frames);
#else
    for (;;) {
        if (tide_window_should_close()) finish(0);
        const int code = step();
        if (code != TIDE_KEEP_RUNNING) finish(code);
    }
#endif
}

int tide_platform_fps(void)
{
    const unsigned count = frames_counted < COUNT_OF(frame_seconds) ? frames_counted : (unsigned)COUNT_OF(frame_seconds);
    float total = 0.0f;
    for (unsigned i = 0; i < count; i++) total += frame_seconds[i];
    return total > 0.0f ? (int)((float)count / total + 0.5f) : 0;
}

// ---------------------------------------------------------------------------
// Devices

// What the devices say comes from the window, or for a headless one from
// what was made up for it (script.h), or inside another program's frame from
// what its window read (tide/embed.h), which all say it the same way.

void tide_platform_embed_frame(const tide_embed_input *input, const tide_float2 screen_size, const float screen_scale)
{
    embedded = true;
    embedded_input = input ? *input : (tide_embed_input){0};
    if (embedded_input.text_count > TIDE_TEXT_MAX) embedded_input.text_count = TIDE_TEXT_MAX;
    if (embedded_input.touch_count > TIDE_EMBED_TOUCHES) embedded_input.touch_count = TIDE_EMBED_TOUCHES;
    embedded_text_at = embedded_touch_at = 0;
    // A size and scale that make no screen (NaN too) make the smallest one
    const float scale = screen_scale >= 0.25f && screen_scale <= 16.0f ? screen_scale : 1.0f;
    const float width = screen_size.x >= 1.0f && screen_size.x <= 16384.0f ? screen_size.x : 1.0f;
    const float height = screen_size.y >= 1.0f && screen_size.y <= 16384.0f ? screen_size.y : 1.0f;
    screen_width = (int)width;
    screen_height = (int)height;
    pixel_width = (int)((float)screen_width * scale + 0.5f);
    pixel_height = (int)((float)screen_height * scale + 0.5f);
}

bool tide_platform_embed_typing(void)
{
    return embedded_typing;
}

bool tide_platform_embed_cursor_lock(void)
{
    return embedded_lock;
}

bool tide_platform_embed_microphone(void)
{
    return embedded_microphone;
}

static bool key_held(const tide_key key)
{
    if (embedded) return (embedded_input.keys[key / 8] >> (key % 8) & 1u) != 0;
    return headless ? tide_script_key_held(key) : tide_window_key_held(key);
}

static void mouse_state(tide_window_mouse *out)
{
    if (embedded) {
        const tide_embed_input *in = &embedded_input;
        *out = (tide_window_mouse){.x = in->mouse_x, .y = in->mouse_y, .wheel_x = in->wheel_x, .wheel_y = in->wheel_y,
                                   .buttons = in->mouse_buttons, .locked = in->mouse_locked,
                                   .moved_x = in->moved_x, .moved_y = in->moved_y};
    } else if (headless) {
        tide_script_mouse_state(out);
    } else {
        tide_window_mouse_state(out);
    }
}

static void gamepad_state(tide_window_gamepad *out)
{
    if (embedded) {
        const tide_embed_input *in = &embedded_input;
        *out = (tide_window_gamepad){.connected = in->gamepad, .left_x = in->left_x, .left_y = in->left_y,
                                     .right_x = in->right_x, .right_y = in->right_y, .left_trigger = in->left_trigger,
                                     .right_trigger = in->right_trigger, .buttons = in->gamepad_buttons};
    } else if (headless) {
        tide_script_gamepad_state(out);
    } else {
        tide_window_gamepad_state(out);
    }
}

static bool touchscreen(void)
{
    if (embedded) return embedded_input.touchscreen;
    return headless ? tide_script_touchscreen() : tide_window_touchscreen();
}

static bool take_touch(tide_touch_report *out)
{
    if (!embedded) return headless ? tide_script_take_touch(out) : tide_window_take_touch(out);
    while (embedded_touch_at < embedded_input.touch_count) {
        const tide_embed_touch *t = &embedded_input.touches[embedded_touch_at++];
        if (t->phase > TIDE_TOUCH_CANCELED) continue; // Not a phase: left out
        *out = (tide_touch_report){.phase = (tide_touch_phase)t->phase, .source = t->source, .x = t->x, .y = t->y};
        return true;
    }
    return false;
}

static uint32_t take_char(void)
{
    if (embedded) return embedded_text_at < embedded_input.text_count ? embedded_input.text[embedded_text_at++] : 0;
    return headless ? tide_script_take_char() : tide_window_take_char();
}

static void poll_keyboard(tide_keyboard *k)
{
    for (size_t i = 0; i < COUNT_OF(key_offsets); i++) tide_button_set(BUTTON_AT(k, key_offsets[i]), key_held((tide_key)i));
}

// The mouse; true if it was used: it moved or scrolled, or a button went down
// or up.
static bool poll_mouse(tide_mouse *m)
{
    static float last_x, last_y;
    static bool polled;
    tide_window_mouse mouse;
    mouse_state(&mouse);
    // How it moved: where the cursor went or, while the window holds the
    // cursor (Cursor.Lock), what the mouse says it moved, as the cursor stays.
    const float dx = mouse.locked ? mouse.moved_x : polled ? mouse.x - last_x : 0.0f;
    const float dy = mouse.locked ? mouse.moved_y : polled ? mouse.y - last_y : 0.0f;
    last_x = mouse.x, last_y = mouse.y;
    polled = true;
    m->locked = mouse.locked;
    // The window counts from the top left, y down; Devices follows Unity: from
    // the bottom left, y up.
    m->position = tide_f2(mouse.x, (float)screen_height - mouse.y);
    // Delta and scroll add up until the input is sampled.
    m->delta = tide_f2(m->delta.x + dx, m->delta.y - dy);
    m->scroll = tide_f2(m->scroll.x + mouse.wheel_x, m->scroll.y + mouse.wheel_y);
    m->poll_delta = tide_f2(dx, -dy);
    m->poll_scroll = tide_f2(mouse.wheel_x, mouse.wheel_y);
    bool used = dx != 0.0f || dy != 0.0f || mouse.wheel_x != 0.0f || mouse.wheel_y != 0.0f;
    for (size_t i = 0; i < COUNT_OF(mouse_offsets); i++) {
        tide_button *b = BUTTON_AT(m, mouse_offsets[i]);
        const bool held = (mouse.buttons >> i & 1u) != 0;
        used |= held != b->held;
        tide_button_set(b, held);
    }
    return used;
}

// Fingers on a touchscreen: the page's on the web, the window's on Windows,
// the activity's on Android.
static void poll_touches(tide_touchscreen *s)
{
    tide_touches_poll(s);
    s->connected = touchscreen();
    tide_touch_report r;
    while (take_touch(&r)) {
        tide_touch_event(s, r.phase, r.source, tide_f2(r.x, (float)screen_height - r.y));
    }
}

static void poll_gamepad(tide_gamepad *g)
{
    tide_window_gamepad pad;
    gamepad_state(&pad);
    g->connected = pad.connected;
    if (pad.connected) {
        g->leftStick = tide_f2(pad.left_x, -pad.left_y);
        g->rightStick = tide_f2(pad.right_x, -pad.right_y);
        g->leftTrigger = pad.left_trigger;
        g->rightTrigger = pad.right_trigger;
    } else {
        g->leftStick = g->rightStick = tide_f2(0.0f, 0.0f);
        g->leftTrigger = g->rightTrigger = 0.0f;
    }
    for (size_t i = 0; i < COUNT_OF(pad_offsets); i++) {
        tide_button_set(BUTTON_AT(g, pad_offsets[i]), pad.connected && (pad.buttons >> i & 1u) != 0);
    }
}

// What Ctrl+V (Cmd+V on macOS) pasted, as characters typed: the next to type
// is pasted[pasted_at]. On the web, the page does it (tide.js).
static uint32_t pasted[1024];
static uint32_t pasted_count, pasted_at;

static void paste(void)
{
    // Never the system's clipboard into a headless run, and another program's window types what it pastes
    const char *text = headless || embedded ? NULL : tide_window_take_paste();
    if (!text) return;
    for (uint32_t c; pasted_count < COUNT_OF(pasted) && (c = tide_utf8_next(&text)) != 0;) {
        if (c >= 32 && c != 127) pasted[pasted_count++] = c; // Not newlines or tabs
    }
}

// Characters typed since the last poll, which follow the keyboard layout. A
// poll takes as many as it holds, and leaves the rest for the next.
static void poll_text(tide_typed *text)
{
    text->count = 0;
    paste();
    while (pasted_at < pasted_count && text->count < TIDE_TEXT_MAX) text->chars[text->count++] = pasted[pasted_at++];
    if (pasted_at == pasted_count) pasted_at = pasted_count = 0;
    while (text->count < TIDE_TEXT_MAX) {
        const uint32_t c = take_char();
        if (c == 0) break;
        text->chars[text->count++] = c;
    }
}

void tide_platform_typing(const bool typing)
{
    static bool was;
    embedded_typing = typing;
    if (typing == was || headless || embedded) return;
    was = typing;
    tide_window_typing(typing);
}

void tide_platform_cursor_lock(const bool lock)
{
    embedded_lock = lock;
    if (embedded) return; // The other program's window holds it, or not (tide_embed_input.mouse_locked)
    if (headless) {
        tide_script_cursor_lock(lock);
        return;
    }
    tide_window_cursor_lock(lock); // Every frame: what holds the cursor changes under it (the focus, the browser)
}

void tide_platform_copy(const char *text)
{
    // A run nobody watches leaves the clipboard of whoever is at the machine alone, and another program's
    // window has no clipboard of Tide's
    if (headless || embedded) return;
    tide_window_copy(text);
}

bool tide_platform_input(const char *event)
{
    char error[512] = "";
    if (!headless) snprintf(error, sizeof error, "only a headless window's input is made up (tide_platform_open_headless)");
    else if (tide_script_event(event, error, sizeof error)) return true;
    fprintf(stderr, "tide: input '%s': %s\n", event, error);
    return false;
}

void tide_platform_poll(tide_devices *devices)
{
    poll_keyboard(&devices->keyboard);
    const bool mouse_used = poll_mouse(&devices->mouse);
    poll_gamepad(&devices->gamepad);
    poll_touches(&devices->touchscreen);
    tide_pointer_poll(devices, mouse_used);
    poll_text(&devices->keyboard.text);
}

// ---------------------------------------------------------------------------
// The renderer: a draw list's commands, through gpu.h, whichever API is under it.

// The camera maps world units (y up) to window pixels (y down). Each frame's
// list starts at the origin with 1 unit per pixel. After TIDE_DRAW_SCREEN, it
// maps the screen's pixels instead: from the top left, y down.
typedef struct camera {
    tide_float2 center;
    float scale; // Pixels per world unit
    bool gui;
} camera;

static camera last_camera = {{0.0f, 0.0f}, 1.0f, false};
static float draw_width, draw_height;           // The selected target's logical pixels, during a draw
static int draw_pixel_width, draw_pixel_height; // ...and its image's own

static tide_float2 to_screen(const camera *cam, const tide_float2 p)
{
    if (cam->gui) return tide_f2(p.x * cam->scale, p.y * cam->scale);
    const float width = draw_width > 0.0f ? draw_width : (float)screen_width;
    const float height = draw_height > 0.0f ? draw_height : (float)screen_height;
    return tide_f2(width * 0.5f + (p.x - cam->center.x) * cam->scale, height * 0.5f - (p.y - cam->center.y) * cam->scale);
}

// A color as the GPU takes it: a byte each.
typedef struct rgba {
    uint8_t r, g, b, a;
} rgba;

// A color as the bytes a GPU gets for it: the list's own conversion (tide_draw_color_bytes), which plain shapes are
// recorded with, so every color is the same bytes whichever converted it.
static rgba to_rgba(const tide_color c)
{
    const uint32_t bytes = tide_draw_color_bytes(c);
    rgba out;
    memcpy(&out, &bytes, sizeof out);
    return out;
}

// Shapes go to the GPU as instances of one quad, each its shape in window
// pixels, so a run of them between other commands is one draw call, however
// many there are. The first kinds fill whole pixels (rects, their outlines and
// lines) or are plain circles; the ones from SHAPE_BOX on are the shapes with
// smooth edges (TIDE_DRAW_SHAPE), which a pixel is in by how far it is from
// their edge.

enum { SHAPE_RECT, SHAPE_WIRE_RECT, SHAPE_CIRCLE, SHAPE_WIRE_CIRCLE, SHAPE_LINE,
       SHAPE_BOX, SHAPE_ELLIPSE, SHAPE_ARC };

typedef struct shape {
    float a[2];       // Its middle, or a line's start
    float b[2];       // Half its size (a circle's radius, twice), or a line's end
    uint8_t color[4]; // RGBA: a smooth shape's fill at its top
    float kind;       // SHAPE_*
} shape;

// What a smooth shape is drawn with besides. Few shapes are smooth ones, and
// these are more than a shape again, so only a run that has some has them:
// one for each of its shapes, in a buffer of their own, which a pipeline of
// its own reads (see STEP_SHAPES).
typedef struct shape_extra {
    float turn[2];     // Its rotation's cos and sin, on the screen; an arc's, to its middle
    float radius;      // Its corners'
    float outline;     // How wide its outline is, inside its edge
    float soft;        // How far its edge fades: outwards, or under 0, inwards
    float sweep;       // SHAPE_ARC: half the angle it covers
    uint8_t color2[4]; // Its fill at its bottom
    uint8_t rim[4];    // Its outline's color
} shape_extra;

// Whether a smooth shape is a solid one, which needs no more of its extra than
// its corners' radius to be drawn: a box or an ellipse, unturned, one color
// from top to bottom, with no outline and no fade. Of the rest, an arc's sweep
// and an outline's color are read by none of those, and the rest is zeros and
// ones. A run whose smooth shapes are all solid ones has a radius for each of
// its shapes (4 bytes) where it would have an extra (32), and goes through a
// pipeline that has the rest as constants in place of the extra's, into the
// same lines of the same shaders (shaders.h): the same pixels. That's round
// rects, plain circles on a scaled display, and plain shapes among either.
static inline bool solid_shape(const shape *s, const shape_extra *e)
{
    return s->kind != (float)SHAPE_ARC && e->turn[0] == 1.0f && e->turn[1] == 0.0f && e->outline == 0.0f
        && e->soft == 0.0f && memcmp(e->color2, s->color, sizeof e->color2) == 0;
}

// A mesh's corner, in window pixels. Meshes go to the GPU as triangles, three
// of these each: a run of them with one texture is one draw call. Text goes
// the same way, two triangles a glyph, its uv in the font atlas's pixels.
typedef struct mesh_vertex {
    float position[2];
    float uv[2];
    uint8_t color[4]; // RGBA
} mesh_vertex;

// Two of those triangles, as one instance of the quads' pipelines, where they
// can be: their six corners' places take two numbers at most on each axis, as
// their uvs do, and they're of one color. That's a sprite, a tile or a glyph
// that isn't turned, in whatever order its corners and triangles come. Each
// corner takes one number or the other on each axis, so the GPU draws the same
// triangles from the same numbers as from six of the vertices above: the same
// pixels, from 40 bytes where those are 120. A run of them is one draw call.
typedef struct quad {
    float box[4];     // The two numbers its places take on each axis, in window pixels: x and y, then the others
    float span[4];    // ...and its uvs'
    uint8_t color[4]; // RGBA
    int32_t pattern;  // Which each corner takes, 4 bits a corner from the first: x, y, u and v, 1 for the second
} quad;

// What a list comes to, in order: runs of shapes, clears, runs of text, runs
// of triangles, clips, runs of instances of 3D meshes, and runs of instances
// of meshes through shaders of the game's. A STEP_STENCIL only ends the run
// before it: the steps after it have another stencil than the ones before.
enum { STEP_SHAPES, STEP_CLEAR, STEP_TEXT, STEP_MESH, STEP_CLIP, STEP_NO_CLIP, STEP_MESH_3D, STEP_SHADED,
       STEP_TARGET, STEP_BLIT, STEP_STENCIL };

typedef struct draw_step {
    uint32_t kind;    // STEP_*
    uint32_t blend;   // How it goes over what's there: tide_blend
    uint32_t depth;   // STEP_MESH_3D, and a STEP_SHADED through a 3D shader: what it does with depth, tide_depth
    uint32_t cull;    // ...and which of its triangles don't draw, as they reach the GPU: tide_gpu_cull
    uint32_t stencil; // What it does with its destination's stencil: a tide_stencil, packed. None, for a clear and a blit
    uint32_t target;  // The destination: 0 for the window, 1 + its place in the list's targets
    uint32_t first;   // STEP_SHAPES: its shapes; STEP_TEXT and STEP_MESH: its vertices;
                      // STEP_MESH_3D: its instances' transforms in the list's matrices;
                      // STEP_SHADED: its run's place in the list's
    uint32_t count;
    uint32_t texture; // STEP_MESH and STEP_MESH_3D: 1 + its texture's place in the list's, or 0 for none
    uint32_t filter;  // ...and how it's sampled: tide_filter...
    uint32_t wrap;    // ...and tide_wrap
    uint32_t mesh;    // STEP_MESH_3D: 1 + its mesh's place in the list's
    uint32_t camera;  // ...and 1 + its camera's matrix's place in the list's, or 0 for the first camera:
                      // a STEP_SHADED's too, for a 3D shader
    uint32_t fit;     // ...which is for a square screen (see TIDE_DRAW_CAMERA_3D)
    uint32_t smooth;  // STEP_SHAPES: for a run with smooth shapes, 1 + where its shapes' extras start; else 0
    uint32_t solid;   // ...or for one whose smooth shapes are all solid ones, 1 + where their radii start (see
                      // solid_shape), where `smooth` is 0
    uint32_t quads;   // STEP_TEXT and STEP_MESH: 1 while every two of its triangles are a quad, which `first` and
                      // `count` are then of, else 0 (see quad)
    camera through;   // STEP_SHADED: the camera its command was under, for a 2D shader
    tide_gpu_id pipeline; // What draws it, once it's found: its shader's, for a STEP_SHADED
    tide_color tint;  // STEP_MESH_3D: what its pixels' colors are multiplied by
    tide_float2 at;   // STEP_CLIP: its top left corner in window pixels
    tide_float2 to;   // ...and its bottom right one
    rgba color;       // STEP_CLEAR
    bool depth_alone; // ...which sets depth back and leaves colors and stencil
} draw_step;

static shape *shapes;
static uint32_t shape_count, shape_capacity;
static shape_extra *shape_extras;
static uint32_t shape_extra_count, shape_extra_capacity;
static float *shape_radii;
static uint32_t shape_radius_count, shape_radius_capacity;
static mesh_vertex *mesh_vertices;
static uint32_t mesh_vertex_count, mesh_vertex_capacity;
static quad *quads;
static uint32_t quad_count, quad_capacity;
static draw_step *steps;
static uint32_t step_count, step_capacity;
static uint32_t draw_target;
static uint32_t draw_blend; // How the command being turned into steps blends: tide_blend
static uint32_t draw_stencil; // What the selected destination's commands do with its stencil, by now: a tide_stencil, packed
static uint32_t draw_calls; // What the last list drawn took, the overlay's aside (tide_platform_draw_calls)
static uint64_t draw_vertices; // Submitted vertices, including every index and instance

// A target's cameras and clip belong to it for the whole frame, including
// passes recorded by different views. The clip is advanced during playback.
typedef struct frame_target {
    camera through, world;
    uint32_t camera_3d, fit;
    bool mirrored; // Its 3D camera mirrors what it sees, so triangles reach the GPU wound the other way
    uint32_t stencil; // What its commands do with its stencil, as the last one turned into steps found it
    draw_step clipping;
    tide_gpu_id gpu;
    float width, height;           // Its logical pixels, as the window's
    int pixel_width, pixel_height; // ...and its image's own
    uint64_t id;
    uint64_t version;
    tide_target_size size; // As the list recorded it
    bool valid;
    bool bare; // It holds depth alone (TIDE_TARGET_DEPTH): a color has nowhere to go there
    tide_gpu_target_format into; // What it holds, which the pipelines that draw into it are made for
} frame_target;

static frame_target *frame_targets;
static uint32_t frame_target_capacity;

// What a list is drawn into, where its commands for the window go: the
// window, a headless one's frame, or pixels to read back. It has a size in
// logical pixels and one in its own, and how many of the second are in the
// first is the display's scale for this draw: the list's render targets that
// are at the display's scale take it from here, as its text does.
typedef struct draw_root {
    tide_gpu_id gpu;
    float width, height;
    int pixel_width, pixel_height;
} draw_root;

static draw_root root; // The draw's, while a list is drawn

// The most pixels a side a render target's image has on the device there is
// (tide_gpu.target_limit), or 0 before one drew.
static int target_most;

// Whether the selected destination's pixels aren't logical ones: a window's
// on a scaled display, or a render target's at another scale than 1. Circles
// are drawn as smooth shapes there (see add_circle).
static bool scaled;

// The window's pixels in a logical one, which its text is fitted to (see font.h).
static tide_float2 window_scale(void)
{
    return tide_f2((float)pixel_width / (float)screen_width, (float)pixel_height / (float)screen_height);
}

// The selected destination's pixels per logical pixel.
static tide_float2 pixel_scale(void)
{
    if (!(draw_width > 0.0f)) return window_scale(); // Not in a draw
    return tide_f2((float)draw_pixel_width / draw_width, (float)draw_pixel_height / draw_height);
}

// A whole number of pixels for `pixels` of them: the nearest, 1 at least.
static int whole_pixels(const double pixels)
{
    // A size is any number a game says, and before the first draw nothing
    // has said what the GPU takes: more than an int holds is the most of one
    if (!(pixels >= 1.5)) return 1;
    return pixels < 2147483647.0 ? (int)floor(pixels + 0.5) : INT32_MAX;
}

// A render target's size as it's drawn under `to`: its logical pixels, like
// the window's, and its image's own. One at the display's scale has as many
// of those in a logical one as `to` has, and one that follows the window has
// `to`'s size times its factor, in both. An image the GPU has no room for
// keeps its shape with fewer pixels (`limited`): what's drawn into it still
// lands where it would, less sharp. False for a size there's no image of.
static bool target_extent(const tide_target_size size, const draw_root *to, float *width, float *height,
                          int *wide_pixels, int *high_pixels, bool *limited)
{
    *width = *height = 0.0f;
    *wide_pixels = *high_pixels = 0;
    *limited = false;
    if (!tide_target_size_valid(size) || !(to->width > 0.0f) || !(to->height > 0.0f)) return false;
    const tide_float2 logical = tide_target_size_logical(size, tide_f2(to->width, to->height));
    if (!(logical.x > 0.0f) || !(logical.y > 0.0f)) return false;
    double wide, high;
    if (size.window > 0.0f) { // The window's own pixels, times the factor: all of them for 1
        wide = (double)to->pixel_width * (double)size.window;
        high = (double)to->pixel_height * (double)size.window;
    } else if (size.scale > 0.0f) {
        wide = (double)size.width * (double)size.scale;
        high = (double)size.height * (double)size.scale;
    } else {
        wide = (double)size.width * (double)to->pixel_width / (double)to->width;
        high = (double)size.height * (double)to->pixel_height / (double)to->height;
    }
    const double longest = wide > high ? wide : high;
    if (target_most > 0 && floor(longest + 0.5) > (double)target_most) {
        wide *= (double)target_most / longest;
        high *= (double)target_most / longest;
        *limited = true;
    }
    *width = logical.x;
    *height = logical.y;
    *wide_pixels = whole_pixels(wide);
    *high_pixels = whole_pixels(high);
    return true;
}

static shape_extra *extra_room(const uint32_t count)
{
    if (count > shape_extra_capacity - shape_extra_count) {
        uint64_t capacity = shape_extra_capacity ? shape_extra_capacity : 256u;
        while (capacity < (uint64_t)shape_extra_count + count) capacity *= 2u;
        if (capacity > UINT32_MAX / sizeof(shape_extra)) tide_out_of_memory();
        shape_extras = tide_realloc(shape_extras, shape_extra_capacity * sizeof(shape_extra),
                                    (size_t)capacity * sizeof(shape_extra));
        shape_extra_capacity = (uint32_t)capacity;
    }
    return shape_extras + shape_extra_count;
}

static shape *shape_room(const uint32_t count)
{
    if (count > shape_capacity - shape_count) {
        uint64_t capacity = shape_capacity ? shape_capacity : 1024u;
        while (capacity < (uint64_t)shape_count + count) capacity *= 2u;
        if (capacity > UINT32_MAX / sizeof(shape)) tide_out_of_memory();
        shapes = tide_realloc(shapes, shape_capacity * sizeof(shape), (size_t)capacity * sizeof(shape));
        shape_capacity = (uint32_t)capacity;
    }
    return shapes + shape_count;
}

// Every shape of a run that has smooth ones has an extra, in the run's order.
// Plain shapes don't say so as they're added, which keeps adding one cheap:
// the ones added since the run's last smooth shape get theirs here, zeroed,
// as nothing reads them. Called before the run's next smooth
// shape, and once the run is over.
static void settle_extras(void)
{
    if (step_count == 0) return;
    const draw_step *run = &steps[step_count - 1];
    if (run->kind != STEP_SHAPES || !run->smooth) return;
    const uint32_t missing = run->count - (shape_extra_count - (run->smooth - 1u));
    if (missing == 0) return;
    memset(extra_room(missing), 0, (size_t)missing * sizeof(shape_extra));
    shape_extra_count += missing;
}

static float *radius_room(const uint32_t count)
{
    if (count > shape_radius_capacity - shape_radius_count) {
        uint64_t capacity = shape_radius_capacity ? shape_radius_capacity : 1024u;
        while (capacity < (uint64_t)shape_radius_count + count) capacity *= 2u;
        if (capacity > UINT32_MAX / sizeof(float)) tide_out_of_memory();
        shape_radii = tide_realloc(shape_radii, shape_radius_capacity * sizeof(float), (size_t)capacity * sizeof(float));
        shape_radius_capacity = (uint32_t)capacity;
    }
    return shape_radii + shape_radius_count;
}

// ...and the same for a run of solid shapes' radii (see solid_shape): the
// plain shapes since its last smooth one get theirs, 0.
static void settle_radii(void)
{
    if (step_count == 0) return;
    const draw_step *run = &steps[step_count - 1];
    if (run->kind != STEP_SHAPES || !run->solid) return;
    const uint32_t missing = run->count - (shape_radius_count - (run->solid - 1u));
    if (missing == 0) return;
    memset(radius_room(missing), 0, (size_t)missing * sizeof(float));
    shape_radius_count += missing;
}

// The run of shapes `run`, the last there is, has a smooth shape that isn't a
// solid one: its shapes get the extras they stood for (the plain ones', zeros),
// in place of their radii, the last there are, and it goes on as a run of
// smooth shapes.
static void smooth_instead(draw_step *run)
{
    settle_radii();
    const uint32_t radii = run->solid - 1u;
    shape_extra *out = extra_room(run->count);
    for (uint32_t i = 0; i < run->count; i++) {
        const shape *s = &shapes[run->first + i];
        out[i] = (shape_extra){0};
        if (!(s->kind >= (float)SHAPE_BOX)) continue;
        out[i].turn[0] = 1.0f;
        out[i].radius = shape_radii[radii + i];
        memcpy(out[i].color2, s->color, sizeof out[i].color2);
    }
    run->smooth = shape_extra_count + 1u;
    run->solid = 0;
    shape_extra_count += run->count;
    shape_radius_count = radii;
}

static draw_step *add_step(const uint32_t kind)
{
    settle_extras(); // The run before it, if it was one of shapes, is over
    settle_radii();
    if (step_count == step_capacity) {
        const uint32_t capacity = step_capacity ? step_capacity * 2u : 64u;
        steps = tide_realloc(steps, step_capacity * sizeof(draw_step), capacity * sizeof(draw_step));
        step_capacity = capacity;
    }
    draw_step *s = &steps[step_count++];
    // A clear sets stencil back whatever is set, and a blit is its destination's whatever is
    *s = (draw_step){.kind = kind, .blend = draw_blend, .target = draw_target,
                     .stencil = kind == STEP_CLEAR || kind == STEP_BLIT ? 0u : draw_stencil};
    return s;
}

// Whether what's drawn next goes on with the last step: it's of that kind, and blends as it does.
static bool run_goes_on(const uint32_t kind)
{
    return step_count != 0 && steps[step_count - 1].kind == kind && steps[step_count - 1].blend == draw_blend;
}

// A run of shapes starts. What multiplies goes through the pipeline that
// draws every kind, the one whose colors go to white where they're clear, so
// each of its shapes has an extra (see settle_extras).
static void start_shapes(void)
{
    draw_step *s = add_step(STEP_SHAPES);
    s->first = shape_count;
    if (draw_blend == TIDE_BLEND_MULTIPLY) s->smooth = shape_extra_count + 1u;
}

// A shape, after the ones before it when nothing came between them. A frame
// of plain shapes spends its time here.
static void add_shape(const int kind, const float ax, const float ay, const float bx, const float by, const rgba color)
{
    if (shape_count == shape_capacity) {
        const uint32_t capacity = shape_capacity ? shape_capacity * 2u : 1024u;
        if (capacity > UINT32_MAX / sizeof(shape)) tide_out_of_memory();
        shapes = tide_realloc(shapes, shape_capacity * sizeof(shape), capacity * sizeof(shape));
        shape_capacity = capacity;
    }
    if (!run_goes_on(STEP_SHAPES)) start_shapes();
    steps[step_count - 1].count++;
    shapes[shape_count++] = (shape){{ax, ay}, {bx, by}, {color.r, color.g, color.b, color.a}, (float)kind};
}

// A smooth shape in window pixels, with its extra. The first one of a run
// makes it a run of smooth shapes, whose every shape has an extra.
static void add_smooth(const int kind, const float x, const float y, const float half_width, const float half_height,
                       const rgba color, const shape_extra extra)
{
    if (!run_goes_on(STEP_SHAPES)) start_shapes();
    draw_step *run = &steps[step_count - 1];
    if (!run->smooth) { // A run of solid shapes while they are
        if (!run->solid) run->solid = shape_radius_count + 1u;
        settle_radii(); // The plain shapes before it in the run
        const shape s = {{x, y}, {half_width, half_height}, {color.r, color.g, color.b, color.a}, (float)kind};
        if (solid_shape(&s, &extra)) {
            add_shape(kind, x, y, half_width, half_height, color);
            *radius_room(1) = extra.radius;
            shape_radius_count++;
            return;
        }
        smooth_instead(run);
    }
    settle_extras(); // The plain shapes before it in the run
    add_shape(kind, x, y, half_width, half_height, color);
    *extra_room(1) = extra;
    shape_extra_count++;
}

// The TIDE_DRAW_SHAPE commands from `at` on, one after another with one
// blend, each as add_smooth adds a shape and its extra, through the camera, in
// one loop (a frame of many smooth shapes spends its time here). Returns where
// the run ends.
static uint32_t add_smooth_run(const tide_draw_list *list, uint32_t at, const camera *cam)
{
    static const int kinds[] = {[TIDE_SHAPE_BOX] = SHAPE_BOX, [TIDE_SHAPE_ELLIPSE] = SHAPE_ELLIPSE,
                                [TIDE_SHAPE_ARC] = SHAPE_ARC};
    const uint8_t blend = list->commands[at].blend;
    if (!run_goes_on(STEP_SHAPES)) start_shapes();
    draw_step *const run = &steps[step_count - 1u];
    if (!run->smooth && !run->solid) run->solid = shape_radius_count + 1u; // A run of solid shapes while they are
    settle_extras(); // The plain shapes before them in the run
    settle_radii();
    const float scale = cam->scale;
    for (;;) {
        const uint32_t left = list->count - at, piece = left < 4096u ? left : 4096u;
        shape *out = shape_room(piece);
        // Room for each shape's extra, or while the run's are solid shapes, for its radius
        shape_extra *extra = run->smooth ? extra_room(piece) : NULL;
        float *radius = run->smooth ? NULL : radius_room(piece);
        uint32_t taken = 0;
        for (; taken < piece; taken++) {
            const tide_draw_command *c = &list->commands[at + taken];
            if (c->kind != TIDE_DRAW_SHAPE || c->blend != blend) break;
            if (c->shape >= list->shape_count) continue;
            const tide_draw_shape_data *s = &list->shapes[c->shape];
            const uint32_t form = s->form & (uint32_t)TIDE_SHAPE_FORMS;
            if (form > TIDE_SHAPE_ARC) continue;
            // Its lengths in window pixels, and its angles as the screen has them, where y goes down
            const tide_float2 center = to_screen(cam, c->a);
            const rgba color = to_rgba(c->color);
            *out = (shape){{center.x, center.y}, {c->b.x * scale * 0.5f, c->b.y * scale * 0.5f},
                           {color.r, color.g, color.b, color.a}, (float)kinds[form]};
            const float outline = s->form & (uint32_t)TIDE_SHAPE_HAIRLINE ? 1.0f : s->outline * scale, soft = s->soft * scale;
            // A solid one (solid_shape's test, on the numbers its extra would have), while the run's are: its radius
            // alone, its extra never made. The same color's bits are the same bytes
            if (radius && form != TIDE_SHAPE_ARC && s->turn.x == 1.0f && s->turn.y == 0.0f && outline == 0.0f && soft == 0.0f) {
                bool one_color = memcmp(&s->color2, &c->color, sizeof s->color2) == 0;
                if (!one_color) {
                    const rgba bottom = to_rgba(s->color2);
                    one_color = memcmp(&bottom, &color, sizeof bottom) == 0;
                }
                if (one_color) {
                    *radius++ = s->radius * scale;
                    out++;
                    continue;
                }
            }
            const rgba color2 = to_rgba(s->color2), rim = to_rgba(s->outline_color);
            const shape_extra e = {.turn = {s->turn.x, cam->gui ? s->turn.y : -s->turn.y},
                                   .radius = s->radius * scale,
                                   // A hairline is a logical pixel wide, whatever the camera
                                   .outline = outline,
                                   .soft = soft,
                                   .sweep = s->sweep,
                                   .color2 = {color2.r, color2.g, color2.b, color2.a},
                                   .rim = {rim.r, rim.g, rim.b, rim.a}};
            if (radius) {
                if (solid_shape(out, &e)) {
                    *radius++ = e.radius;
                    out++;
                    continue;
                }
                // The first that isn't: what's added so far is counted, and the run's shapes get extras
                const uint32_t added = (uint32_t)(out - (shapes + shape_count));
                shape_count += added;
                shape_radius_count += added;
                run->count += added;
                smooth_instead(run);
                radius = NULL;
                extra = extra_room(piece - taken);
            }
            *extra++ = e;
            out++;
        }
        const uint32_t added = (uint32_t)(out - (shapes + shape_count));
        shape_count += added;
        if (radius) shape_radius_count += added;
        else shape_extra_count += added;
        run->count += added;
        at += taken;
        if (taken < piece || at == list->count) return at;
    }
}

// A circle or its outline, `radius` window pixels around (x, y). The plain
// kind's edge is smooth over a logical pixel, which is one of the target's
// where the display isn't scaled. Where it is, that would be soft: there a
// circle goes as the smooth shape it also is, a box that's all corners, whose
// edge is smooth over one of the target's own pixels. Its outline is a
// logical pixel wide either way.
static void add_circle(const bool wire, const float x, const float y, const float radius, const rgba color)
{
    if (!scaled) {
        add_shape(wire ? SHAPE_WIRE_CIRCLE : SHAPE_CIRCLE, x, y, radius, radius, color);
        return;
    }
    const float r = fabsf(radius);
    const rgba fill = wire ? (rgba){0} : color;
    add_smooth(SHAPE_BOX, x, y, r, r, fill,
               (shape_extra){.turn = {1.0f, 0.0f},
                             .radius = r,
                             .outline = wire ? fminf(r, 1.0f) : 0.0f,
                             .color2 = {fill.r, fill.g, fill.b, fill.a},
                             .rim = {color.r, color.g, color.b, color.a}});
}

// A run of the list's plain shapes (TIDE_DRAW_PLAIN: rects, their outlines, lines, and circles where the
// destination isn't scaled) into the run of shapes there is, in one loop: a frame of many shapes spends its time
// here. Each one's arithmetic matches add_shape's to the bit, its color as the list has it. A circle on a scaled
// destination goes as the smooth shape it also is (add_circle), between them.
static void add_plain_run(const tide_draw_list *list, const tide_draw_command *c, const camera *cam)
{
    const uint32_t first = c->plain.first < list->plain_count ? c->plain.first : list->plain_count;
    const uint32_t run = tide_draw_plain_run(list, c);
    const uint32_t count = run < list->plain_count - first ? run : list->plain_count - first;
    const tide_draw_plain *in = list->plains + first, *const end = in + count;
    const bool gui = cam->gui;
    const float scale = cam->scale, cx = cam->center.x, cy = cam->center.y;
    const float half_width = (draw_width > 0.0f ? draw_width : (float)screen_width) * 0.5f;
    const float half_height = (draw_height > 0.0f ? draw_height : (float)screen_height) * 0.5f;
    while (in < end) {
        if (scaled && (in->kind == TIDE_DRAW_CIRCLE || in->kind == TIDE_DRAW_WIRE_CIRCLE)) {
            const tide_float2 p = to_screen(cam, in->a);
            rgba color;
            memcpy(&color, &in->color, sizeof color);
            add_circle(in->kind == TIDE_DRAW_WIRE_CIRCLE, p.x, p.y, in->b.x * cam->scale, color);
            in++;
            continue;
        }
        if (!run_goes_on(STEP_SHAPES)) start_shapes();
        const uint32_t step = step_count - 1u;
        // In pieces, so a long run takes room as it goes
        const uint32_t left = (uint32_t)(end - in), piece = left < 4096u ? left : 4096u;
        shape *const out_first = shape_room(piece);
        shape *out = out_first;
        for (const tide_draw_plain *const stop = in + piece; in < stop; in++) {
            float ax, ay, bx, by;
            int kind;
            switch ((tide_draw_kind)in->kind) {
            case TIDE_DRAW_RECT:
            case TIDE_DRAW_WIRE_RECT: { // Its top left corner through to_screen: y goes up in the world, down in the GUI
                const float x = in->a.x - in->b.x * 0.5f, y = in->a.y + (gui ? -in->b.y * 0.5f : in->b.y * 0.5f);
                const float left_x = gui ? x * scale : half_width + (x - cx) * scale;
                const float top_y = gui ? y * scale : half_height - (y - cy) * scale;
                bx = in->b.x * scale * 0.5f;
                by = in->b.y * scale * 0.5f;
                ax = left_x + bx;
                ay = top_y + by;
                kind = in->kind == TIDE_DRAW_RECT ? SHAPE_RECT : SHAPE_WIRE_RECT;
                break;
            }
            case TIDE_DRAW_CIRCLE:
            case TIDE_DRAW_WIRE_CIRCLE:
                if (scaled) goto done; // A smooth shape there (add_circle)
                ax = gui ? in->a.x * scale : half_width + (in->a.x - cx) * scale;
                ay = gui ? in->a.y * scale : half_height - (in->a.y - cy) * scale;
                bx = by = in->b.x * scale;
                kind = in->kind == TIDE_DRAW_CIRCLE ? SHAPE_CIRCLE : SHAPE_WIRE_CIRCLE;
                break;
            case TIDE_DRAW_LINE:
                ax = gui ? in->a.x * scale : half_width + (in->a.x - cx) * scale;
                ay = gui ? in->a.y * scale : half_height - (in->a.y - cy) * scale;
                bx = gui ? in->b.x * scale : half_width + (in->b.x - cx) * scale;
                by = gui ? in->b.y * scale : half_height - (in->b.y - cy) * scale;
                kind = SHAPE_LINE;
                break;
            default: // No plain shape: nothing drawn
                continue;
            }
            *out = (shape){{ax, ay}, {bx, by}, {0, 0, 0, 0}, (float)kind};
            memcpy(out->color, &in->color, sizeof out->color);
            out++;
        }
    done:;
        const uint32_t added = (uint32_t)(out - out_first);
        shape_count += added;
        steps[step].count += added;
    }
}

static mesh_vertex *mesh_room(const uint32_t count)
{
    if (count > mesh_vertex_capacity - mesh_vertex_count) {
        uint64_t capacity = mesh_vertex_capacity ? mesh_vertex_capacity : 4096u;
        while (capacity < (uint64_t)mesh_vertex_count + count) capacity *= 2u;
        if (capacity > UINT32_MAX / sizeof(mesh_vertex)) tide_out_of_memory();
        mesh_vertices = tide_realloc(mesh_vertices, mesh_vertex_capacity * sizeof(mesh_vertex),
                                     (size_t)capacity * sizeof(mesh_vertex));
        mesh_vertex_capacity = (uint32_t)capacity;
    }
    return mesh_vertices + mesh_vertex_count;
}

static quad *quad_room(const uint32_t count)
{
    if (count > quad_capacity - quad_count) {
        uint64_t capacity = quad_capacity ? quad_capacity : 1024u;
        while (capacity < (uint64_t)quad_count + count) capacity *= 2u;
        if (capacity > UINT32_MAX / sizeof(quad)) tide_out_of_memory();
        quads = tide_realloc(quads, quad_capacity * sizeof(quad), (size_t)capacity * sizeof(quad));
        quad_capacity = (uint32_t)capacity;
    }
    return quads + quad_count;
}

_Static_assert(offsetof(mesh_vertex, uv) == 2 * sizeof(float) && offsetof(mesh_vertex, color) == 4 * sizeof(float),
               "a mesh vertex's place and uv are its first four floats");

// The quad whose six corners, in order, are corner[which[0]] to corner[which[5]], of `corners`, if it's one (see
// quad): their places and uvs take two numbers at most on each axis, told apart by their bits, and they're of one
// color. Nothing is written where it isn't.
static bool make_quad(const mesh_vertex *corner, const uint32_t corners, const uint8_t *which, quad *q)
{
    uint32_t first[4], second[4], color, picks[6] = {0};
    memcpy(first, &corner[0], sizeof first); // Its place, then its uv
    memcpy(second, first, sizeof second);    // The same while no corner had another
    memcpy(&color, corner[0].color, sizeof color);
    for (uint32_t k = 1; k < corners; k++) {
        uint32_t at[4], other;
        memcpy(at, &corner[k], sizeof at);
        memcpy(&other, corner[k].color, sizeof other);
        if (other != color) return false;
        for (uint32_t axis = 0; axis < 4; axis++) {
            if (at[axis] == first[axis]) continue;
            if (second[axis] == first[axis]) second[axis] = at[axis];
            else if (at[axis] != second[axis]) return false;
            picks[k] |= 1u << axis;
        }
    }
    uint32_t pattern = 0;
    for (uint32_t i = 0; i < 6; i++) pattern |= picks[which[i]] << (4u * i);
    const uint32_t box[4] = {first[0], first[1], second[0], second[1]};
    const uint32_t span[4] = {first[2], first[3], second[2], second[3]};
    memcpy(q->box, box, sizeof box);
    memcpy(q->span, span, sizeof span);
    memcpy(q->color, &color, sizeof color);
    q->pattern = (int32_t)pattern;
    return true;
}

// A run of quads has two triangles that can't be one: its quads, the last there are, go back to being the six
// vertices each they stood for, to the bit, and the run goes on as triangles.
static void triangles_instead(draw_step *s)
{
    const uint32_t count = s->count;
    mesh_vertex *out = mesh_room(6u * count);
    for (uint32_t i = 0; i < count; i++) {
        const quad *q = &quads[s->first + i];
        uint32_t box[4], span[4];
        memcpy(box, q->box, sizeof box);
        memcpy(span, q->span, sizeof span);
        for (uint32_t k = 0; k < 6; k++, out++) {
            const uint32_t pick = (uint32_t)q->pattern >> (4u * k);
            const uint32_t at[4] = {box[pick & 1u ? 2 : 0], box[pick & 2u ? 3 : 1], span[pick & 4u ? 2 : 0],
                                    span[pick & 8u ? 3 : 1]};
            memcpy(out, at, sizeof at);
            memcpy(out->color, q->color, sizeof out->color);
        }
    }
    quad_count = s->first;
    s->first = mesh_vertex_count;
    s->count = 6u * count;
    s->quads = 0;
    mesh_vertex_count += 6u * count;
}

// A run of triangles that starts as one of quads, until two of its triangles aren't one.
static draw_step *start_quads(const uint32_t kind)
{
    draw_step *s = add_step(kind);
    s->first = quad_count;
    s->quads = 1;
    return s;
}

// to_screen's arithmetic for a run of corners, with its camera's numbers read once.
typedef struct corner_camera {
    bool gui;
    float scale, cx, cy, half_width, half_height;
} corner_camera;

static corner_camera corner_camera_of(const camera *cam)
{
    return (corner_camera){cam->gui, cam->scale, cam->center.x, cam->center.y,
                           (draw_width > 0.0f ? draw_width : (float)screen_width) * 0.5f,
                           (draw_height > 0.0f ? draw_height : (float)screen_height) * 0.5f};
}

static inline float corner_x(const corner_camera *k, const tide_vertex *v)
{
    return k->gui ? v->position.x * k->scale : k->half_width + (v->position.x - k->cx) * k->scale;
}

static inline float corner_y(const corner_camera *k, const tide_vertex *v)
{
    return k->gui ? v->position.y * k->scale : k->half_height - (v->position.y - k->cy) * k->scale;
}

static inline mesh_vertex corner_of(const corner_camera *k, const tide_vertex *v)
{
    const rgba color = to_rgba(v->color);
    return (mesh_vertex){{corner_x(k, v), corner_y(k, v)}, {v->uv.x, v->uv.y}, {color.r, color.g, color.b, color.a}};
}

_Static_assert(offsetof(tide_vertex, uv) == 2 * sizeof(float) && offsetof(tide_vertex, color) == 4 * sizeof(float),
               "a vertex's place and uv are its first four floats, and its color the next four");

// Four 32-bit lanes, which clang makes one SIMD register where the target has them (SSE2 on x86-64, NEON on
// arm64) and four scalars where it hasn't: a quad's corners compared all at once. Lane by lane, the arithmetic is
// what the same floats get alone, IEEE's.
typedef uint32_t lanes __attribute__((ext_vector_type(4)));
typedef int32_t lane_mask __attribute__((ext_vector_type(4))); // What comparing lanes gives: -1 where they're equal
typedef float float_lanes __attribute__((ext_vector_type(4)));

static inline lanes lanes_of(const void *at)
{
    lanes v;
    memcpy(&v, at, sizeof v);
    return v;
}

// The quad of triangles p q r and p r s, as a quad's two mostly are, where p and r are its opposite corners on
// both axes, and all four are of one color: what make_quad finds of them, sooner. It compares the vertices as the
// list has them, by their bits: a corner's place on the screen is worked out from its own on that axis alone, so
// two the same are the same there, and only p's and r's are worked out. Nothing is written where it isn't one
// (make_quad may still find one).
static bool make_quad_across(const corner_camera *k, const tide_vertex *const corner[4], quad *out)
{
    // A corner's x, y, u and v as bits, side by side, and its color's
    const lanes p = lanes_of(&corner[0]->position), q = lanes_of(&corner[1]->position);
    const lanes r = lanes_of(&corner[2]->position), s = lanes_of(&corner[3]->position);
    const lanes color = lanes_of(&corner[0]->color);
    const lane_mask qp = q == p, rp = r == p, sp = s == p;
    const lane_mask fits = (qp | (q == r)) & (sp | (s == r)) & (lanes_of(&corner[1]->color) == color)
                         & (lanes_of(&corner[2]->color) == color) & (lanes_of(&corner[3]->color) == color);
    if (__builtin_reduce_and(fits) == 0) return false;
    // Which number of each axis each corner takes: p the first, r the second where it's another
    const lane_mask axis = {1, 2, 4, 8};
    const uint32_t q_picks = (uint32_t)__builtin_reduce_or(~qp & axis), r_picks = (uint32_t)__builtin_reduce_or(~rp & axis);
    const uint32_t s_picks = (uint32_t)__builtin_reduce_or(~sp & axis);
    // p's and r's places on the screen, each lane as corner_x and corner_y work them out
    const float_lanes at = (float_lanes)__builtin_shufflevector(p, r, 0, 1, 4, 5);
    float_lanes box;
    if (k->gui) {
        box = at * k->scale;
    } else {
        const float_lanes moved = (at - (float_lanes){k->cx, k->cy, k->cx, k->cy}) * k->scale;
        const float_lanes half = {k->half_width, k->half_height, k->half_width, k->half_height};
        box = __builtin_shufflevector(half + moved, half - moved, 0, 5, 2, 7);
    }
    const lanes span = __builtin_shufflevector(p, r, 2, 3, 6, 7);
    const rgba bytes = to_rgba(corner[0]->color);
    memcpy(out->box, &box, sizeof out->box);
    memcpy(out->span, &span, sizeof out->span);
    memcpy(out->color, &bytes, sizeof out->color);
    out->pattern = (int32_t)(q_picks << 4 | r_picks << 8 | r_picks << 16 | s_picks << 20); // p q r p r s
    return true;
}

// A mesh command's triangles, their corners through the camera, after the
// ones before it when nothing came between them: two at a time as quads, as
// long as they are.
static void add_mesh(const tide_draw_list *list, const tide_draw_command *c, const camera *cam)
{
    const uint32_t count = c->mesh.count;
    draw_step *s = run_goes_on(STEP_MESH) ? &steps[step_count - 1] : NULL;
    if (!s || s->texture != c->mesh.texture || s->filter != c->mesh.filter || s->wrap != c->mesh.wrap) {
        s = start_quads(STEP_MESH);
        s->texture = c->mesh.texture;
        s->filter = c->mesh.filter;
        s->wrap = c->mesh.wrap;
    }
    const uint32_t *indices = list->indices + c->mesh.first;
    const tide_vertex *const vertices = list->vertices;
    const corner_camera k = corner_camera_of(cam);
    uint32_t i = 0;
    if (s->quads) {
        static const uint8_t four[6] = {0, 1, 2, 0, 2, 3}, six[6] = {0, 1, 2, 3, 4, 5};
        quad *const first = quad_room(count / 6u), *out = first;
        for (; i + 6u <= count; i += 6u, out++) {
            const uint32_t *t = indices + i;
            // Two triangles that share their first and last corners, as a quad's mostly do: four corners, once each
            const bool shared = t[3] == t[0] && t[4] == t[2];
            if (shared) {
                const tide_vertex *const across[4] = {&vertices[t[0]], &vertices[t[1]], &vertices[t[2]], &vertices[t[5]]};
                if (make_quad_across(&k, across, out)) continue;
            }
            mesh_vertex corner[6];
            corner[0] = corner_of(&k, &vertices[t[0]]);
            corner[1] = corner_of(&k, &vertices[t[1]]);
            corner[2] = corner_of(&k, &vertices[t[2]]);
            if (shared) {
                corner[3] = corner_of(&k, &vertices[t[5]]);
            } else {
                for (uint32_t j = 3; j < 6; j++) corner[j] = corner_of(&k, &vertices[t[j]]);
            }
            if (!make_quad(corner, shared ? 4u : 6u, shared ? four : six, out)) break;
        }
        const uint32_t made = (uint32_t)(out - first);
        quad_count += made;
        s->count += made;
        if (i == count) return;
        triangles_instead(s);
    }
    mesh_vertex *out = mesh_room(count - i);
    for (uint32_t j = i; j < count; j++) out[j - i] = corner_of(&k, &vertices[indices[j]]);
    s->count += count - i;
    mesh_vertex_count += count - i;
}

// A run of the quads the list kept as quads (TIDE_DRAW_QUADS), after the ones before it when nothing came between
// them: the quads add_mesh makes of the same two triangles, or where the run's triangles already, those triangles.
// The same numbers to the bit: make_quad_across's box of p and r through the camera, a lane each as there, and
// corner_of's corners.
static void add_quads(const tide_draw_list *list, const tide_draw_command *c, const camera *cam)
{
    draw_step *s = run_goes_on(STEP_MESH) ? &steps[step_count - 1] : NULL;
    if (!s || s->texture != c->mesh.texture || s->filter != c->mesh.filter || s->wrap != c->mesh.wrap) {
        s = start_quads(STEP_MESH);
        s->texture = c->mesh.texture;
        s->filter = c->mesh.filter;
        s->wrap = c->mesh.wrap;
    }
    const uint32_t first = c->mesh.first < list->quad_count ? c->mesh.first : list->quad_count;
    const uint32_t count = c->mesh.count < list->quad_count - first ? c->mesh.count : list->quad_count - first;
    const tide_draw_quad *in = list->quads + first;
    const corner_camera k = corner_camera_of(cam);
    if (s->quads) {
        quad *out = quad_room(count);
        const float_lanes centers = {k.cx, k.cy, k.cx, k.cy};
        const float_lanes half = {k.half_width, k.half_height, k.half_width, k.half_height};
        for (uint32_t i = 0; i < count; i++, out++) {
            const tide_draw_quad *q = &in[i];
            const float_lanes at = {q->from.x, q->from.y, q->to.x, q->to.y};
            float_lanes box;
            if (k.gui) {
                box = at * k.scale;
            } else {
                const float_lanes moved = (at - centers) * k.scale;
                box = __builtin_shufflevector(half + moved, half - moved, 0, 5, 2, 7);
            }
            const float span[4] = {q->uv_from.x, q->uv_from.y, q->uv_to.x, q->uv_to.y};
            const rgba bytes = to_rgba(q->color);
            memcpy(out->box, &box, sizeof out->box);
            memcpy(out->span, span, sizeof out->span);
            memcpy(out->color, &bytes, sizeof out->color);
            out->pattern = (int32_t)q->pattern;
        }
        quad_count += count;
        s->count += count;
        return;
    }
    mesh_vertex *out = mesh_room(6u * count);
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t corner = 0; corner < 6u; corner++) {
            const tide_vertex v = tide_draw_quad_corner(&in[i], corner);
            *out++ = corner_of(&k, &v);
        }
    }
    s->count += 6u * count;
    mesh_vertex_count += 6u * count;
}

// A glyph's two triangles over its four corners, after the text before it: a quad where it's one.
static void add_glyph(const mesh_vertex corners[4])
{
    static const uint8_t four[6] = {0, 1, 2, 0, 2, 3};
    draw_step *s = &steps[step_count - 1];
    if (s->quads) {
        if (make_quad(corners, 4u, four, quad_room(1))) {
            quad_count++;
            s->count++;
            return;
        }
        triangles_instead(s);
    }
    mesh_vertex *out = mesh_room(6);
    out[0] = corners[0], out[1] = corners[1], out[2] = corners[2];
    out[3] = corners[0], out[4] = corners[2], out[5] = corners[3];
    mesh_vertex_count += 6;
    s->count += 6;
}

// Where the line of text at `line` starts, in the target's pixels, a whole
// one: at `x`, or for a `side` of 1 or 2, with its middle or its end there.
static float line_start(const unsigned char *font, const char *line, const int pixels, const float stretch,
                        const float x, const uint32_t side)
{
    int64_t across = 0; // A line of any length: more pixels than an int counts
    for (uint32_t c; side && (c = tide_utf8_next(&line)) != 0 && c != '\n';) across += tide_font_advance(font, c, pixels);
    return floorf(x - (float)across * stretch * 0.5f * (float)side + 0.5f);
}

// Text of `size` in window pixels, in the weight `font` is the file of, with
// the place `anchor` names (a tide_anchor) of its box at `at`: a quad a glyph,
// after the text before it when nothing came between them. It's drawn on the
// target's own pixels (see font.h): its lines start on one, and each glyph is
// a whole number of them from the last, so none is ever blended between two.
static void add_text(const char *text, const tide_float2 at, const float size, const rgba color, const uint32_t anchor,
                     const unsigned char *font)
{
    if (!(size > 0.0f)) return;
    const tide_float2 scale = pixel_scale();
    float stretch;
    const int pixels = tide_font_pixels(size, scale.y, &stretch);
    const float pitch = tide_font_pitch(font, pixels, stretch);
    // The anchor's column and row: each line is against its side, and the
    // lines are a box as tall as tide_font_size says, whose top, middle or
    // bottom is at `at`
    const uint32_t side = anchor % 3u, row = anchor / 3u;
    float above = 0.0f;
    if (row) {
        uint32_t lines = 1;
        for (const char *c = text; *c; c++) lines += *c == '\n';
        above = (size * scale.y + (float)(lines - 1u) * pitch) * 0.5f * (float)row;
    }
    // In the target's pixels, as the pen is
    const float x = at.x * scale.x;
    float pen_x = line_start(font, text, pixels, stretch, x, side);
    float pen_y = floorf((at.y + tide_font_baseline(font) * size) * scale.y - above + 0.5f); // On the baseline

    if (!run_goes_on(STEP_TEXT)) start_quads(STEP_TEXT);
    for (uint32_t c; (c = tide_utf8_next(&text)) != 0;) {
        if (c == '\n') {
            pen_x = line_start(font, text, pixels, stretch, x, side);
            pen_y += pitch;
            continue;
        }
        tide_font_glyph g;
        if (!tide_font_glyph_for(font, c, pixels, &g)) continue; // No room for it this frame
        if (g.width) {
            const float x0 = (pen_x + (float)g.left * stretch) / scale.x;
            const float y0 = (pen_y + (float)g.top * stretch) / scale.y;
            const float x1 = x0 + (float)g.width * stretch / scale.x, y1 = y0 + (float)g.height * stretch / scale.y;
            const float u0 = (float)g.x, v0 = (float)g.y, u1 = u0 + (float)g.width, v1 = v0 + (float)g.height;
            const mesh_vertex corners[4] = {
                {{x0, y0}, {u0, v0}, {color.r, color.g, color.b, color.a}},
                {{x1, y0}, {u1, v0}, {color.r, color.g, color.b, color.a}},
                {{x1, y1}, {u1, v1}, {color.r, color.g, color.b, color.a}},
                {{x0, y1}, {u0, v1}, {color.r, color.g, color.b, color.a}},
            };
            add_glyph(corners);
        }
        pen_x += (float)g.advance * stretch;
    }
}

// Text a transform turned or stretched (see tide_draw_set_transform): laid out
// as it would be upright, at the size the transform's mean scale makes it,
// then each glyph through the transform. `at` is where the place `anchor`
// names of its box is on the screen, and `size` its font size in the units
// its command is in, as the transform found it. There are no pixels to fit it
// to: its glyphs are the ones fitted for upright text of that size, and their
// pixels blend where they land.
static void add_text_turned(const char *text, const tide_float2 at, const float size, const camera *cam,
                            const tide_float2x2 turn, const rgba color, const uint32_t anchor, const unsigned char *font)
{
    const float mean = sqrtf(fabsf(turn.c0.x * turn.c1.y - turn.c0.y * turn.c1.x));
    const float drawn = size * cam->scale * mean; // Its size on the screen, in logical pixels
    if (!(drawn > 0.0f) || !(mean > 0.0f)) return;
    const tide_float2 scale = pixel_scale();
    float stretch;
    const int pixels = tide_font_pixels(drawn, scale.y, &stretch);
    const float pitch = tide_font_pitch(font, pixels, stretch);
    // The anchor's column and row, as upright text has them (see add_text)
    const uint32_t side = anchor % 3u, row = anchor / 3u;
    float above = 0.0f;
    if (row) {
        uint32_t lines = 1;
        for (const char *c = text; *c; c++) lines += *c == '\n';
        above = (drawn * scale.y + (float)(lines - 1u) * pitch) * 0.5f * (float)row;
    }
    // Where the layout's right and down go on the screen: through the transform, in the units it's in,
    // whose y goes up under a camera, and less its scale, which the layout's size has already
    const float up = cam->gui ? 1.0f : -1.0f;
    const tide_float2 right = tide_f2(turn.c0.x / mean, turn.c0.y * up / mean);
    const tide_float2 down = tide_f2(turn.c1.x * up / mean, turn.c1.y / mean);
    // From its anchor, in the target's pixels
    float pen_x = line_start(font, text, pixels, stretch, 0.0f, side);
    float pen_y = floorf(tide_font_baseline(font) * drawn * scale.y - above + 0.5f);

    if (!run_goes_on(STEP_TEXT)) start_quads(STEP_TEXT);
    for (uint32_t c; (c = tide_utf8_next(&text)) != 0;) {
        if (c == '\n') {
            pen_x = line_start(font, text, pixels, stretch, 0.0f, side);
            pen_y += pitch;
            continue;
        }
        tide_font_glyph g;
        if (!tide_font_glyph_for(font, c, pixels, &g)) continue; // No room for it this frame
        if (g.width) {
            // Half an atlas pixel more each way: the clear one around the glyph, which its edge blends with
            const float x0 = (pen_x + ((float)g.left - 0.5f) * stretch) / scale.x;
            const float y0 = (pen_y + ((float)g.top - 0.5f) * stretch) / scale.y;
            const float x1 = x0 + ((float)g.width + 1.0f) * stretch / scale.x;
            const float y1 = y0 + ((float)g.height + 1.0f) * stretch / scale.y;
            const float u0 = (float)g.x - 0.5f, v0 = (float)g.y - 0.5f;
            const float u1 = u0 + (float)g.width + 1.0f, v1 = v0 + (float)g.height + 1.0f;
            const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
            const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
            mesh_vertex corners[4];
            for (int i = 0; i < 4; i++) {
                corners[i] = (mesh_vertex){{at.x + xs[i] * right.x + ys[i] * down.x, at.y + xs[i] * right.y + ys[i] * down.y},
                                           {us[i], vs[i]},
                                           {color.r, color.g, color.b, color.a}};
            }
            add_glyph(corners); // A quad still, where nothing turned it
        }
        pen_x += (float)g.advance * stretch;
    }
}

// The engine's own pipelines, each in a version for each way of blending
// (tide_blend) and, for the 3D meshes', each thing a mesh does with depth
// (tide_depth) and each side of a triangle it leaves out (tide_gpu_cull),
// made the first time a list draws with it.
enum { PIPELINE_SHAPES, PIPELINE_SMOOTH_SHAPES, PIPELINE_SOLID_SHAPES, PIPELINE_MESHES, PIPELINE_TEXT, PIPELINE_QUADS, PIPELINE_TEXT_QUADS,
       PIPELINE_MESHES_3D, PIPELINE_KINDS };
enum { BLENDS = TIDE_BLEND_REPLACE + 1, DEPTHS = TIDE_DEPTH_NONE + 1, CULLS = TIDE_GPU_CULL_FRONT + 1 };

// Stencil's numbers are the draw list's all the way down.
_Static_assert((int)TIDE_GPU_STENCIL_ALWAYS == (int)TIDE_STENCIL_ALWAYS && (int)TIDE_GPU_STENCIL_NEVER == (int)TIDE_STENCIL_NEVER
               && (int)TIDE_GPU_STENCIL_LESS == (int)TIDE_STENCIL_LESS && (int)TIDE_GPU_STENCIL_EQUAL == (int)TIDE_STENCIL_EQUAL
               && (int)TIDE_GPU_STENCIL_LESS_EQUAL == (int)TIDE_STENCIL_LESS_EQUAL
               && (int)TIDE_GPU_STENCIL_GREATER == (int)TIDE_STENCIL_GREATER
               && (int)TIDE_GPU_STENCIL_NOT_EQUAL == (int)TIDE_STENCIL_NOT_EQUAL
               && (int)TIDE_GPU_STENCIL_GREATER_EQUAL == (int)TIDE_STENCIL_GREATER_EQUAL,
               "a pipeline's stencil tests are tide_stencil_test's");
_Static_assert((int)TIDE_GPU_STENCIL_KEEP == (int)TIDE_STENCIL_KEEP && (int)TIDE_GPU_STENCIL_REPLACE == (int)TIDE_STENCIL_REPLACE
               && (int)TIDE_GPU_STENCIL_INCREMENT == (int)TIDE_STENCIL_INCREMENT
               && (int)TIDE_GPU_STENCIL_DECREMENT == (int)TIDE_STENCIL_DECREMENT
               && (int)TIDE_GPU_STENCIL_ZERO == (int)TIDE_STENCIL_ZERO && (int)TIDE_GPU_STENCIL_INVERT == (int)TIDE_STENCIL_INVERT,
               "a pipeline's stencil operations are tide_stencil_op's");

// What of a step's stencil is its pipeline's: its test and what it stores (a
// tide_stencil_test, and a tide_stencil_op from the fourth bit). Its number
// is set at the draw (tide_gpu.stencil), so no pipeline is made for one.
static uint32_t stencil_state(const uint32_t stencil)
{
    return stencil & 255u;
}

// ...and its number.
static uint32_t stencil_value(const uint32_t stencil)
{
    return stencil >> 8 & 255u;
}

// Whether a step that draws changes the numbers stored where it draws.
static bool stencil_stores(const uint32_t stencil)
{
    return (stencil >> 4 & 15u) != TIDE_STENCIL_KEEP;
}

// The engine's pipelines that test or store stencil, each made the first time
// a list draws with it, like the others: by kind, blend, depth and cull, by
// the stencil's test and what it stores, and by what they draw into. Few
// games have more than a handful, so they're a list, looked through.
typedef struct stencil_pipeline {
    uint8_t kind, blend, depth, cull, state;
    tide_gpu_target_format into; // What the targets it draws into hold
    tide_gpu_id pipeline;
} stencil_pipeline;

static stencil_pipeline *stencil_pipelines;
static uint32_t stencil_pipeline_count, stencil_pipeline_capacity;

// The number the pass's stencil tests compare with, as the backend has it: 0 as a pass begins.
static uint32_t pass_stencil;

// A pass a list's steps draw in.
static void begin_pass(const tide_gpu_id target, const bool clear)
{
    gpu->begin(target, clear);
    pass_stencil = 0;
}

// Whether a step's pipeline is made yet: one the backend makes in the background isn't drawn with till it is, and
// the step is left out of the frames before (tide_platform_background_pipelines).
static bool pipeline_made(const draw_step *s)
{
    return !gpu->pipeline_ready || !s->pipeline || gpu->pipeline_ready(s->pipeline);
}

// A step's pipeline, for the draws after it, with the step's stencil number where the pass has another.
static void use_pipeline(const draw_step *s)
{
    gpu->use(s->pipeline);
    const uint32_t value = stencil_value(s->stencil);
    if (value != pass_stencil) gpu->stencil(pass_stencil = value);
}

// What a pipeline does with depth for each tide_depth.
static const tide_gpu_depth gpu_depths[DEPTHS] = {
    [TIDE_DEPTH_READ_WRITE] = TIDE_GPU_DEPTH_TEST, [TIDE_DEPTH_READ] = TIDE_GPU_DEPTH_READ,
    [TIDE_DEPTH_NONE] = TIDE_GPU_DEPTH_NONE};

// What the renderer keeps on the GPU (see gpu.h): its pipelines, whose
// shaders are in shaders.h, and the buffers and textures a list's steps draw
// from, made the first time they're needed.
static struct {
    tide_gpu_id far; // A target of depth alone that's 1 everywhere: what a shader reads for a depth it wasn't given
    tide_gpu_id dark; // A cube of one black pixel a face: what a shader reads for a cube it wasn't given
    tide_gpu_id nothing; // A pixel of four zeros: what a shader that reads a list's numbers is bound for a list with none
    tide_gpu_id corners; // The shapes' quad
    tide_gpu_id shape_instances;
    uint32_t shape_capacity; // Shapes the instance buffer holds
    tide_gpu_id shape_extras;
    tide_gpu_id shape_radii;
    uint32_t shape_radius_capacity; // Radii theirs holds
    uint32_t shape_extra_capacity; // Extras theirs holds
    tide_gpu_id mesh_vertices;
    uint32_t mesh_capacity; // Vertices the buffer holds
    tide_gpu_id quads;
    uint32_t quad_capacity; // Quads theirs holds
    tide_gpu_id matrices;
    uint32_t matrix_capacity; // Transforms the 3D meshes' instance buffer holds
    tide_gpu_id instances;
    uint32_t instance_capacity; // Bytes the shaded runs' instance buffer holds
    tide_gpu_id white;        // A white pixel, which a mesh with no texture samples
    tide_gpu_id atlas;        // The font atlas, as of `atlas_version`
    int atlas_width, atlas_height;
    uint32_t atlas_version;
    tide_gpu_id target; // What tide_platform_read_pixels draws into, kept while the window keeps its size
    int target_width, target_height;
    uint8_t *target_pixels; // ...read back: RGBA, rows from the top
    tide_gpu_id frame; // What a headless window's frames draw into, in its window's place
    int frame_width, frame_height;
    uint8_t *frame_pixels; // ...read back (tide_platform_frame_pixels)
} gpu_state;

// The engine's own pipelines into one kind of target, which is what it holds
// (a pipeline is made for one): the window's kind, one color image of 4 bytes
// a pixel, first, and another for each kind a list draws into. They draw one
// color, into a target's first color image, and leave the others it has
// alone, but for the clears', which fill every one. Into depth alone, only
// the 3D meshes' and the clears' leave anything, and what stores stencil
// (writes_depth). The ones that test or store stencil are apart, a list of
// their own (stencil_pipelines), by what their targets hold too.
typedef struct engine_pipelines {
    tide_gpu_target_format into;
    tide_gpu_id made[PIPELINE_KINDS][BLENDS][DEPTHS][CULLS]; // Only the 3D meshes' have more than the first depth and cull
    tide_gpu_id clear; // The clears'
    tide_gpu_id depth_clear; // ...and the ones of depth alone
} engine_pipelines;

static engine_pipelines *engine_made;
static uint32_t engine_made_count;

static engine_pipelines *pipelines_into(const tide_gpu_target_format into)
{
    for (uint32_t i = 0; i < engine_made_count; i++) {
        if (engine_made[i].into == into) return &engine_made[i];
    }
    engine_made = tide_realloc(engine_made, engine_made_count * sizeof(engine_pipelines),
                               (engine_made_count + 1u) * sizeof(engine_pipelines));
    engine_made[engine_made_count] = (engine_pipelines){.into = into};
    return &engine_made[engine_made_count++];
}

// What a render target holds, as the GPU has it: its color images' formats, in order.
static tide_gpu_target_format gpu_target_format(const uint64_t id)
{
    _Static_assert(TIDE_TARGET_COLORS == (int)TIDE_GPU_MAX_COLORS, "gpu.h's targets hold as many colors as draw.h's");
    const tide_target_formats held = tide_render_target_formats(id);
    tide_gpu_target_format format = TIDE_GPU_TARGET_DEPTH;
    for (uint32_t i = 0; i < held.count; i++) {
        format = tide_gpu_target_with(format, held.color[i] == TIDE_TARGET_RGBA16F ? TIDE_GPU_COLOR_FLOAT : TIDE_GPU_COLOR_BYTES);
    }
    // ...and its samples, as this device has them for what it asked for
    const uint32_t asked = tide_render_target_samples(id);
    if (asked > 1u && held.count) {
        const uint32_t samples = gpu->target_samples(tide_gpu_target_sampled(format, asked));
        if (samples < asked) {
            static bool said;
            if (!said) {
                fprintf(stderr, "tide: a render target asked for %u samples a pixel, and this device's %s has %u for "
                                "what it holds: it's drawn with %u\n", (unsigned)asked, gpu->name, (unsigned)samples,
                        (unsigned)samples);
            }
            said = true;
        }
        format = tide_gpu_target_sampled(format, samples);
    }
    return format;
}

// The shapes', meshes' and text's uniforms, as their shaders have them: from
// the target's logical pixels (from its top left, y down) to clip space, as
// OpenGL's matrices go, column by column; for text, 1 over the atlas's size;
// for shapes, the target's own pixels in a logical one, across and down,
// which is how they know where a pixel ends; and `fix`, whose first is 1 for
// what multiplies what's there (TIDE_BLEND_MULTIPLY), 0 otherwise: there a
// color goes to white as its alpha goes to 0, so what's clear changes nothing.
// Its second is 1 for a step that stores stencil: there a pixel is drawn, and
// stores, wherever the draw touches it, a clear shape's too, and none of the
// text's quads' that no glyph covers is.
// A run of the plain shapes alone has `screen` only.
typedef struct screen_uniforms {
    float screen[16];
    float texel[2];
    float scale[2];
    float fix[4];
} screen_uniforms;

// The 3D meshes': the camera, from the world to clip space, what tints them,
// and `fix`, as above.
typedef struct mesh_3d_uniforms {
    tide_float4x4 camera;
    float tint[4];
    float fix[4];
} mesh_3d_uniforms;

// What the uniforms of a shader of the game's start with (see tide_shader),
// before a run's own: `tide_camera`, from the units its mesh is in to clip
// space, `tide_screen`, the target's pixels in a logical one, across and
// down, its height in its own pixels, and 1 where the draw multiplies what's
// there, and `tide_tint`, what its pixels' colors are multiplied by.
typedef struct shaded_uniforms {
    tide_float4x4 camera;
    float screen[4];
    float tint[4];
} shaded_uniforms;

// A float4 short of the block, too: Android's emulator links a shader whose
// block is 255 float4s and refuses one of 256, which it counts as its
// vertex shader's uniform vectors
_Static_assert(sizeof(shaded_uniforms) + TIDE_SHADER_UNIFORM_BYTES <= TIDE_GPU_MAX_UNIFORMS - 16u,
               "a shader's uniforms fit a pipeline's block");
_Static_assert(TIDE_SHADER_ATTRIBUTES <= TIDE_GPU_MAX_ATTRIBUTES && TIDE_SHADER_TEXTURES <= TIDE_GPU_MAX_TEXTURES,
               "a shader's attributes and textures fit a pipeline's");

// A 3D mesh's corner on the GPU. A mesh goes there as its corners, each
// once, and its triangles as indices into them, uploaded when a list first
// draws it, and let go once a frame draws without it. A mesh for a shader of
// the game's (TIDE_MESH_BYTES) goes as the list has it: the shader reads it.
typedef struct mesh_3d_vertex {
    float position[3];
    float uv[2];
    uint8_t color[4]; // RGBA
} mesh_3d_vertex;

// ---------------------------------------------------------------------------
// What the renderer has on the GPU, by its own count (tide_platform_gpu_memory):
// every buffer, texture, cube and target it asked the backend for, by what
// each holds, less what it freed. A device that's lost takes them all, and
// the count with it (gpu_ready). Nothing asks the driver: the count is the
// same number on every backend, and it's what a game's own draws cost, not
// what the window's own framebuffer, the pipelines or the driver take.
typedef struct gpu_held {
    uint64_t bytes;
    uint8_t mipped; // A bit for each image whose smaller images were made: a third more, once
    bool resolved;  // A multisampled target's depth was resolved into a texture: 4 bytes a pixel more, once
} gpu_held;

static gpu_held *held_buffers, *held_textures, *held_targets; // By the backend's id
static uint32_t held_buffer_count, held_texture_count, held_target_count;
static uint64_t gpu_memory;
static uint64_t gpu_written; // Bytes of textures' pixels sent to the GPU (tide_platform_gpu_written)

static gpu_held *held(gpu_held **table, uint32_t *count, const tide_gpu_id id)
{
    if (id >= *count) {
        uint32_t grown = *count ? *count : 64u;
        while (grown <= id) grown *= 2u;
        *table = tide_realloc(*table, *count * sizeof **table, grown * sizeof **table);
        memset(*table + *count, 0, (grown - *count) * sizeof **table);
        *count = grown;
    }
    return *table + id;
}

static void hold(gpu_held *h, const uint64_t bytes)
{
    gpu_memory -= h->bytes;
    *h = (gpu_held){.bytes = bytes};
    gpu_memory += bytes;
}

static void let_go(gpu_held *table, const uint32_t count, const tide_gpu_id id)
{
    if (id < count) hold(&table[id], 0);
}

// A third more for a texture's smaller images, the first time they're made
static void hold_mipmaps(gpu_held *h, const uint64_t image_bytes, const uint32_t image)
{
    if (h->mipped & 1u << image) return;
    h->mipped |= (uint8_t)(1u << image);
    h->bytes += image_bytes / 3u;
    gpu_memory += image_bytes / 3u;
}

static uint64_t format_bytes(const tide_gpu_format format)
{
    return format == TIDE_GPU_RGBA8 ? 4u : format == TIDE_GPU_R16F ? 2u : format == TIDE_GPU_RGBA32I ? 16u : 1u;
}

static uint64_t color_bytes(const tide_gpu_color color)
{
    return color == TIDE_GPU_COLOR_FLOAT ? 8u : 4u;
}

// A target's pixel: its color images and 4 bytes of depth and stencil, each `samples` times when it's multisampled, and
// then each color image once more, resolved
static uint64_t target_bytes(const tide_gpu_target_format format, const int width, const int height)
{
    uint64_t colors = 0;
    for (uint32_t i = 0; i < tide_gpu_target_colors(format); i++) colors += color_bytes(tide_gpu_target_color(format, i));
    const uint64_t samples = tide_gpu_target_samples(format);
    const uint64_t pixel = samples > 1u ? (colors + 4u) * samples + colors : colors + 4u;
    return (uint64_t)width * (uint64_t)height * pixel;
}

static tide_gpu_id made_buffer(const tide_gpu_buffer_kind kind, const size_t size, const void *data)
{
    const tide_gpu_id id = gpu->buffer(kind, size, data);
    if (id) hold(held(&held_buffers, &held_buffer_count, id), size);
    return id;
}

static void free_buffer(const tide_gpu_id id)
{
    let_go(held_buffers, held_buffer_count, id);
    gpu->buffer_free(id);
}

// Mesh buffers the frame's list has no use for: those of meshes it doesn't
// draw, and those too small for what's in their place now, for the meshes it
// draws that need some. Making a buffer is slow on OpenGL (and WebGL), and a
// game that fills a list in a view each frame has its meshes made again each
// frame. What's left as the frame ends goes, so none is counted after it.
typedef struct spare_buffer {
    tide_gpu_id id;
    tide_gpu_buffer_kind kind;
    uint32_t capacity;
} spare_buffer;

enum { SPARE_BUFFERS = 256 };
static spare_buffer spare_buffers[SPARE_BUFFERS];
static uint32_t spare_buffer_count;

static void spare_buffer_of(const tide_gpu_id id, const tide_gpu_buffer_kind kind, const uint32_t capacity)
{
    if (!id) return;
    if (spare_buffer_count == SPARE_BUFFERS) free_buffer(id);
    else spare_buffers[spare_buffer_count++] = (spare_buffer){id, kind, capacity};
}

// A buffer of `kind` for `size` bytes, holding `data`: the smallest spare
// with room that isn't four times as big, or a new one. *capacity is its size.
static tide_gpu_id taken_buffer(const tide_gpu_buffer_kind kind, const size_t size, const void *data, uint32_t *capacity)
{
    uint32_t best = UINT32_MAX;
    for (uint32_t i = 0; i < spare_buffer_count; i++) {
        const spare_buffer *b = &spare_buffers[i];
        if (b->kind != kind || b->capacity < size || (size_t)b->capacity > size * 4u + 4096u) continue;
        if (best == UINT32_MAX || b->capacity < spare_buffers[best].capacity) best = i;
    }
    if (best == UINT32_MAX) {
        *capacity = (uint32_t)size;
        return made_buffer(kind, size, data);
    }
    const spare_buffer b = spare_buffers[best];
    memmove(spare_buffers + best, spare_buffers + best + 1, (spare_buffer_count - best - 1u) * sizeof(spare_buffer));
    spare_buffer_count--;
    if (size) gpu->buffer_write(b.id, 0, data, size);
    *capacity = b.capacity;
    return b.id;
}

static void free_spare_buffers(void)
{
    for (uint32_t i = 0; i < spare_buffer_count; i++) free_buffer(spare_buffers[i].id);
    spare_buffer_count = 0;
}

static tide_gpu_id made_texture(const tide_gpu_format format, const int width, const int height, const void *pixels)
{
    const tide_gpu_id id = gpu->texture(format, width, height, pixels);
    if (id) hold(held(&held_textures, &held_texture_count, id), (uint64_t)width * (uint64_t)height * format_bytes(format));
    return id;
}

static tide_gpu_id made_cube(const tide_gpu_color color, const int size, const void *const *faces)
{
    const tide_gpu_id id = gpu->cube(color, size, faces);
    if (id) hold(held(&held_textures, &held_texture_count, id), 6u * (uint64_t)size * (uint64_t)size * color_bytes(color));
    return id;
}

static void free_texture(const tide_gpu_id id)
{
    let_go(held_textures, held_texture_count, id);
    gpu->texture_free(id);
}

static tide_gpu_id made_target(const tide_gpu_target_format format, const int width, const int height)
{
    const tide_gpu_id id = gpu->target(format, width, height);
    if (id) hold(held(&held_targets, &held_target_count, id), target_bytes(format, width, height));
    return id;
}

// A cube's face as a target: its depth and stencil are its own; its color image is the cube's
static tide_gpu_id made_face(const tide_gpu_id cube, const uint32_t face, const int size)
{
    const tide_gpu_id id = gpu->cube_face(cube, face);
    if (id) hold(held(&held_targets, &held_target_count, id), (uint64_t)size * (uint64_t)size * 4u);
    return id;
}

static void free_target(const tide_gpu_id id)
{
    let_go(held_targets, held_target_count, id);
    gpu->target_free(id);
}

static void made_mipmaps(const tide_gpu_id texture)
{
    gpu->mipmaps(texture);
    gpu_held *h = held(&held_textures, &held_texture_count, texture);
    hold_mipmaps(h, h->bytes, 0);
}

// The smaller images of a target's color image `image`, whose texture is `color`
static void made_target_mipmaps(const tide_gpu_id target, const uint32_t image, const tide_gpu_id color,
                                const tide_gpu_target_format format, const int width, const int height)
{
    gpu->mipmaps(color);
    hold_mipmaps(held(&held_targets, &held_target_count, target),
                 (uint64_t)width * (uint64_t)height * color_bytes(tide_gpu_target_color(format, image)), image);
}

static void forget_held(void)
{
    if (held_buffers) memset(held_buffers, 0, held_buffer_count * sizeof *held_buffers);
    if (held_textures) memset(held_textures, 0, held_texture_count * sizeof *held_textures);
    if (held_targets) memset(held_targets, 0, held_target_count * sizeof *held_targets);
    gpu_memory = 0;
}

uint64_t tide_platform_gpu_memory(void)
{
    return gpu_memory;
}

uint64_t tide_platform_gpu_written(void)
{
    return gpu_written;
}

// The GPU's copy of a list's 3D mesh, kept in the place the list keeps it.
typedef struct gpu_mesh {
    uint64_t id, other, version; // Which it is (see tide_draw_mesh_data)
    uint32_t space;
    uint32_t epoch;
    tide_gpu_id vertices, indices; // Its buffers
    uint32_t vertex_capacity;      // Bytes `vertices` has room for
    uint32_t index_capacity;       // Bytes `indices` has room for
    uint32_t index_count;          // Three a triangle...
    bool wide;                     // ...of 16 bits where they fit, or 32
    uint32_t frame;                // The frame that last drew it
    uint32_t wanted;               // 1 + the frame whose list draws it
    uint32_t base;                 // A level of detail's: 1 + the place of the mesh whose `vertices` it draws with, having none
} gpu_mesh;

static gpu_mesh *gpu_meshes;
static uint32_t gpu_mesh_count; // Places, as the list's
static void *mesh_3d_upload;    // A mesh's corners or indices, on their way to the GPU
static size_t mesh_3d_upload_capacity;

// Room for `bytes` in mesh_3d_upload.
static void *upload_room(const size_t bytes)
{
    if (bytes > mesh_3d_upload_capacity) {
        mesh_3d_upload = tide_realloc(mesh_3d_upload, mesh_3d_upload_capacity, bytes);
        mesh_3d_upload_capacity = bytes;
    }
    return mesh_3d_upload;
}

// A texture on the GPU: a copy of a draw list's, uploaded when a list first
// draws with it and again when its pixels changed. Immutable pack images may
// stay briefly in a bounded idle cache; other textures go after an unused frame.
typedef struct gpu_texture {
    uint64_t id; // Whose pixels (see tide_draw_texture)
    uint64_t version;
    uint32_t space;
    uint32_t format; // TIDE_TEXELS_*
    int32_t width, height;
    uint32_t epoch; // Its list's when it copied them (see tide_draw_forget)
    tide_gpu_id texture;
    uint32_t frame; // The frame that last drew with it
    bool mipped;    // Its smaller images are of these pixels: made when a draw first reads it between them (prepare_mipmaps)
    bool compressed;
    bool protected; // Referenced by a later step of the current list
} gpu_texture;

static gpu_texture *gpu_textures;
static uint32_t gpu_texture_count, gpu_texture_capacity;
static uint32_t *gpu_texture_index; // Texture places by space/id: 1 + a place, or 0 for none
static uint32_t gpu_texture_index_capacity; // A power of two, or 0: at most half taken
static uint32_t gpu_frame;
static tide_texture_idle *idle_textures;
uint32_t tide_texture_idle_frames = TIDE_TEXTURE_IDLE_FRAMES;
static uint32_t idle_texture_capacity;

// Temporary integer-data storage detached from identities absent from this
// list. Only the current invocation can take it; leftovers are freed before
// overlays. The shape buckets and records retain CPU capacity, never GPU ids.
typedef struct spare_data_texture {
    tide_gpu_id texture;
    int32_t width, height;
    uint32_t next;
} spare_data_texture;

static spare_data_texture *spare_data_textures;
static uint32_t spare_data_count, spare_data_capacity;
static uint32_t *spare_data_index;
static uint32_t spare_data_slots;

// Render targets retain their attachments across passes and frames. Their
// owner supplies a stable identity; resizing replaces both attachments, and
// so does an image that comes to another number of pixels: the display's
// scale changed under a target that's at it, or the window's size under one
// that follows it.
typedef struct gpu_target {
    uint64_t id;
    uint64_t version;
    int width, height; // Its image's pixels
    tide_gpu_id target;
    // How many passes have drawn into it, and for each color image, how many had when its smaller images
    // were last made: they're made again for a draw that reads between them once more have (prepare_mipmaps)
    uint32_t drawn;
    uint32_t mipped[TIDE_GPU_MAX_COLORS];
    // A multisampled one's depth, as a texture: 1 + how many passes had drawn when it was last made of its
    // samples, and 0 for never (prepare_source_target)
    uint32_t resolved;
} gpu_target;

static gpu_target *gpu_targets;
static uint32_t gpu_target_count, gpu_target_capacity;

// How a pipeline reads a shader's texture `i`: a color, unless the shader says it's a depth.
static tide_gpu_read gpu_read(const tide_shader *shader, const uint32_t i)
{
    const uint8_t read = i < TIDE_SHADER_TEXTURES ? shader->texture_reads[i] : TIDE_SHADER_READ_COLOR;
    return read == TIDE_SHADER_READ_DEPTH ? TIDE_GPU_READ_DEPTH :
           read == TIDE_SHADER_READ_COMPARE ? TIDE_GPU_READ_COMPARE :
           read == TIDE_SHADER_READ_CUBE ? TIDE_GPU_READ_CUBE :
           read == TIDE_SHADER_READ_DATA ? TIDE_GPU_READ_DATA : TIDE_GPU_READ_COLOR;
}
static uint32_t target_epoch; // A hot reload discards the old build's target identities

// A cube that's drawn into (tide_cubemap_make), by its own id: the
// GPU's cube, which is what's sampled. Each of its faces is a gpu_target
// beside it, by the face's id, made with the cube and freed before it.
typedef struct gpu_cube {
    uint64_t id;
    uint64_t version;
    int size; // A face's side, in pixels
    tide_gpu_id texture;
    // How many passes have drawn into its faces, and how many had when its smaller images were last made
    uint32_t drawn, mipped;
} gpu_cube;

static gpu_cube *gpu_cubes;
static uint32_t gpu_cube_count, gpu_cube_capacity;

static gpu_cube *find_cube(const uint64_t id)
{
    for (uint32_t i = 0; i < gpu_cube_count; i++) {
        if (gpu_cubes[i].id == id) return &gpu_cubes[i];
    }
    return NULL;
}

static gpu_target *find_target(const uint64_t id)
{
    for (uint32_t i = 0; i < gpu_target_count; i++) {
        if (gpu_targets[i].id == id) return &gpu_targets[i];
    }
    return NULL;
}

static gpu_target *add_target(const uint64_t id)
{
    if (gpu_target_count == gpu_target_capacity) {
        const uint32_t capacity = gpu_target_capacity ? gpu_target_capacity * 2u : 8u;
        gpu_targets = tide_realloc(gpu_targets, gpu_target_capacity * sizeof(gpu_target),
                                   capacity * sizeof(gpu_target));
        gpu_target_capacity = capacity;
    }
    gpu_target *g = &gpu_targets[gpu_target_count++];
    *g = (gpu_target){.id = id};
    return g;
}

// The GPU's cube of a cube that's drawn into, of `size`, with a target for
// each of its faces. Called outside a pass: one that's newly made, or made
// again at another size or after a Resize, is black on every face.
static gpu_cube *cube_for(const uint64_t id, const tide_target_size size, const uint64_t version)
{
    float logical_width, logical_height;
    int width, height;
    bool limited;
    if (!tide_render_target_live(id)
        || !target_extent(size, &root, &logical_width, &logical_height, &width, &height, &limited)) return NULL;
    gpu_cube *c = find_cube(id);
    if (!c) {
        if (gpu_cube_count == gpu_cube_capacity) {
            const uint32_t capacity = gpu_cube_capacity ? gpu_cube_capacity * 2u : 4u;
            gpu_cubes = tide_realloc(gpu_cubes, gpu_cube_capacity * sizeof(gpu_cube), capacity * sizeof(gpu_cube));
            gpu_cube_capacity = capacity;
        }
        c = &gpu_cubes[gpu_cube_count++];
        *c = (gpu_cube){.id = id};
    }
    if (c->texture && c->size == width && c->version == version) return c;
    for (uint32_t i = 0; i < gpu_target_count; i++) { // Its faces' targets go first
        gpu_target *g = &gpu_targets[i];
        if (g->target && tide_render_target_cube_of(g->id) == id && tide_render_target_cubic(g->id)) {
            free_target(g->target);
            g->target = 0;
        }
    }
    if (c->texture) free_texture(c->texture);
    c->size = width;
    c->version = version;
    c->texture = made_cube(tide_render_target_format(id) == TIDE_TARGET_RGBA16F ? TIDE_GPU_COLOR_FLOAT : TIDE_GPU_COLOR_BYTES,
                           width, NULL);
    c->drawn++;
    for (uint32_t face = 0; face < TIDE_GPU_CUBE_FACES; face++) {
        const uint64_t of_face = id | (uint64_t)(face + 1u) << TIDE_RENDER_FACE_SHIFT;
        gpu_target *g = find_target(of_face);
        if (!g) g = add_target(of_face);
        g->width = g->height = width;
        g->version = version;
        g->target = made_face(c->texture, face, width);
        g->drawn++;
        gpu->begin(g->target, true);
        gpu->end();
    }
    if (limited) {
        fprintf(stderr, "tide: a cube of %g pixels a side is more than this GPU has a side (%d): its faces are %d, less sharp\n",
                (double)logical_width, target_most, width);
    }
    return find_cube(id);
}

// The GPU's image of a render target of `size`, under the draw's root. Called
// outside a pass: a newly made image is black, with its depth reset.
static gpu_target *target_for(const uint64_t id, const tide_target_size size, const uint64_t version)
{
    float logical_width, logical_height;
    int width, height;
    bool limited;
    if (!tide_render_target_live(id)
        || !target_extent(size, &root, &logical_width, &logical_height, &width, &height, &limited)) return NULL;
    if (tide_render_target_cubic(id)) { // A cube's face: the cube's own id is no destination
        if (tide_render_target_face(id) < 0 || !cube_for(tide_render_target_cube_of(id), size, version)) return NULL;
        return find_target(id);
    }
    gpu_target *g = find_target(id);
    if (!g) g = add_target(id);
    if (!g->target || g->width != width || g->height != height || g->version != version) {
        if (g->target) free_target(g->target);
        g->width = width, g->height = height;
        g->version = version;
        // What it holds is its id's to say: the same for as long as it's this target
        g->target = made_target(gpu_target_format(id), width, height);
        g->drawn++;
        gpu->begin(g->target, true);
        gpu->end();
        if (limited) {
            fprintf(stderr, "tide: a render target of %g by %g is more pixels than this GPU has a side (%d): its image "
                            "is %d by %d, less sharp\n", (double)logical_width, (double)logical_height, target_most,
                    width, height);
        }
    }
    return g;
}

// Dispose invalidates every alias, even one already recorded in a list.
// A hot reload clears this cache first, before old state pointers are read.
static void forget_disposed_targets(void)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < gpu_target_count; i++) {
        if (!tide_render_target_live(gpu_targets[i].id)) {
            if (gpu_targets[i].target) free_target(gpu_targets[i].target);
            continue;
        }
        gpu_targets[kept++] = gpu_targets[i];
    }
    gpu_target_count = kept;
    kept = 0;
    for (uint32_t i = 0; i < gpu_cube_count; i++) { // A cube's faces went above, with it
        if (!tide_render_target_live(gpu_cubes[i].id)) {
            if (gpu_cubes[i].texture) free_texture(gpu_cubes[i].texture);
            continue;
        }
        gpu_cubes[kept++] = gpu_cubes[i];
    }
    gpu_cube_count = kept;
}

// The pipeline of a shader of the game's (tide_shader), made the first time a
// list draws through it and found again by the shader's id. That's a hash of
// all the shader is, so the same shader in another build of the game (hot
// reloading) is the same pipeline, and nothing here keeps the shader itself.
typedef struct shader_pipeline {
    uint64_t id;
    tide_gpu_id pipeline;
    uint32_t frame; // The frame that last drew through it
    uint8_t blend;  // How it goes over what's there: a pipeline for each way a shader is drawn (tide_blend)
    uint8_t depth;  // ...and for each thing a 3D one does with depth (tide_depth): 0 for a 2D one, and a blit
    uint8_t cull;   // ...and each side of a triangle it leaves out (tide_gpu_cull): none, for those too
    uint8_t state;  // ...and each thing it does with stencil (stencil_state): nothing, for a blit
    bool blit;      // The whole destination, leaving its depth alone, rather than an ordinary mesh
    // What the targets it draws into hold, which a pipeline is made for. Into depth alone, its blend is the
    // first, as none is read
    tide_gpu_target_format into;
    uint32_t warmed; // The table of shaders it was made for before they drew (warm_generation), or 0
    bool used;       // A draw drew with it, which the keys drawn with have (use_key)
} shader_pipeline;

static shader_pipeline *shader_pipelines;
static uint32_t shader_pipeline_count, shader_pipeline_capacity;
static uint32_t *shader_index;         // The pipelines by id: 1 + a place, or 0 for none
static uint32_t shader_index_capacity; // A power of two, or 0: at most half of it is taken
static uint32_t shader_epoch;          // The epoch of the last list that drew through them (see tide_draw_forget)

// The shaders tide_platform_prewarm was last given, which it alone reads, and the
// device their pipelines were made on. Each other table is another generation:
// the pipelines made for the current one stay when a build's first frame lets
// go of the last build's (forget_pipelines), though nothing drew with them yet.
static const tide_shader *const *warm_shaders;
static uint32_t warm_count;
static uint32_t warm_epoch;
static uint32_t warm_generation; // 0 before the first table
static bool warming;             // pipeline_for is asked by tide_platform_prewarm, not by a draw
static tide_gpu_id *warm_asked;  // What the last one asked for, the engine's own too, which may be made in the background
static uint32_t warm_asked_count, warm_asked_capacity;
static bool *warm_done;          // By the table's shader: how it draws is known, as it drew, this run or one before
static uint64_t *warm_names;     // ...and its name (name_key)
static uint32_t warm_guessed;    // used_changes as the others were last made the ways their kind drew (kind_keys)

// How the game's shaders were drawn (a used_key: a shader's name and kind,
// and the rest of its pipeline's key), this run and the runs before: what's
// prewarmed for each of them as the game starts, so the frame that first
// draws one draws with what was made. By the shader's name, not its id, so a
// shader whose code changed since, which has another id, has its pipelines
// made too. They're kept in the game's temporary cache folder, as
// `pipelines.keys`, read as the folder is known and written as the window
// closes, natively; a page keeps those of its own run. A shader that never
// drew is made the ways its kind of shader drew (kind_keys).
typedef struct used_key {
    uint64_t name; // FNV-1a of the shader's name
    tide_gpu_target_format into;
    uint8_t dims, blit, outputs; // Its kind
    uint8_t blend, depth, cull, state; // As shader_pipeline has them
    uint8_t zero;
    uint32_t zero2;
} used_key;
_Static_assert(sizeof(used_key) == 24, "a used key is its file's 24 bytes, with no padding the compiler adds");

enum { USED_KEYS_MOST = 4096, USED_KEYS_HEADER = 24 }; // Keys a file holds at most; and its header: TIDEKEY1, count, 0, hash

static used_key *used_keys;
static uint32_t used_key_count, used_key_capacity;
static uint32_t used_changes; // Bumped as a draw draws with a key that wasn't there
#ifndef __wasm__
static bool used_dirty; // ...and not written yet
#endif

static uint64_t name_key(const tide_shader *shader)
{
    uint64_t hash = UINT64_C(0xCBF29CE484222325);
    const char *name = shader->name ? shader->name : "";
    for (const char *c = name; *c; c++) hash = (hash ^ (uint8_t)*c) * UINT64_C(0x100000001B3);
    return shader->name ? hash : hash ^ shader->id;
}

static used_key key_of(const tide_shader *shader, const bool blit, const uint32_t blend, const uint32_t depth,
                       const uint32_t cull, const uint32_t state, const tide_gpu_target_format into)
{
    return (used_key){.name = name_key(shader), .into = into, .dims = (uint8_t)shader->dims, .blit = blit,
                      .outputs = (uint8_t)tide_shader_outputs(shader), .blend = (uint8_t)blend, .depth = (uint8_t)depth,
                      .cull = (uint8_t)cull, .state = (uint8_t)state};
}

// Whether two keys draw alike: the same shader's, or not (`name`), and the same kind of shader's.
static bool keys_alike(const used_key *a, const used_key *b, const bool name)
{
    return (!name || a->name == b->name) && a->into == b->into && a->dims == b->dims && a->blit == b->blit
           && a->outputs == b->outputs && a->blend == b->blend && a->depth == b->depth && a->cull == b->cull
           && a->state == b->state;
}

static void use_key(const used_key key)
{
    for (uint32_t i = 0; i < used_key_count; i++) {
        if (keys_alike(&used_keys[i], &key, true)) return;
    }
    if (used_key_count == USED_KEYS_MOST) return;
    if (used_key_count == used_key_capacity) {
        const uint32_t capacity = used_key_capacity ? used_key_capacity * 2u : 64u;
        used_keys = tide_realloc(used_keys, used_key_capacity * sizeof(used_key), capacity * sizeof(used_key));
        used_key_capacity = capacity;
    }
    used_keys[used_key_count++] = key;
    used_changes++;
#ifndef __wasm__
    used_dirty = true;
#endif
}

#ifndef __wasm__
static uint64_t keys_hash(const uint8_t *bytes, const size_t size)
{
    uint64_t hash = UINT64_C(0xCBF29CE484222325);
    for (size_t i = 0; i < size; i++) hash = (hash ^ bytes[i]) * UINT64_C(0x100000001B3);
    return hash;
}

static char *keys_file(const char *folder)
{
    const size_t length = strlen(folder) + 32u;
    char *file = tide_alloc(length);
    snprintf(file, length, "%s/pipelines.keys", folder);
    return file;
}

// The keys the folder's file has, among those there are: none of a file that's cut short, changed, or of keys no
// pipeline has.
static void read_keys(const char *folder)
{
    char *file = keys_file(folder);
    size_t size = 0;
    uint8_t *bytes = tide_file_read_whole(file, USED_KEYS_HEADER + (size_t)USED_KEYS_MOST * sizeof(used_key), &size);
    free(file);
    uint32_t count = 0;
    uint64_t hash = 0;
    if (bytes && size >= USED_KEYS_HEADER) {
        memcpy(&count, bytes + 8, 4);
        memcpy(&hash, bytes + 16, 8);
    }
    const bool whole = bytes && size >= USED_KEYS_HEADER && memcmp(bytes, "TIDEKEY1", 8) == 0 && count <= USED_KEYS_MOST
                    && size == USED_KEYS_HEADER + (size_t)count * sizeof(used_key)
                    && hash == keys_hash(bytes + USED_KEYS_HEADER, size - USED_KEYS_HEADER);
    for (uint32_t i = 0; whole && i < count; i++) {
        used_key key;
        memcpy(&key, bytes + USED_KEYS_HEADER + (size_t)i * sizeof(used_key), sizeof key);
        if ((key.dims != 2 && key.dims != 3) || key.blit > 1 || key.outputs < 1 || key.outputs > TIDE_TARGET_COLORS
            || key.blend >= BLENDS || key.depth >= DEPTHS || key.cull >= CULLS || (key.state & 15u) > TIDE_GPU_STENCIL_GREATER_EQUAL
            || (key.state >> 4) > TIDE_GPU_STENCIL_INVERT || key.zero || key.zero2) {
            continue;
        }
        const bool dirty = used_dirty;
        use_key(key);
        used_dirty = dirty; // Read, not drawn: nothing to write
    }
    free(bytes);
}

// The keys into the folder's file, where a draw drew with one that wasn't there: of the shaders of the last table
// prewarmed (a shader that's gone takes its keys with it), or all of them where none was.
static void write_keys(const char *folder)
{
    if (!folder || !used_dirty) return;
    if (warm_generation) {
        uint32_t kept = 0;
        for (uint32_t i = 0; i < used_key_count; i++) {
            bool named = false;
            for (uint32_t k = 0; k < warm_count && !named; k++) named = warm_names[k] == used_keys[i].name;
            if (named) used_keys[kept++] = used_keys[i];
        }
        used_key_count = kept;
    }
    const size_t size = USED_KEYS_HEADER + (size_t)used_key_count * sizeof(used_key);
    uint8_t *bytes = tide_alloc(size);
    memcpy(bytes, "TIDEKEY1", 8);
    memcpy(bytes + 8, &used_key_count, 4);
    memset(bytes + 12, 0, 4);
    if (used_key_count) memcpy(bytes + USED_KEYS_HEADER, used_keys, (size_t)used_key_count * sizeof(used_key));
    const uint64_t hash = keys_hash(bytes + USED_KEYS_HEADER, size - USED_KEYS_HEADER);
    memcpy(bytes + 16, &hash, 8);
    char *file = keys_file(folder);
    if (tide_file_write_whole(file, bytes, size)) used_dirty = false;
    free(file);
    free(bytes);
}
#endif

static void write_used_keys(void)
{
#ifndef __wasm__
    write_keys(gpu_cache_folder);
#endif
}

static uint32_t gpu_epoch; // The device all of the above was made on (tide_gpu.epoch)

// Whether there's a device to draw with. When it's another than last time,
// what the renderer had on the last one went with it: it forgets it, and
// makes it again as lists draw. It can: the lists keep their textures' and
// meshes' copies and their runs' shaders, and the font its atlas.
// The backend is told of the game's cache folder as it changes.
static void follow_cache_folder(void)
{
    const char *folder = tide_platform_cache_folder();
    if (folder == gpu_cache_folder || (folder && gpu_cache_folder && strcmp(folder, gpu_cache_folder) == 0)) return;
#ifndef __wasm__
    write_keys(gpu_cache_folder); // How the last game's shaders were drawn, into its folder
    used_key_count = 0;
    used_dirty = false;
    used_changes++;
    if (folder) read_keys(folder); // ...and how this one's were
#endif
    free(gpu_cache_folder);
    gpu_cache_folder = NULL;
    if (folder) {
        const size_t length = strlen(folder) + 1u;
        gpu_cache_folder = memcpy(tide_alloc(length), folder, length);
    }
    if (!gpu->pipeline_cache) return;
    if (!folder) {
        gpu->pipeline_cache(NULL);
        return;
    }
    const size_t length = strlen(folder) + 1u;
    char *file = tide_alloc(length + 32u);
    snprintf(file, length + 32u, "%s/pipelines.vulkan", folder);
    gpu->pipeline_cache(file);
    free(file);
}

// Whether pipelines are made in the background where the backend can (tide_platform_background_pipelines).
static bool background_pipelines;

void tide_platform_background_pipelines(const bool on)
{
    background_pipelines = on;
}

static bool gpu_ready(void)
{
    follow_cache_folder();
    if (gpu->background) gpu->background(background_pipelines);
    const uint32_t epoch = gpu->epoch();
    if (epoch == 0 || epoch == gpu_epoch) return epoch != 0;
    if (gpu->time_reset) gpu->time_reset();
    free(gpu_state.target_pixels);
    free(gpu_state.frame_pixels);
    memset(&gpu_state, 0, sizeof gpu_state);
    target_most = gpu->target_limit();
    if (gpu_meshes) memset(gpu_meshes, 0, gpu_mesh_count * sizeof(gpu_mesh));
    spare_buffer_count = 0; // The device they were on went with them
    gpu_texture_count = 0;
    if (gpu_texture_index) memset(gpu_texture_index, 0, (size_t)gpu_texture_index_capacity * sizeof(uint32_t));
    spare_data_count = 0; // A lost device took these objects too.
    gpu_target_count = 0;
    gpu_cube_count = 0;
    forget_held();
    engine_made_count = 0;
    shader_pipeline_count = 0;
    stencil_pipeline_count = 0;
    if (shader_index) memset(shader_index, 0, shader_index_capacity * sizeof(uint32_t));
    gpu_epoch = epoch;
    return true;
}

// A shader that shaders.h has in two pieces, as one: made once, into *whole,
// and kept. NULL where it has none (WGSL is only the web's).
static const char *joined(char **whole, const char *first, const char *second)
{
    if (!first || !second || *whole) return *whole;
    const size_t length = strlen(first);
    *whole = tide_alloc(length + strlen(second) + 1u);
    memcpy(*whole, first, length);
    strcpy(*whole + length, second);
    return *whole;
}

// Which of a 3D mesh's triangles a pipeline leaves out, for its command's cull
// (a tide_cull, with TIDE_DRAW_MIRRORED or not) under a camera that mirrors or
// doesn't. A triangle's front goes clockwise as its mesh has it, and reaches
// the GPU that way unless what it went through mirrored it an odd number of
// times, its instance's transform and the camera.
static tide_gpu_cull gpu_cull(const uint32_t cull, const bool mirrored)
{
    const uint32_t side = cull & 3u;
    if (side == TIDE_CULL_NONE) return TIDE_GPU_CULL_NONE;
    const bool back = side != TIDE_CULL_FRONT;
    return back != (((cull & TIDE_DRAW_MIRRORED) != 0) != mirrored) ? TIDE_GPU_CULL_BACK : TIDE_GPU_CULL_FRONT;
}

// One of the engine's pipelines, for one way of blending, one thing done with
// stencil (`state`: stencil_state) and, for the 3D meshes', one thing done
// with depth and one side left out, into targets that hold what `into` says:
// made if there's none. Into targets of depth alone, where a color has nowhere
// to go, it's the 3D meshes' that write depth, and whatever stores stencil.
static tide_gpu_id engine_pipeline(const int kind, uint32_t blend, uint32_t depth, uint32_t cull,
                                   const tide_gpu_target_format into, const uint32_t state)
{
    static const tide_gpu_blend blends[BLENDS] = {
        [TIDE_BLEND_ALPHA] = TIDE_GPU_BLEND_ALPHA, [TIDE_BLEND_ADD] = TIDE_GPU_BLEND_ADD,
        [TIDE_BLEND_MULTIPLY] = TIDE_GPU_BLEND_MULTIPLY, [TIDE_BLEND_REPLACE] = TIDE_GPU_BLEND_REPLACE};
    const bool bare = into == TIDE_GPU_TARGET_DEPTH;
    if (blend >= BLENDS || bare) blend = TIDE_BLEND_ALPHA; // None is read where there's no color
    if (kind != PIPELINE_MESHES_3D || depth >= DEPTHS) depth = TIDE_DEPTH_READ_WRITE;
    if (kind != PIPELINE_MESHES_3D || cull >= CULLS) cull = TIDE_GPU_CULL_NONE; // Nothing in 2D has a back
    tide_gpu_id *made;
    if (state) { // One that tests or stores stencil: among the few of those
        made = NULL;
        for (uint32_t i = 0; i < stencil_pipeline_count && !made; i++) {
            stencil_pipeline *p = &stencil_pipelines[i];
            if (p->kind == kind && p->blend == blend && p->depth == depth && p->cull == cull && p->state == state
                && p->into == into) {
                made = &p->pipeline;
            }
        }
        if (!made) {
            if (stencil_pipeline_count == stencil_pipeline_capacity) {
                const uint32_t capacity = stencil_pipeline_capacity ? stencil_pipeline_capacity * 2u : 16u;
                stencil_pipelines = tide_realloc(stencil_pipelines, stencil_pipeline_capacity * sizeof(stencil_pipeline),
                                                 capacity * sizeof(stencil_pipeline));
                stencil_pipeline_capacity = capacity;
            }
            stencil_pipelines[stencil_pipeline_count] = (stencil_pipeline){
                (uint8_t)kind, (uint8_t)blend, (uint8_t)depth, (uint8_t)cull, (uint8_t)state, into, 0};
            made = &stencil_pipelines[stencil_pipeline_count++].pipeline;
        }
    } else {
        made = &pipelines_into(into)->made[kind][blend][depth][cull];
    }
    if (*made) return *made;
    static char *smooth_wgsl, *solid_wgsl; // Their WGSL, joined once (see joined)
    tide_gpu_pipeline_desc desc;
    switch (kind) {
    case PIPELINE_SHAPES:
        desc = (tide_gpu_pipeline_desc){
            .name = "shapes",
            .glsl_vertex = shape_glsl_vertex,
            .glsl_fragment = shape_glsl_fragment,
            .wgsl = shape_wgsl,
            .spirv = SPIRV_OF(shape),
            .spirv_words = SPIRV_WORDS(shape),
            .slots = {{2 * sizeof(float), false}, {sizeof(shape), true}}, // The quad's corners, and an instance a shape
            .slot_count = 2,
            .attributes = {{0, TIDE_GPU_FLOAT2, 0},
                           {1, TIDE_GPU_FLOAT4, offsetof(shape, a)},
                           {1, TIDE_GPU_BYTES4, offsetof(shape, color)},
                           {1, TIDE_GPU_FLOAT, offsetof(shape, kind)}},
            .attribute_count = 4,
            .uniform_size = 16 * sizeof(float),
        };
        break;
    case PIPELINE_SMOOTH_SHAPES:
        desc = (tide_gpu_pipeline_desc){
            .name = "smooth shapes",
            .glsl_vertex = smooth_shape_glsl_vertex,
            .glsl_fragment = smooth_shape_glsl_fragment,
            .wgsl = joined(&smooth_wgsl, smooth_shape_wgsl_vertex, smooth_shape_wgsl_fragment),
            .spirv = SPIRV_OF(smooth_shape),
            .spirv_words = SPIRV_WORDS(smooth_shape),
            // The quad's corners, an instance a shape, and an extra for each
            .slots = {{2 * sizeof(float), false}, {sizeof(shape), true}, {sizeof(shape_extra), true}},
            .slot_count = 3,
            .attributes = {{0, TIDE_GPU_FLOAT2, 0},
                           {1, TIDE_GPU_FLOAT4, offsetof(shape, a)},
                           {1, TIDE_GPU_BYTES4, offsetof(shape, color)},
                           {1, TIDE_GPU_FLOAT, offsetof(shape, kind)},
                           {2, TIDE_GPU_FLOAT4, offsetof(shape_extra, turn)},
                           {2, TIDE_GPU_FLOAT2, offsetof(shape_extra, soft)},
                           {2, TIDE_GPU_BYTES4, offsetof(shape_extra, color2)},
                           {2, TIDE_GPU_BYTES4, offsetof(shape_extra, rim)}},
            .attribute_count = 8,
            .uniform_size = sizeof(screen_uniforms),
        };
        break;
    case PIPELINE_SOLID_SHAPES: // The smooth shapes' again, with a radius for each shape in place of its extra
        desc = (tide_gpu_pipeline_desc){
            .name = "solid shapes",
            .glsl_vertex = solid_shape_glsl_vertex,
            .glsl_fragment = smooth_shape_glsl_fragment,
            .wgsl = joined(&solid_wgsl, solid_shape_wgsl_vertex, smooth_shape_wgsl_fragment),
            .spirv = SPIRV_OF(solid_shape),
            .spirv_words = SPIRV_WORDS(solid_shape),
            .slots = {{2 * sizeof(float), false}, {sizeof(shape), true}, {sizeof(float), true}},
            .slot_count = 3,
            .attributes = {{0, TIDE_GPU_FLOAT2, 0},
                           {1, TIDE_GPU_FLOAT4, offsetof(shape, a)},
                           {1, TIDE_GPU_BYTES4, offsetof(shape, color)},
                           {1, TIDE_GPU_FLOAT, offsetof(shape, kind)},
                           {2, TIDE_GPU_FLOAT, 0}},
            .attribute_count = 5,
            .uniform_size = sizeof(screen_uniforms),
        };
        break;
    case PIPELINE_MESHES:
    case PIPELINE_TEXT: // A mesh's corners, with pixels of its own
        desc = (tide_gpu_pipeline_desc){
            .name = kind == PIPELINE_TEXT ? "text" : "meshes",
            .glsl_vertex = mesh_glsl_vertex,
            .glsl_fragment = kind == PIPELINE_TEXT ? text_glsl_fragment : mesh_glsl_fragment,
            .wgsl = kind == PIPELINE_TEXT ? text_wgsl : mesh_wgsl,
            .spirv = kind == PIPELINE_TEXT ? SPIRV_OF(text) : SPIRV_OF(mesh),
            .spirv_words = kind == PIPELINE_TEXT ? SPIRV_WORDS(text) : SPIRV_WORDS(mesh),
            .slots = {{sizeof(mesh_vertex), false}},
            .slot_count = 1,
            .attributes = {{0, TIDE_GPU_FLOAT2, offsetof(mesh_vertex, position)},
                           {0, TIDE_GPU_FLOAT2, offsetof(mesh_vertex, uv)},
                           {0, TIDE_GPU_BYTES4, offsetof(mesh_vertex, color)}},
            .attribute_count = 3,
            .uniform_size = sizeof(screen_uniforms),
            .texture_count = 1,
        };
        break;
    case PIPELINE_QUADS:
    case PIPELINE_TEXT_QUADS: // The meshes' and the text's again, each corner of a quad picked from its instance
        desc = (tide_gpu_pipeline_desc){
            .name = kind == PIPELINE_TEXT_QUADS ? "text quads" : "quads",
            .glsl_vertex = quad_glsl_vertex,
            .glsl_fragment = kind == PIPELINE_TEXT_QUADS ? text_glsl_fragment : mesh_glsl_fragment,
            .wgsl = kind == PIPELINE_TEXT_QUADS ? text_quad_wgsl : quad_wgsl,
            .spirv = kind == PIPELINE_TEXT_QUADS ? SPIRV_OF(text_quad) : SPIRV_OF(quad),
            .spirv_words = kind == PIPELINE_TEXT_QUADS ? SPIRV_WORDS(text_quad) : SPIRV_WORDS(quad),
            .slots = {{sizeof(quad), true}},
            .slot_count = 1,
            .attributes = {{0, TIDE_GPU_FLOAT4, offsetof(quad, box)},
                           {0, TIDE_GPU_FLOAT4, offsetof(quad, span)},
                           {0, TIDE_GPU_BYTES4, offsetof(quad, color)},
                           {0, TIDE_GPU_INT, offsetof(quad, pattern)}},
            .attribute_count = 4,
            .uniform_size = sizeof(screen_uniforms),
            .texture_count = 1,
        };
        break;
    default:
        desc = (tide_gpu_pipeline_desc){
            .name = "3D meshes",
            .glsl_vertex = mesh_3d_glsl_vertex,
            .glsl_fragment = mesh_glsl_fragment,
            .wgsl = mesh_3d_wgsl,
            .spirv = SPIRV_OF(mesh_3d),
            .spirv_words = SPIRV_WORDS(mesh_3d),
            .slots = {{sizeof(mesh_3d_vertex), false}, {sizeof(tide_float4x4), true}}, // Its corners, and an instance a transform
            .slot_count = 2,
            .attributes = {{0, TIDE_GPU_FLOAT3, offsetof(mesh_3d_vertex, position)},
                           {0, TIDE_GPU_FLOAT2, offsetof(mesh_3d_vertex, uv)},
                           {0, TIDE_GPU_BYTES4, offsetof(mesh_3d_vertex, color)},
                           {1, TIDE_GPU_FLOAT4, 0 * sizeof(tide_float4)},
                           {1, TIDE_GPU_FLOAT4, 1 * sizeof(tide_float4)},
                           {1, TIDE_GPU_FLOAT4, 2 * sizeof(tide_float4)},
                           {1, TIDE_GPU_FLOAT4, 3 * sizeof(tide_float4)}},
            .attribute_count = 7,
            .uniform_size = sizeof(mesh_3d_uniforms),
            .texture_count = 1,
            .depth = gpu_depths[depth],
            .cull = (tide_gpu_cull)cull,
        };
        break;
    }
    desc.blend = blends[blend];
    desc.stencil_test = (tide_gpu_stencil_test)(state & 15u);
    desc.stencil_op = (tide_gpu_stencil_op)(state >> 4);
    desc.into = into;
    desc.outputs = 1; // The one color they have: a target's first image, whatever else it holds
    if (bare && kind == PIPELINE_MESHES_3D) desc.name = "3D meshes into depth alone";
    return *made = gpu->pipeline(&desc);
}

// A clear, in a version for each kind of target: a triangle over it, which
// sets depth back and stencil to 0, and fills each color image it has with
// the clear's color.
static tide_gpu_id clear_pipeline(const tide_gpu_target_format into)
{
    tide_gpu_id *made = &pipelines_into(into)->clear;
    if (*made) return *made;
    const bool several = tide_gpu_target_colors(into) > 1;
    return *made = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = into == TIDE_GPU_TARGET_DEPTH ? "clears of depth alone" : several ? "clears of several colors" : "clears",
        .glsl_vertex = clear_glsl_vertex,
        .glsl_fragment = several ? clear_all_glsl_fragment : clear_glsl_fragment,
        .wgsl = several ? clear_all_wgsl : clear_wgsl,
        .spirv = several ? SPIRV_OF(clear_all) : SPIRV_OF(clear),
        .spirv_words = several ? SPIRV_WORDS(clear_all) : SPIRV_WORDS(clear),
        .uniform_size = 4 * sizeof(float),
        .blend = TIDE_GPU_BLEND_REPLACE,
        .depth = TIDE_GPU_DEPTH_RESET,
        .stencil_op = TIDE_GPU_STENCIL_ZERO,
        .into = into,
        .outputs = several ? TIDE_GPU_MAX_COLORS : 1u,
    });
}

// A clear of depth alone: the clears' triangle with a clear color over what's
// there, which leaves every color image as it is, and no stencil stored.
static tide_gpu_id depth_clear_pipeline(const tide_gpu_target_format into)
{
    tide_gpu_id *made = &pipelines_into(into)->depth_clear;
    if (*made) return *made;
    const bool several = tide_gpu_target_colors(into) > 1;
    return *made = gpu->pipeline(&(tide_gpu_pipeline_desc){
        .name = "clears of depth",
        .glsl_vertex = clear_glsl_vertex,
        .glsl_fragment = several ? clear_all_glsl_fragment : clear_glsl_fragment,
        .wgsl = several ? clear_all_wgsl : clear_wgsl,
        .spirv = several ? SPIRV_OF(clear_all) : SPIRV_OF(clear),
        .spirv_words = several ? SPIRV_WORDS(clear_all) : SPIRV_WORDS(clear),
        .uniform_size = 4 * sizeof(float),
        .blend = TIDE_GPU_BLEND_ALPHA,
        .depth = TIDE_GPU_DEPTH_RESET,
        .into = into,
        .outputs = several ? TIDE_GPU_MAX_COLORS : 1u,
    });
}

// The window's clears' pipeline, the quad and the white pixel, the first time a list is drawn.
static void renderer_start(void)
{
    if (gpu_state.corners) return;
    clear_pipeline(TIDE_GPU_TARGET_COLOR);
    static const float corners[12] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0};
    gpu_state.corners = made_buffer(TIDE_GPU_VERTICES, sizeof corners, corners);
    gpu_state.white = made_texture(TIDE_GPU_RGBA8, 1, 1, (const uint8_t[4]){255, 255, 255, 255});
}

// Whether a step leaves anything in a target of depth alone, where a color
// has nowhere to go: a clear, which sets depth and stencil back, a 3D mesh
// that writes depth, through a shader of the game's or not, and whatever
// draws and stores stencil. Everything else only draws colors, which such a
// target has none of, and is passed by there.
static bool writes_depth(const tide_draw_list *list, const draw_step *s)
{
    switch (s->kind) {
    case STEP_BLIT: return false;
    case STEP_SHAPES:
    case STEP_TEXT:
    case STEP_MESH: return stencil_stores(s->stencil);
    case STEP_MESH_3D: return s->depth == TIDE_DEPTH_READ_WRITE || stencil_stores(s->stencil);
    case STEP_SHADED:
        return (list->runs[s->first].shader->dims == 3 && s->depth == TIDE_DEPTH_READ_WRITE) || stencil_stores(s->stencil);
    default: return true; // A clear, and what only says where the steps after it go
    }
}

// Whether a step goes into a target of depth alone, and leaves nothing there.
static bool leaves_nothing(const tide_draw_list *list, const draw_step *s)
{
    return frame_targets[s->target].bare && !writes_depth(list, s);
}

// The pipeline each step of the engine's own draws through, before anything draws.
static void find_pipelines(const tide_draw_list *list)
{
    for (uint32_t i = 0; i < step_count; i++) {
        draw_step *s = &steps[i];
        if (!frame_targets[s->target].valid || leaves_nothing(list, s)) continue;
        const tide_gpu_target_format into = frame_targets[s->target].into;
        const uint32_t state = stencil_state(s->stencil);
        switch (s->kind) {
        case STEP_SHAPES:
            s->pipeline = engine_pipeline(s->smooth ? PIPELINE_SMOOTH_SHAPES : s->solid ? PIPELINE_SOLID_SHAPES : PIPELINE_SHAPES,
                                          s->blend, 0, 0, into, state);
            break;
        case STEP_MESH:
            s->pipeline = engine_pipeline(s->quads ? PIPELINE_QUADS : PIPELINE_MESHES, s->blend, 0, 0, into, state);
            break;
        case STEP_TEXT:
            s->pipeline = engine_pipeline(s->quads ? PIPELINE_TEXT_QUADS : PIPELINE_TEXT, s->blend, 0, 0, into, state);
            break;
        case STEP_MESH_3D:
            s->pipeline = engine_pipeline(PIPELINE_MESHES_3D, s->blend, s->depth, s->cull, into, state);
            break;
        case STEP_CLEAR: s->pipeline = s->depth_alone ? depth_clear_pipeline(into) : clear_pipeline(into); break;
        default: break;
        }
    }
}

// The GPU's copy of the list's 3D mesh `mesh`, uploaded if it hasn't got it.
static gpu_mesh *mesh_for(const tide_draw_list *list, const uint32_t mesh)
{
    if (list->mesh_count > gpu_mesh_count) {
        gpu_meshes = tide_realloc(gpu_meshes, gpu_mesh_count * sizeof(gpu_mesh), list->mesh_count * sizeof(gpu_mesh));
        memset(gpu_meshes + gpu_mesh_count, 0, (list->mesh_count - gpu_mesh_count) * sizeof(gpu_mesh));
        gpu_mesh_count = list->mesh_count;
    }
    const tide_draw_mesh_data *m = &list->meshes[mesh - 1u];
    gpu_mesh *g = &gpu_meshes[mesh - 1u];
    g->frame = gpu_frame;
    // A level of detail has its mesh's corners, which that mesh keeps: its own are its triangles
    const bool level = m->base && m->base <= list->mesh_count && m->base != mesh && !list->meshes[m->base - 1u].base
                    && list->meshes[m->base - 1u].space != TIDE_MESH_NONE;
    if (level) mesh_for(list, m->base);
    if ((g->vertices || g->base) && g->id == m->id && g->other == m->other && g->version == m->version && g->space == m->space
        && g->epoch == m->epoch && g->base == (level ? m->base : 0u)) {
        return g;
    }
    if (level && g->vertices) { // The place was a whole mesh's
        spare_buffer_of(g->vertices, TIDE_GPU_VERTICES, g->vertex_capacity);
        g->vertices = 0;
        g->vertex_capacity = 0;
    }
    g->base = level ? m->base : 0u;
    // Its corners, each once: as they are for a shader of the game's, which reads their bytes
    const void *corners = m->bytes;
    size_t vertex_bytes = (size_t)m->vertex_count * m->vertex_size;
    if (level) {
        vertex_bytes = 0;
    } else if (m->space != TIDE_MESH_BYTES) {
        vertex_bytes = (size_t)m->vertex_count * sizeof(mesh_3d_vertex);
        mesh_3d_vertex *packed = upload_room(vertex_bytes);
        for (uint32_t i = 0; i < m->vertex_count; i++) {
            const tide_vertex3 *v = &m->vertices[i];
            const rgba color = to_rgba(v->color);
            packed[i] = (mesh_3d_vertex){{v->position.x, v->position.y, v->position.z}, {v->uv.x, v->uv.y},
                                         {color.r, color.g, color.b, color.a}};
        }
        corners = packed;
    }
    if (level) {
        // Nothing of its own
    } else if (g->vertices && vertex_bytes <= g->vertex_capacity) {
        gpu->buffer_write(g->vertices, 0, corners, vertex_bytes);
    } else {
        spare_buffer_of(g->vertices, TIDE_GPU_VERTICES, g->vertex_capacity);
        g->vertices = taken_buffer(TIDE_GPU_VERTICES, vertex_bytes, corners, &g->vertex_capacity);
    }
    // ...and its triangles, in 16 bits where they fit, to a multiple of 4 bytes, as buffers take them
    const bool wide = m->vertex_count > 65536u;
    const size_t index_bytes = ((size_t)m->index_count * (wide ? sizeof(uint32_t) : sizeof(uint16_t)) + 3u) & ~(size_t)3u;
    const void *indices = m->indices;
    if (!wide) {
        uint16_t *narrow = upload_room(index_bytes);
        memset(narrow, 0, index_bytes);
        for (uint32_t i = 0; i < m->index_count; i++) narrow[i] = (uint16_t)m->indices[i];
        indices = narrow;
    }
    if (g->indices && index_bytes <= g->index_capacity) {
        gpu->buffer_write(g->indices, 0, indices, index_bytes);
    } else {
        spare_buffer_of(g->indices, TIDE_GPU_INDICES, g->index_capacity);
        g->indices = taken_buffer(TIDE_GPU_INDICES, index_bytes, indices, &g->index_capacity);
    }
    g->id = m->id;
    g->other = m->other;
    g->version = m->version;
    g->space = m->space;
    g->epoch = m->epoch;
    g->index_count = m->index_count;
    g->wide = wide;
    return g;
}

// The buffer a mesh's corners are in: its own, or for a level of detail, its mesh's.
static tide_gpu_id corners_of(const gpu_mesh *g)
{
    return g->base && g->base <= gpu_mesh_count ? gpu_meshes[g->base - 1u].vertices : g->vertices;
}

// A buffer for `count` elements of `size` bytes, in place of `*buffer` when
// it has no room for them: its capacity doubles until it does.
static void buffer_room(tide_gpu_id *buffer, uint32_t *capacity, const uint32_t count, const size_t size,
                        const uint32_t at_least)
{
    if (count <= *capacity) return;
    uint32_t grown = *capacity ? *capacity : at_least;
    while (grown < count) grown *= 2u;
    if (*buffer) free_buffer(*buffer);
    *buffer = made_buffer(TIDE_GPU_VERTICES, (size_t)grown * size, NULL);
    *capacity = grown;
}

// The list's matrices into the instance buffer, which grows as they need,
// and its 3D meshes onto the GPU, before anything draws.
static void upload_meshes_3d(const tide_draw_list *list)
{
    bool any = false;
    for (uint32_t i = 0; i < step_count; i++) {
        if (steps[i].kind != STEP_MESH_3D) continue;
        any = true;
        mesh_for(list, steps[i].mesh);
    }
    if (!any) return;
    buffer_room(&gpu_state.matrices, &gpu_state.matrix_capacity, list->matrix_count, sizeof(tide_float4x4), 1024u);
    gpu->buffer_write(gpu_state.matrices, 0, list->matrices, (size_t)list->matrix_count * sizeof(tide_float4x4));
}

// A place of a mesh the list draws, and of the mesh its corners are in.
static void mark_mesh(const tide_draw_list *list, const uint32_t mesh)
{
    if (!mesh || mesh > list->mesh_count || mesh > gpu_mesh_count) return;
    gpu_meshes[mesh - 1u].wanted = gpu_frame + 1u;
    const uint32_t base = list->meshes[mesh - 1u].base;
    if (base && base <= gpu_mesh_count) gpu_meshes[base - 1u].wanted = gpu_frame + 1u;
}

// The buffers of the places of meshes the list doesn't draw are spares for
// those it draws that need some, before any is uploaded.
static void spare_unused_meshes(const tide_draw_list *list)
{
    for (uint32_t i = 0; i < step_count; i++) {
        const draw_step *s = &steps[i];
        if (s->kind == STEP_MESH_3D) mark_mesh(list, s->mesh);
        else if (s->kind == STEP_SHADED || s->kind == STEP_BLIT) mark_mesh(list, list->runs[s->first].mesh);
    }
    for (uint32_t i = 0; i < gpu_mesh_count; i++) {
        gpu_mesh *g = &gpu_meshes[i];
        if (g->wanted == gpu_frame + 1u || (!g->vertices && !g->indices)) continue;
        spare_buffer_of(g->vertices, TIDE_GPU_VERTICES, g->vertex_capacity);
        spare_buffer_of(g->indices, TIDE_GPU_INDICES, g->index_capacity);
        *g = (gpu_mesh){0};
    }
}

// Lets go of the 3D meshes the frame didn't draw, and the spares no mesh took.
static void forget_meshes_3d(void)
{
    for (uint32_t i = 0; i < gpu_mesh_count; i++) {
        if ((gpu_meshes[i].vertices || gpu_meshes[i].indices) && gpu_meshes[i].frame != gpu_frame) {
            if (gpu_meshes[i].vertices) free_buffer(gpu_meshes[i].vertices);
            if (gpu_meshes[i].indices) free_buffer(gpu_meshes[i].indices);
            gpu_meshes[i] = (gpu_mesh){0};
        }
    }
    free_spare_buffers();
}

// Where the pipeline of the shader `id`, blending as `blend`, doing `depth` with depth and `state`
// with stencil and leaving `cull` out, into targets that hold what `into` says, is in the index, or
// would go.
static uint32_t shader_slot(const uint64_t id, const bool blit, const uint8_t blend, const uint8_t depth,
                            const uint8_t cull, const uint8_t state, const tide_gpu_target_format into)
{
    const uint32_t mask = shader_index_capacity - 1u;
    uint32_t i = (uint32_t)(id ^ id >> 32 ^ (blit ? 0x9e3779b9u : 0u) ^ blend * 0x85ebca6bu ^ depth * 0xc2b2ae35u
                            ^ cull * 0x27d4eb2fu ^ state * 0x2545f491u ^ into * 0x165667b1u) & mask;
    while (shader_index[i]) {
        const shader_pipeline *p = &shader_pipelines[shader_index[i] - 1u];
        if (p->id == id && p->blit == blit && p->blend == blend && p->depth == depth && p->cull == cull
            && p->state == state && p->into == into) {
            break;
        }
        i = (i + 1u) & mask;
    }
    return i;
}

// The index again, from the pipelines there are, with room for another.
static void index_shaders(void)
{
    uint32_t capacity = shader_index_capacity ? shader_index_capacity : 64u;
    while (capacity / 2u <= shader_pipeline_count) capacity *= 2u;
    if (capacity != shader_index_capacity) {
        free(shader_index);
        shader_index = tide_alloc_zeroed(capacity, sizeof(uint32_t));
        shader_index_capacity = capacity;
    } else {
        memset(shader_index, 0, capacity * sizeof(uint32_t));
    }
    for (uint32_t i = 0; i < shader_pipeline_count; i++) {
        const shader_pipeline *p = &shader_pipelines[i];
        shader_index[shader_slot(p->id, p->blit, p->blend, p->depth, p->cull, p->state, p->into)] = i + 1u;
    }
}

// The pipeline of a shader of the game's for one way of blending, one thing
// done with stencil (`state`: stencil_state) and, for a 3D one, one thing done
// with depth and one side left out, made if there's none: it reads a mesh's
// vertices as they are from the first slot, and from the second an instance's
// bytes, a float4 or an int4 an attribute. It's for targets that hold what
// `into` says, whose color images its frag's outputs go to in order: one with
// no image in its place goes nowhere (into depth alone, that's all of them;
// the frag still runs, as it may discard the pixel), and the images past them
// are left as they were.
static tide_gpu_id pipeline_for(const tide_shader *shader, const bool blit, uint32_t blend, uint32_t depth,
                                uint32_t cull, const tide_gpu_target_format into, uint32_t state)
{
    static const tide_gpu_blend blends[BLENDS] = {
        [TIDE_BLEND_ALPHA] = TIDE_GPU_BLEND_ALPHA, [TIDE_BLEND_ADD] = TIDE_GPU_BLEND_ADD,
        [TIDE_BLEND_MULTIPLY] = TIDE_GPU_BLEND_MULTIPLY, [TIDE_BLEND_REPLACE] = TIDE_GPU_BLEND_REPLACE};
    if (blend >= BLENDS) blend = blit ? TIDE_BLEND_REPLACE : TIDE_BLEND_ALPHA;
    if (into == TIDE_GPU_TARGET_DEPTH) blend = TIDE_BLEND_ALPHA; // None is read
    // Only a 3D mesh has depth: a 2D one draws over what's there, and a blit leaves its destination's alone
    const bool deep = !blit && shader->dims == 3;
    if (!deep || depth >= DEPTHS) depth = TIDE_DEPTH_READ_WRITE;
    if (!deep || cull >= CULLS) cull = TIDE_GPU_CULL_NONE; // ...and only a 3D mesh's triangles have a back
    if (blit) state = 0; // A blit neither tests stencil nor stores any
    if (shader_pipeline_count >= shader_index_capacity / 2u) index_shaders();
    const uint32_t slot = shader_slot(shader->id, blit, (uint8_t)blend, (uint8_t)depth, (uint8_t)cull, (uint8_t)state, into);
    if (shader_index[slot]) {
        shader_pipeline *p = &shader_pipelines[shader_index[slot] - 1u];
        if (warming) {
            p->warmed = warm_generation;
        } else {
            p->frame = gpu_frame;
            if (!p->used) use_key(key_of(shader, blit, blend, depth, cull, state, into));
            p->used = true;
        }
        return p->pipeline;
    }
    static const tide_gpu_attribute_format formats[] = {
        [TIDE_SHADER_FLOAT] = TIDE_GPU_FLOAT,   [TIDE_SHADER_FLOAT2] = TIDE_GPU_FLOAT2,
        [TIDE_SHADER_FLOAT3] = TIDE_GPU_FLOAT3, [TIDE_SHADER_FLOAT4] = TIDE_GPU_FLOAT4,
        [TIDE_SHADER_INT] = TIDE_GPU_INT,       [TIDE_SHADER_INT2] = TIDE_GPU_INT2,
        [TIDE_SHADER_INT3] = TIDE_GPU_INT3,     [TIDE_SHADER_INT4] = TIDE_GPU_INT4,
    };
    const uint32_t instance_size = tide_shader_instance_size(shader);
    tide_gpu_pipeline_desc desc = {
        .name = shader->name ? shader->name : "a shader",
        .glsl_vertex = shader->glsl_vertex,
        .glsl_fragment = shader->glsl_fragment,
        .wgsl = shader->wgsl,
        .spirv = shader->spirv,
        .spirv_words = shader->spirv_words,
        .slots = {{shader->vertex_size, false}, {instance_size, true}},
        .slot_count = instance_size ? 2u : 1u,
        .uniform_size = (uint32_t)sizeof(shaded_uniforms) + tide_shader_uniform_size(shader),
        .texture_count = shader->texture_count,
        .blend = blends[blend],
        .depth = deep ? gpu_depths[depth] : TIDE_GPU_DEPTH_NONE,
        .cull = (tide_gpu_cull)cull,
        .stencil_test = (tide_gpu_stencil_test)(state & 15u),
        .stencil_op = (tide_gpu_stencil_op)(state >> 4),
        .into = into,
        .outputs = tide_shader_outputs(shader),
    };
    for (uint32_t i = 0; i < shader->texture_count && i < TIDE_GPU_MAX_TEXTURES; i++) desc.reads[i] = gpu_read(shader, i);
    for (uint32_t i = 0; i < shader->attribute_count; i++) {
        desc.attributes[i].slot = 0;
        desc.attributes[i].format = formats[shader->attributes[i].format];
        desc.attributes[i].offset = shader->attributes[i].offset;
    }
    desc.attribute_count = shader->attribute_count;
    for (uint32_t i = 0; i < shader->instance_floats + shader->instance_ints; i++) {
        const uint32_t at = desc.attribute_count++;
        desc.attributes[at].slot = 1;
        desc.attributes[at].format = i < shader->instance_floats ? TIDE_GPU_FLOAT4 : TIDE_GPU_INT4;
        desc.attributes[at].offset = i * 16u;
    }
    if (shader_pipeline_count == shader_pipeline_capacity) {
        const uint32_t capacity = shader_pipeline_capacity ? shader_pipeline_capacity * 2u : 16u;
        shader_pipelines = tide_realloc(shader_pipelines, shader_pipeline_capacity * sizeof(shader_pipeline),
                                        capacity * sizeof(shader_pipeline));
        shader_pipeline_capacity = capacity;
    }
    const tide_gpu_id pipeline = gpu->pipeline(&desc);
    shader_pipelines[shader_pipeline_count] = (shader_pipeline){shader->id, pipeline, gpu_frame, (uint8_t)blend,
                                                                (uint8_t)depth, (uint8_t)cull, (uint8_t)state, blit, into,
                                                                warming ? warm_generation : 0u, !warming};
    shader_index[slot] = ++shader_pipeline_count;
    if (!warming) use_key(key_of(shader, blit, blend, depth, cull, state, into));
    return pipeline;
}

// Lets go of the pipelines the frame didn't draw through: for the first frame
// of another build of the game (see tide_draw_forget), whose shaders that
// didn't change keep theirs.
static void forget_pipelines(void)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < shader_pipeline_count; i++) {
        const bool warm = warm_generation && shader_pipelines[i].warmed == warm_generation;
        if (shader_pipelines[i].frame != gpu_frame && !warm) {
            gpu->pipeline_free(shader_pipelines[i].pipeline);
            continue;
        }
        shader_pipelines[kept++] = shader_pipelines[i];
    }
    if (kept == shader_pipeline_count) return;
    shader_pipeline_count = kept;
    index_shaders();
}

// Whether a shader takes no more than a pipeline does.
static bool shader_fits(const tide_shader *shader)
{
    const uint64_t instance_fields = (uint64_t)shader->instance_floats + shader->instance_ints;
    const uint64_t uniform_fields = (uint64_t)shader->uniform_floats + shader->uniform_ints;
    if (shader->attribute_count + instance_fields > TIDE_SHADER_ATTRIBUTES
        || shader->texture_count > TIDE_SHADER_TEXTURES || uniform_fields * 16u > TIDE_SHADER_UNIFORM_BYTES) {
        return false;
    }
    for (uint32_t i = 0; i < shader->attribute_count; i++) {
        if (shader->attributes[i].format > TIDE_SHADER_INT4) return false;
    }
    return true;
}

// Whether the list's run `place` can draw: it wasn't forgotten (see
// tide_draw_forget), its shader takes no more than a pipeline does, its mesh
// is of the shader's vertices, and its instances and uniforms are all there.
static bool run_draws(const tide_draw_list *list, const uint32_t place)
{
    if (place >= list->run_count) return false;
    const tide_draw_run *run = &list->runs[place];
    const tide_shader *shader = run->shader;
    if (!shader || run->mesh == 0 || run->mesh > list->mesh_count || run->count == 0) return false;
    const tide_draw_mesh_data *m = &list->meshes[run->mesh - 1u];
    if (m->space != TIDE_MESH_BYTES || m->index_count == 0 || m->vertex_size != shader->vertex_size) return false;
    if (!shader_fits(shader)) return false;
    const uint64_t instance_fields = (uint64_t)shader->instance_floats + shader->instance_ints;
    const uint64_t uniform_fields = (uint64_t)shader->uniform_floats + shader->uniform_ints;
    return run->first + instance_fields * 16u * run->count <= list->instance_used
           && run->uniforms + uniform_fields * 16u <= list->uniform_used;
}

// A pipeline tide_platform_prewarm asked for, which tide_platform_prewarm_waiting watches.
static void warm_ask(const tide_gpu_id pipeline)
{
    if (!pipeline) return;
    for (uint32_t i = 0; i < warm_asked_count; i++) {
        if (warm_asked[i] == pipeline) return;
    }
    if (warm_asked_count == warm_asked_capacity) {
        const uint32_t capacity = warm_asked_capacity ? warm_asked_capacity * 2u : 64u;
        warm_asked = tide_realloc(warm_asked, warm_asked_capacity * sizeof(tide_gpu_id), capacity * sizeof(tide_gpu_id));
        warm_asked_capacity = capacity;
    }
    warm_asked[warm_asked_count++] = pipeline;
}

// Whether a key is for the shader: by its name, and its kind.
static bool key_for(const used_key *key, const tide_shader *shader, const uint64_t name)
{
    return key->name == name && key->dims == shader->dims && key->blit == shader->blit
           && key->outputs == tide_shader_outputs(shader);
}

enum { KIND_KEYS_MOST = 8 }; // Kinds of target a kind of shader is guessed into, at most

// For each kind of target the shaders of a kind (2D or 3D, a blit, their outputs) were drawn into, the way most of
// them were drawn into it, in `keys`: how many, KIND_KEYS_MOST at most. A shader is compiled for what it draws into
// (the measured drivers compile it again for another format, or for a target of depth alone), so a shader of the
// kind that hasn't drawn yet is made for each: a shadow's frag drawn into a depth map, and a model's into the scene.
static uint32_t kind_keys(const tide_shader *shader, used_key *keys)
{
    const used_key kind = key_of(shader, shader->blit, 0, 0, 0, 0, 0);
    uint32_t count = 0, most[KIND_KEYS_MOST] = {0};
    for (uint32_t i = 0; i < used_key_count; i++) {
        const used_key *a = &used_keys[i];
        if (a->dims != kind.dims || a->blit != kind.blit || a->outputs != kind.outputs) continue;
        uint32_t shaders = 0; // The shaders drawn so: one key each, as a shader's are another's
        for (uint32_t k = 0; k < used_key_count; k++) shaders += keys_alike(a, &used_keys[k], false);
        uint32_t into = 0;
        while (into < count && keys[into].into != a->into) into++;
        if (into == count) {
            if (count == KIND_KEYS_MOST) continue;
            count++;
        }
        if (shaders > most[into]) {
            most[into] = shaders;
            keys[into] = *a;
        }
    }
    return count;
}

// Whether a draw drew with the shader.
static bool drawn(const tide_shader *shader)
{
    for (uint32_t i = 0; i < shader_pipeline_count; i++) {
        const shader_pipeline *p = &shader_pipelines[i];
        if (p->id == shader->id && p->blit == shader->blit && p->used) return true;
    }
    return false;
}

static tide_gpu_id warm_key(const tide_shader *shader, const used_key *key)
{
    return pipeline_for(shader, key->blit, key->blend, key->depth, key->cull, key->into, key->state);
}

void tide_platform_prewarm(const tide_shader *const *shaders, uint32_t count)
{
    if (!shaders) count = 0;
    if (!gpu || gpu == &tide_gpu_record || (!headless && tide_window_unseen()) || !gpu_ready()) return;
    const bool same = warm_generation && shaders == warm_shaders && count == warm_count;
    if (same && warm_epoch == gpu_epoch && warm_guessed == used_changes) return;
    if (!same) {
        warm_generation++;
        if (!warm_generation) warm_generation = 1; // 0 is no table's
        warm_done = tide_realloc(warm_done, warm_count * sizeof(bool), (count + 1u) * sizeof(bool));
        warm_names = tide_realloc(warm_names, warm_count * sizeof(uint64_t), (count + 1u) * sizeof(uint64_t));
        for (uint32_t i = 0; i < count; i++) warm_names[i] = shaders[i] ? name_key(shaders[i]) : 0u;
        warm_shaders = shaders;
        warm_count = count;
    }
    warming = true;
    if (!same || warm_epoch != gpu_epoch) { // All of it, for another table or on another device
        warm_epoch = gpu_epoch;
        warm_asked_count = 0;
        if (count) memset(warm_done, 0, count * sizeof(bool));
        // The renderer's own, as they're drawn by default into the window's kind of target: what a first frame of
        // shapes, text and meshes would make, and what one that draws the first of a kind later would wait for
        const tide_gpu_target_format into = TIDE_GPU_TARGET_COLOR;
        for (int kind = 0; kind < PIPELINE_KINDS; kind++) {
            warm_ask(engine_pipeline(kind, TIDE_BLEND_ALPHA, TIDE_DEPTH_READ_WRITE, TIDE_GPU_CULL_BACK, into, 0));
        }
        warm_ask(clear_pipeline(into));
        warm_ask(depth_clear_pipeline(into));
        // Each of the game's, every way it was drawn before
        for (uint32_t i = 0; i < count; i++) {
            const tide_shader *shader = shaders[i];
            if (!shader || !shader_fits(shader)) continue;
            for (uint32_t k = 0; k < used_key_count; k++) {
                if (!key_for(&used_keys[k], shader, warm_names[i])) continue;
                warm_ask(warm_key(shader, &used_keys[k]));
                warm_done[i] = true;
            }
        }
    }
    // ...and those never drawn, the ways their kind were, once one of their kind was, and again as their kind is
    // drawn into another kind of target
    warm_guessed = used_changes;
    used_key kinds[2][2][TIDE_TARGET_COLORS][KIND_KEYS_MOST]; // By 3D, blit and outputs less one, as kind_keys finds them
    int8_t known[2][2][TIDE_TARGET_COLORS]; // 1 + how many, once looked for
    memset(known, 0, sizeof known);
    for (uint32_t i = 0; i < count; i++) {
        const tide_shader *shader = shaders[i];
        if (warm_done[i] || !shader || !shader_fits(shader)) continue;
        if (drawn(shader)) {
            warm_done[i] = true; // Its draws make what it draws with, and keep it (use_key)
            continue;
        }
        const uint32_t deep = shader->dims == 3, blit = shader->blit, outputs = tide_shader_outputs(shader) - 1u;
        used_key *keys = kinds[deep][blit][outputs];
        if (!known[deep][blit][outputs]) known[deep][blit][outputs] = (int8_t)(1u + kind_keys(shader, keys));
        for (int k = 0; k < known[deep][blit][outputs] - 1; k++) warm_ask(warm_key(shader, &keys[k]));
    }
    warming = false;
}

uint32_t tide_platform_shader_pipelines(void)
{
    return shader_pipeline_count;
}

uint32_t tide_platform_prewarm_waiting(void)
{
    if (!gpu || !gpu->pipeline_ready || warm_epoch != gpu_epoch) return 0;
    uint32_t waiting = 0;
    for (uint32_t i = 0; i < warm_asked_count; i++) waiting += !gpu->pipeline_ready(warm_asked[i]);
    return waiting;
}

// What the list's shaded runs draw from goes to the GPU, before anything
// draws: their meshes, their instances into the instance buffer, which grows
// as they need, and a pipeline for each shader that has none.
static bool upload_shaded(const tide_draw_list *list)
{
    bool any = false;
    for (uint32_t i = 0; i < step_count; i++) {
        if (steps[i].kind != STEP_SHADED && steps[i].kind != STEP_BLIT) continue;
        if (!frame_targets[steps[i].target].valid || leaves_nothing(list, &steps[i])) continue;
        const tide_draw_run *run = &list->runs[steps[i].first];
        any = true;
        mesh_for(list, run->mesh);
        steps[i].pipeline = pipeline_for(run->shader, steps[i].kind == STEP_BLIT, steps[i].blend, steps[i].depth,
                                         steps[i].cull, frame_targets[steps[i].target].into,
                                         stencil_state(steps[i].stencil));
        // A shader that reads a depth it may not be given reads the far one: made here, outside any pass
        for (uint32_t t = 0; t < run->shader->texture_count; t++) {
            const tide_gpu_read read = gpu_read(run->shader, t);
            if (read == TIDE_GPU_READ_CUBE && !gpu_state.dark) { // ...and one that reads a cube, the dark one
                static const uint8_t black[4] = {0, 0, 0, 255};
                static const void *const faces[TIDE_GPU_CUBE_FACES] = {black, black, black, black, black, black};
                gpu_state.dark = made_cube(TIDE_GPU_COLOR_BYTES, 1, faces);
            }
            if (read == TIDE_GPU_READ_DATA && !gpu_state.nothing) { // ...and one that reads a list, no numbers
                gpu_state.nothing = made_texture(TIDE_GPU_RGBA32I, 1, 1, (const int32_t[4]){0, 0, 0, 0});
            }
            if (read == TIDE_GPU_READ_COLOR || read == TIDE_GPU_READ_CUBE || read == TIDE_GPU_READ_DATA || gpu_state.far) continue;
            gpu_state.far = made_target(TIDE_GPU_TARGET_DEPTH, 1, 1);
            gpu->begin(gpu_state.far, true);
            gpu->end();
        }
    }
    if (any && list->instance_used) {
        buffer_room(&gpu_state.instances, &gpu_state.instance_capacity, list->instance_used, 1, 4096u);
        gpu->buffer_write(gpu_state.instances, 0, list->instances, list->instance_used);
    }
    return any;
}

// This list's shapes into the instance buffer, with their extras and radii,
// and its triangles, text's too, and its quads into theirs: all grow as they
// need.
static void upload_shapes_and_meshes(void)
{
    if (shape_count) {
        buffer_room(&gpu_state.shape_instances, &gpu_state.shape_capacity, shape_count, sizeof(shape), 1024u);
        gpu->buffer_write(gpu_state.shape_instances, 0, shapes, (size_t)shape_count * sizeof(shape));
    }
    if (shape_extra_count) {
        buffer_room(&gpu_state.shape_extras, &gpu_state.shape_extra_capacity, shape_extra_count, sizeof(shape_extra),
                    256u);
        gpu->buffer_write(gpu_state.shape_extras, 0, shape_extras, (size_t)shape_extra_count * sizeof(shape_extra));
    }
    if (shape_radius_count) { // A multiple of 4 bytes, as buffers take them
        buffer_room(&gpu_state.shape_radii, &gpu_state.shape_radius_capacity, shape_radius_count, sizeof(float), 1024u);
        gpu->buffer_write(gpu_state.shape_radii, 0, shape_radii, (size_t)shape_radius_count * sizeof(float));
    }
    if (mesh_vertex_count) {
        buffer_room(&gpu_state.mesh_vertices, &gpu_state.mesh_capacity, mesh_vertex_count, sizeof(mesh_vertex), 4096u);
        gpu->buffer_write(gpu_state.mesh_vertices, 0, mesh_vertices, (size_t)mesh_vertex_count * sizeof(mesh_vertex));
    }
    if (quad_count) {
        buffer_room(&gpu_state.quads, &gpu_state.quad_capacity, quad_count, sizeof(quad), 1024u);
        gpu->buffer_write(gpu_state.quads, 0, quads, (size_t)quad_count * sizeof(quad));
    }
}

// The font atlas onto the GPU, as the list's text left it.
static void upload_atlas(void)
{
    int width = 0, height = 0;
    uint32_t version = 0;
    const uint8_t *pixels = tide_font_atlas(&width, &height, &version);
    if (!pixels || (gpu_state.atlas && gpu_state.atlas_version == version)) return;
    int first, rows; // What new glyphs changed: a whole atlas is megabytes, a glyph's rows a few kilobytes
    tide_font_atlas_changed(&first, &rows);
    if (gpu_state.atlas && width == gpu_state.atlas_width && height == gpu_state.atlas_height) {
        if (rows > 0) gpu->texture_write_part(gpu_state.atlas, pixels, 0, first, width, rows);
    } else {
        if (gpu_state.atlas) free_texture(gpu_state.atlas);
        gpu_state.atlas = made_texture(TIDE_GPU_R8, width, height, pixels);
    }
    gpu_state.atlas_width = width;
    gpu_state.atlas_height = height;
    gpu_state.atlas_version = version;
}

static uint32_t gpu_texture_hash(const uint32_t space, const uint64_t id)
{
    uint64_t h = id * UINT64_C(0x9E3779B97F4A7C15) ^ space;
    h ^= h >> 32;
    h *= UINT64_C(0xD6E8FEB86659FD93);
    h ^= h >> 32;
    return (uint32_t)h;
}

static void index_gpu_texture(const uint32_t place)
{
    const gpu_texture *g = &gpu_textures[place];
    const uint32_t mask = gpu_texture_index_capacity - 1u;
    uint32_t i = gpu_texture_hash(g->space, g->id) & mask;
    while (gpu_texture_index[i]) i = (i + 1u) & mask;
    gpu_texture_index[i] = place + 1u;
}

static void reindex_gpu_textures(void)
{
    uint32_t capacity = gpu_texture_index_capacity ? gpu_texture_index_capacity : 64u;
    while (capacity / 2u <= gpu_texture_count) {
        if (capacity > UINT32_MAX / 2u / sizeof(uint32_t)) tide_out_of_memory();
        capacity *= 2u;
    }
    if (capacity != gpu_texture_index_capacity) {
        free(gpu_texture_index);
        gpu_texture_index = tide_alloc_zeroed(capacity, sizeof(uint32_t));
        gpu_texture_index_capacity = capacity;
    } else {
        memset(gpu_texture_index, 0, (size_t)capacity * sizeof(uint32_t));
    }
    for (uint32_t i = 0; i < gpu_texture_count; i++) index_gpu_texture(i);
}

static gpu_texture *find_gpu_texture(const uint32_t space, const uint64_t id)
{
    if (gpu_texture_index_capacity) {
        const uint32_t mask = gpu_texture_index_capacity - 1u;
        for (uint32_t i = gpu_texture_hash(space, id) & mask; gpu_texture_index[i]; i = (i + 1u) & mask) {
            gpu_texture *found = &gpu_textures[gpu_texture_index[i] - 1u];
            if (found->id == id && found->space == space) return found;
        }
    }
    return NULL;
}

static uint32_t data_shape_hash(const int32_t width, const int32_t height)
{
    return gpu_texture_hash((uint32_t)width, (uint32_t)height);
}

// Determine liveness before detaching anything: a later draw can still name a
// previous frame's unchanged palette. Merely protecting it does not count it
// as drawn; skipped destinations and invalid reads still prune.
static void prepare_data_textures(const tide_draw_list *list)
{
    for (uint32_t i = 0; i < gpu_texture_count; i++) gpu_textures[i].protected = false;
    for (uint32_t i = 0; i < step_count; i++) {
        const draw_step *s = &steps[i];
        if (s->kind != STEP_SHADED && s->kind != STEP_BLIT) continue;
        const tide_draw_run *run = &list->runs[s->first];
        for (uint32_t k = 0; k < run->shader->texture_count; k++) {
            const uint32_t slot = run->textures[k];
            if (gpu_read(run->shader, k) != TIDE_GPU_READ_DATA || !slot || slot > list->texture_count) continue;
            const tide_draw_texture *t = &list->textures[slot - 1u];
            gpu_texture *g = find_gpu_texture(t->space, t->id);
            if (g) g->protected = true;
        }
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < gpu_texture_count; i++) {
        const gpu_texture *g = &gpu_textures[i];
        if (g->texture && !g->protected && g->frame != gpu_frame && g->space == TIDE_PIXELS_LIST
            && g->format == TIDE_TEXELS_INTS && (g->id >> 63)) count++;
    }
    spare_data_count = 0;
    if (!count) return;
    if (count > spare_data_capacity) {
        if ((uint64_t)count * sizeof *spare_data_textures > SIZE_MAX) tide_out_of_memory();
        spare_data_textures = tide_realloc(spare_data_textures, (size_t)spare_data_capacity * sizeof *spare_data_textures,
                                           (size_t)count * sizeof *spare_data_textures);
        spare_data_capacity = count;
    }
    uint32_t slots = spare_data_slots ? spare_data_slots : 16u;
    while (slots / 2u < count) {
        if (slots > UINT32_MAX / 2u) tide_out_of_memory();
        slots *= 2u;
    }
    if ((uint64_t)slots * sizeof(uint32_t) > SIZE_MAX) tide_out_of_memory();
    if (slots != spare_data_slots) {
        spare_data_index = tide_realloc(spare_data_index, (size_t)spare_data_slots * sizeof(uint32_t),
                                        (size_t)slots * sizeof(uint32_t));
        spare_data_slots = slots;
    }
    memset(spare_data_index, 0, (size_t)slots * sizeof(uint32_t));
    for (uint32_t i = 0; i < gpu_texture_count; i++) {
        gpu_texture *g = &gpu_textures[i];
        if (!g->texture || g->protected || g->frame == gpu_frame || g->space != TIDE_PIXELS_LIST
            || g->format != TIDE_TEXELS_INTS || !(g->id >> 63)) continue;
        const uint32_t bucket = data_shape_hash(g->width, g->height) & (slots - 1u);
        spare_data_textures[spare_data_count] = (spare_data_texture){g->texture, g->width, g->height, spare_data_index[bucket]};
        spare_data_index[bucket] = ++spare_data_count;
        g->texture = 0; // The old logical identity no longer owns this object.
    }
}

static tide_gpu_id take_data_texture(const tide_draw_texture *t)
{
    if (!spare_data_count || t->space != TIDE_PIXELS_LIST || t->format != TIDE_TEXELS_INTS || !(t->id >> 63)) return 0;
    uint32_t *place = &spare_data_index[data_shape_hash(t->width, t->height) & (spare_data_slots - 1u)];
    while (*place) {
        spare_data_texture *s = &spare_data_textures[*place - 1u];
        if (s->width == t->width && s->height == t->height) {
            const tide_gpu_id texture = s->texture;
            s->texture = 0;
            *place = s->next;
            return texture;
        }
        place = &s->next;
    }
    return 0;
}

static void free_spare_data_textures(void)
{
    for (uint32_t i = 0; i < spare_data_count; i++) {
        if (spare_data_textures[i].texture) free_texture(spare_data_textures[i].texture);
    }
    spare_data_count = 0;
}

// The GPU's copy of a list's texture, uploaded if it hasn't got these pixels.
static gpu_texture *texture_for(const tide_draw_texture *t)
{
    gpu_texture *g = find_gpu_texture(t->space, t->id);
    if (!g) {
        if (gpu_texture_count == gpu_texture_capacity) {
            const uint32_t capacity = gpu_texture_capacity ? gpu_texture_capacity * 2u : 8u;
            gpu_textures = tide_realloc(gpu_textures, gpu_texture_capacity * sizeof(gpu_texture),
                                        capacity * sizeof(gpu_texture));
            gpu_texture_capacity = capacity;
        }
        g = &gpu_textures[gpu_texture_count++];
        *g = (gpu_texture){.id = t->id, .space = t->space};
        if (gpu_texture_count >= gpu_texture_index_capacity / 2u) reindex_gpu_textures();
        else index_gpu_texture(gpu_texture_count - 1u);
    }
    g->frame = gpu_frame;
    const bool same_shape = g->texture && g->width == t->width && g->height == t->height && g->format == t->format;
    if (same_shape && g->version == t->version && g->epoch == t->epoch) return g;
    const bool shaped = same_shape && !g->compressed && !t->compressed.pixels;
    const tide_draw_area *areas;
    uint32_t count = 1;
    bool resident_mips = false;
    if (t->space == TIDE_PIXELS_PACK_CUBE) { // A cube of the pack's: its six faces' pixels, where the pack has them
        if (g->texture) free_texture(g->texture);
        g->texture = made_cube(TIDE_GPU_COLOR_BYTES, t->width, (const void *const *)(uintptr_t)t->pixels);
    } else if (shaped && g->epoch == t->epoch && tide_draw_texture_since(t, g->version, &areas, &count)) {
        // The list says all that changed since the pixels the GPU has: those go, and nothing else
        for (uint32_t i = 0; i < count; i++) {
            gpu->texture_write_part(g->texture, t->pixels, areas[i].x, areas[i].y, areas[i].width, areas[i].height);
            gpu_written += (uint64_t)areas[i].width * (uint64_t)areas[i].height * tide_texels_bytes(t->format);
        }
    } else if (shaped) {
        gpu->texture_write(g->texture, t->pixels);
        gpu_written += (uint64_t)t->width * (uint64_t)t->height * tide_texels_bytes(t->format);
    } else {
        if (g->texture) free_texture(g->texture);
        g->texture = t->compressed.pixels && gpu->texture_blocks
            ? gpu->texture_blocks(t->width, t->height, t->compressed) : 0;
        if (g->texture) {
            hold(held(&held_textures, &held_texture_count, g->texture), t->compressed.bytes);
            gpu_written += t->compressed.bytes;
            resident_mips = true;
        } else g->texture = take_data_texture(t);
        if (g->texture && !resident_mips) gpu->texture_write(g->texture, t->pixels);
        else if (!g->texture) {
            // A grid of floats' cells are one float of 16 bits a pixel
            g->texture = made_texture(t->format == TIDE_TEXELS_HALF ? TIDE_GPU_R16F :
                                      t->format == TIDE_TEXELS_INTS ? TIDE_GPU_RGBA32I : TIDE_GPU_RGBA8, t->width, t->height, t->pixels);
        }
        if (!resident_mips) gpu_written += (uint64_t)t->width * (uint64_t)t->height * tide_texels_bytes(t->format);
    }
    if (count) g->mipped = resident_mips; // Uploaded blocks already contain every mip.
    if (count) g->compressed = resident_mips;
    g->format = t->format;
    g->version = t->version;
    g->width = t->width;
    g->height = t->height;
    g->epoch = t->epoch;
    return g;
}

// Retain only immutable content identities, never C addresses, grid handles,
// targets or scratch data. The cache owns GPU ids, not borrowed pack pointers.
static void forget_textures(const uint32_t epoch)
{
    if (gpu_texture_count > idle_texture_capacity) {
        if ((uint64_t)gpu_texture_count * sizeof *idle_textures > SIZE_MAX) tide_out_of_memory();
        idle_textures = tide_realloc(idle_textures, (size_t)idle_texture_capacity * sizeof *idle_textures,
                                     (size_t)gpu_texture_count * sizeof *idle_textures);
        idle_texture_capacity = gpu_texture_count;
    }
    uint32_t idle_count = 0;
    for (uint32_t i = 0; i < gpu_texture_count; i++) {
        gpu_texture *g = &gpu_textures[i];
        if (g->frame == gpu_frame || !g->texture) continue;
        if (g->epoch != epoch || (g->space != TIDE_PIXELS_PACK && g->space != TIDE_PIXELS_PACK_CUBE)) {
            free_texture(g->texture);
            g->texture = 0;
            continue;
        }
        idle_textures[idle_count++] = (tide_texture_idle){
            .bytes = g->texture < held_texture_count ? held_textures[g->texture].bytes : 0,
            .age = gpu_frame - g->frame, .place = i,
        };
    }
    tide_texture_idle_trim(idle_textures, idle_count, TIDE_TEXTURE_IDLE_BYTES, TIDE_TEXTURE_IDLE_COUNT, tide_texture_idle_frames);
    for (uint32_t i = 0; i < idle_count; i++) {
        if (idle_textures[i].keep) continue;
        gpu_texture *g = &gpu_textures[idle_textures[i].place];
        free_texture(g->texture);
        g->texture = 0;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < gpu_texture_count; i++) {
        if (gpu_textures[i].frame != gpu_frame && !gpu_textures[i].texture) continue;
        gpu_textures[kept++] = gpu_textures[i];
    }
    if (kept != gpu_texture_count) {
        gpu_texture_count = kept;
        reindex_gpu_textures();
    }
    gpu_frame++;
}

// Whether a list's texture is a render target's image: its color, or its depth.
static bool of_target(const tide_draw_texture *t)
{
    return t->space == TIDE_PIXELS_TARGET || t->space == TIDE_PIXELS_TARGET_DEPTH;
}

// ...or a cube: one that's drawn into, or the pack's.
static bool of_cube(const tide_draw_texture *t)
{
    return t->space == TIDE_PIXELS_TARGET_CUBE || t->space == TIDE_PIXELS_PACK_CUBE;
}

// Whether a list's texture is of the destination `id`: one of a target's own images, or the cube a face is of.
static bool of_destination(const tide_draw_texture *t, const uint64_t id)
{
    if (t->space == TIDE_PIXELS_TARGET_CUBE) return tide_render_target_cubic(id) && t->id == tide_render_target_cube_of(id);
    return of_target(t) && tide_render_target_whole(t->id) == id;
}

// The GPU's copy of the list's texture `texture` (1 + its place), for a
// shader that reads it as `read` says (tide_gpu_read). For none, and for one
// that isn't what the shader reads there (a color where it reads a depth, or
// the other way round, as a list filled by hand may say), it's the white
// pixel where a color is read, where a depth is, the far one, and where a
// cube is, the dark one.
static tide_gpu_id texture_to_read(const tide_draw_list *list, const uint32_t texture, const tide_gpu_read read)
{
    const bool depth = read == TIDE_GPU_READ_DEPTH || read == TIDE_GPU_READ_COMPARE, cube = read == TIDE_GPU_READ_CUBE;
    const bool data = read == TIDE_GPU_READ_DATA; // A list's numbers, and nothing else is read so
    const tide_gpu_id none = data ? gpu_state.nothing : cube ? gpu_state.dark : depth ? gpu->target_depth(gpu_state.far)
                           : gpu_state.white;
    if (texture == 0 || texture > list->texture_count) return none;
    const tide_draw_texture *t = &list->textures[texture - 1u];
    if (data != (t->space == TIDE_PIXELS_LIST && t->format == TIDE_TEXELS_INTS)) return none;
    if (cube != of_cube(t)) return none;
    if (t->space == TIDE_PIXELS_TARGET_CUBE) {
        const gpu_cube *c = find_cube(t->id);
        return c && c->texture ? c->texture : none;
    }
    if (depth != (t->space == TIDE_PIXELS_TARGET_DEPTH)) return none;
    if (of_target(t)) { // Its depth, or the color image its id names
        const gpu_target *g = find_target(tide_render_target_whole(t->id));
        const tide_gpu_id image = !g || !g->target ? 0 : depth ? gpu->target_depth(g->target)
                                : gpu->target_texture(g->target, tide_render_target_image(t->id));
        return image ? image : none;
    }
    const gpu_texture *g = texture_for(t);
    return g->texture ? g->texture : none;
}

// Reading an attachment while writing it is undefined on every GPU, a
// target's depth as much as any of its color images, whichever of them the
// draw writes. Refuse the whole draw, including an ordinary mesh or an
// indirect binding.
static bool samples_destination(const tide_draw_list *list, const draw_step *s)
{
    if (!s->target) return false;
    const uint64_t id = frame_targets[s->target].id;
    if (s->kind == STEP_SHADED || s->kind == STEP_BLIT) {
        const tide_draw_run *run = &list->runs[s->first];
        for (uint32_t i = 0; i < run->shader->texture_count; i++) {
            const uint32_t at = run->textures[i];
            if (at && at <= list->texture_count) {
                const tide_draw_texture *t = &list->textures[at - 1u];
                if (of_destination(t, id)) return true;
            }
        }
    } else if (s->texture && s->texture <= list->texture_count) {
        const tide_draw_texture *t = &list->textures[s->texture - 1u];
        if (of_destination(t, id)) return true;
    }
    return false;
}

// Why a list's texture can't be sampled, or NULL: a target that was disposed
// or has no image, the color of one that holds depth alone, and a color image
// past the ones a target holds.
static const char *unavailable_texture(const tide_draw_list *list, const uint32_t texture)
{
    if (!texture || texture > list->texture_count) return NULL;
    const tide_draw_texture *t = &list->textures[texture - 1u];
    if (t->space == TIDE_PIXELS_TARGET_CUBE) {
        return !tide_render_target_live(t->id) || !tide_target_size_valid(t->target) ? "source cube is invalid or disposed" : NULL;
    }
    if (!of_target(t)) return NULL;
    if (!tide_render_target_live(t->id) || !tide_target_size_valid(t->target)) return "source render target is invalid or disposed";
    if (t->space == TIDE_PIXELS_TARGET_DEPTH) return NULL;
    const uint32_t colors = tide_render_target_colors(t->id);
    if (colors == 0) return "source render target holds depth alone: it has no color to sample";
    if (tide_render_target_image(t->id) >= colors) {
        return "source render target has fewer color images than the one sampled: its 'colors' are counted from 0";
    }
    return NULL;
}

static const char *samples_unavailable(const tide_draw_list *list, const draw_step *s)
{
    if (s->kind == STEP_SHADED || s->kind == STEP_BLIT) {
        const tide_draw_run *run = &list->runs[s->first];
        for (uint32_t i = 0; i < run->shader->texture_count; i++) {
            const char *why = unavailable_texture(list, run->textures[i]);
            if (why) return why;
        }
        return NULL;
    }
    return unavailable_texture(list, s->texture);
}

// A source recorded after Resize first sees the replacement black image,
// even when no command draws into it. An older source still aliases the
// current attachment; it never rolls a target back to an earlier revision.
// A source at the current revision, whose image now comes to another number
// of pixels (the display's scale changed, or the window's size under a target
// that follows it), sees the image made again, black, as a destination would.
static bool prepare_source_target(const tide_draw_list *list, const uint32_t texture, const bool check)
{
    if (!texture || texture > list->texture_count) return false;
    const tide_draw_texture *t = &list->textures[texture - 1u];
    if (t->space == TIDE_PIXELS_TARGET_CUBE) { // A cube nothing drew into yet, or one that was resized, is black
        const gpu_cube *c = find_cube(t->id);
        if (c && t->version < c->version) return false;
        float width, height;
        int wide, high;
        bool limited;
        if (!target_extent(t->target, &root, &width, &height, &wide, &high, &limited)) return false;
        if (c && c->texture && t->version == c->version && c->size == wide) return false;
        if (!check) cube_for(t->id, t->target, t->version);
        return true;
    }
    if (!of_target(t)) return false;
    const uint64_t id = tide_render_target_whole(t->id); // The target, whichever of its images is sampled
    gpu_target *g = find_target(id);
    // A multisampled target's depth is read as a texture of one depth a pixel, made of its samples outside a
    // pass, and again once more passes have drawn into it
    const bool depth = t->space == TIDE_PIXELS_TARGET_DEPTH && tide_render_target_samples(id) > 1u;
    bool made = !g || (tide_render_target_managed(id) && t->version >= g->version);
    float width = 0, height = 0;
    int wide = 0, high = 0;
    bool limited = false;
    if (made && !target_extent(t->target, &root, &width, &height, &wide, &high, &limited)) made = false;
    if (made && g && g->target && t->version == g->version && g->width == wide && g->height == high) made = false;
    if (made && !check) g = target_for(id, t->target, t->version);
    const bool stale = depth && (made || (g && g->target && g->resolved != g->drawn + 1u));
    if (stale && !check && g && g->target) {
        gpu->resolve_depth(g->target);
        g->resolved = g->drawn + 1u;
        gpu_held *h = held(&held_targets, &held_target_count, g->target);
        if (!h->resolved && gpu->target_depth(g->target)) { // Where the device gave it one sample, its depth is its own
            h->resolved = true;
            h->bytes += (uint64_t)g->width * (uint64_t)g->height * 4u;
            gpu_memory += (uint64_t)g->width * (uint64_t)g->height * 4u;
        }
    }
    return made || stale;
}

static bool prepare_source_targets(const tide_draw_list *list, const draw_step *s, const bool check)
{
    bool needed = false;
    if (s->kind == STEP_SHADED || s->kind == STEP_BLIT) {
        const tide_draw_run *run = &list->runs[s->first];
        for (uint32_t i = 0; i < run->shader->texture_count; i++) {
            needed |= prepare_source_target(list, run->textures[i], check);
        }
    } else {
        needed = prepare_source_target(list, s->texture, check);
    }
    return needed;
}

// A texture that's read between its smaller images (Filter.Trilinear) has
// them made of its pixels as they are, outside a pass: an uploaded texture's
// once for each upload, and a target's color image's once more passes have
// drawn into the target. So one that's never read so has none, and one that
// doesn't change makes them once. Whether any was to be made; with `check`,
// nothing is.
static bool prepare_mipmaps(const tide_draw_list *list, const uint32_t texture, const tide_filter filter, const bool check)
{
    if (filter != TIDE_FILTER_TRILINEAR || !texture || texture > list->texture_count) return false;
    const tide_draw_texture *t = &list->textures[texture - 1u];
    if (t->space == TIDE_PIXELS_TARGET_DEPTH) return false;
    if (t->space == TIDE_PIXELS_TARGET_CUBE) { // Once more passes have drawn into its faces
        gpu_cube *c = find_cube(t->id);
        if (!c || !c->texture || c->mipped == c->drawn) return false;
        if (!check) {
            made_mipmaps(c->texture);
            c->mipped = c->drawn;
        }
        return true;
    }
    if (of_target(t)) {
        gpu_target *g = find_target(tide_render_target_whole(t->id));
        const uint32_t image = tide_render_target_image(t->id);
        const tide_gpu_id color = g && g->target && image < TIDE_GPU_MAX_COLORS ? gpu->target_texture(g->target, image) : 0;
        if (!color || g->mipped[image] == g->drawn) return false;
        if (!check) {
            made_target_mipmaps(g->target, image, color, gpu_target_format(g->id), g->width, g->height);
            g->mipped[image] = g->drawn;
        }
        return true;
    }
    gpu_texture *g = texture_for(t);
    if (!g->texture || g->mipped) return false;
    if (!check) {
        made_mipmaps(g->texture);
        g->mipped = true;
    }
    return true;
}

static bool prepare_step_mipmaps(const tide_draw_list *list, const draw_step *s, const bool check)
{
    bool needed = false;
    if (s->kind == STEP_SHADED || s->kind == STEP_BLIT) {
        const tide_draw_run *run = &list->runs[s->first];
        for (uint32_t i = 0; i < run->shader->texture_count; i++) {
            if (gpu_read(run->shader, i) != TIDE_GPU_READ_COLOR && gpu_read(run->shader, i) != TIDE_GPU_READ_CUBE) continue;
            needed |= prepare_mipmaps(list, run->textures[i], tide_sampling_filter(run->sampling, i), check);
        }
    } else if (s->kind == STEP_MESH || s->kind == STEP_MESH_3D) {
        needed = prepare_mipmaps(list, s->texture, tide_filter_of((int32_t)s->filter), check);
    }
    return needed;
}

// A list's filter as the GPU's, which has tide_filter's numbers: what isn't one is blended.
static tide_gpu_filter gpu_filter(const tide_filter filter)
{
    _Static_assert(TIDE_GPU_FILTER_NEAREST == (int)TIDE_FILTER_POINT && TIDE_GPU_FILTER_LINEAR == (int)TIDE_FILTER_BILINEAR
                   && TIDE_GPU_FILTER_MIPMAPS == (int)TIDE_FILTER_TRILINEAR, "gpu.h's filters are draw.h's");
    return (tide_gpu_filter)tide_filter_of((int32_t)filter);
}

// A list's wrap as the GPU's, which has tide_wrap's numbers: what isn't one clamps.
static tide_gpu_wrap gpu_wrap(const uint32_t wrap)
{
    _Static_assert(TIDE_GPU_WRAP_REPEAT == (int)TIDE_WRAP_REPEAT && TIDE_GPU_WRAP_MIRROR == (int)TIDE_WRAP_MIRROR,
                   "gpu.h's wraps are draw.h's");
    return wrap == TIDE_WRAP_REPEAT || wrap == TIDE_WRAP_MIRROR ? (tide_gpu_wrap)wrap : TIDE_GPU_WRAP_CLAMP;
}

// The texture a mesh step samples, sampled as the step says.
static void sample_step_texture(const tide_draw_list *list, const draw_step *s)
{
    gpu->sample(0, texture_to_read(list, s->texture, TIDE_GPU_READ_COLOR), gpu_filter((tide_filter)s->filter), gpu_wrap(s->wrap));
}

// A step's 3D camera, from the world to clip space, fitted to the screen's
// shape: x divided by its width over its height.
// Inside another program, a second picture of the frame's 3D through a camera of its own, as its editor's scene
// view has (tide_platform_embed_render_view): only the window's 3D steps draw, each through this, from the world to
// clip space for the target as it's shaped.
static bool view_only_3d;
static tide_float4x4 view_world_to_clip;

// Whether a step is one of the window's 3D, which such a picture draws.
static bool window_3d_step(const tide_draw_list *list, const draw_step *s)
{
    if (draw_target) return false;
    if (s->kind == STEP_MESH_3D) return true;
    return s->kind == STEP_SHADED && list->runs[s->first].shader && list->runs[s->first].shader->dims == 3;
}

static tide_float4x4 camera_3d_to_clip(const tide_draw_list *list, const draw_step *s)
{
    if (view_only_3d) return view_world_to_clip;
    const tide_float4x4 world_to_clip = s->camera ? list->matrices[s->camera - 1u]
                                            : tide_draw_camera_3d_matrix(tide_f3(0.0f, 0.0f, 0.0f), tide_identity_q(), 60.0f);
    return !s->camera || s->fit ? tide_draw_camera_fit(world_to_clip, draw_width, draw_height) : world_to_clip;
}

// A camera as a matrix, from the units its commands are in to clip space:
// where to_screen puts a point, on the target.
static tide_float4x4 camera_to_clip(const camera *cam)
{
    const float x = 2.0f * cam->scale / draw_width, y = 2.0f * cam->scale / draw_height;
    tide_float4x4 m = tide_identity_f4x4();
    m.c0.x = x;
    if (cam->gui) { // From the top left, y down
        m.c1.y = -y;
        m.c3.x = -1.0f, m.c3.y = 1.0f;
    } else {
        m.c1.y = y;
        m.c3.x = -cam->center.x * x, m.c3.y = -cam->center.y * y;
    }
    return m;
}

// A run of instances of a mesh through a shader of the game's, at once.
static uint64_t draw_shaded(const tide_draw_list *list, const draw_step *s)
{
    static union {
        shaded_uniforms engine; // Then the run's own
        uint8_t bytes[TIDE_GPU_MAX_UNIFORMS];
    } block;
    const tide_draw_run *run = &list->runs[s->first];
    const tide_shader *shader = run->shader;
    const gpu_mesh *g = &gpu_meshes[run->mesh - 1u];
    const tide_float2 scale = pixel_scale();
    const uint32_t own = tide_shader_uniform_size(shader);
    block.engine.camera = s->kind == STEP_BLIT ? tide_identity_f4x4() :
                          shader->dims == 3 ? camera_3d_to_clip(list, s) : camera_to_clip(&s->through);
    if (run->moved && shader->dims != 3 && s->kind != STEP_BLIT) { // Its mesh goes through its transform first
        tide_float4x4 moved = tide_identity_f4x4();
        moved.c0.x = run->transform.x.x, moved.c0.y = run->transform.x.y;
        moved.c1.x = run->transform.y.x, moved.c1.y = run->transform.y.y;
        moved.c3.x = run->transform.offset.x, moved.c3.y = run->transform.offset.y;
        block.engine.camera = tide_mul_f4x4(block.engine.camera, moved);
    }
    block.engine.screen[0] = scale.x, block.engine.screen[1] = scale.y;
    block.engine.screen[2] = (float)draw_pixel_height;
    block.engine.screen[3] = s->blend == TIDE_BLEND_MULTIPLY ? 1.0f : 0.0f;
    block.engine.tint[0] = run->tint.r, block.engine.tint[1] = run->tint.g;
    block.engine.tint[2] = run->tint.b, block.engine.tint[3] = run->tint.a;
    if (own) memcpy(block.bytes + sizeof(shaded_uniforms), list->uniforms + run->uniforms, own);
    use_pipeline(s);
    gpu->uniforms(&block, sizeof(shaded_uniforms) + own);
    for (uint32_t i = 0; i < shader->texture_count; i++) {
        gpu->sample(i, texture_to_read(list, run->textures[i], gpu_read(shader, i)),
                    gpu_filter(tide_sampling_filter(run->sampling, i)), gpu_wrap(tide_sampling_wrap(run->sampling, i)));
    }
    gpu->vertices(0, corners_of(g), 0);
    if (tide_shader_instance_size(shader)) gpu->vertices(1, gpu_state.instances, run->first);
    gpu->indices(g->indices, g->wide);
    gpu->draw_indexed(g->index_count, run->count);
    return (uint64_t)g->index_count * run->count;
}

static uint64_t draw_mesh_3d(const tide_draw_list *list, const draw_step *s)
{
    const gpu_mesh *g = &gpu_meshes[s->mesh - 1u];
    const mesh_3d_uniforms uniforms = {.camera = camera_3d_to_clip(list, s),
                                       .tint = {s->tint.r, s->tint.g, s->tint.b, s->tint.a},
                                       .fix = {s->blend == TIDE_BLEND_MULTIPLY ? 1.0f : 0.0f}};
    use_pipeline(s);
    gpu->uniforms(&uniforms, sizeof uniforms);
    sample_step_texture(list, s);
    gpu->vertices(0, corners_of(g), 0);
    gpu->vertices(1, gpu_state.matrices, (size_t)s->first * sizeof(tide_float4x4));
    gpu->indices(g->indices, g->wide);
    gpu->draw_indexed(g->index_count, s->count);
    return (uint64_t)g->index_count * s->count;
}

// A run of a mesh's or of text's triangles, its pipeline used: as its quads, one instance each (see quad), or as
// its vertices. Returns the vertices drawn, six a quad.
static uint64_t draw_triangles(const draw_step *s)
{
    if (s->quads) {
        gpu->vertices(0, gpu_state.quads, (size_t)s->first * sizeof(quad));
        gpu->draw(0, 6, s->count);
        return UINT64_C(6) * s->count;
    }
    gpu->vertices(0, gpu_state.mesh_vertices, 0);
    gpu->draw(s->first, s->count, 1);
    return s->count;
}

// The pixel nearest `v`, from 0 to `most`: a rect's corner is any float a
// game says, so it's kept to the target before it's made an int (NaN is 0).
static int clamp_pixel(const float v, const int most)
{
    const float nearest = floorf(v + 0.5f);
    return !(nearest > 0.0f) ? 0 : nearest < (float)most ? (int)nearest : most;
}

// Whether a step draws: a pass begins for one that does, and only then.
static bool step_draws(const draw_step *s)
{
    switch (s->kind) {
    case STEP_TARGET:
    case STEP_STENCIL:
    case STEP_CLIP:
    case STEP_NO_CLIP:
        return false;
    case STEP_MESH:
        return s->count != 0;
    case STEP_TEXT:
        return s->count != 0 && gpu_state.atlas;
    default:
        return true;
    }
}

// Only what's inside the step's rect draws from here on, or everything again.
static void clip(const draw_step *s)
{
    const int width = draw_pixel_width, height = draw_pixel_height;
    if (s->kind == STEP_NO_CLIP) {
        gpu->scissor(0, 0, width, height);
        return;
    }
    // In the target's pixels, and inside it
    const tide_float2 scale = pixel_scale();
    const int left = clamp_pixel(s->at.x * scale.x, width);
    const int right = clamp_pixel(s->to.x * scale.x, width);
    const int top = clamp_pixel(s->at.y * scale.y, height);
    const int bottom = clamp_pixel(s->to.y * scale.y, height);
    gpu->scissor(left, top, right > left ? right - left : 0, bottom > top ? bottom - top : 0);
}

// One of the list's 2D cameras (1 + its place, or 0 for a frame's first), on the selected target.
static camera camera_named(const tide_draw_list *list, const uint32_t at)
{
    camera cam = {{0.0f, 0.0f}, 1.0f, false};
    if (!at || at > list->camera_count) return cam;
    const tide_draw_camera_data *c = &list->cameras[at - 1u];
    if (c->screen) {
        cam.gui = true;
    } else {
        cam.center = c->center;
        cam.scale = c->size > 0.0f ? (float)draw_height / (2.0f * c->size) : 1.0f;
    }
    return cam;
}

// One of the list's clips on the selected target, from `at` to `to` in its
// logical pixels: its rect through the camera it was made under, and of that,
// what's inside the clip it was made in.
static void clip_corners(const tide_draw_list *list, uint32_t clip, tide_float2 *at, tide_float2 *to)
{
    *at = *to = tide_f2(0.0f, 0.0f);
    bool first = true;
    for (uint32_t depth = 0; clip && clip <= list->clip_count && depth < list->clip_count; depth++) {
        const tide_draw_clip_data *c = &list->clips[clip - 1u];
        const camera cam = camera_named(list, c->camera);
        // Its corners on the screen, whichever way up the units are
        const tide_float2 p = to_screen(&cam, tide_f2(c->rect.x, c->rect.y));
        const tide_float2 q = to_screen(&cam, tide_f2(c->rect.x + c->rect.width, c->rect.y + c->rect.height));
        const tide_float2 low = tide_f2(p.x < q.x ? p.x : q.x, p.y < q.y ? p.y : q.y);
        const tide_float2 high = c->rect.width > 0.0f && c->rect.height > 0.0f
                                     ? tide_f2(p.x < q.x ? q.x : p.x, p.y < q.y ? q.y : p.y) : low;
        if (first) {
            *at = low, *to = high;
        } else {
            *at = tide_f2(at->x > low.x ? at->x : low.x, at->y > low.y ? at->y : low.y);
            *to = tide_f2(to->x < high.x ? to->x : high.x, to->y < high.y ? to->y : high.y);
            if (to->x < at->x) to->x = at->x;
            if (to->y < at->y) to->y = at->y;
        }
        first = false;
        clip = c->parent;
    }
}

static void select_target(const uint32_t at)
{
    const frame_target *f = &frame_targets[at];
    draw_target = at;
    draw_stencil = f->stencil;
    draw_width = f->width;
    draw_height = f->height;
    draw_pixel_width = f->pixel_width;
    draw_pixel_height = f->pixel_height;
    scaled = (float)draw_pixel_width != draw_width || (float)draw_pixel_height != draw_height;
}

static screen_uniforms target_uniforms(void)
{
    const tide_float2 scale = pixel_scale();
    screen_uniforms u = {.screen = {[0] = 2.0f / draw_width, [5] = -2.0f / draw_height,
                                    [10] = 1.0f, [12] = -1.0f, [13] = 1.0f, [15] = 1.0f},
                         .scale = {scale.x, scale.y}};
    if (gpu_state.atlas) {
        u.texel[0] = 1.0f / (float)gpu_state.atlas_width;
        u.texel[1] = 1.0f / (float)gpu_state.atlas_height;
    }
    return u;
}

// The uniforms of a step that draws shapes, a mesh or text: the target's, how the step blends, and whether it
// stores stencil.
static const screen_uniforms *step_uniforms(screen_uniforms *of_target, const draw_step *s)
{
    of_target->fix[0] = s->blend == TIDE_BLEND_MULTIPLY ? 1.0f : 0.0f;
    of_target->fix[1] = stencil_stores(s->stencil) ? 1.0f : 0.0f;
    return of_target;
}

// Draws the list into `to`: the window, a headless one's frame, or
// tide_platform_read_pixels' own pixels. With `over`, it goes over what's
// there (the overlay), and leaves the frame's camera, textures and meshes as
// they are.
static void draw_list(const tide_draw_list *list, const draw_root to, const bool over, const bool timed)
{
    if (!gpu_ready()) { // Between a device that was lost and the next
        if (!over) {
            draw_calls = 0;
            draw_vertices = 0;
        }
        return;
    }
    tide_draw_prepare(list, tide_platform_jobs());
    const tide_gpu_id target = to.gpu;
    root = to;
    if (!over && target_epoch != list->epoch) {
        for (uint32_t i = 0; i < gpu_target_count; i++) {
            if (gpu_targets[i].target) free_target(gpu_targets[i].target);
        }
        for (uint32_t i = 0; i < gpu_cube_count; i++) {
            if (gpu_cubes[i].texture) free_texture(gpu_cubes[i].texture);
        }
        gpu_target_count = 0;
        gpu_cube_count = 0;
        target_epoch = list->epoch;
    }
    if (!over) forget_disposed_targets();
    const uint32_t targets = list->target_count + 1u;
    if (targets + 1u > frame_target_capacity) {
        frame_targets = tide_realloc(frame_targets, frame_target_capacity * sizeof(frame_target),
                                     (targets + 1u) * sizeof(frame_target));
        frame_target_capacity = targets + 1u;
    }
    // Invalid destination
    frame_targets[targets] = (frame_target){.id = TIDE_RENDER_INVALID, .width = 1.0f, .height = 1.0f,
                                            .pixel_width = 1, .pixel_height = 1};
    for (uint32_t i = 0; i < targets; i++) {
        frame_target *f = &frame_targets[i];
        *f = (frame_target){.through = {.scale = 1.0f}, .world = {.scale = 1.0f}, .fit = 1,
                            .clipping = {.kind = STEP_NO_CLIP}};
        if (i) {
            const tide_draw_target_data *t = &list->targets[i - 1u];
            bool limited;
            f->id = t->id;
            f->version = t->version;
            f->size = t->size;
            // In logical pixels, like the window: what's drawn into it lands as it would there
            f->valid = target_extent(t->size, &root, &f->width, &f->height, &f->pixel_width, &f->pixel_height, &limited)
                    && tide_render_target_live(t->id);
            f->into = gpu_target_format(t->id);
            f->bare = f->into == TIDE_GPU_TARGET_DEPTH;
            if (!f->valid) f->width = f->height = 1.0f, f->pixel_width = f->pixel_height = 1;
        } else {
            f->gpu = target;
            f->into = TIDE_GPU_TARGET_COLOR; // The window, or pixels of the renderer's own in its place
            f->width = root.width;
            f->height = root.height;
            f->pixel_width = root.pixel_width;
            f->pixel_height = root.pixel_height;
            f->valid = true;
        }
    }
    select_target(0);
    tide_font_frame();
    shape_count = 0;
    shape_extra_count = 0;
    shape_radius_count = 0;
    mesh_vertex_count = 0;
    quad_count = 0;
    step_count = 0;
    for (uint32_t i = 0; i < list->count; i++) {
        const tide_draw_command *c = &list->commands[i];
        frame_target *f = &frame_targets[draw_target];
        camera *cam = &f->through;
        if (c->kind != TIDE_DRAW_TARGET && !f->valid) continue;
        draw_blend = c->blend;
        if (c->kind == TIDE_DRAW_PLAIN) {
            add_plain_run(list, c, cam);
            continue;
        }
        if (c->kind == TIDE_DRAW_SHAPE) {
            i = add_smooth_run(list, i, cam) - 1u; // ...or the smooth ones
            continue;
        }
        const rgba color = to_rgba(c->color);
        switch ((tide_draw_kind)c->kind) {
        case TIDE_DRAW_TARGET:
            select_target(c->target < targets ? c->target : targets);
            add_step(STEP_TARGET);
            break;
        case TIDE_DRAW_CLEAR:
            if (c->a.x != 0.0f) add_step(STEP_CLEAR)->depth_alone = true;
            else add_step(STEP_CLEAR)->color = color;
            break;
        case TIDE_DRAW_CAMERA:
            cam->center = c->a;
            cam->scale = c->b.x > 0.0f ? draw_height / (2.0f * c->b.x) : 1.0f;
            cam->gui = false;
            f->world = *cam;
            break;
        case TIDE_DRAW_SCREEN:
            cam->scale = 1.0f;
            cam->gui = true;
            break;
        case TIDE_DRAW_MESH:
            add_mesh(list, c, cam);
            break;
        case TIDE_DRAW_QUADS:
            add_quads(list, c, cam);
            break;
        case TIDE_DRAW_CAMERA_3D:
            if (c->camera.matrix >= list->matrix_count) { // TIDE_DRAW_FIRST_CAMERA
                f->camera_3d = 0;
                f->fit = 1;
                f->mirrored = false;
                break;
            }
            f->camera_3d = c->camera.matrix + 1u;
            f->fit = c->camera.fit;
            f->mirrored = c->camera.mirrored != 0;
            break;
        case TIDE_DRAW_MESH_3D: {
            if (!c->mesh3.mesh || c->mesh3.mesh > list->mesh_count) break; // Forgotten (see tide_draw_forget)
            if (list->meshes[c->mesh3.mesh - 1u].space == TIDE_MESH_BYTES) break; // A shader's: not these vertices
            draw_step *s = add_step(STEP_MESH_3D);
            s->depth = c->depth;
            s->cull = gpu_cull(c->cull, f->mirrored);
            s->first = c->mesh3.first;
            s->count = c->mesh3.count;
            s->texture = c->mesh3.texture;
            s->filter = c->mesh3.filter;
            s->wrap = c->mesh3.wrap;
            s->mesh = c->mesh3.mesh;
            s->camera = f->camera_3d;
            s->fit = f->fit;
            s->tint = c->color;
            break;
        }
        case TIDE_DRAW_SHADED:
        case TIDE_DRAW_BLIT: {
            if (!run_draws(list, c->shaded.run)) break;
            if (c->kind == TIDE_DRAW_BLIT && list->runs[c->shaded.run].shader->dims != 2) break;
            draw_step *s = add_step(c->kind == TIDE_DRAW_BLIT ? STEP_BLIT : STEP_SHADED);
            s->depth = c->depth;
            s->cull = gpu_cull(c->cull, f->mirrored);
            s->first = c->shaded.run;
            s->camera = f->camera_3d;
            s->fit = f->fit;
            s->through = *cam;
            break;
        }
        case TIDE_DRAW_CLIP: {
            draw_step *s = add_step(STEP_CLIP);
            clip_corners(list, c->clip, &s->at, &s->to);
            break;
        }
        case TIDE_DRAW_NO_CLIP:
            add_step(STEP_NO_CLIP);
            break;
        case TIDE_DRAW_STENCIL:
            // A destination's own, for the steps after it: what's drawn next starts a run of its own
            f->stencil = draw_stencil = tide_stencil_packed(tide_stencil_unpacked(c->stencil));
            add_step(STEP_STENCIL);
            break;
        case TIDE_DRAW_SHAPE:
        case TIDE_DRAW_PLAIN:
            break; // add_plain_run's and add_smooth_run's, above
        case TIDE_DRAW_CIRCLE:
        case TIDE_DRAW_WIRE_CIRCLE:
        case TIDE_DRAW_RECT:
        case TIDE_DRAW_WIRE_RECT:
        case TIDE_DRAW_LINE:
            break; // Kinds of the list's plain shapes, never of its commands
        case TIDE_DRAW_TEXT: {
            // A command in bold without the bold's file (a list filled by hand) is in the regular weight
            const unsigned char *font = c->words.bold ? list->bold : TIDE_FONT_REGULAR;
            if (c->words.turned && c->words.turned <= list->turn_count) {
                add_text_turned(list->text + c->text, to_screen(cam, c->a), c->b.x, cam, list->turns[c->words.turned - 1u],
                                color, c->words.anchor, font);
            } else {
                add_text(list->text + c->text, to_screen(cam, c->a), c->b.x * cam->scale, color, c->words.anchor, font);
            }
            break;
        }
        }
    }
    settle_extras();
    settle_radii();
    if (!over) last_camera = frame_targets[0].world;

    // What the steps draw from goes to the GPU first
    renderer_start();
    if (!over) prepare_data_textures(list);
    if (!over) spare_unused_meshes(list);
    find_pipelines(list);
    upload_shapes_and_meshes();
    upload_meshes_3d(list);
    const bool shaded = upload_shaded(list);
    upload_atlas();
    select_target(0);
    screen_uniforms uniforms = target_uniforms();
    // Start after the CPU expanded the list and prepared/uploaded resources.
    // Otherwise a desktop GPU waiting for that work would time the CPU too.
    // Texture uploads requested by shaded draws still occur inside the query.
    if (timed && gpu->time_begin) gpu->time_begin();
    // A pass begins at the first step that draws in it, so steps that only
    // move to another target cost none: a pass costs the backends that have
    // them (Vulkan, WebGPU) command buffers or encoders, and barriers. The
    // frame's target starts black in its first (Draw.Clear picks another
    // color), or in one of its own if nothing draws in it.
    bool pass_active = false;
    bool cleared = over;
    uint32_t calls = 0; // The steps that draw, a draw call each
    uint64_t vertices = 0;
    for (uint32_t i = 0; i < step_count; i++) {
        const draw_step *s = &steps[i];
        if (s->target != draw_target) {
            if (pass_active) gpu->end();
            pass_active = false;
            select_target(s->target);
            frame_target *f = &frame_targets[draw_target];
            if (!f->valid) {
                fprintf(stderr, "draw skipped: render target %llu is invalid or disposed\n", (unsigned long long)f->id);
                continue;
            }
            if (draw_target && !view_only_3d) {
                gpu_target *g = target_for(f->id, f->size, f->version);
                f->gpu = g ? g->target : 0;
                if (g) g->drawn++; // What reads between its smaller images after this pass has them made again
                gpu_cube *of = g && tide_render_target_cubic(f->id) ? find_cube(tide_render_target_cube_of(f->id)) : NULL;
                if (of) of->drawn++; // A cube's are all its faces'
            }
            uniforms = target_uniforms();
        }
        if (!frame_targets[draw_target].valid) continue;
        if (view_only_3d && !window_3d_step(list, s)) continue; // Its targets, 2D and clips are the frame's own
        if (leaves_nothing(list, s)) continue; // A color, into a target of depth alone
        const char *unavailable = samples_unavailable(list, s);
        if (unavailable) {
            fprintf(stderr, "draw skipped: %s\n", unavailable);
            continue;
        }
        if (samples_destination(list, s)) {
            fprintf(stderr, "draw skipped: render target %llu cannot sample itself\n",
                    (unsigned long long)frame_targets[s->target].id);
            continue;
        }
        if (prepare_source_targets(list, s, true)) {
            if (pass_active) gpu->end();
            pass_active = false;
            prepare_source_targets(list, s, false);
        }
        if (prepare_step_mipmaps(list, s, true)) { // Made outside a pass, as a backend draws them
            if (pass_active) gpu->end();
            pass_active = false;
            prepare_step_mipmaps(list, s, false);
        }
        if (!pass_active && step_draws(s)) {
            begin_pass(frame_targets[draw_target].gpu, !draw_target && !cleared);
            if (!draw_target) cleared = true;
            pass_active = true;
            clip(&frame_targets[draw_target].clipping);
        }
        switch (s->kind) {
        case STEP_TARGET:
        case STEP_STENCIL:
            break;
        case STEP_SHAPES:
            if (!pipeline_made(s)) break;
            // A run with smooth shapes goes through the pipeline that draws every kind, with their extras
            use_pipeline(s);
            if (s->smooth) {
                gpu->uniforms(step_uniforms(&uniforms, s), sizeof uniforms);
                gpu->vertices(2, gpu_state.shape_extras, (size_t)(s->smooth - 1u) * sizeof(shape_extra));
            } else if (s->solid) {
                gpu->uniforms(step_uniforms(&uniforms, s), sizeof uniforms);
                gpu->vertices(2, gpu_state.shape_radii, (size_t)(s->solid - 1u) * sizeof(float));
            } else {
                gpu->uniforms(uniforms.screen, sizeof uniforms.screen);
            }
            gpu->vertices(0, gpu_state.corners, 0);
            gpu->vertices(1, gpu_state.shape_instances, (size_t)s->first * sizeof(shape));
            gpu->draw(0, 6, s->count);
            vertices += UINT64_C(6) * s->count;
            calls++;
            break;
        case STEP_CLEAR: {
            // A triangle over the target, which the clip cuts, and which sets depth and stencil back too
            if (!pipeline_made(s)) break;
            const float color[4] = {(float)s->color.r / 255.0f, (float)s->color.g / 255.0f, (float)s->color.b / 255.0f,
                                    (float)s->color.a / 255.0f};
            use_pipeline(s);
            gpu->uniforms(color, sizeof color);
            gpu->draw(0, 3, 1);
            vertices += 3;
            calls++;
            break;
        }
        case STEP_MESH:
            if (s->count == 0 || !pipeline_made(s)) break;
            use_pipeline(s);
            gpu->uniforms(step_uniforms(&uniforms, s), sizeof uniforms);
            sample_step_texture(list, s);
            vertices += draw_triangles(s);
            calls++;
            break;
        case STEP_MESH_3D:
            if (!pipeline_made(s)) break;
            vertices += draw_mesh_3d(list, s);
            calls++;
            break;
        case STEP_SHADED:
            if (!pipeline_made(s)) break;
            vertices += draw_shaded(list, s);
            calls++;
            break;
        case STEP_BLIT:
            if (!pipeline_made(s)) break;
            clip(&(draw_step){.kind = STEP_NO_CLIP});
            vertices += draw_shaded(list, s);
            calls++;
            clip(&frame_targets[draw_target].clipping);
            break;
        case STEP_CLIP:
        case STEP_NO_CLIP:
            frame_targets[draw_target].clipping = *s;
            if (pass_active) clip(s);
            break;
        default: // Text
            if (s->count == 0 || !gpu_state.atlas || !pipeline_made(s)) break;
            use_pipeline(s);
            gpu->uniforms(step_uniforms(&uniforms, s), sizeof uniforms);
            // Stretched glyphs blend; the others land on the target's pixels
            gpu->sample(0, gpu_state.atlas, TIDE_GPU_FILTER_LINEAR, TIDE_GPU_WRAP_CLAMP);
            vertices += draw_triangles(s);
            calls++;
            break;
        }
    }
    if (pass_active) gpu->end();
    if (!cleared) { // Nothing drew in the frame's target
        begin_pass(target, true);
        gpu->end();
    }
    if (timed && gpu->time_end) gpu->time_end();
    draw_width = draw_height = 0.0f;
    draw_pixel_width = draw_pixel_height = 0;
    scaled = false;
    draw_target = 0;
    if (!over) {
        free_spare_data_textures();
        draw_calls = calls;
        draw_vertices = vertices;
        // The first frame through shaders after the list forgot (a hot reload): what's left of the last build's goes
        if (shaded && list->epoch != shader_epoch) {
            forget_pipelines();
            shader_epoch = list->epoch;
        }
        forget_meshes_3d();
        forget_textures(list->epoch);
    } else {
        free_spare_buffers(); // Those its meshes had no use for
    }
}

// What frames draw into: the window (0), or a headless one's own pixels, made
// the first time and again when the device is another. Either way it's the
// window's size, at the display's scale.
static draw_root frame_destination(void)
{
    draw_root to = {.width = (float)screen_width, .height = (float)screen_height,
                    .pixel_width = pixel_width, .pixel_height = pixel_height};
    if (!headless || !gpu_ready()) return to;
    if (!gpu_state.frame) {
        gpu_state.frame = made_target(TIDE_GPU_TARGET_COLOR, pixel_width, pixel_height);
        gpu_state.frame_width = pixel_width;
        gpu_state.frame_height = pixel_height;
    }
    to.gpu = gpu_state.frame;
    return to;
}

void tide_platform_draw(const tide_draw_list *list)
{
    // Nobody sees it while the window is minimized or, on the web, the page
    // is hidden, and frames go on meanwhile. A headless window's are for
    // whoever reads them.
    if (headless || !tide_window_unseen()) {
        if (!gpu_ready()) return;
        draw_list(list, frame_destination(), false, gpu_timing);
    }
}

// Inside another program, whose GPU draws: the list through the backend that writes the calls down for it
const uint8_t *tide_platform_embed_render(const tide_draw_list *list, uint32_t *size)
{
    gpu = &tide_gpu_record;
    tide_gpu_record_begin();
    draw_list(list, frame_destination(), false, false);
    return tide_gpu_record_end(size);
}

const uint8_t *tide_platform_embed_render_view(const tide_draw_list *list, const tide_float4x4 world_to_clip, const int width,
                                               const int height, uint32_t *size)
{
    gpu = &tide_gpu_record;
    tide_gpu_record_begin();
    view_only_3d = true;
    view_world_to_clip = world_to_clip;
    // Over the frame: what the frame's own drawing keeps from one to the next stays as it is
    const draw_root to = {.width = (float)width, .height = (float)height, .pixel_width = width, .pixel_height = height};
    draw_list(list, to, true, false);
    view_only_3d = false;
    return tide_gpu_record_end(size);
}

void tide_platform_embed_render_reset(void)
{
    tide_gpu_record_reset();
}

void tide_platform_gpu_timing(const bool enabled)
{
    if (gpu_timing == enabled) return;
    gpu_timing = enabled;
    if (!enabled && gpu && gpu->epoch() && gpu->time_reset) gpu->time_reset();
}

double tide_platform_gpu_ms(void)
{
    if (!gpu_timing || !gpu || !gpu_ready() || (!headless && tide_window_unseen()) || !gpu->time_read) return -1.0;
    return gpu->time_read();
}

tide_float2 tide_platform_world_to_screen(const tide_float2 world)
{
    return to_screen(&last_camera, world);
}

tide_float2 tide_platform_screen_size(void)
{
    return tide_f2((float)screen_width, (float)screen_height);
}

tide_float2 tide_platform_screen_scale(void)
{
    return window_scale();
}

tide_int2 tide_platform_target_pixels(const tide_target_size size)
{
    const draw_root window = {.width = (float)screen_width, .height = (float)screen_height,
                              .pixel_width = pixel_width, .pixel_height = pixel_height};
    const tide_gpu *gpu = tide_window_gpu();
    static uint32_t seen_epoch;
    const uint32_t epoch = gpu ? gpu->epoch() : 0;
    if (!epoch) return tide_i2(0, 0);
    if (!target_most || epoch != seen_epoch) {
        target_most = gpu->target_limit();
        seen_epoch = epoch;
    }
    float width, height;
    int wide, high;
    bool limited;
    return target_extent(size, &window, &width, &height, &wide, &high, &limited) ? tide_i2(wide, high) : tide_i2(0, 0);
}

// As add_text draws it: on the window's pixels, or on those of a render
// target's image, as many to a logical one as a frame drawn into the window
// gives it.
tide_float2 tide_platform_measure_text(const char *text, const float size, const tide_target_size *target,
                                       const unsigned char *font)
{
    tide_float2 scale = window_scale();
    if (target) {
        const draw_root window = {.width = (float)screen_width, .height = (float)screen_height,
                                  .pixel_width = pixel_width, .pixel_height = pixel_height};
        float width, height;
        int wide, high;
        bool limited;
        if (!target_extent(*target, &window, &width, &height, &wide, &high, &limited)) return tide_f2(0.0f, 0.0f);
        scale = tide_f2((float)wide / width, (float)high / height);
    }
    tide_float2 measured;
    tide_font_size(font, text, size, scale.x, scale.y, &measured.x, &measured.y);
    return measured;
}

const char *tide_platform_renderer(void)
{
    return gpu ? gpu->name : "";
}

uint32_t tide_platform_draw_calls(void)
{
    return draw_calls;
}

uint64_t tide_platform_draw_vertices(void)
{
    return draw_vertices;
}

void tide_platform_draw_overlay(const char *text)
{
    if (!headless && tide_window_unseen()) return;
    static tide_draw_list overlay;
    const float size = 16.0f;
    const float pitch = 20.0f; // From one line to the next
    char lines[16][128];
    float widths[16];
    int count = 0;
    float widest = 0.0f;
    for (const char *line = text; line && count < 16; count++) {
        const char *end = strchr(line, '\n');
        size_t n = end ? (size_t)(end - line) : strlen(line);
        if (n >= sizeof lines[0]) n = sizeof lines[0] - 1;
        memcpy(lines[count], line, n);
        lines[count][n] = '\0';
        widths[count] = ceilf(tide_font_measure(TIDE_FONT_REGULAR, lines[count], size, window_scale().y));
        if (widths[count] > widest) widest = widths[count];
        line = end ? end + 1 : NULL;
    }
    // A panel in the bottom right corner, each line against its right edge
    const float right = (float)screen_width - 12.0f;
    const float top = (float)screen_height - 12.0f - (float)count * pitch;
    const tide_float2 panel = tide_f2(widest + 16.0f, (float)count * pitch + 8.0f);
    tide_draw_reset(&overlay);
    tide_draw_set_screen(&overlay);
    tide_draw_rect(&overlay, tide_f2(right + 8.0f - panel.x * 0.5f, top - 6.0f + panel.y * 0.5f), panel,
                   (tide_color){0.0f, 0.0f, 0.0f, 140.0f / 255.0f});
    for (int i = 0; i < count; i++) {
        tide_draw_text(&overlay, lines[i], tide_f2(right - widths[i], top + (float)i * pitch), size,
                       (tide_color){200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f});
    }
    draw_list(&overlay, frame_destination(), true, false);
}

void tide_platform_draw_over(const tide_draw_list *list)
{
    if (headless || !tide_window_unseen()) draw_list(list, frame_destination(), true, false);
}

const uint8_t *tide_platform_frame_pixels(int *width, int *height)
{
    *width = *height = 0;
    if (!headless || !gpu_ready() || !gpu_state.frame) return NULL;
    if (!gpu_state.frame_pixels) {
        gpu_state.frame_pixels = tide_alloc((size_t)gpu_state.frame_width * (size_t)gpu_state.frame_height * 4u);
    }
    gpu->target_read(gpu_state.frame, gpu_state.frame_pixels);
    *width = gpu_state.frame_width;
    *height = gpu_state.frame_height;
    return gpu_state.frame_pixels;
}

bool tide_platform_capture(const char *path)
{
    int width, height;
    const uint8_t *pixels = tide_platform_frame_pixels(&width, &height);
    if (!pixels) {
        fprintf(stderr, "tide: no frame to capture: a headless window's are, once one drew (tide_platform_open_headless)\n");
        return false;
    }
    size_t size = 0;
    uint8_t *png = tide_png_encode(pixels, width, height, &size);
    errno = 0;
    FILE *file = png ? fopen(path, "wb") : NULL;
    bool written = file && fwrite(png, 1, size, file) == size;
    const int error = errno; // Before fclose says something else
    if (file && fclose(file) != 0) written = false;
    free(png);
    if (!written) {
        fprintf(stderr, "tide: can't write %s: %s\n", path, error ? strerror(error) : "the file isn't whole");
        return false;
    }
    printf("tide: captured %s, %d by %d pixels\n", path, width, height);
    fflush(stdout);
    return true;
}

void tide_platform_read_pixels(const tide_draw_list *list, const tide_float2 *points, const int count, uint32_t *out)
{
    tide_platform_read_pixels_as(list, screen_width, screen_height, 1.0f, points, count, out);
}

void tide_platform_read_pixels_as(const tide_draw_list *list, const int logical_width, const int logical_height,
                                  const float scale, const tide_float2 *points, const int count, uint32_t *out)
{
    // As a window of that size has them on a display scaled that much (see follow_window)
    const int width = whole_pixels((double)logical_width * (double)scale);
    const int height = whole_pixels((double)logical_height * (double)scale);
    if (!gpu_ready() || logical_width < 1 || logical_height < 1 || !(scale > 0.0f)) { // Nothing to draw with: no pixels
        memset(out, 0, (size_t)(count > 0 ? count : 0) * sizeof *out);
        return;
    }
    if (!gpu_state.target || gpu_state.target_width != width || gpu_state.target_height != height) {
        if (gpu_state.target) free_target(gpu_state.target);
        gpu_state.target = made_target(TIDE_GPU_TARGET_COLOR, width, height);
        gpu_state.target_pixels =
            tide_realloc(gpu_state.target_pixels, (size_t)gpu_state.target_width * (size_t)gpu_state.target_height * 4u,
                         (size_t)width * (size_t)height * 4u);
        gpu_state.target_width = width;
        gpu_state.target_height = height;
    }
    draw_list(list, (draw_root){.gpu = gpu_state.target, .width = (float)logical_width, .height = (float)logical_height,
                              .pixel_width = width, .pixel_height = height}, false, false);
    // Only the pixels from the points' top left to their bottom right come back, where the backend reads parts:
    // reading a whole target for a few points is slow
    int left = width, top = height, right = 0, bottom = 0;
    for (int i = 0; i < count; i++) {
        const int x = (int)points[i].x, y = (int)points[i].y;
        if (x < 0 || x >= width || y < 0 || y >= height) continue;
        if (x < left) left = x;
        if (x >= right) right = x + 1;
        if (y < top) top = y;
        if (y >= bottom) bottom = y + 1;
    }
    if (right <= left) left = top = 0, right = bottom = 1; // None inside: one is read all the same, which waits for the GPU
    if (!gpu->target_read_part) left = top = 0, right = width, bottom = height;
    const int across = right - left;
    if (across == width && bottom - top == height) gpu->target_read(gpu_state.target, gpu_state.target_pixels);
    else gpu->target_read_part(gpu_state.target, left, top, across, bottom - top, gpu_state.target_pixels);
    for (int i = 0; i < count; i++) {
        const int x = (int)points[i].x, y = (int)points[i].y;
        uint32_t pixel = 0;
        if (x >= 0 && x < width && y >= 0 && y < height) {
            const uint8_t *p = gpu_state.target_pixels + ((size_t)(y - top) * (size_t)across + (size_t)(x - left)) * 4u;
            pixel = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
        }
        out[i] = pixel;
    }
}
