// The files `asset` declarations resolve to (see asset_files.h): found from
// the declaring file, read once each, hashed and decoded, with what's wrong
// said at the declaration in words that say what to write.

#include "asset_files.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "folders.h"
#include "sha256.h"
#include "tide/sha256.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

// ---------------------------------------------------------------------------
// Files read, kept by path: a server that checks a game at every keystroke
// reads and decodes each once, until its size or time changes.

typedef struct read_file {
    char *path;
    unsigned long long size, modified;
    uint8_t *data;
    size_t len;
    uint8_t hash[32];
    bool decoded;          // Tried as a PNG...
    tide_png_image image;  // ...and what it gave (pixels NULL when it isn't one)
    bool utf8_known, utf8; // Whether its bytes are UTF-8 text
} read_file;

static struct {
    read_file *items;
    int count, cap;
} files;

static char *copy_cstr(const char *s)
{
    char *out = malloc(strlen(s) + 1);
    if (!out) abort();
    strcpy(out, s);
    return out;
}

static void forget_file(read_file *f)
{
    free(f->path);
    free(f->data);
    free(f->image.pixels);
    memset(f, 0, sizeof *f);
}

void asset_files_forget(void)
{
    for (int i = 0; i < files.count; i++) forget_file(&files.items[i]);
    free(files.items);
    files.items = NULL;
    files.count = files.cap = 0;
}

// The file at `path`, read: NULL when there's none. Its bytes stay until the
// file changes or asset_files_forget.
static read_file *read_at(const char *path)
{
    unsigned long long size = 0, modified = 0;
    if (!folder_file_info(path, &size, &modified)) return NULL;
    read_file *f = NULL;
    for (int i = 0; i < files.count && !f; i++) {
        if (asset_path_same(files.items[i].path, path)) f = &files.items[i];
    }
    if (f && f->size == size && f->modified == modified) return f;
    if (!f) {
        if (files.count == files.cap) {
            files.cap = files.cap ? files.cap * 2 : 16;
            files.items = realloc(files.items, sizeof *files.items * (size_t)files.cap);
            if (!files.items) abort();
        }
        f = &files.items[files.count++];
        memset(f, 0, sizeof *f);
        f->path = copy_cstr(path);
    } else {
        char *kept = f->path;
        f->path = NULL;
        forget_file(f);
        f->path = kept;
    }
    FILE *in = fopen(path, "rb");
    if (!in) return NULL;
    f->data = malloc(size > 0 ? (size_t)size : 1u);
    if (!f->data) abort();
    f->len = fread(f->data, 1, (size_t)size, in);
    fclose(in);
    f->size = size;
    f->modified = modified;
    tide_sha256_of(f->data, f->len, f->hash);
    return f;
}

static bool is_utf8(const uint8_t *s, const size_t n)
{
    for (size_t i = 0; i < n;) {
        const uint8_t c = s[i];
        size_t more;
        uint32_t least;
        if (c < 0x80u) {
            i++;
            continue;
        } else if ((c & 0xE0u) == 0xC0u) {
            more = 1;
            least = 0x80u;
        } else if ((c & 0xF0u) == 0xE0u) {
            more = 2;
            least = 0x800u;
        } else if ((c & 0xF8u) == 0xF0u) {
            more = 3;
            least = 0x10000u;
        } else {
            return false;
        }
        if (i + more >= n) return false;
        uint32_t code = c & (0x3Fu >> more);
        for (size_t k = 1; k <= more; k++) {
            if (i + k >= n || (s[i + k] & 0xC0u) != 0x80u) return false;
            code = code << 6 | (s[i + k] & 0x3Fu);
        }
        if (code < least || code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu)) return false;
        i += more + 1;
    }
    return true;
}

// ---------------------------------------------------------------------------
// URLs: fetched with curl, as tide fetches packages, and kept in the game's
// .tide/ by URL with the server's ETag, so a build asks whether the file
// changed and goes on from the cache when the server can't be reached. Each
// URL is asked about once in a process: at each build, and at each tide run's
// start.

typedef struct fetched {
    char *url;
    const char *path; // The cache's file, or a temporary one; NULL when it couldn't be had
    char failed[256]; // Why, when it couldn't
} fetched;

static struct {
    fetched *items;
    int count, cap;
} fetches;

static void hex_of(const uint8_t hash[32], char out[65])
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = digits[hash[i] >> 4];
        out[2 * i + 1] = digits[hash[i] & 15];
    }
    out[64] = '\0';
}

// A line of `headers` (an HTTP response's) that starts with `name`, its value
// trimmed, into `out`.
static bool header_value(const char *headers, const char *name, char *out, const size_t size)
{
    const size_t n = strlen(name);
    for (const char *line = headers; *line; ) {
        const char *end = strchr(line, '\n');
        const size_t len = end ? (size_t)(end - line) : strlen(line);
        if (len > n && line[n] == ':') {
            bool same = true;
            for (size_t i = 0; i < n && same; i++) same = tolower((unsigned char)line[i]) == tolower((unsigned char)name[i]);
            if (same) {
                const char *v = line + n + 1;
                while (*v == ' ') v++;
                size_t vlen = (size_t)(line + len - v);
                while (vlen && (v[vlen - 1] == '\r' || v[vlen - 1] == ' ')) vlen--;
                if (vlen >= size) vlen = size - 1;
                memcpy(out, v, vlen);
                out[vlen] = '\0';
                return true;
            }
        }
        if (!end) break;
        line = end + 1;
    }
    return false;
}

// The status of the last response in `headers` (curl writes one block per
// response, redirects included): 0 for none.
static int last_status(const char *headers)
{
    int status = 0;
    for (const char *line = headers; *line; ) {
        if (strncmp(line, "HTTP/", 5) == 0) {
            const char *space = strchr(line, ' ');
            if (space) status = atoi(space + 1);
        }
        const char *end = strchr(line, '\n');
        if (!end) break;
        line = end + 1;
    }
    return status;
}

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)(n > 0 ? n : 0) + 1);
    if (!text) abort();
    const size_t read = n > 0 ? fread(text, 1, (size_t)n, f) : 0;
    text[read] = '\0';
    fclose(f);
    return text;
}

static bool write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fputs(text, f);
    return fclose(f) == 0;
}

static bool safe_for_shell(const char *s)
{
    for (; *s; s++) {
        if (*s == '"' || *s == '\n' || *s == '\r' || *s == '%' || *s == '`' || *s == '$' || *s == '\\') return false;
    }
    return true;
}

// Runs curl for `url`, into `out`, with the response's headers in `headers`:
// what curl said (its exit code), -1 when it couldn't run.
static int run_curl(const char *url, const char *out, const char *headers, const char *etag)
{
    char command[4096];
    const int n = snprintf(command, sizeof command,
                           "curl -fsSL --retry 1 -m 60 -D \"%s\" -o \"%s\"%s%s%s \"%s\" 2>&1", headers, out,
                           etag ? " -H \"If-None-Match: " : "", etag ? etag : "", etag ? "\"" : "", url);
    if (n <= 0 || (size_t)n >= sizeof command) return -1;
    FILE *p = popen(command, "r");
    if (!p) return -1;
    char line[256];
    while (fgets(line, sizeof line, p)) { } // What it said goes unread: a failure is reported in Tide's own words
    const int code = pclose(p);
#ifdef _WIN32
    return code;
#else
    return code == -1 ? -1 : (code >> 8) & 0xFF;
#endif
}

// `url`'s bytes, fetched once in this process and kept in `game_folder`'s
// .tide/assets/urls/ by URL and ETag: the file they're in, or NULL with why.
static const fetched *fetch_url(const char *url, const char *game_folder)
{
    for (int i = 0; i < fetches.count; i++) {
        if (strcmp(fetches.items[i].url, url) == 0) return &fetches.items[i];
    }
    if (fetches.count == fetches.cap) {
        fetches.cap = fetches.cap ? fetches.cap * 2 : 8;
        fetches.items = realloc(fetches.items, sizeof *fetches.items * (size_t)fetches.cap);
        if (!fetches.items) abort();
    }
    fetched *f = &fetches.items[fetches.count++];
    memset(f, 0, sizeof *f);
    f->url = copy_cstr(url);
    if (!safe_for_shell(url)) {
        snprintf(f->failed, sizeof f->failed, "its address has a character tide can't pass on (a quote, a backslash, a dollar sign)");
        return f;
    }
    // Where it's kept: by the URL's hash, with a .etag beside it
    uint8_t hash[32];
    tide_sha256_of(url, strlen(url), hash);
    char hex[65];
    hex_of(hash, hex);
    char dir[2048];
    if (game_folder) snprintf(dir, sizeof dir, "%s/.tide/assets/urls", game_folder);
    else snprintf(dir, sizeof dir, "%s/tide-assets", getenv("TMPDIR") ? getenv("TMPDIR") : getenv("TEMP") ? getenv("TEMP") : "/tmp");
    if (!safe_for_shell(dir)) {
        snprintf(f->failed, sizeof f->failed, "the folder it would be cached in has a character tide can't pass on");
        return f;
    }
    folder_make(dir);
    char file[2200], etag_file[2200], headers_file[2200], fresh[2200];
    snprintf(file, sizeof file, "%s/%s", dir, hex);
    snprintf(etag_file, sizeof etag_file, "%s/%s.etag", dir, hex);
    snprintf(headers_file, sizeof headers_file, "%s/%s.headers", dir, hex);
    snprintf(fresh, sizeof fresh, "%s/%s.new", dir, hex);
    const bool cached = folder_file_info(file, NULL, NULL);
    char *etag = cached ? read_text(etag_file) : NULL;
    if (etag && !*etag) {
        free(etag);
        etag = NULL;
    }
    if (etag && !safe_for_shell(etag)) etag[0] = '\0';
    remove(headers_file);
    remove(fresh);
    const int code = run_curl(url, fresh, headers_file, etag && *etag ? etag : NULL);
    char *headers = read_text(headers_file);
    const int status = headers ? last_status(headers) : 0;
    remove(headers_file);
    if (code == 0 && status == 304 && cached) {
        remove(fresh); // Unchanged: what's cached stands
        f->path = str_to_cstr(str_from(file));
    } else if (code == 0 && status >= 200 && status < 300 && folder_file_info(fresh, NULL, NULL)) {
        remove(file);
        if (rename(fresh, file) != 0) {
            snprintf(f->failed, sizeof f->failed, "it was fetched, but can't be kept in %s", dir);
        } else {
            char tag[512] = "";
            if (headers) header_value(headers, "ETag", tag, sizeof tag);
            write_text(etag_file, tag);
            f->path = str_to_cstr(str_from(file));
        }
    } else if (cached) {
        remove(fresh); // The server can't be reached: the cache stands, as an offline build does
        f->path = str_to_cstr(str_from(file));
    } else {
        remove(fresh);
        if (code == -1) snprintf(f->failed, sizeof f->failed, "curl couldn't run; it's what tide fetches with");
        else if (status) snprintf(f->failed, sizeof f->failed, "the server answered %d", status);
        else snprintf(f->failed, sizeof f->failed, "the server couldn't be reached (curl's exit code %d), and nothing of it is cached", code);
    }
    free(headers);
    free(etag);
    return f;
}

// ---------------------------------------------------------------------------
// Globs: `*` within a name, `**` across folders, `?` a character. Windows
// paths ignore case, as asset_path_same does.

static char fold(const char c)
{
#ifdef _WIN32
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
#else
    return c;
#endif
}

bool asset_glob_match(const char *p, const char *s)
{
    while (*p) {
        if (p[0] == '*' && p[1] == '*') {
            const char *rest = p + 2;
            while (*rest == '/') rest++;
            for (const char *t = s;; t++) {
                if (asset_glob_match(rest, t)) return true;
                if (!*t) return false;
            }
        }
        if (*p == '*') {
            for (const char *t = s;; t++) {
                if (asset_glob_match(p + 1, t)) return true;
                if (!*t || *t == '/') return false;
            }
        }
        if (*p == '?') {
            if (!*s || *s == '/') return false;
        } else if (!*s || fold(*p) != fold(*s)) {
            return false;
        }
        p++;
        s++;
    }
    return *s == '\0';
}

// The written path's fixed start, up to the folder the first wildcard is in:
// "art/run/*.png" -> "art/run/", "**/*.png" -> "".
static size_t fixed_prefix(const char *pattern)
{
    size_t last_slash = 0;
    for (size_t i = 0; pattern[i]; i++) {
        if (pattern[i] == '*' || pattern[i] == '?') return last_slash;
        if (pattern[i] == '/') last_slash = i + 1;
    }
    return last_slash;
}

typedef struct glob_walk {
    const char *base;    // The folder walked, absolute, ending in '/'
    const char *rest;    // The pattern from the base on
    const char *prefix;  // What the written path starts with, before the pattern's rest
    VEC(char *) keys;    // Each match as written from the declaring file
    VEC(char *) paths;   // ...and where it is
} glob_walk;

static void glob_found(void *user, const char *path, const unsigned long long size)
{
    (void)size;
    glob_walk *w = user;
    const size_t base_len = strlen(w->base);
    if (strncmp(path, w->base, base_len) != 0) return;
    const char *relative = path + base_len;
    if (!asset_glob_match(w->rest, relative)) return;
    sb key = {0};
    sb_printf(&key, "%s%s", w->prefix, relative);
    vec_push(w->keys, key.data);
    vec_push(w->paths, str_to_cstr(str_from(path)));
}

static int key_order(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

// ---------------------------------------------------------------------------

// The path as written, with its escapes undone and backslashes as forward
// slashes. Arena.
static char *written_path(const str text)
{
    char *out = arena_alloc((size_t)text.len + 1);
    int n = 0;
    for (int i = 0; i < text.len; i++) {
        char c = text.ptr[i];
        if (c == '\\' && i + 1 < text.len) {
            const char e = text.ptr[++i];
            c = e == 'n' ? '\n' : e == '\\' ? '/' : e;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return out;
}

static const char *kind_name(const asset_kind kind)
{
    return kind == ASSET_TEXT ? "string" : kind == ASSET_BYTES ? "List<byte>" : "Grid2<Color>";
}

// Where a missing file might be: one with the same bytes, by the hashes the
// game's .tide/ keeps, or else files of that name, in the game's folder (or
// the declaring file's, with no game folder known). The peer tools say the
// same words, so editors attach their fixes to it.
typedef struct candidates {
    const char *name;
    const char *hex;
    unsigned long long size;
    VEC(char *) same_bytes;
    VEC(char *) same_name;
} candidates;

static void candidate_found(void *user, const char *path, const unsigned long long size)
{
    candidates *c = user;
    if (c->hex && size == c->size) {
        const read_file *f = read_at(path);
        char hex[65];
        if (f) hex_of(f->hash, hex);
        if (f && strcmp(hex, c->hex) == 0) vec_push(c->same_bytes, str_to_cstr(str_from(path)));
    }
    if (asset_path_same(asset_path_name(path), c->name)) vec_push(c->same_name, str_to_cstr(str_from(path)));
}

static void say_missing(const char *written, const char *abs, const char *folder, const char *game_folder, const loc at)
{
    diag_error(at, "there's no file at '%s' (%s)", written, abs);
    candidates c = {asset_path_name(abs), NULL, 0, {0}, {0}};
    asset_hashes known = {0};
    if (game_folder) {
        asset_hashes_load(&known, game_folder);
        char *from_game = asset_path_relative(game_folder, abs);
        const asset_hash *h = asset_hashes_find(&known, from_game);
        if (h) {
            c.hex = h->hex;
            c.size = h->size;
        }
        free(from_game);
    }
    char *root = NULL;
    if (game_folder) {
        root = malloc(strlen(game_folder) + 2);
        if (!root) abort();
        sprintf(root, "%s/", game_folder);
    }
    folder_find_files(root ? root : folder, candidate_found, &c);
    free(root);
    if (c.same_bytes.count) {
        char *relative = asset_path_relative(folder, c.same_bytes.items[0]);
        diag_note("a file with the same bytes is at '%s'", relative);
        free(relative);
    } else if (c.same_name.count) {
        sb list = {0};
        for (int i = 0; i < c.same_name.count && i < 5; i++) {
            char *relative = asset_path_relative(folder, c.same_name.items[i]);
            sb_printf(&list, "%s'%s'", i ? ", " : "", relative);
            free(relative);
        }
        diag_note("files of that name: %s", list.data);
    }
    asset_hashes_free(&known);
}

// Reads `path`'s file as an item of `kind`; false, having said why, when it
// can't be read as one.
static bool read_item(asset_match *item, const char *key, const char *path, const asset_kind kind, const char *written, const loc at)
{
    read_file *f = read_at(path);
    if (!f) {
        diag_error(at, "'%s' can't be read (%s)", written, path);
        return false;
    }
    item->key = key;
    item->abs = f->path;
    item->data = f->data;
    item->size = f->len;
    memcpy(item->hash, f->hash, 32);
    if (kind == ASSET_GRID) {
        if (!f->decoded) {
            f->decoded = true;
            if (!tide_png_decode(f->data, f->len, &f->image)) f->image = (tide_png_image){0};
        }
        if (!f->image.pixels) {
            diag_error(at, "'%s' isn't a PNG that decodes", key);
            diag_note("a Grid2<Color> asset is a PNG; for the file's bytes as they are, read it as a List<byte>");
            return false;
        }
        item->image = f->image;
    } else if (kind == ASSET_TEXT) {
        if (!f->utf8_known) {
            f->utf8_known = true;
            f->utf8 = is_utf8(f->data, f->len);
        }
        if (!f->utf8) {
            diag_error(at, "'%s' isn't UTF-8 text", key);
            diag_note("a string asset is text; for the file's bytes as they are, read it as a List<byte>");
            return false;
        }
    }
    return true;
}

asset_value *asset_resolve(const char *declaring, const str path, const asset_kind kind, const bool many, const bool is_map,
                           const loc at, const char *game_folder)
{
    const char *written = written_path(path);
    if (!*written) {
        diag_error(at, "an asset's path can't be empty: write where the file is from this one, like \"art/hero.png\"");
        return NULL;
    }
    char *folder = asset_path_folder(declaring);
    asset_value *v = NEW(asset_value);
    v->kind = kind;
    v->many = many;
    v->is_map = is_map;
    const bool url = asset_path_is_url(written, strlen(written));
    const bool glob = !url && asset_path_is_glob(written, strlen(written));
    if (url && many) {
        diag_error(at, "a URL is one file, and the type holds many: write the type as '%s'", kind_name(kind));
        free(folder);
        return NULL;
    }
    if (glob && !many) {
        diag_error(at, "'%s' is a glob, which matches many files, and the type holds one", written);
        diag_note("write the type as a list of them, 'List<%s>', or as a map of their paths, 'Map<string, %s>', or name one file",
                  kind_name(kind), kind_name(kind));
        free(folder);
        return NULL;
    }
    if (!glob && many) {
        diag_error(at, "the type holds many files, and '%s' names one", written);
        diag_note("write a glob, like \"art/*.png\" ('*' within a name, '**' across folders, '?' a character), or the type as '%s'",
                  kind_name(kind));
        free(folder);
        return NULL;
    }
    bool ok = true;
    if (url) {
        const fetched *f = fetch_url(written, game_folder);
        if (!f->path) {
            diag_error(at, "'%s' can't be fetched: %s", written, f->failed);
            diag_note("tidec fetches a URL when the game is built and keeps it in the game's .tide/; once it has, the game builds offline too");
            ok = false;
        } else {
            v->items = arena_alloc(sizeof(asset_match));
            v->count = 1;
            ok = read_item(&v->items[0], written, f->path, kind, written, at);
        }
    } else if (glob) {
        const size_t prefix_len = fixed_prefix(written);
        char *prefix = str_to_cstr((str){written, (int)prefix_len});
        char *base = asset_path_join(folder, prefix, prefix_len);
        const size_t base_len = strlen(base);
        char *base_slash = malloc(base_len + 2);
        if (!base_slash) abort();
        sprintf(base_slash, "%s%s", base, base_len && base[base_len - 1] == '/' ? "" : "/");
        glob_walk w = {base_slash, written + prefix_len, prefix, {0}, {0}};
        folder_find_files(base_slash, glob_found, &w);
        if (w.keys.count == 0) {
            diag_error(at, "'%s' matches no file", written);
            diag_note("it's looked for from the folder this file is in, %s", folder);
            ok = false;
        } else {
            // In order of their paths' bytes, which is the order a List<T> has them and a map's keys are in
            char **pairs = malloc(sizeof(char *) * 2u * (size_t)w.keys.count);
            if (!pairs) abort();
            for (int i = 0; i < w.keys.count; i++) {
                pairs[2 * i] = w.keys.items[i];
                pairs[2 * i + 1] = w.paths.items[i];
            }
            qsort(pairs, (size_t)w.keys.count, sizeof(char *) * 2u, key_order);
            v->items = arena_alloc(sizeof(asset_match) * (size_t)w.keys.count);
            v->count = w.keys.count;
            for (int i = 0; i < w.keys.count && ok; i++) ok = read_item(&v->items[i], pairs[2 * i], pairs[2 * i + 1], kind, written, at);
            free(pairs);
        }
        free(base_slash);
        free(base);
    } else {
        char *abs = asset_path_join(folder, written, strlen(written));
        if (!folder_file_info(abs, NULL, NULL)) {
            say_missing(written, abs, folder, game_folder, at);
            ok = false;
        } else {
            v->items = arena_alloc(sizeof(asset_match));
            v->count = 1;
            ok = read_item(&v->items[0], written, abs, kind, written, at);
        }
        free(abs);
    }
    free(folder);
    return ok ? v : NULL;
}

void asset_watched(const char *declaring, const str path, asset_path_fn found, void *user)
{
    const char *written = written_path(path);
    if (!*written || asset_path_is_url(written, strlen(written))) return;
    char *folder = asset_path_folder(declaring);
    if (asset_path_is_glob(written, strlen(written))) {
        const size_t prefix_len = fixed_prefix(written);
        char *base = asset_path_join(folder, written, prefix_len);
        sb with_slash = {0};
        sb_printf(&with_slash, "%s%s", base, base[0] && base[strlen(base) - 1] == '/' ? "" : "/");
        found(user, with_slash.data);
        free(base);
    } else {
        char *abs = asset_path_join(folder, written, strlen(written));
        found(user, abs);
        free(abs);
    }
    free(folder);
}
