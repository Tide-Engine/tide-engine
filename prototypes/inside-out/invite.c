// The web shell passes its origin/path, optional material review choice and
// playtest switch through ordinary WASI arguments.
// Native invitations use the published preview until native room links exist.
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(__wasi__)
#include <wasi/api.h>

static char invite_base[2048];
static int preview_look = -1;
static int playtest;
static int arguments_loaded;

static void read_arguments(void)
{
    if (arguments_loaded) return;
    arguments_loaded = 1;
    __wasi_size_t count = 0;
    __wasi_size_t bytes = 0;
    if (__wasi_args_sizes_get(&count, &bytes) != 0 || count > 128 || bytes > 16384) return;
    uint8_t *arguments[129] = {0};
    uint8_t text[16384] = {0};
    if (__wasi_args_get(arguments, text) != 0) return;
    static const char prefix[] = "--inside-out-invite-base=";
    static const char look_prefix[] = "--inside-out-look=";
    for (size_t i = 0; i < count; i++) {
        const char *const argument = (const char *)arguments[i];
        if (!argument) continue;
        if (strcmp(argument, "--inside-out-playtest") == 0) {
            playtest = 1;
            continue;
        }
        if (strncmp(argument, look_prefix, sizeof look_prefix - 1) == 0) {
            const char *const value = argument + sizeof look_prefix - 1;
            if (value[0] >= '0' && value[0] <= '4' && value[1] == '\0') preview_look = value[0] - '0';
            continue;
        }
        if (strncmp(argument, prefix, sizeof prefix - 1) != 0) continue;
        const char *const value = argument + sizeof prefix - 1;
        const size_t length = strlen(value);
        if (length == 0 || length >= sizeof invite_base ||
            (strncmp(value, "https://", 8) != 0 && strncmp(value, "http://", 7) != 0)) continue;
        int valid = 1;
        for (size_t j = 0; j < length; j++) {
            const unsigned char c = (unsigned char)value[j];
            if (c <= 32 || c == 127 || c == '?' || c == '#') valid = 0;
        }
        if (valid) memcpy(invite_base, value, length + 1);
    }
}
#endif

const char *InsideOutInviteBase(void)
{
    static const char fallback[] = "https://tide-engine.dev/prototypes/inside-out/";
#if defined(__wasi__)
    read_arguments();
    return invite_base[0] ? invite_base : fallback;
#else
    return fallback;
#endif
}

int InsideOutPreviewLook(void)
{
#if defined(__wasi__)
    read_arguments();
    return preview_look;
#else
    return -1;
#endif
}

// Natively, a nonempty INSIDE_OUT_PLAYTEST other than "0" asks for it.
int InsideOutPlaytest(void)
{
#if defined(__wasi__)
    read_arguments();
    return playtest;
#else
    const char *const value = getenv("INSIDE_OUT_PLAYTEST");
    return value && value[0] && strcmp(value, "0") != 0;
#endif
}
