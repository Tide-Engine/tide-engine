#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A small web server on this machine only (127.0.0.1), for tide run --web: it
// answers GET requests with what `answer` gives, and the page's own POSTs (a
// dev build's checkpoint, saved to the game's folder), and nothing else.

typedef struct serve_reply {
    int status;       // 200, or 404
    const char *type; // Its Content-Type
    const void *body;
    size_t size;
} serve_reply;

// Fills `reply` for a request of `path`, like "/" or "/build". A query (`?...`) stays on the path only for a request
// with an X-Tide header, which only the page itself sends. `body` is a POST's (`size` bytes), which only the page
// sends too; NULL for a GET.
typedef void (*serve_fn)(void *user, const char *path, const uint8_t *body, size_t size, serve_reply *reply);

// The most a POST's body has: a big game's checkpoint
#define SERVE_BODY_MOST (1024u * 1024u * 1024u)

typedef struct serve serve;

// Listens on a free port of 127.0.0.1, which goes in `port`. NULL if it can't.
serve *serve_open(uint16_t *port);

// Waits up to `ms` milliseconds for requests, and answers each one that's
// whole.
void serve_poll(serve *s, int ms, serve_fn answer, void *user);
