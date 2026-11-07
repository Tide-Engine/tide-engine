#pragma once

// Fonts text is drawn in: a TrueType file read once into outlines the
// renderer draws from (font data, below), and the fonts a game has, by the
// numbers its Font values hold (the registry).
//
// Font data is what the renderer reads of a font, and all it reads: each
// glyph's outline as stb_truetype's shapes are (moves, lines and quadratic
// curves, in font units), its advance, its box, and which code points are
// which glyphs. It's made of a file by tide_font_data_make, and nothing a
// file says is trusted to be a place: every offset and count in it is checked
// against the bytes there before it's read, a composite glyph is followed
// only so deep, and a file past a limit (tide_font_limits) is refused from
// the counts it says, before what they ask for is made. Font data that came
// from anywhere else (a pack, which is a file too) goes through
// tide_font_data_check before anything reads it, which is what makes any
// bytes that pass it safe to draw: so the renderer never reads a font file,
// even the engine's own, by any other code.
//
// What's read: TrueType outlines ('glyf'), in a .ttf, an .otf that has them,
// or the first font of a collection (.ttc); the cmap subtables stb_truetype
// reads (Unicode's, formats 0, 4, 6, 12 and 13); and the metrics of 'head',
// 'hhea', 'hmtx' and 'maxp'. Hints, kerning and CFF outlines are not.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Limits: what a file can ask of tide_font_data_make, each its number only.
// Font.Decode reads a file from anywhere (a player's, a fetched URL), so a
// small hostile one mustn't end the program or take minutes. At these
// numbers font data is at most 46 MiB: 10 bytes a vertex and 20 a glyph,
// and making it reads at most 64 Mi vertices and code points in all.

#define TIDE_FONT_MAX_BYTES (64u << 20)         // A file's bytes
#define TIDE_FONT_MAX_GLYPH_POINTS 16384u       // Points of one glyph's contours
#define TIDE_FONT_MAX_GLYPH_VERTICES 65535u     // Outline vertices of one glyph, its parts' too
#define TIDE_FONT_MAX_VERTICES (1u << 22)       // Outline vertices of all its glyphs
#define TIDE_FONT_MAX_DEPTH 8u                  // How deep a composite glyph's parts nest
#define TIDE_FONT_MAX_RUNS (1u << 18)           // Runs of code points (font data's cmap)

// ---------------------------------------------------------------------------
// Font data, little-endian, 8-aligned: a header, the runs of code points in
// order, the glyphs, then the vertices.

#define TIDE_FONT_DATA_MAGIC "TIDEFONT"
#define TIDE_FONT_DATA_VERSION 1u

typedef struct tide_font_data_header {
    char magic[8];
    uint32_t version;
    uint32_t em;           // Font units in an em, from 16 to 16384
    int32_t ascent;        // Above the baseline, in font units: hhea's, as stb_truetype's
    int32_t descent;       // Below it, less than 0 for below
    int32_t gap;           // Between lines
    uint32_t glyph_count;  // From 1 to 65535
    uint32_t run_count;
    uint32_t vertex_count;
    uint64_t id;           // What the outlines are: a hash of all after the header, 2 or more
    uint8_t reserved[16];
} tide_font_data_header;

// Code points first to first + count - 1 are glyphs glyph to glyph + count - 1,
// or all `glyph` when TIDE_FONT_RUN_SAME is set in it.
typedef struct tide_font_run {
    uint32_t first;
    uint32_t count;
    uint32_t glyph;
} tide_font_run;
#define TIDE_FONT_RUN_SAME 0x80000000u

typedef struct tide_font_glyph_data {
    uint16_t advance;           // Font units the pen moves
    int16_t bearing;            // Left side bearing
    int16_t x0, y0, x1, y1;     // Its box, y up: as the file says, where `boxed`
    uint32_t first;             // Its first vertex
    uint16_t count;             // Its vertices: 0 for one with nothing to draw
    uint8_t boxed;              // 1 when the file has a box for it (a glyph with an outline)
    uint8_t reserved;
} tide_font_glyph_data;

enum { TIDE_FONT_MOVE = 1, TIDE_FONT_LINE = 2, TIDE_FONT_CURVE = 3 }; // stb_truetype's vmove, vline, vcurve

// A point of an outline: a move starts a contour, a line or a curve goes to
// (x, y) from the last, a curve through control point (cx, cy).
typedef struct tide_font_vertex {
    int16_t x, y, cx, cy;
    uint8_t type;
    uint8_t reserved;
} tide_font_vertex;

_Static_assert(sizeof(tide_font_data_header) == 64, "font data's header");
_Static_assert(sizeof(tide_font_run) == 12, "font data's run");
_Static_assert(sizeof(tide_font_glyph_data) == 20, "font data's glyph");
_Static_assert(sizeof(tide_font_vertex) == 10, "font data's vertex");

typedef enum tide_font_error {
    TIDE_FONT_OK,
    TIDE_FONT_NOT_TRUETYPE,  // Not a font file: no TrueType or collection header
    TIDE_FONT_CFF,           // An OpenType font of CFF outlines, which aren't read
    TIDE_FONT_BROKEN,        // A table it needs is missing, or says a place outside the file
    TIDE_FONT_NO_UNICODE,    // No cmap subtable maps Unicode code points
    TIDE_FONT_TOO_BIG,       // Past a limit (above)
} tide_font_error;

// What's wrong, said so a game's author can fix it ("its outlines are CFF...").
const char *tide_font_error_text(tide_font_error error);

// Reads `size` bytes of a font file into new font data (tide_alloc's, which
// free() releases), *data and *data_size. Bytes past what the file says are
// never read; nothing it makes points into them.
tide_font_error tide_font_data_make(const uint8_t *file, size_t size, uint8_t **data, size_t *data_size);

// Whether `size` bytes are font data that's safe to draw from: its counts add
// up to its size, its runs are in order, inside Unicode and its glyphs, its
// glyphs' vertices are inside its vertices, each starting with a move, and
// every vertex a move, a line or a curve. Its id isn't checked.
bool tide_font_data_check(const uint8_t *data, size_t size);

// Reading checked font data: its header, the glyph a code point is (0 for
// none), a glyph (glyph 0 for one past them), and its vertices.
const tide_font_data_header *tide_font_data_header_of(const uint8_t *data);
uint32_t tide_font_data_glyph_index(const uint8_t *data, uint32_t codepoint);
const tide_font_glyph_data *tide_font_data_glyph(const uint8_t *data, uint32_t glyph);
const tide_font_vertex *tide_font_data_vertices(const uint8_t *data, const tide_font_glyph_data *glyph);

// ---------------------------------------------------------------------------
// A font as drawing takes one (tide/draw.h): checked font data, and the
// number it's known by. NULL is the regular weight of the engine's font
// (TIDE_FONT_REGULAR); the engine's bold weight is TIDE_FONT_BOLD, a TrueType
// file of its own the renderer reads into font data where it's first drawn,
// with `data` NULL. Two fonts of the same id have the same outlines.

typedef struct tide_font {
    const uint8_t *data; // Checked font data; NULL for the engine's own weights
    size_t size;
    uint64_t id;         // 1 for the engine's bold weight, the data's own (2 or more) for the rest
    const uint8_t *file; // The engine's own weight: its TrueType file, which it trusts
    size_t file_size;
} tide_font;

// The registry: the fonts this program has, which a Font names by its
// number (0 is the regular weight, 1 the bold). It keeps a copy of each
// font's data, once however many times the same data is added, until the
// program ends; tide_font_find gives the same pointer for a number every
// time, so draw lists can hold it.

#define TIDE_FONT_REGISTRY_MAX_FONTS 1024u
#define TIDE_FONT_REGISTRY_MAX_BYTES (512ull << 20)

// Checks `size` bytes of font data and adds a copy of it: its number, or 0
// when the bytes aren't font data or the registry is full.
uint64_t tide_font_add(const uint8_t *data, size_t size);

// Reads a font file (tide_font_data_make) and adds what it is: its number,
// or 0 and *error.
uint64_t tide_font_decode(const uint8_t *file, size_t size, tide_font_error *error);

// The font added under `id`, or NULL for one there isn't (or 0 or 1).
const tide_font *tide_font_find(uint64_t id);

// Tide's Font: a font by its number, which is this machine's (local code
// holds one, never the match). 0 is Font.Regular, which a zeroed one is, and
// 1 Font.Bold; any other is the registry's, an asset's (a pack's fonts are
// added as it's loaded) or one Font.Decode read. A number the registry hasn't
// is drawn in the regular weight.
typedef struct tide_font_ref {
    uint64_t id;
} tide_font_ref;
