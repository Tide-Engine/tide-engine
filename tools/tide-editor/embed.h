#pragma once
#include <stdbool.h>
#include <stdint.h>

// Private, versioned transport. The IDE accepts one authenticated loopback
// connection for frames; input stays on stdin, logs on stdout/stderr. One FRAME
// request consumes queued input, draws once, and returns one PNG. EOF/QUIT
// closes normally, so the IDE owns the backend lifetime.
bool editor_embed_open(uint16_t port, const char *token);
int editor_embed_input(void); // -1 draw, 0 quit, 2 invalid protocol.
bool editor_embed_frame(bool dirty, uint64_t revision, int errors, int warnings);
