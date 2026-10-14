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
// Everything the renderer reads is the mixer's own, or memory the frame's
// thread made for it (an effect's state of more than TIDE_AUDIO_STATE_SMALL
// bytes, which that thread frees once the renderer is done with it): it
// allocates nothing and blocks on nothing, so it's fit for a system's audio
// thread. What it plays is the engine's own float arithmetic and nothing of a
// clock's, so a run with no device renders the same bytes on every machine.

#define TIDE_MIXER_VOICES 256u    // Voices and one-shots at once; past it, a new one takes the quietest's place
#define TIDE_MIXER_ONE_SHOTS 1024u // One-shots waiting for the next block; past it, they're dropped

// Makes the mixer, once: before the first submit or render. Safe to call
// again, and from both threads at once: it's made when the call returns.
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

// The frame's: how many effects' big states the mixer holds (states of more
// than TIDE_AUDIO_STATE_SMALL bytes, each memory of its own): the ones frames
// play, and the ones it waits for the renderer to be done with before it
// frees them.
uint32_t tide_mixer_states(void);

// For tests and a run with no device, where the frame's thread renders: has
// the renderer take what's submitted now. A device's callback does this
// itself at each block.
void tide_mixer_take(void);

// The frame's: stops every voice at once, with no fade, and drops every clip
// and effect, then returns once the renderer has let go of them: at once with
// no device rendering on a thread of its own, and else when that thread has
// taken it (or half a second passed, for a device that stalled).
void tide_mixer_forget(void);

// A backend says a thread of its own renders (tide_mixer_render) from here on,
// or no longer: what tide_mixer_forget waits for. Said before that thread can
// ask for its first block, so the mixer is made by then.
void tide_mixer_rendered_elsewhere(bool elsewhere);

// The renderer's: drops what's left of the last block, so the next render
// starts a new one. For a device that starts over, and for tests.
void tide_mixer_flush(void);

// What the capture device heard: `frames` mono frames at TIDE_AUDIO_RATE, from
// the device's thread (through audio_in.h), into a ring the renderer takes a
// block from as each of its blocks begins, which every voice whose clip is
// the microphone then plays. The renderer keeps the ring within a few blocks
// of the newest frames, so the microphone is never far behind; what nobody
// took in time is skipped, and a ring that's full drops what comes.
void tide_mixer_capture(const float *mono, uint32_t frames);
