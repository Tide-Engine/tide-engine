#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/list.h"
#include "tide/text.h"

// Tide's `Json.Read<T>` and `Json.Write`: JSON to and from a game's own
// types. Nothing here knows a type: tidec writes a reader and a writer for
// each type a game reads or writes, which call the functions below for the
// values in it, so there's no registry and no value of any shape at run time.
//
// A reader goes through its text once, from the start, and makes what it
// reads in the scratch area (see tide/text.h). The text is whatever bytes a
// game was handed, so nothing of it is trusted: every read is checked against
// its end, containers nest TIDE_JSON_DEPTH deep at the most, text of
// TIDE_JSON_LIMIT bytes or more isn't read, and what a read makes is counted
// (TIDE_JSON_ROOM), so a small text can't ask for all the memory there is.
// Once anything is wrong the reader has failed, and every function says so
// from then on: a loop over an array or an object ends, and generated code
// gives nothing.
//
// Everything is deterministic: numbers are read with exact integer
// arithmetic, never strtod, so the same text gives the same bits on every
// platform (see tide_str_parse_float in tide/text.h).

// How deep arrays and objects nest: one inside another past this fails.
#define TIDE_JSON_DEPTH 32u
// The most text a reader takes: what a fetch gives at the most (tide/io.h).
#define TIDE_JSON_LIMIT (64u << 20)
// The most a read makes, in bytes of elements, entries and text: 128 MiB.
#define TIDE_JSON_ROOM (128u << 20)

typedef struct tide_json {
    const uint8_t *at;  // What's read next
    const uint8_t *end; // Past the text's last byte
    uint64_t made;      // Bytes of what the read has made
    uint32_t depth;     // Arrays and objects open
    bool failed;
    bool first[TIDE_JSON_DEPTH]; // For each one open: nothing of it is read yet
} tide_json;

// A reader at the start of `text`, or of `bytes`, a List<byte> of UTF-8.
void tide_json_begin(tide_json *j, tide_str text);
void tide_json_begin_bytes(tide_json *j, tide_list bytes);
// Whether the text was one value and nothing after it but spaces.
bool tide_json_end(tide_json *j);

// `null`, read if it's what comes next.
bool tide_json_null(tide_json *j);

// One value of each kind, into `*v`; false, with the reader failed, when what
// comes next isn't one. An int is a number that's whole (`3`, `3.0`, `3e2`)
// from `least` to `most`. A float is any number a float holds, rounded to the
// nearest one; one past its range fails. Text and a char are a string, the
// char one of exactly one character, or '\0' for "". A name is a string
// that's one of `names`, whose index goes to `*v`.
bool tide_json_bool(tide_json *j, bool *v);
bool tide_json_int(tide_json *j, int64_t least, int64_t most, int64_t *v);
bool tide_json_float(tide_json *j, float *v);
bool tide_json_text(tide_json *j, tide_text *v);
bool tide_json_char(tide_json *j, int32_t *v);
bool tide_json_name(tide_json *j, const char *const *names, int32_t count, int32_t *v);
// Whether a number comes next, which nothing is read of.
bool tide_json_number_next(tide_json *j);

// An array: tide_json_array reads its `[`, then tide_json_item says whether
// an element comes next, until it has read the `]`. `bytes` is what the
// caller makes for an element, which counts against TIDE_JSON_ROOM.
bool tide_json_array(tide_json *j);
bool tide_json_item(tide_json *j, uint32_t bytes);

// An object: tide_json_object reads its `{`, then one of the two below reads
// each key and its `:`, until it has read the `}`. tide_json_field gives the
// index in `names` of the key, TIDE_JSON_UNKNOWN for a key that's none of
// them (the caller skips its value), and TIDE_JSON_DONE once the object ends
// or the reader fails. tide_json_key gives the key as text.
#define TIDE_JSON_UNKNOWN (-1)
#define TIDE_JSON_DONE (-2)
bool tide_json_object(tide_json *j);
int32_t tide_json_field(tide_json *j, const char *const *names, int32_t count);
bool tide_json_key(tide_json *j, tide_text *key, uint32_t bytes);

// Reads a value of any kind and keeps nothing of it. It still has to be JSON.
bool tide_json_skip(tide_json *j);

// Writing: `a` followed by the value. Text is in quotes with what JSON
// escapes escaped, and bytes that are no character's UTF-8 as U+FFFD, so what
// comes out is always JSON. A char is the text of that character, "" for
// '\0'. A float is the shortest text that reads back as the same float (see
// tide_str_add_float), `-0` for a negative zero, and `null` for NaN and the
// infinities, which JSON has no number for.
tide_str tide_json_add_text(tide_str a, tide_str text);
tide_str tide_json_add_char(tide_str a, int32_t c);
tide_str tide_json_add_float(tide_str a, float v);
