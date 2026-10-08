#pragma once

// Build-time PSD images: the saved RGB composite, not the document's layers.
// The pixels have the same layout and ownership as the PNG asset reader's.
// No Photoshop code or application is needed, and none of this ships in games.

#include "tide/png.h"

// PSD version 1, RGB, 8/16 bits per channel, raw, PackBits or ZIP (with or
// without prediction). False for unsupported or invalid files, with a reason
// in `why`, and an empty `out`. The caller frees a successful image's pixels.
bool psd_file_read(const uint8_t *bytes, size_t size, tide_png_image *out, char why[256]);
