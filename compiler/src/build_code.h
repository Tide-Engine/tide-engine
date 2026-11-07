#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ast.h"
#include "codegen.h"

// Build code (docs/spec.md, Build code): the assets and constants a game's code makes as it builds. tidec gives
// the program it checked to build_code_run, which finds what each one made in the cache, or else generates the
// program that runs their code (codegen_options.build_program: the game's C and a main), compiles it with the
// build's C compiler, runs it, and keeps what it wrote. What it made goes into the declarations
// (decl.built, and an asset's files' values), which the game's pack and C are then made from.

// What one asset's or constant's code made, in the form the pack and generated C take it.
typedef struct build_value {
    uint8_t *data;   // Text's or bytes' bytes; a plain constant's value as C lays it out; a list's elements; a
    size_t size;     // mesh's vertices as its struct lays them out; an image's tide_colors, rows from the first;
                     // a clip's interleaved float samples
    int32_t chars;   // Text: its UTF-8 characters
    uint32_t element; // A list's element, or a mesh's vertex: bytes each
    uint32_t count;   // ...and how many
    int32_t width, height; // An image's size
    int32_t rate, channels; // A clip's
    uint32_t *indices; // A mesh's triangles, three each
    uint32_t index_count;
    uint8_t hash[32]; // SHA-256 of what it made: its value's identity, as a file's hash is a file's
} build_value;

// Finds what `prog`'s build code made, already made (in this process, or in the game's cache), and puts it into the
// declarations; or else runs it, keeps what it made, and sets *again: the program that ran build code was generated
// from `prog`, which codegen changes, so the caller compiles the game again from its sources, and finds it then.
// False with errors said.
bool build_code_run(program *prog, const codegen_options *opts, bool *again);
