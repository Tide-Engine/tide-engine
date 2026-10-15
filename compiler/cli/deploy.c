#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE
#endif
#include "deploy.h"
#include "build.h"
#include "json.h"
#include "sys.h"
#include "tide/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifndef TIDE_VERSION
#define TIDE_VERSION "0.0.0-dev"
#endif

static char *copy(const char *s)
{
    if (!s) return NULL;
    char *p = malloc(strlen(s) + 1);
    if (!p) abort();
    strcpy(p, s);
    return p;
}
static bool private_write(const char *path, const char *data, const size_t size)
{
#ifdef _WIN32
    // This directory is under this user's LocalAppData and inherits its ACL.
    return sys_write_file(path, data, size);
#else
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    bool ok = fchmod(fd, 0600) == 0;
    size_t at = 0;
    while (ok && at < size) {
        const ssize_t n = write(fd, data + at, size - at);
        if (n <= 0) ok = false;
        else at += (size_t)n;
    }
    if (close(fd)) ok = false;
    return ok;
#endif
}
static char *credentials_folder(void)
{
    const char *base = sys_env("LOCALAPPDATA");
#ifndef _WIN32
    base = sys_env("HOME");
#endif
    if (!base) return NULL;
    char *folder = path_join(base,
#ifdef _WIN32
        "Tide/hosting"
#else
        ".tide/hosting"
#endif
    );
    if (!sys_mkdirs(folder)) { free(folder); return NULL; }
#ifndef _WIN32
    if (chmod(folder, 0700)) { free(folder); return NULL; }
#endif
    return folder;
}
static bool save_login(const char *folder, const char *origin, const char *token)
{
    jbuf b = {0};
    jb_put(&b, "{\"origin\":"); jb_string(&b, origin);
    jb_put(&b, ",\"token\":"); jb_string(&b, token); jb_put(&b, "}");
    char *path = path_join(folder, "login");
    bool ok;
#ifdef _WIN32
    DATA_BLOB input = {.cbData = (DWORD)b.len, .pbData = (BYTE *)b.data}, output = {0};
    ok = CryptProtectData(&input, L"Tide hosting login", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &output) != 0;
    if (ok) { ok = private_write(path, (const char *)output.pbData, output.cbData); LocalFree(output.pbData); }
#else
    ok = private_write(path, b.data, b.len);
#endif
    jb_free(&b); free(path);
    return ok;
}
static char *load_login(const char *folder, const char *origin)
{
    const char *environment = sys_env("TIDE_HOSTING_TOKEN");
    if (environment) return copy(environment);
    char *path = path_join(folder, "login");
    size_t size = 0;
    char *bytes = sys_read_file(path, &size);
    free(path);
    if (!bytes || size > 16384) { free(bytes); return NULL; }
#ifdef _WIN32
    DATA_BLOB input = {.cbData = (DWORD)size, .pbData = (BYTE *)bytes}, output = {0};
    if (!CryptUnprotectData(&input, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &output)) { free(bytes); return NULL; }
    free(bytes);
    size = output.cbData;
    bytes = malloc(size + 1);
    if (!bytes) abort();
    memcpy(bytes, output.pbData, size); bytes[size] = 0;
    LocalFree(output.pbData);
#endif
    const json *login = json_parse(bytes, size);
    const char *saved_origin = json_str(json_get(login, "origin"));
    char *token = saved_origin && strcmp(saved_origin, origin) == 0 ? copy(json_str(json_get(login, "token"))) : NULL;
    json_release(); free(bytes);
    return token;
}

typedef struct hosting_http { const char *origin, *folder, *token; } hosting_http;
// No redirects: publishing credentials are bound to the configured service.
static char *request(const hosting_http *h, const char *method, const char *route,
                     const char *body, const char *upload)
{
    char url[2048], filename[96];
    if (snprintf(url, sizeof url, "%s%s", h->origin, route) >= (int)sizeof url) return NULL;
    snprintf(filename, sizeof filename, "request-%u", sys_pid());
    char *auth = path_join(h->folder, filename);
    snprintf(filename, sizeof filename, "body-%u", sys_pid());
    char *input = path_join(h->folder, filename);
    snprintf(filename, sizeof filename, "reply-%u", sys_pid());
    char *output = path_join(h->folder, filename);
    jbuf headers = {0};
    if (h->token) {
        const size_t len = strlen(h->token);
        if (len < 20 || len > 128 || strspn(h->token, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != len) {
            fputs("tide: invalid hosting token; run tide login\n", stderr); goto failed;
        }
        jb_printf(&headers, "Authorization: Bearer %s\n", h->token);
    }
    jb_put(&headers, upload ? "Content-Type: application/octet-stream\n" : "Content-Type: application/json\n");
    if (!private_write(auth, headers.data, headers.len) || (body && !private_write(input, body, strlen(body)))) goto failed;
    char auth_arg[2048], input_arg[2048];
    snprintf(auth_arg, sizeof auth_arg, "@%s", auth);
    snprintf(input_arg, sizeof input_arg, "@%s", upload ? upload : input);
    const char *args[32] = {sys_tool("curl"), "--silent", "--show-error", "--max-time", "60", "--max-filesize", "1048576",
        "--request", method, "--header", auth_arg, "--output", output, "--write-out", "%{http_code}"};
    int n = 15;
    args[n++] = "--globoff";
    if (body || upload) { args[n++] = "--data-binary"; args[n++] = input_arg; }
    args[n++] = url; args[n] = NULL;
    char status[16] = {0};
    const int code = sys_capture(args, status, sizeof status);
    sys_remove(auth); sys_remove(input);
    size_t size = 0;
    char *reply = code == 0 && sys_file_size(output) <= (1 << 20) ? sys_read_file(output, &size) : NULL;
    sys_remove(output);
    const int http = atoi(status);
    if (http < 200 || http >= 300 || !reply || size > (1u << 20)) {
        const json *error = reply ? json_parse(reply, size) : NULL;
        const char *message = json_str(json_get(error, "error"));
        fprintf(stderr, "tide: hosting request failed (%d): %s\n", http, message ? message : "check the service address and login");
        json_release(); free(reply); reply = NULL;
    }
    free(auth); free(input); free(output); jb_free(&headers);
    return reply;
failed:
    sys_remove(auth); sys_remove(input); sys_remove(output);
    free(auth); free(input); free(output); jb_free(&headers);
    return NULL;
}

static int login(hosting_http *h)
{
    char *reply = request(h, "POST", "/api/device", "{}", NULL);
    if (!reply) return 1;
    const json *r = json_parse(reply, strlen(reply));
    char *device = copy(json_str(json_get(r, "device"))), *code = copy(json_str(json_get(r, "code"))),
        *url = copy(json_str(json_get(r, "verificationUrl")));
    json_release(); free(reply);
    if (!device || !code || !url || strncmp(url, h->origin, strlen(h->origin)) || url[strlen(h->origin)] != '/') {
        free(device); free(code); free(url); fputs("tide: invalid login response\n", stderr); return 1;
    }
    printf("Open %s\nApprove only if the browser shows this code: %s\n", url, code);
    fflush(stdout);
    if (!sys_hidden()) (void)sys_open_in_browser(url);
    jbuf poll = {0}; jb_put(&poll, "{\"device\":"); jb_string(&poll, device); jb_put(&poll, "}");
    free(device); free(code); free(url);
    int result = 1;
    const int64_t until = sys_now_ms() + 10 * 60 * 1000;
    while (sys_now_ms() < until) {
#ifdef _WIN32
        Sleep(5000);
#else
        sleep(5);
#endif
        reply = request(h, "POST", "/api/device/poll", poll.data, NULL);
        if (!reply) break;
        r = json_parse(reply, strlen(reply));
        const char *token = json_str(json_get(r, "token"));
        if (token) {
            result = save_login(h->folder, h->origin, token) ? 0 : 1;
            puts(result ? "Could not save the login." : "Signed in. CLI access expires after 30 days.");
        }
        const bool complete = token != NULL;
        json_release(); free(reply);
        if (complete) break;
    }
    jb_free(&poll);
    return result;
}

static void hex(const void *data, const size_t size, char out[65])
{
    uint8_t hash[32]; tide_sha256_of(data, size, hash);
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", hash[i]);
}
static char *bundle(const char *root, const char *folder, const bool server_only)
{
    char *work = tide_work_folder(folder), *directory = path_join(work, "deploy");
    sys_mkdirs(directory); free(work);
    char *server = path_join(directory, "server.wasm"), *client = path_join(directory, "client.html");
    build_options opts = {.folder = folder, .release = true, .web = true, .server = true, .managed = true, .output = server};
    char *built = tide_build(root, &opts);
    if (!built) { free(directory); free(server); free(client); return NULL; }
    free(built);
    char *identity_path = malloc(strlen(server) + 12);
    if (!identity_path) abort();
    sprintf(identity_path, "%s.simulation", server);
    char *identity = sys_read_file(identity_path, NULL); free(identity_path);
    if (!server_only) {
        opts.server = opts.managed = false; opts.output = client;
        built = tide_build(root, &opts);
        if (!built) { free(identity); free(directory); free(server); free(client); return NULL; }
        free(built);
        identity_path = malloc(strlen(client) + 12);
        if (!identity_path) abort();
        sprintf(identity_path, "%s.simulation", client);
        char *client_identity = sys_read_file(identity_path, NULL); free(identity_path);
        const bool same = identity && client_identity && strcmp(identity, client_identity) == 0;
        free(client_identity);
        if (!same) { fputs("tide: client and server simulation identities differ; deploy refused\n", stderr); free(identity); free(directory); free(server); free(client); return NULL; }
    }
    char *pack = path_join(directory, "server.pack");
    const char *paths[] = {server, pack, server_only ? NULL : client};
    const char *names[] = {"server.wasm", "server.pack", "client.html"};
    char *data[3] = {0}; size_t sizes[3] = {0};
    jbuf manifest = {0}; jb_put(&manifest, "{\"format\":1,\"tide\":"); jb_string(&manifest, TIDE_VERSION);
    jb_put(&manifest, ",\"simulation\":"); jb_string(&manifest, identity ? identity : ""); jb_put(&manifest, ",\"files\":[");
    bool comma = false;
    size_t total = 0;
    for (int i = 0; i < 3; i++) {
        if (!paths[i]) continue;
        data[i] = sys_read_file(paths[i], &sizes[i]);
        if (!data[i]) continue;
        char digest[65]; hex(data[i], sizes[i], digest);
        jb_printf(&manifest, "%s{\"name\":\"%s\",\"size\":%llu,\"sha256\":\"%s\"}", comma ? "," : "", names[i],
                  (unsigned long long)sizes[i], digest);
        comma = true; total += sizes[i];
    }
    jb_put(&manifest, "]}");
    char *path = path_join(directory, "release.tide-deploy");
    FILE *out = data[0] && total <= (32u << 20) && manifest.len <= 65536 ? fopen(path, "wb") : NULL;
    bool ok = out != NULL;
    if (out) {
        uint8_t header[12] = {'T','I','D','E','D','E','P','1'};
        for (int i = 0; i < 4; i++) header[8 + i] = (uint8_t)(manifest.len >> (8 * i));
        ok = fwrite(header, 1, sizeof header, out) == sizeof header && fwrite(manifest.data, 1, manifest.len, out) == manifest.len;
        for (int i = 0; i < 3; i++) if (data[i] && fwrite(data[i], 1, sizes[i], out) != sizes[i]) ok = false;
        if (fclose(out)) ok = false;
    }
    for (int i = 0; i < 3; i++) free(data[i]);
    jb_free(&manifest); free(identity); free(directory); free(server); free(client); free(pack);
    if (!ok) { fprintf(stderr, "tide: cannot write deployment bundle (maximum 32 MiB)\n"); sys_remove(path); free(path); return NULL; }
    return path;
}

int tide_hosting_command(const char *root, const int argc, char **argv)
{
    const bool sign_in = strcmp(argv[1], "login") == 0, sign_out = strcmp(argv[1], "logout") == 0;
    const char *origin = sys_env("TIDE_HOSTING_URL");
    if (!origin) origin = "https://console.tide-engine.dev";
    const bool local = strncmp(origin, "http://localhost:", 17) == 0 || strncmp(origin, "http://127.0.0.1:", 17) == 0;
    const char *authority = strstr(origin, "://");
    if ((!local && strncmp(origin, "https://", 8)) || !authority || !authority[3]
        || strpbrk(authority + 3, "/?#@\r\n")) {
        fputs("tide: TIDE_HOSTING_URL must be an HTTPS origin (or a local development origin), without a trailing slash\n", stderr); return 2;
    }
    bool server_only = false, bundle_only = false;
    const char *folder = ".", *project = NULL;
    for (int i = 2; i < argc; i++) {
        if (!sign_in && !sign_out && strcmp(argv[i], "--server-only") == 0) server_only = true;
        else if (!sign_in && !sign_out && strcmp(argv[i], "--bundle-only") == 0) bundle_only = true;
        else if (!sign_in && !sign_out && strcmp(argv[i], "--project") == 0 && i + 1 < argc) project = argv[++i];
        else if (!sign_in && !sign_out && argv[i][0] != '-' && strcmp(folder, ".") == 0) folder = argv[i];
        else { fprintf(stderr, "tide: %s doesn't take '%s'\n", argv[1], argv[i]); return 2; }
    }
    if (bundle_only) {
        char *path = bundle(root, folder, server_only);
        if (path) printf("Built deployment bundle: %s\n", path);
        const int result = path ? 0 : 1; free(path); return result;
    }
    char *credentials = credentials_folder();
    if (!credentials) { fputs("tide: cannot access the hosting credentials folder\n", stderr); return 1; }
    hosting_http h = {.origin = origin, .folder = credentials};
    if (sign_in) { const int result = login(&h); free(credentials); return result; }
    char *token = load_login(credentials, origin); h.token = token;
    if (!token) { fputs("tide: sign in first with tide login\n", stderr); free(credentials); return 1; }
    if (sign_out) {
        char *reply = request(&h, "POST", "/api/logout", "{}", NULL);
        const int result = reply ? 0 : 1;
        if (reply) { char *path = path_join(credentials, "login"); sys_remove(path); free(path); puts("Signed out."); }
        free(reply); free(token); free(credentials); return result;
    }
    char *reply = request(&h, "GET", "/api/account", NULL, NULL);
    if (!reply) { free(token); free(credentials); return 1; }
    const json *account = json_parse(reply, strlen(reply));
    const json *projects = json_get(account, "projects");
    char *absolute = path_absolute(folder);
    const char *name = project ? project : path_base(absolute);
    char *id = NULL, *region = copy(json_str(json_get(account, "region")));
    for (int i = 0; projects && i < projects->count; i++) {
        const json *p = projects->items[i];
        const char *pn = json_str(json_get(p, "name")), *pi = json_str(json_get(p, "id"));
        if ((pn && strcmp(pn, name) == 0) || (pi && strcmp(pi, name) == 0)) { id = copy(pi); break; }
    }
    json_release(); free(reply);
    if (!id && region) {
        jbuf create = {0}; jb_put(&create, "{\"name\":"); jb_string(&create, name);
        jb_put(&create, ",\"region\":"); jb_string(&create, region);
        jb_put(&create, ",\"preset\":\"small\",\"maximum\":1}");
        reply = request(&h, "POST", "/api/projects", create.data, NULL); jb_free(&create);
        const json *p = reply ? json_parse(reply, strlen(reply)) : NULL;
        id = copy(json_str(json_get(p, "id"))); json_release(); free(reply);
    }
    free(region);
    char *path = id ? bundle(root, absolute, server_only) : NULL;
    int result = 1;
    if (path) {
        char route[128]; snprintf(route, sizeof route, "/api/projects/%s/releases", id);
        reply = request(&h, "POST", route, NULL, path);
        if (reply) {
            const json *r = json_parse(reply, strlen(reply));
            const char *release = json_str(json_get(r, "release")), *play = json_str(json_get(r, "playUrl")),
                *endpoint = json_str(json_get(r, "endpoint"));
            if (release) {
                printf("Deployed release %s\n", release);
                if (!server_only && play) printf("Play: %s\n", play);
                if (endpoint) printf("Server: %s\n", endpoint);
                printf("Dashboard: %s/dashboard\n", origin); result = 0;
            }
            json_release(); free(reply);
        }
    }
    free(path); free(id); free(absolute); free(token); free(credentials);
    return result;
}
