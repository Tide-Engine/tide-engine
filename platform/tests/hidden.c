// A run nobody watches (TIDE_HIDDEN in the environment: tide/page.h), on the
// platform layer. hidden.mjs runs this with the variable set and without, and
// reads what it says:
//
//   tide_platform_hidden says     whether nobody watches, and whether a stop would show a dialog
//   tide_platform_hidden open     opens a window that isn't hidden, which ends the program there
//   tide_platform_hidden abort    stops as the engine stops a program, which shows nothing
//   tide_platform_hidden frames   a hidden window, as tests open theirs: three frames of it
//   tide_platform_hidden audio    a hidden window that plays, and names the microphone: which devices it asks for
//   tide_platform_hidden window   the window itself, asked to show: it doesn't
//
// `open`, `abort` and `window` are for a run with the variable set, and say so
// without it rather than put a window or a dialog on somebody's screen.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_in.h"
#include "audio_out.h"
#include "mixer.h"
#include "tide/page.h"
#include "tide/platform.h"
#include "window.h"

// The audio devices are this file's own, in the place of the system's
// (platform/desktop/audio.c, which a program that has these four isn't linked
// with: the platform layer is a static library). They open nothing, so nobody
// hears this program however it's run, and in `audio` they say what the
// platform asked of them, which is what hidden.mjs reads.
static bool says_audio, output_open, microphone_open;

bool tide_audio_out_open(void)
{
    output_open = true;
    if (says_audio) printf("audio: the output device opens\n");
    return true;
}

void tide_audio_out_close(void)
{
    if (says_audio && output_open) printf("audio: the output device closes\n");
    output_open = false;
}

bool tide_audio_in_open(void)
{
    microphone_open = true;
    if (says_audio) printf("audio: the microphone opens\n");
    return true;
}

void tide_audio_in_close(void)
{
    if (says_audio && microphone_open) printf("audio: the microphone closes\n");
    microphone_open = false;
}

#ifdef _WIN32
#include <crtdbg.h>

__declspec(dllimport) unsigned int __stdcall GetErrorMode(void); // Windows', without its headers

// What a stop would show, asked without changing it: 1 when every dialog is
// off, 0 when they're as the C runtime starts with them, -1 for anything else.
// With `say`, it prints what it found.
static int dialogs(const bool say)
{
    const unsigned stops = _set_abort_behavior(0, 0);                // No bit in the mask: only asked
    const int asserts = _set_error_mode(_REPORT_ERRMODE);
    const unsigned system = GetErrorMode() & (0x0001u | 0x0002u | 0x8000u); // As engine/src/page.c sets it
    bool off = stops == 0 && asserts == _OUT_TO_STDERR && system == (0x0001u | 0x0002u | 0x8000u);
    bool untouched = stops != 0 && asserts == _OUT_TO_DEFAULT; // The system's are its parent's: nothing to tell by
#ifdef _DEBUG
    for (int kind = _CRT_ERROR; kind <= _CRT_ASSERT; kind++) {
        const int mode = _CrtSetReportMode(kind, _CRTDBG_REPORT_MODE);
        off = off && mode == _CRTDBG_MODE_FILE;
        untouched = untouched && mode == _CRTDBG_MODE_WNDW;
    }
#endif
    if (say) printf("a stop: abort %u, assert %d, the system's 0x%X\n", stops, asserts, system);
    return off ? 1 : untouched ? 0 : -1;
}
#else
// Only Windows has dialogs for a program that stops.
static int dialogs(const bool say)
{
    (void)say;
    return tide_hidden() ? 1 : 0;
}
#endif

static int frames;

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    tide_draw_list list = {0};
    tide_draw_clear(&list, TIDE_COLOR_BLUE);
    tide_platform_draw(&list);
    tide_draw_free(&list);
    if (++frames < 3) return TIDE_KEEP_RUNNING;
    printf("drew %d frames\n", frames);
    return 0;
}

// A frame of `audio`: a voice that plays a clip of ones, and for the first two
// frames the microphone named, as a voice of it would. After the third, the
// mixer has the voice whoever listens: with no device's thread to render it,
// this one does, a block.
static int sound(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static float ones[TIDE_AUDIO_SAMPLES];
    for (uint32_t i = 0; i < TIDE_AUDIO_SAMPLES; i++) ones[i] = 1.0f;
    const tide_clip clip = {.samples = ones, .frames = TIDE_AUDIO_BLOCK, .channels = 2, .rate = TIDE_AUDIO_RATE, .id = 1};
    tide_audio_frame f = {0};
    tide_audio_reset(&f);
    tide_audio_voice(&f, &(tide_voice){.id = 1, .clip = clip, .loop = true, .playing = true, .volume = 1.0f, .pitch = 1.0f});
    f.microphone = frames < 2;
    tide_platform_audio(&f);
    tide_audio_free(&f);
    tide_draw_list list = {0};
    tide_draw_clear(&list, TIDE_COLOR_BLUE);
    tide_platform_draw(&list);
    tide_draw_free(&list);
    if (++frames < 3) return TIDE_KEEP_RUNNING;
    float block[TIDE_AUDIO_SAMPLES] = {0};
    tide_mixer_render(block, TIDE_AUDIO_BLOCK);
    const bool plays = tide_mixer_voices() == 1 && block[0] == 1.0f && block[TIDE_AUDIO_SAMPLES - 1] == 1.0f;
    printf("audio: the mixer %s\n", plays ? "plays the frames' voice" : "doesn't play the frames' voice");
    return 0;
}

// GLFW's, without its header: whether the window the context is current in shows.
typedef struct GLFWwindow GLFWwindow;
GLFWwindow *glfwGetCurrentContext(void);
int glfwGetWindowAttrib(GLFWwindow *window, int attrib);
#define GLFW_VISIBLE 0x00020004

int main(const int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "says";
    const bool hidden = tide_hidden();
    const int quiet = dialogs(strcmp(mode, "says") == 0);

    if (strcmp(mode, "says") == 0) {
        printf("%s\n", hidden ? "hidden" : "watched");
        printf("dialogs: %s\n", quiet == 1 ? "off" : quiet == 0 ? "the runtime's" : "neither");
        return 0;
    }
    if (strcmp(mode, "frames") == 0) {
        tide_platform_open(&(tide_window_desc){.title = "hidden", .width = 64, .height = 64, .hidden = true});
        tide_platform_run(frame, NULL);
    }
    if (strcmp(mode, "audio") == 0) {
        says_audio = true;
        tide_platform_open(&(tide_window_desc){.title = "hidden", .width = 64, .height = 64, .hidden = true});
        tide_platform_run(sound, NULL);
    }
    if (!hidden || quiet != 1) {
        printf("FAIL: `%s` is for a run with TIDE_HIDDEN set, and its dialogs off\n", mode);
        return 1;
    }
    if (strcmp(mode, "open") == 0) {
        tide_platform_open(&(tide_window_desc){.title = "hidden", .width = 64, .height = 64});
        printf("FAIL: a window that isn't hidden opened\n");
        tide_window_close();
        return 1;
    }
    if (strcmp(mode, "abort") == 0) {
        fprintf(stderr, "stopping\n");
        abort();
    }
    if (strcmp(mode, "window") == 0) {
        // Under the platform's refusal, the window itself: whatever asks for one gets it hidden
        if (!tide_window_open(&(tide_window_desc){.title = "hidden", .width = 64, .height = 64})) return 1;
        const bool shows = glfwGetWindowAttrib(glfwGetCurrentContext(), GLFW_VISIBLE) != 0;
        tide_window_close();
        printf("%s: a window asked to show doesn't\n", shows ? "FAIL" : "ok");
        return shows ? 1 : 0;
    }
    printf("FAIL: no mode is named '%s'\n", mode);
    return 1;
}
