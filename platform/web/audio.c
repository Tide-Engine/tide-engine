// The output device on the web (src/audio_out.h): the page's AudioContext and
// an AudioWorklet of its own (tide.js), which the program's main thread
// feeds, as a page has no other thread the program runs on: the page asks for
// frames as its queue runs low (tide_web_audio_render), from its frame and a
// timer, and the worklet plays them.

#include "audio_out.h"

#include <stdio.h>

#include "mixer.h"
#include "tide_web.h"

#define RENDER_MOST 4096u // Frames a page may ask for at once

static float rendered[RENDER_MOST * 2];
static uint32_t rate;

bool tide_audio_out_open(void)
{
    if (rate) return true;
    rate = (uint32_t)tide_web_audio_open();
    if (!rate) {
        fprintf(stderr, "tide: no audio: the browser gave the page no AudioContext\n");
        return false;
    }
    return true;
}

void tide_audio_out_close(void)
{
    if (!rate) return;
    tide_web_audio_close();
    rate = 0;
}

// The page's: `frames` stereo frames at the context's rate, rendered into the
// program's memory, whose address it gets; at most RENDER_MOST.
__attribute__((export_name("tide_web_audio_render"))) const float *tide_web_audio_render(uint32_t frames)
{
    if (frames > RENDER_MOST) frames = RENDER_MOST;
    tide_audio_out_fill(rendered, frames, rate, 2);
    return rendered;
}

// ---------------------------------------------------------------------------
// The capture device (src/audio_in.h): the page's microphone, through
// getUserMedia and a worklet of tide.js's, which posts what it hears to the
// page's main thread, and the page to the program: into a buffer of the
// program's (tide_web_audio_capture_buffer), then tide_web_audio_captured.

#include "audio_in.h"

#define CAPTURE_MOST 4096u

static float captured[CAPTURE_MOST];
static bool capturing;

bool tide_audio_in_open(void)
{
    if (capturing) return true;
    if (!tide_web_audio_capture_open()) {
        fprintf(stderr, "tide: no microphone: the browser has no audio capture, or the page no AudioContext\n");
        return false;
    }
    tide_audio_in_reset();
    capturing = true;
    return true;
}

void tide_audio_in_close(void)
{
    if (!capturing) return;
    tide_web_audio_capture_close();
    capturing = false;
}

__attribute__((export_name("tide_web_audio_capture_buffer"))) float *tide_web_audio_capture_buffer(void) { return captured; }

__attribute__((export_name("tide_web_audio_captured"))) void tide_web_audio_captured(uint32_t frames, const uint32_t rate)
{
    if (!capturing || !rate) return;
    if (frames > CAPTURE_MOST) frames = CAPTURE_MOST;
    tide_audio_in_push(captured, frames, rate, 1u);
}
