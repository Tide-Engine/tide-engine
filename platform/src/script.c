// Made-up input for a headless window, and scripts of it (see script.h).
//
// A script is a text file, a line an event: the frame it happens at, counted
// from 1, then what happens. Lines are in the order of their frames. A blank
// line, or one that starts with #, says nothing.
//
//     120 key d down                 a key, by its name in tide/devices.h: goes down...
//     180 key d up                   ...and up
//     200 key enter                  ...or neither: a tap, down for this frame and up at the next
//     210 mouse 640 360              the mouse goes to a place, in logical pixels from the top left
//     211 button left down           its buttons: left, right, middle, back, forward (down, up, or a click)
//     220 move 40 -8                 the mouse moves by so much: what turns a camera whose cursor is locked
//     230 scroll 0 -1                its wheel turns, y positive away from the user
//     260 text K7QF2M                characters typed, to the line's end
//     300 touch 1 down 100 200       a finger, by a number of the script's: touches a place...
//     310 touch 1 move 140 200       ...moves...
//     320 touch 1 up                 ...and lifts, there or where it was
//     330 pad buttonSouth down       a gamepad's buttons, by name (dpad.up and the like too)
//     331 pad leftStick 0.5 1        ...its sticks, from -1 to 1, y up...
//     332 pad rightTrigger 1         ...and its triggers, from 0 to 1
//     400 capture fight.png          the frame, once it drew, as a PNG beside the script
//     500 quit                       the run ends once this frame drew, with exit code 0
//
// What a line does at a frame is in the devices at that frame's poll, as a
// player's input since the frame before would be.

#include "script.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "font.h" // tide_utf8_next

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

// Keys and buttons by the names tide/devices.h has them under, in its order,
// which is tide_key's and tide_pad_button's (window.h).
#define NAME(name) #name,
#define DPAD_NAME(name) "dpad." #name,
static const char *const key_names[] = {TIDE_KEYBOARD_KEYS(NAME)};
static const char *const mouse_names[] = {TIDE_MOUSE_BUTTONS(NAME)};
static const char *const pad_names[] = {TIDE_GAMEPAD_BUTTONS(NAME) TIDE_DPAD_BUTTONS(DPAD_NAME)};
static const char *const stick_names[] = {TIDE_GAMEPAD_STICKS(NAME)};
static const char *const trigger_names[] = {TIDE_GAMEPAD_TRIGGERS(NAME)};
#undef NAME
#undef DPAD_NAME

_Static_assert(COUNT_OF(key_names) == TIDE_KEY_COUNT && COUNT_OF(pad_names) == TIDE_PAD_COUNT,
               "a name for every key and gamepad button");

// ---------------------------------------------------------------------------
// The devices, as a window would say them (see window.h)

static struct {
    // Held, and pressed since last asked: a tap between two frames reads as held for one
    bool keys_held[TIDE_KEY_COUNT], keys_tapped[TIDE_KEY_COUNT];
    uint32_t mouse_held, mouse_tapped;
    float x, y;               // Where events last put the mouse: outside the window until one does
    float locked_x, locked_y; // ...and where it was when the cursor was locked, which it's said to stay at
    float moved_x, moved_y;   // What it moved while locked, since last asked
    float wheel_x, wheel_y;
    bool locked;
    tide_window_gamepad pad;
    uint32_t pad_tapped;
    uint32_t typed[1024]; // Characters not yet taken: more than fit are dropped
    uint32_t typed_start, typed_count;
    tide_touch_report touches[64]; // Touch events not yet taken, the same
    uint32_t touch_start, touch_count;
    struct {
        uint32_t source;
        float x, y;
        bool down;
    } fingers[TIDE_TOUCHES]; // Where each finger that touches is, for one that lifts without saying where
    bool touchscreen;        // There's one once a finger touched
} made = {.x = -1.0f, .y = -1.0f};

bool tide_script_key_held(const tide_key key)
{
    const bool held = made.keys_held[key] || made.keys_tapped[key];
    made.keys_tapped[key] = false;
    return held;
}

void tide_script_mouse_state(tide_window_mouse *out)
{
    *out = (tide_window_mouse){
        .x = made.locked ? made.locked_x : made.x,
        .y = made.locked ? made.locked_y : made.y,
        .wheel_x = made.wheel_x,
        .wheel_y = made.wheel_y,
        .buttons = made.mouse_held | made.mouse_tapped,
        .locked = made.locked,
        .moved_x = made.locked ? made.moved_x : 0.0f,
        .moved_y = made.locked ? made.moved_y : 0.0f,
    };
    made.wheel_x = made.wheel_y = made.moved_x = made.moved_y = 0.0f;
    made.mouse_tapped = 0;
}

void tide_script_gamepad_state(tide_window_gamepad *out)
{
    *out = made.pad;
    out->buttons |= made.pad_tapped;
    made.pad_tapped = 0;
}

bool tide_script_touchscreen(void)
{
    return made.touchscreen;
}

bool tide_script_take_touch(tide_touch_report *report)
{
    if (made.touch_count == 0) return false;
    *report = made.touches[made.touch_start];
    made.touch_start = (made.touch_start + 1u) % COUNT_OF(made.touches);
    made.touch_count--;
    return true;
}

uint32_t tide_script_take_char(void)
{
    if (made.typed_count == 0) return 0;
    const uint32_t c = made.typed[made.typed_start];
    made.typed_start = (made.typed_start + 1u) % COUNT_OF(made.typed);
    made.typed_count--;
    return c;
}

void tide_script_cursor_lock(const bool lock)
{
    if (lock == made.locked) return;
    if (lock) made.locked_x = made.x, made.locked_y = made.y;
    else made.x = made.locked_x, made.y = made.locked_y; // Letting go puts the cursor back where it was
    made.locked = lock;
    made.moved_x = made.moved_y = 0.0f;
}

// ---------------------------------------------------------------------------
// Events

typedef enum event_kind {
    EVENT_KEY,
    EVENT_BUTTON,
    EVENT_MOUSE,
    EVENT_MOVE,
    EVENT_SCROLL,
    EVENT_TEXT,
    EVENT_TOUCH,
    EVENT_PAD_BUTTON,
    EVENT_PAD_STICK,
    EVENT_PAD_TRIGGER,
    EVENT_CAPTURE, // A script's own, from here on: not input
    EVENT_QUIT,
} event_kind;

// What a key or button does: a tap is down for a frame, and up at the next.
enum { PRESS_DOWN, PRESS_UP, PRESS_TAP };

typedef struct event {
    uint32_t frame, line; // A script's: when it happens, and where the file says so
    event_kind kind;
    uint32_t which; // The key, button, stick, trigger or finger
    uint32_t press; // PRESS_*, or a touch's tide_touch_phase
    bool placed;    // A touch that says where
    float x, y;
    char *text; // What's typed, or a capture's file
} event;

static void mouse_by(const float dx, const float dy)
{
    made.x += dx, made.y += dy;
    if (made.locked) made.moved_x += dx, made.moved_y += dy;
}

static void finger_event(const event *e)
{
    int slot = -1;
    for (int i = 0; i < TIDE_TOUCHES; i++) {
        if (made.fingers[i].down && made.fingers[i].source == e->which) slot = i;
    }
    if (e->press == TIDE_TOUCH_BEGAN) {
        if (slot >= 0) return; // It touches already
        for (int i = 0; i < TIDE_TOUCHES && slot < 0; i++) {
            if (!made.fingers[i].down) slot = i;
        }
        if (slot < 0) return; // More fingers than a touchscreen follows
        made.fingers[slot].source = e->which;
        made.fingers[slot].down = true;
        made.touchscreen = true;
    }
    if (slot < 0) return; // A finger that never touched can't move or lift
    if (e->placed) made.fingers[slot].x = e->x, made.fingers[slot].y = e->y;
    if (e->press == TIDE_TOUCH_ENDED) made.fingers[slot].down = false;
    if (made.touch_count == COUNT_OF(made.touches)) return;
    made.touches[(made.touch_start + made.touch_count++) % COUNT_OF(made.touches)] =
        (tide_touch_report){(tide_touch_phase)e->press, e->which, made.fingers[slot].x, made.fingers[slot].y};
}

// An event of input, into the devices.
static void apply(const event *e)
{
    const uint32_t bit = 1u << (e->which & 31u);
    switch (e->kind) {
    case EVENT_KEY:
        if (e->press != PRESS_UP) made.keys_tapped[e->which] = true;
        made.keys_held[e->which] = e->press == PRESS_DOWN;
        break;
    case EVENT_BUTTON:
        if (e->press != PRESS_UP) made.mouse_tapped |= bit;
        made.mouse_held = e->press == PRESS_DOWN ? made.mouse_held | bit : made.mouse_held & ~bit;
        break;
    case EVENT_MOUSE:
        mouse_by(e->x - made.x, e->y - made.y);
        break;
    case EVENT_MOVE:
        mouse_by(e->x, e->y);
        break;
    case EVENT_SCROLL:
        made.wheel_x += e->x, made.wheel_y += e->y;
        break;
    case EVENT_TEXT: {
        const char *text = e->text;
        for (uint32_t c; (c = tide_utf8_next(&text)) != 0;) {
            if (c < 32u || c == 127u || made.typed_count == COUNT_OF(made.typed)) continue; // As a paste: no tabs
            made.typed[(made.typed_start + made.typed_count++) % COUNT_OF(made.typed)] = c;
        }
        break;
    }
    case EVENT_TOUCH:
        finger_event(e);
        break;
    case EVENT_PAD_BUTTON:
        made.pad.connected = true;
        if (e->press != PRESS_UP) made.pad_tapped |= bit;
        made.pad.buttons = e->press == PRESS_DOWN ? made.pad.buttons | bit : made.pad.buttons & ~bit;
        break;
    case EVENT_PAD_STICK: // The window says a stick's y down, as gamepads do
        made.pad.connected = true;
        if (e->which == 0) made.pad.left_x = e->x, made.pad.left_y = -e->y;
        else made.pad.right_x = e->x, made.pad.right_y = -e->y;
        break;
    case EVENT_PAD_TRIGGER:
        made.pad.connected = true;
        if (e->which == 0) made.pad.left_trigger = e->x;
        else made.pad.right_trigger = e->x;
        break;
    case EVENT_CAPTURE:
    case EVENT_QUIT:
        break;
    }
}

// ---------------------------------------------------------------------------
// Reading an event

static bool fail(char *error, const size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if (error && size) vsnprintf(error, size, format, args);
    va_end(args);
    return false;
}

static bool blank(const char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// The next word of `*at` into `out`, moving past it; false at the text's end.
// A word longer than `out` holds is cut, which makes it no name or number.
static bool word(const char **at, char *out, const size_t size)
{
    const char *p = *at;
    while (blank(*p)) p++;
    size_t n = 0;
    for (; *p && !blank(*p); p++) {
        if (n + 1 < size) out[n++] = *p;
    }
    out[n] = '\0';
    *at = p;
    return n > 0;
}

// What's left of `at`, without the blanks around it, as memory of its own.
static char *rest(const char *at)
{
    while (blank(*at)) at++;
    size_t n = strlen(at);
    while (n && blank(at[n - 1])) n--;
    char *out = tide_alloc(n + 1);
    memcpy(out, at, n);
    out[n] = '\0';
    return out;
}

static bool number(const char *text, float *out)
{
    char *end = NULL;
    const double v = strtod(text, &end);
    if (end == text || *end || !isfinite(v) || fabs(v) > 1e9) return false;
    *out = (float)v;
    return true;
}

static bool whole(const char *text, uint32_t *out)
{
    char *end = NULL;
    if (text[0] < '0' || text[0] > '9') return false;
    const unsigned long v = strtoul(text, &end, 10);
    if (*end || v > 1000000000ul) return false;
    *out = (uint32_t)v;
    return true;
}

static int find(const char *const *names, const size_t count, const char *name)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0) return (int)i;
    }
    return -1;
}

static char lower(const char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

// Whether `name` has `part` in it, whatever the case; all of it, for `whole`.
static bool has(const char *name, const char *part, const bool all)
{
    const size_t n = strlen(part);
    for (const char *from = name; *from; from++) {
        size_t i = 0;
        while (i < n && from[i] && lower(from[i]) == lower(part[i])) i++;
        if (i == n && (!all || (from == name && !from[n]))) return true;
        if (all) break;
    }
    return false;
}

// The name that was probably meant by `name`, which is none of `names`: the
// one that only differs by its case, or else the first with it inside
// ("shift" for leftShift, "1" for digit1); NULL if none.
static const char *meant(const char *const *names, const size_t count, const char *name)
{
    if (!name[0]) return NULL;
    for (size_t i = 0; i < count; i++) {
        if (has(names[i], name, true)) return names[i];
    }
    for (size_t i = 0; i < count; i++) {
        if (has(names[i], name, false)) return names[i];
    }
    return NULL;
}

// Why `name` isn't one of `names`, which are `what`s: with the one that was
// probably meant, or else `like`, what such names look like.
static bool no_such(char *error, const size_t size, const char *what, const char *name, const char *const *names,
                    const size_t count, const char *like)
{
    const char *guess = meant(names, count, name);
    if (guess) return fail(error, size, "no %s is named '%s': did you mean '%s'?", what, name, guess);
    return fail(error, size, "no %s is named '%s': %s", what, name, like);
}

// Nothing more on the line.
static bool ended(const char **at, char *error, const size_t size)
{
    char more[32];
    if (!word(at, more, sizeof more)) return true;
    return fail(error, size, "'%s' is one word too many", more);
}

// `down`, `up`, or the line's end for a tap, after a key's or a button's name.
static bool press_of(const char **at, uint32_t *press, char *error, const size_t size)
{
    char how[32];
    if (!word(at, how, sizeof how)) {
        *press = PRESS_TAP;
        return true;
    }
    if (strcmp(how, "down") == 0) *press = PRESS_DOWN;
    else if (strcmp(how, "up") == 0) *press = PRESS_UP;
    else return fail(error, size, "'%s' is neither `down` nor `up`, which come after the name, or nothing for a tap", how);
    return ended(at, error, size);
}

// Two numbers, then the line's end.
static bool pair(const char **at, event *e, const char *usage, char *error, const size_t size)
{
    char a[64], b[64];
    if (!word(at, a, sizeof a) || !word(at, b, sizeof b) || !number(a, &e->x) || !number(b, &e->y)) {
        return fail(error, size, "%s", usage);
    }
    return ended(at, error, size);
}

// An event from `text`, a script's line after its frame. False, with why in
// `error`. `e->text` is memory of the event's own.
static bool parse(const char *text, event *e, char *error, const size_t size)
{
    const char *at = text;
    char verb[32], name[64];
    if (!word(&at, verb, sizeof verb)) return fail(error, size, "nothing happens: after its frame, a line says what, like `key space down`");
    if (strcmp(verb, "key") == 0) {
        e->kind = EVENT_KEY;
        if (!word(&at, name, sizeof name)) return fail(error, size, "`key` takes a key's name, then `down` or `up`: `key space down`");
        const int key = find(key_names, COUNT_OF(key_names), name);
        if (key < 0) {
            return no_such(error, size, "key", name, key_names, COUNT_OF(key_names),
                           "keys have the names Devices gives them, like w, space, enter, leftShift, upArrow and digit1");
        }
        e->which = (uint32_t)key;
        return press_of(&at, &e->press, error, size);
    }
    if (strcmp(verb, "button") == 0) {
        e->kind = EVENT_BUTTON;
        if (!word(&at, name, sizeof name)) return fail(error, size, "`button` takes a mouse button's name, then `down` or `up`: `button left down`");
        const int button = find(mouse_names, COUNT_OF(mouse_names), name);
        if (button < 0) {
            return no_such(error, size, "mouse button", name, mouse_names, COUNT_OF(mouse_names),
                           "they're left, right, middle, back and forward");
        }
        e->which = (uint32_t)button;
        return press_of(&at, &e->press, error, size);
    }
    if (strcmp(verb, "mouse") == 0) {
        e->kind = EVENT_MOUSE;
        return pair(&at, e, "`mouse` takes where the mouse goes, in pixels from the window's top left: `mouse 640 360`", error, size);
    }
    if (strcmp(verb, "move") == 0) {
        e->kind = EVENT_MOVE;
        return pair(&at, e, "`move` takes how far the mouse moves, in pixels across and down: `move 40 -8`", error, size);
    }
    if (strcmp(verb, "scroll") == 0) {
        e->kind = EVENT_SCROLL;
        return pair(&at, e, "`scroll` takes how far the wheel turns, across and away from the user: `scroll 0 -1`", error, size);
    }
    if (strcmp(verb, "text") == 0) {
        e->kind = EVENT_TEXT;
        e->text = rest(at);
        if (!e->text[0]) return fail(error, size, "`text` takes the characters typed, to the line's end: `text K7QF2M`");
        return true;
    }
    if (strcmp(verb, "touch") == 0) {
        static const char usage[] = "`touch` takes a finger's number, then `down` or `move` with where, or `up`: `touch 1 down 100 200`";
        char how[32];
        e->kind = EVENT_TOUCH;
        if (!word(&at, name, sizeof name) || !whole(name, &e->which) || !word(&at, how, sizeof how)) return fail(error, size, "%s", usage);
        if (strcmp(how, "down") == 0) e->press = TIDE_TOUCH_BEGAN;
        else if (strcmp(how, "move") == 0) e->press = TIDE_TOUCH_MOVED;
        else if (strcmp(how, "up") == 0) e->press = TIDE_TOUCH_ENDED;
        else return fail(error, size, "'%s' is none of `down`, `move` and `up`, which come after a finger's number", how);
        const char *where = at;
        if (e->press == TIDE_TOUCH_ENDED && !word(&where, name, sizeof name)) return true; // It lifts where it was
        e->placed = true;
        return pair(&at, e, usage, error, size);
    }
    if (strcmp(verb, "pad") == 0) {
        if (!word(&at, name, sizeof name)) return fail(error, size, "`pad` takes a gamepad button's, stick's or trigger's name: `pad buttonSouth down`");
        int which = find(pad_names, COUNT_OF(pad_names), name);
        if (which >= 0) {
            e->kind = EVENT_PAD_BUTTON;
            e->which = (uint32_t)which;
            return press_of(&at, &e->press, error, size);
        }
        if ((which = find(stick_names, COUNT_OF(stick_names), name)) >= 0) {
            e->kind = EVENT_PAD_STICK;
            e->which = (uint32_t)which;
            return pair(&at, e, "a stick takes where it's pushed, from -1 to 1 across and up: `pad leftStick 0.5 1`", error, size);
        }
        if ((which = find(trigger_names, COUNT_OF(trigger_names), name)) >= 0) {
            char value[64];
            e->kind = EVENT_PAD_TRIGGER;
            e->which = (uint32_t)which;
            if (!word(&at, value, sizeof value) || !number(value, &e->x)) {
                return fail(error, size, "a trigger takes how far it's pulled, from 0 to 1: `pad rightTrigger 1`");
            }
            return ended(&at, error, size);
        }
        const char *guess = meant(pad_names, COUNT_OF(pad_names), name);
        if (!guess) guess = meant(stick_names, COUNT_OF(stick_names), name);
        if (!guess) guess = meant(trigger_names, COUNT_OF(trigger_names), name);
        if (guess) return fail(error, size, "nothing on a gamepad is named '%s': did you mean '%s'?", name, guess);
        return fail(error, size, "nothing on a gamepad is named '%s': its buttons, sticks and triggers have the names Devices gives "
                                 "them, like buttonSouth, dpad.up, leftStick and rightTrigger", name);
    }
    if (strcmp(verb, "capture") == 0) {
        e->kind = EVENT_CAPTURE;
        e->text = rest(at);
        if (!e->text[0]) return fail(error, size, "`capture` takes the file the frame goes to: `capture menu.png`");
        return true;
    }
    if (strcmp(verb, "quit") == 0) {
        e->kind = EVENT_QUIT;
        return ended(&at, error, size);
    }
    return fail(error, size, "'%s' isn't something a line does: that's key, button, mouse, move, scroll, text, touch, pad, capture or quit",
                verb);
}

bool tide_script_event(const char *text, char *error, const size_t error_size)
{
    event e = {0};
    bool ok = parse(text, &e, error, error_size);
    if (ok && e.kind >= EVENT_CAPTURE) ok = fail(error, error_size, "`capture` and `quit` are a script's lines, not input");
    if (ok) apply(&e);
    free(e.text);
    return ok;
}

// ---------------------------------------------------------------------------
// A script

static struct {
    event *events; // In the order of their frames
    uint32_t count, capacity;
    uint32_t next_input; // The first whose frame hasn't started
    uint32_t next_drawn; // The first whose frame hasn't drawn
    bool quits;
    char *folder; // The script's own, which its captures' files are named from: "" or ending with a slash
} script;

static bool slash(const char c)
{
    return c == '/' || c == '\\';
}

// A file a script names, as a path to open: beside the script, unless it says
// where from the top (/shots/menu.png, C:\shots\menu.png).
static char *beside_script(const char *file)
{
    const bool rooted = slash(file[0]) || (file[0] && file[1] == ':');
    const size_t folder = rooted ? 0 : strlen(script.folder), n = strlen(file);
    char *path = tide_alloc(folder + n + 1);
    memcpy(path, script.folder, folder);
    memcpy(path + folder, file, n + 1);
    return path;
}

bool tide_script_load(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "tide: can't read the script %s: %s\n", path, strerror(errno));
        return false;
    }
    size_t size = 0, capacity = 4096;
    char *text = tide_alloc(capacity);
    for (size_t got; (got = fread(text + size, 1, capacity - size - 1, file)) > 0;) {
        size += got;
        if (size + 1 == capacity) {
            text = tide_realloc(text, capacity, capacity * 2);
            capacity *= 2;
        }
    }
    fclose(file);
    text[size] = '\0';

    size_t folder = strlen(path);
    while (folder && !slash(path[folder - 1])) folder--;
    script.folder = tide_alloc(folder + 1);
    memcpy(script.folder, path, folder);
    script.folder[folder] = '\0';

    bool ok = true;
    uint32_t line = 0, last = 0;
    char *at = text;
    if (size >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) at += 3; // A byte order mark, which some editors start files with
    while (*at && ok) {
        char *end = strchr(at, '\n');
        char *next = end ? end + 1 : at + strlen(at);
        if (end) *end = '\0';
        line++;
        const char *p = at;
        at = next;
        while (blank(*p)) p++;
        if (!*p || *p == '#') continue;

        char first[32], error[512] = "";
        event e = {.line = line};
        word(&p, first, sizeof first);
        if (!whole(first, &e.frame) || e.frame == 0) {
            snprintf(error, sizeof error, "a line starts with its frame, counted from 1, like `120 key d down`: '%s' isn't one", first);
        } else if (e.frame < last) {
            snprintf(error, sizeof error, "frame %u comes after frame %u: a script's lines are in the order of their frames",
                     (unsigned)e.frame, (unsigned)last);
        } else {
            parse(p, &e, error, sizeof error);
        }
        if (error[0]) {
            fprintf(stderr, "tide: %s:%u: %s\n", path, (unsigned)line, error);
            free(e.text);
            ok = false;
            break;
        }
        last = e.frame;
        if (e.kind == EVENT_QUIT) script.quits = true;
        if (script.count == script.capacity) {
            const uint32_t grown = script.capacity ? script.capacity * 2u : 64u;
            script.events = tide_realloc(script.events, script.capacity * sizeof(event), grown * sizeof(event));
            script.capacity = grown;
        }
        script.events[script.count++] = e;
    }
    free(text);
    return ok;
}

bool tide_script_quits(void)
{
    return script.quits;
}

void tide_script_input(const uint32_t frame)
{
    while (script.next_input < script.count && script.events[script.next_input].frame <= frame) {
        apply(&script.events[script.next_input++]);
    }
}

int tide_script_drawn(const uint32_t frame)
{
    bool quit = false;
    for (; script.next_drawn < script.count && script.events[script.next_drawn].frame <= frame; script.next_drawn++) {
        const event *e = &script.events[script.next_drawn];
        if (e->kind == EVENT_QUIT) quit = true;
        if (e->kind != EVENT_CAPTURE) continue;
        char *path = beside_script(e->text);
        const bool written = tide_platform_capture(path);
        free(path);
        if (!written) return 1;
    }
    return quit ? 0 : TIDE_KEEP_RUNNING;
}
