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
//     401 capture-audio fight.wav    what played since the last such line (or the start), as a WAV beside it
//     410 record fight.gif           every second frame from this one, as a GIF beside the script...
//     470 record stop                ...to this one; or to another `record`, or the run's end
//     480 sleep                      the machine sleeps and wakes: it leaves a match others can be in
//     500 quit                       the run ends once this frame drew, with exit code 0
//
// What a line does at a frame is in the devices at that frame's poll, as a
// player's input since the frame before would be.
//
// In a run of several players (players.h) every player's process reads the
// one script, and a line says whose it is after its frame:
//
//     120 player 2 key d down        the second player's key
//     130 key a down                 with none said, input is the first player's
//     400 capture fight.png          ...and a picture is everyone's: fight-1.png, fight-2.png
//     410 player 2 capture hers.png  ...or the one player's: hers-2.png
//     420 player 2 leave             the player leaves its match, as Session.Leave does...
//     430 player 3 drop              ...or goes silent both ways, as a phone that went away...
//     480 player 3 rejoin            ...and joins what it joined again, as the same player
//     490 player 2 sleep             ...or the player's machine sleeps
//     500 quit                       the run's end is every player's, so it takes none

#include "script.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide/page.h"
#include "font.h" // tide_utf8_next
#include "players.h"

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
    EVENT_CAPTURE_AUDIO,
    EVENT_RECORD,
    EVENT_RECORD_STOP,
    EVENT_QUIT,
    EVENT_SLEEP, // The machine slept (tide_platform_slept): a player's, in a run of several
    EVENT_LEAVE, // A player's match, in a run of several
    EVENT_DROP,
    EVENT_REJOIN,
} event_kind;

// What a key or button does: a tap is down for a frame, and up at the next.
enum { PRESS_DOWN, PRESS_UP, PRESS_TAP };

typedef struct event {
    uint32_t frame, line; // A script's: when it happens, and where the file says so
    uint32_t player;      // ...and whose it is in a run of several players, from 1: 0 when the line doesn't say
    event_kind kind;
    uint32_t which; // The key, button, stick, trigger or finger
    uint32_t press; // PRESS_*, or a touch's tide_touch_phase
    bool placed;    // A touch that says where
    float x, y;
    char *text; // What's typed, or a capture's or a recording's file
    char *source; // An interactive event's original line, for replay after it runs
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
    case EVENT_SLEEP:
        tide_platform_pretend_sleep();
        break;
    case EVENT_LEAVE:
        tide_platform_players_leave();
        break;
    case EVENT_DROP:
        tide_platform_players_drop();
        break;
    case EVENT_REJOIN:
        tide_platform_players_rejoin();
        break;
    case EVENT_CAPTURE:
    case EVENT_CAPTURE_AUDIO:
    case EVENT_RECORD:
    case EVENT_RECORD_STOP:
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
    if (strcmp(verb, "capture-audio") == 0) {
        e->kind = EVENT_CAPTURE_AUDIO;
        e->text = rest(at);
        if (!e->text[0]) return fail(error, size, "`capture-audio` takes the file what played goes to: `capture-audio shot.wav`");
        return true;
    }
    if (strcmp(verb, "record") == 0) {
        e->text = rest(at);
        e->kind = strcmp(e->text, "stop") == 0 ? EVENT_RECORD_STOP : EVENT_RECORD;
        if (!e->text[0]) {
            return fail(error, size, "`record` takes the file the frames go to, `record fight.gif`, or ends the recording, "
                                     "`record stop`");
        }
        return true;
    }
    if (strcmp(verb, "quit") == 0) {
        e->kind = EVENT_QUIT;
        return ended(&at, error, size);
    }
    if (strcmp(verb, "sleep") == 0) {
        e->kind = EVENT_SLEEP;
        return ended(&at, error, size);
    }
    if (strcmp(verb, "leave") == 0 || strcmp(verb, "drop") == 0 || strcmp(verb, "rejoin") == 0) {
        e->kind = verb[0] == 'l' ? EVENT_LEAVE : verb[0] == 'd' ? EVENT_DROP : EVENT_REJOIN;
        return ended(&at, error, size);
    }
    return fail(error, size, "'%s' isn't something a line does: that's key, button, mouse, move, scroll, text, touch, pad, capture, "
                             "capture-audio, record, sleep, leave, drop, rejoin or quit",
                verb);
}

bool tide_script_event(const char *text, char *error, const size_t error_size)
{
    event e = {0};
    bool ok = parse(text, &e, error, error_size);
    if (ok && e.kind >= EVENT_CAPTURE) {
        ok = fail(error, error_size,
                  "`capture`, `capture-audio`, `record`, `quit`, `sleep`, `leave`, `drop` and `rejoin` are a script's lines, not input");
    }
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
    uint32_t last_audio; // 1 + the place of its last capture-audio line: 0 for none
    char *folder; // The script's own, which its captures' files are named from: "" or ending with a slash
} script;

static bool interactive;
static FILE *replay;
static bool replay_failed;

static bool first_player(void)
{
    return tide_platform_player() <= 1u;
}

static bool for_player(const event *e)
{
    const uint32_t own = tide_platform_player() ? tide_platform_player() : 1u;
    return e->kind == EVENT_QUIT || (e->player ? e->player == own : e->kind >= EVENT_CAPTURE || own == 1u);
}

static void replay_line(const char *line)
{
    if (replay && (fprintf(replay, "%s\n", line) < 0 || fflush(replay) != 0)) replay_failed = true;
}

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

// `player <n>` after a line's frame, which says whose the line is in a run of
// several players: moves `*at` past it, and leaves `*player` 0 for a line
// with none. False, with why in `error`, for a player the run doesn't have.
static bool player_of(const char **at, uint32_t *player, char *error, const size_t size)
{
    const char *p = *at;
    char first[32], number[32];
    if (!word(&p, first, sizeof first) || strcmp(first, "player") != 0) return true;
    const uint32_t players = tide_platform_players() ? tide_platform_players() : 1u;
    if (!word(&p, number, sizeof number) || !whole(number, player) || *player == 0) {
        return fail(error, size, "`player` takes which player the line is for, counted from 1, then what happens: "
                                 "`player 2 key d down`");
    }
    if (*player > players && players == 1u) {
        return fail(error, size, "there's no player %u in a run of one: --players %u makes it a run of %u", (unsigned)*player,
                    (unsigned)*player, (unsigned)*player);
    }
    if (*player > players) {
        return fail(error, size, "there's no player %u in this run of %u players (--players %u)", (unsigned)*player,
                    (unsigned)players, (unsigned)players);
    }
    *at = p;
    return true;
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
        } else if (player_of(&p, &e.player, error, sizeof error) && parse(p, &e, error, sizeof error)) {
            if (e.kind == EVENT_QUIT && e.player) {
                snprintf(error, sizeof error, "`quit` ends the run, which is every player's: it takes no player before it");
            }
            const char *const match_verbs[] = {"leave", "drop", "rejoin"};
            const char *verb = e.kind >= EVENT_LEAVE ? match_verbs[e.kind - EVENT_LEAVE] : NULL;
            if (verb && tide_platform_players() < 2u) {
                snprintf(error, sizeof error, "`%s` is for a run of several players, whose matches it changes: --players 2 and more",
                         verb);
            } else if (verb && !e.player) {
                snprintf(error, sizeof error, "`%s` says whose match it is, after the frame: `%u player 2 %s`", verb,
                         (unsigned)e.frame, verb);
            } else if (e.kind == EVENT_SLEEP && tide_platform_players() > 1u && !e.player) {
                snprintf(error, sizeof error, "`sleep` says whose machine sleeps in a run of several players, after the frame: "
                         "`%u player 2 sleep`", (unsigned)e.frame);
            }
        }
        if (error[0]) {
            fprintf(stderr, "tide: %s:%u: %s\n", path, (unsigned)line, error);
            free(e.text);
            ok = false;
            break;
        }
        last = e.frame;
        // In a run of several players, each one's process keeps its own lines: input is the first's unless
        // the line says whose, and a picture is everyone's unless it does
        const uint32_t own = tide_platform_player() ? tide_platform_player() : 1u;
        if (e.kind != EVENT_QUIT && (e.player ? e.player != own : e.kind < EVENT_CAPTURE && own != 1u)) {
            free(e.text);
            continue;
        }
        if (e.kind == EVENT_QUIT) script.quits = true;
        if (script.count == script.capacity) {
            const uint32_t grown = script.capacity ? script.capacity * 2u : 64u;
            script.events = tide_realloc(script.events, script.capacity * sizeof(event), grown * sizeof(event));
            script.capacity = grown;
        }
        script.events[script.count++] = e;
        if (e.kind == EVENT_CAPTURE_AUDIO) script.last_audio = script.count;
    }
    free(text);
    return ok;
}

bool tide_script_quits(void)
{
    return script.quits;
}

bool tide_script_captures_audio(void)
{
    return script.next_drawn < script.last_audio;
}

void tide_script_input(const uint32_t frame)
{
    while (script.next_input < script.count && script.events[script.next_input].frame <= frame) {
        const event *e = &script.events[script.next_input++];
        if (e->source) replay_line(e->source);
        if (for_player(e)) apply(e);
    }
}

int tide_script_drawn(const uint32_t frame)
{
    bool quit = false;
    for (; script.next_drawn < script.count && script.events[script.next_drawn].frame <= frame; script.next_drawn++) {
        const event *e = &script.events[script.next_drawn];
        if (!for_player(e)) continue;
        if (e->kind == EVENT_QUIT) quit = true;
        if (e->kind == EVENT_RECORD_STOP && !tide_platform_record_stop()) return 1;
        if (e->kind != EVENT_CAPTURE && e->kind != EVENT_CAPTURE_AUDIO && e->kind != EVENT_RECORD) continue;
        char *beside = beside_script(e->text);
        char *path = tide_platform_players_file(beside); // With the player's number, in a run of several
        free(beside);
        const bool written = e->kind == EVENT_CAPTURE         ? tide_platform_capture(path)
                             : e->kind == EVENT_CAPTURE_AUDIO ? tide_platform_capture_audio(path)
                                                              : tide_platform_record(path);
        free(path);
        if (!written) return 1;
    }
    if (interactive && script.next_drawn) {
        const uint32_t n = script.next_drawn;
        for (uint32_t i = 0; i < n; i++) {
            free(script.events[i].text);
            free(script.events[i].source);
        }
        script.count -= n;
        memmove(script.events, script.events + n, script.count * sizeof(event));
        script.next_input -= n;
        script.next_drawn = 0;
        script.last_audio = script.last_audio > n ? script.last_audio - n : 0;
    }
    return replay_failed ? 1 : quit ? 0 : TIDE_KEEP_RUNNING;
}

bool tide_script_interactive_open(const char *path)
{
    interactive = true;
    size_t folder = strlen(path);
    while (folder && !slash(path[folder - 1])) folder--;
    script.folder = tide_alloc(folder + 1u);
    memcpy(script.folder, path, folder);
    script.folder[folder] = '\0';
    if (!first_player()) return true;
    replay = fopen(path, "wb");
    if (!replay) {
        fprintf(stderr, "tide: --interactive: can't write replay %s: %s\n", path, strerror(errno));
        return false;
    }
    replay_line("# Interactive play; replay with the same size, seed and players.");
    return !replay_failed;
}

// Parse an interactive event using the same device and capture grammar as a
// script. The numbered form queues it; a capture without a frame observes the
// current frame without advancing the game.
static bool interactive_event(const char *line, const uint32_t frame, char *error, const size_t error_size)
{
    const char *p = line;
    char number[32];
    event e = {0};
    word(&p, number, sizeof number);
    const bool numbered = whole(number, &e.frame);
    if (!numbered) {
        e.frame = frame;
        p = line;
    }
    bool ok = player_of(&p, &e.player, error, error_size) && parse(p, &e, error, error_size);
    if (ok && numbered && (e.frame <= frame || e.frame > 1000000000u)) {
        ok = fail(error, error_size, "queue a future frame, from %u to 1000000000", (unsigned)(frame + 1u));
    }
    if (ok && (e.kind == EVENT_QUIT || (!numbered && e.kind != EVENT_CAPTURE))) {
        ok = fail(error, error_size, "use `<frame> <event>` to queue input, `capture <file.png>` to look, or `quit` to end");
    }
    if (ok && e.kind == EVENT_CAPTURE_AUDIO) {
        ok = fail(error, error_size, "use --capture-audio <file.wav> to keep the interactive run's audio from its start");
    }
    if (ok && e.kind >= EVENT_LEAVE && (tide_platform_players() < 2u || !e.player)) {
        ok = fail(error, error_size, "leave, drop and rejoin need --players 2 or more and `player <n>` before the event");
    }
    if (ok && e.kind == EVENT_SLEEP && tide_platform_players() > 1u && !e.player) {
        ok = fail(error, error_size, "sleep needs `player <n>` before it in a run of several players");
    }
    if (ok && numbered && script.count >= 65536u) {
        ok = fail(error, error_size, "the queue has 65536 events: step the game before adding more");
    }
    if (!ok) {
        free(e.text);
        return false;
    }
    if (!numbered) {
        if (for_player(&e)) {
            char *beside = beside_script(e.text);
            char *path = tide_platform_players_file(beside);
            ok = tide_platform_capture(path);
            free(path);
            free(beside);
        }
        free(e.text);
        if (!ok) return fail(error, error_size, "capture failed; choose a writable PNG path");
        return true;
    }
    const size_t length = strlen(line) + 1u;
    e.source = tide_alloc(length);
    memcpy(e.source, line, length);
    if (script.count == script.capacity) {
        const uint32_t grown = script.capacity ? script.capacity * 2u : 64u;
        script.events = tide_realloc(script.events, script.capacity * sizeof(event), grown * sizeof(event));
        script.capacity = grown;
    }
    uint32_t at = script.count;
    // Stable insertion: an agent can add an earlier action to a future plan.
    while (at && script.events[at - 1u].frame > e.frame) {
        script.events[at] = script.events[at - 1u];
        at--;
    }
    script.events[at] = e;
    script.count++;
    return true;
}

// ---------------------------------------------------------------------------
// The GUI's widgets, which an interactive run lists and clicks by number or label

static tide_gui *listed_gui;           // The host's, whose widgets `controls` lists
static tide_gui_listing *gui_listing; // Made once an interactive run's host asks

void tide_platform_list_controls(tide_gui *g)
{
    if (interactive && !gui_listing) gui_listing = tide_alloc_zeroed(1, sizeof *gui_listing);
    listed_gui = g;
    g->listing = interactive ? gui_listing : NULL;
}

#define GUI_ANSWER (1u << 20) // Room for a frame's whole listing: 1,024 widgets and their labels, quoted

static const char *const kind_names[] = {"widget", "button", "control", "toggle", "slider", "number", "text"};

// Appends to a growing text: `out` of `*size` bytes so far, `*capacity` held.
static void append(char **out, size_t *size, size_t *capacity, const char *format, ...)
{
    for (;;) {
        va_list args;
        va_start(args, format);
        const int n = vsnprintf(*out + *size, *capacity - *size, format, args);
        va_end(args);
        if (n < 0) return;
        if (*size + (size_t)n < *capacity) {
            *size += (size_t)n;
            return;
        }
        *out = tide_realloc(*out, *capacity, *capacity * 2u);
        *capacity *= 2u;
    }
}

// A label in quotes, with \" \\ and control characters escaped, as `click "..."` reads it back.
static void append_quoted(char **out, size_t *size, size_t *capacity, const char *text, const uint32_t length)
{
    append(out, size, capacity, "\"");
    for (uint32_t i = 0; i < length; i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') append(out, size, capacity, "\\%c", c);
        else if (c == '\n') append(out, size, capacity, "\\n");
        else if (c == '\t') append(out, size, capacity, "\\t");
        else if (c < 32u || c == 127u) append(out, size, capacity, "\\x%02x", c);
        else append(out, size, capacity, "%c", c);
    }
    append(out, size, capacity, "\"");
}

static tide_gui_listed *listed_now(uint32_t *count)
{
    *count = 0;
    if (!listed_gui) return NULL;
    const uint32_t n = tide_gui_controls(listed_gui, NULL, 0);
    tide_gui_listed *all = tide_alloc_zeroed(n ? n : 1u, sizeof *all);
    *count = tide_gui_controls(listed_gui, all, n);
    return all;
}

// `controls`: a line for each widget the frame drew, in its order, numbered from 1.
static char *list_controls(void)
{
    uint32_t count;
    tide_gui_listed *all = listed_now(&count);
    size_t size = 0, capacity = 4096;
    char *out = tide_alloc(capacity);
    out[0] = '\0';
    for (uint32_t i = 0; i < count; i++) {
        const tide_gui_listed *c = &all[i];
        append(&out, &size, &capacity, "tide: control %u %s ", (unsigned)(i + 1u), kind_names[c->kind]);
        append_quoted(&out, &size, &capacity, c->label, c->label_length);
        append(&out, &size, &capacity, " %g %g %g %g at %.9g %.9g%s%s%s\n", (double)c->rect.x, (double)c->rect.y,
               (double)c->rect.width, (double)c->rect.height, (double)c->at.x, (double)c->at.y,
               c->disabled ? " disabled" : "", c->covered ? " covered" : "", c->focused ? " focused" : "");
    }
    append(&out, &size, &capacity, "tide: controls %u\n", (unsigned)count);
    free(all);
    return out;
}

// A quoted label as list_controls writes it, into `out`: false where the quotes don't close.
static bool unquote(const char *at, char *out, const size_t size, const char **after)
{
    size_t n = 0;
    for (const char *p = at + 1; *p; p++) {
        char c = *p;
        if (c == '"') {
            out[n] = '\0';
            *after = p + 1;
            return true;
        }
        if (c == '\\' && p[1]) {
            p++;
            c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
            if (*p == 'x' && p[1] && p[2]) {
                const char hex[3] = {p[1], p[2], '\0'};
                c = (char)strtoul(hex, NULL, 16);
                p += 2;
            }
        }
        if (n + 1u < size) out[n++] = c;
    }
    return false;
}

static bool same_label(const tide_gui_listed *c, const char *label, const bool any_case, const bool part)
{
    const size_t n = strlen(label);
    if (!part) {
        if (c->label_length != n) return false;
        for (size_t i = 0; i < n; i++) {
            if (any_case ? lower(c->label[i]) != lower(label[i]) : c->label[i] != label[i]) return false;
        }
        return true;
    }
    for (size_t from = 0; n && from + n <= c->label_length; from++) {
        size_t i = 0;
        while (i < n && lower(c->label[from + i]) == lower(label[i])) i++;
        if (i == n) return true;
    }
    return false;
}

// `click <number>`, `click "label"` or `click label`: where the widget is
// clicked, as "x y" in `out`. False, with why in `error`.
static bool find_click(const char *args, char *out, const size_t out_size, char *error, const size_t error_size)
{
    while (blank(*args)) args++;
    char label[512];
    uint32_t number = 0;
    bool by_number = false;
    if (*args == '"') {
        const char *after;
        if (!unquote(args, label, sizeof label, &after)) return fail(error, error_size, "the label's quotes don't close");
        char more[32];
        if (word(&after, more, sizeof more)) return fail(error, error_size, "'%s' is one word too many after the label", more);
    } else {
        char *text = rest(args);
        snprintf(label, sizeof label, "%s", text);
        free(text);
        by_number = whole(label, &number);
    }
    if (!label[0] && !by_number) {
        return fail(error, error_size, "`click` takes a widget's number or label from `controls`: `click 3` or `click \"Play\"`");
    }
    uint32_t count;
    tide_gui_listed *all = listed_now(&count);
    const tide_gui_listed *found = NULL;
    bool ok = true;
    if (by_number) {
        if (number >= 1u && number <= count) found = &all[number - 1u];
        else if (!count) ok = fail(error, error_size, "the frame drew no widgets to click: capture it to see what's there");
        else ok = fail(error, error_size, "there's no widget %u: `controls` lists %u, numbered from 1", (unsigned)number,
                       (unsigned)count);
    } else {
        // Its whole label, then whatever the case, then a part of it: the first of those that some widget has
        for (int tier = 0; tier < 3 && !found && ok; tier++) {
            uint32_t matches = 0, first = 0, second = 0;
            for (uint32_t i = 0; i < count; i++) {
                if (!same_label(&all[i], label, tier > 0, tier == 2)) continue;
                if (!matches) first = i + 1u;
                else if (matches == 1u) second = i + 1u;
                matches++;
            }
            if (matches == 1u) found = &all[first - 1u];
            else if (matches > 1u) {
                ok = fail(error, error_size, "%u widgets have the label '%s' (%u and %u among them): click one by its number from `controls`",
                          (unsigned)matches, label, (unsigned)first, (unsigned)second);
            }
        }
        if (ok && !found) ok = fail(error, error_size, "no widget has the label '%s': `controls` lists them", label);
    }
    if (found && found->disabled) ok = fail(error, error_size, "that widget is disabled: it takes no clicks");
    else if (found && found->covered) {
        ok = fail(error, error_size, "something is over all of that widget, which would take the click: `mouse` and `button` "
                                     "click wherever you like");
    }
    if (found && ok) snprintf(out, out_size, "%.9g %.9g", (double)found->at.x, (double)found->at.y);
    free(all);
    return ok;
}

// Whether a line is `controls` or `click`, after its frame and player if it
// has them: the verb, and where its arguments start. `now` is the frame drawn;
// what's wrong with the line is in `error`.
static bool gui_line(const char *line, const uint32_t now, uint32_t *frame, uint32_t *player, char *verb,
                     const size_t verb_size, const char **args, char *error, const size_t error_size)
{
    const char *p = line;
    char first[32];
    word(&p, first, sizeof first);
    *frame = 0;
    if (!whole(first, frame)) p = line;
    const char *before = p;
    *player = 0;
    if (!player_of(&p, player, NULL, 0)) p = before;
    if (!word(&p, verb, verb_size) || (strcmp(verb, "controls") != 0 && strcmp(verb, "click") != 0)) return false;
    *args = p;
    const bool listing = strcmp(verb, "controls") == 0;
    if (listing && *frame) fail(error, error_size, "`controls` lists the frame drawn now: it takes no frame");
    else if (listing && word(&p, first, sizeof first)) fail(error, error_size, "`controls` takes no arguments");
    else if (*frame && (*frame <= now || *frame > 1000000000u)) {
        fail(error, error_size, "queue a future frame, from %u to 1000000000", (unsigned)(now + 1u));
    }
    return true; // A line of the GUI's, whether or not it's right
}

int tide_script_interactive_wait(const uint32_t frame)
{
    if (first_player()) {
        printf("tide: interactive ready %u\n", (unsigned)frame);
        fflush(stdout);
    }
    for (;;) {
        char line[4096] = "", error[512] = "";
        if (first_player()) {
            if (!fgets(line, sizeof line, stdin)) {
                if (ferror(stdin)) return -1;
                memcpy(line, "quit", 5);
            } else if (!strchr(line, '\n') && !feof(stdin)) {
                int c;
                while ((c = fgetc(stdin)) != '\n' && c != EOF) { }
                puts("tide: interactive error command is too long (4094 bytes maximum)");
                fflush(stdout);
                continue;
            }
            line[strcspn(line, "\r\n")] = '\0';
        }
        if (!tide_platform_players_control(line, sizeof line)) return -1;
        const char *p = line;
        char verb[32], number[32];
        word(&p, verb, sizeof verb);
        uint32_t steps = 0;
        bool quit = false;
        bool observation = false;
        bool gui = false, clicks = false;
        const uint32_t own = tide_platform_player() ? tide_platform_player() : 1u;
        uint32_t from = 1u, gui_frame = 0, gui_player = 0;
        char gui_verb[32];
        const char *gui_args = NULL;
        char *reply = NULL; // What this player says of the GUI's widgets
        if (!verb[0] || verb[0] == '#') {
            // Empty lines and comments are accepted, as in script files.
        } else if (strcmp(verb, "quit") == 0) {
            quit = !word(&p, number, sizeof number);
            if (!quit) snprintf(error, sizeof error, "quit takes no arguments");
        } else if (strcmp(verb, "step") == 0) {
            if (!word(&p, number, sizeof number) || !whole(number, &steps) || !steps || steps > 1000000u ||
                steps > 1000000000u - frame || word(&p, number, sizeof number)) {
                steps = 0;
                snprintf(error, sizeof error, "step takes 1 to 1000000 frames, ending at frame 1000000000 or earlier");
            }
        } else if (strcmp(verb, "status") == 0) {
            if (word(&p, number, sizeof number)) snprintf(error, sizeof error, "status takes no arguments");
        } else if (strcmp(verb, "clear") == 0) {
            if (word(&p, number, sizeof number)) snprintf(error, sizeof error, "clear takes no arguments");
            else {
                for (uint32_t i = 0; i < script.count; i++) {
                    free(script.events[i].text);
                    free(script.events[i].source);
                }
                script.count = script.next_input = script.next_drawn = script.last_audio = 0;
            }
        } else if (gui_line(line, frame, &gui_frame, &gui_player, gui_verb, sizeof gui_verb, &gui_args, error, sizeof error)) {
            gui = true;
            clicks = strcmp(gui_verb, "click") == 0;
            from = gui_player ? gui_player : 1u;
            if (clicks && script.count + 2u > 65536u) {
                snprintf(error, sizeof error, "the queue has 65536 events: step the game before adding more");
            }
            if (!error[0] && own == from) {
                if (!clicks) reply = list_controls();
                else {
                    reply = tide_alloc(64);
                    if (!find_click(gui_args, reply, 64, error, sizeof error)) reply[0] = '\0';
                }
            }
        } else {
            uint32_t at;
            observation = !whole(verb, &at);
            interactive_event(line, frame, error, sizeof error);
        }
        bool ok = !error[0] && !replay_failed;
        char *answer = gui && first_player() ? tide_alloc(GUI_ANSWER) : NULL;
        const bool done = tide_platform_players_control_done(&ok, error[0] ? error : reply, from, answer, answer ? GUI_ANSWER : 0);
        if (!done || replay_failed) {
            free(reply);
            free(answer);
            return -1;
        }
        if (!ok && !error[0]) {
            if (answer && answer[0]) snprintf(error, sizeof error, "%s", answer);
            else snprintf(error, sizeof error, "command failed for another player; see stderr");
        }
        // A click is the mouse there and a tap of its left button, queued as
        // those lines: whose it is and the first player's, which replays them
        const char *where = first_player() ? answer : own == from ? reply : NULL;
        if (ok && clicks && where && where[0]) {
            const uint32_t at = gui_frame ? gui_frame : frame + 1u;
            char whose[32] = "", queued[160], queue_error[512] = "";
            if (gui_player) snprintf(whose, sizeof whose, "player %u ", (unsigned)gui_player);
            snprintf(queued, sizeof queued, "%u %smouse %s", (unsigned)at, whose, where);
            bool queued_ok = interactive_event(queued, frame, queue_error, sizeof queue_error);
            snprintf(queued, sizeof queued, "%u %sbutton left", (unsigned)at, whose);
            queued_ok = queued_ok && interactive_event(queued, frame, queue_error, sizeof queue_error);
            if (!queued_ok) snprintf(error, sizeof error, "%s", queue_error);
            else if (first_player()) printf("tide: click %u %s\n", (unsigned)at, where);
        } else if (ok && gui && !clicks && first_player() && answer) {
            fputs(answer, stdout);
        }
        free(reply);
        free(answer);
        if (ok && observation && replay) {
            if (fprintf(replay, "%u %s\n", (unsigned)frame, line) < 0 || fflush(replay) != 0) replay_failed = true;
            if (replay_failed) return -1;
        }
        if (quit) return 0;
        if (steps) return (int)steps;
        if (first_player()) {
            if (error[0]) printf("tide: interactive error %s\n", error);
            else printf("tide: interactive ok %u\n", (unsigned)frame);
            fflush(stdout);
        }
    }
}

bool tide_script_interactive_close(const uint32_t frame)
{
    if (!replay) return !replay_failed;
    if (fprintf(replay, "%u quit\n", (unsigned)(frame ? frame : 1u)) < 0) replay_failed = true;
    if (fclose(replay) != 0) replay_failed = true;
    replay = NULL;
    if (replay_failed) fprintf(stderr, "tide: couldn't write the interactive replay\n");
    return !replay_failed;
}
