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
    // The mixer is made, and told a thread renders, before there's a thread:
    // the device asks for its first block as soon as it's started, which is
    // before this function knows it was
    tide_mixer_rendered_elsewhere(true);
    if (wasapi.event && wasapi.opened) wasapi.thread = CreateThread(NULL, 0, wasapi_thread, NULL, 0, NULL);
    if (!wasapi.thread) {
        fprintf(stderr, "tide: no audio: the device's thread couldn't start\n");
        if (wasapi.event) CloseHandle(wasapi.event);
        if (wasapi.opened) CloseHandle(wasapi.opened);
        memset(&wasapi, 0, sizeof wasapi);
        tide_mixer_rendered_elsewhere(false);
        return false;
    }
    WaitForSingleObject(wasapi.opened, 5000);
    if (!atomic_load(&wasapi.ok)) {
        fprintf(stderr, "tide: no audio: no output device, or none this program can open (WASAPI)\n");
        tide_audio_out_close();
        return false;
    }
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
#include <CoreAudio/CoreAudio.h>

static struct {
    AudioUnit unit;
    bool open;
    double rate;
} core;

// Whether the system has a device to play through, which the unit doesn't
// say: with none it opens and starts all the same, and waits for one, so its
// callback never runs, and a forget would wait for a thread that never comes.
static bool core_has_output(void)
{
    // Element 0 is the main one, whose name changed with macOS 12
    const AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, 0};
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 size = sizeof device;
    return AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, &device) == noErr && device != kAudioObjectUnknown;
}

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
    if (!core_has_output()) goto failed;
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

// ---------------------------------------------------------------------------
// The capture device (src/audio_in.h): the default microphone, each system's
// own way, pushing what it hears through tide_audio_in_push from the
// device's thread or a thread of our own.

#include "audio_in.h"

#if defined(_WIN32)

static const IID iid_capture = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};

static struct {
    HANDLE thread;
    HANDLE event;
    HANDLE opened;
    atomic_bool stop;
    atomic_bool ok;
    float *scratch;
} wasapi_in;

static DWORD WINAPI wasapi_in_thread(LPVOID arg)
{
    (void)arg;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *client = NULL;
    IAudioCaptureClient *capture = NULL;
    WAVEFORMATEX *format = NULL;
    const HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool started = false;
    if (FAILED(co) && co != RPC_E_CHANGED_MODE) goto done;
    if (FAILED(CoCreateInstance(&clsid_enumerator, NULL, CLSCTX_ALL, &iid_enumerator, (void **)&enumerator))) goto done;
    if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eCapture, eConsole, &device))) goto done;
    if (FAILED(IMMDevice_Activate(device, &iid_client, CLSCTX_ALL, NULL, (void **)&client))) goto done;
    if (FAILED(IAudioClient_GetMixFormat(client, &format))) goto done;
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
    if (FAILED(IAudioClient_SetEventHandle(client, wasapi_in.event))) goto done;
    if (FAILED(IAudioClient_GetService(client, &iid_capture, (void **)&capture))) goto done;
    wasapi_in.scratch = calloc((size_t)buffer_frames * format->nChannels + 1u, sizeof *wasapi_in.scratch);
    if (!wasapi_in.scratch) goto done;
    tide_audio_in_reset();
    if (FAILED(IAudioClient_Start(client))) goto done;
    started = true;
    atomic_store(&wasapi_in.ok, true);
    SetEvent(wasapi_in.opened);
    while (!atomic_load(&wasapi_in.stop)) {
        if (WaitForSingleObject(wasapi_in.event, 2000) != WAIT_OBJECT_0) continue;
        for (;;) {
            UINT32 packet = 0;
            if (FAILED(IAudioCaptureClient_GetNextPacketSize(capture, &packet)) || !packet) break;
            BYTE *data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(IAudioCaptureClient_GetBuffer(capture, &data, &frames, &flags, NULL, NULL))) break;
            if (frames > buffer_frames) frames = buffer_frames;
            const size_t count = (size_t)frames * format->nChannels;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                memset(wasapi_in.scratch, 0, count * sizeof(float));
            } else if (floats) {
                memcpy(wasapi_in.scratch, data, count * sizeof(float));
            } else {
                const int16_t *in = (const int16_t *)data;
                for (size_t i = 0; i < count; i++) wasapi_in.scratch[i] = (float)in[i] / 32768.0f;
            }
            tide_audio_in_push(wasapi_in.scratch, frames, format->nSamplesPerSec, format->nChannels);
            IAudioCaptureClient_ReleaseBuffer(capture, frames);
        }
    }
done:
    if (!started) SetEvent(wasapi_in.opened);
    if (client && started) IAudioClient_Stop(client);
    if (capture) IAudioCaptureClient_Release(capture);
    if (format) CoTaskMemFree(format);
    if (client) IAudioClient_Release(client);
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    if (SUCCEEDED(co)) CoUninitialize();
    return 0;
}

bool tide_audio_in_open(void)
{
    if (wasapi_in.thread) return atomic_load(&wasapi_in.ok);
    atomic_store(&wasapi_in.stop, false);
    atomic_store(&wasapi_in.ok, false);
    wasapi_in.event = CreateEventW(NULL, FALSE, FALSE, NULL);
    wasapi_in.opened = CreateEventW(NULL, TRUE, FALSE, NULL);
    wasapi_in.thread = CreateThread(NULL, 0, wasapi_in_thread, NULL, 0, NULL);
    if (!wasapi_in.thread || !wasapi_in.event || !wasapi_in.opened) {
        fprintf(stderr, "tide: no microphone: the device's thread couldn't start\n");
        tide_audio_in_close();
        return false;
    }
    WaitForSingleObject(wasapi_in.opened, INFINITE);
    if (!atomic_load(&wasapi_in.ok)) {
        fprintf(stderr, "tide: no microphone: no capture device, or none this program can open (WASAPI)\n");
        tide_audio_in_close();
        return false;
    }
    return true;
}

void tide_audio_in_close(void)
{
    atomic_store(&wasapi_in.stop, true);
    if (wasapi_in.event) SetEvent(wasapi_in.event);
    if (wasapi_in.thread) {
        WaitForSingleObject(wasapi_in.thread, INFINITE);
        CloseHandle(wasapi_in.thread);
    }
    if (wasapi_in.event) CloseHandle(wasapi_in.event);
    if (wasapi_in.opened) CloseHandle(wasapi_in.opened);
    free(wasapi_in.scratch);
    memset(&wasapi_in, 0, sizeof wasapi_in);
}

#elif defined(__APPLE__)
// An AudioQueue of mono floats at 48 kHz, which the system converts from the
// device's own; it asks the player the first time, where the program's
// bundle says why (NSMicrophoneUsageDescription).

#define CORE_IN_BUFFERS 3
#define CORE_IN_FRAMES 480

static struct {
    AudioQueueRef queue;
    bool open;
} core_in;

static void core_in_callback(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer, const AudioTimeStamp *when, UInt32 packets,
                             const AudioStreamPacketDescription *descriptions)
{
    (void)user;
    (void)when;
    (void)packets;
    (void)descriptions;
    const UInt32 frames = buffer->mAudioDataByteSize / sizeof(float);
    if (frames) tide_audio_in_push((const float *)buffer->mAudioData, frames, 48000u, 1u);
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

bool tide_audio_in_open(void)
{
    if (core_in.open) return true;
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = 48000.0;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = 4;
    format.mBytesPerPacket = 4;
    if (AudioQueueNewInput(&format, core_in_callback, NULL, NULL, NULL, 0, &core_in.queue) != noErr) goto failed;
    for (int i = 0; i < CORE_IN_BUFFERS; i++) {
        AudioQueueBufferRef buffer = NULL;
        if (AudioQueueAllocateBuffer(core_in.queue, CORE_IN_FRAMES * sizeof(float), &buffer) != noErr) goto failed;
        AudioQueueEnqueueBuffer(core_in.queue, buffer, 0, NULL);
    }
    tide_audio_in_reset();
    if (AudioQueueStart(core_in.queue, NULL) != noErr) goto failed;
    core_in.open = true;
    return true;
failed:
    fprintf(stderr, "tide: no microphone: no capture device, or none this program can open (CoreAudio)\n");
    if (core_in.queue) AudioQueueDispose(core_in.queue, true);
    memset(&core_in, 0, sizeof core_in);
    return false;
}

void tide_audio_in_close(void)
{
    if (!core_in.open) return;
    AudioQueueStop(core_in.queue, true);
    AudioQueueDispose(core_in.queue, true);
    memset(&core_in, 0, sizeof core_in);
}

#else
// ALSA's capture stream, mono floats at 48 kHz (ALSA resamples), read on a thread of our own

enum { SND_PCM_STREAM_CAPTURE = 1 };

static struct {
    void *lib;
    int (*open)(snd_pcm_t **, const char *, int, int);
    int (*set_params)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned);
    long (*readi)(snd_pcm_t *, void *, unsigned long);
    int (*recover)(snd_pcm_t *, int, int);
    int (*close)(snd_pcm_t *);
    snd_pcm_t *pcm;
    pthread_t thread;
    atomic_bool stop;
    bool opened;
} alsa_in;

static void *alsa_in_thread(void *arg)
{
    (void)arg;
    float block[TIDE_AUDIO_BLOCK];
    while (!atomic_load(&alsa_in.stop)) {
        const long got = alsa_in.readi(alsa_in.pcm, block, TIDE_AUDIO_BLOCK);
        if (got < 0) {
            if (alsa_in.recover(alsa_in.pcm, (int)got, 1) < 0) break;
            continue;
        }
        if (got > 0) tide_audio_in_push(block, (uint32_t)got, ALSA_RATE, 1u);
    }
    return NULL;
}

bool tide_audio_in_open(void)
{
    if (alsa_in.opened) return true;
    alsa_in.lib = dlopen("libasound.so.2", RTLD_NOW);
    if (!alsa_in.lib) {
        fprintf(stderr, "tide: no microphone: libasound.so.2 isn't there (ALSA)\n");
        return false;
    }
    *(void **)&alsa_in.open = dlsym(alsa_in.lib, "snd_pcm_open");
    *(void **)&alsa_in.set_params = dlsym(alsa_in.lib, "snd_pcm_set_params");
    *(void **)&alsa_in.readi = dlsym(alsa_in.lib, "snd_pcm_readi");
    *(void **)&alsa_in.recover = dlsym(alsa_in.lib, "snd_pcm_recover");
    *(void **)&alsa_in.close = dlsym(alsa_in.lib, "snd_pcm_close");
    if (!alsa_in.open || !alsa_in.set_params || !alsa_in.readi || !alsa_in.recover || !alsa_in.close
        || alsa_in.open(&alsa_in.pcm, "default", SND_PCM_STREAM_CAPTURE, 0) < 0
        || alsa_in.set_params(alsa_in.pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, ALSA_RATE, 1, 50000) < 0) {
        fprintf(stderr, "tide: no microphone: no capture device, or none this program can open (ALSA)\n");
        if (alsa_in.pcm && alsa_in.close) alsa_in.close(alsa_in.pcm);
        dlclose(alsa_in.lib);
        memset(&alsa_in, 0, sizeof alsa_in);
        return false;
    }
    atomic_store(&alsa_in.stop, false);
    tide_audio_in_reset();
    if (pthread_create(&alsa_in.thread, NULL, alsa_in_thread, NULL) != 0) {
        alsa_in.close(alsa_in.pcm);
        dlclose(alsa_in.lib);
        memset(&alsa_in, 0, sizeof alsa_in);
        fprintf(stderr, "tide: no microphone: the device's thread couldn't start\n");
        return false;
    }
    alsa_in.opened = true;
    return true;
}

void tide_audio_in_close(void)
{
    if (!alsa_in.opened) return;
    atomic_store(&alsa_in.stop, true);
    pthread_join(alsa_in.thread, NULL);
    alsa_in.close(alsa_in.pcm);
    dlclose(alsa_in.lib);
    memset(&alsa_in, 0, sizeof alsa_in);
}

#endif
