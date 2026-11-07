// Whether the machine slept (tide_platform_slept): the time it spent asleep is
// a clock that counts sleep less one that doesn't, which only grows while the
// machine sleeps. The engine's own clock (tide_time_now_ns) is one that
// doesn't, so a match sees no gap where the machine slept.

#if !defined(_WIN32) && !defined(__APPLE__)
#define _DEFAULT_SOURCE // CLOCK_BOOTTIME under strict C
#endif

#include "tide/platform.h"

#if defined(_WIN32)
// Windows 7, which has QueryUnbiasedInterruptTime. MinGW's headers start older.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#elif !defined(__wasm__)
#include <time.h>
#endif

// Linux desktops: logind says before the machine sleeps
#if defined(__linux__) && !defined(__ANDROID__)
#define LOGIND_SAYS
#include <stdlib.h>
#include <unistd.h>

#include "dbus.h"
#include "tide/page.h" // tide_hidden
#endif

// Less than this asleep isn't a sleep: what the two clocks' readings differ
// by besides (Windows' tick count moves in steps of about 16 ms)
#define SLEPT_LEAST 1.0

static bool pretended; // A script's `sleep` (tide_platform_pretend_sleep)
static bool known;
static double asleep_before; // Seconds asleep since the machine started, as of the last call

// Seconds the machine spent asleep since it started, or -1 where the system
// doesn't say (the web, where the browser freezes a page it hides).
static double seconds_asleep(void)
{
#if defined(_WIN32)
    // The tick count goes on while the machine sleeps, and the unbiased interrupt time doesn't
    ULONGLONG unbiased = 0;
    if (!QueryUnbiasedInterruptTime(&unbiased)) return -1.0;
    return (double)GetTickCount64() * 1e-3 - (double)unbiased * 1e-7;
#elif defined(__APPLE__)
    mach_timebase_info_data_t base;
    if (mach_timebase_info(&base) != KERN_SUCCESS || !base.denom) return -1.0;
    const uint64_t awake = mach_absolute_time(), all = mach_continuous_time();
    return all > awake ? (double)(all - awake) * (double)base.numer / (double)base.denom * 1e-9 : 0.0;
#elif defined(__wasm__) || !defined(CLOCK_BOOTTIME)
    return -1.0;
#else
    // Linux and Android: the boot clock counts sleep, the monotonic one doesn't
    struct timespec awake, all;
    if (clock_gettime(CLOCK_MONOTONIC, &awake) != 0 || clock_gettime(CLOCK_BOOTTIME, &all) != 0) return -1.0;
    return (double)(all.tv_sec - awake.tv_sec) + (double)(all.tv_nsec - awake.tv_nsec) * 1e-9;
#endif
}

#ifdef LOGIND_SAYS
// ---------------------------------------------------------------------------
// logind (systemd's), on Linux, says on the system bus when the machine is
// about to sleep (PrepareForSleep, true) and when it woke (false). A program
// holding a "delay" lock on sleep is waited for, up to logind's limit (5
// seconds unless the system says otherwise), until it lets the lock go: so
// the machine leaves its match with a goodbye, and the others' match changes
// hands at once rather than when they stop hearing it. The lock is let go at
// the next call after the one that said so, the host having left in between;
// a new one is taken when the machine wakes.
//
// TIDE_SLEEP_BUS names another bus to find logind on, for tests
// (tests/sleep_logind.c). A run nobody watches (TIDE_HIDDEN) uses no other.

#define LOGIND "org.freedesktop.login1"
#define LOGIND_PATH "/org/freedesktop/login1"
#define LOGIND_MANAGER "org.freedesktop.login1.Manager"

static const tide_dbus *bus;
static DBusConnection *connection;
static bool asked_bus;           // Whether a connection was tried for: once
static uint32_t asking;          // The serial of a lock asked for and not answered yet, or 0
static int lock = -1;            // The lock held: logind waits for it to be closed
static bool letting_go;          // The machine is about to sleep: let the lock go at the next call

static void ask_for_lock(void)
{
    if (asking || lock >= 0) return;
    DBusMessage *call = bus->message_new_method_call(LOGIND, LOGIND_PATH, LOGIND_MANAGER, "Inhibit");
    if (!call) return;
    const char *what = "sleep", *who = "Tide", *why = "Leaving the match", *mode = "delay";
    // Its answer is read with the rest, by its serial: pop_message hands replies over as messages
    if (bus->message_append_args(call, TIDE_DBUS_TYPE_STRING, &what, TIDE_DBUS_TYPE_STRING, &who, TIDE_DBUS_TYPE_STRING,
                                 &why, TIDE_DBUS_TYPE_STRING, &mode, TIDE_DBUS_TYPE_INVALID)
        && !bus->connection_send(connection, call, &asking)) {
        asking = 0;
    }
    bus->message_unref(call);
}

static void connect_bus(void)
{
    asked_bus = true;
    const char *other = getenv("TIDE_SLEEP_BUS");
    if ((!other || !other[0]) && tide_hidden()) return;
    bus = tide_dbus_load();
    if (!bus) return;
    DBusError error;
    bus->error_init(&error);
    if (other && other[0]) {
        connection = bus->connection_open_private(other, &error);
        if (connection && !bus->bus_register(connection, &error)) {
            bus->connection_close(connection);
            bus->connection_unref(connection);
            connection = NULL;
        }
    } else {
        connection = bus->bus_get_private(TIDE_DBUS_BUS_SYSTEM, &error);
    }
    bus->error_free(&error);
    if (!connection) return;
    // A bus that goes away ends the connection, never the program, as libdbus would by default
    bus->connection_set_exit_on_disconnect(connection, 0);
    // From logind alone: the bus gives what its name's owner sends, and nobody else can own that name.
    // With no error to fill, it doesn't wait for the answer.
    bus->bus_add_match(connection,
                       "type='signal',sender='" LOGIND "',path='" LOGIND_PATH "',interface='" LOGIND_MANAGER
                       "',member='PrepareForSleep'",
                       NULL);
    ask_for_lock();
}

// Whether logind said the machine is about to sleep, since the last call
static bool logind_says_sleep(void)
{
    if (!asked_bus) connect_bus();
    if (letting_go) {
        letting_go = false;
        if (lock >= 0) close(lock);
        lock = -1;
    }
    if (!connection) return false;
    if (!bus->connection_read_write(connection, 0)) { // The bus went away
        bus->connection_close(connection);
        bus->connection_unref(connection);
        connection = NULL;
        asking = 0;
        if (lock >= 0) close(lock); // Nobody waits for it now
        lock = -1;
        return false;
    }
    bool sleeps = false;
    for (DBusMessage *m; (m = bus->connection_pop_message(connection)) != NULL;) {
        const int type = bus->message_get_type(m);
        dbus_bool_t start = 0;
        int fd = -1;
        if (asking && (type == TIDE_DBUS_MESSAGE_TYPE_METHOD_RETURN || type == TIDE_DBUS_MESSAGE_TYPE_ERROR)
            && bus->message_get_reply_serial(m) == asking) {
            asking = 0; // An error is no lock: sleep isn't held up
            if (type == TIDE_DBUS_MESSAGE_TYPE_METHOD_RETURN
                && bus->message_get_args(m, NULL, TIDE_DBUS_TYPE_UNIX_FD, &fd, TIDE_DBUS_TYPE_INVALID)) {
                lock = fd;
            }
        } else if (bus->message_is_signal(m, LOGIND_MANAGER, "PrepareForSleep")
                   && bus->message_get_args(m, NULL, TIDE_DBUS_TYPE_BOOLEAN, &start, TIDE_DBUS_TYPE_INVALID)) {
            if (start) sleeps = letting_go = true;
            else ask_for_lock(); // It woke: hold the next sleep up too
        }
        bus->message_unref(m);
    }
    return sleeps;
}
#else
static bool logind_says_sleep(void)
{
    return false;
}
#endif

bool tide_platform_slept(void)
{
    const double asleep = seconds_asleep();
    const bool told = logind_says_sleep(); // Every call, for the lock it holds and lets go
    const bool slept = told || pretended || (known && asleep >= 0.0 && asleep - asleep_before >= SLEPT_LEAST);
    pretended = false;
    if (asleep >= 0.0) {
        known = true;
        asleep_before = asleep;
    }
    return slept;
}

void tide_platform_pretend_sleep(void)
{
    pretended = true;
}
