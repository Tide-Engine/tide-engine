#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif
#include "embed.h"
#include "tide/platform.h"
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET editor_socket;
#define BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
typedef int editor_socket;
#define BAD_SOCKET (-1)
#endif

static editor_socket frames = BAD_SOCKET;
static void close_frames(void)
{
    if (frames == BAD_SOCKET) return;
#ifdef _WIN32
    closesocket(frames); WSACleanup();
#else
    close(frames);
#endif
    frames = BAD_SOCKET;
}
static bool send_bytes(const void *data, size_t size)
{
    const char *bytes = data;
    while (size) {
        const int count = send(frames,bytes,size > 65536 ? 65536 : (int)size,0);
        if (count <= 0) {
#ifndef _WIN32
            if (count < 0 && errno == EINTR) continue;
#endif
            return false;
        }
        bytes += count; size -= (size_t)count;
    }
    return true;
}

bool editor_embed_open(uint16_t port, const char *token)
{
    if (!port || !token || strlen(token) != 32 || strspn(token,"0123456789abcdef") != 32) return false;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2,2),&data)) return false;
#else
    signal(SIGPIPE,SIG_IGN);
#endif
    frames = socket(AF_INET,SOCK_STREAM,0);
    if (frames == BAD_SOCKET) return false;
#ifdef _WIN32
    if (!SetHandleInformation((HANDLE)frames,HANDLE_FLAG_INHERIT,0)) { close_frames(); return false; }
#else
    if (fcntl(frames,F_SETFD,FD_CLOEXEC) < 0) { close_frames(); return false; }
#endif
    atexit(close_frames);
    const struct sockaddr_in address = {.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
    if (connect(frames,(const struct sockaddr *)&address,sizeof address)) return false;
    char hello[64]; const int length = snprintf(hello,sizeof hello,"TIDEHELLO 1 %s\n",token);
    return send_bytes(hello,(size_t)length);
}

int editor_embed_input(void)
{
    char line[4096]; unsigned events = 0;
    while (fgets(line,sizeof line,stdin)) {
        const size_t length = strlen(line);
        if (!length || line[length-1] != '\n' || ++events > 256) break;
        line[length-1] = '\0';
        if (!strcmp(line,"QUIT")) return 0;
        if (!strcmp(line,"FRAME 1")) return -1;
        if (strncmp(line,"INPUT ",6) != 0) break;
        const char *input = line+6;
        if (strncmp(input,"mouse ",6) && strncmp(input,"button ",7) && strncmp(input,"key ",4) &&
            strncmp(input,"text ",5) && strncmp(input,"scroll ",7)) break;
        if (!tide_platform_input(input)) break;
    }
    if (feof(stdin)) return 0;
    fputs("tide editor: invalid embedding protocol (expected INPUT event, FRAME 1 or QUIT)\n",stderr);
    return 2;
}

bool editor_embed_frame(bool dirty, uint64_t revision, int errors, int warnings)
{
    int width, height; size_t size;
    const uint8_t *pixels = tide_platform_frame_pixels(&width,&height);
    uint8_t *png = pixels ? tide_png_encode(pixels,width,height,&size) : NULL;
    if (!png) return false;
    char header[160];
    const int length = snprintf(header,sizeof header,"TIDEFRAME 1 %zu %d %llu %d %d\n",size,dirty ? 1 : 0,
        (unsigned long long)revision,errors,warnings);
    const bool ok = send_bytes(header,(size_t)length) && send_bytes(png,size);
    free(png); return ok;
}
