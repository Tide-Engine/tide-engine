#include "publish.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "build.h"
#include "json.h"
#include "line.h"
#include "sys.h"

#ifndef TIDE_BUILDS_URL
#define TIDE_BUILDS_URL "https://builds.tide-engine.dev"
#endif

// ---------------------------------------------------------------------------
// The archive: a tar (POSIX's ustar) written here rather than by the system's
// tar, so it has nothing of this machine in it: no times, owners or modes, and
// the files in one order. The service names a version by its archive's bytes.

typedef struct bytes {
    unsigned char *data;
    size_t size;
    size_t room;
} bytes;

static void put(bytes *b, const void *data, const size_t n)
{
    if (b->size + n > b->room) {
        b->room = (b->size + n) * 2 + 4096;
        b->data = realloc(b->data, b->room);
        if (!b->data) abort();
    }
    if (n > 0) memcpy(b->data + b->size, data, n);
    b->size += n;
}

static void put_zeros(bytes *b, const size_t n)
{
    static const unsigned char zeros[512];
    for (size_t left = n; left > 0;) {
        const size_t part = left < sizeof zeros ? left : sizeof zeros;
        put(b, zeros, part);
        left -= part;
    }
}

// A number as a header has it: octal digits, then a NUL.
static void octal(unsigned char *field, const size_t size, uint64_t value)
{
    field[size - 1] = '\0';
    for (size_t i = size - 1; i-- > 0;) {
        field[i] = (unsigned char)('0' + (value & 7));
        value >>= 3;
    }
}

// Adds a file. A header holds a path of 100 bytes, or of 255 that a '/'
// splits into 155 and 100: false for one that's neither.
static bool tar_add(bytes *out, const char *path, const unsigned char *data, const size_t size)
{
    unsigned char header[512] = {0};
    const size_t n = strlen(path);
    const char *name = path;
    if (n > 100) {
        // The first '/' that leaves a name of 100 at most
        const char *slash = NULL;
        for (const char *p = path; *p && !slash; p++) {
            if (*p == '/' && n - (size_t)(p - path) - 1 <= 100 && p > path) slash = p;
        }
        if (!slash || (size_t)(slash - path) > 155 || slash[1] == '\0') return false;
        memcpy(header + 345, path, (size_t)(slash - path));
        name = slash + 1;
    }
    memcpy(header, name, strlen(name));
    octal(header + 100, 8, 0644);
    octal(header + 108, 8, 0);
    octal(header + 116, 8, 0);
    octal(header + 124, 12, size);
    octal(header + 136, 12, 0);
    memset(header + 148, ' ', 8); // The checksum counts itself as spaces
    header[156] = '0';            // A file
    memcpy(header + 257, "ustar", 6);
    memcpy(header + 263, "00", 2);
    unsigned sum = 0;
    for (size_t i = 0; i < sizeof header; i++) sum += header[i];
    octal(header + 148, 7, sum);
    header[155] = ' ';
    put(out, header, sizeof header);
    put(out, data, size);
    put_zeros(out, (512 - size % 512) % 512);
    return true;
}

// ---------------------------------------------------------------------------

// A key is the game's id, a dot, and the secret: the id is in the service's
// addresses, so it's lower-case letters, digits, dots and dashes.
static bool split_key(const char *key, char *id, const size_t id_size)
{
    const char *dot = strrchr(key, '.');
    if (!dot || dot == key || dot[1] == '\0' || (size_t)(dot - key) >= id_size) return false;
    for (const char *c = key; c < dot; c++) {
        const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || ((*c == '.' || *c == '-') && c > key);
        if (!ok) return false;
    }
    for (const char *c = dot + 1; *c; c++) {
        const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-'
                     || *c == '_';
        if (!ok) return false;
    }
    memcpy(id, key, (size_t)(dot - key));
    id[dot - key] = '\0';
    return true;
}

static bool ends_with(const char *text, const char *end)
{
    const size_t n = strlen(text);
    const size_t e = strlen(end);
    return n >= e && strcmp(text + n - e, end) == 0;
}

// The status of the last answer in what curl wrote of the response's headers
// (-D): the service's own, after any "100 Continue". 0 if there's none.
static int http_status(const char *headers)
{
    int status = 0;
    for (const char *line = headers; line && *line;) {
        if (strncmp(line, "HTTP/", 5) == 0) {
            const char *space = strchr(line, ' ');
            if (space) status = atoi(space + 1);
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    return status;
}

// Runs curl. In a terminal, a line says what it waits for, which the
// command's last line then takes the place of; what curl says waits for that.
static int upload(const char *const *argv, const char *work, const char *waiting, char **said)
{
    *said = NULL;
    if (!sys_is_terminal()) return sys_run(argv, NULL, false);
    char *log = path_join(work, "curl.log");
    sys_process *curl = sys_start_logged(argv, NULL, log);
    int code = -1;
    for (int frame = 0; curl; frame++) {
        if (sys_wait(curl, frame ? 100 : 0, &code)) break;
        char line[256];
        snprintf(line, sizeof line, "%s %c", waiting, "|/-\\"[frame % 4]);
        line_draw(line);
    }
    *said = sys_read_file(log, NULL);
    free(log);
    return code;
}

int tide_publish(const char *folder_arg)
{
    const char *key = sys_env("TIDE_GAME_KEY");
    char id[128];
    if (!key) {
        fprintf(stderr, "tide: publish needs the game's key, in the environment as TIDE_GAME_KEY\n");
        fprintf(stderr, "  = note: a game's key comes from the build service, when the game is made there\n");
        return 2;
    }
    if (!split_key(key, id, sizeof id)) {
        fprintf(stderr, "tide: what's in TIDE_GAME_KEY isn't a game's key\n");
        fprintf(stderr, "  = note: a key is the game's id, a dot and its secret, as the build service gave it\n");
        return 2;
    }

    int count = 0;
    char **paths = tide_game_sources(folder_arg, &count);
    if (!paths) return 1;
    char *folder = path_absolute(folder_arg);
    bool any = false;
    for (int i = 0; i < count && !any; i++) any = ends_with(paths[i], ".tide");
    if (!any) {
        fprintf(stderr, "tide: there are no .tide files in %s\n", folder);
        fprintf(stderr, "  = note: a game is every .tide file in its folder; add one, like game.tide\n");
        return 1;
    }

    bytes archive = {0};
    for (int i = 0; i < count; i++) {
        char *path = path_join(folder, paths[i]);
        size_t size = 0;
        char *data = sys_read_file(path, &size);
        if (!data) {
            fprintf(stderr, "tide: can't read %s\n", path);
            return 1;
        }
        if (!tar_add(&archive, paths[i], (const unsigned char *)data, size)) {
            fprintf(stderr, "tide: %s can't be published: its path is too long\n", path);
            fprintf(stderr, "  = note: a path from the game's folder is 255 characters at most, with no name in it over 100\n");
            return 1;
        }
        free(data);
        free(path);
    }
    put_zeros(&archive, 1024); // What ends a tar

    char *tide_dir = tide_work_folder(folder);
    char name[64];
    snprintf(name, sizeof name, "publish-%u", (unsigned)sys_pid());
    char *work = path_join(tide_dir, name);
    sys_remove_tree(work);
    char *tar = path_join(work, "source.tar");
    char *request = path_join(work, "request");
    char *headers = path_join(work, "headers");
    char *body = path_join(work, "answer");
    // The key goes to curl in a file: a command line is there for other programs to read
    const size_t auth_size = strlen(key) + 64;
    char *auth = malloc(auth_size);
    if (!auth) abort();
    snprintf(auth, auth_size, "Authorization: Bearer %s\n", key);
    const bool written = sys_mkdirs(work) && sys_write_file(tar, (const char *)archive.data, archive.size)
                      && sys_write_text(request, auth);
    const double megabytes = (double)archive.size / (1024.0 * 1024.0);
    free(auth);
    free(archive.data);
    if (!written) {
        fprintf(stderr, "tide: can't write to %s\n", work);
        sys_remove_tree(work);
        return 1;
    }

    const char *service = sys_env("TIDE_BUILDS_URL");
    if (!service) service = TIDE_BUILDS_URL;
    char url[1024];
    snprintf(url, sizeof url, "%.*s/games/%s/versions", (int)(strlen(service) - (ends_with(service, "/") ? 1 : 0)), service, id);
    char header_arg[1100];
    snprintf(header_arg, sizeof header_arg, "@%s", request);
    const char *const curl[] = {sys_tool("curl"), "-sS", "-m", "600", "-T", tar, "-H", header_arg, "-H",
                                "Content-Type: application/x-tar", "-D", headers, "-o", body, url, NULL};
    char waiting[256];
    snprintf(waiting, sizeof waiting, "Publishing %s (%.1f MB)", id, megabytes);
    char *said = NULL;
    const int code = upload(curl, work, waiting, &said);

    char *head = sys_read_file(headers, NULL);
    size_t answer_size = 0;
    char *answer = sys_read_file(body, &answer_size);
    sys_remove_tree(work);
    const int status = code == 0 && head ? http_status(head) : 0;
    const json *reply = answer ? json_parse(answer, answer_size) : NULL;
    const char *version = json_str(json_get(reply, "version"));
    int result = 1;
    if (status >= 200 && status < 300 && version) {
        char line[512];
        snprintf(line, sizeof line, "Published %s version %s", id, version);
        line_draw(line);
        line_end();
        result = 0;
    } else {
        line_clear();
        if (said) fputs(said, stderr);
        const char *error = json_str(json_get(reply, "error"));
        if (status == 0) fprintf(stderr, "tide: couldn't reach the build service at %s\n", service);
        else if (error) fprintf(stderr, "tide: the build service refused: %s\n", error);
        else fprintf(stderr, "tide: the build service at %s answered %d, which isn't its answer to a version\n", service, status);
    }
    free(said);
    free(head);
    free(answer);
    free(tide_dir);
    return result;
}
