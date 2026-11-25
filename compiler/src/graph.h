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
// While held, the schema is read once for every node asked about (graph_data_wires, graph_exec_wires); what's read
// is the arena's, so a hold ends before the arena goes. Holds nest.
void graph_schema_hold(bool hold);
// The ids a node's data inputs are wired from, in the order its text reads them, by its kind.
void graph_data_wires(const graph_json *node, graph_ids *out);
// ...and the statements its exec outputs other than `next` go to (an if's then and else, a loop's body...).
void graph_exec_wires(const graph_json *node, graph_ids *out);

// Laying out a graph (graph_layout.c): a node, its size, what's wired into it and the code it holds
typedef struct graph_layout_node {
    int id;
    int w, h, x, y;
    bool placed;
    graph_ids inputs; // Its data inputs' nodes
    graph_ids bodies; // The first statements of the code it holds
    int next;
    str kind, op;     // Its 'k' and its 'op', for what's shown in the input that reads it
    str name;         // ...and a call's name
    int text;         // Its longest text's length, as measured
    bool folded;      // Shown in its reader's input (graph_layout_fold): it has no size and no place of its own
} graph_layout_node;

// A node's size, from how many ports it has and its longest text
void graph_layout_measure(graph_layout_node *n, int widest_text);
// Which nodes editors show in the input that reads them and not as nodes of their own (the schema's "inline": a
// literal, a name, a path of members from one, a negative number; each read by one input). They take no room,
// and their reader is as wide as showing them takes. graph_layout_place does it; this is for a caller that
// measures placed nodes.
void graph_layout_fold(graph_layout_node *nodes, int count);
// 0 for a kind of node that's always a node, 1 for one always shown in its reader's input, 2 for one that is
// when what it's a member of is, 3 for a `-` over a number, 4 for a call that is when it's named a type made of
// numbers (graph_schema_numbers) and its arguments are numbers.
int graph_schema_inline(str kind);
bool graph_schema_numbers(str name);
// Where each node goes, from `root`: a body's first statement, or a value's node
void graph_layout_place(graph_layout_node *nodes, int count, int root, bool body);
// A graph's JSON ({"entry"|"root", "nodes"}) laid out: {"<id>": [x, y], ...}; false for what isn't a graph
bool graph_layout_json(const graph_json *graph, sb *out);

// A whole file as a board: where each declaration's card or frame goes (and each member's that has code of its
// own, and a property's accessors), data in a column on the left and code beside it. A frame's nodes are placed
// from its own corner, so they stay as they are.
typedef struct graph_board_place {
    int id, x, y;
} graph_board_place;
typedef VEC(graph_board_place) graph_board;
void graph_layout_board(const graph_json *file, graph_board *out);
// ...as JSON: {"<id>": [x, y], ...}
bool graph_layout_board_json(const graph_json *file, sb *out);

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
    VEC(graph_span) spans;   // In the order each was finished: an inner one before the one it's in
    VEC(graph_place) places; // Every element with an id, by id
    VEC(int) line_starts;    // Offsets of the written text's lines, from line 1
    bool lowered;            // The source's text is the Tide it stands for (else still the file's JSON)
    int holes;               // Inputs with no wire, each said and written as GRAPH_HOLE, which the checker says no more of
} graph_map;

// What's written where an input has no wire: the rest of the file is still read and checked around it.
#define GRAPH_HOLE "_graphHole"
// While a hole is being said (a diagnostic's sink is called): the input's key, and its place among the key's
// items from 0 or -1 for a key that's one wire. NULL otherwise.
const char *graph_hole_port(int *index);

// Whether a path is a graph's, by its name: ends with .tidegraph.
bool graph_is_path(const char *path);
// What's printed of graphs (a graph, its Tide, the schema) is the bytes as they are: LF on Windows too.
void graph_binary_stdout(void);

// Reads the graph `src`, whose text is the file's JSON and which diag_add_source has registered, and makes its
// text the Tide the graph stands for, with src->graph saying where each part of it came from. False after saying
// what's wrong, at the graph's places (src->text is the JSON still).
bool graph_lower(source *src);
// ...writing value node `value` even where nothing reads it (as `_ = value;` at its body's end), for an editor's
// question about a value it has just placed (tidels' tide/graphMembers); 0 for none.
bool graph_lower_with(source *src, int value);

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

// The schema graph editors read (graph_schema.json): every kind of declaration, member and node, their keys in the
// order they're written, and each node's ports, as JSON.
const char *graph_schema_text(size_t *len);
