// The browser host waits for exit(), since returning from main can mean that
// an ordinary game's asynchronous frame loop is still running.
#define main run_tests
#include "../../tests/test_main.c"
#undef main

int main(const int argc, char **argv)
{
    exit(run_tests(argc, argv));
}
