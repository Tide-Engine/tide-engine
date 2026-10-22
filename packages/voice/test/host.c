#include "game.h"
#include "tide/run.h"

int main(int argc, char **argv)
{
    tide_run(&(tide_run_desc){.title = "Voice test", .argc = argc, .argv = argv});
}
