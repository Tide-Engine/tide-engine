#include "editors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jetbrains.h"
#include "sys.h"

typedef struct editor {
    const char *name;
    const char *command;    // Looked up on PATH
    const char *installed;  // Where its installer puts the command, in an install root (see install_roots)
    const char *flatpak;    // Its Flatpak's ID, on Linux
    const char *extensions; // Its extensions folder, in the home folder (Flatpaks' too)
} editor;

// The VS Code family: they all install extensions with `<command> --install-extension`.
static const editor editors[] = {
#if defined(_WIN32)
    // Batch files: VS Code's bin also has a `code` shell script, for Git Bash.
    {"VS Code", "code.cmd", "Microsoft VS Code/bin/code.cmd", NULL, ".vscode/extensions"},
    {"VS Code Insiders", "code-insiders.cmd", "Microsoft VS Code Insiders/bin/code-insiders.cmd", NULL,
     ".vscode-insiders/extensions"},
    {"Cursor", "cursor.cmd", "cursor/resources/app/bin/cursor.cmd", NULL, ".cursor/extensions"},
    {"VSCodium", "codium.cmd", "VSCodium/bin/codium.cmd", NULL, ".vscode-oss/extensions"},
    {"Windsurf", "windsurf.cmd", "Windsurf/bin/windsurf.cmd", NULL, ".windsurf/extensions"},
#elif defined(__APPLE__)
    // On macOS, the commands are only on PATH if the user asked for them.
    {"VS Code", "code", "Visual Studio Code.app/Contents/Resources/app/bin/code", NULL, ".vscode/extensions"},
    {"VS Code Insiders", "code-insiders", "Visual Studio Code - Insiders.app/Contents/Resources/app/bin/code-insiders",
     NULL, ".vscode-insiders/extensions"},
    {"Cursor", "cursor", "Cursor.app/Contents/Resources/app/bin/cursor", NULL, ".cursor/extensions"},
    {"VSCodium", "codium", "VSCodium.app/Contents/Resources/app/bin/codium", NULL, ".vscode-oss/extensions"},
    {"Windsurf", "windsurf", "Windsurf.app/Contents/Resources/app/bin/windsurf", NULL, ".windsurf/extensions"},
#else
    // Package managers put the commands on PATH; Flatpak doesn't.
    {"VS Code", "code", NULL, "com.visualstudio.code", ".vscode/extensions"},
    {"VS Code Insiders", "code-insiders", NULL, NULL, ".vscode-insiders/extensions"},
    {"Cursor", "cursor", NULL, NULL, ".cursor/extensions"},
    {"VSCodium", "codium", NULL, "com.vscodium.codium", ".vscode-oss/extensions"},
    {"Windsurf", "windsurf", NULL, NULL, ".windsurf/extensions"},
#endif
};

static const char *home_dir(void)
{
    const char *home = sys_env("USERPROFILE");
    return home ? home : sys_env("HOME");
}

// Where editors install themselves, the user's own first: %LOCALAPPDATA%\Programs
// and %ProgramFiles% on Windows, ~/Applications and /Applications on macOS.
// Each is malloc'd, or NULL when it's unknown; the list ends at `count`.
static int install_roots(char **roots)
{
    int count = 0;
#if defined(_WIN32)
    const char *local = sys_env("LOCALAPPDATA");
    const char *system = sys_env("ProgramFiles");
    roots[count++] = local ? path_join(local, "Programs") : NULL;
    roots[count++] = system ? path_join(system, "") : NULL;
#elif defined(__APPLE__)
    const char *home = home_dir();
    roots[count++] = home ? path_join(home, "Applications") : NULL;
    roots[count++] = path_join("/Applications", "");
#else
    (void)roots;
#endif
    return count;
}

// Flatpaks' commands: `flatpak run <id>`, as a program of their own, for the
// user's Flatpaks and the system's.
static char *find_flatpak(const char *id)
{
    const char *home = home_dir();
    char *user = home ? path_join(home, ".local/share/flatpak/exports/bin") : NULL;
    const char *const dirs[] = {user, "/var/lib/flatpak/exports/bin"};
    char *found = NULL;
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0] && !found; i++) {
        if (!dirs[i]) continue;
        char *path = path_join(dirs[i], id);
        if (sys_exists(path)) found = path;
        else free(path);
    }
    free(user);
    return found;
}

// The editor's command, or NULL if it isn't installed.
static char *find_editor(const editor *e)
{
    char *found = sys_which(e->command);
    if (found) return found;
    if (e->installed) {
        char *roots[4];
        const int count = install_roots(roots);
        for (int i = 0; i < count; i++) {
            if (roots[i] && !found) {
                char *path = path_join(roots[i], e->installed);
                if (sys_exists(path)) found = path;
                else free(path);
            }
            free(roots[i]);
        }
    }
    if (!found && e->flatpak) found = find_flatpak(e->flatpak);
    return found;
}

// The extension's folder is tide-engine.tide-<version>; tide-engine.tide-syntax-<version>
// is the grammar alone (tools/tide-syntax), which doesn't count.
static void find_extension(void *user, const char *name, const bool is_dir)
{
    static const char prefix[] = "tide-engine.tide-";
    const size_t n = sizeof prefix - 1;
    if (is_dir && strncmp(name, prefix, n) == 0 && name[n] >= '0' && name[n] <= '9') *(bool *)user = true;
}

// Whether the editor has the extension already.
static bool has_extension(const editor *e)
{
    const char *home = home_dir();
    if (!home) return false;
    char *dir = path_join(home, e->extensions);
    bool found = false;
    sys_list(dir, find_extension, &found);
    free(dir);
    return found;
}

bool tide_editors_have_tide(void)
{
    bool have = false;
    for (size_t i = 0; i < sizeof editors / sizeof editors[0] && !have; i++) {
        char *command = find_editor(&editors[i]);
        have = command && has_extension(&editors[i]);
        free(command);
    }
    return have || tide_jetbrains_have_tide();
}

int tide_editors(const char *root, const bool only_updates)
{
    char *vsix = path_join(root, "editors/tide.vsix");
    char *jetbrains = path_join(root, "editors/tide-jetbrains-installer.zip");
    const bool have_vsix = sys_exists(vsix);
    const bool have_jetbrains = sys_exists(jetbrains);
    free(jetbrains);
    if (!have_vsix && !have_jetbrains) {
        const bool needed = !only_updates || tide_editors_have_tide();
        if (needed) fprintf(stderr, "tide: there's no editor extension in %s; reinstall tide\n", root);
        free(vsix);
        return needed ? 1 : 0;
    }

    int found = 0;
    int added = 0;
    int failed = 0;
    for (size_t i = 0; i < sizeof editors / sizeof editors[0]; i++) {
        if (!have_vsix) break;
        const editor *e = &editors[i];
        char *command = find_editor(e);
        if (!command) continue;
        found++;
        if (only_updates && !has_extension(e)) {
            free(command);
            continue;
        }
        const char *const argv[] = {command, "--install-extension", vsix, "--force", NULL};
        // An update only says what went wrong: tide upgrade's own line is what there is to say.
        if (sys_run(argv, NULL, true) == 0) {
            if (!only_updates) printf("Added Tide to %s.\n", e->name);
            added++;
        } else {
            fprintf(stderr, "tide: couldn't %s %s (%s --install-extension failed)\n",
                    only_updates ? "update Tide in" : "add Tide to", e->name, command);
            failed++;
        }
        free(command);
    }
    if (added > 0 && !only_updates) {
        printf("Windows of these editors that are open get it once reloaded: Developer: Reload Window.\n");
    }
    failed += tide_jetbrains(root, only_updates, &found);
    if (found == 0 && !only_updates) {
        printf("tide found no supported editor or JetBrains profile.\n"
               "For a JetBrains IDE, open it once, then run `tide editors` again.\n"
               "See https://tide-engine.dev/tools/editors\n");
    }
    free(vsix);
    return failed > 0 ? 1 : 0;
}

typedef enum config_language { CONFIG_ELISP, CONFIG_VIM, CONFIG_LUA } config_language;

// Configuration is code, so quote paths for the editor, never for a shell.
// A Lua decimal escape always has three digits so the next digit stays literal.
static void config_string(const char *text, const config_language language)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '\\' || *p == '"') printf("\\%c", *p);
        else if (*p < 32 || *p == 127) {
            if (language == CONFIG_ELISP) printf("\\u%04x", (unsigned)*p);
            else if (language == CONFIG_VIM) printf("\\x%02x", (unsigned)*p);
            else printf("\\%03u", (unsigned)*p);
        } else putchar(*p);
    }
    putchar('"');
}

int tide_ide(const char *root, const int count, const char *const *args)
{
    if (count != 1 || (strcmp(args[0], "emacs") && strcmp(args[0], "vim") && strcmp(args[0], "neovim"))) {
        fprintf(stderr, "tide: ide takes emacs, vim or neovim: `tide ide emacs` (see `tide help ide`)\n");
        return 2;
    }
    const bool emacs = strcmp(args[0], "emacs") == 0;
    const bool neovim = strcmp(args[0], "neovim") == 0;
    char *runtime = path_join(root, emacs ? "editors/tide-emacs" : "editors/tide-vim");
#if defined(_WIN32)
    char *server = path_join(root, "bin/tidels.exe");
#else
    char *server = path_join(root, "bin/tidels");
#endif
    const char *files[] = {emacs ? "tide-lang-mode.el" : "ftdetect/tide.vim",
                           emacs ? NULL : "ftplugin/tide.vim",
                           emacs ? NULL : "syntax/tide.vim",
                           neovim ? "lsp/tidels.lua" : NULL};
    char *missing = NULL;
    for (size_t i = 0; i < sizeof files / sizeof files[0] && !missing; i++) {
        if (!files[i]) continue;
        char *path = path_join(runtime, files[i]);
        if (!sys_exists(path) || sys_is_dir(path)) missing = path;
        else free(path);
    }
    if (!missing && (!sys_exists(server) || sys_is_dir(server))) missing = path_absolute(server);
    if (missing) {
        fprintf(stderr, "tide: missing editor support file %s; reinstall tide (or install this build with cmake --install)\n", missing);
        free(missing); free(runtime); free(server);
        return 1;
    }
    // Vim's runtime search expands glob patterns, including directory names.
    // Backslash escaping these is not portable to Windows runtime paths.
    if (!emacs && strpbrk(runtime, "*?[]")) {
        fprintf(stderr, "tide: Vim/Neovim runtime paths cannot contain *, ?, [ or ]; "
                        "install tide in a folder without these characters\n");
        free(runtime); free(server);
        return 1;
    }

    if (emacs) {
        printf(";; Add to your Emacs init file (Emacs 29.1+).\n"
               ";; Connects to tidels automatically when opening Tide files.\n"
               "(add-to-list 'load-path ");
        config_string(runtime, CONFIG_ELISP);
        printf(")\n(require 'tide-lang-mode)\n(require 'eglot)\n(setq tide-lang-server-command '(");
        config_string(server, CONFIG_ELISP);
        printf("))\n(add-hook 'tide-lang-mode-hook #'eglot-ensure)\n");
    } else if (neovim) {
        printf("-- Add to init.lua (Neovim 0.11+).\n"
               "-- Connects to tidels automatically when opening Tide files.\n"
               "vim.opt.runtimepath:prepend(vim.fn.escape(");
        config_string(runtime, CONFIG_LUA);
        printf(", ','))\nvim.cmd('filetype plugin indent on')\nvim.cmd('syntax enable')\n"
               "vim.lsp.config('tidels', { cmd = { ");
        config_string(server, CONFIG_LUA);
        printf(" } })\nvim.lsp.enable('tidels')\n");
    } else {
        printf("\" Add to your vimrc (Vim 8+).\n"
               "\" For LSP, configure your client for filetype tide and this command:\n\" ");
        config_string(server, CONFIG_VIM);
        printf("\nlet &runtimepath = escape(");
        config_string(runtime, CONFIG_VIM);
        printf(", ',\\') . ',' . &runtimepath\nfiletype plugin indent on\nsyntax enable\n");
    }
    free(runtime); free(server);
    if (fflush(stdout) == EOF || ferror(stdout)) {
        fprintf(stderr, "tide: couldn't write the editor configuration\n");
        return 1;
    }
    return 0;
}
