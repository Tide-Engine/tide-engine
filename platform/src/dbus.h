#pragma once

// D-Bus on Linux (dbus.c): libdbus, loaded as the program runs, so a game
// links nothing for it and a machine without it goes without. What's here is
// what Tide uses of it: logind's word that the machine is about to sleep
// (sleep.c), and in tests, a logind that isn't (tests/sleep_logind.c).
// Declared here as libdbus has them, since its headers aren't on every
// machine that builds Tide.

#if defined(__linux__) && !defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef uint32_t dbus_bool_t;

// libdbus's DBusError, whose layout is part of its ABI
typedef struct DBusError {
    const char *name;
    const char *message;
    unsigned int dummy;
    void *padding1;
} DBusError;

enum {
    TIDE_DBUS_BUS_SYSTEM = 1,
    TIDE_DBUS_TYPE_INVALID = 0,
    TIDE_DBUS_TYPE_BOOLEAN = 'b',
    TIDE_DBUS_TYPE_STRING = 's',
    TIDE_DBUS_TYPE_UNIX_FD = 'h',
    TIDE_DBUS_MESSAGE_TYPE_METHOD_RETURN = 2,
    TIDE_DBUS_MESSAGE_TYPE_ERROR = 3,
    TIDE_DBUS_NAME_FLAG_DO_NOT_QUEUE = 4,
    TIDE_DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER = 1,
};

typedef struct tide_dbus {
    void (*error_init)(DBusError *);
    void (*error_free)(DBusError *);
    DBusConnection *(*bus_get_private)(int, DBusError *);
    DBusConnection *(*connection_open_private)(const char *, DBusError *);
    dbus_bool_t (*bus_register)(DBusConnection *, DBusError *);
    int (*bus_request_name)(DBusConnection *, const char *, unsigned int, DBusError *);
    void (*bus_add_match)(DBusConnection *, const char *, DBusError *);
    void (*connection_set_exit_on_disconnect)(DBusConnection *, dbus_bool_t);
    dbus_bool_t (*connection_read_write)(DBusConnection *, int);
    DBusMessage *(*connection_pop_message)(DBusConnection *);
    dbus_bool_t (*connection_send)(DBusConnection *, DBusMessage *, uint32_t *);
    void (*connection_flush)(DBusConnection *);
    void (*connection_close)(DBusConnection *);
    void (*connection_unref)(DBusConnection *);
    DBusMessage *(*message_new_method_call)(const char *, const char *, const char *, const char *);
    DBusMessage *(*message_new_method_return)(DBusMessage *);
    DBusMessage *(*message_new_signal)(const char *, const char *, const char *);
    dbus_bool_t (*message_append_args)(DBusMessage *, int, ...);
    dbus_bool_t (*message_get_args)(DBusMessage *, DBusError *, int, ...);
    int (*message_get_type)(DBusMessage *);
    uint32_t (*message_get_reply_serial)(DBusMessage *);
    dbus_bool_t (*message_is_signal)(DBusMessage *, const char *, const char *);
    dbus_bool_t (*message_is_method_call)(DBusMessage *, const char *, const char *);
    void (*message_unref)(DBusMessage *);
} tide_dbus;

// libdbus's functions, loaded the first time; NULL where it isn't there.
const tide_dbus *tide_dbus_load(void);

#endif
