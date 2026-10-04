// The language server, driven with JSON-RPC messages as an editor sends them.
// In test documents, `$` marks the cursor.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "json.h"
#include "tide_test.h"
#include "server.h"

static lsp_server server;
static char *sent[16];
static int sent_count;

static void clear_sent(void)
{
    for (int i = 0; i < sent_count; i++) free(sent[i]);
    sent_count = 0;
}

static void capture(void *user, const char *message, const size_t len)
{
    (void)user;
    if (sent_count == 16) return;
    char *copy = malloc(len + 1);
    memcpy(copy, message, len);
    copy[len] = '\0';
    sent[sent_count++] = copy;
}

static void handle(const char *message)
{
    lsp_handle(&server, message, strlen(message));
}

static const char *last_sent(void)
{
    return sent_count > 0 ? sent[sent_count - 1] : "";
}

static bool has(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

// An editor like VS Code: it applies edits that create files.
#define EDITOR_CAPABILITIES                                                                                        \
    "{\"capabilities\":{\"workspace\":{\"workspaceEdit\":{\"documentChanges\":true,"                                \
    "\"resourceOperations\":[\"create\",\"rename\",\"delete\"]}}}}"

static void start_with(const char *params)
{
    clear_sent();
    lsp_free(&server);
    lsp_init(&server, capture, NULL);
    char message[512];
    snprintf(message, sizeof message, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":%s}", params);
    handle(message);
}

static void start(void)
{
    start_with(EDITOR_CAPABILITIES);
}

// Opens `text` as the document, first removing the `$` that marks the cursor.
static int cursor_line;
static int cursor_character;

// Documents are written with the cursor as `$`; `$$` is a `$` of the text,
// as in $$"score {score}".
static void open_document(const char *marked)
{
    char text[8192];
    size_t n = 0;
    int line = 0;
    int character = 0;
    cursor_line = cursor_character = -1;
    for (const char *p = marked; *p && n + 1 < sizeof text; p++) {
        if (*p == '$' && p[1] != '$') {
            cursor_line = line;
            cursor_character = character;
            continue;
        }
        if (*p == '$') p++;
        text[n++] = *p;
        if (*p == '\n') {
            line++;
            character = 0;
        } else {
            character++;
        }
    }
    text[n] = '\0';

    jbuf b = {0};
    jb_put(&b, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":"
               "{\"uri\":\"file:///test.tide\",\"languageId\":\"tide\",\"version\":1,\"text\":");
    jb_string(&b, text);
    jb_put(&b, "}}}");
    clear_sent();
    handle(b.data);
    jb_free(&b);
}

// Sends a request at the cursor, with `extra` params (JSON members, or ""), and
// returns the reply.
static const char *request_with(const char *method, const char *extra)
{
    char message[1024];
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"%s\",\"params\":{\"textDocument\":{\"uri\":\"file:///test.tide\"},"
             "\"position\":{\"line\":%d,\"character\":%d}%s%s}}",
             method, cursor_line, cursor_character, extra[0] ? "," : "", extra);
    clear_sent();
    handle(message);
    return last_sent();
}

static const char *request(const char *method)
{
    return request_with(method, "");
}

// How many times `needle` appears.
static int count(const char *haystack, const char *needle)
{
    int n = 0;
    for (const char *p = strstr(haystack, needle); p; p = strstr(p + 1, needle)) n++;
    return n;
}

static size_t offset_of(const char *text, const int line, const int character)
{
    size_t i = 0;
    for (int l = 0; l < line && text[i]; i++) {
        if (text[i] == '\n') l++;
    }
    return i + (size_t)character;
}

static int edit_order(const void *a, const void *b)
{
    const json *x = *(const json *const *)a;
    const json *y = *(const json *const *)b;
    const int lx = json_int(json_path(x, "range", "start", "line", NULL), 0);
    const int ly = json_int(json_path(y, "range", "start", "line", NULL), 0);
    if (lx != ly) return ly - lx;
    return json_int(json_path(y, "range", "start", "character", NULL), 0)
         - json_int(json_path(x, "range", "start", "character", NULL), 0);
}

// Applies the TextEdit[] in `edits` to `text` (ASCII), last edit first.
static void apply_edits(char *text, const size_t cap, const json *edits)
{
    json *sorted[256];
    int n = 0;
    for (int i = 0; i < edits->count && n < 256; i++) sorted[n++] = edits->items[i];
    qsort(sorted, (size_t)n, sizeof sorted[0], edit_order);
    for (int i = 0; i < n; i++) {
        const json *range = json_get(sorted[i], "range");
        const size_t start = offset_of(text, json_int(json_path(range, "start", "line", NULL), 0),
                                       json_int(json_path(range, "start", "character", NULL), 0));
        const size_t end = offset_of(text, json_int(json_path(range, "end", "line", NULL), 0),
                                     json_int(json_path(range, "end", "character", NULL), 0));
        const json *replacement = json_get(sorted[i], "newText");
        const size_t len = strlen(text);
        if (len - (end - start) + replacement->string_len >= cap) return;
        memmove(text + start + replacement->string_len, text + end, len - end + 1);
        memcpy(text + start, replacement->string, replacement->string_len);
    }
}

// The document after applying the edits in the last reply's result (or at `path` inside it).
static char applied[8192];

static const char *apply_reply(const char *original, const char *path)
{
    size_t n = 0;
    for (const char *p = original; *p && n + 1 < sizeof applied; p++) { // As open_document reads it
        if (*p == '$' && p[1] != '$') continue;
        if (*p == '$') p++;
        applied[n++] = *p;
    }
    applied[n] = '\0';
    const json *reply = json_parse(last_sent(), strlen(last_sent()));
    const json *edits = json_get(reply, "result");
    if (path) edits = json_path(edits, "changes", path, NULL);
    if (edits && edits->kind == JSON_ARRAY) apply_edits(applied, sizeof applied, edits);
    json_release();
    return applied;
}

static const char *complete(const char *marked)
{
    open_document(marked);
    return request("textDocument/completion");
}

// `"label":"name"` appears in a completion list.
static bool offers(const char *reply, const char *label)
{
    char needle[128];
    snprintf(needle, sizeof needle, "\"label\":\"%s\"", label);
    return has(reply, needle);
}

#define GAME_TYPES                                                             \
    "component Body\n"                                                         \
    "{\n"                                                                      \
    "    float2 position;\n"                                                   \
    "    float radius = 10;\n"                                                 \
    "}\n"                                                                      \
    "\n"                                                                       \
    "singleton Arena\n"                                                        \
    "{\n"                                                                      \
    "    float2 halfSize = float2(400, 225);\n"                                \
    "}\n"                                                                      \
    "\n"                                                                       \
    "input PlayerInput\n"                                                      \
    "{\n"                                                                      \
    "    float2 move;\n"                                                       \
    "    bool fire;\n"                                                         \
    "}\n"                                                                      \
    "\n"                                                                       \
    "scene Main { }\n"                                                         \
    "event(Spawned) Setup(with Main)\n"                                        \
    "{\n"                                                                      \
    "    Spawn(Body, Owner);\n"                                                \
    "}\n"

TIDE_TEST(lsp_initialize_advertises_features)
{
    start();
    const char *reply = last_sent();
    TIDE_CHECK(has(reply, "\"id\":1"));
    TIDE_CHECK(has(reply, "\"completionProvider\""));
    TIDE_CHECK(has(reply, "\"hoverProvider\":true"));
    TIDE_CHECK(has(reply, "\"tokenTypes\":[\"namespace\""));
}

TIDE_TEST(lsp_diagnostics)
{
    start();
    open_document(GAME_TYPES);
    TIDE_CHECK(has(last_sent(), "\"method\":\"textDocument/publishDiagnostics\""));
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.pos = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "Body has no field 'pos'"));
    TIDE_CHECK(has(last_sent(), "\"range\":{\"start\":{\"line\":24,\"character\":9},\"end\":{\"line\":24,\"character\":12}}"));
    TIDE_CHECK(has(last_sent(), "\"severity\":1"));
}

TIDE_TEST(lsp_diagnostics_keep_going_after_syntax_errors)
{
    start();
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.position = ;\n    var x = 1 +;\n    body.radius = true;\n}\n");
    const char *reply = last_sent();
    TIDE_CHECK(has(reply, "expected an expression, found ';'"));
    TIDE_CHECK(has(reply, "\"line\":24")); // The second syntax error
    TIDE_CHECK(has(reply, "can't assign bool to float")); // And the checker still ran
}

TIDE_TEST(lsp_complete_fields_after_dot)
{
    start();
    const char *reply = complete(GAME_TYPES "system Move(mut Body body)\n{\n    body.$\n}\n");
    TIDE_CHECK(offers(reply, "position"));
    TIDE_CHECK(offers(reply, "radius"));
    TIDE_CHECK(!offers(reply, "halfSize"));
}

TIDE_TEST(lsp_complete_chains_and_swizzles)
{
    start();
    const char *reply = complete(GAME_TYPES "system Move(mut Body body)\n{\n    var p = body.position.$\n}\n");
    TIDE_CHECK(offers(reply, "x"));
    TIDE_CHECK(offers(reply, "y"));
    TIDE_CHECK(!offers(reply, "z"));
    TIDE_CHECK(offers(reply, "xy") == false); // A float2's only swizzle of all its components is itself
}

TIDE_TEST(lsp_complete_input_edges)
{
    start();
    const char *reply = complete(GAME_TYPES "system Fire(PlayerInput input)\n{\n    if (input.fire.$) return;\n}\n");
    TIDE_CHECK(offers(reply, "down"));
    TIDE_CHECK(offers(reply, "up"));
}

// Inside Sanitize, the input's fields are in scope; this machine's devices aren't.
TIDE_TEST(lsp_sanitize)
{
    start();
    static const char program[] = "input PlayerInput\n{\n    float move;\n\n    Sample() { }\n\n"
                                  "    Sanitize()\n    {\n        move = Math.Clamp($, -1, 1);\n    }\n}\n"
                                  "scene Main { }\n";
    const char *reply = complete(program);
    TIDE_CHECK(offers(reply, "move"));
    TIDE_CHECK(!offers(reply, "Devices"));

    open_document("input PlayerInput\n{\n    float move;\n    Sani$tize() { move = Math.Clamp(move, -1, 1); }\n}\n"
         "scene Main { }\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "Runs on every input before the simulation reads it"));

    open_document("input PlayerInput\n{\n    bool jump;\n    Sam$ple() { jump = Devices.keyboard.space.pressed; }\n}\n"
         "scene Main { }\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "Builds the player's input from this machine's `Devices`"));
}

static const char *format_reply(const char *text);
static const char *request_at(const char *uri, const char *method, int line, int character, const char *extra);
static const char *actions_at(int line);

// [Clamp], [Min] and [Max] on input fields: completion, and formatting.
TIDE_TEST(lsp_field_attributes)
{
    start();
    const char *names = complete("input PlayerInput\n{\n    [$] float move;\n}\nscene Main { }\n");
    TIDE_CHECK(offers(names, "Clamp") && offers(names, "Min") && offers(names, "Max"));
    TIDE_CHECK(!offers(names, "Before"));
    const char *bounds = complete("input PlayerInput\n{\n    [Clamp(float2(0, 0), $)] float2 aim;\n}\nscene Main { }\n");
    TIDE_CHECK(offers(bounds, "Math") && offers(bounds, "float2"));

    static const char messy[] = "input PlayerInput\n{\n[Clamp(-1,1)]float move;\n    [Min(0),Max(3)]   int gear;\n}\n"
                                "scene Main { }\n";
    static const char expected[] = "input PlayerInput\n{\n    [Clamp(-1, 1)] float move;\n    [Min(0), Max(3)] int gear;\n}\n"
                                   "scene Main { }\n";
    open_document(messy);
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}

TIDE_TEST(lsp_complete_builtin_owners)
{
    start();
    const char *math = complete(GAME_TYPES "system S(mut Body body)\n{\n    body.radius = Math.$\n}\n");
    TIDE_CHECK(offers(math, "Sin"));
    TIDE_CHECK(offers(math, "PI"));
    TIDE_CHECK(offers(math, "Normalize"));
    TIDE_CHECK(!offers(math, "NormalizeSafe")); // Gone: Normalize is the safe one

    const char *draw = complete(GAME_TYPES "view V(Body body)\n{\n    Draw2D.$\n}\n");
    TIDE_CHECK(offers(draw, "Circle"));
    TIDE_CHECK(has(draw, "Circle(${1:center}, ${2:radius}, ${3:color})"));
    TIDE_CHECK(offers(draw, "WorldToScreen") && offers(draw, "ScreenToWorld") && offers(draw, "Instanced") && !offers(draw, "Cube"));

    const char *draw3 = complete(GAME_TYPES "view V(Body body)\n{\n    Draw3D.$\n}\n");
    TIDE_CHECK(offers(draw3, "Cube") && offers(draw3, "Sphere") && offers(draw3, "Cylinder") && offers(draw3, "Plane"));
    TIDE_CHECK(offers(draw3, "WorldToScreen") && offers(draw3, "ScreenToWorld") && offers(draw3, "ScreenRayOrigin")
               && offers(draw3, "ScreenRayDirection") && offers(draw3, "Instanced") && offers(draw3, "list"));
    TIDE_CHECK(!offers(draw3, "Circle") && !offers(draw3, "Rect") && !offers(draw3, "Screen"));

    const char *color = complete(GAME_TYPES "view V(Body body)\n{\n    Draw2D.Circle(body.position, 1, Color.$);\n}\n");
    TIDE_CHECK(offers(color, "red"));
}

TIDE_TEST(lsp_complete_draw_only_in_views)
{
    start();
    const char *system = complete(GAME_TYPES "system S(mut Body body)\n{\n    $\n}\n");
    TIDE_CHECK(!offers(system, "Draw2D"));
    TIDE_CHECK(!offers(system, "Draw3D"));
    TIDE_CHECK(offers(system, "Spawn"));
    const char *view = complete(GAME_TYPES "view V(Body body)\n{\n    $\n}\n");
    TIDE_CHECK(offers(view, "Draw2D"));
    TIDE_CHECK(offers(view, "Draw3D") && !offers(view, "Draw"));
    TIDE_CHECK(!offers(view, "Body")); // Views spawn local entities, never the match's
}

TIDE_TEST(lsp_complete_io_in_local_code)
{
    start();
    const char *reply = complete("local scene Main { }\nlocal event(Spawned) Setup(with Main) { $ }\n");
    TIDE_CHECK(offers(reply, "File") && offers(reply, "Directory") && offers(reply, "Application") && offers(reply, "Path"));
    reply = complete("scene Main { }\nevent(Spawned) Setup(with Main) { $ }\n");
    TIDE_CHECK(!offers(reply, "File") && !offers(reply, "Directory") && !offers(reply, "Application"));
    TIDE_CHECK(offers(reply, "Path"));
    reply = complete("local scene Main { }\nlocal event(Spawned) Setup(with Main) { File.$ }\n");
    TIDE_CHECK(offers(reply, "ReadAllText") && offers(reply, "ReadAllBytes") && offers(reply, "WriteAllBytes"));
    TIDE_CHECK(has(reply, "List<byte>"));
    TIDE_CHECK(has(reply, "fails IOError"));
    reply = complete("local scene Main { }\nlocal event(Spawned) Setup(with Main) { Application.$ }\n");
    TIDE_CHECK(offers(reply, "persistentDataPath") && offers(reply, "temporaryCachePath"));
}

TIDE_TEST(lsp_complete_names_in_scope)
{
    start();
    const char *reply = complete(GAME_TYPES "system S(Time time, mut Body body)\n{\n    var speed = 2.0;\n"
                                            "    if (true)\n    {\n        var inner = 1;\n    }\n    body.radius = $\n}\n");
    TIDE_CHECK(offers(reply, "speed"));
    TIDE_CHECK(offers(reply, "time"));
    TIDE_CHECK(offers(reply, "body"));
    TIDE_CHECK(!offers(reply, "inner")); // Out of scope
    TIDE_CHECK(offers(reply, "float3"));
    TIDE_CHECK(offers(reply, "Math"));
}

TIDE_TEST(lsp_complete_in_constructor)
{
    start();
    const char *devices = complete("input PlayerInput\n{\n    float2 move;\n\n    Sample()\n    {\n"
                                   "        var keys = Devices.$\n    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(devices, "keyboard"));
    TIDE_CHECK(offers(devices, "gamepad"));

    const char *keys = complete("input PlayerInput\n{\n    float2 move;\n\n    Sample()\n    {\n"
                                "        var keys = Devices.keyboard;\n        if (keys.$\n    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(keys, "space"));
    TIDE_CHECK(offers(keys, "leftShift"));

    // Sample takes local singletons; Devices is a name.
    const char *params = complete("local singleton Menu { bool open; }\ninput PlayerInput\n{\n    float2 move;\n\n"
                                  "    Sample($)\n    {\n    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(params, "Menu"));
    TIDE_CHECK(!offers(params, "Devices"));
    const char *names = complete("input PlayerInput\n{\n    float2 move;\n\n    Sample()\n    {\n        move = $\n"
                                 "    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(names, "Devices"));

    // Views read them too, and systems take them.
    const char *view = complete("scene Main { }\nview Pause()\n{\n    if (Devices.keyboard.$\n}\n");
    TIDE_CHECK(offers(view, "escape"));
    // What's typed is a frame's: views and functions read it, not Sample or systems.
    TIDE_CHECK(offers(view, "text"));
    TIDE_CHECK(offers(complete("scene Main { }\nint Typed(Keyboard keys)\n{\n    return keys.$\n}\n"), "text"));
    TIDE_CHECK(!offers(complete("input PlayerInput\n{\n    float2 move;\n\n    Sample()\n    {\n"
                                "        if (Devices.keyboard.$\n    }\n}\nscene Main { }\n"),
                       "text"));
    TIDE_CHECK(!offers(complete("scene Main { }\ncomponent Body { float x; }\n"
                                "system Move(Devices devices, mut Body body)\n{\n    body.x += devices.keyboard.$\n}\n"),
                       "text"));
    TIDE_CHECK(offers(complete("scene Main { }\nview Chat()\n{\n    var n = Devices.keyboard.text.$\n}\n"), "length"));
    open_document("scene Main { }\nlocal singleton Chat { string line; }\n"
                  "view Type(mut Chat chat)\n{\n    chat.line += Devices.keyboard.te$xt;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *typed = request("textDocument/hover");
    TIDE_CHECK(has(typed, "string text"));
    TIDE_CHECK(has(typed, "What was typed since the last frame"));
    const char *system = complete("scene Main { }\ncomponent Body { float x; }\n"
                                  "system Move(Devices devices, mut Body body)\n{\n    body.x += devices.gamepad.$\n}\n");
    TIDE_CHECK(offers(system, "leftStick"));
    const char *header = complete("scene Main { }\ncomponent Body { float x; }\nsystem Move($)\n{\n}\n");
    TIDE_CHECK(offers(header, "Devices"));
}

TIDE_TEST(lsp_complete_declarations_and_headers)
{
    start();
    const char *top = complete(GAME_TYPES "\n$");
    TIDE_CHECK(offers(top, "component"));
    TIDE_CHECK(offers(top, "view"));

    const char *header = complete(GAME_TYPES "system S($)\n{\n}\n");
    TIDE_CHECK(offers(header, "mut"));
    TIDE_CHECK(offers(header, "without"));
    TIDE_CHECK(offers(header, "Body"));
    TIDE_CHECK(offers(header, "Arena"));
    TIDE_CHECK(offers(header, "PlayerInput"));

    const char *filter = complete(GAME_TYPES "system S(with $)\n{\n}\n");
    TIDE_CHECK(offers(filter, "Body"));
    TIDE_CHECK(!offers(filter, "Arena")); // Singletons aren't filters

    const char *name = complete(GAME_TYPES "system S(PlayerInput $)\n{\n}\n");
    TIDE_CHECK(offers(name, "playerInput"));
}

TIDE_TEST(lsp_complete_literal_fields)
{
    start();
    const char *reply = complete(GAME_TYPES "system S()\n{\n    Spawn(Body { $ });\n}\n");
    TIDE_CHECK(offers(reply, "position"));
    TIDE_CHECK(offers(reply, "radius"));
}

// What a function's or method's parameter can be, not an input's Sample's.
TIDE_TEST(lsp_complete_routine_params)
{
    start();
#define TYPES "struct Stats { float health; }\nlocal singleton Menu { bool open; }\n" GAME_TYPES
    const char *function = complete(TYPES "void Heal($)\n{\n}\n");
    TIDE_CHECK(offers(function, "mut") && offers(function, "int") && offers(function, "bool") && offers(function, "string"));
    TIDE_CHECK(offers(function, "List") && offers(function, "Entity") && offers(function, "Stats") && offers(function, "Body"));
    TIDE_CHECK(offers(function, "Action") && offers(function, "Keyboard"));
    TIDE_CHECK(!offers(function, "Menu") && !offers(function, "Session") && !offers(function, "Grid2"));
    const char *after_mut = complete(TYPES "void Heal(mut $)\n{\n}\n");
    TIDE_CHECK(offers(after_mut, "Stats") && offers(after_mut, "float2") && !offers(after_mut, "mut"));
    TIDE_CHECK(!offers(after_mut, "Action"));
    TIDE_CHECK(offers(complete(TYPES "async void Load(Stats s, $)\n{\n}\n"), "Arena")); // A task's singletons
    TIDE_CHECK(offers(complete(TYPES "void Heal(Stats $)\n{\n}\n"), "stats"));
#undef TYPES

    // A method's, in its type's body: no default values there.
    const char *method = complete("struct Stats\n{\n    float health;\n    bool Near($) { return true; }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(method, "mut") && offers(method, "bool") && offers(method, "string") && offers(method, "Entity"));
    TIDE_CHECK(!offers(method, "Math") && !offers(method, "Action"));
    TIDE_CHECK(offers(complete("struct Stats\n{\n    float health;\n    mut void Set(mut $) { }\n}\nscene Main { }\n"), "float"));
    // The input's Sample still takes local singletons, and Sanitize nothing.
    TIDE_CHECK(offers(complete("local singleton Menu { bool open; }\ninput I\n{\n    bool fire;\n    Sample($) { }\n}\n"
                               "scene Main { }\n"),
                      "Menu"));
    TIDE_CHECK(!offers(complete("local singleton Menu { bool open; }\ninput I\n{\n    bool fire;\n    Sanitize($) { }\n}\n"
                                "scene Main { }\n"),
                       "Menu"));
}

// Locals in bodies without braces: an else if's, a loop's, and its variable.
TIDE_TEST(lsp_complete_locals_in_braceless_bodies)
{
    start();
    TIDE_CHECK(offers(complete("scene Main { }\nsystem S()\n{\n    var a = 1;\n    if (a > 1) { }\n"
                               "    else if (a > 0)\n    {\n        var inner = 2;\n        var x = $\n    }\n}\n"),
                      "inner"));
#define PARTS "struct Part { float size; }\ncomponent Kit { List<Part> parts; }\nscene Main { }\nvoid Use(float size) { }\n"
    // A whole statement, and one being typed, which doesn't parse yet
    TIDE_CHECK(offers(complete(PARTS "system S(Kit kit)\n{\n    foreach (var p in kit.parts) Use(p.$size);\n}\n"), "size"));
    TIDE_CHECK(offers(complete(PARTS "system S(Kit kit)\n{\n    foreach (var p in kit.parts) Use(p.$)\n}\n"), "size"));
    TIDE_CHECK(offers(complete(PARTS "system S(Kit kit)\n{\n    foreach (var p in kit.parts) Use($);\n}\n"), "p"));
    TIDE_CHECK(offers(complete(PARTS "system S(Kit kit)\n{\n    for (var i = 0; i < 3; i++) Use($);\n}\n"), "i"));
    TIDE_CHECK(offers(complete("singleton F { Grid2<int> cells; }\nscene Main { }\nvoid Use(int2 p) { }\n"
                               "system S(mut F f)\n{\n    parallel (var at in f.cells) Use($);\n}\n"),
                      "at"));
    // Not after the body ends
    TIDE_CHECK(!offers(complete(PARTS "system S(Kit kit)\n{\n    foreach (var p in kit.parts) Use(1);\n    var x = $\n}\n"), "p"));
#undef PARTS
}

// The types a local can be, where a statement starts.
TIDE_TEST(lsp_complete_statement_types)
{
    start();
    const char *system = complete("scene Main { }\nsystem S()\n{\n    $\n}\n");
    TIDE_CHECK(offers(system, "bool") && offers(system, "string") && offers(system, "Entity") && offers(system, "List"));
    TIDE_CHECK(!offers(system, "Grid2") && !offers(system, "LocalEntity"));
    TIDE_CHECK(offers(complete("scene Main { }\nview V()\n{\n    $\n}\n"), "LocalEntity"));
    // In a value: what makes one
    const char *value = complete("scene Main { }\nsystem S()\n{\n    var x = $\n}\n");
    TIDE_CHECK(offers(value, "float3") && !offers(value, "bool") && !offers(value, "List"));
    // After mut: var, or a type
    const char *after_mut = complete("scene Main { }\nsystem S()\n{\n    mut $\n}\n");
    TIDE_CHECK(offers(after_mut, "var") && offers(after_mut, "int") && offers(after_mut, "string"));
}

// Completion in the rest of the places that had none.
TIDE_TEST(lsp_complete_more_places)
{
    start();
    const char *constant = complete("struct Stats { int armor; }\nenum Page { Title }\nconst $\nscene Main { }\n");
    TIDE_CHECK(offers(constant, "int") && offers(constant, "string") && offers(constant, "Stats") && offers(constant, "Page"));
    TIDE_CHECK(!offers(constant, "Entity") && !offers(constant, "List"));
    const char *error = complete("enum ParseError { Empty }\nint Parse(string t) fails $\nscene Main { }\n");
    TIDE_CHECK(offers(error, "ParseError"));
    TIDE_CHECK(offers(complete("enum E { A }\nstruct S\n{\n    int x;\n    int Get() fails $\n}\nscene Main { }\n"), "E"));

    // An enum's body: names, and after '=', an int constant
    const char *members = complete("const int FIRST = 3;\nconst float HALF = 0.5;\nenum Page\n{\n    Title,\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(!offers(members, "float3") && !offers(members, "Math") && !offers(members, "FIRST"));
    const char *value = complete("const int FIRST = 3;\nconst float HALF = 0.5;\nenum Page : byte\n{\n    Title = $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(value, "FIRST") && !offers(value, "HALF") && !offers(value, "float3"));

    // A loop's keywords
    TIDE_CHECK(offers(complete("scene Main { }\nsystem S()\n{\n    foreach (var x $\n}\n"), "in"));
    TIDE_CHECK(offers(complete("scene Main { }\nsystem S()\n{\n    foreach (var x i$\n}\n"), "in"));
#define FIELD "singleton F { Grid2<int> cells; }\nscene Main { }\n"
    const char *parallel = complete(FIELD "system S(mut F f)\n{\n    parallel (var at in f.cells $\n}\n");
    TIDE_CHECK(offers(parallel, "by") && offers(parallel, "offset"));
    const char *offset = complete(FIELD "system S(mut F f)\n{\n    parallel (var at in f.cells by 2 $\n}\n");
    TIDE_CHECK(!offers(offset, "by") && offers(offset, "offset"));
    TIDE_CHECK(!offers(complete(FIELD "system S(mut F f)\n{\n    parallel (var at in f.cells by $\n}\n"), "offset"));
#undef FIELD

    // A grid field's size
    TIDE_CHECK(offers(complete("singleton F { Grid2<int> cells = $ }\nscene Main { }\n"), "Grid2"));
    TIDE_CHECK(!offers(complete("singleton F { int cells = $ }\nscene Main { }\n"), "Grid2"));
    TIDE_CHECK(!offers(complete("struct S\n{\n    $\n}\nscene Main { }\n"), "Grid2")); // Structs can't hold one
}

TIDE_TEST(lsp_hover)
{
    start();
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body$.radius = 1;\n}\n");
    const char *param = request("textDocument/hover");
    TIDE_CHECK(has(param, "mut Body body"));

    open_document(GAME_TYPES "system Move(mut B$ody body)\n{\n}\n");
    const char *type = request("textDocument/hover");
    TIDE_CHECK(has(type, "component Body"));
    TIDE_CHECK(has(type, "float radius = 10;"));

    open_document(GAME_TYPES "view V(Body body)\n{\n    Draw2D.Cir$cle(body.position, 1, Color.red);\n}\n");
    const char *function = request("textDocument/hover");
    TIDE_CHECK(has(function, "Draw2D.Circle(float2 center, float radius, Color color)"));
    TIDE_CHECK(has(function, "A filled circle."));

    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.position = Math.Nor$malize(body.position);\n}\n");
    const char *normalize = request("textDocument/hover");
    TIDE_CHECK(has(normalize, "Math.Normalize(float2 vector)"));
    TIDE_CHECK(has(normalize, "gives zero, never NaN"));
    TIDE_CHECK(has(normalize, "gives the identity, never NaN")); // A quaternion's

    // Time's fields say what they are, where they're read and where they're offered
    open_document(GAME_TYPES "system Move(mut Body body, Time time)\n{\n    body.radius = time.t$ * time.tickRate;\n}\n");
    const char *seconds = request("textDocument/hover");
    TIDE_CHECK(has(seconds, "float t") && has(seconds, "The seconds since the match started: `tick * dt`"));
    const char *time = complete(GAME_TYPES "system Move(mut Body body, Time time)\n{\n    body.radius = time.$\n}\n");
    TIDE_CHECK(offers(time, "dt") && offers(time, "tick") && offers(time, "t") && offers(time, "tickRate"));
    TIDE_CHECK(has(time, "The ticks in a second") && has(time, "The length of a tick in seconds"));
}

TIDE_TEST(lsp_definition)
{
    start();
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    bo$dy.radius = 1;\n}\n");
    const char *param = request("textDocument/definition");
    TIDE_CHECK(has(param, "\"range\":{\"start\":{\"line\":22,\"character\":21}"));

    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.rad$ius = 1;\n}\n");
    const char *field = request("textDocument/definition");
    TIDE_CHECK(has(field, "\"range\":{\"start\":{\"line\":3,\"character\":10}"));
}

// The line a definition reply points at, read from the file it names ("" if none).
static const char *definition_line(void)
{
    static char text[512];
    text[0] = '\0';
    const json *reply = json_parse(last_sent(), strlen(last_sent()));
    const char *uri = json_str(json_path(reply, "result", "uri", NULL));
    const int line = json_int(json_path(reply, "result", "range", "start", "line", NULL), -1);
    if (uri && strncmp(uri, "file://", 7) == 0 && line >= 0) {
        const char *path = uri + 7;
        if (path[0] == '/' && path[2] == ':') path++; // file:///D:/... on Windows
        FILE *f = fopen(path, "rb");
        for (int l = 0; f && l <= line && fgets(text, sizeof text, f); l++) {
        }
        if (f) fclose(f);
    }
    json_release();
    return text;
}

static const char *c_definition(const char *marked)
{
    open_document(marked);
    request("textDocument/definition");
    return definition_line();
}

// Meshes: Draw2D.Mesh, Draw2D.Clip and Draw2D.Screen, the built-in Vertex and
// Filter, and the draw list a view passes to C.
TIDE_TEST(lsp_meshes)
{
    start();
#define MESH_TYPES                                                                 \
    "scene Main { }\n"                                                             \
    "extern void DrawUI(DrawList list);\n"                                         \
    "local singleton Art\n{\n    Grid2<Color> pixels = Grid2(4, 4);\n    List<Vertex> corners;\n}\n"
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw2D.Screen();\n    Draw2D.Clip(Rect(0, 0, 4, 4));\n"
                             "    Draw2D.Mesh(art.corners, [0, 1, 2], art.pixels, Filter.Point);\n"
                             "    Draw2D.Mesh([Vertex { position = float2(1, 2) }], [0, 0, 0]);\n"
                             "    Draw3D.Camera(float3(0, 0, -5), quaternion.identity, 60);\n"
                             "    Draw3D.Mesh([Vertex3 { position = float3(1, 2, 3) }], [0, 0, 0], float4x4.identity);\n"
                             "    Draw2D.Clip();\n    DrawUI(Draw2D.list);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *draw = complete(MESH_TYPES "view V(Art art)\n{\n    Draw2D.$\n}\n");
    TIDE_CHECK(offers(draw, "Mesh") && offers(draw, "Clip") && offers(draw, "Screen") && offers(draw, "list"));
    TIDE_CHECK(has(draw, "Mesh(${1:vertices}, ${2:indices})"));
    TIDE_CHECK(has(draw, "Draw2D.list: DrawList"));
    const char *filter = complete(MESH_TYPES "view V(Art art)\n{\n    Draw2D.Mesh(art.corners, [0, 1, 2], art.pixels, Filter.$);\n}\n");
    TIDE_CHECK(offers(filter, "Point") && offers(filter, "Bilinear"));
    const char *corner = complete(MESH_TYPES "view V(Art art)\n{\n    var v = Vertex { $ };\n}\n");
    TIDE_CHECK(offers(corner, "position") && offers(corner, "uv") && offers(corner, "color"));
    const char *corner3 = complete(MESH_TYPES "view V(Art art)\n{\n    var v = Vertex3 { $ };\n}\n");
    TIDE_CHECK(offers(corner3, "position") && offers(corner3, "uv") && offers(corner3, "color"));
    const char *param = complete(MESH_TYPES "extern void Paint($);\n");
    TIDE_CHECK(offers(param, "DrawList") && offers(param, "Vertex"));
    const char *tide_param = complete(MESH_TYPES "void Paint($) { }\n"); // Only C takes one
    TIDE_CHECK(!offers(tide_param, "DrawList") && offers(tide_param, "Vertex"));

    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw2D.Me$sh(art.corners, [0, 1, 2], art.pixels);\n}\n");
    const char *mesh = request("textDocument/hover");
    TIDE_CHECK(has(mesh, "Draw2D.Mesh(List<Vertex> vertices, List<int> indices, Grid2<Color> texture, Filter filter)"));
    TIDE_CHECK(has(mesh, "Triangles: three of `indices` each"));
    // Each version's doc, 2D and 3D
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw3D.Cam$era(float3(0, 0, 0), quaternion.identity, 60);\n}\n");
    const char *camera = request("textDocument/hover");
    TIDE_CHECK(has(camera, "Draw3D.Camera(float3 position, quaternion rotation, float fieldOfView)"));
    TIDE_CHECK(has(camera, "Sets the 3D camera for the 3D meshes after it") && !has(camera, "orthographic size"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    DrawUI(Draw2D.li$st);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Draw2D.list: DrawList"));
    // Its two forms, a 2D mesh's first for a List<Vertex>, and each parameter by what's written before it
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw2D.Mesh(art.corners, $\n}\n");
    const char *indices = request("textDocument/signatureHelp");
    TIDE_CHECK(has(indices, "\"label\":\"Draw2D.Mesh(vertices, indices[, texture[, filter]][, vert][, frag])\""));
    TIDE_CHECK(!has(indices, "\"label\":\"Draw3D.Mesh(vertices, indices, transform[, texture[, filter]][, vert][, frag])\""));
    TIDE_CHECK(has(indices, "\"activeSignature\":0,\"activeParameter\":1"));
    TIDE_CHECK(has(indices, "{\"label\":[22,29],\"documentation\":{\"kind\":\"markdown\",\"value\":\"A `List<int>`"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw2D.Mesh(art.corners, [0, 1, 2], $\n}\n"); // A list's commas aren't the call's
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":2"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw2D.Mesh(art.corners, [0, 1, 2], art.pixels, $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":3"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw3D.Mesh([Vertex3 { position = float3(1, 2, 3) }], [0, 0, 0], $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":2"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw2D.Mesh($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":0"));

    TIDE_CHECK(has(c_definition(MESH_TYPES "view V(Art art)\n{\n    Draw2D.Me$sh(art.corners, [0, 1, 2]);\n}\n"),
                   "void tide_draw_mesh_lists("));
    TIDE_CHECK(has(c_definition(MESH_TYPES "view V(Art art)\n{\n    Draw2D.Cl$ip();\n}\n"), "void tide_draw_no_clip("));
    TIDE_CHECK(has(c_definition(MESH_TYPES "view V(Art art)\n{\n    Draw2D.Scr$een();\n}\n"), "void tide_draw_screen("));
#undef MESH_TYPES
}

static const char *hover_at(const int line, const int character)
{
    return request_at("file:///test.tide", "textDocument/hover", line, character, "");
}

TIDE_TEST(lsp_instanced_drawing)
{
    start();
#define INSTANCE_TYPES \
    "scene Main { }\nstruct Corner2 { float2 position; }\nstruct Corner3 { float3 position; }\n" \
    "struct Item2 { float2 offset; }\nstruct Item3 { Color tint; }\n" \
    "local singleton Art { List<Corner2> flat; List<Corner3> space; List<int> faces; List<Item2> items2; List<Item3> items3; }\n" \
    "vert Place2(mut Corner2 vertex, Item2 item) { vertex.position += item.offset; }\n" \
    "frag Shade3(Corner3 vertex, mut Color color, Item3 item) { color = item.tint * vertex.position.x; }\n"
    open_document(INSTANCE_TYPES "view Show(Art art) { Draw2D.Instanced(art.flat, art.faces, art.items2, Place2); "
                                 "Draw3D.Instanced(art.space, art.faces, art.items3, Shade3); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document(INSTANCE_TYPES "view Show(Art art) { Draw2D.Instanced(art.flat, art.faces, $\n}\n");
    const char *flat = request("textDocument/signatureHelp");
    TIDE_CHECK(has(flat, "Draw2D.Instanced(vertices, indices, instances["));
    TIDE_CHECK(has(flat, "\"activeParameter\":2") && has(flat, "A List<T> of instance structs"));
    open_document(INSTANCE_TYPES "view Show(Art art) { Draw3D.Instanced(art.space, art.faces, $\n}\n");
    const char *space = request("textDocument/signatureHelp");
    TIDE_CHECK(has(space, "Draw3D.Instanced(vertices, indices, instances["));
    TIDE_CHECK(has(space, "\"activeParameter\":2") && has(space, "A List<float4x4> of transforms"));
    TIDE_CHECK(!has(space, "indices, transform["));
    const char *shaders = complete(INSTANCE_TYPES "view Show(Art art) { Draw2D.Instanced(art.flat, art.faces, art.items2, $); }\n");
    TIDE_CHECK(offers(shaders, "Place2") && !offers(shaders, "Shade3"));
    open_document(INSTANCE_TYPES "view Show(Art art) { Draw3D.Instanced(art.space, art.faces, art.items3, Shade3); }\n");
    TIDE_CHECK(has(hover_at(7, 6), "Draw3D.Instanced"));
    TIDE_CHECK(has(hover_at(7, 52), "An element of the draw's instance list"));
#undef INSTANCE_TYPES
}

// Frags and verts: code the GPU runs for a mesh's pixels and vertices, which a
// view names last at Draw2D.Mesh. SHADER_TYPES is 31 lines; in SHADERS the vert
// is on line 31, the frag on line 35 and the view's Draw2D.Mesh on line 43.
#define SHADER_TYPES                                                                                                  \
    "scene Main { }\n"                                                                                                \
    "struct BoxVertex\n{\n    float3 position;\n    float3 normal;\n    float2 uv;\n}\n"                              \
    "component Box\n{\n    Color tint = Color.white;\n    float glow;\n}\n"                                           \
    "singleton Sun\n{\n    float3 direction = float3(0, -1, 0);\n    float strength = 1;\n}\n"                        \
    "local singleton Models\n{\n    List<BoxVertex> crate;\n    List<int> faces;\n    Grid2<Color> wood = Grid2(4, 4);\n}\n" \
    "event(Spawned) Setup(with Main)\n{\n    Spawn(Box);\n}\n"                                                        \
    "float Lit(float3 normal, float3 direction)\n{\n"                                                                 \
    "    return Math.Max(Math.Dot(Math.Normalize(normal), -direction), 0.1);\n}\n"

#define SHADER_CODE                                                                                                   \
    SHADER_TYPES                                                                                                      \
    "vert Wave(mut BoxVertex vertex, Time time, Instance instance)\n{\n"                                              \
    "    vertex.position.y += Math.Sin(time.tick * time.dt + instance.transform.c3.x) * 0.1;\n}\n"                    \
    "frag Shade(BoxVertex vertex, mut Color color, Box box, Sun sun, Models models, Pixel pixel)\n{\n"                \
    "    float light = Lit(vertex.normal, sun.direction);\n"                                                          \
    "    color = models.wood.Sample(vertex.uv) * box.tint * light;\n"                                                 \
    "    if (pixel.position.x < 10) pixel.Discard();\n}\n"

#define SHADERS                                                                                                       \
    SHADER_CODE                                                                                                       \
    "view DrawBox(with Box, Models models)\n{\n"                                                                      \
    "    Draw3D.Mesh(models.crate, models.faces, float4x4.Translate(float3(0, 0, 5)), Wave, Shade);\n}\n"

static const char *rename_at(const int line, const int character, const char *name)
{
    char extra[96];
    snprintf(extra, sizeof extra, "\"newName\":\"%s\"", name);
    return request_at("file:///test.tide", "textDocument/rename", line, character, extra);
}

// Completion: `frag` and `vert` where declarations start, what their
// parameters can be, what the GPU has in their code, and their names where a
// Draw2D.Mesh takes them.
TIDE_TEST(lsp_shaders_complete)
{
    start();
    const char *top = complete(SHADER_TYPES "\n$");
    TIDE_CHECK(offers(top, "frag") && offers(top, "vert"));
    TIDE_CHECK(has(top, "frag ${1:Name}(${2:Vertex} vertex, mut Color color$3)"));
    TIDE_CHECK(has(top, "vert ${1:Name}(mut ${2:Vertex} vertex$3)"));

    // The vertex first: a struct with a position, which a vert takes mut
    const char *vertex = complete(SHADER_TYPES "struct Stats { float health; }\nfrag Shade($)\n{\n}\n");
    TIDE_CHECK(offers(vertex, "BoxVertex") && offers(vertex, "Vertex3") && !offers(vertex, "Stats"));
    TIDE_CHECK(!offers(vertex, "mut") && !offers(vertex, "Box") && !offers(vertex, "Pixel"));
    const char *changed = complete(SHADER_TYPES "vert Wave($)\n{\n}\n");
    TIDE_CHECK(offers(changed, "mut") && offers(changed, "BoxVertex"));
    TIDE_CHECK(offers(complete(SHADER_TYPES "vert Wave(mut $)\n{\n}\n"), "BoxVertex"));
    // Then a frag's pixel, and what the engine supplies: components, singletons, Pixel and Instance
    const char *frag = complete(SHADER_TYPES "frag Shade(BoxVertex vertex, $)\n{\n}\n");
    TIDE_CHECK(offers(frag, "mut") && offers(frag, "Box") && offers(frag, "Sun") && offers(frag, "Models") && offers(frag, "Time"));
    TIDE_CHECK(offers(frag, "Pixel") && offers(frag, "Instance"));
    TIDE_CHECK(!offers(frag, "with") && !offers(frag, "without") && !offers(frag, "World") && !offers(frag, "Devices"));
    TIDE_CHECK(!offers(frag, "BoxVertex") && !offers(frag, "float3"));
    const char *written = complete(SHADER_TYPES "frag Shade(BoxVertex vertex, mut $)\n{\n}\n");
    TIDE_CHECK(offers(written, "Color") && !offers(written, "Box") && !offers(written, "Pixel"));
    const char *rest = complete(SHADER_TYPES "frag Shade(BoxVertex vertex, mut Color color, $)\n{\n}\n");
    TIDE_CHECK(!offers(rest, "mut") && offers(rest, "Pixel") && offers(rest, "Sun"));
    const char *vert = complete(SHADER_TYPES "vert Wave(mut BoxVertex vertex, $)\n{\n}\n");
    TIDE_CHECK(offers(vert, "Instance") && offers(vert, "Time") && !offers(vert, "Pixel") && !offers(vert, "mut"));
    TIDE_CHECK(offers(complete(SHADER_TYPES "frag Shade(BoxVertex vertex, mut Color color, Pixel $)\n{\n}\n"), "pixel"));
    // A game's own Pixel is the game's
    const char *own = complete(SHADER_TYPES "component Pixel { float x; }\nfrag Shade(BoxVertex vertex, $)\n{\n}\n");
    TIDE_CHECK(has(own, "{\"label\":\"Pixel\",\"kind\":22,\"detail\":\"component\"}") && !has(own, "where its pixel is"));

    // In its code: what it takes, its locals, the game's functions and Math; nothing of the world or the frame
#define IN_FRAG(code)                                                                                                 \
    SHADER_TYPES "extern float Noise(float x);\n"                                                                     \
    "frag Shade(BoxVertex vertex, mut Color color, Box box, Models models, Pixel pixel)\n{\n    " code "\n}\n"
    const char *code = complete(IN_FRAG("var glow = box.glow;\n    $"));
    TIDE_CHECK(offers(code, "vertex") && offers(code, "color") && offers(code, "box") && offers(code, "pixel") && offers(code, "glow"));
    TIDE_CHECK(offers(code, "Math") && offers(code, "Lit") && offers(code, "float3") && offers(code, "bool") && offers(code, "BoxVertex"));
    TIDE_CHECK(offers(code, "if") && offers(code, "for") && offers(code, "var"));
    TIDE_CHECK(!offers(code, "Draw2D") && !offers(code, "GUI") && !offers(code, "Spawn") && !offers(code, "Send"));
    TIDE_CHECK(!offers(code, "Devices") && !offers(code, "Scene") && !offers(code, "Debug") && !offers(code, "Cursor"));
    TIDE_CHECK(!offers(code, "while") && !offers(code, "foreach") && !offers(code, "switch") && !offers(code, "this"));
    TIDE_CHECK(!offers(code, "byte") && !offers(code, "long") && !offers(code, "Entity") && !offers(code, "string") && !offers(code, "List"));
    TIDE_CHECK(!offers(code, "Noise") && !offers(code, "Box")); // C runs on the CPU, and nothing spawns here
    const char *value = complete(IN_FRAG("color = $"));
    TIDE_CHECK(offers(value, "box") && offers(value, "Math") && offers(value, "Lit") && !offers(value, "Draw2D") && !offers(value, "null"));
    TIDE_CHECK(!offers(complete(IN_FRAG("mut $")), "Entity") && offers(complete(IN_FRAG("mut $")), "float3"));

    const char *pixel = complete(IN_FRAG("var x = pixel.$"));
    TIDE_CHECK(offers(pixel, "position") && offers(pixel, "depth") && offers(pixel, "world") && offers(pixel, "Discard"));
    TIDE_CHECK(has(pixel, "Where the pixel is on the screen"));
    const char *grid = complete(IN_FRAG("color = models.wood.$"));
    TIDE_CHECK(offers(grid, "Sample") && !offers(grid, "Clear") && !offers(grid, "size"));
    TIDE_CHECK(has(grid, "Grid2<Color>.Sample(float2 uv, Filter filter) -> Color"));
    const char *list = complete(IN_FRAG("var n = models.crate.$"));
    TIDE_CHECK(!offers(list, "Add") && !offers(list, "count")); // The GPU has no lists
    TIDE_CHECK(offers(complete(IN_FRAG("color = box.$")), "tint"));
    TIDE_CHECK(offers(complete(IN_FRAG("color = Color.$")), "white") && offers(complete(IN_FRAG("var s = Math.$")), "Sin"));
    TIDE_CHECK(!offers(complete(IN_FRAG("Draw2D.$")), "Circle") && !offers(complete(IN_FRAG("var k = Devices.$")), "keyboard"));
    TIDE_CHECK(offers(complete(IN_FRAG("color = models.wood.Sample(vertex.uv, Filter.$")), "Point"));
#undef IN_FRAG
    const char *instance = complete(SHADER_TYPES "vert Wave(mut BoxVertex vertex, Instance instance)\n{\n    var m = instance.$\n}\n");
    TIDE_CHECK(offers(instance, "transform") && !offers(instance, "Discard"));
    // Outside a frag or vert, a grid has its own methods, and no Sample
    const char *cells = complete(SHADER_TYPES "view V(mut Models models)\n{\n    models.wood.$\n}\n");
    TIDE_CHECK(offers(cells, "Clear") && offers(cells, "size") && !offers(cells, "Sample"));

    // After a mesh's lists, at Draw2D.Mesh: the verts and frags that run on its vertex, by name
#define IN_VIEW(code)                                                                                                 \
    SHADER_CODE "struct FlatVertex { float2 position; }\n"                                                            \
    "frag Flat(FlatVertex vertex, mut Color color)\n{\n    color = Color.red;\n}\n"                                   \
    "view DrawBox(with Box, Models models)\n{\n    " code "\n}\n"
    const char *named = complete(IN_VIEW("Draw3D.Mesh(models.crate, models.faces, float4x4.identity, $);"));
    TIDE_CHECK(offers(named, "Wave") && offers(named, "Shade") && !offers(named, "Flat") && offers(named, "models"));
    TIDE_CHECK(has(named, "{\"label\":\"Wave\",\"kind\":3,\"detail\":\"vert Wave(mut BoxVertex vertex, Time time, Instance instance)\""));
    // ...while the call is still being typed too, but not before its lists, nor in another call
    TIDE_CHECK(offers(complete(IN_VIEW("Draw3D.Mesh(models.crate, models.faces, float4x4.identity, Wave, $")), "Shade"));
    TIDE_CHECK(!offers(complete(IN_VIEW("Draw3D.Mesh(models.crate, $")), "Shade"));
    TIDE_CHECK(!offers(complete(IN_VIEW("Draw2D.Circle(float2(0, 0), 1, $")), "Shade"));
#undef IN_VIEW
    // ...nor in a function: the components they read are those of the entity a view runs for
    TIDE_CHECK(!offers(complete(SHADER_CODE "void Paint(Models models)\n{\n"
                                            "    Draw3D.Mesh(models.crate, models.faces, float4x4.identity, $\n}\n"),
                       "Shade"));
    // ...and by its namespace
    const char *qualified = complete("namespace Effects;\nscene Main { }\nstruct V { float2 position; }\n"
                                     "local singleton Art { List<V> corners; List<int> faces; }\n"
                                     "frag Tint(V vertex, mut Color color) { color = Color.red; }\n"
                                     "view Show(Art art) { Draw2D.Mesh(art.corners, art.faces, Effects.$); }\n");
    TIDE_CHECK(has(qualified, "{\"label\":\"Tint\",\"kind\":3,\"detail\":\"frag\"}"));
}

// Hovers: a frag or vert with what the engine sends for it, grouped as it's
// sent; its pixel; what it takes; and Pixel, Instance, Discard and Sample.
TIDE_TEST(lsp_shaders_hover)
{
    start();
    open_document(SHADERS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *frag = hover_at(35, 6);
    TIDE_CHECK(has(frag, "frag Shade(BoxVertex vertex, mut Color color, Box box, Sun sun, Models models, Pixel pixel)"));
    TIDE_CHECK(has(frag, "Runs on the GPU, for each pixel of a mesh a view draws through it"));
    TIDE_CHECK(has(frag, "- for each instance: `Box.tint`\\n- for each draw: `Sun.direction`\\n- as textures: `Models.wood`\\n"
                         "- of its vertex, blended across the triangle: `normal`, `uv`"));
    TIDE_CHECK(!has(frag, "glow") && !has(frag, "strength") && !has(frag, "Runs once per tick"));
    // ...and where a Draw2D.Mesh names it
    const char *vert = hover_at(43, 82);
    TIDE_CHECK(has(vert, "vert Wave(mut BoxVertex vertex, Time time, Instance instance)") && has(vert, "for each vertex"));
    TIDE_CHECK(has(vert, "- for each instance: `Instance.transform`\\n- for each draw: `Time.dt`, `Time.tick`"));
    TIDE_CHECK(has(hover_at(43, 88), "frag Shade(") && has(hover_at(31, 6), "vert Wave("));

    // A frag's pixel, where it's declared and where it's written
    const char *color = hover_at(35, 40);
    TIDE_CHECK(has(color, "mut Color color") && has(color, "The pixel's color, which the frag writes"));
    TIDE_CHECK(has(color, "starts as `Color.white`") && has(color, "isn't what's on the screen already"));
    TIDE_CHECK(has(color, "blended over what's behind it by its alpha"));
    TIDE_CHECK(has(hover_at(38, 5), "The pixel's color, which the frag writes"));
    // What the engine supplies: how often it's sent, and the fields this code reads
    const char *box = hover_at(35, 51);
    TIDE_CHECK(has(box, "sent to the GPU for each instance drawn") && has(box, "the only ones sent: `tint`.") && !has(box, "glow"));
    const char *sun = hover_at(35, 60);
    TIDE_CHECK(has(sun, "A singleton, sent to the GPU once for each draw") && has(sun, "the only ones sent: `direction`."));
    TIDE_CHECK(has(hover_at(35, 72), "Grids it samples, sent as textures: `wood`."));
    TIDE_CHECK(has(hover_at(31, 38), "the only ones sent: `dt`, `tick`."));
    // The vertex: a frag's blended, a vert's changed
    const char *blended = hover_at(35, 22);
    TIDE_CHECK(has(blended, "blended across the triangle from its three corners") && has(blended, "`normal`, `uv`"));
    TIDE_CHECK(has(hover_at(31, 25), "which it changes in place"));

    // Pixel and Instance, their fields, Discard and Sample
    const char *pixel = hover_at(35, 80);
    TIDE_CHECK(has(pixel, "record Pixel") && has(pixel, "float3 world;") && has(pixel, "void Discard();"));
    TIDE_CHECK(has(pixel, "where its pixel is") && !has(pixel, "Built into the engine"));
    TIDE_CHECK(has(hover_at(35, 86), "A frag's parameter: where its pixel is"));
    TIDE_CHECK(has(hover_at(31, 45), "record Instance") && has(hover_at(31, 54), "how the mesh was drawn"));
    TIDE_CHECK(has(hover_at(39, 15), "Field of record `Pixel`. Where the pixel is on the screen"));
    TIDE_CHECK(has(hover_at(33, 66), "The transform `Draw3D.Mesh` was given"));
    const char *discard = hover_at(39, 38);
    TIDE_CHECK(has(discard, "Pixel.Discard()") && has(discard, "Leaves the pixel undrawn"));
    const char *sample = hover_at(38, 25);
    TIDE_CHECK(has(sample, "Grid2<Color>.Sample(float2 uv, Filter filter) -> Color") && has(sample, "Reads the grid as a texture"));
    TIDE_CHECK(has(hover_at(43, 12), "A vert and a frag, named last, run on the GPU"));
    // They're the GPU's: no C to go to
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 35, 80, ""), "\"result\":null"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/typeDefinition", 35, 86, ""), "\"result\":null"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 39, 15, ""), "\"result\":null"));

    // A component a function takes whole is sent whole, and one never read not at all
    open_document(SHADER_TYPES "float Glow(Box box) { return box.glow; }\n"
                  "frag Shade(BoxVertex vertex, mut Color color, Box box, Sun sun, Pixel pixel)\n{\n"
                  "    color = Color.white * Glow(box) * pixel.depth * pixel.world.x;\n}\n");
    TIDE_CHECK(has(last_sent(), "'sun' is never used") && !has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(has(hover_at(32, 51), "the only ones sent: `tint`, `glow`."));
    TIDE_CHECK(has(hover_at(32, 60), "This code reads none of its fields, so nothing of it is sent."));
    TIDE_CHECK(has(hover_at(34, 45), "How deep the pixel is") && has(hover_at(34, 59), "Where the pixel is in the world."));

    // The local fields of the match's components and singletons it reads go the same way
#define LOCAL_FIELDS                                                                                                   \
    SHADER_TYPES "component Crate\n{\n    float worn;\n    local float fade = 1;\n}\n"                                \
    "singleton Fog\n{\n    float density;\n    local Color tone = Color.white;\n"                                      \
    "    local Grid2<Color> noise = Grid2(4, 4);\n}\n"
    open_document(LOCAL_FIELDS "frag Fade(BoxVertex vertex, mut Color color, Crate crate, Fog fog)\n{\n"
                               "    color = fog.noise.Sample(vertex.uv) * fog.tone * crate.fade;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *local = hover_at(42, 6);
    TIDE_CHECK(has(local, "- for each instance: `Crate.fade`\\n- for each draw: `Fog.tone`\\n- as textures: `Fog.noise`"));
    TIDE_CHECK(!has(local, "worn") && !has(local, "density"));
    TIDE_CHECK(has(hover_at(42, 52), "for each instance drawn") && has(hover_at(42, 52), "the only ones sent: `fade`."));
    const char *fog = hover_at(42, 63);
    TIDE_CHECK(has(fog, "the only ones sent: `tone`.") && has(fog, "Grids it samples, sent as textures: `noise`."));
    const char *fields = complete(LOCAL_FIELDS "frag Fade(BoxVertex vertex, mut Color color, Crate crate)\n{\n"
                                               "    color = Color.white * crate.$\n}\n");
    TIDE_CHECK(offers(fields, "worn") && offers(fields, "fade") && has(fields, "A frag or vert reads it like any other field"));
#undef LOCAL_FIELDS

    // While the game has an error, the checker plans no shaders, and the hovers say so
    open_document(SHADERS "system Broken(mut Box box)\n{\n    box.nope = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"severity\":1"));
    const char *unplanned = hover_at(35, 6);
    TIDE_CHECK(has(unplanned, "frag Shade(") && has(unplanned, "What it reads is worked out once the game has no errors."));
    TIDE_CHECK(!has(unplanned, "for each instance"));
    TIDE_CHECK(has(hover_at(35, 51), "Which of its fields this code reads is worked out once the game has no errors."));
}

// A frag's or vert's name at Draw2D.Mesh is its declaration's: definitions,
// usages, highlights, renames and the call hierarchy go both ways.
TIDE_TEST(lsp_shaders_names)
{
    start();
    open_document(SHADERS);
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 43, 88, ""),
                   "\"range\":{\"start\":{\"line\":35,\"character\":5},\"end\":{\"line\":35,\"character\":10}}"));
    const char *usages = request_at("file:///test.tide", "textDocument/references", 35, 6,
                                    "\"context\":{\"includeDeclaration\":false}");
    TIDE_CHECK(count(usages, "\"uri\"") == 1 && has(usages, "{\"line\":43,\"character\":87}"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 43, 82, "\"context\":{\"includeDeclaration\":true}"),
                     "\"uri\"") == 2);
    const char *highlights = request_at("file:///test.tide", "textDocument/documentHighlight", 43, 82, "");
    TIDE_CHECK(has(highlights, "{\"range\":{\"start\":{\"line\":31,\"character\":5},\"end\":{\"line\":31,\"character\":9}},\"kind\":3}"));
    TIDE_CHECK(has(highlights, "{\"range\":{\"start\":{\"line\":43,\"character\":81},\"end\":{\"line\":43,\"character\":85}},\"kind\":2}"));

    // Renamed from the Draw2D.Mesh that names it, and from its declaration
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 43, 88, ""), "\"result\":{\"start\""));
    rename_at(43, 88, "Tint");
    const char *renamed = apply_reply(SHADERS, "file:///test.tide");
    TIDE_CHECK(has(renamed, "frag Tint(BoxVertex vertex, mut Color color,") && has(renamed, ", Wave, Tint);"));
    TIDE_CHECK(!has(renamed, "Shade"));
    char copy[8192];
    snprintf(copy, sizeof copy, "%s", renamed);
    open_document(copy);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document(SHADERS);
    rename_at(31, 6, "Ripple");
    renamed = apply_reply(SHADERS, "file:///test.tide");
    TIDE_CHECK(has(renamed, "vert Ripple(mut BoxVertex vertex,") && has(renamed, ", Ripple, Shade);"));
    // It shares its name with nothing, nor a view with it, and its word isn't a name, but for a parameter
    TIDE_CHECK(has(rename_at(31, 6, "Shade"), "'Shade' is already declared"));
    TIDE_CHECK(has(rename_at(31, 6, "DrawBox"), "'DrawBox' is already declared"));
    TIDE_CHECK(has(rename_at(31, 6, "Box"), "'Box' is already declared"));
    TIDE_CHECK(has(rename_at(41, 6, "Shade"), "'Shade' is already declared"));
    TIDE_CHECK(has(rename_at(31, 6, "frag"), "keyword") && has(rename_at(31, 6, "vert"), "keyword"));
    TIDE_CHECK(has(rename_at(35, 22, "vert"), "\"changes\""));

    // A qualified name: its last part
    static const char effects[] = "namespace Effects;\nscene Main { }\nstruct V { float2 position; }\n"
                                  "local singleton Art { List<V> corners; List<int> faces; }\n"
                                  "frag Tint(V vertex, mut Color color) { color = Color.red; }\n"
                                  "view Show(Art art) { Draw2D.Mesh(art.corners, art.faces, Effects.Tint); }\n";
    open_document(effects);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 5, 66, ""),
                   "\"range\":{\"start\":{\"line\":4,\"character\":5}"));
    rename_at(4, 6, "Paint");
    renamed = apply_reply(effects, "file:///test.tide");
    TIDE_CHECK(has(renamed, "frag Paint(V vertex") && has(renamed, "art.faces, Effects.Paint);"));

    // The call hierarchy: a view calls the vert and the frag it names, and they the game's functions
    open_document(SHADERS);
    const char *item = request_at("file:///test.tide", "textDocument/prepareCallHierarchy", 43, 88, "");
    TIDE_CHECK(has(item, "{\"name\":\"Shade\",\"kind\":12,\"detail\":\"frag Shade(BoxVertex vertex, mut Color color, Box box, Sun sun, "
                         "Models models, Pixel pixel)\""));
    TIDE_CHECK(has(item, "\"selectionRange\":{\"start\":{\"line\":35,\"character\":5},\"end\":{\"line\":35,\"character\":10}}"));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"callHierarchy/incomingCalls\",\"params\":{\"item\":{\"name\":\"Shade\","
           "\"kind\":12,\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":35,\"character\":0},\"end\":"
           "{\"line\":40,\"character\":1}},\"selectionRange\":{\"start\":{\"line\":35,\"character\":5},\"end\":"
           "{\"line\":35,\"character\":10}}}}}");
    TIDE_CHECK(has(last_sent(), "{\"from\":{\"name\":\"DrawBox\",\"kind\":12,\"detail\":\"view DrawBox(with Box, Models models)\""));
    TIDE_CHECK(has(last_sent(), "\"fromRanges\":[{\"start\":{\"line\":43,\"character\":87},\"end\":{\"line\":43,\"character\":92}}]"));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"callHierarchy/outgoingCalls\",\"params\":{\"item\":{\"name\":\"DrawBox\","
           "\"kind\":12,\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":41,\"character\":0},\"end\":"
           "{\"line\":44,\"character\":1}},\"selectionRange\":{\"start\":{\"line\":41,\"character\":5},\"end\":"
           "{\"line\":41,\"character\":12}}}}}");
    TIDE_CHECK(has(last_sent(), "{\"to\":{\"name\":\"Wave\",\"kind\":12,\"detail\":\"vert Wave("));
    TIDE_CHECK(has(last_sent(), "{\"to\":{\"name\":\"Shade\"") && count(last_sent(), "\"to\":") == 2);
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"callHierarchy/outgoingCalls\",\"params\":{\"item\":{\"name\":\"Shade\","
           "\"kind\":12,\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":35,\"character\":0},\"end\":"
           "{\"line\":40,\"character\":1}},\"selectionRange\":{\"start\":{\"line\":35,\"character\":5},\"end\":"
           "{\"line\":35,\"character\":10}}}}}");
    TIDE_CHECK(has(last_sent(), "{\"to\":{\"name\":\"Lit\"") && count(last_sent(), "\"to\":") == 1);
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"callHierarchy/incomingCalls\",\"params\":{\"item\":{\"name\":\"Lit\","
           "\"kind\":12,\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":27,\"character\":0},\"end\":"
           "{\"line\":30,\"character\":1}},\"selectionRange\":{\"start\":{\"line\":27,\"character\":6},\"end\":"
           "{\"line\":27,\"character\":9}}}}}");
    TIDE_CHECK(has(last_sent(), "{\"from\":{\"name\":\"Shade\",\"kind\":12,\"detail\":\"frag Shade("));
}

// The outline, symbols across the game and semantic tokens: frags and verts
// are functions, their words keywords, and Pixel and Instance the engine's types.
TIDE_TEST(lsp_shaders_symbols_and_tokens)
{
    start();
    open_document(SHADERS);
    const char *outline = request("textDocument/documentSymbol");
    TIDE_CHECK(has(outline, "{\"name\":\"Wave\",\"detail\":\"vert\",\"kind\":12,\"range\":{\"start\":{\"line\":31,\"character\":0},"
                            "\"end\":{\"line\":34,\"character\":1}}"));
    TIDE_CHECK(has(outline, "{\"name\":\"Shade\",\"detail\":\"frag\",\"kind\":12,\"range\":{\"start\":{\"line\":35,\"character\":0},"
                            "\"end\":{\"line\":40,\"character\":1}}"));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"sha\"}}");
    TIDE_CHECK(has(last_sent(), "{\"name\":\"Shade\",\"kind\":12,\"location\":{\"uri\":\"file:///test.tide\",\"range\":{\"start\":"
                                "{\"line\":35,\"character\":5}"));

    const char *tokens = request("textDocument/semanticTokens/full");
    // `vert` a keyword (11), Wave a function (9) declared, its vertex a parameter it writes (5, not readonly),
    // Time a class of the engine's (3, 8) and Instance a type of the engine's (1, 8)
    TIDE_CHECK(has(tokens, ",2,0,4,11,0,0,5,4,9,1,0,9,9,2,0,0,10,6,5,1,0,8,4,3,8,0,5,4,5,3,0,6,8,1,8,0,9,8,5,3,"));
    // `frag`, Shade, its vertex, then its pixel: Color, and `color`, which it writes
    TIDE_CHECK(has(tokens, ",2,0,4,11,0,0,5,5,9,1,0,6,9,2,0,0,10,6,5,3,0,12,5,1,8,0,6,5,5,1,"));
    TIDE_CHECK(has(tokens, ",0,8,5,1,8,0,6,5,5,3,")); // Pixel pixel
    TIDE_CHECK(has(tokens, ",4,9,0,0,6,5,9,0]"));     // Wave and Shade at Draw2D.Mesh: functions

    // Anywhere else, the two words are names
    open_document("scene Main { }\nint Twice(int frag)\n{\n    var vert = frag * 2;\n    return vert;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(hover_at(1, 15), "int frag") && has(hover_at(3, 9), "int vert"));
}

// Signature help at Draw2D.Mesh goes by what each argument is, as what follows
// a mesh's lists can be left out; Sample has its own.
TIDE_TEST(lsp_shaders_signature_help)
{
    start();
#define IN_VIEW(code) SHADER_CODE "view DrawBox(with Box, Models models)\n{\n    " code "\n}\n"
    open_document(IN_VIEW("Draw3D.Mesh(models.crate, $"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":1")); // 3D: its position is a float3
    open_document(IN_VIEW("Draw3D.Mesh(models.crate, models.faces, $"));
    const char *transform = request("textDocument/signatureHelp");
    TIDE_CHECK(has(transform, "\"activeSignature\":0,\"activeParameter\":2"));
    TIDE_CHECK(has(transform, "{\"label\":[31,40],\"documentation\":{\"kind\":\"markdown\",\"value\":\"A `float4x4`: where the mesh is "
                              "in the world.\"}}"));
    open_document(IN_VIEW("Draw3D.Mesh(models.crate, models.faces, float4x4.identity, $"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":3")); // A texture, at the earliest
    open_document(IN_VIEW("Draw3D.Mesh(models.crate, models.faces, float4x4.identity, Wave, $"));
    const char *frag = request("textDocument/signatureHelp");
    TIDE_CHECK(has(frag, "\"activeSignature\":0,\"activeParameter\":6")); // After a vert, the frag
    TIDE_CHECK(has(frag, "{\"label\":[64,68],\"documentation\":{\"kind\":\"markdown\",\"value\":\"A vert, by name"));
    TIDE_CHECK(has(frag, "{\"label\":[72,76],\"documentation\":{\"kind\":\"markdown\",\"value\":\"A frag, by name"));
    TIDE_CHECK(has(frag, "A vert and a frag, named last, run on the GPU"));
    // In a call that's whole, each name is what it is
    open_document(IN_VIEW("Draw3D.Mesh(models.crate, models.faces, float4x4.identity, Wa$ve, Shade);"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeParameter\":5"));
    open_document(IN_VIEW("Draw3D.Mesh(models.crate, models.faces, float4x4.identity, Sha$de);"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeParameter\":6"));
#undef IN_VIEW

    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, mut Color color, Models models)\n{\n"
                               "    color = models.wood.Sample(vertex.uv, $\n}\n");
    const char *sample = request("textDocument/signatureHelp");
    TIDE_CHECK(has(sample, "\"label\":\"Grid2<Color>.Sample(float2 uv, Filter filter) -> Color\"") && has(sample, "\"activeParameter\":1"));
}

// Formatting, folding, selection, inlay hints and moving one to a file of its
// own treat a frag or vert as they do a view or a function.
TIDE_TEST(lsp_shaders_format_fold_and_move)
{
    start();
    static const char messy[] = SHADER_TYPES "frag Shade( BoxVertex vertex,mut Color color,Box box ){\ncolor=box.tint;\n"
                                             "if(vertex.uv.x>0.5){color=Color.red;}\n}\n"
                                             "vert Wave(mut BoxVertex vertex,Time time) {\nvertex.position.y+=time.dt;\n}\n";
    static const char expected[] = SHADER_TYPES "frag Shade(BoxVertex vertex, mut Color color, Box box)\n{\n"
                                                "    color = box.tint;\n"
                                                "    if (vertex.uv.x > 0.5) { color = Color.red; }\n}\n"
                                                "vert Wave(mut BoxVertex vertex, Time time)\n{\n"
                                                "    vertex.position.y += time.dt;\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));
    open_document(expected);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    open_document(SHADERS);
    const char *folds = request("textDocument/foldingRange");
    TIDE_CHECK(has(folds, "{\"startLine\":31,\"endLine\":33}") && has(folds, "{\"startLine\":35,\"endLine\":39}"));
    // From Sample out to its frag
    const char *ranges = request_at("file:///test.tide", "textDocument/selectionRange", 38, 26,
                                    "\"positions\":[{\"line\":38,\"character\":26}]");
    TIDE_CHECK(has(ranges, "\"result\":[{\"range\":{\"start\":{\"line\":38,\"character\":24},\"end\":{\"line\":38,\"character\":30}}"));
    TIDE_CHECK(has(ranges, "\"parent\":{\"range\":{\"start\":{\"line\":36,\"character\":0},\"end\":{\"line\":40,\"character\":1}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":35,\"character\":0},\"end\":{\"line\":40,\"character\":1}}}}"));
    // Nothing orders frags and verts by their files, so they move, as functions do
    const char *move = actions_at(35);
    TIDE_CHECK(has(move, "\"title\":\"Move 'Shade' to Shade.tide\",\"kind\":\"refactor.move\""));
    TIDE_CHECK(has(move, "\"newText\":\"frag Shade(BoxVertex vertex, mut Color color, Box box, Sun sun, Models models, "
                         "Pixel pixel)\\n{\\n"));
    TIDE_CHECK(has(move, "{\"range\":{\"start\":{\"line\":35,\"character\":0},\"end\":{\"line\":41,\"character\":0}},"
                         "\"newText\":\"\"}"));
    TIDE_CHECK(has(actions_at(32), "Move 'Wave' to Wave.tide") && !has(actions_at(42), "Move '"));

    // A local's type, as in any code; and a name at Draw2D.Mesh isn't taken for its texture
    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, mut Color color, Models models)\n{\n"
                               "    var texel = models.wood.Sample(vertex.uv);\n"
                               "    color = texel * Lit(vertex.normal, float3(0, -1, 0));\n}\n"
                               "view DrawBox(with Box, Models models)\n{\n"
                               "    Draw3D.Mesh(models.crate, models.faces, float4x4.identity, Shade);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":50,\"character\":0}}");
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":33,\"character\":13},\"label\":\": Color\",\"kind\":1"));
    TIDE_CHECK(!has(hints, "\"label\":\"texture:\"") && !has(hints, "\"label\":\"filter:\""));
}

// Whether the diagnostics just published have one over `length` characters
// from (line, character), an error (1) or a warning (2), that starts with `message`.
static bool diagnosed(const int line, const int character, const int length, const int severity, const char *message)
{
    char start[512];
    snprintf(start, sizeof start,
             "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}},\"severity\":%d,"
             "\"source\":\"tidec\",\"message\":\"%s",
             line, character, line, character + length, severity, message);
    return has(last_sent(), start);
}

// The checker's errors about frags and verts reach the editor where they are:
// on the name, the word or the call they're about, with a note about another
// place there, and what's never used faded.
TIDE_TEST(lsp_shaders_diagnostics)
{
    start_with("{\"capabilities\":{\"textDocument\":{\"publishDiagnostics\":{\"relatedInformation\":true}}}}");
    // A parameter never used: on its name, marked unnecessary
    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, mut Color color, Sun sun)\n{\n    color = Color.red;\n}\n");
    TIDE_CHECK(diagnosed(31, 50, 3, 2, "'sun' is never used") && has(last_sent(), "\"tags\":[1]"));
    // What only the CPU does: on the call
    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, mut Color color)\n{\n    Draw2D.Clear(Color.red);\n"
                               "    color = Color.red;\n}\n");
    TIDE_CHECK(diagnosed(33, 11, 5, 1, "a frag runs on the GPU, for each pixel, so it can't draw"));
    // In a function a frag calls: where it is, with the frag that reaches it as a note there
    open_document(SHADER_TYPES "float Count(float to)\n{\n    mut float x = 0;\n    while (x < to) x += 1;\n    return x;\n}\n"
                               "frag Shade(BoxVertex vertex, mut Color color)\n{\n"
                               "    color = Color.white * Count(vertex.uv.x);\n}\n");
    TIDE_CHECK(diagnosed(34, 4, 5, 1, "a loop the GPU runs counts"));
    TIDE_CHECK(has(last_sent(), "\"relatedInformation\":[{\"location\":{\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":37,"
                                "\"character\":5},\"end\":{\"line\":37,\"character\":10}}},\"message\":\"frag 'Shade' runs on the GPU, "
                                "and 'Count' with it, as it calls it\"}]"));
    TIDE_CHECK(!has(last_sent(), "\\nnote: frag 'Shade' runs on the GPU"));
    // A declaration that's wrong: on its name, and on the parameter
    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, mut Box box)\n{\n}\n");
    TIDE_CHECK(diagnosed(31, 5, 5, 1, "frag 'Shade' has no pixel to write"));
    TIDE_CHECK(diagnosed(31, 29, 3, 1, "a frag reads 'Box', and can't change it: drop 'mut'"));
    // At Draw2D.Mesh: on the name that's out of place, or whose frag the view can't supply
    open_document(SHADER_CODE "view DrawBox(with Box, Models models)\n{\n"
                              "    Draw3D.Mesh(models.crate, models.faces, float4x4.identity, Shade, Wave);\n}\n");
    TIDE_CHECK(diagnosed(43, 63, 5, 1, "the vert comes before the frag"));
    open_document(SHADER_CODE "view DrawBox(Models models)\n{\n"
                              "    Draw3D.Mesh(models.crate, models.faces, float4x4.identity, Wave, Shade);\n}\n");
    TIDE_CHECK(diagnosed(43, 69, 5, 1, "view 'DrawBox' doesn't guarantee its entity has a 'Box', which frag 'Shade' reads"));
    // What the GPU can't hold, found once the rest of the game is right: on the index
    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, mut Color color, Models models)\n{\n"
                               "    color = models.wood[0, 0];\n}\n");
    TIDE_CHECK(diagnosed(33, 23, 1, 1, "the GPU has no lists, maps or grids to index or make"));
}

// The type of what's at the cursor, and an event's or the input's code.
TIDE_TEST(lsp_type_definition_and_implementation)
{
    start();
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    bo$dy.radius = 1;\n}\n");
    TIDE_CHECK(has(request("textDocument/typeDefinition"), "\"range\":{\"start\":{\"line\":0,\"character\":10}"));
    open_document("struct Part { float size; }\ncomponent Kit { List<Part> parts; }\nscene Main { }\n"
                  "system S(Kit kit)\n{\n    var n = kit.pa$rts.count;\n}\n");
    TIDE_CHECK(has(request("textDocument/typeDefinition"), "\"range\":{\"start\":{\"line\":0,\"character\":7}")); // Part
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.rad$ius = 1;\n}\n");
    TIDE_CHECK(has(request("textDocument/typeDefinition"), "\"result\":null")); // A float

    open_document(GAME_TYPES "event Hit\n{\n    int damage = 1;\n}\n\nsystem Strike(with Body)\n{\n"
                  "    this.Send(Hit { damage = 2 });\n}\n\nevent(Hit hit) TakeHit(mut Body body)\n{\n"
                  "    body.radius -= hit.damage;\n}\n\nevent(Spawned) Grow(mut Body body)\n{\n    body.radius += 1;\n}\n");
    const char *handlers = request_at("file:///test.tide", "textDocument/implementation", 22, 7, ""); // event Hit
    TIDE_CHECK(has(handlers, "\"range\":{\"start\":{\"line\":32,\"character\":15}")); // TakeHit
    const char *spawned = request_at("file:///test.tide", "textDocument/implementation", 37, 8, ""); // event(Spawned)
    TIDE_CHECK(has(spawned, "{\"line\":18,\"character\":15}") && has(spawned, "{\"line\":37,\"character\":15}")); // Setup, Grow
    // While the game has an error elsewhere too, as it does while typing.
    open_document(GAME_TYPES "event Hit\n{\n    int damage = 1;\n}\n\nsystem Strike(with Body)\n{\n"
                  "    this.Send(Hit { damage = 2 });\n}\n\nevent(Hit hit) TakeHit(mut Body body)\n{\n"
                  "    body.radius -= hit.damge;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"severity\":1"));
    const char *broken = request_at("file:///test.tide", "textDocument/implementation", 22, 7, "");
    TIDE_CHECK(has(broken, "\"range\":{\"start\":{\"line\":32,\"character\":15}"));
    open_document("input Ke$ys\n{\n    bool fire;\n    Sample() { }\n    Sanitize() { }\n}\nscene Main { }\n");
    const char *input = request("textDocument/implementation");
    TIDE_CHECK(has(input, "{\"start\":{\"line\":3,\"character\":4},\"end\":{\"line\":3,\"character\":10}}"));
    TIDE_CHECK(has(input, "{\"start\":{\"line\":4,\"character\":4},\"end\":{\"line\":4,\"character\":12}}"));
}

#define CALLS                                                                                                     \
    "struct Stats\n{\n    float health = 100;\n    mut void Hurt(float amount) { health -= amount; }\n}\n"    \
    "component Unit { Stats stats; }\n"                                                                           \
    "float Heal(mut Stats stats, float amount)\n{\n    stats.Hurt(-amount);\n    return stats.health;\n}\n"        \
    "scene Main { }\n"                                                                                            \
    "system Fight(mut Unit unit)\n{\n    Heal(unit.stats, 1);\n    Heal(unit.stats, 2);\n    unit.stats.Hurt(1);\n}\n"

// Who calls a function, and what a system calls.
TIDE_TEST(lsp_call_hierarchy)
{
    start();
    open_document(CALLS);
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 0, 0, ""), "\"result\":null")); // Analysed
    const char *item = request_at("file:///test.tide", "textDocument/prepareCallHierarchy", 15, 5, ""); // A call of Heal
    TIDE_CHECK(has(item, "{\"name\":\"Heal\",\"kind\":12,\"detail\":\"float Heal(mut Stats stats, float amount)\""));
    TIDE_CHECK(has(item, "\"selectionRange\":{\"start\":{\"line\":6,\"character\":6},\"end\":{\"line\":6,\"character\":10}}"));

    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"callHierarchy/incomingCalls\",\"params\":{\"item\":{\"name\":\"Heal\","
           "\"kind\":12,\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":6,\"character\":0},\"end\":"
           "{\"line\":10,\"character\":1}},\"selectionRange\":{\"start\":{\"line\":6,\"character\":6},\"end\":"
           "{\"line\":6,\"character\":10}}}}}");
    TIDE_CHECK(has(last_sent(), "{\"from\":{\"name\":\"Fight\",\"kind\":12"));
    TIDE_CHECK(count(last_sent(), "\"from\":") == 1 && count(last_sent(), "\"character\":4},\"end\":{\"line\":") == 2);

    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"callHierarchy/outgoingCalls\",\"params\":{\"item\":{\"name\":\"Fight\","
           "\"kind\":12,\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":12,\"character\":0},\"end\":"
           "{\"line\":17,\"character\":1}},\"selectionRange\":{\"start\":{\"line\":12,\"character\":7},\"end\":"
           "{\"line\":12,\"character\":12}}}}}");
    TIDE_CHECK(has(last_sent(), "{\"to\":{\"name\":\"Heal\"") && has(last_sent(), "{\"to\":{\"name\":\"Hurt\",\"kind\":6"));
    TIDE_CHECK(count(last_sent(), "\"to\":") == 2);

    // The input's Sample calls too
    open_document("float Twice(float x) { return x * 2; }\ninput Keys\n{\n    float aim;\n    Sample() { aim = Twice(1); }\n}\n"
                  "scene Main { }\n");
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareCallHierarchy", 4, 5, ""), "{\"name\":\"Sample\",\"kind\":6"));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"callHierarchy/incomingCalls\",\"params\":{\"item\":{\"name\":\"Twice\","
           "\"kind\":12,\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":"
           "{\"line\":0,\"character\":38}},\"selectionRange\":{\"start\":{\"line\":0,\"character\":6},\"end\":"
           "{\"line\":0,\"character\":11}}}}}");
    TIDE_CHECK(has(last_sent(), "{\"from\":{\"name\":\"Sample\",\"kind\":6,\"detail\":\"input Keys\""));
}

// Built-ins lead to their C definitions in the engine headers.
TIDE_TEST(lsp_definition_in_c)
{
    start();
#define IN_SYSTEM(code) GAME_TYPES "system S(mut Body body, Time time)\n{\n    " code "\n}\n"
#define IN_VIEW(code) GAME_TYPES "view V(Body body)\n{\n    " code "\n}\n"
    TIDE_CHECK(has(c_definition(IN_VIEW("Draw2D.Cir$cle(body.position, 1, Color.red);")), "void tide_draw_circle("));
    TIDE_CHECK(has(c_definition(IN_VIEW("Draw2D.Circle(body.position, 1, Color.r$ed);")), "#define TIDE_COLOR_RED"));
    TIDE_CHECK(has(c_definition(IN_VIEW("Dr$aw2D.Clear(Color.red);")), "#pragma once"));
    TIDE_CHECK(has(c_definition(IN_SYSTEM("body.radius = Math.S$in(body.radius);")), "tide_sin_f("));
    TIDE_CHECK(has(c_definition(IN_SYSTEM("body.position = Math.S$in(body.position);")), "TIDE_MAP1(float, f, sin, f)"));
    TIDE_CHECK(has(c_definition(IN_SYSTEM("body.position = Math.Nor$malize(body.position);")), "tide_normalize_f##N("));
    TIDE_CHECK(has(c_definition(IN_SYSTEM("body.radius = Math.D$ot(float3(1), float3(2));")), "tide_dot_f3("));
    TIDE_CHECK(has(c_definition(IN_SYSTEM("body.radius = Math.P$I;")), "#define TIDE_PI_F"));
    TIDE_CHECK(has(c_definition(IN_SYSTEM("var q = quaternion.ident$ity;")), "tide_identity_q(void)"));
    TIDE_CHECK(has(c_definition(IN_SYSTEM("flo$at3 v = float3(1);")), "typedef struct tide_float3"));
    TIDE_CHECK(has(c_definition(IN_SYSTEM("body.radius = body.position.x$;")), "typedef struct tide_float2 { float x"));
    TIDE_CHECK(has(c_definition(IN_SYSTEM("var c = Color.red.g$;")), "float r, g, b, a;"));
    TIDE_CHECK(strcmp(c_definition(GAME_TYPES "system S(Ti$me time) { }\n"), "") == 0); // Generated: no C definition

    static const char constructor[] = "input PlayerInput\n{\n    bool fire;\n\n    Sample()\n    {\n"
                                      "        var keys = Devices.key$board;\n        fire = keys.space.pressed;\n    }\n}\n"
                                      "scene Main { }\n";
    TIDE_CHECK(has(c_definition(constructor), "tide_keyboard keyboard;"));
    static const char key[] = "input PlayerInput\n{\n    bool fire;\n\n    Sample()\n    {\n"
                              "        var keys = Devices.keyboard;\n        fire = keys.spa$ce.pressed;\n    }\n}\n"
                              "scene Main { }\n";
    TIDE_CHECK(has(c_definition(key), "X(space)"));
    static const char button[] = "input PlayerInput\n{\n    bool fire;\n\n    Sample()\n    {\n"
                                 "        fire = Devices.mouse.left.pre$ssed;\n    }\n}\nscene Main { }\n";
    TIDE_CHECK(has(c_definition(button), "bool pressed;"));
    static const char devices[] = "input PlayerInput\n{\n    bool fire;\n\n    Sample()\n    {\n"
                                  "        fire = Devi$ces.mouse.left.pressed;\n    }\n}\nscene Main { }\n";
    TIDE_CHECK(has(c_definition(devices), "typedef struct tide_devices"));
#undef IN_SYSTEM
#undef IN_VIEW
}

TIDE_TEST(lsp_symbols)
{
    start();
    open_document(GAME_TYPES "view DrawBodies(Body body) { }\n");
    const char *reply = request("textDocument/documentSymbol");
    TIDE_CHECK(has(reply, "\"name\":\"Body\""));
    TIDE_CHECK(has(reply, "\"name\":\"radius\""));
    TIDE_CHECK(has(reply, "\"name\":\"DrawBodies\",\"detail\":\"view\""));
}

TIDE_TEST(lsp_semantic_tokens)
{
    start();
    open_document(GAME_TYPES);
    const char *reply = request("textDocument/semanticTokens/full");
    // `Body` on line 0, column 10: a struct (2), declared (1).
    TIDE_CHECK(has(reply, "\"data\":[0,10,4,2,1,"));

    // Columns count UTF-16 units: `é` is two bytes but one unit.
    open_document("/* \xC3\xA9 */ component Body { float x; }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), "\"data\":[0,18,4,2,1,"));

    // Built-in value types are keywords (11), like C#'s float: `float3` 7 columns after `Body`.
    open_document("component Body { float3 p; }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), "\"data\":[0,10,4,2,1,0,7,6,11,0,"));
    // Sample and Sanitize are keywords too, and attributes decorators (12).
    open_document("input PlayerInput\n{\n    [Clamp(-1, 1)] float move;\n    Sample() { }\n}\nscene Main { }\n");
    const char *input = request("textDocument/semanticTokens/full");
    TIDE_CHECK(has(input, "2,5,5,12,0,")); // Clamp: line +2, column 5
    TIDE_CHECK(has(input, "1,4,6,11,0,")); // Sample: line +1, column 4
}

#define EVENTS                                                                 \
    GAME_TYPES                                                                 \
    "event Hit\n"                                                              \
    "{\n"                                                                      \
    "    int damage = 1;\n"                                                    \
    "}\n"                                                                      \
    "\n"                                                                       \
    "system Strike(with Body)\n"                                              \
    "{\n"                                                                      \
    "    this.Send(Hit { damage = 2 });\n"                                     \
    "}\n"                                                                      \
    "\n"                                                                       \
    "event(Hit hit) TakeHit(mut Body body)\n"                                  \
    "{\n"                                                                      \
    "    body.radius -= hit.damage;\n"                                         \
    "}\n"                                                                      \
    "\n"                                                                       \
    "event(Spawned) Grow(mut Body body)\n"                                     \
    "{\n"                                                                      \
    "    body.radius += 1;\n"                                                  \
    "}\n"

TIDE_TEST(lsp_events)
{
    start();
    open_document(EVENTS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    // The handler: its trigger, and when it runs.
    open_document(GAME_TYPES "event Hit { int damage; }\nsystem S(with Body) { this.Send(Hit); }\n"
                  "event(Hit hit) Take$Hit(mut Body body) { body.radius -= hit.damage; }\n");
    const char *handler = request("textDocument/hover");
    TIDE_CHECK(has(handler, "event(Hit hit) TakeHit(mut Body body)"));
    TIDE_CHECK(has(handler, "Runs when a `Hit` is sent to an entity that matches its parameters"));

    open_document(GAME_TYPES "event(Spawn$ed) Grow(mut Body body) { body.radius += 1; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Sent to each entity as it's spawned"));

    open_document(GAME_TYPES "event Hit { int damage; }\nsystem S(with Body) { this.Send(Hit); }\n"
                  "event(Hit hit) TakeHit(mut Body body) { body.radius -= h$it.damage; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "The event being handled"));

    // Completion: declarations, the trigger, a handler's parameters, an event's fields, and Send.
    const char *top = complete(GAME_TYPES "\n$");
    TIDE_CHECK(offers(top, "event"));
    TIDE_CHECK(offers(top, "event handler"));
    const char *trigger = complete(GAME_TYPES "event Hit { int damage; }\nevent($)\n");
    TIDE_CHECK(offers(trigger, "Hit"));
    TIDE_CHECK(offers(trigger, "Spawned"));
    TIDE_CHECK(!offers(trigger, "Devices"));
    const char *params = complete(GAME_TYPES "event Hit { int damage; }\nevent(Hit hit) TakeHit($)\n{\n}\n");
    TIDE_CHECK(offers(params, "mut"));
    TIDE_CHECK(offers(params, "Body"));
    const char *fields = complete(GAME_TYPES "event Hit { int damage; }\nevent(Hit hit) TakeHit(mut Body body)\n{\n"
                                  "    body.radius -= hit.$\n}\n");
    TIDE_CHECK(offers(fields, "damage"));
    const char *send = complete(GAME_TYPES "event Hit { int damage; }\nsystem S(with Body)\n{\n    this.$\n}\n");
    TIDE_CHECK(offers(send, "Send"));
    const char *literal = complete(GAME_TYPES "event Hit { int damage; }\nsystem S(with Body)\n{\n    this.Send(Hit { $ });\n}\n");
    TIDE_CHECK(offers(literal, "damage"));

    // The outline: events are events (24), and handlers say what they are.
    open_document(EVENTS);
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"Hit\",\"detail\":\"event\",\"kind\":24"));
    TIDE_CHECK(has(symbols, "\"name\":\"TakeHit\",\"detail\":\"event handler\""));
}

TIDE_TEST(lsp_local_state)
{
    start();
    static const char game[] = GAME_TYPES
        "local component Spark { int framesLeft = 2; }\n"
        "local singleton Menu { bool open; }\n"
        "view Trail(with Body, mut Menu menu) { Spawn(Spark); menu.open = true; }\n"
        "view Fade(mut Spark spark) { spark.framesLeft -= 1; if (spark.framesLeft < 0) this.Destroy(); }\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    open_document(GAME_TYPES "local component Sp$ark { int framesLeft = 2; }\nview Fade(mut Spark spark) { spark.framesLeft -= 1; }\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "local component Spark"));
    TIDE_CHECK(has(hover, "Local: this machine's own"));

    // In a view, Spawn makes local entities: the local components are offered, not the match's.
    const char *spawn = complete(GAME_TYPES "local component Spark { int framesLeft = 2; }\nview Trail(Body body)\n{\n    $\n}\n");
    TIDE_CHECK(offers(spawn, "Spawn"));
    TIDE_CHECK(offers(spawn, "Spark"));
    TIDE_CHECK(!offers(spawn, "Body"));
    const char *entity = complete(GAME_TYPES "local component Spark { int framesLeft = 2; }\n"
                                  "view Fade(Spark spark)\n{\n    this.$\n}\n");
    TIDE_CHECK(offers(entity, "Destroy"));
    TIDE_CHECK(offers(complete(GAME_TYPES "\n$"), "local component"));

    open_document(game);
    TIDE_CHECK(has(request("textDocument/documentSymbol"), "\"name\":\"Spark\",\"detail\":\"local component\""));
}

// `local` before a field of a match component or singleton: this machine's,
// outside the match. Views have it, the match doesn't.
#define LOCAL_FIELDS                                                           \
    "component Box\n"                                                          \
    "{\n"                                                                      \
    "    float3 pos;\n"                                                        \
    "    local List<int> verts;\n"                                             \
    "    local int bakes = 0;\n"                                               \
    "}\n"                                                                      \
    "singleton Arena\n"                                                        \
    "{\n"                                                                      \
    "    float size;\n"                                                        \
    "    local int painted;\n"                                                 \
    "}\n"                                                                      \
    "scene Main { }\n"                                                         \
    "event(Spawned) Setup(with Main) { Spawn(Box); }\n"

// The first code action of the last reply, applied to `original`.
static const char *apply_first_action(const char *original)
{
    static char text[8192];
    snprintf(text, sizeof text, "%s", original);
    const json *reply = json_parse(last_sent(), strlen(last_sent()));
    const json *result = json_get(reply, "result");
    const json *edits = result && result->kind == JSON_ARRAY && result->count > 0
                            ? json_path(result->items[0], "edit", "changes", "file:///test.tide", NULL)
                            : NULL;
    if (edits) apply_edits(text, sizeof text, edits);
    json_release();
    return text;
}

TIDE_TEST(lsp_local_fields)
{
    start();
    static const char game[] = LOCAL_FIELDS
        "void Bake(mut Box box) { box.verts.Add(1); box.bakes += 1; }\n"
        "view Show(mut Box box, mut Arena arena) { if (box.verts.count == 0) Bake(box); arena.painted += 1; }\n"
        "local event(Forgotten) Free(Box box) { Debug.Log($$\"{box.bakes}\"); }\n"
        "system Slide(mut Box box) { box.pos.x += 1; }\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    if (!has(last_sent(), "\"diagnostics\":[]")) printf("%s\n", last_sent());

    // Hovers say they're local: the field where it's declared and where it's used, and its type
    open_document(LOCAL_FIELDS "view Show(mut Box box) { box.ba$kes += 1; }\n");
    const char *use = request("textDocument/hover");
    TIDE_CHECK(has(use, "local int bakes = 0"));
    TIDE_CHECK(has(use, "Local field of component `Box`: this machine's, outside the match"));
    open_document("component Box\n{\n    float3 pos;\n    local List<int> ve$rts;\n}\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "local List<int> verts"));
    open_document(LOCAL_FIELDS "view Show(mut B$ox box) { box.bakes += 1; }\n");
    const char *type = request("textDocument/hover");
    TIDE_CHECK(has(type, "    float3 pos;") && has(type, "    local List<int> verts;") && has(type, "    local int bakes = 0;"));
    open_document(LOCAL_FIELDS "local event(Forgo$tten) Free(Box box) { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "local fields go"));

    // Completion: in views, local handlers and functions; never in the match
    const char *view = complete(LOCAL_FIELDS "view Show(mut Box box)\n{\n    box.$\n}\n");
    TIDE_CHECK(offers(view, "pos") && offers(view, "verts") && offers(view, "bakes"));
    TIDE_CHECK(has(view, "\"detail\":\"local List<int>\""));
    const char *singleton = complete(LOCAL_FIELDS "view Paint(mut Arena arena)\n{\n    arena.$\n}\n");
    TIDE_CHECK(offers(singleton, "size") && offers(singleton, "painted"));
    const char *function = complete(LOCAL_FIELDS "void Bake(mut Box box)\n{\n    box.$\n}\n");
    TIDE_CHECK(offers(function, "verts"));
    const char *handler = complete(LOCAL_FIELDS "local event(Forgotten) Free(Box box)\n{\n    box.$\n}\n");
    TIDE_CHECK(offers(handler, "bakes"));
    const char *system = complete(LOCAL_FIELDS "system Slide(mut Box box)\n{\n    box.$\n}\n");
    TIDE_CHECK(offers(system, "pos") && !offers(system, "verts") && !offers(system, "bakes"));
    const char *chain = complete(LOCAL_FIELDS "view Show(Box box)\n{\n    var n = box.verts.$\n}\n");
    TIDE_CHECK(offers(chain, "count"));
    // A value hasn't got them
    const char *literal = complete(LOCAL_FIELDS "system Reset(mut Box box)\n{\n    box = Box { $ };\n}\n");
    TIDE_CHECK(offers(literal, "pos") && !offers(literal, "verts"));
    // The event that says they're going, where a handler's trigger goes
    TIDE_CHECK(offers(complete(LOCAL_FIELDS "local event($)\n"), "Forgotten"));

    // Go to definition, and every use, through the component's parameter
    open_document(LOCAL_FIELDS "void Bake(mut Box box) { box.bakes += 1; }\nview Show(mut Box box) { box.ba$kes += 1; Bake(box); }\n");
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":4,\"character\":14}"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 3);
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"pos\""), "already"));

    // The outline, and highlighting: `local` is a modifier (16) of the property (7)
    open_document(game);
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"verts\",\"detail\":\"local List<int>\""));
    TIDE_CHECK(has(symbols, "\"name\":\"pos\",\"detail\":\"float3\""));
    open_document("component Box\n{\n    float3 pos;\n    local int bakes;\n}\nscene Main { }\n");
    const char *tokens = request("textDocument/semanticTokens/full");
    TIDE_CHECK(has(tokens, "0,7,3,7,1,"));  // pos, after its type: declared (1)
    TIDE_CHECK(has(tokens, "0,4,5,7,17,")); // bakes, after its type: declared and local (16)
    open_document(LOCAL_FIELDS "view Show(mut Box box) { box.bakes += 1; box.pos.x; }\n");
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), ",5,7,16,")); // Where it's used: local

    // The errors, as diagnostics
    open_document(LOCAL_FIELDS "system Slide(mut Box box) { box.bakes += 1; }\n");
    TIDE_CHECK(has(last_sent(), "it's this machine's, and the match can't see it"));
    open_document(LOCAL_FIELDS "view Show(mut Box box) { box.bakes += 1; box.pos.x = 0; }\n");
    TIDE_CHECK(has(last_sent(), "a view can't change the match, and 'pos' is a match field of 'Box'"));

    // A view that writes one without mut: declare the parameter mut
    static const char read_only[] = LOCAL_FIELDS "view Show(Box box) { box.bakes += 1; }\n";
    open_document(read_only);
    TIDE_CHECK(has(last_sent(), "'box' is read-only"));
    TIDE_CHECK(has(actions_at(13), "Declare 'box' as mut"));
    TIDE_CHECK(has(apply_first_action(read_only), "view Show(mut Box box) { box.bakes += 1; }"));
    // ...and one that takes it mut and writes none: remove it
    static const char unused[] = LOCAL_FIELDS "view Show(mut Box box) { if (box.bakes > 0) Draw2D.Clear(Color.black); }\n";
    open_document(unused);
    TIDE_CHECK(has(last_sent(), "'box' is declared mut but never written"));
    TIDE_CHECK(has(actions_at(13), "Remove 'mut' from 'box'"));
    TIDE_CHECK(has(apply_first_action(unused), "view Show(Box box) { if"));
    // `local` on a field of a declaration that's local already: remove the word
    static const char twice[] = "local component Spark\n{\n    float2 position;\n    local int frames;\n}\nscene Main { }\n";
    open_document(twice);
    TIDE_CHECK(has(last_sent(), "'Spark' is local already, and so is every field of it"));
    TIDE_CHECK(has(actions_at(3), "Remove 'local'"));
    TIDE_CHECK(has(apply_first_action(twice), "{\n    float2 position;\n    int frames;\n}"));

    // Formatting keeps `local` with its field
    static const char messy[] = "component Box\n{\nfloat3 pos;\n  local   List<int>  verts ;\nlocal int bakes=0;\n}\nscene Main { }\n";
    static const char tidy[] = "component Box\n{\n    float3 pos;\n    local List<int> verts;\n    local int bakes = 0;\n}\nscene Main { }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, tidy) == 0);
    if (strcmp(formatted, tidy) != 0) printf("--- got:\n%s---\n", formatted);
}


TIDE_TEST(lsp_enums_and_switch)
{
    start();
    static const char game[] = "enum Phase { Warmup, Playing = 5 }\nsingleton Match { Phase phase; int n; }\nscene Main { }\n"
                               "system S(mut Match match)\n{\n    switch (match.phase)\n    {\n        case Phase.Warmup:\n"
                               "            match.n = 1;\n            break;\n        default:\n            break;\n    }\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    open_document("enum Pha$se { Warmup, Playing = 5 }\nscene Main { }\n");
    const char *type = request("textDocument/hover");
    TIDE_CHECK(has(type, "enum Phase"));
    TIDE_CHECK(has(type, "Playing = 5,"));

    open_document("enum Phase { Warmup, Playing = 5 }\nsingleton Match { Phase phase; }\nscene Main { }\n"
                  "system S(mut Match match) { match.phase = Phase.Play$ing; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Phase.Playing = 5"));

    const char *members = complete("enum Phase { Warmup, Playing = 5 }\nsingleton Match { Phase phase; }\nscene Main { }\n"
                                   "system S(mut Match match)\n{\n    match.phase = Phase.$\n}\n");
    TIDE_CHECK(offers(members, "Warmup"));
    TIDE_CHECK(offers(members, "Playing"));

    open_document(game);
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"Phase\",\"detail\":\"enum\",\"kind\":10"));
    TIDE_CHECK(has(symbols, "\"name\":\"Playing\",\"detail\":\"5\",\"kind\":22"));
}

TIDE_TEST(lsp_scheduling_phases)
{
    start();
    static const char declarations[] = "enum Phases { Setup = 30, Objects = 2, UI = 10 }\n"
                                       "const int OFFSET = 4;\nconst float SCALE = 0.5;\n"
                                       "scene Main { }\nview Existing() { }\n";
    char text[2048];

    snprintf(text, sizeof text, "%s[$]\nview V() { }\n", declarations);
    TIDE_CHECK(offers(complete(text), "Stage"));
    snprintf(text, sizeof text, "%s[Stage($)]\nview V() { }\n", declarations);
    const char *completion = complete(text);
    TIDE_CHECK(offers(completion, "Phases"));
    TIDE_CHECK(!offers(completion, "Existing"));
    snprintf(text, sizeof text, "%s[After($)]\nview V() { }\n", declarations);
    completion = complete(text);
    TIDE_CHECK(offers(completion, "Phases") && offers(completion, "Existing"));
    snprintf(text, sizeof text, "%s[Stage(Phases.$)]\nview V() { }\n", declarations);
    completion = complete(text);
    TIDE_CHECK(offers(completion, "Setup") && offers(completion, "Objects") && offers(completion, "UI"));
    snprintf(text, sizeof text, "%s[Stage(Phases.Objects, -$OFFSET)]\nview V() { }\n", declarations);
    completion = complete(text);
    TIDE_CHECK(offers(completion, "OFFSET"));
    TIDE_CHECK(!offers(completion, "Existing") && !offers(completion, "Phases") && !offers(completion, "SCALE"));
    snprintf(text, sizeof text, "%s[Stage(Phases.Objects, ($OFFSET + 1))]\nview V() { }\n", declarations);
    TIDE_CHECK(offers(complete(text), "OFFSET"));

    static const char game[] = "enum Phases { Setup = 30, Objects = 2, UI = 10 }\n"
                               "const int OFFSET = 4;\nscene Main { }\n"
                               "[Stage(Phases.Obj$ects, -OFFSET)]\nview DrawScene() { }\n"
                               "[After(Phases.Objects)]\nview Present() { }\n"
                               "[Stage(Phases.UI)]\nview Overlay() { }\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "Phases.Objects = 2"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":0,\"character\":26}"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 3);
    request_with("textDocument/rename", "\"newName\":\"Geometry\"");
    const char *renamed = apply_reply(game, "file:///test.tide");
    TIDE_CHECK(has(renamed, "Geometry = 2") && has(renamed, "Stage(Phases.Geometry, -OFFSET)")
               && has(renamed, "After(Phases.Geometry)"));

    snprintf(text, sizeof text, "%s[Stage(Pha$ses.Objects, -OFFSET)]\nview V() { }\n", declarations);
    open_document(text);
    TIDE_CHECK(has(request("textDocument/hover"), "enum Phases"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":0,\"character\":5}"));
    request_with("textDocument/rename", "\"newName\":\"FrameStages\"");
    renamed = apply_reply(text, "file:///test.tide");
    TIDE_CHECK(has(renamed, "enum FrameStages") && has(renamed, "Stage(FrameStages.Objects, -OFFSET)"));

    snprintf(text, sizeof text, "%s[Stage(Phases.Objects, -OFF$SET)]\nview V() { }\n", declarations);
    open_document(text);
    TIDE_CHECK(has(request("textDocument/hover"), "const int OFFSET = 4"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":1,\"character\":10}"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 2);

    snprintf(text, sizeof text, "%s[Sta$ge(Phases.Objects, -OFFSET)]\nview V() { }\n", declarations);
    open_document(text);
    TIDE_CHECK(has(request("textDocument/hover"), "enum's declaration order"));
    snprintf(text, sizeof text, "%s[Stage(Phases.Objects, -OFFSET)]\nview V$() { }\n", declarations);
    open_document(text);
    TIDE_CHECK(has(request("textDocument/hover"), "Phase `Phases.Objects`, offset -4"));
    snprintf(text, sizeof text, "%s[Stage(Phases.Objects, $-OFFSET)]\nview V() { }\n", declarations);
    open_document(text);
    const char *signature = request("textDocument/signatureHelp");
    TIDE_CHECK(has(signature, "Stage(Stages.Phase, int offset = 0)"));
    TIDE_CHECK(has(signature, "\"activeParameter\":1"));

    // A fully qualified phase indexes its namespace, enum type, and member separately.
    open_document("namespace Frame;\nenum Phases { Objects }\nscene Main { }\n"
                  "[Stage(Frame.Pha$ses.Objects)]\nview V() { }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":1,\"character\":5}"));
    completion = complete("namespace Frame;\nenum Phases { Objects }\nscene Main { }\n"
                          "[Stage(Frame.$)]\nview V() { }\n");
    TIDE_CHECK(offers(completion, "Phases") && !offers(completion, "Main") && !offers(completion, "V"));
}

TIDE_TEST(lsp_constants)
{
    start();
    static const char game[] = "const int MAX = 100;\nconst float2 ORIGIN = float2(1, 2);\nstruct Stats { int armor; }\n"
                               "const Stats START = Stats { armor = MAX / 2 };\nsingleton Match { int n = MAX; float x; }\n"
                               "scene Main { }\nsystem S(mut Match match)\n{\n    match.n += MAX;\n    match.x = ORIGIN.y;\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(game), "\"result\":[]")); // Laid out as the formatter would

    open_document("const int MAX = 100;\nsingleton Match { int n; }\nscene Main { }\n"
                  "system S(mut Match match) { match.n += M$AX; }\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "const int MAX = 100"));
    TIDE_CHECK(has(hover, "Constant: the same on every machine"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"range\":{\"start\":{\"line\":0,\"character\":10}"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 2);

    // A value with braces in it shows whole.
    open_document("struct Stats { int armor; }\nconst Stats ST$ART = Stats { armor = 2 };\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "const Stats START = Stats { armor = 2 }"));

    const char *in_code = complete("const int MAX = 100;\nsingleton Match { int n; }\nscene Main { }\n"
                                   "system S(mut Match match) { match.n = $ }\n");
    TIDE_CHECK(offers(in_code, "MAX"));
    TIDE_CHECK(offers(complete("const int MAX = 100;\nsingleton Match { int n = $ }\nscene Main { }\n"), "MAX"));
    TIDE_CHECK(offers(complete("const float2 ORIGIN = float2(1, 2);\nsingleton Match { float x; }\nscene Main { }\n"
                               "system S(mut Match match) { match.x = ORIGIN.$ }\n"),
                      "y"));
    TIDE_CHECK(offers(complete("$\n"), "const"));

    open_document(game);
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"MAX\",\"detail\":\"const\",\"kind\":14"));

    // Renaming one renames every use.
    static const char program[] = "const int MAX = 100;\nsingleton Match { int n = MAX; }\nscene Main { }\n"
                                  "system S(mut Match match) { match.n += MAX; }\n";
    open_document("const int M$AX = 100;\nsingleton Match { int n = MAX; }\nscene Main { }\n"
                  "system S(mut Match match) { match.n += MAX; }\n");
    TIDE_CHECK(has(request("textDocument/prepareRename"), "\"result\":{\"start\""));
    request_with("textDocument/rename", "\"newName\":\"LIMIT\"");
    const char *renamed = apply_reply(program, "file:///test.tide");
    TIDE_CHECK(has(renamed, "const int LIMIT = 100;"));
    TIDE_CHECK(has(renamed, "int n = LIMIT;"));
    TIDE_CHECK(has(renamed, "match.n += LIMIT;"));
    TIDE_CHECK(!has(renamed, "MAX"));
}

TIDE_TEST(lsp_settings)
{
    start();
    static const char game[] = "const int RATE = 30;\n\nsettings\n{\n    title = \"Asteroids\";\n    tickRate = RATE;\n}\n\n"
                               "scene Main { }\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(game), "\"result\":[]")); // Laid out as the formatter would

    open_document("settings\n{\n    tick$Rate = 30;\n}\nscene Main { }\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "int tickRate"));
    TIDE_CHECK(has(hover, "Without it: 60."));
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Settings are the engine's"));
    open_document("settings\n{\n    hostMig$ration = true;\n}\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "bool hostMigration"));
    open_document("settings\n{\n    vers$ion = \"1.2.0\";\n}\nscene Main { }\n");
    hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "string version"));
    TIDE_CHECK(has(hover, "Without it: 1.0."));

    const char *empty = complete("settings\n{\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(empty, "tickRate"));
    TIDE_CHECK(offers(empty, "title"));
    TIDE_CHECK(offers(empty, "appId"));
    TIDE_CHECK(offers(empty, "version"));
    // A version phones won't take is an error the editor shows
    open_document("settings { version = \"v2\"; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "isn't a version that Android and iOS both take"));
    TIDE_CHECK(!offers(empty, "float3")); // Names, not values
    const char *rest = complete("settings\n{\n    tickRate = 30;\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(!offers(rest, "tickRate")); // Set already
    TIDE_CHECK(offers(rest, "title"));
    TIDE_CHECK(offers(complete("const int RATE = 30;\nsettings\n{\n    tickRate = $\n}\nscene Main { }\n"), "RATE"));
    TIDE_CHECK(offers(complete("scene Main { }\n$\n"), "settings"));

    open_document(game);
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"settings\",\"detail\":\"the engine's\",\"kind\":19"));
    TIDE_CHECK(has(symbols, "\"name\":\"tickRate\",\"kind\":7"));

    // A name that isn't a setting says which one was meant.
    open_document("settings { tickRat = 30; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "did you mean 'tickRate'?"));
}

TIDE_TEST(lsp_default_value)
{
    start();
    open_document("scene Main { }\nview Render(with Main)\n{\n    Draw2D.Circle(def$ault, 5, Color.yellow);\n}\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "default: float2"));
    TIDE_CHECK(has(hover, "All zeros: the default value of `float2`"));

    open_document("struct Stats { int armor = 3; }\nsingleton S { Stats stats; }\nscene Main { }\n"
                  "system Reset(mut S s) { s.stats = defa$ult; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "`Stats { }`: each field's default"));

    TIDE_CHECK(offers(complete("singleton S { int n; }\nscene Main { }\nsystem Reset(mut S s) { s.n = $ }\n"), "default"));

    // `default` in ?: isn't a switch's label.
    static const char tidy[] = "singleton S { int n; bool on; }\nscene Main { }\n"
                               "system Pick(mut S s)\n{\n    s.n = s.on ? default : 2;\n    s.n = s.on ? 1 : default;\n}\n";
    TIDE_CHECK(has(format_reply(tidy), "\"result\":[]"));
}

TIDE_TEST(lsp_byte_scalar_hover_and_completion)
{
    start();
    open_document("singleton Data { by$te value = 255; List<byte> content = [0, 255]; }\nscene Main { }\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "An unsigned 8-bit integer, from 0 to 255"));
    TIDE_CHECK(offers(complete("singleton Data { $ }\nscene Main { }\n"), "byte"));
    open_document("singleton Data { byte value; }\nscene Main { }\nsystem Reset(mut Data data) { data.value = def$ault; }\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "default: byte") && has(hover, "`0`"));
}

TIDE_TEST(lsp_format_switch)
{
    start();
    static const char messy[] =
        "enum Phase { Warmup, Playing }\nsingleton Match { Phase phase; int n; }\nsystem Main() { }\n"
        "system S(mut Match match)\n{\nswitch (match.phase)\n{\ncase Phase.Warmup :\nmatch.n = 1;\nbreak;\n"
        "default:\n// Nothing to do\nif (match.n > 1) { match.n = 0; }\nbreak;\n}\n}\n";
    static const char expected[] =
        "enum Phase { Warmup, Playing }\nsingleton Match { Phase phase; int n; }\nsystem Main() { }\n"
        "system S(mut Match match)\n{\n    switch (match.phase)\n    {\n        case Phase.Warmup:\n            match.n = 1;\n"
        "            break;\n        default:\n            // Nothing to do\n            if (match.n > 1) { match.n = 0; }\n"
        "            break;\n    }\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));
}


TIDE_TEST(lsp_gui)
{
    start();
    static const char game[] =
        "local singleton Menu { bool open; float volume; }\nscene Main { }\n\n"
        "void Section(string title, mut bool open, Action content)\n{\n    GUILayout.Toggle(title, open);\n"
        "    if (open) content();\n}\n\n"
        "view Options(mut Menu menu)\n{\n    GUILayout.Area(Anchor.MiddleCenter)\n    {\n"
        "        Section(\"Audio\", menu.open)\n        {\n            GUILayout.Slider(\"Volume\", menu.volume, 0, 1);\n"
        "        }\n    }\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(game), "\"result\":[]")); // Actions after calls keep their braces on lines of their own

    const char *widgets = complete("scene Main { }\nview V()\n{\n    GUILayout.$\n}\n");
    TIDE_CHECK(offers(widgets, "Button"));
    TIDE_CHECK(offers(widgets, "Horizontal"));
    TIDE_CHECK(offers(widgets, "Area"));
    TIDE_CHECK(offers(complete("scene Main { }\nview V()\n{\n    $\n}\n"), "GUILayout"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    $\n}\n"), "GUILayout"));
    TIDE_CHECK(offers(complete("scene Main { }\nvoid F()\n{\n    $\n}\n"), "Screen"));

    // Claims, for widgets a view draws itself
    const char *claims = complete("scene Main { }\nview V()\n{\n    GUI.$\n}\n");
    TIDE_CHECK(offers(claims, "ClaimPointer"));
    TIDE_CHECK(offers(claims, "ClaimKeyboard"));
    TIDE_CHECK(offers(claims, "ShowKeyboard"));
    open_document("scene Main { }\nview V()\n{\n    GUI.Claim$Pointer();\n    GUI.ClaimKeyboard();\n    GUI.ShowKeyboard();\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *claim = request("textDocument/hover");
    TIDE_CHECK(has(claim, "GUI.ClaimPointer()"));
    TIDE_CHECK(has(claim, "hidden from the input's `Sample` and from the other views"));
    // At a rect: where a widget of the view's own is
    open_document("scene Main { }\nview V()\n{\n    GUI.Claim$Pointer(Rect(0, 0, 200, 200));\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "GUI.ClaimPointer(Rect rect)"));

    open_document("local singleton M { bool on; }\nscene Main { }\nview V(mut M m) { GUILayout.Tog$gle(\"On\", m.on); }\n");
    const char *toggle = request("textDocument/hover");
    TIDE_CHECK(has(toggle, "GUILayout.Toggle(string text, mut bool value) -> bool"));
    TIDE_CHECK(has(toggle, "Returns whether it changed it"));

    open_document("scene Main { }\nvoid Twice(Action content) { con$tent(); content(); }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Action content"));

    open_document("scene Main { }\nview V() { var w = Screen.wid$th; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Screen.width: float"));

    // The cursor's lock: views ask for it and read whether it holds, and so
    // do the functions they call; the input's Sample only reads; the match
    // has neither.
    static const char look[] = "input In\n{\n    float2 look;\n    Sample()\n    {\n        $\n    }\n"
                               "    Sanitize()\n    {\n        look = float2(0, 0);\n    }\n}\nscene Main { }\n";
    TIDE_CHECK(offers(complete("scene Main { }\nview V()\n{\n    $\n}\n"), "Cursor"));
    TIDE_CHECK(offers(complete("scene Main { }\nvoid F()\n{\n    $\n}\n"), "Cursor"));
    TIDE_CHECK(offers(complete(look), "Cursor"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    $\n}\n"), "Cursor"));
    const char *cursor = complete("scene Main { }\nview V()\n{\n    Cursor.$\n}\n");
    TIDE_CHECK(offers(cursor, "Lock") && offers(cursor, "locked"));
    const char *sampled = complete("input In\n{\n    float2 look;\n    Sample()\n    {\n        if (Cursor.$) look = Devices.mouse.delta;\n"
                                   "    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(sampled, "locked") && !offers(sampled, "Lock"));
    const char *in_match = complete("scene Main { }\nsystem S()\n{\n    Cursor.$\n}\n");
    TIDE_CHECK(!offers(in_match, "locked") && !offers(in_match, "Lock"));
    static const char camera[] = "input In\n{\n    float2 look;\n    Sample() { if (Cursor.locked) look = Devices.mouse.delta; }\n}\n"
                                 "scene Main { }\nview Look()\n{\n    Cursor.Lo$ck();\n    if (Cursor.locked) Draw2D.Clear(Color.black);\n}\n";
    open_document(camera);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *lock = request("textDocument/hover");
    TIDE_CHECK(has(lock, "Cursor.Lock()"));
    TIDE_CHECK(has(lock, "Call it every frame the game wants the mouse"));
    open_document("scene Main { }\nview Look()\n{\n    Cursor.Lock();\n    if (Cursor.loc$ked) Draw2D.Clear(Color.black);\n}\n");
    const char *locked = request("textDocument/hover");
    TIDE_CHECK(has(locked, "Cursor.locked: bool"));
    TIDE_CHECK(has(locked, "what the system says"));
    open_document("scene Main { }\nview Look()\n{\n    Cur$sor.Lock();\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "The mouse's cursor"));
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Built-in names can't be renamed"));
    // In the match, the error says where it goes instead
    open_document("scene Main { }\nsystem S()\n{\n    Cursor.Lock();\n}\n");
    TIDE_CHECK(has(last_sent(), "so it can't call Cursor.Lock"));
    // ...and a signature, as the GUI's calls have
    open_document("scene Main { }\nview Look()\n{\n    Cursor.Lock($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Cursor.Lock()"));

    // What an Action's caller writes is the caller's code: its names are the caller's.
    open_document("local singleton M { bool on; }\nscene Main { }\nvoid Twice(Action content) { content(); content(); }\n"
                  "view V(mut M m)\n{\n    Twice()\n    {\n        m.o$n = true;\n    }\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "bool on"));
}

TIDE_TEST(lsp_format_blocks)
{
    start();
    static const char messy[] = "scene Main { }\nview V()\n{\nGUILayout.Horizontal() {\nGUILayout.Label(\"a\");\n}\n"
                                "GUILayout.Vertical() { GUILayout.Label(\"b\"); }\n}\n";
    static const char expected[] = "scene Main { }\nview V()\n{\n    GUILayout.Horizontal()\n    {\n        GUILayout.Label(\"a\");\n"
                                   "    }\n    GUILayout.Vertical() { GUILayout.Label(\"b\"); }\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}


TIDE_TEST(lsp_loops)
{
    start();
    static const char game[] = "component Tally { int n; }\nscene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Tally); }\n\n"
                               "system Count(mut Tally t)\n{\n    for (var i = 0; i < 10; i++)\n    {\n"
                               "        if (i % 2 == 1) continue;\n        t.n += i;\n    }\n"
                               "    mut var left = 3;\n    while (left > 0) { left--; }\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(game), "\"result\":[]")); // Already formatted

    // The for's variable is in scope in its body.
    TIDE_CHECK(offers(complete("component Tally { int n; }\nscene Main { }\nsystem S(mut Tally t)\n{\n"
                               "    for (var index = 0; index < 3; index++)\n    {\n        t.n = $\n    }\n}\n"),
                      "index"));
    TIDE_CHECK(offers(complete("scene Main { }\nsystem S()\n{\n    $\n}\n"), "while"));
    open_document("component Tally { int n; }\nscene Main { }\nsystem S(mut Tally t)\n{\n"
                  "    for (var index = 0; index < 3; index++) t.n += ind$ex;\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "int index"));
}

TIDE_TEST(lsp_errors_as_values)
{
    start();
    // Formatted already, with no diagnostics but the warning for an ignored error.
    static const char game[] =
        "enum ParseError { Empty }\n\n"
        "int Parse(string text) fails ParseError\n{\n    if (text == \"\") fail ParseError.Empty;\n    return 1;\n}\n\n"
        "int? Find(int x)\n{\n    if (x > 0) return x;\n    return null;\n}\n\n"
        "int Twice(string text) fails ParseError\n{\n    var n = try Parse(text);\n"
        "    if (Parse(text) is int m && m > 0) return m;\n    return (Find(n) ?? 0) + Parse(text)! - 1;\n}\n\n"
        "scene Main { }\nsystem S()\n{\n    Parse(\"1\");\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "'Parse' can fail, and nothing handles its error here"));
    TIDE_CHECK(has(last_sent(), "\"severity\":2"));
    TIDE_CHECK(count(last_sent(), "\"severity\"") == 1);
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));

    // The name after `is` is in scope where the test is true.
    TIDE_CHECK(offers(complete("enum E { A }\nint P() fails E { return 1; }\nscene Main { }\n"
                               "system S()\n{\n    if (P() is int score)\n    {\n        var x = $\n    }\n}\n"),
                      "score"));
    TIDE_CHECK(!offers(complete("enum E { A }\nint P() fails E { return 1; }\nscene Main { }\n"
                                "system S()\n{\n    if (P() is int score) { }\n    var x = $\n}\n"),
                       "score"));
    // try and fail in a function that can fail, and only there.
    const char *in_failing = complete("enum E { A }\nint P() fails E\n{\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(in_failing, "fail"));
    TIDE_CHECK(offers(in_failing, "try"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    $\n}\n"), "fail"));

    // Hovers: what a call can fail with, the name `is` gives, and null.
    open_document("enum E { A }\nint P() fails E { return 1; }\nscene Main { }\nsystem S()\n{\n    var x = P$() ?? 0;\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "int P() fails E"));
    open_document("enum E { A }\nint P() fails E { return 1; }\nscene Main { }\n"
                  "system S()\n{\n    if (P() is int sc$ore) { }\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "int score"));
    open_document("int? F() { return nu$ll; }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "null: int?"));
}

TIDE_TEST(lsp_format_errors_as_values)
{
    start();
    static const char messy[] = "enum E { A }\nint ? Find(int x)\n{\nreturn null;\n}\n"
                                "int P(int ? y) fails E\n{\nint ? z = y;\nvar a = try P(1)+1;\n"
                                "return (z??Find(2) ??0)+P(3) ! -a;\n}\nscene Main { }\n";
    static const char expected[] = "enum E { A }\nint? Find(int x)\n{\n    return null;\n}\n"
                                   "int P(int? y) fails E\n{\n    int? z = y;\n    var a = try P(1) + 1;\n"
                                   "    return (z ?? Find(2) ?? 0) + P(3)! - a;\n}\nscene Main { }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));
}

TIDE_TEST(lsp_async)
{
    start();
    // Formatted already, with no diagnostics
    static const char game[] =
        "singleton Log { int n; }\n\n"
        "async int Doubled(int x)\n{\n    await Wait.Ticks(1);\n    return x * 2;\n}\n\n"
        "async event(Spawned) Count(mut Log log)\n{\n    log.n = await Doubled(2) + 1;\n}\n\n"
        "scene Main { }\nsystem S(Log log)\n{\n    if (log.n == 0) Doubled(1);\n}\n";
    open_document(game);
    TIDE_CHECK(!has(last_sent(), "\"severity\""));
    if (has(last_sent(), "\"severity\"")) printf("%s\n", last_sent());
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));
    // A messy one: `async` stays where it is
    static const char messy[] = "async void F( )\n{\nawait Wait.Frames( 2 );\n}\nscene Main { }\n";
    static const char expected[] = "async void F()\n{\n    await Wait.Frames(2);\n}\nscene Main { }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);

    // await and Wait in async code, and only there
    const char *in_async = complete("async void F()\n{\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(in_async, "await"));
    TIDE_CHECK(offers(in_async, "Wait"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    $\n}\n"), "await"));
    const char *waits = complete("async void F()\n{\n    await Wait.$\n}\nscene Main { }\n");
    TIDE_CHECK(offers(waits, "Ticks") && offers(waits, "Frames") && offers(waits, "Seconds"));
    TIDE_CHECK(offers(complete("$"), "async function"));

    // Hovers: an async function's signature, and what Wait waits for
    open_document("async int D() { await Wait.Ticks(1); return 1; }\nscene Main { }\nsystem S()\n{\n    D$();\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "async int D()"));
    open_document("async void F()\n{\n    await Wait.Sec$onds(1);\n}\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "await Wait.Seconds(float seconds)"));
}

TIDE_TEST(lsp_format_loops)
{
    start();
    static const char messy[] = "scene Main { }\nsystem S()\n{\nmut var n = 0;\nfor(var i=0;i<3;i ++){\nn+=i;\n}\n"
                                "while (n > 0) {\nn --;\nif (n == 1) break;\n}\n}\n";
    static const char expected[] = "scene Main { }\nsystem S()\n{\n    mut var n = 0;\n    for (var i = 0; i < 3; i++)\n    {\n"
                                   "        n += i;\n    }\n    while (n > 0)\n    {\n        n--;\n        if (n == 1) break;\n"
                                   "    }\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}


TIDE_TEST(lsp_text)
{
    start();
    static const char game[] = "local singleton Score { int points; float time; }\nscene Main { }\n\n"
                               "view Hud(Score score)\n{\n    var name = \"cat\";\n"
                               "    GUILayout.Label($$\"{name}: {score.points:D3} in {score.time:F1}s\" + \"!\");\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(game), "\"result\":[]")); // Values sit against their braces

    // Completion works in a value, not in the text around it.
    TIDE_CHECK(offers(complete("local singleton Score { int points; }\nscene Main { }\nview V(Score score)\n{\n"
                               "    GUILayout.Label($$\"points: {sco$}\");\n}\n"),
                      "score"));
    TIDE_CHECK(!offers(complete("local singleton Score { int points; }\nscene Main { }\nview V(Score score)\n{\n"
                                "    GUILayout.Label($$\"poi$ {score.points}\");\n}\n"),
                       "score"));
    const char *methods = complete("scene Main { }\nview V()\n{\n    var name = \"cat\";\n    var n = name.$\n}\n");
    TIDE_CHECK(offers(methods, "length"));
    TIDE_CHECK(offers(methods, "Contains"));

    open_document("scene Main { }\nview V() { var n = \"cat\".Cont$ains(\"a\"); }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Whether `value` is in the text."));

    // Text in fields
    TIDE_CHECK(offers(complete("component Name\n{\n    $\n}\nscene Main { }\n"), "string"));
    open_document("component Name { string va$lue = \"cat\"; }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "string value"));
    const char *fields = complete("component Name { string value; }\nscene Main { }\nsystem S(Name name)\n{\n"
                                  "    var n = name.value.$\n}\n");
    TIDE_CHECK(offers(fields, "length"));
}

TIDE_TEST(lsp_format_text)
{
    start();
    static const char messy[] = "scene Main { }\nview V()\n{\nvar a = $$\"x { 1 + 2 :F2} y {3}\";\n}\n";
    static const char expected[] = "scene Main { }\nview V()\n{\n    var a = $\"x {1 + 2:F2} y {3}\";\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}


TIDE_TEST(lsp_lists)
{
    start();
    static const char game[] = "component Inventory { List<int> scores = [1, 2]; }\nscene Main { }\n"
                               "event(Spawned) Setup(with Main) { Spawn(Inventory); }\n\n"
                               "system Count(mut Inventory inv)\n{\n    foreach (var score in inv.scores)\n    {\n"
                               "        if (score > 1) inv.scores.Add(score);\n    }\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(game), "\"result\":[]")); // List<int> keeps no spaces

    const char *members = complete("component Inventory { List<int> scores; }\nscene Main { }\nsystem S(Inventory inv)\n{\n"
                                   "    var n = inv.scores.$\n}\n");
    TIDE_CHECK(offers(members, "count"));
    TIDE_CHECK(offers(members, "Add"));
    TIDE_CHECK(offers(complete("component Inventory\n{\n    $\n}\nscene Main { }\n"), "List"));

    // The loop's variable is in scope in its body.
    TIDE_CHECK(offers(complete("component Inventory { List<int> scores; }\nscene Main { }\nsystem S(mut Inventory inv)\n{\n"
                               "    foreach (var score in inv.scores)\n    {\n        var x = $\n    }\n}\n"),
                      "score"));
    open_document("component Inventory { List<int> sco$res; }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "List<int> scores"));
}

TIDE_TEST(lsp_grids)
{
    start();
    static const char game[] = "singleton Field { Grid2<int> cells = Grid2(64, 64); }\nscene Main { }\n\n"
                               "system Fall(mut Field field)\n{\n"
                               "    for (var y = 0; y < field.cells.size.y; y++) field.cells[0, y] = 1;\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(game), "\"result\":[]")); // Grid2<int> keeps no spaces

    const char *members = complete("singleton Field { Grid2<int> cells; }\nscene Main { }\nsystem S(Field field)\n{\n"
                                   "    var n = field.cells.$\n}\n");
    TIDE_CHECK(offers(members, "size"));
    TIDE_CHECK(offers(members, "Clear"));
    TIDE_CHECK(offers(complete("singleton Field\n{\n    $\n}\nscene Main { }\n"), "Grid2"));
    open_document("singleton Field { Grid2<int> ce$lls; }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Grid2<int> cells"));

    // Byte-sized cells: the enum keeps its backing as written, and hovers show it
    static const char voxels[] = "enum Voxel : byte\n{\n    Air,\n    Stone,\n}\n\nsingleton Terrain { Grid3<Voxel> cells; }\nscene Main { }\n";
    open_document(voxels);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(voxels), "\"result\":[]"));
    open_document("enum Vox$el : byte { Air, Stone }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "enum Voxel : byte"));
    const char *backings = complete("enum Voxel : $\nscene Main { }\n");
    TIDE_CHECK(offers(backings, "byte"));
    TIDE_CHECK(offers(backings, "ushort"));
}

// Parallel loops: offered where they can go, formatted, and their cells' places
// hovered.
TIDE_TEST(lsp_parallel)
{
    start();
    static const char game[] = "singleton Field { Grid2<int> cells = Grid2(64, 64); }\nscene Main { }\n\n"
                               "system Fall(mut Field field, Time time)\n{\n"
                               "    parallel (var at in field.cells by 2 offset time.tick % 2)\n    {\n"
                               "        var below = field.cells[at];\n"
                               "        field.cells[at + int2(0, 1)] = below;\n    }\n"
                               "    foreach (var at in field.cells) field.cells[at] = 0;\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));
    static const char messy[] = "singleton Field { Grid2<int> cells; }\nscene Main { }\n"
                                "system S(mut Field field)\n{\n    parallel(var at in field.cells by 2 offset 1){ field.cells[at] = 1; }\n}\n";
    format_reply(messy);
    TIDE_CHECK(has(apply_reply(messy, NULL), "    parallel (var at in field.cells by 2 offset 1) { field.cells[at] = 1; }\n"));

    TIDE_CHECK(offers(complete("singleton Field { Grid2<int> cells; }\nscene Main { }\nsystem S(mut Field field)\n{\n    $\n}\n"),
                      "parallel"));
    TIDE_CHECK(!offers(complete("scene Main { }\nvoid F()\n{\n    $\n}\n"), "parallel")); // Only systems, views and handlers
    open_document("singleton Field { Grid2<int> cells; }\nscene Main { }\nsystem S(mut Field field)\n{\n"
                  "    parallel (var at in field.cells) field.cells[a$t] = 1;\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "int2 at"));
    open_document("singleton Field { Grid2<int> cells; }\nscene Main { }\nsystem S(mut Field field)\n{\n"
                  "    parallel (var at in field.cells) field.cells[at + int2(1, 0)] = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "a parallel loop's step changes only its own cell"));
}

TIDE_TEST(lsp_format_lists)
{
    start();
    static const char messy[] = "scene Main { }\nint F(List < int > xs)\n{\nmut List<int> ys = [ 1,2 ];\nys.Add(xs [0]);\nreturn ys.count;\n}\n";
    static const char expected[] = "scene Main { }\nint F(List<int> xs)\n{\n    mut List<int> ys = [1, 2];\n    ys.Add(xs[0]);\n"
                                   "    return ys.count;\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}


TIDE_TEST(lsp_scenes)
{
    start();
    static const char game[] = "scene Arena { int size = 20; }\nscene Main { }\n"
                               "event(Spawned) Setup(with Main) { Scene.Load(Arena { size = 30 }); }\n"
                               "system Close(Arena arena) { if (arena.size > 40) Scene.Unload(this); }\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    open_document("scene Are$na { int size = 20; }\nscene Main { }\nevent(Spawned) Setup(with Main) { Scene.Load(Arena); }\n");
    const char *type = request("textDocument/hover");
    TIDE_CHECK(has(type, "scene Arena"));
    TIDE_CHECK(has(type, "int size = 20;"));
    TIDE_CHECK(!has(type, "tide_players")); // The engine's own fields stay hidden

    open_document("scene Arena { }\nscene Main { }\nevent(Spawned) Setup(with Main) { Scene.Lo$ad(Arena); }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Loads a scene and returns its entity"));

    const char *calls = complete("scene Arena { }\nscene Main { }\nevent(Spawned) Setup(with Main)\n{\n    Scene.$\n}\n");
    TIDE_CHECK(offers(calls, "Load"));
    TIDE_CHECK(offers(calls, "Unload"));
    const char *fields = complete("scene Arena { int size; }\nscene Main { }\nsystem S(Arena arena)\n{\n    var x = arena.$\n}\n");
    TIDE_CHECK(offers(fields, "size"));
    TIDE_CHECK(!offers(fields, "tide_visibility"));

    open_document(game);
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"Arena\",\"detail\":\"scene\""));
    TIDE_CHECK(!has(symbols, "tide_players"));
}

// Snap: on entities and singletons in match code. A type's Interpolate isn't for calling.
TIDE_TEST(lsp_snap)
{
    start();
    const char *entity = complete("component Body { float2 p; }\nscene Main { }\n"
                                  "system Respawn(mut Body body)\n{\n    this.$\n}\n");
    TIDE_CHECK(offers(entity, "Snap"));
    TIDE_CHECK(offers(entity, "Destroy"));
    const char *singleton = complete("singleton Camera { float2 center; }\nscene Main { }\n"
                                     "system Cut(mut Camera camera)\n{\n    camera.$\n}\n");
    TIDE_CHECK(offers(singleton, "Snap"));
    TIDE_CHECK(offers(singleton, "center"));
    const char *methods = complete("struct Angle\n{\n    float degrees;\n    Angle Interpolate(Angle from, Angle to, float t) { return to; }\n"
                                   "    float Radians() { return degrees; }\n}\ncomponent Body { Angle heading; }\nscene Main { }\n"
                                   "system S(Body body)\n{\n    var r = body.heading.$\n}\n");
    TIDE_CHECK(offers(methods, "Radians"));
    TIDE_CHECK(!offers(methods, "Interpolate"));
}

#define THIS_GAME                                                              \
    "component Health\n"                                                       \
    "{\n"                                                                      \
    "    int value;\n"                                                         \
    "    bool IsMine(Entity e) { return e == th$is; }\n"                       \
    "}\n"                                                                      \
    "local component Spark { int left; }\n"                                    \
    "scene Main { }\n"                                                         \
    "system Die(Health health) { if (health.IsMine(th$is)) this.Destroy(); }\n" \
    "view Fade(mut Spark spark) { spark.left -= 1; if (spark.left <= 0) th$is.Destroy(); }\n"

// this: the entity the code runs for, and whose component a method is called on.
TIDE_TEST(lsp_this)
{
    start();
    open_document(THIS_GAME);
    TIDE_CHECK(!has(last_sent(), "\"severity\":1")); // No errors

    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 3, 41, ""), "The entity whose `Health` this is."));
    const char *system = request_at("file:///test.tide", "textDocument/hover", 7, 48, "");
    TIDE_CHECK(has(system, "Entity this") && has(system, "The entity `Die` runs for."));
    const char *view = request_at("file:///test.tide", "textDocument/hover", 8, 68, "");
    TIDE_CHECK(has(view, "LocalEntity this"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 7, 48, ""), "'this' is a keyword"));

    // Offered where the code runs for an entity, and its methods after the dot.
    TIDE_CHECK(offers(complete("component Body { int x; }\nscene Main { }\nsystem S(Body body)\n{\n    $\n}\n"), "this"));
    TIDE_CHECK(!offers(complete("component Body { int x; }\nscene Main { }\nsystem S(Time time)\n{\n    $\n}\n"), "this"));
    TIDE_CHECK(offers(complete("component Body { int x; }\nscene Main { }\nsystem S(with Body)\n{\n    this.$\n}\n"),
                      "Destroy"));

    // The entity isn't a parameter: the quick fix removes it and writes `this`.
    open_document("component Lifetime { int ticks; }\nscene Main { }\n"
                  "system Expire(Entity self, mut Lifetime life)\n{\n    life.ticks -= 1;\n    if (life.ticks <= 0) self.Destroy();\n}\n");
    TIDE_CHECK(has(last_sent(), "the entity a system runs for is 'this', not a parameter"));
    const char *fix = actions_at(2);
    TIDE_CHECK(has(fix, "Use 'this' instead of 'self'"));
    TIDE_CHECK(has(fix, "{\"range\":{\"start\":{\"line\":2,\"character\":14},\"end\":{\"line\":2,\"character\":27}},\"newText\":\"\"}"));
    TIDE_CHECK(has(fix, "{\"range\":{\"start\":{\"line\":5,\"character\":25},\"end\":{\"line\":5,\"character\":29}},\"newText\":\"this\"}"));
}

// Session: its calls from local code, and its singleton and events.
TIDE_TEST(lsp_sessions)
{
    start();
    static const char game[] = "local scene Main { }\nscene Arena { int size = 20; }\n"
                               "view Menu(Session session)\n{\n"
                               "    if (GUILayout.Button(\"Play\") && session.state == SessionState.Offline) Session.Start(Arena);\n"
                               "    if (GUILayout.Button(\"Open\") && !session.open) Session.Open(7777);\n"
                               "    if (GUILayout.Button(\"Join\")) Session.Join(\"K7QF2M\");\n"
                               "    if (GUILayout.Button(\"Connect\")) Session.Connect(\"127.0.0.1\");\n}\n"
                               "local event(Disconnected gone) Lost() { }\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    // The name itself, where a statement starts in local code, even inside a
    // container's block and a branch.
    const char *name = complete("local scene Main { }\nview Menu()\n{\n    GUILayout.Area(Anchor.MiddleCenter)\n    {\n"
                                "        if (GUILayout.Button(\"Host\"))\n        {\n            Sess$\n        }\n    }\n}\n");
    TIDE_CHECK(offers(name, "Session"));
    TIDE_CHECK(offers(complete("local scene Main { }\nlocal event(Disconnected gone) Lost()\n{\n    $\n}\n"), "Session"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem Move()\n{\n    $\n}\n"), "Session"));
    TIDE_CHECK(!offers(complete("local scene Main { }\nview Menu()\n{\n    var x = $\n}\n"), "Session"));

    const char *calls = complete("local scene Main { }\nview Menu()\n{\n    Session.$\n}\n");
    TIDE_CHECK(offers(calls, "Start"));
    TIDE_CHECK(offers(calls, "Open"));
    TIDE_CHECK(offers(calls, "Close"));
    TIDE_CHECK(offers(calls, "Kick"));
    TIDE_CHECK(offers(calls, "KickAll"));
    TIDE_CHECK(offers(calls, "End"));
    TIDE_CHECK(offers(calls, "Join"));
    TIDE_CHECK(offers(calls, "Connect"));
    TIDE_CHECK(offers(calls, "Leave"));
    const char *gone = complete("local scene Main { }\nlocal event(Disconnected gone) Lost()\n{\n    var why = gone.$\n}\n");
    TIDE_CHECK(offers(gone, "reason"));
    TIDE_CHECK(offers(gone, "message"));
    const char *fields = complete("local scene Main { }\nview Menu(Session session)\n{\n    var s = session.$\n}\n");
    TIDE_CHECK(offers(fields, "state"));
    TIDE_CHECK(offers(fields, "ping"));
    TIDE_CHECK(offers(fields, "open"));
    TIDE_CHECK(offers(fields, "room"));

    open_document("local scene Main { }\nview Menu()\n{\n    Session.Le$ave();\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Leaves the match"));

    // Clipboard, where Session is: its one call
    TIDE_CHECK(offers(complete("local scene Main { }\nview Menu()\n{\n    $\n}\n"), "Clipboard"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem Move()\n{\n    $\n}\n"), "Clipboard"));
    TIDE_CHECK(offers(complete("local scene Main { }\nview Menu()\n{\n    Clipboard.$\n}\n"), "Copy"));
    open_document("local scene Main { }\nview Menu(Session session)\n{\n"
                  "    if (GUILayout.Button(\"Copy\")) Clipboard.Co$py(session.room);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "on this machine's clipboard"));
}


TIDE_TEST(lsp_format_events)
{
    start();
    static const char messy[] = "event Hit { int damage; }\nsystem Main() { Send(Hit); }\n"
                                "event (Hit hit) TakeHit( ) { }\nevent ( Spawned ) Grow() { }\n";
    static const char expected[] = "event Hit { int damage; }\nsystem Main() { Send(Hit); }\n"
                                   "event(Hit hit) TakeHit() { }\nevent(Spawned) Grow() { }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}

#define STRUCTS                                                                \
    "struct Stats\n"                                                           \
    "{\n"                                                                      \
    "    float health = 100;\n"                                                \
    "}\n"                                                                      \
    "\n"                                                                       \
    "component Unit\n"                                                         \
    "{\n"                                                                      \
    "    Stats stats;\n"                                                       \
    "}\n"                                                                      \
    "\n"                                                                       \
    "scene Main { }\n"                                                         \
    "event(Spawned) Setup(with Main)\n"                                        \
    "{\n"                                                                      \
    "    Spawn(Unit);\n"                                                       \
    "}\n"

TIDE_TEST(lsp_structs)
{
    start();
    open_document(STRUCTS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    // `Stats` on line 0, column 7: a struct (2), declared (1).
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), "\"data\":[0,7,5,2,1,"));
    TIDE_CHECK(has(request("textDocument/documentSymbol"), "\"name\":\"Stats\""));

    // As a field type, through members, and in values.
    TIDE_CHECK(offers(complete("struct Stats { float health; }\ncomponent Unit\n{\n    $\n}\n"), "Stats"));
    TIDE_CHECK(offers(complete(STRUCTS "system Hurt(mut Unit unit)\n{\n    unit.stats.$\n}\n"), "health"));
    TIDE_CHECK(offers(complete(STRUCTS "system Hurt(mut Unit unit)\n{\n    unit.stats = Stats { $ };\n}\n"), "health"));

    open_document("struct St$ats\n{\n    float health = 100;\n}\nscene Main { }\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "struct Stats"));
    TIDE_CHECK(has(hover, "float health = 100;"));

    // Laid out like the other declarations.
    static const char messy[] = "struct Stats {\nfloat health = 100;\n}\nscene Main { }\n";
    format_reply(messy);
    TIDE_CHECK(strcmp(apply_reply(messy, NULL), "struct Stats\n{\n    float health = 100;\n}\nscene Main { }\n") == 0);
}

#define METHODS                                                                \
    "struct Stats\n"                                                           \
    "{\n"                                                                      \
    "    float health = 100;\n"                                                \
    "\n"                                                                       \
    "    bool IsDead() { return health <= 0; }\n"                              \
    "    mut void Hurt(float amount) { health -= amount; }\n"                  \
    "}\n"                                                                      \
    "\n"                                                                       \
    "component Unit { Stats stats; }\n"                                        \
    "\n"                                                                       \
    "float Heal(mut Stats stats, float amount)\n"                              \
    "{\n"                                                                      \
    "    stats.health += amount;\n"                                            \
    "    return stats.health;\n"                                               \
    "}\n"                                                                      \
    "\n"                                                                       \
    "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Unit); }\n"

TIDE_TEST(lsp_methods_and_functions)
{
    start();
    open_document(METHODS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"IsDead\",\"detail\":\"bool IsDead()\",\"kind\":6"));
    TIDE_CHECK(has(symbols, "\"name\":\"Heal\",\"detail\":\"function\",\"kind\":12"));

    // After a dot, methods with their signatures; in code, functions; in a method, its type's fields.
    const char *members = complete(METHODS "system Fight(mut Unit unit)\n{\n    unit.stats.$\n}\n");
    TIDE_CHECK(offers(members, "IsDead") && offers(members, "Hurt") && offers(members, "health"));
    TIDE_CHECK(has(members, "mut void Hurt(float amount)"));
    TIDE_CHECK(offers(complete(METHODS "system Fight(mut Unit unit)\n{\n    $\n}\n"), "Heal"));
    TIDE_CHECK(offers(complete("struct Stats\n{\n    float health;\n    bool IsDead() { return $ }\n}\nscene Main { }\n"),
                      "health"));

    // Hover, definition and references go to the method itself.
    open_document(METHODS "system Fight(mut Unit unit)\n{\n    unit.stats.Hu$rt(1);\n}\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "mut void Hurt(float amount)"));
    TIDE_CHECK(has(hover, "Method of struct `Stats`"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"range\":{\"start\":{\"line\":5,\"character\":13}"));

    open_document(METHODS "system Fight(mut Unit unit)\n{\n    He$al(unit.stats, 1);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "float Heal(mut Stats stats, float amount)"));
    open_document(METHODS "system Fight(mut Unit unit)\n{\n    Heal(unit.stats, $\n}\n");
    const char *signature = request("textDocument/signatureHelp");
    TIDE_CHECK(has(signature, "float Heal(mut Stats stats, float amount)"));
    TIDE_CHECK(has(signature, "\"activeParameter\":1"));

    // Inlay hints: what a var is, and parameter names at literal arguments.
    open_document(METHODS "system Fight(mut Unit unit)\n{\n    var left = Heal(unit.stats, 2);\n}\n");
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":30,\"character\":0}}");
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":20,\"character\":12},\"label\":\": float\",\"kind\":1"));
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":20,\"character\":32},\"label\":\"amount:\",\"kind\":2"));
    TIDE_CHECK(!has(hints, "\"stats:\"")); // Not for arguments that already say what they are

    // Workspace symbols: every declaration of the game, methods with their type, by fuzzy name.
    open_document(METHODS);
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"hrt\"}}");
    TIDE_CHECK(has(last_sent(), "\"name\":\"Hurt\",\"kind\":6") && has(last_sent(), "\"containerName\":\"Stats\""));
    TIDE_CHECK(!has(last_sent(), "\"name\":\"Heal\""));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"\"}}");
    TIDE_CHECK(has(last_sent(), "\"name\":\"Heal\",\"kind\":12") && has(last_sent(), "\"name\":\"Unit\",\"kind\":23"));

    // Laid out like other code: braces on their own lines, one-liners kept.
    static const char messy[] = "struct Stats {\nfloat health;\nbool IsDead() { return health <= 0; }\n"
                                "mut void Hurt() {\nhealth -= 1; }\n}\nvoid Reset(mut Stats s) {\ns.health = 0; }\n"
                                "scene Main { }\n";
    static const char expected[] = "struct Stats\n{\n    float health;\n    bool IsDead() { return health <= 0; }\n"
                                   "    mut void Hurt()\n    {\n        health -= 1;\n    }\n}\n"
                                   "void Reset(mut Stats s)\n{\n    s.health = 0;\n}\nscene Main { }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}

#define EXTERNS                                                                \
    "[NativeName(\"stb_perlin_noise3\")]\n"                                    \
    "extern float Noise(float x, float y);\n"                                  \
    "extern int twice(int x);\n"                                               \
    "extern float Weigh(in float3 v, List<float> weights, string name);\n"    \
    "\n"                                                                       \
    "component Ground { float height; }\n"                                     \
    "scene Main { }\n"                                                         \
    "event(Spawned) Setup(with Main)\n"                                        \
    "{\n"                                                                      \
    "    Spawn(Ground);\n"                                                     \
    "}\n"

TIDE_TEST(lsp_extern_functions)
{
    start();
    open_document(EXTERNS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    // Hover says it's C's, and which C function.
    open_document(EXTERNS "system Shape(mut Ground ground)\n{\n    ground.height = No$ise(1, 2);\n}\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "extern float Noise(float x, float y)"));
    TIDE_CHECK(has(hover, "[NativeName(\\\"stb_perlin_noise3\\\")]"));
    TIDE_CHECK(has(hover, "C function `stb_perlin_noise3`"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"range\":{\"start\":{\"line\":1,\"character\":13}"));
    open_document(EXTERNS "system Shape(mut Ground ground)\n{\n    ground.height = tw$ice(2);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "C function `twice`"));
    open_document(EXTERNS "system Shape(mut Ground ground)\n{\n    ground.height = We$igh(float3(1), [1], \"a\");\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "extern float Weigh(in float3 v, List<float> weights, string name)"));

    // Completion: the declaration, its return type and parameters, the attribute, and calls.
    TIDE_CHECK(offers(complete(EXTERNS "$"), "extern"));
    TIDE_CHECK(offers(complete(EXTERNS "extern $"), "float"));
    const char *params = complete(EXTERNS "extern float Sum($");
    TIDE_CHECK(offers(params, "in") && offers(params, "mut") && offers(params, "float3"));
    TIDE_CHECK(offers(complete(EXTERNS "extern float Sum(in $"), "float3"));
    TIDE_CHECK(offers(complete(EXTERNS "[$"), "NativeName"));
    TIDE_CHECK(offers(complete(EXTERNS "system Shape(mut Ground ground)\n{\n    $\n}\n"), "Noise"));

    // Errors come with the code around them.
    open_document("scene Main { }\nextern void Log(List<string> lines);\n");
    TIDE_CHECK(has(last_sent(), "C functions can't take lists of text yet"));

    // The formatter leaves an extern on its line.
    static const char messy[] = "extern  float Noise( float x ,float y ) ;\nscene Main { }\n";
    static const char expected[] = "extern float Noise(float x, float y);\nscene Main { }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}

#define OPERATORS                                                              \
    "struct Money\n"                                                           \
    "{\n"                                                                      \
    "    int cents;\n"                                                         \
    "\n"                                                                       \
    "    Money operator +(Money a, Money b) { return Money { cents = a.cents + b.cents }; }\n" \
    "    int Dollars() { return cents / 100; }\n"                              \
    "}\n"                                                                      \
    "\n"                                                                       \
    "singleton Wallet { Money total; }\n"                                      \
    "\n"                                                                       \
    "scene Main { }\n"                                                         \
    "event(Spawned) Setup(with Main, mut Wallet wallet)\n"                     \
    "{\n"                                                                      \
    "    wallet.total = wallet.total + Money { cents = 5 };\n"                 \
    "}\n"

TIDE_TEST(lsp_operators)
{
    start();
    open_document(OPERATORS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    // `+` in code goes to the operator.
    const char *hover = request_at("file:///test.tide", "textDocument/hover", 13, 32, "");
    TIDE_CHECK(has(hover, "Money operator +(Money a, Money b)") && has(hover, "Operator of struct `Money`."));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 13, 32, ""),
                   "\"range\":{\"start\":{\"line\":4,\"character\":10}"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 13, 32, ""),
                   "Operators are named by their symbol"));
    // Methods are listed after a dot; operators aren't.
    const char *members = complete(OPERATORS "system S(Wallet wallet)\n{\n    var d = wallet.total.$\n}\n");
    TIDE_CHECK(offers(members, "Dollars") && !has(members, "operator"));

    // Methods rename, at their declaration and every call.
    open_document(OPERATORS "system S(Wallet wallet)\n{\n    var d = wallet.total.Dol$lars();\n}\n");
    request_with("textDocument/rename", "\"newName\":\"Whole\"");
    const char *renamed = apply_reply(OPERATORS "system S(Wallet wallet)\n{\n    var d = wallet.total.Dollars();\n}\n",
                                      "file:///test.tide");
    TIDE_CHECK(has(renamed, "int Whole() {") && has(renamed, "wallet.total.Whole();"));

    // Snippets for what goes where: functions at the top, methods in structs
    // and components, operators in structs.
    TIDE_CHECK(offers(complete("$"), "function"));
    const char *in_struct = complete("struct Money\n{\n    int cents;\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(in_struct, "method") && offers(in_struct, "mut method") && offers(in_struct, "operator"));
    TIDE_CHECK(has(in_struct, "Money operator ${1:+}(Money a, Money b)"));
    const char *in_component = complete("component Unit\n{\n    int kills;\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(in_component, "method") && !offers(in_component, "operator"));
    TIDE_CHECK(offers(complete("struct Money\n{\n    int cents;\n    Money $\n}\nscene Main { }\n"), "operator"));

    // Laid out like C#: `operator +(`, and unary minus stays unary.
    static const char messy[] = "struct Money\n{\nint cents;\nMoney operator+(Money a,Money b) { return a; }\n"
                                "Money operator - (Money a) { return Money { cents = -a.cents }; }\n}\nscene Main { }\n";
    static const char expected[] = "struct Money\n{\n    int cents;\n    Money operator +(Money a, Money b) { return a; }\n"
                                   "    Money operator -(Money a) { return Money { cents = -a.cents }; }\n}\n"
                                   "scene Main { }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}

// #if and the rest: the formatter leaves directives and what they leave out as
// they are, which reads as comments, and each branch folds.
TIDE_TEST(lsp_directives)
{
    start();
    static const char messy[] = "scene Main { }\n"
                                "#if TIDE_0_1_OR_NEWER\n"
                                "const int A = 1;\n"
                                "#else\n"
                                "   this isn't /* code {{{\n"
                                "#endif\n"
                                "singleton Count { int n; }\n"
                                "system S(mut Count c)\n"
                                "{\n"
                                "#if TIDE_9999_0_OR_NEWER\n"
                                "        nonsense(\n"
                                "#else\n"
                                "c.n=A;\n"
                                "#endif\n"
                                "}\n";
    static const char expected[] = "scene Main { }\n"
                                   "#if TIDE_0_1_OR_NEWER\n"
                                   "const int A = 1;\n"
                                   "#else\n"
                                   "   this isn't /* code {{{\n"
                                   "#endif\n"
                                   "singleton Count { int n; }\n"
                                   "system S(mut Count c)\n"
                                   "{\n"
                                   "#if TIDE_9999_0_OR_NEWER\n"
                                   "        nonsense(\n"
                                   "#else\n"
                                   "    c.n = A;\n"
                                   "#endif\n"
                                   "}\n";
    open_document(messy);
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]")); // What's left out isn't read
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));

    open_document(expected);
    const char *tokens = request("textDocument/semanticTokens/full");
    TIDE_CHECK(has(tokens, "1,0,3,11,0,0,4,17,15,0"));   // #if a keyword, its symbol a macro
    TIDE_CHECK(has(tokens, "1,3,22,14,0"));              // The line it leaves out a comment
    const char *folds = request("textDocument/foldingRange");
    TIDE_CHECK(has(folds, "{\"startLine\":1,\"endLine\":2,\"kind\":\"region\"}")); // #if to #else
    TIDE_CHECK(has(folds, "{\"startLine\":3,\"endLine\":4,\"kind\":\"region\"}")); // #else to #endif
    open_document("scene Main { }\n#if false\nthis is no$t code\n#endif\n");
    TIDE_CHECK(has(request("textDocument/completion"), "\"items\":[]"));
}

TIDE_TEST(lsp_folding)
{
    start();
    open_document("// One\n// Two\n// Three\ncomponent Body\n{\n    float x;\n}\nsystem Main()\n{\n    Spawn(Body);\n}\n");
    const char *folds = request("textDocument/foldingRange");
    TIDE_CHECK(has(folds, "{\"startLine\":0,\"endLine\":2,\"kind\":\"comment\"}"));
    TIDE_CHECK(has(folds, "{\"startLine\":3,\"endLine\":5}")); // From `component Body`, keeping `}` in sight
    TIDE_CHECK(has(folds, "{\"startLine\":7,\"endLine\":9}"));

    // Comments in /* */, lists of arguments and elements, and using lines
    open_document("using A;\nusing B;\n/* One\n   two */\nnamespace C;\nvoid Draw(int x,\n          int y)\n{\n}\n"
                  "void Call()\n{\n    Draw(\n        1,\n        2\n    );\n    List<int> xs = [\n        1,\n        2];\n}\n");
    folds = request("textDocument/foldingRange");
    TIDE_CHECK(has(folds, "{\"startLine\":0,\"endLine\":1,\"kind\":\"imports\"}"));
    TIDE_CHECK(has(folds, "{\"startLine\":2,\"endLine\":3,\"kind\":\"comment\"}"));
    TIDE_CHECK(has(folds, "{\"startLine\":5,\"endLine\":6}"));   // The parameters, with their `)`
    TIDE_CHECK(has(folds, "{\"startLine\":11,\"endLine\":13}")); // The arguments, keeping `);` in sight
    TIDE_CHECK(has(folds, "{\"startLine\":15,\"endLine\":17}")); // The list's elements
}

// Braceless bodies under loops keep their level, however deep; and an enum
// with a backing type gets its brace moved like any other.
TIDE_TEST(lsp_format_braceless_loops)
{
    start();
    static const char messy[] = "singleton F { List<int> xs; Grid2<int> cells; }\nscene Main { }\n"
                                "system S(mut F f)\n{\nwhile (f.xs.count > 3)\nif (f.xs.count > 4)\nreturn;\n"
                                "foreach (var x in f.xs)\nfor (var i = 0; i < x; i++)\nif (i > 2)\nreturn;\n"
                                "parallel (var at in f.cells)\nf.cells[at] = 1;\nreturn;\n}\n";
    static const char expected[] = "singleton F { List<int> xs; Grid2<int> cells; }\nscene Main { }\n"
                                   "system S(mut F f)\n{\n    while (f.xs.count > 3)\n        if (f.xs.count > 4)\n"
                                   "            return;\n    foreach (var x in f.xs)\n        for (var i = 0; i < x; i++)\n"
                                   "            if (i > 2)\n                return;\n    parallel (var at in f.cells)\n"
                                   "        f.cells[at] = 1;\n    return;\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));

    static const char backed[] = "enum Voxel : byte {\n    Air,\n    Stone,\n}\nenum Page {\n    Title,\n}\nscene Main { }\n";
    static const char braced[] = "enum Voxel : byte\n{\n    Air,\n    Stone,\n}\nenum Page\n{\n    Title,\n}\nscene Main { }\n";
    format_reply(backed);
    TIDE_CHECK(strcmp(apply_reply(backed, NULL), braced) == 0);
    TIDE_CHECK(has(format_reply(braced), "\"result\":[]"));
}

// A code action at a 0-based line.
static const char *actions_at(const int line)
{
    char range[160];
    snprintf(range, sizeof range, "\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,\"character\":0}}",
             line, line);
    return request_at("file:///test.tide", "textDocument/codeAction", line, 0, range);
}

TIDE_TEST(lsp_quick_fixes)
{
    start();
    open_document("struct Stats\n{\n    float health;\n    void Heal(float amount) { health += amount; }\n}\n"
                  "component Unit { Stats stats; Armor armor; }\n"
                  "void Bump(int count) { count += 1; }\n"
                  "event(Spawned) Setup(with Main) { var x = 1; x = 2; Spawn(Unit); Grow(x, 2.5); }\n"
                  "system Move(mut Velocity velocity) { }\n"
                  "scene Main { }\n");
    const char *method = actions_at(3);
    TIDE_CHECK(has(method, "Make 'Heal' mut") && has(method, "\"start\":{\"line\":3,\"character\":4}"));
    TIDE_CHECK(has(actions_at(5), "Create struct 'Armor'"));
    const char *param = actions_at(6);
    TIDE_CHECK(has(param, "Declare 'count' as mut") && has(param, "\"start\":{\"line\":6,\"character\":10}"));
    const char *main = actions_at(7);
    TIDE_CHECK(has(main, "Declare 'x' as mut") && has(main, "\"start\":{\"line\":7,\"character\":34}"));
    TIDE_CHECK(has(main, "Create function 'Grow'") && has(main, "\"newText\":\"\\nvoid Grow(int x, float value)\\n{\\n}\\n\""));
    TIDE_CHECK(has(main, "\"start\":{\"line\":10,\"character\":0}")); // At the end of the file
    TIDE_CHECK(has(actions_at(8), "Create component 'Velocity'"));
}

// The checker's "did you mean" is a quick fix, from where the name is, not
// from the message.
TIDE_TEST(lsp_quick_fixes_from_suggestions)
{
    start();
    static const char typo[] = GAME_TYPES "system Move(mut Body body)\n{\n    body.radus = 1;\n}\n";
    open_document(typo);
    TIDE_CHECK(has(last_sent(), "did you mean 'radius'?"));
    const char *fix = actions_at(24);
    TIDE_CHECK(has(fix, "\"title\":\"Change to 'radius'\",\"kind\":\"quickfix\",\"isPreferred\":true,\"diagnostics\":[{"));
    const json *reply = json_parse(last_sent(), strlen(last_sent()));
    const json *edits = json_path(json_get(reply, "result")->items[0], "edit", "changes", "file:///test.tide", NULL);
    char text[8192];
    snprintf(text, sizeof text, "%s", GAME_TYPES "system Move(mut Body body)\n{\n    body.radus = 1;\n}\n");
    apply_edits(text, sizeof text, edits);
    json_release();
    TIDE_CHECK(has(text, "    body.radius = 1;\n"));

    // One of the engine's: a member of Math
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.radius = Math.Clmp(body.radius, 0, 1);\n}\n");
    const char *math = actions_at(24);
    TIDE_CHECK(has(math, "Change to 'Clamp'"));
    TIDE_CHECK(has(math, "{\"range\":{\"start\":{\"line\":24,\"character\":23},\"end\":{\"line\":24,\"character\":27}},"
                         "\"newText\":\"Clamp\"}"));
    TIDE_CHECK(!has(actions_at(23), "Change to")); // Only on its line

    // A spelling that's gone: the error says what to write, and the fix writes it
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.position = Math.NormalizeSafe(body.position);\n}\n");
    TIDE_CHECK(has(last_sent(), "Math.NormalizeSafe is gone: write Math.Normalize"));
    TIDE_CHECK(has(last_sent(), "Math.Normalize no longer gives NaN"));
    const char *gone = actions_at(24);
    TIDE_CHECK(has(gone, "\"title\":\"Change to 'Normalize'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    TIDE_CHECK(has(gone, "{\"range\":{\"start\":{\"line\":24,\"character\":25},\"end\":{\"line\":24,\"character\":38}},"
                         "\"newText\":\"Normalize\"}"));
}

// `with ownership` and `without ownership`: a view's filter by this machine's
// player. `ownership` is a keyword right after them, and a name anywhere else.
TIDE_TEST(lsp_ownership)
{
    start();
    // Offered after `with` and `without` in a view, beside the components
    const char *with = complete(GAME_TYPES "view Mine(Body body, with $)\n{\n}\n");
    TIDE_CHECK(offers(with, "ownership"));
    TIDE_CHECK(offers(with, "Body"));
    TIDE_CHECK(has(with, "Entities this machine's player owns"));
    const char *without = complete(GAME_TYPES "view Rest(Body body, without $)\n{\n}\n");
    TIDE_CHECK(offers(without, "ownership"));
    TIDE_CHECK(has(without, "Entities this machine's player doesn't own"));
    // ...and nowhere else: the match has no machine of its own
    TIDE_CHECK(!offers(complete(GAME_TYPES "system Move(mut Body body, with $)\n{\n}\n"), "ownership"));
    TIDE_CHECK(!offers(complete(GAME_TYPES "event(Spawned) Grow(mut Body body, without $)\n{\n}\n"), "ownership"));
    TIDE_CHECK(!offers(complete(GAME_TYPES "view Mine($)\n{\n}\n"), "ownership"));

    open_document(GAME_TYPES "view Mine(Body body, with owner$ship)\n{\n    Draw2D.Circle(body.position, 1, Color.red);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "with ownership"));
    TIDE_CHECK(has(hover, "the entities this machine's player owns"));
    // A keyword (11), 11 columns after the parameter `body`
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), ",0,11,9,11,0,"));
    TIDE_CHECK(has(request("textDocument/prepareRename"), "'ownership' is a keyword after 'with' and 'without'"));
    // The view's own hover shows its filter
    open_document(GAME_TYPES "view Re$st(Body body, with Owner, without ownership)\n{\n    Draw2D.Circle(body.position, 1, Color.red);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "view Rest(Body body, with Owner, without ownership)"));

    // In the match it's an error that says what to write, which a quick fix does
    open_document(GAME_TYPES "system Move(mut Body body, with ownership)\n{\n    body.radius = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "the match runs the same on every machine, so a system can't filter by 'with ownership'"));
    TIDE_CHECK(has(last_sent(), "for entities that have any owner, write 'with Owner'"));
    const char *fix = actions_at(22);
    TIDE_CHECK(has(fix, "\"title\":\"Change to 'Owner'\",\"kind\":\"quickfix\""));
    TIDE_CHECK(has(fix, "{\"range\":{\"start\":{\"line\":22,\"character\":32},\"end\":{\"line\":22,\"character\":41}},"
                        "\"newText\":\"Owner\"}"));

    // The formatter keeps the filter as it is
    static const char messy[] = "component Body { float x; }\nscene Main { }\n"
                                "event(Spawned) Setup(with Main) { Spawn(Body, Owner); }\n"
                                "view Mine( Body body,with   ownership ){ Draw2D.Circle(float2(body.x, 0), 1, Color.red); }\n"
                                "view Rest(Body body ,without ownership) { Draw2D.Circle(float2(body.x, 0), 1, Color.red); }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(has(formatted, "view Mine(Body body, with ownership) { "));
    TIDE_CHECK(has(formatted, "view Rest(Body body, without ownership) { "));

    // Anywhere else `ownership` is a name: a singleton's, a field's, a parameter's
    open_document("local singleton ownership { int ownership = 3; }\nscene Main { }\n"
                  "int Twice(int ownership) { return ownership * 2; }\n"
                  "view Share(owner$ship ownership)\n{\n    if (Twice(ownership.ownership) > 1) Draw2D.Clear(Color.black);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "local singleton ownership"));
    const char *tokens = request("textDocument/semanticTokens/full");
    TIDE_CHECK(has(tokens, ",1,5,5,9,1,0,6,9,3,0,0,10,9,5,3,")); // Share, then a class (3) and a parameter (5)
}

// An error nothing handles: carry on with the default, or pass it on.
TIDE_TEST(lsp_quick_fixes_for_errors)
{
    start();
    open_document("enum ParseError { Empty }\nint Parse(string t) fails ParseError { return 1; }\n"
                  "int Twice(string t) fails ParseError\n{\n    Parse(t);\n    var n = Parse(t) + 1;\n    return n;\n}\n"
                  "scene Main { }\nsystem S()\n{\n    Parse(\"1\");\n}\n");
    const char *statement = actions_at(4);
    TIDE_CHECK(has(statement, "add '!'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    TIDE_CHECK(has(statement, "{\"range\":{\"start\":{\"line\":4,\"character\":12},\"end\":{\"line\":4,\"character\":12}},"
                              "\"newText\":\"!\"}"));
    TIDE_CHECK(has(statement, "Pass the error on to the caller of 'Twice': add 'try'"));
    TIDE_CHECK(has(statement, "{\"range\":{\"start\":{\"line\":4,\"character\":4},\"end\":{\"line\":4,\"character\":4}},"
                              "\"newText\":\"try \"}"));
    const char *value = actions_at(5);
    TIDE_CHECK(has(value, "\"newText\":\"!\"") && has(value, "{\"line\":5,\"character\":20}"));
    // A system has no caller to pass it to
    const char *system = actions_at(11);
    TIDE_CHECK(has(system, "add '!'") && !has(system, "add 'try'"));
}

// The document after the quick fix titled `title` among the last reply's code
// actions, or NULL when it isn't offered.
static const char *apply_fix(const char *original, const char *title)
{
    snprintf(applied, sizeof applied, "%s", original);
    const json *reply = json_parse(last_sent(), strlen(last_sent()));
    const json *actions = json_get(reply, "result");
    const char *fixed = NULL;
    for (int i = 0; actions && actions->kind == JSON_ARRAY && i < actions->count && !fixed; i++) {
        const char *named = json_str(json_get(actions->items[i], "title"));
        if (!named || strcmp(named, title) != 0) continue;
        const json *edits = json_path(actions->items[i], "edit", "changes", "file:///test.tide", NULL);
        if (!edits || edits->kind != JSON_ARRAY) continue;
        apply_edits(applied, sizeof applied, edits);
        fixed = applied;
    }
    json_release();
    return fixed;
}

// A local nothing reads: a warning that editors fade, with a fix that removes
// it, or keeps its call when the call does something.
TIDE_TEST(lsp_unused_locals)
{
    start();
    // Line 24 is the system's first statement
    static const char unused[] = GAME_TYPES "system Move(mut Body body, Time time)\n{\n"
                                            "    var move = Math.Normalize(body.position);\n"
                                            "    var fwd = move * 2;\n"
                                            "    body.position += move * time.dt;\n}\n";
    open_document(unused);
    TIDE_CHECK(has(last_sent(), "\"message\":\"local 'fwd' is never used\\nnote: nothing reads its value: remove it, or use it "
                                "where it was meant\",\"tags\":[1]"));
    TIDE_CHECK(has(last_sent(), "\"range\":{\"start\":{\"line\":25,\"character\":8},\"end\":{\"line\":25,\"character\":11}}"));
    TIDE_CHECK(count(last_sent(), "\"severity\":2") == 1); // Only fwd: move is read
    const char *remove = actions_at(25);
    TIDE_CHECK(has(remove, "\"title\":\"Remove 'fwd'\",\"kind\":\"quickfix\",\"isPreferred\":true,\"diagnostics\":[{"));
    const char *removed = apply_fix(unused, "Remove 'fwd'");
    TIDE_REQUIRE(removed != NULL);
    TIDE_CHECK(has(removed, "    var move = Math.Normalize(body.position);\n    body.position += move * time.dt;\n}\n"));
    TIDE_CHECK(!has(actions_at(24), "Remove '")); // Only on its line

    // A value that does something stays, as a statement of its own
    static const char spawns[] = GAME_TYPES "system Split(mut Body body)\n{\n"
                                            "    var half = Spawn(Body { radius = body.radius / 2 });\n"
                                            "    body.radius /= 2;\n}\n";
    open_document(spawns);
    TIDE_CHECK(has(last_sent(), "local 'half' is never used\\nnote: nothing reads its value, but 'Spawn(...)' does "
                                "something: keep the call as a statement of its own, without the local\",\"tags\":[1]"));
    const char *keep = actions_at(24);
    TIDE_CHECK(has(keep, "\"title\":\"Keep the call, without 'half'\"") && !has(keep, "Remove 'half'"));
    const char *kept = apply_fix(spawns, "Keep the call, without 'half'");
    TIDE_REQUIRE(kept != NULL);
    TIDE_CHECK(has(kept, "{\n    Spawn(Body { radius = body.radius / 2 });\n    body.radius /= 2;\n}\n"));

    // ...and with its type and `mut` written, they go with the name
    static const char typed[] = GAME_TYPES "int Bump(mut float radius) { radius += 1; return 1; }\n"
                                           "system Grow(mut Body body)\n{\n    mut int grown = Bump(body.radius);\n}\n";
    open_document(typed);
    actions_at(25);
    const char *bumped = apply_fix(typed, "Keep the call, without 'grown'");
    TIDE_REQUIRE(bumped != NULL);
    TIDE_CHECK(has(bumped, "{\n    Bump(body.radius);\n}\n"));

    // Beside other statements on its line, only it goes
    static const char shared[] = GAME_TYPES "system Move(mut Body body)\n{\n"
                                            "    var a = 1; var b = 2; // Two\n    body.radius = b;\n}\n";
    open_document(shared);
    actions_at(24);
    const char *alone = apply_fix(shared, "Remove 'a'");
    TIDE_REQUIRE(alone != NULL);
    TIDE_CHECK(has(alone, "{\n    var b = 2; // Two\n    body.radius = b;\n}\n"));

    // Assigned later, or declared by a for: faded, with what to do in the note and nothing to apply
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    mut var total = 0;\n    total += 1;\n"
                             "    for (var i = 0; body.radius < 3; body.radius += 1) { }\n}\n");
    TIDE_CHECK(has(last_sent(), "local 'total' is never used\\nnote: it's assigned, but nothing reads its value"));
    TIDE_CHECK(has(last_sent(), "local 'i' is never used"));
    TIDE_CHECK(count(last_sent(), "\"tags\":[1]") == 2);
    TIDE_CHECK(!has(actions_at(24), "Remove '") && !has(actions_at(24), "Keep the call"));
    TIDE_CHECK(!has(actions_at(26), "Remove '"));

    // The names a loop, an `is` and a Sort need aren't locals to remove
    open_document("component Bag { List<int> items; int count; }\nscene Main { }\n"
                  "event(Spawned) Setup(with Main) { Spawn(Bag); }\n"
                  "int? First(int count) { if (count == 0) return null; return count; }\n"
                  "system Count(mut Bag bag)\n{\n    foreach (var item in bag.items) bag.count += 1;\n"
                  "    if (First(bag.count) is int first) bag.count += 1;\n"
                  "    bag.items.Sort(a, b) { return false; }\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    // Errors hide uses: code that didn't check says nothing of what it reads, so
    // a local whose scope has one waits for it to be fixed
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    var speed = 2;\n    body.radus = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "Body has no field 'radus'"));
    TIDE_CHECK(!has(last_sent(), "is never used"));
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    var speed = 2;\n    body.radius = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "local 'speed' is never used"));
}

// Notes about another place go there, and access a system doesn't use is
// marked as unnecessary.
TIDE_TEST(lsp_diagnostic_notes_and_tags)
{
    static const char twice[] = "settings { tickRate = 30; }\nsettings { title = \"x\"; }\nscene Main { }\n";
    start_with("{\"capabilities\":{\"textDocument\":{\"publishDiagnostics\":{\"relatedInformation\":true}}}}");
    open_document(twice);
    TIDE_CHECK(has(last_sent(), "\"message\":\"a game has one 'settings' block\",\"relatedInformation\":[{\"location\":"
                                "{\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":0,\"character\":0}"));
    TIDE_CHECK(has(last_sent(), "\"message\":\"the other one is in /test.tide, on line 1\"}]"));
    TIDE_CHECK(!has(last_sent(), "\\nnote: the other one"));
    start(); // An editor that doesn't show them keeps them in the message
    open_document(twice);
    TIDE_CHECK(has(last_sent(), "a game has one 'settings' block\\nnote: the other one is in"));

    open_document("component Body { float2 position; }\nsingleton Score { int total; }\n"
                  "event(Spawned) Setup(with Main) { Spawn(Body); }\n"
                  "system Look(mut Body body, Score s) { if (body.position.x > 0) return; }\nscene Main { }\n");
    TIDE_CHECK(count(last_sent(), "\"tags\":[1]") == 2); // mut never written, and never used
}

TIDE_TEST(lsp_move_to_file)
{
    start();
    open_document("namespace Combat;\n\n// How much damage it takes.\ncomponent Health\n{\n    int value;\n}\n\n"
                  "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Health); }\n");
    const char *move = request_at("file:///test.tide", "textDocument/codeAction", 4, 0,
                                  "\"range\":{\"start\":{\"line\":4,\"character\":0},\"end\":{\"line\":4,\"character\":0}}");
    TIDE_CHECK(has(move, "\"title\":\"Move 'Health' to Health.tide\",\"kind\":\"refactor.move\""));
    TIDE_CHECK(has(move, "{\"kind\":\"create\",\"uri\":\"file:///Health.tide\""));
    // The new file: the namespace, then the declaration with its comment.
    TIDE_CHECK(has(move, "\"newText\":\"namespace Combat;\\n\\n// How much damage it takes.\\ncomponent Health\\n{\\n"
                         "    int value;\\n}\\n\""));
    // Removed here, with the blank line after it.
    TIDE_CHECK(has(move, "{\"range\":{\"start\":{\"line\":2,\"character\":0},\"end\":{\"line\":8,\"character\":0}},"
                         "\"newText\":\"\"}"));

    // Systems and handlers stay: files decide the order they run in.
    const char *system = request_at("file:///test.tide", "textDocument/codeAction", 9, 0,
                                    "\"range\":{\"start\":{\"line\":9,\"character\":0},\"end\":{\"line\":9,\"character\":0}}");
    TIDE_CHECK(!has(system, "Move '"));

    // An editor that can't create files isn't offered it.
    start_with("{}");
    open_document("component Health\n{\n    int value;\n}\nscene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Health); }\n");
    const char *unable = request_at("file:///test.tide", "textDocument/codeAction", 1, 0,
                                    "\"range\":{\"start\":{\"line\":1,\"character\":0},\"end\":{\"line\":1,\"character\":0}}");
    TIDE_CHECK(!has(unable, "Move '"));
}

#define USES_RADIUS                                                            \
    GAME_TYPES                                                                 \
    "system Grow(mut Body body)\n{\n    body.radius += 1;\n}\n"                  \
    "system Make()\n{\n    Spawn(Body { radius = 2 });\n}\n"

TIDE_TEST(lsp_references)
{
    start();
    open_document(USES_RADIUS "view V(Body body)\n{\n    Draw2D.Circle(body.position, body.rad$ius, Color.red);\n}\n");
    // The declaration, `body.radius += 1`, the literal and the view.
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 4);
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":false}"), "\"uri\"") == 3);

    open_document(GAME_TYPES "system Move(mut Body bo$dy)\n{\n    body.radius = body.radius * 2;\n}\n");
    const char *highlights = request("textDocument/documentHighlight");
    TIDE_CHECK(count(highlights, "\"range\"") == 3);
    TIDE_CHECK(count(highlights, "\"kind\":3") == 1); // The declaration

    // Uses in what a method returns.
    open_document("struct Range\n{\n    float l$o;\n    float hi;\n    float Width() { return hi - lo; }\n}\n");
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":false}"), "\"uri\"") == 1);
}

TIDE_TEST(lsp_rename)
{
    start();
    static const char program[] = USES_RADIUS "view V(Body body)\n{\n    Draw2D.Circle(body.position, body.radius, Color.red);\n}\n";
    open_document(USES_RADIUS "view V(Body body)\n{\n    Draw2D.Circle(body.position, body.rad$ius, Color.red);\n}\n");
    TIDE_CHECK(has(request("textDocument/prepareRename"), "\"result\":{\"start\""));
    request_with("textDocument/rename", "\"newName\":\"size\"");
    const char *renamed = apply_reply(program, "file:///test.tide");
    TIDE_CHECK(has(renamed, "float size = 10;"));
    TIDE_CHECK(has(renamed, "body.size += 1;"));
    TIDE_CHECK(has(renamed, "Body { size = 2 }"));
    TIDE_CHECK(has(renamed, "body.position, body.size,"));
    TIDE_CHECK(!has(renamed, "radius"));
    // The result still compiles cleanly.
    char copy[8192];
    snprintf(copy, sizeof copy, "%s", renamed);
    open_document(copy);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
}

// Main renames like anything else the game declares, though the program then
// has no entry point.
TIDE_TEST(lsp_rename_main)
{
    start();
    open_document("scene Ma$in { }\nevent(Spawned) Setup(with Main) { }\n");
    TIDE_CHECK(has(request("textDocument/prepareRename"), "\"result\":{\"start\""));
    request_with("textDocument/rename", "\"newName\":\"Menu\"");
    TIDE_CHECK(strcmp(apply_reply("scene Main { }\nevent(Spawned) Setup(with Main) { }\n", "file:///test.tide"),
                      "scene Menu { }\nevent(Spawned) Setup(with Menu) { }\n") == 0);
}

#define NAMESPACED                                                                                            \
    "component Health { int value; }\n"                                                                       \
    "scene Main { }\n"                                                                                        \
    "system Hurt(mut Game.Combat.Health health) { health.value -= 1; }\n"                                     \
    "event(Spawned) Setup(with Main) { Spawn(Game.Combat.Health { value = 3 }); Spawn(Game.Combat.Health); }\n"

// A namespace renames one part at a time, wherever its path is written.
TIDE_TEST(lsp_rename_namespace)
{
    start();
    open_document("namespace Game.Com$bat;\n" NAMESPACED);
    TIDE_CHECK(has(request("textDocument/prepareRename"), "\"result\":{\"start\""));
    request_with("textDocument/rename", "\"newName\":\"Fight\"");
    const char *renamed = apply_reply("namespace Game.Combat;\n" NAMESPACED, "file:///test.tide");
    TIDE_CHECK(strcmp(renamed, "namespace Game.Fight;\n"
                               "component Health { int value; }\n"
                               "scene Main { }\n"
                               "system Hurt(mut Game.Fight.Health health) { health.value -= 1; }\n"
                               "event(Spawned) Setup(with Main) { Spawn(Game.Fight.Health { value = 3 }); "
                               "Spawn(Game.Fight.Health); }\n") == 0);
    char copy[8192];
    snprintf(copy, sizeof copy, "%s", renamed);
    open_document(copy);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    // The outer part, from code.
    open_document("namespace Game.Combat;\n"
                  "component Health { int value; }\n"
                  "scene Main { }\n"
                  "system Hurt(mut Game.Combat.Health health) { health.value -= 1; }\n"
                  "event(Spawned) Setup(with Main) { Spawn(Game.Combat.Health { value = 3 }); Spawn(Ga$me.Combat.Health); }\n");
    request_with("textDocument/rename", "\"newName\":\"Play\"");
    renamed = apply_reply("namespace Game.Combat;\n" NAMESPACED, "file:///test.tide");
    TIDE_CHECK(count(renamed, "Play.Combat") == 4 && !has(renamed, "Game"));
}

TIDE_TEST(lsp_rename_refusals)
{
    start();
    open_document(USES_RADIUS "view V(Body body)\n{\n    Draw2D.Circle(body.position, body.rad$ius, Color.red);\n}\n");
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"position\""), "'position' is already declared"));
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"return\""), "keyword"));
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"float3\""), "built into the language"));
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"2fast\""), "start with a letter"));

    open_document(USES_RADIUS "view V(Body body)\n{\n    Draw2D.Cir$cle(body.position, body.radius, Color.red);\n}\n");
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Built-in names can't be renamed"));

    open_document(GAME_TYPES "system Mo$ve(mut Body body)\n{\n    body.radius = ;\n}\n");
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Fix the syntax errors first"));
}

#define BUILT_IN_METHODS                                                                                    \
    "singleton Field { Grid2<int> cells = Grid2(8, 8); List<int> items; string name; }\n"                   \
    "singleton Camera { float2 center; }\ncomponent Body { float x; }\nscene Main { }\n"

// Built-in methods are what they're called on's: a list's Clear isn't an
// entity's Destroy. Hovers, signatures and references say so.
TIDE_TEST(lsp_built_in_methods)
{
    start();
    open_document(BUILT_IN_METHODS "system S(mut Field field)\n{\n    field.items.Cl$ear();\n}\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    const char *clear = request("textDocument/hover");
    TIDE_CHECK(has(clear, "List<int>.Clear()") && has(clear, "Removes every element.") && !has(clear, "Destroys"));
    open_document(BUILT_IN_METHODS "system S(mut Field field)\n{\n    var has = field.items.Conta$ins(3);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "List<int>.Contains(int item) -> bool"));
    open_document(BUILT_IN_METHODS "system S(mut Field field)\n{\n    field.cells.Cl$ear();\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Grid2<int>.Clear()"));
    open_document(BUILT_IN_METHODS "system S(mut Body body)\n{\n    this.Sn$ap();\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "entity.Snap()"));
    open_document(BUILT_IN_METHODS "system S(mut Camera camera)\n{\n    camera.Sn$ap();\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "singleton.Snap()"));
    open_document(BUILT_IN_METHODS "system S(mut Body body)\n{\n    this.Dest$roy();\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "entity.Destroy()"));
    open_document(BUILT_IN_METHODS "system S(Field field)\n{\n    var n = field.name.len$gth;\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "How many characters the text has."));
    // A grid made in a default: a type, as constructors are
    open_document("singleton Field { Grid2<int> cells = Gr$id2(8, 8); }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Grid2<int>"));

    // Signatures, by what the call is on
    open_document(BUILT_IN_METHODS "system S(mut Field field)\n{\n    field.items.Insert($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "List<int>.Insert(int index, int item)"));
    open_document(BUILT_IN_METHODS "system S(mut Field field)\n{\n    field.items.Add($\n}\n");
    const char *add = request("textDocument/signatureHelp");
    TIDE_CHECK(has(add, "List<int>.Add(int item)") && !has(add, "components"));
    open_document(BUILT_IN_METHODS "system S(mut Body body)\n{\n    this.Add($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "entity.Add(components...)"));
    open_document(BUILT_IN_METHODS "system S(Field field)\n{\n    var t = field.name.Substring(1, $\n}\n");
    const char *substring = request("textDocument/signatureHelp");
    TIDE_CHECK(has(substring, "string.Substring(int start, int length) -> string") && has(substring, "\"activeSignature\":1"));
    open_document("scene Main { }\nview V()\n{\n    var t = \"cat\".ToUpper($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "string.ToUpper()"));
    open_document("scene Main { }\nview V()\n{\n    Clipboard.Copy($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Clipboard.Copy(string text)"));
    open_document("singleton Field { Grid2<int> cells = Grid2(8, $ }\nscene Main { }\n");
    const char *grid = request("textDocument/signatureHelp");
    TIDE_CHECK(has(grid, "Grid2(int width, int height)") && has(grid, "\"activeParameter\":1"));
    open_document("scene Main { }\nview V()\n{\n    Draw2D.Text(\"a\", $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Draw2D.Text(string text, float2 position, float size, Color color)"));

    // References: a grid's Clear, not a list's; and Snap and Grid2 are found at all
    static const char program[] = BUILT_IN_METHODS "system S(mut Field field, with Body)\n{\n    field.cells.Clear();\n"
                                  "    field.items.Clear();\n    field.cells.Clear();\n    this.Snap();\n}\n";
    open_document(program);
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 6, 17,
                                "\"context\":{\"includeDeclaration\":true}"),
                     "\"uri\"") == 2);
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 7, 17,
                                "\"context\":{\"includeDeclaration\":true}"),
                     "\"uri\"") == 1);
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 9, 10, ""), "entity.Snap()"));
}

// Inlay hints name the parameters literal arguments go to, the built-ins' too,
// but for one-letter names, which only say where the argument is.
TIDE_TEST(lsp_inlay_hints_for_built_ins)
{
    start();
    open_document("local singleton Menu { float volume; }\nscene Main { }\n"
                  "view V(mut Menu menu)\n{\n    Draw2D.Circle(float2(1, 2), 3, Color.red);\n"
                  "    menu.volume = Math.Clamp(0.5, 0, menu.volume);\n"
                  "    GUILayout.Slider(\"Volume\", menu.volume, 0, 1);\n    Session.Open(7777);\n"
                  "    var c = Color(1, 0.5, 0);\n}\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":20,\"character\":0}}");
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":4,\"character\":32},\"label\":\"radius:\",\"kind\":2"));
    TIDE_CHECK(!has(hints, "\"label\":\"center:\"") && !has(hints, "\"label\":\"color:\"")); // Not literals
    TIDE_CHECK(has(hints, "\"label\":\"label:\"") && has(hints, "\"label\":\"min:\"") && has(hints, "\"label\":\"max:\""));
    TIDE_CHECK(has(hints, "\"label\":\"port:\""));
    // float2(1, 2), Math.Clamp(x, a, b) and Color(r, g, b)
    TIDE_CHECK(!has(hints, "\"label\":\"x:\"") && !has(hints, "\"label\":\"a:\"") && !has(hints, "\"label\":\"r:\""));
}

// A grid's cells[x, y] is cells[int2(x, y)] to the checker: x is still x.
TIDE_TEST(lsp_grid_positions_keep_their_names)
{
    start();
    open_document("singleton Field { Grid2<int> cells; }\nscene Main { }\n"
                  "system S(mut Field field)\n{\n    var x$ = 1;\n    field.cells[x, 2] = x;\n}\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 3);
    request_with("textDocument/rename", "\"newName\":\"column\"");
    TIDE_CHECK(has(apply_reply("singleton Field { Grid2<int> cells; }\nscene Main { }\n"
                               "system S(mut Field field)\n{\n    var x = 1;\n    field.cells[x, 2] = x;\n}\n",
                               "file:///test.tide"),
                   "field.cells[column, 2] = column;"));
}

// Go to definition on an enum's member, and hovers that had none or were stale.
TIDE_TEST(lsp_enum_members_and_hovers)
{
    start();
    open_document("enum Page { Title, Options }\nsingleton Menu { Page page; }\nscene Main { }\n"
                  "system S(mut Menu menu) { menu.page = Page.Opt$ions; }\n");
    TIDE_CHECK(has(request("textDocument/definition"), "\"range\":{\"start\":{\"line\":0,\"character\":19}"));
    // The engine's enums have no source to go to, and keep their names.
    open_document("scene Arena { }\nscene Main { }\n"
                  "event(Spawned) Setup(with Main) { Scene.Load(Arena, SceneVisibility.Priv$ate); }\n");
    TIDE_CHECK(has(request("textDocument/definition"), "\"result\":null"));
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Members of the engine's enums can't be renamed."));

    // Session's calls, as the checker has them
    open_document("local scene Main { }\nview Menu()\n{\n    Sess$ion.Leave();\n}\n");
    const char *session = request("textDocument/hover");
    TIDE_CHECK(has(session, "Start, Open, Close, Kick, KickAll, Join, Connect, Leave and End") && !has(session, "Play"));
    open_document("[Native$Name(\"c_noise\")]\nextern float Noise(float x);\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "The C function the extern function after it calls"));
    open_document("enum Voxel : by$te { Air, Stone }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "one byte, so its members go from 0 to 255"));
    // byte reads as a keyword (11), like int
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), "\"data\":[0,5,5,13,1,0,8,4,11,0,"));
}

// Renaming refuses every keyword, the lexer's and the parser's.
TIDE_TEST(lsp_rename_keywords)
{
    start();
    open_document(GAME_TYPES "system Mo$ve(mut Body body)\n{\n    body.radius = 1;\n}\n");
    static const char *const keywords[] = {"while", "for", "foreach", "parallel", "continue", "this", "await", "null",
                                           "struct", "event", "const", "async", "fails", "in", "void"};
    for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        char extra[64];
        snprintf(extra, sizeof extra, "\"newName\":\"%s\"", keywords[i]);
        TIDE_CHECK(has(request_with("textDocument/rename", extra), "keyword"));
    }
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"string\""), "built into the language"));
    // A local can take a word that only starts declarations, as the parser allows.
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    var spe$ed = 1;\n    body.radius = speed;\n}\n");
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"scene\""), "\"changes\""));
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"while\""), "keyword"));
}

// The input's Sample and Sanitize are in the outline.
TIDE_TEST(lsp_outline_of_input)
{
    start();
    open_document("local singleton Menu { bool open; }\ninput Keys\n{\n    bool fire;\n\n    Sample(Menu menu) { }\n"
                  "    Sanitize() { }\n}\nscene Main { }\n");
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "{\"name\":\"Sample\",\"detail\":\"Sample(Menu menu)\",\"kind\":6,\"range\":{\"start\":{\"line\":5,"
                            "\"character\":4},\"end\":{\"line\":5,\"character\":25}}"));
    TIDE_CHECK(has(symbols, "\"name\":\"Sanitize\",\"detail\":\"Sanitize()\",\"kind\":6"));
}

// Highlights tell what an assignment changes from what it reads.
TIDE_TEST(lsp_highlights_writes)
{
    start();
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    mut var n$ = 1;\n    n = 2;\n    n += body.radius;\n"
                  "    n++;\n    body.radius = n;\n}\n");
    const char *local = request("textDocument/documentHighlight");
    TIDE_CHECK(count(local, "\"kind\":3") == 4); // Declared, set, added to, incremented
    TIDE_CHECK(count(local, "\"kind\":2") == 1); // Read into body.radius
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.rad$ius = 1;\n    var r = body.radius;\n}\n");
    const char *field = request("textDocument/documentHighlight");
    TIDE_CHECK(count(field, "\"kind\":3") == 2 && count(field, "\"kind\":2") == 1); // Its declaration is in the file too
}

TIDE_TEST(lsp_signature_help)
{
    start();
    open_document(GAME_TYPES "view V(Body body)\n{\n    Draw2D.Circle(body.position, $\n}\n");
    const char *circle = request("textDocument/signatureHelp");
    TIDE_CHECK(has(circle, "\"label\":\"Draw2D.Circle(float2 center, float radius, Color color)\""));
    TIDE_CHECK(has(circle, "\"activeParameter\":1"));
    // "Draw2D.Circle(" is 14 characters: `float2 center` is 14 to 27.
    TIDE_CHECK(has(circle, "\"parameters\":[{\"label\":[14,27]}"));

    open_document(GAME_TYPES "system S()\n{\n    var v = float3(float2(1, 2), $\n}\n");
    const char *vector = request("textDocument/signatureHelp");
    TIDE_CHECK(has(vector, "float3(float2 xy, float z)"));
    TIDE_CHECK(has(vector, "\"activeSignature\":0,\"activeParameter\":1"));

    // Commas inside a component literal don't count.
    open_document(GAME_TYPES "system S()\n{\n    Spawn(Body { position = float2(1, 2), radius = 3 }, $\n}\n");
    const char *spawn = request("textDocument/signatureHelp");
    TIDE_CHECK(has(spawn, "Spawn(components...)"));
    TIDE_CHECK(has(spawn, "\"activeParameter\":0"));

    open_document(GAME_TYPES "system S()\n{\n    $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"result\":null"));
}

static const char *format_reply(const char *text)
{
    open_document(text);
    return request_with("textDocument/formatting", "\"options\":{\"tabSize\":4,\"insertSpaces\":true}");
}

TIDE_TEST(lsp_format)
{
    start();
    static const char messy[] =
        "\n\ncomponent Body\n{\nfloat2 position;   // Where it is\n  float radius=10;\n}\n\n\n\n"
        "system Move( mut Body body,Time time )\n{\nif(body.radius>1)\nbody.radius-=1;\n"
        "  else if (body.radius <-5) { body.radius = -body.radius *2; }\n"
        "    var p = float2(1,-2)+body.position;\n"
        "var s=body.radius>1?-1:0;\n"
        "Spawn(Body{position=p},\nOwner);\n\n}\n"
        "/* a\n   block */\n"
        "system Main( ) { }";
    static const char expected[] =
        "component Body\n{\n    float2 position;   // Where it is\n    float radius = 10;\n}\n\n"
        "system Move(mut Body body, Time time)\n{\n    if (body.radius > 1)\n        body.radius -= 1;\n"
        "    else if (body.radius < -5) { body.radius = -body.radius * 2; }\n"
        "    var p = float2(1, -2) + body.position;\n"
        "    var s = body.radius > 1 ? -1 : 0;\n"
        "    Spawn(Body { position = p },\n        Owner);\n\n}\n"
        "/* a\n   block */\n"
        "system Main() { }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);

    // Formatting what's formatted changes nothing.
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));

    // Continuation lines go one level deeper, or keep deeper alignment.
    static const char continued[] =
        "system Main()\n{\n    var x = 1\n    + 2;\n    Spawn(Owner,\n          Owner);\n}\n";
    format_reply(continued);
    TIDE_CHECK(strcmp(apply_reply(continued, NULL),
                      "system Main()\n{\n    var x = 1\n        + 2;\n    Spawn(Owner,\n          Owner);\n}\n") == 0);
}

// Selection grows from the word at the cursor through what holds it, out to
// its declaration.
TIDE_TEST(lsp_selection_ranges)
{
    start();
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    if (body.radius > 1)\n    {\n"
                  "        body.radius = Math.Max(body.ra$dius * 2, 1);\n    }\n}\n");
    const char *ranges = request_with("textDocument/selectionRange", "\"positions\":[{\"line\":26,\"character\":38}]");
    TIDE_CHECK(has(ranges, "\"result\":[{\"range\":{\"start\":{\"line\":26,\"character\":36},\"end\":{\"line\":26,\"character\":42}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":26,\"character\":31},\"end\":{\"line\":26,\"character\":42}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":26,\"character\":31},\"end\":{\"line\":26,\"character\":46}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":26,\"character\":22},\"end\":{\"line\":26,\"character\":50}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":26,\"character\":8},\"end\":{\"line\":26,\"character\":51}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":25,\"character\":4},\"end\":{\"line\":27,\"character\":5}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":24,\"character\":4},\"end\":{\"line\":27,\"character\":5}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":23,\"character\":0},\"end\":{\"line\":28,\"character\":1}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":22,\"character\":0},\"end\":{\"line\":28,\"character\":1}}}}}}}}}}}]"));
    if (!has(ranges, "\"parent\":{\"range\":{\"start\":{\"line\":22,")) printf("%s\n", ranges);
    // Every position gets one, even outside the code
    const char *two = request_with("textDocument/selectionRange", "\"positions\":[{\"line\":0,\"character\":0},{\"line\":30,\"character\":0}]");
    TIDE_CHECK(count(two, "{\"range\":") == 3); // `component` in its declaration, and the empty end
}

// Formatting a range changes its lines only, as formatting everything would.
TIDE_TEST(lsp_range_formatting)
{
    start();
    TIDE_CHECK(has(last_sent(), "\"documentRangeFormattingProvider\":true"));
    static const char messy[] = "scene Main { }\nsystem S()\n{\nmut var n = 0;\nif (n > 0)\nn+=1;\nn+=2;\n}\n";
    open_document(messy);
    request_with("textDocument/rangeFormatting", "\"range\":{\"start\":{\"line\":5,\"character\":0},\"end\":"
                                                 "{\"line\":6,\"character\":0}},\"options\":{\"tabSize\":4,\"insertSpaces\":true}");
    const char *formatted = apply_reply(messy, NULL);
    // Its indentation as the lines before it make it, though they stay as they are
    TIDE_CHECK(strcmp(formatted, "scene Main { }\nsystem S()\n{\nmut var n = 0;\nif (n > 0)\n        n += 1;\nn+=2;\n}\n") == 0);
    if (strcmp(formatted, "scene Main { }\nsystem S()\n{\nmut var n = 0;\nif (n > 0)\n        n += 1;\nn+=2;\n}\n") != 0) {
        printf("--- got:\n%s---\n", formatted);
    }
}

// C#-style braces: a block that spans lines has its braces on lines of their
// own. Actions on one line and literals stay as they are.
TIDE_TEST(lsp_format_braces)
{
    start();
    static const char js[] =
        "component Body {\n    float2 position;\n}\n"
        "input Keys {\n    bool fire;\n\n    Sample() {\n        fire = Devices.keyboard.space.pressed; }\n}\n"
        "system Move(mut Body body) { // Every body\n"
        "    if (body.position.x > 1) {\n        body.position.x = 0;\n    } else {\n        body.position.x += 1;\n    }\n"
        "    if (body.position.y > 1) { return; } else {\n        body.position.y = 0;\n    }\n"
        "    if (body.position.y < 0) {return;}\n"
        "    var b = Spawn(Body { position = float2(1) });\n"
        "    Spawn(Body {\n        position = float2(2)\n    });\n"
        "}\n"
        "system Main() { }\n";
    static const char expected[] =
        "component Body\n{\n    float2 position;\n}\n"
        "input Keys\n{\n    bool fire;\n\n    Sample()\n    {\n        fire = Devices.keyboard.space.pressed;\n    }\n}\n"
        "system Move(mut Body body)\n{ // Every body\n"
        "    if (body.position.x > 1)\n    {\n        body.position.x = 0;\n    }\n    else\n    {\n        body.position.x += 1;\n    }\n"
        "    if (body.position.y > 1) { return; }\n    else\n    {\n        body.position.y = 0;\n    }\n"
        "    if (body.position.y < 0) { return; }\n"
        "    var b = Spawn(Body { position = float2(1) });\n"
        "    Spawn(Body {\n        position = float2(2)\n    });\n" // A literal's fields: one level in, even in a call
        "}\n"
        "system Main() { }\n";
    format_reply(js);
    const char *formatted = apply_reply(js, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);

    // Formatting what's formatted changes nothing.
    char again[8192];
    snprintf(again, sizeof again, "%s", formatted);
    TIDE_CHECK(has(format_reply(again), "\"result\":[]"));

    // New lines match the file's.
    static const char crlf[] = "system Main() {\r\n    return;\r\n}\r\n";
    format_reply(crlf);
    TIDE_CHECK(strcmp(apply_reply(crlf, NULL), "system Main()\r\n{\r\n    return;\r\n}\r\n") == 0);
}

// Formatting keeps every token, in order: only whitespace changes.
TIDE_TEST(lsp_format_keeps_tokens)
{
    start();
    static const char program[] =
        GAME_TYPES
        "system Move(PlayerInput input, Time time, mut Body body)\n{\n"
        "      mut var speed=Math.Length(body.position.xy)*2;\n"
        "  if (input.fire.down&&speed<10) { body.position+=input.move*time.dt; }\n"
        "else body.radius=Math.Clamp(body.radius,1,-2) ;\n"
        "    var e=Spawn(Body{position=float2(1,2)});e.Destroy();\n}\n"
        "view DrawBody(Body body, Arena arena)\n{\n"
        "Draw2D.Text(\"a  b   \\\"c\\\"\", body.position,12,Color(1,0.5,0));\n}\n";
    format_reply(program);
    char formatted[8192];
    snprintf(formatted, sizeof formatted, "%s", apply_reply(program, NULL));
    TIDE_CHECK(strcmp(formatted, program) != 0);

    // Same text with all whitespace removed.
    char a[8192];
    char b[8192];
    size_t na = 0;
    size_t nb = 0;
    for (const char *p = program; *p; p++) if (*p != ' ' && *p != '\n') a[na++] = *p;
    for (const char *p = formatted; *p; p++) if (*p != ' ' && *p != '\n') b[nb++] = *p;
    a[na] = b[nb] = '\0';
    TIDE_CHECK(strcmp(a, b) == 0);
    TIDE_CHECK(has(formatted, "\"a  b   \\\"c\\\"\"")); // Text keeps its spaces

    // It still compiles as before, and formatting again changes nothing.
    open_document(formatted);
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(has(format_reply(formatted), "\"result\":[]"));
}

// Types `program` out, from its first `from` characters on: every prefix is
// analysed and queried, as an editor does on each keystroke.
static void type_out(const char *program, const size_t from)
{
    char text[8192];
    const size_t size = strlen(program);
    for (size_t n = from; n <= size && n < sizeof text; n++) {
        memcpy(text, program, n);
        text[n] = '\0';
        open_document(text);
        int line = 0;
        int character = 0;
        for (size_t i = 0; i < n; i++) {
            if (text[i] == '\n') {
                line++;
                character = 0;
            } else {
                character++;
            }
        }
        cursor_line = line;
        cursor_character = character;
        request("textDocument/completion");
        request("textDocument/hover");
        request("textDocument/semanticTokens/full");
        request("textDocument/signatureHelp");
        request("textDocument/documentHighlight");
        request("textDocument/typeDefinition");
        char positions[96];
        snprintf(positions, sizeof positions, "\"positions\":[{\"line\":%d,\"character\":%d}]", line, character);
        request_with("textDocument/selectionRange", positions);
        if (n % 16 == 0) {
            request("textDocument/documentSymbol");
            request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}");
            request_with("textDocument/rename", "\"newName\":\"renamed\"");
            request_with("textDocument/formatting", "\"options\":{\"tabSize\":4,\"insertSpaces\":true}");
            request_with("textDocument/rangeFormatting", "\"range\":{\"start\":{\"line\":3,\"character\":0},\"end\":"
                                                         "{\"line\":9,\"character\":0}},\"options\":{\"tabSize\":4}");
            request("textDocument/implementation");
            request("textDocument/prepareCallHierarchy");
            request("textDocument/foldingRange");
            char range[160];
            snprintf(range, sizeof range, "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":%d,\"character\":0}}",
                     line + 1);
            request_with("textDocument/codeAction", range);
            request_with("textDocument/inlayHint", range);
        }
    }
}

// Typing a program from scratch: nothing may crash, whatever state it's in.
TIDE_TEST(lsp_every_prefix_is_safe)
{
    static const char program[] =
        GAME_TYPES
        "struct Range\n{\n    float lo;\n    float hi = 1;\n\n    float Width() { return hi - lo; }\n"
        "    mut void Scale(float by) { lo *= by; hi *= by; }\n}\n"
        "float Grow(mut Range range, float by)\n{\n    range.Scale(by);\n    return range.Width();\n}\n"
        "[NativeName(\"c_noise\")]\nextern float Noise(float x, mut Range range);\n"
        "input Controls\n{\n    float2 aim;\n\n    Sample()\n    {\n"
        "        var keys = Devices.keyboard;\n        if (keys.w.pressed) aim.y += 1;\n    }\n}\n"
        "system Move(Controls controls, Devices devices, Time time, mut Body body, without Arena)\n{\n"
        "    if (devices.gamepad.buttonSouth.down) body.radius += 1;\n"
        "    mut var speed = Math.Length(body.position.xy) * 2;\n"
        "    if (controls.aim.x > 0 && speed < 10) { body.position += controls.aim * time.dt; }\n"
        "    else body.radius = Math.Clamp(body.radius, 1, 2);\n"
        "    var e = Spawn(Body { position = float2(1, 2) });\n    e.Send(Hit { damage = 2 });\n    e.Destroy();\n}\n"
        "event Hit\n{\n    int damage = 1;\n}\n"
        "event(Hit hit) TakeHit(mut Body body)\n{\n    body.radius -= hit.damage;\n}\n"
        "view DrawBody(Body body, Arena arena)\n{\n"
        "    Draw2D.Text(\"hi \\\"there\\\"\", body.position, 12, Color(1, 0.5, 0));\n"
        "    Draw2D.Circle(body.position, body.radius, Color.red);\n}\n"
        "enum ParseError { Empty }\n"
        "int Parse(string t) fails ParseError\n{\n    if (t == \"\") fail ParseError.Empty;\n    return 1;\n}\n"
        "int? Find(int x)\n{\n    if (x > 0) return x;\n    return null;\n}\n"
        "int Twice(string t) fails ParseError\n{\n    var n = try Parse(t);\n"
        "    if (Parse(t) is int m && m > 0) return m;\n    return (Find(n) ?? 0) + Parse(t)!;\n}\n"
        "singleton Field { Grid2<int> cells = Grid2(8, 8); List<int> items; }\n"
        "system Fill(mut Field field)\n{\n    foreach (var i in field.items) field.items.Add(i);\n"
        "    parallel (var at in field.cells) field.cells[at] = 1;\n"
        "    field.cells[1, 2] = field.items.IndexOf($$\"{3}\".length);\n    field.cells.Clear();\n}\n"
        "component Aim { Entity target; }\n"
        "system Track(Aim aim, mut World world)\n{\n    if (!world.Has<Body>(aim.target)) return;\n"
        "    mut var b = world.Get<Body>(aim.target);\n    b.radius = b.radius < 2 ? 2 : 1;\n"
        "    world.Set<Body>(aim.target, b);\n}\n";
    start();
    type_out(program, 0);
    TIDE_CHECK(has(last_sent(), "\"result\""));
}

// The same for frags, verts and the view that draws through them, after the
// types they use; and once they're whole, asking at every place in them.
TIDE_TEST(lsp_shaders_every_prefix_is_safe)
{
    start();
    type_out(SHADERS, strlen(SHADER_TYPES));
    TIDE_CHECK(has(last_sent(), "\"result\""));

    static const char *const methods[] = {"textDocument/completion", "textDocument/hover", "textDocument/signatureHelp",
                                          "textDocument/definition", "textDocument/typeDefinition",
                                          "textDocument/documentHighlight", "textDocument/prepareCallHierarchy",
                                          "textDocument/prepareRename"};
    static const char program[] = SHADERS;
    open_document(program);
    cursor_line = cursor_character = 0;
    for (size_t n = 0; program[n]; n++) {
        for (size_t i = 0; n >= strlen(SHADER_TYPES) && i < sizeof methods / sizeof methods[0]; i++) {
            const char *reply = request(methods[i]);
            TIDE_CHECK(has(reply, "\"result\"") || has(reply, "\"error\""));
        }
        cursor_character = program[n] == '\n' ? 0 : cursor_character + 1;
        if (program[n] == '\n') cursor_line++;
    }
}

TIDE_TEST(lsp_render_targets_texture_members_and_blit)
{
    start();
    static const char types[] = "scene Main { }\nlocal singleton Buffers { RenderTarget scene; RenderTarget scratch; }\n";
    char program[2048];
    snprintf(program, sizeof program, "%sview V(Buffers buffers)\n{\n    buffers.scene.$\n}\n", types);
    const char *members = complete(program);
    TIDE_CHECK(offers(members, "color") && offers(members, "Clear") && offers(members, "Mesh"));
    TIDE_CHECK(offers(members, "Resize") && offers(members, "Dispose"));
    TIDE_CHECK(!offers(members, "list") && !offers(members, "Blit"));
    TIDE_CHECK(offers(complete("scene Main { }\nview V() { Draw.window.$ }\n"), "Camera"));
    TIDE_CHECK(offers(complete("scene Main { }\nview V() { Draw.$ }\n"), "Blit"));
    TIDE_CHECK(offers(complete("scene Main { }\nfrag Copy(Vertex v, mut Color color, $"), "Texture"));
    TIDE_CHECK(offers(complete("scene Main { }\nfrag Copy(Vertex v, mut Color color, Texture source)\n"
                              "{\n    color = source.$\n}\n"), "Sample"));
    snprintf(program, sizeof program, "%sfrag Copy(Vertex v, mut Color color, Texture source)\n"
                                      "{ color = source.Sample(v.uv); }\n"
                                      "view V(Buffers buffers)\n{\n    Draw.Blit(buffers.scene.color, $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "RenderTarget destination"));
    TIDE_CHECK(has(last_sent(), "\"activeParameter\":1"));
    snprintf(program, sizeof program, "%sview V(Buffers buffers)\n{\n    buffers.scene.Cl$ear(Color.black);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "Draw2D.Clear"));
    snprintf(program, sizeof program, "%sview V(Buffers buffers)\n{\n    buffers.scene.Mesh([], [], $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "[, texture[, filter]]"));
    snprintf(program, sizeof program, "%sview V(Buffers buffers)\n{\n    buffers.scene.Resize(16, $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Resize(int width, int height)"));
    snprintf(program, sizeof program, "%sview V()\n{\n    var scene = RenderTarget(16, $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "RenderTarget(int width, int height)"));
    snprintf(program, sizeof program, "%sview V(Buffers buffers)\n{\n    buffers.scene.Dis$pose();\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "All target and color aliases"));
}

// ---------------------------------------------------------------------------
// A game of several files, found through the manifest tide_add_game writes

static char game_dir[512];

static void game_path(char *out, const size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", game_dir, name);
}

static void write_file(const char *name, const char *text)
{
    char path[640];
    game_path(path, sizeof path, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void open_uri(const char *uri, const char *text)
{
    jbuf b = {0};
    jb_put(&b, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":");
    jb_string(&b, uri);
    jb_put(&b, ",\"languageId\":\"tide\",\"version\":1,\"text\":");
    jb_string(&b, text);
    jb_put(&b, "}}}");
    clear_sent();
    handle(b.data);
    jb_free(&b);
}

static void close_uri(const char *uri)
{
    jbuf b = {0};
    jb_put(&b, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":{\"uri\":");
    jb_string(&b, uri);
    jb_put(&b, "}}}");
    clear_sent();
    handle(b.data);
    jb_free(&b);
}

static const char *request_at(const char *uri, const char *method, const int line, const int character,
                              const char *extra)
{
    char message[2048];
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"%s\",\"params\":{\"textDocument\":{\"uri\":\"%s\"},"
             "\"position\":{\"line\":%d,\"character\":%d}%s%s}}",
             method, uri, line, character, extra[0] ? "," : "", extra);
    clear_sent();
    handle(message);
    return last_sent();
}

// Sets game_dir to the working directory, with forward slashes.
static bool find_game_dir(void)
{
    char buffer[512] = "";
#ifdef _WIN32
    if (!_getcwd(buffer, sizeof buffer)) return false;
#else
    if (!getcwd(buffer, sizeof buffer)) return false;
#endif
    for (char *p = buffer; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    snprintf(game_dir, sizeof game_dir, "%s", buffer);
    return true;
}

TIDE_TEST(lsp_game_of_several_files)
{
    if (!find_game_dir()) return;

    static const char physics[] = "namespace Physics;\n"
                                  "component Body { float2 position; }\n"
                                  "system Gravity(mut Body body) { body.position.y -= 1; }\n";
    static const char main_file[] = "using Physics;\n"
                                    "event(Spawned) Setup(with Main) { Spawn(Body); }\n"
                                    "[After(Physics.Gravity)]\n"
                                    "system Move(mut Body body) { body.position.x += 1; }\n"
                                    "scene Main { }\n";
    write_file("lsp_game_physics.tide", physics);
    write_file("lsp_game_main.tide", main_file);
    char a[640], b[640], manifest[640], manifest_text[1400], uri_a[700], uri_b[700];
    game_path(a, sizeof a, "lsp_game_physics.tide");
    game_path(b, sizeof b, "lsp_game_main.tide");
    game_path(manifest, sizeof manifest, "lsp_game_manifest.txt");
    snprintf(manifest_text, sizeof manifest_text, "other\t%s/elsewhere.tide\ngame\t%s\ngame\t%s\n", game_dir, a, b);
    write_file("lsp_game_manifest.txt", manifest_text);
    snprintf(uri_a, sizeof uri_a, "file:///%s", a[0] == '/' ? a + 1 : a);
    snprintf(uri_b, sizeof uri_b, "file:///%s", b[0] == '/' ? b + 1 : b);

    start();
    server.manifest = manifest;
    open_uri(uri_b, main_file);
    // Diagnostics for both files, in path order, none of them errors: Body and
    // Gravity come from the other file.
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(count(sent[0], uri_b) == 1 && count(sent[1], uri_a) == 1);
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]") && has(sent[1], "\"diagnostics\":[]"));

    // `Body` leads to the other file.
    TIDE_CHECK(has(request_at(uri_b, "textDocument/definition", 1, 40, ""), uri_a));
    TIDE_CHECK(count(request_at(uri_b, "textDocument/references", 1, 40, "\"context\":{\"includeDeclaration\":true}"),
                     uri_a) >= 2); // The declaration and Gravity's parameter
    TIDE_CHECK(has(last_sent(), uri_b));

    // The hover shows where Move runs.
    TIDE_CHECK(has(request_at(uri_b, "textDocument/hover", 3, 8, ""), "Runs 2nd of 2 systems each tick, after `Physics.Gravity`"));

    // Renaming Gravity from the attribute edits both files.
    const char *rename = request_at(uri_b, "textDocument/rename", 2, 17, "\"newName\":\"Fall\"");
    TIDE_CHECK(has(rename, uri_a) && has(rename, uri_b));
    TIDE_CHECK(count(rename, "\"newText\":\"Fall\"") == 2);

    // So does renaming Physics: its `namespace` line, the `using` and the attribute.
    rename = request_at(uri_b, "textDocument/rename", 0, 8, "\"newName\":\"World\"");
    TIDE_CHECK(has(rename, uri_a) && has(rename, uri_b));
    TIDE_CHECK(count(rename, "\"newText\":\"World\"") == 3);
    TIDE_CHECK(has(request_at(uri_b, "textDocument/rename", 0, 8, "\"newName\":\"Main\""), "'Main' is already declared"));

    // Completion after `Physics.` lists what's inside it.
    open_uri(uri_b, "using Physics;\nevent(Spawned) Setup(with Main) { Spawn(Physics.); }\nscene Main { }\n");
    const char *completion = request_at(uri_b, "textDocument/completion", 1, 48, "");
    TIDE_CHECK(offers(completion, "Body"));
    TIDE_CHECK(!offers(completion, "Gravity")); // Systems only in Before and After

    // An error in one file shows in the other: Main now spawns a Body nobody declares.
    open_uri(uri_b, main_file);
    open_uri(uri_a, "namespace Physics;\ncomponent Shape { float2 position; }\n");
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[1], uri_a) && has(sent[1], "\"diagnostics\":[]")); // The file that changed is fine...
    TIDE_CHECK(has(sent[0], uri_b) && has(sent[0], "unknown component or singleton 'Body'")); // ...its user isn't

    remove(a);
    remove(b);
    remove(manifest);
    clear_sent();
    lsp_free(&server);
}

static void make_folder(const char *name)
{
    char path[640];
    game_path(path, sizeof path, name);
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

static void remove_folder(const char *name)
{
    char path[640];
    game_path(path, sizeof path, name);
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

static void remove_game_file(const char *name)
{
    char path[640];
    game_path(path, sizeof path, name);
    remove(path);
}

// A game listed as a folder, as tide_add_game lists one without SOURCES: every
// .tide file in it and its subfolders, even one the editor hasn't saved yet.
TIDE_TEST(lsp_game_folder)
{
    if (!find_game_dir()) return;
    make_folder("lsp_folder");
    make_folder("lsp_folder/sub");
    static const char physics[] = "namespace Physics;\n"
                                  "component Body { float2 position; }\n"
                                  "system Gravity(mut Body body) { body.position.y -= 1; }\n";
    static const char main_file[] = "using Physics;\n"
                                    "event(Spawned) Setup(with Main) { Spawn(Body); }\n"
                                    "system Move(mut Body body) { body.position.x += 1; }\n"
                                    "scene Main { }\n";
    write_file("lsp_folder/sub/physics.tide", physics);
    write_file("lsp_folder/main.tide", main_file);
    write_file("lsp_folder/notes.txt", "not Tide");
    char manifest[640], manifest_text[1400], uri_main[700], uri_new[700], uri_physics[700];
    game_path(manifest, sizeof manifest, "lsp_folder_manifest.txt");
    snprintf(manifest_text, sizeof manifest_text, "other\t%s/elsewhere.tide\ngame\t%s/lsp_folder/\n", game_dir,
             game_dir);
    write_file("lsp_folder_manifest.txt", manifest_text);
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_folder/main.tide", dir);
    snprintf(uri_new, sizeof uri_new, "file:///%s/lsp_folder/new.tide", dir);
    snprintf(uri_physics, sizeof uri_physics, "file:///%s/lsp_folder/sub/physics.tide", dir);

    start();
    server.manifest = manifest;
    open_uri(uri_main, main_file);
    // Body comes from the file in the subfolder.
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[0], uri_main) && has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(sent[1], uri_physics) && has(sent[1], "\"diagnostics\":[]"));
    // In path order, main.tide comes before sub/physics.tide.
    TIDE_CHECK(has(request_at(uri_main, "textDocument/hover", 2, 8, ""), "Runs 1st of 2 systems each tick."));

    // A new file joins the game before it's saved.
    open_uri(uri_new, "using Physics;\nsystem Fall(mut Body body) { body.position.y -= 2; }\n");
    TIDE_CHECK(sent_count == 3);
    for (int i = 0; i < sent_count; i++) TIDE_CHECK(has(sent[i], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request_at(uri_new, "textDocument/hover", 1, 8, ""), "Runs 2nd of 3 systems each tick."));

    remove_game_file("lsp_folder/sub/physics.tide");
    remove_game_file("lsp_folder/main.tide");
    remove_game_file("lsp_folder/notes.txt");
    remove_folder("lsp_folder/sub");
    remove_folder("lsp_folder");
    remove(manifest);
    clear_sent();
    lsp_free(&server);
}

// Starts the server with `folder` open in the editor, as a URI.
static void start_in(const char *folder)
{
    clear_sent();
    lsp_free(&server);
    lsp_init(&server, capture, NULL);
    char message[1024];
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":"
             "{\"rootUri\":\"%s\",\"workspaceFolders\":[{\"uri\":\"%s\",\"name\":\"game\"}]}}",
             folder, folder);
    handle(message);
}

// A folder open in the editor is a game, as `tide run` builds it: every .tide
// file in it and its subfolders, with no manifest.
TIDE_TEST(lsp_open_folder_is_a_game)
{
    if (!find_game_dir()) return;
    make_folder("lsp_open");
    make_folder("lsp_open/sub");
    static const char physics[] = "namespace Physics;\n"
                                  "component Body { float2 position; }\n"
                                  "system Gravity(mut Body body) { body.position.y -= 1; }\n";
    static const char main_file[] = "using Physics;\n"
                                    "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\n";
    write_file("lsp_open/sub/physics.tide", physics);
    write_file("lsp_open/main.tide", main_file);
    char root[700], uri_main[700], uri_physics[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_open", dir); // Editors send folders without the last '/'
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_open/main.tide", dir);
    snprintf(uri_physics, sizeof uri_physics, "file:///%s/lsp_open/sub/physics.tide", dir);

    start_in(root);
    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[0], uri_main) && has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(sent[1], uri_physics) && has(sent[1], "\"diagnostics\":[]"));

    // Without the folder open, the file stands alone.
    start();
    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 1 && !has(sent[0], "\"diagnostics\":[]"));

    remove_game_file("lsp_open/sub/physics.tide");
    remove_game_file("lsp_open/main.tide");
    remove_folder("lsp_open/sub");
    remove_folder("lsp_open");
    clear_sent();
    lsp_free(&server);
}

// A game whose tide.packages lists a package in a folder: its files are
// analyzed with the game's, in its namespace. A package's file is analyzed
// with the game that uses it, or else alone, with no Main; and a folder of
// the game with a tide.packages of its own isn't the game's.
TIDE_TEST(lsp_open_folder_with_packages)
{
    if (!find_game_dir()) return;
    make_folder("lsp_pk");
    make_folder("lsp_pk/game");
    make_folder("lsp_pk/game/other");
    make_folder("lsp_pk/physics");
    static const char body[] = "namespace Physics;\n"
                               "component Body { float2 position; }\n"
                               "system Gravity(mut Body body) { body.position.y -= 1; }\n";
    static const char main_file[] = "using Physics;\n"
                                    "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\n";
    write_file("lsp_pk/game/tide.packages", "../physics\n");
    write_file("lsp_pk/game/main.tide", main_file);
    write_file("lsp_pk/game/other/tide.packages", "package Other\n");
    write_file("lsp_pk/game/other/loose.tide", "struct Pair { float a; }\n"); // Outside its namespace, but not the game's
    write_file("lsp_pk/physics/tide.packages", "package Physics\n");
    write_file("lsp_pk/physics/body.tide", body);
    char root[700], uri_main[700], uri_body[700], uri_loose[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_pk/game", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_pk/game/main.tide", dir);
    snprintf(uri_body, sizeof uri_body, "file:///%s/lsp_pk/physics/body.tide", dir);
    snprintf(uri_loose, sizeof uri_loose, "file:///%s/lsp_pk/physics/loose.tide", dir);

    start_in(root);
    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[0], uri_body) && has(sent[0], "\"diagnostics\":[]")); // The package's files come first
    TIDE_CHECK(has(sent[1], uri_main) && has(sent[1], "\"diagnostics\":[]"));
    // Opened from the game, the package's file is the game's.
    open_uri(uri_body, body);
    TIDE_CHECK(sent_count == 2 && has(sent[1], uri_main));

    // A file of the package outside its namespace says so.
    open_uri(uri_loose, "struct Pair { float a; }\n");
    TIDE_CHECK(has(sent[1], uri_loose) && has(sent[1], "what package Physics declares goes in its namespace"));
    close_uri(uri_loose);

    // Without the game open, the package stands alone: no Main is missing.
    start();
    open_uri(uri_body, body);
    TIDE_CHECK(sent_count == 1 && has(sent[0], "\"diagnostics\":[]"));

    remove_game_file("lsp_pk/game/tide.packages");
    remove_game_file("lsp_pk/game/main.tide");
    remove_game_file("lsp_pk/game/other/tide.packages");
    remove_game_file("lsp_pk/game/other/loose.tide");
    remove_game_file("lsp_pk/physics/tide.packages");
    remove_game_file("lsp_pk/physics/body.tide");
    remove_folder("lsp_pk/game/other");
    remove_folder("lsp_pk/game");
    remove_folder("lsp_pk/physics");
    remove_folder("lsp_pk");
    clear_sent();
    lsp_free(&server);
}

// An open folder that builds its games with CMake has the manifest
// tide_add_game writes, in build/tools. Its games come from there, and its
// other files stand alone instead of making one game of the whole folder.
TIDE_TEST(lsp_open_folder_with_manifest)
{
    if (!find_game_dir()) return;
    make_folder("lsp_cmake");
    make_folder("lsp_cmake/build");
    make_folder("lsp_cmake/build/tools");
    make_folder("lsp_cmake/game");
    make_folder("lsp_cmake/tests");
    static const char physics[] = "component Body { float2 position; }\n";
    static const char main_file[] = "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\n";
    static const char alone[] = "component Body { float2 position; }\nscene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\n";
    write_file("lsp_cmake/game/physics.tide", physics);
    write_file("lsp_cmake/game/main.tide", main_file);
    write_file("lsp_cmake/tests/alone.tide", alone);
    char manifest_text[700], root[700], uri_main[700], uri_alone[700];
    snprintf(manifest_text, sizeof manifest_text, "game\t%s/lsp_cmake/game/\n", game_dir);
    write_file("lsp_cmake/build/tools/games.txt", manifest_text);
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_cmake", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_cmake/game/main.tide", dir);
    snprintf(uri_alone, sizeof uri_alone, "file:///%s/lsp_cmake/tests/alone.tide", dir);

    start_in(root);
    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 2);
    for (int i = 0; i < sent_count; i++) TIDE_CHECK(has(sent[i], "\"diagnostics\":[]"));
    // Its own Body doesn't clash with the game's.
    open_uri(uri_alone, alone);
    TIDE_CHECK(sent_count == 1 && has(sent[0], uri_alone) && has(sent[0], "\"diagnostics\":[]"));

    remove_game_file("lsp_cmake/game/physics.tide");
    remove_game_file("lsp_cmake/game/main.tide");
    remove_game_file("lsp_cmake/tests/alone.tide");
    remove_game_file("lsp_cmake/build/tools/games.txt");
    remove_folder("lsp_cmake/game");
    remove_folder("lsp_cmake/tests");
    remove_folder("lsp_cmake/build/tools");
    remove_folder("lsp_cmake/build");
    remove_folder("lsp_cmake");
    clear_sent();
    lsp_free(&server);
}

// The index of the message sent with `uri` in it, or -1.
static int sent_for(const char *uri)
{
    for (int i = 0; i < sent_count; i++) {
        if (has(sent[i], uri)) return i;
    }
    return -1;
}

// Files that change on disk, as the editor reports them through the watchers
// the server registers, and when it saves: the open documents' games are
// analysed again.
TIDE_TEST(lsp_files_change_on_disk)
{
    if (!find_game_dir()) return;
    make_folder("lsp_watch");
    static const char physics[] = "component Body { float2 position; }\n";
    static const char main_file[] = "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\n";
    write_file("lsp_watch/physics.tide", physics);
    write_file("lsp_watch/main.tide", main_file);
    char root[700], uri_main[700], uri_physics[700], uri_new[700], manifest[700], message[2048];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_watch", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_watch/main.tide", dir);
    snprintf(uri_physics, sizeof uri_physics, "file:///%s/lsp_watch/physics.tide", dir);
    snprintf(uri_new, sizeof uri_new, "file:///%s/lsp_watch/new.tide", dir);
    snprintf(manifest, sizeof manifest, "%s/elsewhere/build/tools/games.txt", game_dir);

    // Watchers, registered once the editor says it's ready, when it can take them
    clear_sent();
    lsp_free(&server);
    lsp_init(&server, capture, NULL);
    server.manifest = manifest;
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"%s\",\"workspaceFolders\":"
             "[{\"uri\":\"%s\",\"name\":\"game\"}],\"capabilities\":{\"workspace\":{\"didChangeWatchedFiles\":"
             "{\"dynamicRegistration\":true,\"relativePatternSupport\":true}}}}}",
             root, root);
    handle(message);
    TIDE_CHECK(has(last_sent(), "\"save\":{\"includeText\":false}"));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
    TIDE_CHECK(sent_count == 1 && has(sent[0], "\"method\":\"client/registerCapability\""));
    TIDE_CHECK(has(sent[0], "\"method\":\"workspace/didChangeWatchedFiles\""));
    TIDE_CHECK(has(sent[0], "{\"globPattern\":\"**/*.tide\"}") && has(sent[0], "{\"globPattern\":\"**/build/tools/games.txt\"}"));
    TIDE_CHECK(has(sent[0], "/elsewhere/build/tools\",\"pattern\":\"games.txt\"}")); // The server's own, outside the folder
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":\"tidels-watch\",\"result\":null}"); // The editor's answer needs none
    TIDE_CHECK(sent_count == 0);

    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 2 && has(sent[sent_for(uri_main)], "\"diagnostics\":[]"));

    // Another file changes on disk: Body is gone
    write_file("lsp_watch/physics.tide", "component Shape { float2 position; }\n");
    clear_sent();
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\",\"params\":{\"changes\":"
             "[{\"uri\":\"%s\",\"type\":2}]}}",
             uri_physics);
    handle(message);
    TIDE_CHECK(sent_for(uri_main) >= 0 && has(sent[sent_for(uri_main)], "unknown name 'Body'"));

    // A new one brings it back
    write_file("lsp_watch/new.tide", physics);
    clear_sent();
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\",\"params\":{\"changes\":"
             "[{\"uri\":\"%s\",\"type\":1}]}}",
             uri_new);
    handle(message);
    TIDE_CHECK(sent_count == 3 && has(sent[sent_for(uri_main)], "\"diagnostics\":[]"));

    // A deleted one takes its diagnostics with it
    write_file("lsp_watch/physics.tide", "component Body { float2 position; }\n"); // Declared twice now
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{\"textDocument\":{\"uri\":\"file:///none\"}}}");
    TIDE_CHECK(has(sent[sent_for(uri_physics)], "\"severity\":1")); // Saving looks at the disk again too
    remove_game_file("lsp_watch/physics.tide");
    clear_sent();
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\",\"params\":{\"changes\":"
             "[{\"uri\":\"%s\",\"type\":3}]}}",
             uri_physics);
    handle(message);
    TIDE_CHECK(sent_for(uri_physics) >= 0 && has(sent[sent_for(uri_physics)], "\"diagnostics\":[]"));
    TIDE_CHECK(has(sent[sent_for(uri_main)], "\"diagnostics\":[]"));

    // An editor that can't watch gets no request
    start_with("{\"capabilities\":{}}");
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
    TIDE_CHECK(sent_count == 0);

    remove_game_file("lsp_watch/new.tide");
    remove_game_file("lsp_watch/main.tide");
    remove_folder("lsp_watch");
    clear_sent();
    lsp_free(&server);
}

// Folders opened and closed in the editor change which files make up a game.
TIDE_TEST(lsp_workspace_folders_change)
{
    if (!find_game_dir()) return;
    make_folder("lsp_folders");
    static const char main_file[] = "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\n";
    write_file("lsp_folders/physics.tide", "component Body { float2 position; }\n");
    write_file("lsp_folders/main.tide", main_file);
    char root[700], uri_main[700], message[2048];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_folders", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_folders/main.tide", dir);

    start();
    TIDE_CHECK(has(last_sent(), "\"workspaceFolders\":{\"supported\":true,\"changeNotifications\":true}"));
    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 1 && !has(sent[0], "\"diagnostics\":[]")); // Alone: no Body
    clear_sent();
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWorkspaceFolders\",\"params\":{\"event\":"
             "{\"added\":[{\"uri\":\"%s\",\"name\":\"game\"}],\"removed\":[]}}}",
             root);
    handle(message);
    TIDE_CHECK(sent_count == 2 && has(sent[sent_for(uri_main)], "\"diagnostics\":[]"));
    clear_sent();
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWorkspaceFolders\",\"params\":{\"event\":"
             "{\"added\":[],\"removed\":[{\"uri\":\"%s\",\"name\":\"game\"}]}}}",
             root);
    handle(message);
    TIDE_CHECK(sent_count == 1 && !has(sent[0], "\"diagnostics\":[]"));

    remove_game_file("lsp_folders/physics.tide");
    remove_game_file("lsp_folders/main.tide");
    remove_folder("lsp_folders");
    clear_sent();
    lsp_free(&server);
}

// Workspace symbols come from every game in the open folders, with nothing open.
TIDE_TEST(lsp_workspace_symbols_of_every_game)
{
    if (!find_game_dir()) return;
    make_folder("lsp_symbols");
    make_folder("lsp_symbols/build");
    make_folder("lsp_symbols/build/tools");
    make_folder("lsp_symbols/a");
    make_folder("lsp_symbols/b");
    write_file("lsp_symbols/a/main.tide", "component Rocket { float fuel; }\nscene Main { }\n");
    write_file("lsp_symbols/b/main.tide", "component Rover { float speed; }\nscene Main { }\n");
    char manifest_text[1400], root[700];
    snprintf(manifest_text, sizeof manifest_text, "a\t%s/lsp_symbols/a/\nb\t%s/lsp_symbols/b/main.tide\n", game_dir, game_dir);
    write_file("lsp_symbols/build/tools/games.txt", manifest_text);
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_symbols", dir);

    start_in(root);
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"ro\"}}");
    TIDE_CHECK(has(last_sent(), "\"name\":\"Rocket\"") && has(last_sent(), "\"name\":\"Rover\""));
    TIDE_CHECK(has(last_sent(), "lsp_symbols/a/main.tide") && has(last_sent(), "lsp_symbols/b/main.tide"));
    TIDE_CHECK(count(last_sent(), "\"name\":\"Main\"") == 0); // Not what the query asks for

    remove_game_file("lsp_symbols/a/main.tide");
    remove_game_file("lsp_symbols/b/main.tide");
    remove_game_file("lsp_symbols/build/tools/games.txt");
    remove_folder("lsp_symbols/a");
    remove_folder("lsp_symbols/b");
    remove_folder("lsp_symbols/build/tools");
    remove_folder("lsp_symbols/build");
    remove_folder("lsp_symbols");
    clear_sent();
    lsp_free(&server);
}

// A name another namespace declares: write it with it, or use it.
TIDE_TEST(lsp_quick_fixes_for_namespaces)
{
    if (!find_game_dir()) return;
    make_folder("lsp_using");
    static const char main_file[] = "namespace Game;\nusing Other;\n\nscene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\n";
    write_file("lsp_using/physics.tide", "namespace Physics;\ncomponent Body { float2 position; }\n");
    write_file("lsp_using/main.tide", main_file);
    write_file("lsp_using/other.tide", "namespace Other;\ncomponent Shape { float2 position; }\n");
    char root[700], uri_main[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_using", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_using/main.tide", dir);

    start_in(root);
    open_uri(uri_main, main_file);
    const char *fixes = request_at(uri_main, "textDocument/codeAction", 4, 0,
                                   "\"range\":{\"start\":{\"line\":4,\"character\":0},\"end\":{\"line\":4,\"character\":0}}");
    TIDE_CHECK(has(fixes, "\"title\":\"Write 'Physics.Body'\""));
    TIDE_CHECK(has(fixes, "\"newText\":\"Physics.Body\""));
    TIDE_CHECK(has(fixes, "\"title\":\"Add 'using Physics;'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    TIDE_CHECK(has(fixes, "{\"range\":{\"start\":{\"line\":2,\"character\":0},\"end\":{\"line\":2,\"character\":0}},"
                          "\"newText\":\"using Physics;\\n\"}")); // After the usings

    remove_game_file("lsp_using/physics.tide");
    remove_game_file("lsp_using/main.tide");
    remove_game_file("lsp_using/other.tide");
    remove_folder("lsp_using");
    clear_sent();
    lsp_free(&server);
}

TIDE_TEST(lsp_shutdown_and_exit)
{
    start();
    handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"shutdown\"}");
    TIDE_CHECK(has(last_sent(), "\"id\":9,\"result\":null"));
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}");
    TIDE_CHECK(server.exited && server.exit_code == 0);
    clear_sent();
    lsp_free(&server);
}

// Above each system: its stage and why it waits. Access a system doesn't use
// gets a warning and a quick fix.
TIDE_TEST(lsp_schedule_lenses_and_fixes)
{
    start();
    static const char program[] = "component Body { float2 position; float2 velocity; }\n"
                                  "singleton Score { int total; }\n"
                                  "event(Spawned) Setup(with Main) { Spawn(Body); }\n"
                                  "system Move(mut Body body) { body.position += body.velocity; }\n"
                                  "system Look(mut Body body, mut Score s) { s.total = 0; if (body.position.x > 0) return; }\n"
                                  "system Tag(mut Score s, Body body) { s.total = 1; }\n"
                                  "scene Main { }\n";
    open_document(program);
    TIDE_CHECK(has(sent[0], "'body' is declared mut but never written"));
    TIDE_CHECK(has(sent[0], "'body' is never used"));

    const char *lenses = request("textDocument/codeLens");
    TIDE_CHECK(has(lenses, "stage 1"));
    TIDE_CHECK(has(lenses, "stage 2") && has(lenses, "after Move: both write Body"));
    TIDE_CHECK(has(lenses, "stage 3") && has(lenses, "after Look: both write Score; it writes Body, which this reads"));
    TIDE_CHECK(has(lenses, "nothing runs alongside"));

    const char *hover = request_at("file:///test.tide", "textDocument/hover", 5, 8, "");
    TIDE_CHECK(has(hover, "**Stage 3.**") && has(hover, "`Move`: it writes `Body`, which this reads"));
    TIDE_CHECK(has(hover, "No other system can run at the same time."));

    const char *remove_mut = request_at("file:///test.tide", "textDocument/codeAction", 4, 0,
                                        "\"range\":{\"start\":{\"line\":4,\"character\":0},\"end\":{\"line\":4,\"character\":0}}");
    TIDE_CHECK(has(remove_mut, "Remove 'mut' from 'body'") && has(remove_mut, "\"newText\":\"\""));
    TIDE_CHECK(has(remove_mut, "\"start\":{\"line\":4,\"character\":12}"));
    const char *use_with = request_at("file:///test.tide", "textDocument/codeAction", 5, 0,
                                      "\"range\":{\"start\":{\"line\":5,\"character\":0},\"end\":{\"line\":5,\"character\":0}}");
    TIDE_CHECK(has(use_with, "\"newText\":\"with Body\""));

    open_document("component Body { float2 position; }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\n"
         "system Push(Body body) { body.position.x = 1; }\nscene Main { }\n");
    const char *add_mut = request_at("file:///test.tide", "textDocument/codeAction", 2, 0,
                                     "\"range\":{\"start\":{\"line\":2,\"character\":25},\"end\":{\"line\":2,\"character\":29}}");
    TIDE_CHECK(has(add_mut, "Declare 'body' as mut") && has(add_mut, "\"newText\":\"mut \""));
    TIDE_CHECK(has(add_mut, "\"start\":{\"line\":2,\"character\":12}"));
}

#define WORLD_GAME                                                                                      \
    "component Health { int value; }\n"                                                                 \
    "component Healer { Entity patient; }\n"                                                            \
    "singleton Score { int total; }\n"                                                                  \
    "scene Main { }\n"                                                                                  \
    "event(Spawned) Setup(with Main) { var a = Spawn(Health); Spawn(Healer { patient = a }); }\n"

// The World: any entity's component through world.Get<Health>(e), Has and
// Set. Its signature doesn't name the components, so hovers and lenses do.
TIDE_TEST(lsp_world)
{
    start();
    static const char game[] = WORLD_GAME "system Heal(Healer healer, mut World world)\n{\n"
                                          "    if (!world.Has<Health>(healer.patient)) return;\n"
                                          "    mut var h = world.Get<Health>(healer.patient);\n"
                                          "    h.value += 1;\n"
                                          "    world.Set<Health>(healer.patient, h);\n}\n"
                                          "system Watch(Healer healer, World world)\n{\n"
                                          "    if (world.Has<Health>(healer.patient)) return;\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    // The lens and the hovers say what goes through the World
    const char *lenses = request("textDocument/codeLens");
    TIDE_CHECK(has(lenses, "stage 1") && has(lenses, "World: writes Health"));
    TIDE_CHECK(has(lenses, "World: reads Health") && has(lenses, "after Heal: it writes Health, which this reads"));
    const char *system = request_at("file:///test.tide", "textDocument/hover", 5, 8, "");
    TIDE_CHECK(has(system, "Through its `World`, it writes any entity's `Health`."));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 12, 8, ""),
                   "Through its `World`, it reads any entity's `Health`."));
    const char *parameter = request_at("file:///test.tide", "textDocument/hover", 8, 18, "");
    TIDE_CHECK(has(parameter, "mut World world") && has(parameter, "The World: any entity's components by its handle"));
    TIDE_CHECK(has(parameter, "it writes any entity's `Health`"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 5, 32, ""), "`world.Get<Health>(entity)` is a copy"));
    const char *get = request_at("file:///test.tide", "textDocument/hover", 8, 23, "");
    TIDE_CHECK(has(get, "World.Get<Health>(Entity entity) -> Health") && has(get, "zero value when the entity has none"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 10, 11, ""), "World.Set<Health>(Entity entity, Health value)"));
    // The component in <> is the component: its hover, its declaration, and a type for `var`
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 8, 28, ""), "component Health"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 8, 28, ""), "\"start\":{\"line\":0,\"character\":10}"));
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":20,\"character\":0}}");
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":8,\"character\":13},\"label\":\": Health\""));

    // Formatting keeps a method's type tight, and comparisons apart
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));
    static const char messy[] = WORLD_GAME "system Heal(Healer healer,mut World world)\n{\n"
                                           "var h=world.Get <Health> (healer.patient);\n"
                                           "if(h.value<3&&world.Get<Health>(healer.patient).value>(2))return;\n"
                                           "world.Set< Health >(healer.patient,h);\n}\n";
    format_reply(messy);
    const char *tidy = apply_reply(messy, NULL);
    TIDE_CHECK(has(tidy, "    var h = world.Get<Health>(healer.patient);\n"));
    TIDE_CHECK(has(tidy, "    if (h.value < 3 && world.Get<Health>(healer.patient).value > (2)) return;\n"));
    TIDE_CHECK(has(tidy, "    world.Set<Health>(healer.patient, h);\n"));

    // Semantic tokens: World a built-in type (1, defaultLibrary 8), Get a built-in method (10), Health a struct (2)
    open_document("component Health { int value; }\nscene Main { }\nsystem S(World world, with Health)\n{\n"
                  "    var h = world.Get<Health>(this);\n}\n");
    const char *tokens = request("textDocument/semanticTokens/full");
    TIDE_CHECK(has(tokens, "0,2,5,1,8,0,6,5,5,3,0,12,6,2,0,"));
    TIDE_CHECK(has(tokens, "0,4,5,5,2,0,6,3,10,8,0,4,6,2,0,0,8,4,11,0"));

    // Completion: the World's methods, the components in <>, and World where a parameter's type goes
    const char *methods = complete(WORLD_GAME "system Heal(Healer healer, mut World world)\n{\n    world.$\n}\n");
    TIDE_CHECK(offers(methods, "Get") && offers(methods, "Has") && offers(methods, "Set"));
    TIDE_CHECK(has(methods, "World.Get<T>(Entity entity) -> T") && has(methods, "Get<$1>($2)"));
    const char *components = complete(WORLD_GAME "system Heal(Healer healer, mut World world)\n{\n    var h = world.Get<$\n}\n");
    TIDE_CHECK(offers(components, "Health") && offers(components, "Healer") && !offers(components, "Score"));
    TIDE_CHECK(offers(complete(WORLD_GAME "system Heal(Healer healer, mut World world)\n{\n    var h = world.Get<He$\n}\n"),
                      "Health"));
    const char *types = complete(WORLD_GAME "system Heal(Healer healer, $)\n{\n}\n");
    TIDE_CHECK(offers(types, "World") && !offers(types, "lookup"));
    TIDE_CHECK(offers(complete(WORLD_GAME "system Heal(Healer healer, mut $)\n{\n}\n"), "World"));
    TIDE_CHECK(offers(complete(WORLD_GAME "system Heal(Healer healer, World $)\n{\n}\n"), "world"));
    // A local handler's World reads the local world's, a view's both
    static const char local[] = "component Health { int value; }\nlocal component Spark { int life; }\nlocal event Tap { }\n"
                                "scene Main { }\n";
    char text[1024];
    snprintf(text, sizeof text, "%slocal event(Tap) OnTap(World world, with Spark)\n{\n    var s = world.Get<$\n}\n", local);
    const char *locals = complete(text);
    TIDE_CHECK(offers(locals, "Spark") && !offers(locals, "Health"));
    snprintf(text, sizeof text, "%sview Show(World world, with Spark)\n{\n    var s = world.Get<$\n}\n", local);
    const char *both = complete(text);
    TIDE_CHECK(offers(both, "Spark") && offers(both, "Health"));

    // Signature help, with the component it's of
    open_document(WORLD_GAME "system Heal(Healer healer, mut World world)\n{\n    world.Set<Health>(healer.patient, $\n}\n");
    const char *set = request("textDocument/signatureHelp");
    TIDE_CHECK(has(set, "World.Set<Health>(Entity entity, Health value)") && has(set, "\"activeParameter\":1"));
    open_document(WORLD_GAME "system Heal(Healer healer, World world)\n{\n    var h = world.Get<Health>($);\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "World.Get<Health>(Entity entity) -> Health"));

    // Renaming the component renames it in <> too, and the uses there are found
    open_document(WORLD_GAME "system Heal(Healer healer, mut World world)\n{\n"
                             "    if (!world.Has<Health>(healer.patient)) return;\n"
                             "    mut var h = world.Get<Hea$lth>(healer.patient);\n"
                             "    h.value += 1;\n"
                             "    world.Set<Health>(healer.patient, h);\n}\n"
                             "system Watch(Healer healer, World world)\n{\n"
                             "    if (world.Has<Health>(healer.patient)) return;\n}\n");
    // Its declaration, Setup's Spawn, and the four calls
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 6);
    request_with("textDocument/rename", "\"newName\":\"Hp\"");
    const char *renamed = apply_reply(game, "file:///test.tide");
    TIDE_CHECK(has(renamed, "world.Has<Hp>(healer.patient)") && has(renamed, "world.Get<Hp>(healer.patient)"));
    TIDE_CHECK(has(renamed, "world.Set<Hp>(healer.patient, h)") && !has(renamed, "Health"));
    char copy[8192];
    snprintf(copy, sizeof copy, "%s", renamed);
    open_document(copy);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    // A type outside a namespace can't take the World's name
    open_document(WORLD_GAME "system Count(mut Sco$re score) { score.total = 1; }\n");
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"World\""), "'World' is built into the engine"));

    // Set through a World that isn't mut: the fix declares it mut
    open_document(WORLD_GAME "system Heal(Healer healer, World world)\n{\n"
                             "    world.Set<Health>(healer.patient, Health { value = 1 });\n}\n");
    TIDE_CHECK(has(last_sent(), "'world' is read-only") && has(last_sent(), "declare the parameter as 'mut World world'"));
    const char *add_mut = actions_at(7);
    TIDE_CHECK(has(add_mut, "Declare 'world' as mut") && has(add_mut, "\"newText\":\"mut \""));
    TIDE_CHECK(has(add_mut, "\"start\":{\"line\":5,\"character\":27}"));
    // A mut World that never calls Set, and one that's never used: warnings, faded, and the first has a fix
    open_document(WORLD_GAME "system Heal(Healer healer, mut World world)\n{\n"
                             "    if (world.Has<Health>(healer.patient)) return;\n}\n"
                             "system Idle(Healer healer, World world)\n{\n    if (healer.patient == this) return;\n}\n");
    TIDE_CHECK(has(last_sent(), "'world' is declared mut but never calls Set") && has(last_sent(), "'world' is never used"));
    TIDE_CHECK(count(last_sent(), "\"tags\":[1]") == 2);
    const char *remove_mut = actions_at(5);
    TIDE_CHECK(has(remove_mut, "Remove 'mut' from 'world'") && has(remove_mut, "\"newText\":\"\""));
    TIDE_CHECK(has(remove_mut, "\"start\":{\"line\":5,\"character\":27}"));

    // `lookup` is gone: the error says what to write
    open_document(WORLD_GAME "system Heal(Healer healer, lookup Health health)\n{\n}\n");
    TIDE_CHECK(has(last_sent(), "'lookup' parameters are gone") && has(last_sent(), "world.Get<Health>(entity)"));
    // An unknown component in <>: a suggestion, as a fix
    open_document(WORLD_GAME "system Heal(Healer healer, World world)\n{\n    var h = world.Get<Helth>(healer.patient);\n}\n");
    TIDE_CHECK(has(last_sent(), "unknown component 'Helth'") && has(actions_at(7), "Change to 'Health'"));
}

// ---------------------------------------------------------------------------
// Reading ahead: changes wait to be analysed together, and cancelled requests
// aren't worked out (as main.c runs the server)

static void change_document(const char *text)
{
    jbuf b = {0};
    jb_put(&b, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{\"textDocument\":"
               "{\"uri\":\"file:///test.tide\",\"version\":2},\"contentChanges\":[{\"text\":");
    jb_string(&b, text);
    jb_put(&b, "}]}}");
    handle(b.data);
    jb_free(&b);
}

TIDE_TEST(lsp_changes_wait_to_be_analysed_together)
{
    start();
    server.defer = true;
    // Nothing goes out for a change, or for the ones after it
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.pos = 1;\n}\n");
    TIDE_CHECK(sent_count == 0);
    change_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.spin = 1;\n}\n");
    TIDE_CHECK(sent_count == 0);
    // Once input goes quiet, the text as the last change left it is analysed, once
    lsp_settle(&server);
    TIDE_CHECK(sent_count == 1);
    TIDE_CHECK(has(last_sent(), "\"method\":\"textDocument/publishDiagnostics\""));
    TIDE_CHECK(has(last_sent(), "Body has no field 'spin'"));
    TIDE_CHECK(!has(last_sent(), "Body has no field 'pos'"));
    clear_sent();
    lsp_settle(&server); // Nothing waits any more
    TIDE_CHECK(sent_count == 0);

    // A request doesn't wait for quiet: the change is analysed for it, and
    // its diagnostics go out before the reply, as they would without waiting
    change_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.position.x = 1;\n}\n");
    TIDE_CHECK(sent_count == 0);
    cursor_line = 24;
    cursor_character = 6;
    const char *reply = request("textDocument/hover");
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[0], "\"method\":\"textDocument/publishDiagnostics\"") && has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(reply, "\"id\":7") && has(reply, "\"result\""));
    clear_sent();
    lsp_settle(&server);
    TIDE_CHECK(sent_count == 0);

    // Closing isn't a change: what waited is analysed first, then the
    // document's diagnostics go with it
    change_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.pos = 1;\n}\n");
    TIDE_CHECK(sent_count == 0 && server.unsettled_count == 1);
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{\"textDocument\":"
           "{\"uri\":\"file:///test.tide\"}}}");
    TIDE_CHECK(server.unsettled_count == 0);
    TIDE_CHECK(sent_count == 2 && has(sent[0], "Body has no field 'pos'") && has(sent[1], "\"diagnostics\":[]"));
}

TIDE_TEST(lsp_cancelled_requests_are_skipped)
{
    start();
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.position.x = 1;\n}\n");
    static const char hover[] =
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":"
        "{\"uri\":\"file:///test.tide\"},\"position\":{\"line\":24,\"character\":6}}}";
    static const char named[] =
        "{\"jsonrpc\":\"2.0\",\"id\":\"h-7\",\"method\":\"textDocument/hover\",\"params\":{\"textDocument\":"
        "{\"uri\":\"file:///test.tide\"},\"position\":{\"line\":24,\"character\":6}}}";
    static const char cancel_7[] = "{\"jsonrpc\":\"2.0\",\"method\":\"$/cancelRequest\",\"params\":{\"id\":7}}";
    static const char cancel_8[] = "{\"jsonrpc\":\"2.0\",\"method\":\"$/cancelRequest\",\"params\":{\"id\":8}}";
    static const char cancel_named[] = "{\"jsonrpc\":\"2.0\",\"method\":\"$/cancelRequest\",\"params\":{\"id\":\"h-7\"}}";
    static const char unrelated[] = "{\"jsonrpc\":\"2.0\",\"method\":\"$/setTrace\",\"params\":{\"value\":\"off\"}}";

    // With nothing after it, or nothing that cancels it, a request is answered
    clear_sent();
    lsp_handle_ahead(&server, hover, strlen(hover), NULL, 0);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"id\":7") && has(last_sent(), "\"result\""));
    const lsp_message others[] = {{unrelated, strlen(unrelated)}, {cancel_8, strlen(cancel_8)}, {cancel_named, strlen(cancel_named)}};
    clear_sent();
    lsp_handle_ahead(&server, hover, strlen(hover), others, 3);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"id\":7") && has(last_sent(), "\"result\""));

    // One that a later message cancels is answered as cancelled
    const lsp_message later[] = {{unrelated, strlen(unrelated)}, {cancel_7, strlen(cancel_7)}};
    clear_sent();
    lsp_handle_ahead(&server, hover, strlen(hover), later, 2);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"id\":7") && has(last_sent(), "\"code\":-32800"));
    TIDE_CHECK(!has(last_sent(), "\"result\""));

    // Ids can be text
    clear_sent();
    lsp_handle_ahead(&server, named, strlen(named), others, 3);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"id\":\"h-7\"") && has(last_sent(), "\"code\":-32800"));
    clear_sent();
    lsp_handle_ahead(&server, named, strlen(named), later, 2);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"id\":\"h-7\"") && has(last_sent(), "\"result\""));

    // A notification has no id to cancel: a change is taken in all the same
    clear_sent();
    lsp_handle_ahead(&server, cancel_7, strlen(cancel_7), later, 2);
    TIDE_CHECK(sent_count == 0);
}
