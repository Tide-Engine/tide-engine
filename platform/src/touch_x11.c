// Touches on Linux: X11's touch events (XInput 2.2), on a connection of our
// own to the window GLFW made, whose own connection drops every XInput event
// but raw motion. A client that selects a window's touches takes them from
// the system, which then makes no mouse of them for that window: they're only
// the touchscreen's, as Unity's are, and as on Windows (touch_win32.c).
//
// XWayland has them too, so a Steam Deck's screen comes this way, as long as
// Steam passes its touches through to the game rather than making a mouse of
// them itself.
//
// libX11 and libXi are loaded as the program runs, as GLFW loads them: a
// machine without them has no touch, and the program needs neither to start.

#if defined(__linux__) && !defined(__ANDROID__)

#define _DEFAULT_SOURCE // dlopen under strict C

#include "touch_x11.h"

#include <dlfcn.h>
#include <stddef.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#define QUEUE 256 // Events between two polls; more are dropped, as a finger that touches while every slot is taken

// What's used of libX11 and libXi
static struct {
    void *x11, *xi;
    Display *(*open_display)(const char *);
    int (*close_display)(Display *);
    int (*pending)(Display *);
    int (*next_event)(Display *, XEvent *);
    Bool (*get_event_data)(Display *, XGenericEventCookie *);
    void (*free_event_data)(Display *, XGenericEventCookie *);
    Bool (*query_extension)(Display *, const char *, int *, int *, int *);
    int (*sync)(Display *, Bool);
    XErrorHandler (*set_error_handler)(XErrorHandler);
    Status (*query_version)(Display *, int *, int *);
    int (*select_events)(Display *, Window, XIEventMask *, int);
    XIDeviceInfo *(*query_device)(Display *, int, int *);
    void (*free_device_info)(XIDeviceInfo *);
} x;

static Display *display; // Our own connection, while a window's touches are ours
static int opcode;       // XInput's, on it
static bool refused;     // An X error on our connection as it asked for the touches
static bool touchscreen;
static tide_touch_report queue[QUEUE];
static unsigned queue_start, queue_count;

static bool load(void)
{
    if (x.x11 && x.xi) return true;
    if (!x.x11) x.x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!x.xi) x.xi = dlopen("libXi.so.6", RTLD_NOW | RTLD_LOCAL);
    if (!x.x11 || !x.xi) return false;
    // ISO C has no cast from an object pointer to a function's: through the bytes of one
#define FIND(library, field, name)        \
    do {                                  \
        void *p = dlsym(x.library, name); \
        memcpy(&x.field, &p, sizeof p);   \
    } while (0)
    FIND(x11, open_display, "XOpenDisplay");
    FIND(x11, close_display, "XCloseDisplay");
    FIND(x11, pending, "XPending");
    FIND(x11, next_event, "XNextEvent");
    FIND(x11, get_event_data, "XGetEventData");
    FIND(x11, free_event_data, "XFreeEventData");
    FIND(x11, query_extension, "XQueryExtension");
    FIND(x11, sync, "XSync");
    FIND(x11, set_error_handler, "XSetErrorHandler");
    FIND(xi, query_version, "XIQueryVersion");
    FIND(xi, select_events, "XISelectEvents");
    FIND(xi, query_device, "XIQueryDevice");
    FIND(xi, free_device_info, "XIFreeDeviceInfo");
#undef FIND
    return x.open_display && x.close_display && x.pending && x.next_event && x.get_event_data && x.free_event_data
           && x.query_extension && x.sync && x.set_error_handler && x.query_version && x.select_events && x.query_device
           && x.free_device_info;
}

static XErrorHandler previous_handler;

// Errors on our connection are ours, and only say the touches weren't had;
// everyone else's go where they went (GLFW's, or Xlib's own)
static int on_error(Display *from, XErrorEvent *error)
{
    if (from == display) {
        refused = true;
        return 0;
    }
    return previous_handler ? previous_handler(from, error) : 0;
}

// Whether a device of the system's is a touchscreen: touches where they land
// (direct), not a touchpad's
static bool has_touchscreen(void)
{
    int count = 0;
    XIDeviceInfo *devices = x.query_device(display, XIAllDevices, &count);
    bool found = false;
    for (int i = 0; devices && i < count && !found; i++) {
        for (int k = 0; k < devices[i].num_classes && !found; k++) {
            const XIAnyClassInfo *c = devices[i].classes[k];
            found = c->type == XITouchClass && ((const XITouchClassInfo *)c)->mode == XIDirectTouch;
        }
    }
    if (devices) x.free_device_info(devices);
    return found;
}

void tide_x11_touch_attach(const unsigned long window)
{
    if (display || !window || !load()) return;
    display = x.open_display(NULL);
    if (!display) return;
    int event = 0, error = 0, major = 2, minor = 2;
    if (!x.query_extension(display, "XInputExtension", &opcode, &event, &error)
        || x.query_version(display, &major, &minor) != Success || major < 2 || (major == 2 && minor < 2)) {
        x.close_display(display);
        display = NULL;
        return;
    }
    unsigned char bits[XIMaskLen(XI_LASTEVENT)] = {0};
    XISetMask(bits, XI_TouchBegin);
    XISetMask(bits, XI_TouchUpdate);
    XISetMask(bits, XI_TouchEnd);
    XIEventMask mask = {.deviceid = XIAllMasterDevices, .mask_len = (int)sizeof bits, .mask = bits};
    refused = false;
    previous_handler = x.set_error_handler(on_error);
    x.select_events(display, (Window)window, &mask, 1);
    x.sync(display, False); // Another client took the window's touches first, or the window's gone: an error, now
    x.set_error_handler(previous_handler);
    previous_handler = NULL;
    if (refused) {
        x.close_display(display);
        display = NULL;
        return;
    }
    touchscreen = has_touchscreen();
    queue_start = queue_count = 0;
}

void tide_x11_touch_detach(void)
{
    if (display) x.close_display(display);
    display = NULL;
    touchscreen = false;
    queue_start = queue_count = 0;
}

bool tide_x11_touchscreen(void)
{
    return touchscreen;
}

static void report(const XIDeviceEvent *e, const int across, const int down)
{
    if (queue_count == QUEUE) return;
    const tide_touch_phase phase = e->evtype == XI_TouchBegin ? TIDE_TOUCH_BEGAN
                                   : e->evtype == XI_TouchEnd ? TIDE_TOUCH_ENDED
                                                              : TIDE_TOUCH_MOVED;
    tide_touch_report r = {phase, (uint32_t)e->detail, 0.0f, 0.0f};
    if (across > 0 && down > 0) {
        r.x = (float)(e->event_x / (double)across);
        r.y = (float)(e->event_y / (double)down);
    }
    queue[(queue_start + queue_count++) % QUEUE] = r;
}

bool tide_x11_take_touch(tide_touch_report *r, const int across, const int down)
{
    // What the system said since the last call, which only these touches are
    while (display && x.pending(display) > 0) {
        XEvent event;
        x.next_event(display, &event);
        XGenericEventCookie *cookie = &event.xcookie;
        if (cookie->type != GenericEvent || cookie->extension != opcode || !x.get_event_data(display, cookie)) continue;
        if (cookie->evtype == XI_TouchBegin || cookie->evtype == XI_TouchUpdate || cookie->evtype == XI_TouchEnd) {
            report(cookie->data, across, down);
        }
        x.free_event_data(display, cookie);
    }
    if (queue_count == 0) return false;
    *r = queue[queue_start];
    queue_start = (queue_start + 1) % QUEUE;
    queue_count--;
    return true;
}

#else
typedef int tide_no_x11_touch; // ISO C wants something in every file
#endif
