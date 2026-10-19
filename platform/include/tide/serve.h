#pragma once

#include "tide/hosting.h"

typedef struct tide_serve_desc {
    int argc;
    char **argv;
    const tide_game *game;
    const char *pack_name; // Basename beside the executable, without .pack.
    bool main_is_local;
    bool (*assets)(const uint8_t *bytes, size_t size);
    // Process entry point: configure policy/authentication and the per-match
    // start callback here. Host defaults and command-line limits are supplied.
    void (*entry)(tide_hosting_desc *host);
} tide_serve_desc;

// Real-time headless service; no window, graphics context, audio or local state.
// Returns on SIGINT/SIGTERM or --frames. Desktop only.
int tide_serve(const tide_serve_desc *desc);
