// curl 8.14's --retry can print a transfer error yet exit with 0. Exercise
// the real package fetch with that process result and no archive, without
// depending on the installed curl or the network.
#define sys_run fixture_run
#include "../fetch.c"
#undef sys_run

static bool unpacked;

int fixture_run(const char *const *argv, const char *cwd, const bool quiet)
{
    (void)cwd;
    (void)quiet;
    if (strcmp(argv[1], "--version") == 0) return 0;
    if (strcmp(argv[1], "-fsSL") == 0) return 0; // No output was written.
    if (strcmp(argv[1], "-xzf") == 0) {
        unpacked = true;
        return 1;
    }
    abort();
}

char *packages_repo(const char *source)
{
    return dup_n(source, strlen(source));
}

char *packages_cache(void)
{
    const char *folder = "fetch_download_test";
    return dup_n(folder, strlen(folder));
}

int main(void)
{
    const char *folder = "fetch_download_test";
    sys_remove_tree(folder);
    const package_line line = {
        .source = "github.com/test/missing",
        .commit = "3333333333333333333333333333333333333333",
    };
    const bool fetched = fetch_package(NULL, &line, "fetch_download_test/package");
    sys_remove_tree(folder);
    if (fetched || unpacked) {
        fprintf(stderr, "a download with no archive must fail before unpacking\n");
        return 1;
    }
    puts("ok: a download with exit code 0 and no archive fails before unpacking");
    return 0;
}
