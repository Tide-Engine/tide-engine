#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Checks a Clip's bytes by their contents. WAV and Ogg Vorbis stay as they
// are; MP3 and native FLAC are fully decoded to a mono/stereo float32 WAV.
// Success leaves *converted NULL and *converted_size 0 for unchanged bytes,
// or a malloc-owned WAV for an import. The input is never changed. Both
// output pointers may be NULL when only validation is wanted. On failure,
// outputs are cleared and why describes the format and what is wrong.
// Imports are limited to 128 MiB of source, 256 MiB of PCM and 30 minutes.
bool audio_file_read(const uint8_t *data, size_t size, uint8_t **converted, size_t *converted_size, char why[256]);
