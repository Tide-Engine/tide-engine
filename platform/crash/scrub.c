// What tide-crash masks in a report before it leaves the player's machine
// (scrub.h). Everything here reads bytes as units, a byte each for UTF-8 and
// two for UTF-16 (the little-endian strings of Windows, which a dump holds),
// and compares letters of ASCII without case. Masking writes '*' over each
// unit it masks, so nothing moves.
#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE // gethostname under strict C
#endif

#include "scrub.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct units {
    unsigned char *bytes;
    size_t count; // Units
    int width;    // 1 or 2 bytes a unit
} units;

static uint32_t at(const units *u, const size_t i)
{
    if (u->width == 1) return u->bytes[i];
    return (uint32_t)u->bytes[i * 2] | (uint32_t)u->bytes[i * 2 + 1] << 8;
}

static void mask(const units *u, const size_t i)
{
    if (u->width == 1) u->bytes[i] = '*';
    else {
        u->bytes[i * 2] = '*';
        u->bytes[i * 2 + 1] = 0;
    }
}

static uint32_t lower(const uint32_t c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static bool digit(const uint32_t c)
{
    return c >= '0' && c <= '9';
}

static bool hex(const uint32_t c)
{
    return digit(c) || (lower(c) >= 'a' && lower(c) <= 'f');
}

// A letter or digit of a word: ASCII's. Not anything past it: a name next to
// a letter of another language is masked, as one next to a dump's binary,
// which is anything, is (a string cut short in a stack's memory)
static bool word(const uint32_t c)
{
    return digit(c) || (lower(c) >= 'a' && lower(c) <= 'z') || c == '_';
}

// Whether `text` (ASCII) is at unit i, without case
static bool matches(const units *u, const size_t i, const char *text)
{
    const size_t n = strlen(text);
    if (i + n > u->count) return false;
    for (size_t k = 0; k < n; k++) if (lower(at(u, i + k)) != (uint32_t)text[k]) return false;
    return true;
}

static bool separator(const uint32_t c)
{
    return c == '/' || c == '\\';
}

// ---------------------------------------------------------------------------
// The folder after Users/ or home/: the user's name on every system

// Where a folder's name in a path ends. Spaces don't end it ("John Smith").
static bool ends_folder(const uint32_t c)
{
    return c < 0x20 || separator(c) || strchr("\"'<>|:*?)],;", (int)c) != NULL;
}

static void scrub_paths(const units *u)
{
    static const char *const homes[] = {"users", "home", "documents and settings"};
    static const char *const shared[] = {"public", "default", "default user", "all users", "shared"};
    for (size_t i = 0; i + 1 < u->count; i++) {
        if (!separator(at(u, i))) continue;
        for (size_t h = 0; h < sizeof homes / sizeof homes[0]; h++) {
            const size_t n = strlen(homes[h]);
            if (!matches(u, i + 1, homes[h]) || i + 1 + n >= u->count || !separator(at(u, i + 1 + n))) continue;
            const size_t start = i + 2 + n;
            size_t end = start;
            while (end < u->count && end - start < 128 && !ends_folder(at(u, end))) end++;
            bool common = end == start;
            for (size_t s = 0; !common && s < sizeof shared / sizeof shared[0]; s++) {
                common = strlen(shared[s]) == end - start && matches(u, start, shared[s]);
            }
            if (!common) for (size_t k = start; k < end; k++) mask(u, k);
            i = end - 1;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Names, wherever they're a whole word

static bool common_name(const char *name)
{
    static const char *const words[] = {"user", "users", "admin", "administrator", "root", "owner", "guest", "home",
                                        "desktop", "default", "player", "test", "localhost", "computer", "pc"};
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        const char *a = name, *b = words[i];
        while (*a && *b && lower((unsigned char)*a) == (uint32_t)*b) a++, b++;
        if (!*a && !*b) return true;
    }
    return false;
}

// `name`'s units in `u`'s width: its bytes, or UTF-16 from its UTF-8
static size_t name_units(const char *name, const int width, uint32_t *out, const size_t most)
{
    size_t n = 0;
    const unsigned char *c = (const unsigned char *)name;
    while (*c && n < most) {
        if (width == 1) {
            out[n++] = *c++;
            continue;
        }
        uint32_t point = *c;
        int more = point >= 0xf0 ? 3 : point >= 0xe0 ? 2 : point >= 0xc0 ? 1 : 0;
        point &= more == 3 ? 0x07u : more == 2 ? 0x0fu : more == 1 ? 0x1fu : 0x7fu;
        c++;
        while (more-- && (*c & 0xc0) == 0x80) point = point << 6 | (*c++ & 0x3fu);
        if (point >= 0x10000 && n + 1 < most) {
            point -= 0x10000;
            out[n++] = 0xd800 + (point >> 10);
            out[n++] = 0xdc00 + (point & 0x3ffu);
        } else if (point < 0x10000) {
            out[n++] = point;
        }
    }
    return n;
}

static void scrub_name(const units *u, const char *name)
{
    uint32_t want[256];
    const size_t n = name_units(name, u->width, want, 256);
    if (n < 3 || common_name(name)) return;
    for (size_t i = 0; i + n <= u->count; i++) {
        if (i > 0 && word(at(u, i - 1))) continue;
        if (i + n < u->count && word(at(u, i + n))) continue;
        size_t k = 0;
        while (k < n && lower(at(u, i + k)) == lower(want[k])) k++;
        if (k < n) continue;
        for (k = 0; k < n; k++) mask(u, i + k);
        i += n - 1;
    }
}

// ---------------------------------------------------------------------------
// Addresses: IP, and email

// An IPv4 address at i (1.2.3.4, each part at most 255), and where it ends.
// Not one inside a longer run of numbers and dots (a version, 1.2.3.4.5).
static bool ipv4(const units *u, const size_t i, size_t *end, uint32_t parts[4])
{
    if (i > 0 && (word(at(u, i - 1)) || at(u, i - 1) == '.')) return false;
    size_t j = i;
    for (int p = 0; p < 4; p++) {
        if (p > 0) {
            if (j >= u->count || at(u, j) != '.') return false;
            j++;
        }
        uint32_t value = 0;
        int count = 0;
        while (j < u->count && digit(at(u, j)) && count < 4) value = value * 10 + at(u, j++) - '0', count++;
        if (count == 0 || count > 3 || value > 255) return false;
        parts[p] = value;
    }
    if (j < u->count && word(at(u, j))) return false;
    if (j + 1 < u->count && at(u, j) == '.' && digit(at(u, j + 1))) return false;
    *end = j;
    return true;
}

static void scrub_ipv4(const units *u)
{
    for (size_t i = 0; i < u->count; i++) {
        size_t end;
        uint32_t parts[4];
        if (!digit(at(u, i)) || !ipv4(u, i, &end, parts)) continue;
        const bool own = parts[0] == 127 || (parts[0] == 0 && parts[1] == 0 && parts[2] == 0 && parts[3] == 0)
                         || (parts[0] == 255 && parts[1] == 255 && parts[2] == 255 && parts[3] == 255);
        if (!own) for (size_t k = i; k < end; k++) if (digit(at(u, k))) mask(u, k);
        i = end - 1;
    }
}

// An IPv6 address: hex groups of at most 4 between colons, with "::" or all 8
// groups (and maybe IPv4 at its end). Times (12:34:56) and C++'s names
// (std::vector) aren't.
static void scrub_ipv6(const units *u)
{
    for (size_t i = 0; i < u->count; i++) {
        const uint32_t c = at(u, i);
        if (!hex(c) && c != ':') continue;
        if (i > 0 && (word(at(u, i - 1)) || at(u, i - 1) == ':' || at(u, i - 1) == '.')) continue;
        size_t j = i;
        int colons = 0, group = 0, digits = 0, doubles = 0;
        bool valid = true;
        while (j < u->count && (hex(at(u, j)) || at(u, j) == ':' || at(u, j) == '.')) {
            const uint32_t d = at(u, j);
            if (d == ':') {
                colons++;
                if (j > i && at(u, j - 1) == ':') doubles++;
                group = 0;
            } else if (d == '.') {
                group = 0; // Its IPv4 end
            } else {
                if (++group > 4 && colons) valid = false;
                if (d != '0') digits++;
            }
            j++;
        }
        if (j < u->count && word(at(u, j))) valid = false;
        const size_t length = j - i;
        const bool loopback = length == 3 && matches(u, i, "::1");
        if (valid && colons >= 2 && colons <= 7 && (doubles == 1 || (doubles == 0 && colons == 7)) && digits > 0 && !loopback) {
            for (size_t k = i; k < j; k++) if (hex(at(u, k)) || digit(at(u, k))) mask(u, k);
        }
        i = j > i ? j - 1 : i;
    }
}

static bool email_local(const uint32_t c)
{
    return digit(c) || (lower(c) >= 'a' && lower(c) <= 'z') || strchr("._%+-", (int)c) != NULL;
}

static bool email_domain(const uint32_t c)
{
    return digit(c) || (lower(c) >= 'a' && lower(c) <= 'z') || c == '.' || c == '-';
}

static void scrub_email(const units *u)
{
    for (size_t i = 1; i + 1 < u->count; i++) {
        if (at(u, i) != '@') continue;
        size_t start = i;
        while (start > 0 && email_local(at(u, start - 1)) && i - start < 64) start--;
        size_t end = i + 1;
        while (end < u->count && email_domain(at(u, end)) && end - i < 256) end++;
        while (end > i + 1 && at(u, end - 1) == '.') end--; // A sentence's full stop
        // The domain's last label: 2 letters or more, after a dot
        size_t dot = end;
        while (dot > i + 1 && at(u, dot - 1) != '.') dot--;
        bool letters = dot > i + 2 && end - dot >= 2;
        for (size_t k = dot; letters && k < end; k++) letters = lower(at(u, k)) >= 'a' && lower(at(u, k)) <= 'z';
        if (start == i || !letters) continue;
        for (size_t k = start; k < end; k++) if (k != i) mask(u, k);
        i = end;
    }
}

// ---------------------------------------------------------------------------

static void scrub_units(const units *u, const scrub_names *names, const bool addresses)
{
    scrub_paths(u);
    for (int n = 0; names && n < names->count; n++) scrub_name(u, names->name[n]);
    if (!addresses) return;
    scrub_ipv4(u);
    scrub_ipv6(u);
    scrub_email(u);
}

void scrub_names_add(scrub_names *names, const char *name)
{
    if (!name || !*name || names->count >= SCRUB_NAMES || strlen(name) >= sizeof names->name[0]) return;
    for (int i = 0; i < names->count; i++) if (strcmp(names->name[i], name) == 0) return;
    snprintf(names->name[names->count++], sizeof names->name[0], "%s", name);
}

#ifdef _WIN32
static void add_variable(scrub_names *names, const wchar_t *variable, const bool last_folder)
{
    wchar_t value[512];
    const DWORD length = GetEnvironmentVariableW(variable, value, 512);
    if (!length || length >= 512) return;
    const wchar_t *from = value;
    if (last_folder) for (const wchar_t *c = value; *c; c++) if (*c == L'\\' || *c == L'/') from = c + 1;
    char name[128];
    if (WideCharToMultiByte(CP_UTF8, 0, from, -1, name, sizeof name, NULL, NULL)) scrub_names_add(names, name);
}
#endif

void scrub_names_here(scrub_names *names)
{
    memset(names, 0, sizeof *names);
#ifdef _WIN32
    add_variable(names, L"USERNAME", false);
    add_variable(names, L"USERPROFILE", true);
    add_variable(names, L"COMPUTERNAME", false);
#else
    scrub_names_add(names, getenv("USER"));
    const char *home = getenv("HOME");
    const char *last = home ? strrchr(home, '/') : NULL;
    if (last) scrub_names_add(names, last + 1);
    char host[128];
    if (gethostname(host, sizeof host) == 0) {
        host[sizeof host - 1] = '\0';
        char *dot = strchr(host, '.'); // Its domain isn't the machine's
        if (dot) *dot = '\0';
        scrub_names_add(names, host);
    }
#endif
}

void scrub_text(char *bytes, const size_t size, const scrub_names *names)
{
    const units u = {(unsigned char *)bytes, size, 1};
    scrub_units(&u, names, true);
}

static uint32_t read32(const unsigned char *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static uint64_t read64(const unsigned char *b)
{
    return (uint64_t)read32(b) | (uint64_t)read32(b + 4) << 32;
}

// `size` bytes at `bytes` in bytes, and in UTF-16 from each of the first two:
// a dump keeps nothing of where its strings were aligned
static void scrub_all(unsigned char *bytes, const size_t size, const scrub_names *names, const bool addresses)
{
    const units narrow = {bytes, size, 1};
    scrub_units(&narrow, names, addresses);
    for (size_t from = 0; from < 2 && from < size; from++) {
        const units wide = {bytes + from, (size - from) / 2, 2};
        scrub_units(&wide, names, addresses);
    }
}

// The memory a dump holds, `size` bytes at `offset`: a text's rules
static void scrub_memory(unsigned char *bytes, const size_t total, const uint64_t offset, const uint64_t size,
                         const scrub_names *names)
{
    if (offset >= total || size > total - offset) return;
    scrub_all(bytes + offset, (size_t)size, names, true);
}

void scrub_dump(unsigned char *bytes, const size_t size, const scrub_names *names)
{
    // Paths and names through all of it: a module's path is a string of its
    // own, outside the memory it holds
    scrub_all(bytes, size, names, false);
    // MINIDUMP_HEADER: "MDMP", its version, the streams' count and where their directory is
    if (size < 32 || memcmp(bytes, "MDMP", 4) != 0) return;
    const uint32_t streams = read32(bytes + 8), directory = read32(bytes + 12);
    if (directory > size || streams > (size - directory) / 12) return;
    for (uint32_t s = 0; s < streams; s++) {
        const unsigned char *entry = bytes + directory + s * 12u;
        const uint32_t type = read32(entry), length = read32(entry + 4), rva = read32(entry + 8);
        if (rva > size || length > size - rva) continue;
        if (type == 5 && length >= 4) { // MemoryListStream: a count, then 16 bytes a range
            const uint32_t count = read32(bytes + rva);
            for (uint32_t i = 0; i < count && 4u + (uint64_t)(i + 1) * 16u <= length; i++) {
                const unsigned char *range = bytes + rva + 4 + i * 16u;
                scrub_memory(bytes, size, read32(range + 12), read32(range + 8), names);
            }
        } else if (type == 9 && length >= 16) { // Memory64ListStream: a count, where the memory starts, 16 bytes a range
            const uint64_t count = read64(bytes + rva);
            uint64_t offset = read64(bytes + rva + 8);
            for (uint64_t i = 0; i < count && 16u + (i + 1) * 16u <= length; i++) {
                const uint64_t range_size = read64(bytes + rva + 16 + i * 16u + 8);
                scrub_memory(bytes, size, offset, range_size, names);
                offset += range_size;
            }
        }
    }
}
