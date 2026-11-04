// The language server for the docs playground: the same tidels, without its
// stdin/stdout transport. The page's worker hands it one JSON-RPC message at a
// time and collects what it sends back, each message ended by a NUL (which
// JSON text cannot contain).

#include <stdlib.h>
#include <string.h>

#include "cdefs.h"
#include "packages.h"
#include "server.h"

static lsp_server server;
static char *input;
static size_t input_cap;
static char *output;
static size_t output_len;
static size_t output_cap;

static void collect(void *user, const char *message, const size_t len)
{
    (void)user;
    if (output_len + len + 1 > output_cap) {
        output_cap = (output_len + len + 1) * 2;
        output = realloc(output, output_cap);
        if (!output) abort();
    }
    memcpy(output + output_len, message, len);
    output_len += len;
    output[output_len++] = '\0';
}

// Room for the next message, `len` bytes, which the worker writes there.
__attribute__((export_name("tidels_input"))) char *tidels_input(const size_t len)
{
    if (len + 1 > input_cap) {
        input_cap = len + 1;
        free(input);
        input = malloc(input_cap);
        if (!input) abort();
    }
    return input;
}

// Handles the message written to tidels_input. What it sends back is in
// tidels_output until the next call.
__attribute__((export_name("tidels_handle"))) void tidels_handle(const size_t len)
{
    static bool started;
    if (!started) {
        started = true;
        lsp_init(&server, collect, NULL);
        cdefs_set_include_dir("/include"); // The engine headers the compiler bundle carries
        packages_set_builtin_folder(NULL); // The playground is one file without packages
    }
    output_len = 0;
    input[len] = '\0';
    lsp_handle(&server, input, len);
}

__attribute__((export_name("tidels_output"))) const char *tidels_output(void)
{
    return output;
}

__attribute__((export_name("tidels_output_len"))) size_t tidels_output_len(void)
{
    return output_len;
}
