// libdbus, loaded as the program runs (dbus.h).

#if defined(__linux__) && !defined(__ANDROID__)

#define _DEFAULT_SOURCE // dlopen under strict C

#include "dbus.h"

#include <dlfcn.h>
#include <stddef.h>
#include <string.h>

static tide_dbus d;
static bool tried, loaded;

const tide_dbus *tide_dbus_load(void)
{
    if (tried) return loaded ? &d : NULL;
    tried = true;
    void *library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!library) return NULL;
    bool all = true;
    // ISO C has no cast from an object pointer to a function's: through the bytes of one
#define FIND(field, name)                     \
    do {                                      \
        void *p = dlsym(library, name);       \
        all = all && p;                       \
        memcpy(&d.field, &p, sizeof p);       \
    } while (0)
    FIND(error_init, "dbus_error_init");
    FIND(error_free, "dbus_error_free");
    FIND(bus_get_private, "dbus_bus_get_private");
    FIND(connection_open_private, "dbus_connection_open_private");
    FIND(bus_register, "dbus_bus_register");
    FIND(bus_request_name, "dbus_bus_request_name");
    FIND(bus_add_match, "dbus_bus_add_match");
    FIND(connection_set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
    FIND(connection_read_write, "dbus_connection_read_write");
    FIND(connection_pop_message, "dbus_connection_pop_message");
    FIND(connection_send, "dbus_connection_send");
    FIND(connection_flush, "dbus_connection_flush");
    FIND(connection_close, "dbus_connection_close");
    FIND(connection_unref, "dbus_connection_unref");
    FIND(message_new_method_call, "dbus_message_new_method_call");
    FIND(message_new_method_return, "dbus_message_new_method_return");
    FIND(message_new_signal, "dbus_message_new_signal");
    FIND(message_append_args, "dbus_message_append_args");
    FIND(message_get_args, "dbus_message_get_args");
    FIND(message_get_type, "dbus_message_get_type");
    FIND(message_get_reply_serial, "dbus_message_get_reply_serial");
    FIND(message_is_signal, "dbus_message_is_signal");
    FIND(message_is_method_call, "dbus_message_is_method_call");
    FIND(message_unref, "dbus_message_unref");
#undef FIND
    loaded = all;
    return loaded ? &d : NULL;
}

#else
typedef int tide_no_dbus; // ISO C wants something in every file
#endif
