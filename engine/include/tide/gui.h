#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tide/color.h"
#include "tide/devices.h"
#include "tide/draw.h"
#include "tide/math.h"
#include "tide/profile.h"

typedef struct tide_textref tide_textref;

// Immediate-mode GUI, as Tide's GUI and GUILayout see it: widgets are
// function calls made every frame from views, which draw themselves and say
// what the player did with them. Nothing is kept between frames but which
// widget is hovered, pressed, focused or being typed into.
//
// Generated code calls the tide_gui_* functions below.
//
// Positions and sizes are in the window's pixels, from the top left with y
// down, as in Unity's GUI. Widgets keep their size when the window changes
// size, as a web page's do, until there isn't room for them: then laid out
// widgets shrink, as far as each can, to fit the width they have.
//
// A frame of a host:
//
//     tide_draw_reset(&draw);
//     tide_draw_screen_size(&draw, tide_platform_screen_size()); // What meshes' levels of detail are picked by
//     tide_gui_begin(&gui, &devices, tide_platform_screen_size(), tide_platform_measure_text);
//     tide_frame(match, previous, alpha, verified, &local, &draw, &gui);
//     tide_gui_end(&gui, &draw); // The GUI's commands go last, over the world
//     tide_platform_draw(&draw);
//
// and before sampling input, it hides what the GUI is using from the game:
//
//     tide_devices sampled = devices;
//     tide_gui_hide(&gui, &sampled);
//     const tide_input input = tide_input_sample(&sampled, &local);
//
// Views read the devices once per frame (`Devices` in Tide), through the
// GUI: tide_gui_begin works out what changed since last frame, and hides what
// the GUI is using from them too.
//
// A view's own widgets are controls (GUI.Control): the view draws one however
// it likes, and the GUI says what the player does with it, as it would for a
// button of its own. The GUI draws nothing for it.
//
// A view with widgets that read the devices themselves, like a library's,
// claims what they use (GUI.ClaimPointer, GUI.ClaimKeyboard), each frame it
// uses it: from the end of that frame to the end of the next, it's hidden from
// the game and from the other views as what the GUI uses is, and the view that
// claimed it goes on reading it. A view that says where its widget is
// (GUI.ClaimPointer(rect)) has the pointer wherever it's on it, from the frame
// it comes there. The GUI's own use comes first: a view never reads what the
// GUI is using, whatever it claimed.
//
// A modal (GUILayout.Modal) takes the whole screen while it's up: the widgets
// outside it stop working, the focus goes to its first widget, the game and
// the views get nothing from the devices, and back (Escape, or the east
// button) closes it.
//
// A view that wants the mouse for itself, like a camera that looks around,
// asks for the cursor's lock (Cursor.Lock) each frame it does. The host passes
// the frame's ask on to the platform (tide_gui_cursor_lock), and the devices
// say whether the window holds the cursor (tide_mouse.locked, Cursor.locked),
// which the system decides. While it does, the cursor is hidden and points at
// nothing: the mouse hovers and presses no widget, and all of it is the
// game's. A finger still presses what it touches.
//
// Widgets in a Disabled block (GUI.Disabled) are drawn faded and don't work,
// as with Unity's GUI.enabled: a menu stays up, grayed out, while it waits.
//
// Widgets have IDs, which generated code derives from where they're called
// (see tide_gui_id). Keyboard and gamepad navigation are built in: Tab, the
// arrows or the d-pad move the focus between widgets, in the order they're
// called, Enter, Space or the south button press, Escape or the east button
// let go. Left and right step a slider's or a number field's value, and move
// the focus from the other widgets.
//
// The GUI's own widgets look as its theme says (tide_gui_theme: their colors
// and their corners), which every frame starts as the default
// (GUI.SetTheme, GUI.Theme).

#ifndef TIDE_GUI_MAX_DEPTH
#define TIDE_GUI_MAX_DEPTH 32 // Containers and Disabled blocks open at once
#endif
#ifndef TIDE_GUI_MAX_NAV
#define TIDE_GUI_MAX_NAV 512 // Widgets the focus can move between, per frame
#endif
#ifndef TIDE_GUI_MAX_IDS
#define TIDE_GUI_MAX_IDS 2048 // Distinct call sites per frame; a power of two
#endif
#ifndef TIDE_GUI_MAX_SIZES
#define TIDE_GUI_MAX_SIZES 256 // Containers whose size is remembered; a power of two
#endif
#ifndef TIDE_GUI_MAX_RECTS
#define TIDE_GUI_MAX_RECTS 1024 // Widgets and areas whose place a frame remembers: the pointer there is the GUI's
#endif
#ifndef TIDE_GUI_MAX_CLAIMS
#define TIDE_GUI_MAX_CLAIMS 16 // Views that claim devices in one frame and go on reading them
#endif
#ifndef TIDE_GUI_MAX_CLAIM_RECTS
#define TIDE_GUI_MAX_CLAIM_RECTS 256 // Places views claim the pointer at, per frame
#endif

// How the GUI's own widgets look: their colors, and how round their corners
// are. Tide's GUITheme has the same fields. Every frame starts with the
// default (tide_gui_theme_default), and a theme is that with what it changes.
// The stats overlay and the debug views aren't the GUI's widgets: they keep
// their own look.
typedef struct tide_gui_theme {
    tide_color text;      // Text, and a slider's thumb
    tide_color panel;     // An area's background
    tide_color control;   // A button, a toggle's box, a slider's rail and a color's frame
    tide_color hot;       // ...with the pointer on it
    tide_color pressed;   // ...held down
    tide_color field;     // A field to type into
    tide_color field_hot; // ...with the pointer on it
    tide_color accent;    // The focus's outline, a toggle's check and a slider's filled part
    float radius;         // Their corners', in pixels: 0 for square ones. No rounder than half a part's smaller side
} tide_gui_theme;

// What the player is doing with a control (GUI.Control). Tide's GUIControl
// has the same fields.
typedef struct tide_control {
    bool hovered; // The pointer is on it, with nothing of the GUI's over it
    bool pressed; // The pointer holds it: from a press on it until it lets go, wherever it goes meanwhile
    bool focused; // The keyboard and the gamepad are on it
    bool clicked; // A press on it let go on it, or Enter, Space or the south button went down with the focus on it
} tide_control;

typedef struct tide_gui_group {
    uint32_t id;
    uint32_t kind;            // Vertical, horizontal or area
    tide_float2 origin;       // Where its content starts
    tide_float2 cursor;       // Where the next widget goes
    tide_float2 size;         // Its content's size so far
    float natural;            // Its content's width with room: its widest widget, or a row's widgets and spacing
    float least;              // ...and squeezed as far as it goes
    float room;               // The width its content has
    float squeeze;            // A row: how far its widgets shrink from natural to least (0 to 1), from last frame
    float stretch;            // In a vertical group: the width widgets stretch to, from last frame
    tide_float2 guess;        // An anchored area: its top left, from last frame's size
    int32_t anchor;           // An area: its tide_anchor, or -1 when it's at a rect
    uint32_t panel;           // An area: its background's command
    tide_rect rect;           // An area at a rect: the rect
    uint32_t hot_before;      // An area: what was under the mouse before its content
    uint32_t rects_before;    // An area: how many places were remembered before its content
    uint32_t in_modal_before; // The modal it's in, back when it closes
    float radius;             // An area: its background's corners, as the theme had them when it opened
    bool disabled;            // Its widgets are grayed out and don't work: it's in a Disabled block
    bool scope;               // A Disabled or Theme block: the container around it, carried on, and handed back when it closes
    bool themed;              // A Theme block...
    tide_gui_theme before;    // ...and the theme it found, which is back when it closes
} tide_gui_group;

// A container's size last frame, which lays out the next.
typedef struct tide_gui_size {
    uint32_t id;
    uint32_t frame;
    tide_float2 size;
    float natural;
    float least;
} tide_gui_size;

// How many widgets came from one call site this frame, which tells apart the
// ones a loop makes.
typedef struct tide_gui_seen {
    uint32_t key;
    uint32_t frame;
    uint32_t count;
} tide_gui_seen;

// What a view claims (GUI.ClaimPointer, GUI.ClaimKeyboard).
#define TIDE_GUI_POINTER 1u  // The mouse's buttons and scroll, the pointer's press and the primary touch
#define TIDE_GUI_KEYBOARD 2u // The keys and what's typed

// What one view claimed in a frame.
typedef struct tide_gui_claim {
    uint32_t view;
    uint32_t what;
} tide_gui_claim;

// Where a view claimed the pointer (GUI.ClaimPointer(rect)): a widget of its own.
typedef struct tide_gui_claim_rect {
    tide_rect rect;
    uint32_t view;
} tide_gui_claim_rect;

// Where a frame drew a widget or an area, and which one.
typedef struct tide_gui_rect {
    tide_rect rect;
    uint32_t id;
    // An area: how many places the frame had before its content's, which come
    // before its own. A widget: UINT32_MAX.
    uint32_t content;
} tide_gui_rect;

// A key or a gamepad's button that went down while the keyboard and the
// gamepad were the GUI's: it's the GUI's until it's let go, whatever became of
// the widget meanwhile. From views until a frame has seen it up, and from the
// game's samples until one has.
typedef struct tide_gui_kept {
    bool views, samples;
} tide_gui_kept;

#define TIDE_GUI_KEPT(name) tide_gui_kept name;

typedef struct tide_gui {
    // The screen this frame (Screen in Tide).
    float width;
    float height;

    // Input this frame
    tide_measure_fn measure;
    tide_float2 mouse;
    bool mouse_held, mouse_pressed, mouse_released;
    bool mouse_was_held;
    bool press_was_held;   // The pointer's press last frame, whether the GUI saw it or the cursor was locked
    uint32_t keys;         // Navigation keys held (see gui.c)
    uint32_t keys_pressed; // ...that went down this frame
    tide_typed text;
    bool game_input; // tide_gui_hide ran since the last frame: the game reads the devices
    bool nav_arrows; // The arrows and d-pad can move the focus this frame
    bool back;       // Escape or the east button went down, and didn't stop typing
    // As the view that's running reads them: what changed since last frame,
    // less what the GUI uses and what the other views claimed
    tide_devices devices;
    tide_devices unclaimed; // ...less what the GUI uses alone
    tide_devices last;      // The devices last frame, as the platform had them

    // What views claimed, kept from the end of a frame to the end of the next
    uint32_t view;                                   // The view that's running, from 1; 0 for none
    uint32_t taken, taken_next;                      // What any view claimed last frame, and so far this frame
    tide_gui_claim claims[TIDE_GUI_MAX_CLAIMS];      // Who claimed what last frame
    tide_gui_claim claims_next[TIDE_GUI_MAX_CLAIMS]; // ...and so far this frame
    uint32_t claim_count, claim_count_next;
    // Where views' own widgets were last frame, and this frame so far: the
    // pointer there is the view's from the frame it comes, as on the GUI's own
    tide_gui_claim_rect claim_rects[TIDE_GUI_MAX_CLAIM_RECTS];
    tide_gui_claim_rect claim_rects_next[TIDE_GUI_MAX_CLAIM_RECTS];
    uint32_t claim_rect_count, claim_rect_count_next;
    bool keyboard_shown, keyboard_shown_next; // A view asked for the phone's keyboard (GUI.ShowKeyboard)
    bool navigate, navigate_next;             // A view said a menu is up (GUI.Navigate)
    bool cursor_lock, cursor_lock_next;       // A view asked for the cursor's lock (Cursor.Lock)
    // Where the frame's views say how long each took (tide/profile.h), or
    // NULL: nothing is measured. The host's, set before each tide_frame.
    tide_profile *profile;

    // Kept between frames
    uint32_t frame;
    // The widget under the mouse, drawn last of those there: where the mouse
    // is now, as the last frame drew them (see rects), and so far this frame
    uint32_t hot, hot_next;
    tide_rect hot_rect, hot_rect_next; // ...and where it is
    uint32_t active;                  // The widget the mouse is pressing
    bool active_seen, claimed;
    uint32_t focus;                   // The widget the keyboard and gamepad are on
    bool focus_seen;
    // The control that has the focus and the view that called it, which goes
    // on reading the keyboard and the gamepad, less the keys that move the
    // focus and click
    uint32_t focus_control, focus_view;
    bool focus_reads;                 // ...this frame, as these have them
    tide_keyboard focus_keyboard;
    tide_gamepad focus_gamepad;
    // The keys and buttons that are the GUI's until they're let go
    struct {
        TIDE_KEYBOARD_KEYS(TIDE_GUI_KEPT)
    } kept_keys;
    struct {
        TIDE_GAMEPAD_BUTTONS(TIDE_GUI_KEPT)
        struct {
            TIDE_DPAD_BUTTONS(TIDE_GUI_KEPT)
        } dpad;
    } kept_pad;
    uint32_t editing;                 // The field being typed into
    bool editing_seen;
    uint32_t edit_len;
    char edit[256];                   // What's typed so far
    bool edit_fresh;                  // Nothing typed yet: the first character replaces the value
    uint8_t edit_kind;                // The kind of number that's typed, or that it's text
    // A number field whose typing another field ended this frame, before it
    // ran, and the number it had: the field's to keep as it runs
    uint32_t edit_left;
    double edit_left_value;
    bool over, over_next;             // The mouse was over the GUI as the frame drew it
    // Where the last frame drew its widgets and areas, and this frame so far.
    // The game and views read the devices before a frame's GUI does, so the
    // pointer is the GUI's where it's on one of these now. A frame's own
    // widgets go by them too: the one the pointer is on is the last of these
    // it's on now, wherever the last frame saw it.
    tide_gui_rect rects[TIDE_GUI_MAX_RECTS];
    tide_gui_rect rects_next[TIDE_GUI_MAX_RECTS];
    uint32_t rect_count, rect_count_next;
    bool rects_full, rects_full_next; // More than fit: past them, `over` and `hot` say, a frame late
    uint32_t modal, modal_next;       // The modal on top, last frame's and this frame's
    uint32_t in_modal;                // The modal whose content is being drawn
    uint32_t grab;                    // A modal just came on top: its first widget takes the focus
    uint32_t nav[TIDE_GUI_MAX_NAV];   // This frame's widgets, in order
    bool nav_steps[TIDE_GUI_MAX_NAV]; // ...and whether each steps its value with left and right
    uint32_t nav_count;
    uint32_t nav_last[TIDE_GUI_MAX_NAV]; // Last frame's
    bool nav_steps_last[TIDE_GUI_MAX_NAV];
    uint32_t nav_last_count;

    tide_gui_theme theme; // How widgets drawn now look (GUI.SetTheme, GUI.Theme)

    tide_gui_group groups[TIDE_GUI_MAX_DEPTH + 1]; // [0] is the screen's own
    int depth;
    tide_gui_size sizes[TIDE_GUI_MAX_SIZES];
    tide_gui_seen seen[TIDE_GUI_MAX_IDS];
    tide_draw_list list; // This frame's commands, drawn over the world at the end
} tide_gui;

// Starts a frame. `screen` is the window's size in pixels, and `measure` how
// big the renderer draws text (tide/draw.h: tide_platform_measure_text), or
// NULL to guess. A zeroed tide_gui is ready to use.
void tide_gui_begin(tide_gui *g, const tide_devices *devices, tide_float2 screen, tide_measure_fn measure);

// Ends the frame: closes what's still open and adds the GUI's commands to the
// end of `draw`, over everything else.
void tide_gui_end(tide_gui *g, tide_draw_list *draw);

// Whether the player is typing: into a field of the GUI's, or into what a
// view said shows the keyboard (GUI.ShowKeyboard) in the frame that just
// ended. Phones show their keyboard meanwhile (tide_platform_typing).
bool tide_gui_typing(const tide_gui *g);

// Whether the game wants the cursor locked: a view asked (Cursor.Lock) in the
// frame that just ended, and no modal is up, whose widgets need the pointer.
// The host passes it on once a frame (tide_platform_cursor_lock). Whether the
// window then holds the cursor is the system's to say, and the devices have
// it (tide_mouse.locked).
bool tide_gui_cursor_lock(const tide_gui *g);

// Hides what the GUI is using from `devices`, a copy about to be sampled as
// the game's input: the keyboard and gamepad while a widget has the focus,
// the mouse's buttons and the primary touch while the pointer is over the GUI
// or pressing a widget, and everything while a modal is up. What views claimed
// in the last frame is hidden too, and what's typed always: it's a frame's.
// A mouse whose cursor is locked is on no widget, so its buttons are the game's.
//
// The pointer is over the GUI where `devices` has it on a widget or an area as
// the last frame drew them, so a press there is the GUI's from the sample it
// lands in, before the GUI's own frame sees it: a finger that touches a
// button, or a click the mouse made as it came. Fill the pointer first
// (tide_pointer_poll). A widget that first appears under the pointer takes it
// a frame later.
void tide_gui_hide(tide_gui *g, tide_devices *devices);

// ---------------------------------------------------------------------------
// For generated code

// Before each view runs: `view` tells the views apart, from 1. The devices it
// reads are the frame's, less what the GUI uses and what the other views
// claimed last frame: what it claimed itself, it goes on reading.
void tide_gui_view(tide_gui *g, uint32_t view);

// GUI.ClaimPointer and GUI.ClaimKeyboard: the view that's running is using
// the pointer or the keyboard, for widgets of its own. Called every frame it
// does: a claim lasts until the end of the next frame.
void tide_gui_claim_pointer(tide_gui *g);
void tide_gui_claim_keyboard(tide_gui *g);

// GUI.ClaimPointer(rect): the view that's running has a widget of its own at
// `rect`, in the GUI's pixels. Called every frame the widget is there. The
// pointer on it is the view's, as it's the GUI's on the GUI's own widgets:
// from the frame it comes there, with no claim before it, so a finger that
// touches the widget never reaches the game. Past TIDE_GUI_MAX_CLAIM_RECTS
// places in a frame, it claims the pointer while the pointer is on the rect,
// which hides a frame late.
void tide_gui_claim_pointer_at(tide_gui *g, tide_rect rect);

// GUI.ShowKeyboard: the player is typing into something of the view's, so
// phones show their keyboard. Called every frame they are.
void tide_gui_show_keyboard(tide_gui *g);

// GUI.Navigate: a menu is up, so the arrows, the d-pad and the left stick can
// start moving the focus though the game reads the devices. Without it they
// only move a focus that's there, so they never take a HUD's button away from
// the game. Called every frame the menu is up. Once a widget has the focus,
// the keyboard and the gamepad are hidden from the game, as ever.
void tide_gui_navigate(tide_gui *g);

// Cursor.Lock: a view wants the mouse for itself, so the window holds the
// cursor, hidden (see tide_gui_cursor_lock). Called every frame it does: the
// cursor comes back once a frame goes by without it.
void tide_gui_lock_cursor(tide_gui *g);

// Mixes two numbers into an ID. Never 0, which means no widget.
uint32_t tide_gui_seed(uint32_t a, uint32_t b);

// A widget's ID: `site` is where it's called from, and `seed` tells apart the
// entities a view runs for and the places a function is called from. Calls
// from the same site with the same seed, as in a loop, count up.
uint32_t tide_gui_id(tide_gui *g, uint32_t seed, uint32_t site);

// The look every frame starts with.
tide_gui_theme tide_gui_theme_default(void);

// GUI.SetTheme: the GUI's widgets look as `theme` says from here on, in this
// view and the ones after it, until it's set again or the frame ends.
void tide_gui_set_theme(tide_gui *g, tide_gui_theme theme);

// GUI.Control: a widget the caller draws itself, at `rect`. The GUI draws
// nothing for it, and handles its input as a button's: the pointer on it is
// the GUI's, so a press there never reaches the game or the views, from the
// frame it lands in; the focus moves to it and from it with the other widgets,
// in the order they're called; and a Disabled block or a modal over it stops
// it as it stops any widget: then nothing of it is true. While it has the
// focus, the view that called it goes on reading the keyboard and the
// gamepad, but for what moves the focus and clicks: the arrows, Tab, Enter,
// Space and Escape, and the d-pad, the left stick and the south and east
// buttons.
tide_control tide_gui_control(tide_gui *g, uint32_t id, tide_rect rect);

// Widgets at a rect (GUI). The ones with a value change it, and return whether
// they did; a button returns whether it was pressed.
void tide_gui_label(tide_gui *g, tide_rect rect, const char *text);
bool tide_gui_button(tide_gui *g, uint32_t id, tide_rect rect, const char *text);
bool tide_gui_toggle(tide_gui *g, uint32_t id, tide_rect rect, const char *text, bool *value);
bool tide_gui_slider(tide_gui *g, uint32_t id, tide_rect rect, const char *label, float *value, float min, float max);
bool tide_gui_int_slider(tide_gui *g, uint32_t id, tide_rect rect, const char *label, int32_t *value, int32_t min,
                         int32_t max);
bool tide_gui_int_field(tide_gui *g, uint32_t id, tide_rect rect, const char *label, int32_t *value);
bool tide_gui_float_field(tide_gui *g, uint32_t id, tide_rect rect, const char *label, float *value);
bool tide_gui_float2_field(tide_gui *g, uint32_t id, tide_rect rect, const char *label, tide_float2 *value);
bool tide_gui_float3_field(tide_gui *g, uint32_t id, tide_rect rect, const char *label, tide_float3 *value);
bool tide_gui_float4_field(tide_gui *g, uint32_t id, tide_rect rect, const char *label, tide_float4 *value);
bool tide_gui_color_field(tide_gui *g, uint32_t id, tide_rect rect, const char *label, tide_color *value);
bool tide_gui_text_field(tide_gui *g, uint32_t id, tide_rect rect, const char *label, tide_textref value);

// The fields that are typed into, saying whether the player entered what they
// typed (TextField(rect, label, value, out entered)): `entered` is true in the
// frame Enter ends the typing in this field (the numpad's too, and for a
// number field the south button, which ends its typing as well), with what
// was typed before it in that frame in the value, and false in every other:
// while it's typed into, when Enter starts the typing, and when the typing
// ends another way (Escape, the focus moving on, a click elsewhere, the field
// disabled or under a modal). What they return is as ever: whether the value
// changed, which a number field's does as it's entered.
bool tide_gui_int_field_entered(tide_gui *g, uint32_t id, tide_rect rect, const char *label, int32_t *value, bool *entered);
bool tide_gui_float_field_entered(tide_gui *g, uint32_t id, tide_rect rect, const char *label, float *value, bool *entered);
bool tide_gui_text_field_entered(tide_gui *g, uint32_t id, tide_rect rect, const char *label, tide_textref value,
                                 bool *entered);

// The same, laid out automatically (GUILayout).
void tide_gui_layout_label(tide_gui *g, const char *text);
bool tide_gui_layout_button(tide_gui *g, uint32_t id, const char *text);
bool tide_gui_layout_toggle(tide_gui *g, uint32_t id, const char *text, bool *value);
bool tide_gui_layout_slider(tide_gui *g, uint32_t id, const char *label, float *value, float min, float max);
bool tide_gui_layout_int_slider(tide_gui *g, uint32_t id, const char *label, int32_t *value, int32_t min, int32_t max);
bool tide_gui_layout_int_field(tide_gui *g, uint32_t id, const char *label, int32_t *value);
bool tide_gui_layout_float_field(tide_gui *g, uint32_t id, const char *label, float *value);
bool tide_gui_layout_float2_field(tide_gui *g, uint32_t id, const char *label, tide_float2 *value);
bool tide_gui_layout_float3_field(tide_gui *g, uint32_t id, const char *label, tide_float3 *value);
bool tide_gui_layout_float4_field(tide_gui *g, uint32_t id, const char *label, tide_float4 *value);
bool tide_gui_layout_color_field(tide_gui *g, uint32_t id, const char *label, tide_color *value);
bool tide_gui_layout_text_field(tide_gui *g, uint32_t id, const char *label, tide_textref value);
bool tide_gui_layout_int_field_entered(tide_gui *g, uint32_t id, const char *label, int32_t *value, bool *entered);
bool tide_gui_layout_float_field_entered(tide_gui *g, uint32_t id, const char *label, float *value, bool *entered);
bool tide_gui_layout_text_field_entered(tide_gui *g, uint32_t id, const char *label, tide_textref value, bool *entered);
void tide_gui_layout_space(tide_gui *g, float size);

// Containers. Each returns how many were open before it, which the matching
// tide_gui_close takes: it closes this one and any left open inside it.
int tide_gui_begin_vertical(tide_gui *g, uint32_t id);
int tide_gui_begin_horizontal(tide_gui *g, uint32_t id);
int tide_gui_begin_area(tide_gui *g, uint32_t id, tide_rect rect);
int tide_gui_begin_area_at(tide_gui *g, uint32_t id, int32_t anchor);
// A modal: an area at an anchor over a dimmed screen, while `open` is true.
// Back closes it, setting `open` to false. Returns -1 when it isn't up: then
// its content doesn't run, and there's nothing to close.
int tide_gui_begin_modal(tide_gui *g, uint32_t id, int32_t anchor, bool *open);
// Widgets until the matching close are grayed out and don't work while
// `disabled` is true: they can't be hovered, pressed, focused or typed into,
// and let go of what they had. It lays out nothing: its widgets go on in the
// container around it. Inside another, it stays disabled whatever `disabled` is.
int tide_gui_begin_disabled(tide_gui *g, bool disabled);
// GUI.Theme: widgets until the matching close look as `theme` says, and then
// as they did before it. It lays out nothing, like a Disabled block.
int tide_gui_begin_theme(tide_gui *g, tide_gui_theme theme);
void tide_gui_close(tide_gui *g, int depth);
