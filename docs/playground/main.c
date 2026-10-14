#include "game.h"
#include "tide/run.h"

extern const tide_layout tide_game_layout;

int main(int argc, char **argv)
{
#ifdef __wasm__
    static tide_host_game game;
    game = tide_host_game_api;
    game.layout = &tide_game_layout;
    tide_host_run_web(&(tide_run_desc){.title = "Tide playground", .argc = argc, .argv = argv}, &game);
#else
    tide_run(&(tide_run_desc){.title = "Tide playground", .argc = argc, .argv = argv});
#endif
}
