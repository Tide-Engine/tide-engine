#pragma once

#include <stdbool.h>
#include <stddef.h>

// The language server protocol, minus the transport: lsp_handle takes one
// JSON-RPC message and sends replies and notifications through `send`.

typedef struct lsp_document {
    char *uri;
    char *text;
    size_t len;
} lsp_document;

typedef void (*lsp_send_fn)(void *user, const char *message, size_t len);

// A message that has arrived, for a host that reads ahead of the one it
// handles (lsp_handle_ahead).
typedef struct lsp_message {
    const char *text;
    size_t len;
} lsp_message;

typedef struct lsp_server {
    lsp_send_fn send;
    void *user;
    // Which files make up each game: lines of "<game>\t<path>", written by
    // tide_add_game. Open folders can have their own (see game_of). May be NULL.
    const char *manifest;
    char **roots; // The folders open in the editor, ending in '/'
    int root_count;
    lsp_document *docs;
    int doc_count;
    int doc_cap;
    unsigned changes;           // Bumped whenever a document changes
    unsigned analyzed_changes;  // `changes` when the last analysis ran
    bool analyzed;
    char **kept;                // Strings the analysis points to: texts read from disk, URIs, paths
    int kept_count;
    bool watch_files;           // The editor watches the files we ask it to (dynamic registration)
    bool relative_patterns;     // ...and takes patterns relative to a folder, for one outside the open ones
    // The editor asks before it renames files (workspace/willRenameFiles), and takes the filters for which
    // ones as a registration (dynamic) or in the initialize result (static, every file). Assets' paths
    // are rewritten through it.
    bool rename_dynamic;
    char *rename_filters;       // The filters registered, as JSON, to register again only when they change
    char *asset_watchers;       // The watchers registered for the assets' files, the same way
    struct lsp_asset_registration *asset_registrations; // Heap-owned snapshots of open games' asset paths
    int asset_registration_count;
    struct lsp_asset_context *asset_contexts; // Active keys/paths, reused until the documents or workspace change
    int asset_context_count;
    int *asset_context_documents; // Each open document's selected context, in docs order
    unsigned asset_context_changes;
    struct lsp_asset_context *analyzed_context; // Heap-owned key of the retained analysis
    bool shutdown;
    bool exited;
    int exit_code;
    // A big game takes a while to analyse, and an editor sends a change at
    // every key. A host that reads ahead sets `defer`: a document's change
    // then waits to be analysed, until a message that isn't a change is
    // handled or the host calls lsp_settle, so the changes that arrived
    // meanwhile are analysed once, as the last of them left the text.
    bool defer;
    char **unsettled;           // The documents changed since their diagnostics last went out
    int unsettled_count;
} lsp_server;

void lsp_init(lsp_server *s, lsp_send_fn send, void *user);
void lsp_handle(lsp_server *s, const char *message, size_t len);
// lsp_handle, for a host that reads ahead: `later` is what has arrived after
// this message, in order. A request that one of them cancels
// ($/cancelRequest) is answered as cancelled, without its work.
void lsp_handle_ahead(lsp_server *s, const char *message, size_t len, const lsp_message *later, int later_count);
// Analyses the documents whose changes waited (`defer`), and sends their
// games' diagnostics. The host calls it once nothing more has arrived.
void lsp_settle(lsp_server *s);
void lsp_free(lsp_server *s);
