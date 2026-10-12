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
} editor_node;

typedef struct editor_document {
    char *path, *project, *text, *saved;
    char *draft_path, *recovery, *recovery_base;
    char message[1024];
    bool conflict, syntax_errors;
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
bool editor_remove_node(editor_document *d, int node);
expr *editor_component(const editor_node *n, str name);
expr *editor_value(const expr *component, const field *f);
char *editor_expression(const expr *value); // Arena copy of the exact source expression, or "default".
char *editor_reference_expression(const editor_document *d, int node, const expr *value);
