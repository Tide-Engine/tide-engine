#include "game.h"
#include "tide/hosting.h"

#include <stdlib.h>

static tide_start selected = {.scene = 0, .value.Arena = {.amount = 42}};

static bool start_match(void *user, const tide_hosting_request *request, tide_server_desc *match)
{
    (void)user;
    (void)request;
    match->start = &selected;
    return true;
}

static void ready(void *user, tide_hosting *host)
{
    (void)user;
    const tide_server_desc desc = {.start = &selected};
    const uint64_t a = tide_hosting_start(host, &desc);
    selected.value.Arena.amount = 7;
    const uint64_t b = tide_hosting_start(host, &desc);
    const tide_world *wa = tide_hosting_world(host, a), *wb = tide_hosting_world(host, b);
    if (!wa || !wb || a == b || wa->State.amount != 42 || wb->State.amount != 7) abort();
}

void tide_server_entry(tide_hosting_desc *host)
{
    host->start = start_match;
    host->ready = ready;
}
