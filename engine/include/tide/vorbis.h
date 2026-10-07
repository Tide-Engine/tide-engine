#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/wav.h"

// Ogg Vorbis files, read by the engine's own code, written from the Vorbis I
// specification and RFC 3533 (Ogg), so games carry no license terms. Floats
// throughout, with the engine's own sine for its windows and transforms, so a
// file decodes to the same samples on every machine.

// A file opened: its headers read, its codebooks built and its audio packets
// copied out of their pages, so the bytes it was opened from can go. What a
// reader decodes from; several can read one at once (they write nothing in it).
typedef struct tide_vorbis tide_vorbis;

// Opens a file: NULL for one that isn't Ogg Vorbis, is cut short, has a page
// whose checksum is wrong, or has more than two channels, saying why in `*why`
// (a constant text) where that's given. Floor type 0, which no encoder has
// written in twenty years, is refused too.
tide_vorbis *tide_vorbis_open(const uint8_t *bytes, size_t size, const char **why);
void tide_vorbis_close(tide_vorbis *file);

uint32_t tide_vorbis_channels(const tide_vorbis *file); // 1 or 2
uint32_t tide_vorbis_rate(const tide_vorbis *file);
// The frames the file says it holds (its last page's granule position), which
// is as many as a reader can give at the most: a file whose encoder padded
// nothing ends a part of a block short of it.
uint64_t tide_vorbis_frames(const tide_vorbis *file);
// The decoder's state a reader needs, in bytes, for whoever counts memory.
size_t tide_vorbis_reader_size(const tide_vorbis *file);

// A reader decodes a file from its start, as many frames at a time as asked:
// what a voice that streams a long clip holds. One malloc'd piece.
typedef struct tide_vorbis_reader tide_vorbis_reader;

tide_vorbis_reader *tide_vorbis_reader_open(const tide_vorbis *file);
void tide_vorbis_reader_close(tide_vorbis_reader *reader);
// Decodes up to `frames` interleaved frames of the file's channels into `out`,
// and returns how many: fewer only at the file's end, after which it's 0 until
// the reader is rewound. A packet that's cut short or corrupt decodes as
// silence, as the specification has it, and the file goes on.
uint32_t tide_vorbis_read(tide_vorbis_reader *reader, float *out, uint32_t frames);
void tide_vorbis_rewind(tide_vorbis_reader *reader);

// The whole file at once, as a WAV decodes: false for a file that doesn't open.
bool tide_vorbis_decode(const uint8_t *bytes, size_t size, tide_sound *out);

// For tests: the inverse MDCT of `n` / 2 coefficients into `n` samples, as the
// decoder computes it (through an FFT), to hold against its definition.
void tide_vorbis_imdct_check(uint32_t n, const float *x, float *y);
