// GitHub authentication, without a login or a network request. The filesystem
// is real; only credential discovery and the programs curl would run are captured.
#define sys_env upgrade_test_env
#define sys_which upgrade_test_which
#define sys_capture upgrade_test_capture
#define sys_run upgrade_test_run
#define sys_tool upgrade_test_tool
#define tide_editors_have_tide upgrade_test_editors_have_tide
#define line_draw upgrade_test_line_draw
#define line_end upgrade_test_line_end
#include "../upgrade.c"
#undef sys_env
#undef sys_which
#undef sys_capture
#undef sys_run
#undef sys_tool
#undef tide_editors_have_tide
#undef line_draw
#undef line_end

#include "tide_test.h"

static const char *env_token;
static const char *env_github_token;
static const char *login_token;
static int login_calls;
static int login_code;
static int curl_code;
static char arguments[40][1024];
static int argument_count;
static char credential[4096];

const char *upgrade_test_env(const char *name)
{
    if (strcmp(name, "GH_TOKEN") == 0) return env_token;
    if (strcmp(name, "GITHUB_TOKEN") == 0) return env_github_token;
    return NULL;
}

char *upgrade_test_which(const char *name)
{
    if (strcmp(name, "gh") != 0 || !login_token) return NULL;
    char *gh = malloc(3);
    if (!gh) abort();
    memcpy(gh, "gh", 3);
    return gh;
}

int upgrade_test_capture(const char *const *argv, char *out, const size_t size)
{
    login_calls++;
    TIDE_CHECK(strcmp(argv[0], "gh") == 0);
    TIDE_CHECK(strcmp(argv[1], "auth") == 0);
    TIDE_CHECK(strcmp(argv[2], "token") == 0);
    TIDE_CHECK(strcmp(argv[3], "--hostname") == 0);
    TIDE_CHECK(strcmp(argv[4], "github.com") == 0);
    TIDE_CHECK(argv[5] == NULL);
    snprintf(out, size, "%s", login_token);
    return login_code;
}

const char *upgrade_test_tool(const char *name)
{
    return name;
}

int upgrade_test_run(const char *const *argv, const char *cwd, const bool quiet)
{
    (void)cwd;
    (void)quiet;
    argument_count = 0;
    credential[0] = '\0';
    for (int i = 0; argv[i] && i < 40; i++) {
        snprintf(arguments[argument_count++], sizeof arguments[0], "%s", argv[i]);
        if (i && strcmp(argv[i - 1], "-H") == 0 && argv[i][0] == '@') {
            char *text = sys_read_file(argv[i] + 1, NULL);
            TIDE_CHECK(text != NULL);
            snprintf(credential, sizeof credential, "%s", text ? text : "");
            free(text);
        }
    }
    return curl_code;
}

bool upgrade_test_editors_have_tide(void) { return false; }
void upgrade_test_line_draw(const char *text) { (void)text; }
void upgrade_test_line_end(void) {}

static char *auth_work(void)
{
    char *bin = sys_exe_dir();
    char *work = path_join(bin, "upgrade-auth-test");
    free(bin);
    sys_remove_tree(work);
    sys_mkdirs(work);
    env_token = NULL;
    env_github_token = NULL;
    login_token = NULL;
    login_calls = 0;
    login_code = 0;
    curl_code = 0;
    return work;
}

static void check_header(const char *header, const char *wanted)
{
    TIDE_REQUIRE(header && header[0] == '@');
    char *text = sys_read_file(header + 1, NULL);
    TIDE_CHECK(text && strcmp(text, wanted) == 0);
    free(text);
#ifndef _WIN32
    struct stat file = {0};
    TIDE_CHECK(stat(header + 1, &file) == 0);
    TIDE_CHECK((file.st_mode & 0777) == 0600);
#endif
}

TIDE_TEST(cli_upgrade_tokens_prefer_environment_then_github_login)
{
    char *work = auth_work();
    env_token = "from-GH_TOKEN";
    env_github_token = "from-GITHUB_TOKEN";
    login_token = "from-gh-login\r\n";
    char *header = NULL;
    TIDE_REQUIRE(github_auth(work, &header));
    check_header(header, "Authorization: Bearer from-GH_TOKEN\n");
    TIDE_CHECK(login_calls == 0);
    github_auth_free(header);

    env_token = NULL;
    TIDE_REQUIRE(github_auth(work, &header));
    check_header(header, "Authorization: Bearer from-GITHUB_TOKEN\n");
    TIDE_CHECK(login_calls == 0);
    github_auth_free(header);

    env_github_token = NULL;
    TIDE_REQUIRE(github_auth(work, &header));
    check_header(header, "Authorization: Bearer from-gh-login\n");
    TIDE_CHECK(login_calls == 1);
    github_auth_free(header);

    login_token = NULL;
    TIDE_REQUIRE(github_auth(work, &header));
    TIDE_CHECK(header == NULL);
    login_token = "";
    login_code = 1; // gh is installed, with no usable login
    TIDE_REQUIRE(github_auth(work, &header));
    TIDE_CHECK(header == NULL);
    sys_remove_tree(work);
    free(work);
}

TIDE_TEST(cli_upgrade_credentials_stay_off_arguments_and_other_hosts)
{
    char *work = auth_work();
    char *path = path_join(work, "releases.json");
    char *auth_path = path_join(work, "github-auth");
    env_token = "private-test-token";
    TIDE_REQUIRE(download("https://api.github.com/repos/owner/repo/releases", path, false));
    TIDE_CHECK(strcmp(arguments[1], "-q") == 0);
    TIDE_CHECK(strcmp(credential, "Authorization: Bearer private-test-token\n") == 0);
    for (int i = 0; i < argument_count; i++) {
        TIDE_CHECK(strstr(arguments[i], env_token) == NULL);
        TIDE_CHECK(strcmp(arguments[i], "--location-trusted") != 0);
    }
    TIDE_CHECK(!sys_exists(auth_path));

    const char *const other[] = {"file:///releases.json", "https://example.com/releases",
                                "https://api.github.com.evil.test/releases", "https://api.github.com@evil.test/releases",
                                "http://api.github.com/releases"};
    for (size_t i = 0; i < sizeof other / sizeof other[0]; i++) {
        TIDE_REQUIRE(download(other[i], path, false));
        TIDE_CHECK(credential[0] == '\0');
        TIDE_CHECK(!sys_exists(auth_path));
    }

    curl_code = 22;
    TIDE_CHECK(!download("https://api.github.com/repos/owner/repo/releases", path, true));
    TIDE_CHECK(!sys_exists(auth_path)); // Failed requests leave no credential either
    free(auth_path);
    free(path);
    sys_remove_tree(work);
    free(work);
}

TIDE_TEST(cli_upgrade_rejects_header_injection_and_existing_credential_files)
{
    char *work = auth_work();
    env_token = "token\nInjected: value";
    char *header = NULL;
    TIDE_CHECK(!github_auth(work, &header));
    TIDE_CHECK(header == NULL);

    env_token = "test-token";
    char *path = path_join(work, "github-auth");
    sys_write_text(path, "existing file");
    TIDE_CHECK(!github_auth(work, &header));
    char *text = sys_read_file(path, NULL);
    TIDE_CHECK(text && strcmp(text, "existing file") == 0);
    free(text);
    free(path);
    sys_remove_tree(work);
    free(work);
}

TIDE_TEST(cli_upgrade_private_assets_use_api_urls_and_binary_accept)
{
    const char source[] = "{\"assets\":[{\"name\":\"package\",\"url\":\"https://api.github.com/repos/owner/repo/releases/assets/1\","
                          "\"browser_download_url\":\"https://github.com/owner/repo/releases/download/v1/package\"},"
                          "{\"name\":\"local\",\"browser_download_url\":\"file:///package\"}]}";
    const json *release = json_parse(source, sizeof source - 1);
    const char *api = asset_url(release, "package", true);
    TIDE_REQUIRE(api != NULL);
    TIDE_CHECK(strcmp(api, "https://api.github.com/repos/owner/repo/releases/assets/1") == 0);
    TIDE_CHECK(strcmp(asset_url(release, "package", false),
                      "https://github.com/owner/repo/releases/download/v1/package") == 0);
    TIDE_CHECK(strcmp(asset_url(release, "local", true), "file:///package") == 0);

    const char *argv[20];
    const int count = curl_request(argv, api, "package", "Accept: application/octet-stream", "@auth", false);
    bool binary = false;
    bool auth = false;
    bool redirects = false;
    for (int i = 0; i < count; i++) {
        if (strcmp(argv[i], "Accept: application/octet-stream") == 0) binary = true;
        if (strcmp(argv[i], "@auth") == 0) auth = true;
        if (strcmp(argv[i], "--proto-redir") == 0 && strcmp(argv[i + 1], "=https") == 0) redirects = true;
    }
    TIDE_CHECK(binary && auth && redirects);
    TIDE_CHECK(argv[count] == NULL);
    curl_request(argv, "file:///package", "package", "Accept: application/octet-stream", "@auth", false);
    for (int i = 0; argv[i]; i++) TIDE_CHECK(strcmp(argv[i], "@auth") != 0);
}
