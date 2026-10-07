// The output device on Android (src/audio_out.h): AAudio, a stream of floats
// whose data callback renders from the mixer on the system's audio thread.

#include "audio_out.h"

#include <aaudio/AAudio.h>
#include <stdio.h>
#include <string.h>

#include "mixer.h"

static struct {
    AAudioStream *stream;
    int32_t rate, channels;
} aaudio;

static aaudio_data_callback_result_t data(AAudioStream *stream, void *user, void *audio, const int32_t frames)
{
    (void)stream;
    (void)user;
    tide_audio_out_fill(audio, (uint32_t)frames, (uint32_t)aaudio.rate, (uint32_t)aaudio.channels);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

bool tide_audio_out_open(void)
{
    if (aaudio.stream) return true;
    AAudioStreamBuilder *builder = NULL;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) goto failed;
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, (int32_t)TIDE_AUDIO_RATE);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, data, NULL);
    if (AAudioStreamBuilder_openStream(builder, &aaudio.stream) != AAUDIO_OK) goto failed;
    AAudioStreamBuilder_delete(builder);
    builder = NULL;
    aaudio.rate = AAudioStream_getSampleRate(aaudio.stream);
    aaudio.channels = AAudioStream_getChannelCount(aaudio.stream);
    tide_mixer_rendered_elsewhere(true);
    if (AAudioStream_requestStart(aaudio.stream) != AAUDIO_OK) {
        tide_mixer_rendered_elsewhere(false);
        goto failed;
    }
    return true;
failed:
    fprintf(stderr, "tide: no audio: no output stream, or none this app can open (AAudio)\n");
    if (builder) AAudioStreamBuilder_delete(builder);
    if (aaudio.stream) AAudioStream_close(aaudio.stream);
    memset(&aaudio, 0, sizeof aaudio);
    return false;
}

void tide_audio_out_close(void)
{
    if (!aaudio.stream) return;
    AAudioStream_requestStop(aaudio.stream);
    AAudioStream_close(aaudio.stream);
    memset(&aaudio, 0, sizeof aaudio);
    tide_mixer_rendered_elsewhere(false);
}

// ---------------------------------------------------------------------------
// The capture device (src/audio_in.h): an AAudio input stream, once the player
// allowed RECORD_AUDIO, which the game's manifest names (tide build, for a game
// that says Clip.Microphone) and the app asks for the first time it's wanted;
// until then there's no stream, and the platform asks again every second.

#include "audio_in.h"

bool tide_android_microphone_permitted(bool ask); // activity.c

static struct {
    AAudioStream *stream;
    int32_t rate, channels;
    bool asked;
} aaudio_in;

static aaudio_data_callback_result_t captured(AAudioStream *stream, void *user, void *audio, const int32_t frames)
{
    (void)stream;
    (void)user;
    if (frames > 0) tide_audio_in_push(audio, (uint32_t)frames, (uint32_t)aaudio_in.rate, (uint32_t)aaudio_in.channels);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

bool tide_audio_in_open(void)
{
    if (aaudio_in.stream) return true;
    if (!tide_android_microphone_permitted(!aaudio_in.asked)) {
        aaudio_in.asked = true;
        return false;
    }
    AAudioStreamBuilder *builder = NULL;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) goto failed;
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setSampleRate(builder, (int32_t)TIDE_AUDIO_RATE);
    AAudioStreamBuilder_setDataCallback(builder, captured, NULL);
    if (AAudioStreamBuilder_openStream(builder, &aaudio_in.stream) != AAUDIO_OK) goto failed;
    AAudioStreamBuilder_delete(builder);
    builder = NULL;
    aaudio_in.rate = AAudioStream_getSampleRate(aaudio_in.stream);
    aaudio_in.channels = AAudioStream_getChannelCount(aaudio_in.stream);
    tide_audio_in_reset();
    if (AAudioStream_requestStart(aaudio_in.stream) != AAUDIO_OK) goto failed;
    return true;
failed:
    fprintf(stderr, "tide: no microphone: no input stream, or none this app can open (AAudio)\n");
    if (builder) AAudioStreamBuilder_delete(builder);
    if (aaudio_in.stream) AAudioStream_close(aaudio_in.stream);
    aaudio_in.stream = NULL;
    return false;
}

void tide_audio_in_close(void)
{
    if (!aaudio_in.stream) return;
    AAudioStream_requestStop(aaudio_in.stream);
    AAudioStream_close(aaudio_in.stream);
    aaudio_in.stream = NULL;
}
