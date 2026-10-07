#pragma once

#include <stdbool.h>
#include <stdint.h>

// The capture device, the microphone: what each build has of the system,
// feeding the mixer (mixer.h), which plays what's captured through every
// voice whose clip is the microphone (Tide's `Clip.Microphone`). A backend
// opens the system's default capture device and pushes what it captures from
// the device's thread (WASAPI, CoreAudio and ALSA on desktop, AAudio on
// Android, and a worklet of the page's on the web); a build with none, or a
// machine with no microphone, or a player who hasn't allowed it yet, leaves
// the microphone silent, and nothing else differs. The platform opens it
// while a frame names a microphone voice and closes it when none does.

// Opens the capture device and starts capturing: false when there's none, or
// the system hasn't allowed it (yet), with a line on stderr for a device
// that's missing, and none for a permission still asked.
bool tide_audio_in_open(void);

// Stops capturing and lets the device go. Safe with none open.
void tide_audio_in_close(void);

// What every backend pushes what it captured through: `frames` frames of
// `channels` (1 to 8, averaged to one) at `rate`, resampled (linearly) to
// TIDE_AUDIO_RATE where the rates differ, into the mixer's capture ring. From
// one thread at a time, the device's; a headless run pushes a file's frames
// from its own.
void tide_audio_in_push(const float *samples, uint32_t frames, uint32_t rate, uint32_t channels);

// Starts the resampler over, for a device opened again at another rate.
void tide_audio_in_reset(void);
