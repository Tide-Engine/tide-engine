#pragma once

// Build-time PSD images: the saved RGB composite, not the document's layers.
// The pixels have the same layout and ownership as the PNG asset reader's.
// No Photoshop code or application is needed, and none of this ships in games.

#include "tide/png.h"

// ---------------------------------------------------------------------------
// Limits: what a PSD file's bytes can ask of an import. A file can be anyone's
// (an asset's URL), so a small hostile file mustn't take the tools' memory.
// Each limit is only its number here: change it and nothing else changes. A
// file past one is refused before what it asks for is made or done. Sides are
// TIDE_PNG_MAX_SIDE's, as PNG's.
//
// A ZIP composite packs about a thousand to one, so a file under a megabyte
// can say a 16384 by 16384 picture: 4 GiB of decoded colors. The decoded
// colors are the import's memory; its work is inflating their channels once.

// The most pixels in a composite. A decoded pixel is 16 bytes, so 8192 by
// 8192 is 1 GiB.
#define TIDE_PSD_MAX_PIXELS (8192u * 8192u)

// PSD version 1, RGB, 8/16 bits per channel, raw, PackBits or ZIP (with or
// without prediction). False for unsupported or invalid files, with a reason
// in `why`, and an empty `out`. The caller frees a successful image's pixels.
bool psd_file_read(const uint8_t *bytes, size_t size, tide_png_image *out, char why[256]);
