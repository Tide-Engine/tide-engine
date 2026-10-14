# stb_image

`stb_image.h` v2.30 and `LICENSE` come from https://github.com/nothings/stb
at commit `2c980bb59875b0d32144a71867fbdebb2f77cd20`.

Only the compiler's build-time JPEG, BMP and TGA import uses this code.
`image_files.c` disables every other format, stdio, SIMD and float/HDR
conversion. The same scalar integer decode runs on every build host.
Temporary allocations and final pixels have a combined 512 MiB budget;
sources have a 512 MiB limit, with 16384 pixels per side.

Local patches:

- JPEG: an invalid marker after a scan fails instead of returning a partial
  image as successful.
- JPEG: scalar IDCT intermediates use 64-bit integers so corrupt large
  coefficients cannot overflow signed 32-bit arithmetic. Valid pixels retain
  the original integer operations and rounding.
- TGA: a palette's first entry is an index origin, not bytes to skip; subtract
  it when looking up a pixel. The wrapper validates every index first.
- BMP: 56-byte V3 headers already contain their bitfields; read them there
  instead of reading a second mask set after the header.

The wrapper additionally checks complete JPEG marker streams, BMP row spans
and palette indices, and TGA raw/RLE spans, then corrects TGA's right-origin
orientation. Packed RGB15/16 TGA is opaque; declared 16-bit alpha is refused
with export guidance rather than discarded. Color profiles and JPEG EXIF
orientation are not applied, matching the existing numeric image pipeline.
