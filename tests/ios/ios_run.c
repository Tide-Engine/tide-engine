// Starts a test in the iOS simulator from the folder CTest ran it in (cmake/run_ios.sh): the simulator gives a
// program a working directory of its own.
//
//   tide_ios_run <folder> <program> [arguments...]
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "tide_ios_run <folder> <program> [arguments...]\n");
        return 2;
    }
    if (chdir(argv[1]) != 0) {
        perror(argv[1]);
        return 2;
    }
    execv(argv[2], argv + 2);
    perror(argv[2]);
    return 127;
}
