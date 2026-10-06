#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "json.h"

// One file of the game to analyse.
typedef struct analysis_file {
    const char *uri;  // As the editor knows it
    const char *path; // For messages
    const char *text;
    size_t len;
    const char *package; // The package it's in, by name; NULL for the game's own
} analysis_file;

// The compiler's front end run on a whole game, for the language server:
// tolerant lexing and parsing of every file, then the checker. Results stay
// valid until the next analysis_run, which resets the compiler's arena. The
// files must outlive the results.
void analysis_run(const analysis_file *files, int count);

// Picks the document the requests below are about. False if it isn't part of
// the analysed game.
bool analysis_select(const char *uri);
int analysis_file_count(void);
const char *analysis_file_uri(int file);
const char *analysis_file_path(int file);

// Diagnostics for one of the game's files.
void analysis_diagnostics(int file, jbuf *out);                              // Diagnostic[]

// LSP results about the selected document, written as JSON. Positions are
// LSP's: 0-based lines and UTF-16 columns. Results can point into other files
// of the game (definitions, references, renames).
void analysis_completion(int line, int character, jbuf *out);                // CompletionList
void analysis_hover(int line, int character, jbuf *out);                     // Hover or null
void analysis_definition(const char *uri, int line, int character, jbuf *out); // Location or null
void analysis_type_definition(int line, int character, jbuf *out);           // Location or null
void analysis_implementation(int line, int character, jbuf *out);            // Location[] or null
// The call hierarchy. Its calls are an item's: the position of its name, its selectionRange.
void analysis_prepare_call_hierarchy(int line, int character, jbuf *out);    // CallHierarchyItem[] or null
void analysis_incoming_calls(int line, int character, jbuf *out);            // CallHierarchyIncomingCall[]
void analysis_outgoing_calls(int line, int character, jbuf *out);            // CallHierarchyOutgoingCall[]
void analysis_symbols(jbuf *out);                                            // DocumentSymbol[]
// SymbolInformation items of the whole game, comma-separated after `written` others.
void analysis_workspace_symbols(const char *query, jbuf *out, int *written);
void analysis_inlay_hints(int start_line, int end_line, jbuf *out);          // InlayHint[]
void analysis_folding_ranges(jbuf *out);                                     // FoldingRange[]
void analysis_selection_ranges(const json *positions, jbuf *out);            // SelectionRange[], one per Position
void analysis_semantic_tokens(jbuf *out);                                    // SemanticTokens
void analysis_semantic_legend(jbuf *out);                                    // SemanticTokensLegend
void analysis_references(const char *uri, int line, int character, bool declaration, jbuf *out); // Location[]
void analysis_highlights(int line, int character, jbuf *out);                // DocumentHighlight[]
void analysis_signature_help(int line, int character, jbuf *out);           // SignatureHelp or null

// These return an error message for the user instead of writing a result
// when they can't do their job, and NULL when they did.
const char *analysis_prepare_rename(int line, int character, jbuf *out);    // Range
const char *analysis_rename(const char *uri, int line, int character, const char *new_name, jbuf *out); // WorkspaceEdit
// Edits on lines `first_line` to `last_line` (0-based, inclusive) only.
const char *analysis_format(int tab_size, bool insert_spaces, int first_line, int last_line, jbuf *out); // TextEdit[]
void analysis_code_lenses(jbuf *out);                                        // CodeLens[]
void analysis_code_actions(int start_line, int end_line, jbuf *out);         // CodeAction[]

// Whether the editor can create files in an edit, which moving a declaration
// to a file of its own needs. From its capabilities, at initialization.
void analysis_set_can_create_files(bool can);

// Whether the editor shows a diagnostic's notes about other places there
// (relatedInformation), so they can leave its message. Also from its
// capabilities.
void analysis_set_related_information(bool shows);

// ---------------------------------------------------------------------------
// Assets: the files `asset` declarations name (docs/spec.md, Assets).

// Where the analysed game is, before analysis_run: its folder, whose .tide/
// keeps the hashes of its named files (NULL for a game a manifest lists, which
// has none), and the folders a moved file is looked for in: the game's and its
// packages'. Copied; kept until set again.
void analysis_set_game_folders(const char *game_folder, const char *const *roots, int root_count);

// The game's asset sites: each one's file as an absolute path with forward
// slashes (a glob where the declaration's is; NULL for a URL), and the folder
// of the file that names it. For the server's registrations.
int analysis_asset_count(void);
const char *analysis_asset_path(int i);
bool analysis_asset_is_glob(int i);
const char *analysis_asset_folder(int i);

// Files or folders about to be renamed (absolute paths, forward slashes, no
// trailing slash): the edits that keep every asset path at its file, each
// relative to its own declaring file. Globs aren't rewritten.
typedef struct asset_rename {
    const char *old_path;
    const char *new_path;
} asset_rename;
void analysis_rename_files(const asset_rename *renames, int count, jbuf *out); // WorkspaceEdit
