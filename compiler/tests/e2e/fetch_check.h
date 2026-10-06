// What fetch.tide's Fetches say once they're done, checked: shared by the
// native host (fetch_test.c, against a server fetch.mjs starts) and the page's
// (fetch_web.c, against the server that serves the page), both with
// platform/tests/fetch_routes.mjs answering.
#pragma once

#include <stdio.h>
#include <string.h>

#include "game.h"

static int fetch_failures;

static void fetch_check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) fetch_failures++;
}

static int check_fetches(tide_local *local)
{
    tide_text_use(NULL, &local->heap);
    const tide_heap *heap = &local->heap;
    const char *hello = "hello, tide";
    const int32_t n = tide_list_read_count(heap, local->Fetches.hello);
    bool same = n == (int32_t)strlen(hello);
    for (int32_t i = 0; same && i < n; i++) {
        const uint8_t *b = tide_list_read(heap, local->Fetches.hello, i, 1);
        same = b && *b == (uint8_t)hello[i];
    }
    fetch_check(same, "/hello's bytes come as they are");
    fetch_check(local->Fetches.missing, "a 404 is NotFound");
    fetch_check(local->Fetches.failed, "a 500 is Failed");
    fetch_check(local->Fetches.invalid, "a URL that isn't http:// or https:// is InvalidPath");
    fetch_check(local->Fetches.refused, "a port nobody listens on is Failed");
    // 100 chunks of 1000 bytes, byte i of each i % 251
    fetch_check(local->Fetches.chunked == 100000 && local->Fetches.chunkedSum == 12450600, "a body in chunks comes whole");
    fetch_check(local->Fetches.moved == (int32_t)strlen(hello), "redirects are followed, relative and absolute, to /hello");
    fetch_check(local->Fetches.loop, "a redirect that goes round is Failed");
    fetch_check(local->Fetches.big, "a body the server says is past the limit is Failed");
    fetch_check(local->Fetches.endless, "a body that goes on past the limit is Failed");
    fetch_check(local->Fetches.empty == 0, "a 204 is no bytes and no error");
    fetch_check(local->Fetches.nothing == 0, "a body of 0 bytes is no bytes and no error");
    fetch_check(local->Fetches.frames > 1, "the fetches took frames, which went on meanwhile");
    return fetch_failures;
}
