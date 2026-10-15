// The web shell passes its own origin/path through ordinary WASI arguments.
// Native invitations use the published preview until native room links exist.
#include <stddef.h>
#include <string.h>

#if defined(__wasi__)
#include <wasi/api.h>
#endif

const char *InsideOutInviteBase(void)
{
    static const char fallback[] = "https://tide-engine.dev/prototypes/inside-out/";
#if defined(__wasi__)
    static char base[2048];
    static int loaded;
    if (loaded) return base[0] ? base : fallback;
    loaded = 1;
    __wasi_size_t count = 0;
    __wasi_size_t bytes = 0;
    if (__wasi_args_sizes_get(&count, &bytes) != 0 || count > 128 || bytes > 16384) return fallback;
    uint8_t *arguments[129] = {0};
    uint8_t text[16384] = {0};
    if (__wasi_args_get(arguments, text) != 0) return fallback;
    static const char prefix[] = "--inside-out-invite-base=";
    for (size_t i = 0; i < count; i++) {
        const char *const argument = (const char *)arguments[i];
        if (!argument || strncmp(argument, prefix, sizeof prefix - 1) != 0) continue;
        const char *const value = argument + sizeof prefix - 1;
        const size_t length = strlen(value);
        if (length == 0 || length >= sizeof base ||
            (strncmp(value, "https://", 8) != 0 && strncmp(value, "http://", 7) != 0)) continue;
        int valid = 1;
        for (size_t j = 0; j < length; j++) {
            const unsigned char c = (unsigned char)value[j];
            if (c <= 32 || c == 127 || c == '?' || c == '#') valid = 0;
        }
        if (valid) memcpy(base, value, length + 1);
    }
    return base[0] ? base : fallback;
#else
    return fallback;
#endif
}
