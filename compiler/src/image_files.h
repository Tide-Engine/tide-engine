#pragma once

#include "tide/png.h"

// ---------------------------------------------------------------------------
// Limits: what a JPEG, TGA or BMP file's bytes can ask of an import. A file
// can be anyone's (an asset's URL), so a small hostile file mustn't take the
// tools' memory or minutes. Each limit is only its number here: change it and
// nothing else changes. A file past one is refused before what it asks for is
// made or done. Sides are TIDE_PNG_MAX_SIDE's, as PNG's; PSD's own limit is in
// psd_files.h.
//
// At these numbers an import's peak memory is the source plus
// TIDE_IMAGE_MAX_BYTES, and its work is about TIDE_IMAGE_MAX_JPEG_SCANS passes
// over the largest picture the budget allows (about 26 million pixels).

// A source file's bytes, and the decoder's own allocations together with the
// decoded float pixels (16 bytes a pixel, and 4 more while decoding).
#define TIDE_IMAGE_MAX_BYTES (512u << 20)

// Scans in a JPEG. Each scan of a progressive JPEG decodes the whole picture
// again, and a scan's header is a few bytes; 2,000 of them over a 4096 by
// 4096 picture took 26 seconds. Encoders write ten or so.
#define TIDE_IMAGE_MAX_JPEG_SCANS 256u

// Build-time images, decoded into straight RGBA float samples. PNG and PSD
// retain their existing readers; JPEG, BMP and TGA use the pinned tool-only
// decoder. Magic takes priority over a filename (URLs use their path suffix).
// TGA needs its suffix because its header has no reliable signature.
// Success owns free-compatible pixels. Failure empties out and explains why.
bool image_file_read(const uint8_t *data, size_t size, const char *name,
                     tide_png_image *out, char why[256]);
