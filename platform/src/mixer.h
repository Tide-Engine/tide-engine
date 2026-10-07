#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/audio.h"

// The mixer: plays what frames say (tide/audio.h) into stereo blocks of
// TIDE_AUDIO_BLOCK frames at TIDE_AUDIO_RATE, on whatever thread asks for
// them. Two threads touch it: the frame's (tide_mixer_submit, once a frame)
// and the one that renders (tide_mixer_render: a device's callback, or the
// frame's own in a run with no device), which never wait for each other. A
// frame's voices, listener and buses go over in a buffer the renderer takes
// at its next block, the newest winning; its one-shots go through a queue, so
// none is lost when two frames come between two blocks.
//
// Everything the renderer reads is the mixer's own: it allocates nothing and
// blocks on nothing, so it's fit for a system's audio thread. What it plays is
// the engine's own float arithmetic and nothing of a clock's, so a run with
// no device renders the same bytes on every machine.

#define TIDE_MIXER_VOICES 256u    // Voices and one-shots at once; past it, a new one takes the quietest's place
#define TIDE_MIXER_ONE_SHOTS 1024u // One-shots waiting for the next block; past it, they're dropped

// Makes the mixer, once: before the first submit or render. Safe to call again.
void tide_mixer_init(void);

// The frame's: hands the frame's audio over. The frame's arrays are copied, so
// the caller can reset it at once.
void tide_mixer_submit(const tide_audio_frame *frame);

// The renderer's: fills `out` with `frames` stereo frames (2 floats each), as
// many whole blocks as that takes, keeping what's left of the last block for
// the next call. So rendering in any sizes gives the same samples as in blocks.
void tide_mixer_render(float *out, uint32_t frames);

// How many voices are playing (one-shots among them), as the renderer last
// left it: for the stats.
uint32_t tide_mixer_voices(void);

// How many one-shots were dropped for want of room since the mixer was made.
uint32_t tide_mixer_dropped(void);

// For tests and a run with no device, where the frame's thread renders: has
// the renderer take what's submitted now. A device's callback does this
// itself at each block.
void tide_mixer_take(void);

// The renderer's: drops what's left of the last block, so the next render
// starts a new one. For a device that starts over, and for tests.
void tide_mixer_flush(void);
