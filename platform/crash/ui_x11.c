// tide-crash's window on Linux (crash.h): plain X11, which Wayland desktops
// run too (XWayland), with the heading, the status line, the report's
// preview in a box that scrolls (the wheel, arrows, Page Up and Down) and two
// flat buttons, drawn by hand. Enter shows the report, Escape closes. With no
// X display there's no window: the crash folder and its zip are still there,
// and the game's log said where.
#define _DEFAULT_SOURCE

#include "crash.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

enum { WIDTH = 640, HEIGHT = 460, PAD = 24, BUTTON_HEIGHT = 32 };

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

static XFontStruct *load_font(Display *display, const char *const names[3])
{
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
        if (take <= 0) take = text[0] == '\n' ? 0 : 1;
        if (take) XDrawString(display, window, gc, x, y + font->ascent, text, take);
        y += line_height;
        text += take;
        if (*text == '\n') text++;
    }
    return y;
}

// The preview's lines: where each starts in the text, and how long it is
typedef struct lines {
    const char **starts;
    int *lengths;
    int count;
} lines;

static lines split_lines(const char *text)
{
    lines l = {0};
    int most = 1;
    for (const char *c = text; *c; c++) most += *c == '\n';
    l.starts = calloc((size_t)most, sizeof *l.starts);
    l.lengths = calloc((size_t)most, sizeof *l.lengths);
    if (!l.starts || !l.lengths) return l;
    const char *start = text;
    for (const char *c = text;; c++) {
        if (*c == '\n' || !*c) {
            int length = (int)(c - start);
            if (length && start[length - 1] == '\r') length--;
            l.starts[l.count] = start;
            l.lengths[l.count++] = length;
            if (!*c) break;
            start = c + 1;
        }
    }
    return l;
}

int crash_show(const crash_view *view, const crash_status status, void *user)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) return -1;
    const int screen = DefaultScreen(display);
    const unsigned long white = rgb(display, screen, 255, 255, 255), ink = rgb(display, screen, 28, 28, 30),
                        soft = rgb(display, screen, 80, 80, 86), blue = rgb(display, screen, 37, 99, 235),
                        deep = rgb(display, screen, 29, 78, 216), line = rgb(display, screen, 200, 200, 206),
                        hover = rgb(display, screen, 242, 242, 245), box = rgb(display, screen, 247, 247, 249),
                        frame = rgb(display, screen, 220, 220, 226), code = rgb(display, screen, 40, 40, 46);
    const Window root = RootWindow(display, screen);
    const int x = (DisplayWidth(display, screen) - WIDTH) / 2, y = (DisplayHeight(display, screen) - HEIGHT) / 2;
    const Window window = XCreateSimpleWindow(display, root, x, y, WIDTH, HEIGHT, 0, ink, white);
    XStoreName(display, window, view->title);
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
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | LeaveWindowMask);
    XMapRaised(display, window);
    const GC gc = XCreateGC(display, window, 0, NULL);
    static const char *const heavy[] = {"-*-dejavu sans-bold-r-normal--16-*-*-*-*-*-iso10646-1",
                                        "-*-helvetica-bold-r-normal--17-*-*-*-*-*-*-*", "fixed"};
    static const char *const regular[] = {"-*-dejavu sans-medium-r-normal--14-*-*-*-*-*-iso10646-1",
                                          "-*-helvetica-medium-r-normal--14-*-*-*-*-*-*-*", "fixed"};
    static const char *const mono[] = {"-*-dejavu sans mono-medium-r-normal--12-*-*-*-*-*-iso10646-1",
                                       "-*-courier-medium-r-normal--12-*-*-*-*-*-*-*", "fixed"};
    XFontStruct *heading_font = load_font(display, heavy), *body_font = load_font(display, regular),
                *mono_font = load_font(display, mono);
    if (!heading_font || !body_font || !mono_font) {
        XCloseDisplay(display);
        return -1;
    }
    char body[1024] = "";
    bool settled = status == NULL;
    if (status) {
        const char *now = status(user, &settled);
        if (now) snprintf(body, sizeof body, "%s", now);
    }
    const lines preview = split_lines(view->preview ? view->preview : "");
    const int mono_line = mono_font->ascent + mono_font->descent + 1;
    button buttons[2] = {{.label = view->show, .width = 124, .primary = true}, {.label = "Close", .width = 88}};
    int right = WIDTH - PAD;
    for (int i = 1; i >= 0; i--) {
        buttons[i].x = right - buttons[i].width;
        buttons[i].y = HEIGHT - PAD - BUTTON_HEIGHT;
        right -= buttons[i].width + 8;
    }
    int over = -1, answer = 0, scroll = 0, box_lines = 1;
    bool open = true, redraw = true;
    const int fd = ConnectionNumber(display);
    while (open) {
        if (redraw && !XPending(display)) {
            redraw = false;
            XSetForeground(display, gc, white);
            XFillRectangle(display, window, gc, 0, 0, WIDTH, HEIGHT);
            XSetFont(display, gc, heading_font->fid);
            XSetForeground(display, gc, ink);
            const int below = draw_wrapped(display, window, gc, heading_font, view->heading, PAD, PAD, WIDTH - 2 * PAD);
            XSetFont(display, gc, body_font->fid);
            XSetForeground(display, gc, soft);
            const int under = draw_wrapped(display, window, gc, body_font, body, PAD, below + 8, WIDTH - 2 * PAD);
            // The preview: as many lines as fit, from the scroll's
            const int top = under + 14, bottom = HEIGHT - PAD - BUTTON_HEIGHT - 16;
            box_lines = bottom - top - 16 > mono_line ? (bottom - top - 16) / mono_line : 1;
            if (bottom > top) {
                XSetForeground(display, gc, box);
                XFillRectangle(display, window, gc, PAD, top, WIDTH - 2 * PAD, (unsigned)(bottom - top));
                XSetForeground(display, gc, frame);
                XDrawRectangle(display, window, gc, PAD, top, WIDTH - 2 * PAD - 1, (unsigned)(bottom - top - 1));
                XRectangle clip = {(short)(PAD + 1), (short)(top + 1), (unsigned short)(WIDTH - 2 * PAD - 2),
                                   (unsigned short)(bottom - top - 2)};
                XSetClipRectangles(display, gc, 0, 0, &clip, 1, Unsorted);
                XSetFont(display, gc, mono_font->fid);
                XSetForeground(display, gc, code);
                int at = top + 8;
                for (int i = scroll; i < preview.count && at + mono_line <= bottom; i++, at += mono_line) {
                    XDrawString(display, window, gc, PAD + 8, at + mono_font->ascent, preview.starts[i], preview.lengths[i]);
                }
                XSetClipMask(display, gc, None);
            }
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
                XSetFont(display, gc, body_font->fid);
                const int label = (int)strlen(b->label);
                const int label_width = XTextWidth(body_font, b->label, label);
                XSetForeground(display, gc, b->primary ? white : ink);
                XDrawString(display, window, gc, b->x + (b->width - label_width) / 2,
                            b->y + (BUTTON_HEIGHT + body_font->ascent - body_font->descent) / 2, b->label, label);
            }
            XFlush(display);
            continue;
        }
        if (!XPending(display)) {
            // Nothing to do but wait: for the status, a tenth of a second at a time, while it may change
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(fd, &ready);
            struct timeval wait = {0, 100000};
            select(fd + 1, &ready, NULL, NULL, settled ? NULL : &wait);
            if (!settled) {
                const char *now = status(user, &settled);
                if (now) {
                    snprintf(body, sizeof body, "%s", now);
                    redraw = true;
                }
            }
            continue;
        }
        XEvent event;
        XNextEvent(display, &event);
        const int most = preview.count > box_lines ? preview.count - box_lines : 0;
        switch (event.type) {
        case Expose: redraw = redraw || event.xexpose.count == 0; break;
        case MotionNotify: case LeaveNotify: {
            int now = -1;
            for (int i = 0; i < 2 && event.type == MotionNotify; i++) {
                const button *b = &buttons[i];
                if (event.xmotion.x >= b->x && event.xmotion.x < b->x + b->width && event.xmotion.y >= b->y
                    && event.xmotion.y < b->y + BUTTON_HEIGHT) now = i;
            }
            redraw = redraw || now != over;
            over = now;
            break;
        }
        case ButtonPress: // The wheel
            if (event.xbutton.button == Button4) scroll -= 3, redraw = true;
            if (event.xbutton.button == Button5) scroll += 3, redraw = true;
            break;
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
            if (key == XK_Up) scroll -= 1;
            if (key == XK_Down) scroll += 1;
            if (key == XK_Page_Up) scroll -= box_lines;
            if (key == XK_Page_Down) scroll += box_lines;
            if (key == XK_Home) scroll = 0;
            if (key == XK_End) scroll = most;
            redraw = true;
            break;
        }
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == closing) open = false;
            break;
        default: break;
        }
        if (scroll > most) scroll = most;
        if (scroll < 0) scroll = 0;
    }
    free(preview.starts);
    free(preview.lengths);
    XFreeFont(display, heading_font);
    XFreeFont(display, body_font);
    XFreeFont(display, mono_font);
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return answer;
}
