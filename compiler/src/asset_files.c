// The files `asset` declarations resolve to (see asset_files.h): found from
// the declaring file, read once each, hashed and decoded, with what's wrong
// said at the declaration in words that say what to write.

#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE // popen and pclose under strict C
#endif

#include "asset_files.h"

#include "fbx_files.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "asset_import_cache.h"
#include "texture_cache.h"
#include "audio_files.h"
#include "folders.h"
#include "image_files.h"
#include "sha256.h"
#include "tide/font.h"
#include "tide/sha256.h"
#include "tide/skinned_data.h"

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
    bool changed; // A file notification overrides matching size/time metadata
    uint8_t *data;
    size_t len;
    uint8_t hash[32];
    uint8_t *prepared_texture;
    size_t prepared_texture_size;
    bool prepared_raw;
    bool prepared_tried;
    bool decoded;          // Tried as an image...
    tide_png_image image;  // ...and what it gave (pixels NULL when neither decodes)
    tide_texture_blocks image_mips;
    char image_why[256];
    bool utf8_known, utf8; // Whether its bytes are UTF-8 text
    bool sound_tried, sound;
    bool font_tried;
    tide_font_error font_error;
    uint8_t *font_data; // Its font data (tide/font.h), once read as a Font
    size_t font_size;
    char sound_why[256];
    uint8_t *sound_data; // MP3/FLAC imported as float PCM WAV; raw file bytes remain available separately
    size_t sound_size;
    // Tried as a mesh, and what it gave: the mesh, or why not. A .gltf's buffers can be files beside it, so
    // what was read holds while those are as they were (`sides`)
    bool mesh_tried, mesh_ok;
    mesh_file mesh;
    char mesh_why[256], mesh_note[256];
    bool tangent_tried, tangent_ok;
    mesh_file tangent_mesh; // Typed static float4-tangent consumers only; canonical mesh stays unsplit
    char tangent_why[256], tangent_note[256];
    struct mesh_side *sides;
    int side_count;
    bool skin_tried, skin_ok;
    uint8_t *skin;
    uint32_t skin_size;
    mesh_file skin_mesh;
    char skin_why[256], skin_note[256];
    struct mesh_side *skin_sides;
    int skin_side_count;
} read_file;

typedef struct mesh_side {
    char *path;
    unsigned long long size, modified;
} mesh_side;

static struct {
    read_file *items;
    int count, cap;
} files;
static bool use_build_cache;

void asset_files_use_build_cache(const bool enabled)
{
    use_build_cache = enabled;
}

static char *copy_cstr(const char *s)
{
    char *out = malloc(strlen(s) + 1);
    if (!out) abort();
    strcpy(out, s);
    return out;
}

static void forget_mesh(read_file *f)
{
    if (f->tangent_ok) mesh_file_free(&f->tangent_mesh);
    f->tangent_tried = f->tangent_ok = false;
    if (f->mesh_ok) mesh_file_free(&f->mesh);
    for (int i = 0; i < f->side_count; i++) free(f->sides[i].path);
    free(f->sides);
    f->sides = NULL;
    f->side_count = 0;
    f->mesh_tried = f->mesh_ok = false;
}

static void forget_skin(read_file *f)
{
    if (f->skin_ok) mesh_file_free(&f->skin_mesh);
    tide_skinned_data_free(f->skin);
    for (int i = 0; i < f->skin_side_count; i++) free(f->skin_sides[i].path);
    free(f->skin_sides);
    f->skin = NULL;
    f->skin_sides = NULL;
    f->skin_side_count = 0;
    f->skin_tried = f->skin_ok = false;
}

static void forget_file(read_file *f)
{
    forget_mesh(f);
    forget_skin(f);
    free(f->path);
    free(f->data);
    free(f->sound_data);
    free(f->font_data);
    free(f->image.pixels);
    free(f->prepared_texture);
    free((void *)(uintptr_t)f->image_mips.pixels);
    memset(f, 0, sizeof *f);
}

void asset_files_forget(void)
{
    for (int i = 0; i < files.count; i++) forget_file(&files.items[i]);
    free(files.items);
    files.items = NULL;
    files.count = files.cap = 0;
}

void asset_files_changed(const char *path)
{
    for (int i = 0; i < files.count; i++) {
        read_file *f = &files.items[i];
        if (asset_path_same(f->path, path) || asset_path_under(f->path, path)) f->changed = true;
        for (int k = 0; k < f->side_count && !f->changed; k++) {
            if (asset_path_same(f->sides[k].path, path) || asset_path_under(f->sides[k].path, path)) f->changed = true;
        }
        for (int k = 0; k < f->skin_side_count && !f->changed; k++) {
            if (asset_path_same(f->skin_sides[k].path, path) || asset_path_under(f->skin_sides[k].path, path)) f->changed = true;
        }
    }
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
    if (f && !f->changed && f->size == size && f->modified == modified) return f;
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

// Whether an ETag can go in a header line: nothing in it ends the line or is
// no text. It's the server's, so it never goes on the command line, where a
// quote of it would end its argument (cmd.exe has no escape for one): curl
// reads it from a file.
static bool safe_etag(const char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || *s == 0x7F) return false;
    }
    return true;
}

// Runs curl for `url`, into `out`, with the response's headers in `headers`:
// what curl said (its exit code), -1 when it couldn't run. `request`, if
// given, is a file of the request's own header lines (If-None-Match).
static int run_curl(const char *url, const char *out, const char *headers, const char *request)
{
    char command[4096];
    const int n = snprintf(command, sizeof command,
                           "curl -fsSL --retry 1 -m 60 -D \"%s\" -o \"%s\"%s%s%s \"%s\" 2>&1", headers, out,
                           request ? " -H \"@" : "", request ? request : "", request ? "\"" : "", url);
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
    char file[2200], etag_file[2200], headers_file[2200], fresh[2200], request_file[2200];
    snprintf(file, sizeof file, "%s/%s", dir, hex);
    snprintf(etag_file, sizeof etag_file, "%s/%s.etag", dir, hex);
    snprintf(headers_file, sizeof headers_file, "%s/%s.headers", dir, hex);
    snprintf(fresh, sizeof fresh, "%s/%s.new", dir, hex);
    snprintf(request_file, sizeof request_file, "%s/%s.request", dir, hex);
    const bool cached = folder_file_info(file, NULL, NULL);
    char *etag = cached ? read_text(etag_file) : NULL;
    if (etag && !*etag) {
        free(etag);
        etag = NULL;
    }
    if (etag && !safe_etag(etag)) etag[0] = '\0';
    remove(headers_file);
    remove(fresh);
    remove(request_file);
    bool asks = false; // Whether the request says the ETag
    if (etag && *etag) {
        FILE *r = fopen(request_file, "wb");
        if (r) {
            fprintf(r, "If-None-Match: %s\n", etag);
            asks = fclose(r) == 0;
        }
    }
    const int code = run_curl(url, fresh, headers_file, asks ? request_file : NULL);
    remove(request_file);
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
const char *asset_written_path(const str text)
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
         : kind == ASSET_CUBEMAP ? "Cubemap" : kind == ASSET_CLIP ? "Clip" : kind == ASSET_MESH ? "Mesh<...>"
         : kind == ASSET_SKINNED ? "SkinnedMesh<...>" : kind == ASSET_ANIMATIONS ? "AnimationCatalog"
         : kind == ASSET_ANIMATION ? "Animation" : kind == ASSET_HUMANOID ? "HumanoidRig" : kind == ASSET_FONT ? "Font"
         : "Grid2<Color>";
}

static bool source_extension(const char *path, const char *extension)
{
    size_t end = strlen(path);
    if (asset_path_is_url(path, end)) {
        const char *authority = strstr(path, "://") + 3;
        const char *part = authority + strcspn(authority, "/?#");
        if (*part != '/') return false;
        end = (size_t)(part - path) + strcspn(part, "?#");
    }
    const size_t length = strlen(extension);
    if (end < length) return false;
    for (size_t i = 0; i < length; i++) {
        const char ch = path[end - length + i];
        if ((ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch) != extension[i]) return false;
    }
    return true;
}

static bool asset_model_format_diagnostic(const char *path, const char *why, const loc at, const bool animations)
{
    // Called only after content inspection failed. A renamed glTF is still accepted.
    // Preserve errors from recognized model content, including unsupported features.
    if (!why || !strstr(why, "JSON") || strstr(why, "GLB") || strstr(why, "FBX")) return false;
    if (source_extension(path, ".gltf") || source_extension(path, ".glb") || source_extension(path, ".fbx")) return false;
    if (source_extension(path, ".obj")) {
        diag_error(at, "%s asset requires glTF 2.0 (.gltf or .glb) or FBX (.fbx), not OBJ", animations ? "an AnimationCatalog" : "a SkinnedMesh");
        if (animations) diag_note("export the model's animations as glTF 2.0 or FBX; OBJ contains no animation data");
        else diag_note("use Mesh<V> and Draw.Mesh for a static OBJ, or export a rigged model as glTF 2.0 or FBX");
    } else {
        diag_error(at, "'%s' isn't a supported %s format", path, animations ? "AnimationCatalog" : "SkinnedMesh");
        diag_note("export %s as glTF 2.0 (.gltf or .glb) or FBX (.fbx); scene files such as .blend, .dae and .usd aren't imported directly",
                  animations ? "the model's animations" : "the rigged model");
    }
    return true;
}

bool asset_skinned_format_diagnostic(const char *path, const char *why, const loc at)
{
    return asset_model_format_diagnostic(path, why, at, false);
}

bool asset_animations_format_diagnostic(const char *path, const char *why, const loc at)
{
    return asset_model_format_diagnostic(path, why, at, true);
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

static tide_skinned_keep_part_fn parts_keep;
static void *parts_user;
static uint8_t parts_key[32];

void asset_choose_parts(const tide_skinned_keep_part_fn keep, void *user, const str *patterns, const int count)
{
    parts_keep = keep;
    parts_user = user;
    tide_sha256 h;
    tide_sha256_init(&h);
    for (int i = 0; i < count; i++) {
        const uint32_t length = (uint32_t)patterns[i].len;
        tide_sha256_add(&h, &length, sizeof length);
        tide_sha256_add(&h, patterns[i].ptr, length);
    }
    tide_sha256_end(&h, parts_key);
}

static void clear_sides(side_reader *sides)
{
    for (int i = 0; i < sides->count; i++) free(sides->sides[i].path);
    free(sides->sides);
    sides->sides = NULL;
    sides->count = 0;
}

static bool check_cached_side(void *user, const char *path, const uint8_t hash[32])
{
    side_reader *sides = user;
    const read_file *f = read_at(path);
    if (!f || memcmp(f->hash, hash, 32)) return false;
    mesh_side *next = realloc(sides->sides, sizeof *next * (size_t)(sides->count + 1));
    if (!next) return false;
    sides->sides = next;
    sides->sides[sides->count++] = (mesh_side){copy_cstr(path), f->size, f->modified};
    return true;
}

static void import_cache_key(uint8_t cache[32], const read_file *f, const char *name, const char *kind, const bool parts)
{
    tide_sha256 h;
    uint8_t variant[32];
    tide_sha256_init(&h);
    tide_sha256_add(&h, f->path, strlen(f->path) + 1);
    tide_sha256_add(&h, name, strlen(name) + 1); // Suffix-based format selection, including URLs.
    if (parts) tide_sha256_add(&h, parts_key, sizeof parts_key);
    tide_sha256_end(&h, variant);
    asset_cache_key(cache, kind, f->hash, variant, sizeof variant);
}

static void write_import_cache(const char *folder, const uint8_t key[32], const side_reader *sides,
                               const void *bytes, const size_t size)
{
    if (!folder) return;
    asset_cache_dependency *deps = calloc((size_t)sides->count + 1, sizeof *deps);
    if (!deps) return;
    bool ok = true;
    for (int i = 0; i < sides->count && ok; i++) {
        const read_file *f = read_at(sides->sides[i].path);
        ok = f != NULL;
        if (f) { deps[i].path = sides->sides[i].path; memcpy(deps[i].hash, f->hash, 32); }
    }
    if (ok) asset_import_cache_write(folder, key, deps, sides->count, bytes, size);
    free(deps);
}

bool asset_name_matches(const str pattern, const char *name, const uint32_t length)
{
    int p = 0, star = -1;
    uint32_t n = 0, resume = 0;
    while (n < length) {
        if (p < pattern.len && pattern.ptr[p] == '*') {
            star = p++;
            resume = n;
        } else if (p < pattern.len && pattern.ptr[p] == name[n]) {
            p++, n++;
        } else if (star >= 0) {
            p = star + 1;
            n = ++resume;
        } else return false;
    }
    while (p < pattern.len && pattern.ptr[p] == '*') p++;
    return p == pattern.len;
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

// The portable model's geometry, in the same column layout the ordinary mesh
// pack writer takes. Skinning keeps these vertices in geometry-local space.
static mesh_file skin_geometry(const uint8_t *bytes, const uint32_t size)
{
    mesh_file m;
    if (!tide_mesh_data_of_model(bytes, size, &m)) abort(); // The decoder's own bytes, validated
    return m;
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
static bool read_item(asset_match *item, const char *key, const char *path, const asset_kind kind, const char *written, const loc at, const char *game_folder)
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
        // Draw-only textures can use their prepared payload without decoding
        // float pixels. Grid consumers still take the ordinary decoder path.
        if (use_build_cache && kind == ASSET_TEXTURE && !f->decoded) {
            texture_cache_view view;
            if (!f->prepared_tried) {
                f->prepared_tried = true;
                uint8_t cache[32];
                texture_cache_key(cache, f->hash, key, false, true);
                f->prepared_texture = texture_cache_read(game_folder, cache, &f->prepared_texture_size, &view);
                if (!f->prepared_texture) {
                    texture_cache_key(cache, f->hash, key, true, true);
                    f->prepared_texture = texture_cache_read(game_folder, cache, &f->prepared_texture_size, &view);
                    f->prepared_raw = true;
                }
            }
            if (f->prepared_texture && texture_cache_open(f->prepared_texture, f->prepared_texture_size, &view)) {
                item->image.width = view.width; item->image.height = view.height;
                item->prepared_texture = f->prepared_texture;
                item->prepared_texture_size = f->prepared_texture_size;
                item->prepared_raw = f->prepared_raw;
                return true;
            }
        }
        if (!f->decoded) {
            f->decoded = true;
            image_file_read_mips(f->data, f->len, key, &f->image, &f->image_mips, f->image_why);
        }
        if (!f->image.pixels) {
            diag_error(at, "'%s' can't be imported as an image: %s", key, f->image_why);
            diag_note("a %s asset accepts PNG, RGB PSD composites, JPEG, TGA, BMP or RGBA8 DDS; for the file's bytes as they are, "
                      "read it as a List<byte>", kind_name(kind));
            return false;
        }
        item->image = f->image;
        item->image_mips = f->image_mips;
    } else if (kind == ASSET_CLIP) {
        if (!f->sound_tried) {
            f->sound_tried = true;
            uint8_t cache[32];
            asset_cache_key(cache, "audio", f->hash, NULL, 0);
            size_t cached_size;
            uint8_t *cached = asset_cache_read(game_folder, cache, &cached_size);
            f->sound = cached && ((cached_size == 1 && cached[0] == 0)
                || (cached_size > 1 && cached[0] == 1 && audio_file_read(cached + 1, cached_size - 1, NULL, NULL, f->sound_why)));
            if (f->sound && cached[0] == 1) {
                f->sound_size = cached_size - 1;
                memmove(cached, cached + 1, f->sound_size);
                f->sound_data = cached;
                cached = NULL;
            }
            free(cached);
            if (!f->sound) {
                f->sound = audio_file_read(f->data, f->len, &f->sound_data, &f->sound_size, f->sound_why);
                if (f->sound && game_folder && f->sound_size < ASSET_CACHE_MAX_BYTES) {
                    uint8_t *encoded = malloc(f->sound_size + 1);
                    if (encoded) {
                        encoded[0] = f->sound_data ? 1 : 0;
                        if (f->sound_size) memcpy(encoded + 1, f->sound_data, f->sound_size);
                        asset_cache_write(game_folder, cache, encoded, f->sound_size + 1);
                        free(encoded);
                    }
                }
            }
        }
        if (!f->sound) {
            diag_error(at, "'%s' can't be imported as audio: %s", key, f->sound_why);
            diag_note("a Clip asset accepts PCM or float WAV, Ogg Vorbis, MP3 or FLAC, of one or two channels; for the file's bytes as "
                      "they are, read it as a List<byte>");
            return false;
        }
        if (f->sound_data) {
            item->data = f->sound_data;
            item->size = f->sound_size;
        }
    } else if (kind == ASSET_FONT) {
        // Read as Font.Decode reads one, so a file that builds is one the game can draw, and one that
        // doesn't says why here
        if (!f->font_tried) {
            f->font_tried = true;
            f->font_error = tide_font_data_make(f->data, f->len, &f->font_data, &f->font_size);
        }
        if (f->font_error != TIDE_FONT_OK) {
            diag_error(at, "'%s' can't be read as a font: %s", key, tide_font_error_text(f->font_error));
            diag_note("a Font asset is a TrueType font file (.ttf, an .otf of TrueType outlines, or a .ttc's first font); "
                      "for the file's bytes as they are, read it as a List<byte>");
            return false;
        }
        item->data = f->font_data;
        item->size = f->font_size;
    } else if ((kind == ASSET_SKINNED || kind == ASSET_ANIMATIONS) && parts_keep) {
        // Some of its parts, for this declaration: decoded here, never into the file's cache, which has all of them
        char *folder = asset_path_folder(path);
        side_reader sides = {folder, NULL, 0};
        uint8_t *bytes = NULL, *fbx_glb = NULL;
        uint32_t size = 0;
        size_t fbx_glb_size = 0;
        char why[256], note[256];
        uint8_t cache[32];
        import_cache_key(cache, f, key, "skin-parts", true);
        size_t cached_size;
        bytes = asset_import_cache_read(game_folder, cache, check_cached_side, &sides, &cached_size);
        bool ok = bytes && cached_size <= UINT32_MAX && tide_skinned_data_validate(bytes, cached_size);
        if (ok) size = (uint32_t)cached_size;
        else {
            free(bytes); bytes = NULL;
            clear_sides(&sides);
            f = read_at(path);
            if (!f) {
                clear_sides(&sides);
                free(folder);
                diag_error(at, "'%s' can't be read (%s)", written, path);
                return false;
            }
            const bool is_fbx = fbx_file_is(f->data, f->len, key);
            ok = !is_fbx || fbx_file_to_glb_parts(f->data, f->len, true, parts_keep, parts_user, &fbx_glb, &fbx_glb_size, why, note);
            // An FBX's parts were chosen as it was converted (its helpers' names are their parents'): its GLB keeps all of it
            if (ok) ok = tide_skinned_data_decode_parts(is_fbx ? fbx_glb : f->data, is_fbx ? fbx_glb_size : f->len, read_side, &sides,
                                                        is_fbx ? NULL : parts_keep, parts_user, &bytes, &size, why, note);

            if (ok) write_import_cache(game_folder, cache, &sides, bytes, size);
        }
        free(fbx_glb);
        free(folder);
        if (!ok) {
            for (int i = 0; i < sides.count; i++) free(sides.sides[i].path);
            free(sides.sides);
            if (!asset_skinned_format_diagnostic(key, why, at)) {
                diag_error(at, "'%s' %s", key, why);
                if (note[0]) diag_note("%s", note);
            }
            return false;
        }
        uint8_t *copy = arena_alloc(size ? size : 1u);
        memcpy(copy, bytes, size);
        item->skinned = copy;
        item->skinned_size = size;
        item->mesh = skin_geometry(bytes, size); // Lives as the compiler does, as the arena's copy
        tide_skinned_data_free(bytes);
        item->side_count = sides.count;
        item->sides = arena_alloc(sizeof(char *) * (size_t)(sides.count ? sides.count : 1));
        for (int i = 0; i < sides.count; i++) {
            item->sides[i] = str_to_cstr(str_from(sides.sides[i].path));
            free(sides.sides[i].path);
        }
        free(sides.sides);
    } else if (kind == ASSET_SKINNED || kind == ASSET_ANIMATIONS || kind == ASSET_ANIMATION) {
        const read_file sides_check = {.sides = f->skin_sides, .side_count = f->skin_side_count};
        if (f->skin_tried && !sides_same(&sides_check)) forget_skin(f);
        if (!f->skin_tried) {
            char *folder = asset_path_folder(path);
            side_reader sides = {folder, NULL, 0};
            uint8_t *bytes = NULL;
            uint32_t size = 0;
            char why[256], note[256];
            uint8_t *fbx_glb = NULL;
            size_t fbx_glb_size = 0;
            uint8_t cache[32];
            import_cache_key(cache, f, key, "skin", false);
            size_t cached_size;
            bytes = asset_import_cache_read(game_folder, cache, check_cached_side, &sides, &cached_size);
            bool ok = bytes && cached_size <= UINT32_MAX && tide_skinned_data_validate(bytes, cached_size);
            if (ok) size = (uint32_t)cached_size;
            else {
                free(bytes); bytes = NULL;
                clear_sides(&sides);
                f = read_at(path);
                if (!f) {
                    clear_sides(&sides);
                    free(folder);
                    diag_error(at, "'%s' can't be read (%s)", written, path);
                    return false;
                }
                const bool is_fbx = fbx_file_is(f->data, f->len, key);
                ok = !is_fbx || fbx_file_to_glb(f->data, f->len, true, &fbx_glb, &fbx_glb_size, why, note);
                if (ok) ok = tide_skinned_data_decode(is_fbx ? fbx_glb : f->data, is_fbx ? fbx_glb_size : f->len,
                    false, read_side, &sides, &bytes, &size, why, note);
                if (!ok && !is_fbx && source_extension(key, ".glb") &&
                    (f->len < 4 || memcmp(f->data, "glTF", 4) != 0) &&
                    strstr(why, "JSON")) {
                    snprintf(why, sizeof why, "has an invalid GLB header (expected glTF 2.0)");
                    snprintf(note, sizeof note, "re-export as glTF 2.0 binary (.glb), glTF JSON (.gltf) or FBX (.fbx)");
                }

                if (ok) write_import_cache(game_folder, cache, &sides, bytes, size);
            }
            if (ok) why[0] = note[0] = '\0';
            free(fbx_glb);
            free(folder);
            f = read_at(path);
            if (!f) {
                tide_skinned_data_free(bytes);
                for (int i = 0; i < sides.count; i++) free(sides.sides[i].path);
                free(sides.sides);
                diag_error(at, "'%s' can't be read (%s)", written, path);
                return false;
            }
            f->skin_tried = true;
            f->skin_ok = ok;
            f->skin = bytes;
            f->skin_size = size;
            f->skin_sides = sides.sides;
            f->skin_side_count = sides.count;
            memcpy(f->skin_why, why, sizeof why);
            memcpy(f->skin_note, note, sizeof note);
            if (ok) f->skin_mesh = skin_geometry(bytes, size);
        }
        if (!f->skin_ok) {
            if (!(kind == ASSET_ANIMATIONS || kind == ASSET_ANIMATION ? asset_animations_format_diagnostic(key, f->skin_why, at)
                                          : asset_skinned_format_diagnostic(key, f->skin_why, at))) {
                diag_error(at, "'%s' %s", key, f->skin_why);
                if (f->skin_note[0]) diag_note("%s", f->skin_note);
            }
            return false;
        }
        item->mesh = f->skin_mesh;
        item->skinned = f->skin;
        item->skinned_size = f->skin_size;
        item->side_count = f->skin_side_count;
        item->sides = arena_alloc(sizeof(char *) * (size_t)(f->skin_side_count ? f->skin_side_count : 1));
        for (int i = 0; i < f->skin_side_count; i++) item->sides[i] = str_to_cstr(str_from(f->skin_sides[i].path));
    } else if (kind == ASSET_MESH) {
        if (f->mesh_tried && !sides_same(f)) forget_mesh(f);
        if (!f->mesh_tried) {
            char *folder = asset_path_folder(path);
            side_reader sides = {folder, NULL, 0};
            mesh_file mesh = {0};
            char why[256], note[256];
            // What it is where its first bytes don't say is its name's to say: as written, as a fetched URL's file has another
            uint8_t cache[32];
            import_cache_key(cache, f, key, "mesh", false);
            size_t cached_size;
            uint8_t *cached = asset_import_cache_read(game_folder, cache, check_cached_side, &sides, &cached_size);
            bool ok = cached && asset_mesh_cache_decode(cached, cached_size, &mesh);
            free(cached);
            if (!ok) {
                clear_sides(&sides);
                f = read_at(path);
                if (!f) {
                    free(folder);
                    diag_error(at, "'%s' can't be read (%s)", written, path);
                    return false;
                }
                ok = mesh_file_read(f->data, f->len, key, read_side, &sides, &mesh, why, note);
                if (ok && game_folder) {
                    size_t encoded_size;
                    uint8_t *encoded = asset_mesh_cache_encode(&mesh, &encoded_size);
                    if (encoded) write_import_cache(game_folder, cache, &sides, encoded, encoded_size);
                    free(encoded);
                }
            }
            if (ok) why[0] = note[0] = '\0';
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

bool asset_mesh_tangents(asset_value *value, const loc at)
{
    if (!value || value->kind != ASSET_MESH || value->paths_only) return false;
    for (int i = 0; i < value->count; i++) {
        asset_match *item = &value->items[i];
        read_file *f = NULL;
        // Resolution already validated the file and its side buffers. Lookup
        // only: generating a variant never reimports or moves the file table.
        for (int k = 0; k < files.count && !f; k++) {
            if (asset_path_same(files.items[k].path, item->abs)) f = &files.items[k];
        }
        if (!f || !f->mesh_ok) {
            diag_error(at, "'%s' has no resolved static mesh for its tangents", item->key);
            return false;
        }
        if (!f->tangent_tried) {
            f->tangent_tried = true;
            uint8_t cache[32], source[32];
            size_t encoded_size = 0, cached_size;
            uint8_t *encoded = value->cache_folder ? asset_mesh_cache_encode(&f->mesh, &encoded_size) : NULL;
            uint8_t *cached = NULL;
            if (encoded) {
                tide_sha256_of(encoded, encoded_size, source);
                asset_cache_key(cache, "mesh-tangents", source, NULL, 0);
                cached = asset_cache_read(value->cache_folder, cache, &cached_size);
            }
            f->tangent_ok = cached && asset_mesh_cache_decode(cached, cached_size, &f->tangent_mesh);
            free(cached);
            if (!f->tangent_ok) {
                f->tangent_ok = mesh_file_with_tangents(&f->mesh, &f->tangent_mesh, f->tangent_why, f->tangent_note);
                if (f->tangent_ok && encoded) {
                    size_t size;
                    uint8_t *bytes = asset_mesh_cache_encode(&f->tangent_mesh, &size);
                    if (bytes) asset_cache_write(value->cache_folder, cache, bytes, size);
                    free(bytes);
                }
            }
            free(encoded);
        }
        if (!f->tangent_ok) {
            diag_error(at, "'%s' %s", item->key, f->tangent_why);
            if (f->tangent_note[0]) diag_note("%s", f->tangent_note);
            return false;
        }
        item->mesh = f->tangent_mesh;
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

static bool resolve_item(asset_match *item, const char *key, const char *absolute, const asset_kind kind,
                         const char *written, const loc at, const bool paths_only, const char *game_folder)
{
    if (!paths_only) return read_item(item, key, absolute, kind, written, at, game_folder);
    item->key = str_to_cstr(str_from(key));
    item->abs = absolute ? str_to_cstr(str_from(absolute)) : NULL;
    return true;
}

static asset_value *asset_resolve_impl(const char *declaring_as_given, const str path, const asset_kind kind,
                                      const bool many, const bool is_map, const loc at, const char *game_folder,
                                      const bool paths_only)
{
    const char *cache_folder = use_build_cache ? game_folder : NULL;
    char *declaring = declaring_abs(declaring_as_given);
    const char *written = asset_written_path(path);
    if (!*written) {
        diag_error(at, "an asset's path can't be empty: write where the file is from this one, like \"art/hero.png\"");
        free(declaring);
        return NULL;
    }
    char *folder = asset_path_folder(declaring);
    free(declaring);
    asset_value *v = NEW(asset_value);
    v->kind = kind;
    v->cache_folder = cache_folder;
    v->many = many;
    v->is_map = is_map;
    v->paths_only = paths_only;
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
    if (url && paths_only) {
        v->items = arena_alloc(sizeof(asset_match));
        v->count = 1;
        resolve_item(&v->items[0], written, NULL, kind, written, at, true, cache_folder);
    } else if (url) {
        const fetched *f = fetch_url(written, game_folder);
        if (!f->path) {
            diag_error(at, "'%s' can't be fetched: %s", written, f->failed);
            diag_note("tidec fetches a URL when the game is built and keeps it in the game's .tide/; once it has, the game builds offline too");
            ok = false;
        } else {
            v->items = arena_alloc(sizeof(asset_match));
            v->count = 1;
            ok = read_item(&v->items[0], written, f->path, kind, written, at, cache_folder);
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
            for (int i = 0; i < w.keys.count && ok; i++)
                ok = resolve_item(&v->items[i], pairs[2 * i], pairs[2 * i + 1], kind, written, at, paths_only, cache_folder);
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
            ok = resolve_item(&v->items[0], written, abs, kind, written, at, paths_only, cache_folder);
        }
        free(abs);
    }
    free(folder);
    return ok ? v : NULL;
}

asset_value *asset_resolve(const char *declaring, const str path, const asset_kind kind, const bool many,
                           const bool is_map, const loc at, const char *game_folder)
{
    return asset_resolve_impl(declaring, path, kind, many, is_map, at, game_folder, false);
}

asset_value *asset_resolve_paths(const char *declaring, const str path, const asset_kind kind, const bool many,
                                 const bool is_map, const loc at, const char *game_folder)
{
    return asset_resolve_impl(declaring, path, kind, many, is_map, at, game_folder, true);
}

void asset_watched(const char *declaring_as_given, const str path, asset_path_fn found, void *user)
{
    const char *written = asset_written_path(path);
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
        const char *written = asset_written_path(n->path);
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
