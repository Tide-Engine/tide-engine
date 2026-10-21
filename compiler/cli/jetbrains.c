#include "jetbrains.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys.h"
#include "unzip.h"

typedef struct jetbrains {
    const char *plugins;
    const char *archive;
    bool only_updates;
    bool have;
    int found, failed;
} jetbrains;

// Profiles are created by the IDE's first launch. Do not create settings for
// an IDE that has never run, or install into other JetBrains applications.
static bool supported_profile(const char *name)
{
    static const char *const products[] = {
        "Rider", "CLion", "IntelliJIdea", "IdeaIC", "PyCharm", "PyCharmCE",
        "WebStorm", "GoLand", "PhpStorm", "RubyMine", "RustRover", "DataGrip"
    };
    for (size_t i = 0; i < sizeof products / sizeof products[0]; i++) {
        const size_t n = strlen(products[i]);
        if (strncmp(name, products[i], n)) continue;
        int year = 0, minor = 0, end = 0;
        if (sscanf(name + n, "%4d.%1d%n", &year, &minor, &end) != 2 || end != 6) continue;
        const char *suffix = name + n + end;
        if (*suffix && strcmp(suffix, "-EAP")) continue;
        return year > 2024 || (year == 2024 && minor >= 2);
    }
    return false;
}

static bool plugin_present(const char *plugins, const char *name)
{
    char *dir = path_join(plugins, name);
    const bool present = sys_is_dir(dir);
    free(dir);
    return present;
}

// The installer archive has only these two plugin roots. unzip also checks
// paths for traversal. Count actual files so a broken/empty payload cannot
// replace a working installation.
static bool plugin_entry(void *user, const char *name, char *to, const size_t size)
{
    int *files = user;
    const bool tide = strncmp(name, "tide/", 5) == 0;
    const bool lsp = strncmp(name, "lsp4ij/", 7) == 0;
    if (!tide && !lsp) return false;
    const size_t n = strlen(name);
    if (n >= size) return false;
    if (name[n - 1] != '/') files[lsp ? 1 : 0]++;
    memcpy(to, name, n + 1);
    return true;
}

// Move a whole plugin aside before replacing it. In particular, do not write
// through an open IDE's JARs. A refused rename leaves the old plugin intact.
static bool replace_plugin(const char *stage, const char *plugins, const char *name)
{
    char *fresh = path_join(stage, name);
    char *to = path_join(plugins, name);
    char *old = path_join(stage, "old");
    const bool existed = sys_exists(to);
    bool ok = !existed || sys_rename(to, old);
    if (ok && !sys_rename(fresh, to)) {
        ok = false;
        if (existed && !sys_rename(old, to))
            fprintf(stderr, "tide: restore %s from %s before starting the IDE\n", to, old);
    }
    free(fresh);
    free(to);
    free(old);
    return ok;
}

static bool install_plugin(const jetbrains *s, const char *plugins)
{
    // Stage outside plugins: the IDE must never discover half an extraction
    // or a second copy of Tide. mkdir, not a shared fixed temporary folder.
    char *parent = path_dir(plugins);
    char name[64];
    snprintf(name, sizeof name, ".tide-plugins-%u", sys_pid());
    char *stage = path_join(parent, name);
    free(parent);
    if (sys_exists(stage)) {
        fprintf(stderr, "tide: remove the unfinished plugin installation %s and run `tide editors` again\n", stage);
        free(stage);
        return false;
    }
    int files[2] = {0};
    bool ok = sys_mkdirs(stage) && unzip(s->archive, stage, plugin_entry, files) && files[0] && files[1];
    char *lib = path_join(stage, "tide/lib");
    char *lsp_lib = path_join(stage, "lsp4ij/lib");
    ok = ok && sys_is_dir(lib) && sys_is_dir(lsp_lib) && sys_mkdirs(plugins);
    free(lib);
    free(lsp_lib);
    // An independently installed LSP4IJ may be newer and serve other plugins.
    if (ok && !plugin_present(plugins, "lsp4ij")) ok = replace_plugin(stage, plugins, "lsp4ij");
    if (ok) ok = replace_plugin(stage, plugins, "tide");
    // Keep a backup if rollback itself failed. Never delete the user's only
    // remaining copy while reporting the recovery path above.
    char *old = path_join(stage, "old");
    if (ok || !sys_exists(old)) sys_remove_tree(stage);
    free(old);
    free(stage);
    return ok;
}

static void visit_profile(void *user, const char *name, const bool is_dir)
{
    jetbrains *s = user;
    if (!is_dir || !supported_profile(name)) return;
    char *data = path_join(s->plugins, name);
#if defined(_WIN32) || defined(__APPLE__)
    char *plugins = path_join(data, "plugins");
    free(data);
#else
    char *plugins = data;
#endif
    const bool have = plugin_present(plugins, "tide");
    s->have |= have;
    s->found++;
    if (s->archive && (!s->only_updates || have)) {
        if (!sys_exists(s->archive) || !install_plugin(s, plugins)) {
            fprintf(stderr, "tide: couldn't %s Tide in %s; close the IDE and run `tide editors` again "
                            "(reinstall tide if %s is missing or damaged)\n",
                    s->only_updates ? "update" : "add", name, s->archive);
            s->failed++;
        } else if (!s->only_updates) {
            printf("Added Tide to %s. Restart the IDE to load it.\n", name);
        }
    }
    free(plugins);
}

static void visit_profiles(jetbrains *s)
{
    const char *home = sys_env("USERPROFILE");
    if (!home) home = sys_env("HOME");
#if defined(_WIN32)
    const char *appdata = sys_env("APPDATA");
    char *profiles = appdata ? path_join(appdata, "JetBrains") : NULL;
    char *plugins = profiles ? path_join(profiles, "") : NULL;
#elif defined(__APPLE__)
    char *profiles = home ? path_join(home, "Library/Application Support/JetBrains") : NULL;
    char *plugins = profiles ? path_join(profiles, "") : NULL;
#else
    const char *config = sys_env("XDG_CONFIG_HOME");
    const char *data = sys_env("XDG_DATA_HOME");
    char *config_root = config ? path_join(config, "") : home ? path_join(home, ".config") : NULL;
    char *data_root = data ? path_join(data, "") : home ? path_join(home, ".local/share") : NULL;
    char *profiles = config_root ? path_join(config_root, "JetBrains") : NULL;
    char *plugins = data_root ? path_join(data_root, "JetBrains") : NULL;
    free(config_root);
    free(data_root);
#endif
    (void)home;
    s->plugins = plugins;
    if (profiles && plugins) sys_list(profiles, visit_profile, s);
    free(profiles);
    free(plugins);
}

bool tide_jetbrains_have_tide(void)
{
    jetbrains s = {0};
    visit_profiles(&s);
    return s.have;
}

int tide_jetbrains(const char *root, const bool only_updates, int *found)
{
    char *archive = path_join(root, "editors/tide-jetbrains-installer.zip");
    jetbrains s = {.archive = archive, .only_updates = only_updates};
    visit_profiles(&s);
    *found += s.found;
    free(archive);
    return s.failed ? 1 : 0;
}
