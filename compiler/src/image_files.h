#pragma once

#include "tide/png.h"

// Build-time images, decoded into straight RGBA float samples. PNG and PSD
// retain their existing readers; JPEG, BMP and TGA use the pinned tool-only
// decoder. Magic takes priority over a filename (URLs use their path suffix).
// TGA needs its suffix because its header has no reliable signature.
// Success owns free-compatible pixels. Failure empties out and explains why.
bool image_file_read(const uint8_t *data, size_t size, const char *name,
                     tide_png_image *out, char why[256]);
