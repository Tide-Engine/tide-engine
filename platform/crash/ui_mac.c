// tide-crash's window on macOS (crash.h): an alert, through the Objective-C
// runtime from C, so the build needs no Objective-C, with the report's
// preview in a scrolling text view under it. An alert runs modally and its
// text doesn't change, so it shows once the status has settled (a report
// sent, or not, within the request's time limit). Return shows the report
// (its first button), Escape closes.
#define _DARWIN_C_SOURCE // usleep

#include "crash.h"

#include <CoreGraphics/CGGeometry.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <string.h>
#include <unistd.h>

typedef id (*send_fn)(id, SEL);
typedef id (*send_id_fn)(id, SEL, id);
typedef id (*send_text_fn)(id, SEL, const char *);
typedef id (*send_rect_fn)(id, SEL, CGRect);
typedef id (*send_size_fn)(id, SEL, double);
typedef void (*send_long_fn)(id, SEL, long);
typedef void (*send_bool_fn)(id, SEL, signed char);
typedef long (*send_long_back_fn)(id, SEL);

static id send(id to, const char *selector)
{
    return ((send_fn)objc_msgSend)(to, sel_registerName(selector));
}

static void send_id(id to, const char *selector, id value)
{
    ((send_id_fn)objc_msgSend)(to, sel_registerName(selector), value);
}

static void send_bool(id to, const char *selector, const bool value)
{
    ((send_bool_fn)objc_msgSend)(to, sel_registerName(selector), value ? 1 : 0);
}

static id string(const char *text)
{
    return ((send_text_fn)objc_msgSend)((id)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), text);
}

// The preview: a text view to read, select and copy, in a view that scrolls
static id preview_view(const char *text)
{
    const CGRect frame = {{0, 0}, {560, 280}};
    id scroll = ((send_rect_fn)objc_msgSend)(send((id)objc_getClass("NSScrollView"), "alloc"),
                                              sel_registerName("initWithFrame:"), frame);
    send_bool(scroll, "setHasVerticalScroller:", true);
    send_bool(scroll, "setHasHorizontalScroller:", true);
    ((send_long_fn)objc_msgSend)(scroll, sel_registerName("setBorderType:"), 2); // NSBezelBorder
    id view = ((send_rect_fn)objc_msgSend)(send((id)objc_getClass("NSTextView"), "alloc"),
                                            sel_registerName("initWithFrame:"), frame);
    send_bool(view, "setEditable:", false);
    send_bool(view, "setSelectable:", true);
    id font = ((send_size_fn)objc_msgSend)((id)objc_getClass("NSFont"), sel_registerName("userFixedPitchFontOfSize:"), 11.0);
    if (font) send_id(view, "setFont:", font);
    send_id(view, "setString:", string(text));
    send_id(scroll, "setDocumentView:", view);
    return scroll;
}

int crash_show(const crash_view *view, const crash_status status, void *user)
{
    char line[1024] = "";
    for (bool settled = status == NULL; !settled;) {
        const char *now = status(user, &settled);
        if (now) {
            strncpy(line, now, sizeof line - 1u);
            line[sizeof line - 1u] = '\0';
        }
        if (!settled) usleep(50000);
    }
    id pool = send(send((id)objc_getClass("NSAutoreleasePool"), "alloc"), "init");
    id app = send((id)objc_getClass("NSApplication"), "sharedApplication");
    ((send_long_fn)objc_msgSend)(app, sel_registerName("setActivationPolicy:"), 0); // Regular: in the Dock, in front
    send_bool(app, "activateIgnoringOtherApps:", true);
    id alert = send(send((id)objc_getClass("NSAlert"), "alloc"), "init");
    send_id(alert, "setMessageText:", string(view->heading));
    send_id(alert, "setInformativeText:", string(line));
    send_id(alert, "addButtonWithTitle:", string(view->show));
    send_id(alert, "addButtonWithTitle:", string("Close"));
    send_id(alert, "setAccessoryView:", preview_view(view->preview ? view->preview : ""));
    const long answer = ((send_long_back_fn)objc_msgSend)(alert, sel_registerName("runModal"));
    send(pool, "drain");
    return answer == 1000 ? 1 : 0; // NSAlertFirstButtonReturn
}
