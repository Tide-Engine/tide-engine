#pragma once

// Include after the generated game header. Optional process entry supplied by
// game C: void tide_server_entry(tide_hosting_desc *host).
#include "tide/serve.h"

#ifdef __APPLE__
// Supply a replaceable default so standalone Mach-O servers link without a hook.
__attribute__((weak)) void tide_server_entry(tide_hosting_desc *host)
{
    (void)host;
}
#else
extern void tide_server_entry(tide_hosting_desc *host) __attribute__((weak));
#endif

static inline int tide_server_run(const int argc, char **argv, const char *name)
{
    const tide_serve_desc desc = {.argc = argc, .argv = argv, .game = &tide_game_api, .pack_name = name,
#ifdef TIDE_MAIN_IS_LOCAL
        .main_is_local = true,
#endif
#if defined(TIDE_HAS_ASSETS) && (!defined(TIDE_SERVER_BUILD) || TIDE_SERVER_HAS_ASSETS)
        .assets = tide_assets_load,
#endif
        .entry = tide_server_entry};
    return tide_serve(&desc);
}
