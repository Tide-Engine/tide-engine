#pragma once

#include "tide/audio.h"
#include "tide/session.h"

// Optional voice implementation, supplied by a package. The engine contains
// only this registration; no codec, buffers, microphone or network activity.
// A game library has its own registration, like tide/network.h. The standard
// host asks its game's table for it after views, before submitting audio.
typedef struct tide_voice_provider {
    void *self;
    void (*frame)(void *self, tide_session *session, tide_audio_frame *audio, double now);
    // Called after the mixer forgets the package's effects, before a reload
    // or shutdown. Detach session callbacks and release all private state.
    void (*stop)(void *self, tide_session *session);
} tide_voice_provider;

void tide_voice_set(const tide_voice_provider *provider);
const tide_voice_provider *tide_voice_get(void);
