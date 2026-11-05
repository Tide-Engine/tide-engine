// tide-crash's window on Linux (crash.h): plain X11, which Wayland desktops
// run too (XWayland), with the heading, the text and two flat buttons drawn
// by hand. Enter shows the report, Escape closes. With no X display there's
// no window: the crash folder is still there, and the game's log said where.
#define _DEFAULT_SOURCE

#include "crash.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 460, HEIGHT = 190, PAD = 24, BUTTON_HEIGHT = 32 };

typedef struct button {
    const char *label;
    int x, y, width;
    bool primary;
} button;

static unsigned long rgb(Display *display, const int screen, const unsigned r, const unsigned g, const unsigned b)
{
    XColor color = {.red = (unsigned short)(r * 257u), .green = (unsigned short)(g * 257u),
                    .blue = (unsigned short)(b * 257u), .flags = DoRed | DoGreen | DoBlue};
    if (!XAllocColor(display, DefaultColormap(display, screen), &color)) return BlackPixel(display, screen);
    return color.pixel;
}

static XFontStruct *load_font(Display *display, const bool bold)
{
    static const char *const regular[] = {"-*-dejavu sans-medium-r-normal--14-*-*-*-*-*-iso10646-1",
                                          "-*-helvetica-medium-r-normal--14-*-*-*-*-*-*-*", "fixed"};
    static const char *const heavy[] = {"-*-dejavu sans-bold-r-normal--16-*-*-*-*-*-iso10646-1",
                                        "-*-helvetica-bold-r-normal--17-*-*-*-*-*-*-*", "fixed"};
    const char *const *names = bold ? heavy : regular;
    for (int i = 0; i < 3; i++) {
        XFontStruct *font = XLoadQueryFont(display, names[i]);
        if (font) return font;
    }
    return NULL;
}

// Text in lines that fit `width`, from `y` down; returns where the next line would be
static int draw_wrapped(Display *display, const Window window, const GC gc, XFontStruct *font, const char *text,
                        const int x, int y, const int width)
{
    const int line_height = font->ascent + font->descent + 2;
    while (*text) {
        while (*text == ' ') text++;
        int fit = 0, last_space = -1, i = 0;
        for (; text[i] && text[i] != '\n'; i++) {
            if (XTextWidth(font, text, i + 1) > width) break;
            if (text[i] == ' ') last_space = i;
            fit = i + 1;
        }
        int take = fit;
        if (text[i] && text[i] != '\n' && last_space > 0) take = last_space;
        if (take <= 0) take = 1;
        XDrawString(display, window, gc, x, y + font->ascent, text, take);
        y += line_height;
        text += take;
        if (*text == '\n') text++;
    }
    return y;
}

int crash_ask(const char *title, const char *text)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) return -1;
    const int screen = DefaultScreen(display);
    const unsigned long white = rgb(display, screen, 255, 255, 255), ink = rgb(display, screen, 28, 28, 30),
                        soft = rgb(display, screen, 80, 80, 86), blue = rgb(display, screen, 37, 99, 235),
                        deep = rgb(display, screen, 29, 78, 216), line = rgb(display, screen, 200, 200, 206),
                        hover = rgb(display, screen, 242, 242, 245);
    const Window root = RootWindow(display, screen);
    const int x = (DisplayWidth(display, screen) - WIDTH) / 2, y = (DisplayHeight(display, screen) - HEIGHT) / 2;
    const Window window = XCreateSimpleWindow(display, root, x, y, WIDTH, HEIGHT, 0, ink, white);
    XStoreName(display, window, title);
    // A dialog's size, which the window manager keeps
    XSizeHints *hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PMinSize | PMaxSize | PPosition;
        hints->min_width = hints->max_width = WIDTH;
        hints->min_height = hints->max_height = HEIGHT;
        hints->x = x;
        hints->y = y;
        XSetWMNormalHints(display, window, hints);
        XFree(hints);
    }
    Atom closing = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &closing, 1);
    XSelectInput(display, window, ExposureMask | KeyPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask);
    XMapRaised(display, window);
    const GC gc = XCreateGC(display, window, 0, NULL);
    XFontStruct *heading_font = load_font(display, true), *body_font = load_font(display, false);
    if (!heading_font || !body_font) {
        XCloseDisplay(display);
        return -1;
    }
    const char *split = strstr(text, "\n\n");
    char heading[512];
    const size_t length = split ? (size_t)(split - text) : strlen(text);
    const size_t kept = length < sizeof heading - 1u ? length : sizeof heading - 1u;
    memcpy(heading, text, kept);
    heading[kept] = '\0';
    const char *body = split ? split + 2 : "";
    button buttons[2] = {{.label = "Show report", .width = 124, .primary = true}, {.label = "Close", .width = 88}};
    int right = WIDTH - PAD;
    for (int i = 1; i >= 0; i--) {
        buttons[i].x = right - buttons[i].width;
        buttons[i].y = HEIGHT - PAD - BUTTON_HEIGHT;
        right -= buttons[i].width + 8;
    }
    int over = -1, answer = 0;
    bool open = true;
    while (open) {
        XEvent event;
        XNextEvent(display, &event);
        bool redraw = event.type == Expose && event.xexpose.count == 0;
        switch (event.type) {
        case MotionNotify: case LeaveNotify: {
            int now = -1;
            for (int i = 0; i < 2 && event.type == MotionNotify; i++) {
                const button *b = &buttons[i];
                if (event.xmotion.x >= b->x && event.xmotion.x < b->x + b->width && event.xmotion.y >= b->y
                    && event.xmotion.y < b->y + BUTTON_HEIGHT) now = i;
            }
            redraw = now != over;
            over = now;
            break;
        }
        case ButtonRelease:
            if (event.xbutton.button == Button1 && over >= 0) {
                answer = over == 0 ? 1 : 0;
                open = false;
            }
            break;
        case KeyPress: {
            const KeySym key = XLookupKeysym(&event.xkey, 0);
            if (key == XK_Return || key == XK_KP_Enter) answer = 1, open = false;
            if (key == XK_Escape) answer = 0, open = false;
            break;
        }
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == closing) open = false;
            break;
        default: break;
        }
        if (!redraw || !open) continue;
        XSetForeground(display, gc, white);
        XFillRectangle(display, window, gc, 0, 0, WIDTH, HEIGHT);
        XSetFont(display, gc, heading_font->fid);
        XSetForeground(display, gc, ink);
        const int below = draw_wrapped(display, window, gc, heading_font, heading, PAD, PAD, WIDTH - 2 * PAD);
        XSetFont(display, gc, body_font->fid);
        XSetForeground(display, gc, soft);
        draw_wrapped(display, window, gc, body_font, body, PAD, below + 8, WIDTH - 2 * PAD);
        for (int i = 0; i < 2; i++) {
            const button *b = &buttons[i];
            if (b->primary) {
                XSetForeground(display, gc, over == i ? deep : blue);
                XFillRectangle(display, window, gc, b->x, b->y, (unsigned)b->width, BUTTON_HEIGHT);
            } else {
                XSetForeground(display, gc, over == i ? hover : white);
                XFillRectangle(display, window, gc, b->x, b->y, (unsigned)b->width, BUTTON_HEIGHT);
                XSetForeground(display, gc, line);
                XDrawRectangle(display, window, gc, b->x, b->y, (unsigned)b->width - 1u, BUTTON_HEIGHT - 1u);
            }
            const int label = (int)strlen(b->label);
            const int label_width = XTextWidth(body_font, b->label, label);
            XSetForeground(display, gc, b->primary ? white : ink);
            XDrawString(display, window, gc, b->x + (b->width - label_width) / 2,
                        b->y + (BUTTON_HEIGHT + body_font->ascent - body_font->descent) / 2, b->label, label);
        }
        XFlush(display);
    }
    XFreeFont(display, heading_font);
    XFreeFont(display, body_font);
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return answer;
}
