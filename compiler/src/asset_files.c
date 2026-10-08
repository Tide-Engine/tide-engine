// The files `asset` declarations resolve to (see asset_files.h): found from
// the declaring file, read once each, hashed and decoded, with what's wrong
// said at the declaration in words that say what to write.

#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE // popen and pclose under strict C
#endif

#include "asset_files.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "folders.h"
#include "psd_files.h"
#include "sha256.h"
#include "tide/vorbis.h"
#include "tide/wav.h"
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
    bool decoded;          // Tried as a PNG or PSD...
    tide_png_image image;  // ...and what it gave (pixels NULL when neither decodes)
    char psd_why[256];     // What's wrong with bytes that start as a PSD
    bool utf8_known, utf8; // Whether its bytes are UTF-8 text
    bool sound_tried, sound; // Whether it's a WAV or an Ogg Vorbis file that decodes
    const char *vorbis_why;  // What's wrong with it as Ogg Vorbis, for a file that starts as one
    // Tried as a mesh, and what it gave: the mesh, or why not. A .gltf's buffers can be files beside it, so
    // what was read holds while those are as they were (`sides`)
    bool mesh_tried, mesh_ok;
    mesh_file mesh;
    char mesh_why[256], mesh_note[256];
    struct mesh_side *sides;
    int side_count;
} read_file;

typedef struct mesh_side {
    char *path;
    unsigned long long size, modified;
} mesh_side;

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

static void forget_mesh(read_file *f)
{
    if (f->mesh_ok) mesh_file_free(&f->mesh);
    for (int i = 0; i < f->side_count; i++) free(f->sides[i].path);
    free(f->sides);
    f->sides = NULL;
    f->side_count = 0;
    f->mesh_tried = f->mesh_ok = false;
}

static void forget_file(read_file *f)
{
    forget_mesh(f);
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

// Whether an ETag can go in a header on the command line: its quotes are
// escaped there, and nothing else of it is for the shell.
static bool safe_etag(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n' || *s == '\r' || *s == '%' || *s == '`' || *s == '$' || *s == '\\') return false;
    }
    return true;
}

// Runs curl for `url`, into `out`, with the response's headers in `headers`:
// what curl said (its exit code), -1 when it couldn't run. `etag` goes as
// If-None-Match, its quotes escaped for the shell.
static int run_curl(const char *url, const char *out, const char *headers, const char *etag)
{
    char header[1024] = "";
    if (etag) {
        size_t n = 0;
        for (const char *c = etag; *c && n + 3 < sizeof header; c++) {
            if (*c == '"') header[n++] = '\\';
            header[n++] = *c;
        }
        header[n] = '\0';
    }
    char command[4096];
    const int n = snprintf(command, sizeof command,
                           "curl -fsSL --retry 1 -m 60 -D \"%s\" -o \"%s\"%s%s%s \"%s\" 2>&1", headers, out,
                           etag ? " -H \"If-None-Match: " : "", etag ? header : "", etag ? "\"" : "", url);
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
    for (char *c = dir; *c; c++) { // Forward slashes, which every system and curl take, and the shell leaves alone
        if (*c == '\\') *c = '/';
    }
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
    if (etag && !safe_etag(etag)) etag[0] = '\0';
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

// Whether the pattern from `p` up to `p_end` matches the path from `s` up to
// `s_end`.
static bool glob_match(const char *p, const char *p_end, const char *s, const char *s_end)
{
    while (p < p_end) {
        if (p_end - p >= 2 && p[0] == '*' && p[1] == '*') {
            const char *rest = p + 2;
            while (rest < p_end && *rest == '/') rest++;
            for (const char *t = s;; t++) {
                if (glob_match(rest, p_end, t, s_end)) return true;
                if (t == s_end) return false;
            }
        }
        if (*p == '*') {
            for (const char *t = s;; t++) {
                if (glob_match(p + 1, p_end, t, s_end)) return true;
                if (t == s_end || *t == '/') return false;
            }
        }
        if (*p == '?') {
            if (s == s_end || *s == '/') return false;
        } else if (s == s_end || fold(*p) != fold(*s)) {
            return false;
        }
        p++;
        s++;
    }
    return s == s_end;
}

bool asset_glob_match(const char *p, const char *s)
{
    return glob_match(p, p + strlen(p), s, s + strlen(s));
}

// Whether the glob `p` could match a file in `folder` or under it, a path
// from where the glob starts that ends in '/': each of its folders' names has
// to match the pattern's part in its place, up to a part with a `**`, which
// matches at any depth. The pattern's last part is a file's name, so a
// pattern with no `**` reaches as many folders deep as it has slashes.
static bool glob_reaches(const char *p, const char *folder)
{
    while (*folder) {
        const char *part = p;
        for (; *p && *p != '/'; p++) {
            if (p[0] == '*' && p[1] == '*') return true;
        }
        if (!*p) return false;
        const char *name = folder;
        while (*folder != '/') folder++;
        if (!glob_match(part, p, name, folder)) return false;
        p++;
        folder++;
    }
    return true;
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

// A glob's walk only goes where it could match: "*.png" in a game's own
// folder reads that folder's entries, and none of the folders under it.
static bool glob_enters(void *user, const char *folder)
{
    const glob_walk *w = user;
    const size_t base_len = strlen(w->base);
    return strncmp(folder, w->base, base_len) == 0 && glob_reaches(w->rest, folder + base_len);
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
    return kind == ASSET_TEXT ? "string" : kind == ASSET_BYTES ? "List<byte>" : kind == ASSET_TEXTURE ? "Texture"
         : kind == ASSET_CUBEMAP ? "Cubemap" : kind == ASSET_CLIP ? "Clip" : kind == ASSET_MESH ? "Mesh<...>" : "Grid2<Color>";
}

// A file a .gltf names beside it: read like any, and noted, so the mesh is
// read again when it changes. The files' list can move as one is read, so the
// .gltf's own entry is found again by its path afterwards.
typedef struct side_reader {
    const char *folder; // The .gltf's
    mesh_side *sides;
    int count;
} side_reader;

static const uint8_t *read_side(void *user, const char *path, size_t *size)
{
    side_reader *s = user;
    char *abs = asset_path_join(s->folder, path, strlen(path));
    const read_file *f = read_at(abs);
    s->sides = realloc(s->sides, sizeof *s->sides * (size_t)(s->count + 1));
    if (!s->sides) abort();
    s->sides[s->count++] = (mesh_side){abs, f ? f->size : 0, f ? f->modified : 0}; // One that's missing is noted too: it may come
    if (!f) return NULL;
    *size = f->len;
    return f->data;
}

// Whether the files a mesh was read with are as they were then.
static bool sides_same(const read_file *f)
{
    for (int i = 0; i < f->side_count; i++) {
        unsigned long long size = 0, modified = 0;
        if (!folder_file_info(f->sides[i].path, &size, &modified)) size = modified = 0;
        if (size != f->sides[i].size || modified != f->sides[i].modified) return false;
    }
    return true;
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

static int candidate_order(const void *a, const void *b)
{
    return path_compare(*(char *const *)a, *(char *const *)b);
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
    // By their paths, as the language server has them: a system lists a folder in whatever order it keeps it in
    if (c.same_bytes.count > 1) qsort(c.same_bytes.items, (size_t)c.same_bytes.count, sizeof(char *), candidate_order);
    if (c.same_name.count > 1) qsort(c.same_name.items, (size_t)c.same_name.count, sizeof(char *), candidate_order);
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
    if (kind == ASSET_GRID || kind == ASSET_TEXTURE || kind == ASSET_CUBEMAP) {
        if (!f->decoded) {
            f->decoded = true;
            if (f->len >= 4 && memcmp(f->data, "8BPS", 4) == 0) {
                psd_file_read(f->data, f->len, &f->image, f->psd_why);
            } else if (!tide_png_decode(f->data, f->len, &f->image)) f->image = (tide_png_image){0};
        }
        if (!f->image.pixels) {
            diag_error(at, "'%s' isn't a PNG or a PSD that decodes", key);
            diag_note("a %s asset is a PNG or an RGB PSD's saved composite image; for the file's bytes as they are, "
                      "read it as a List<byte>", kind_name(kind));
            if (f->psd_why[0]) diag_note("as PSD, %s", f->psd_why);
            return false;
        }
        item->image = f->image;
    } else if (kind == ASSET_CLIP) {
        if (!f->sound_tried) {
            f->sound_tried = true;
            tide_sound sound;
            f->sound = tide_wav_decode(f->data, f->len, &sound);
            if (f->sound) {
                tide_sound_free(&sound);
            } else {
                const char *why = NULL;
                tide_vorbis *v = tide_vorbis_open(f->data, f->len, &why); // Its headers and pages: no audio is decoded
                f->sound = v != NULL;
                tide_vorbis_close(v);
                if (!f->sound && f->len >= 4 && memcmp(f->data, "OggS", 4) == 0) f->vorbis_why = why;
            }
        }
        if (!f->sound) {
            diag_error(at, "'%s' isn't a WAV or an Ogg Vorbis file that decodes", key);
            diag_note("a Clip asset is a WAV of PCM or floats, or Ogg Vorbis, of one or two channels; for the file's bytes as "
                      "they are, read it as a List<byte>");
            if (f->vorbis_why) diag_note("as Ogg Vorbis, %s", f->vorbis_why);
            return false;
        }
    } else if (kind == ASSET_MESH) {
        if (f->mesh_tried && !sides_same(f)) forget_mesh(f);
        if (!f->mesh_tried) {
            char *folder = asset_path_folder(path);
            side_reader sides = {folder, NULL, 0};
            mesh_file mesh = {0};
            char why[256], note[256];
            // What it is where its first bytes don't say is its name's to say: as written, as a fetched URL's file has another
            const bool ok = mesh_file_read(f->data, f->len, key, read_side, &sides, &mesh, why, note);
            free(folder);
            f = read_at(path); // Where it is now: reading a file beside it may have moved the list
            if (!f) {
                diag_error(at, "'%s' can't be read (%s)", written, path);
                return false;
            }
            f->mesh_tried = true;
            f->mesh_ok = ok;
            f->mesh = mesh;
            f->sides = sides.sides;
            f->side_count = sides.count;
            memcpy(f->mesh_why, why, sizeof why);
            memcpy(f->mesh_note, note, sizeof note);
        }
        if (!f->mesh_ok) {
            diag_error(at, "'%s' %s", key, f->mesh_why);
            if (f->mesh_note[0]) diag_note("%s", f->mesh_note);
            return false;
        }
        item->mesh = f->mesh;
        item->side_count = f->side_count;
        item->sides = arena_alloc(sizeof(char *) * (size_t)(f->side_count ? f->side_count : 1));
        for (int i = 0; i < f->side_count; i++) item->sides[i] = str_to_cstr(str_from(f->sides[i].path));
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

// The declaring file's path, absolute: tidec is given paths as they were
// typed, which can be from where it runs.
static char *declaring_abs(const char *declaring)
{
    const bool absolute = declaring[0] == '/' || declaring[0] == '\\' || (declaring[0] && declaring[1] == ':');
    if (absolute) return copy_cstr(declaring);
    char cwd[4096];
    if (!folder_current(cwd, sizeof cwd)) return copy_cstr(declaring);
    char *abs = malloc(strlen(cwd) + strlen(declaring) + 2);
    if (!abs) abort();
    sprintf(abs, "%s/%s", cwd, declaring);
    return abs;
}

// A cube's faces, in the order a cube has them, by how a file's name ends before its extension.
static const char *const cube_faces[6] = {"px", "nx", "py", "ny", "pz", "nz"};
static const char *const cube_sides[6] = {"+x", "-x", "+y", "-y", "+z", "-z"};

// Which face a file is, by its name: -1 for one named for none.
static int cube_face_of(const char *key)
{
    const char *name = strrchr(key, '/');
    name = name ? name + 1 : key;
    const char *dot = strrchr(name, '.');
    const size_t length = dot ? (size_t)(dot - name) : strlen(name);
    if (length < 2) return -1;
    for (int face = 0; face < 6; face++) {
        if (tolower((unsigned char)name[length - 2]) == cube_faces[face][0]
            && tolower((unsigned char)name[length - 1]) == cube_faces[face][1]) return face;
    }
    return -1;
}

// Puts the files a Cubemap's glob matched in the faces' order, six of them, squares of one size. What's wrong is said.
static bool order_cube(asset_value *v, const char *written, const loc at)
{
    asset_match *faces[6] = {0};
    for (int i = 0; i < v->count; i++) {
        asset_match *m = &v->items[i];
        const int face = cube_face_of(m->key);
        if (face < 0) {
            diag_error(at, "'%s' isn't named for a face of the cube: a face's file ends in px, nx, py, ny, pz or nz before its "
                           "extension", m->key);
            diag_note("like 'sky_px.png' for the face +x points at, 'sky_nx.png' for -x's, and so on: a glob that matches "
                      "only the six, like \"sky_??.png\", leaves other files out");
            return false;
        }
        if (faces[face]) {
            diag_error(at, "'%s' and '%s' are both the cube's %s face: a Cubemap has one file a face", faces[face]->key, m->key,
                       cube_sides[face]);
            return false;
        }
        faces[face] = m;
    }
    for (int face = 0; face < 6; face++) {
        if (faces[face]) continue;
        diag_error(at, "'%s' matches no file for the cube's %s face, whose name ends in '%s' before its extension", written,
                   cube_sides[face], cube_faces[face]);
        diag_note("a Cubemap is six files, one a face: px, nx, py, ny, pz and nz, the faces the axes point at and away from");
        return false;
    }
    asset_match *ordered = arena_alloc(sizeof(asset_match) * 6u);
    for (int face = 0; face < 6; face++) ordered[face] = *faces[face];
    for (int face = 0; face < 6; face++) {
        const asset_match *m = &ordered[face];
        if (m->image.width != m->image.height) {
            diag_error(at, "'%s' is %d by %d pixels: a cube's face is square", m->key, (int)m->image.width, (int)m->image.height);
            return false;
        }
        if (m->image.width != ordered[0].image.width) {
            diag_error(at, "'%s' is %d pixels a side and '%s' is %d: a cube's six faces are one size", ordered[0].key,
                       (int)ordered[0].image.width, m->key, (int)m->image.width);
            return false;
        }
    }
    v->items = ordered;
    v->count = 6;
    return true;
}

asset_value *asset_resolve(const char *declaring_as_given, const str path, const asset_kind kind, const bool many, const bool is_map,
                           const loc at, const char *game_folder)
{
    char *declaring = declaring_abs(declaring_as_given);
    const char *written = written_path(path);
    if (!*written) {
        diag_error(at, "an asset's path can't be empty: write where the file is from this one, like \"art/hero.png\"");
        free(declaring);
        return NULL;
    }
    char *folder = asset_path_folder(declaring);
    free(declaring);
    asset_value *v = NEW(asset_value);
    v->kind = kind;
    v->many = many;
    v->is_map = is_map;
    const bool url = asset_path_is_url(written, strlen(written));
    const bool glob = !url && asset_path_is_glob(written, strlen(written));
    if (kind == ASSET_CUBEMAP && !glob) {
        diag_error(at, "a Cubemap is six files, its faces, and '%s' names one: write a glob that matches the six", written);
        diag_note("like \"art/sky_??.png\", for files named for their faces by how their names end: sky_px.png, sky_nx.png, "
                  "sky_py.png, sky_ny.png, sky_pz.png and sky_nz.png");
        free(folder);
        return NULL;
    }
    if (glob && !many && kind != ASSET_CUBEMAP) {
        diag_error(at, "'%s' is a glob, which matches many files, and the type holds one", written);
        diag_note("write the type as a list of them, 'List<%s>', or as a map of their paths, 'Map<string, %s>', or name one file",
                  kind_name(kind), kind_name(kind));
        free(folder);
        return NULL;
    }
    // One file, or a URL, where the type holds many is a list or a map of that one, its key the path as written: a
    // declaration's path can go from a file to a glob and back with its type, and the code that reads it, as they are
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
        folder_find_files_in(base_slash, glob_enters, glob_found, &w);
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
            if (ok && kind == ASSET_CUBEMAP) ok = order_cube(v, written, at);
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

void asset_watched(const char *declaring_as_given, const str path, asset_path_fn found, void *user)
{
    const char *written = written_path(path);
    if (!*written || asset_path_is_url(written, strlen(written))) return;
    char *declaring = declaring_abs(declaring_as_given);
    char *folder = asset_path_folder(declaring);
    free(declaring);
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

void asset_files_save_hashes(const char *game_folder, const asset_named *named, const int count)
{
    if (!game_folder) return;
    asset_hashes known = {0};
    asset_hashes now = {0};
    asset_hashes_load(&known, game_folder);
    for (int i = 0; i < count; i++) {
        const asset_named *n = &named[i];
        const char *written = written_path(n->path);
        if (!*written || asset_path_is_url(written, strlen(written))) continue;
        if (n->value) {
            for (int k = 0; k < n->value->count; k++) {
                const asset_match *item = &n->value->items[k];
                char hex[65];
                for (int b = 0; b < 32; b++) snprintf(hex + b * 2, 3, "%02x", item->hash[b]);
                char *from_game = asset_path_relative(game_folder, item->abs);
                asset_hashes_set(&now, from_game, hex, item->size);
                free(from_game);
            }
        } else if (!asset_path_is_glob(written, strlen(written))) {
            // Missing, or unreadable: what was known of it stays, for finding it
            char *declaring = declaring_abs(n->declaring);
            char *folder = asset_path_folder(declaring);
            char *abs = asset_path_join(folder, written, strlen(written));
            char *from_game = asset_path_relative(game_folder, abs);
            asset_hashes_keep(&now, &known, from_game);
            free(from_game);
            free(abs);
            free(folder);
            free(declaring);
        }
    }
    asset_hashes_save(&now, game_folder);
    asset_hashes_free(&known);
    asset_hashes_free(&now);
}
