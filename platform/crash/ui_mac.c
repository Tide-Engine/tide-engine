// tide-crash's window on macOS (crash.h): an alert, through the Objective-C
// runtime from C, so the build needs no Objective-C. Return shows the report
// (its first button), Escape closes.
#include "crash.h"

#include <objc/message.h>
#include <objc/runtime.h>
#include <string.h>

typedef id (*send_fn)(id, SEL);
typedef id (*send_id_fn)(id, SEL, id);
typedef id (*send_text_fn)(id, SEL, const char *);
typedef void (*send_long_fn)(id, SEL, long);
typedef void (*send_bool_fn)(id, SEL, signed char);
typedef long (*send_long_back_fn)(id, SEL);

static id send(id to, const char *selector)
{
    return ((send_fn)objc_msgSend)(to, sel_registerName(selector));
}

static id string(const char *text)
{
    return ((send_text_fn)objc_msgSend)((id)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), text);
}

int crash_ask(const char *title, const char *text)
{
    (void)title; // An alert has no title bar
    id pool = send(send((id)objc_getClass("NSAutoreleasePool"), "alloc"), "init");
    id app = send((id)objc_getClass("NSApplication"), "sharedApplication");
    ((send_long_fn)objc_msgSend)(app, sel_registerName("setActivationPolicy:"), 0); // Regular: in the Dock, in front
    ((send_bool_fn)objc_msgSend)(app, sel_registerName("activateIgnoringOtherApps:"), 1);
    const char *split = strstr(text, "\n\n");
    char heading[512];
    const size_t length = split ? (size_t)(split - text) : strlen(text);
    const size_t kept = length < sizeof heading - 1u ? length : sizeof heading - 1u;
    memcpy(heading, text, kept);
    heading[kept] = '\0';
    id alert = send(send((id)objc_getClass("NSAlert"), "alloc"), "init");
    ((send_id_fn)objc_msgSend)(alert, sel_registerName("setMessageText:"), string(heading));
    ((send_id_fn)objc_msgSend)(alert, sel_registerName("setInformativeText:"), string(split ? split + 2 : ""));
    ((send_id_fn)objc_msgSend)(alert, sel_registerName("addButtonWithTitle:"), string("Show report"));
    ((send_id_fn)objc_msgSend)(alert, sel_registerName("addButtonWithTitle:"), string("Close"));
    const long answer = ((send_long_back_fn)objc_msgSend)(alert, sel_registerName("runModal"));
    send(pool, "drain");
    return answer == 1000 ? 1 : 0; // NSAlertFirstButtonReturn
}
