#include "tide/voice.h"

static tide_voice_provider voice;

void tide_voice_set(const tide_voice_provider *provider)
{
    voice = provider ? *provider : (tide_voice_provider){0};
}

const tide_voice_provider *tide_voice_get(void)
{
    return voice.frame ? &voice : NULL;
}
