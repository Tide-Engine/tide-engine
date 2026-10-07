#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/math.h"

// Immediate-mode audio: a frame says what should be sounding, as it says
// what should be drawn. Local code records into a tide_audio_frame (Tide's
// Audio.Play and Sound fields will, and a game's C can), the host hands the
// frame to the platform once a frame (tide_platform_audio), and the mixer
// there plays it on the system's audio thread. The frame knows nothing of any
// device, and nothing of it is simulation state: no sound is in a snapshot,
// a hash or a delta.
//
// Two ways to play. A one-shot (tide_audio_play) starts a sound the mixer
// then owns: nothing comes back, and nothing can stop it. A voice
// (tide_audio_voice) is a sound source a game keeps track of, named by an id
// of its own: the frame says its clip, volume, pitch, position and whether it
// plays, and the mixer keeps a voice for each id a frame names, fading one out
// when a frame stops naming it, so a destroyed entity's sound doesn't hang.
//
// The mixer is stereo at TIDE_AUDIO_RATE, in blocks of TIDE_AUDIO_BLOCK
// frames (TIDE_AUDIO_SAMPLES floats, left then right). A clip of another
// rate, or of one channel, is read at that rate as it plays.
//
// Code of the game's can run on the audio thread, once a block, on a voice's
// or a bus's samples (tide_audio_effect: Tide's `audio` declarations): what
// Unity's OnAudioFilterRead lets a game do, and a source too, for a voice with
// no clip.

#define TIDE_AUDIO_RATE 48000u
#define TIDE_AUDIO_BLOCK 256u                        // Frames a block
#define TIDE_AUDIO_SAMPLES (2u * TIDE_AUDIO_BLOCK)  // Floats a block: interleaved stereo
#define TIDE_AUDIO_BUSES 16u                         // Bus 0 is the master, which every other goes through
#define TIDE_AUDIO_STATE_BYTES 4096u                 // The most an effect's state takes
#define TIDE_AUDIO_KNOBS_BYTES 1024u                 // The most an effect's knobs take

// Sound to play: interleaved samples of `channels` (1 or 2) at `rate` frames
// a second, which stay where they are until no voice plays them: the mixer
// reads them on its own thread, and copies nothing. `id` names the clip (0
// for none): a voice whose clip's id changes starts over from the new one's
// beginning. A clip with no samples plays silence, which is what a source
// (an effect on a voice with no clip) starts from.
typedef struct tide_clip {
    const float *samples;
    uint32_t frames;
    uint32_t channels;
    uint32_t rate;
    uint64_t id;
    uint64_t name; // The Clip it came from, where it's the pack's (tide_clip_of); 0 otherwise
} tide_clip;

// Tide's `Clip`: a sound of the game's pack, named by its declaration (as a
// Texture of the pack's names its image: a number made of the declaration's
// name, and for one of a glob's, of its place in the list or its key in the
// map), so a copy a field keeps goes on naming what the declaration does,
// the file as it is now, through `tide run`'s packs. 0 is `Clip.none`.
typedef struct tide_clip_ref {
    uint64_t name;
} tide_clip_ref;

// The samples a Clip names, as the loaded pack has them: decoded from the
// file's bytes the first time each file is asked for, and kept, so a voice
// that plays them is never left with a pack that's gone (tide run packs
// again when a file changes: the new file is other samples beside the old).
// A name the pack hasn't got is a clip with no samples. The samples stay
// until tide_clips_free, which a host calls once the mixer let go of them.
tide_clip tide_clip_of(tide_clip_ref ref);
void tide_clips_free(void);

// Code the audio thread runs once a block: `samples` is the block, TIDE_AUDIO_SAMPLES
// floats it reads and writes; `state` is `state_size` bytes of the voice's (or
// the bus's) own, zeros when the effect starts and kept between blocks, which
// nothing else reads or writes; `knobs` is a copy of the `knobs_size` bytes the
// frame last gave, read-only. It allocates nothing, blocks on nothing and
// reaches nothing outside its arguments.
typedef void (*tide_audio_effect_fn)(float *samples, void *state, const void *knobs);

typedef struct tide_audio_effect {
    tide_audio_effect_fn fn; // NULL for no effect
    uint64_t id;             // Names the code: another id starts the state over (0 with no fn)
    uint32_t state_size;     // At most TIDE_AUDIO_STATE_BYTES
    uint32_t knobs_size;     // At most TIDE_AUDIO_KNOBS_BYTES
    const void *knobs; // Copied as the frame records it; NULL with none
} tide_audio_effect;

// A sound source, as a frame says it. A voice with `id` 0 is a one-shot.
typedef struct tide_voice {
    uint64_t id;
    tide_clip clip;
    tide_float3 position; // Where it is, when `spatial`
    bool spatial;         // Panned and attenuated by the listener; else plain stereo at `volume`
    bool loop;
    bool playing; // False fades the voice out, and the next true starts its clip over
    float volume; // 1 is the clip as it is
    float pitch;  // 1 is the clip's own speed; 2 an octave up
    uint32_t bus; // 0 to TIDE_AUDIO_BUSES - 1
    tide_audio_effect effect;
    uint32_t knobs_at; // Set by the frame as it records: where the effect's knobs are in `knobs`
} tide_voice;

// A frame's audio. A zeroed one is ready to use, and grows as it needs; running
// out of memory ends the program. It's not simulation state.
typedef struct tide_audio_frame {
    tide_voice *voices; // The voices a frame names, and its one-shots (id 0), in order
    uint32_t voice_count, voice_capacity;
    uint8_t *knobs; // Every effect's knobs, copied
    uint32_t knobs_used, knobs_capacity;

    tide_float3 listener_position;
    tide_quaternion listener_rotation;
    // Every frame starts from the defaults, as a frame's draw settings do: with
    // no listener said, it's at the origin, its right +x; with no falloff said,
    // a spatial sound is at its volume within 1 and silent past 50.
    bool listener_set;
    float falloff_near; // A spatial sound within this distance plays at its volume; 0 for the default
    float falloff_far;  // ...and is silent past this one; 0 for the default
    float bus_volume[TIDE_AUDIO_BUSES];           // 1 is as it is: tide_audio_reset sets each to 1
    tide_audio_effect bus_effect[TIDE_AUDIO_BUSES]; // Its knobs copied too (knobs_at below)
    uint32_t bus_knobs_at[TIDE_AUDIO_BUSES];
    bool forget; // The host says: stop everything and let go of every clip and effect (a reload)

    uint32_t frame; // Counts tide_audio_reset
} tide_audio_frame;

// Empties the frame for a new one, with every bus at volume 1, no effects and
// no listener said. It keeps its memory. Every function below records
// nothing into a NULL frame: local code run by a host that gave it none.
void tide_audio_reset(tide_audio_frame *f);
void tide_audio_free(tide_audio_frame *f);

// Starts a one-shot: `clip` at `volume` and `pitch`, plain stereo.
void tide_audio_play(tide_audio_frame *f, tide_clip clip, float volume, float pitch);
// ...placed at `position` under the listener.
void tide_audio_play_at(tide_audio_frame *f, tide_clip clip, tide_float3 position, float volume, float pitch);

// ...at a float2, which is a float3 with z 0 (Tide's 2D form).
static inline void tide_audio_play_at2(tide_audio_frame *f, const tide_clip clip, const tide_float2 position, const float volume,
                                       const float pitch)
{
    tide_audio_play_at(f, clip, (tide_float3){position.x, position.y, 0.0f}, volume, pitch);
}

// Names a voice this frame, as `voice` says. Its `id` has to be above 0 (0
// records a one-shot that nothing can stop, as tide_audio_play does). The
// effect's knobs are copied into the frame.
void tide_audio_voice(tide_audio_frame *f, const tide_voice *voice);

// Where the listener is and which way it faces (its right is +x turned by
// `rotation`), for the spatial sounds of this frame on.
void tide_audio_set_listener(tide_audio_frame *f, tide_float3 position, tide_quaternion rotation);
// ...at a 2D place, z 0, facing +z with its right at +x (Tide's 2D form).
static inline void tide_audio_set_listener2(tide_audio_frame *f, const tide_float2 center)
{
    tide_audio_set_listener(f, (tide_float3){center.x, center.y, 0.0f}, tide_identity_q());
}

// How a spatial sound's volume falls off: at its volume within `near`,
// silent past `far`.
void tide_audio_set_falloff(tide_audio_frame *f, float near, float far);

// A bus's volume for this frame on, and the effect on its mix (knobs copied).
void tide_audio_set_bus_volume(tide_audio_frame *f, uint32_t bus, float volume);
void tide_audio_set_bus_effect(tide_audio_frame *f, uint32_t bus, const tide_audio_effect *effect);

// The knobs a voice's or a bus's effect was recorded with, in the frame.
static inline const void *tide_audio_knobs(const tide_audio_frame *f, const uint32_t at, const uint32_t size)
{
    return size ? f->knobs + at : NULL;
}
