#include "audio_files.h"

#include <float.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dr_flac.h>
#include <dr_mp3.h>

#include "tide/internal/count_text.h"
#include "tide/vorbis.h"

#define IMPORT_SOURCE_MOST ((size_t)128u * 1024u * 1024u)
#define IMPORT_PCM_MOST ((size_t)256u * 1024u * 1024u)
#define DECODER_MEMORY_MOST ((size_t)32u * 1024u * 1024u)
#define IMPORT_SECONDS_MOST 1800u
#define WAV_HEADER 58u
#define DECODE_FRAMES 4096u

static bool refuse(char why[256], const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(why, 256, format, args);
    va_end(args);
    return false;
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

static bool finite_float(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    return (bits & 0x7f800000u) != 0x7f800000u;
}

// Validate without allocating a second copy of an existing WAV's PCM.
static bool check_wav(const uint8_t *data, size_t size, char why[256])
{
    if (size < 12) return refuse(why, "WAV header is cut short");
    if (memcmp(data + 8, "WAVE", 4)) return refuse(why, "RIFF file does not contain WAVE audio");
    const uint32_t riff = le32(data + 4);
    if (riff < 4 || riff > size - 8) return refuse(why, "WAV RIFF chunk is cut short or has an invalid size");
    const size_t end = (size_t)riff + 8;
    uint32_t format = 0, channels = 0, rate = 0, bits = 0, block = 0;
    bool fmt = false;
    for (size_t at = 12; at < end;) {
        if (end - at < 8) return refuse(why, "WAV chunk header is cut short");
        const uint32_t count = le32(data + at + 4);
        if (count > end - at - 8) return refuse(why, "WAV chunk data is cut short");
        const uint8_t *p = data + at + 8;
        if (!memcmp(data + at, "fmt ", 4)) {
            if (count < 16) return refuse(why, "WAV format chunk is cut short");
            format = le16(p); channels = le16(p + 2); rate = le32(p + 4); block = le16(p + 12); bits = le16(p + 14);
            if (format == 0xfffe) {
                static const uint8_t guid_end[12] = {0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71};
                if (count < 40 || le16(p + 16) < 22) return refuse(why, "WAV extensible format chunk is cut short");
                if (memcmp(p + 28, guid_end, sizeof guid_end) || p[26] || p[27])
                    return refuse(why, "WAV extensible subformat is unsupported; use PCM or IEEE float");
                format = le16(p + 24);
                const uint32_t valid = le16(p + 18);
                if (valid && valid > bits) return refuse(why, "WAV valid sample bits exceed its sample width");
            }
            if (format != 1 && format != 3)
                return refuse(why, "WAV codec 0x%04x is unsupported; use PCM or IEEE float", format);
            if (channels < 1 || channels > 2)
                return refuse(why, "WAV has %u channels; only mono and stereo are supported", channels);
            if (!rate) return refuse(why, "WAV sample rate is zero");
            if (format == 1 ? !(bits == 8 || bits == 16 || bits == 24 || bits == 32) : !(bits == 32 || bits == 64))
                return refuse(why, "WAV %s sample width %u is unsupported", format == 1 ? "PCM" : "float", bits);
            if (block != channels * (bits / 8)) return refuse(why, "WAV block alignment does not match its channels and sample width");
            fmt = true;
        } else if (!memcmp(data + at, "data", 4)) {
            if (!fmt) return refuse(why, "WAV data precedes its format chunk");
            if (count % block) return refuse(why, "WAV data ends partway through a sample frame");
            if (count / (bits / 8u) >= TIDE_SOUND_MAX_SAMPLES) {
                char have[32], most[32];
                return refuse(why, "WAV has %s samples, more than the %s a sound holds (TIDE_SOUND_MAX_SAMPLES)",
                              tide_count_text(count / (bits / 8u), have), tide_count_text(TIDE_SOUND_MAX_SAMPLES - 1u, most));
            }
            if (format == 3) {
                const uint32_t width = bits / 8;
                for (size_t i = 0; i < count; i += width) {
                    const bool finite = width == 4 ? (le32(p + i) & 0x7f800000u) != 0x7f800000u
                                                  : (le32(p + i + 4) & 0x7ff00000u) != 0x7ff00000u;
                    if (!finite) return refuse(why, "WAV contains a nonfinite float sample");
                    if (width == 8) {
                        uint64_t raw = (uint64_t)le32(p + i) | (uint64_t)le32(p + i + 4) << 32;
                        double sample;
                        memcpy(&sample, &raw, sizeof sample);
                        if (sample > FLT_MAX || sample < -FLT_MAX) return refuse(why, "WAV float sample is too large for float32 playback");
                    }
                }
            }
            return true;
        }
        const size_t padded = (size_t)count + (count & 1u);
        if (padded > end - at - 8) return refuse(why, "WAV chunk padding is cut short");
        at += 8 + padded;
    }
    return refuse(why, "WAV has no audio data chunk");
}

// Decoder callbacks have an aggregate bound independent of file metadata.
typedef struct allocation_budget { size_t live; bool exceeded; } allocation_budget;
typedef union allocation_header { max_align_t alignment; size_t size; } allocation_header;

static void *decoder_realloc(void *ptr, size_t size, void *user)
{
    allocation_budget *budget = user;
    allocation_header *old = ptr ? (allocation_header *)ptr - 1 : NULL;
    const size_t before = old ? old->size : 0;
    if (size > DECODER_MEMORY_MOST - (budget->live - before)) { budget->exceeded = true; return NULL; }
    allocation_header *next = realloc(old, sizeof *next + (size ? size : 1));
    if (!next) return NULL;
    budget->live = budget->live - before + size;
    next->size = size;
    return next + 1;
}
static void *decoder_malloc(size_t size, void *user) { return decoder_realloc(NULL, size, user); }
static void decoder_free(void *ptr, void *user)
{
    if (!ptr) return;
    allocation_budget *budget = user;
    allocation_header *header = (allocation_header *)ptr - 1;
    budget->live -= header->size;
    free(header);
}

typedef struct pcm_wave {
    uint8_t *bytes;
    size_t capacity;
    uint64_t frames;
    uint32_t channels, rate;
    bool emit;
} pcm_wave;

static bool frame_limit(uint64_t frames, uint32_t channels, uint32_t rate, char why[256], const char *format)
{
    if (!rate || channels < 1 || channels > 2) return refuse(why, "%s requires mono or stereo at a nonzero sample rate", format);
    if (frames > IMPORT_PCM_MOST / (channels * sizeof(float))) return refuse(why, "%s decoded audio exceeds the 256 MiB import limit", format);
    if (frames > (uint64_t)rate * IMPORT_SECONDS_MOST) return refuse(why, "%s duration exceeds the 30 minute import limit", format);
    return true;
}

static bool append_pcm(pcm_wave *wave, const float *pcm, uint32_t frames, char why[256], const char *format)
{
    const uint64_t total = wave->frames + frames;
    if (!frame_limit(total, wave->channels, wave->rate, why, format)) return false;
    const size_t samples = (size_t)frames * wave->channels;
    for (size_t i = 0; i < samples; i++) if (!finite_float(pcm[i])) return refuse(why, "%s decoded a nonfinite sample", format);
    if (wave->emit) {
        const size_t required = WAV_HEADER + (size_t)total * wave->channels * sizeof(float);
        if (required > wave->capacity) {
            size_t capacity = wave->capacity ? wave->capacity : 65536;
            while (capacity < required) capacity = capacity > (IMPORT_PCM_MOST + WAV_HEADER) / 2 ? IMPORT_PCM_MOST + WAV_HEADER : capacity * 2;
            uint8_t *bytes = realloc(wave->bytes, capacity);
            if (!bytes) return refuse(why, "%s import ran out of memory", format);
            wave->bytes = bytes; wave->capacity = capacity;
        }
        uint8_t *out = wave->bytes + WAV_HEADER + (size_t)wave->frames * wave->channels * sizeof(float);
        for (size_t i = 0; i < samples; i++) {
            uint32_t bits;
            memcpy(&bits, pcm + i, sizeof bits);
            put32(out + i * 4, bits);
        }
    }
    wave->frames = total;
    return true;
}

static void finish_wave(pcm_wave *wave, uint8_t **converted, size_t *converted_size)
{
    if (!wave->emit) return;
    uint8_t *p = wave->bytes;
    const uint32_t data = (uint32_t)(wave->frames * wave->channels * 4);
    memcpy(p, "RIFF", 4); put32(p + 4, WAV_HEADER - 8 + data); memcpy(p + 8, "WAVEfmt ", 8);
    put32(p + 16, 18); put16(p + 20, 3); put16(p + 22, wave->channels); put32(p + 24, wave->rate);
    put32(p + 28, wave->rate * wave->channels * 4); put16(p + 32, wave->channels * 4); put16(p + 34, 32); put16(p + 36, 0);
    memcpy(p + 38, "fact", 4); put32(p + 42, 4); put32(p + 46, (uint32_t)wave->frames);
    memcpy(p + 50, "data", 4); put32(p + 54, data);
    *converted = p; *converted_size = WAV_HEADER + data;
}

typedef struct mp3_scan { size_t begin, end; uint64_t frames; uint32_t channels, rate, samples; } mp3_scan;

static bool scan_mp3(const uint8_t *data, size_t size, mp3_scan *scan, char why[256])
{
    size_t begin = 0, end = size;
    while (end >= 128 && !memcmp(data + end - 128, "TAG", 3)) end -= 128;
    if (end >= 32 && !memcmp(data + end - 32, "APETAGEX", 8)) {
        const uint32_t count = le32(data + end - 20);
        if (count < 32 || count > end) return refuse(why, "MP3 APE tag is cut short");
        end -= count;
        if (end >= 32 && !memcmp(data + end - 32, "APETAGEX", 8)) end -= 32;
    }
    while (end - begin >= 3 && !memcmp(data + begin, "ID3", 3)) {
        if (end - begin < 10) return refuse(why, "MP3 ID3 tag header is cut short");
        const uint8_t *tag = data + begin;
        if ((tag[6] | tag[7] | tag[8] | tag[9]) & 0x80u) return refuse(why, "MP3 ID3 tag size is invalid");
        const uint32_t count = (uint32_t)tag[6] << 21 | (uint32_t)tag[7] << 14 | (uint32_t)tag[8] << 7 | tag[9];
        const size_t bytes = 10u + count + (tag[3] == 4 && (tag[5] & 0x10u) ? 10u : 0u);
        if (bytes > end - begin) return refuse(why, "MP3 ID3 tag data is cut short");
        begin += bytes;
    }
    scan->begin = begin; scan->end = end;
    static const uint16_t bitrate1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const uint16_t bitrate2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static const uint32_t rates[3] = {44100, 48000, 32000};
    for (size_t at = begin; at < end;) {
        if (end - at < 4) return refuse(why, "MP3 final frame header is cut short");
        const uint8_t *p = data + at;
        const uint32_t version = (p[1] >> 3) & 3u, layer = (p[1] >> 1) & 3u, bitrate = p[2] >> 4, frequency = (p[2] >> 2) & 3u;
        if (p[0] != 0xff || (p[1] & 0xe0u) != 0xe0u || version == 1 || layer != 1 || frequency == 3 || bitrate == 15)
            return refuse(why, "MP3 frame header is invalid or is not MPEG Layer III");
        if (!bitrate) return refuse(why, "MP3 free-format bitrate is unsupported; use a standard CBR or VBR bitrate");
        const uint32_t rate = rates[frequency] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
        const uint32_t channels = p[3] >> 6 == 3 ? 1 : 2;
        const uint32_t samples = version == 3 ? 1152 : 576;
        const uint32_t kbps = version == 3 ? bitrate1[bitrate] : bitrate2[bitrate];
        const uint32_t bytes = (version == 3 ? 144000u : 72000u) * kbps / rate + ((p[2] >> 1) & 1u);
        if (bytes > end - at) return refuse(why, "MP3 final frame is cut short");
        if (!scan->frames) { scan->rate = rate; scan->channels = channels; scan->samples = samples; }
        else if (scan->rate != rate || scan->channels != channels || scan->samples != samples)
            return refuse(why, "MP3 changes sample rate, channel count or MPEG version partway through");
        scan->frames++;
        if (!frame_limit(scan->frames * samples, channels, rate, why, "MP3")) return false;
        at += bytes;
    }
    if (!scan->frames) return refuse(why, "MP3 contains no audio frames");
    return true;
}

static bool import_mp3(const uint8_t *data, size_t size, pcm_wave *wave, char why[256])
{
    mp3_scan scan = {0};
    if (!scan_mp3(data, size, &scan, why)) return false;
    allocation_budget budget = {0};
    const drmp3_allocation_callbacks alloc = {&budget, decoder_malloc, decoder_realloc, decoder_free};
    drmp3 mp3;
    if (!drmp3_init_memory(&mp3, data + scan.begin, scan.end - scan.begin, &alloc))
        return refuse(why, budget.exceeded ? "MP3 decoder exceeds its 32 MiB memory limit" : "MP3 audio frame data does not decode");
    wave->channels = mp3.channels; wave->rate = mp3.sampleRate;
    uint64_t expected = scan.frames * scan.samples;
    // dr_mp3 skips the Xing/Info metadata frame and applies encoder delay/padding.
    if (mp3.streamStartOffset) expected -= scan.samples;
    const uint64_t trim = (uint64_t)mp3.delayInPCMFrames + mp3.paddingInPCMFrames;
    bool ok = expected >= trim;
    if (!ok) refuse(why, "MP3 encoder delay and padding exceed its audio length");
    else expected -= trim;
    if (ok && mp3.totalPCMFrameCount != UINT64_MAX && mp3.totalPCMFrameCount != expected + trim) {
        ok = refuse(why, "MP3 frame count does not match its Xing/Info header; the file is truncated or corrupt");
    }
    float pcm[DECODE_FRAMES * 2];
    while (ok) {
        const uint64_t got = drmp3_read_pcm_frames_f32(&mp3, DECODE_FRAMES, pcm);
        if (!got) break;
        ok = append_pcm(wave, pcm, (uint32_t)got, why, "MP3");
    }
    drmp3_uninit(&mp3);
    if (ok && wave->frames != expected) ok = refuse(why, "MP3 audio is truncated or corrupt; decoded frame count does not match its frames");
    return ok;
}

static bool import_flac(const uint8_t *data, size_t size, pcm_wave *wave, char why[256])
{
    if (size < 42) return refuse(why, "FLAC STREAMINFO header is cut short");
    if ((data[4] & 0x7fu) != 0 || data[5] || data[6] || data[7] != 34) return refuse(why, "FLAC must begin with a 34-byte STREAMINFO block");
    const uint32_t channels = ((data[20] >> 1) & 7u) + 1;
    if (channels > 2) return refuse(why, "FLAC has %u channels; only mono and stereo are supported", channels);
    size_t at = 4;
    for (;;) {
        if (size - at < 4) return refuse(why, "FLAC metadata header is cut short");
        const uint32_t count = (uint32_t)data[at + 1] << 16 | (uint32_t)data[at + 2] << 8 | data[at + 3];
        if (count > size - at - 4) return refuse(why, "FLAC metadata block is cut short");
        const bool last = (data[at] & 0x80u) != 0;
        at += 4u + count;
        if (last) break;
    }
    allocation_budget budget = {0};
    const drflac_allocation_callbacks alloc = {&budget, decoder_malloc, decoder_realloc, decoder_free};
    drflac *flac = drflac_open_memory(data, size, &alloc);
    if (!flac) return refuse(why, budget.exceeded ? "FLAC decoder exceeds its 32 MiB memory limit" : "FLAC headers or audio frames do not decode");
    wave->channels = flac->channels; wave->rate = flac->sampleRate;
    const uint64_t expected = flac->totalPCMFrameCount;
    bool ok = frame_limit(expected, wave->channels, wave->rate, why, "FLAC");
    if (ok && !expected) ok = refuse(why, "FLAC has no declared sample count; export a complete file with STREAMINFO length");
    float pcm[DECODE_FRAMES * 2];
    while (ok) {
        const uint64_t got = drflac_read_pcm_frames_f32(flac, DECODE_FRAMES, pcm);
        if (!got) break;
        ok = append_pcm(wave, pcm, (uint32_t)got, why, "FLAC");
    }
    drflac_close(flac);
    if (ok && wave->frames != expected) ok = refuse(why, "FLAC audio is truncated or corrupt; decoded sample count or frame checksum is wrong");
    return ok;
}

// Decodes an Ogg Vorbis clip once as every player's runtime will, keeping no
// samples: the file stays as it is, but one whose packets are cut short or
// corrupt (which a player hears as silence), that says it holds more than
// they give, or that decodes to a nonfinite sample is refused here.
static bool check_vorbis(const tide_vorbis *vorbis, char why[256])
{
    tide_vorbis_reader *reader = tide_vorbis_reader_open(vorbis);
    const uint32_t channels = tide_vorbis_channels(vorbis);
    float pcm[DECODE_FRAMES * 2];
    bool ok = true;
    for (;;) {
        const uint32_t got = tide_vorbis_read(reader, pcm, DECODE_FRAMES);
        if (!got) break;
        for (size_t i = 0; ok && i < (size_t)got * channels; i++) {
            if (!finite_float(pcm[i])) ok = refuse(why, "Ogg Vorbis decoded a nonfinite sample; the file is corrupt");
        }
        if (!ok) break;
    }
    if (ok && tide_vorbis_reader_damaged(reader)) ok = refuse(why, "Ogg Vorbis audio packet is cut short or corrupt");
    // A granule past what the packets give isn't refused: encoders write such
    // files, and they play as the packets give, which is what a clip is sized by
    tide_vorbis_reader_close(reader);
    return ok;
}

bool audio_file_read(const uint8_t *data, size_t size, uint8_t **converted, size_t *converted_size, char why[256])
{
    if (converted) *converted = NULL;
    if (converted_size) *converted_size = 0;
    why[0] = 0;
    if ((!converted) != (!converted_size)) return refuse(why, "audio conversion requires both output pointers");
    if (!data || !size) return refuse(why, "audio file is empty");
    if (size >= 4 && !memcmp(data, "RIFF", 4)) return check_wav(data, size, why);
    if (size >= 4 && !memcmp(data, "OggS", 4)) {
        const size_t payload = size >= 27 ? 27u + data[26] : size;
        if (payload <= size && size - payload >= 8 && !memcmp(data + payload, "OpusHead", 8))
            return refuse(why, "Ogg Opus is unsupported; use Ogg Vorbis, WAV, MP3 or FLAC");
        const char *detail = NULL;
        tide_vorbis *vorbis = tide_vorbis_open(data, size, &detail);
        if (!vorbis) return refuse(why, "Ogg Vorbis: %s", detail ? detail : "file does not decode");
        const bool ok = check_vorbis(vorbis, why);
        tide_vorbis_close(vorbis);
        return ok;
    }
    const bool flac = size >= 4 && !memcmp(data, "fLaC", 4);
    const bool mp3 = (size >= 3 && !memcmp(data, "ID3", 3)) || (size >= 2 && data[0] == 0xff && (data[1] & 0xe0u) == 0xe0u);
    if (!flac && !mp3) {
        if (size >= 4 && (!memcmp(data, "RIFX", 4) || !memcmp(data, "RF64", 4)))
            return refuse(why, "WAV container %.4s is unsupported; use little-endian RIFF WAVE", (const char *)data);
        if (size >= 12 && !memcmp(data, "FORM", 4) && (!memcmp(data + 8, "AIFF", 4) || !memcmp(data + 8, "AIFC", 4)))
            return refuse(why, "AIFF audio is unsupported; use WAV, Ogg Vorbis, MP3 or FLAC");
        return refuse(why, "unrecognized audio format; use WAV, Ogg Vorbis, MP3 or FLAC");
    }
    if (size > IMPORT_SOURCE_MOST) return refuse(why, "%s source exceeds the 128 MiB import limit", flac ? "FLAC" : "MP3");
    pcm_wave wave = {.emit = converted != NULL};
    const bool ok = flac ? import_flac(data, size, &wave, why) : import_mp3(data, size, &wave, why);
    if (ok && wave.frames) finish_wave(&wave, converted, converted_size);
    else {
        free(wave.bytes);
        if (ok) return refuse(why, "%s contains no decoded audio frames", flac ? "FLAC" : "MP3");
    }
    return ok;
}
