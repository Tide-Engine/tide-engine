// Paths made whole (path_absolute): a file that isn't there yet, as the one a
// run with no window is given to write, is where it was typed from this
// folder, under the path it has once it's written.

#ifndef _WIN32
#define _DEFAULT_SOURCE // symlink under strict C
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "sys.h"
#include "tide_test.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#define test_dup _dup
#define test_dup2 _dup2
#define test_fileno _fileno
#define test_close _close
#else
#include <unistd.h>
#define test_dup dup
#define test_dup2 dup2
#define test_fileno fileno
#define test_close close
#endif

#define WORK "sys_test_out"

typedef struct activity_probe {
    atomic_int draws;
    int ended;
} activity_probe;

static void probe_activity(void *user, const bool done)
{
    activity_probe *probe = user;
    if (done) probe->ended++;
    else atomic_fetch_add_explicit(&probe->draws, 1, memory_order_relaxed);
}

TIDE_TEST(cli_activity_animates_and_preserves_diagnostics)
{
    FILE *capture = tmpfile();
    TIDE_REQUIRE(capture != NULL);
    fflush(stderr);
    const int saved = test_dup(test_fileno(stderr));
    TIDE_REQUIRE(saved >= 0);
    TIDE_REQUIRE(test_dup2(test_fileno(capture), test_fileno(stderr)) >= 0);
#ifdef _WIN32
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(test_fileno(stderr)));
#endif
    bool started = true, animated = true, deferred = true;
    bool native_stderr = true;
    activity_probe probe = {0};
    atomic_init(&probe.draws, 0);
    // Repeat to catch a stale descriptor/thread carrying over into the next build.
    for (int round = 0; round < 2; round++) {
        const long captured_before = ftell(capture);
        sys_activity *activity = sys_activity_begin(probe_activity, &probe);
        if (!activity) { started = false; break; }
        // More than the replay buffer, plus ANSI text and newlines.
        for (int i = 0; i < 9000; i++) fputc('a', stderr);
        fputs("\n\x1b[33mwarning\x1b[0m\n", stderr);
        fflush(stderr);
        deferred &= ftell(capture) == captured_before;
        const int before = atomic_load_explicit(&probe.draws, memory_order_relaxed);
        sys_sleep_ms(240);
        sys_activity_end(activity);
#ifdef _WIN32
        native_stderr &= GetStdHandle(STD_ERROR_HANDLE) == (HANDLE)_get_osfhandle(test_fileno(stderr));
        DWORD flags = 0;
        native_stderr &= GetHandleInformation(GetStdHandle(STD_ERROR_HANDLE), &flags) != 0
            && (flags & HANDLE_FLAG_INHERIT) != 0;
        const char *const child[] = {"cmd.exe", "/d", "/c", "echo child>&2", NULL};
#else
        const char *const child[] = {"/bin/sh", "-c", "printf 'child\\n' >&2", NULL};
#endif
        native_stderr &= sys_run(child, NULL, false) == 0;
        animated &= atomic_load_explicit(&probe.draws, memory_order_relaxed) > before;
    }
    fputs("restored\n", stderr);
    fflush(stderr);
    test_dup2(saved, test_fileno(stderr));
#ifdef _WIN32
    SetStdHandle(STD_ERROR_HANDLE, (HANDLE)_get_osfhandle(test_fileno(stderr)));
#endif
    test_close(saved);
    rewind(capture);
    bool exact = true;
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < 9000; i++) exact &= fgetc(capture) == 'a';
        const char *tail = "\n\x1b[33mwarning\x1b[0m\n";
        for (; *tail; tail++) exact &= fgetc(capture) == (unsigned char)*tail;
#ifdef _WIN32
        const char *child_text = "child\r\n";
#else
        const char *child_text = "child\n";
#endif
        for (; *child_text; child_text++) exact &= fgetc(capture) == (unsigned char)*child_text;
    }
    const char *restored = "restored\n";
    for (; *restored; restored++) exact &= fgetc(capture) == (unsigned char)*restored;
    exact &= fgetc(capture) == EOF;
    fclose(capture);
    TIDE_CHECK(started);
    TIDE_CHECK(animated);
    TIDE_CHECK(deferred);
    TIDE_CHECK(probe.ended == 2);
    TIDE_CHECK(exact);
    TIDE_CHECK(native_stderr);
}

// From the root, and with forward slashes alone: the same file from any folder.
static bool whole(const char *path)
{
    if (strchr(path, '\\')) return false;
#ifdef _WIN32
    return isalpha((unsigned char)path[0]) && path[1] == ':' && path[2] == '/';
#else
    return path[0] == '/';
#endif
}

// Whether `typed` made whole is `name` in `folder`, a path that's whole already.
static bool is_in(const char *typed, const char *folder, const char *name)
{
    char *got = path_absolute(typed);
    char *expected = path_join(folder, name);
    const bool same = strcmp(got, expected) == 0;
    if (!same) printf("    '%s' is '%s', not '%s'\n", typed, got, expected);
    free(expected);
    free(got);
    return same;
}

TIDE_TEST(absolute_path_of_a_file_not_there_yet)
{
    sys_remove_tree(WORK);
    TIDE_REQUIRE(sys_mkdirs(WORK "/shots"));
    char *here = path_absolute(".");
    char *work = path_absolute(WORK);
    char *shots = path_absolute(WORK "/shots");
    TIDE_CHECK(whole(here));
    TIDE_CHECK(is_in(WORK, here, WORK));
    TIDE_CHECK(is_in(WORK "/shots", work, "shots"));

    // A file to be written, in a folder that's there: nothing is made of it
    char *frame = path_absolute(WORK "/shots/frame.png");
    TIDE_CHECK(whole(frame));
    TIDE_CHECK(is_in(WORK "/shots/frame.png", shots, "frame.png"));
    TIDE_CHECK(!sys_exists(frame));
    // A name alone is in this folder
    TIDE_CHECK(is_in("sys_test_not_there.png", here, "sys_test_not_there.png"));
    TIDE_CHECK(!sys_exists("sys_test_not_there.png"));
    // Folders that aren't there either, which whoever writes the file may make
    TIDE_CHECK(is_in(WORK "/not/there/frame.png", work, "not/there/frame.png"));
    TIDE_CHECK(is_in(WORK "/not/", work, "not"));
    TIDE_CHECK(!sys_exists(WORK "/not"));
    // The part that's there says where it is: up from a folder is the one it's in
    TIDE_CHECK(is_in(WORK "/shots/../frame.png", work, "frame.png"));
    TIDE_CHECK(is_in("./" WORK "/shots/./frame.png", shots, "frame.png"));
    // A whole path stays what it is
    char *elsewhere = path_join(work, "not/there/frame.png");
    TIDE_CHECK(is_in(elsewhere, work, "not/there/frame.png"));
    free(elsewhere);
    // No path names nothing
    char *none = path_absolute("");
    TIDE_CHECK(strcmp(none, "") == 0);
    free(none);

    // Written through the whole path, it's the file that was typed, and its path is the same once it's there
    TIDE_CHECK(sys_write_text(frame, "a frame"));
    TIDE_CHECK(sys_exists(WORK "/shots/frame.png"));
    TIDE_CHECK(is_in(WORK "/shots/frame.png", shots, "frame.png"));

#ifndef _WIN32
    // Through a link, a file is in the folder the link names, there or not
    TIDE_REQUIRE(symlink("shots", WORK "/link") == 0);
    TIDE_CHECK(is_in(WORK "/link", work, "shots"));
    TIDE_CHECK(is_in(WORK "/link/frame.png", shots, "frame.png"));
    TIDE_CHECK(is_in(WORK "/link/later.png", shots, "later.png"));
    TIDE_CHECK(is_in(WORK "/link/../later.png", work, "later.png"));
#endif

    free(frame);
    free(shots);
    free(work);
    free(here);
    sys_remove_tree(WORK);
}

// A run's folder is its own while it has the folder's lock (open_run in build.c)
TIDE_TEST(a_locked_file_is_one_process_holders_at_a_time)
{
    sys_remove_tree(WORK);
    TIDE_REQUIRE(sys_mkdirs(WORK));
    sys_lock *held = sys_lock_file(WORK "/watch-1.lock");
    TIDE_REQUIRE(held != NULL);
    TIDE_CHECK(sys_exists(WORK "/watch-1.lock"));
    // Another holder, even in this process, waits for it to be let go of
    sys_lock *again = sys_lock_file(WORK "/watch-1.lock");
    TIDE_CHECK(again == NULL);
    // ...which leaves the others free
    sys_lock *beside = sys_lock_file(WORK "/watch-2.lock");
    TIDE_CHECK(beside != NULL);
    sys_unlock(held);
    again = sys_lock_file(WORK "/watch-1.lock");
    TIDE_CHECK(again != NULL);
    if (again) sys_unlock(again);
    if (beside) sys_unlock(beside);
    sys_remove_tree(WORK);
}
