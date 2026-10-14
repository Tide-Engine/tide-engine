# dr_libs: build-time audio imports

`dr_mp3.h` (0.7.4), `dr_flac.h` (0.13.4) and `LICENSE` are unchanged from
[mackron/dr_libs](https://github.com/mackron/dr_libs) revision
`dfe8377631000664666519fdb83da193fd8037f4`.

`dr_decoders.c` builds the two decoders for compiler tools only, with scalar
arithmetic, no filesystem access, MP3 Layer III only, and native FLAC only.
FLAC checksum validation remains enabled. Imported samples become float32
WAVs in the game's pack; none of this code is linked into a game.

The upstream license offers a choice of the Unlicense or MIT-0. Tide uses
the Unlicense; the complete upstream license is retained here.
