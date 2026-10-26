#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tide/wav.h"

// Ogg Vorbis files, read by the engine's own code, written from the Vorbis I
// specification and RFC 3533 (Ogg), so games carry no license terms. Floats
// throughout, with the engine's own sine for its windows and transforms, so a
// file decodes to the same samples on every machine.

// ---------------------------------------------------------------------------
// Limits: what a file's bytes can ask of opening it. A clip's file can come
// from anywhere (a fetched URL), and every player's machine opens it, so a
// small hostile file mustn't end the program or take minutes. Each limit is
// only its number here: change it and nothing else changes. A file past one
// is refused before what it asks for is made. (How many samples a whole
// decode gives is TIDE_SOUND_MAX_SAMPLES, in tide/wav.h.)

// The most codebook entries a file's codebooks have between them. A codebook
// says its count in 24 bits, a few bits of lengths can give millions of them,
// and each is a node of its tree and work as it's built: so up to 256
// codebooks of a hundred bytes each could ask for gigabytes and minutes.
// libvorbis's encoder writes about 11,000 (6,561 in its biggest codebook).
#define TIDE_VORBIS_MAX_ENTRIES (1u << 20)

// The most numbers (entries times dimensions) the vectors of a file's
// codebooks have between them, a float each. libvorbis's encoder writes
// about 61,000.
#define TIDE_VORBIS_MAX_VALUES (1u << 22)

// The most block samples (each block's size, which its transform and window
// go over) the packets take for each frame they give, up to the frames the
// file says it holds. A block gives half its size or so, but one whose
// window says it's next to short blocks it isn't next to gives as little as
// 32 frames for 8192 samples of work: a file of those would take seconds to
// load a few seconds of sound. Counted from each packet's first bits as the
// file opens, with two longest blocks' worth allowed beyond it (a file of a
// block or two takes 8 a frame). libvorbis's and ffmpeg's files take 2 to 2.5.
#define TIDE_VORBIS_MAX_WORK_PER_FRAME 16u

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
// The frames a reader gives, exactly: tide_vorbis_frames, or fewer where the
// packets give fewer (counted from each packet's first bits as the file
// opens). What a whole decode is sized by, as a file can say any number.
uint64_t tide_vorbis_frames_given(const tide_vorbis *file);
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
// Whether a packet read since the start was skipped (not audio, or naming a
// mode the file hasn't got) or ended inside its floors or residue, and so
// decoded as silence or in part: what tidec refuses a file for.
bool tide_vorbis_reader_damaged(const tide_vorbis_reader *reader);

// The whole file at once, as a WAV decodes: false for a file that doesn't
// open, or that gives more samples than a WAV may hold (2^30).
bool tide_vorbis_decode(const uint8_t *bytes, size_t size, tide_sound *out);

// For tests: the inverse MDCT of `n` / 2 coefficients into `n` samples, as the
// decoder computes it (through an FFT), to hold against its definition.
void tide_vorbis_imdct_check(uint32_t n, const float *x, float *y);
