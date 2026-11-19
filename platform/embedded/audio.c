// The audio device (src/audio_out.h, src/audio_in.h) of a build that has none of its own: Tide inside another
// program (tide/embed.h), which pulls the mixer's sound on its own audio thread (tide_embed_audio) and pushes what
// its microphone hears (tide_embed_microphone).

#include "audio_in.h"
#include "audio_out.h"

bool tide_audio_out_open(void)
{
    return false;
}

void tide_audio_out_close(void)
{
}

bool tide_audio_in_open(void)
{
    return false;
}

void tide_audio_in_close(void)
{
}
