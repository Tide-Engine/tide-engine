#pragma once

#include <stdbool.h>
#include <stdint.h>

// The audio device: what each build has of the system, under the mixer
// (mixer.h). A backend opens the system's output device and renders from the
// mixer on the device's thread (WASAPI, CoreAudio and ALSA on desktop, AAudio
// on Android, and the page's AudioWorklet on the web, which pulls from the
// program's main thread as the page has no other); a build with none, or a
// machine with no device, leaves the mixer unheard, which a run with no window
// is anyway (platform.c renders those itself).

// Opens the output device, and starts rendering: false, with a line on stderr,
// when there's none, after which nothing is heard and nothing else differs.
bool tide_audio_out_open(void);

// Stops rendering and lets the device go. Safe with none open.
void tide_audio_out_close(void);

// What every backend fills its device with: `frames` frames of `channels`
// (1 to 8) at `rate` from the mixer's stereo at TIDE_AUDIO_RATE, resampled
// (linearly) where the rates differ, a mono device given the two averaged,
// more channels given the two and silence. Called on the device's thread, or
// the page's, one thread at a time.
void tide_audio_out_fill(float *out, uint32_t frames, uint32_t rate, uint32_t channels);
