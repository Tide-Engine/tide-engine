#pragma once

// .tidegraph files: Tide source stored as a graph (notes/visual-scripting.md). tidec reads one as the Tide text it
// stands for, which graph_lower writes, and says where in the graph each part of that text came from.

#include "common.h"

// A JSON value, as a .tidegraph file holds them: read into the arena, with where each one starts in the file.
typedef enum graph_json_kind { GJ_NULL, GJ_BOOL, GJ_NUMBER, GJ_STRING, GJ_ARRAY, GJ_OBJECT } graph_json_kind;

typedef struct graph_json {
    graph_json_kind kind;
    loc at;       // Its first character in the file
    bool boolean; // GJ_BOOL
    double number; // GJ_NUMBER
    str text;     // GJ_STRING: decoded; GJ_NUMBER: as written
    int count;    // GJ_ARRAY's items, GJ_OBJECT's pairs
    struct graph_json **items;
    str *keys;    // GJ_OBJECT
} graph_json;

// Reads JSON text, saying what's wrong with it at its place in `src` (whose text it is). NULL after an error.
graph_json *graph_json_parse(const source *src);
const graph_json *graph_json_get(const graph_json *object, const char *key);

typedef VEC(int) graph_ids;
// The ids a node's data inputs are wired from, in the order its text reads them, by its kind.
void graph_data_wires(const graph_json *node, graph_ids *out);

// A stretch of the text graph_lower wrote, and the element (node, declaration, member, parameter...) it came from.
typedef struct graph_span {
    int start, end; // Offsets in the text, end past the last character
    int id;
} graph_span;

typedef struct graph_place {
    int id;
    loc at; // The element's '{' in the file
} graph_place;

typedef struct graph_map {
    VEC(graph_span) spans;   // In the order they were written: an inner one after the one it's in
    VEC(graph_place) places; // Every element with an id, by id
    VEC(int) line_starts;    // Offsets of the written text's lines, from line 1
    bool lowered;            // The source's text is the Tide it stands for (else still the file's JSON)
} graph_map;

// Whether a path is a graph's, by its name: ends with .tidegraph.
bool graph_is_path(const char *path);

// Reads the graph `src`, whose text is the file's JSON and which diag_add_source has registered, and makes its
// text the Tide the graph stands for, with src->graph saying where each part of it came from. False after saying
// what's wrong, at the graph's places (src->text is the JSON still).
bool graph_lower(source *src);

// The innermost element a place in the written text came from, or 0.
int graph_id_at(const graph_map *map, loc at);
// The element at or just before a place in the file's JSON, or 0.
int graph_id_at_json(const graph_map *map, loc at);
// Where an element starts in the file's JSON, or a zero loc for an id it doesn't have.
loc graph_json_at(const graph_map *map, int id);

// A file's contents, from the arena, NUL-terminated; NULL when it can't be read.
char *graph_read_file(const char *path, size_t *len);
// The Tide a graph's file stands for, as tidec reads it, added to `out`; false after saying what's wrong.
bool graph_text_of(const char *path, sb *out);

// A .tide file as a graph's file (graph_convert.c): its JSON, added to `out`. False, with `*why` saying why, for a
// file the converter can't write as a graph (it doesn't parse).
bool graph_from_tide(const char *path, sb *out, const char **why);

// The test of a .tide file's graph (graph_roundtrip.c): it's made a graph, and the graph's Tide parses to the same
// syntax tree as the file. Says each difference on stderr; returns how many files differed (0 or 1).
int graph_roundtrip(const char *path, bool verbose);
