// The platform's word that the machine is about to sleep (src/sleep.c), from
// a logind that isn't: a D-Bus daemon of the test's own, with this program
// owning logind's name on it, answering Inhibit with one end of a pipe and
// saying PrepareForSleep. The pipe's other end says whether the lock is held:
// logind waits for it to be closed.
//
// Linux only. Without dbus-daemon or libdbus the test can't run, and says so
// with exit code 77, which CTest counts as skipped.

#define _DEFAULT_SOURCE // pipe, poll, kill, posix_spawnp, setenv under strict C

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "dbus.h"
#include "tide/platform.h"

#define LOGIND "org.freedesktop.login1"
#define LOGIND_PATH "/org/freedesktop/login1"
#define LOGIND_MANAGER "org.freedesktop.login1.Manager"
#define SKIP 77

static int failures;
static const tide_dbus *bus;
static DBusConnection *logind; // The test's logind
static int locks;              // Inhibit calls it answered
static int lock_end = -1;      // The read end of the last lock's pipe

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

static DBusConnection *join(const char *address)
{
    DBusConnection *c = bus->connection_open_private(address, NULL);
    if (c && !bus->bus_register(c, NULL)) return NULL;
    if (c) bus->connection_set_exit_on_disconnect(c, 0);
    return c;
}

// What the test's logind does with what came: answers Inhibit with a pipe's write end
static void serve(void)
{
    bus->connection_read_write(logind, 10);
    for (DBusMessage *m; (m = bus->connection_pop_message(logind)) != NULL;) {
        const char *what = "", *who = "", *why = "", *mode = "";
        if (bus->message_is_method_call(m, LOGIND_MANAGER, "Inhibit")
            && bus->message_get_args(m, NULL, TIDE_DBUS_TYPE_STRING, &what, TIDE_DBUS_TYPE_STRING, &who,
                                     TIDE_DBUS_TYPE_STRING, &why, TIDE_DBUS_TYPE_STRING, &mode, TIDE_DBUS_TYPE_INVALID)) {
            check(strcmp(what, "sleep") == 0 && strcmp(mode, "delay") == 0, "the lock asked for is a delay of sleep");
            int ends[2];
            if (pipe(ends) == 0) {
                if (lock_end >= 0) close(lock_end);
                lock_end = ends[0];
                DBusMessage *reply = bus->message_new_method_return(m);
                const int write_end = ends[1];
                bus->message_append_args(reply, TIDE_DBUS_TYPE_UNIX_FD, &write_end, TIDE_DBUS_TYPE_INVALID);
                bus->connection_send(logind, reply, NULL);
                bus->message_unref(reply);
                close(ends[1]); // The message kept a copy of its own, which goes with it
                locks++;
            }
        }
        bus->message_unref(m);
    }
    bus->connection_flush(logind);
}

// Whether the platform holds the last lock: its pipe's write end is open somewhere
static bool held(void)
{
    struct pollfd p = {.fd = lock_end, .events = POLLIN};
    return lock_end >= 0 && poll(&p, 1, 0) == 0;
}

static void say_sleep(DBusConnection *from, const bool start)
{
    DBusMessage *signal = bus->message_new_signal(LOGIND_PATH, LOGIND_MANAGER, "PrepareForSleep");
    const dbus_bool_t value = start;
    bus->message_append_args(signal, TIDE_DBUS_TYPE_BOOLEAN, &value, TIDE_DBUS_TYPE_INVALID);
    bus->connection_send(from, signal, NULL);
    bus->message_unref(signal);
    bus->connection_flush(from);
}

// Frames until the platform says the machine sleeps, serving meanwhile; -1 if it never did
static int frames_until_slept(const int most)
{
    for (int frame = 0; frame < most; frame++) {
        serve();
        if (tide_platform_slept()) return frame;
    }
    return -1;
}

int main(void)
{
    bus = tide_dbus_load();
    if (!bus) {
        printf("skipped: no libdbus\n");
        return SKIP;
    }
    // A bus of the test's own, whose address it prints on a pipe
    int address_pipe[2];
    if (pipe(address_pipe) != 0) return 1;
    char print[32];
    snprintf(print, sizeof print, "--print-address=%d", address_pipe[1]);
    char *argv[] = {"dbus-daemon", "--session", "--nofork", print, NULL};
    extern char **environ;
    pid_t daemon = 0;
    if (posix_spawnp(&daemon, "dbus-daemon", NULL, NULL, argv, environ) != 0) {
        printf("skipped: no dbus-daemon\n");
        return SKIP;
    }
    close(address_pipe[1]);
    char address[512] = "";
    size_t got = 0;
    for (ssize_t n; got < sizeof address - 1 && (n = read(address_pipe[0], address + got, sizeof address - 1 - got)) > 0;) {
        got += (size_t)n;
        if (memchr(address, '\n', got)) break;
    }
    address[got] = '\0';
    char *newline = strchr(address, '\n');
    if (newline) *newline = '\0';
    if (!address[0]) {
        printf("skipped: dbus-daemon gave no address\n");
        kill(daemon, SIGTERM);
        waitpid(daemon, NULL, 0);
        return SKIP;
    }

    logind = join(address);
    check(logind && bus->bus_request_name(logind, LOGIND, TIDE_DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL)
                        == TIDE_DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER,
          "the test's logind has logind's name on its bus");
    DBusConnection *stranger = join(address);
    setenv("TIDE_SLEEP_BUS", address, 1);

    if (logind && stranger) {
        // The platform asks for a lock as it starts, and says nothing meanwhile
        check(frames_until_slept(100) < 0 && locks == 1, "the platform holds sleep up from the start");
        check(held(), "and holds the lock");

        // Somebody that isn't logind says the machine sleeps: nothing happens
        say_sleep(stranger, true);
        check(frames_until_slept(50) < 0 && held(), "PrepareForSleep from anyone but logind is no sleep");

        // logind says it: the platform says so once, for the host to leave its match, and lets the lock go after
        say_sleep(logind, true);
        check(frames_until_slept(100) >= 0, "the platform says the machine is about to sleep when logind does");
        check(held(), "and holds the lock for that frame, the one the host leaves its match in");
        serve();
        check(!tide_platform_slept() && !held(), "and lets it go at the next");

        // It woke: a new lock, for the next sleep
        say_sleep(logind, false);
        check(frames_until_slept(100) < 0 && locks == 2 && held(), "waking, it takes a lock again, and says no sleep");

        // The bus goes away: no more locks, and the program goes on
        bus->connection_close(logind);
        bus->connection_unref(logind);
        logind = NULL;
        kill(daemon, SIGTERM);
        waitpid(daemon, NULL, 0);
        daemon = 0;
        bool any = false;
        for (int i = 0; i < 20; i++) any = tide_platform_slept() || any;
        check(!any && !held(), "a bus that goes away lets the lock go and ends nothing");
    }
    if (stranger) {
        bus->connection_close(stranger);
        bus->connection_unref(stranger);
    }
    if (daemon) {
        kill(daemon, SIGTERM);
        waitpid(daemon, NULL, 0);
    }
    return failures ? 1 : 0;
}
