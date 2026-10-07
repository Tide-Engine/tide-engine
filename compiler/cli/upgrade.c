#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif

#include "upgrade.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "editors.h"
#include "install.h"
#include "json.h"
#include "line.h"
#include "release.h"
#include "sha256.h"
#include "sys.h"
#include "unzip.h"

#ifndef TIDE_VERSION
#define TIDE_VERSION "0.0.0-dev"
#endif
#ifndef TIDE_CHANNEL
#define TIDE_CHANNEL "nightly"
#endif
#ifndef TIDE_REPOSITORY
#define TIDE_REPOSITORY "BlenMiner/tide-engine"
#endif

// The package for this machine, as the release workflow names it.
#if defined(_WIN32)
#define PACKAGE "tide-windows-x64.zip"
#elif defined(__APPLE__)
#define PACKAGE "tide-macos-arm64.tar.gz"
#else
#define PACKAGE "tide-linux-x64.tar.gz"
#endif

static bool is_dev_build(void)
{
    const char *version = TIDE_VERSION;
    const size_t n = strlen(version);
    return n >= 4 && strcmp(version + n - 4, "-dev") == 0;
}

const char *tide_channel(const char *root)
{
    static char channel[32];
    char *path = path_join(root, "channel");
    char *text = sys_read_file(path, NULL);
    free(path);
    snprintf(channel, sizeof channel, "%s", text ? text : TIDE_CHANNEL);
    free(text);
    for (char *c = channel; *c; c++) {
        if (*c == '\r' || *c == '\n' || *c == ' ') *c = '\0';
    }
    return strcmp(channel, "stable") == 0 || strcmp(channel, "nightly") == 0 ? channel : TIDE_CHANNEL;
}

// curl reads the credential from a short-lived file, never its command line.
// A GitHub CLI login is enough when neither token environment variable is set.
static bool github_auth(const char *work, char **header)
{
    *header = NULL;
    const char *token = sys_env("GH_TOKEN");
    if (!token) token = sys_env("GITHUB_TOKEN");
    char login[4096] = {0};
    if (!token) {
        char *gh = sys_which("gh");
        const char *const argv[] = {gh, "auth", "token", "--hostname", "github.com", NULL};
        const bool found = gh && sys_capture(argv, login, sizeof login) == 0;
        free(gh);
        if (found) {
            size_t n = strlen(login);
            while (n && (login[n - 1] == '\r' || login[n - 1] == '\n')) login[--n] = '\0';
            if (n) token = login;
        }
    }
    if (!token) return true;
    for (const unsigned char *c = (const unsigned char *)token; *c; c++) {
        if (*c <= ' ' || *c >= 127) return false;
    }
    char *path = path_join(work, "github-auth");
    FILE *file = fopen(path, "wbx"); // Never follow an existing file or symlink
    bool ok = file != NULL;
#ifndef _WIN32
    if (ok) ok = chmod(path, S_IRUSR | S_IWUSR) == 0; // Before the file holds the token
#endif
    if (ok) ok = fprintf(file, "Authorization: Bearer %s\n", token) > 0;
    if (file && fclose(file) != 0) ok = false;
    if (!ok) {
        if (file) sys_remove(path);
        free(path);
        return false;
    }
    *header = malloc(strlen(path) + 2);
    if (!*header) abort();
    sprintf(*header, "@%s", path);
    free(path);
    return true;
}

static void github_auth_free(char *header)
{
    if (header) sys_remove(header + 1);
    free(header);
}

static bool github_api(const char *url)
{
    const char prefix[] = "https://api.github.com/";
    return url && strncmp(url, prefix, sizeof prefix - 1) == 0;
}

// One curl transfer. --next keeps a token off another transfer's URL, and
// curl's normal redirects keep Authorization off other origins. -q at the
// start of the command ignores settings that might log or forward it.
static int curl_request(const char **argv, const char *url, const char *path, const char *accept,
                        const char *header, const bool quiet)
{
    const char *const options[] = {"-fsSL", "--retry", "2", "-m", quiet ? "5" : "600",
                                   "--proto-redir", "=https", "-H", accept, "-o", path};
    const int count = (int)(sizeof options / sizeof options[0]);
    memcpy(argv, options, sizeof options);
    int n = count;
    if (header && github_api(url)) {
        argv[n++] = "-H";
        argv[n++] = header;
    }
    argv[n++] = url;
    argv[n] = NULL;
    return n;
}

// Downloads `url` to `path`.
static bool download(const char *url, const char *path, const bool quiet)
{
    char *work = path_dir(path);
    char *header = NULL;
    const bool ready = !github_api(url) || github_auth(work, &header);
    free(work);
    if (!ready) return false;
    const char *argv[20] = {sys_tool("curl"), "-q"};
    curl_request(argv + 2, url, path, "Accept: application/vnd.github+json", header, quiet);
    const bool ok = sys_run(argv, NULL, quiet) == 0;
    github_auth_free(header);
    return ok;
}

#define RELEASES_API "https://api.github.com/repos/" TIDE_REPOSITORY "/releases"

// JSON from `url`, saved in `work` as `name`; NULL if it didn't come.
static const json *fetch_json(const char *work, const char *url, const char *name)
{
    char *path = path_join(work, name);
    const bool ok = download(url, path, true);
    size_t len = 0;
    char *text = ok ? sys_read_file(path, &len) : NULL;
    free(path);
    return text ? json_parse(text, len) : NULL;
}

// The GitHub releases list, newest first: the last 100 published, which
// always include the newest nightly.
static const json *fetch_releases(const char *work, const bool quiet)
{
    char *list = path_join(work, "releases.json");
    const char *custom = sys_env("TIDE_RELEASES_URL"); // For tests: a file:// URL to a releases list
    const char *url = custom ? custom : RELEASES_API "?per_page=100";
    const bool ok = download(url, list, quiet);
    if (!ok) {
        if (!quiet) fprintf(stderr, "tide: couldn't reach GitHub to look for new versions\n");
        if (!quiet) fprintf(stderr, "  = note: for private releases, use `gh auth login` or set GH_TOKEN (or GITHUB_TOKEN) "
                                   "with repository read access\n");
        free(list);
        return NULL;
    }
    size_t len = 0;
    char *text = sys_read_file(list, &len);
    free(list);
    const json *releases = text ? json_parse(text, len) : NULL;
    if (!releases || releases->kind != JSON_ARRAY) {
        if (!quiet) fprintf(stderr, "tide: GitHub's list of releases didn't make sense\n");
        return NULL;
    }
    return releases;
}

// The newest version of `channel` (see tide_release_pick). Stable asks GitHub
// for its latest release too: nightly ones can push it out of the list.
static const json *newest(const char *work, const json *releases, const char *channel)
{
    const bool ask = strcmp(channel, "stable") == 0 && !sys_env("TIDE_RELEASES_URL");
    const json *latest = ask ? fetch_json(work, RELEASES_API "/latest", "latest.json") : NULL;
    return tide_release_pick(releases, latest, channel, PACKAGE);
}

// The release of `version`, in the list or, for an older one, asked by its tag.
static const json *exactly(const char *work, const json *releases, const char *version)
{
    const json *found = tide_release_find(releases, version);
    if (found || sys_env("TIDE_RELEASES_URL")) return found;
    char url[512];
    snprintf(url, sizeof url, RELEASES_API "/tags/v%s", version);
    const json *release = fetch_json(work, url, "tag.json");
    return tide_release_is(release, version) ? release : NULL;
}

static const char *asset_url(const json *release, const char *name, const bool authenticated)
{
    const json *asset = tide_release_asset(release, name);
    const char *api = authenticated ? json_str(json_get(asset, "url")) : NULL;
    return api ? api : json_str(json_get(asset, "browser_download_url"));
}

// The asset's size in bytes, as GitHub gives it; 0 if it doesn't.
static int64_t asset_size(const json *release, const char *name)
{
    const json *size = json_get(tide_release_asset(release, name), "size");
    return size && size->kind == JSON_NUMBER && size->number > 0 ? (int64_t)size->number : 0;
}

// ---------------------------------------------------------------------------
// What an upgrade shows: a bar for each step that takes a while, drawn over
// itself on its line of the terminal, and a line once it's done.

// Draws how far a step is: a bar when its size is known, a spinner when it
// isn't.
static void draw_progress(const char *step, int64_t got, const int64_t size, const int64_t ms, const int frame,
                          const bool done)
{
    const double mb = 1024.0 * 1024.0;
    const double seconds = (double)ms / 1000.0;
    if (got < 0) got = 0;
    if (size > 0 && (done || got > size)) got = size;
    char line[160];
    int n;
    if (size > 0) {
        enum { WIDTH = 24 };
        const int filled = (int)(got * WIDTH / size);
        char bar[WIDTH + 1];
        for (int i = 0; i < WIDTH; i++) bar[i] = i < filled ? '=' : i == filled ? '>' : ' ';
        bar[WIDTH] = '\0';
        n = snprintf(line, sizeof line, "%-11s [%s] %3d%%  %.1f / %.1f MB", step, bar, (int)(got * 100 / size), got / mb,
                     size / mb);
    } else if (done) {
        n = snprintf(line, sizeof line, "%-11s %.1f MB", step, got / mb);
    } else {
        n = snprintf(line, sizeof line, "%-11s %c %.1f MB", step, "|/-\\"[frame % 4], got / mb);
    }
    if (done) snprintf(line + n, sizeof line - (size_t)n, "  in %.1fs", seconds);
    else if (seconds >= 1) snprintf(line + n, sizeof line - (size_t)n, "  %.1f MB/s", got / mb / seconds);
    line_draw(line);
}

// How many bytes the files in `dir` and its folders hold so far.
static int64_t tree_size(const char *dir);

typedef struct tree {
    const char *dir;
    int64_t bytes;
} tree;

static void add_size(void *user, const char *name, const bool is_dir)
{
    tree *t = user;
    char *path = path_join(t->dir, name);
    const int64_t size = is_dir ? tree_size(path) : sys_file_size(path);
    if (size > 0) t->bytes += size;
    free(path);
}

static int64_t tree_size(const char *dir)
{
    tree t = {dir, 0};
    sys_list(dir, add_size, &t);
    return t.bytes;
}

// How far a step is, in bytes, from the file or folder it fills.
typedef int64_t (*progress_fn)(const char *path);

// Runs a step of the upgrade, a program. In a terminal it has a bar: the
// program runs in the background, `progress` says how much of `size` bytes is
// in `path`, and what the program says waits in a log until the bar is done
// with the line.
static bool run_step(const char *step, const char *const *argv, const char *work, const progress_fn progress,
                     const char *path, const int64_t size)
{
    if (!sys_is_terminal()) return sys_run(argv, NULL, false) == 0;
    char *log = path_join(work, "step.log");
    sys_process *program = sys_start_logged(argv, NULL, log);
    int code = -1;
    if (program) {
        const int64_t start = sys_now_ms();
        for (int frame = 0;; frame++) {
            const bool ended = sys_wait(program, frame ? 100 : 0, &code);
            if (!ended || code == 0) draw_progress(step, progress(path), size, sys_now_ms() - start, frame, ended);
            if (ended) break;
        }
        line_end();
        char *said = sys_read_file(log, NULL);
        if (said) fputs(said, stderr);
        free(said);
    }
    free(log);
    return code == 0;
}

// About how many bytes the package unpacks to: a zip's directory says each
// entry's size, and a gzip ends with the size of what's in it, the tar, which
// is its files and a little more. 0 if it doesn't say.
static int64_t unpacked_size(const char *package)
{
    const size_t n = strlen(package);
    if (n > 4 && strcmp(package + n - 4, ".zip") == 0) {
        const int64_t size = unzip_size(package);
        return size > 0 ? size : 0;
    }
    uint8_t tail[4] = {0};
    FILE *f = fopen(package, "rb");
    const bool ok = f && fseek(f, -4, SEEK_END) == 0 && fread(tail, 1, 4, f) == 4;
    if (f) fclose(f);
    const uint32_t size = (uint32_t)tail[0] | (uint32_t)tail[1] << 8 | (uint32_t)tail[2] << 16 | (uint32_t)tail[3] << 24;
    return ok ? (int64_t)size : 0;
}

// The hash SHA256SUMS lists for `file` ("<hash>  <file>" lines).
static bool expected_hash(const char *sums, const char *file, char out[65])
{
    for (const char *line = sums; line && *line;) {
        const char *end = strchr(line, '\n');
        const size_t n = end ? (size_t)(end - line) : strlen(line);
        const char *name = memchr(line, ' ', n);
        if (name && name - line == 64) {
            while (*name == ' ' || *name == '*') name++;
            const size_t name_len = n - (size_t)(name - line);
            const size_t file_len = strlen(file);
            if (name_len >= file_len && strncmp(name, file, file_len) == 0) {
                memcpy(out, line, 64);
                out[64] = '\0';
                return true;
            }
        }
        line = end ? end + 1 : NULL;
    }
    return false;
}

// ---------------------------------------------------------------------------

// The programs an upgrade moved aside while they ran (install.c).
static void remove_old(void *user, const char *name, const bool is_dir)
{
    const char *bin = user;
    if (is_dir || !strstr(name, ".old-")) return;
    char *path = path_join(bin, name);
    sys_remove(path); // Fails while that old copy still runs; the next run tries again
    free(path);
}

void tide_cleanup(const char *root)
{
    char *bin = path_join(root, "bin");
    sys_list(bin, remove_old, bin);
    free(bin);
}

// ---------------------------------------------------------------------------

int tide_upgrade(const char *root, const char *channel, const char *version)
{
    if (version && version[0] == 'v') version++;
    if (version && !tide_version_valid(version)) {
        fprintf(stderr, "tide: '%s' isn't a version\n", version);
        fprintf(stderr, "  = note: versions look like 0.2.0, or 0.2.0-nightly.3 for nightly ones\n");
        return 2;
    }
    // Switching channels, it installs the other one's newest even if it's older.
    const bool switching = channel && strcmp(channel, tide_channel(root)) != 0;
    if (!channel) channel = tide_channel(root);
    char *work = path_join(root, ".upgrade");
    sys_remove_tree(work);
    if (!sys_mkdirs(work)) {
        fprintf(stderr, "tide: can't write to %s, where tide is installed\n", root);
        return 1;
    }

    const json *releases = fetch_releases(work, false);
    if (!releases) return 1;
    const json *release = version ? exactly(work, releases, version) : newest(work, releases, channel);
    if (!release) {
        if (version) fprintf(stderr, "tide: there's no release called %s\n", version);
        else fprintf(stderr, "tide: there's no %s release with a package for this platform (%s) yet\n", channel, PACKAGE);
        return 1;
    }
    const char *next = tide_release_version(release);
    // Upgrading only ever goes forward; an exact version or another channel is what was asked for.
    // An installation that isn't whole (an upgrade stopped partway) is installed again, whichever
    // version its tide is.
    const bool whole = tide_installation_whole(root, TIDE_VERSION);
    const int order = tide_version_compare(next, TIDE_VERSION);
    if (whole && (order == 0 || (order < 0 && !version && !switching))) {
        if (order == 0 && version) printf("tide %s is already installed.\n", TIDE_VERSION);
        else if (order == 0) printf("tide %s is the newest %s version.\n", TIDE_VERSION, channel);
        else printf("tide %s is newer than any %s version out (%s).\n", TIDE_VERSION, channel, next);
        char *channel_file = path_join(root, "channel");
        sys_write_text(channel_file, channel);
        sys_remove_tree(work);
        return 0;
    }

    char *header = NULL;
    if (!github_auth(work, &header)) {
        fprintf(stderr, "tide: couldn't prepare GitHub authentication\n");
        return 1;
    }
    const char *package_url = asset_url(release, PACKAGE, header != NULL);
    const char *sums_url = asset_url(release, "SHA256SUMS", header != NULL);
    if (!package_url || !sums_url) {
        github_auth_free(header);
        fprintf(stderr, "tide: release %s has no package for this platform (%s)\n", next, PACKAGE);
        return 1;
    }
    if (!whole) printf("This installation isn't whole (an upgrade stopped partway): installing tide %s over it.\n", next);
    fflush(stdout);
    char *package = path_join(work, PACKAGE);
    char *sums_path = path_join(work, "SHA256SUMS");
    // The checksums come first, so the bar ends when the package is all there.
    const char *curl[40] = {sys_tool("curl"), "-q", "--fail-early"};
    int n = 3 + curl_request(curl + 3, sums_url, sums_path, "Accept: application/octet-stream", header, false);
    curl[n++] = "--next";
    curl_request(curl + n, package_url, package, "Accept: application/octet-stream", header, false);
    const bool downloaded = run_step("Downloading", curl, work, sys_file_size, package, asset_size(release, PACKAGE));
    github_auth_free(header);
    if (!downloaded) {
        fprintf(stderr, "tide: the download failed\n");
        return 1;
    }

    // The package must match the checksum published with it.
    char *sums = sys_read_file(sums_path, NULL);
    char want[65];
    if (!sums || !expected_hash(sums, PACKAGE, want)) {
        fprintf(stderr, "tide: SHA256SUMS doesn't list %s\n", PACKAGE);
        return 1;
    }
    size_t len = 0;
    char *data = sys_read_file(package, &len);
    char got[65];
    sha256_hex(data, len, got);
    free(data);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "tide: the download is damaged (its checksum doesn't match); nothing was changed\n");
        return 1;
    }

    char *fresh = path_join(work, "new");
    sys_mkdirs(fresh);
    const char *const tar[] = {sys_tool("tar"), "-xf", package, "-C", fresh, NULL};
    if (!run_step("Unpacking", tar, work, tree_size, fresh, unpacked_size(package))) {
        fprintf(stderr, "tide: couldn't unpack the download\n");
        return 1;
    }
    char *trash = path_join(work, "old");
    char failed[1024];
    if (!tide_install(root, fresh, trash, failed, sizeof failed)) {
        fprintf(stderr, "tide: couldn't replace %s\n", failed);
        fprintf(stderr, "  = note: another program may have it open, like an editor showing it or a game being built: "
                        "close it, then run `tide upgrade` again\n");
        if (tide_installation_whole(root, TIDE_VERSION)) fprintf(stderr, "  = note: nothing was changed\n");
        else fprintf(stderr, "  = note: the upgrade stopped partway, and tide builds no games until it's finished\n");
        return 1;
    }
    char *channel_file = path_join(root, "channel");
    sys_write_text(channel_file, channel);

    // The new tide updates the editor extension, in the editors that have it,
    // which takes them a while. It only says what went wrong, which waits in a
    // log for the upgrade's own line.
#ifdef _WIN32
    char *tide = path_join(root, "bin/tide.exe");
#else
    char *tide = path_join(root, "bin/tide");
#endif
    const char *const editors[] = {tide, "editors", "--update", NULL};
    char *log = path_join(work, "editors.log");
    if (sys_is_terminal() && tide_editors_have_tide()) line_draw("Updating Tide in your editors...");
    sys_process *update = sys_start_logged(editors, NULL, log);
    int code = 0;
    while (update && !sys_wait(update, 1000, &code)) {
        // Still at it
    }
    char *said = sys_read_file(log, NULL);
    free(log);
    free(tide);
    sys_remove_tree(work); // What stays, a program still running has open: the next upgrade deletes it

    char line[256];
    if (order == 0) snprintf(line, sizeof line, "Installed tide %s again (%s).", next, channel);
    else snprintf(line, sizeof line, "Upgraded tide %s -> %s (%s).", TIDE_VERSION, next, channel);
    line_draw(line);
    line_end();
    if (said) fputs(said, stderr);
    free(said);
    return 0;
}

void tide_check_for_update(const char *root)
{
    if (is_dev_build() || sys_env("TIDE_NO_UPDATE_CHECK")) return;
    char *stamp = path_join(root, ".last-update-check");
    char *text = sys_read_file(stamp, NULL);
    const long long last = text ? atoll(text) : 0;
    free(text);
    if (sys_now() - last < 24 * 60 * 60) {
        free(stamp);
        return;
    }
    char now[32];
    snprintf(now, sizeof now, "%lld", (long long)sys_now());
    sys_write_text(stamp, now);
    free(stamp);

    char *work = path_join(root, ".update-check");
    sys_mkdirs(work);
    const json *releases = fetch_releases(work, true);
    const char *channel = tide_channel(root);
    const json *release = releases ? newest(work, releases, channel) : NULL;
    if (release && tide_version_compare(tide_release_version(release), TIDE_VERSION) > 0) {
        fprintf(stderr, "\ntide %s is out (you have %s): run `tide upgrade`\n", tide_release_version(release), TIDE_VERSION);
    }
    sys_remove_tree(work);
}
