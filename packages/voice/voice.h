#pragma once

#include "tide/voice.h"

// A custom host may keep one client per player, and call frame after its
// views. stop/free require the mixer to have forgotten its effects first.
// Opus and all buffers live here, outside Tide's match/local serialization.
typedef struct tide_voice_client tide_voice_client;
tide_voice_client *tide_voice_client_create(void);
// Suppression filters only outgoing microphone audio.
void tide_voice_client_configure(tide_voice_client *v, bool enabled, bool transmitting, bool noise_suppression, float volume);
void tide_voice_client_volume(tide_voice_client *v, tide_player_id player, float volume);
// Incoming voice uses the normal mixer buses/effects; 0 is master.
void tide_voice_client_bus(tide_voice_client *v, int32_t bus);
// A negative bus clears the player's override, following the default again.
void tide_voice_client_player_bus(tide_voice_client *v, tide_player_id player, int32_t bus);
void tide_voice_client_frame(tide_voice_client *v, tide_session *session, tide_audio_frame *audio, double now);
void tide_voice_client_free(tide_voice_client *v, tide_session *session);
