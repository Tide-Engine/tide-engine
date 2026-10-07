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
