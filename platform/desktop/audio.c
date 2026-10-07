// The output device on desktop (src/audio_out.h): WASAPI on Windows, CoreAudio
// on macOS, ALSA on Linux, loaded at run time as OpenGL and X11 are, so games
// link nothing. Each renders from the mixer on a thread of the system's or
// of its own, through tide_audio_out_fill, at the device's rate and channels.

#include "audio_out.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mixer.h"

#if defined(_WIN32)
// ---------------------------------------------------------------------------
// WASAPI: the default render endpoint, shared mode, event driven, at its own mix format

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

// The GUIDs, spelled out: no libuuid, which the package's MinGW doesn't bring
static const CLSID clsid_enumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID iid_enumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID iid_client = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const IID iid_render = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
static const GUID subtype_float = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const GUID subtype_pcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

static struct {
    HANDLE thread;
    HANDLE event;
    HANDLE opened;         // Signaled once the thread knows whether the device is there
    atomic_bool stop;
    atomic_bool ok;
    float *scratch;        // Frames rendered before they go into the device's own format
    uint32_t scratch_frames;
} wasapi;

static DWORD WINAPI wasapi_thread(LPVOID arg)
{
    (void)arg;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    WAVEFORMATEX *format = NULL;
    const HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool started = false;
    if (FAILED(co) && co != RPC_E_CHANGED_MODE) goto done;
    if (FAILED(CoCreateInstance(&clsid_enumerator, NULL, CLSCTX_ALL, &iid_enumerator, (void **)&enumerator))) goto done;
    if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device))) goto done;
    if (FAILED(IMMDevice_Activate(device, &iid_client, CLSCTX_ALL, NULL, (void **)&client))) goto done;
    if (FAILED(IAudioClient_GetMixFormat(client, &format))) goto done;
    // Floats, or 16-bit integers: what shared mode mixes in, which is nearly always floats
    bool floats = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool ints16 = format->wFormatTag == WAVE_FORMAT_PCM && format->wBitsPerSample == 16;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *x = (const WAVEFORMATEXTENSIBLE *)format;
        floats = memcmp(&x->SubFormat, &subtype_float, sizeof subtype_float) == 0 && format->wBitsPerSample == 32;
        ints16 = memcmp(&x->SubFormat, &subtype_pcm, sizeof subtype_pcm) == 0 && format->wBitsPerSample == 16;
    }
    if (!floats && !ints16) goto done;
    if (FAILED(IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, format, NULL))) goto done;
    UINT32 buffer_frames = 0;
    if (FAILED(IAudioClient_GetBufferSize(client, &buffer_frames))) goto done;
    if (FAILED(IAudioClient_SetEventHandle(client, wasapi.event))) goto done;
    if (FAILED(IAudioClient_GetService(client, &iid_render, (void **)&render))) goto done;
    wasapi.scratch_frames = buffer_frames;
    wasapi.scratch = calloc((size_t)buffer_frames * format->nChannels, sizeof *wasapi.scratch);
    if (!wasapi.scratch) goto done;
    if (FAILED(IAudioClient_Start(client))) goto done;
    started = true;
    atomic_store(&wasapi.ok, true);
    SetEvent(wasapi.opened);
    while (!atomic_load(&wasapi.stop)) {
        if (WaitForSingleObject(wasapi.event, 2000) != WAIT_OBJECT_0) continue;
        UINT32 padding = 0;
        if (FAILED(IAudioClient_GetCurrentPadding(client, &padding))) break;
        const UINT32 frames = buffer_frames > padding ? buffer_frames - padding : 0;
        if (!frames) continue;
        BYTE *data = NULL;
        if (FAILED(IAudioRenderClient_GetBuffer(render, frames, &data))) break;
        tide_audio_out_fill(wasapi.scratch, frames, format->nSamplesPerSec, format->nChannels);
        if (floats) {
            memcpy(data, wasapi.scratch, (size_t)frames * format->nChannels * sizeof(float));
        } else {
            int16_t *out = (int16_t *)data;
            for (size_t i = 0; i < (size_t)frames * format->nChannels; i++) {
                const float v = wasapi.scratch[i] > 1.0f ? 1.0f : wasapi.scratch[i] < -1.0f ? -1.0f : wasapi.scratch[i];
                out[i] = (int16_t)(int)(v * 32767.0f + (v >= 0.0f ? 0.5f : -0.5f));
            }
        }
        IAudioRenderClient_ReleaseBuffer(render, frames, 0);
    }
done:
    if (!started) SetEvent(wasapi.opened);
    if (client && started) IAudioClient_Stop(client);
    if (render) IAudioRenderClient_Release(render);
    if (format) CoTaskMemFree(format);
    if (client) IAudioClient_Release(client);
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    if (SUCCEEDED(co)) CoUninitialize();
    return 0;
}

bool tide_audio_out_open(void)
{
    if (wasapi.thread) return atomic_load(&wasapi.ok);
    atomic_store(&wasapi.stop, false);
    atomic_store(&wasapi.ok, false);
    wasapi.event = CreateEventW(NULL, FALSE, FALSE, NULL);
    wasapi.opened = CreateEventW(NULL, TRUE, FALSE, NULL);
    wasapi.thread = CreateThread(NULL, 0, wasapi_thread, NULL, 0, NULL);
    if (!wasapi.thread || !wasapi.event || !wasapi.opened) {
        fprintf(stderr, "tide: no audio: the device's thread couldn't start\n");
        return false;
    }
    WaitForSingleObject(wasapi.opened, 5000);
    if (!atomic_load(&wasapi.ok)) {
        fprintf(stderr, "tide: no audio: no output device, or none this program can open (WASAPI)\n");
        tide_audio_out_close();
        return false;
    }
    tide_mixer_rendered_elsewhere(true);
    return true;
}

void tide_audio_out_close(void)
{
    if (!wasapi.thread) return;
    atomic_store(&wasapi.stop, true);
    SetEvent(wasapi.event);
    WaitForSingleObject(wasapi.thread, 5000);
    CloseHandle(wasapi.thread);
    CloseHandle(wasapi.event);
    CloseHandle(wasapi.opened);
    free(wasapi.scratch);
    memset(&wasapi, 0, sizeof wasapi);
    tide_mixer_rendered_elsewhere(false);
}

#elif defined(__APPLE__)
// ---------------------------------------------------------------------------
// CoreAudio: the default output unit, rendering floats at the rate it has

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>

static struct {
    AudioUnit unit;
    bool open;
    double rate;
} core;

static OSStatus core_render(void *in, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *when, UInt32 bus,
                            UInt32 frames, AudioBufferList *data)
{
    (void)in;
    (void)flags;
    (void)when;
    (void)bus;
    for (UInt32 i = 0; i < data->mNumberBuffers; i++) {
        AudioBuffer *b = &data->mBuffers[i];
        tide_audio_out_fill((float *)b->mData, frames, (uint32_t)core.rate, b->mNumberChannels);
    }
    return noErr;
}

bool tide_audio_out_open(void)
{
    if (core.open) return true;
    AudioComponentDescription want = {.componentType = kAudioUnitType_Output, .componentSubType = kAudioUnitSubType_DefaultOutput,
                                      .componentManufacturer = kAudioUnitManufacturer_Apple};
    AudioComponent component = AudioComponentFindNext(NULL, &want);
    if (!component || AudioComponentInstanceNew(component, &core.unit) != noErr) goto failed;
    // Stereo floats, interleaved, at 48 kHz: the unit converts to the device's own
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = 48000.0;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = 8;
    format.mBytesPerPacket = 8;
    core.rate = format.mSampleRate;
    if (AudioUnitSetProperty(core.unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof format) != noErr) goto failed;
    AURenderCallbackStruct callback = {.inputProc = core_render, .inputProcRefCon = NULL};
    if (AudioUnitSetProperty(core.unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof callback) != noErr) goto failed;
    if (AudioUnitInitialize(core.unit) != noErr) goto failed;
    tide_mixer_rendered_elsewhere(true);
    if (AudioOutputUnitStart(core.unit) != noErr) {
        tide_mixer_rendered_elsewhere(false);
        goto failed;
    }
    core.open = true;
    return true;
failed:
    fprintf(stderr, "tide: no audio: no output device, or none this program can open (CoreAudio)\n");
    if (core.unit) AudioComponentInstanceDispose(core.unit);
    core.unit = NULL;
    return false;
}

void tide_audio_out_close(void)
{
    if (!core.open) return;
    AudioOutputUnitStop(core.unit);
    AudioUnitUninitialize(core.unit);
    AudioComponentInstanceDispose(core.unit);
    memset(&core, 0, sizeof core);
    tide_mixer_rendered_elsewhere(false);
}

#else
// ---------------------------------------------------------------------------
// ALSA: libasound, loaded at run time, written on a thread of our own

#include <dlfcn.h>
#include <pthread.h>

typedef struct snd_pcm snd_pcm_t;
enum { SND_PCM_STREAM_PLAYBACK = 0, SND_PCM_FORMAT_FLOAT_LE = 14, SND_PCM_ACCESS_RW_INTERLEAVED = 3 };

static struct {
    void *lib;
    int (*open)(snd_pcm_t **, const char *, int, int);
    int (*set_params)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned);
    long (*writei)(snd_pcm_t *, const void *, unsigned long);
    int (*recover)(snd_pcm_t *, int, int);
    int (*close)(snd_pcm_t *);
    snd_pcm_t *pcm;
    pthread_t thread;
    atomic_bool stop;
    bool opened;
} alsa;

#define ALSA_RATE 48000u
#define ALSA_CHANNELS 2u

static void *alsa_thread(void *arg)
{
    (void)arg;
    float block[TIDE_AUDIO_BLOCK * ALSA_CHANNELS];
    while (!atomic_load(&alsa.stop)) {
        tide_audio_out_fill(block, TIDE_AUDIO_BLOCK, ALSA_RATE, ALSA_CHANNELS);
        const long wrote = alsa.writei(alsa.pcm, block, TIDE_AUDIO_BLOCK);
        if (wrote < 0 && alsa.recover(alsa.pcm, (int)wrote, 1) < 0) break; // An underrun is recovered from; anything else ends it
    }
    return NULL;
}

bool tide_audio_out_open(void)
{
    if (alsa.opened) return true;
    alsa.lib = dlopen("libasound.so.2", RTLD_NOW);
    if (!alsa.lib) {
        fprintf(stderr, "tide: no audio: libasound.so.2 isn't there (ALSA)\n");
        return false;
    }
    *(void **)&alsa.open = dlsym(alsa.lib, "snd_pcm_open");
    *(void **)&alsa.set_params = dlsym(alsa.lib, "snd_pcm_set_params");
    *(void **)&alsa.writei = dlsym(alsa.lib, "snd_pcm_writei");
    *(void **)&alsa.recover = dlsym(alsa.lib, "snd_pcm_recover");
    *(void **)&alsa.close = dlsym(alsa.lib, "snd_pcm_close");
    if (!alsa.open || !alsa.set_params || !alsa.writei || !alsa.recover || !alsa.close
        || alsa.open(&alsa.pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0
        || alsa.set_params(alsa.pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, ALSA_CHANNELS, ALSA_RATE, 1, 50000) < 0) {
        fprintf(stderr, "tide: no audio: no output device, or none this program can open (ALSA)\n");
        if (alsa.pcm && alsa.close) alsa.close(alsa.pcm);
        dlclose(alsa.lib);
        memset(&alsa, 0, sizeof alsa);
        return false;
    }
    atomic_store(&alsa.stop, false);
    tide_mixer_rendered_elsewhere(true);
    if (pthread_create(&alsa.thread, NULL, alsa_thread, NULL) != 0) {
        tide_mixer_rendered_elsewhere(false);
        alsa.close(alsa.pcm);
        dlclose(alsa.lib);
        memset(&alsa, 0, sizeof alsa);
        fprintf(stderr, "tide: no audio: the device's thread couldn't start\n");
        return false;
    }
    alsa.opened = true;
    return true;
}

void tide_audio_out_close(void)
{
    if (!alsa.opened) return;
    atomic_store(&alsa.stop, true);
    pthread_join(alsa.thread, NULL);
    alsa.close(alsa.pcm);
    dlclose(alsa.lib);
    memset(&alsa, 0, sizeof alsa);
    tide_mixer_rendered_elsewhere(false);
}
#endif
