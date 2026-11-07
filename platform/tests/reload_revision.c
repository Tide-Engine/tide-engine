// The production web save/resume path, across fresh WASM instances of this game.
#include <inttypes.h>
#include <stdio.h>

#include "game.h"
#include "tide/run.h"
#include "tide_web.h"

void RevisionSeen(const int64_t previous, const int64_t current, const int64_t alias, const int32_t bakes)
{
    static bool reported;
    if (reported) return;
    reported = true;
    char script[256];
    snprintf(script, sizeof script, "globalThis.revisionSeen(%" PRId64 ",%" PRId64 ",%" PRId64 ",%d)",
             previous, current, alias, bakes);
    tide_web_eval(script);
}

int main(int argc, char **argv)
{
    extern const tide_layout tide_game_layout;
    const tide_host_game game = tide_host_game_api;
    tide_host_game reloading = game;
    reloading.layout = &tide_game_layout;
    tide_host_run_web(&(tide_run_desc){.argc = argc, .argv = argv}, &reloading);
}
