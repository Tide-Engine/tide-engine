// Packages without the network: what tide.packages says, sources as people
// write them, git's lists of branches and tags, which commit a line follows,
// and a downloaded commit's files put where packages are kept.
// compiler/cli/tests/packages_test.cmake runs tide on games.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fetch.h"
#include "packages.h"
#include "sys.h"
#include "tide_test.h"
#include "version.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static bool parses(const char *text, const char *source, const char *ref)
{
    char *got_source = NULL;
    char *got_ref = NULL;
    const bool ok = packages_parse_source(text, &got_source, &got_ref);
    const bool same = ok && strcmp(got_source, source) == 0
                   && ((!ref && !got_ref) || (ref && got_ref && strcmp(got_ref, ref) == 0));
    if (!same) fprintf(stderr, "  '%s' -> '%s' @ '%s'\n", text, got_source ? got_source : "", got_ref ? got_ref : "");
    free(got_source);
    free(got_ref);
    return same;
}

static bool rejects(const char *text)
{
    char *source = NULL;
    char *ref = NULL;
    const bool ok = packages_parse_source(text, &source, &ref);
    free(source);
    free(ref);
    return !ok;
}

TIDE_TEST(cli_packages_take_sources_as_people_write_them)
{
    TIDE_CHECK(parses("github.com/owner/repo", "github.com/owner/repo", NULL));
    TIDE_CHECK(parses("https://github.com/owner/repo", "github.com/owner/repo", NULL));
    TIDE_CHECK(parses("https://github.com/owner/repo.git", "github.com/owner/repo", NULL));
    TIDE_CHECK(parses("https://GitHub.com/Owner/Repo/", "github.com/Owner/Repo", NULL)); // Hosts ignore case
    TIDE_CHECK(parses("git@github.com:owner/repo.git", "github.com/owner/repo", NULL));
    TIDE_CHECK(parses("ssh://git@gitlab.com/group/sub/repo", "gitlab.com/group/sub/repo", NULL));
    TIDE_CHECK(parses("github.com/owner/repo@dev", "github.com/owner/repo", "dev"));
    TIDE_CHECK(parses("https://github.com/owner/repo.git@v1", "github.com/owner/repo", "v1"));
    TIDE_CHECK(parses("github.com/owner/repo//packages/physics@v2.1", "github.com/owner/repo//packages/physics", "v2.1"));
    TIDE_CHECK(parses("  ../shared  ", "../shared", NULL));
    TIDE_CHECK(parses("..\\shared", "../shared", NULL));
    TIDE_CHECK(parses("D:/games/shared", "D:/games/shared", NULL));
    TIDE_CHECK(rejects("github.com/owner"));     // No repository
    TIDE_CHECK(rejects("localhost/owner/repo")); // A host has a '.'
    TIDE_CHECK(rejects("github.com/owner/repo@"));
    TIDE_CHECK(rejects("github.com/owner/../repo"));
    TIDE_CHECK(rejects("github.com/owner/repo//"));
    TIDE_CHECK(rejects(""));
    // A name alone is a package that comes with tide. Its @ref is given, for the caller to refuse.
    TIDE_CHECK(parses("box2d", "box2d", NULL));
    TIDE_CHECK(parses("box2d@v1", "box2d", "v1"));
    TIDE_CHECK(rejects("box2d@"));
    TIDE_CHECK(rejects("Box2D"));
    TIDE_CHECK(rejects("box-2d"));
    TIDE_CHECK(rejects("2d"));
}

TIDE_TEST(cli_packages_that_come_with_tide_are_names_alone)
{
    TIDE_CHECK(packages_is_builtin("box2d"));
    TIDE_CHECK(packages_is_builtin("a"));
    TIDE_CHECK(!packages_is_builtin(""));
    TIDE_CHECK(!packages_is_builtin("Box2D"));
    TIDE_CHECK(!packages_is_builtin("2d"));
    TIDE_CHECK(!packages_is_builtin("box_2d"));
    TIDE_CHECK(!packages_is_builtin("box2d/joints"));
    TIDE_CHECK(!packages_is_builtin("box2d.tide"));
    TIDE_CHECK(!packages_is_builtin("./box2d")); // A folder
    TIDE_CHECK(!packages_is_builtin("github.com"));
    TIDE_CHECK(!packages_is_builtin("package")); // The words that start the file's other lines
    TIDE_CHECK(!packages_is_builtin("tide"));
    TIDE_CHECK(packages_is_builtin("tides"));
}

static bool relative_is(const char *from, const char *path, const char *want)
{
    char *got = packages_relative(from, path);
    const bool same = strcmp(got, want) == 0;
    if (!same) fprintf(stderr, "  %s from %s: '%s'\n", path, from, got);
    free(got);
    return same;
}

TIDE_TEST(cli_packages_write_folders_relative_to_the_game)
{
    TIDE_CHECK(relative_is("D:/games/mine/", "D:/games/shared/", "../shared"));
    TIDE_CHECK(relative_is("D:/games/mine/", "D:/games/mine/physics/", "./physics"));
    TIDE_CHECK(relative_is("D:/games/mine/", "D:/games/mine/", "."));
    TIDE_CHECK(relative_is("/home/a/game/", "/home/a/game/../lib/x/", "../lib/x"));
    TIDE_CHECK(relative_is("/home/a/game/", "/opt/packages/x/", "../../../opt/packages/x"));
#ifdef _WIN32
    TIDE_CHECK(relative_is("C:/games/", "D:/shared/", "D:/shared")); // Another drive
#endif
}

// A folder for scratch files, ending in '/'.
static const char *scratch(void)
{
    static char dir[1024];
    const char *base = sys_env("TEMP") ? sys_env("TEMP") : sys_env("TMPDIR") ? sys_env("TMPDIR") : "/tmp";
    snprintf(dir, sizeof dir, "%s/tide_packages_test/", base);
    return dir;
}

// A folder of its own for each test, with a tide.packages saying `text`.
static char *write_packages(const char *name, const char *text)
{
    char *dir = malloc(strlen(scratch()) + strlen(name) + 2);
    if (!dir) abort();
    sprintf(dir, "%s%s/", scratch(), name);
    TIDE_CHECK(sys_mkdirs(dir));
    char *path = path_join(dir, PACKAGES_FILE);
    TIDE_CHECK(sys_write_text(path, text));
    free(path);
    return dir;
}

typedef struct reports {
    int count;
    int last_line;
    char last[512];
} reports;

static void collect(void *user, const char *where, const int line, const char *message, const char *note)
{
    (void)where;
    (void)note;
    reports *r = user;
    r->count++;
    r->last_line = line;
    snprintf(r->last, sizeof r->last, "%s", message);
}

TIDE_TEST(cli_packages_files_say_what_they_are)
{
    char *dir = write_packages("read", "# A package\r\n"
                                       "package Tide.Physics\r\n"
                                       "tide 0.3  # the oldest\r\n"
                                       "\r\n"
                                       "github.com/owner/math@v1 0123456789ABCDEF0123456789abcdef01234567\r\n"
                                       "gitlab.com/group/repo//sub/folder\r\n"
                                       "../shared  # A comment after a space\r\n");
    packages_file f;
    reports r = {0};
    TIDE_CHECK(packages_read(dir, &f, collect, &r));
    TIDE_CHECK(r.count == 0);
    TIDE_CHECK(f.exists);
    TIDE_CHECK(f.name && strcmp(f.name, "Tide.Physics") == 0);
    TIDE_CHECK(f.tide[0] == 0 && f.tide[1] == 3 && f.tide[2] == 0 && f.tide_line == 3);
    TIDE_REQUIRE(f.count == 3);
    TIDE_CHECK(f.lines[0].line == 5 && !f.lines[0].local);
    TIDE_CHECK(strcmp(f.lines[0].source, "github.com/owner/math") == 0 && strcmp(f.lines[0].ref, "v1") == 0);
    TIDE_CHECK(strcmp(f.lines[0].commit, "0123456789abcdef0123456789abcdef01234567") == 0); // Written in lowercase
    TIDE_CHECK(strcmp(f.lines[1].source, "gitlab.com/group/repo//sub/folder") == 0 && !f.lines[1].commit);
    TIDE_CHECK(f.lines[2].local && strcmp(f.lines[2].source, "../shared") == 0);
    packages_free_file(&f);
    free(dir);

    // Each mistake on its line.
    dir = write_packages("mistakes", "package 3D\n"
                                     "tide three\n"
                                     "github.com/owner/repo 123\n"
                                     "../shared abc\n"
                                     "a b c\n"
                                     "../shared\n"
                                     "../shared/\n");
    r = (reports){0};
    TIDE_CHECK(!packages_read(dir, &f, collect, &r));
    TIDE_CHECK(r.count == 6);
    TIDE_CHECK(r.last_line == 7 && strstr(r.last, "listed twice")); // ../shared and ../shared/ are one folder
    packages_free_file(&f);
    free(dir);

    // A folder with no tide.packages is a game with no packages.
    char none[1100];
    snprintf(none, sizeof none, "%snone_at_all/", scratch());
    TIDE_CHECK(packages_read(none, &f, collect, &r));
    TIDE_CHECK(!f.exists && f.count == 0 && !f.name);
    packages_free_file(&f);
}

// ---------------------------------------------------------------------------
// The packages that come with tide, in an installation made up for the test:
// they're in its src/packages, which tide says as it starts

// Everything reported, a line each: "<line>: <message> (<note>)".
typedef struct said {
    char text[4096];
    size_t len;
} said;

static void say(void *user, const char *where, const int line, const char *message, const char *note)
{
    (void)where;
    said *s = user;
    if (s->len >= sizeof s->text) return;
    s->len += (size_t)snprintf(s->text + s->len, sizeof s->text - s->len, "%d: %s (%s)\n", line, message, note ? note : "");
}

static void put(const char *root, const char *file, const char *text);

// An installation with packages, in its src/packages: dice, which needs
// rolls; rolls; lonely, which needs a folder beside it; and what isn't one of
// them. Gives the installation's folder.
static char *installation(void)
{
    char *root = path_join(scratch(), "installed");
    sys_remove_tree(root);
    put(root, "src/packages/dice/" PACKAGES_FILE, "package Dice\nrolls\n");
    put(root, "src/packages/rolls/" PACKAGES_FILE, "package Rolls\n");
    put(root, "src/packages/lonely/" PACKAGES_FILE, "package Lonely\n../rolls\n");
    // Not packages that come with tide: a folder with no tide.packages, and
    // one named as none is
    put(root, "src/packages/empty/dice.tide", "namespace Empty;\n");
    put(root, "src/packages/Loud/" PACKAGES_FILE, "package Loud\n");
    return root;
}

TIDE_TEST(cli_packages_that_come_with_tide_are_in_one_folder)
{
    char *root = installation();
    char *builtin = path_join(root, "src/packages");
    // Before anything says which folder they're in, none does.
    char **names = NULL;
    TIDE_CHECK(packages_builtin_names(&names) == 0 && !names);
    TIDE_CHECK(!packages_builtin_exists("dice"));

    packages_set_builtin_folder(builtin);
    TIDE_REQUIRE(packages_builtin_names(&names) == 3);
    TIDE_CHECK(strcmp(names[0], "dice") == 0 && strcmp(names[1], "lonely") == 0 && strcmp(names[2], "rolls") == 0);
    packages_free_names(names, 3);
    TIDE_CHECK(packages_builtin_exists("dice"));
    TIDE_CHECK(!packages_builtin_exists("empty"));
    TIDE_CHECK(!packages_builtin_exists("nope"));

    // A game that lists them: each is its folder there, after what it needs.
    char *game = write_packages("builtin_game", "dice\nrolls\n");
    game_packages g;
    said s = {0};
    TIDE_CHECK(packages_resolve(game, &g, NULL, say, &s));
    TIDE_CHECK(s.len == 0);
    TIDE_REQUIRE(g.count == 2);
    TIDE_CHECK(g.file.lines[0].builtin && !g.file.lines[0].local && !g.file.lines[0].commit);
    TIDE_CHECK(strcmp(g.items[0].name, "Rolls") == 0 && strcmp(g.items[1].name, "Dice") == 0);
    char *want = path_join(builtin, "dice/");
    char *relative = packages_relative(want, g.items[1].folder);
    TIDE_CHECK(strcmp(relative, ".") == 0); // The same folder, however each is written
    free(relative);
    free(want);
    packages_free(&g);
    free(game);

    // What a package needs, the game lists, by its name.
    game = write_packages("builtin_needs", "dice\n");
    s = (said){0};
    TIDE_CHECK(!packages_resolve(game, &g, NULL, say, &s));
    TIDE_CHECK(strstr(s.text, "1: Dice needs rolls, which this file doesn't list (add this line: rolls)"));
    packages_free(&g);
    package_need *needs = NULL;
    TIDE_REQUIRE(packages_missing(game, NULL, NULL, &needs) == 1);
    TIDE_CHECK(strcmp(needs[0].line, "rolls") == 0 && strcmp(needs[0].by, "Dice") == 0);
    packages_free_needs(needs, 1);
    free(game);

    // A name no package has says which there are, the one that's close, and
    // how the folder of that name beside the file is written.
    game = write_packages("builtin_unknown", "dise\nshared\n");
    char *shared = path_join(game, "shared");
    TIDE_CHECK(sys_mkdirs(shared));
    free(shared);
    s = (said){0};
    TIDE_CHECK(!packages_resolve(game, &g, NULL, say, &s));
    TIDE_CHECK(strstr(s.text, "1: no package called dise comes with this tide "
                              "(did you mean 'dice'? It comes with dice, lonely and rolls)"));
    TIDE_CHECK(strstr(s.text, "2: no package called shared comes with this tide "
                              "(it comes with dice, lonely and rolls; a folder is written as a path, like ./shared)"));
    packages_free(&g);
    free(game);

    // One that comes with tide names what it needs as a game does, never by a folder.
    game = write_packages("builtin_folder", "lonely\nrolls\n");
    s = (said){0};
    TIDE_CHECK(!packages_resolve(game, &g, NULL, say, &s));
    TIDE_CHECK(strstr(s.text, "2: a package that comes with tide can't use a folder on this machine (../rolls)"));
    packages_free(&g);
    free(game);

    // A package from a folder can need one that comes with tide, by its name.
    char *folder = write_packages("builtin_user", "package User\ndice\n");
    free(folder);
    game = write_packages("builtin_through", "../builtin_user\ndice\nrolls\n");
    s = (said){0};
    TIDE_CHECK(packages_resolve(game, &g, NULL, say, &s));
    TIDE_REQUIRE(g.count == 3);
    TIDE_CHECK(strcmp(g.items[0].name, "Rolls") == 0 && strcmp(g.items[1].name, "Dice") == 0);
    TIDE_CHECK(strcmp(g.items[2].name, "User") == 0);
    packages_free(&g);
    free(game);

    // A tide that comes with none says so: its folder of them isn't there.
    char *none = path_join(root, "src/none");
    packages_set_builtin_folder(none);
    char *note = packages_builtin_unknown("dice", NULL);
    TIDE_CHECK(strcmp(note, "it comes with none") == 0);
    free(note);
    free(none);

    packages_set_builtin_folder(NULL);
    sys_remove_tree(root);
    free(builtin);
    free(root);
}

TIDE_TEST(cli_a_line_for_a_package_that_comes_with_tide_is_its_name_alone)
{
    char *dir = write_packages("builtin_lines", "box2d  # The version this tide came with\n"
                                                "./box2d\n"
                                                "github.com/owner/box2d 0123456789abcdef0123456789abcdef01234567\n");
    packages_file f;
    said s = {0};
    TIDE_CHECK(packages_read(dir, &f, say, &s));
    TIDE_REQUIRE(f.count == 3);
    TIDE_CHECK(f.lines[0].builtin && !f.lines[0].local && !f.lines[0].commit && !f.lines[0].ref);
    TIDE_CHECK(strcmp(f.lines[0].source, "box2d") == 0);
    TIDE_CHECK(f.lines[1].local && !f.lines[1].builtin); // A folder of the same name is still a folder
    TIDE_CHECK(!f.lines[2].local && !f.lines[2].builtin);
    // Its folder is in the one tide says they're in, and nothing said here.
    char *folder = packages_folder(&f.lines[0], f.folder);
    TIDE_CHECK(!folder);
    packages_free_file(&f);
    free(dir);

    // It takes no commit and follows nothing, and `package` and `tide` are never names.
    dir = write_packages("builtin_mistakes", "box2d 0123456789abcdef0123456789abcdef01234567\n"
                                             "box2d@v1\n"
                                             "box2d\n"
                                             "box2d\n"
                                             "package\n"
                                             "tide\n"
                                             "Box2D\n");
    s = (said){0};
    TIDE_CHECK(!packages_read(dir, &f, say, &s));
    TIDE_CHECK(f.count == 1);
    TIDE_CHECK(strstr(s.text, "1: a package that comes with tide needs no commit (a name alone, like `box2d`, is a "
                              "package that comes with tide: it's the version this tide came with)"));
    TIDE_CHECK(strstr(s.text, "2: a package that comes with tide follows no branch or version"));
    TIDE_CHECK(strstr(s.text, "4: box2d is listed twice"));
    TIDE_CHECK(strstr(s.text, "5: `package` needs a name"));
    TIDE_CHECK(strstr(s.text, "6: `tide` needs a version"));
    TIDE_CHECK(strstr(s.text, "7: 'Box2D' isn't a package"));
    packages_free_file(&f);
    free(dir);
}

// Git's list as a host sends it: the service, then each ref, the first with
// what the server can do after a NUL, and annotated tags with the commit
// they're on after them.
static const char REFS[] = "001e# service=git-upload-pack\n"
                           "0000"
                           "0064aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa HEAD\0multi_ack symref=HEAD:refs/heads/main agent=git/x\n"
                           "003daaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa refs/heads/main\n"
                           "003cbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb refs/heads/dev\n"
                           "003e1111111111111111111111111111111111111111 refs/tags/v1.0.0\n"
                           "00411010101010101010101010101010101010101010 refs/tags/v1.0.0^{}\n"
                           "003e2222222222222222222222222222222222222222 refs/tags/v1.2.0\n"
                           "003f3333333333333333333333333333333333333333 refs/tags/v1.10.1\n"
                           "00444444444444444444444444444444444444444444 refs/tags/v1.11.0-rc.1\n"
                           "003d5555555555555555555555555555555555555555 refs/tags/2.0.0\n"
                           "003b6666666666666666666666666666666666666666 refs/tags/dev\n"
                           "0000";

TIDE_TEST(cli_packages_read_gits_list_of_branches_and_tags)
{
    git_refs refs;
    TIDE_REQUIRE(git_refs_parse(REFS, sizeof REFS - 1, &refs));
    TIDE_CHECK(refs.head && strcmp(refs.head, "refs/heads/main") == 0);
    TIDE_CHECK(refs.count == 9); // HEAD, 2 branches and 6 tags; the peeled line is the tag before it

    const char *name = NULL;
    // The default branch
    TIDE_CHECK(strcmp(git_refs_resolve(&refs, NULL, &name), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 0);
    TIDE_CHECK(strcmp(name, "refs/heads/main") == 0);
    // A branch; a branch over a tag of the same name
    TIDE_CHECK(strcmp(git_refs_resolve(&refs, "dev", &name), "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") == 0);
    // Versions: the newest tag of them, by number, leaving out pre-releases
    TIDE_CHECK(strcmp(git_refs_resolve(&refs, "v1", &name), "3333333333333333333333333333333333333333") == 0);
    TIDE_CHECK(strcmp(name, "refs/tags/v1.10.1") == 0);
    TIDE_CHECK(strcmp(git_refs_resolve(&refs, "v1.2", &name), "2222222222222222222222222222222222222222") == 0);
    TIDE_CHECK(strcmp(git_refs_resolve(&refs, "2", &name), "5555555555555555555555555555555555555555") == 0); // Without the v
    // An annotated tag is the commit it's on.
    TIDE_CHECK(strcmp(git_refs_resolve(&refs, "v1.0.0", &name), "1010101010101010101010101010101010101010") == 0);
    TIDE_CHECK(git_refs_resolve(&refs, "v3", &name) == NULL && name == NULL);
    TIDE_CHECK(git_refs_resolve(&refs, "nope", &name) == NULL);
    git_refs_free(&refs);

    // Without a symref, HEAD itself; and what isn't git's list at all.
    static const char BARE[] = "0032cccccccccccccccccccccccccccccccccccccccc HEAD\n0000";
    TIDE_REQUIRE(git_refs_parse(BARE, sizeof BARE - 1, &refs));
    TIDE_CHECK(strcmp(git_refs_resolve(&refs, NULL, &name), "cccccccccccccccccccccccccccccccccccccccc") == 0);
    git_refs_free(&refs);
    TIDE_CHECK(!git_refs_parse("<html>not found</html>", 22, &refs));
    TIDE_CHECK(!git_refs_parse("00ffshort", 9, &refs));
}

// ---------------------------------------------------------------------------
// A commit's files, unpacked, put where packages are kept

static void put(const char *root, const char *file, const char *text)
{
    char *path = path_join(root, file);
    char *dir = path_dir(path);
    TIDE_CHECK(sys_mkdirs(dir));
    TIDE_CHECK(sys_write_text(path, text));
    free(dir);
    free(path);
}

static bool says(const char *root, const char *file, const char *text)
{
    char *path = path_join(root, file);
    char *got = sys_read_file(path, NULL);
    const bool same = got && strcmp(got, text) == 0;
    free(got);
    free(path);
    return same;
}

// A folder of its own for a test of placing, made anew, which the test
// removes once it's done: `files` gets where a commit is unpacked, and `dir`
// where it goes, ending in '/' as packages_repo_dir's does.
static const char *place_folders(const char *name, char files[1200], char dir[1200])
{
    static char work[1100];
    snprintf(work, sizeof work, "%s%s", scratch(), name);
    sys_remove_tree(work);
    snprintf(files, 1200, "%s/.downloads/1/files", work);
    snprintf(dir, 1200, "%s/github.com/test/physics/0123456789abcdef0123456789abcdef01234567/", work);
    return work;
}

// A repository with a package in a folder of it, as tar unpacks its archive.
static void unpack(const char *files)
{
    put(files, "README.md", "Physics\n");
    put(files, "packages/physics/tide.packages", "package Physics\n");
    put(files, "packages/physics/body.tide", "namespace Physics;\n");
    put(files, "packages/physics/joints/hinge.tide", "namespace Physics.Joints;\n");
}

static void check_placed(const char *dir)
{
    TIDE_CHECK(packages_fetched(dir));
    TIDE_CHECK(says(dir, "README.md", "Physics\n"));
    TIDE_CHECK(says(dir, "packages/physics/tide.packages", "package Physics\n"));
    TIDE_CHECK(says(dir, "packages/physics/body.tide", "namespace Physics;\n"));
    TIDE_CHECK(says(dir, "packages/physics/joints/hinge.tide", "namespace Physics.Joints;\n"));
}

TIDE_TEST(cli_a_package_is_fetched_once_its_last_file_is_in)
{
    char files[1200], dir[1200], failed[1024];
    const char *work = place_folders("place", files, dir);
    unpack(files);
    // The repository's own file of the marker's name isn't what says so.
    put(files, PACKAGES_FETCHED, "the repository's");
    TIDE_CHECK(!packages_fetched(dir));
    TIDE_CHECK(fetch_place(files, dir, failed, sizeof failed));
    TIDE_CHECK(failed[0] == '\0');
    check_placed(dir);
    TIDE_CHECK(says(dir, PACKAGES_FETCHED, ""));
    sys_remove_tree(work);
}

// What a download that stopped partway leaves: some of the files, and nothing
// that says they're all there.
TIDE_TEST(cli_a_fetch_goes_on_from_one_that_stopped)
{
    char files[1200], dir[1200], failed[1024];
    const char *work = place_folders("stopped", files, dir);
    put(dir, "README.md", "Physics\n");
    put(dir, "packages/physics/body.tide", "namespace Physics;\n");
    TIDE_CHECK(!packages_fetched(dir));
    unpack(files);
    TIDE_CHECK(fetch_place(files, dir, failed, sizeof failed));
    check_placed(dir);
    // Again, over a commit that's all there, as a second tide's would be.
    unpack(files);
    TIDE_CHECK(fetch_place(files, dir, failed, sizeof failed));
    check_placed(dir);
    sys_remove_tree(work);
}

#ifdef _WIN32

// Only Windows keeps a folder from moving while a program has a file in it
// open, however the program shares the file.

TIDE_TEST(cli_a_fetch_gets_past_a_file_antivirus_has_open)
{
    char files[1200], dir[1200], failed[1024];
    const char *work = place_folders("scanned", files, dir);
    unpack(files);
    // A file as antivirus reads one that was just written: sharing everything.
    char *scanned = path_join(files, "packages/physics/body.tide");
    const DWORD everything = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    const HANDLE antivirus = CreateFileA(scanned, GENERIC_READ, everything, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    TIDE_REQUIRE(antivirus != INVALID_HANDLE_VALUE);
    // What fetching tripped on: the folder it's in, at any depth, doesn't move.
    char *moved = path_join(work, "moved");
    TIDE_CHECK(!sys_rename(files, moved));
    TIDE_CHECK(fetch_place(files, dir, failed, sizeof failed));
    check_placed(dir);
    CloseHandle(antivirus);
    free(moved);
    free(scanned);
    sys_remove_tree(work);
}

TIDE_TEST(cli_a_fetch_that_cant_move_a_file_says_which_and_isnt_fetched)
{
    char files[1200], dir[1200], failed[1024];
    const char *work = place_folders("held", files, dir);
    unpack(files);
    // A file as a program that doesn't share it has it, which nothing moves.
    char *held = path_join(files, "packages/physics/body.tide");
    const HANDLE program = CreateFileA(held, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    TIDE_REQUIRE(program != INVALID_HANDLE_VALUE);
    TIDE_CHECK(!fetch_place(files, dir, failed, sizeof failed));
    TIDE_CHECK(strstr(failed, "packages/physics/body.tide") != NULL);
    TIDE_CHECK(!packages_fetched(dir));
    CloseHandle(program);

    // The next download finishes it.
    unpack(files);
    TIDE_CHECK(fetch_place(files, dir, failed, sizeof failed));
    check_placed(dir);
    free(held);
    sys_remove_tree(work);
}

#endif

TIDE_TEST(cli_packages_compare_tide_versions_by_their_numbers)
{
    int v[3];
    TIDE_CHECK(tide_version_parse("0.3", 3, v) && v[0] == 0 && v[1] == 3 && v[2] == 0);
    TIDE_CHECK(tide_version_parse("1.20.4", 6, v) && v[1] == 20 && v[2] == 4);
    TIDE_CHECK(!tide_version_parse("1", 1, v));
    TIDE_CHECK(!tide_version_parse("1.2.3.4", 7, v));
    TIDE_CHECK(!tide_version_parse("1.x", 3, v));
    TIDE_CHECK(!tide_version_parse("1.", 2, v));
    TIDE_CHECK(tide_version_at_least(0, 0, 0));
    TIDE_CHECK(tide_version_at_least(0, 1, 0)); // This tide is 0.2 or newer
    TIDE_CHECK(!tide_version_at_least(999, 0, 0));
}

// ---------------------------------------------------------------------------
// A game made in Unity: the whole of a project's Assets folder, whose packages
// are the folders in it that say they're one, with no lines for them

// Two folders' paths alike but for slashes (TEMP has Windows' own) and, on Windows, case
static bool same_folder(const char *a, const char *b)
{
    for (; *a || *b; a++, b++) {
        char x = *a == (char)92 ? '/' : *a;
        char y = *b == (char)92 ? '/' : *b;
#ifdef _WIN32
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
#endif
        if (x != y) return false;
    }
    return true;
}

TIDE_TEST(cli_a_unity_projects_assets_is_a_game_whose_packages_are_found)
{
    char *project = path_join(scratch(), "unity_project");
    sys_remove_tree(project);
    put(project, "ProjectSettings/ProjectVersion.txt", "m_EditorVersion: 6000.6.0f1\n");
    put(project, "Assets/Game/game.tide", "scene Main { }\n");
    put(project, "Assets/Physics/" PACKAGES_FILE, "package Physics\n");
    put(project, "Assets/Physics/Inner/" PACKAGES_FILE, "package Inner\n"); // A package's own folders are its business
    put(project, "Assets/Kit/Tools/" PACKAGES_FILE, "package Tools\n");
    put(project, "Assets/Samples~/Extra/" PACKAGES_FILE, "package Extra\n"); // Folders Unity leaves out
    put(project, "Assets/.hidden/" PACKAGES_FILE, "package Hidden\n");
    char *assets = path_join(project, "Assets/");
    char *game = path_join(project, "Assets/Game/");
    char *physics = path_join(project, "Assets/Physics/");
    TIDE_CHECK(packages_unity_assets(assets));
    TIDE_CHECK(!packages_unity_assets(game));
    TIDE_CHECK(!packages_unity_assets(project));

    // A command run in a folder of it works on Assets, unless that's a package's own folder
    char *found = packages_unity_game(game);
    TIDE_CHECK(found && same_folder(found, assets));
    free(found);
    found = packages_unity_game(assets);
    TIDE_CHECK(found && same_folder(found, assets));
    free(found);
    TIDE_CHECK(!packages_unity_game(physics));
    TIDE_CHECK(!packages_unity_game(project));

    TIDE_CHECK(packages_unity_left_out("Samples~/a.tide"));
    TIDE_CHECK(packages_unity_left_out(".tide/web/a.tide"));
    TIDE_CHECK(packages_unity_left_out("Game/Docs~/a.tide"));
    TIDE_CHECK(!packages_unity_left_out("Game/a~.tide")); // A file's own name isn't a folder
    TIDE_CHECK(!packages_unity_left_out("Game/a.tide"));

    // Physics and Tools, with no line in a tide.packages, which Assets hasn't
    game_packages g;
    said s = {0};
    TIDE_CHECK(packages_resolve(assets, &g, NULL, say, &s));
    TIDE_CHECK(s.len == 0);
    TIDE_REQUIRE(g.count == 2);
    TIDE_CHECK(strcmp(g.items[0].name, "Physics") == 0 && same_folder(g.items[0].folder, physics));
    TIDE_CHECK(strcmp(g.items[1].name, "Tools") == 0);
    TIDE_CHECK(!g.file.exists && !g.file.name);
    packages_free(&g);

    // One the game's file lists already is one package, not two
    put(project, "Assets/" PACKAGES_FILE, "./Physics/\n");
    TIDE_CHECK(packages_resolve(assets, &g, NULL, say, &s));
    TIDE_CHECK(g.count == 2 && g.file.count == 2);
    packages_free(&g);

    // A game's tide.packages in a folder of Assets says where it goes
    put(project, "Assets/Game/" PACKAGES_FILE, "# Made in the wrong folder\n");
    TIDE_CHECK(!packages_resolve(assets, &g, NULL, say, &s));
    TIDE_CHECK(strstr(s.text, "the game is the whole of Assets") && strstr(s.text, "move it to the Assets folder"));
    packages_free(&g);

    free(physics);
    free(game);
    free(assets);
    sys_remove_tree(project);
    free(project);
}
