// Exit while the terminal reader waits for more input, with its pipe still open.
#include "sys.h"
#include <stdio.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define pipe_pair(p) _pipe(p, 4096, _O_BINARY)
#define redirect_input(fd) _dup2(fd, _fileno(stdin))
#define write_input(fd) _write(fd, "r\n", 2)
#else
#include <unistd.h>
#define pipe_pair(p) pipe(p)
#define redirect_input(fd) dup2(fd, STDIN_FILENO)
#define write_input(fd) write(fd, "r\n", 2)
#endif

int main(void)
{
    int input[2];
    if (pipe_pair(input) != 0 || redirect_input(input[0]) < 0 || write_input(input[1]) != 2) return 1;
    sys_read_lines();
    const int64_t deadline = sys_now_ms() + 5000;
    char line[256];
    while (!sys_typed_line(line, sizeof line)) {
        if (sys_now_ms() >= deadline) return 1;
        sys_sleep_ms(1);
    }
    // Give the reader time to block again. Keep the writing end open through exit.
    sys_sleep_ms(100);
    return line[0] == 'r' && line[1] == '\0' ? 0 : 1;
}
