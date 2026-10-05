// tidels: the Tide language server. Editors start it and talk to it over
// stdin and stdout (LSP's base protocol: a Content-Length header, then JSON).

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

#include "cdefs.h"
#include "folders.h"
#include "packages.h"
#include "server.h"

// Set by the build: the list of games and their files.
#ifndef TIDE_GAMES_MANIFEST
#define TIDE_GAMES_MANIFEST NULL
#endif

// Set by the build: the repo this server was built from, whose packages/ has
// the packages that come with tide.
#ifndef TIDE_SOURCE_DIR
#define TIDE_SOURCE_DIR NULL
#endif

static void send_message(void *user, const char *message, const size_t len)
{
    (void)user;
    fprintf(stdout, "Content-Length: %zu\r\n\r\n", len);
    fwrite(message, 1, len, stdout);
    fflush(stdout);
}

// tidels reads ahead of the message it handles: an editor sends a change at
// every key and cancels the requests it no longer wants, and analysing a big
// game takes a while. So it takes in everything that has arrived first, and
// then the changes that came together are analysed once, and a request that
// was cancelled meanwhile isn't worked out (lsp_handle_ahead, lsp_settle).
// Nothing is timed: what hasn't arrived yet is simply handled next.

// What's been read of stdin and not yet taken as messages.
static char *input;
static size_t input_len; // The bytes read
static size_t input_cap;
static size_t input_at;  // Where the next message starts

enum { READ_SIZE = 65536 };

// Whether stdin has bytes to read now. Only a pipe can tell on Windows; for
// anything else (a file, in tests) it says yes, as reading a file never waits.
static bool input_arrived(void)
{
#ifdef _WIN32
    DWORD available = 0;
    if (!PeekNamedPipe((HANDLE)_get_osfhandle(0), NULL, 0, NULL, &available, NULL)) return true;
    return available > 0;
#else
    struct pollfd in = {0, POLLIN, 0};
    return poll(&in, 1, 0) != 0;
#endif
}

// Reads more of stdin: with `wait` it waits for some, and without it only
// takes what has arrived. 1 when it read some, 0 when nothing has arrived,
// -1 at the end of input.
static int read_more(const bool wait)
{
    if (!wait && !input_arrived()) return 0;
    if (input_at > 0) { // What's been taken makes room
        memmove(input, input + input_at, input_len - input_at);
        input_len -= input_at;
        input_at = 0;
    }
    if (input_len + READ_SIZE > input_cap) {
        input_cap = (input_len + READ_SIZE) * 2;
        input = realloc(input, input_cap);
        if (!input) abort();
    }
#ifdef _WIN32
    const int n = _read(0, input + input_len, READ_SIZE);
#else
    ssize_t n;
    do n = read(0, input + input_len, READ_SIZE);
    while (n < 0 && errno == EINTR);
#endif
    if (n <= 0) return -1;
    input_len += (size_t)n;
    return 1;
}

// Takes the next message from what's been read: 1 with its body in `out`
// (NUL-terminated, the caller's to free), 0 when it isn't all there yet, -1
// when its headers give no length.
static int take_message(lsp_message *out)
{
    long length = -1;
    size_t at = input_at;
    for (;;) {
        const char *line = input + at;
        const char *end = at < input_len ? memchr(line, '\n', input_len - at) : NULL;
        if (!end) return 0;
        const size_t n = (size_t)(end - line) + 1;
        at += n;
        if (n == 1 || (n == 2 && line[0] == '\r')) break; // The empty line that ends the headers
        if (n > 15 && strncmp(line, "Content-Length:", 15) == 0) length = strtol(line + 15, NULL, 10);
    }
    if (length < 0) return -1;
    if (input_len - at < (size_t)length) return 0;
    char *body = malloc((size_t)length + 1);
    if (!body) abort();
    memcpy(body, input + at, (size_t)length);
    body[length] = '\0';
    input_at = at + (size_t)length;
    *out = (lsp_message){body, (size_t)length};
    return 1;
}

// Installed, tidels is in <install>/bin and the engine headers are in
// <install>/include: the installation's folder, or NULL if they aren't there,
// as in a build of the repo.
static char *installed_root(void)
{
    char *bin = folder_of_program();
    if (!bin) return NULL;
    // "<install>/bin/" -> "<install>"
    char *last = strrchr(bin, '/');
    *last = '\0';
    last = strrchr(bin, '/');
    if (!last) {
        free(bin);
        return NULL;
    }
    *last = '\0';
    const size_t len = strlen(bin) + 32;
    char *probe = malloc(len);
    if (!probe) abort();
    snprintf(probe, len, "%s/include/tide/math.h", bin);
    FILE *f = fopen(probe, "rb");
    free(probe);
    if (!f) {
        free(bin);
        return NULL;
    }
    fclose(f);
    return bin;
}

int main(void)
{
#ifdef _WIN32
    // Content-Length counts bytes: no \n to \r\n translation.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    lsp_server server;
    lsp_init(&server, send_message, NULL);
    server.manifest = TIDE_GAMES_MANIFEST; // Written by tide_add_game (see cmake/Tide.cmake)
    server.defer = true;                   // It reads ahead: see read_more
    // The engine's headers and the packages that come with tide: the
    // installation's, or in a build of the repo, the repo's own.
    char *root = installed_root();
    char *include_dir = NULL;
    if (root) {
        const size_t len = strlen(root) + 16;
        include_dir = malloc(len);
        if (!include_dir) abort();
        snprintf(include_dir, len, "%s/include", root);
        cdefs_set_include_dir(include_dir);
    }
    packages_set_root(root ? root : TIDE_SOURCE_DIR);
    free(root);

    lsp_message *queue = NULL; // The messages that have arrived, from `next` on those not handled yet
    int queued = 0;
    int queue_cap = 0;
    int next = 0;
    bool ended = false;
    while (!server.exited) {
        // Everything that has arrived, without waiting for more
        while (!ended) {
            lsp_message message;
            const int took = take_message(&message);
            if (took > 0) {
                if (queued == queue_cap) {
                    queue_cap = queue_cap ? queue_cap * 2 : 16;
                    queue = realloc(queue, sizeof(lsp_message) * (size_t)queue_cap);
                    if (!queue) abort();
                }
                queue[queued++] = message;
                continue;
            }
            if (took < 0) {
                ended = true;
                break;
            }
            // No whole message in what's been read: more, if it has arrived
            const int more = read_more(false);
            if (more < 0) ended = true;
            if (more <= 0) break;
        }
        if (next == queued) {
            next = queued = 0;
            if (ended) break;
            if (server.unsettled_count > 0) {
                // Input went quiet: what changed is analysed now, and what
                // arrived meanwhile is looked at before waiting again
                lsp_settle(&server);
                continue;
            }
            if (read_more(true) < 0) ended = true;
            continue;
        }
        const lsp_message message = queue[next++];
        lsp_handle_ahead(&server, message.text, message.len, queue + next, queued - next);
        free((void *)message.text);
    }

    for (int i = next; i < queued; i++) free((void *)queue[i].text);
    free(queue);
    free(input);
    lsp_free(&server);
    free(include_dir);
    return server.exited ? server.exit_code : 1;
}
