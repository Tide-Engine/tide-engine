#pragma once

#include "ast.h"

// Compiler pointers and slices last until editor_refresh. Source edits own their
// bytes; they never retain pointers into an analysis or execute game code.
typedef struct editor_node {
    str path;
    int parent;
    prefab_node *authored; // NULL for an inherited descendant.
    expr *effective;
    int open, close;      // Token indices in the edited file, or -1.
    bool inherited;      // True even when a local value-override block exists.
} editor_node;

typedef struct editor_document {
    char *path, *project, *text, *saved;
    char *draft_path, *recovery, *recovery_base;
    char message[1024];
    bool conflict, syntax_errors, parsing;
    int errors, warnings;
    program *prog;
    source *src;
    token *tokens;
    int token_count;
    VEC(decl *) prefabs;
    VEC(editor_node) nodes;
    VEC(char *) diagnostics;
    int active;
    char *undo[64], *redo[64];
    int undo_count, redo_count;
    uint64_t stamp;
    uint64_t revision;
} editor_document;

bool editor_open(editor_document *d, const char *path, const char *project);
// Close the previous document first: compiler analysis uses one global arena.
// Creates an exclusive new file in an existing project's source boundary.
bool editor_create(editor_document *d, const char *path, const char *project, const char *name);
// Appends the first prefab to an open, syntax-valid own source document.
// One undo/recovery transaction; leaves the source file unchanged until Save.
bool editor_initialize_prefab(editor_document *d, const char *name);
void editor_close(editor_document *d);
void editor_refresh(editor_document *d);
void editor_poll(editor_document *d); // Rescan source/package changes; protect unsaved edits.
bool editor_dirty(const editor_document *d);
bool editor_save(editor_document *d);
bool editor_reload(editor_document *d, bool discard);
bool editor_recover(editor_document *d, bool recover);
bool editor_undo(editor_document *d, bool redo);
void editor_select_prefab(editor_document *d, int index);
bool editor_set_field(editor_document *d, int node, str component, str field, const char *value);
bool editor_reset_field(editor_document *d, int node, str component, str field);
bool editor_add_component(editor_document *d, int node, str component);
bool editor_remove_component(editor_document *d, int node, str component);
bool editor_add_child(editor_document *d, int node, const char *base);
bool editor_add_named_child(editor_document *d, int node, const char *name, const char *base);
bool editor_add_model(editor_document *d, int node, const char *name, str component, str field, const char *asset);
bool editor_remove_node(editor_document *d, int node);
bool editor_rename_node(editor_document *d, int node, const char *name);
bool editor_duplicate_node(editor_document *d, int node, const char *name);
// Preserves local Transform values; world-space placement can change.
bool editor_reparent_node(editor_document *d, int node, int parent);
// Writes a new asset before replacing this block. Undo keeps the new asset.
bool editor_extract_prefab(editor_document *d, int node, const char *path, const char *name);
bool editor_node_inherited(const editor_document *d, int node);
bool editor_can_structure(const editor_document *d, int node);
bool editor_can_edit_fields(const editor_document *d, int node);
bool editor_field_overridden(const editor_document *d, int node, str component, str field);
const char *editor_node_source(const editor_document *d, int node); // Borrowed until refresh.
// Initial lex/parse result and tokens for this analysis; unknown sources fail closed.
bool editor_source_syntax_errors(const editor_document *d, const source *src);
token *editor_source_tokens(const editor_document *d, const source *src, int *count);
expr *editor_component(const editor_node *n, str name);
expr *editor_value(const expr *component, const field *f);
char *editor_expression(const expr *value); // Arena copy of the exact source expression, or "default".
char *editor_reference_expression(const editor_document *d, int node, const expr *value);
