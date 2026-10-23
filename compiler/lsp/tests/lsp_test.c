// The language server, driven with JSON-RPC messages as an editor sends them.
// In test documents, `$` marks the cursor.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <sys/utime.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#endif

#include "assets.h"
#include "json.h"
#include "model_metadata.h"
#include "../../tests/fbx_metadata_fixture.h"
#include "packages.h"
#include "sha256.h"
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

// Sends a request at the cursor, with `extra` params (JSON members, or ""), and returns the reply.
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
    for (const char *p = original; *p && n + 1 < sizeof applied; p++) {
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

// A semantic token's absolute position, decoded from LSP's relative data.
static bool token_is(const char *reply, const int line, const int col, const int type, const int mods)
{
    const json *data = json_path(json_parse(reply, strlen(reply)), "result", "data", NULL);
    bool found = false;
    int at_line = 0, at_col = 0;
    for (int i = 0; data && i + 4 < data->count; i += 5) {
        const int delta = json_int(data->items[i], 0);
        at_line += delta;
        at_col = (delta ? 0 : at_col) + json_int(data->items[i + 1], 0);
        if (at_line == line && at_col == col) {
            found = json_int(data->items[i + 3], -1) == type && json_int(data->items[i + 4], -1) == mods;
            break;
        }
    }
    json_release();
    return found;
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
    TIDE_CHECK(has(reply, "\"line\":24"));
    TIDE_CHECK(has(reply, "can't assign bool to float"));
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
    TIDE_CHECK(offers(reply, "xy") == false);
}

TIDE_TEST(lsp_complete_input_edges)
{
    start();
    const char *reply = complete(GAME_TYPES "system Fire(PlayerInput input)\n{\n    if (input.fire.$) return;\n}\n");
    TIDE_CHECK(offers(reply, "down"));
    TIDE_CHECK(offers(reply, "up"));
}

TIDE_TEST(lsp_borrowed_input_in_helpers)
{
    start();
    TIDE_CHECK(offers(complete("input Keys { bool jump; }\nscene Main { }\nbool Pressed($) { return false; }\n"), "Keys"));
    TIDE_CHECK(!offers(complete("input Keys { bool jump; }\nscene Main { }\nasync void Read($) { }\n"), "Keys"));
    TIDE_CHECK(!offers(complete("input Keys { bool jump; }\nscene Main { }\nvoid Read(mut $) { }\n"), "Keys"));
    TIDE_CHECK(!offers(complete("input Keys { bool jump; }\nscene Main { }\nvoid Read(out $) { }\n"), "Keys"));
    TIDE_CHECK(!offers(complete("input Keys { bool jump; }\nscene Main { }\nextern bool Read($);\n"), "Keys"));
    TIDE_CHECK(!offers(complete("input Keys { bool jump; }\nscene Main { }\naudio Read($) { }\n"), "Keys"));
    const char *edges = complete("struct Buttons { bool fire; }\ninput Keys { Buttons buttons; }\nscene Main { }\n"
                                 "bool Pressed(Keys input) { return input.buttons.fire.$; }\n");
    TIDE_CHECK(offers(edges, "down") && offers(edges, "up"));
    open_document("input Keys { bool jump; }\nscene Main { }\n"
                  "bool Pressed(Keys con$trols) { return controls.jump.down; }\n"
                  "system Check(Keys input) { if (Pressed(input)) Debug.Log(\"jump\"); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "Read-only input borrowed from the caller"));
    TIDE_CHECK(has(request("textDocument/hover"), "Current and previous tick"));
}

TIDE_TEST(lsp_borrowed_asset_collections_in_helpers)
{
    start();
    open_document("scene Main { }\nint Count(List<Clip> val$ues) { return values.count; }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "List<Clip> values"));
    TIDE_CHECK(has(request("textDocument/hover"), "Read-only asset collection borrowed"));
    TIDE_CHECK(offers(complete("scene Main { }\nint Count(List<Texture> values) { return values.$; }\n"), "count"));
    TIDE_CHECK(offers(complete("scene Main { }\nint Count(Map<string, Clip> values) { return values.$; }\n"), "TryGet"));
    const char *shader = complete("scene Main { }\nint Count(List<Texture> values) { return values.count; }\n"
                                 "frag Shade(Vertex vertex, out Color color) { $ }\n");
    TIDE_CHECK(!offers(shader, "Count"));
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

    open_document("input PlayerInput\n{\n    bool jump;\n    Sam$ple(Devices devices) { jump = devices.keyboard.space.pressed; }\n}\n"
         "scene Main { }\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "Builds the player's input from this machine's `Devices`"));
}

static const char *format_reply(const char *text);
static const char *request_at(const char *uri, const char *method, int line, int character, const char *extra);

TIDE_TEST(lsp_input_prediction)
{
    start();
    const char *fields = complete("local singleton Menu { bool open; }\ninput Keys\n{\n"
                                  "    [DontPredict] bool jump;\n    float move;\n"
                                  "    Sample(Menu menu) { jump = menu.open; }\n"
                                  "    Predict()\n    {\n        mut var amount = move;\n        amount = $\n    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(fields, "jump") && offers(fields, "move") && offers(fields, "amount"));
    TIDE_CHECK(!offers(fields, "menu") && !offers(fields, "Devices") && !offers(fields, "Cursor"));
    TIDE_CHECK(offers(complete("input Keys\n{\n    [$] bool jump;\n}\nscene Main { }\n"), "DontPredict"));
    TIDE_CHECK(!offers(complete("input Keys\n{\n    bool jump;\n    Predict($) { }\n}\nscene Main { }\n"), "Devices"));
    TIDE_CHECK(!offers(complete("input Keys\n{\n    int luck;\n    Predict() { luck = Random.$; }\n}\nscene Main { }\n"), "NextInt"));
    open_document("float Fade(float value) { return value * 0.5; }\n"
                  "input Keys\n{\n    [DontPredict] bool jump;\n    float move;\n"
                  "    Pre$dict() { jump = false; move = Fade(move); }\n}\nscene Main { }\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "Adjusts the previous input when a client guesses a future tick"));
    TIDE_CHECK(has(request("textDocument/documentSymbol"), "\"name\":\"Predict\",\"detail\":\"Predict()\",\"kind\":6"));
    const char *implementation = request_at("file:///test.tide", "textDocument/implementation", 1, 7, "");
    TIDE_CHECK(has(implementation, "\"line\":5,\"character\":4"));
    const char *hierarchy = request("textDocument/prepareCallHierarchy");
    TIDE_CHECK(has(hierarchy, "\"name\":\"Predict\""));
    open_document("input Keys\n{\n    [Dont$Predict] bool jump;\n}\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "declared default on guessed ticks"));
}

// Response builders are temporary; repeated requests keep the same JSON,
// and later requests must still be able to read the existing analysis.
static void check_repeat_at(const char *uri, const char *method, const int line, const int character, const char *extra)
{
    jbuf expected = {0};
    jb_put(&expected, request_at(uri, method, line, character, extra));
    for (int i = 0; i < 3; i++) {
        TIDE_CHECK(strcmp(expected.data, request_at(uri, method, line, character, extra)) == 0);
    }
    jb_free(&expected);
}

static void check_repeat_lenses(const char *uri)
{
    check_repeat_at(uri, "textDocument/codeLens", 0, 0, "");
}
static const char *actions_at(int line);

// [Clamp], [Min] and [Max] on input fields: completion, and formatting.
TIDE_TEST(lsp_field_attributes)
{
    start();
    const char *names = complete("input PlayerInput\n{\n    [$] float move;\n}\nscene Main { }\n");
    TIDE_CHECK(offers(names, "Clamp") && offers(names, "Min") && offers(names, "Max") && offers(names, "Absolute"));
    TIDE_CHECK(!offers(names, "Before"));
    const char *roles = complete("component Body\n{\n    [$] float2 position;\n}\nscene Main { }\n");
    TIDE_CHECK(offers(roles, "Position") && offers(roles, "Rotation"));
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

TIDE_TEST(lsp_editor_view_attributes)
{
    start();
    const char *names = complete("[$] view Render() { }\nscene Main { }\n");
    TIDE_CHECK(offers(names, "RunInEditor") && offers(names, "EditorOnly"));
    open_document("[RunIn$Editor] view Render() { }\nscene Main { }\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "both game rendering and the authored prefab preview"));
    open_document("[Editor$Only] view Render() { }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "not in game frames"));
    open_document("scene Main {}\ncomponent Body { int value; }\nprefab Level { Body; }\n"
        "[RunInEditor] view DrawBody(Body body) { Draw.Circle(float2(body.value,0),1,Color.white); }\n"
        "[EditorOnly] view DrawLocal(local Body body) { Draw.Circle(float2(body.value,0),1,Color.white); }\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    open_document("[RunInEditor] system Update() { }\nscene Main { }\n");
    TIDE_CHECK(has(sent[0], "applies to views only"));
}

TIDE_TEST(lsp_complete_builtin_owners)
{
    start();
    const char *math = complete(GAME_TYPES "system S(mut Body body)\n{\n    body.radius = Math.$\n}\n");
    TIDE_CHECK(offers(math, "Sin"));
    TIDE_CHECK(offers(math, "PI"));
    TIDE_CHECK(offers(math, "Normalize"));
    TIDE_CHECK(!offers(math, "NormalizeSafe"));

    const char *draw = complete(GAME_TYPES "view V(Body body)\n{\n    Draw.$\n}\n");
    TIDE_CHECK(offers(draw, "Circle"));
    TIDE_CHECK(has(draw, "Circle(${1:center}, ${2:radius}, ${3:color})"));
    TIDE_CHECK(offers(draw, "WorldToScreen") && offers(draw, "ScreenToWorld") && offers(draw, "Instanced"));
    TIDE_CHECK(has(draw, "MeasureText(${1:text}, ${2:size})"));

    TIDE_CHECK(offers(draw, "Cube") && offers(draw, "Sphere") && offers(draw, "Cylinder") && offers(draw, "Plane"));
    TIDE_CHECK(offers(draw, "ScreenRayOrigin") && offers(draw, "ScreenRayDirection") && offers(draw, "list") && offers(draw, "window"));
    TIDE_CHECK(offers(draw, "Rect") && offers(draw, "Screen") && offers(draw, "Camera") && offers(draw, "Blend") && offers(draw, "Depth"));
    TIDE_CHECK(offers(draw, "Blit") && !offers(draw, "Draw2D") && !offers(draw, "Draw3D"));

    const char *color = complete(GAME_TYPES "view V(Body body)\n{\n    Draw.Circle(body.position, 1, Color.$);\n}\n");
    TIDE_CHECK(offers(color, "red"));
}

TIDE_TEST(lsp_complete_draw_only_in_views)
{
    start();
    const char *system = complete(GAME_TYPES "system S(mut Body body)\n{\n    $\n}\n");
    TIDE_CHECK(!offers(system, "Draw"));
    TIDE_CHECK(offers(system, "Spawn"));
    const char *view = complete(GAME_TYPES "view V(Body body)\n{\n    $\n}\n");
    TIDE_CHECK(offers(view, "Draw") && !offers(view, "Draw2D") && !offers(view, "Draw3D"));
    TIDE_CHECK(offers(view, "Body"));
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
    TIDE_CHECK(!offers(reply, "inner"));
    TIDE_CHECK(offers(reply, "float3"));
    TIDE_CHECK(offers(reply, "Math"));
}

TIDE_TEST(lsp_complete_in_constructor)
{
    start();
    const char *devices = complete("input PlayerInput\n{\n    float2 move;\n\n    Sample(Devices devices)\n    {\n"
                                   "        var keys = devices.$\n    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(devices, "keyboard"));
    TIDE_CHECK(offers(devices, "gamepad"));

    const char *keys = complete("input PlayerInput\n{\n    float2 move;\n\n    Sample(Devices devices)\n    {\n"
                                "        var keys = devices.keyboard;\n        if (keys.$\n    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(keys, "space"));
    TIDE_CHECK(offers(keys, "leftShift"));

    const char *params = complete("local singleton Menu { bool open; }\ninput PlayerInput\n{\n    float2 move;\n\n"
                                  "    Sample($)\n    {\n    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(params, "Menu"));
    TIDE_CHECK(offers(params, "Devices"));
    const char *names = complete("input PlayerInput\n{\n    float2 move;\n\n    Sample(Devices devices)\n    {\n        move = $\n"
                                 "    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(names, "devices") && !offers(names, "Devices"));

    const char *view = complete("scene Main { }\nview Pause(Devices devices)\n{\n    if (devices.keyboard.$\n}\n");
    TIDE_CHECK(offers(view, "escape"));
    TIDE_CHECK(offers(view, "text"));
    TIDE_CHECK(!offers(complete("scene Main { }\nview Pause()\n{\n    var keys = Devices.$\n}\n"), "keyboard"));
    TIDE_CHECK(offers(complete("scene Main { }\nint Typed(Keyboard keys)\n{\n    return keys.$\n}\n"), "text"));
    TIDE_CHECK(!offers(complete("input PlayerInput\n{\n    float2 move;\n\n    Sample(Devices devices)\n    {\n"
                                "        if (devices.keyboard.$\n    }\n}\nscene Main { }\n"),
                       "text"));
    TIDE_CHECK(!offers(complete("scene Main { }\ncomponent Body { float x; }\n"
                                "system Move(Devices devices, mut Body body)\n{\n    body.x += devices.keyboard.$\n}\n"),
                       "text"));
    TIDE_CHECK(offers(complete("scene Main { }\nview Chat(Devices devices)\n{\n    var n = devices.keyboard.text.$\n}\n"), "length"));
    open_document("scene Main { }\nlocal singleton Chat { string line; }\n"
                  "view Type(Devices devices, mut Chat chat)\n{\n    chat.line += devices.keyboard.te$xt;\n}\n");
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
    TIDE_CHECK(!offers(filter, "Arena"));

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
    TIDE_CHECK(offers(function, "Menu") && offers(function, "Arena") && offers(function, "Session"));
    TIDE_CHECK(!offers(function, "Grid2"));
    const char *after_mut = complete(TYPES "void Heal(mut $)\n{\n}\n");
    TIDE_CHECK(offers(after_mut, "Stats") && offers(after_mut, "float2") && !offers(after_mut, "mut"));
    TIDE_CHECK(!offers(after_mut, "Action"));
    TIDE_CHECK(offers(after_mut, "Menu") && offers(after_mut, "Arena") && offers(after_mut, "Session"));
    TIDE_CHECK(offers(complete(TYPES "async void Load(Stats s, $)\n{\n}\n"), "Arena"));
    TIDE_CHECK(offers(complete(TYPES "void Heal(Stats $)\n{\n}\n"), "stats"));
#undef TYPES

    const char *method = complete("singleton Arena { int size; }\nlocal singleton Menu { bool open; }\n"
                                  "struct Stats\n{\n    float health;\n    bool Near($) { return true; }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(method, "mut") && offers(method, "bool") && offers(method, "string") && offers(method, "Entity"));
    TIDE_CHECK(!offers(method, "Math") && !offers(method, "Action"));
    TIDE_CHECK(offers(method, "Arena") && offers(method, "Menu") && offers(method, "Session"));
    TIDE_CHECK(offers(complete("struct Stats\n{\n    float health;\n    mut void Set(mut $) { }\n}\nscene Main { }\n"), "float"));
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
    TIDE_CHECK(offers(complete(PARTS "system S(Kit kit)\n{\n    foreach (var p in kit.parts) Use(p.$size);\n}\n"), "size"));
    TIDE_CHECK(offers(complete(PARTS "system S(Kit kit)\n{\n    foreach (var p in kit.parts) Use(p.$)\n}\n"), "size"));
    TIDE_CHECK(offers(complete(PARTS "system S(Kit kit)\n{\n    foreach (var p in kit.parts) Use($);\n}\n"), "p"));
    TIDE_CHECK(offers(complete(PARTS "system S(Kit kit)\n{\n    for (var i = 0; i < 3; i++) Use($);\n}\n"), "i"));
    TIDE_CHECK(offers(complete("singleton F { Grid2<int> cells; }\nscene Main { }\nvoid Use(int2 p) { }\n"
                               "system S(mut F f)\n{\n    parallel (var at in f.cells) Use($);\n}\n"),
                      "at"));
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
    const char *value = complete("scene Main { }\nsystem S()\n{\n    var x = $\n}\n");
    TIDE_CHECK(offers(value, "float3") && !offers(value, "bool") && !offers(value, "List"));
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

    const char *members = complete("const int FIRST = 3;\nconst float HALF = 0.5;\nenum Page\n{\n    Title,\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(!offers(members, "float3") && !offers(members, "Math") && !offers(members, "FIRST"));
    const char *value = complete("const int FIRST = 3;\nconst float HALF = 0.5;\nenum Page : byte\n{\n    Title = $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(value, "FIRST") && !offers(value, "HALF") && !offers(value, "float3"));

    TIDE_CHECK(offers(complete("scene Main { }\nsystem S()\n{\n    foreach (var x $\n}\n"), "in"));
    TIDE_CHECK(offers(complete("scene Main { }\nsystem S()\n{\n    foreach (var x i$\n}\n"), "in"));
#define FIELD "singleton F { Grid2<int> cells; }\nscene Main { }\n"
    const char *parallel = complete(FIELD "system S(mut F f)\n{\n    parallel (var at in f.cells $\n}\n");
    TIDE_CHECK(offers(parallel, "by") && offers(parallel, "offset"));
    const char *offset = complete(FIELD "system S(mut F f)\n{\n    parallel (var at in f.cells by 2 $\n}\n");
    TIDE_CHECK(!offers(offset, "by") && offers(offset, "offset"));
    TIDE_CHECK(!offers(complete(FIELD "system S(mut F f)\n{\n    parallel (var at in f.cells by $\n}\n"), "offset"));
#undef FIELD

    TIDE_CHECK(offers(complete("singleton F { Grid2<int> cells = $ }\nscene Main { }\n"), "Grid2"));
    TIDE_CHECK(!offers(complete("singleton F { int cells = $ }\nscene Main { }\n"), "Grid2"));
    TIDE_CHECK(!offers(complete("struct S\n{\n    $\n}\nscene Main { }\n"), "Grid2"));
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
    check_repeat_at("file:///test.tide", "textDocument/hover", cursor_line, cursor_character, "");
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 3);

    open_document(GAME_TYPES "view V(Body body)\n{\n    Draw.Cir$cle(body.position, 1, Color.red);\n}\n");
    const char *function = request("textDocument/hover");
    TIDE_CHECK(has(function, "Draw.Circle(float2 center, float radius, Color color)"));
    TIDE_CHECK(has(function, "A filled circle."));

    open_document(GAME_TYPES "view V(Body body)\n{\n    Draw.Circle(Draw.Measure$Text(\"a\", 12), 1, Color.red);\n}\n");
    const char *measure = request("textDocument/hover");
    TIDE_CHECK(has(measure, "Draw.MeasureText(string text, float size, Font font = Font.Regular) -> float2"));
    TIDE_CHECK(has(measure, "the width of its widest line in x and the height of its lines in y"));

    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.position = Math.Nor$malize(body.position);\n}\n");
    const char *normalize = request("textDocument/hover");
    TIDE_CHECK(has(normalize, "Math.Normalize(float2 vector)"));
    TIDE_CHECK(has(normalize, "gives zero, never NaN"));
    TIDE_CHECK(has(normalize, "gives the identity, never NaN"));

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

// Meshes: Draw.Mesh, Draw.Clip, Draw.Screen, the built-in Vertex and Filter, and the draw list a view passes to C.
TIDE_TEST(lsp_meshes)
{
    start();
#define MESH_TYPES                                                                 \
    "scene Main { }\n"                                                             \
    "extern void DrawUI(DrawList list);\n"                                         \
    "local singleton Art\n{\n    Grid2<Color> pixels = Grid2(4, 4);\n    List<Vertex> corners;\n}\n"
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw.SetScreen();\n    Draw.SetClip(Rect(0, 0, 4, 4));\n"
                             "    Draw.Mesh(art.corners, [0, 1, 2], art.pixels, Filter.Point);\n"
                             "    Draw.Mesh([Vertex { position = float2(1, 2) }], [0, 0, 0]);\n"
                             "    Draw.SetCamera(float3(0, 0, -5), quaternion.identity, 60);\n"
                             "    Draw.Mesh([Vertex3 { position = float3(1, 2, 3) }], [0, 0, 0], float4x4.identity);\n"
                             "    Draw.SetClip();\n    DrawUI(Draw.list);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *draw = complete(MESH_TYPES "view V(Art art)\n{\n    Draw.$\n}\n");
    TIDE_CHECK(offers(draw, "Mesh") && offers(draw, "Clip") && offers(draw, "Screen") && offers(draw, "list"));
    TIDE_CHECK(offers(draw, "SetClip") && offers(draw, "SetScreen") && offers(draw, "SetCamera") && offers(draw, "Transform"));
    TIDE_CHECK(has(draw, "Mesh(${1:vertices}, ${2:indices})"));
    TIDE_CHECK(has(draw, "Draw.list: DrawList"));
    const char *filter = complete(MESH_TYPES "view V(Art art)\n{\n    Draw.Mesh(art.corners, [0, 1, 2], art.pixels, Filter.$);\n}\n");
    TIDE_CHECK(offers(filter, "Point") && offers(filter, "Bilinear"));
    const char *corner = complete(MESH_TYPES "view V(Art art)\n{\n    var v = Vertex { $ };\n}\n");
    TIDE_CHECK(offers(corner, "position") && offers(corner, "uv") && offers(corner, "color"));
    const char *corner3 = complete(MESH_TYPES "view V(Art art)\n{\n    var v = Vertex3 { $ };\n}\n");
    TIDE_CHECK(offers(corner3, "position") && offers(corner3, "uv") && offers(corner3, "color"));
    const char *param = complete(MESH_TYPES "extern void Paint($);\n");
    TIDE_CHECK(offers(param, "DrawList") && offers(param, "Vertex"));
    const char *tide_param = complete(MESH_TYPES "void Paint($) { }\n");
    TIDE_CHECK(!offers(tide_param, "DrawList") && offers(tide_param, "Vertex"));

    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw.Me$sh(art.corners, [0, 1, 2], art.pixels);\n}\n");
    const char *mesh = request("textDocument/hover");
    TIDE_CHECK(has(mesh, "Draw.Mesh(List<Vertex> vertices, List<int> indices, Texture texture, Filter filter = the texture's, "
                         "Wrap wrap = the texture's)"));
    TIDE_CHECK(has(mesh, "triangles, three of `indices` each"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw.SetCam$era(float3(0, 0, 0), quaternion.identity, 60);\n}\n");
    const char *camera = request("textDocument/hover");
    TIDE_CHECK(has(camera, "Draw.SetCamera(float3 position, quaternion rotation, float fieldOfView)"));
    TIDE_CHECK(has(camera, "Draw.SetCamera(float2 center, float size)") && has(camera, "Draw.SetCamera()"));
    TIDE_CHECK(has(camera, "The 3D camera the 3D meshes go through") && has(camera, "orthographic size"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    DrawUI(Draw.li$st);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Draw.list: DrawList"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw.Mesh(art.corners, $\n}\n");
    const char *indices = request("textDocument/signatureHelp");
    TIDE_CHECK(has(indices, "\"label\":\"Draw.Mesh(vertices, indices[, texture[, filter[, wrap]]][, vert][, frag][, values])\""));
    TIDE_CHECK(!has(indices, "\"label\":\"Draw.Mesh(vertices, indices, transform[, texture[, filter[, wrap]]][, vert][, frag])\""));
    TIDE_CHECK(has(indices, "\"activeSignature\":0,\"activeParameter\":1"));
    TIDE_CHECK(has(indices, "{\"label\":[20,27],\"documentation\":{\"kind\":\"markdown\",\"value\":\"A `List<int>`"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw.Mesh(art.corners, [0, 1, 2], $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":2"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw.Mesh(art.corners, [0, 1, 2], art.pixels, $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":3"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw.Mesh([Vertex3 { position = float3(1, 2, 3) }], [0, 0, 0], $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":2"));
    open_document(MESH_TYPES "view V(Art art)\n{\n    Draw.Mesh($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":0"));

    TIDE_CHECK(has(c_definition(MESH_TYPES "view V(Art art)\n{\n    Draw.Me$sh(art.corners, [0, 1, 2]);\n}\n"),
                   "void tide_draw_mesh_lists("));
    TIDE_CHECK(has(c_definition(MESH_TYPES "view V(Art art)\n{\n    Draw.SetCl$ip();\n}\n"), "void tide_draw_unset_clip("));
    TIDE_CHECK(has(c_definition(MESH_TYPES "view V(Art art)\n{\n    Draw.SetScr$een();\n}\n"), "void tide_draw_set_screen("));
    TIDE_CHECK(has(c_definition(MESH_TYPES "view V(Art art)\n{\n    Draw.Cl$ip(Rect(0, 0, 4, 4))\n    {\n    }\n}\n"),
                   "uint32_t tide_draw_push_clip("));
#undef MESH_TYPES
}

// The draw settings: a block form and a Set form each, in completion, hovers and rename fixes.
TIDE_TEST(lsp_draw_settings)
{
    start();
    open_document("scene Main { }\nview V()\n{\n    Draw.Blend(Blend.Add),\n    Draw.Tint(Color(1, 1, 1, 0.5)),\n"
                  "    Draw.Transform(float2(1, 2), 0.5, 2)\n    {\n        Draw.Rect(float2(0, 0), float2(1, 1), Color.red);\n"
                  "    }\n    Draw.SetBlend(Blend.Multiply);\n    Draw.SetBlend();\n    Draw.SetTransform(float4x4.identity);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *shared = complete("scene Main { }\nview V()\n{\n    Draw.$\n}\n");
    TIDE_CHECK(offers(shared, "Blend") && offers(shared, "SetBlend") && offers(shared, "Tint") && offers(shared, "SetTint"));
    TIDE_CHECK(offers(shared, "Blit") && offers(shared, "window"));
    TIDE_CHECK(has(shared, "Blend(${1:mode})\\n{\\n\\t$0\\n}") && has(shared, "\"SetBlend(${1:mode})\""));
    const char *modes = complete("scene Main { }\nview V()\n{\n    Draw.SetBlend(Blend.$);\n}\n");
    TIDE_CHECK(offers(modes, "Alpha") && offers(modes, "Add") && offers(modes, "Multiply") && offers(modes, "Replace"));

    open_document("scene Main { }\nview V()\n{\n    Draw.Ti$nt(Color.red)\n    {\n    }\n}\n");
    const char *tint = request("textDocument/hover");
    TIDE_CHECK(has(tint, "Draw.Tint(Color color, Block content)") && has(tint, "For the draws in its block"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetTi$nt(Color.red);\n}\n");
    const char *set = request("textDocument/hover");
    TIDE_CHECK(has(set, "Draw.SetTint(Color color)") && has(set, "Draw.SetTint()") && has(set, "For the draws after it"));

    open_document("scene Main { }\nview V()\n{\n    Draw.Camera(float2(0, 0), 5);\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.Camera sets it around a block") && has(last_sent(), "Draw.SetCamera(...);"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'SetCamera'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetBlend(Blend.Add)\n    {\n    }\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.SetBlend sets it for the draws that follow, and takes no block"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Blend'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw.Clip();\n    Draw.NoClip();\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.Clip() is 'Draw.SetClip()' now") && has(last_sent(), "Draw.NoClip() is 'Draw.SetClip()' now"));
    TIDE_CHECK(has(actions_at(3), "Change to 'SetClip'") && has(actions_at(4), "Change to 'SetClip'"));
}

// Depth, the 3D mesh setting: both forms, its modes, hovers, and rename fixes for each form and the old spellings.
TIDE_TEST(lsp_draw_depth)
{
    start();
    open_document("scene Main { }\nlocal singleton Look { RenderTarget target; }\nview V(Look look)\n{\n"
                  "    Draw.Blend(Blend.Add),\n    Draw.Depth(Depth.Read)\n    {\n"
                  "        Draw.Cube(float3(0, 0, 5), float3(1, 1, 1), Color.red);\n    }\n"
                  "    Draw.SetDepth(Depth.None);\n    Draw.SetDepth();\n"
                  "    look.target.Depth(Depth.Read)\n    {\n"
                  "        look.target.Cube(float3(0, 0, 5), float3(1, 1, 1), Color.red);\n    }\n"
                  "    look.target.SetDepth();\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *three = complete("scene Main { }\nview V()\n{\n    Draw.$\n}\n");
    TIDE_CHECK(offers(three, "Depth") && offers(three, "SetDepth") && offers(three, "Transform"));
    TIDE_CHECK(has(three, "Depth(${1:mode})\\n{\\n\\t$0\\n}") && has(three, "\"SetDepth(${1:mode})\""));
    TIDE_CHECK(offers(three, "Blend") && offers(three, "Clip"));
    const char *modes = complete("scene Main { }\nview V()\n{\n    Draw.SetDepth(Depth.$);\n}\n");
    TIDE_CHECK(offers(modes, "ReadWrite") && offers(modes, "Read") && offers(modes, "None"));

    open_document("scene Main { }\nview V()\n{\n    Draw.De$pth(Depth.Read)\n    {\n    }\n}\n");
    const char *around = request("textDocument/hover");
    TIDE_CHECK(has(around, "Draw.Depth(Depth mode, Block content)") && has(around, "For the draws in its block"));
    TIDE_CHECK(has(around, "nearer, and hides nothing") && has(around, "Only 3D meshes have depth"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetDe$pth(Depth.Read);\n}\n");
    const char *set = request("textDocument/hover");
    TIDE_CHECK(has(set, "Draw.SetDepth(Depth mode)") && has(set, "Draw.SetDepth()") && has(set, "For the draws after it"));

    open_document("scene Main { }\nview V()\n{\n    Draw.Depth(Depth.Read);\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.Depth sets it around a block") && has(last_sent(), "Draw.SetDepth(...);"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'SetDepth'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetDepth(Depth.Read)\n    {\n    }\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.SetDepth sets it for the draws that follow, and takes no block"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Depth'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw3D.SetDepth(Depth.Read);\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw3D.SetDepth is 'Draw.SetDepth' now") && has(last_sent(), "Draw2D and Draw3D are gone"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Draw'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw2D.Depth(Depth.Read)\n    {\n    }\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw2D.Depth is 'Draw.Depth' now"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Draw'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw3D.SetBlend(Blend.Add);\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw3D.SetBlend is 'Draw.SetBlend' now"));
    open_document("scene Main { }\ncomponent Lay$er { int z; }\n");
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"Depth\""), "built into"));
}

// Levels of detail: [LOD], and the two settings naming a level and a camera, each with both forms, on Draw and on a target.
TIDE_TEST(lsp_levels_of_detail)
{
    start();
    open_document("scene Main { }\nlocal singleton Look { RenderTarget shadow; }\nview V(Look look)\n{\n"
                  "    Draw.LOD(0)\n    {\n        Draw.Cube(float3(0, 0, 5), float3(1, 1, 1), Color.red);\n    }\n"
                  "    Draw.SetLOD(2);\n    Draw.SetLOD();\n"
                  "    Draw.LOD()\n    {\n    }\n"
                  "    look.shadow.SetLODCamera(float3(0, 0, 0), quaternion.identity, 60);\n"
                  "    look.shadow.LODCamera(Draw.CameraTransform(), Draw.CameraProjection())\n    {\n    }\n"
                  "    look.shadow.SetLODCamera();\n    look.shadow.LOD(1)\n    {\n    }\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *draw = complete("scene Main { }\nview V()\n{\n    Draw.$\n}\n");
    TIDE_CHECK(offers(draw, "LOD") && offers(draw, "SetLOD") && offers(draw, "LODCamera") && offers(draw, "SetLODCamera"));
    TIDE_CHECK(has(draw, "LOD(${1:level})\\n{\\n\\t$0\\n}") && has(draw, "\"SetLOD(${1:level})\""));
    const char *target = complete("scene Main { }\nlocal singleton Look { RenderTarget shadow; }\nview V(Look look)\n{\n"
                                  "    look.shadow.$\n}\n");
    TIDE_CHECK(offers(target, "LOD") && offers(target, "SetLODCamera"));

    open_document("scene Main { }\nview V()\n{\n    Draw.SetLO$D(1);\n}\n");
    const char *set = request("textDocument/hover");
    TIDE_CHECK(has(set, "Draw.SetLOD(int level)") && has(set, "Draw.SetLOD()") && has(set, "`0` is the full mesh"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetLODCam$era();\n}\n");
    const char *camera = request("textDocument/hover");
    TIDE_CHECK(has(camera, "Draw.SetLODCamera(float3 position, quaternion rotation, float fieldOfView)"));
    TIDE_CHECK(has(camera, "Draw.SetLODCamera(float4x4 transform, float4x4 projection)") && has(camera, "the pixels it counts are the window's"));

    open_document("scene Main { }\nview V()\n{\n    Draw.LOD(1);\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.LOD sets it around a block") && has(last_sent(), "Draw.SetLOD(...);"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'SetLOD'\",\"kind\":\"quickfix\",\"isPreferred\":true"));

    const char *offered = complete("[$] asset Texture masks = \"art/tile.png\";\nscene Main { }\n");
    TIDE_CHECK(offers(offered, "LOD") && offers(offered, "Filter"));
    open_document("[LO$D] component Rock { float3 position; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "LOD has simpler versions of a mesh made") && has(last_sent(), "'Rock' isn't a mesh"));
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "[LOD]") && has(hover, "levels of detail made as the game is built") && has(hover, "Draw.LODCamera"));
    open_document("[Lod] component Rock { float3 position; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "unknown attribute 'Lod'") && has(last_sent(), "did you mean 'LOD'?"));
}

// Cull, which side of a 3D mesh's triangles draws: both forms, its sides, hovers, and rename fixes.
TIDE_TEST(lsp_draw_cull)
{
    start();
    open_document("scene Main { }\nlocal singleton Look { RenderTarget target; Cull kept = Cull.Front; }\nview V(Look look)\n{\n"
                  "    Draw.Depth(Depth.Read),\n    Draw.Cull(Cull.None)\n    {\n"
                  "        Draw.Plane(float3(0, 0, 5), float2(1, 1), Color.red);\n    }\n"
                  "    Draw.SetCull(look.kept);\n    Draw.SetCull();\n"
                  "    look.target.Cull(Cull.Front)\n    {\n"
                  "        look.target.Cube(float3(0, 0, 5), float3(1, 1, 1), Color.red);\n    }\n"
                  "    look.target.SetCull();\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *three = complete("scene Main { }\nview V()\n{\n    Draw.$\n}\n");
    TIDE_CHECK(offers(three, "Cull") && offers(three, "SetCull") && offers(three, "Depth"));
    TIDE_CHECK(has(three, "Cull(${1:side})\\n{\\n\\t$0\\n}") && has(three, "\"SetCull(${1:side})\""));
    TIDE_CHECK(offers(three, "Tint") && offers(three, "Clip"));
    const char *sides = complete("scene Main { }\nview V()\n{\n    Draw.SetCull(Cull.$);\n}\n");
    TIDE_CHECK(offers(sides, "Back") && offers(sides, "Front") && offers(sides, "None"));
    const char *target = complete("scene Main { }\nlocal singleton Look { RenderTarget target; }\nview V(Look look)\n{\n"
                                  "    look.target.$\n}\n");
    TIDE_CHECK(offers(target, "Cull") && offers(target, "SetCull"));

    open_document("scene Main { }\nview V()\n{\n    Draw.Cu$ll(Cull.None)\n    {\n    }\n}\n");
    const char *around = request("textDocument/hover");
    TIDE_CHECK(has(around, "Draw.Cull(Cull side, Block content)") && has(around, "For the draws in its block"));
    TIDE_CHECK(has(around, "the side its corners go clockwise on") && has(around, "usually a triangle wound the other way"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetCu$ll(Cull.None);\n}\n");
    const char *set = request("textDocument/hover");
    TIDE_CHECK(has(set, "Draw.SetCull(Cull side)") && has(set, "Draw.SetCull()") && has(set, "For the draws after it"));

    open_document("scene Main { }\nview V()\n{\n    Draw.Cull(Cull.None);\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.Cull sets it around a block") && has(last_sent(), "Draw.SetCull(...);"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'SetCull'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetCull(Cull.None)\n    {\n    }\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.SetCull sets it for the draws that follow, and takes no block"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Cull'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw3D.SetCull(Cull.None);\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw3D.SetCull is 'Draw.SetCull' now"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Draw'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw2D.Cull(Cull.None)\n    {\n    }\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw2D.Cull is 'Draw.Cull' now"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetCull(Cull.Both);\n}\n");
    TIDE_CHECK(has(last_sent(), "enum 'Cull' has no member 'Both'") && has(last_sent(), "'Cull.None'"));
    open_document("scene Main { }\ncomponent Fa$ce { int z; }\n");
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"Cull\""), "built into"));
}

// Stencil, a setting of 2D and 3D draws and render targets: both forms, its two enums, and rename fixes.
TIDE_TEST(lsp_draw_stencil)
{
    start();
    open_document("scene Main { }\nlocal singleton Look { RenderTarget target; StencilOp kept = StencilOp.Invert; }\n"
                  "view V(Look look)\n{\n"
                  "    Draw.Stencil(StencilTest.Always, 1, StencilOp.Replace)\n    {\n"
                  "        Draw.Circle(float2(0, 0), 20, Color(0, 0, 0, 0));\n    }\n"
                  "    Draw.Blend(Blend.Add),\n    Draw.Stencil(StencilTest.Equal, 1)\n    {\n"
                  "        Draw.Cube(float3(0, 0, 5), float3(1, 1, 1), Color.red);\n    }\n"
                  "    Draw.SetStencil(StencilTest.NotEqual, 1, look.kept);\n    Draw.SetStencil();\n"
                  "    look.target.Stencil(StencilTest.Less, 2)\n    {\n"
                  "        look.target.Rect(float2(0, 0), float2(8, 8), Color.red);\n    }\n"
                  "    look.target.SetStencil(StencilTest.Always, 0, StencilOp.Zero);\n    look.target.SetStencil();\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *shared = complete("scene Main { }\nview V()\n{\n    Draw.$\n}\n");
    TIDE_CHECK(offers(shared, "Stencil") && offers(shared, "SetStencil") && offers(shared, "Blend"));
    TIDE_CHECK(has(shared, "Stencil(${1:test}, ${2:value})\\n{\\n\\t$0\\n}") && has(shared, "\"SetStencil(${1:test}, ${2:value})\""));
    TIDE_CHECK(offers(shared, "Clip") && offers(shared, "Cull"));
    const char *tests = complete("scene Main { }\nview V()\n{\n    Draw.SetStencil(StencilTest.$, 1);\n}\n");
    TIDE_CHECK(offers(tests, "Always") && offers(tests, "Never") && offers(tests, "Less") && offers(tests, "Equal"));
    TIDE_CHECK(offers(tests, "LessEqual") && offers(tests, "Greater") && offers(tests, "NotEqual"));
    TIDE_CHECK(offers(tests, "GreaterEqual"));
    const char *ops = complete("scene Main { }\nview V()\n{\n    Draw.SetStencil(StencilTest.Always, 1, StencilOp.$);\n}\n");
    TIDE_CHECK(offers(ops, "Keep") && offers(ops, "Replace") && offers(ops, "Increment") && offers(ops, "Decrement"));
    TIDE_CHECK(offers(ops, "Zero") && offers(ops, "Invert"));
    const char *target = complete("scene Main { }\nlocal singleton Look { RenderTarget target; }\nview V(Look look)\n{\n"
                                  "    look.target.$\n}\n");
    TIDE_CHECK(offers(target, "Stencil") && offers(target, "SetStencil") && offers(target, "Cull"));
    TIDE_CHECK(offers(target, "Blend") && offers(target, "Tint") && offers(target, "SetBlend") && !offers(target, "Blit"));
    TIDE_CHECK(!offers(target, "window") && !offers(target, "list"));

    open_document("scene Main { }\nview V()\n{\n    Draw.Sten$cil(StencilTest.Equal, 1)\n    {\n    }\n}\n");
    const char *around = request("textDocument/hover");
    TIDE_CHECK(has(around, "Draw.Stencil(StencilTest test, int value, Block content)"));
    TIDE_CHECK(has(around, "Draw.Stencil(StencilTest test, int value, StencilOp operation, Block content)"));
    TIDE_CHECK(has(around, "For the draws in its block") && has(around, "a number from 0 to 255 for each pixel"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetSten$cil(StencilTest.Equal, 1);\n}\n");
    const char *set = request("textDocument/hover");
    TIDE_CHECK(has(set, "Draw.SetStencil(StencilTest test, int value)") && has(set, "Draw.SetStencil()"));
    TIDE_CHECK(has(set, "For the draws after it"));
    open_document("scene Main { }\nlocal singleton Look { RenderTarget target; }\nview V(Look look)\n{\n"
                  "    look.target.SetSten$cil(StencilTest.Equal, 1);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Draw.SetStencil(StencilTest test, int value, StencilOp operation)"));
    open_document("scene Main { }\nlocal singleton Look { RenderTarget target; }\nview V(Look look)\n{\n"
                  "    look.target.SetStencil($);\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "StencilTest test, int value, StencilOp operation"));

    open_document("scene Main { }\nview V()\n{\n    Draw.Stencil(StencilTest.Equal, 1);\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.Stencil sets it around a block") && has(last_sent(), "Draw.SetStencil(...);"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'SetStencil'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetStencil(StencilTest.Equal, 1)\n    {\n    }\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.SetStencil sets it for the draws that follow, and takes no block"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Stencil'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw2D.SetStencil(StencilTest.Equal, 1);\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw2D.SetStencil is 'Draw.SetStencil' now"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Draw'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("extern void Over(DrawList list);\nscene Main { }\nview V()\n{\n    Over(Draw3D.list);\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw3D.list is 'Draw.list' now"));
    TIDE_CHECK(has(actions_at(4), "\"title\":\"Change to 'Draw'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetStencil(StencilTest.Same, 1);\n}\n");
    TIDE_CHECK(has(last_sent(), "enum 'StencilTest' has no member 'Same'") && has(last_sent(), "'GreaterEqual'"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetStencil(StencilTest.Always, 1, StencilOp.Add);\n}\n");
    TIDE_CHECK(has(last_sent(), "enum 'StencilOp' has no member 'Add'") && has(last_sent(), "'Increment'"));
    open_document("scene Main { }\ncomponent Ma$sk { int z; }\n");
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"StencilOp\""), "built into"));
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"StencilTest\""), "built into"));
}

// Wrap, what a uv past a texture's edges reads: completion, hovers, signature help, and errors for the wrong order.
TIDE_TEST(lsp_wrap)
{
    start();
#define WRAP_TYPES                                                                                    \
    "scene Main { }\n"                                                                                \
    "struct Corner\n{\n    float2 position;\n    float2 uv;\n}\n"                                      \
    "local singleton Art\n{\n    Grid2<Color> tiles = Grid2(4, 4);\n    RenderTarget target;\n"        \
    "    List<Vertex> flat;\n    List<Vertex3> solid;\n    List<Corner> corners;\n    Wrap kept = Wrap.Mirror;\n}\n"
#define IN_VIEW(code) WRAP_TYPES "view V(Art art)\n{\n    " code "\n}\n"
#define IN_FRAG(code) WRAP_TYPES "frag Tile(Corner vertex, out Color color, Art art)\n{\n    " code "\n}\n"
    open_document(WRAP_TYPES "frag Ti$le(Corner vertex, out Color color, Art art)\n{\n"
                             "    color = art.tiles.Sample(vertex.uv, Filter.Point, Wrap.Repeat) * art.tiles.Sample(vertex.uv);\n}\n"
                             "view V(Art art)\n{\n"
                             "    Draw.Mesh(art.flat, [0, 1, 2], art.tiles, Filter.Point, Wrap.Repeat);\n"
                             "    Draw.Mesh(art.flat, [0, 1, 2], art.target.color, Filter.Bilinear, art.kept);\n"
                             "    Draw.Mesh(art.solid, [0, 1, 2], float4x4.identity, art.tiles, Filter.Point, Wrap.Mirror);\n"
                             "    art.target.Mesh(art.flat, [0, 1, 2], art.tiles, Filter.Point, Wrap.Repeat);\n"
                             "    Draw.Mesh(art.corners, [0, 1, 2], art.tiles, Filter.Point, Wrap.Repeat);\n"
                             "    Draw.Mesh(art.corners, [0, 1, 2], Tile);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *frag = request("textDocument/hover");
    TIDE_CHECK(has(frag, "`Art.tiles` (`Filter.Point`) (`Wrap.Repeat`)") && has(frag, ", `Art.tiles`"));

    const char *members = complete(IN_VIEW("Draw.Mesh(art.flat, [0, 1, 2], art.tiles, Filter.Point, Wrap.$);"));
    TIDE_CHECK(offers(members, "Clamp") && offers(members, "Repeat") && offers(members, "Mirror") && !offers(members, "Point"));
    const char *sampled = complete(IN_FRAG("color = art.tiles.Sample(vertex.uv, Filter.Point, Wrap.$);"));
    TIDE_CHECK(offers(sampled, "Clamp") && offers(sampled, "Repeat") && offers(sampled, "Mirror"));
    const char *types = complete(WRAP_TYPES "local singleton More\n{\n    $\n}\n");
    TIDE_CHECK(offers(types, "Wrap") && offers(types, "Filter"));

    open_document(IN_VIEW("Draw.Me$sh(art.flat, [0, 1, 2], art.tiles, Filter.Point, Wrap.Repeat);"));
    const char *mesh = request("textDocument/hover");
    TIDE_CHECK(has(mesh, "Texture texture, Filter filter = the texture's, Wrap wrap = the texture's)"));
    TIDE_CHECK(!has(mesh, "Grid2<Color> texture"));
    TIDE_CHECK(has(mesh, "the texture again (`Wrap.Repeat`)"));
    open_document(IN_VIEW("Draw.Me$sh(art.solid, [0, 1, 2], float4x4.identity, art.target.color, Filter.Point, Wrap.Repeat);"));
    TIDE_CHECK(has(request("textDocument/hover"), "float4x4 transform, Texture texture, Filter filter = the texture's, "
                                                 "Wrap wrap = the texture's)"));
    open_document(IN_VIEW("Draw.Mesh(art.flat, [0, 1, 2], art.tiles, Filter.Point, $"));
    const char *after = request("textDocument/signatureHelp");
    TIDE_CHECK(has(after, "\"label\":\"Draw.Mesh(vertices, indices[, texture[, filter[, wrap]]][, vert][, frag][, values])\""));
    TIDE_CHECK(has(after, "\"activeParameter\":4") && has(after, "What a `uv` past 0 or 1 reads, written after the filter"));
    open_document(IN_VIEW("Draw.Mesh(art.solid, [0, 1, 2], float4x4.identity, art.tiles, Filter.Point, Wrap.Re$peat);"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeParameter\":5"));
    open_document(IN_VIEW("Draw.Mesh(art.flat, [0, 1, 2], art.tiles, Filter.Point, art.ke$pt);"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeParameter\":4"));
    open_document(IN_FRAG("color = art.tiles.Sample(vertex.uv, Filter.Point, $"));
    const char *sample = request("textDocument/signatureHelp");
    TIDE_CHECK(has(sample, "Grid2<Color>.Sample(float2 uv, Filter filter = the grid's, Wrap wrap = the grid's) -> Color"));
    TIDE_CHECK(has(sample, "\"activeParameter\":2") && has(sample, "the grid again (`Wrap.Repeat`)"));

    open_document(IN_VIEW("Draw.Mesh(art.flat, [0, 1, 2], art.tiles, Wrap.Repeat);"));
    TIDE_CHECK(has(last_sent(), "a mesh's wrap comes after its filter") && has(last_sent(), "one that only wraps another way says so"));
    open_document(IN_FRAG("color = art.tiles.Sample(vertex.uv, Wrap.Repeat);"));
    TIDE_CHECK(has(last_sent(), "a wrap comes after the filter: 'texture.Sample(uv, Filter.Bilinear, Wrap.Repeat)'"));
    open_document(IN_FRAG("color = art.tiles.Sample(vertex.uv, Filter.Point, art.kept);"));
    TIDE_CHECK(has(last_sent(), "the wrap is written here, 'Wrap.Clamp', 'Wrap.Repeat' or 'Wrap.Mirror'"));
    open_document(IN_VIEW("Draw.Mesh(art.flat, [0, 1, 2], art.tiles, Filter.Point, Wrap.Tile);"));
    TIDE_CHECK(has(last_sent(), "enum 'Wrap' has no member 'Tile'") && has(last_sent(), "the texture again, 'Wrap.Repeat'"));
    open_document("scene Main { }\nfloat Wrap(float value)\n{\n    return value;\n}\n");
    TIDE_CHECK(has(last_sent(), "'Wrap' is built into the engine"));
    open_document("scene Main { }\nfloat Aro$und(float value)\n{\n    return value;\n}\n");
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"Wrap\""), "built into"));
#undef IN_FRAG
#undef IN_VIEW
#undef WRAP_TYPES
}

// Filter.Trilinear: its hover on a frag that samples so, and the error for a uv a frag can't work out as it starts.
TIDE_TEST(lsp_trilinear)
{
    start();
#define MIP_TYPES                                                                                      \
    "scene Main { }\n"                                                                                 \
    "local singleton Art\n{\n    [Filter(Filter.Trilinear)]\n    Grid2<Color> tiles = Grid2(64, 64);\n" \
    "    Grid2<Color> plain = Grid2(4, 4);\n    List<Vertex> flat;\n}\n"
#define IN_FRAG(code) MIP_TYPES "frag Tile(Vertex vertex, out Color color, Art art)\n{\n    " code "\n}\n"
    open_document(MIP_TYPES "frag Ti$le(Vertex vertex, out Color color, Art art)\n{\n"
                            "    color = art.tiles.Sample(vertex.uv) * art.plain.Sample(vertex.uv, Filter.Trilinear, Wrap.Repeat);\n}\n"
                            "view V(Art art)\n{\n"
                            "    Draw.Mesh(art.flat, [0, 1, 2], art.plain, Filter.Trilinear);\n"
                            "    Draw.Mesh(art.flat, [0, 1, 2], Tile);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *frag = request("textDocument/hover");
    TIDE_CHECK(has(frag, "`Art.tiles` (`Filter.Trilinear`)") && has(frag, "`Art.plain` (`Filter.Trilinear`) (`Wrap.Repeat`)"));
    const char *members = complete(IN_FRAG("color = art.plain.Sample(vertex.uv, Filter.$);"));
    TIDE_CHECK(offers(members, "Point") && offers(members, "Bilinear") && offers(members, "Trilinear"));
    open_document(IN_FRAG("color = art.plain.Sam$ple(vertex.uv, Filter.Trilinear);"));
    TIDE_CHECK(has(request("textDocument/hover"), "smaller images too"));
    open_document(IN_FRAG("mut var uv = vertex.uv;\n    uv.x += 1;\n    color = art.tiles.Sample(uv);"));
    TIDE_CHECK(has(last_sent(), "a Sample that's Filter.Trilinear reads where the frag can work out as it starts"));
    TIDE_CHECK(has(last_sent(), "or sample with Filter.Bilinear here"));
    open_document(IN_FRAG("mut var uv = vertex.uv;\n    uv.x += 1;\n    color = art.tiles.Sample(uv, Filter.Bilinear);"));
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
#undef IN_FRAG
#undef MIP_TYPES
}

// Calls listed with commas before one block: one to a line, each starting where the first does, the brace under them.
TIDE_TEST(lsp_format_stacked_calls)
{
    start();
    static const char messy[] = "scene Main { }\nview V()\n{\nDraw.Blend(Blend.Add), Draw.Tint(Color.red),\n"
                                "        Draw.Clip(Rect(0, 0, 4, 4)) {\nDraw.Rect(float2(0, 0), float2(1, 1), Color.red);\n}\n"
                                "    GUILayout.Area(Anchor.MiddleCenter),\nGUI.Disabled(true)\n{\nGUILayout.Label(\"a\");\n}\n}\n";
    static const char expected[] = "scene Main { }\nview V()\n{\n    Draw.Blend(Blend.Add),\n    Draw.Tint(Color.red),\n"
                                   "    Draw.Clip(Rect(0, 0, 4, 4))\n    {\n"
                                   "        Draw.Rect(float2(0, 0), float2(1, 1), Color.red);\n    }\n"
                                   "    GUILayout.Area(Anchor.MiddleCenter),\n    GUI.Disabled(true)\n    {\n"
                                   "        GUILayout.Label(\"a\");\n    }\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));
    static const char others[] = "scene Main { }\ncomponent Body { float2 position; float radius; }\nevent(Spawned) Setup(with Main)\n{\n"
                                 "    Spawn(Body { position = float2(1, 2), radius = Math.Abs(1) });\n"
                                 "    var far = Math.Max(Math.Abs(1),\n        2);\n}\n";
    TIDE_CHECK(has(format_reply(others), "\"result\":[]"));
}

// The shapes with smooth edges: their Draw functions, the built-in Shape and Color.none
TIDE_TEST(lsp_shapes)
{
    start();
#define SHAPE_VIEW(body) "scene Main { }\nview V()\n{\n" body "\n}\n"
    open_document(SHAPE_VIEW("    Draw.RoundRect(float2(0, 0), float2(4, 2), 1, Color.red);\n"
                             "    Draw.Line(float2(0, 0), float2(4, 2), 0.5, Color.red);\n"
                             "    Draw.Arc(float2(0, 0), 4, 0, Math.PI, 1, Color.red);\n"
                             "    Draw.Shape(Shape { size = float2(4, 2), color2 = Color.none, soft = 2 });"));
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *draw = complete(SHAPE_VIEW("    Draw.$"));
    TIDE_CHECK(offers(draw, "RoundRect") && offers(draw, "WireRoundRect") && offers(draw, "Ellipse"));
    TIDE_CHECK(offers(draw, "WireEllipse") && offers(draw, "Arc") && offers(draw, "Shape"));
    const char *fields = complete(SHAPE_VIEW("    var s = Shape { $ };"));
    TIDE_CHECK(offers(fields, "center") && offers(fields, "size") && offers(fields, "radius") && offers(fields, "soft"));
    TIDE_CHECK(offers(fields, "outline") && offers(fields, "outlineColor") && offers(fields, "color2"));
    TIDE_CHECK(offers(complete(SHAPE_VIEW("    var c = Color.$")), "none"));

    open_document(SHAPE_VIEW("    Draw.Li$ne(float2(0, 0), float2(4, 2), 0.5, Color.red);"));
    const char *line = request("textDocument/hover");
    TIDE_CHECK(has(line, "Draw.Line(float2 from, float2 to, Color color)"));
    TIDE_CHECK(has(line, "Draw.Line(float2 from, float2 to, float width, Color color)") && has(line, "round ends"));
    open_document(SHAPE_VIEW("    Draw.Shape(Sh$ape { size = float2(4, 2) });"));
    TIDE_CHECK(has(request("textDocument/hover"), "What `Draw.Shape` draws"));
    open_document(SHAPE_VIEW("    Draw.Shape(Shape { size = float2(4, 2), so$ft = 2 });"));
    TIDE_CHECK(has(request("textDocument/hover"), "How far its edge fades"));
    open_document(SHAPE_VIEW("    var c = Color.no$ne;"));
    TIDE_CHECK(has(request("textDocument/hover"), "No color, where one can be left out"));

    TIDE_CHECK(has(c_definition(SHAPE_VIEW("    Draw.Sh$ape(Shape { size = float2(4, 2) });")), "void tide_draw_shape("));
    TIDE_CHECK(has(c_definition(SHAPE_VIEW("    Draw.Round$Rect(float2(0, 0), float2(4, 2), 1, Color.red);")),
                   "void tide_draw_round_rect("));
#undef SHAPE_VIEW
}

// Text at an anchor and in a weight: every version of Draw.Text, and the built-in Font
TIDE_TEST(lsp_text_anchor_and_font)
{
    start();
#define TEXT_VIEW(body) "scene Main { }\nview V()\n{\n" body "\n}\n"
    open_document(TEXT_VIEW("    Draw.Text(\"DUEL\", float2(0, 0), 88, Color.white, Anchor.MiddleCenter, Font.Bold);\n"
                            "    Font weight = Font.Regular;\n"
                            "    var size = Draw.MeasureText(\"DUEL\", 88, weight);\n"
                            "    Draw.Text(\"DUEL\", size, 88, Color.white, weight);"));
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *weights = complete(TEXT_VIEW("    var weight = Font.$"));
    TIDE_CHECK(offers(weights, "Regular") && offers(weights, "Bold"));

    open_document(TEXT_VIEW("    Draw.Te$xt(\"DUEL\", float2(0, 0), 88, Color.white, Font.Bold);"));
    const char *text = request("textDocument/hover");
    TIDE_CHECK(has(text, "Draw.Text(string text, float2 position, float size, Color color)"));
    TIDE_CHECK(has(text, "Draw.Text(string text, float2 position, float size, Color color, Font font)"));
    TIDE_CHECK(has(text, "Draw.Text(string text, float2 position, float size, Color color, Anchor anchor, Font font = Font.Regular)"));
    TIDE_CHECK(has(text, "Anchor.MiddleCenter") && has(text, "Font.Bold"));
    open_document(TEXT_VIEW("    var size = Draw.Measure$Text(\"DUEL\", 88, Font.Bold);\n    Draw.Rect(size, size, Color.white);"));
    TIDE_CHECK(has(request("textDocument/hover"), "Draw.MeasureText(string text, float size, Font font = Font.Regular) -> float2"));
    open_document(TEXT_VIEW("    Draw.Text(\"DUEL\", float2(0, 0), 88, Color.white, Fo$nt.Bold);"));
    TIDE_CHECK(has(request("textDocument/hover"), "A game only carries the bold weight's file when its code says"));
    open_document(TEXT_VIEW("    Draw.Text(\"DUEL\", float2(0, 0), 88, Color.white, Anc$hor.MiddleCenter);"));
    TIDE_CHECK(has(request("textDocument/hover"), "which place of a text's box is at the position"));

    TIDE_CHECK(has(c_definition(TEXT_VIEW("    Draw.Te$xt(\"DUEL\", float2(0, 0), 88, Color.white, Font.Bold);")),
                   "void tide_draw_text_at("));
#undef TEXT_VIEW
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
    "frag Shade3(Corner3 vertex, out Color color, Item3 item) { color = item.tint * vertex.position.x; }\n"
    open_document(INSTANCE_TYPES "view Show(Art art) { Draw.Instanced(art.flat, art.faces, art.items2, Place2); "
                                 "Draw.Instanced(art.space, art.faces, art.items3, Shade3); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document(INSTANCE_TYPES "view Show(Art art) { Draw.Instanced(art.flat, art.faces, $\n}\n");
    const char *flat = request("textDocument/signatureHelp");
    TIDE_CHECK(has(flat, "Draw.Instanced(vertices, indices, instances["));
    TIDE_CHECK(has(flat, "\"activeParameter\":2") && has(flat, "A List<T> of instance structs"));
    open_document(INSTANCE_TYPES "view Show(Art art) { Draw.Instanced(art.space, art.faces, $\n}\n");
    const char *space = request("textDocument/signatureHelp");
    TIDE_CHECK(has(space, "Draw.Instanced(vertices, indices, instances["));
    TIDE_CHECK(has(space, "\"activeParameter\":2") && has(space, "A List<float4x4> of transforms"));
    TIDE_CHECK(!has(space, "indices, transform["));
    const char *shaders = complete(INSTANCE_TYPES "view Show(Art art) { Draw.Instanced(art.flat, art.faces, art.items2, $); }\n");
    TIDE_CHECK(offers(shaders, "Place2") && !offers(shaders, "Shade3"));
    open_document(INSTANCE_TYPES "view Show(Art art) { Draw.Instanced(art.space, art.faces, art.items3, Shade3); }\n");
    TIDE_CHECK(has(hover_at(7, 6), "Draw.Instanced"));
    TIDE_CHECK(has(hover_at(7, 52), "An element of the draw's instance list"));
#undef INSTANCE_TYPES
}

// A draw's values, given after a vert and frag. VALUE_TYPES is 8 lines: the vert is on line 5, the frags on lines 6 and 7.
// The cameras as matrices: Draw's functions, and a target's.
TIDE_TEST(lsp_camera_matrices)
{
    start();
#define CAMERA_GAME \
    "scene Main { }\nlocal singleton Lamp { RenderTarget map; float4x4 toLamp; float4x4 place; float3x3 flat; }\n"
    open_document(CAMERA_GAME "view Cast(mut Lamp lamp)\n{\n"
                              "    lamp.toLamp = lamp.map.WorldToClip();\n"
                              "    lamp.place = Draw.CameraTransform();\n"
                              "    lamp.place = Draw.CameraProjection();\n"
                              "    lamp.flat = Draw.WorldToScreenMatrix();\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *to_clip = hover_at(4, 28);
    TIDE_CHECK(has(to_clip, "float4x4") && has(to_clip, "from the world to clip space") && has(to_clip, "the turn of z included"));
    TIDE_CHECK(has(hover_at(5, 24), "Where the current 3D camera is in the world"));
    TIDE_CHECK(has(hover_at(6, 24), "fitted to the shape of what it draws into"));
    const char *flat = hover_at(7, 24);
    TIDE_CHECK(has(flat, "float3x3") && has(flat, "The identity inside `Screen`"));

    const char *draw = complete(CAMERA_GAME "view V(mut Lamp lamp)\n{\n    Draw.$\n}\n");
    TIDE_CHECK(offers(draw, "WorldToClip") && offers(draw, "CameraTransform") && offers(draw, "CameraProjection")
               && offers(draw, "WorldToScreenMatrix"));
    const char *target = complete(CAMERA_GAME "view V(mut Lamp lamp)\n{\n    lamp.map.$\n}\n");
    TIDE_CHECK(offers(target, "WorldToClip") && offers(target, "CameraProjection") && offers(target, "WorldToScreenMatrix"));

    open_document(CAMERA_GAME "singleton Rules { float4x4 seen; }\nsystem S(mut Rules rules)\n{\n    rules.seen = Draw.WorldToClip();\n}\n");
    TIDE_CHECK(!has(last_sent(), "\"diagnostics\":[]"));
}

TIDE_TEST(lsp_draw_values)
{
    start();
#define VALUE_TYPES \
    "scene Main { }\nstruct Corner { float3 position; }\n" \
    "struct Swell { float height; Color tint; float unread; }\nstruct BlurStep { float2 along; }\n" \
    "local singleton Art { List<Corner> space; List<int> faces; RenderTarget bright; RenderTarget spread; }\n" \
    "vert Wave(mut Corner vertex, Swell swell) { vertex.position.y += swell.height; }\n" \
    "frag Shade(Corner vertex, out Color color, Swell swell) { color = swell.tint; }\n" \
    "frag Blur(Vertex vertex, out Color color, Texture source, BlurStep step) { color = source.Sample(vertex.uv + step.along); }\n"
#define VALUE_DRAWS \
    VALUE_TYPES "view Show(Art art)\n{\n" \
                "    Draw.Mesh(art.space, art.faces, float4x4.identity, Wave, Shade, Swell { height = 2 });\n" \
                "    Draw.Blit(art.bright.color, art.spread, Blur, BlurStep { along = float2(1, 0) });\n}\n"
    open_document(VALUE_DRAWS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *vert = hover_at(5, 36);
    TIDE_CHECK(has(vert, "The values of the draw") && has(vert, "sent to the GPU once for the draw"));
    TIDE_CHECK(has(vert, "height") && !has(vert, "tint") && !has(vert, "An element of the draw's instance list"));
    const char *frag = hover_at(6, 50);
    TIDE_CHECK(has(frag, "The values of the draw") && has(frag, "tint") && !has(frag, "unread"));
    TIDE_CHECK(has(hover_at(7, 68), "The values of the draw"));
    const char *shade = hover_at(6, 6);
    TIDE_CHECK(has(shade, "for each draw: ") && has(shade, "Draw.Mesh(..., Shade)") && !has(shade, "for each instance"));
    TIDE_CHECK(has(hover_at(10, 10), "A struct's value after them is the draw's values"));
    TIDE_CHECK(has(hover_at(11, 10), "[, Blend blend], frag shader[, values])") && has(hover_at(11, 10), "this blit's values"));

    open_document(VALUE_TYPES "view Show(Art art)\n{\n    Draw.Mesh(art.space, art.faces, float4x4.identity, Wave, Shade, $\n}\n");
    const char *help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "[, vert][, frag][, values])") && has(help, "\"activeParameter\":8"));
    TIDE_CHECK(has(help, "A struct's value, after the vert and the frag"));
    open_document(VALUE_TYPES "view Show(Art art)\n{\n    Draw.Mesh(art.space, art.faces, float4x4.identity, Shade, Swell { height = 2 }$);\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeParameter\":8"));
    open_document(VALUE_TYPES "view Show(Art art)\n{\n    Draw.Blit(art.bright.color, art.spread, Blur, $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "frag shader[, values])"));

    const char *mesh = complete(VALUE_TYPES "view Show(Art art)\n{\n    Draw.Mesh(art.space, art.faces, float4x4.identity, Shade, $);\n}\n");
    TIDE_CHECK(offers(mesh, "Swell") && has(mesh, "Swell { $1 }") && has(mesh, "The values this draw gives frag `Shade`"));
    TIDE_CHECK(!has(mesh, "BlurStep { $1 }"));
    const char *blit = complete(VALUE_TYPES "view Show(Art art)\n{\n    Draw.Blit(art.bright.color, art.spread, Blur, $);\n}\n");
    TIDE_CHECK(offers(blit, "BlurStep") && has(blit, "BlurStep { $1 }") && !has(blit, "Swell { $1 }"));
    const char *before = complete(VALUE_TYPES "view Show(Art art)\n{\n    Draw.Mesh(art.space, art.faces, float4x4.identity, $);\n}\n");
    TIDE_CHECK(offers(before, "Shade") && !has(before, "Swell { $1 }"));

    open_document(VALUE_TYPES "view Show(Art art)\n{\n"
                              "    Draw.Blit(art.bright.color, art.spread, Blur, Swell { height = 2 });\n}\n");
    TIDE_CHECK(has(last_sent(), "frag 'Blur' reads a 'BlurStep'") && has(last_sent(), "or the values the draw gives after its frag"));
#undef VALUE_DRAWS
#undef VALUE_TYPES
}

TIDE_TEST(lsp_shaded_drawing_helpers)
{
    start();
#define HELPER_SHADERS "scene Main { }\nstruct Look { Color tint; }\n" \
    "singleton Light { float strength; }\n" \
    "frag Shade(Vertex vertex, out Color color, Look look, Light light) { color = look.tint * light.strength; }\n"
    open_document(HELPER_SHADERS "void Show(List<Vertex> vertices, List<int> indices, Light light)\n"
                                "{ Draw.Mesh(vertices, indices, Shade, Look { tint = Color.red }); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *shaders = complete(HELPER_SHADERS "void Show(List<Vertex> vertices, List<int> indices, Light light)\n"
                                                "{ Draw.Mesh(vertices, indices, $); }\n");
    TIDE_CHECK(offers(shaders, "Shade"));
    const char *values = complete(HELPER_SHADERS "void Show(List<Vertex> vertices, List<int> indices, Light light)\n"
                                               "{ Draw.Mesh(vertices, indices, Shade, $); }\n");
    TIDE_CHECK(offers(values, "Look") && has(values, "Look { $1 }"));
    open_document(HELPER_SHADERS "void Show(List<Vertex> vertices, List<int> indices)\n"
                                "{ Draw.Mesh(vertices, indices, Shade, Look { tint = Color.red }); }\n");
    TIDE_CHECK(has(last_sent(), "must take exactly one 'Light' parameter"));
#undef HELPER_SHADERS
}

// Frags and verts. SHADER_TYPES is 31 lines; in SHADERS the vert is on line 31, the frag on line 35 and the view's Draw.Mesh on line 43.
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
    "frag Shade(BoxVertex vertex, out Color color, Box box, Sun sun, Models models)\n{\n"                \
    "    float light = Lit(vertex.normal, sun.direction);\n"                                                          \
    "    color = models.wood.Sample(vertex.uv) * box.tint * light;\n"                                                 \
    "    if (this.position.x < 10) this.Discard();\n}\n"

#define SHADERS                                                                                                       \
    SHADER_CODE                                                                                                       \
    "view DrawBox(with Box, Models models)\n{\n"                                                                      \
    "    Draw.Mesh(models.crate, models.faces, float4x4.Translate(float3(0, 0, 5)), Wave, Shade);\n}\n"

static const char *rename_at(const int line, const int character, const char *name)
{
    char extra[96];
    snprintf(extra, sizeof extra, "\"newName\":\"%s\"", name);
    return request_at("file:///test.tide", "textDocument/rename", line, character, extra);
}

// Completion in frags and verts: their words, parameters, GPU code, and names at Draw.Mesh.
TIDE_TEST(lsp_shaders_complete)
{
    start();
    const char *top = complete(SHADER_TYPES "\n$");
    TIDE_CHECK(offers(top, "frag") && offers(top, "vert"));
    TIDE_CHECK(has(top, "frag ${1:Name}(${2:Vertex} vertex, out Color color$3)"));
    TIDE_CHECK(has(top, "vert ${1:Name}(mut ${2:Vertex} vertex$3)"));

    const char *vertex = complete(SHADER_TYPES "struct Stats { float health; }\nfrag Shade($)\n{\n}\n");
    TIDE_CHECK(offers(vertex, "BoxVertex") && offers(vertex, "Vertex3") && !offers(vertex, "Stats"));
    TIDE_CHECK(!offers(vertex, "mut") && !offers(vertex, "Box") && !offers(vertex, "Pixel"));
    const char *changed = complete(SHADER_TYPES "vert Wave($)\n{\n}\n");
    TIDE_CHECK(offers(changed, "mut") && offers(changed, "BoxVertex"));
    TIDE_CHECK(offers(complete(SHADER_TYPES "vert Wave(mut $)\n{\n}\n"), "BoxVertex"));
    const char *frag = complete(SHADER_TYPES "frag Shade(BoxVertex vertex, $)\n{\n}\n");
    TIDE_CHECK(offers(frag, "out") && !offers(frag, "mut"));
    TIDE_CHECK(offers(frag, "Box") && offers(frag, "Sun") && offers(frag, "Models") && offers(frag, "Time"));
    TIDE_CHECK(!offers(frag, "Pixel") && offers(frag, "Instance"));
    TIDE_CHECK(!offers(frag, "with") && !offers(frag, "without") && !offers(frag, "World") && !offers(frag, "Devices"));
    TIDE_CHECK(!offers(frag, "BoxVertex") && !offers(frag, "float3"));
    const char *written = complete(SHADER_TYPES "frag Shade(BoxVertex vertex, out $)\n{\n}\n");
    TIDE_CHECK(offers(written, "Color") && !offers(written, "Box") && !offers(written, "Pixel") && !offers(written, "BoxVertex"));
    TIDE_CHECK(!offers(complete(SHADER_TYPES "frag Shade(BoxVertex vertex, mut $)\n{\n}\n"), "Color"));
    const char *rest = complete(SHADER_TYPES "frag Shade(BoxVertex vertex, out Color color, $)\n{\n}\n");
    TIDE_CHECK(!offers(rest, "mut") && !offers(rest, "Pixel") && offers(rest, "Sun"));
    TIDE_CHECK(offers(rest, "out") && has(rest, "one more color the frag writes"));
    const char *vert = complete(SHADER_TYPES "vert Wave(mut BoxVertex vertex, $)\n{\n}\n");
    TIDE_CHECK(offers(vert, "Instance") && offers(vert, "Time") && !offers(vert, "Pixel") && !offers(vert, "mut"));
    TIDE_CHECK(offers(vert, "out"));
    const char *hands = complete(SHADER_TYPES "struct FromLamp { float glow; }\nvert Wave(mut BoxVertex vertex, out $)\n{\n}\n");
    TIDE_CHECK(offers(hands, "FromLamp") && !offers(hands, "Color") && !offers(hands, "Box") && !offers(hands, "Time"));
    TIDE_CHECK(offers(complete(SHADER_TYPES "component Pixel { float x; }\n"
                                         "frag Shade(BoxVertex vertex, out Color color, Pixel $)\n{\n}\n"), "pixel"));
    const char *own = complete(SHADER_TYPES "component Pixel { float x; }\nfrag Shade(BoxVertex vertex, $)\n{\n}\n");
    TIDE_CHECK(has(own, "{\"label\":\"Pixel\",\"kind\":22,\"detail\":\"component\"}") && !has(own, "where its pixel is"));

#define IN_FRAG(code)                                                                                                 \
    SHADER_TYPES "extern float Noise(float x);\n"                                                                     \
    "frag Shade(BoxVertex vertex, out Color color, Box box, Models models)\n{\n    " code "\n}\n"
    const char *code = complete(IN_FRAG("var glow = box.glow;\n    $"));
    TIDE_CHECK(offers(code, "vertex") && offers(code, "color") && offers(code, "box") && offers(code, "this") && offers(code, "glow"));
    TIDE_CHECK(has(code, "A frag's `this`: where its pixel is") && has(code, "`this.Discard()`"));
    TIDE_CHECK(offers(code, "Math") && offers(code, "Lit") && offers(code, "float3") && offers(code, "bool") && offers(code, "BoxVertex"));
    TIDE_CHECK(offers(code, "if") && offers(code, "for") && offers(code, "var"));
    TIDE_CHECK(!offers(code, "Draw") && !offers(code, "GUI") && !offers(code, "Spawn") && !offers(code, "Send"));
    TIDE_CHECK(!offers(code, "Devices") && !offers(code, "Scene") && !offers(code, "Debug") && !offers(code, "Cursor"));
    TIDE_CHECK(offers(code, "while") && offers(code, "foreach") && offers(code, "switch"));
    TIDE_CHECK(offers(code, "byte") && !offers(code, "long") && !offers(code, "Entity") && !offers(code, "string") && !offers(code, "List"));
    TIDE_CHECK(!offers(code, "Noise") && !offers(code, "Box"));
    const char *value = complete(IN_FRAG("color = $"));
    TIDE_CHECK(offers(value, "box") && offers(value, "Math") && offers(value, "Lit") && !offers(value, "Draw") && !offers(value, "null"));
    TIDE_CHECK(!offers(complete(IN_FRAG("mut $")), "Entity") && offers(complete(IN_FRAG("mut $")), "float3"));

    const char *pixel = complete(IN_FRAG("var x = this.$"));
    TIDE_CHECK(offers(pixel, "position") && offers(pixel, "depth") && offers(pixel, "world") && offers(pixel, "Discard"));
    TIDE_CHECK(has(pixel, "Where the pixel is on the screen"));
    const char *position = complete(IN_FRAG("var x = this.position.$"));
    TIDE_CHECK(offers(position, "x") && offers(position, "y") && !offers(position, "z") && !offers(position, "Discard"));
    const char *world = complete(IN_FRAG("var x = this.world.$"));
    TIDE_CHECK(offers(world, "x") && offers(world, "y") && offers(world, "z"));
    TIDE_CHECK(offers(complete(IN_FRAG("var x = this.world.xy.$")), "x"));
    const char *grid = complete(IN_FRAG("color = models.wood.$"));
    TIDE_CHECK(offers(grid, "Sample") && !offers(grid, "Clear") && offers(grid, "size"));
    TIDE_CHECK(has(grid, "Grid2<Color>.Sample(float2 uv, Filter filter = the grid's, Wrap wrap = the grid's) -> Color"));
    const char *list = complete(IN_FRAG("var n = models.crate.$"));
    TIDE_CHECK(!offers(list, "Add") && offers(list, "count"));
#define IN_KEYED(code)                                                                                                \
    SHADER_TYPES "local singleton Keyed\n{\n    Map<int, Color> hues;\n    Set<int> lit;\n}\n"                          \
    "frag Shade(BoxVertex vertex, out Color color, Keyed keyed)\n{\n    " code "\n}\n"
    const char *map = complete(IN_KEYED("var n = keyed.hues.$"));
    TIDE_CHECK(offers(map, "count") && offers(map, "Has") && offers(map, "TryGet"));
    TIDE_CHECK(!offers(map, "Remove") && !offers(map, "Clear") && !offers(map, "Get"));
    const char *set = complete(IN_KEYED("var n = keyed.lit.$"));
    TIDE_CHECK(offers(set, "count") && offers(set, "Has") && !offers(set, "Add") && !offers(set, "TryGet"));
#undef IN_KEYED
    const char *named_list = complete(IN_FRAG("var crate = models.crate;\n    var n = crate.$"));
    TIDE_CHECK(!offers(named_list, "Add") && offers(named_list, "count"));
    const char *named_grid = complete(IN_FRAG("var wood = models.wood;\n    color = wood.$"));
    TIDE_CHECK(!offers(named_grid, "Clear") && offers(named_grid, "size"));
    const char *taken = complete(SHADER_TYPES "int Cell(Grid2<int> cells, int2 at)\n{\n    return cells.$\n}\n");
    TIDE_CHECK(offers(taken, "size") && !offers(taken, "Clear") && !offers(taken, "Decode"));
    TIDE_CHECK(offers(complete(IN_FRAG("color = box.$")), "tint"));
    TIDE_CHECK(offers(complete(IN_FRAG("color = Color.$")), "white") && offers(complete(IN_FRAG("var s = Math.$")), "Sin"));
    TIDE_CHECK(!offers(complete(IN_FRAG("Draw.$")), "Circle") && !offers(complete(IN_FRAG("var k = Devices.$")), "keyboard"));
    TIDE_CHECK(offers(complete(IN_FRAG("color = models.wood.Sample(vertex.uv, Filter.$")), "Point"));
#undef IN_FRAG
    const char *instance = complete(SHADER_TYPES "vert Wave(mut BoxVertex vertex, Instance instance)\n{\n    var m = instance.$\n}\n");
    TIDE_CHECK(offers(instance, "transform") && !offers(instance, "Discard"));
    TIDE_CHECK(offers(complete(SHADER_TYPES "vert Wave(mut BoxVertex vertex)\n{\n    $\n}\n"), "this"));
    const char *placed = complete(SHADER_TYPES "vert Wave(mut BoxVertex vertex)\n{\n    this.$\n}\n");
    TIDE_CHECK(offers(placed, "clip") && !offers(placed, "Discard") && !offers(placed, "position"));
    TIDE_CHECK(!offers(complete(SHADER_TYPES "void Helper()\n{\n    this.$\n}\n"), "Discard"));
    const char *cells = complete(SHADER_TYPES "view V(mut Models models)\n{\n    models.wood.$\n}\n");
    TIDE_CHECK(offers(cells, "Clear") && offers(cells, "size") && !offers(cells, "Sample"));
    const char *solid = complete("scene Main { }\nstruct Corner { float2 position; }\nlocal singleton Land { Grid3<int> solid; }\n"
                                 "frag Dig(Corner corner, out Color color, Land land)\n{\n    var s = land.solid.$\n}\n");
    TIDE_CHECK(offers(solid, "size") && !offers(solid, "Sample") && !offers(solid, "Clear"));
    TIDE_CHECK(has(solid, "int3"));

#define IN_VIEW(code)                                                                                                 \
    SHADER_CODE "struct FlatVertex { float2 position; }\n"                                                            \
    "frag Flat(FlatVertex vertex, out Color color)\n{\n    color = Color.red;\n}\n"                                   \
    "view DrawBox(with Box, Models models)\n{\n    " code "\n}\n"
    const char *named = complete(IN_VIEW("Draw.Mesh(models.crate, models.faces, float4x4.identity, $);"));
    TIDE_CHECK(offers(named, "Wave") && offers(named, "Shade") && !offers(named, "Flat") && offers(named, "models"));
    TIDE_CHECK(has(named, "{\"label\":\"Wave\",\"kind\":3,\"detail\":\"vert Wave(mut BoxVertex vertex, Time time, Instance instance)\""));
    TIDE_CHECK(offers(complete(IN_VIEW("Draw.Mesh(models.crate, models.faces, float4x4.identity, Wave, $")), "Shade"));
    TIDE_CHECK(!offers(complete(IN_VIEW("Draw.Mesh(models.crate, $")), "Shade"));
    TIDE_CHECK(!offers(complete(IN_VIEW("Draw.Circle(float2(0, 0), 1, $")), "Shade"));
#undef IN_VIEW
    TIDE_CHECK(offers(complete(SHADER_CODE "void Paint(Models models, Box box, Sun sun)\n{\n"
                                            "    Draw.Mesh(models.crate, models.faces, float4x4.identity, $\n}\n"),
                       "Shade"));
    const char *qualified = complete("namespace Effects;\nscene Main { }\nstruct V { float2 position; }\n"
                                     "local singleton Art { List<V> corners; List<int> faces; }\n"
                                     "frag Tint(V vertex, out Color color) { color = Color.red; }\n"
                                     "view Show(Art art) { Draw.Mesh(art.corners, art.faces, Effects.$); }\n");
    TIDE_CHECK(has(qualified, "{\"label\":\"Tint\",\"kind\":3,\"detail\":\"frag\"}"));
}

// Hovers: a frag or vert with what the engine sends for it, its pixel, parameters, Pixel, Instance, Discard and Sample.
TIDE_TEST(lsp_shaders_hover)
{
    start();
    open_document(SHADERS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *frag = hover_at(35, 6);
    TIDE_CHECK(has(frag, "frag Shade(BoxVertex vertex, out Color color, Box box, Sun sun, Models models)"));
    TIDE_CHECK(has(frag, "Runs on the GPU, for each pixel of a mesh a view draws through it"));
    TIDE_CHECK(has(frag, "- for each instance: `Box.tint`\\n- for each draw: `Sun.direction`\\n- as textures: `Models.wood`\\n"
                         "- of its vertex, blended across the triangle: `normal`, `uv`"));
    TIDE_CHECK(!has(frag, "glow") && !has(frag, "strength") && !has(frag, "Runs once per tick"));
    const char *vert = hover_at(43, 82);
    TIDE_CHECK(has(vert, "vert Wave(mut BoxVertex vertex, Time time, Instance instance)") && has(vert, "for each vertex"));
    TIDE_CHECK(has(vert, "- for each instance: `Instance.transform`\\n- for each draw: `Time.dt`, `Time.tick`"));
    TIDE_CHECK(has(hover_at(43, 88), "frag Shade(") && has(hover_at(31, 6), "vert Wave("));

    const char *color = hover_at(35, 40);
    TIDE_CHECK(has(color, "out Color color") && has(color, "The pixel's color, which the frag writes"));
    TIDE_CHECK(has(color, "starts with no value") && has(color, "isn't what's on the screen already"));
    TIDE_CHECK(has(color, "blended over what's behind it by its alpha"));
    const char *word = hover_at(35, 30);
    TIDE_CHECK(has(word, "out Color color") && has(word, "The pixel's color, which the frag writes"));
    TIDE_CHECK(has(hover_at(38, 5), "The pixel's color, which the frag writes"));
    const char *box = hover_at(35, 51);
    TIDE_CHECK(has(box, "sent to the GPU for each instance drawn") && has(box, "the only ones sent: `tint`.") && !has(box, "glow"));
    const char *sun = hover_at(35, 60);
    TIDE_CHECK(has(sun, "A singleton, sent to the GPU once for each draw") && has(sun, "the only ones sent: `direction`."));
    TIDE_CHECK(has(hover_at(35, 72), "Grids it samples, sent as textures: `wood`."));
    TIDE_CHECK(has(hover_at(31, 38), "the only ones sent: `dt`, `tick`."));
    const char *blended = hover_at(35, 22);
    TIDE_CHECK(has(blended, "blended across the triangle from its three corners") && has(blended, "`normal`, `uv`"));
    TIDE_CHECK(has(hover_at(31, 25), "which it changes in place"));

    const char *pixel = hover_at(39, 9);
    TIDE_CHECK(has(pixel, "Pixel this") && has(pixel, "The pixel `Shade` runs for."));
    TIDE_CHECK(has(pixel, "A frag's `this`") && has(pixel, "read-only fields are `position`, `depth` and `world`"));
    TIDE_CHECK(has(hover_at(31, 45), "record Instance") && has(hover_at(31, 54), "how the mesh was drawn"));
    TIDE_CHECK(has(hover_at(39, 15), "Field of record `Pixel`. Where the pixel is on the screen"));
    TIDE_CHECK(has(hover_at(33, 66), "The transform `Draw.Mesh` was given"));
    const char *discard = hover_at(39, 38);
    TIDE_CHECK(has(discard, "this.Discard()") && has(discard, "Leaves the pixel undrawn"));
    const char *sample = hover_at(38, 25);
    TIDE_CHECK(has(sample, "Grid2<Color>.Sample(float2 uv, Filter filter = the grid's, Wrap wrap = the grid's) -> Color") && has(sample, "Reads the grid as the texture it is"));
    TIDE_CHECK(has(hover_at(43, 12), "A vert and a frag, named last, run on the GPU"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 39, 9, ""), "\"result\":null"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/typeDefinition", 39, 9, ""), "\"result\":null"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 39, 15, ""), "\"result\":null"));

    open_document(SHADER_TYPES "float Glow(Box box) { return box.glow; }\n"
                  "frag Shade(BoxVertex vertex, out Color color, Box box, Sun sun)\n{\n"
                  "    color = Color.white * Glow(box) * this.depth * this.world.x;\n}\n");
    TIDE_CHECK(has(last_sent(), "'sun' is never used") && !has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(has(hover_at(32, 51), "the only ones sent: `tint`, `glow`."));
    TIDE_CHECK(has(hover_at(32, 60), "This code reads none of its fields, so nothing of it is sent."));
    TIDE_CHECK(has(hover_at(34, 44), "How deep the pixel is") && has(hover_at(34, 57), "Where the pixel is in the world."));

#define LOCAL_FIELDS                                                                                                   \
    SHADER_TYPES "component Crate\n{\n    float worn;\n    local float fade = 1;\n}\n"                                \
    "singleton Fog\n{\n    float density;\n    local Color tone = Color.white;\n"                                      \
    "    local Grid2<Color> noise = Grid2(4, 4);\n}\n"
    open_document(LOCAL_FIELDS "frag Fade(BoxVertex vertex, out Color color, Crate crate, Fog fog)\n{\n"
                               "    color = fog.noise.Sample(vertex.uv) * fog.tone * crate.fade;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *local = hover_at(42, 6);
    TIDE_CHECK(has(local, "- for each instance: `Crate.fade`\\n- for each draw: `Fog.tone`\\n- as textures: `Fog.noise`"));
    TIDE_CHECK(!has(local, "worn") && !has(local, "density"));
    TIDE_CHECK(has(hover_at(42, 52), "for each instance drawn") && has(hover_at(42, 52), "the only ones sent: `fade`."));
    const char *fog = hover_at(42, 63);
    TIDE_CHECK(has(fog, "the only ones sent: `tone`.") && has(fog, "Grids it samples, sent as textures: `noise`."));
    const char *fields = complete(LOCAL_FIELDS "frag Fade(BoxVertex vertex, out Color color, Crate crate)\n{\n"
                                               "    color = Color.white * crate.$\n}\n");
    TIDE_CHECK(offers(fields, "worn") && offers(fields, "fade") && has(fields, "A frag or vert reads it like any other field"));
#undef LOCAL_FIELDS

    open_document(SHADERS "system Broken(mut Box box)\n{\n    box.nope = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"severity\":1"));
    const char *unplanned = hover_at(35, 6);
    TIDE_CHECK(has(unplanned, "frag Shade(") && has(unplanned, "What it reads is worked out once the game has no errors."));
    TIDE_CHECK(!has(unplanned, "for each instance"));
    TIDE_CHECK(has(hover_at(35, 51), "Which of its fields this code reads is worked out once the game has no errors."));
}

// A frag's or vert's name at Draw.Mesh is its declaration's: navigation, renames and the call hierarchy go both ways.
TIDE_TEST(lsp_shaders_names)
{
    start();
    open_document(SHADERS);
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 43, 86, ""),
                   "\"range\":{\"start\":{\"line\":35,\"character\":5},\"end\":{\"line\":35,\"character\":10}}"));
    const char *usages = request_at("file:///test.tide", "textDocument/references", 35, 6,
                                    "\"context\":{\"includeDeclaration\":false}");
    TIDE_CHECK(count(usages, "\"uri\"") == 1 && has(usages, "{\"line\":43,\"character\":85}"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 43, 80, "\"context\":{\"includeDeclaration\":true}"),
                     "\"uri\"") == 2);
    const char *highlights = request_at("file:///test.tide", "textDocument/documentHighlight", 43, 80, "");
    TIDE_CHECK(has(highlights, "{\"range\":{\"start\":{\"line\":31,\"character\":5},\"end\":{\"line\":31,\"character\":9}},\"kind\":3}"));
    TIDE_CHECK(has(highlights, "{\"range\":{\"start\":{\"line\":43,\"character\":79},\"end\":{\"line\":43,\"character\":83}},\"kind\":2}"));

    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 43, 88, ""), "\"result\":{\"start\""));
    rename_at(43, 88, "Tint");
    const char *renamed = apply_reply(SHADERS, "file:///test.tide");
    TIDE_CHECK(has(renamed, "frag Tint(BoxVertex vertex, out Color color,") && has(renamed, ", Wave, Tint);"));
    TIDE_CHECK(!has(renamed, "Shade"));
    char copy[8192];
    snprintf(copy, sizeof copy, "%s", renamed);
    open_document(copy);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document(SHADERS);
    rename_at(31, 6, "Ripple");
    renamed = apply_reply(SHADERS, "file:///test.tide");
    TIDE_CHECK(has(renamed, "vert Ripple(mut BoxVertex vertex,") && has(renamed, ", Ripple, Shade);"));
    TIDE_CHECK(has(rename_at(31, 6, "Shade"), "'Shade' is already declared"));
    TIDE_CHECK(has(rename_at(31, 6, "DrawBox"), "'DrawBox' is already declared"));
    TIDE_CHECK(has(rename_at(31, 6, "Box"), "'Box' is already declared"));
    TIDE_CHECK(has(rename_at(41, 6, "Shade"), "'Shade' is already declared"));
    TIDE_CHECK(has(rename_at(31, 6, "frag"), "keyword") && has(rename_at(31, 6, "vert"), "keyword"));
    TIDE_CHECK(has(rename_at(35, 22, "vert"), "\"changes\""));

    static const char effects[] = "namespace Effects;\nscene Main { }\nstruct V { float2 position; }\n"
                                  "local singleton Art { List<V> corners; List<int> faces; }\n"
                                  "frag Tint(V vertex, out Color color) { color = Color.red; }\n"
                                  "view Show(Art art) { Draw.Mesh(art.corners, art.faces, Effects.Tint); }\n";
    open_document(effects);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 5, 66, ""),
                   "\"range\":{\"start\":{\"line\":4,\"character\":5}"));
    rename_at(4, 6, "Paint");
    renamed = apply_reply(effects, "file:///test.tide");
    TIDE_CHECK(has(renamed, "frag Paint(V vertex") && has(renamed, "art.faces, Effects.Paint);"));

    open_document(SHADERS);
    const char *item = request_at("file:///test.tide", "textDocument/prepareCallHierarchy", 43, 88, "");
    TIDE_CHECK(has(item, "{\"name\":\"Shade\",\"kind\":12,\"detail\":\"frag Shade(BoxVertex vertex, out Color color, Box box, Sun sun, "
                         "Models models)\""));
    TIDE_CHECK(has(item, "\"selectionRange\":{\"start\":{\"line\":35,\"character\":5},\"end\":{\"line\":35,\"character\":10}}"));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"callHierarchy/incomingCalls\",\"params\":{\"item\":{\"name\":\"Shade\","
           "\"kind\":12,\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":35,\"character\":0},\"end\":"
           "{\"line\":40,\"character\":1}},\"selectionRange\":{\"start\":{\"line\":35,\"character\":5},\"end\":"
           "{\"line\":35,\"character\":10}}}}}");
    TIDE_CHECK(has(last_sent(), "{\"from\":{\"name\":\"DrawBox\",\"kind\":12,\"detail\":\"view DrawBox(with Box, Models models)\""));
    TIDE_CHECK(has(last_sent(), "\"fromRanges\":[{\"start\":{\"line\":43,\"character\":85},\"end\":{\"line\":43,\"character\":90}}]"));
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

// The outline, workspace symbols and semantic tokens for frags and verts.
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
    TIDE_CHECK(has(tokens, ",2,0,4,11,0,0,5,5,9,1,0,6,9,2,0,0,10,6,5,3,0,8,3,11,0,0,4,5,1,8,0,6,5,5,1,"));
    TIDE_CHECK(has(tokens, ",1,8,4,11,0,0,5,8,7,10,")); // this.position: keyword and read-only engine property
    TIDE_CHECK(has(tokens, ",4,9,0,0,6,5,9,0]"));     // Wave and Shade at Draw.Mesh: functions

    open_document("scene Main { }\nint Twice(int frag)\n{\n    var vert = frag * 2;\n    return vert;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(hover_at(1, 15), "int frag") && has(hover_at(3, 9), "int vert"));
}

// Signature help at Draw.Mesh goes by what each argument is; Sample has its own.
TIDE_TEST(lsp_shaders_signature_help)
{
    start();
#define IN_VIEW(code) SHADER_CODE "view DrawBox(with Box, Models models)\n{\n    " code "\n}\n"
    open_document(IN_VIEW("Draw.Mesh(models.crate, $"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":1"));
    open_document(IN_VIEW("Draw.Mesh(models.crate, models.faces, $"));
    const char *transform = request("textDocument/signatureHelp");
    TIDE_CHECK(has(transform, "\"activeSignature\":0,\"activeParameter\":2"));
    TIDE_CHECK(has(transform, "{\"label\":[29,38],\"documentation\":{\"kind\":\"markdown\",\"value\":\"A `float4x4`: where the mesh is "
                              "in the world.\"}}"));
    open_document(IN_VIEW("Draw.Mesh(models.crate, models.faces, float4x4.identity, $"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":3"));
    open_document(IN_VIEW("Draw.Mesh(models.crate, models.faces, float4x4.identity, Wave, $"));
    const char *frag = request("textDocument/signatureHelp");
    TIDE_CHECK(has(frag, "\"activeSignature\":0,\"activeParameter\":7"));
    TIDE_CHECK(has(frag, "{\"label\":[70,74],\"documentation\":{\"kind\":\"markdown\",\"value\":\"A vert, by name"));
    TIDE_CHECK(has(frag, "{\"label\":[78,82],\"documentation\":{\"kind\":\"markdown\",\"value\":\"A frag, by name"));
    TIDE_CHECK(has(frag, "A vert and a frag, named last, run on the GPU"));
    open_document(IN_VIEW("Draw.Mesh(models.crate, models.faces, float4x4.identity, Wa$ve, Shade);"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeParameter\":6"));
    open_document(IN_VIEW("Draw.Mesh(models.crate, models.faces, float4x4.identity, Sha$de);"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeParameter\":7"));
#undef IN_VIEW

    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, out Color color, Models models)\n{\n"
                               "    color = models.wood.Sample(vertex.uv, $\n}\n");
    const char *sample = request("textDocument/signatureHelp");
    TIDE_CHECK(has(sample, "\"label\":\"Grid2<Color>.Sample(float2 uv, Filter filter = the grid's, Wrap wrap = the grid's) -> Color\"") && has(sample, "\"activeParameter\":1"));
}

// Formatting, folding, selection, inlay hints and moving to a file treat a frag or vert like a function.
TIDE_TEST(lsp_shaders_format_fold_and_move)
{
    start();
    static const char messy[] = SHADER_TYPES "frag Shade( BoxVertex vertex,out Color color,Box box ){\ncolor=box.tint;\n"
                                             "if(vertex.uv.x>0.5){color=Color.red;}\n}\n"
                                             "vert Wave(mut BoxVertex vertex,Time time) {\nvertex.position.y+=time.dt;\n}\n";
    static const char expected[] = SHADER_TYPES "frag Shade(BoxVertex vertex, out Color color, Box box)\n{\n"
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
    const char *ranges = request_at("file:///test.tide", "textDocument/selectionRange", 38, 26,
                                    "\"positions\":[{\"line\":38,\"character\":26}]");
    TIDE_CHECK(has(ranges, "\"result\":[{\"range\":{\"start\":{\"line\":38,\"character\":24},\"end\":{\"line\":38,\"character\":30}}"));
    TIDE_CHECK(has(ranges, "\"parent\":{\"range\":{\"start\":{\"line\":36,\"character\":0},\"end\":{\"line\":40,\"character\":1}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":35,\"character\":0},\"end\":{\"line\":40,\"character\":1}}}}"));
    const char *move = actions_at(35);
    TIDE_CHECK(has(move, "\"title\":\"Move 'Shade' to Shade.tide\",\"kind\":\"refactor.move\""));
    TIDE_CHECK(has(move, "\"newText\":\"frag Shade(BoxVertex vertex, out Color color, Box box, Sun sun, Models models)\\n{\\n"));
    TIDE_CHECK(has(move, "{\"range\":{\"start\":{\"line\":35,\"character\":0},\"end\":{\"line\":41,\"character\":0}},"
                         "\"newText\":\"\"}"));
    TIDE_CHECK(has(actions_at(32), "Move 'Wave' to Wave.tide") && !has(actions_at(42), "Move '"));

    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, out Color color, Models models)\n{\n"
                               "    var texel = models.wood.Sample(vertex.uv);\n"
                               "    color = texel * Lit(vertex.normal, float3(0, -1, 0));\n}\n"
                               "view DrawBox(with Box, Models models)\n{\n"
                               "    Draw.Mesh(models.crate, models.faces, float4x4.identity, Shade);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":50,\"character\":0}}");
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":33,\"character\":13},\"label\":\": Color\",\"kind\":1"));
    TIDE_CHECK(!has(hints, "\"label\":\"texture:\"") && !has(hints, "\"label\":\"filter:\""));
}

// Whether the diagnostics just published have one over `length` characters
// from (line, character), with LSP severity (error 1, warning 2, information 3), that starts with `message`.
static bool diagnosed(const int line, const int character, const int length, const int severity, const char *message)
{
    char start[512];
    snprintf(start, sizeof start,
             "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}},\"severity\":%d,"
             "\"source\":\"tidec\",\"message\":\"%s",
             line, character, line, character + length, severity, message);
    return has(last_sent(), start);
}

// The checker's errors about frags and verts reach the editor where they are.
TIDE_TEST(lsp_shaders_diagnostics)
{
    start_with("{\"capabilities\":{\"textDocument\":{\"publishDiagnostics\":{\"relatedInformation\":true}}}}");
    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, out Color color, Sun sun)\n{\n    color = Color.red;\n}\n");
    TIDE_CHECK(diagnosed(31, 50, 3, 2, "'sun' is never used") && has(last_sent(), "\"tags\":[1]"));
    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, out Color color)\n{\n    Draw.Clear(Color.red);\n"
                               "    color = Color.red;\n}\n");
    TIDE_CHECK(diagnosed(33, 9, 5, 1, "a frag runs on the GPU, for each pixel, so it can't draw"));
    open_document(SHADER_TYPES "float Count(float to)\n{\n    mut float x = 0;\n    string label = \"far\";\n    return x + to;\n}\n"
                               "frag Shade(BoxVertex vertex, out Color color)\n{\n"
                               "    color = Color.white * Count(vertex.uv.x);\n}\n");
    TIDE_CHECK(diagnosed(34, 11, 5, 1, "string can't go to the GPU"));
    TIDE_CHECK(has(last_sent(), "\"relatedInformation\":[{\"location\":{\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":37,"
                                "\"character\":5},\"end\":{\"line\":37,\"character\":10}}},\"message\":\"frag 'Shade' runs on the GPU, "
                                "and 'Count' with it, as it calls it\"}]"));
    TIDE_CHECK(!has(last_sent(), "\\nnote: frag 'Shade' runs on the GPU"));
    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, mut Box box)\n{\n}\n");
    TIDE_CHECK(diagnosed(31, 5, 5, 1, "frag 'Shade' has no pixel to write"));
    TIDE_CHECK(diagnosed(31, 29, 3, 1, "a frag reads 'Box', and can't change it: drop 'mut'"));
    open_document(SHADER_CODE "view DrawBox(with Box, Models models)\n{\n"
                              "    Draw.Mesh(models.crate, models.faces, float4x4.identity, Shade, Wave);\n}\n");
    TIDE_CHECK(diagnosed(43, 61, 5, 1, "the vert comes before the frag"));
    open_document(SHADER_CODE "view DrawBox(Models models)\n{\n"
                              "    Draw.Mesh(models.crate, models.faces, float4x4.identity, Wave, Shade);\n}\n");
    TIDE_CHECK(diagnosed(43, 67, 5, 1, "view 'DrawBox' doesn't guarantee its entity has a 'Box', which frag 'Shade' reads"));
    open_document(SHADER_TYPES "frag Shade(BoxVertex vertex, out Color color, Models models)\n{\n"
                               "    mut var all = models.faces;\n    color = Color.white;\n}\n");
    TIDE_CHECK(diagnosed(33, 12, 3, 1, "a list's local in code the GPU runs names one list, for good: 'all' can't be 'mut'"));
}

// instance.Normal(normal), in a frag and a vert. The frag is on line 31, after SHADER_TYPES, and its code on line 33.
TIDE_TEST(lsp_instance_normal)
{
    start();
#define LIT(code)                                                                                                     \
    SHADER_TYPES "frag Glow(BoxVertex vertex, out Color color, Instance instance)\n{\n    " code "\n    color = Color.white;\n}\n"
    const char *members = complete(LIT("var normal = instance.$"));
    TIDE_CHECK(offers(members, "transform") && offers(members, "Normal") && !offers(members, "Discard"));
    TIDE_CHECK(has(members, "Instance.Normal(float3 normal) -> float3") && has(members, "as it is in the world, of length 1"));
    const char *in_vert = complete(SHADER_TYPES "vert Turn(mut BoxVertex vertex, Instance instance)\n{\n    vertex.normal = instance.$\n}\n");
    TIDE_CHECK(offers(in_vert, "Normal"));

    open_document(LIT("color = Color.white * instance.Normal(vertex.normal).y;"));
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = hover_at(33, 37);
    TIDE_CHECK(has(hover, "Instance.Normal(float3 normal) -> float3") && has(hover, "A normal of the mesh as it is in the world"));
    TIDE_CHECK(has(hover, "Nothing more is sent for it"));
    TIDE_CHECK(has(hover_at(31, 46), "float3 Normal(float3 normal);"));
    open_document(LIT("color = Color.white * instance.Normal($"));
    const char *help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "\"label\":\"Instance.Normal(float3 normal) -> float3\"") && has(help, "\"activeParameter\":0"));

    open_document(LIT("color = Color.white * instance.Rotate(vertex.normal).y;"));
    TIDE_CHECK(diagnosed(33, 35, 6, 1, "an Instance has one method, 'Normal(normal)'"));
    open_document(LIT("color = Color.white * instance.Normal(vertex.uv).y;"));
    TIDE_CHECK(diagnosed(33, 35, 6, 1, "Normal takes a normal of the mesh, a float3 of any length"));
#undef LIT
}

// The type of what's at the cursor, and an event's or the input's code.
TIDE_TEST(lsp_type_definition_and_implementation)
{
    start();
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    bo$dy.radius = 1;\n}\n");
    TIDE_CHECK(has(request("textDocument/typeDefinition"), "\"range\":{\"start\":{\"line\":0,\"character\":10}"));
    open_document("struct Part { float size; }\ncomponent Kit { List<Part> parts; }\nscene Main { }\n"
                  "system S(Kit kit)\n{\n    var n = kit.pa$rts.count;\n}\n");
    TIDE_CHECK(has(request("textDocument/typeDefinition"), "\"range\":{\"start\":{\"line\":0,\"character\":7}"));
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.rad$ius = 1;\n}\n");
    TIDE_CHECK(has(request("textDocument/typeDefinition"), "\"result\":null"));

    open_document(GAME_TYPES "event Hit\n{\n    int damage = 1;\n}\n\nsystem Strike(with Body)\n{\n"
                  "    this.Send(Hit { damage = 2 });\n}\n\nevent(Hit hit) TakeHit(mut Body body)\n{\n"
                  "    body.radius -= hit.damage;\n}\n\nevent(Spawned) Grow(mut Body body)\n{\n    body.radius += 1;\n}\n");
    const char *handlers = request_at("file:///test.tide", "textDocument/implementation", 22, 7, "");
    TIDE_CHECK(has(handlers, "\"range\":{\"start\":{\"line\":32,\"character\":15}"));
    const char *spawned = request_at("file:///test.tide", "textDocument/implementation", 37, 8, "");
    TIDE_CHECK(has(spawned, "{\"line\":18,\"character\":15}") && has(spawned, "{\"line\":37,\"character\":15}"));
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
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 0, 0, ""), "\"result\":null"));
    const char *item = request_at("file:///test.tide", "textDocument/prepareCallHierarchy", 15, 5, "");
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
    check_repeat_at("file:///test.tide", "textDocument/prepareCallHierarchy", 15, 5, "");
    check_repeat_at("file:///test.tide", "callHierarchy/incomingCalls", 6, 6,
                    "\"item\":{\"uri\":\"file:///test.tide\",\"selectionRange\":{\"start\":{\"line\":6,\"character\":6}}}");
    check_repeat_at("file:///test.tide", "callHierarchy/outgoingCalls", 12, 7,
                    "\"item\":{\"uri\":\"file:///test.tide\",\"selectionRange\":{\"start\":{\"line\":12,\"character\":7}}}");
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 6, 6, ""), "float Heal(mut Stats stats, float amount)"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 6, 6,
                                "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 3);

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
    TIDE_CHECK(has(c_definition(IN_VIEW("Draw.Cir$cle(body.position, 1, Color.red);")), "void tide_draw_circle("));
    TIDE_CHECK(has(c_definition(IN_VIEW("Draw.Circle(body.position, 1, Color.r$ed);")), "#define TIDE_COLOR_RED"));
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
    TIDE_CHECK(strcmp(c_definition(GAME_TYPES "system S(Ti$me time) { }\n"), "") == 0);

    static const char constructor[] = "input PlayerInput\n{\n    bool fire;\n\n    Sample(Devices devices)\n    {\n"
                                      "        var keys = devices.key$board;\n        fire = keys.space.pressed;\n    }\n}\n"
                                      "scene Main { }\n";
    TIDE_CHECK(has(c_definition(constructor), "tide_keyboard keyboard;"));
    static const char key[] = "input PlayerInput\n{\n    bool fire;\n\n    Sample(Devices devices)\n    {\n"
                              "        var keys = devices.keyboard;\n        fire = keys.spa$ce.pressed;\n    }\n}\n"
                              "scene Main { }\n";
    TIDE_CHECK(has(c_definition(key), "X(space)"));
    static const char button[] = "input PlayerInput\n{\n    bool fire;\n\n    Sample(Devices devices)\n    {\n"
                                 "        fire = devices.mouse.left.pre$ssed;\n    }\n}\nscene Main { }\n";
    TIDE_CHECK(has(c_definition(button), "bool pressed;"));
    static const char devices[] = "input PlayerInput\n{\n    bool fire;\n\n    Sample(Devi$ces devices)\n    {\n"
                                  "        fire = devices.mouse.left.pressed;\n    }\n}\nscene Main { }\n";
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
    check_repeat_at("file:///test.tide", "textDocument/documentSymbol", 0, 0, "");
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 0, 10, ""), "component Body"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 0, 10,
                                "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 3);
}

TIDE_TEST(lsp_semantic_tokens)
{
    start();
    open_document(GAME_TYPES);
    const char *reply = request("textDocument/semanticTokens/full");
    // `Body` on line 0, column 10: a struct (2), declared (1).
    TIDE_CHECK(has(reply, "\"data\":[0,10,4,2,1,"));
    check_repeat_at("file:///test.tide", "textDocument/semanticTokens/full", 0, 0, "");
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 0, 10, ""), "component Body"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 0, 10,
                                "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 2);

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
        "component Spark { int framesLeft = 2; }\n"
        "local singleton Menu { bool open; }\n"
        "view Trail(with Body, mut Menu menu) { local Spawn(Spark); menu.open = true; }\n"
        "view Fade(mut local Spark spark) { spark.framesLeft -= 1; if (spark.framesLeft < 0) this.Destroy(); }\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    open_document(GAME_TYPES "component Sp$ark { int framesLeft = 2; }\nview Fade(mut local Spark spark) { spark.framesLeft -= 1; }\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "component Spark"));
    TIDE_CHECK(has(hover, "component Spark"));

    const char *spawn = complete(GAME_TYPES "component Spark { int framesLeft = 2; }\nview Trail(Body body)\n{\n    $\n}\n");
    TIDE_CHECK(offers(spawn, "local Spawn"));
    TIDE_CHECK(offers(spawn, "Spark"));
    TIDE_CHECK(offers(spawn, "Body"));
    const char *entity = complete(GAME_TYPES "component Spark { int framesLeft = 2; }\n"
                                  "view Fade(local Spark spark)\n{\n    this.$\n}\n");
    TIDE_CHECK(offers(entity, "Destroy"));
    TIDE_CHECK(offers(complete(GAME_TYPES "\n$"), "component"));

    open_document(game);
    TIDE_CHECK(has(request("textDocument/documentSymbol"), "\"name\":\"Spark\",\"detail\":\"component\""));
}

TIDE_TEST(lsp_shared_component_placement_and_parenting)
{
    start();
    open_document("component Marker { LocalEntity Self() { return th$is; } }\n"
                  "scene Main { }\nview Setup() { local Spawn(Marker); }\nview Show(local Marker marker) { _ = marker.Self(); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "LocalEntity this"));
    const char *methods = complete("scene Main { }\nview Show(local Transform transform)\n{\n"
                                  "    Debug.Log($$\"{transform.localPosition}\"); this.$\n}\n");
    TIDE_CHECK(offers(methods, "SetParent"));
    open_document("scene Main { }\nview Show(local Transform transform)\n{\n"
                  "    Debug.Log($$\"{transform.localPosition}\"); this.SetParent($);\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "LocalEntity parent"));
    const char *world = complete("scene Main { }\nview Show(local Transform transform, World world)\n{\n"
                                "    Debug.Log($$\"{transform.localPosition}\"); world.$\n}\n");
    TIDE_CHECK(offers(world, "Parent") && offers(world, "LocalToWorld"));
    open_document("component Marker { int value; }\nscene Main { }\n"
                  "event(Spawned) Setup(with Main) { Spawn(Marker); }\n"
                  "view SetupLocal() { local Spawn(Marker); }\n"
                  "view Show(local Mar$ker marker) { Debug.Log($$\"{marker.value}\"); }\n");
    const char *rename = request_with("textDocument/rename", "\"newName\":\"Shared\"");
    TIDE_CHECK(count(rename, "\"newText\":\"Shared\"") == 4);
}

TIDE_TEST(lsp_commands)
{
    start();
    static const char prefix[] = GAME_TYPES
        "command Bump { float amount; }\n"
        "command(Bump bump, PlayerID sender) Handle(Owner owner, mut Body body) { if (sender != owner.player) return; body.radius += bump.amount; }\n";
    open_document(prefix);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"Bump\",\"detail\":\"command\""));
    TIDE_CHECK(has(symbols, "\"name\":\"Handle\",\"detail\":\"command handler\""));
    open_document(GAME_TYPES "command Bump { float amount; }\n"
                  "command(Bump bump, PlayerID sender) Han$dle(Owner owner, mut Body body) { if (sender != owner.player) return; body.radius += bump.amount; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "command(Bump bump, PlayerID sender) Handle(Owner owner, mut Body body)"));
    open_document(GAME_TYPES "command Bump { float amount; }\n"
                  "command(Bump bump, PlayerID sender) Handle(Owner owner, mut Body body) { if (sen$der != owner.player) return; body.radius += bump.amount; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "authenticated by the connection"));
    const char *trigger = complete(GAME_TYPES "command Bump { float amount; }\nevent Hit { }\ncommand($)\n");
    TIDE_CHECK(offers(trigger, "Bump"));
    TIDE_CHECK(!offers(trigger, "Hit"));
    TIDE_CHECK(!offers(trigger, "Spawned"));
    const char *sender = complete(GAME_TYPES "command Bump { }\ncommand(Bump bump, $)\n");
    TIDE_CHECK(offers(sender, "PlayerID"));
    const char *entity = complete(GAME_TYPES "command Bump { }\nview Menu(with Body) { this.$ }\n");
    TIDE_CHECK(offers(entity, "Send"));
    TIDE_CHECK(!offers(entity, "Destroy"));
    const char *top = complete(GAME_TYPES "\n$");
    TIDE_CHECK(offers(top, "command"));
    TIDE_CHECK(offers(top, "command handler"));
}

// `local` before a field of a match component or singleton: this machine's, outside the match.
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
    const char *literal = complete(LOCAL_FIELDS "system Reset(mut Box box)\n{\n    box = Box { $ };\n}\n");
    TIDE_CHECK(offers(literal, "pos") && !offers(literal, "verts"));
    TIDE_CHECK(offers(complete(LOCAL_FIELDS "local event($)\n"), "Forgotten"));

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

    open_document(LOCAL_FIELDS "system Slide(mut Box box) { box.bakes += 1; }\n");
    TIDE_CHECK(has(last_sent(), "it's this machine's, and the match can't see it"));
    open_document(LOCAL_FIELDS "view Show(mut Box box) { box.bakes += 1; box.pos.x = 0; }\n");
    TIDE_CHECK(has(last_sent(), "a view can't change the match, and 'pos' is a match field of 'Box'"));

    static const char read_only[] = LOCAL_FIELDS "view Show(Box box) { box.bakes += 1; }\n";
    open_document(read_only);
    TIDE_CHECK(has(last_sent(), "'box' is read-only"));
    TIDE_CHECK(has(actions_at(13), "Declare 'box' as mut"));
    TIDE_CHECK(has(apply_first_action(read_only), "view Show(mut Box box) { box.bakes += 1; }"));
    static const char unused[] = LOCAL_FIELDS "view Show(mut Box box) { if (box.bakes > 0) Draw.Clear(Color.black); }\n";
    open_document(unused);
    TIDE_CHECK(has(last_sent(), "'box' is declared mut but never written"));
    TIDE_CHECK(has(actions_at(13), "Remove 'mut' from 'box'"));
    TIDE_CHECK(has(apply_first_action(unused), "view Show(Box box) { if"));
    static const char twice[] = "local singleton Spark\n{\n    float2 position;\n    local int frames;\n}\nscene Main { }\n";
    open_document(twice);
    TIDE_CHECK(has(last_sent(), "'Spark' is local already, and so is every field of it"));
    TIDE_CHECK(has(actions_at(3), "Remove 'local'"));
    TIDE_CHECK(has(apply_first_action(twice), "{\n    float2 position;\n    int frames;\n}"));

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

// A switch on an enum completes by its members, and whole cases where one can start.
TIDE_TEST(lsp_enum_assignment_completion_and_shorthand)
{
    start();
#define ENUM_SOURCE "enum ZombieState { Idle, Run, Jump }\n" \
                    "enum Other { Idle }\nsingleton Zombie { ZombieState state; }\nscene Main { }\n"
    const char *reply = complete(ENUM_SOURCE "system Animate(mut Zombie zombie) { zombie.state = $; }\n");
    TIDE_CHECK(has(reply, "\"label\":\"ZombieState\",\"kind\":13,\"detail\":\"enum (expected type)\",\"sortText\":\"000\",\"preselect\":true"));
    TIDE_CHECK(offers(reply, "ZombieState.Idle") && offers(reply, "ZombieState.Run"));
    TIDE_CHECK(count(reply, "\"preselect\":true") == 1);
    check_repeat_at("file:///test.tide", "textDocument/completion", cursor_line, cursor_character, "");
    reply = complete(ENUM_SOURCE "system Animate(mut Zombie zombie) { switch (zombie.state) { case ZombieState.Jump: zombie.state = $; break; } }\n");
    TIDE_CHECK(has(reply, "\"label\":\"ZombieState\",\"kind\":13,\"detail\":\"enum (expected type)\",\"sortText\":\"000\",\"preselect\":true"));
    reply = complete(ENUM_SOURCE "system Animate(mut Zombie zombie) { zombie.state = .$; }\n");
    TIDE_CHECK(offers(reply, "Idle") && offers(reply, "Run") && offers(reply, "Jump"));
    TIDE_CHECK(count(reply, "\"label\"") == 3);
    reply = complete(ENUM_SOURCE "system Animate(mut Zombie zombie) { zombie.state = .Ru$; }\n");
    TIDE_CHECK(offers(reply, "Run") && count(reply, "\"label\"") == 3);
    reply = complete(ENUM_SOURCE "ZombieState Next() { return .$; }\n");
    TIDE_CHECK(offers(reply, "Run") && count(reply, "\"label\"") == 3);
    reply = complete(ENUM_SOURCE "view V() { ZombieState state = .$; Debug.Log(state); }\n");
    TIDE_CHECK(offers(reply, "Run") && count(reply, "\"label\"") == 3);
    reply = complete(ENUM_SOURCE "system Animate(mut Zombie zombie) { switch (zombie.state) { case .$: break; } }\n");
    TIDE_CHECK(offers(reply, "Run") && count(reply, "\"label\"") == 3);
    reply = complete(ENUM_SOURCE "system Animate(mut Zombie zombie) { switch (zombie.state) { case .Idle: break; case .$: break; } }\n");
    TIDE_CHECK(has(reply, "\"label\":\"Run\",\"kind\":20,\"sortText\":\"00001\",\"preselect\":true"));
    reply = complete(ENUM_SOURCE "system Animate(mut Zombie zombie) { if (zombie.state == .$) Debug.Log(1); }\n");
    TIDE_CHECK(offers(reply, "Run") && count(reply, "\"label\"") == 3);
    reply = complete(ENUM_SOURCE "ZombieState Pick(ZombieState state) { return state; }\nview V() { Debug.Log(Pick(.$)); }\n");
    TIDE_CHECK(offers(reply, "Run") && count(reply, "\"label\"") == 3);
    reply = complete(ENUM_SOURCE "ZombieState Pick(ZombieState state) { return state; }\nview V() { Debug.Log(Pick(state: .$)); }\n");
    TIDE_CHECK(offers(reply, "Run") && count(reply, "\"label\"") == 3);
    reply = complete(ENUM_SOURCE "view V() { Draw.MeasureText(\"text\", 12, .$); }\n");
    TIDE_CHECK(offers(reply, "Bold") && offers(reply, "Regular") && count(reply, "\"label\"") == 2);
    reply = complete(ENUM_SOURCE "void Add(mut List<ZombieState> states) { states.Add(.$); }\n");
    TIDE_CHECK(offers(reply, "Run") && count(reply, "\"label\"") == 3);
    reply = complete(ENUM_SOURCE "Color Sample(Texture source, float2 uv) { return source.Sample(uv, .$); }\n");
    TIDE_CHECK(offers(reply, "Point") && offers(reply, "Trilinear") && count(reply, "\"label\"") == 3);
    open_document(ENUM_SOURCE "view V() { var size = Draw.MeasureText(\"text\", 12, .Bold); Debug.Log($$\"{size}\"); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    reply = complete(ENUM_SOURCE "view V() { int n = $; Debug.Log(n); }\n");
    TIDE_CHECK(!has(reply, "\"preselect\":true"));
    open_document(ENUM_SOURCE "system Animate(mut Zombie zombie) { zombie.state = .Id$le; }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    reply = request("textDocument/hover");
    TIDE_CHECK(has(reply, "ZombieState.Idle"));
    reply = request_with("textDocument/rename", "\"newName\":\"Rest\"");
    const char *renamed = apply_reply(ENUM_SOURCE "system Animate(mut Zombie zombie) { zombie.state = .Idle; }\n", "file:///test.tide");
    TIDE_CHECK(has(renamed, "ZombieState { Rest, Run, Jump }") && has(renamed, "zombie.state = .Rest;"));
    TIDE_CHECK(has(renamed, "Other { Idle }"));
    const char *original = ENUM_SOURCE "system Animate(mut Zombie zombie)\n{\n    zombie.state = .Idle;\n}\n";
    format_reply(original);
    const char *formatted = apply_reply(original, NULL);
    TIDE_CHECK(has(formatted, "zombie.state = .Idle;"));
    TIDE_CHECK(has(format_reply(formatted), "\"result\":[]"));
    reply = complete("=.$");
    TIDE_CHECK(has(reply, "\"items\":[]"));
    clear_sent();
    lsp_free(&server);
#undef ENUM_SOURCE
}

TIDE_TEST(lsp_switch_cases_complete_enum_members)
{
    start();
#define ZOMBIES "enum ZombieState { None, Idle, Run, Jump }\nsingleton Zombie { ZombieState state; int n; }\nscene Main { }\n"                \
                "ZombieState Next(ZombieState state) { return state; }\n"
#define ZOMBIE_SWITCH "system Animate(mut Zombie zombie)\n{\n    switch (zombie.state)\n    {\n"
    const char *labels = complete(ZOMBIES ZOMBIE_SWITCH "        case $\n        case ZombieState.Jump: zombie.n = 2; break;\n"
                                  "        case ZombieState.Idle:\n        default:\n            break;\n    }\n}\n");
    TIDE_CHECK(has(labels, "{\"label\":\"ZombieState.None\",\"kind\":20,\"sortText\":\"00000\",\"preselect\":true,\"detail\":\"0\"}"));
    TIDE_CHECK(has(labels, "{\"label\":\"ZombieState.Run\",\"kind\":20,\"sortText\":\"00002\",\"detail\":\"2\"}"));
    TIDE_CHECK(has(labels, "{\"label\":\"ZombieState.Jump\",\"kind\":20,\"sortText\":\"10003\",\"detail\":\"3, which a case has\"}"));
    TIDE_CHECK(has(labels, "{\"label\":\"ZombieState.Idle\",\"kind\":20,\"sortText\":\"10001\""));
    TIDE_CHECK(count(labels, "\"label\"") == 4 && count(labels, "\"preselect\"") == 1);
    check_repeat_at("file:///test.tide", "textDocument/completion", cursor_line, cursor_character, "");
    TIDE_CHECK(count(complete(ZOMBIES ZOMBIE_SWITCH "        case Ru$\n        case ZombieState.Jump: break;\n    }\n}\n"),
                     "\"label\"") == 4);
    const char *members = complete(ZOMBIES ZOMBIE_SWITCH "        case ZombieState.$\n        case ZombieState.Jump: break;\n    }\n}\n");
    TIDE_CHECK(offers(members, "Run") && offers(members, "Jump") && count(members, "\"label\"") == 4);
    const char *chosen = complete(ZOMBIES "system Animate(mut Zombie zombie)\n{\n"
                                  "    switch (zombie.n > 0 ? zombie.state : ZombieState.Idle)\n    {\n"
                                  "        case ZombieState.Idle:\n            break;\n        $\n    }\n}\n");
    TIDE_CHECK(offers(chosen, "case ZombieState.Run:") && !offers(chosen, "case ZombieState.Idle:"));
    TIDE_CHECK(offers(complete(ZOMBIES "system Animate(mut Zombie zombie)\n{\n    switch (Next(zombie.state))\n    {\n"
                               "        case $:\n            break;\n    }\n}\n"), "ZombieState.Run"));
    TIDE_CHECK(offers(complete(ZOMBIES "system Animate(mut Zombie zombie)\n{\n    var state = zombie.state;\n    switch (state)\n    {\n"
                               "        case $\n    }\n}\n"), "ZombieState.Run"));
    TIDE_CHECK(offers(complete("namespace Game.Dead;\nenum Kind { Walker, Runner }\n" "component Body { Kind kind; }\n"
                               "scene Main { }\nsystem S(mut Body body)\n{\n    switch (body.kind)\n    {\n        case $\n    }\n}\n"),
                      "Kind.Walker"));

    const char *starts = complete(ZOMBIES ZOMBIE_SWITCH "        case ZombieState.Jump: zombie.n = 2; break;\n        $\n"
                                  "        case ZombieState.Idle:\n        default:\n            break;\n    }\n}\n");
    TIDE_CHECK(offers(starts, "case") && offers(starts, "default") && offers(starts, "zombie"));
    TIDE_CHECK(has(starts, "{\"label\":\"case ZombieState.Run:\",\"kind\":20,\"sortText\":\"case 0002\",\"detail\":\"2\"}"));
    TIDE_CHECK(offers(starts, "case ZombieState.None:") && !offers(starts, "case ZombieState.Jump:")
               && !offers(starts, "case ZombieState.Idle:") && !has(starts, "\"preselect\""));
    const char *after = complete(ZOMBIES ZOMBIE_SWITCH "        case ZombieState.Idle:\n        $\n    }\n}\n");
    TIDE_CHECK(offers(after, "case") && offers(after, "break") && offers(after, "if") && offers(after, "case ZombieState.Run:"));
    TIDE_CHECK(offers(complete(ZOMBIES ZOMBIE_SWITCH "        default:\n            $\n    }\n}\n"), "break"));
    const char *choice = complete(ZOMBIES ZOMBIE_SWITCH "        case ZombieState.Idle:\n            zombie.n = zombie.n > 0 ? 1 : $\n"
                                  "    }\n}\n");
    TIDE_CHECK(!offers(choice, "break") && !offers(choice, "case ZombieState.Run:") && offers(choice, "zombie"));
    const char *inner = complete(ZOMBIES ZOMBIE_SWITCH "        case ZombieState.Idle:\n        {\n            $\n        }\n    }\n}\n");
    TIDE_CHECK(offers(inner, "break") && !offers(inner, "case ZombieState.Run:"));

    const char *number = complete(ZOMBIES "system Animate(mut Zombie zombie)\n{\n    switch (zombie.n)\n    {\n        case $\n    }\n}\n");
    TIDE_CHECK(offers(number, "zombie") && !offers(number, "ZombieState.Run"));
    const char *number_start = complete(ZOMBIES "system Animate(mut Zombie zombie)\n{\n    switch (zombie.n)\n    {\n"
                                        "        case 1:\n            $\n    }\n}\n");
    TIDE_CHECK(offers(number_start, "break") && !has(number_start, "\"label\":\"case "));

    open_document(ZOMBIES ZOMBIE_SWITCH "        case ZombieState.Run: zombie.n = 1; break;\n        case ZombieState.Jump: zombie.n = 2; break;\n"
                  "        case ZombieState.Idle:\n        case ZombieState.None:\n        default:\n            break;\n    }\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
#undef ZOMBIE_SWITCH
#undef ZOMBIES
}

// An enum's name converts an int, Phase(5): there the name is the enum.
TIDE_TEST(lsp_enum_conversion)
{
    start();
#define PHASES "enum Phase { Warmup, Playing = 5 }\nsingleton Match { Phase phase; int n; }\nscene Main { }\n"
    static const char game[] = PHASES "system S(mut Match match)\n{\n    match.phase = Pha$se(match.n);\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "enum Phase"));
    TIDE_CHECK(has(hover, "`Phase(number)` is the `Phase` with that number"));
    TIDE_CHECK(!has(hover, "low 8 bits"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":0,\"character\":5}"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 3);
    request_with("textDocument/rename", "\"newName\":\"Stage\"");
    const char *renamed = apply_reply(game, "file:///test.tide");
    TIDE_CHECK(has(renamed, "enum Stage") && has(renamed, "Stage phase;") && has(renamed, "match.phase = Stage(match.n);"));

    open_document(PHASES "system S(mut Match match)\n{\n    match.phase = Phase($\n}\n");
    const char *signature = request("textDocument/signatureHelp");
    TIDE_CHECK(has(signature, "Phase(int number)"));
    TIDE_CHECK(has(signature, "A number none of its members has stays as it is"));
    TIDE_CHECK(offers(complete(PHASES "system S(mut Match match)\n{\n    match.phase = $\n}\n"), "Phase"));

    open_document(PHASES "system S(mut Match match)\n{\n    match.phase = Phase(5);\n}\n");
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":30,\"character\":0}}");
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":5,\"character\":24},\"label\":\"number:\",\"kind\":2"));

    open_document(PHASES "system S(mut Match match)\n{\n    match.phase = Pha$se(1.5);\n}\n");
    TIDE_CHECK(has(last_sent(), "Phase(...) converts an int, not float"));
    TIDE_CHECK(has(request("textDocument/hover"), "enum Phase"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":0,\"character\":5}"));

    open_document("enum Voxel : byte { Air, Stone }\nsingleton Terrain { Voxel under; int n; }\nscene Main { }\n"
                  "system S(mut Terrain terrain)\n{\n    terrain.under = Vox$el(terrain.n);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "its low 8 bits"));

    static const char qualified[] = "namespace Combat;\nenum Phase { Warmup, Playing }\nsingleton Match { Phase phase; int n; }\n"
                                 "scene Main { }\nsystem S(mut Match match)\n{\n    match.phase = Combat.Pha$se(match.n);\n}\n";
    open_document(qualified);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "enum Phase"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":1,\"character\":5}"));
    request_with("textDocument/rename", "\"newName\":\"Stage\"");
    renamed = apply_reply(qualified, "file:///test.tide");
    TIDE_CHECK(has(renamed, "enum Stage") && has(renamed, "match.phase = Combat.Stage(match.n);"));
    open_document("namespace Combat;\nenum Phase { Warmup, Playing }\nsingleton Match { Phase phase; int n; }\n"
                  "scene Main { }\nsystem S(mut Match match)\n{\n    match.phase = Combat.Phase($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Phase(int number)"));

    open_document(PHASES "system S(World world, mut Match match)\n{\n    match.n = int(world.Ge$t<Phase>(default));\n}\n");
    TIDE_CHECK(has(last_sent(), "'Phase' is an enum"));
    TIDE_CHECK(!has(request("textDocument/hover"), "`Phase(number)`"));
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"Stage\""), "\"error\""));
#undef PHASES
}

TIDE_TEST(lsp_enum_count_and_cast_diagnostic)
{
    start();
#define COUNT_GAME "enum Direction { North, East = 10, Count = 10 }\nsingleton Match { int n; }\nscene Main { }\n"
    const char *completion = complete(COUNT_GAME "system S(mut Match match) { match.n = Direction.$; }\n");
    TIDE_CHECK(offers(completion, "Count"));
    TIDE_CHECK(offers(completion, "Count()"));
    TIDE_CHECK(has(completion, "Count() -> int"));
    open_document(COUNT_GAME "system S(mut Match match) { match.n = Direction.Co$unt(); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "Direction.Count() -> int"));
    TIDE_CHECK(has(hover, "Compile-time constant: 3"));
    TIDE_CHECK(has(hover, "same numeric value"));
    open_document(COUNT_GAME "system S(mut Match match) { match.n = Direction.Count($); }\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Direction.Count() -> int"));
    open_document(COUNT_GAME "int Number(Direction direction) { return direction.Count($); }\n");
    TIDE_CHECK(!has(request("textDocument/signatureHelp"), "Direction.Count() -> int"));

    static const char qualified[] = "namespace Game;\nenum Direction { North, East }\n"
                                    "singleton Match { int n; }\nscene Main { }\n"
                                    "system S(mut Match match) { match.n = Game.Direc$tion.Count(); }\n";
    open_document(qualified);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":1,\"character\":5}"));
    request_with("textDocument/rename", "\"newName\":\"Heading\"");
    TIDE_CHECK(has(apply_reply(qualified, "file:///test.tide"), "Game.Heading.Count()"));

    open_document(COUNT_GAME "int Number(Direction direction) { return (int)direct$ion; }\n");
    TIDE_CHECK(has(last_sent(), "Tide converts values with 'int(value)'"));
    TIDE_CHECK(has(last_sent(), "write 'int(direction)' instead"));
    open_document(COUNT_GAME "int Number(int value) { mut int n = (value) - 1; int[2] values = [n, value];"
                             " return ((values)[0]) + (n)++; }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
#undef COUNT_GAME
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

    open_document("namespace Paint;\nenum Phases { Objects }\nscene Main { }\n"
                  "[Stage(Paint.Pha$ses.Objects)]\nview V() { }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":1,\"character\":5}"));
    completion = complete("namespace Paint;\nenum Phases { Objects }\nscene Main { }\n"
                          "[Stage(Paint.$)]\nview V() { }\n");
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
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));

    open_document("const int MAX = 100;\nsingleton Match { int n; }\nscene Main { }\n"
                  "system S(mut Match match) { match.n += M$AX; }\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "const int MAX = 100"));
    TIDE_CHECK(has(hover, "Constant: the same on every machine"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"range\":{\"start\":{\"line\":0,\"character\":10}"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 2);

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
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));

    open_document("settings\n{\n    tick$Rate = 30;\n}\nscene Main { }\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "int tickRate"));
    TIDE_CHECK(has(hover, "Without it: 60."));
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Settings are the engine's"));
    open_document("settings\n{\n    hostMig$ration = true;\n}\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "bool hostMigration"));
    open_document("settings\n{\n    sta$ts = true;\n}\nscene Main { }\n");
    hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "bool stats"));
    TIDE_CHECK(has(hover, "or F8, opens it to the rest") && has(hover, "`tide run --stats` shows them open"));
    open_document("settings\n{\n    vers$ion = \"1.2.0\";\n}\nscene Main { }\n");
    hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "string version"));
    TIDE_CHECK(has(hover, "Without it: 1.0."));

    const char *empty = complete("settings\n{\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(empty, "tickRate"));
    TIDE_CHECK(offers(empty, "title"));
    TIDE_CHECK(offers(empty, "appId"));
    TIDE_CHECK(offers(empty, "version"));
    TIDE_CHECK(!offers(empty, "float3"));
    open_document("settings { version = \"v2\"; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "isn't a version that Android and iOS both take"));
    const char *rest = complete("settings\n{\n    tickRate = 30;\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(!offers(rest, "tickRate"));
    TIDE_CHECK(offers(rest, "title"));
    TIDE_CHECK(offers(complete("const int RATE = 30;\nsettings\n{\n    tickRate = $\n}\nscene Main { }\n"), "RATE"));
    TIDE_CHECK(offers(complete("scene Main { }\n$\n"), "settings"));

    open_document("settings { tickRate = 30; }\nsettings { title = \"x\"; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *other = complete("settings { tickRate = 30; }\nsettings\n{\n    stats = true;\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(!offers(other, "tickRate") && !offers(other, "stats"));
    TIDE_CHECK(offers(other, "title"));
    TIDE_CHECK(offers(complete("settings\n{\n    $\n}\nsettings { tickRate = 30; }\nscene Main { }\n"), "title"));

    open_document(game);
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"settings\",\"detail\":\"the engine's\",\"kind\":19"));
    TIDE_CHECK(has(symbols, "\"name\":\"tickRate\",\"kind\":7"));

    open_document("settings { tickRat = 30; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "did you mean 'tickRate'?"));
}

TIDE_TEST(lsp_default_value)
{
    start();
    open_document("scene Main { }\nview Render(with Main)\n{\n    Draw.Circle(def$ault, 5, Color.yellow);\n}\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "default: float2"));
    TIDE_CHECK(has(hover, "All zeros: the default value of `float2`"));

    open_document("struct Stats { int armor = 3; }\nsingleton S { Stats stats; }\nscene Main { }\n"
                  "system Reset(mut S s) { s.stats = defa$ult; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "`Stats { }`: each field's default"));

    TIDE_CHECK(offers(complete("singleton S { int n; }\nscene Main { }\nsystem Reset(mut S s) { s.n = $ }\n"), "default"));

    static const char tidy[] = "singleton S { int n; bool on; }\nscene Main { }\n"
                               "system Pick(mut S s)\n{\n    s.n = s.on ? default : 2;\n    s.n = s.on ? 1 : default;\n}\n";
    TIDE_CHECK(has(format_reply(tidy), "\"result\":[]"));
}

// A type inside another's <> is placed where it's written, through the '>>' closing two and any spacing.
TIDE_TEST(lsp_nested_types_are_placed_from_the_tree)
{
    start();
    open_document("asset List<Grid2<Color>> frames = \"*.png\";\n"
                  "asset Map< string , List<byte> > files = \"*.bin\";\n"
                  "scene Main { }\n");
    TIDE_CHECK(has(hover_at(0, 17), "A color: r, g, b and a from 0 to 1"));
    TIDE_CHECK(has(hover_at(0, 12), "Cells at positions"));
    TIDE_CHECK(has(hover_at(0, 7), "A list of values"));
    TIDE_CHECK(has(hover_at(1, 27), "An unsigned 8-bit integer"));
    TIDE_CHECK(has(hover_at(1, 21), "A list of values"));
    TIDE_CHECK(has(hover_at(1, 7), "Values by key"));
}

// An array's type names its element before the [N].
TIDE_TEST(lsp_arrays_hover_and_count)
{
    start();
    open_document("const int CAP = 4;\n"
                  "component Ring { int[CAP] cells; float[2][3] grid; }\n"
                  "scene Main { }\n"
                  "event(Spawned) Setup(with Main) { Spawn(Ring); }\n"
                  "system Spin(mut Ring ring) { ring.cells[0] = ring.cells.cou$nt; ring.grid[1][2] = 1; }\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "How many elements the array holds: its length, a constant."));
    TIDE_CHECK(has(hover_at(1, 17), "A 32-bit integer"));
    TIDE_CHECK(has(hover_at(1, 36), "32-bit float"));
    const char *members = complete("const int CAP = 4;\ncomponent Ring { int[CAP] cells; }\nscene Main { }\n"
                                   "system S(Ring r) { var n = r.cells.$; }\n");
    TIDE_CHECK(offers(members, "count"));
    TIDE_CHECK(has(members, "How many elements the array holds: its length, a constant."));
}

TIDE_TEST(lsp_asset_reference_members)
{
    start();
    open_document("component Art { Asset<Mesh<Vertex3>> model; }\nscene Main {}\n"
                  "system Read(Art art) { Debug.Assert(art.model.Va$lue.indices.count == 0); }\n"
                  "event(Spawned) Begin(with Main) { Spawn(Art); }\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    const char *members = complete("component Art { Asset<Mesh<Vertex3>> model; }\nscene Main {}\n"
                                   "system Read(Art art) { var model = art.model.$; }\n");
    TIDE_CHECK(offers(members, "Value"));
    TIDE_CHECK(!offers(members, "id"));
    TIDE_CHECK(offers(complete("component Art { $ }\nscene Main {}\n"), "Asset"));
    TIDE_CHECK(offers(complete("component Art { Asset<Mesh<Vertex3>> model; }\nscene Main {}\n"
                             "system Read(Art art) { var model = art.model.Value.$; }\n"), "indices"));
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

// char: hover, completion, what text's index and foreach give, its default, and literals the formatter leaves as written.
TIDE_TEST(lsp_char_hover_completion_and_format)
{
    start();
    open_document("singleton Data { ch$ar mark = 'a'; string plan = \"ab\"; }\nscene Main { }\n"
                  "system Read(mut Data data)\n{\n    var first = data.plan[0];\n"
                  "    foreach (var each in data.plan) data.mark = each == first ? '{' : each;\n}\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "One character: a Unicode code point"));
    TIDE_CHECK(has(hover_at(4, 9), "char"));
    TIDE_CHECK(has(hover_at(5, 18), "char"));
    TIDE_CHECK(offers(complete("singleton Data { $ }\nscene Main { }\n"), "char"));
    open_document("singleton Data { char mark; }\nscene Main { }\nsystem Reset(mut Data data) { data.mark = def$ault; }\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "default: char") && has(hover, "which is no character"));

    static const char messy[] =
        "singleton Data { char mark; int n; }\nscene Main { }\nsystem S(mut Data data)\n{\n"
        "if(data.mark=='{'||data.mark== '\\'' )data.n=data.mark-'0';\nswitch(data.mark)\n{\ncase '}' :\ndata.n=1;\nbreak;\n}\n}\n";
    static const char expected[] =
        "singleton Data { char mark; int n; }\nscene Main { }\nsystem S(mut Data data)\n{\n"
        "    if (data.mark == '{' || data.mark == '\\'') data.n = data.mark - '0';\n    switch (data.mark)\n    {\n"
        "        case '}':\n            data.n = 1;\n            break;\n    }\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));
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
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));

    const char *widgets = complete("scene Main { }\nview V()\n{\n    GUILayout.$\n}\n");
    TIDE_CHECK(offers(widgets, "Button"));
    TIDE_CHECK(offers(widgets, "Horizontal"));
    TIDE_CHECK(offers(widgets, "Area"));
    TIDE_CHECK(offers(complete("scene Main { }\nview V()\n{\n    $\n}\n"), "GUILayout"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    $\n}\n"), "GUILayout"));
    TIDE_CHECK(offers(complete("scene Main { }\nvoid F()\n{\n    $\n}\n"), "Screen"));

    const char *claims = complete("scene Main { }\nview V()\n{\n    GUI.$\n}\n");
    TIDE_CHECK(offers(claims, "ClaimPointer"));
    TIDE_CHECK(offers(claims, "ClaimKeyboard"));
    TIDE_CHECK(offers(claims, "ShowKeyboard"));

    TIDE_CHECK(offers(claims, "Control") && offers(claims, "Navigate") && offers(claims, "SetTheme") && offers(claims, "Theme"));
    TIDE_CHECK(has(claims, "Theme(${1:theme})\\n{\\n\\t$0\\n}"));
#define CONTROL_VIEW(body) "scene Main { }\nview V()\n{\n" body "\n}\n"
    open_document(CONTROL_VIEW("    GUI.Navigate();\n    var card = GUI.Con$trol(Rect(0, 0, 200, 200));\n"
                               "    if (card.clicked || card.pressed && card.hovered) GUI.SetTheme(GUITheme { radius = 4 });\n"
                               "    GUI.Theme(GUITheme { hot = Color.red, fieldHot = Color.red })\n    {\n"
                               "        GUI.Label(Rect(0, 0, 10, 10), card.focused ? \"on\" : \"off\");\n    }"));
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *control = request("textDocument/hover");
    TIDE_CHECK(has(control, "GUI.Control(Rect rect) -> GUIControl"));
    TIDE_CHECK(has(control, "the GUI handles its input as a button's, and draws nothing"));
    const char *said = complete(CONTROL_VIEW("    var card = GUI.Control(Rect(0, 0, 200, 200));\n    var on = card.$"));
    TIDE_CHECK(offers(said, "hovered") && offers(said, "pressed") && offers(said, "focused") && offers(said, "clicked"));
    TIDE_CHECK(has(said, "a press on it let go on it"));
    const char *looks = complete(CONTROL_VIEW("    GUI.SetTheme(GUITheme { $ });"));
    TIDE_CHECK(offers(looks, "text") && offers(looks, "panel") && offers(looks, "control") && offers(looks, "hot"));
    TIDE_CHECK(offers(looks, "pressed") && offers(looks, "field") && offers(looks, "fieldHot") && offers(looks, "accent"));
    TIDE_CHECK(offers(looks, "radius"));
    open_document(CONTROL_VIEW("    GUI.Set$Theme(GUITheme { });"));
    TIDE_CHECK(has(request("textDocument/hover"), "GUI.SetTheme(GUITheme theme)"));
    open_document(CONTROL_VIEW("    GUI.The$me(GUITheme { })\n    {\n    }"));
    TIDE_CHECK(has(request("textDocument/hover"), "GUI.Theme(GUITheme theme, Block content)"));
    open_document(CONTROL_VIEW("    var look = GUIThe$me { radius = 4 };\n    GUI.SetTheme(look);"));
    TIDE_CHECK(has(request("textDocument/hover"), "a theme names what it changes"));
    open_document(CONTROL_VIEW("    GUI.Theme(GUITheme { });"));
    TIDE_CHECK(has(last_sent(), "GUI.Theme takes a block") && has(last_sent(), "GUI.SetTheme(...)"));
#undef CONTROL_VIEW
    open_document("scene Main { }\nview V()\n{\n    GUI.Claim$Pointer();\n    GUI.ClaimKeyboard();\n    GUI.ShowKeyboard();\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *claim = request("textDocument/hover");
    TIDE_CHECK(has(claim, "GUI.ClaimPointer()"));
    TIDE_CHECK(has(claim, "hidden from the input's `Sample` and from the other views"));
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
    open_document("scene Main { }\nview V() { var scale = Screen.sc$ale; }\n");
    const char *scale = request("textDocument/hover");
    TIDE_CHECK(has(scale, "Screen.scale: float2") && has(scale, "Physical pixels per logical window pixel"));

    const char *screen = complete("scene Main { }\nview V()\n{\n    Screen.$\n}\n");
    TIDE_CHECK(offers(screen, "width") && offers(screen, "height") && offers(screen, "scale") && offers(screen, "Flip"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    Screen.$\n}\n"), "Flip"));
    open_document("scene Main { }\nview V(Devices devices)\n{\n    Draw.Circle(Screen.Fl$ip(devices.pointer.position), 4, Color.white);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *flip = request("textDocument/hover");
    TIDE_CHECK(has(flip, "Screen.Flip(float2 position) -> float2"));
    TIDE_CHECK(has(flip, "counted from the screen's other edge"));

    static const char look[] = "input In\n{\n    float2 look;\n    Sample(Devices devices)\n    {\n        $\n    }\n"
                               "    Sanitize()\n    {\n        look = float2(0, 0);\n    }\n}\nscene Main { }\n";
    TIDE_CHECK(offers(complete("scene Main { }\nview V()\n{\n    $\n}\n"), "Cursor"));
    TIDE_CHECK(offers(complete("scene Main { }\nvoid F()\n{\n    $\n}\n"), "Cursor"));
    TIDE_CHECK(offers(complete(look), "Cursor"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    $\n}\n"), "Cursor"));
    const char *cursor = complete("scene Main { }\nview V()\n{\n    Cursor.$\n}\n");
    TIDE_CHECK(offers(cursor, "Lock") && offers(cursor, "locked"));
    const char *sampled = complete("input In\n{\n    float2 look;\n    Sample(Devices devices)\n    {\n        if (Cursor.$) look = devices.mouse.delta;\n"
                                   "    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(sampled, "locked") && !offers(sampled, "Lock"));
    const char *in_match = complete("scene Main { }\nsystem S()\n{\n    Cursor.$\n}\n");
    TIDE_CHECK(!offers(in_match, "locked") && !offers(in_match, "Lock"));
    static const char camera[] = "input In\n{\n    float2 look;\n    Sample(Devices devices) { if (Cursor.locked) look = devices.mouse.delta; }\n}\n"
                                 "scene Main { }\nview Look()\n{\n    Cursor.Lo$ck();\n    if (Cursor.locked) Draw.Clear(Color.black);\n}\n";
    open_document(camera);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *lock = request("textDocument/hover");
    TIDE_CHECK(has(lock, "Cursor.Lock()"));
    TIDE_CHECK(has(lock, "Call it every frame the game wants the mouse"));
    open_document("scene Main { }\nview Look()\n{\n    Cursor.Lock();\n    if (Cursor.loc$ked) Draw.Clear(Color.black);\n}\n");
    const char *locked = request("textDocument/hover");
    TIDE_CHECK(has(locked, "Cursor.locked: bool"));
    TIDE_CHECK(has(locked, "what the system says"));
    open_document("scene Main { }\nview Look()\n{\n    Cur$sor.Lock();\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "The mouse's cursor"));
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Built-in names can't be renamed"));
    open_document("scene Main { }\nsystem S()\n{\n    Cursor.Lock();\n}\n");
    TIDE_CHECK(has(last_sent(), "so it can't call Cursor.Lock"));
    open_document("scene Main { }\nview Look()\n{\n    Cursor.Lock($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Cursor.Lock()"));

    TIDE_CHECK(offers(complete("scene Main { }\nview V()\n{\n    $\n}\n"), "Frame"));
    TIDE_CHECK(offers(complete("scene Main { }\nvoid F()\n{\n    $\n}\n"), "Frame"));
    TIDE_CHECK(offers(complete("scene Main { }\nlocal event E { }\nlocal event(E) H()\n{\n    $\n}\n"), "Frame"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    $\n}\n"), "Frame"));
    TIDE_CHECK(offers(complete("scene Main { }\nview V()\n{\n    var dt = Frame.$;\n}\n"), "dt"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    var dt = Frame.$;\n}\n"), "dt"));
    open_document("local singleton Fx { float age; }\nscene Main { }\nview V(mut Fx fx) { fx.age += Frame.d$t; }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *frame = request("textDocument/hover");
    TIDE_CHECK(has(frame, "Frame.dt: float"));
    TIDE_CHECK(has(frame, "The seconds since this machine's previous frame started"));
    TIDE_CHECK(has(frame, "for a clock, add it up in a field of local state"));
    open_document("local singleton Fx { float age; }\nscene Main { }\nview V(mut Fx fx) { fx.age += Fra$me.dt; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "This machine's frame"));
    TIDE_CHECK(has(last_sent(), "There's no clock beside it"));
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Built-in names can't be renamed"));
    open_document("singleton Clock { float age; }\nscene Main { }\nsystem S(mut Clock clock)\n{\n    clock.age += Frame.dt;\n}\n");
    TIDE_CHECK(has(last_sent(), "so it can't read Frame.dt"));
    TIDE_CHECK(has(last_sent(), "time.dt"));
    open_document("local singleton Fx { float age; }\nscene Main { }\nview V(mut Fx fx)\n{\n    fx.age = Frame.t;\n}\n");
    TIDE_CHECK(has(last_sent(), "Frame has no member 't'") && has(last_sent(), "add 'Frame.dt' up in a field of local state"));
    TIDE_CHECK(has(last_sent(), "read 'time.t'") && !has(last_sent(), "did you mean"));
    TIDE_CHECK(!has(actions_at(4), "Change to 'dt'"));
    open_document("local singleton Fx { float age; }\nscene Main { }\nview V(mut Fx fx)\n{\n    fx.age = Frame.DT;\n}\n");
    TIDE_CHECK(has(last_sent(), "did you mean 'dt'?") && has(actions_at(4), "Change to 'dt'"));

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
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));

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

    TIDE_CHECK(offers(complete("enum E { A }\nint P() fails E { return 1; }\nscene Main { }\n"
                               "system S()\n{\n    if (P() is int score)\n    {\n        var x = $\n    }\n}\n"),
                      "score"));
    TIDE_CHECK(!offers(complete("enum E { A }\nint P() fails E { return 1; }\nscene Main { }\n"
                                "system S()\n{\n    if (P() is int score) { }\n    var x = $\n}\n"),
                       "score"));
    const char *in_failing = complete("enum E { A }\nint P() fails E\n{\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(in_failing, "fail"));
    TIDE_CHECK(offers(in_failing, "try"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    $\n}\n"), "fail"));

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
    static const char game[] =
        "singleton Log { int n; }\n\n"
        "async int Doubled(int x)\n{\n    await Wait.Ticks(1);\n    return x * 2;\n}\n\n"
        "async event(Spawned) Count(mut Log log)\n{\n    log.n = await Doubled(2) + 1;\n}\n\n"
        "scene Main { }\nsystem S(Log log)\n{\n    if (log.n == 0) Doubled(1);\n}\n";
    open_document(game);
    TIDE_CHECK(!has(last_sent(), "\"severity\""));
    if (has(last_sent(), "\"severity\"")) printf("%s\n", last_sent());
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));
    static const char messy[] = "async void F( )\n{\nawait Wait.Frames( 2 );\n}\nscene Main { }\n";
    static const char expected[] = "async void F()\n{\n    await Wait.Frames(2);\n}\nscene Main { }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);

    const char *in_async = complete("async void F()\n{\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(in_async, "await"));
    TIDE_CHECK(offers(in_async, "Wait"));
    TIDE_CHECK(!offers(complete("scene Main { }\nsystem S()\n{\n    $\n}\n"), "await"));
    const char *waits = complete("async void F()\n{\n    await Wait.$\n}\nscene Main { }\n");
    TIDE_CHECK(offers(waits, "Ticks") && offers(waits, "Frames") && offers(waits, "Seconds"));
    TIDE_CHECK(offers(complete("$"), "async function"));

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
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));

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
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));

    const char *members = complete("component Inventory { List<int> scores; }\nscene Main { }\nsystem S(Inventory inv)\n{\n"
                                   "    var n = inv.scores.$\n}\n");
    TIDE_CHECK(offers(members, "count"));
    TIDE_CHECK(offers(members, "Add"));
    TIDE_CHECK(offers(complete("component Inventory\n{\n    $\n}\nscene Main { }\n"), "List"));

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
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));

    const char *members = complete("singleton Field { Grid2<int> cells; }\nscene Main { }\nsystem S(Field field)\n{\n"
                                   "    var n = field.cells.$\n}\n");
    TIDE_CHECK(offers(members, "size"));
    TIDE_CHECK(offers(members, "Clear"));
    TIDE_CHECK(offers(complete("singleton Field\n{\n    $\n}\nscene Main { }\n"), "Grid2"));
    open_document("singleton Field { Grid2<int> ce$lls; }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Grid2<int> cells"));

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

// Parallel loops: completion, formatting, and hovers of their cells' places.
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
    TIDE_CHECK(!offers(complete("scene Main { }\nvoid F()\n{\n    $\n}\n"), "parallel"));
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


TIDE_TEST(lsp_empty_scene_declarations)
{
    start();
    open_document("namespace Levels { scene Are$na; }\nscene Main;\n"
                  "event(Spawned) Setup(with Main) { Scene.Load(Levels.Arena); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "scene Arena"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 2);
    TIDE_CHECK(count(request_with("textDocument/rename", "\"newName\":\"Room\""), "\"newText\":\"Room\"") == 2);

    open_document("local scene Main;\nlocal scene Menu;\n"
                  "local event(Spawned) Open(with Main) { Scene.Load(Menu); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"Main\",\"detail\":\"local scene\""));
    TIDE_CHECK(has(symbols, "\"name\":\"Menu\",\"detail\":\"local scene\""));
    const char *scenes = complete("scene Arena;\nscene Main;\nevent(Spawned) Setup(with Main) { Scene.Load($); }\n");
    TIDE_CHECK(offers(scenes, "Arena"));

    static const char messy[] = "local  scene Main ; // Menu\nnamespace Levels\n{\nscene Arena ;\n}\n";
    static const char expected[] = "local scene Main; // Menu\nnamespace Levels\n{\n    scene Arena;\n}\n";
    format_reply(messy);
    TIDE_CHECK(strcmp(apply_reply(messy, NULL), expected) == 0);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));

    open_document("scene Main;\ncomponent Body { int value; }\n"
                  "event(Spawned) Setup(with Main) { Spawn(Body); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document("scene Main;\ncomponent Body;\n");
    TIDE_CHECK(has(last_sent(), "expected '{'"));
    open_document("scene Main\n");
    TIDE_CHECK(has(last_sent(), "expected '{'"));
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
    TIDE_CHECK(!has(type, "tide_players"));

    open_document("scene Arena { }\nscene Main { }\nevent(Spawned) Setup(with Main) { Scene.Lo$ad(Arena); }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Loads a scene and returns its entity"));

    const char *calls = complete("scene Arena { }\nscene Main { }\nevent(Spawned) Setup(with Main)\n{\n    Scene.$\n}\n");
    TIDE_CHECK(offers(calls, "Load"));
    TIDE_CHECK(offers(calls, "Unload"));
    TIDE_CHECK(!offers(calls, "AddPlayer"));
    TIDE_CHECK(!offers(calls, "RemovePlayer"));
    const char *fields = complete("scene Arena { int size; }\nscene Main { }\nsystem S(Arena arena)\n{\n    var x = arena.$\n}\n");
    TIDE_CHECK(offers(fields, "size"));
    TIDE_CHECK(!offers(fields, "tide_visibility"));

    open_document(game);
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"Arena\",\"detail\":\"scene\""));
    TIDE_CHECK(!has(symbols, "tide_players"));
    open_document("scene Arena { }\nscene Main { }\n"
                  "event(Spawned) Setup(with Main) { var group = Visibility.CreateGroup(Audience.None); "
                  "Scene.Lo$ad(Arena, Visibility { group = group }); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *load = request("textDocument/hover");
    TIDE_CHECK(has(load, "Scene.Load(scene, components...)"));
    TIDE_CHECK(has(load, "before its `Spawned` handlers"));
    TIDE_CHECK(!has(load, "SceneVisibility"));
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
    "component Spark { int left; }\n"                                    \
    "scene Main { }\n"                                                         \
    "system Die(Health health) { if (health.IsMine(th$is)) this.Destroy(); }\n" \
    "view Fade(mut local Spark spark) { spark.left -= 1; if (spark.left <= 0) th$is.Destroy(); }\n"

// this: the entity the code runs for, and whose component a method is called on.
TIDE_TEST(lsp_this)
{
    start();
    open_document(THIS_GAME);
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));

    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 3, 41, ""), "The entity whose `Health` this is."));
    const char *system = request_at("file:///test.tide", "textDocument/hover", 7, 48, "");
    TIDE_CHECK(has(system, "Entity this") && has(system, "The entity `Die` runs for."));
    const char *view = request_at("file:///test.tide", "textDocument/hover", 8, 74, "");
    TIDE_CHECK(has(view, "LocalEntity this"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 7, 48, ""), "'this' is a keyword"));

    TIDE_CHECK(offers(complete("component Body { int x; }\nscene Main { }\nsystem S(Body body)\n{\n    $\n}\n"), "this"));
    TIDE_CHECK(!offers(complete("component Body { int x; }\nscene Main { }\nsystem S(Time time)\n{\n    $\n}\n"), "this"));
    TIDE_CHECK(offers(complete("component Body { int x; }\nscene Main { }\nsystem S(with Body)\n{\n    this.$\n}\n"),
                      "Destroy"));

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
    TIDE_CHECK(offers(fields, "invite"));

    open_document("local scene Main { }\nview Menu()\n{\n    Session.Le$ave();\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Leaves the match"));

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

    TIDE_CHECK(offers(complete("struct Stats { float health; }\ncomponent Unit\n{\n    $\n}\n"), "Stats"));
    TIDE_CHECK(offers(complete(STRUCTS "system Hurt(mut Unit unit)\n{\n    unit.stats.$\n}\n"), "health"));
    TIDE_CHECK(offers(complete(STRUCTS "system Hurt(mut Unit unit)\n{\n    unit.stats = Stats { $ };\n}\n"), "health"));

    open_document("struct St$ats\n{\n    float health = 100;\n}\nscene Main { }\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "struct Stats"));
    TIDE_CHECK(has(hover, "float health = 100;"));

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

TIDE_TEST(lsp_user_properties)
{
    start();
#define PROPERTY_GAME "struct Gauge { int stored; int amount { get { return stored; } set { stored = value; } } }\nscene Main;\n"
    open_document(PROPERTY_GAME "system Tick() { mut var g = Gauge {}; g.amount = 3; _ = g.am$ount; }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *property_hover = request("textDocument/hover");
    TIDE_CHECK(has(property_hover, "int amount { get; set; }"));
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"amount\",\"detail\":\"int amount { get; set; }\",\"kind\":7"));
    TIDE_CHECK(count(symbols, "\"name\":\"amount\"") == 1);
    const char *renamed = request_with("textDocument/rename", "\"newName\":\"level\"");
    TIDE_CHECK(count(renamed, "\"newText\":\"level\"") == 3);
    const char *members = complete(PROPERTY_GAME "system Tick() { var g = Gauge {}; _ = g.$amount; }\n");
    TIDE_CHECK(offers(members, "amount") && has(members, "\"label\":\"amount\",\"kind\":10"));
    TIDE_CHECK(!has(members, "amount("));
    open_document("scene Main;\nview Show(Transform t) { _ = t.po$sition; }\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(has(request("textDocument/hover"), "float3 position { get; set; }"));
    open_document("struct Gauge { int amount { } }\nscene Main;\nsystem Tick() { mut var g = Gauge {}; g.amount = 1; }\n");
    TIDE_CHECK(has(last_sent(), "\"severity\":1"));
    open_document("struct Gauge { int amount { get } }\nscene Main;\nsystem Tick() { mut var g = Gauge {}; g.amount = 1; }\n");
    TIDE_CHECK(has(last_sent(), "\"severity\":1"));
#undef PROPERTY_GAME
}

TIDE_TEST(lsp_methods_and_functions)
{
    start();
    open_document(METHODS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"IsDead\",\"detail\":\"bool IsDead()\",\"kind\":6"));
    TIDE_CHECK(has(symbols, "\"name\":\"Heal\",\"detail\":\"function\",\"kind\":12"));

    const char *members = complete(METHODS "system Fight(mut Unit unit)\n{\n    unit.stats.$\n}\n");
    TIDE_CHECK(offers(members, "IsDead") && offers(members, "Hurt") && offers(members, "health"));
    TIDE_CHECK(has(members, "mut void Hurt(float amount)"));
    TIDE_CHECK(offers(complete(METHODS "system Fight(mut Unit unit)\n{\n    $\n}\n"), "Heal"));
    TIDE_CHECK(offers(complete("struct Stats\n{\n    float health;\n    bool IsDead() { return $ }\n}\nscene Main { }\n"),
                      "health"));

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

    open_document(METHODS "system Fight(mut Unit unit)\n{\n    var left = Heal(unit.stats, 2);\n}\n");
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":30,\"character\":0}}");
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":20,\"character\":12},\"label\":\": float\",\"kind\":1"));
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":20,\"character\":32},\"label\":\"amount:\",\"kind\":2"));
    TIDE_CHECK(!has(hints, "\"stats:\""));

    open_document(METHODS);
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"hrt\"}}");
    TIDE_CHECK(has(last_sent(), "\"name\":\"Hurt\",\"kind\":6") && has(last_sent(), "\"containerName\":\"Stats\""));
    TIDE_CHECK(!has(last_sent(), "\"name\":\"Heal\""));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"\"}}");
    TIDE_CHECK(has(last_sent(), "\"name\":\"Heal\",\"kind\":12") && has(last_sent(), "\"name\":\"Unit\",\"kind\":23"));

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
    "extern int Fill(out List<byte> bytes, int count);\n"                     \
    "\n"                                                                       \
    "component Ground { float height; }\n"                                     \
    "scene Main { }\n"                                                         \
    "event(Spawned) Setup(with Main)\n"                                        \
    "{\n"                                                                      \
    "    Spawn(Ground);\n"                                                     \
    "}\n"

TIDE_TEST(lsp_singleton_methods_and_presented_view_time)
{
    start();
#define SINGLETON_METHODS "settings { maxRewind = 0.25; }\n" \
        "singleton Physics { float distance; bool Raycast(float value) { return value > distance; }\n" \
        "mut void Reset() { distance = 0; } }\ninput Controls { bool fire; }\nscene Main { }\n"
    const char *members = complete("singleton Physics { float distance; bool Raycast(float value) { return value > distance; } }\n"
        "scene Main { }\nview Show(Physics physics) { physics.$ }\n");
    TIDE_CHECK(offers(members, "Raycast") && offers(members, "distance"));
    TIDE_CHECK(offers(complete("singleton Physics { float distance; bool Raycast(float value) { return $ } }\nscene Main { }\n"), "distance"));
    open_document(SINGLETON_METHODS "system Fire(Controls controls, Physics physics) { if (controls.fire && controls.viewTime.valid) physics.Raycast(1); }\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    open_document(SINGLETON_METHODS "view Show(Physics physics) { physics.Ray$cast(1); }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Method of singleton `Physics`"));
    open_document(SINGLETON_METHODS "system Change(Physics physics) { physics.Reset(); }\n");
    TIDE_CHECK(has(last_sent(), "read-only"));
    TIDE_CHECK(offers(complete(SINGLETON_METHODS "system Fire(Controls controls) { controls.viewTime.$ }\n"), "valid"));
#undef SINGLETON_METHODS
}

TIDE_TEST(lsp_extern_functions)
{
    start();
    open_document(EXTERNS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

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

#define FILLED "singleton Store { List<byte> image; int size; }\n"
    open_document(EXTERNS FILLED "system Make(mut Store store)\n{\n    store.size = Fi$ll(out store.image, 4);\n}\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\""));
    const char *fill = request("textDocument/hover");
    TIDE_CHECK(has(fill, "extern int Fill(out List<byte> bytes, int count)") && has(fill, "C function `Fill`"));
    open_document(EXTERNS FILLED "system Make(mut Store store)\n{\n    store.size = Fill($out store.image, 4);\n}\n");
    const char *filling = request("textDocument/signatureHelp");
    TIDE_CHECK(has(filling, "Fill(out List<byte> bytes, int count)") && has(filling, "\"activeParameter\":0"));
    open_document(EXTERNS FILLED "system Make(Store store, mut Ground ground)\n{\n    ground.height = Fill(out store.image, 4);\n}\n");
    TIDE_CHECK(has(last_sent(), "'store' is read-only") && has(last_sent(), "'Fill' makes its 'bytes'"));
    open_document("extern int Fill(out List<byte> by$tes, int count);\nscene Main { }\n");
    const char *made = request("textDocument/hover");
    TIDE_CHECK(has(made, "List<byte> bytes") && has(made, "the caller's list, which C makes") && has(made, "tide_list_out"));
    TIDE_CHECK(has(made, "A call says `out` before its argument"));
    open_document("extern bool Half(int value, out int ha$lf);\nscene Main { }\n");
    const char *pointer = request("textDocument/hover");
    TIDE_CHECK(has(pointer, "out int half") && has(pointer, "which C makes: it gets a pointer") && !has(pointer, "tide_list_out"));
    open_document(EXTERNS FILLED "system Make(mut Store store)\n{\n    store.size = Fill(store.image, 4);\n}\n");
    TIDE_CHECK(has(last_sent(), "'Fill' makes its 'bytes', so the argument says 'out'"));
    open_document(EXTERNS FILLED "system Make(mut Store store)\n{\n    Fill(out store.image, 4);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
#undef FILLED

    TIDE_CHECK(offers(complete(EXTERNS "$"), "extern"));
    TIDE_CHECK(offers(complete(EXTERNS "extern $"), "float"));
    const char *params = complete(EXTERNS "extern float Sum($");
    TIDE_CHECK(offers(params, "in") && offers(params, "mut") && offers(params, "out") && offers(params, "float3"));
    TIDE_CHECK(has(params, "C makes the caller's variable: it gets a pointer to write through, or for a list, a tide_list_out"));
    TIDE_CHECK(offers(complete(EXTERNS "extern float Sum(in $"), "float3"));
    const char *after_out = complete(EXTERNS "extern int Make(int count, out $");
    TIDE_CHECK(offers(after_out, "List") && offers(after_out, "float3") && offers(after_out, "bool"));
    TIDE_CHECK(!offers(after_out, "string") && !offers(after_out, "out") && !offers(after_out, "DrawList"));
    TIDE_CHECK(!offers(complete(EXTERNS "extern int Make(int out $"), "List"));
    TIDE_CHECK(offers(complete(EXTERNS "[$"), "NativeName"));
    TIDE_CHECK(offers(complete(EXTERNS "system Shape(mut Ground ground)\n{\n    $\n}\n"), "Noise"));

    open_document("scene Main { }\nextern void Log(List<string> lines);\n");
    TIDE_CHECK(has(last_sent(), "C functions can't take lists of text yet"));

    static const char messy[] = "extern  float Noise( float x ,float y ) ;\nextern int Fill( out  List<byte> bytes ,in float3 v ) ;\n"
                                "scene Main { }\n";
    static const char expected[] = "extern float Noise(float x, float y);\nextern int Fill(out List<byte> bytes, in float3 v);\n"
                                   "scene Main { }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}

#define EXTERN_SYSTEMS                                                         \
    "component Place { float2 at; }\n"                                         \
    "component Speed { float2 by; }\n"                                         \
    "component Frozen { }\n"                                                   \
    "singleton Wind { float2 by; }\n"                                          \
    "singleton Tally { int moved; }\n"                                         \
    "scene Main { }\n"                                                         \
    "event(Spawned) Setup(with Main)\n"                                        \
    "{\n"                                                                      \
    "    Spawn(Place, Speed);\n"                                               \
    "}\n"                                                                      \
    "system Blow(mut Speed speed, Wind wind)\n"                                \
    "{\n"                                                                      \
    "    speed.by += wind.by;\n"                                               \
    "}\n"

TIDE_TEST(lsp_extern_systems)
{
    start();
#define MOVE "[NativeName(\"game_move\")]\nextern system Move(mut Place place, Speed speed, Time time, mut Tally tally, without Frozen);\n"
    open_document(EXTERN_SYSTEMS MOVE);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    open_document(EXTERN_SYSTEMS "[NativeName(\"game_move\")]\nextern system Mo$ve(mut Place place, Speed speed, Time time, mut Tally tally, "
                                 "without Frozen);\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "[NativeName(\\\"game_move\\\")]\\nextern system Move(mut Place place, Speed speed, Time time, mut Tally "
                          "tally, without Frozen)"));
    TIDE_CHECK(has(hover, "A system written in C: the C function `game_move`") && has(hover, "`tide/rows.h`"));
    TIDE_CHECK(has(hover, "for each run of rows of the entities that match"));
    TIDE_CHECK(has(hover, "**Stage 2.** It waits for:\\n- `Blow`: it writes `Speed`, which this reads"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"range\":{\"start\":{\"line\":15,\"character\":14}"));
    const char *lenses = request("textDocument/codeLens");
    TIDE_CHECK(has(lenses, "\"title\":\"stage 2 ") && has(lenses, " in C") &&has(lenses, "\"after Blow: it writes Speed, which this reads\""));
    const char *symbols = request("textDocument/documentSymbol");
    TIDE_CHECK(has(symbols, "\"name\":\"Move\",\"detail\":\"extern system\",\"kind\":12"));
    TIDE_CHECK(count(request("textDocument/foldingRange"), "\"startLine\"") == 2);
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), ",1,14,4,9,1,0,9,5,2,0,0,6,5,5,1,"));

    open_document(EXTERN_SYSTEMS "extern system Move(mut Place pl$ace, Speed speed, Time time, mut Tally tally, without Frozen);\n");
    const char *place = request("textDocument/hover");
    TIDE_CHECK(has(place, "mut Place place") && has(place, "Component 0 of the rows C is handed, which it can change"));
    TIDE_CHECK(has(place, "`tide_rows_floats_mut(rows, 0, offsetof(...))`"));
    open_document(EXTERN_SYSTEMS "extern system Move(mut Place place, Speed sp$eed, Time time, mut Tally tally, without Frozen);\n");
    const char *speed = request("textDocument/hover");
    TIDE_CHECK(has(speed, "Component 1 of the rows C is handed, read-only") && has(speed, "`tide_rows_floats(rows, 1, offsetof(...))`"));
    open_document(EXTERN_SYSTEMS "extern system Move(mut Place place, Speed speed, Time ti$me, mut Tally tally, without Frozen);\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Singleton, read-only: C gets a pointer to it, argument 1 after the rows."));
    open_document(EXTERN_SYSTEMS "extern system Move(mut Place place, Speed speed, Time time, mut Tally ta$lly, without Frozen);\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Singleton, writable: C gets a pointer to it, argument 2 after the rows."));
    open_document(EXTERN_SYSTEMS "extern system Mo$ve(mut Place place);\n");
    TIDE_CHECK(has(request("textDocument/hover"), "the C function `Move`"));

    open_document(EXTERN_SYSTEMS "[After(Mo$ve)]\nsystem Later(mut Tally tally)\n{\n    tally.moved = 0;\n}\n" MOVE);
    TIDE_CHECK(has(request("textDocument/hover"), "extern system Move(mut Place place"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"range\":{\"start\":{\"line\":20,\"character\":14}"));
    open_document(EXTERN_SYSTEMS "extern system St$ep(mut Tally tally, Time time);\n");
    TIDE_CHECK(has(request("textDocument/hover"), "It's called once per tick, with no rows."));

    TIDE_CHECK(offers(complete(EXTERN_SYSTEMS "$"), "extern system"));
    const char *after_extern = complete(EXTERN_SYSTEMS "extern $");
    TIDE_CHECK(offers(after_extern, "system") && offers(after_extern, "float"));
    const char *params = complete(EXTERN_SYSTEMS "extern system Move($");
    TIDE_CHECK(offers(params, "mut") && offers(params, "with") && offers(params, "without"));
    TIDE_CHECK(offers(params, "Place") && offers(params, "Wind") && offers(params, "Time"));
    TIDE_CHECK(!offers(params, "World") && !offers(params, "Devices") && !offers(params, "query"));
    TIDE_CHECK(!offers(params, "in") && !offers(params, "out") && !offers(params, "float3"));
    const char *writes = complete(EXTERN_SYSTEMS "extern system Move(mut $");
    TIDE_CHECK(offers(writes, "Place") && offers(writes, "Tally") && !offers(writes, "World"));
    const char *filters = complete(EXTERN_SYSTEMS "extern system Move(mut Place place, with $");
    TIDE_CHECK(offers(filters, "Frozen") && !offers(filters, "Wind"));

    open_document(EXTERN_SYSTEMS "extern system Move(mut Place place, World world);\n");
    TIDE_CHECK(has(last_sent(), "a system written in C can't take the World yet"));
    open_document(EXTERN_SYSTEMS "extern system Move(mut Place place)\n{\n}\nsystem After(mut Tally tally)\n{\n    tally.moved = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "an extern system has no body: its code is in C"));
    TIDE_CHECK(count(last_sent(), "\"severity\":1") == 1);
    open_document(EXTERN_SYSTEMS "extern system Move(Frozen frozen, mut Place place);\n");
    TIDE_CHECK(has(last_sent(), "'Frozen' has no fields, so C has nothing of it to read"));
    const char *fix = request_with("textDocument/codeAction", "\"range\":{\"start\":{\"line\":14,\"character\":20},\"end\":{\"line\":14,"
                                                               "\"character\":26}},\"context\":{\"diagnostics\":[]}");
    TIDE_CHECK(has(fix, "Only require it: 'with Frozen'") && has(fix, "\"newText\":\"with Frozen\""));

    static const char messy[] = "component Place { float2 at; }\nscene Main { }\n[NativeName( \"game_move\" )]\n"
                                "extern   system  Move( mut Place place ,with Main ) ;\n";
    static const char expected[] = "component Place { float2 at; }\nscene Main { }\n[NativeName(\"game_move\")]\n"
                                   "extern system Move(mut Place place, with Main);\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
#undef MOVE
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
    const char *hover = request_at("file:///test.tide", "textDocument/hover", 13, 32, "");
    TIDE_CHECK(has(hover, "Money operator +(Money a, Money b)") && has(hover, "Operator of struct `Money`."));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 13, 32, ""),
                   "\"range\":{\"start\":{\"line\":4,\"character\":10}"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 13, 32, ""),
                   "Operators are named by their symbol"));
    const char *members = complete(OPERATORS "system S(Wallet wallet)\n{\n    var d = wallet.total.$\n}\n");
    TIDE_CHECK(offers(members, "Dollars") && !has(members, "operator"));

    open_document(OPERATORS "system S(Wallet wallet)\n{\n    var d = wallet.total.Dol$lars();\n}\n");
    request_with("textDocument/rename", "\"newName\":\"Whole\"");
    const char *renamed = apply_reply(OPERATORS "system S(Wallet wallet)\n{\n    var d = wallet.total.Dollars();\n}\n",
                                      "file:///test.tide");
    TIDE_CHECK(has(renamed, "int Whole() {") && has(renamed, "wallet.total.Whole();"));

    TIDE_CHECK(offers(complete("$"), "function"));
    const char *in_struct = complete("struct Money\n{\n    int cents;\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(in_struct, "method") && offers(in_struct, "mut method") && offers(in_struct, "operator"));
    TIDE_CHECK(has(in_struct, "Money operator ${1:+}(Money a, Money b)"));
    const char *in_component = complete("component Unit\n{\n    int kills;\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(in_component, "method") && !offers(in_component, "operator"));
    TIDE_CHECK(offers(complete("struct Money\n{\n    int cents;\n    Money $\n}\nscene Main { }\n"), "operator"));

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

// #if and the rest: the formatter leaves directives and what they leave out as they are, and each branch folds.
// ++i and i++ where a value goes: no diagnostic, and the formatter leaves them against their variable.
TIDE_TEST(lsp_delayed_locals_and_switch_expressions)
{
    start();
    static const char messy[] = "scene Main { }\n"
                                "enum Mode { Run, Jump }\n"
                                "int Pick(Mode mode)\n"
                                "{\n"
                                "int current;\n"
                                "current=mode switch\n"
                                "{\n"
                                "Mode.Run=>1,\n"
                                "Mode.Jump=>2,\n"
                                "_=>3,\n"
                                "};\n"
                                "return current;\n"
                                "}\n"
                                "Mode PickMode(Mode mode)\n{\nreturn mode switch\n{\n.Run=>.Jump,\n_=>.Run,\n};\n}\n";
    static const char expected[] = "scene Main { }\n"
                                   "enum Mode { Run, Jump }\n"
                                   "int Pick(Mode mode)\n"
                                   "{\n"
                                   "    int current;\n"
                                   "    current = mode switch\n"
                                   "    {\n"
                                   "        Mode.Run => 1,\n"
                                   "        Mode.Jump => 2,\n"
                                   "        _ => 3,\n"
                                   "    };\n"
                                   "    return current;\n"
                                   "}\n"
                                   "Mode PickMode(Mode mode)\n{\n    return mode switch\n    {\n        .Run => .Jump,\n        _ => .Run,\n    };\n}\n";
    open_document(messy);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 4, 6, ""), "int current"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 4, 6,
                               "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 3);
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 7, 6, ""), "Mode.Run"));
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));
}

TIDE_TEST(lsp_increments_as_values)
{
    start();
    static const char messy[] = "scene Main { }\n"
                                "singleton Count { int n; int picked; }\n"
                                "system S(mut Count c)\n"
                                "{\n"
                                "c.picked=(++c.n%2==0)?c.n++:1;\n"
                                "}\n";
    static const char expected[] = "scene Main { }\n"
                                   "singleton Count { int n; int picked; }\n"
                                   "system S(mut Count c)\n"
                                   "{\n"
                                   "    c.picked = (++c.n % 2 == 0) ? c.n++ : 1;\n"
                                   "}\n";
    open_document(messy);
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
}

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
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
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
    TIDE_CHECK(has(folds, "{\"startLine\":1,\"endLine\":2,\"kind\":\"region\"}"));
    TIDE_CHECK(has(folds, "{\"startLine\":3,\"endLine\":4,\"kind\":\"region\"}"));
    open_document("scene Main { }\n#if false\nthis is no$t code\n#endif\n");
    TIDE_CHECK(has(request("textDocument/completion"), "\"items\":[]"));

    static const char symbols[] = "#define MINE\n"
                                  "#if MINE && DEBUG && !FROM_A_COMMAND_LINE\n"
                                  "scene Main { }\n"
                                  "#else\n"
                                  "not code\n"
                                  "#endif\n";
    open_document(symbols);
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(symbols), "\"result\":[]"));
    const char *branches = request("textDocument/foldingRange");
    TIDE_CHECK(has(branches, "{\"startLine\":1,\"endLine\":2,\"kind\":\"region\"}"));
    TIDE_CHECK(!has(branches, "\"startLine\":0"));
    open_document("scene Main { }\n#define LATE\n");
    TIDE_CHECK(has(sent[0], "comes after code"));
}

TIDE_TEST(lsp_folding)
{
    start();
    open_document("// One\n// Two\n// Three\ncomponent Body\n{\n    float x;\n}\nsystem Main()\n{\n    Spawn(Body);\n}\n");
    const char *folds = request("textDocument/foldingRange");
    TIDE_CHECK(has(folds, "{\"startLine\":0,\"endLine\":2,\"kind\":\"comment\"}"));
    TIDE_CHECK(has(folds, "{\"startLine\":3,\"endLine\":5}"));
    TIDE_CHECK(has(folds, "{\"startLine\":7,\"endLine\":9}"));
    check_repeat_at("file:///test.tide", "textDocument/foldingRange", 0, 0, "");
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 3, 10, ""), "component Body"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 3, 10,
                                "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 2);

    open_document("using A;\nusing B;\n/* One\n   two */\nnamespace C;\nvoid Draw(int x,\n          int y)\n{\n}\n"
                  "void Call()\n{\n    Draw(\n        1,\n        2\n    );\n    List<int> xs = [\n        1,\n        2];\n}\n");
    folds = request("textDocument/foldingRange");
    TIDE_CHECK(has(folds, "{\"startLine\":0,\"endLine\":1,\"kind\":\"imports\"}"));
    TIDE_CHECK(has(folds, "{\"startLine\":2,\"endLine\":3,\"kind\":\"comment\"}"));
    TIDE_CHECK(has(folds, "{\"startLine\":5,\"endLine\":6}"));
    TIDE_CHECK(has(folds, "{\"startLine\":11,\"endLine\":13}"));
    TIDE_CHECK(has(folds, "{\"startLine\":15,\"endLine\":17}"));
}

// Braceless bodies under loops keep their level; an enum with a backing type gets its brace moved.
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
    TIDE_CHECK(has(main, "\"start\":{\"line\":10,\"character\":0}"));
    TIDE_CHECK(has(actions_at(8), "Create component 'Velocity'"));
    check_repeat_at("file:///test.tide", "textDocument/codeAction", 7, 0,
                    "\"range\":{\"start\":{\"line\":7,\"character\":0},\"end\":{\"line\":7,\"character\":0}}");
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 0, 8, ""), "struct Stats"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 0, 8,
                                "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 2);
}

// A parameter that's never used, and isn't a component (which `with` replaces), is removed with its comma.
TIDE_TEST(lsp_quick_fixes_remove_unused_parameters)
{
    start();
    static const char game[] = "component Spin { float speed; }\n"
                               "singleton Score { int points; }\n"
                               "scene Main { }\n"
                               "event(Spawned) Setup(with Main) { Spawn(Spin); }\n"
                               "system Turn(World world, mut Spin spin) { spin.speed += 1; }\n"
                               "system Count(mut Spin spin, Score score) { spin.speed += 2; }\n";
    for (int line = 4; line <= 5; line++) {
        open_document(game);
        const char *fix = actions_at(line);
        TIDE_CHECK(has(fix, line == 4 ? "\"title\":\"Remove 'world'\",\"kind\":\"quickfix\",\"isPreferred\":true,\"diagnostics\":[{"
                                      : "\"title\":\"Remove 'score'\",\"kind\":\"quickfix\",\"isPreferred\":true,\"diagnostics\":[{"));
        const json *reply = json_parse(last_sent(), strlen(last_sent()));
        const json *edits = json_path(json_get(reply, "result")->items[0], "edit", "changes", "file:///test.tide", NULL);
        char text[1024];
        snprintf(text, sizeof text, "%s", game);
        apply_edits(text, sizeof text, edits);
        json_release();
        TIDE_CHECK(has(text, line == 4 ? "system Turn(mut Spin spin) {" : "system Count(mut Spin spin) {"));
        open_document(text);
        TIDE_CHECK(!has(last_sent(), line == 4 ? "'world' is never used" : "'score' is never used"));
        TIDE_CHECK(has(last_sent(), line == 4 ? "'score' is never used" : "'world' is never used"));
    }
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

    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.radius = Math.Clmp(body.radius, 0, 1);\n}\n");
    const char *math = actions_at(24);
    TIDE_CHECK(has(math, "Change to 'Clamp'"));
    TIDE_CHECK(has(math, "{\"range\":{\"start\":{\"line\":24,\"character\":23},\"end\":{\"line\":24,\"character\":27}},"
                         "\"newText\":\"Clamp\"}"));
    TIDE_CHECK(!has(actions_at(23), "Change to"));

    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.position = Math.NormalizeSafe(body.position);\n}\n");
    TIDE_CHECK(has(last_sent(), "Math.NormalizeSafe is gone: write Math.Normalize"));
    TIDE_CHECK(has(last_sent(), "Math.Normalize no longer gives NaN"));
    const char *gone = actions_at(24);
    TIDE_CHECK(has(gone, "\"title\":\"Change to 'Normalize'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    TIDE_CHECK(has(gone, "{\"range\":{\"start\":{\"line\":24,\"character\":25},\"end\":{\"line\":24,\"character\":38}},"
                         "\"newText\":\"Normalize\"}"));
}

// `with ownership` and `without ownership`: `ownership` is a keyword right after them, and a name anywhere else.
TIDE_TEST(lsp_ownership)
{
    start();
    const char *with = complete(GAME_TYPES "view Mine(Body body, with $)\n{\n}\n");
    TIDE_CHECK(offers(with, "ownership"));
    TIDE_CHECK(offers(with, "Body"));
    TIDE_CHECK(has(with, "Entities this machine's player owns"));
    const char *without = complete(GAME_TYPES "view Rest(Body body, without $)\n{\n}\n");
    TIDE_CHECK(offers(without, "ownership"));
    TIDE_CHECK(has(without, "Entities this machine's player doesn't own"));
    TIDE_CHECK(offers(complete(GAME_TYPES "local event(Spawned) Mine(Body body, with $)\n{\n}\n"), "ownership"));
    TIDE_CHECK(offers(complete(GAME_TYPES "local event(verified Destroyed) Rest(Body body, without $)\n{\n}\n"), "ownership"));
    TIDE_CHECK(!offers(complete(GAME_TYPES "local event Blink { }\nlocal event(Blink) Blinked(with $)\n{\n}\n"), "ownership"));
    TIDE_CHECK(!offers(complete(GAME_TYPES "system Move(mut Body body, with $)\n{\n}\n"), "ownership"));
    TIDE_CHECK(!offers(complete(GAME_TYPES "event(Spawned) Grow(mut Body body, without $)\n{\n}\n"), "ownership"));
    TIDE_CHECK(!offers(complete(GAME_TYPES "view Mine($)\n{\n}\n"), "ownership"));

    open_document(GAME_TYPES "view Mine(Body body, with owner$ship)\n{\n    Draw.Circle(body.position, 1, Color.red);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "with ownership"));
    TIDE_CHECK(has(hover, "the entities this machine's player owns"));
    // A keyword (11), 11 columns after the parameter `body`
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), ",0,11,9,11,0,"));
    TIDE_CHECK(has(request("textDocument/prepareRename"), "'ownership' is a keyword after 'with' and 'without'"));
    open_document(GAME_TYPES "local event(Spawned) Mine(Body body, with owner$ship)\n{\n    Debug.Assert(body.radius > 0);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *listener = request("textDocument/hover");
    TIDE_CHECK(has(listener, "local handler") && has(listener, "captured at the event's turn"));
    open_document(GAME_TYPES "view Re$st(Body body, with Owner, without ownership)\n{\n    Draw.Circle(body.position, 1, Color.red);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "view Rest(Body body, with Owner, without ownership)"));

    open_document(GAME_TYPES "system Move(mut Body body, with ownership)\n{\n    body.radius = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "the match runs the same on every machine, so a system can't filter by 'with ownership'"));
    TIDE_CHECK(has(last_sent(), "for entities that have any owner, write 'with Owner'"));
    const char *fix = actions_at(22);
    TIDE_CHECK(has(fix, "\"title\":\"Change to 'Owner'\",\"kind\":\"quickfix\""));
    TIDE_CHECK(has(fix, "{\"range\":{\"start\":{\"line\":22,\"character\":32},\"end\":{\"line\":22,\"character\":41}},"
                        "\"newText\":\"Owner\"}"));

    static const char messy[] = "component Body { float x; }\nscene Main { }\n"
                                "event(Spawned) Setup(with Main) { Spawn(Body, Owner); }\n"
                                "view Mine( Body body,with   ownership ){ Draw.Circle(float2(body.x, 0), 1, Color.red); }\n"
                                "view Rest(Body body ,without ownership) { Draw.Circle(float2(body.x, 0), 1, Color.red); }\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(has(formatted, "view Mine(Body body, with ownership) { "));
    TIDE_CHECK(has(formatted, "view Rest(Body body, without ownership) { "));

    open_document("local singleton ownership { int ownership = 3; }\nscene Main { }\n"
                  "int Twice(int ownership) { return ownership * 2; }\n"
                  "view Share(owner$ship ownership)\n{\n    if (Twice(ownership.ownership) > 1) Draw.Clear(Color.black);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "local singleton ownership"));
    const char *tokens = request("textDocument/semanticTokens/full");
    TIDE_CHECK(has(tokens, ",1,5,5,9,1,0,6,9,3,0,0,10,9,5,3,")); // Share, then a class (3) and a parameter (5)
}

#define VERIFIED_TYPES                                                         \
    GAME_TYPES                                                                 \
    "component Dead { }\n"                                                     \
    "component Box { float3 pos; local int bakes; }\n"                         \
    "local singleton Seen { int count; }\n"

// `verified` before a view's parameter, after `with` and `without`: a keyword there, and a name anywhere else.
TIDE_TEST(lsp_verified)
{
    start();
    const char *first = complete(VERIFIED_TYPES "view Bar($)\n{\n}\n");
    TIDE_CHECK(offers(first, "verified") && offers(first, "mut") && offers(first, "Body"));
    TIDE_CHECK(has(first, "As the server confirmed it, not as this machine predicts it"));
    TIDE_CHECK(offers(complete(VERIFIED_TYPES "view Bar(Body body, $)\n{\n}\n"), "verified"));
    const char *with = complete(VERIFIED_TYPES "view Corpses(Body body, with $)\n{\n}\n");
    TIDE_CHECK(offers(with, "verified") && offers(with, "ownership") && offers(with, "Dead"));
    TIDE_CHECK(has(with, "Entities that have it in the world the server has confirmed"));
    const char *without = complete(VERIFIED_TYPES "view Fresh(Body body, without $)\n{\n}\n");
    TIDE_CHECK(offers(without, "verified"));
    TIDE_CHECK(has(without, "Entities that don't have it in the world the server has confirmed"));
    const char *type = complete(VERIFIED_TYPES "view Bar(Body body, verified $)\n{\n}\n");
    TIDE_CHECK(offers(type, "Body") && offers(type, "Dead") && offers(type, "Arena"));
    TIDE_CHECK(!offers(type, "Seen") && !offers(type, "PlayerInput") && !offers(type, "verified"));
    const char *filter = complete(VERIFIED_TYPES "view Corpses(Body body, with verified $)\n{\n}\n");
    TIDE_CHECK(offers(filter, "Dead") && offers(filter, "Body") && !offers(filter, "Arena") && !offers(filter, "Seen"));
    TIDE_CHECK(!offers(complete(VERIFIED_TYPES "system Move(mut Body body, $)\n{\n}\n"), "verified"));
    TIDE_CHECK(!offers(complete(VERIFIED_TYPES "system Move(mut Body body, with $)\n{\n}\n"), "verified"));
    TIDE_CHECK(!offers(complete(VERIFIED_TYPES "event(Spawned) Grow(mut Body body, $)\n{\n}\n"), "verified"));
    TIDE_CHECK(!offers(complete(VERIFIED_TYPES "local event(Spawned) Grow(Body body, $)\n{\n}\n"), "verified"));
    TIDE_CHECK(!offers(complete(VERIFIED_TYPES "frag Shade(Vertex vertex, out Color color, $)\n{\n}\n"), "verified"));
    TIDE_CHECK(!offers(complete(VERIFIED_TYPES "void Heal(Body body, $)\n{\n}\n"), "verified"));
    TIDE_CHECK(!offers(complete(VERIFIED_TYPES "view Bar(mut $)\n{\n}\n"), "verified"));
    TIDE_CHECK(offers(complete(VERIFIED_TYPES "view Bar(mut verified $)\n{\n}\n"), "Box"));

    open_document(VERIFIED_TYPES "view Bar(Body mine, veri$fied Body sure, mut Seen seen)\n{\n"
                                 "    if (mine.radius < sure.radius) seen.count += 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "verified Body sure"));
    TIDE_CHECK(has(hover, "The entity's `Body` as the last tick the server confirmed has it"));
    TIDE_CHECK(has(hover, "The view runs for the entities it draws that are the same entity in the confirmed world too."));
    // A keyword (11), 6 columns after the parameter `mine`, then the struct (2) it's of
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), ",0,5,4,5,3,0,6,8,11,0,0,9,4,2,0,"));
    TIDE_CHECK(has(request("textDocument/prepareRename"), "'verified' is a keyword before a parameter's type"));
    open_document(VERIFIED_TYPES "view Ba$r(Body mine, verified Body sure, mut Seen seen)\n{\n"
                                 "    if (mine.radius < sure.radius) seen.count += 1;\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "view Bar(Body mine, verified Body sure, mut Seen seen)"));
    open_document(VERIFIED_TYPES "view Bar(Body mine, verified Body su$re, mut Seen seen)\n{\n"
                                 "    if (mine.radius < sure.radius) seen.count += 1;\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "verified Body sure"));
    open_document(VERIFIED_TYPES "view Sure(veri$fied Body body, mut Seen seen)\n{\n    if (body.radius > 0) seen.count += 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "All the view's components are `verified`, so it runs for the confirmed "
                                                  "world's entities."));
    open_document(VERIFIED_TYPES "view Board(Arena mine, veri$fied Arena sure, mut Seen seen)\n{\n"
                                 "    if (mine.halfSize.x != sure.halfSize.x) seen.count += 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "`Arena` as the last tick the server confirmed has it"));
    open_document(VERIFIED_TYPES "view Corpses(Body body, mut Seen seen, with veri$fied Dead)\n{\n"
                                 "    if (body.radius > 0) seen.count += 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *has_it = request("textDocument/hover");
    TIDE_CHECK(has(has_it, "with verified Dead"));
    TIDE_CHECK(has(has_it, "Filter: the view runs for the entities that have `Dead` in the world the server has confirmed"));
    open_document(VERIFIED_TYPES "view Fresh(Body body, mut Seen seen, without veri$fied Dead)\n{\n"
                                 "    if (body.radius > 0) seen.count += 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *lacks_it = request("textDocument/hover");
    TIDE_CHECK(has(lacks_it, "without verified Dead"));
    TIDE_CHECK(has(lacks_it, "are in the world the server has confirmed and don't have `Dead` there"));
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), ",0,14,8,11,0,0,9,4,2,0,")); // After `seen, without`, then Dead

    open_document(VERIFIED_TYPES "system Move(veri$fied Body body, mut Arena arena)\n{\n    arena.halfSize.x = body.radius;\n}\n");
    TIDE_CHECK(has(last_sent(), "the match is the same on every machine, so a system has nothing 'verified' to read"));
    TIDE_CHECK(has(request("textDocument/hover"), "It goes before a component or singleton of the match in a view's parameters"));
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), ",0,5,8,11,0,0,9,4,2,0,")); // After `Move`

    static const char unused[] = VERIFIED_TYPES "view Ghosts(Body body, verified Dead dead, mut Seen seen)\n{\n"
                                                "    if (body.radius > 0) seen.count += 1;\n}\n";
    open_document(unused);
    TIDE_CHECK(has(last_sent(), "'dead' is never used"));
    TIDE_CHECK(has(actions_at(25), "Only require it: 'with verified Dead'"));
    TIDE_CHECK(has(apply_first_action(unused), "view Ghosts(Body body, with verified Dead, mut Seen seen)\n"));
    static const char never_written[] = VERIFIED_TYPES "view Show(mut verified Box box, mut Seen seen)\n{\n"
                                                       "    if (box.bakes > 0) seen.count += 1;\n}\n";
    open_document(never_written);
    TIDE_CHECK(has(last_sent(), "'box' is declared mut but never written"));
    TIDE_CHECK(has(actions_at(25), "Remove 'mut' from 'box'"));
    TIDE_CHECK(has(apply_first_action(never_written), "view Show(verified Box box, mut Seen seen)\n"));
    static const char read_only[] = VERIFIED_TYPES "view Show(verified Box box)\n{\n    box.bakes += 1;\n}\n";
    open_document(read_only);
    TIDE_CHECK(has(last_sent(), "'box' is read-only"));
    TIDE_CHECK(has(actions_at(27), "Declare 'box' as mut"));
    TIDE_CHECK(has(apply_first_action(read_only), "view Show(mut verified Box box)\n"));

    static const char messy[] = VERIFIED_TYPES
        "view Bar( Body mine,verified   Body sure ,mut Seen seen,without  verified Dead ){ if (mine.radius < sure.radius) seen.count += 1; }\n";
    format_reply(messy);
    TIDE_CHECK(has(apply_reply(messy, NULL), "view Bar(Body mine, verified Body sure, mut Seen seen, without verified Dead) { "));

    open_document("local singleton verified { int verified = 3; }\nscene Main { }\n"
                  "int Twice(int verified) { return verified * 2; }\n"
                  "view Share(veri$fied trust)\n{\n    if (Twice(trust.verified) > 1) Draw.Clear(Color.black);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "local singleton verified"));
    const char *tokens = request("textDocument/semanticTokens/full");
    TIDE_CHECK(has(tokens, ",1,5,5,9,1,0,6,8,3,0,0,9,5,5,3,")); // Share, then a class (3) and a parameter (5)
}

#define HEARD                                                                                                   \
    GAME_TYPES                                                                                                  \
    "event Hit { int damage; }\n"                                                                               \
    "event Rang { int at; }\n"                                                                                  \
    "local singleton Fx { int flashes; }\n"                                                                     \
    "system Strike(with Body) { this.Send(Hit { damage = 2 }); Send(Rang { at = 1 }); }\n"                      \
    "event(Hit hit) TakeHit(mut Body body) { body.radius -= hit.damage; }\n"                                    \
    "local event(Hit hit) ShowHit(Body body, mut Fx fx) { if (body.radius > hit.damage) fx.flashes += 1; }\n"   \
    "local event(verified Hit hit) SureHit(mut Fx fx) { fx.flashes += hit.damage; }\n"                          \
    "local event(Rang rang) HearRang(mut Fx fx) { fx.flashes += rang.at; }\n"                                   \
    "local event(Spawned) Born(Body body, mut Fx fx) { if (body.radius > 0) fx.flashes += 1; }\n"

// Local handlers of the match's events, `local event(Hit hit)` and `local event(verified Hit hit)`: hovers, lenses, implementations.
TIDE_TEST(lsp_local_handlers_of_match_events)
{
    start();
    open_document(HEARD);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *shows = request_at("file:///test.tide", "textDocument/hover", 27, 24, "");
    TIDE_CHECK(has(shows, "local event(Hit hit) ShowHit(Body body, mut Fx fx)"));
    TIDE_CHECK(has(shows, "A local handler of the match's `Hit`: it runs on this machine at the start of a frame, before the views"));
    TIDE_CHECK(has(shows, "for each `Hit` the ticks since the last frame sent to an entity that matches its parameters"));
    TIDE_CHECK(has(shows, "It hears each once, whatever ticks are run again"));
    TIDE_CHECK(has(shows, "Runs 1st of 2 local handlers of `Hit`."));
    const char *sure = request_at("file:///test.tide", "textDocument/hover", 28, 33, "");
    TIDE_CHECK(has(sure, "local event(verified Hit hit) SureHit(mut Fx fx)"));
    TIDE_CHECK(has(sure, "for each `Hit` sent in the ticks the server confirmed since the last frame"));
    TIDE_CHECK(has(sure, "It hears each once, when the server confirmed its tick"));
    TIDE_CHECK(has(sure, "Runs 2nd of 2 local handlers of `Hit`."));
    const char *born = request_at("file:///test.tide", "textDocument/hover", 30, 23, "");
    TIDE_CHECK(has(born, "A local handler of the match's `Spawned`"));
    const char *takes = request_at("file:///test.tide", "textDocument/hover", 26, 17, "");
    TIDE_CHECK(has(takes, "Runs when a `Hit` is sent to an entity that matches its parameters, at the end of the tick."));
    TIDE_CHECK(!has(takes, "handlers of"));

    // `verified` before the event: a keyword (11), then the event (a struct, 2), its name and the handler's
    const char *word = request_at("file:///test.tide", "textDocument/hover", 28, 14, "");
    TIDE_CHECK(has(word, "event(verified Hit hit)"));
    TIDE_CHECK(has(word, "The handler hears each `Hit` once, when the server confirmed the tick that sent it"));
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), ",1,12,8,11,0,0,9,3,2,0,0,4,3,5,3,0,5,7,9,1,"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 28, 14, ""), "'verified' is a keyword"));

    const char *hit = request_at("file:///test.tide", "textDocument/hover", 22, 7, "");
    TIDE_CHECK(has(hit, "Sent with `Send`, and handled by `event(...)` handlers at the end of the tick."));
    TIDE_CHECK(has(hit, "Local handlers hear it on each machine, at the start of a frame: `ShowHit`, `SureHit` (once the "
                        "server confirmed it)."));
    const char *rang = request_at("file:///test.tide", "textDocument/hover", 23, 7, "");
    TIDE_CHECK(has(rang, "only for local code: no handler in the match takes it, so it costs the match nothing."));
    TIDE_CHECK(has(rang, "at the start of a frame: `HearRang`."));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 30, 14, ""), "at the start of a frame: `Born`."));

    const char *lenses = request("textDocument/codeLens");
    TIDE_CHECK(has(lenses, "stage 1"));
    TIDE_CHECK(has(lenses, "{\"range\":{\"start\":{\"line\":27,\"character\":21},\"end\":{\"line\":27,\"character\":28}},"
                           "\"command\":{\"title\":\"hears Hit at the start of a frame\""));
    TIDE_CHECK(has(lenses, "\"title\":\"hears Hit once the server confirmed it\""));
    TIDE_CHECK(has(lenses, "\"title\":\"hears Rang at the start of a frame\""));
    TIDE_CHECK(has(lenses, "\"title\":\"hears Spawned at the start of a frame\""));
    TIDE_CHECK(count(lenses, "\"title\":\"hears ") == 4);
    check_repeat_lenses("file:///test.tide");
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 27, 24, ""), "Runs 1st of 2 local handlers"));

    const char *handlers = request_at("file:///test.tide", "textDocument/implementation", 22, 7, "");
    TIDE_CHECK(has(handlers, "{\"line\":26,\"character\":15}"));
    TIDE_CHECK(has(handlers, "{\"line\":27,\"character\":21}"));
    TIDE_CHECK(has(handlers, "{\"line\":28,\"character\":30}"));
    TIDE_CHECK(count(handlers, "\"uri\"") == 3);
    const char *uses = request_at("file:///test.tide", "textDocument/references", 22, 7, "\"context\":{\"includeDeclaration\":true}");
    TIDE_CHECK(count(uses, "\"uri\"") == 5);
    TIDE_CHECK(has(uses, "{\"line\":28,\"character\":21}"));
    TIDE_CHECK(has(request("textDocument/documentSymbol"), "\"name\":\"ShowHit\",\"detail\":\"local event handler\""));

    open_document(HEARD "system Break(mut Body body)\n{\n    body.pos = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(count(request("textDocument/codeLens"), "\"title\":\"hears ") == 4);
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 23, 7, ""), "only for local code"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/implementation", 22, 7, ""), "\"uri\"") == 3);

    open_document(GAME_TYPES "event Hit { int damage; }\nsystem Strike(with Body) { this.Send(Hit { damage = 2 }); }\n"
                             "event(veri$fied Hit hit) TakeHit(mut Body body) { body.radius -= hit.damage; }\n");
    TIDE_CHECK(has(last_sent(), "so a match handler has no 'verified' events"));
    TIDE_CHECK(has(request("textDocument/hover"), "and before a local handler's event."));

    const char *trigger = complete(GAME_TYPES "event Hit { }\nlocal event Note { }\nlocal event($)\n");
    TIDE_CHECK(offers(trigger, "verified") && offers(trigger, "Hit") && offers(trigger, "Note") && offers(trigger, "Spawned"));
    TIDE_CHECK(offers(trigger, "Shown") && offers(trigger, "Hidden"));
    TIDE_CHECK(has(trigger, "Once the server confirmed the tick that sent the event"));
    const char *confirmed = complete(GAME_TYPES "event Hit { }\nlocal event Note { }\nlocal event(verified $)\n");
    TIDE_CHECK(offers(confirmed, "Hit") && offers(confirmed, "Spawned") && offers(confirmed, "PlayerJoined"));
    TIDE_CHECK(offers(confirmed, "Shown") && offers(confirmed, "Hidden"));
    TIDE_CHECK(!offers(confirmed, "Note") && !offers(confirmed, "Forgotten") && !offers(confirmed, "verified"));
    TIDE_CHECK(!offers(complete(GAME_TYPES "event Hit { }\nevent($)\n"), "verified"));
    TIDE_CHECK(!offers(complete(GAME_TYPES "event($)\n"), "Shown"));
    TIDE_CHECK(!offers(complete(GAME_TYPES "event($)\n"), "Hidden"));
    TIDE_CHECK(!offers(complete(GAME_TYPES "command Bump { }\ncommand($)\n"), "verified"));

    open_document(GAME_TYPES "local singleton Seen { int n; }\n"
                  "local event(Sho$wn) Available(Body body, mut Seen seen) { seen.n += int(body.radius); }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "local availability notification"));
    open_document(GAME_TYPES "local singleton Seen { int n; }\n"
                  "local event(Hid$den) Concealed(Body body, mut Seen seen) { seen.n += int(body.radius); }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "last permitted state"));

    static const char messy[] = GAME_TYPES "event Hit { int damage; }\nlocal singleton Fx { int flashes; }\n"
                                           "system Strike(with Body) { this.Send(Hit { damage = 2 }); }\n"
                                           "local event( verified   Hit hit )SureHit(mut Fx fx){ fx.flashes += hit.damage; }\n";
    format_reply(messy);
    TIDE_CHECK(has(apply_reply(messy, NULL), "local event(verified Hit hit) SureHit(mut Fx fx) { fx.flashes += hit.damage; }\n"));
}

#define VISIBILITY_GAME \
    "component Card { int value; }\n" \
    "singleton Policies { VisibilityGroup group; }\n" \
    "scene Main { }\n" \
    "event(Spawned) Setup(with Main, mut Policies policies)\n" \
    "{\n" \
    "    policies.group = Visibility.CreateGroup(Audience.None);\n" \
    "    Spawn(Card, Visibility { group = policies.group }, Owner);\n" \
    "}\n"

TIDE_TEST(lsp_visibility_policies)
{
    start();
    const char *audience = complete(VISIBILITY_GAME "system Work() { Audience.$ }\n");
    TIDE_CHECK(offers(audience, "All") && offers(audience, "None"));
    TIDE_CHECK(offers(audience, "Only") && offers(audience, "From"));
    TIDE_CHECK(has(audience, "PlayerID player") && has(audience, "List<PlayerID> players"));
    const char *groups = complete(VISIBILITY_GAME "system Work() { Visibility.$ }\n");
    TIDE_CHECK(offers(groups, "CreateGroup") && offers(groups, "Set") && offers(groups, "DestroyGroup"));
    TIDE_CHECK(!offers(complete(VISIBILITY_GAME "view Work() { Visibility.$ }\n"), "CreateGroup"));
    const char *members = complete(VISIBILITY_GAME "system Work(mut Visibility visibility) { visibility.$ }\n");
    TIDE_CHECK(offers(members, "group") && offers(members, "Set") && offers(members, "Reset"));
    TIDE_CHECK(has(members, "Set<T>(Audience audience)") && has(members, "Set<T>(VisibilityGroup group)"));
    TIDE_CHECK(!offers(members, "overrides"));
    members = complete(VISIBILITY_GAME "system Work() { mut var audience = Audience.None; audience.$ }\n");
    TIDE_CHECK(offers(members, "Add") && offers(members, "Remove") && offers(members, "Contains"));
    TIDE_CHECK(!offers(members, "words") && !offers(members, "all"));
    const char *types = complete(VISIBILITY_GAME "system Work(mut Visibility visibility) { visibility.Set<$ }\n");
    TIDE_CHECK(offers(types, "Card") && !offers(types, "Main") && !offers(types, "Visibility"));

    open_document(VISIBILITY_GAME "system Work(Policies policies) { Visibility.Se$t(policies.group, Audience.All); }\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(has(request("textDocument/hover"), "Visibility.Set(VisibilityGroup group, Audience audience)"));
    open_document(VISIBILITY_GAME "system Work() { var audience = Audience.A$ll; Debug.Assert(audience.Contains(default)); }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Every player"));
    open_document(VISIBILITY_GAME "system Work(mut Visibility visibility) { visibility.Se$t<Card>(Audience.None); }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "whole component"));
    open_document(VISIBILITY_GAME "system Work() { Visibility.Set($); }\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "VisibilityGroup group, Audience audience"));
    open_document(VISIBILITY_GAME "system Work(mut Visibility visibility) { visibility.Set<Card>($); }\n");
    const char *signatures = request("textDocument/signatureHelp");
    TIDE_CHECK(has(signatures, "Set<T>(Audience audience)") && has(signatures, "Set<T>(VisibilityGroup group)"));
}

#undef VISIBILITY_GAME

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
    const char *system = actions_at(11);
    TIDE_CHECK(has(system, "add '!'") && !has(system, "add 'try'"));
}

// The document after the quick fix titled `title`, or NULL when it isn't offered.
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

// A frag's pixel is implicit `this`: the old parameter migrates, and a game's own Pixel type and entity `this` stay independent.
TIDE_TEST(lsp_shader_this)
{
    start();
    static const char last[] = "scene Main { }\n"
                              "frag Mask(Vertex3 vertex, out Color color, Pixel fragment)\n{\n"
                              "    if (fragment.depth > 0.5) fragment.Discard();\n"
                              "    color = Color(fragment.world.x, fragment.position.x, 0);\n}\n";
    open_document(last);
    TIDE_CHECK(has(last_sent(), "a frag's pixel is 'this' now: remove the 'Pixel' parameter and use 'this'"));
    TIDE_CHECK(has(actions_at(1), "Use 'this' instead of 'fragment'"));
    const char *migrated = apply_fix(last, "Use 'this' instead of 'fragment'");
    TIDE_REQUIRE(migrated != NULL);
    TIDE_CHECK(has(migrated, "frag Mask(Vertex3 vertex, out Color color)\n"));
    TIDE_CHECK(has(migrated, "if (this.depth > 0.5) this.Discard();"));
    TIDE_CHECK(has(migrated, "Color(this.world.x, this.position.x, 0)"));
    open_document(migrated);
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 3, 9, ""), "'this' is a keyword"));

    static const char middle[] = "scene Main { }\n"
                                "frag Mask(Vertex3 vertex, Pixel pixel, out Color color)\n{\n"
                                "    color = Color.white * pixel.depth;\n}\n";
    open_document(middle);
    actions_at(1);
    migrated = apply_fix(middle, "Use 'this' instead of 'pixel'");
    TIDE_REQUIRE(migrated != NULL);
    TIDE_CHECK(has(migrated, "frag Mask(Vertex3 vertex, out Color color)\n"));
    TIDE_CHECK(has(migrated, "Color.white * this.depth"));
    open_document(migrated);
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));

    open_document("scene Main { }\ncomponent Pixel { float strength = 1; }\n"
                  "frag Mask(Vertex3 vertex, out Color color, Pixel pixel)\n{\n"
                  "    color = Color.white * pixel.strength * this.depth;\n}\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(!has(actions_at(2), "Use 'this' instead of 'pixel'"));
    const char *fields = complete("scene Main { }\ncomponent Pixel { float strength = 1; }\n"
                                  "frag Mask(Vertex3 vertex, out Color color, Pixel pixel)\n{\n    this.$\n}\n");
    TIDE_CHECK(offers(fields, "Discard") && offers(fields, "depth") && !offers(fields, "strength"));

    open_document("scene Main { }\nfrag Mask(Vertex3 vertex, out Color color)\n{\n"
                  "    this.depth = 0.5;\n    color = Color.white;\n}\n");
    TIDE_CHECK(has(last_sent(), "a frag's pixel can only be read"));
    open_document("scene Main { }\nvert Place(mut Vertex3 vertex)\n{\n    this.Discard();\n}\n");
    TIDE_CHECK(has(last_sent(), "a vert's 'this' has no methods: 'this.clip' is where the engine would put its vertex"));
    open_document("scene Main { }\nvoid Helper()\n{\n    this.Discard();\n}\n");
    TIDE_CHECK(has(last_sent(), "functions don't run for an entity, so they have no 'this'"));
}

// What a frag and a vert write, `out`: the old spelling as a change, unwritten and read-first colors, hovers, and unwritten structs.
TIDE_TEST(lsp_shader_outputs)
{
    start();
#define OUTPUT_TYPES                                                                                                  \
    "scene Main { }\n"                                                                                                \
    "struct Corner { float3 position; float2 uv; }\n"                                                                 \
    "struct FromLamp { float3 place; float glow; }\n"                                                                 \
    "struct Crate { Color paint; }\n"                                                                                 \
    "singleton Sun { float3 at; }\n"                                                                                  \
    "local singleton Models { List<Corner> crate; List<int> faces; List<Crate> crates; }\n"
    static const char old[] = OUTPUT_TYPES "frag Shade(Corner corner, mut Color color)\n{\n    color = Color.red;\n}\n";
    open_document(old);
    TIDE_CHECK(has(last_sent(), "a frag's color is an 'out' parameter: 'out Color color'"));
    TIDE_CHECK(has(last_sent(), "where 'mut Color color' started white"));
    actions_at(6);
    const char *respelled = apply_fix(old, "Change to 'out'");
    TIDE_REQUIRE(respelled != NULL);
    TIDE_CHECK(has(respelled, "frag Shade(Corner corner, out Color color)\n"));
    open_document(respelled);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    static const char changed[] = OUTPUT_TYPES "vert Place(mut Corner corner, mut FromLamp lamp)\n{\n"
                                               "    lamp.place = corner.position;\n    lamp.glow = 1;\n}\n";
    open_document(changed);
    TIDE_CHECK(has(last_sent(), "what it writes for its frag is 'out': 'out FromLamp lamp'"));
    actions_at(6);
    const char *handed = apply_fix(changed, "Change to 'out'");
    TIDE_REQUIRE(handed != NULL);
    TIDE_CHECK(has(handed, "vert Place(mut Corner corner, out FromLamp lamp)\n"));
    open_document(handed);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    static const char vertex[] = OUTPUT_TYPES "vert Place(out Corner corner)\n{\n    corner.position = float3(0);\n}\n";
    open_document(vertex);
    TIDE_CHECK(has(last_sent(), "a vert is handed its vertex and changes it, so it takes it 'mut', not 'out'"));
    actions_at(6);
    const char *taken = apply_fix(vertex, "Change to 'mut'");
    TIDE_REQUIRE(taken != NULL);
    TIDE_CHECK(has(taken, "vert Place(mut Corner corner)\n"));

    static const char unwritten[] = OUTPUT_TYPES "frag Shade(Corner corner, out Color color)\n{\n"
                                                 "    if (corner.uv.x > 0.5) color = Color.red;\n}\n";
    open_document(unwritten);
    TIDE_CHECK(has(last_sent(), "frag 'Shade' can end without writing its pixel's color, 'color'"));
    TIDE_CHECK(has(last_sent(), "leaves the pixel with 'this.Discard();'"));
    const char *ends = actions_at(6);
    TIDE_CHECK(has(ends, "\"title\":\"Write 'color = Color.white;'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    const char *whitened = apply_fix(unwritten, "Write 'color = Color.white;'");
    TIDE_REQUIRE(whitened != NULL);
    TIDE_CHECK(has(whitened, "    if (corner.uv.x > 0.5) color = Color.red;\n    color = Color.white;\n}\n"));
    static const char early[] = OUTPUT_TYPES "frag Shade(Corner corner, out Color color)\n{\n    color.a = corner.uv.x;\n}\n";
    open_document(early);
    TIDE_CHECK(has(last_sent(), "'color' is read before it's written"));
    const char *reads = actions_at(8);
    TIDE_CHECK(has(reads, "Write 'color = Color.white;' first") && !has(reads, "as 'mut'"));
    const char *started = apply_fix(early, "Write 'color = Color.white;' first");
    TIDE_REQUIRE(started != NULL);
    TIDE_CHECK(has(started, "{\n    color = Color.white;\n    color.a = corner.uv.x;\n}\n"));
    open_document(started);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    static const char partly[] = OUTPUT_TYPES "vert Place(mut Corner corner, out FromLamp lamp, Sun sun)\n{\n"
                                              "    lamp.place = corner.position - sun.at;\n"
                                              "    if (corner.uv.x > 0) lamp.glow = 1;\n}\n";
    open_document(partly);
    TIDE_CHECK(has(last_sent(), "vert 'Place' can end without writing 'lamp.glow'"));
    actions_at(6);
    const char *filled = apply_fix(partly, "Write 'lamp = default;'");
    TIDE_REQUIRE(filled != NULL);
    TIDE_CHECK(has(filled, "    if (corner.uv.x > 0) lamp.glow = 1;\n    lamp = default;\n}\n"));

#define OUTPUTS                                                                                                       \
    OUTPUT_TYPES                                                                                                      \
    "vert Place(mut Corner corner, out FromLamp lamp, Sun sun)\n{\n"                                                  \
    "    lamp.place = corner.position - sun.at;\n    lamp.glow = 1;\n}\n"                                             \
    "frag Look(Corner corner, FromLamp lamp, Crate crate, out Color color)\n{\n"                                      \
    "    color = crate.paint * lamp.glow;\n}\n"
    static const char whole[] = OUTPUTS "view Show(Models models)\n{\n"
                                        "    Draw.Instanced(models.crate, models.faces, models.crates, Place, Look);\n}\n";
    open_document(whole);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *vert = hover_at(6, 6);
    TIDE_CHECK(has(vert, "vert Place(mut Corner corner, out FromLamp lamp, Sun sun)"));
    TIDE_CHECK(has(vert, "What it writes to a struct it takes as `out`, the frag it's drawn with reads, blended across the triangle"));
    TIDE_CHECK(has(vert, "- for each draw: `Sun.at`") && !has(vert, "FromLamp."));
    const char *writes = hover_at(6, 45);
    TIDE_CHECK(has(writes, "out FromLamp lamp") && has(writes, "What the vert writes for the frag it's drawn with"));
    TIDE_CHECK(has(hover_at(6, 32), "What the vert writes for the frag it's drawn with"));
    const char *frag = hover_at(11, 6);
    TIDE_CHECK(has(frag, "Draw.Instanced(..., Look)") && has(frag, "What it writes to its `out Color` is the pixel"));
    TIDE_CHECK(has(frag, "- for each instance: `Crate.paint`"));
    TIDE_CHECK(has(frag, "- of what its vert wrote for it, blended across the triangle: `FromLamp.glow`") && !has(frag, "FromLamp.place"));
    const char *lamp = hover_at(11, 35);
    TIDE_CHECK(has(lamp, "What the vert this frag is drawn with wrote for it") && has(lamp, "which the corners pass on: `glow`."));
    TIDE_CHECK(has(hover_at(11, 47), "An element of the draw's instance list"));
    const char *tokens = request("textDocument/semanticTokens/full");
    TIDE_CHECK(has(tokens, ",0,8,3,11,0,0,4,8,2,0,0,9,4,5,1,")); // , out FromLamp lamp: what it writes
    static const char lone[] = OUTPUTS "view Show(Models models)\n{\n"
                                       "    Draw.Instanced(models.crate, models.faces, models.crates, Look);\n}\n";
    open_document(lone);
    TIDE_CHECK(has(last_sent(), "frag 'Look' reads a 'FromLamp', and this draw names no vert that writes one"));
    TIDE_CHECK(has(last_sent(), "'out FromLamp lamp' among the vert's parameters"));
    static const char other[] = OUTPUTS "vert Lift(mut Corner corner)\n{\n    corner.position.y += 1;\n}\n"
                                        "view Show(Models models)\n{\n"
                                        "    Draw.Instanced(models.crate, models.faces, models.crates, Lift, Look);\n}\n";
    open_document(other);
    TIDE_CHECK(has(last_sent(), "frag 'Look' reads a 'FromLamp', which vert 'Lift' doesn't write"));
    const char *named = complete(OUTPUTS "view Show(Models models)\n{\n"
                                         "    Draw.Instanced(models.crate, models.faces, models.crates, $);\n}\n");
    TIDE_CHECK(offers(named, "Place") && offers(named, "Look"));
#undef OUTPUTS
#undef OUTPUT_TYPES
}

// A vert that puts its vertex itself, `out float4` and `this.clip`: the old spelling, unwritten and read-first paths, hovers, completion.
TIDE_TEST(lsp_vert_place)
{
    start();
#define PLACE_TYPES                                                                                                   \
    "scene Main { }\n"                                                                                                \
    "struct Corner { float3 position; float2 uv; }\n"                                                                 \
    "local singleton Lens { float2 size = float2(480, 270); }\n"                                                      \
    "local singleton Models { List<Corner> crate; List<int> faces; }\n"
    static const char old[] = PLACE_TYPES "vert Snap(mut Corner corner, mut float4 clip)\n{\n    clip = this.clip;\n}\n";
    open_document(old);
    TIDE_CHECK(has(last_sent(), "where a vert puts its vertex is an 'out' parameter: 'out float4 clip'"));
    actions_at(4);
    const char *respelled = apply_fix(old, "Change to 'out'");
    TIDE_REQUIRE(respelled != NULL);
    TIDE_CHECK(has(respelled, "vert Snap(mut Corner corner, out float4 clip)\n"));
    open_document(respelled);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    static const char unwritten[] = PLACE_TYPES "vert Snap(mut Corner corner, out float4 clip)\n{\n"
                                                "    if (corner.uv.x > 0.5) clip = float4(0, 0, 0, 1);\n}\n";
    open_document(unwritten);
    TIDE_CHECK(has(last_sent(), "vert 'Snap' can end without writing where its vertex goes, 'clip'"));
    TIDE_CHECK(has(actions_at(4), "\"title\":\"Write 'clip = this.clip;'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    const char *left = apply_fix(unwritten, "Write 'clip = this.clip;'");
    TIDE_REQUIRE(left != NULL);
    TIDE_CHECK(has(left, "    if (corner.uv.x > 0.5) clip = float4(0, 0, 0, 1);\n    clip = this.clip;\n}\n"));
    open_document(left);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    static const char early[] = PLACE_TYPES "vert Snap(mut Corner corner, out float4 clip)\n{\n    clip.z = clip.w;\n}\n";
    open_document(early);
    TIDE_CHECK(has(last_sent(), "'clip' is read before it's written"));
    const char *reads = actions_at(6);
    TIDE_CHECK(has(reads, "Write 'clip = this.clip;' first") && !has(reads, "as 'mut'"));
    const char *started = apply_fix(early, "Write 'clip = this.clip;' first");
    TIDE_REQUIRE(started != NULL);
    TIDE_CHECK(has(started, "{\n    clip = this.clip;\n    clip.z = clip.w;\n}\n"));
    open_document(started);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    static const char whole[] = PLACE_TYPES "vert Snap(mut Corner corner, out float4 clip, Lens lens)\n{\n"
                                            "    float2 half = lens.size * 0.5;\n"
                                            "    clip = this.clip;\n"
                                            "    clip.xy = Math.Round(clip.xy / clip.w * half) / half * clip.w;\n}\n"
                                            "view Show(Models models)\n{\n"
                                            "    Draw.Mesh(models.crate, models.faces, float4x4.identity, Snap);\n}\n";
    open_document(whole);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(hover_at(4, 6), "It puts its vertex itself, where it writes `clip`"));
    const char *place = hover_at(4, 40);
    TIDE_CHECK(has(place, "out float4 clip") && has(place, "Where the vert puts its vertex, in clip space"));
    TIDE_CHECK(has(place, "`clip = this.clip;` leaves it there"));
    const char *self = hover_at(7, 12);
    TIDE_CHECK(has(self, "The vertex `Snap` runs for") && has(self, "Its one read-only field is `clip`"));
    const char *field = hover_at(7, 17);
    TIDE_CHECK(has(field, "Where the engine would put the vertex, in clip space"));
    const char *fields = complete(PLACE_TYPES "vert Snap(mut Corner corner, out float4 clip)\n{\n    clip = this.$\n}\n");
    TIDE_CHECK(offers(fields, "clip") && !offers(fields, "Discard") && !offers(fields, "depth") && !offers(fields, "world"));
    const char *names = complete(PLACE_TYPES "vert Snap(mut Corner corner, out float4 clip)\n{\n    clip = $\n}\n");
    TIDE_CHECK(offers(names, "this"));
    const char *written = complete(PLACE_TYPES "vert Snap(mut Corner corner, out $)\n{\n}\n");
    TIDE_CHECK(offers(written, "float4") && offers(written, "Corner"));
    const char *again = complete(PLACE_TYPES "vert Snap(mut Corner corner, out float4 clip, out $)\n{\n}\n");
    TIDE_CHECK(!offers(again, "float4") && offers(again, "Corner"));
    const char *colors = complete(PLACE_TYPES "frag Shade(Corner corner, out $)\n{\n}\n");
    TIDE_CHECK(offers(colors, "Color") && !offers(colors, "float4"));

    open_document(PLACE_TYPES "vert Snap(mut Corner corner)\n{\n    corner.position.x = this.world.x;\n}\n");
    TIDE_CHECK(has(last_sent(), "a vert's 'this' has no 'world': it has 'clip', where the engine would put its vertex"));
    open_document(PLACE_TYPES "vert Snap(mut Corner corner)\n{\n    this.clip.z = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "where the engine would put a vert's vertex can only be read"));
#undef PLACE_TYPES
}

// A local nothing reads: a faded warning, with a fix that removes it or keeps its call.
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
    TIDE_CHECK(count(last_sent(), "\"severity\":2") == 1);
    const char *remove = actions_at(25);
    TIDE_CHECK(has(remove, "\"title\":\"Remove 'fwd'\",\"kind\":\"quickfix\",\"isPreferred\":true,\"diagnostics\":[{"));
    const char *removed = apply_fix(unused, "Remove 'fwd'");
    TIDE_REQUIRE(removed != NULL);
    TIDE_CHECK(has(removed, "    var move = Math.Normalize(body.position);\n    body.position += move * time.dt;\n}\n"));
    TIDE_CHECK(!has(actions_at(24), "Remove '"));

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

    static const char typed[] = GAME_TYPES "int Bump(mut float radius) { radius += 1; return 1; }\n"
                                           "system Grow(mut Body body)\n{\n    mut int grown = Bump(body.radius);\n}\n";
    open_document(typed);
    actions_at(25);
    const char *bumped = apply_fix(typed, "Keep the call, without 'grown'");
    TIDE_REQUIRE(bumped != NULL);
    TIDE_CHECK(has(bumped, "{\n    Bump(body.radius);\n}\n"));

    static const char shared[] = GAME_TYPES "system Move(mut Body body)\n{\n"
                                            "    var a = 1; var b = 2; // Two\n    body.radius = b;\n}\n";
    open_document(shared);
    actions_at(24);
    const char *alone = apply_fix(shared, "Remove 'a'");
    TIDE_REQUIRE(alone != NULL);
    TIDE_CHECK(has(alone, "{\n    var b = 2; // Two\n    body.radius = b;\n}\n"));

    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    mut var total = 0;\n    total += 1;\n"
                             "    for (var i = 0; body.radius < 3; body.radius += 1) { }\n}\n");
    TIDE_CHECK(has(last_sent(), "local 'total' is never used\\nnote: it's assigned, but nothing reads its value"));
    TIDE_CHECK(has(last_sent(), "local 'i' is never used"));
    TIDE_CHECK(count(last_sent(), "\"tags\":[1]") == 2);
    TIDE_CHECK(!has(actions_at(24), "Remove '") && !has(actions_at(24), "Keep the call"));
    TIDE_CHECK(!has(actions_at(26), "Remove '"));

    open_document("component Bag { List<int> items; int count; }\nscene Main { }\n"
                  "event(Spawned) Setup(with Main) { Spawn(Bag); }\n"
                  "int? First(int count) { if (count == 0) return null; return count; }\n"
                  "system Count(mut Bag bag)\n{\n    foreach (var item in bag.items) bag.count += 1;\n"
                  "    if (First(bag.count) is int first) bag.count += 1;\n"
                  "    bag.items.Sort(a, b) { return false; }\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    var speed = 2;\n    body.radus = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "Body has no field 'radus'"));
    TIDE_CHECK(!has(last_sent(), "is never used"));
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    var speed = 2;\n    body.radius = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "local 'speed' is never used"));
}

// Notes about another place go there, and unused access is marked unnecessary.
TIDE_TEST(lsp_diagnostic_notes_and_tags)
{
    static const char twice[] = "settings { tickRate = 30; }\nsettings { tickRate = 20; }\nscene Main { }\n";
    start_with("{\"capabilities\":{\"textDocument\":{\"publishDiagnostics\":{\"relatedInformation\":true}}}}");
    open_document(twice);
    TIDE_CHECK(has(last_sent(), "\"message\":\"'tickRate' is set twice\",\"relatedInformation\":[{\"location\":"
                                "{\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":0,\"character\":11}"));
    TIDE_CHECK(has(last_sent(), "\"message\":\"the other one is on line 1\"}]"));
    TIDE_CHECK(!has(last_sent(), "\\nnote: the other one"));
    start();
    open_document(twice);
    TIDE_CHECK(has(last_sent(), "'tickRate' is set twice\\nnote: the other one is on"));

    open_document("component Body { float2 position; }\nsingleton Score { int total; }\n"
                  "event(Spawned) Setup(with Main) { Spawn(Body); }\n"
                  "system Look(mut Body body, Score s) { if (body.position.x > 0) return; }\nscene Main { }\n");
    TIDE_CHECK(count(last_sent(), "\"tags\":[1]") == 2);
}

// An audio declaration's state and knobs over the size limit are errors at their parameter; a state's delay line is exempt.
TIDE_TEST(lsp_audio_state_and_knobs_sizes)
{
    start();
    open_document("struct Tail { int at; float[262144] line; }\nstruct Curve { float[65] bands; }\n"
                  "audio Echo(mut Samples samples, mut Tail tail, Curve curve)\n{\n    tail.at += 1;\n"
                  "    samples.values[0] = curve.bands[0];\n}\nscene Main { }\n");
    TIDE_CHECK(diagnosed(2, 32, 3, 1, "an audio declaration's state is 1048576 bytes at the most, and 'Tail' is 1048580"));
    TIDE_CHECK(diagnosed(2, 47, 5, 1, "an audio declaration's knobs are 256 bytes at the most, and 'Curve' is 260"));
    TIDE_CHECK(has(last_sent(), "make 'Tail' smaller, with a shorter array or fewer fields"));
    open_document("struct Tail { int at; float[262143] line; }\nstruct Curve { float[64] bands; }\n"
                  "audio Echo(mut Samples samples, mut Tail tail, Curve curve)\n{\n    tail.at += 1;\n"
                  "    samples.values[0] = curve.bands[0] + tail.line[tail.at];\n}\nscene Main { }\n");
    TIDE_CHECK(!has(last_sent(), "bytes at the most") && !has(last_sent(), "\"severity\":1"));
    open_document("struct Tail { int at; float[96000] line; }\n"
                  "audio Echo(mut Samples samples, mut Tail tail)\n{\n    var copy = tail;\n"
                  "    samples.values[0] = copy.line[0];\n}\ncomponent Body { Tail tail; }\nscene Main { }\n");
    TIDE_CHECK(diagnosed(3, 15, 4, 1, "'Tail' is never copied: it holds an array of more than 4096 elements"));
    TIDE_CHECK(diagnosed(6, 17, 4, 1, "a component's field can't be 'Tail'"));
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
    TIDE_CHECK(has(move, "\"newText\":\"namespace Combat;\\n\\n// How much damage it takes.\\ncomponent Health\\n{\\n"
                         "    int value;\\n}\\n\""));
    TIDE_CHECK(has(move, "{\"range\":{\"start\":{\"line\":2,\"character\":0},\"end\":{\"line\":8,\"character\":0}},"
                         "\"newText\":\"\"}"));

    const char *system = request_at("file:///test.tide", "textDocument/codeAction", 9, 0,
                                    "\"range\":{\"start\":{\"line\":9,\"character\":0},\"end\":{\"line\":9,\"character\":0}}");
    TIDE_CHECK(!has(system, "Move '"));

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
    open_document(USES_RADIUS "view V(Body body)\n{\n    Draw.Circle(body.position, body.rad$ius, Color.red);\n}\n");
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 4);
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":false}"), "\"uri\"") == 3);

    open_document(GAME_TYPES "system Move(mut Body bo$dy)\n{\n    body.radius = body.radius * 2;\n}\n");
    const char *highlights = request("textDocument/documentHighlight");
    TIDE_CHECK(count(highlights, "\"range\"") == 3);
    TIDE_CHECK(count(highlights, "\"kind\":3") == 1);

    open_document("struct Range\n{\n    float l$o;\n    float hi;\n    float Width() { return hi - lo; }\n}\n");
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":false}"), "\"uri\"") == 1);
}

TIDE_TEST(lsp_rename)
{
    start();
    static const char program[] = USES_RADIUS "view V(Body body)\n{\n    Draw.Circle(body.position, body.radius, Color.red);\n}\n";
    open_document(USES_RADIUS "view V(Body body)\n{\n    Draw.Circle(body.position, body.rad$ius, Color.red);\n}\n");
    TIDE_CHECK(has(request("textDocument/prepareRename"), "\"result\":{\"start\""));
    request_with("textDocument/rename", "\"newName\":\"size\"");
    check_repeat_at("file:///test.tide", "textDocument/rename", cursor_line, cursor_character, "\"newName\":\"size\"");
    const char *renamed = apply_reply(program, "file:///test.tide");
    TIDE_CHECK(has(renamed, "float size = 10;"));
    TIDE_CHECK(has(renamed, "body.size += 1;"));
    TIDE_CHECK(has(renamed, "Body { size = 2 }"));
    TIDE_CHECK(has(renamed, "body.position, body.size,"));
    TIDE_CHECK(!has(renamed, "radius"));
    TIDE_CHECK(has(request("textDocument/hover"), "float radius = 10"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 4);
    char copy[8192];
    snprintf(copy, sizeof copy, "%s", renamed);
    open_document(copy);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
}

// Main renames like anything else the game declares, though the program then has no entry point.
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

    open_document("namespace Game.Combat;\n"
                  "component Health { int value; }\n"
                  "scene Main { }\n"
                  "system Hurt(mut Game.Combat.Health health) { health.value -= 1; }\n"
                  "event(Spawned) Setup(with Main) { Spawn(Game.Combat.Health { value = 3 }); Spawn(Ga$me.Combat.Health); }\n");
    request_with("textDocument/rename", "\"newName\":\"Play\"");
    renamed = apply_reply("namespace Game.Combat;\n" NAMESPACED, "file:///test.tide");
    TIDE_CHECK(count(renamed, "Play.Combat") == 4 && !has(renamed, "Game"));
}

// `namespace Name { ... }`: a file's declarations in several namespaces.
#define BLOCKS                                                    \
    "namespace Combat\n"                                          \
    "{\n"                                                         \
    "    component Health { int value; }\n"                       \
    "\n"                                                          \
    "    namespace Rules\n"                                       \
    "    {\n"                                                     \
    "        int Cap(int value) { return Math.Min(value, 9); }\n" \
    "    }\n"                                                     \
    "}\n"                                                         \
    "\n"                                                          \
    "scene Main { }\n"

TIDE_TEST(lsp_namespace_blocks)
{
    start();
    open_document(BLOCKS "system Hurt(mut Combat.Health health) { health.value = Combat.Rules.Cap(health.value); }\n"
                         "event(Spawned) Setup(with Main) { Spawn(Combat.Health); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(hover_at(0, 12), "namespace Combat") && has(hover_at(0, 12), "`Health`"));
    TIDE_CHECK(has(hover_at(4, 16), "namespace Combat.Rules") && has(hover_at(4, 16), "`Cap`"));

    const char *reply = complete("namespace Combat\n{\n    component Health { int value; }\n\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(reply, "component") && offers(reply, "system") && offers(reply, "namespace block"));
    reply = complete("namespace Combat\n{\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(reply, "component") && offers(reply, "system"));
    reply = complete(BLOCKS "$\n");
    TIDE_CHECK(offers(reply, "component") && offers(reply, "system"));

    reply = complete("namespace Combat\n{\n    component Health { int value; }\n\n    system Hurt(mut $) { }\n}\n"
                     "namespace Items\n{\n    component Bag { int coins; }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(reply, "Health") && offers(reply, "Items.Bag") && !offers(reply, "Combat.Health"));
    reply = complete("namespace Combat\n{\n    component Health { int value; }\n}\n"
                     "namespace Items\n{\n    component Bag { int coins; }\n}\nscene Main { }\nsystem Hurt(mut $) { }\n");
    TIDE_CHECK(offers(reply, "Combat.Health") && offers(reply, "Items.Bag") && !offers(reply, "Health"));
    reply = complete("namespace Game;\ncomponent Body { int mass; }\nnamespace Combat\n{\n    system Hurt(mut $) { }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(reply, "Body") && !offers(reply, "Game.Body"));

    open_document("namespace Combat\n{\n    component Health { int value; }\n}\nscene Main { }\n"
                  "system Hurt(mut Hea$lth health) { health.value -= 1; }\n");
    const char *fixes = actions_at(5);
    TIDE_CHECK(has(fixes, "\"title\":\"Write 'Combat.Health'\"") && has(fixes, "\"title\":\"Add 'using Combat;'\""));

    static const char twice[] = "namespace Combat\n{\n    component Health { int value; }\n}\n"
                                "scene Main { }\n"
                                "namespace Combat\n{\n    system Hurt(mut Health health) { health.value -= 1; }\n}\n"
                                "system Heal(mut Combat.Health health) { health.value += 1; }\n"
                                "event(Spawned) Setup(with Main) { Spawn(Combat.Health); }\n";
    open_document("namespace Com$bat\n{\n    component Health { int value; }\n}\n"
                  "scene Main { }\n"
                  "namespace Combat\n{\n    system Hurt(mut Health health) { health.value -= 1; }\n}\n"
                  "system Heal(mut Combat.Health health) { health.value += 1; }\n"
                  "event(Spawned) Setup(with Main) { Spawn(Combat.Health); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    request_with("textDocument/rename", "\"newName\":\"Fight\"");
    const char *renamed = apply_reply(twice, "file:///test.tide");
    TIDE_CHECK(count(renamed, "Fight") == 4 && !has(renamed, "Combat"));
    static const char inner[] = "namespace Game;\nnamespace Combat\n{\n    component Health { int value; }\n}\n"
                                "scene Main { }\nsystem Heal(mut Game.Combat.Health health) { health.value += 1; }\n";
    open_document("namespace Game;\nnamespace Combat\n{\n    component Health { int value; }\n}\n"
                  "scene Main { }\nsystem Heal(mut Game.Com$bat.Health health) { health.value += 1; }\n");
    request_with("textDocument/rename", "\"newName\":\"Fight\"");
    renamed = apply_reply(inner, "file:///test.tide");
    TIDE_CHECK(has(renamed, "namespace Fight\n{") && has(renamed, "Game.Fight.Health") && !has(renamed, "Combat"));

    static const char messy[] = "namespace Combat {\ncomponent Health { int value; }\n\n"
                                "  namespace Rules\n{\nint Cap(int value)\n{\nreturn value;\n}\n}\n"
                                "system Hurt(mut Health health)\n{\nhealth.value -= 1;\n}\n}\nscene Main { }\n";
    static const char tidy[] = "namespace Combat\n{\n    component Health { int value; }\n\n"
                               "    namespace Rules\n    {\n        int Cap(int value)\n        {\n            return value;\n        }\n    }\n"
                               "    system Hurt(mut Health health)\n    {\n        health.value -= 1;\n    }\n}\nscene Main { }\n";
    format_reply(messy);
    TIDE_CHECK(strcmp(apply_reply(messy, NULL), tidy) == 0);
    TIDE_CHECK(has(format_reply(tidy), "\"result\":[]"));
    static const char ordered[] = "namespace Combat\n{\n    system First() { }\n\n    [After(First)]\n    system Second() { }\n}\nscene Main { }\n";
    TIDE_CHECK(has(format_reply(ordered), "\"result\":[]"));

    open_document("namespace Combat\n{\n    component Health { int value; }\n\n    system Hurt(mut Health health\n\n"
                  "    system Heal(mut Health health) { health.value += 1; }\n}\n\nscene Main { }\nsystem Look(Combat.Hea$lth health) { }\n");
    TIDE_CHECK(count(last_sent(), "\"severity\":1") == 1);
    TIDE_CHECK(has(request("textDocument/hover"), "component Health"));

    open_document("namespace Game;\nnamespace Combat\n{\n    component Health\n    {\n        int value;\n    }\n}\n\n"
                  "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Combat.Health); }\n");
    const char *move = request_at("file:///test.tide", "textDocument/codeAction", 3, 0,
                                  "\"range\":{\"start\":{\"line\":3,\"character\":0},\"end\":{\"line\":3,\"character\":0}}");
    TIDE_CHECK(has(move, "\"title\":\"Move 'Health' to Health.tide\""));
    TIDE_CHECK(has(move, "\"newText\":\"namespace Game.Combat;\\n\\ncomponent Health\\n{\\n    int value;\\n}\\n\""));

    open_document("scene Main { }\nnamespace Combat\n{\n    component Health { int value; }\n");
    TIDE_CHECK(has(last_sent(), "to end the namespace 'Combat'"));
    open_document("scene Main { }\nnamespace Combat\n{\n    using Physics;\n}\n");
    TIDE_CHECK(has(last_sent(), "'using' goes at the top of the file"));
}

TIDE_TEST(lsp_rename_refusals)
{
    start();
    open_document(USES_RADIUS "view V(Body body)\n{\n    Draw.Circle(body.position, body.rad$ius, Color.red);\n}\n");
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"position\""), "'position' is already declared"));
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"return\""), "keyword"));
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"float3\""), "built into the language"));
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"2fast\""), "start with a letter"));
    const char *const errors[] = {"\"newName\":\"position\"", "\"newName\":\"return\"", "\"newName\":\"float3\"", "\"newName\":\"2fast\""};
    for (size_t i = 0; i < sizeof errors / sizeof errors[0]; i++) {
        check_repeat_at("file:///test.tide", "textDocument/rename", cursor_line, cursor_character, errors[i]);
    }
    TIDE_CHECK(has(request("textDocument/hover"), "float radius = 10"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 4);

    open_document(USES_RADIUS "view V(Body body)\n{\n    Draw.Cir$cle(body.position, body.radius, Color.red);\n}\n");
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Built-in names can't be renamed"));

    open_document(GAME_TYPES "system Mo$ve(mut Body body)\n{\n    body.radius = ;\n}\n");
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Fix the syntax errors first"));
}

#define BUILT_IN_METHODS                                                                                    \
    "singleton Field { Grid2<int> cells = Grid2(8, 8); List<int> items; string name; }\n"                   \
    "singleton Camera { float2 center; }\ncomponent Body { float x; }\nscene Main { }\n"

// Built-in methods are what they're called on's: a list's Clear isn't an entity's Destroy.
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
    open_document("singleton Field { Grid2<int> cells = Gr$id2(8, 8); }\nscene Main { }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Grid2<int>"));

    open_document(BUILT_IN_METHODS "system S(mut Field field)\n{\n    field.items.Insert($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "List<int>.Insert(int index, int item)"));
    open_document(BUILT_IN_METHODS "system S(mut Field field)\n{\n    field.items.Add($\n}\n");
    const char *add = request("textDocument/signatureHelp");
    TIDE_CHECK(has(add, "List<int>.Add(int item)") && !has(add, "components"));
    TIDE_CHECK(offers(complete(BUILT_IN_METHODS "system S(mut Field field)\n{\n    field.items.$\n}\n"), "Resize"));
    open_document(BUILT_IN_METHODS "system S(mut Field field)\n{\n    field.items.Res$ize(4);\n}\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    const char *resize = request("textDocument/hover");
    TIDE_CHECK(has(resize, "List<int>.Resize(int count)") && has(resize, "new ones are the element type's zero"));
    open_document(BUILT_IN_METHODS "system S(mut Field field)\n{\n    field.items.Resize($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "List<int>.Resize(int count)"));
    open_document(BUILT_IN_METHODS "system S(Field field)\n{\n    field.items.Resize(4);\n}\n");
    TIDE_CHECK(has(last_sent(), "'field' is read-only") && has(last_sent(), "'List.Resize' is a mut method"));
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
    TIDE_CHECK(has(grid, "Grid2(int width = 0, int height = 0)") && has(grid, "\"activeParameter\":1"));
    open_document("scene Main { }\nview V()\n{\n    Draw.Text(\"a\", $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Draw.Text(string text, float2 position, float size, Color color)"));

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

// Inlay hints name the parameters literal arguments go to, except one-letter names.
TIDE_TEST(lsp_inlay_hints_for_built_ins)
{
    start();
    open_document("local singleton Menu { float volume; }\nscene Main { }\n"
                  "view V(mut Menu menu)\n{\n    Draw.Circle(float2(1, 2), 3, Color.red);\n"
                  "    menu.volume = Math.Clamp(0.5, 0, menu.volume);\n"
                  "    GUILayout.Slider(\"Volume\", menu.volume, 0, 1);\n    Session.Open(7777);\n"
                  "    var c = Color(1, 0.5, 0);\n}\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":20,\"character\":0}}");
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":4,\"character\":30},\"label\":\"radius:\",\"kind\":2"));
    TIDE_CHECK(!has(hints, "\"label\":\"center:\"") && !has(hints, "\"label\":\"color:\""));
    TIDE_CHECK(has(hints, "\"label\":\"label:\"") && has(hints, "\"label\":\"min:\"") && has(hints, "\"label\":\"max:\""));
    TIDE_CHECK(has(hints, "\"label\":\"port:\""));
    TIDE_CHECK(!has(hints, "\"label\":\"x:\"") && !has(hints, "\"label\":\"a:\"") && !has(hints, "\"label\":\"r:\""));
}

// Sample's omitted playback is a compiler default, not a written literal.
TIDE_TEST(lsp_skinned_sample_hints_only_written_arguments)
{
    start();
    open_document("Pose AtTime(Animation current, float seconds) { return current.Sample(seconds); }\n"
                  "Pose AtZero(Animation current) { return current.Sample(0); }\n"
                  "Pose AtNamedZero(Animation current) { return current.Sample(seconds: 0); }\n"
                  "Pose AtReordered(Animation current) { return current.Sample(playback: Playback.Loop, seconds: 0); }\n"
                  "Pose AtDefault(Animation current) { return current.Sample(0, default); }\n"
                  "void Update(mut Pose pose, Animation current, float seconds)\n{\n"
                  "    pose.Sample(current, seconds);\n"
                  "    pose.Sample(current, 0);\n"
                  "    pose.Sample(seconds: 0, animation: current);\n"
                  "    pose.Sample(current, 0, default);\n}\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":20,\"character\":0}}");
    TIDE_CHECK(count(hints, "\"label\":\"seconds:\"") == 4);
    TIDE_CHECK(count(hints, "\"label\":\"playback:\"") == 2);
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":1,\"character\":55},\"label\":\"seconds:\",\"kind\":2"));
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":4,\"character\":61},\"label\":\"playback:\",\"kind\":2"));
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":10,\"character\":28},\"label\":\"playback:\",\"kind\":2"));
    char *first = malloc(strlen(hints) + 1);
    strcpy(first, hints);
    TIDE_CHECK(strcmp(first, request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                       "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":20,\"character\":0}}")) == 0);
    free(first);
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 0, 65, ""), "Playback.Clamp"));
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
    open_document("scene Arena { Prediction prediction; }\nscene Main { }\n"
                  "event(Spawned) Setup(with Main) { Scene.Load(Arena { prediction = Prediction.Roll$back }); }\n");
    TIDE_CHECK(has(request("textDocument/definition"), "\"result\":null"));
    TIDE_CHECK(has(request("textDocument/prepareRename"), "Members of the engine's enums can't be renamed."));

    open_document("local scene Main { }\nview Menu()\n{\n    Sess$ion.Leave();\n}\n");
    const char *session = request("textDocument/hover");
    TIDE_CHECK(has(session, "Start, Open, Close, Kick, KickAll, Join, Connect, Leave, End, Watch and Unwatch") && !has(session, "Play"));
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
    TIDE_CHECK(count(local, "\"kind\":3") == 4);
    TIDE_CHECK(count(local, "\"kind\":2") == 1);
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.rad$ius = 1;\n    var r = body.radius;\n}\n");
    const char *field = request("textDocument/documentHighlight");
    TIDE_CHECK(count(field, "\"kind\":3") == 2 && count(field, "\"kind\":2") == 1);
}

TIDE_TEST(lsp_signature_help)
{
    start();
    open_document(GAME_TYPES "view V(Body body)\n{\n    Draw.Circle(body.position, $\n}\n");
    const char *circle = request("textDocument/signatureHelp");
    TIDE_CHECK(has(circle, "\"label\":\"Draw.Circle(float2 center, float radius, Color color)\""));
    TIDE_CHECK(has(circle, "\"activeParameter\":1"));
    TIDE_CHECK(has(circle, "\"parameters\":[{\"label\":[12,25]}"));

    open_document(GAME_TYPES "system S()\n{\n    var v = float3(float2(1, 2), $\n}\n");
    const char *vector = request("textDocument/signatureHelp");
    TIDE_CHECK(has(vector, "float3(float2 xy, float z)"));
    TIDE_CHECK(has(vector, "\"activeSignature\":0,\"activeParameter\":1"));

    open_document(GAME_TYPES "system S()\n{\n    Spawn(Body { position = float2(1, 2), radius = 3 }, $\n}\n");
    const char *spawn = request("textDocument/signatureHelp");
    TIDE_CHECK(has(spawn, "Spawn(components...)"));
    TIDE_CHECK(has(spawn, "\"activeParameter\":0"));

    open_document(GAME_TYPES "system S()\n{\n    $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"result\":null"));
}

TIDE_TEST(lsp_quaternion_euler_signature_help)
{
    start();
    static const struct {
        const char *arguments;
        bool vector;
        int parameter;
    } cases[] = {
        {"$float3(0.1, 0.2, 0.3)", true, 0},
        {"0.1, 0.2, $0.3", false, 2},
        {"z: $0.3, x: 0.1, y: 0.2", false, 2},
        {"z: 0.3, x: $0.1, y: 0.2", false, 0},
        {"z: 0.3, x: 0.1, y: $0.2", false, 1},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char source[256];
        snprintf(source, sizeof source, "scene Main { }\nquaternion Rotation() { return quaternion.Euler(%s); }\n", cases[i].arguments);
        open_document(source);
        TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
        const char *reply = request("textDocument/signatureHelp");
        TIDE_CHECK(has(reply, "quaternion.Euler(float3 radians)")
                   && has(reply, "quaternion.Euler(float x, float y, float z)") && has(reply, "in radians"));
        const json *result = json_get(json_parse(reply, strlen(reply)), "result");
        const json *signatures = json_get(result, "signatures");
        const int active = json_int(json_get(result, "activeSignature"), -1);
        TIDE_REQUIRE(signatures && active >= 0 && active < signatures->count);
        const char *label = json_str(json_get(signatures->items[active], "label"));
        TIDE_REQUIRE(label);
        TIDE_CHECK(strcmp(label, cases[i].vector ? "quaternion.Euler(float3 radians) -> quaternion" : "quaternion.Euler(float x, float y, float z) -> quaternion") == 0);
        TIDE_CHECK(json_int(json_get(result, "activeParameter"), -1) == cases[i].parameter);
        json_release();
    }
}

TIDE_TEST(lsp_singleton_function_parameters)
{
    start();
    open_document("singleton Count { int value; }\n"
                  "void Add(mut Count count) { count.value += 1; }\n"
                  "scene Main { }\nsystem Tick(mut Count count) { Add($count); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document("singleton Count { int value; }\n"
                  "void Add(mut Count count) { count.value += 1; }\n"
                  "scene Main { }\nsystem Tick(mut Count count)\n{\n    Add($\n}\n");
    const char *signature = request("textDocument/signatureHelp");
    TIDE_CHECK(has(signature, "void Add(mut Count count)"));
    TIDE_CHECK(has(signature, "\"activeParameter\":0"));

    open_document("singleton Count { int value; }\n"
                  "void Add(mut Count count) { count.value += 1; }\n"
                  "scene Main { }\nsystem Tick(Count count) { Add(count); }\n");
    TIDE_CHECK(has(last_sent(), "'count' is read-only"));
    TIDE_CHECK(!has(last_sent(), "can't take singletons"));
}

// A long signature keeps every parameter, even when the last is passed first by name; the short overload mustn't become active.
TIDE_TEST(lsp_many_function_parameters)
{
    for (int named = 0; named < 2; named++) {
        start();
        jbuf source = {0};
        jb_put(&source, "int Many(");
        for (int i = 0; i < 40; i++) jb_printf(&source, "%sint value%d", i ? ", " : "", i);
        jb_put(&source, ") { return ");
        for (int i = 0; i < 40; i++) jb_printf(&source, "%svalue%d", i ? " + " : "", i);
        jb_put(&source, "; }\nint Many(int single) { return single; }\n"
                        "singleton State { int value; }\nscene Main { }\n"
                        "system Tick(mut State state) { state.value = Many(");
        if (named) jb_put(&source, "value39: 3$9, ");
        for (int i = 0; i < (named ? 39 : 40); i++) {
            if (i) jb_put(&source, ", ");
            if (named) jb_printf(&source, "value%d: ", i);
            if (!named && i == 39) jb_put(&source, "3$9");
            else jb_printf(&source, "%d", i);
        }
        jb_put(&source, "); }\n");
        open_document(source.data);
        TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
        const char *reply = request("textDocument/signatureHelp");
        const json *result = json_get(json_parse(reply, strlen(reply)), "result");
        const json *signatures = json_get(result, "signatures");
        const int active = json_int(json_get(result, "activeSignature"), -1);
        TIDE_REQUIRE(signatures && active >= 0 && active < signatures->count);
        const json *signature = signatures->items[active];
        const json *params = json_get(signature, "parameters");
        TIDE_REQUIRE(params && params->count == 40);
        TIDE_CHECK(json_int(json_get(result, "activeParameter"), -1) == 39);
        const json *span = json_get(params->items[39], "label");
        const char *label = json_str(json_get(signature, "label"));
        TIDE_REQUIRE(span && span->count == 2 && label);
        const int from = json_int(span->items[0], -1);
        const int to = json_int(span->items[1], -1);
        TIDE_REQUIRE(from >= 0 && from <= to && to <= (int)strlen(label));
        TIDE_CHECK(to - from == (int)strlen("int value39"));
        TIDE_CHECK(strncmp(label + from, "int value39", strlen("int value39")) == 0);
        json_release();
        check_repeat_at("file:///test.tide", "textDocument/signatureHelp", cursor_line, cursor_character, "");
        const char *hints = request_with("textDocument/inlayHint",
                                         "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":10,\"character\":0}}");
        if (named) {
            TIDE_CHECK(has(hints, "\"result\":[]"));
        } else {
            TIDE_CHECK(count(hints, "\"kind\":2") == 40 && has(hints, "\"label\":\"value39:\""));
        }
        check_repeat_at("file:///test.tide", "textDocument/inlayHint", cursor_line, cursor_character,
                        "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":10,\"character\":0}}");
        TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 0, 5, ""), "int value39"));
        TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 0, 5,
                                    "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 2);
        jb_free(&source);
    }
}

static const char *format_reply(const char *text)
{
    open_document(text);
    return request_with("textDocument/formatting", "\"options\":{\"tabSize\":4,\"insertSpaces\":true}");
}

TIDE_TEST(lsp_degree_literals_keep_source_spelling)
{
    start();
    static const char messy[] =
        "const float HALF_TURN=180deg;\nscene Main { }\nfloat Turn()\n{\nreturn -1_8_0deg+.5deg+1e2deg;\n}\n"
        "quaternion Facing()\n{\nreturn quaternion.Euler(0,HALF_TURN,0);\n}\n";
    static const char expected[] =
        "const float HALF_TURN = 180deg;\nscene Main { }\nfloat Turn()\n{\n    return -1_8_0deg + .5deg + 1e2deg;\n}\n"
        "quaternion Facing()\n{\n    return quaternion.Euler(0, HALF_TURN, 0);\n}\n";
    open_document(messy);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request_at("file:///test.tide", "textDocument/hover", 0, 15, "");
    TIDE_CHECK(has(hover, "const float HALF_TURN = 180deg"));
    format_reply(messy);
    TIDE_CHECK(strcmp(apply_reply(messy, NULL), expected) == 0);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));
    open_document("scene Main { }\nquaternion Facing() { return quaternion.Euler(0, $180deg, 0); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *signature = request("textDocument/signatureHelp");
    TIDE_CHECK(has(signature, "quaternion.Euler(float x, float y, float z)")
               && has(signature, "\"activeParameter\":1"));
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
    check_repeat_at("file:///test.tide", "textDocument/formatting", 0, 0,
                    "\"options\":{\"tabSize\":4,\"insertSpaces\":true}");
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 2, 11, ""), "component Body"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 2, 11,
                                "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 3);

    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));

    static const char continued[] =
        "system Main()\n{\n    var x = 1\n    + 2;\n    Spawn(Owner,\n          Owner);\n}\n";
    format_reply(continued);
    TIDE_CHECK(strcmp(apply_reply(continued, NULL),
                      "system Main()\n{\n    var x = 1\n        + 2;\n    Spawn(Owner,\n          Owner);\n}\n") == 0);
}

// Selection grows from the word at the cursor out to its declaration.
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
    const char *two = request_with("textDocument/selectionRange", "\"positions\":[{\"line\":0,\"character\":0},{\"line\":30,\"character\":0}]");
    TIDE_CHECK(count(two, "{\"range\":") == 3);
    check_repeat_at("file:///test.tide", "textDocument/selectionRange", cursor_line, cursor_character,
                    "\"positions\":[{\"line\":26,\"character\":38}]");
    TIDE_CHECK(has(request("textDocument/hover"), "float radius"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 4);
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
    TIDE_CHECK(strcmp(formatted, "scene Main { }\nsystem S()\n{\nmut var n = 0;\nif (n > 0)\n        n += 1;\nn+=2;\n}\n") == 0);
    if (strcmp(formatted, "scene Main { }\nsystem S()\n{\nmut var n = 0;\nif (n > 0)\n        n += 1;\nn+=2;\n}\n") != 0) {
        printf("--- got:\n%s---\n", formatted);
    }
}

// C#-style braces: a block that spans lines has its braces on lines of their own; one-line actions and literals stay.
TIDE_TEST(lsp_format_braces)
{
    start();
    static const char js[] =
        "component Body {\n    float2 position;\n}\n"
        "input Keys {\n    bool fire;\n\n    Sample(Devices devices) {\n        fire = devices.keyboard.space.pressed; }\n}\n"
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
        "input Keys\n{\n    bool fire;\n\n    Sample(Devices devices)\n    {\n        fire = devices.keyboard.space.pressed;\n    }\n}\n"
        "system Move(mut Body body)\n{ // Every body\n"
        "    if (body.position.x > 1)\n    {\n        body.position.x = 0;\n    }\n    else\n    {\n        body.position.x += 1;\n    }\n"
        "    if (body.position.y > 1) { return; }\n    else\n    {\n        body.position.y = 0;\n    }\n"
        "    if (body.position.y < 0) { return; }\n"
        "    var b = Spawn(Body { position = float2(1) });\n"
        "    Spawn(Body {\n        position = float2(2)\n    });\n"
        "}\n"
        "system Main() { }\n";
    format_reply(js);
    const char *formatted = apply_reply(js, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);

    char again[8192];
    snprintf(again, sizeof again, "%s", formatted);
    TIDE_CHECK(has(format_reply(again), "\"result\":[]"));

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
        "Draw.Text(\"a  b   \\\"c\\\"\", body.position,12,Color(1,0.5,0));\n}\n";
    format_reply(program);
    char formatted[8192];
    snprintf(formatted, sizeof formatted, "%s", apply_reply(program, NULL));
    TIDE_CHECK(strcmp(formatted, program) != 0);

    char a[8192];
    char b[8192];
    size_t na = 0;
    size_t nb = 0;
    for (const char *p = program; *p; p++) if (*p != ' ' && *p != '\n') a[na++] = *p;
    for (const char *p = formatted; *p; p++) if (*p != ' ' && *p != '\n') b[nb++] = *p;
    a[na] = b[nb] = '\0';
    TIDE_CHECK(strcmp(a, b) == 0);
    TIDE_CHECK(has(formatted, "\"a  b   \\\"c\\\"\""));

    open_document(formatted);
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(has(format_reply(formatted), "\"result\":[]"));
}

// Types `program` out from its first `from` characters on, analysing and querying every prefix as an editor does.
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
        "input Controls\n{\n    float2 aim;\n\n    Sample(Devices devices)\n    {\n"
        "        var keys = devices.keyboard;\n        if (keys.w.pressed) aim.y += 1;\n    }\n}\n"
        "system Move(Controls controls, Devices devices, Time time, mut Body body, without Arena)\n{\n"
        "    if (devices.gamepad.buttonSouth.down) body.radius += 1;\n"
        "    mut var speed = Math.Length(body.position.xy) * 2;\n"
        "    if (controls.aim.x > 0 && speed < 10) { body.position += controls.aim * time.dt; }\n"
        "    else body.radius = Math.Clamp(body.radius, 1, 2);\n"
        "    var e = Spawn(Body { position = float2(1, 2) });\n    e.Send(Hit { damage = 2 });\n    e.Destroy();\n}\n"
        "event Hit\n{\n    int damage = 1;\n}\n"
        "event(Hit hit) TakeHit(mut Body body)\n{\n    body.radius -= hit.damage;\n}\n"
        "view DrawBody(Body body, Arena arena)\n{\n"
        "    Draw.Text(\"hi \\\"there\\\"\", body.position, 12, Color(1, 0.5, 0));\n"
        "    Draw.Circle(body.position, body.radius, Color.red);\n}\n"
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

// The same for frags, verts and the view that draws through them, and at every place once they're whole.
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
    TIDE_CHECK(offers(complete("scene Main { }\nfrag Copy(Vertex v, out Color color, $"), "Texture"));
    TIDE_CHECK(offers(complete("scene Main { }\nfrag Copy(Vertex v, out Color color, Texture source)\n"
                              "{\n    color = source.$\n}\n"), "Sample"));
    snprintf(program, sizeof program, "%sfrag Copy(Vertex v, out Color color, Texture source)\n"
                                      "{ color = source.Sample(v.uv); }\n"
                                      "view V(Buffers buffers)\n{\n    Draw.Blit(buffers.scene.color, $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "RenderTarget destination"));
    TIDE_CHECK(has(last_sent(), "\"activeParameter\":1"));
    snprintf(program, sizeof program, "%sview V(Buffers buffers)\n{\n    buffers.scene.Cl$ear(Color.black);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "Draw.Clear"));
    snprintf(program, sizeof program, "%sview V(Buffers buffers)\n{\n    buffers.scene.Mesh([], [], $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "[, texture[, filter[, wrap]]]"));
    snprintf(program, sizeof program, "%sview V(Buffers buffers)\n{\n    buffers.scene.Resize(16, $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Resize(float width, float height)"));
    snprintf(program, sizeof program, "%sview V()\n{\n    var scene = RenderTarget(16, $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "RenderTarget(float width, float height)"));
    TIDE_CHECK(has(last_sent(), "RenderTarget(float width, float height, float pixelsPerUnit)"));
    snprintf(program, sizeof program, "%sview V()\n{\n    var scene = RenderTarget.$\n}\n", types);
    TIDE_CHECK(offers(complete(program), "Window"));
    snprintf(program, sizeof program, "%sview V()\n{\n    var scene = RenderTarget.Window($\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "always the window's size"));
    snprintf(program, sizeof program, "%sview V()\n{\n    var scene = RenderTarget.Win$dow(0.5);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "always the window's size"));
    snprintf(program, sizeof program, "%sview V(Buffers buffers)\n{\n    buffers.scene.Dis$pose();\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "All target and color aliases"));
}

// Render targets: formats, depth as a DepthTexture a frag reads, constructors, Screen, Sample and Compare.
TIDE_TEST(lsp_render_target_formats_and_depth)
{
    start();
    static const char types[] = "scene Main { }\nlocal singleton Lamp { RenderTarget map; DepthTexture shadow; }\n";
    char program[2048];
    snprintf(program, sizeof program, "%sfrag Lit(Vertex3 vertex, out Color color, Lamp lamp)\n{\n"
                                      "    float lit = lamp.shadow.Compare(vertex.uv, 0.5) * lamp.shadow.Sample(vertex.uv);\n"
                                      "    color = Color(lit, lit, lit);\n}\n"
                                      "view V(mut Lamp lamp)\n{\n"
                                      "    lamp.map = RenderTarget(64, 64, Format.Depth);\n"
                                      "    lamp.map = RenderTarget(64, 64, 1, Format.Depth);\n"
                                      "    lamp.map = RenderTarget.Window(Format.Depth);\n"
                                      "    lamp.map = RenderTarget.Window(0.5, Format.Rgba8);\n"
                                      "    lamp.shadow = lamp.map.depth;\n"
                                      "    Draw.Mesh([Vertex3 { }], [0, 0, 0], float4x4.identity, Lit);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    snprintf(program, sizeof program, "%sview V(mut Lamp lamp)\n{\n    lamp.map = RenderTarget(64, 64, Format.$);\n}\n", types);
    const char *formats = complete(program);
    TIDE_CHECK(offers(formats, "Rgba8") && offers(formats, "Depth"));
    snprintf(program, sizeof program, "%sview V(mut Lamp lamp)\n{\n    lamp.map = RenderTarget(64, 64, $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "RenderTarget(float width, float height, Format formats...)"));
    TIDE_CHECK(has(last_sent(), "RenderTarget(float width, float height, float pixelsPerUnit, Format formats...)"));
    TIDE_CHECK(has(last_sent(), "depth alone"));
    snprintf(program, sizeof program, "%sview V(mut Lamp lamp)\n{\n    lamp.map = RenderTarget.Window($\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Format format") && has(last_sent(), "float factor, Format format"));

    snprintf(program, sizeof program, "%sview V(mut Lamp lamp)\n{\n    lamp.shadow = lamp.map.$\n}\n", types);
    const char *members = complete(program);
    TIDE_CHECK(offers(members, "depth") && offers(members, "color") && has(members, "DepthTexture"));
    snprintf(program, sizeof program, "%sview V(mut Lamp lamp)\n{\n    var shadow = lamp.map.depth;\n    lamp.shadow = sha$dow;\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "DepthTexture"));
    TIDE_CHECK(offers(complete("scene Main { }\nlocal singleton Lamp { $ }\n"), "DepthTexture"));

    snprintf(program, sizeof program, "%sfrag Lit(Vertex3 vertex, out Color color, Lamp lamp)\n{\n    float lit = lamp.shadow.$\n}\n", types);
    const char *reads = complete(program);
    TIDE_CHECK(offers(reads, "Sample") && offers(reads, "Compare"));
    TIDE_CHECK(has(reads, "DepthTexture.Compare(float2 uv, float value) -> float") && has(reads, "DepthTexture.Sample(float2 uv) -> float"));
    snprintf(program, sizeof program, "%sfrag Lit(Vertex3 vertex, out Color color, Lamp lamp)\n{\n"
                                      "    float lit = lamp.shadow.Compare(vertex.uv, $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "DepthTexture.Compare(float2 uv, float value) -> float"));
    TIDE_CHECK(has(last_sent(), "\"activeParameter\":1"));
    snprintf(program, sizeof program, "%sfrag Lit(Vertex3 vertex, out Color color, Lamp lamp)\n{\n"
                                      "    float lit = lamp.shadow.Com$pare(vertex.uv, 0.5);\n    color = Color(lit, lit, lit);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "isn't nearer than"));
    snprintf(program, sizeof program, "%sfrag Lit(Vertex3 vertex, out Color color, Lamp lamp)\n{\n"
                                      "    float far = lamp.shadow.Sam$ple(vertex.uv);\n    color = Color(far, far, far);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "the nearest thing drawn"));

    snprintf(program, sizeof program, "%sfrag Lit(Vertex3 vertex, out Color color, DepthTexture shadow)\n{\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "a field of a component or singleton it takes"));
}

// A function a frag calls samples the textures it takes as parameters.
TIDE_TEST(lsp_textures_in_functions)
{
    start();
    static const char types[] = "scene Main { }\n"
                                "local singleton Lamp { RenderTarget map; DepthTexture shadow; Texture image; Cubemap sky; }\n";
    char program[4096];
    snprintf(program, sizeof program, "%sfloat Sunlit(DepthTexture depth, float3 at) { return depth.Compare(at.xy, at.z) * depth.Sample(at.xy); }\n"
                                      "float Twice(DepthTexture depth, float3 at) { return Sunlit(depth, at) * 2; }\n"
                                      "Color Tile(Texture image, float2 uv) { return image.Sample(uv, Filter.Point, Wrap.Repeat); }\n"
                                      "Color Above(Cubemap sky, float3 way) { return sky.Sample(way); }\n"
                                      "frag Lit(Vertex3 vertex, out Color color, Lamp lamp)\n{\n"
                                      "    float lit = Twice(lamp.shadow, this.world);\n"
                                      "    color = Tile(lamp.image, vertex.uv) * Above(lamp.sky, this.world) * lit;\n}\n"
                                      "view V(mut Lamp lamp)\n{\n"
                                      "    lamp.shadow = lamp.map.depth;\n"
                                      "    Draw.Mesh([Vertex3 { }], [0, 0, 0], float4x4.identity, Lit);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    snprintf(program, sizeof program, "%sfloat Sunlit(DepthTexture depth, float3 at)\n{\n    return depth.$\n}\n", types);
    const char *reads = complete(program);
    TIDE_CHECK(offers(reads, "Sample") && offers(reads, "Compare"));
    TIDE_CHECK(has(reads, "DepthTexture.Compare(float2 uv, float value) -> float"));
    snprintf(program, sizeof program, "%sColor Tile(Texture image, float2 uv)\n{\n    return image.$\n}\n", types);
    TIDE_CHECK(offers(complete(program), "Sample"));
    TIDE_CHECK(offers(complete(program), "size"));
    snprintf(program, sizeof program, "%sint2 Size(Texture image) { return image.size; }\nview V(Lamp lamp) { Debug.Log($$\"{Size(lamp.image)}\"); }\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    snprintf(program, sizeof program, "%sview V(Lamp lamp) { var extent = lamp.image.si$ze; Debug.Log($$\"{extent}\"); }\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "physical pixels"));
    snprintf(program, sizeof program, "%sview V(Lamp lamp) { lamp.image.$ }\n", types);
    TIDE_CHECK(offers(complete(program), "size"));
    snprintf(program, sizeof program, "%sColor Above(Cubemap sky, float3 way)\n{\n    return sky.$\n}\n", types);
    TIDE_CHECK(offers(complete(program), "Sample"));
    snprintf(program, sizeof program, "%sfloat Sunlit(DepthTexture depth, float3 at)\n{\n    float near = depth.Sample(at.xy);\n"
                                      "    return near * depth.$\n}\n", types);
    TIDE_CHECK(offers(complete(program), "Compare"));
    snprintf(program, sizeof program, "%sview V(mut Lamp lamp)\n{\n    lamp.shadow.$\n}\n", types);
    TIDE_CHECK(!offers(complete(program), "Compare"));

    snprintf(program, sizeof program, "%sfloat Sunlit(Depth$Texture depth, float3 at) { return depth.Compare(at.xy, at.z); }\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "reads the depth its call names"));
    snprintf(program, sizeof program, "%sfloat Sunlit(DepthTexture depth, float3 at) { return depth.Com$pare(at.xy, at.z); }\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "A function they call compares the depth it takes"));

    snprintf(program, sizeof program, "%sfloat Sunlit(DepthTexture depth, float3 at) { return depth.Compare(at.xy, at.z); }\n"
                                      "view V(mut Lamp lamp)\n{\n    lamp.shadow = lamp.map.depth;\n"
                                      "    Debug.Log(Sunlit(lamp.shadow, float3(0)));\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "reads a render target's depth, which only code the GPU runs does"));
    snprintf(program, sizeof program, "%sfloat Sunlit(Lamp lamp, float3 at) { return lamp.shadow.Compare(at.xy, at.z); }\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "a function samples a DepthTexture it takes as a parameter of its own"));
}

// A target of several color images: a format each, `colors`, and a frag's outputs after the pixel's color.
TIDE_TEST(lsp_render_target_samples)
{
    start();
    static const char types[] = "scene Main { }\nlocal singleton Maps { RenderTarget scene; DepthTexture deep; }\n";
    char program[2048];
    snprintf(program, sizeof program, "%sconst int SMOOTH = 4;\nview V(mut Maps maps)\n{\n"
                                      "    maps.scene = RenderTarget(64, 64, samples: 4);\n"
                                      "    maps.scene = RenderTarget(64, 64, 1, Format.Rgba8, Format.Rgba16f, samples: SMOOTH);\n"
                                      "    maps.scene = RenderTarget.Window(samples: 2);\n"
                                      "    maps.scene = RenderTarget.Window(0.5, Format.Rgba16f, samples: 8);\n"
                                      "    maps.deep = maps.scene.depth;\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene = RenderTarget(64, 64, $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "samples: 4") && has(last_sent(), "smooths the edges"));
    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene = RenderTarget.Window($\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "RenderTarget.Window(samples: 4)"));
    snprintf(program, sizeof program, "%slocal singleton Other { Render$Target scene; }\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "samples: 4"));

    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene = RenderTarget(64, 48, 2, Format.Rgba16f, samples: 4);\n"
                                      "    maps.scene = RenderTarget.Window(0.5, samples: 4);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":20,\"character\":0}}");
    TIDE_CHECK(has(hints, "\"label\":\"width:\"") && has(hints, "\"label\":\"pixelsPerUnit:\"") && has(hints, "\"label\":\"factor:\""));
    TIDE_CHECK(!has(hints, "\"label\":\"samples:\""));

    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene = RenderTarget(64, 64, samples: 3);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "samples are 1, 2, 4 or 8, and this says 3"));
    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene = RenderTarget(64, 64, Format.Depth, samples: 4);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "samples smooth a target's colors, and it has none"));
    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene = RenderTarget.Window(factor: 0.5);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "without names, but for its samples"));
}

TIDE_TEST(lsp_render_target_colors_and_frag_outputs)
{
    start();
    static const char types[] = "scene Main { }\nlocal singleton Maps { RenderTarget scene; Texture normals; }\n";
    char program[2048];
    snprintf(program, sizeof program, "%sfrag Fill(Vertex3 vertex, out Color albedo, out Color normal)\n{\n"
                                      "    albedo = vertex.color;\n    normal = Color(0, 0, 1);\n}\n"
                                      "view V(mut Maps maps)\n{\n"
                                      "    maps.scene = RenderTarget(64, 64, Format.Rgba8, Format.Rgba16f);\n"
                                      "    maps.scene = RenderTarget(64, 64, 1, Format.Rgba16f);\n"
                                      "    maps.scene = RenderTarget.Window(0.5, Format.Rgba8, Format.Rgba16f, Format.Rgba16f);\n"
                                      "    maps.normals = maps.scene.colors[1];\n"
                                      "    maps.scene.Mesh([Vertex3 { }], [0, 0, 0], float4x4.identity, Fill);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene = RenderTarget(64, 64, Format.Rgba8, Format.$);\n}\n", types);
    const char *formats = complete(program);
    TIDE_CHECK(offers(formats, "Rgba16f") && offers(formats, "Rgba8") && offers(formats, "Depth"));
    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene = RenderTarget(64, 64, Format.Rgba8, $\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "RenderTarget(float width, float height, Format formats...)"));
    TIDE_CHECK(has(last_sent(), "one for each color image") && has(last_sent(), "target.colors[1]"));
    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene = RenderTarget.Window($\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "RenderTarget.Window(Format.Rgba8, Format.Rgba16f)"));

    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.normals = maps.scene.$\n}\n", types);
    const char *members = complete(program);
    TIDE_CHECK(offers(members, "colors") && offers(members, "color") && offers(members, "depth"));
    TIDE_CHECK(has(members, "colors[1]") && has(members, "in the order of the formats it was made with"));
    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    var second = maps.scene.colors[1];\n    maps.normals = sec$ond;\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "Texture"));
    snprintf(program, sizeof program, "%sfrag Lit(Vertex3 vertex, out Color color, Maps maps)\n{\n    color = maps.scene.$\n}\n", types);
    TIDE_CHECK(!offers(complete(program), "colors"));
    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    var all = maps.scene.colors;\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "read one at a time"));
    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene.colors[1] = maps.normals;\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "they can't be assigned"));
    snprintf(program, sizeof program, "%sview V(mut Maps maps)\n{\n    maps.scene = RenderTarget(8, 8, Format.Depth, Format.Rgba8);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "it's a target's only format"));

    static const char fill[] = "frag Fill(Vertex3 vertex, out Color albedo, out Color nor$mal)\n{\n"
                               "    albedo = vertex.color;\n    normal = Color(0, 0, 1);\n}\n";
    snprintf(program, sizeof program, "%s%s", types, fill);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "second output") && has(last_sent(), "target.colors[1]"));
    snprintf(program, sizeof program, "%sfrag Fill(Vertex3 vertex, out Color alb$edo, out Color normal)\n{\n"
                                      "    albedo = vertex.color;\n    normal = Color(0, 0, 1);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "The pixel's color") && !has(last_sent(), "second output"));
    snprintf(program, sizeof program, "%sfrag Fill(Vertex3 vertex, out Color albedo, out Color normal, o$ut Color far)\n{\n"
                                      "    albedo = vertex.color;\n    normal = albedo;\n    far = albedo;\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "third output") && has(last_sent(), "target.colors[2]"));
    snprintf(program, sizeof program, "%sfrag Fill(Vertex3 vertex, out Color albedo, $)\n{\n}\n", types);
    const char *more = complete(program);
    TIDE_CHECK(offers(more, "out") && has(more, "one more color the frag writes"));
    snprintf(program, sizeof program, "%sfrag Fill(Vertex3 vertex, out Color albedo, out $)\n{\n}\n", types);
    TIDE_CHECK(offers(complete(program), "Color"));
    static char unwritten[1024];
    snprintf(unwritten, sizeof unwritten, "%sfrag Fill(Vertex3 vertex, out Color albedo, out Color normal)\n{\n"
                                          "    albedo = vertex.color;\n}\n", types);
    open_document(unwritten);
    TIDE_CHECK(has(last_sent(), "without writing 'normal', its second output"));
    actions_at(2);
    const char *written = apply_fix(unwritten, "Write 'normal = Color.white;'");
    TIDE_REQUIRE(written != NULL);
    open_document(written);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    snprintf(program, sizeof program, "%sfrag Fill(Vertex3 vertex, out Color a, out Color b, out Color c, out Color d, out Color e)\n{\n"
                                      "    a = vertex.color;\n    b = a;\n    c = a;\n    d = a;\n    e = a;\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "a frag writes 4 colors at the most"));
}

// The window's one word, and formats named by what they store; the old spellings are a rename away.
TIDE_TEST(lsp_window_pixels_and_format_names)
{
    start();
    open_document("scene Main { }\nview V()\n{\n    var scene = RenderTarget.Screen(0.5);\n    scene.Dispose();\n}\n");
    TIDE_CHECK(has(last_sent(), "RenderTarget.Screen is 'RenderTarget.Window' now"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Window'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw.SetPixels();\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.SetPixels is 'Draw.SetScreen' now"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'SetScreen'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    Draw.Pixels()\n    {\n    }\n}\n");
    TIDE_CHECK(has(last_sent(), "Draw.Pixels is 'Draw.Screen' now"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Screen'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    var lamp = RenderTarget(64, 64, Format.DepthOnly);\n    lamp.Dispose();\n}\n");
    TIDE_CHECK(has(last_sent(), "Format.DepthOnly is 'Format.Depth' now"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Depth'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    var glow = RenderTarget(64, 64, Format.Float);\n    glow.Dispose();\n}\n");
    TIDE_CHECK(has(last_sent(), "Format.Float is 'Format.Rgba16f' now"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Rgba16f'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    var plain = RenderTarget(64, 64, Format.Color);\n    plain.Dispose();\n}\n");
    TIDE_CHECK(has(last_sent(), "Format.Color is 'Format.Rgba8' now"));
    TIDE_CHECK(has(actions_at(3), "\"title\":\"Change to 'Rgba8'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    open_document("scene Main { }\nview V()\n{\n    var scene = RenderTarget.Window(0.5, Format.Rgba8, Format.Rgba16f);\n"
                  "    scene.SetScreen();\n    scene.Screen()\n    {\n    }\n    Draw.SetScreen();\n    scene.Dispose();\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
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
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(count(sent[0], uri_b) == 1 && count(sent[1], uri_a) == 1);
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]") && has(sent[1], "\"diagnostics\":[]"));

    TIDE_CHECK(has(request_at(uri_b, "textDocument/definition", 1, 40, ""), uri_a));
    TIDE_CHECK(count(request_at(uri_b, "textDocument/references", 1, 40, "\"context\":{\"includeDeclaration\":true}"),
                     uri_a) >= 2);
    TIDE_CHECK(has(last_sent(), uri_b));

    TIDE_CHECK(has(request_at(uri_b, "textDocument/hover", 3, 8, ""), "Runs 2nd of 2 systems each tick, after `Physics.Gravity`"));

    const char *rename = request_at(uri_b, "textDocument/rename", 2, 17, "\"newName\":\"Fall\"");
    TIDE_CHECK(has(rename, uri_a) && has(rename, uri_b));
    TIDE_CHECK(count(rename, "\"newText\":\"Fall\"") == 2);

    rename = request_at(uri_b, "textDocument/rename", 0, 8, "\"newName\":\"World\"");
    TIDE_CHECK(has(rename, uri_a) && has(rename, uri_b));
    TIDE_CHECK(count(rename, "\"newText\":\"World\"") == 3);
    TIDE_CHECK(has(request_at(uri_b, "textDocument/rename", 0, 8, "\"newName\":\"Main\""), "'Main' is already declared"));

    open_uri(uri_b, "using Physics;\nevent(Spawned) Setup(with Main) { Spawn(Physics.); }\nscene Main { }\n");
    const char *completion = request_at(uri_b, "textDocument/completion", 1, 48, "");
    TIDE_CHECK(offers(completion, "Body"));
    TIDE_CHECK(!offers(completion, "Gravity"));

    open_uri(uri_b, main_file);
    open_uri(uri_a, "namespace Physics;\ncomponent Shape { float2 position; }\n");
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[1], uri_a) && has(sent[1], "\"diagnostics\":[]"));
    TIDE_CHECK(has(sent[0], uri_b) && has(sent[0], "unknown component or singleton 'Body'"));

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

// A game listed as a folder: every .tide file in it and its subfolders, even one the editor hasn't saved yet.
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
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[0], uri_main) && has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(sent[1], uri_physics) && has(sent[1], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request_at(uri_main, "textDocument/hover", 2, 8, ""), "Runs 1st of 2 systems each tick."));

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

// `with scene Arena` names the scene as a type does: renaming it renames it there, and it has the scene's hover.
TIDE_TEST(lsp_scene_filter_names_its_scene)
{
    start();
    open_document("scene Main { }\nscene $Arena { int round; }\ncomponent Spin { float speed; }\n"
                  "system Spinning(mut Spin spin, with scene Arena) { spin.speed += 1; }\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    const char *renamed = request_with("textDocument/rename", "\"newName\":\"Level\"");
    TIDE_CHECK(has(renamed, "{\"range\":{\"start\":{\"line\":1,\"character\":6},\"end\":{\"line\":1,\"character\":11}},\"newText\":\"Level\"}"));
    TIDE_CHECK(has(renamed, "{\"range\":{\"start\":{\"line\":3,\"character\":42},\"end\":{\"line\":3,\"character\":47}},\"newText\":\"Level\"}"));
    open_document("scene Main { }\nscene Arena { int round; }\ncomponent Spin { float speed; }\n"
                  "system Spinning(mut Spin spin, with scene $Arena) { spin.speed += 1; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Arena"));
    lsp_free(&server);
}

// A game in a Unity project's Assets (tide unity --assets): renames reach TideEntity lists and prefab instance type changes; a field's only in that component's values.
// The game's own components (com.tide.game's): a rename gives their objects the new name's script GUID, and a field's their key and a prefab instance's change of it, not another component's.
TIDE_TEST(lsp_rename_reaches_unity_assets)
{
    if (!find_game_dir()) return;
    make_folder("lsp_unity");
    make_folder("lsp_unity/ProjectSettings");
    make_folder("lsp_unity/Assets");
    make_folder("lsp_unity/Assets/Tide");
    write_file("lsp_unity/ProjectSettings/ProjectVersion.txt", "m_EditorVersion: 6000.0.0f1\n");
    static const char game[] = "component Health { int hp; int armor; Hold hold; List<Hold> holds; int Value; }\n"
                               "component Part { int hp; }\n"
                               "scene Main { }\n"
                               "struct Hold { int mass; }\n";
    write_file("lsp_unity/Assets/Tide/game.tide", game);
    write_file("lsp_unity/Assets/Thing.prefab",
               "%YAML 1.1\n"
               "--- !u!114 &1\n"
               "MonoBehaviour:\n"
               "  m_Script: {fileID: 11500000, guid: 4f7d2c1e9a8b4b6c8d3e2f1a0b9c8d7e, type: 3}\n"
               "  components:\n"
               "  - type: Health\n"            // line 5
               "    values: hp = 10, armor = 1\n" // line 6
               "  - type: Part\n"
               "    values: hp = 3\n"
               "--- !u!1001 &2\n"
               "PrefabInstance:\n"
               "  m_Modification:\n"
               "    m_Modifications:\n"
               "    - target: {fileID: 1, guid: 0123456789abcdef0123456789abcdef, type: 3}\n"
               "      propertyPath: components.Array.data[0].type\n"
               "      value: Health\n"); // line 15
    write_file("lsp_unity/Assets/Other.prefab", "--- !u!1 &1\nGameObject:\n  m_Name: Health\n  type: Health\n");
    write_file("lsp_unity/Assets/Own.prefab",
               "--- !u!114 &50\n"
               "MonoBehaviour:\n"
               "  m_Script: {fileID: 11500000, guid: 3c0696cc9d36fc389947e23ca18dad65, type: 3}\n" // line 2: Health's
               "  m_EditorClassIdentifier: Tide.Game::Tide.Game.Health\n"
               "  hp: 10\n" // line 4
               "  armor: 1\n"
               "  hold:\n"
               "    mass: 4\n" // line 7: a struct's field
               "  holds:\n"
               "  - mass: 1\n" // line 9: ...in a list's element
               "  - mass: 2\n"
               "  Value_: 3\n" // line 11: Value, a member's name, has _ after
               "--- !u!114 &51\n"
               "MonoBehaviour:\n"
               "  m_Script: {fileID: 11500000, guid: e20821468e5bb86e036f171ea41778ed, type: 3}\n" // Part's
               "  hp: 3\n");
    write_file("lsp_unity/Assets/Own.prefab.meta", "fileFormatVersion: 2\nguid: 11112222333344445555666677778888\n");
    write_file("lsp_unity/Assets/Holder.prefab",
               "--- !u!1001 &7\n"
               "PrefabInstance:\n"
               "  m_Modification:\n"
               "    m_Modifications:\n"
               "    - target: {fileID: 50, guid: 11112222333344445555666677778888, type: 3}\n"
               "      propertyPath: hp\n" // line 5: its Health's
               "      value: 4\n"
               "    - target: {fileID: 51, guid: 11112222333344445555666677778888, type: 3}\n"
               "      propertyPath: hp\n" // its Part's
               "      value: 5\n"
               "    - target: {fileID: 50, guid: 11112222333344445555666677778888, type: 3}\n"
               "      propertyPath: holds.Array.data[1].mass\n" // line 11: a struct's field, in a list's element
               "      value: 7\n"
               "  m_SourcePrefab: {fileID: 100100000, guid: 11112222333344445555666677778888, type: 3}\n");
    write_file("lsp_unity/Assets/Holder.prefab.meta", "fileFormatVersion: 2\nguid: 99992222333344445555666677778888\n");
    char root[700], uri[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_unity/Assets/Tide", dir);
    snprintf(uri, sizeof uri, "file:///%s/lsp_unity/Assets/Tide/game.tide", dir);
    start_in(root);
    open_uri(uri, game);
    const char *renamed = request_at(uri, "textDocument/rename", 0, 12, "\"newName\":\"Vitality\"");
    TIDE_CHECK(has(renamed, "Thing.prefab"));
    TIDE_CHECK(has(renamed, "{\"range\":{\"start\":{\"line\":5,\"character\":10},\"end\":{\"line\":5,\"character\":16}},\"newText\":\"Vitality\"}"));
    TIDE_CHECK(has(renamed, "{\"range\":{\"start\":{\"line\":15,\"character\":13},\"end\":{\"line\":15,\"character\":19}},\"newText\":\"Vitality\"}"));
    TIDE_CHECK(!has(renamed, "Other.prefab"));
    TIDE_CHECK(has(renamed, "{\"range\":{\"start\":{\"line\":2,\"character\":37},\"end\":{\"line\":2,\"character\":69}},\"newText\":\"d51eee06e4b36a1bd29798379cb8e68f\"}"));
    TIDE_CHECK(has(renamed, "{\"range\":{\"start\":{\"line\":3,\"character\":48},\"end\":{\"line\":3,\"character\":54}},\"newText\":\"Vitality\"}"));
    TIDE_CHECK(!has(renamed, "\"line\":8,"));
    const char *field = request_at(uri, "textDocument/rename", 0, 23, "\"newName\":\"health\"");
    TIDE_CHECK(has(field, "{\"range\":{\"start\":{\"line\":6,\"character\":12},\"end\":{\"line\":6,\"character\":14}},\"newText\":\"health\"}"));
    TIDE_CHECK(has(field, "{\"range\":{\"start\":{\"line\":4,\"character\":2},\"end\":{\"line\":4,\"character\":4}},\"newText\":\"health\"}"));
    TIDE_CHECK(has(field, "{\"range\":{\"start\":{\"line\":5,\"character\":20},\"end\":{\"line\":5,\"character\":22}},\"newText\":\"health\"}"));
    TIDE_CHECK(has(field, "Holder.prefab"));
    TIDE_CHECK(!has(field, "\"line\":8,") && !has(field, "\"line\":9,"));
    // A struct's field, wherever a component holds the struct: its keys, and an instance's change of one
    const char *mass = request_at(uri, "textDocument/rename", 3, 18, "\"newName\":\"weight\"");
    TIDE_CHECK(has(mass, "{\"range\":{\"start\":{\"line\":7,\"character\":4},\"end\":{\"line\":7,\"character\":8}},\"newText\":\"weight\"}"));
    TIDE_CHECK(has(mass, "{\"range\":{\"start\":{\"line\":9,\"character\":4},\"end\":{\"line\":9,\"character\":8}},\"newText\":\"weight\"}"));
    TIDE_CHECK(has(mass, "{\"range\":{\"start\":{\"line\":10,\"character\":4},\"end\":{\"line\":10,\"character\":8}},\"newText\":\"weight\"}"));
    TIDE_CHECK(has(mass, "{\"range\":{\"start\":{\"line\":11,\"character\":40},\"end\":{\"line\":11,\"character\":44}},\"newText\":\"weight\"}"));
    // A field named as the class's members are: its key with _ after, renamed to one without
    const int value_at = (int)(strstr(game, "int Value") + 4 - game);
    const char *count = request_at(uri, "textDocument/rename", 0, value_at, "\"newName\":\"count\"");
    TIDE_CHECK(has(count, "{\"range\":{\"start\":{\"line\":11,\"character\":2},\"end\":{\"line\":11,\"character\":8}},\"newText\":\"count\"}"));
    remove_game_file("lsp_unity/Assets/Tide/game.tide");
    remove_game_file("lsp_unity/Assets/Thing.prefab");
    remove_game_file("lsp_unity/Assets/Other.prefab");
    remove_game_file("lsp_unity/Assets/Own.prefab");
    remove_game_file("lsp_unity/Assets/Own.prefab.meta");
    remove_game_file("lsp_unity/Assets/Holder.prefab");
    remove_game_file("lsp_unity/Assets/Holder.prefab.meta");
    remove_game_file("lsp_unity/ProjectSettings/ProjectVersion.txt");
    remove_folder("lsp_unity/Assets/Tide");
    remove_folder("lsp_unity/Assets");
    remove_folder("lsp_unity/ProjectSettings");
    remove_folder("lsp_unity");
    clear_sent();
    lsp_free(&server);
}

// A Unity project open in the editor: its game is every .tide file in Assets (not in a folder Unity leaves out), with what tide unity wrote in Library/Tide.
TIDE_TEST(lsp_unity_game_is_its_assets_with_what_tide_unity_wrote)
{
    if (!find_game_dir()) return;
    make_folder("lsp_unity_game");
    make_folder("lsp_unity_game/ProjectSettings");
    make_folder("lsp_unity_game/Assets");
    make_folder("lsp_unity_game/Assets/Tide");
    make_folder("lsp_unity_game/Assets/Rules");
    make_folder("lsp_unity_game/Assets/Samples~");
    make_folder("lsp_unity_game/Library");
    make_folder("lsp_unity_game/Library/Tide");
    make_folder("lsp_unity_game/Library/Tide/prefabs");
    make_folder("lsp_unity_game/Library/Tide/prefabs/Assets");
    write_file("lsp_unity_game/ProjectSettings/ProjectVersion.txt", "m_EditorVersion: 6000.6.0f1\n");
    static const char game[] = "using UnityEngine.Prefabs;\n"
                               "scene Main { }\n"
                               "event(Spawned) Begin(with Main) { Spawn(Roller); Spawn(Big); }\n"
                               "system Go(mut Speed speed) { speed.value += 1; }\n";
    // A prefab of 31 entities, a line each: its hover shows the first lines and its closing brace
    char big[4096];
    int written = snprintf(big, sizeof big, "namespace UnityEngine.Prefabs;\n\nprefab Big\n{\n    Speed { value = 0 };\n");
    for (int i = 1; i <= 30; i++) written += snprintf(big + written, sizeof big - (size_t)written, "    entity C%d { Speed { value = %d }; }\n", i, i);
    snprintf(big + written, sizeof big - (size_t)written, "}\n");
    write_file("lsp_unity_game/Assets/Tide/game.tide", game);
    write_file("lsp_unity_game/Assets/Rules/rules.tide", "component Speed { float value; }\n");
    write_file("lsp_unity_game/Assets/Samples~/broken.tide", "this isn't Tide\n");
    write_file("lsp_unity_game/Library/Tide/Unity.tide", "namespace UnityEngine;\ncomponent Source { int asset; int node; }\n");
    write_file("lsp_unity_game/Library/Tide/prefabs/Assets/Roller.prefab.tprefab",
               "namespace UnityEngine.Prefabs;\n\nprefab Roller\n{\n    UnityEngine.Source { asset = 0, node = 0 };\n"
               "    Speed { value = 1 };\n}\n");
    write_file("lsp_unity_game/Library/Tide/prefabs/Assets/Big.prefab.tprefab", big);
    char root[700], uri[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_unity_game", dir);
    snprintf(uri, sizeof uri, "file:///%s/lsp_unity_game/Assets/Tide/game.tide", dir);
    start_in(root);
    open_uri(uri, game);
    bool seen = false;
    for (int i = 0; i < sent_count; i++) {
        TIDE_CHECK(has(sent[i], "\"diagnostics\":[]"));
        TIDE_CHECK(!has(sent[i], "broken.tide"));
        seen = seen || has(sent[i], uri);
    }
    TIDE_CHECK(seen);
    // A prefab's hover is the prefab as tide read it, and the asset it's of
    const char *hover = request_at(uri, "textDocument/hover", 2, 41, "");
    TIDE_CHECK(has(hover, "prefab Roller"));
    TIDE_CHECK(has(hover, "UnityEngine.Source { asset = 0, node = 0 };"));
    TIDE_CHECK(has(hover, "Speed { value = 1 };"));
    TIDE_CHECK(has(hover, "From `Assets/Roller.prefab` in the Unity project"));
    hover = request_at(uri, "textDocument/hover", 2, 56, "");
    TIDE_CHECK(has(hover, "prefab Big"));
    TIDE_CHECK(has(hover, "entity C19 { Speed { value = 19 }; }"));
    TIDE_CHECK(!has(hover, "entity C20 "));
    TIDE_CHECK(!has(hover, "entity C30 "));
    TIDE_CHECK(has(hover, "// ...11 more lines"));
    TIDE_CHECK(has(hover, "From `Assets/Big.prefab`"));

    remove_game_file("lsp_unity_game/Library/Tide/prefabs/Assets/Big.prefab.tprefab");
    remove_game_file("lsp_unity_game/Library/Tide/prefabs/Assets/Roller.prefab.tprefab");
    start_in(root);
    open_uri(uri, game);
    bool unknown = false;
    for (int i = 0; i < sent_count; i++) unknown = unknown || (has(sent[i], uri) && has(sent[i], "Roller"));
    TIDE_CHECK(unknown);

    remove_game_file("lsp_unity_game/Library/Tide/Unity.tide");
    remove_game_file("lsp_unity_game/Assets/Samples~/broken.tide");
    remove_game_file("lsp_unity_game/Assets/Rules/rules.tide");
    remove_game_file("lsp_unity_game/Assets/Tide/game.tide");
    remove_game_file("lsp_unity_game/ProjectSettings/ProjectVersion.txt");
    remove_folder("lsp_unity_game/Library/Tide/prefabs/Assets");
    remove_folder("lsp_unity_game/Library/Tide/prefabs");
    remove_folder("lsp_unity_game/Library/Tide");
    remove_folder("lsp_unity_game/Library");
    remove_folder("lsp_unity_game/Assets/Samples~");
    remove_folder("lsp_unity_game/Assets/Rules");
    remove_folder("lsp_unity_game/Assets/Tide");
    remove_folder("lsp_unity_game/Assets");
    remove_folder("lsp_unity_game/ProjectSettings");
    remove_folder("lsp_unity_game");
    clear_sent();
    lsp_free(&server);
}

// A folder open in the editor is a game, as `tide run` builds it: every .tide file in it and its subfolders, with no manifest.
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

static void notify_file(const char *uri, int type);

TIDE_TEST(lsp_prefab_project_and_refactors)
{
    if (!find_game_dir()) return;
    make_folder("lsp_prefab");
    static const char main_file[] = "namespace Game;\ncomponent Health { int value = 7; }\nscene Main { }\n"
                                    "event(Spawned) Setup(with Main) { Spawn(Enemy); }\n";
    static const char prefab_file[] = "namespace Game;\nprefab Enemy\n{\n    Health { value = 3 };\n    entity Child : Part { }\n}\n"
                                      "prefab Part { Transform; }\n";
    write_file("lsp_prefab/main.tide", main_file);
    write_file("lsp_prefab/Enemy.tprefab", prefab_file);
    char root[700], uri_main[700], uri_prefab[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_prefab", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_prefab/main.tide", dir);
    snprintf(uri_prefab, sizeof uri_prefab, "file:///%s/lsp_prefab/Enemy.tprefab", dir);
    start_in(root);
    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]") && has(sent[1], "\"diagnostics\":[]"));
    const char *rename = request_at(uri_main, "textDocument/rename", 1, 12, "\"newName\":\"Vitals\"");
    TIDE_CHECK(has(rename, uri_prefab) && has(rename, uri_main) && has(rename, "Vitals"));
    rename = request_at(uri_main, "textDocument/rename", 1, 24, "\"newName\":\"amount\"");
    TIDE_CHECK(has(rename, uri_prefab) && has(rename, "amount"));
    open_uri(uri_prefab, prefab_file);
    rename = request_at(uri_prefab, "textDocument/rename", 1, 8, "\"newName\":\"Foe\"");
    TIDE_CHECK(has(rename, uri_main) && has(rename, "Foe"));
    rename = request_at(uri_prefab, "textDocument/rename", 6, 8, "\"newName\":\"Branch\"");
    TIDE_CHECK(has(rename, "\"line\":4") && has(rename, "Branch"));
    write_file("lsp_prefab/Enemy.tprefab", "namespace Game;\nprefab Enemy { Removed; }\n");
    close_uri(uri_prefab);
    notify_file(uri_prefab, 2);
    bool prefab_diagnosed = false;
    for (int i = 0; i < sent_count; i++) prefab_diagnosed |= has(sent[i], uri_prefab) && has(sent[i], "a prefab entry must name an ordinary component");
    TIDE_CHECK(prefab_diagnosed);
    remove_game_file("lsp_prefab/main.tide");
    remove_game_file("lsp_prefab/Enemy.tprefab");
    remove_folder("lsp_prefab");
    clear_sent();
    lsp_free(&server);
}

// A package in a folder: analyzed with the game's files in its namespace, or alone with no Main; a folder with its own tide.packages isn't the game's.
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
    write_file("lsp_pk/game/other/loose.tide", "struct Pair { float a; }\n");
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
    TIDE_CHECK(has(sent[0], uri_body) && has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(sent[1], uri_main) && has(sent[1], "\"diagnostics\":[]"));
    open_uri(uri_body, body);
    TIDE_CHECK(sent_count == 2 && has(sent[1], uri_main));

    open_uri(uri_loose, "struct Pair { float a; }\n");
    TIDE_CHECK(has(sent[1], uri_loose) && has(sent[1], "what package Physics declares goes in its namespace"));
    close_uri(uri_loose);

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

// A package that comes with tide: its files, in tide's installation, are analyzed with the game's.
TIDE_TEST(lsp_open_folder_with_a_package_that_comes_with_tide)
{
    if (!find_game_dir()) return;
    make_folder("lsp_bi");
    make_folder("lsp_bi/game");
    make_folder("lsp_bi/install");
    make_folder("lsp_bi/install/src");
    make_folder("lsp_bi/install/src/packages");
    make_folder("lsp_bi/install/src/packages/dice");
    static const char die[] = "namespace Dice;\n"
                              "component Die { int face; }\n"
                              "system Roll(mut Die die) { die.face += 1; }\n";
    static const char main_file[] = "using Dice;\n"
                                    "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Die); }\n";
    write_file("lsp_bi/game/tide.packages", "dice\n");
    write_file("lsp_bi/game/main.tide", main_file);
    write_file("lsp_bi/install/src/packages/dice/tide.packages", "package Dice\n");
    write_file("lsp_bi/install/src/packages/dice/die.tide", die);
    char builtin[700], root[700], uri_main[700], uri_die[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    game_path(builtin, sizeof builtin, "lsp_bi/install/src/packages");
    snprintf(root, sizeof root, "file:///%s/lsp_bi/game", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_bi/game/main.tide", dir);
    snprintf(uri_die, sizeof uri_die, "file:///%s/lsp_bi/install/src/packages/dice/die.tide", dir);

    start_in(root);
    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 1 && has(sent[0], uri_main) && !has(sent[0], "\"diagnostics\":[]"));

    packages_set_builtin_folder(builtin);
    start_in(root);
    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[0], uri_die) && has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(sent[1], uri_main) && has(sent[1], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request_at(uri_main, "textDocument/definition", 2, 41, ""), uri_die));
    open_uri(uri_die, die);
    TIDE_CHECK(sent_count == 2 && has(sent[1], uri_main));

    packages_set_builtin_folder(NULL);
    remove_game_file("lsp_bi/game/tide.packages");
    remove_game_file("lsp_bi/game/main.tide");
    remove_game_file("lsp_bi/install/src/packages/dice/tide.packages");
    remove_game_file("lsp_bi/install/src/packages/dice/die.tide");
    remove_folder("lsp_bi/install/src/packages/dice");
    remove_folder("lsp_bi/install/src/packages");
    remove_folder("lsp_bi/install/src");
    remove_folder("lsp_bi/install");
    remove_folder("lsp_bi/game");
    remove_folder("lsp_bi");
    clear_sent();
    lsp_free(&server);
}

// An open folder built with CMake uses the manifest tide_add_game writes in build/tools; its other files stand alone.
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

// Files changed on disk, as the watchers report them or on save: the open documents' games are analysed again.
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
    TIDE_CHECK(has(sent[0], "/elsewhere/build/tools\",\"pattern\":\"games.txt\"}"));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":\"tidels-watch\",\"result\":null}");
    TIDE_CHECK(sent_count == 0);

    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 2 && has(sent[sent_for(uri_main)], "\"diagnostics\":[]"));

    write_file("lsp_watch/physics.tide", "component Hull { float2 position; }\n");
    clear_sent();
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\",\"params\":{\"changes\":"
             "[{\"uri\":\"%s\",\"type\":2}]}}",
             uri_physics);
    handle(message);
    TIDE_CHECK(sent_for(uri_main) >= 0 && has(sent[sent_for(uri_main)], "unknown name 'Body'"));

    write_file("lsp_watch/new.tide", physics);
    clear_sent();
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\",\"params\":{\"changes\":"
             "[{\"uri\":\"%s\",\"type\":1}]}}",
             uri_new);
    handle(message);
    TIDE_CHECK(sent_count == 3 && has(sent[sent_for(uri_main)], "\"diagnostics\":[]"));

    write_file("lsp_watch/physics.tide", "component Body { float2 position; }\n");
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{\"textDocument\":{\"uri\":\"file:///none\"}}}");
    TIDE_CHECK(has(sent[sent_for(uri_physics)], "\"severity\":1"));
    remove_game_file("lsp_watch/physics.tide");
    clear_sent();
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\",\"params\":{\"changes\":"
             "[{\"uri\":\"%s\",\"type\":3}]}}",
             uri_physics);
    handle(message);
    TIDE_CHECK(sent_for(uri_physics) >= 0 && has(sent[sent_for(uri_physics)], "\"diagnostics\":[]"));
    TIDE_CHECK(has(sent[sent_for(uri_main)], "\"diagnostics\":[]"));

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
    TIDE_CHECK(sent_count == 1 && !has(sent[0], "\"diagnostics\":[]"));
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
    TIDE_CHECK(count(last_sent(), "\"name\":\"Main\"") == 0);

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
                          "\"newText\":\"using Physics;\\n\"}"));

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
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"\"}}");
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"code\":-32600"));
    static const char after[] = "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"\"}}";
    static const char cancel[] = "{\"jsonrpc\":\"2.0\",\"method\":\"$/cancelRequest\",\"params\":{\"id\":10}}";
    const lsp_message later = {cancel, sizeof cancel - 1};
    clear_sent();
    lsp_handle_ahead(&server, after, sizeof after - 1, &later, 1);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"code\":-32600"));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":"
           "{\"uri\":\"file:///shutdown.tide\",\"text\":\"scene Main { }\",\"version\":1}}}");
    TIDE_CHECK(server.doc_count == 0 && sent_count == 0);
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}");
    TIDE_CHECK(server.exited && server.exit_code == 0);
    clear_sent();
    lsp_free(&server);
}

TIDE_TEST(lsp_positions_outside_the_document_are_safe)
{
    start();
    open_document("const int Value = 1;\nscene Main { }\n");
    static const int lines[] = {-1, 3, 2147483647};
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", lines[i], 2147483647, ""), "\"result\":null"));
    }
    clear_sent();
    lsp_free(&server);
}

static void check_reference_lenses(const char *uri);

// Check the full navigation contract, including the root declaration ranges.
static void check_entry_lens(const char *uri, const int line, const char *root_uri, const int *lines, const int *columns,
                             const int *lengths, const int count_expected)
{
    const char *reply = request_at(uri, "textDocument/codeLens", 0, 0, "");
    const json *lenses = json_get(json_parse(reply, strlen(reply)), "result");
    TIDE_REQUIRE(lenses && lenses->kind == JSON_ARRAY);
    int found = 0;
    for (int i = 0; i < lenses->count; i++) {
        const json *lens = lenses->items[i];
        const json *start_at = json_get(json_get(lens, "range"), "start");
        const json *command = json_get(lens, "command");
        const char *title = json_str(json_get(command, "title"));
        if (json_int(json_get(start_at, "line"), -1) != line || !title || !strstr(title, " entry point")) continue;
        found++;
        char expected[40];
        snprintf(expected, sizeof expected, "%d entry point%s", count_expected, count_expected == 1 ? "" : "s");
        TIDE_CHECK(strcmp(title, expected) == 0);
        const char *name = json_str(json_get(command, "command"));
        TIDE_REQUIRE(name);
        if (!count_expected) {
            TIDE_CHECK(strcmp(name, "") == 0);
            continue;
        }
        TIDE_CHECK(strcmp(name, "editor.action.goToLocations") == 0);
        const json *args = json_get(command, "arguments");
        TIDE_REQUIRE(args && args->kind == JSON_ARRAY && args->count == 4);
        TIDE_CHECK(strcmp(json_str(args->items[0]), uri) == 0);
        TIDE_CHECK(json_int(json_get(args->items[1], "line"), -1) == line);
        TIDE_CHECK(strcmp(json_str(args->items[3]), count_expected == 1 ? "goto" : "peek") == 0);
        const json *locations = args->items[2];
        TIDE_REQUIRE(locations->kind == JSON_ARRAY && locations->count == count_expected);
        for (int k = 0; k < count_expected; k++) {
            const json *location = locations->items[k];
            const json *range = json_get(location, "range");
            const json *from = json_get(range, "start"), *to = json_get(range, "end");
            TIDE_CHECK(strcmp(json_str(json_get(location, "uri")), root_uri) == 0);
            TIDE_CHECK(json_int(json_get(from, "line"), -1) == lines[k]);
            TIDE_CHECK(json_int(json_get(to, "line"), -1) == lines[k]);
            TIDE_CHECK(json_int(json_get(from, "character"), -1) == columns[k]);
            TIDE_CHECK(json_int(json_get(to, "character"), -1) == columns[k] + lengths[k]);
        }
    }
    TIDE_CHECK(found == 1);
    json_release();
}

TIDE_TEST(lsp_entry_points_follow_helpers_and_stop_at_handlers)
{
    start();
    open_document("extern void Touch();\n"
                  "void Leaf() { Touch(); }\n"
                  "void Left() { Leaf(); }\n"
                  "void Right() { Leaf(); }\n"
                  "void Unused() { Leaf(); }\n"
                  "event Ping { }\n"
                  "command Move { }\n"
                  "scene Main { }\n"
                  "system Tick() { Left(); Right(); Leaf(); }\n"
                  "view Show() { Left(); }\n"
                  "event(Ping) OnPing() { Right(); }\n"
                  "command(Move move, PlayerID sender) OnMove() { Leaf(); }\n"
                  "system Emit() { Send(Ping); }\n"
                  "event(Spawned) Setup(with Main) { Leaf(); }\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    static const int lines[] = {8, 9, 10, 11, 13}, columns[] = {7, 5, 12, 36, 15}, lengths[] = {4, 4, 6, 6, 5};
    check_entry_lens("file:///test.tide", 1, "file:///test.tide", lines, columns, lengths, 5);
    check_entry_lens("file:///test.tide", 0, "file:///test.tide", lines, columns, lengths, 5);
    check_entry_lens("file:///test.tide", 4, NULL, NULL, NULL, NULL, 0);
    check_repeat_lenses("file:///test.tide");
    check_reference_lenses("file:///test.tide");
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 1, 6, ""), "void Leaf()"));
}

TIDE_TEST(lsp_entry_points_handle_recursion_methods_and_overloads)
{
    start();
    open_document("void CycleA() { CycleB(); }\n"
                  "void CycleB() { CycleA(); }\n"
                  "void Leaf(int n) { CycleA(); }\n"
                  "void Leaf(float n) { }\n"
                  "struct Helper { void Run() { Leaf(1); } }\n"
                  "scene Main { }\n"
                  "system Tick() { var h = Helper {}; h.Run(); CycleB(); }\n"
                  "view Show() { Leaf(1.0); }\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    static const int lines[] = {6}, columns[] = {7}, lengths[] = {4};
    for (int i = 0; i < 3; i++) check_entry_lens("file:///test.tide", i, "file:///test.tide", lines, columns, lengths, 1);
    check_entry_lens("file:///test.tide", 4, "file:///test.tide", lines, columns, lengths, 1);
    check_entry_lens("file:///test.tide", 3, "file:///test.tide", (int[]){7}, (int[]){5}, (int[]){4}, 1);
    check_repeat_lenses("file:///test.tide");
    open_document("void CycleA() { CycleB(); }\nvoid CycleB() { CycleA(); }\nscene Main { }\n");
    check_entry_lens("file:///test.tide", 0, NULL, NULL, NULL, NULL, 0);
    check_entry_lens("file:///test.tide", 1, NULL, NULL, NULL, NULL, 0);
}

TIDE_TEST(lsp_entry_points_distinguish_input_hooks)
{
    start();
    open_document("float Value() { return 1; }\n"
                  "input Keys {\n"
                  "    float value;\n"
                  "    Sample() { value = Value(); }\n"
                  "    Sanitize() { value = Value(); }\n"
                  "    Predict() { value = Value(); }\n"
                  "}\n"
                  "scene Main { }\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    check_entry_lens("file:///test.tide", 0, "file:///test.tide", (int[]){3, 4, 5}, (int[]){4, 4, 4}, (int[]){6, 8, 7}, 3);
    check_repeat_lenses("file:///test.tide");
}

TIDE_TEST(lsp_entry_points_follow_local_methods_and_engine_callbacks)
{
    start();
    open_document("component Marker { LocalEntity Self() { return this; } }\n"
                  "scene Main { }\n"
                  "view Setup() { local Spawn(Marker); }\n"
                  "view Show(local Marker marker) { _ = marker.Self(); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    check_entry_lens("file:///test.tide", 0, "file:///test.tide", (int[]){3}, (int[]){5}, (int[]){4}, 1);
    open_document("float Value() { return 1; }\n"
                  "struct Angle { float degrees;\n"
                  "    Angle Interpolate(Angle from, Angle to, float t) { return Angle { degrees = Value() }; }\n"
                  "}\n"
                  "audio Tone(mut Samples samples) { samples.values[0] = Value(); }\n"
                  "scene Main { }\n");
    TIDE_CHECK(!has(last_sent(), "\"severity\":1"));
    check_entry_lens("file:///test.tide", 0, "file:///test.tide", (int[]){2, 4}, (int[]){10, 6}, (int[]){11, 4}, 2);
    check_repeat_lenses("file:///test.tide");
}

TIDE_TEST(lsp_entry_points_follow_shader_calls_and_refresh_unsaved_edits)
{
    start();
    open_document(SHADERS);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    check_entry_lens("file:///test.tide", 27, "file:///test.tide", (int[]){41}, (int[]){5}, (int[]){7}, 1);
    // Adding/removing the view in the unsaved buffer changes the same helper's roots.
    open_document(SHADER_CODE);
    check_entry_lens("file:///test.tide", 27, NULL, NULL, NULL, NULL, 0);
    open_document(SHADERS);
    check_entry_lens("file:///test.tide", 27, "file:///test.tide", (int[]){41}, (int[]){5}, (int[]){7}, 1);
    check_repeat_lenses("file:///test.tide");
}

// Above each system: its stage and why it waits; access it doesn't use gets a warning and a quick fix.
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
    TIDE_CHECK(has(lenses, "\"title\":\"after Move: both write Body\",\"command\":\"editor.action.goToLocations\","
                           "\"arguments\":[\"file:///test.tide\",{\"line\":4,\"character\":7},[{\"uri\":\"file:///test.tide\","
                           "\"range\":{\"start\":{\"line\":3,\"character\":7},\"end\":{\"line\":3,\"character\":11}}}],\"goto\"]"));
    TIDE_CHECK(has(lenses, "{\"range\":{\"start\":{\"line\":0,\"character\":10},\"end\":{\"line\":0,\"character\":14}},"
                           "\"command\":{\"title\":\"4 references\",\"command\":\"editor.action.showReferences\","
                           "\"arguments\":[\"file:///test.tide\",{\"line\":0,\"character\":10},[{\"uri\":\"file:///test.tide\","
                           "\"range\":{\"start\":{\"line\":2,\"character\":40},\"end\":{\"line\":2,\"character\":44}}},"));
    TIDE_CHECK(has(lenses, "\"title\":\"2 references\"") && has(lenses, "\"title\":\"1 reference\""));
    TIDE_CHECK(count(lenses, " reference") == 3);
    check_repeat_lenses("file:///test.tide");
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 0, 11,
                                "\"context\":{\"includeDeclaration\":false}"), "\"uri\"") == 4);

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

// Every reference lens must list exactly what find-usages returns, in the same order. Copy expectations before requests release the JSON tree.
static void check_reference_lenses(const char *uri)
{
    typedef struct lens_check {
        int line, character, count;
        char *locations;
    } lens_check;
    lens_check *checks = NULL;
    int check_count = 0;
    const char *reply = request_at(uri, "textDocument/codeLens", 0, 0, "");
    const json *lenses = json_get(json_parse(reply, strlen(reply)), "result");
    TIDE_REQUIRE(lenses && lenses->kind == JSON_ARRAY);
    for (int i = 0; i < lenses->count; i++) {
        const json *command = json_get(lenses->items[i], "command");
        const char *name = json_str(json_get(command, "command"));
        if (!name || strcmp(name, "editor.action.showReferences") != 0) continue;
        const json *args = json_get(command, "arguments");
        TIDE_REQUIRE(args && args->kind == JSON_ARRAY && args->count == 3);
        checks = realloc(checks, sizeof(*checks) * (size_t)(check_count + 1));
        if (!checks) abort();
        jbuf locations = {0};
        jb_json(&locations, args->items[2]);
        checks[check_count++] = (lens_check){json_int(json_get(args->items[1], "line"), -1),
                                           json_int(json_get(args->items[1], "character"), -1),
                                           args->items[2]->count, locations.data};
        char title[64];
        snprintf(title, sizeof title, "%d reference%s", args->items[2]->count, args->items[2]->count == 1 ? "" : "s");
        TIDE_CHECK(strcmp(json_str(json_get(command, "title")), title) == 0);
    }
    json_release();
    for (int i = 0; i < check_count; i++) {
        const char *references = request_at(uri, "textDocument/references", checks[i].line, checks[i].character,
                                            "\"context\":{\"includeDeclaration\":false}");
        const json *result = json_get(json_parse(references, strlen(references)), "result");
        TIDE_CHECK(result && result->kind == JSON_ARRAY && result->count == checks[i].count);
        jbuf actual = {0};
        jb_json(&actual, result);
        TIDE_CHECK(strcmp(actual.data, checks[i].locations) == 0);
        jb_free(&actual);
        free(checks[i].locations);
        json_release();
    }
    free(checks);
}

TIDE_TEST(lsp_completion_keeps_complete_member_chains)
{
    start();
    jbuf ns = {0}, source = {0};
    for (int i = 0; i < 40; i++) jb_printf(&ns, "%sN%d", i ? "." : "", i);
    static const char before[] = "view Show() { Debug.Log(\"\" + ";
    jb_printf(&source, "namespace %s { const int Marker = 1; }\nscene Main { }\n%s%s.Marker); }\n", ns.data, before, ns.data);
    open_uri("file:///test.tide", source.data);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *completion = request_at("file:///test.tide", "textDocument/completion", 2, (int)(strlen(before) + ns.len + 1), "");
    TIDE_CHECK(offers(completion, "Marker"));
    check_repeat_at("file:///test.tide", "textDocument/completion", 2, (int)(strlen(before) + ns.len + 1), "");
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 2, (int)(strlen(before) + ns.len + 1), ""), "const int Marker"));
    jb_free(&ns);
    jb_free(&source);

    jb_put(&source, "struct Link39 { int leaf; }\n");
    for (int i = 38; i >= 0; i--) jb_printf(&source, "struct Link%d { Link%d child; }\n", i, i + 1);
    jb_put(&source, "singleton Root { Link0 tree; }\nscene Main { }\n");
    static const char value_before[] = "view Show(Root root) { Debug.Log(\"\" + ";
    jb_put(&ns, "root.tree");
    for (int i = 0; i < 39; i++) jb_put(&ns, ".child");
    jb_printf(&source, "%s%s.leaf); }\n", value_before, ns.data);
    open_uri("file:///test.tide", source.data);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    completion = request_at("file:///test.tide", "textDocument/completion", 42, (int)(strlen(value_before) + ns.len + 1), "");
    TIDE_CHECK(offers(completion, "leaf") && !offers(completion, "child") && !offers(completion, "tree"));
    check_repeat_at("file:///test.tide", "textDocument/completion", 42, (int)(strlen(value_before) + ns.len + 1), "");
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 42, (int)(strlen(value_before) + ns.len + 1), ""), "int leaf"));
    jb_free(&ns);
    jb_free(&source);
}

TIDE_TEST(lsp_asset_type_completion)
{
    start();
    static const char *const declarations[] = {
        "asset $", "asset Tex$", "asset Anim$", "asset Ski$", "asset\n    $",
        "[LOD] asset Me$", "namespace Art { asset Tex$ }",
    };
    static const char *const types[] = {
        "string", "Texture", "Clip", "Grid2", "Mesh", "SkinnedMesh", "AnimationCatalog", "Animation", "Cubemap", "List", "Map",
    };
    for (size_t i = 0; i < sizeof declarations / sizeof declarations[0]; i++) {
        open_document(declarations[i]);
        const char *reply = request("textDocument/completion");
        for (size_t k = 0; k < sizeof types / sizeof types[0]; k++) TIDE_CHECK(offers(reply, types[k]));
        TIDE_CHECK(!offers(reply, "int") && !offers(reply, "Pose") && !offers(reply, "Set") && !offers(reply, "component"));
        TIDE_CHECK(has(reply, "\"insertText\":\"Grid2<Color>\""));
        TIDE_CHECK(has(reply, "\"insertText\":\"Mesh<${1:Vertex3}>\""));
        TIDE_CHECK(has(reply, "\"insertText\":\"Map<string, ${1:Texture}>\""));
    }
    static const char *const collections[] = {"asset List<$", "asset List<Tex$", "asset Map<string, $", "asset Map<string, Anim$"};
    for (size_t i = 0; i < sizeof collections / sizeof collections[0]; i++) {
        open_document(collections[i]);
        const char *reply = request("textDocument/completion");
        TIDE_CHECK(offers(reply, "Texture") && offers(reply, "Mesh") && offers(reply, "SkinnedMesh") && offers(reply, "AnimationCatalog"));
        TIDE_CHECK(!offers(reply, "Animation") && !offers(reply, "Cubemap") && !offers(reply, "Map"));
        TIDE_CHECK(has(reply, "\"insertText\":\"List<byte>\""));
        TIDE_CHECK(offers(reply, "byte") == (i < 2));
    }
    open_document("asset Map<str$");
    const char *reply = request("textDocument/completion");
    TIDE_CHECK(offers(reply, "string") && count(reply, "\"label\"") == 1);
    static const char *const pixels[] = {"asset Grid2<Col$", "asset List<Grid2<$", "asset Map<string, Grid2<$"};
    for (size_t i = 0; i < sizeof pixels / sizeof pixels[0]; i++) {
        open_document(pixels[i]);
        reply = request("textDocument/completion");
        TIDE_CHECK(offers(reply, "Color") && count(reply, "\"label\"") == 1);
    }
    open_document("asset Map<string, List<by$");
    reply = request("textDocument/completion");
    TIDE_CHECK(offers(reply, "byte") && count(reply, "\"label\"") == 1);
    static const char *const meshes[] = {"Mesh<$", "SkinnedMesh<Cor$", "List<Mesh<$", "Map<string, SkinnedMesh<$", "SkinnedMesh<Vertex3, $"};
    for (size_t i = 0; i < sizeof meshes / sizeof meshes[0]; i++) {
        char source[512];
        snprintf(source, sizeof source, "struct Corner { float3 position; }\nstruct Rig { Animation idle; }\nenum Choice { One }\nasset %s", meshes[i]);
        open_document(source);
        reply = request("textDocument/completion");
        TIDE_CHECK(offers(reply, "Corner") && offers(reply, "Rig") && offers(reply, "Vertex3"));
        TIDE_CHECK(!offers(reply, "Choice") && !offers(reply, "Texture") && !offers(reply, "Map"));
    }
    open_document("asset Texture logo $;");
    TIDE_CHECK(!offers(request("textDocument/completion"), "Texture"));
    open_document("asset Texture logo = $;");
    TIDE_CHECK(!offers(request("textDocument/completion"), "Texture"));
    open_document("asset Texture logo = missing;\n$");
    reply = request("textDocument/completion");
    TIDE_CHECK(offers(reply, "component") && !offers(reply, "Texture"));
}

TIDE_TEST(lsp_completion_keeps_deep_block_context)
{
    start();
    jbuf source = {0};
    jb_put(&source, "int Read(int seed)\n{\n    mut int kept = seed;\n");
    for (int i = 0; i < 70; i++) jb_put(&source, "    if (seed > 0) {\n");
    for (int i = 0; i < 65; i++) jb_put(&source, "    }\n");
    jb_put(&source, "    $kept += seed;\n");
    for (int i = 0; i < 5; i++) jb_put(&source, "    }\n");
    jb_put(&source, "    return kept;\n}\nscene Main { }\n");
    open_document(source.data);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *completion = request("textDocument/completion");
    TIDE_CHECK(offers(completion, "kept") && offers(completion, "seed") && offers(completion, "return"));
    TIDE_CHECK(!offers(completion, "component") && !offers(completion, "system"));
    check_repeat_at("file:///test.tide", "textDocument/completion", cursor_line, cursor_character, "");
    TIDE_CHECK(has(request("textDocument/hover"), "mut int kept"));
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 3);
    jb_free(&source);
}

TIDE_TEST(lsp_reference_lenses_keep_symbol_groups_and_order)
{
    start();
    open_document("component Body { int left; int right; }\n"
                  "namespace Catalog { const int Low = 2; }\nnamespace Catalog { const int High = 4; }\n"
                  "struct Part\n{\n    int Read() { return 1; }\n    int Read(int value) { return value; }\n}\n"
                  "const int CAP = 5;\nconst int UNUSED = 0;\n"
                  "int Pick(int value) { return value; }\nfloat Pick(float value) { return value; }\n"
                  "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\nsystem Tick(mut Body body)\n{\n"
                  "    body.left = Pick(CAP) + Catalog.Low;\n    body.right = Pick(7) + Catalog.High;\n    var part = Part { };\n"
                  "    body.left += part.Read();\n    body.right += part.Read(2);\n"
                  "    var result = Pick(0.5);\n    Debug.Log(\"\" + result);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    check_reference_lenses("file:///test.tide");

    jbuf source = {0};
    for (int i = 0; i < 1200; i++) jb_printf(&source, "const int Value%d = %d;\n", i, i);
    jb_put(&source, "singleton Total { int value; }\nscene Main { }\n"
                    "system Tick(mut Total total) { total.value = Value0 + Value600 + Value1199 + Value600; }\n");
    open_uri("file:///test.tide", source.data);
    jb_free(&source);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *lenses = request_at("file:///test.tide", "textDocument/codeLens", 0, 0, "");
    TIDE_CHECK(count(lenses, "\"title\":\"0 references\"") == 1197);
    TIDE_CHECK(count(lenses, "\"title\":\"1 reference\"") == 3);
    TIDE_CHECK(count(lenses, "\"title\":\"2 references\"") == 1);
    check_reference_lenses("file:///test.tide");
}

#define WORLD_GAME                                                                                      \
    "component Health { int value; }\n"                                                                 \
    "component Healer { Entity patient; }\n"                                                            \
    "singleton Score { int total; }\n"                                                                  \
    "scene Main { }\n"                                                                                  \
    "event(Spawned) Setup(with Main) { var a = Spawn(Health); Spawn(Healer { patient = a }); }\n"

// The World: any entity's component through world.Get<Health>(e), Has and Set; hovers and lenses name the components.
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
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 8, 28, ""), "component Health"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 8, 28, ""), "\"start\":{\"line\":0,\"character\":10}"));
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":20,\"character\":0}}");
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":8,\"character\":13},\"label\":\": Health\""));

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
    static const char local[] = "component Health { int value; }\ncomponent Spark { int life; }\nlocal event Tap { }\n"
                                "scene Main { }\n";
    char text[1024];
    snprintf(text, sizeof text, "%slocal event(Tap) OnTap(World world, with local Spark)\n{\n    var s = world.Get<$\n}\n", local);
    const char *locals = complete(text);
    TIDE_CHECK(offers(locals, "Spark") && offers(locals, "Health"));
    snprintf(text, sizeof text, "%sview Show(World world, with local Spark)\n{\n    var s = world.Get<$\n}\n", local);
    const char *both = complete(text);
    TIDE_CHECK(offers(both, "Spark") && offers(both, "Health"));

    open_document(WORLD_GAME "system Heal(Healer healer, mut World world)\n{\n    world.Set<Health>(healer.patient, $\n}\n");
    const char *set = request("textDocument/signatureHelp");
    TIDE_CHECK(has(set, "World.Set<Health>(Entity entity, Health value)") && has(set, "\"activeParameter\":1"));
    open_document(WORLD_GAME "system Heal(Healer healer, World world)\n{\n    var h = world.Get<Health>($);\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "World.Get<Health>(Entity entity) -> Health"));

    open_document(WORLD_GAME "system Heal(Healer healer, mut World world)\n{\n"
                             "    if (!world.Has<Health>(healer.patient)) return;\n"
                             "    mut var h = world.Get<Hea$lth>(healer.patient);\n"
                             "    h.value += 1;\n"
                             "    world.Set<Health>(healer.patient, h);\n}\n"
                             "system Watch(Healer healer, World world)\n{\n"
                             "    if (world.Has<Health>(healer.patient)) return;\n}\n");
    TIDE_CHECK(count(request_with("textDocument/references", "\"context\":{\"includeDeclaration\":true}"), "\"uri\"") == 6);
    request_with("textDocument/rename", "\"newName\":\"Hp\"");
    const char *renamed = apply_reply(game, "file:///test.tide");
    TIDE_CHECK(has(renamed, "world.Has<Hp>(healer.patient)") && has(renamed, "world.Get<Hp>(healer.patient)"));
    TIDE_CHECK(has(renamed, "world.Set<Hp>(healer.patient, h)") && !has(renamed, "Health"));
    char copy[8192];
    snprintf(copy, sizeof copy, "%s", renamed);
    open_document(copy);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document(WORLD_GAME "system Count(mut Sco$re score) { score.total = 1; }\n");
    TIDE_CHECK(has(request_with("textDocument/rename", "\"newName\":\"World\""), "'World' is built into the engine"));

    open_document(WORLD_GAME "system Heal(Healer healer, World world)\n{\n"
                             "    world.Set<Health>(healer.patient, Health { value = 1 });\n}\n");
    TIDE_CHECK(has(last_sent(), "'world' is read-only") && has(last_sent(), "declare the parameter as 'mut World world'"));
    const char *add_mut = actions_at(7);
    TIDE_CHECK(has(add_mut, "Declare 'world' as mut") && has(add_mut, "\"newText\":\"mut \""));
    TIDE_CHECK(has(add_mut, "\"start\":{\"line\":5,\"character\":27}"));
    open_document(WORLD_GAME "system Heal(Healer healer, mut World world)\n{\n"
                             "    if (world.Has<Health>(healer.patient)) return;\n}\n"
                             "system Idle(Healer healer, World world)\n{\n    if (healer.patient == this) return;\n}\n");
    TIDE_CHECK(has(last_sent(), "'world' is declared mut but never calls Set") && has(last_sent(), "'world' is never used"));
    TIDE_CHECK(count(last_sent(), "\"tags\":[1]") == 2);
    const char *remove_mut = actions_at(5);
    TIDE_CHECK(has(remove_mut, "Remove 'mut' from 'world'") && has(remove_mut, "\"newText\":\"\""));
    TIDE_CHECK(has(remove_mut, "\"start\":{\"line\":5,\"character\":27}"));

    open_document(WORLD_GAME "system Heal(Healer healer, lookup Health health)\n{\n}\n");
    TIDE_CHECK(has(last_sent(), "'lookup' parameters are gone") && has(last_sent(), "world.Get<Health>(entity)"));
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
    open_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.pos = 1;\n}\n");
    TIDE_CHECK(sent_count == 0);
    change_document(GAME_TYPES "system Move(mut Body body)\n{\n    body.spin = 1;\n}\n");
    TIDE_CHECK(sent_count == 0);
    lsp_settle(&server);
    TIDE_CHECK(sent_count == 1);
    TIDE_CHECK(has(last_sent(), "\"method\":\"textDocument/publishDiagnostics\""));
    TIDE_CHECK(has(last_sent(), "Body has no field 'spin'"));
    TIDE_CHECK(!has(last_sent(), "Body has no field 'pos'"));
    clear_sent();
    lsp_settle(&server);
    TIDE_CHECK(sent_count == 0);

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

    clear_sent();
    lsp_handle_ahead(&server, hover, strlen(hover), NULL, 0);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"id\":7") && has(last_sent(), "\"result\""));
    const lsp_message others[] = {{unrelated, strlen(unrelated)}, {cancel_8, strlen(cancel_8)}, {cancel_named, strlen(cancel_named)}};
    clear_sent();
    lsp_handle_ahead(&server, hover, strlen(hover), others, 3);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"id\":7") && has(last_sent(), "\"result\""));

    const lsp_message later[] = {{unrelated, strlen(unrelated)}, {cancel_7, strlen(cancel_7)}};
    clear_sent();
    lsp_handle_ahead(&server, hover, strlen(hover), later, 2);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"id\":7") && has(last_sent(), "\"code\":-32800"));
    TIDE_CHECK(!has(last_sent(), "\"result\""));

    clear_sent();
    lsp_handle_ahead(&server, named, strlen(named), others, 3);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"id\":\"h-7\"") && has(last_sent(), "\"code\":-32800"));
    clear_sent();
    lsp_handle_ahead(&server, named, strlen(named), later, 2);
    TIDE_CHECK(sent_count == 1 && has(last_sent(), "\"id\":\"h-7\"") && has(last_sent(), "\"result\""));

    clear_sent();
    lsp_handle_ahead(&server, cancel_7, strlen(cancel_7), later, 2);
    TIDE_CHECK(sent_count == 0);
}

#define QUERY_TYPES \
    "component Body { float2 position; }\n" \
    "component Fighter { int health; }\n" \
    "component Ghost { }\n" \
    "component Bullet { }\n" \
    "singleton Score { int value; }\n" \
    "scene Main { }\n" \
    "event(Spawned) Setup(with Main) { Spawn(Body, Fighter); Spawn(Body, Bullet); }\n"

TIDE_TEST(lsp_query_local_fields)
{
    start();
#define LOCAL_QUERY "component Body { int health; local int flash; }\nscene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\n"
    TIDE_CHECK(offers(complete(LOCAL_QUERY "view Show(query ($) bodies) { }\n"), "mut"));
    TIDE_CHECK(!offers(complete(LOCAL_QUERY "system Update(query ($) bodies) { }\n"), "mut"));
    TIDE_CHECK(offers(complete(LOCAL_QUERY "void Read(query ($) bodies) { }\n"), "mut"));
    TIDE_CHECK(offers(complete(LOCAL_QUERY "local event(Shown) Show(query ($) bodies) { }\n"), "mut"));
    TIDE_CHECK(offers(complete(LOCAL_QUERY "view Show(query (mut $) bodies) { }\n"), "Body"));
    const char *members = complete(LOCAL_QUERY "view Show(query (Body body) bodies) { foreach (var row in bodies) Debug.Assert(row.body.$ >= 0); }\n");
    TIDE_CHECK(offers(members, "flash") && offers(members, "health"));
    TIDE_CHECK(offers(complete(LOCAL_QUERY "struct Tool { int Read(query (Body body) rows) { mut var n = 0; foreach (var row in rows) n += row.body.$; return n; } }\n"), "flash"));
    open_document(LOCAL_QUERY "view Show(query (mut Body bo$dy) bodies) { foreach (var row in bodies) row.body.flash += 1; }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "can write those local fields"));
    open_document(LOCAL_QUERY "view Show(query (mut Body body) bodies) { foreach (var row in bodies) row.body.health = 0; }\n");
    TIDE_CHECK(has(last_sent(), "query row 'row' is read-only"));
#undef LOCAL_QUERY
}

TIDE_TEST(lsp_entity_query_members_and_headers)
{
    start();
    const char *header = complete(QUERY_TYPES "system Hits(Body shot, $) { }\n");
    TIDE_CHECK(offers(header, "query"));
    TIDE_CHECK(has(header, "query (${1:Component} ${2:value}) ${3:entities}"));
    TIDE_CHECK(offers(complete(QUERY_TYPES "view Show($) { }\n"), "query"));
    TIDE_CHECK(offers(complete(QUERY_TYPES "event Hit { int damage; }\nlocal event(Hit hit) Show($) { }\n"), "query"));
    TIDE_CHECK(offers(complete(QUERY_TYPES "event(Spawned) OnSpawn($) { }\n"), "query"));
    TIDE_CHECK(offers(complete(QUERY_TYPES "command Inspect { }\ncommand(Inspect value, PlayerID sender) InspectAll($) { }\n"), "query"));
    TIDE_CHECK(!offers(complete(QUERY_TYPES "async event(Spawned) OnSpawn($) { }\n"), "query"));

    const char *bindings = complete(QUERY_TYPES "system Hits(Body shot, query ($) fighters) { }\n");
    TIDE_CHECK(offers(bindings, "Body") && offers(bindings, "Fighter"));
    TIDE_CHECK(offers(bindings, "with") && offers(bindings, "without"));
    TIDE_CHECK(!offers(bindings, "mut") && !offers(bindings, "Score") && !offers(bindings, "World"));
    TIDE_CHECK(offers(complete(QUERY_TYPES "system Hits(query (Body $) fighters) { }\n"), "body"));
    const char *filters = complete(QUERY_TYPES "system Hits(query (Body body, without $) fighters) { }\n");
    TIDE_CHECK(offers(filters, "Ghost") && !offers(filters, "ownership") && !offers(filters, "Score"));

    const char *rows = complete(QUERY_TYPES "system Hits(query (Body body, Fighter stats, without Ghost) fighters)\n{\n"
                                          "    foreach (var fighter in fighters) { var value = fighter.$; }\n}\n");
    TIDE_CHECK(offers(rows, "entity") && offers(rows, "body") && offers(rows, "stats"));
    TIDE_CHECK(!offers(rows, "position") && !offers(rows, "health"));
    TIDE_CHECK(has(rows, "Read-only component data from this query row"));
    const char *fields = complete(QUERY_TYPES "system Hits(query (Body body, with Fighter) fighters)\n{\n"
                                            "    foreach (var fighter in fighters) { var value = fighter.body.$; }\n}\n");
    TIDE_CHECK(offers(fields, "position"));
    const char *braceless = complete(QUERY_TYPES "system Hits(query (Body body) fighters)\n{\n"
                                               "    foreach (var fighter in fighters) Debug.Log(fighter.$\n}\n");
    TIDE_CHECK(offers(braceless, "entity") && offers(braceless, "body"));
}

TIDE_TEST(lsp_entity_query_in_helpers)
{
    start();
    TIDE_CHECK(offers(complete(QUERY_TYPES "float Total($) { return 0; }\n"), "query"));
    TIDE_CHECK(!offers(complete(QUERY_TYPES "async void Total($) { }\n"), "query"));
    TIDE_CHECK(!offers(complete(QUERY_TYPES "extern float Total($);\n"), "query"));
    const char *bindings = complete(QUERY_TYPES "float Total(query ($) bodies) { return 0; }\n");
    TIDE_CHECK(offers(bindings, "Body") && offers(bindings, "with") && offers(bindings, "without"));
    TIDE_CHECK(offers(bindings, "mut") && !offers(bindings, "verified") && !offers(bindings, "Score"));
    const char *rows = complete(QUERY_TYPES "float Total(query (Body body) bodies)\n{\n"
        "    foreach (var row in bodies) { var x = row.$; }\n    return 0;\n}\n");
    TIDE_CHECK(offers(rows, "entity") && offers(rows, "body"));
    open_document(QUERY_TYPES
        "float Total(query (Body body) bodies)\n"
        "{ mut var x = 0.0; foreach (var row in bodies) x += row.body.position.x; return x; }\n"
        "system Count(mut Score score, query (Body other, with Fighter) bodies)\n"
        "{ score.value = int(Total(bodies)); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 7, 32, ""), "query borrowed from the caller"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 7, 32, ""), "extra bindings and filters"));
    const char *args = complete(QUERY_TYPES
        "float Total(query (Body body) values) { return 0; }\n"
        "system Count(query (Body body) bodies) { Total($); }\n");
    TIDE_CHECK(offers(args, "bodies"));
}

TIDE_TEST(lsp_entity_query_in_match_handlers)
{
    start();
    const char *bindings = complete(QUERY_TYPES "event(Spawned) Inspect(query ($) bodies) { }\n");
    TIDE_CHECK(offers(bindings, "Body") && offers(bindings, "with") && offers(bindings, "without"));
    TIDE_CHECK(!offers(bindings, "mut") && !offers(bindings, "verified") && !offers(bindings, "Score"));
    const char *rows = complete(QUERY_TYPES "command Inspect { }\n"
        "command(Inspect value, PlayerID sender) InspectAll(query (Body body) bodies)\n{\n"
        "    foreach (var row in bodies) Debug.Log(row.$);\n}\n");
    TIDE_CHECK(offers(rows, "entity") && offers(rows, "body"));
    open_document(QUERY_TYPES
        "event Scan { }\n"
        "system ScanNow() { Send(Scan); }\n"
        "event(Scan) Inspect(query (Body body) bodies)\n"
        "{ foreach (var row in bodies) Debug.Assert(row.body.position.x >= 0); }\n"
        "command InspectCommand { }\n"
        "command(InspectCommand value, PlayerID sender) InspectAll(query (Body body) bodies)\n"
        "{ foreach (var row in bodies) Debug.Assert(row.body.position.x >= 0); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *event_query = request_at("file:///test.tide", "textDocument/hover", 9, 40, "");
    TIDE_CHECK(has(event_query, "current match at this handler's turn in the tick-end queue"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 12, 78, ""),
                   "current match at this handler's turn in the tick-end queue"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 9, 13, ""),
                   "Through its queries, it reads any entity's `Body`"));
    TIDE_CHECK(count(request("textDocument/codeLens"), "Query: reads Body") == 2);
}

// A view's and a local handler's query: completion and editor information, as for a system's.
TIDE_TEST(lsp_entity_query_in_views_and_local_handlers)
{
    start();
    const char *bindings = complete(QUERY_TYPES "view Show(query ($) fighters) { }\n");
    TIDE_CHECK(offers(bindings, "Body") && offers(bindings, "with") && offers(bindings, "verified"));
    TIDE_CHECK(offers(bindings, "mut") && !offers(bindings, "Score") && !offers(bindings, "World"));
    TIDE_CHECK(!offers(complete(QUERY_TYPES "system Hits(query ($) fighters) { }\n"), "verified"));
    TIDE_CHECK(offers(complete(QUERY_TYPES "view Show(query (Body body, with $) fighters) { }\n"), "verified"));
    const char *confirmed = complete(QUERY_TYPES "component Spark { int value; }\n"
                                                 "view Show(query (verified $) fighters) { }\n");
    TIDE_CHECK(offers(confirmed, "Body") && offers(confirmed, "Spark") && !offers(confirmed, "Score"));
    const char *rows = complete(QUERY_TYPES "view Show(query (Body body, verified Fighter sure) fighters)\n{\n"
                                          "    foreach (var fighter in fighters) { var value = fighter.$; }\n}\n");
    TIDE_CHECK(offers(rows, "entity") && offers(rows, "body") && offers(rows, "sure"));

    static const char game[] = QUERY_TYPES
        "local singleton Seen { float x; int health; }\n"
        "event Hit { int damage; }\n"
        "system Strike(with Fighter) { this.Send(Hit { damage = 1 }); }\n"
        "view Show(Body shot, mut Seen seen, query (Body body, verified Fighter sure) fighters)\n{\n"
        "    foreach (var fighter in fighters)\n    {\n"
        "        seen.x = fighter.body.position.x + shot.position.x;\n"
        "        seen.health = fighter.sure.health;\n"
        "    }\n}\n"
        "local event(Hit hit) Heard(mut Seen seen, query (Body body, with Fighter) fighters)\n{\n"
        "    foreach (var fighter in fighters) seen.x = fighter.body.position.x + hit.damage;\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *query = request_at("file:///test.tide", "textDocument/hover", 10, 79, "");
    TIDE_CHECK(has(query, "second entity set, independent of the entity this view runs for"));
    TIDE_CHECK(has(query, "the same entity in the world the server has confirmed"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 10, 72, ""), "As the last tick the server confirmed has it"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 10, 56, ""),
                   "The query goes through the entities the view draws that are the same entity in the confirmed world too"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 10, 6, ""),
                   "Through its queries, it reads any entity's `Body` and `Fighter`"));
    const char *lenses = request("textDocument/codeLens");
    TIDE_CHECK(has(lenses, "Query: reads Body and Fighter"));
    TIDE_CHECK(has(lenses, "hears Hit at the start of a frame") && count(lenses, "Query: reads Body") == 2);
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 18, 76, ""),
                   "Its rows are the match as the tick that sent the event left it"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 18, 23, ""), "Through its queries, it reads any entity's `Body`"));

    open_document(QUERY_TYPES "component Spark { int value; }\nlocal singleton Seen { float x; }\n"
                  "view Show(mut Seen seen, query (Body body) bodies, query (local Spark spark) sparks)\n{\n"
                  "    foreach (var row in bodies) seen.x += row.body.position.x;\n"
                  "    foreach (var row in sparks) seen.x += row.spark.value;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 9, 45, ""), "blended between the last two ticks"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 9, 78, ""), "this machine's own entities, as they are"));
    open_document(QUERY_TYPES "view Show(query (mut Body body) bodies)\n{\n"
                  "    foreach (var row in bodies) Debug.Log($\"{row.body.position.x}\");\n}\n");
    TIDE_CHECK(has(last_sent(), "a mutable query binding writes attached local fields"));
}

TIDE_TEST(lsp_entity_query_symbols)
{
    start();
    static const char game[] = QUERY_TYPES
        "system Hits(Body shot, with Bullet, query (Body body, Fighter stats, without Ghost) fighters)\n{\n"
        "    foreach (var fighter in fighters)\n    {\n"
        "        var value = fighter.body.position + shot.position;\n"
        "        var target = fighter.entity;\n"
        "        if (fighter.stats.health == 0 && target != this && value.x > 1) return;\n"
        "    }\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 7, 8, ""),
                   "query (Body body, Fighter stats, without Ghost) fighters"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 9, 32, ""), "second entity set"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 11, 29, ""), "Read-only match data from this query row"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/hover", 12, 30, ""), "entity matched by this query row, read-only"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 11, 29, ""),
                   "\"start\":{\"line\":7,\"character\":48}"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/typeDefinition", 11, 29, ""),
                   "\"start\":{\"line\":0,\"character\":10}"));
    const char *refs = request_at("file:///test.tide", "textDocument/references", 7, 48,
                                  "\"context\":{\"includeDeclaration\":true}");
    TIDE_CHECK(count(refs, "\"uri\":\"file:///test.tide\"") == 2);
    request_at("file:///test.tide", "textDocument/rename", 11, 29, "\"newName\":\"shape\"");
    const char *renamed = apply_reply(game, "file:///test.tide");
    TIDE_CHECK(has(renamed, "query (Body shape, Fighter stats, without Ghost) fighters"));
    TIDE_CHECK(has(renamed, "fighter.shape.position") && has(renamed, "Body shot"));
    TIDE_CHECK(!has(renamed, "fighter.body"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/rename", 11, 29, "\"newName\":\"entity\""),
                   "already declared there"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 12, 30, ""),
                   "Built-in names can't be renamed"));

    request_at("file:///test.tide", "textDocument/rename", 7, 43, "\"newName\":\"Hull\"");
    renamed = apply_reply(game, "file:///test.tide");
    TIDE_CHECK(has(renamed, "component Hull") && has(renamed, "query (Hull body"));
    TIDE_CHECK(has(renamed, "Hull shot") && !has(renamed, "Body"));

    open_document(QUERY_TYPES "system Hits(query (Body body) fighters)\n{\n"
                  "    foreach (var fighter in fighters) fighter.body.position.x = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "query row 'fighter' is read-only"));
}

TIDE_TEST(lsp_entity_query_format_tokens_and_schedule)
{
    start();
    static const char messy[] = QUERY_TYPES "system Hits(Body shot,query(Body body,with Fighter,without Ghost) fighters)\n{\n"
        "foreach(var fighter in fighters)\n{\nvar value=fighter.body.position+shot.position;\n"
        "if(value.x>1&&fighter.entity!=this)return;\n}\n}\n";
    format_reply(messy);
    const char *tidy = apply_reply(messy, NULL);
    TIDE_CHECK(has(tidy, "system Hits(Body shot, query (Body body, with Fighter, without Ghost) fighters)"));
    TIDE_CHECK(has(tidy, "    foreach (var fighter in fighters)\n    {\n"));
    char again[8192];
    snprintf(again, sizeof again, "%s", tidy);
    TIDE_CHECK(has(format_reply(again), "\"result\":[]"));

    open_document("component Body { int value; }\nscene Main { }\n"
                  "system S(query (Body body) candidates) { foreach (var row in candidates) { var x = row.body.value; } }\n");
    const char *tokens = request("textDocument/semanticTokens/full");
    TIDE_CHECK(has(tokens, "0,2,5,11,0,0,7,4,2,0,0,5,4,5,3,0,6,10,5,3")); // query, Body, body, candidates
    TIDE_CHECK(has(tokens, "0,4,4,7,2")); // row.body: a readonly property

    open_document(QUERY_TYPES "system Move(mut Body body, with Fighter) { body.position.x += 1; }\n"
                  "system Hits(Body shot, with Bullet, query (Body body, with Fighter) fighters)\n{\n"
                  "    foreach (var fighter in fighters) { var value = fighter.body.position + shot.position; if (value.x > 1) return; }\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/codeLens"), "Query: reads Body"));
    const char *hover = request_at("file:///test.tide", "textDocument/hover", 8, 8, "");
    TIDE_CHECK(has(hover, "Through its queries, it reads any entity's `Body`"));
    TIDE_CHECK(has(hover, "`Move`: it writes `Body`, which this reads"));

    open_document(QUERY_TYPES "system Rewrite(query (Body body, with Fighter) fighters, mut World world)\n{\n"
                  "    foreach (var fighter in fighters)\n    {\n"
                  "        mut var next = fighter.body;\n"
                  "        next.position.x += 1;\n"
                  "        world.Set<Body>(fighter.entity, next);\n"
                  "    }\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/codeLens"), "World/query: reads Body; writes Body"));
    const char *alias = request_at("file:///test.tide", "textDocument/hover", 7, 8, "");
    TIDE_CHECK(has(alias, "Through its `World` and queries, it reads any entity's `Body` and writes any entity's `Body`"));
}

TIDE_TEST(lsp_entity_query_every_prefix_is_safe)
{
    start();
    static const char program[] = QUERY_TYPES
        "system Hits(Body shot, query (Body body, Fighter stats, without Ghost) fighters)\n{\n"
        "    foreach (var fighter in fighters)\n    {\n"
        "        var value = fighter.body.position + shot.position;\n"
        "        if (fighter.stats.health == 0 && fighter.entity != this && value.x > 1) return;\n"
        "    }\n}\n"
        "system Filter(query (with Fighter, without Ghost) fighters)\n{\n"
        "    foreach (var fighter in fighters) fighter.entity.Destroy();\n}\n";
    type_out(program, strlen(QUERY_TYPES));
}

TIDE_TEST(lsp_entity_query_unused_bindings)
{
    start();
    static const char game[] = QUERY_TYPES "system Filter(query (Body body, with Fighter) fighters)\n{\n"
        "    foreach (var fighter in fighters) fighter.entity.Destroy();\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "'body' is never used") && has(last_sent(), "\"tags\":[1]"));
    request_at("file:///test.tide", "textDocument/codeAction", 7, 26,
               "\"range\":{\"start\":{\"line\":7,\"character\":0},\"end\":{\"line\":7,\"character\":72}},"
               "\"context\":{\"diagnostics\":[]}");
    TIDE_CHECK(has(last_sent(), "Only require it: 'with Body'"));
    const json *reply = json_parse(last_sent(), strlen(last_sent()));
    const json *actions = json_get(reply, "result");
    bool found = false;
    for (int i = 0; actions && i < actions->count; i++) {
        const json *edits = json_path(actions->items[i], "edit", "changes", "file:///test.tide", NULL);
        char text[8192];
        snprintf(text, sizeof text, "%s", game);
        if (edits && edits->kind == JSON_ARRAY) apply_edits(text, sizeof text, edits);
        found |= has(text, "query (with Body, with Fighter) fighters");
    }
    json_release();
    TIDE_CHECK(found);
}

// Random: a generator's methods, Random(seed), and its rolls and the match's seed on the type.
TIDE_TEST(lsp_random)
{
    start();
#define DICE "component Dice\n{\n    Random random;\n    int last;\n}\nscene Main { }\n"
    const char *methods = complete(DICE "system Roll(mut Dice dice)\n{\n    dice.last = dice.random.$\n}\n");
    TIDE_CHECK(offers(methods, "NextInt") && offers(methods, "NextFloat") && offers(methods, "NextBool"));
    TIDE_CHECK(offers(methods, "NextFloat2Direction") && offers(methods, "NextQuaternionRotation") && offers(methods, "state"));
    TIDE_CHECK(has(methods, "Random.NextInt() -> int") && has(methods, "never goes negative"));
    TIDE_CHECK(!offers(methods, "NextState") && !offers(methods, "Dot"));

    open_document(DICE "system Roll(mut Dice dice)\n{\n    dice.last = dice.random.Next$Int(1, 7);\n}\n");
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "Random.NextInt(int min, int max) -> int") && has(hover, "is a die"));

    open_document(DICE "system Roll(mut Dice dice)\n{\n    dice.last = dice.random.NextInt($\n}\n");
    const char *help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "Random.NextInt(int max) -> int") && has(help, "Random.NextInt(int min, int max) -> int"));

    open_document("component Dice\n{\n    Ran$dom random;\n}\nscene Main { }\n");
    const char *type = request("textDocument/hover");
    TIDE_CHECK(has(type, "struct Random") && has(type, "long state") && has(type, "A random number generator's state"));
    open_document(DICE "system Roll(mut Dice dice)\n{\n    dice.random = Ran$dom(42);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "A random number generator's state"));
    open_document(DICE "system Roll(mut Dice dice)\n{\n    dice.random = Random($\n}\n");
    const char *seeded = request("textDocument/signatureHelp");
    TIDE_CHECK(has(seeded, "Random(long seed)") && has(seeded, "the same seed, the same numbers"));
    TIDE_CHECK(offers(complete("component Dice\n{\n    $\n}\nscene Main { }\n"), "Random"));
    open_document(DICE "system Roll(mut Dice dice)\n{\n    dice.random.sta$te = 5;\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "All there is to the generator"));

#define DECK "singleton Deck { int top; }\nscene Main { }\n"
    const char *rolls = complete(DECK "system Roll(mut Deck deck)\n{\n    deck.top = Random.$\n}\n");
    TIDE_CHECK(offers(rolls, "NextInt") && offers(rolls, "NextFloat2Direction") && offers(rolls, "seed"));
    TIDE_CHECK(has(rolls, "The match's seed") && !offers(rolls, "state"));
    const char *task = complete(DECK "async void Deal(mut Deck deck)\n{\n    await Wait.Ticks(1);\n    deck.top = Random.$\n}\n");
    TIDE_CHECK(offers(task, "NextInt") && offers(task, "seed"));
    const char *sample = complete("input Keys\n{\n    int luck;\n    Sample(Devices devices)\n    {\n        luck = Random.$\n    }\n}\nscene Main { }\n");
    TIDE_CHECK(offers(sample, "NextInt"));
    const char *sanitize = complete("input Keys\n{\n    int luck;\n    Sanitize()\n    {\n        luck = Random.$\n    }\n}\nscene Main { }\n");
    TIDE_CHECK(!offers(sanitize, "NextInt"));
    open_document(DECK "system Roll(mut Deck deck)\n{\n    deck.top = Random.Next$Int(6);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Random.NextInt(int max) -> int"));
    open_document(DECK "system Roll(mut Deck deck)\n{\n    deck.top = Random.NextInt($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Random.NextInt(int min, int max) -> int"));
    const char *time = complete("scene Main { }\nsystem Roll(Time time)\n{\n    var seed = time.$\n}\n");
    TIDE_CHECK(!offers(time, "seed") && offers(time, "tick"));
    open_document(DECK "system Roll(Time time, mut Deck deck)\n{\n    deck.top = time.seed;\n}\n");
    TIDE_CHECK(has(last_sent(), "the match's seed is 'Random.seed' now"));

    open_document(DICE "system Roll(Dice dice)\n{\n    dice.random.NextInt(6);\n}\n");
    TIDE_CHECK(has(last_sent(), "'dice' is read-only") && has(last_sent(), "'NextInt' is a mut method"));

#define GRAIN "scene Main { }\nlocal singleton Art { List<Vertex> corners; List<int> faces; }\n"
#define GRAIN_DRAWN "view Show(Art art)\n{\n    Draw.Mesh(art.corners, art.faces, Grain);\n}\n"
    const char *pixel = complete(GRAIN "frag Grain(Vertex vertex, out Color color)\n{\n    color = Color(Random.$\n}\n");
    TIDE_CHECK(offers(pixel, "NextFloat") && offers(pixel, "NextFloat3Direction") && offers(pixel, "NextQuaternionRotation"));
    TIDE_CHECK(offers(pixel, "seed") && !offers(pixel, "state"));
    const char *corner = complete(GRAIN "vert Shake(mut Vertex vertex)\n{\n    vertex.position += Random.$\n}\n");
    TIDE_CHECK(offers(corner, "NextFloat2") && offers(corner, "NextInt"));
    TIDE_CHECK(offers(complete(GRAIN "frag Grain(Vertex vertex, out Color color)\n{\n    color = Color($\n}\n"), "Random"));
    open_document(GRAIN "frag Grain(Vertex vertex, out Color color)\n{\n"
                        "    color = Color(Random.NextFloat(), float(Random.seed % 2), 0);\n}\n" GRAIN_DRAWN);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document(GRAIN "frag Grain(Vertex vertex, out Color color)\n{\n    color = Color(Random.Next$Float(2), 0, 0);\n}\n" GRAIN_DRAWN);
    TIDE_CHECK(has(request("textDocument/hover"), "Random.NextFloat(float max) -> float"));
    open_document(GRAIN "frag Grain(Vertex vertex, out Color color)\n{\n    color = Color(Random.NextInt($\n}\n" GRAIN_DRAWN);
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Random.NextInt(int min, int max) -> int"));
    open_document(GRAIN "frag Grain(Vertex vertex, out Color color)\n{\n    mut var mine = Random(7);\n"
                        "    color = Color(mine.NextFloat(), 0, 0);\n}\n" GRAIN_DRAWN);
    TIDE_CHECK(has(last_sent(), "so it can't make a Random") && has(last_sent(), "roll on the type, 'Random.NextInt(6)'"));
}

// Default values, arguments by name, and functions that share a name
#define OVERLOADS                                                                                                     \
    "scene Main { }\nsingleton Log { float total; }\nstruct Weapon { float power = 3; }\n"                            \
    "float Damage(float base, float multiplier = 1, bool crit = false) { return crit ? base * multiplier * 2 : base * multiplier; }\n" \
    "float Damage(float base, Weapon weapon) { return base + weapon.power; }\n"

TIDE_TEST(lsp_overloads)
{
    start();
    open_document(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10, crit: true) + Damage(1.0, Weapon { });\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    open_document(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10, $\n}\n");
    const char *help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "\"label\":\"float Damage(float base, float multiplier = 1, bool crit = false)\""));
    TIDE_CHECK(has(help, "\"label\":\"float Damage(float base, Weapon weapon)\""));
    TIDE_CHECK(has(help, "\"activeSignature\":0,\"activeParameter\":1"));
    TIDE_CHECK(has(help, "{\"label\":[13,23]},{\"label\":[25,45]},{\"label\":[47,64]}"));
    open_document(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10, crit: $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":0,\"activeParameter\":2"));
    open_document(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10, weapon: $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeSignature\":1,\"activeParameter\":1"));

    open_document(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Dam$age(1.0, Weapon { });\n}\n");
    const char *picked = request("textDocument/hover");
    TIDE_CHECK(has(picked, "float Damage(float base, Weapon weapon)") && !has(picked, "multiplier"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":4,\"character\":6}"));
    open_document(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Dam$age(10, crit: true);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "float Damage(float base, float multiplier = 1, bool crit = false)"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":3,\"character\":6}"));

    static const char program[] = OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10, crit: true);\n}\n";
    open_document(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10, cr$it: true);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "bool crit"));
    request_with("textDocument/rename", "\"newName\":\"critical\"");
    const char *renamed = apply_reply(program, "file:///test.tide");
    TIDE_CHECK(has(renamed, "bool critical = false) { return critical ? base"));
    TIDE_CHECK(has(renamed, "Damage(10, critical: true)"));

    open_document(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10, 2, crit: true);\n}\n");
    const char *hints = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                   "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":20,\"character\":0}}");
    TIDE_CHECK(has(hints, "\"label\":\"base:\"") && has(hints, "\"label\":\"multiplier:\"") && !has(hints, "\"label\":\"crit:\""));
    open_document(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10);\n}\n");
    const char *left_out = request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                                      "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":20,\"character\":0}}");
    TIDE_CHECK(has(left_out, "\"label\":\"base:\"") && !has(left_out, "\"label\":\"multiplier:\""));

    open_document(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10, crti: true);\n}\n");
    TIDE_CHECK(has(last_sent(), "'Damage' has no parameter 'crti'") || has(last_sent(), "no 'Damage' takes"));

    const char *names = complete(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10, $);\n}\n");
    TIDE_CHECK(offers(names, "multiplier:") && offers(names, "crit:") && offers(names, "weapon:") && !offers(names, "base:"));
    TIDE_CHECK(has(names, "\"detail\":\"bool crit = false\"") && offers(names, "log"));
    const char *rest = complete(OVERLOADS "system Run(mut Log log)\n{\n    log.total = Damage(10, crit: true, $);\n}\n");
    TIDE_CHECK(offers(rest, "multiplier:") && !offers(rest, "crit:"));

    static const char messy[] = "scene Main { }\nsingleton Log { float total; }\n"
                                "float Scale(float value,float by=2,bool on=true)\n{\nreturn on?value*by:value;\n}\n"
                                "system Run(mut Log log)\n{\nlog.total = Scale(1,on :false,by:log.total>1?2:3);\n}\n";
    static const char expected[] = "scene Main { }\nsingleton Log { float total; }\n"
                                   "float Scale(float value, float by = 2, bool on = true)\n{\n    return on ? value * by : value;\n}\n"
                                   "system Run(mut Log log)\n{\n    log.total = Scale(1, on: false, by: log.total > 1 ? 2 : 3);\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));
}

// The engine's own functions show what a call can leave out as a default
TIDE_TEST(lsp_built_in_defaults)
{
    start();
    open_document("scene Main { }\nlocal singleton Menu { bool open; }\nview Show(Menu menu)\n{\n"
                  "    if (menu.open) Session.Open($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"label\":\"Session.Open(int port = 7777)\""));
    open_document("scene Main { }\nlocal singleton Menu { bool open; }\nview Show(Menu menu)\n{\n"
                  "    if (menu.open) Session.Kick(PlayerID(1), $\n}\n");
    const char *kick = request("textDocument/signatureHelp");
    TIDE_CHECK(has(kick, "Session.Kick(PlayerID player, string message = \\\"\\\")") && has(kick, "\"activeParameter\":1"));
    open_document("scene Main { }\nlocal singleton Menu { bool open; }\nview Show(Menu menu)\n{\n"
                  "    Debug.Ass$ert(menu.open);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Debug.Assert(bool condition, string message = \\\"\\\")"));
    open_document("scene Main { }\nview Show()\n{\n    Draw.Circle(radius: 2, center: float2(3, 4), color: Color.red);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
}

// Systems that share a name: [After] writes the one it means with its parameters' types.
#define MOVES                                                                                                         \
    "scene Main { }\ncomponent Body { float x; }\ncomponent Player { }\ncomponent Enemy { }\n"                        \
    "system Move(mut Body body, with Player) { body.x += 1; }\n"                                                       \
    "system Move(mut Body body, with Enemy) { body.x -= 1; }\n"                                                        \
    "event(Spawned) Setup(with Main) { Spawn(Body, Player); Spawn(Body, Enemy); }\n"

TIDE_TEST(lsp_shared_names)
{
    start();
    static const char program[] = MOVES "[After(Move(Body, with Enemy))]\nsystem Settle(mut Body body) { body.x *= 0.5; }\n";
    open_document(MOVES "[After(Mo$ve(Body, with Enemy))]\nsystem Settle(mut Body body) { body.x *= 0.5; }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "system Move(mut Body body, with Enemy)"));
    TIDE_CHECK(has(request("textDocument/definition"), "\"start\":{\"line\":5,\"character\":7}"));

    open_document(MOVES "[After(Move(Body, with Ene$my))]\nsystem Settle(mut Body body) { body.x *= 0.5; }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "component Enemy"));
    request_with("textDocument/rename", "\"newName\":\"Foe\"");
    const char *renamed = apply_reply(program, "file:///test.tide");
    TIDE_CHECK(has(renamed, "component Foe { }") && has(renamed, "with Foe) { body.x -= 1; }"));
    TIDE_CHECK(has(renamed, "[After(Move(Body, with Foe))]"));

    open_document(MOVES "[After(Move)]\nsystem Settle(mut Body body) { body.x *= 0.5; }\n");
    TIDE_CHECK(has(last_sent(), "2 declarations share the name 'Move'") && has(last_sent(), "'Move(Body, with Player)'"));
}

// `out` parameters, as C#'s: a function's and a method's, and a map's TryGet.
#define OUT_PARAMS                                                             \
    "struct Stats\n"                                                           \
    "{\n"                                                                      \
    "    int power = 3;\n"                                                     \
    "\n"                                                                       \
    "    bool TryPower(int least, out int found)\n"                            \
    "    {\n"                                                                  \
    "        found = power;\n"                                                 \
    "        return power >= least;\n"                                         \
    "    }\n"                                                                  \
    "}\n"                                                                      \
    "\n"                                                                       \
    "bool TryHalf(int value, out int half)\n"                                  \
    "{\n"                                                                      \
    "    half = value / 2;\n"                                                  \
    "    return value % 2 == 0;\n"                                             \
    "}\n"                                                                      \
    "\n"                                                                       \
    "bool Use(int value) { return value > 0; }\n"                              \
    "singleton Tally { int total; Stats stats; Map<int, float> scores; Map<int, Stats> all; }\n" \
    "scene Main { }\n"

// A system whose first statement is on line 22
#define OUT_RUN OUT_PARAMS "system Run(mut Tally tally)\n{\n"

// Every way an argument says `out`: a local it declares (with `var`, a type or `mut`), a field the code has, nothing, or its parameter's name.
#define OUT_EVERY                                                              \
    OUT_RUN                                                                    \
    "    if (TryHalf(8, out var half)) tally.total = half;\n"                  \
    "    tally.total += half;\n"                                               \
    "    TryHalf(8, out int typed);\n"                                         \
    "    TryHalf(typed, out mut var changed);\n"                               \
    "    changed += 1;\n"                                                      \
    "    TryHalf(changed, out tally.total);\n"                                 \
    "    TryHalf(8, out _);\n"                                                 \
    "    TryHalf(value: 8, half: out var named);\n"                            \
    "    tally.stats.TryPower(named, out int _);\n"                            \
    "}\n"

static const char *all_hints(void)
{
    return request_at("file:///test.tide", "textDocument/inlayHint", 0, 0,
                      "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":60,\"character\":0}}");
}

// The parameter where it's declared, in hovers, the outline and signatures.
TIDE_TEST(lsp_out_parameters)
{
    start();
    open_document(OUT_EVERY);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *param = hover_at(11, 33);
    TIDE_CHECK(has(param, "out int half") && has(param, "the caller's variable, which the call makes: it starts unassigned here"));
    TIDE_CHECK(has(param, "A call says `out` before its argument: `out var half` declares the variable there"));
    const char *word = hover_at(11, 25);
    TIDE_CHECK(has(word, "out int half") && has(word, "which the call makes"));
    TIDE_CHECK(has(word, "\"range\":{\"start\":{\"line\":11,\"character\":24},\"end\":{\"line\":11,\"character\":27}}"));
    const char *method = hover_at(4, 38);
    TIDE_CHECK(has(method, "out int found") && has(method, "`out var found` declares the variable there"));
    TIDE_CHECK(has(hover_at(13, 5), "out int half"));
    TIDE_CHECK(has(hover_at(22, 10), "bool TryHalf(int value, out int half)"));
    TIDE_CHECK(has(hover_at(30, 18), "bool TryPower(int least, out int found)"));
    TIDE_CHECK(has(request("textDocument/documentSymbol"), "\"name\":\"TryPower\",\"detail\":\"bool TryPower(int least, out int found)\""));

    open_document(OUT_RUN "    TryHalf(8, out var $\n}\n");
    const char *help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "\"label\":\"bool TryHalf(int value, out int half)\"") && has(help, "\"activeParameter\":1"));
    TIDE_CHECK(has(help, "{\"label\":[13,22]},{\"label\":[24,36]}")); // `out int half` is 24 to 36
    open_document(OUT_RUN "    TryHalf(half: out var h, value: $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeParameter\":0"));
    open_document(OUT_RUN "    tally.stats.TryPower(2, $\n}\n");
    const char *power = request("textDocument/signatureHelp");
    TIDE_CHECK(has(power, "bool TryPower(int least, out int found)") && has(power, "\"activeParameter\":1"));

    open_document(OUT_RUN "    TryHalf(8, tally.total);\n}\n");
    TIDE_CHECK(has(last_sent(), "'TryHalf' makes its 'half', so the argument says 'out'"));
    TIDE_CHECK(has(last_sent(), "'out var half' declares one there, and 'out _' leaves it out"));
    open_document(OUT_PARAMS "system Run(out Tally tally) { tally.total = 1; }\n");
    TIDE_CHECK(has(last_sent(), "the engine calls this itself, so nothing would take what an 'out' parameter makes"));
    TIDE_CHECK(has(last_sent(), "this takes what it changes as 'mut': 'mut Tally tally'"));
}

// The `out` argument: its word, the local it declares and the variable it writes, in editor features.
TIDE_TEST(lsp_out_arguments)
{
    start();
    open_document(OUT_EVERY);
    const char *declares = hover_at(22, 20);
    TIDE_CHECK(has(declares, "out int half") && has(declares, "Declares `half` here, a local the call makes: read-only afterwards"));
    TIDE_CHECK(has(declares, "For `TryHalf`'s `out int half`."));
    TIDE_CHECK(has(hover_at(25, 20), "out mut int changed") && has(hover_at(25, 20), "the code can change it afterwards"));
    const char *assigns = hover_at(27, 22);
    TIDE_CHECK(has(assigns, "The call makes the variable after it: it's written, as by an assignment"));
    const char *leaves = hover_at(28, 16);
    TIDE_CHECK(has(leaves, "out _") && has(leaves, "Leaves out what the call makes there: no variable takes it."));
    TIDE_CHECK(has(hover_at(28, 19), "\"result\":null"));
    TIDE_CHECK(has(hover_at(30, 40), "\"result\":null"));
    TIDE_CHECK(has(hover_at(30, 37), "A 32-bit integer"));
    TIDE_CHECK(has(hover_at(29, 23), "out int half"));

    const char *half = hover_at(22, 28);
    TIDE_CHECK(has(half, "int half") && has(half, "Local variable, read-only. An `out` argument declares it, and the call it's in makes it"));
    TIDE_CHECK(has(half, "`out mut var half` declares one the code can change afterwards."));
    TIDE_CHECK(has(hover_at(24, 24), "`out mut int typed` declares one") && has(hover_at(24, 20), "A 32-bit integer"));
    const char *changed = hover_at(25, 33);
    TIDE_CHECK(has(changed, "mut int changed") && has(changed, "Local variable. An `out` argument declares it") && !has(changed, "read-only"));
    TIDE_CHECK(has(hover_at(23, 20), "Local variable, read-only. An `out` argument declares it"));

    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/definition", 23, 20, ""),
                   "\"range\":{\"start\":{\"line\":22,\"character\":27},\"end\":{\"line\":22,\"character\":31}}"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 22, 28, "\"context\":{\"includeDeclaration\":true}"),
                     "\"uri\"") == 3);
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 23, 20, "\"context\":{\"includeDeclaration\":false}"),
                     "\"uri\"") == 2);
    const char *marks = request_at("file:///test.tide", "textDocument/documentHighlight", 25, 33, "");
    TIDE_CHECK(count(marks, "\"kind\":3") == 2 && count(marks, "\"kind\":2") == 1);

    open_document(OUT_RUN "    if (tally.all.TryGet(1, out var sta$ts)) tally.total = stats.power;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/typeDefinition"), "\"range\":{\"start\":{\"line\":0,\"character\":7}"));
    TIDE_CHECK(has(request("textDocument/hover"), "Stats stats"));

    open_document(OUT_RUN "    mut var ke$pt = 0;\n    TryHalf(8, out kept);\n    tally.total = kept;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *kept = request("textDocument/documentHighlight");
    TIDE_CHECK(count(kept, "\"kind\":3") == 2 && count(kept, "\"kind\":2") == 1);
    TIDE_CHECK(has(kept, "{\"range\":{\"start\":{\"line\":23,\"character\":19},\"end\":{\"line\":23,\"character\":23}},\"kind\":3}"));
    open_document(OUT_RUN "    TryHalf(8, out tally.to$tal);\n    tally.stats.power = tally.total;\n}\n");
    const char *field = request("textDocument/documentHighlight");
    TIDE_CHECK(has(field, "{\"range\":{\"start\":{\"line\":22,\"character\":25},\"end\":{\"line\":22,\"character\":30}},\"kind\":3}"));
    TIDE_CHECK(has(field, "{\"range\":{\"start\":{\"line\":23,\"character\":30},\"end\":{\"line\":23,\"character\":35}},\"kind\":2}"));

    static const char program[] = OUT_RUN "    if (TryHalf(8, out var half)) tally.total = half;\n    tally.total += half;\n}\n";
    open_document(OUT_RUN "    if (TryHalf(8, out var half)) tally.total = half;\n    tally.total += ha$lf;\n}\n");
    TIDE_CHECK(has(request("textDocument/prepareRename"), "\"result\":{\"start\":{\"line\":23,\"character\":19}"));
    request_with("textDocument/rename", "\"newName\":\"part\"");
    check_repeat_at("file:///test.tide", "textDocument/rename", cursor_line, cursor_character, "\"newName\":\"part\"");
    const char *renamed = apply_reply(program, "file:///test.tide");
    TIDE_CHECK(has(renamed, "if (TryHalf(8, out var part)) tally.total = part;\n    tally.total += part;\n"));
    TIDE_CHECK(has(renamed, "bool TryHalf(int value, out int half)\n{\n    half = value / 2;"));
    char copy[8192];
    snprintf(copy, sizeof copy, "%s", renamed);
    open_document(copy);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    static const char by_name[] = OUT_RUN "    TryHalf(value: 8, half: out var named);\n    tally.total = named;\n}\n";
    open_document(by_name);
    rename_at(22, 23, "part");
    const char *parts = apply_reply(by_name, "file:///test.tide");
    TIDE_CHECK(has(parts, "bool TryHalf(int value, out int part)\n{\n    part = value / 2;") && has(parts, "part: out var named"));

    open_document(OUT_EVERY);
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 28, 19, ""), "Nothing to rename here."));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 28, 16, ""),
                   "'out' is a keyword before a parameter's type or an argument, so it can't be renamed."));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 11, 25, ""), "'out' is a keyword"));
    TIDE_CHECK(has(rename_at(22, 28, "named"), "'named' is already declared"));
    TIDE_CHECK(has(rename_at(22, 28, "out"), "\"changes\""));
}

// A local an argument declares is in scope after it: the enclosing block's, or the loop's own when its header declares it.
TIDE_TEST(lsp_out_local_scopes)
{
    start();
    TIDE_CHECK(offers(complete(OUT_RUN "    if (TryHalf(8, out var half))\n    {\n        tally.total = $\n    }\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    if (TryHalf(8, out var half)) { }\n    else\n    {\n        tally.total = $\n    }\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    if (TryHalf(8, out var half)) { }\n    tally.total = $\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    if (TryHalf(8, out var half)) Use($);\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    if (TryHalf(8, out var half) && Use($)) { }\n}\n"), "half"));
    const char *chained = complete(OUT_RUN "    if (TryHalf(8, out var half) && TryHalf(half, out var quarter) && Use($)) { }\n}\n");
    TIDE_CHECK(offers(chained, "half") && offers(chained, "quarter"));
    TIDE_CHECK(has(chained, "{\"label\":\"quarter\",\"kind\":6,\"detail\":\"int\"}"));
    TIDE_CHECK(!offers(complete(OUT_RUN "    Use($);\n    if (TryHalf(8, out var half)) { }\n}\n"), "half"));
    TIDE_CHECK(!offers(complete(OUT_RUN "    if (TryHalf(Use($) ? 1 : 2, out var half)) { }\n}\n"), "half"));
    TIDE_CHECK(!offers(complete(OUT_RUN "    {\n        if (TryHalf(8, out var half)) { }\n    }\n    Use($);\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    if (tally.total > 1) { }\n    else if (TryHalf(8, out var half))\n    {\n"
                                       "        tally.total = $\n    }\n}\n"),
                      "half"));
    TIDE_CHECK(!offers(complete(OUT_RUN "    if (tally.total > 1) { }\n    else if (TryHalf(8, out var half)) { }\n    Use($);\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    TryHalf(8, out var half);\n    tally.total = $\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    var even = TryHalf(8, out var half);\n    tally.total = $\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    mut var even = false;\n    even = TryHalf(8, out var half);\n    tally.total = $\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    switch (TryHalf(8, out var half) ? 1 : 0)\n    {\n        case 1:\n            tally.total = $\n"
                                       "            break;\n    }\n}\n"),
                      "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    switch (TryHalf(8, out var half) ? 1 : 0)\n    {\n        case 1:\n            break;\n    }\n"
                                       "    tally.total = $\n}\n"),
                      "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    while (TryHalf(tally.total, out var half))\n    {\n        tally.total = $\n    }\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    while (TryHalf(tally.total, out var half)) Use($);\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    while (TryHalf(tally.total, out var half) && Use($)) { }\n}\n"), "half"));
    TIDE_CHECK(!offers(complete(OUT_RUN "    while (TryHalf(tally.total, out var half)) { }\n    Use($);\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    for (var i = 0; TryHalf(i, out var half); Use($)) { }\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    for (var i = 0; TryHalf(i, out var half); i++)\n    {\n        tally.total = $\n    }\n}\n"), "half"));
    TIDE_CHECK(!offers(complete(OUT_RUN "    for (var i = 0; TryHalf(i, out var half); i++) { }\n    Use($);\n}\n"), "half"));
    TIDE_CHECK(offers(complete(OUT_PARAMS "void Twice(out int ran, Action content) { ran = 2; content(); content(); }\n"
                                          "system Run(mut Tally tally)\n{\n    Twice(out var ran)\n    {\n        Use($);\n    }\n}\n"),
                      "ran"));
    TIDE_CHECK(!offers(complete(OUT_PARAMS "void Twice(out int ran, Action content) { ran = 2; content(); content(); }\n"
                                           "system Run(mut Tally tally)\n{\n    Twice(out var ran)\n    {\n        TryHalf(8, out var half);\n"
                                           "    }\n    Use($);\n}\n"),
                       "half"));
    const char *nothing = complete(OUT_RUN "    TryHalf(8, out _);\n    Use($);\n}\n");
    TIDE_CHECK(offers(nothing, "tally") && !offers(nothing, "_") && !has(nothing, "tide_out_"));

    const char *members = complete(OUT_RUN "    if (tally.all.TryGet(3, out var stats)) tally.total += stats.$power;\n}\n");
    TIDE_CHECK(offers(members, "power") && offers(members, "TryPower"));
    const char *typing = complete(OUT_RUN "    if (tally.all.TryGet(3, out var stats)) tally.total += stats.$\n}\n");
    TIDE_CHECK(offers(typing, "power") && offers(typing, "TryPower"));
    TIDE_CHECK(offers(complete(OUT_RUN "    if (tally.all.TryGet(3, out Stats stats)) tally.total += stats.$\n}\n"), "power"));
    TIDE_CHECK(offers(complete(OUT_RUN "    if (tally.all.TryGet(key: 3, value: out mut var stats)) tally.total += stats.$\n}\n"), "power"));
    TIDE_CHECK(!offers(complete(OUT_RUN "    if (TryHalf(3, out var stats)) tally.total += stats.$\n}\n"), "power"));
}

// Completion where `out` goes: parameters and the argument for an `out` parameter.
TIDE_TEST(lsp_out_completion)
{
    start();
    const char *function = complete(OUT_PARAMS "bool Find(int key, $");
    TIDE_CHECK(offers(function, "mut") && offers(function, "out") && offers(function, "int"));
    TIDE_CHECK(has(function, "The caller's variable, which the call makes: assigned here on every path that returns"));
    TIDE_CHECK(offers(complete("struct S\n{\n    int a;\n    bool Find($\n"), "out"));
    TIDE_CHECK(offers(complete(OUT_PARAMS "extern bool Find(int key, $"), "out"));
    TIDE_CHECK(!offers(complete(OUT_PARAMS "system Find($"), "out") && !offers(complete(OUT_PARAMS "view Find($"), "out"));
    TIDE_CHECK(!offers(complete(OUT_PARAMS "event(Spawned) Find($"), "out") && !offers(complete(OUT_PARAMS "frag Find($"), "out"));
    TIDE_CHECK(!offers(complete(OUT_PARAMS "async void Find($"), "out"));
    TIDE_CHECK(!offers(complete("struct S\n{\n    int a;\n    S operator +($\n"), "out"));
    const char *after = complete(OUT_PARAMS "bool Find(int key, out $");
    TIDE_CHECK(offers(after, "int") && offers(after, "List") && offers(after, "Stats") && offers(after, "Tally"));
    TIDE_CHECK(!offers(after, "Action") && !offers(after, "Devices") && !offers(after, "mut") && !offers(after, "out"));
    TIDE_CHECK(offers(complete(OUT_PARAMS "bool Find(int key, out Stats $"), "stats"));
    TIDE_CHECK(!offers(complete(OUT_PARAMS "bool Find(int out $"), "Stats"));

    const char *argument = complete(OUT_RUN "    TryHalf(8, $);\n}\n");
    TIDE_CHECK(offers(argument, "out var half") && offers(argument, "out") && offers(argument, "out _") && offers(argument, "half:"));
    TIDE_CHECK(has(argument, "{\"label\":\"out var half\",\"kind\":15,\"detail\":\"Declares the variable the call makes\""));
    TIDE_CHECK(has(argument, "\"insertText\":\"out var ${1:half}\",\"insertTextFormat\":2"));
    TIDE_CHECK(!offers(argument, "tally") && !offers(argument, "true"));
    TIDE_CHECK(offers(complete(OUT_RUN "    TryHalf(8, $\n}\n"), "out var half"));
    TIDE_CHECK(offers(complete(OUT_RUN "    TryHalf(8, o$\n}\n"), "out var half"));
    const char *named = complete(OUT_RUN "    TryHalf(value: 8, half: $);\n}\n");
    TIDE_CHECK(offers(named, "out var half") && offers(named, "out _") && !offers(named, "tally"));
    TIDE_CHECK(offers(complete(OUT_RUN "    tally.stats.TryPower(2, $);\n}\n"), "out var found"));
    const char *value = complete(OUT_RUN "    TryHalf($);\n}\n");
    TIDE_CHECK(!offers(value, "out") && !offers(value, "out _") && offers(value, "tally") && offers(value, "half:"));
    TIDE_CHECK(!offers(complete(OUT_RUN "    Use($);\n}\n"), "out"));
    const char *shared = complete(OUT_PARAMS "bool Pick(int key, out int value) { value = key; return true; }\n"
                                             "bool Pick(int key, int least) { return key > least; }\n"
                                             "system Run(mut Tally tally)\n{\n    Pick(1, $);\n}\n");
    TIDE_CHECK(offers(shared, "out var value") && offers(shared, "tally"));

    const char *made = complete(OUT_RUN "    mut var spare = 0;\n    var fixed = 1;\n    mut var other = 0.5;\n    TryHalf(8, out $);\n}\n");
    TIDE_CHECK(offers(made, "var") && offers(made, "mut") && offers(made, "_") && offers(made, "int"));
    TIDE_CHECK(has(made, "\"insertText\":\"var ${1:half}\"") && has(made, "\"insertText\":\"int ${1:half}\""));
    TIDE_CHECK(offers(made, "spare") && offers(made, "tally"));
    TIDE_CHECK(!offers(made, "fixed") && !offers(made, "other") && !offers(made, "TryHalf") && !offers(made, "true"));
    const char *changes = complete(OUT_RUN "    TryHalf(8, out mut $);\n}\n");
    TIDE_CHECK(offers(changes, "var") && offers(changes, "int") && !offers(changes, "_") && !offers(changes, "tally"));
    TIDE_CHECK(has(complete(OUT_RUN "    TryHalf(8, out var $);\n}\n"), "\"items\":[]"));
    TIDE_CHECK(offers(complete(OUT_RUN "    TryHalf(value: 8, half: out $);\n}\n"), "var"));
    const char *name = complete(OUT_RUN "    mut var out = 2;\n    Use(out $);\n}\n");
    TIDE_CHECK(!offers(name, "var") && !offers(name, "_"));
    TIDE_CHECK(offers(complete(OUT_PARAMS "struct Pile\n{\n    int size;\n    mut void Halve() { TryHalf(size, out $); }\n}\n"), "size"));
}

TIDE_TEST(lsp_animation_catalog_try_get)
{
    start();
    static const char prefix[] = "scene Main { }\nvoid Read(AnimationCatalog clips, Skeleton skeleton)\n{\n";
    char text[1024];
    snprintf(text, sizeof text, "%s    clips.$\n}\n", prefix);
    const char *members = complete(text);
    TIDE_CHECK(offers(members, "TryGetByName") && offers(members, "TryGetAt")
               && !offers(members, "Find") && !offers(members, "At"));
    TIDE_CHECK(has(members, "AnimationCatalog.TryGetAt(int index, out Animation result) -> bool"));
    TIDE_CHECK(has(members, "TryGetByName(${1:name}, out var ${2:result})"));
    snprintf(text, sizeof text, "%s    clips.TryGetAt(0, $);\n}\n", prefix);
    TIDE_CHECK(offers(complete(text), "out var result"));
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "out Animation result) -> bool"));
    snprintf(text, sizeof text, "%s    clips.TryGetByName(result: out $, name: \"Run\");\n}\n", prefix);
    TIDE_CHECK(offers(complete(text), "Animation"));
    snprintf(text, sizeof text, "%s    if (clips.TryGetAt(0, out var clip)) Debug.Log(clip.$);\n}\n", prefix);
    TIDE_CHECK(offers(complete(text), "duration"));
    snprintf(text, sizeof text, "%s    skeleton.bones.$\n}\n", prefix);
    members = complete(text);
    TIDE_CHECK(has(members, "TryGetAt(int index, out Bone result) -> bool") && !offers(members, "Find"));
    snprintf(text, sizeof text, "%s    skeleton.TryGetByName(\"Arm\", out $);\n}\n", prefix);
    TIDE_CHECK(offers(complete(text), "Bone"));
    snprintf(text, sizeof text, "%s    if (clips.TryGetByNa$me(\"Run\", out var clip)) Debug.Log(clip.name);\n}\n", prefix);
    open_document(text);
    TIDE_CHECK(has(request("textDocument/hover"), "TryGetByName(string name, out Animation result) -> bool"));
}

// A map's TryGet, the engine's own `out` parameter.
TIDE_TEST(lsp_map_try_get)
{
    start();
    open_document(OUT_RUN "    if (tally.scores.TryGet(3, out var score)) tally.total += int(score);\n"
                          "    tally.scores.TryGet(key: 4, value: out _);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = hover_at(22, 22);
    TIDE_CHECK(has(hover, "Map<int, float>.TryGet(int key, out float value) -> bool"));
    TIDE_CHECK(has(hover, "Whether the key is in the map, and through `out`, its value, or the value type's default"));
    TIDE_CHECK(has(hover_at(22, 40), "float score") && has(hover_at(22, 40), "An `out` argument declares it"));
    TIDE_CHECK(has(hover_at(22, 32), "out float score") && has(hover_at(22, 32), "Declares `score` here"));
    TIDE_CHECK(has(all_hints(), "{\"position\":{\"line\":22,\"character\":44},\"label\":\": float\",\"kind\":1"));
    TIDE_CHECK(has(request_at("file:///test.tide", "textDocument/prepareRename", 22, 22, ""), "Built-in names can't be renamed"));
    open_document(OUT_RUN "    if (tally.scores.Ha$s(3)) tally.scores.Remove(3);\n}\n");
    TIDE_CHECK(has(request("textDocument/hover"), "Map<int, float>.Has(int key) -> bool"));

    const char *members = complete(OUT_RUN "    tally.scores.$\n}\n");
    TIDE_CHECK(offers(members, "TryGet") && offers(members, "Get") && offers(members, "Has"));
    TIDE_CHECK(has(members, "\"detail\":\"Map<int, float>.TryGet(int key, out float value) -> bool\""));
    TIDE_CHECK(has(members, "\"insertText\":\"TryGet(${1:key}, out var ${2:value})\",\"insertTextFormat\":2"));
    TIDE_CHECK(has(members, "\"insertText\":\"Has($1)\""));
    TIDE_CHECK(!offers(complete("singleton Seen { Set<int> keys; }\nscene Main { }\nsystem Run(mut Seen seen)\n{\n    seen.keys.$\n}\n"), "TryGet"));

    open_document(OUT_RUN "    tally.scores.TryGet(3, $\n}\n");
    const char *help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "\"label\":\"Map<int, float>.TryGet(int key, out float value) -> bool\"") && has(help, "\"activeParameter\":1"));
    TIDE_CHECK(has(help, "{\"label\":[23,30]},{\"label\":[32,47]}")); // `out float value` is 32 to 47
    open_document(OUT_RUN "    tally.scores.TryGet(value: out var score, key: $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "\"activeParameter\":0"));
    const char *argument = complete(OUT_RUN "    tally.scores.TryGet(3, $);\n}\n");
    TIDE_CHECK(offers(argument, "out var value") && offers(argument, "out") && offers(argument, "out _") && !offers(argument, "tally"));
    const char *after = complete(OUT_RUN "    mut var best = 0.5;\n    mut var count = 0;\n    tally.scores.TryGet(3, out $);\n}\n");
    TIDE_CHECK(offers(after, "var") && offers(after, "float") && offers(after, "_") && offers(after, "best") && !offers(after, "count"));
    TIDE_CHECK(offers(complete(OUT_RUN "    tally.all.TryGet(3, out $);\n}\n"), "Stats"));
    TIDE_CHECK(!offers(complete(OUT_RUN "    tally.scores.TryGet($);\n}\n"), "out var value"));

    open_document(OUT_RUN "    tally.scores.TryGet(3, tally.total);\n}\n");
    TIDE_CHECK(has(last_sent(), "'TryGet' makes its 'value', so the argument says 'out'"));
    open_document(OUT_RUN "    tally.scores.TryGet(3, out tally.total);\n}\n");
    TIDE_CHECK(has(last_sent(), "'TryGet' makes its 'value' float, not int"));
}

// The GUI's fields that say whether the player entered what they typed: an `out` version beside the one without.
#define ENTERED_LOBBY "local singleton Lobby { string code; int count; float ratio; bool entered; int joins; }\nscene Main { }\n"
#define ENTERED_VIEW ENTERED_LOBBY "view Join(mut Lobby lobby)\n{\n"

TIDE_TEST(lsp_gui_entered)
{
    start();
    static const char game[] = ENTERED_VIEW "    GUILayout.TextField(\"Code\", lobby.code, out var entered);\n"
                                            "    if (GUILayout.Button(\"Join\") || entered) lobby.joins += 1;\n"
                                            "    GUI.IntField(Rect(0, 0, 200, 30), \"Count\", lobby.count, out lobby.entered);\n"
                                            "    GUILayout.FloatField(\"Ratio\", lobby.ratio, out _);\n"
                                            "    GUILayout.FloatField(label: \"Ratio\", entered: out bool typed, value: lobby.ratio);\n"
                                            "    if (typed) lobby.joins += 1;\n"
                                            "    GUILayout.TextField(\"Code\", lobby.code);\n}\n";
    open_document(game);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(format_reply(game), "\"result\":[]"));

    open_document(game);
    const char *field = hover_at(4, 16);
    TIDE_CHECK(has(field, "GUILayout.TextField(string label, mut string value) -> bool\\n"
                          "GUILayout.TextField(string label, mut string value, out bool entered) -> bool"));
    TIDE_CHECK(has(field, "It changes `value` as the player types, and returns whether it did."));
    TIDE_CHECK(has(field, "true in the frame Enter ends the typing in this field"));
    const char *number = hover_at(6, 10);
    TIDE_CHECK(has(number, "GUI.IntField(Rect rect, string label, mut int value) -> bool\\n"
                           "GUI.IntField(Rect rect, string label, mut int value, out bool entered) -> bool"));
    TIDE_CHECK(has(number, "or a gamepad's south button, ends the typing in this field"));
    const char *word = hover_at(4, 46);
    TIDE_CHECK(has(word, "out bool entered") && has(word, "Declares `entered` here, a local the call makes: read-only afterwards"));
    const char *local = hover_at(4, 54);
    TIDE_CHECK(has(local, "bool entered") && has(local, "Local variable, read-only. An `out` argument declares it"));
    TIDE_CHECK(has(hover_at(5, 40), "An `out` argument declares it"));
    TIDE_CHECK(has(hover_at(6, 62), "The call makes the variable after it: it's written, as by an assignment"));
    TIDE_CHECK(has(hover_at(7, 48), "Leaves out what the call makes there"));
    const char *hints = all_hints();
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":4,\"character\":59},\"label\":\": bool\",\"kind\":1,\"paddingLeft\":true}"));
    TIDE_CHECK(count(hints, "\"label\":\": bool\"") == 1);
    const char *marks = request_at("file:///test.tide", "textDocument/documentHighlight", 6, 74, "");
    TIDE_CHECK(has(marks, "{\"range\":{\"start\":{\"line\":6,\"character\":70},\"end\":{\"line\":6,\"character\":77}},\"kind\":3}"));
    TIDE_CHECK(count(marks, "\"kind\":2") == 0);
    rename_at(5, 40, "joined");
    const char *renamed = apply_reply(game, "file:///test.tide");
    TIDE_CHECK(has(renamed, "lobby.code, out var joined);\n    if (GUILayout.Button(\"Join\") || joined) lobby.joins += 1;\n"));

    open_document(ENTERED_VIEW "    GUILayout.TextField(\"Code\", $\n}\n");
    const char *second = request("textDocument/signatureHelp");
    TIDE_CHECK(has(second, "\"label\":\"GUILayout.TextField(string label, mut string value) -> bool\""));
    TIDE_CHECK(has(second, "\"label\":\"GUILayout.TextField(string label, mut string value, out bool entered) -> bool\""));
    TIDE_CHECK(has(second, "\"activeSignature\":0,\"activeParameter\":1"));
    open_document(ENTERED_VIEW "    GUILayout.TextField(\"Code\", lobby.code, $\n}\n");
    const char *third = request("textDocument/signatureHelp");
    TIDE_CHECK(has(third, "\"activeSignature\":1,\"activeParameter\":2"));
    TIDE_CHECK(has(third, "\"parameters\":[{\"label\":[20,32]},{\"label\":[34,50]},{\"label\":[52,68]}]")); // `out bool entered` is 52 to 68
    open_document(ENTERED_VIEW "    GUI.FloatField(Rect(0, 0, 200, 30), \"Ratio\", lobby.ratio, out var $\n}\n");
    const char *rect = request("textDocument/signatureHelp");
    TIDE_CHECK(has(rect, "\"label\":\"GUI.FloatField(Rect rect, string label, mut float value, out bool entered) -> bool\""));
    TIDE_CHECK(has(rect, "\"activeSignature\":1,\"activeParameter\":3"));

    const char *members = complete(ENTERED_VIEW "    GUILayout.$\n}\n");
    TIDE_CHECK(count(members, "\"label\":\"TextField\"") == 1);
    TIDE_CHECK(has(members, "\"detail\":\"GUILayout.TextField(string label, mut string value) -> bool (+1 version)\""));
    TIDE_CHECK(has(members, "returns whether it did.\\n\\n```tide\\nGUILayout.TextField(string label, mut string value, out bool entered) -> bool"
                            "\\n```\\nWith `out entered`, it also says whether the player entered the text"));
    TIDE_CHECK(has(members, "\"detail\":\"GUILayout.IntField(string label, mut int value) -> bool (+1 version)\""));
    TIDE_CHECK(has(members, "\"detail\":\"GUILayout.FloatField(string label, mut float value) -> bool (+1 version)\""));
    TIDE_CHECK(has(members, "\"detail\":\"GUILayout.Float2Field(string label, mut float2 value) -> bool\""));
    TIDE_CHECK(has(members, "\"insertText\":\"TextField(${1:label}, ${2:value})\""));
    const char *argument = complete(ENTERED_VIEW "    GUILayout.TextField(\"Code\", lobby.code, $);\n}\n");
    TIDE_CHECK(offers(argument, "out var entered") && offers(argument, "out") && offers(argument, "out _"));
    TIDE_CHECK(has(argument, "\"insertText\":\"out var ${1:entered}\",\"insertTextFormat\":2"));
    TIDE_CHECK(has(argument, "A read-only local of the parameter's type, `bool`, which the call makes"));
    TIDE_CHECK(!offers(argument, "lobby") && !offers(argument, "true"));
    const char *after = complete(ENTERED_VIEW "    mut var done = false;\n    mut var tries = 0;\n"
                                              "    GUI.IntField(Rect(0, 0, 200, 30), \"Count\", lobby.count, out $);\n}\n");
    TIDE_CHECK(offers(after, "var") && offers(after, "bool") && offers(after, "mut") && offers(after, "_"));
    TIDE_CHECK(offers(after, "done") && offers(after, "lobby") && !offers(after, "tries"));
    TIDE_CHECK(offers(complete(ENTERED_VIEW "    GUILayout.FloatField(\"Ratio\", lobby.ratio, entered: $);\n}\n"), "out var entered"));
    const char *value = complete(ENTERED_VIEW "    GUILayout.TextField(\"Code\", $);\n}\n");
    TIDE_CHECK(!offers(value, "out var entered") && !offers(value, "out _") && offers(value, "lobby"));
    TIDE_CHECK(!offers(complete(ENTERED_VIEW "    GUILayout.Slider(\"Ratio\", lobby.ratio, $);\n}\n"), "out var entered"));

    static const char missing[] = ENTERED_VIEW "    GUILayout.TextField(\"Code\", lobby.code, lobby.entered);\n}\n";
    open_document(missing);
    TIDE_CHECK(has(last_sent(), "'GUILayout.TextField' makes its 'entered', so the argument says 'out'"));
    TIDE_CHECK(has(last_sent(), "'out var entered' declares one there, and 'out _' leaves it out"));
    actions_at(4);
    const char *added = apply_fix(missing, "Add 'out'");
    TIDE_REQUIRE(added != NULL);
    TIDE_CHECK(has(added, "    GUILayout.TextField(\"Code\", lobby.code, out lobby.entered);\n"));
    static const char extra[] = ENTERED_VIEW "    GUILayout.TextField(\"Code\", out lobby.code);\n}\n";
    open_document(extra);
    TIDE_CHECK(has(last_sent(), "'GUILayout.TextField' changes its 'value', a 'mut' parameter: it takes the variable as it is, without 'out'"));
    actions_at(4);
    const char *removed = apply_fix(extra, "Remove 'out'");
    TIDE_REQUIRE(removed != NULL);
    TIDE_CHECK(has(removed, "    GUILayout.TextField(\"Code\", lobby.code);\n"));
    static const char typed[] = ENTERED_VIEW "    GUILayout.TextField(\"Code\", lobby.code, out int entered);\n    lobby.count = entered;\n}\n";
    open_document(typed);
    TIDE_CHECK(has(last_sent(), "'GUILayout.TextField' makes its 'entered' bool, not int"));
    actions_at(4);
    const char *takes = apply_fix(typed, "Change to 'var'");
    TIDE_REQUIRE(takes != NULL);
    TIDE_CHECK(has(takes, "    GUILayout.TextField(\"Code\", lobby.code, out var entered);\n"));
    static const char unread[] = ENTERED_VIEW "    GUILayout.TextField(\"Code\", lobby.code, out var entered);\n}\n";
    open_document(unread);
    TIDE_CHECK(has(last_sent(), "local 'entered' is never used") && has(last_sent(), "write 'out _' to leave it out"));
    actions_at(4);
    const char *left = apply_fix(unread, "Leave 'entered' out: 'out _'");
    TIDE_REQUIRE(left != NULL);
    TIDE_CHECK(has(left, "    GUILayout.TextField(\"Code\", lobby.code, out _);\n"));
    open_document("singleton Rules { bool entered; }\n" ENTERED_LOBBY
                  "view Join(mut Lobby lobby, Rules rules) { GUILayout.TextField(\"Code\", lobby.code, out rules.entered); }\n");
    TIDE_CHECK(has(last_sent(), "'rules' is read-only") && has(last_sent(), "'GUILayout.TextField' makes its 'entered'"));
    TIDE_CHECK(has(last_sent(), "a view can't change the match"));
    open_document(ENTERED_VIEW "    GUILayout.TextField(\"Code\", mut lobby.code);\n}\n");
    TIDE_CHECK(has(last_sent(), "an argument takes no 'mut': a 'mut' parameter changes the variable it's given as it is"));
    TIDE_CHECK(has(last_sent(), "remove 'mut'; only 'out' is written at a call"));
}

// Inlay hints and semantic tokens around `out`.
TIDE_TEST(lsp_out_hints_and_tokens)
{
    start();
    open_document(OUT_EVERY);
    const char *hints = all_hints();
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":22,\"character\":31},\"label\":\": int\",\"kind\":1,\"paddingLeft\":true}"));
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":25,\"character\":38},\"label\":\": int\",\"kind\":1"));
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":29,\"character\":41},\"label\":\": int\",\"kind\":1"));
    TIDE_CHECK(count(hints, "\"kind\":1") == 3);
    TIDE_CHECK(has(hints, "{\"position\":{\"line\":22,\"character\":16},\"label\":\"value:\",\"kind\":2,\"paddingRight\":true}"));
    TIDE_CHECK(count(hints, "\"label\":\"value:\"") == 3 && count(hints, "\"kind\":2") == 3);
    TIDE_CHECK(!has(hints, "\"label\":\"half:\"") && !has(hints, "\"label\":\"found:\""));

    // The word is a keyword (11) where a parameter starts and before an argument; the local an argument declares is a
    // variable (6), declared (1) and read-only (2) unless it says `mut`; `_` is nothing
    open_document("bool F(out int a) { a = 1; return true; }\nscene Main { }\n"
                  "system S() { F(out var x); F(out mut var y); F(out _); }\n");
    const char *tokens = request("textDocument/semanticTokens/full");
    // Line 0: bool, F, out, int, a (a parameter, 5, declared and not read-only), and its use
    TIDE_CHECK(has(tokens, "\"data\":[0,0,4,11,0,0,5,1,9,1,0,2,3,11,0,0,4,3,11,0,0,4,1,5,1,0,5,1,5,0,"));
    // Line 2, from S: F, out, x; F, out, y; F, out
    TIDE_CHECK(has(tokens, "1,7,1,9,1,0,6,1,9,0,0,2,3,11,0,0,8,1,6,3,0,4,1,9,0,0,2,3,11,0,0,12,1,6,1,0,4,1,9,0,0,2,3,11,0]"));
    open_document("scene Main { }\nint Twice(int out) { return out * 2; }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/semanticTokens/full"), "1,0,3,11,0,0,4,5,9,1,0,6,3,11,0,0,4,3,5,3,0,14,3,5,2]"));
}

// The formatter keeps an argument's `out` a space from what follows, and a map's types tight.
TIDE_TEST(lsp_format_out)
{
    start();
    static const char messy[] = "bool TryHalf(int value ,out   int half)\n{\nhalf=value/2;return true;\n}\n"
                                "bool Find(Map<int,float> scores,out List< int > found,out   int? best,out Map< int ,float > copy)\n"
                                "{\nfound=[];best=null;copy=scores;return scores.TryGet(1,out  _);\n}\n"
                                "singleton Tally { int total; Map < int,float > scores; Set < int > seen; }\nscene Main { }\n"
                                "system Run( mut Tally tally )\n{\n"
                                "if (TryHalf( 8 ,out   var half )) tally.total=half;\n"
                                "TryHalf(8,out mut  int typed);\ntyped+=1;\n"
                                "TryHalf(typed , out _ );\n"
                                "TryHalf(value : 8,half :out  var named);\n"
                                "if(tally.scores.TryGet(named,out  var score)&&score>1) tally.total=named;\n"
                                "TryHalf(8,\nout tally . total);\n}\n";
    static const char expected[] = "bool TryHalf(int value, out int half)\n{\n    half = value / 2; return true;\n}\n"
                                   "bool Find(Map<int, float> scores, out List<int> found, out int? best, out Map<int, float> copy)\n"
                                   "{\n    found = []; best = null; copy = scores; return scores.TryGet(1, out _);\n}\n"
                                   "singleton Tally { int total; Map<int, float> scores; Set<int> seen; }\nscene Main { }\n"
                                   "system Run(mut Tally tally)\n{\n"
                                   "    if (TryHalf(8, out var half)) tally.total = half;\n"
                                   "    TryHalf(8, out mut int typed);\n    typed += 1;\n"
                                   "    TryHalf(typed, out _);\n"
                                   "    TryHalf(value: 8, half: out var named);\n"
                                   "    if (tally.scores.TryGet(named, out var score) && score > 1) tally.total = named;\n"
                                   "    TryHalf(8,\n        out tally.total);\n}\n";
    format_reply(messy);
    const char *formatted = apply_reply(messy, NULL);
    TIDE_CHECK(strcmp(formatted, expected) == 0);
    if (strcmp(formatted, expected) != 0) printf("--- got:\n%s---\n", formatted);
    TIDE_CHECK(has(format_reply(expected), "\"result\":[]"));
    TIDE_CHECK(has(format_reply(OUT_EVERY), "\"result\":[]"));
    TIDE_CHECK(has(last_sent(), "\"result\":[]"));

    static const char lines[] = "bool TryHalf(int value, out int half) { half = value / 2; return true; }\nscene Main { }\n"
                                "system S()\n{\nTryHalf(8,out  var a);\nTryHalf(a ,out   _);\n}\n";
    open_document(lines);
    request_with("textDocument/rangeFormatting", "\"range\":{\"start\":{\"line\":5,\"character\":0},\"end\":"
                                                 "{\"line\":6,\"character\":0}},\"options\":{\"tabSize\":4,\"insertSpaces\":true}");
    const char *ranged = apply_reply(lines, NULL);
    TIDE_CHECK(has(ranged, "{\nTryHalf(8,out  var a);\n    TryHalf(a, out _);\n}\n"));
}

// Quick fixes around `out`: the word missing or misplaced, a local nothing reads, and one the code changes.
TIDE_TEST(lsp_out_quick_fixes)
{
    start();
    static const char missing[] = OUT_RUN "    TryHalf(8, tally.total);\n    tally.scores.TryGet(1, tally.stats.power);\n}\n";
    open_document(missing);
    const char *add = actions_at(22);
    TIDE_CHECK(has(add, "\"title\":\"Add 'out'\",\"kind\":\"quickfix\",\"isPreferred\":true,\"diagnostics\":[{"));
    TIDE_CHECK(has(add, "{\"range\":{\"start\":{\"line\":22,\"character\":15},\"end\":{\"line\":22,\"character\":15}},\"newText\":\"out \"}"));
    const char *added = apply_fix(missing, "Add 'out'");
    TIDE_REQUIRE(added != NULL);
    TIDE_CHECK(has(added, "    TryHalf(8, out tally.total);\n"));
    actions_at(23);
    const char *got = apply_fix(missing, "Add 'out'");
    TIDE_REQUIRE(got != NULL);
    TIDE_CHECK(has(got, "    tally.scores.TryGet(1, out tally.stats.power);\n"));

    static const char extra[] = OUT_RUN "    Use(out tally.total);\n    tally.total = Math.Max(out  tally.total, 1);\n}\n";
    open_document(extra);
    TIDE_CHECK(has(last_sent(), "'Use' takes a value for 'value', which isn't an 'out' parameter"));
    const char *remove = actions_at(22);
    TIDE_CHECK(has(remove, "\"title\":\"Remove 'out'\",\"kind\":\"quickfix\",\"isPreferred\":true,\"diagnostics\":[{"));
    const char *removed = apply_fix(extra, "Remove 'out'");
    TIDE_REQUIRE(removed != NULL);
    TIDE_CHECK(has(removed, "    Use(tally.total);\n"));
    actions_at(23);
    const char *plain = apply_fix(extra, "Remove 'out'");
    TIDE_REQUIRE(plain != NULL);
    TIDE_CHECK(has(plain, "    tally.total = Math.Max(tally.total, 1);\n"));
    TIDE_CHECK(!has(actions_at(21), "Remove 'out'"));

    static const char unknown[] = OUT_RUN "    Shrink(8, out tally.total);\n    Other(out _);\n}\n";
    open_document(unknown);
    TIDE_CHECK(has(last_sent(), "unknown function 'Shrink'") && has(last_sent(), "unknown function 'Other'"));
    TIDE_CHECK(!has(last_sent(), "nothing here takes one") && !has(last_sent(), "unknown name '_'"));
    const char *create = actions_at(22);
    TIDE_CHECK(!has(create, "Remove 'out'"));
    TIDE_CHECK(has(create, "Create function 'Shrink'"));
    TIDE_CHECK(has(create, "void Shrink(int value, out int total)\\n{\\n    total = default;\\n}\\n"));

    static const char both[] = "bool TryHalf(int value, mut out int half)\n{\n    half = value / 2;\n    return true;\n}\nscene Main { }\n";
    open_document(both);
    actions_at(0);
    const char *dropped = apply_fix(both, "Change to 'out'");
    TIDE_REQUIRE(dropped != NULL);
    TIDE_CHECK(has(dropped, "bool TryHalf(int value, out int half)\n"));
    static const char engine[] = "singleton Store { int size; }\nsystem Run(out Store store) { store.size = 1; }\nscene Main { }\n";
    open_document(engine);
    actions_at(1);
    const char *taken = apply_fix(engine, "Change to 'mut'");
    TIDE_REQUIRE(taken != NULL);
    TIDE_CHECK(has(taken, "system Run(mut Store store) { store.size = 1; }\n"));

    static const char again[] = OUT_RUN "    mut var half = 0;\n    TryHalf(8, out var half);\n    tally.total = half;\n}\n";
    open_document(again);
    actions_at(23);
    const char *assigns = apply_fix(again, "Change to 'half'");
    TIDE_REQUIRE(assigns != NULL);
    TIDE_CHECK(has(assigns, "    TryHalf(8, out half);\n"));
    static const char wrong_type[] = OUT_RUN "    TryHalf(8, out mut float half);\n    tally.total = int(half);\n}\n";
    open_document(wrong_type);
    actions_at(22);
    const char *takes = apply_fix(wrong_type, "Change to 'var'");
    TIDE_REQUIRE(takes != NULL);
    TIDE_CHECK(has(takes, "    TryHalf(8, out mut var half);\n"));

    static const char unassigned[] = "bool TryHalf(int value, out int half)\n{\n    if (value % 2 != 0) return false;\n"
                                     "    if (value < 0)\n    {\n        return false;\n    }\n    half = value / 2;\n"
                                     "    return true;\n}\nvoid Fill(int n, out int count)\n{\n    if (n > 0) count = n;\n}\n"
                                     "scene Main { }\n";
    open_document(unassigned);
    TIDE_CHECK(has(last_sent(), "'TryHalf' returns here without assigning its 'half'"));
    actions_at(2);
    const char *braced = apply_fix(unassigned, "Assign 'half = default;'");
    TIDE_REQUIRE(braced != NULL);
    TIDE_CHECK(has(braced, "    if (value % 2 != 0) { half = default; return false; }\n    if (value < 0)\n"));
    actions_at(5);
    const char *filled = apply_fix(unassigned, "Assign 'half = default;'");
    TIDE_REQUIRE(filled != NULL);
    TIDE_CHECK(has(filled, "    {\n        half = default;\n        return false;\n    }\n"));
    actions_at(10);
    const char *ended = apply_fix(unassigned, "Assign 'count = default;'");
    TIDE_REQUIRE(ended != NULL);
    TIDE_CHECK(has(ended, "    if (n > 0) count = n;\n    count = default;\n}\n"));

    static const char early[] = "singleton Store { List<int> values; int left; }\n"
                                "void Fill(int n, out List<int> values)\n{\n    values.Add(n);\n}\n"
                                "bool Left(int size, out int left) { left += size; return left > 0; }\n"
                                "system Run(mut Store store)\n{\n    Fill(3, out store.values);\n    Fill(n: 4, values: out store.values);\n"
                                "    if (Left(2, out var left)) store.left = left;\n}\n"
                                "scene Main { }\n";
    open_document(early);
    TIDE_CHECK(has(last_sent(), "'values' is read before it's assigned") && has(last_sent(), "'left' is read before it's assigned"));
    const char *reads = actions_at(3);
    TIDE_CHECK(has(reads, "\"title\":\"Assign 'values = default;' first\",\"kind\":\"quickfix\",\"isPreferred\":true,\"diagnostics\":[{"));
    TIDE_CHECK(has(reads, "\"title\":\"Take 'values' as 'mut', at its calls too\",\"kind\":\"quickfix\",\"isPreferred\":false"));
    const char *starts = apply_fix(early, "Assign 'values = default;' first");
    TIDE_REQUIRE(starts != NULL);
    TIDE_CHECK(has(starts, "void Fill(int n, out List<int> values)\n{\n    values = default;\n    values.Add(n);\n}\n"));
    const char *changes = apply_fix(early, "Take 'values' as 'mut', at its calls too");
    TIDE_REQUIRE(changes != NULL);
    TIDE_CHECK(has(changes, "void Fill(int n, mut List<int> values)\n"));
    TIDE_CHECK(has(changes, "    Fill(3, store.values);\n    Fill(n: 4, values: store.values);\n"));
    open_document(changes);
    TIDE_CHECK(!has(last_sent(), "'values'") && !has(last_sent(), "'Fill'"));
    open_document(early);
    const char *one_line = actions_at(5);
    TIDE_CHECK(!has(one_line, "Take 'left' as 'mut'"));
    const char *inline_first = apply_fix(early, "Assign 'left = default;' first");
    TIDE_REQUIRE(inline_first != NULL);
    TIDE_CHECK(has(inline_first, "bool Left(int size, out int left) { left = default; left += size; return left > 0; }\n"));

    static const char unread[] = OUT_RUN "    TryHalf(8, out var rest);\n    TryHalf(8, out mut int spare);\n"
                                         "    tally.all.TryGet(1, out Stats found);\n    tally.total = 1;\n}\n";
    open_document(unread);
    TIDE_CHECK(has(last_sent(), "\"message\":\"local 'rest' is never used\\nnote: nothing reads what the call makes there: write "
                                "'out _' to leave it out\",\"tags\":[1]"));
    TIDE_CHECK(has(last_sent(), "\"range\":{\"start\":{\"line\":22,\"character\":23},\"end\":{\"line\":22,\"character\":27}}"));
    TIDE_CHECK(count(last_sent(), "\"tags\":[1]") == 3 && count(last_sent(), "\"severity\":2") == 3);
    const char *leave = actions_at(22);
    TIDE_CHECK(has(leave, "\"title\":\"Leave 'rest' out: 'out _'\",\"kind\":\"quickfix\",\"isPreferred\":true,\"diagnostics\":[{"));
    TIDE_CHECK(!has(leave, "Remove 'rest'") && !has(leave, "Keep the call"));
    const char *left = apply_fix(unread, "Leave 'rest' out: 'out _'");
    TIDE_REQUIRE(left != NULL);
    TIDE_CHECK(has(left, "{\n    TryHalf(8, out _);\n    TryHalf(8, out mut int spare);\n"));
    actions_at(23);
    const char *spared = apply_fix(unread, "Leave 'spare' out: 'out _'");
    TIDE_REQUIRE(spared != NULL);
    TIDE_CHECK(has(spared, "    TryHalf(8, out _);\n    tally.all.TryGet(1, out Stats found);\n"));
    actions_at(24);
    const char *lost = apply_fix(unread, "Leave 'found' out: 'out _'");
    TIDE_REQUIRE(lost != NULL);
    TIDE_CHECK(has(lost, "    tally.all.TryGet(1, out _);\n    tally.total = 1;\n}\n"));
    open_document(OUT_RUN "    TryHalf(8, out _);\n    TryHalf(8, out _);\n    tally.all.TryGet(1, out _);\n    tally.total = 1;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    static const char changed[] = OUT_RUN "    TryHalf(8, out var half);\n    half += 1;\n    TryHalf(8, out int typed);\n"
                                          "    TryHalf(half, out typed);\n    tally.total = half + typed;\n}\n";
    open_document(changed);
    TIDE_CHECK(has(last_sent(), "'half' is read-only\\nnote: declare it as 'out mut var half' to change it after the call that makes it"));
    TIDE_CHECK(has(last_sent(), "declare it as 'out mut int typed' to change it"));
    const char *declare = actions_at(23);
    TIDE_CHECK(has(declare, "Declare 'half' as mut") && has(declare, "\"start\":{\"line\":22,\"character\":19}"));
    const char *mutable = apply_fix(changed, "Declare 'half' as mut");
    TIDE_REQUIRE(mutable != NULL);
    TIDE_CHECK(has(mutable, "    TryHalf(8, out mut var half);\n    half += 1;\n"));
    actions_at(25);
    const char *typed = apply_fix(changed, "Declare 'typed' as mut");
    TIDE_REQUIRE(typed != NULL);
    TIDE_CHECK(has(typed, "    TryHalf(8, out mut int typed);\n    TryHalf(half, out typed);\n"));
}

// What holds an `out` argument, as selections grow, and the calls that say it.
TIDE_TEST(lsp_out_selection_and_calls)
{
    start();
    open_document(OUT_EVERY);
    const char *ranges = request_at("file:///test.tide", "textDocument/selectionRange", 0, 0,
                                    "\"positions\":[{\"line\":22,\"character\":28}]");
    TIDE_CHECK(has(ranges, "\"result\":[{\"range\":{\"start\":{\"line\":22,\"character\":27},\"end\":{\"line\":22,\"character\":31}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":22,\"character\":19},\"end\":{\"line\":22,\"character\":31}},"
                           "\"parent\":{\"range\":{\"start\":{\"line\":22,\"character\":8},\"end\":{\"line\":22,\"character\":32}},"));
    const char *blank = request_at("file:///test.tide", "textDocument/selectionRange", 0, 0,
                                   "\"positions\":[{\"line\":28,\"character\":19}]");
    TIDE_CHECK(has(blank, "\"result\":[{\"range\":{\"start\":{\"line\":28,\"character\":19},\"end\":{\"line\":28,\"character\":20}},"
                          "\"parent\":{\"range\":{\"start\":{\"line\":28,\"character\":15},\"end\":{\"line\":28,\"character\":20}},"
                          "\"parent\":{\"range\":{\"start\":{\"line\":28,\"character\":4},\"end\":{\"line\":28,\"character\":21}},"));
    const char *field = request_at("file:///test.tide", "textDocument/selectionRange", 0, 0,
                                   "\"positions\":[{\"line\":27,\"character\":32}]");
    TIDE_CHECK(has(field, "\"parent\":{\"range\":{\"start\":{\"line\":27,\"character\":21},\"end\":{\"line\":27,\"character\":36}},"));

    const char *item = request_at("file:///test.tide", "textDocument/prepareCallHierarchy", 22, 10, "");
    TIDE_CHECK(has(item, "{\"name\":\"TryHalf\",\"kind\":12,\"detail\":\"bool TryHalf(int value, out int half)\""));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"callHierarchy/incomingCalls\",\"params\":{\"item\":{\"name\":\"TryHalf\","
           "\"kind\":12,\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":11,\"character\":0},\"end\":"
           "{\"line\":15,\"character\":1}},\"selectionRange\":{\"start\":{\"line\":11,\"character\":5},\"end\":"
           "{\"line\":11,\"character\":12}}}}}");
    TIDE_CHECK(has(last_sent(), "{\"from\":{\"name\":\"Run\",\"kind\":12") && count(last_sent(), "\"from\":") == 1);
    TIDE_CHECK(has(last_sent(), "\"fromRanges\":[{\"start\":{\"line\":22,\"character\":8},\"end\":{\"line\":22,\"character\":15}}"));
    TIDE_CHECK(count(last_sent(), "\"character\":4},\"end\":{\"line\":2") == 5);
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"callHierarchy/outgoingCalls\",\"params\":{\"item\":{\"name\":\"Run\","
           "\"kind\":12,\"uri\":\"file:///test.tide\",\"range\":{\"start\":{\"line\":20,\"character\":0},\"end\":"
           "{\"line\":32,\"character\":1}},\"selectionRange\":{\"start\":{\"line\":20,\"character\":7},\"end\":"
           "{\"line\":20,\"character\":10}}}}}");
    TIDE_CHECK(has(last_sent(), "{\"to\":{\"name\":\"TryHalf\"") && has(last_sent(), "{\"to\":{\"name\":\"TryPower\",\"kind\":6"));
    TIDE_CHECK(count(last_sent(), "\"to\":") == 2);

    TIDE_CHECK(has(request("textDocument/foldingRange"), "{\"startLine\":11,\"endLine\":14}"));
    const char *move = request_at("file:///test.tide", "textDocument/codeAction", 12, 0,
                                  "\"range\":{\"start\":{\"line\":12,\"character\":0},\"end\":{\"line\":12,\"character\":0}}");
    TIDE_CHECK(has(move, "\"title\":\"Move 'TryHalf' to TryHalf.tide\",\"kind\":\"refactor.move\""));
    TIDE_CHECK(has(move, "\"newText\":\"bool TryHalf(int value, out int half)\\n{\\n    half = value / 2;\\n"
                         "    return value % 2 == 0;\\n}\\n\""));

    open_document(OUT_PARAMS "singleton Seen { int total; }\n"
                             "system Halve(mut Tally tally) { TryHalf(8, out tally.total); }\n"
                             "system Read(Tally tally, mut Seen seen) { seen.total = tally.total; }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/codeLens"), "after Halve: it writes Tally, which this reads"));
}

// The World in a function: its caller's, passed on; what it reaches shows as the system's own, and world.TryGet makes its variable.
#define WORLD_FUNCTION                                                         \
    "component Health { int value = 10; }\n"                                   \
    "component Healer { Entity patient; int seen; }\n"                         \
    "bool TryFind(Entity target, World world, out Health health)\n"            \
    "{\n"                                                                      \
    "    return world.TryGet<Health>(target, out health);\n"                   \
    "}\n"                                                                      \
    "scene Main { }\n"                                                         \
    "event(Spawned) Setup(with Main) { Spawn(Health { }); Spawn(Healer { }); }\n"

// A function sends, spawns and changes entities as its caller could: the editor offers them, and the error is at a call from code that can't.
// Web.Request's types are Tide's own code, a file of the game's while the game names them
TIDE_TEST(lsp_web_requests)
{
    start();
#define TASK "local scene Main { }\nasync void Go()\n{\n"
    const char *web = complete(TASK "    Web.$\n}\n");
    TIDE_CHECK(offers(web, "Request") && offers(web, "Fetch"));
    const char *built = complete(TASK "    var request = Web.Request(\"http://example.com\");\n    request.$\n}\n");
    TIDE_CHECK(offers(built, "SetMethod") && offers(built, "SetHeader") && offers(built, "SetBody") && offers(built, "url"));
    TIDE_CHECK(offers(built, "SetTimeout") && offers(built, "timeout"));
    TIDE_CHECK(has(built, "WebRequest SetTimeout(float seconds)"));
    TIDE_CHECK(offers(complete(TASK "    var request = Web.Request(\"http://example.com\");\n    var late = IOError.$\n}\n"), "TimedOut"));
    const char *plain = complete(TASK "    var late = IOError.$\n}\n");
    TIDE_CHECK(offers(plain, "Failed") && !offers(plain, "TimedOut"));
    open_document(TASK "    var answer = await Web.Request(\"http://example.com\").SetTimeout(5).Send();\n"
                       "    if (answer is IOError.TimedOut) return;\n}\nview Start() { Go(); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(hover_at(3, 60), "WebRequest SetTimeout(float seconds)"));
    const char *answer = complete(TASK "    var answer = await Web.Request(\"http://example.com\").Send() ?? WebResponse { };\n"
                                       "    answer.$\n}\n");
    TIDE_CHECK(offers(answer, "status") && offers(answer, "body"));
    open_document(TASK "    Web.Request(\"http://example.com\").Send();\n}\nview Start() { Go(); }\n");
    TIDE_CHECK(has(last_sent(), "Send is awaited"));
    open_document("local scene Main { }\nview Start() { }\n");
    TIDE_CHECK(!has(last_sent(), "web.tide"));
#undef TASK
}

// Json's calls, string's own and the numbers out of text: completion, hovers, and use of the type a read names.
TIDE_TEST(lsp_json_and_text_primitives)
{
    start();
#define JSON_TYPES                                                                                              \
    "struct Score { string name; [Json(\"max_players\")] int most; }\nsingleton Seen { string text; int n; }\n" \
    "scene Main { }\n"
    const char *json = complete(JSON_TYPES "system Run(mut Seen seen)\n{\n    var text = Json.$\n}\n");
    TIDE_CHECK(offers(json, "Read") && offers(json, "Write") && has(json, "Json.Read<T>(string json) -> T?"));
    TIDE_CHECK(offers(complete(JSON_TYPES "system Run(mut Seen seen)\n{\n    var text = $\n}\n"), "Json"));
    const char *own = complete(JSON_TYPES "system Run(mut Seen seen)\n{\n    var text = string.$\n}\n");
    TIDE_CHECK(offers(own, "FromBytes") && !offers(own, "Trim"));
    const char *text = complete(JSON_TYPES "system Run(mut Seen seen)\n{\n    var pieces = seen.text.$\n}\n");
    TIDE_CHECK(offers(text, "ToBytes") && offers(text, "Split") && offers(text, "Trim") && !offers(text, "FromBytes"));
    TIDE_CHECK(offers(complete(JSON_TYPES "system Run(mut Seen seen)\n{\n    var ok = int.$\n}\n"), "TryParse"));
    TIDE_CHECK(offers(complete(JSON_TYPES "system Run(mut Seen seen)\n{\n    var ok = float.$\n}\n"), "TryParse"));
    TIDE_CHECK(offers(complete("struct Score\n{\n    [$] int most;\n}\nscene Main { }\n"), "Json"));

    open_document(JSON_TYPES "system Run(mut Seen seen)\n{\n"
                             "    var score = Json.Read<Score>(seen.text) ?? Score { };\n"
                             "    var scores = Json.Read<List<Score>>(seen.text.ToBytes()) ?? [];\n"
                             "    seen.text = Json.Write(score) + (string.FromBytes(seen.text.ToBytes()) ?? \"\");\n"
                             "    if (int.TryParse(seen.text, out var n) && float.TryParse(seen.text, out var f)) seen.n = n + int(f);\n"
                             "    seen.n += seen.text.Split(\",\").count + scores.count;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *read = hover_at(5, 23);
    TIDE_CHECK(has(read, "Json.Read<T>(string json) -> T?") && has(read, "Json.Read<T>(List<byte> json) -> T?"));
    TIDE_CHECK(has(read, "A key the JSON leaves out keeps its field's default"));
    TIDE_CHECK(has(hover_at(5, 17), "JSON to and from the game's own structs"));
    TIDE_CHECK(has(hover_at(5, 28), "struct Score"));
    TIDE_CHECK(has(hover_at(6, 34), "struct Score"));
    TIDE_CHECK(has(hover_at(5, 9), "Score score"));
    TIDE_CHECK(has(hover_at(7, 23), "Json.Write(T value) -> string"));
    const char *from = hover_at(7, 45);
    TIDE_CHECK(has(from, "string.FromBytes(List<byte> bytes) -> string?") && has(from, "or nothing when they aren't"));
    TIDE_CHECK(has(hover_at(7, 66), "string.ToBytes() -> List<byte>"));
    const char *parse = hover_at(8, 14);
    TIDE_CHECK(has(parse, "int.TryParse(string text, out int value) -> bool") && has(parse, "Whether the whole text is an int"));
    TIDE_CHECK(has(hover_at(8, 54), "float.TryParse(string text, out float value) -> bool"));
    TIDE_CHECK(has(hover_at(8, 40), "int n"));
    const char *split = hover_at(9, 24);
    TIDE_CHECK(has(split, "string.Split(string separator) -> List<string>") && has(split, "string.Split(char separator) -> List<string>"));
    TIDE_CHECK(has(hover_at(0, 30), "[Json(\\\"key\\\")]") && has(hover_at(0, 30), "This field's key in JSON"));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 0, 8, "\"context\":{\"includeDeclaration\":true}"),
                     "\"uri\"") == 4);

    open_document(JSON_TYPES "system Run(mut Seen seen)\n{\n    var score = Json.Read<Score>($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Json.Read<T>(string json) -> T?"));
    open_document(JSON_TYPES "system Run(mut Seen seen)\n{\n    var scores = Json.Read<List<Score>>($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Json.Read<T>(List<byte> json) -> T?"));
    open_document(JSON_TYPES "system Run(mut Seen seen)\n{\n    seen.text = Json.Write($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "Json.Write(T value) -> string"));
    open_document(JSON_TYPES "system Run(mut Seen seen)\n{\n    var text = string.FromBytes($\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "string.FromBytes(List<byte> bytes) -> string?"));
    open_document(JSON_TYPES "system Run(mut Seen seen)\n{\n    var ok = long.TryParse(seen.text, $\n}\n");
    const char *help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "long.TryParse(string text, out long value) -> bool") && has(help, "\"activeParameter\":1"));

    open_document(JSON_TYPES "struct Held { Entity owner; }\nsystem Run(mut Seen seen)\n{\n    seen.text = Json.Write(Held { });\n}\n");
    TIDE_CHECK(has(last_sent(), "JSON has nothing for Entity, which 'Held.owner' holds"));
#undef JSON_TYPES
}

// Fields declared T?: read as the T? they are, in hovers, hints and completions, and what's wrong said where it's written.
TIDE_TEST(lsp_optional_fields)
{
    start();
#define OPTIONAL_TYPES                                                                                      \
    "struct Slot { int? item; string? label; }\nsingleton Bag { Slot slot; int? best; List<int>? picks; }\n" \
    "scene Main { }\n"
    open_document(OPTIONAL_TYPES "system Run(mut Bag bag)\n{\n"
                                 "    var kept = bag.best;\n"
                                 "    var item = bag.slot.item ?? 0;\n"
                                 "    bag.best = item;\n"
                                 "    bag.slot.label = null;\n"
                                 "    if (bag.best is int found) bag.best = found + 1;\n"
                                 "    bag.slot = Slot { item = kept ?? 1 };\n"
                                 "    bag.picks = [item];\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(hover_at(5, 19), "int? best"));
    TIDE_CHECK(has(hover_at(7, 9), "int? best"));
    TIDE_CHECK(has(hover_at(1, 33), "int? best"));
    TIDE_CHECK(has(hover_at(8, 14), "string? label"));
    TIDE_CHECK(has(hover_at(0, 19), "int? item"));
    TIDE_CHECK(has(hover_at(5, 9), "int? kept"));
    TIDE_CHECK(has(hover_at(6, 9), "int item"));
    TIDE_CHECK(has(hover_at(9, 26), "int found"));
    TIDE_CHECK(has(hover_at(10, 22), "int? item"));
    TIDE_CHECK(has(hover_at(11, 9), "List<int>? picks"));
    const char *hints = all_hints();
    TIDE_CHECK(has(hints, "\": int?\"") && has(hints, "\": int\""));
    TIDE_CHECK(count(request_at("file:///test.tide", "textDocument/references", 1, 33, "\"context\":{\"includeDeclaration\":true}"),
                     "\"uri\"") == 5);
    const char *renamed = rename_at(1, 33, "top");
    TIDE_CHECK(count(renamed, "\"newText\":\"top\"") == 5);

    const char *fields = complete(OPTIONAL_TYPES "system Run(mut Bag bag)\n{\n    bag.$\n}\n");
    TIDE_CHECK(offers(fields, "best") && offers(fields, "slot") && offers(fields, "picks") && has(fields, "int?") && has(fields, "List<int>?"));
    const char *inner = complete(OPTIONAL_TYPES "system Run(mut Bag bag)\n{\n    bag.slot.$\n}\n");
    TIDE_CHECK(offers(inner, "item") && offers(inner, "label") && has(inner, "string?"));
    const char *literal = complete(OPTIONAL_TYPES "system Run(mut Bag bag)\n{\n    bag.slot = Slot { item = 1, $ };\n}\n");
    TIDE_CHECK(offers(literal, "label") && has(literal, "string?"));
    const char *wrapped = complete(OPTIONAL_TYPES "system Run(mut Bag bag)\n{\n    var n = bag.slot.label.$\n}\n");
    TIDE_CHECK(!offers(wrapped, "length") && !offers(wrapped, "value") && !offers(wrapped, "ok"));
    const char *unwrapped = complete(OPTIONAL_TYPES "system Run(mut Bag bag)\n{\n    var label = bag.slot.label ?? \"\";\n    var n = label.$\n}\n");
    TIDE_CHECK(offers(unwrapped, "length"));
    TIDE_CHECK(offers(complete("struct Slot { int? item; }\nstruct Bag\n{\n    Sl$? slot;\n}\nscene Main { }\n"), "Slot"));

    const char *used = OPTIONAL_TYPES "system Run(mut Bag bag)\n{\n    var next = bag.best + 1;\n}\n";
    open_document(used);
    TIDE_CHECK(has(last_sent(), "'best' is int?, a value or nothing, so it needs unwrapping first"));
    actions_at(5);
    const char *fixed = apply_fix(used, "Carry on with the default if there's none: add '!'");
    if (!fixed) printf("    actions: %s\n", last_sent());
    TIDE_CHECK(fixed && has(fixed, "var next = bag.best! + 1;"));
    open_document(OPTIONAL_TYPES "void Bump(mut int? value) { value = 1; }\nsystem Run(mut Bag bag)\n{\n    Bump(bag.best);\n}\n");
    TIDE_CHECK(has(last_sent(), "a T? field can't be passed as 'mut'"));
    open_document("component Targets { List<int?> ids; int?? twice; int?[4] cells; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "what a List holds is always there, so it can't be a T?"));
    open_document("component Board { Grid2<int>? cells; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "a field can't be a grid or nothing"));
    TIDE_CHECK(has(format_reply("struct Slot{int ?item;string? label=null;}\nscene Main { }\n"), "int? item;") );
#undef OPTIONAL_TYPES
}

TIDE_TEST(lsp_functions_send_and_spawn)
{
    start();
#define ACTS GAME_TYPES "event Hit { int damage; }\nlocal event Blink { int n; }\n"
    const char *code = complete(ACTS "void Wave()\n{\n    $\n}\n");
    TIDE_CHECK(offers(code, "Spawn") && offers(code, "Send") && offers(code, "Hit") && offers(code, "Blink"));
    TIDE_CHECK(!offers(code, "Scene"));
    const char *dot = complete(ACTS "void Kill(Entity target)\n{\n    target.$\n}\n");
    TIDE_CHECK(offers(dot, "Destroy") && offers(dot, "Add") && offers(dot, "Remove") && offers(dot, "Send") && !offers(dot, "Snap"));
    const char *method = complete(ACTS "struct Bell\n{\n    int tone;\n    void Ring(LocalEntity target)\n    {\n        target.$\n    }\n}\n");
    TIDE_CHECK(offers(method, "Destroy") && offers(method, "Send"));
    const char *op = complete(ACTS "struct Money\n{\n    int cents;\n    Money operator +(Money a, Money b)\n    {\n        $\n"
                                   "        return a;\n    }\n}\n");
    TIDE_CHECK(!offers(op, "Spawn") && !offers(op, "Send"));

    open_document(ACTS "void Poke(Entity target) { target.Send(Hit { damage = 1 }); }\n"
                       "event(Hit hit) Take(mut Body body) { body.radius -= hit.damage; }\n"
                       "system Fight(Body body) { if (body.radius > 20) Poke(this); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document(ACTS "void Poke(Entity target) { target.Send(Hit { damage = 1 }); }\n"
                       "event(Hit hit) Take(mut Body body) { body.radius -= hit.damage; }\n"
                       "system Fight(Body body) { if (body.radius > 20) Poke(this); }\n"
                       "view Show(Body body) { if (body.radius > 20) Poke(this); }\n");
    TIDE_CHECK(has(last_sent(), "a view can't change the match, and 'Poke' sends 'Hit'"));
    const char *session = complete(ACTS "void Host()\n{\n    Session.$\n}\n");
    TIDE_CHECK(offers(session, "Open") && offers(session, "Join") && offers(session, "Start"));
    const char *clipboard = complete(ACTS "void Share()\n{\n    Clipboard.$\n}\n");
    TIDE_CHECK(offers(clipboard, "Copy"));
    const char *op_session = complete(ACTS "struct Money\n{\n    int cents;\n    Money operator +(Money a, Money b)\n    {\n"
                                           "        Session.$\n        return a;\n    }\n}\n");
    TIDE_CHECK(!offers(op_session, "Open"));
    open_document(ACTS "void Host() { Session.Open(); }\nsystem Fight(Body body) { if (body.radius > 20) Host(); }\n");
    TIDE_CHECK(has(last_sent(), "'Host' calls Session.Open, and the match runs the same on every machine"));
#define LATER "async void Later(int n)\n{\n    await Wait.Ticks(n);\n}\n"
    TIDE_CHECK(offers(complete(ACTS LATER "void Soon()\n{\n    $\n}\n"), "Later"));
    open_document(ACTS LATER "void Soon() { Later(2); }\nsystem Fight(Body body) { if (body.radius > 20) Soon(); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document(ACTS LATER "void Soon() { Later(2); }\nsystem Fight(Body body) { if (body.radius > 20) Soon(); }\n"
                             "view Show(Body body) { if (body.radius > 20) Soon(); }\n");
    TIDE_CHECK(has(last_sent(), "a view can't change the match, and 'Soon' starts 'Later', a task that changes the match"));
#undef LATER
#undef ACTS
}

TIDE_TEST(lsp_world_in_functions)
{
    start();
    open_document(WORLD_FUNCTION "system Look(mut Healer healer, World world)\n{\n"
                                 "    if (TryFind(healer.patient, world, out var health)) healer.seen = health.value;\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(hover_at(8, 38), "Through its `World`, it reads any entity's `Health`."));
    const char *own = hover_at(2, 36);
    TIDE_CHECK(has(own, "The World: any entity's components by its handle") && has(own, "`TryGet<T>(entity, out value)`"));
    TIDE_CHECK(has(own, "Through its `World`, it reads any entity's `Health`."));
    TIDE_CHECK(has(hover_at(4, 19), "World.TryGet<Health>(Entity entity, out Health value) -> bool"));
    TIDE_CHECK(has(hover_at(10, 49), "Health health") && has(hover_at(10, 49), "An `out` argument declares it"));

    TIDE_CHECK(offers(complete(WORLD_FUNCTION "bool Alive(Entity target, $) { return true; }\n"), "World"));
    TIDE_CHECK(!offers(complete(WORLD_FUNCTION "async void Watch(Entity target, $) { await Wait.Ticks(1); }\n"), "World"));
    const char *members = complete(WORLD_FUNCTION "bool Alive(Entity target, World world) { return world.$; }\n");
    TIDE_CHECK(offers(members, "TryGet") && offers(members, "Get") && offers(members, "Has") && offers(members, "Set"));
    TIDE_CHECK(has(members, "\"insertText\":\"TryGet<${1:Component}>(${2:entity}, out var ${3:value})\",\"insertTextFormat\":2"));

    const char *argument = complete(WORLD_FUNCTION "system Look(mut Healer healer, World world)\n{\n"
                                                   "    world.TryGet<Health>(healer.patient, $);\n}\n");
    TIDE_CHECK(offers(argument, "out var value") && offers(argument, "out") && offers(argument, "out _"));
    const char *after = complete(WORLD_FUNCTION "system Look(mut Healer healer, World world)\n{\n"
                                                "    world.TryGet<Health>(healer.patient, out $);\n}\n");
    TIDE_CHECK(offers(after, "var") && offers(after, "Health") && offers(after, "_"));
    open_document(WORLD_FUNCTION "system Look(mut Healer healer, World world)\n{\n    world.TryGet<Health>(healer.patient, $\n}\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "TryGet<Health>(Entity entity, out Health value) -> bool"));

    open_document(WORLD_FUNCTION "void Heal(Entity target, mut World world) { world.Set<Health>(target, Health { value = 1 }); }\n"
                                 "view Show(Healer healer, mut World world) { Heal(healer.patient, world); }\n");
    TIDE_CHECK(has(last_sent(), "a view reads the match, and 'Heal' changes 'Health' through its World"));
    static const char read_only[] = WORLD_FUNCTION
        "void Heal(Entity target, mut World world) { world.Set<Health>(target, Health { value = 1 }); }\n"
        "system Mend(Healer healer, World world) { Heal(healer.patient, world); }\n";
    open_document(read_only);
    TIDE_CHECK(has(last_sent(), "declare the parameter as 'mut World world' to set entities' components through it"));
    actions_at(9);
    const char *fixed = apply_fix(read_only, "Declare 'world' as mut");
    TIDE_REQUIRE(fixed != NULL);
    TIDE_CHECK(has(fixed, "system Mend(Healer healer, mut World world) { Heal(healer.patient, world); }\n"));
    // Its following read must not suggest removing mut, which would keep an entry copy.
    open_document(WORLD_FUNCTION
        "void Heal(Entity target, mut World world) { world.Set<Health>(target, Health { value = 20 }); }\n"
        "void Forward(Entity target, mut World world) { Heal(target, world); }\n"
        "event(Spawned) Both(with Main) { Spawn(Health, Healer); }\n"
        "system Mend(mut Health health, mut Healer healer, mut World world)\n{\n"
        "    Forward(this, world);\n    healer.seen = health.value;\n}\n");
    if (!has(last_sent(), "\"diagnostics\":[]")) printf("%s\n", last_sent());
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_document(WORLD_FUNCTION
        "event(Spawned) Both(with Main) { Spawn(Health, Healer); }\n"
        "system Mend(mut Health health, mut Healer healer, mut World world)\n{\n"
        "    world.Set<Health>(this, Health { value = 20 });\n    healer.seen = health.value;\n}\n");
    if (!has(last_sent(), "\"diagnostics\":[]")) printf("%s\n", last_sent());
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
}

// Typing code with `out` from scratch, and once whole, asking at every place: nothing may crash.
TIDE_TEST(lsp_out_every_prefix_is_safe)
{
    static const char program[] = OUT_EVERY
        "bool Both(int a, out int halfA, out List<int> made)\n{\n    made = [];\n    return TryHalf(a, out halfA) && halfA > 1;\n}\n"
        "system Loop(mut Tally tally)\n{\n"
        "    mut var n = 40;\n"
        "    while (TryHalf(n, out var halved) && halved > 4) n = halved;\n"
        "    for (var i = 0; TryHalf(i, out mut var h); i++) { h += 1; tally.total = h; }\n"
        "    if (tally.all.TryGet(n, out Stats found) && found.TryPower(1, out _)) tally.total = found.power;\n"
        "    switch (Both(n, out var a, out var made) ? a : made.count)\n    {\n        case 1:\n            tally.total = a;\n"
        "            break;\n    }\n"
        "    tally.scores.TryGet(key: 1, value: out var score);\n    tally.total += int(score);\n}\n";
    start();
    type_out(program, 0);
    TIDE_CHECK(has(last_sent(), "\"result\""));
    open_document(program);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    static const char *const methods[] = {"textDocument/completion", "textDocument/hover", "textDocument/signatureHelp",
                                          "textDocument/definition", "textDocument/typeDefinition",
                                          "textDocument/documentHighlight", "textDocument/prepareCallHierarchy",
                                          "textDocument/prepareRename"};
    cursor_line = cursor_character = 0;
    for (size_t n = 0; program[n]; n++) {
        for (size_t i = 0; n >= strlen(OUT_PARAMS) && i < sizeof methods / sizeof methods[0]; i++) {
            const char *reply = request(methods[i]);
            TIDE_CHECK(has(reply, "\"result\"") || has(reply, "\"error\""));
        }
        cursor_character = program[n] == '\n' ? 0 : cursor_character + 1;
        if (program[n] == '\n') cursor_line++;
    }
}

// `virtual` and `override`: a package replaces a system, function and method; hovers, usages and renames treat a replaced declaration and its override as one name.
TIDE_TEST(lsp_virtual_and_override)
{
    if (!find_game_dir()) return;
    make_folder("lsp_ov");
    make_folder("lsp_ov/game");
    make_folder("lsp_ov/fast");
    static const char main_file[] =
        "namespace Combat;\n"                                                           // 0
        "component Position { float value; }\n"                                         // 1
        "struct Stats\n{\n    float health;\n"                                          // 2, 3, 4
        "    virtual bool IsDead() { return health <= 0; }\n}\n"                        // 5, 6
        "component Unit { Stats stats; }\n"                                             // 7
        "virtual float Damage(float base) { return base; }\n"                           // 8
        "virtual system Move(mut Position position)\n{\n"                               // 9, 10
        "    position.value += Damage(1);\n}\n"                                         // 11, 12
        "system Check(Unit unit, mut Position position)\n{\n"                           // 13, 14
        "    if (unit.stats.IsDead()) position.value = 0;\n}\n"                         // 15, 16
        "[After(Move)]\n"                                                               // 17
        "system Late(mut Position position) { position.value += 1; }\n"                 // 18
        "virtual system Idle(mut Position position) { position.value -= 1; }\n"         // 19
        "system Fall(mut Position position) { position.value -= 2; }\n"                 // 20
        "scene Main { }\n"                                                              // 21
        "event(Spawned) Setup(with Main) { Spawn(Position); Spawn(Unit, Position); }\n"; // 22
    static const char fast_file[] =
        "namespace Fast;\n"                                                 // 0
        "using Combat;\n"                                                   // 1
        "override system Combat.Move(mut Position position)\n{\n"           // 2, 3
        "    position.value += Damage(2);\n}\n"                             // 4, 5
        "override float Damage(float base) { return base * 2; }\n"          // 6
        "override bool Stats.IsDead() { return health <= 10; }\n";          // 7
    write_file("lsp_ov/game/tide.packages", "../fast\n");
    write_file("lsp_ov/game/main.tide", main_file);
    write_file("lsp_ov/fast/tide.packages", "package Fast\n");
    write_file("lsp_ov/fast/fast.tide", fast_file);
    char root[700], uri_main[700], uri_fast[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_ov/game", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_ov/game/main.tide", dir);
    snprintf(uri_fast, sizeof uri_fast, "file:///%s/lsp_ov/fast/fast.tide", dir);

    start_in(root);
    open_uri(uri_main, main_file);
    TIDE_REQUIRE(sent_count == 2);
    TIDE_CHECK(has(sent[0], uri_fast) && has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(sent[1], uri_main) && has(sent[1], "\"diagnostics\":[]"));
    open_uri(uri_fast, fast_file);

    const char *over = request_at(uri_fast, "textDocument/hover", 2, 23, "");
    TIDE_CHECK(has(over, "override system Move(mut Position position)"));
    TIDE_CHECK(has(over, "Overrides `Combat.Move` of the game"));
    TIDE_CHECK(has(over, "Stage 1"));
    const char *replaced = request_at(uri_main, "textDocument/hover", 9, 15, "");
    TIDE_CHECK(has(replaced, "virtual system Move(mut Position position)"));
    TIDE_CHECK(has(replaced, "**Replaced** by package Fast's override: this code isn't in the build"));
    const char *open_one = request_at(uri_main, "textDocument/hover", 19, 15, "");
    TIDE_CHECK(has(open_one, "`virtual`: another package can replace it. Nothing does."));
    const char *word = request_at(uri_main, "textDocument/hover", 9, 2, "");
    TIDE_CHECK(has(word, "Another package can replace this declaration") && has(word, "**Replaced** by package Fast"));
    const char *override_word = request_at(uri_fast, "textDocument/hover", 6, 3, "");
    TIDE_CHECK(has(override_word, "Replaces the `virtual` declaration of this name") && has(override_word, "Overrides `Combat.Damage`"));
    const char *method = request_at(uri_fast, "textDocument/hover", 7, 21, "");
    TIDE_CHECK(has(method, "override bool IsDead()") && has(method, "Overrides `Combat.Stats.IsDead` of the game"));
    TIDE_CHECK(has(request_at(uri_fast, "textDocument/hover", 7, 15, ""), "struct Stats"));
    TIDE_CHECK(has(request_at(uri_fast, "textDocument/hover", 2, 17, ""), "namespace Combat"));
    TIDE_CHECK(has(request_at(uri_main, "textDocument/hover", 11, 22, ""), "override float Damage(float base)"));

    const char *to_virtual = request_at(uri_fast, "textDocument/definition", 2, 23, "");
    TIDE_CHECK(has(to_virtual, uri_main) && has(to_virtual, "\"start\":{\"line\":9,\"character\":15}"));
    const char *from_word = request_at(uri_fast, "textDocument/definition", 6, 3, "");
    TIDE_CHECK(has(from_word, uri_main) && has(from_word, "\"start\":{\"line\":8,\"character\":14}"));
    const char *to_built = request_at(uri_main, "textDocument/definition", 17, 8, "");
    TIDE_CHECK(has(to_built, uri_fast) && has(to_built, "\"start\":{\"line\":2,\"character\":23}"));
    const char *implemented = request_at(uri_main, "textDocument/implementation", 9, 15, "");
    TIDE_CHECK(has(implemented, uri_fast) && has(implemented, "\"start\":{\"line\":2,\"character\":23}"));
    const char *method_implemented = request_at(uri_main, "textDocument/implementation", 5, 18, "");
    TIDE_CHECK(has(method_implemented, uri_fast) && has(method_implemented, "\"start\":{\"line\":7,\"character\":20}"));
    TIDE_CHECK(has(request_at(uri_main, "textDocument/implementation", 19, 15, ""), "\"result\":[]"));

    const char *usages = request_at(uri_main, "textDocument/references", 8, 15, "\"context\":{\"includeDeclaration\":true}");
    TIDE_CHECK(count(usages, "\"uri\":") == 4 && count(usages, uri_fast) == 2);
    const char *method_usages = request_at(uri_fast, "textDocument/references", 7, 21, "\"context\":{\"includeDeclaration\":true}");
    TIDE_CHECK(count(method_usages, "\"uri\":") == 3 && count(method_usages, uri_main) == 2);
    const char *renamed = request_at(uri_fast, "textDocument/rename", 4, 22, "\"newName\":\"Harm\"");
    TIDE_CHECK(count(renamed, "\"newText\":\"Harm\"") == 4 && has(renamed, uri_main) && has(renamed, uri_fast));
    const char *system_renamed = request_at(uri_main, "textDocument/rename", 9, 15, "\"newName\":\"Step\"");
    TIDE_CHECK(count(system_renamed, "\"newText\":\"Step\"") == 3);

    const char *lenses = request_at(uri_main, "textDocument/codeLens", 0, 0, "");
    TIDE_CHECK(count(lenses, "replaced by package Fast: not in the build") == 3);
    const char *fast_lenses = request_at(uri_fast, "textDocument/codeLens", 0, 0, "");
    TIDE_CHECK(has(fast_lenses, "replaces Combat.Move of the game") && has(fast_lenses, "replaces Combat.Stats.IsDead of the game"));
    TIDE_CHECK(has(fast_lenses, "stage 1"));
    check_entry_lens(uri_main, 8, uri_fast, (int[]){2}, (int[]){23}, (int[]){4}, 1);
    check_entry_lens(uri_fast, 6, uri_fast, (int[]){2}, (int[]){23}, (int[]){4}, 1);
    check_entry_lens(uri_main, 5, uri_main, (int[]){13}, (int[]){7}, (int[]){5}, 1);
    check_entry_lens(uri_fast, 7, uri_main, (int[]){13}, (int[]){7}, (int[]){5}, 1);
    check_repeat_lenses(uri_main);
    check_repeat_lenses(uri_fast);
    TIDE_CHECK(has(request_at(uri_fast, "textDocument/hover", 7, 21, ""), "Overrides `Combat.Stats.IsDead` of the game"));
    check_reference_lenses(uri_main);
    check_reference_lenses(uri_fast);

    const char *outline = request_at(uri_main, "textDocument/documentSymbol", 0, 0, "");
    TIDE_CHECK(has(outline, "\"name\":\"Move\"") && has(outline, "\"name\":\"IsDead\",\"detail\":\"virtual bool IsDead()\""));
    const char *fast_outline = request_at(uri_fast, "textDocument/documentSymbol", 0, 0, "");
    TIDE_CHECK(has(fast_outline, "\"name\":\"Move\"") && has(fast_outline, "\"name\":\"Damage\"") && has(fast_outline, "\"name\":\"IsDead\""));
    // The words are keywords: `override` at the start of line 2, `virtual` at the start of line 8
    TIDE_CHECK(has(request_at(uri_fast, "textDocument/semanticTokens/full", 0, 0, ""), ",1,0,8,11,0,"));
    TIDE_CHECK(has(request_at(uri_main, "textDocument/semanticTokens/full", 0, 0, ""), ",1,0,7,11,0,"));

    static char typing[1200];
    snprintf(typing, sizeof typing, "%soverride ", fast_file);
    open_uri(uri_fast, typing);
    const char *offered = request_at(uri_fast, "textDocument/completion", 8, 9, "");
    TIDE_CHECK(has(offered, "\"label\":\"system Idle(mut Position position)\""));
    TIDE_CHECK(has(offered, "\"detail\":\"replaces the game's\""));
    TIDE_CHECK(has(offered, "system Idle(mut Position position)\\n{\\n    $0\\n}"));
    TIDE_CHECK(!has(offered, "\"label\":\"system Combat.Move") && !has(offered, "\"label\":\"system Move"));
    TIDE_CHECK(!has(offered, "\"label\":\"system Fall"));
    static const char apart[] = "namespace Fast;\noverride ";
    open_uri(uri_fast, apart);
    const char *qualified = request_at(uri_fast, "textDocument/completion", 1, 9, "");
    TIDE_CHECK(has(qualified, "\"label\":\"system Combat.Idle(mut Combat.Position position)\""));
    TIDE_CHECK(has(qualified, "\"label\":\"bool Combat.Stats.IsDead()\""));
    TIDE_CHECK(has(qualified, "\"label\":\"float Combat.Damage(float base)\""));

    snprintf(typing, sizeof typing, "%soverride system Fall(mut Position position) { position.value -= 3; }\n", fast_file);
    open_uri(uri_fast, typing);
    char range[160];
    snprintf(range, sizeof range, "\"range\":{\"start\":{\"line\":8,\"character\":0},\"end\":{\"line\":8,\"character\":0}}");
    const char *fixes = request_at(uri_fast, "textDocument/codeAction", 8, 0, range);
    TIDE_CHECK(has(fixes, "\"title\":\"Mark 'Fall' virtual\"") && has(fixes, uri_main));
    TIDE_CHECK(has(fixes, "\"start\":{\"line\":20,\"character\":0},\"end\":{\"line\":20,\"character\":0}},\"newText\":\"virtual \""));

    remove_game_file("lsp_ov/game/tide.packages");
    remove_game_file("lsp_ov/game/main.tide");
    remove_game_file("lsp_ov/fast/tide.packages");
    remove_game_file("lsp_ov/fast/fast.tide");
    remove_folder("lsp_ov/game");
    remove_folder("lsp_ov/fast");
    remove_folder("lsp_ov");
    clear_sent();
    lsp_free(&server);
}

// `virtual` and `override` in one file: what they're offered before, how they're laid out, and the fix where one means nothing.
TIDE_TEST(lsp_virtual_and_override_words)
{
    start();
    const char *top = complete("scene Main { }\n$");
    TIDE_CHECK(offers(top, "virtual") && offers(top, "override"));
    const char *after = complete("scene Main { }\nvirtual $");
    TIDE_CHECK(offers(after, "system") && offers(after, "view") && offers(after, "function") && !offers(after, "component"));
    const char *member = complete("struct Stats\n{\n    float health;\n    $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(member, "virtual"));
    const char *member_type = complete("struct Stats\n{\n    float health;\n    virtual $\n}\nscene Main { }\n");
    TIDE_CHECK(offers(member_type, "void") && offers(member_type, "bool") && offers(member_type, "mut"));
    const char *params = complete("namespace Fast;\ncomponent Body { float x; }\noverride system Combat.Move($)\n{\n}\n");
    TIDE_CHECK(offers(params, "Body") && offers(params, "with") && offers(params, "mut"));

    static const char messy[] =
        "component Body { float x; }\n"
        "virtual   system Move( mut Body body )\n{\nbody.x+=1;\n}\n"
        "struct Stats\n{\nfloat health;\nvirtual   bool IsDead( ){return health<=0;}\n}\n"
        "override   float Combat.Damage( float base ){return base;}\n"
        "override mut  void Combat.Stats.Hurt( float amount )\n{\nhealth-=amount;\n}\n"
        "scene Main { }\n";
    format_reply(messy);
    const char *tidy = apply_reply(messy, NULL);
    TIDE_CHECK(has(tidy, "virtual system Move(mut Body body)\n{\n    body.x += 1;\n}\n"));
    TIDE_CHECK(has(tidy, "    virtual bool IsDead() { return health <= 0; }\n"));
    TIDE_CHECK(has(tidy, "override float Combat.Damage(float base) { return base; }\n"));
    TIDE_CHECK(has(tidy, "override mut void Combat.Stats.Hurt(float amount)\n{\n    health -= amount;\n}\n"));

    static const char data[] = "virtual component Body { float x; }\nscene Main { }\n";
    open_document(data);
    TIDE_CHECK(has(last_sent(), "'virtual' goes before a declaration with code"));
    actions_at(0);
    const char *fixed = apply_fix(data, "Remove 'virtual'");
    TIDE_REQUIRE(fixed != NULL);
    TIDE_CHECK(has(fixed, "component Body { float x; }\nscene Main { }\n") && !has(fixed, "virtual"));
}

// A manifest's game with a package: the package's file is analyzed as the package's, before the game's own.
TIDE_TEST(lsp_game_manifest_with_a_package)
{
    if (!find_game_dir()) return;
    static const char game_file[] = "namespace Combat;\ncomponent Body { float x; }\n"
                                    "virtual system Move(mut Body body) { body.x += 1; }\n"
                                    "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\n";
    static const char mod_file[] = "namespace Mod;\nusing Combat;\n"
                                   "override system Move(mut Body body) { body.x += 2; }\n";
    write_file("lsp_pkg_game.tide", game_file);
    write_file("lsp_pkg_mod.tide", mod_file);
    char a[640], b[640], manifest[640], manifest_text[1400], uri_a[700], uri_b[700];
    game_path(a, sizeof a, "lsp_pkg_game.tide");
    game_path(b, sizeof b, "lsp_pkg_mod.tide");
    game_path(manifest, sizeof manifest, "lsp_pkg_manifest.txt");
    snprintf(manifest_text, sizeof manifest_text, "game\t%s\ngame\t%s\tMod\n", a, b);
    write_file("lsp_pkg_manifest.txt", manifest_text);
    snprintf(uri_a, sizeof uri_a, "file:///%s", a[0] == '/' ? a + 1 : a);
    snprintf(uri_b, sizeof uri_b, "file:///%s", b[0] == '/' ? b + 1 : b);

    start();
    server.manifest = manifest;
    open_uri(uri_a, game_file);
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[0], uri_b) && has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(has(sent[1], uri_a) && has(sent[1], "\"diagnostics\":[]"));
    TIDE_CHECK(has(request_at(uri_a, "textDocument/hover", 2, 16, ""), "**Replaced** by package Mod's override"));

    snprintf(manifest_text, sizeof manifest_text, "game\t%s\ngame\t%s\n", a, b);
    write_file("lsp_pkg_manifest.txt", manifest_text);
    start();
    server.manifest = manifest;
    open_uri(uri_a, game_file);
    TIDE_CHECK(sent_count == 2);
    TIDE_CHECK(has(sent[1], uri_b) && has(sent[1], "an override is in another package than what it replaces"));

    static const char open_mod[] = "namespace Mod;\ncomponent Thing { float x; }\n"
                                   "virtual system Tick(mut Thing thing) { thing.x += 1; }\n";
    static const char again[] = "namespace Mod;\nsystem Tick(mut Thing thing) { thing.x += 2; }\n"
                                "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Thing); }\n";
    write_file("lsp_pkg_mod.tide", open_mod);
    snprintf(manifest_text, sizeof manifest_text, "game\t%s\ngame\t%s\tMod\n", a, b);
    write_file("lsp_pkg_manifest.txt", manifest_text);
    start();
    server.manifest = manifest;
    open_uri(uri_a, again);
    TIDE_CHECK(has(sent[1], uri_a) && has(sent[1], "'Mod.Tick' is already declared with these parameters"));
    TIDE_CHECK(has(sent[1], "the other one is virtual: to replace it, write 'override' before this one"));
    const char *fix = request_at(uri_a, "textDocument/codeAction", 1, 0,
                                 "\"range\":{\"start\":{\"line\":1,\"character\":0},\"end\":{\"line\":1,\"character\":0}}");
    TIDE_CHECK(has(fix, "\"title\":\"Make it an override of 'Mod.Tick'\""));
    TIDE_CHECK(has(fix, "\"start\":{\"line\":1,\"character\":0},\"end\":{\"line\":1,\"character\":0}},\"newText\":\"override \""));
    open_uri(uri_a, "namespace Mod;\noverride system Tick(mut Thing thing) { thing.x += 2; }\n"
                    "scene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Thing); }\n");
    TIDE_CHECK(has(sent[0], "\"diagnostics\":[]") && has(sent[1], "\"diagnostics\":[]"));

    remove(a);
    remove(b);
    remove(manifest);
    clear_sent();
    lsp_free(&server);
}

// ---------------------------------------------------------------------------
// Assets: the files `asset` declarations name (docs/spec.md, Assets). The
// declaration isn't in the parser yet: the server reads a site off the tokens,
// so these games have the parser's own error at each `asset` line beside what's
// checked here.

// An editor like VS Code: it asks before renaming files, takes the filters as
// registrations, and watches files with patterns relative to a folder.
#define ASSET_EDITOR_CAPABILITIES                                                                                  \
    "{\"workspace\":{\"workspaceEdit\":{\"documentChanges\":true},"                                                 \
    "\"fileOperations\":{\"dynamicRegistration\":true,\"willRename\":true,\"didRename\":true},"                    \
    "\"didChangeWatchedFiles\":{\"dynamicRegistration\":true,\"relativePatternSupport\":true}}}"

static void start_in_with(const char *folder, const char *capabilities)
{
    clear_sent();
    lsp_free(&server);
    lsp_init(&server, capture, NULL);
    char message[2048];
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":"
             "{\"rootUri\":\"%s\",\"workspaceFolders\":[{\"uri\":\"%s\",\"name\":\"game\"}],\"capabilities\":%s}}",
             folder, folder, capabilities);
    handle(message);
}

static void start_in_pair_with(const char *first, const char *second, const char *capabilities)
{
    clear_sent();
    lsp_free(&server);
    lsp_init(&server, capture, NULL);
    jbuf initialize = {0};
    jb_printf(&initialize, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
                           "\"workspaceFolders\":[{\"uri\":\"%s\",\"name\":\"a\"},{\"uri\":\"%s\",\"name\":\"b\"}],"
                           "\"capabilities\":%s}}", first, second, capabilities);
    handle(initialize.data);
    jb_free(&initialize);
}

// Files searched in one game aren't the same compilation context in
// another: a shared package sees each consumer's own PACKAGE_ symbols.
TIDE_TEST(lsp_workspace_symbols_of_shared_package_contexts)
{
    if (!find_game_dir()) return;
    make_folder("lsp_symbol_contexts");
    make_folder("lsp_symbol_contexts/a");
    make_folder("lsp_symbol_contexts/b");
    make_folder("lsp_symbol_contexts/pk");
    make_folder("lsp_symbol_contexts/extra");
    write_file("lsp_symbol_contexts/a/tide.packages", "../pk\n../extra\n");
    write_file("lsp_symbol_contexts/b/tide.packages", "../pk\n");
    write_file("lsp_symbol_contexts/pk/tide.packages", "package Pk\n");
    write_file("lsp_symbol_contexts/extra/tide.packages", "package Extra\n");
    static const char package[] = "namespace Pk;\n#if PACKAGE_EXTRA\nconst int WithExtra = 1;\n#else\n"
                                  "const int WithoutExtra = 2;\n#endif\n"
                                  "const int Marker = 5;\nstruct Shared { int value; int Read() { return value; } }\n";
    write_file("lsp_symbol_contexts/pk/main.tide", package);
    write_file("lsp_symbol_contexts/extra/main.tide", "namespace Extra;\nconst int Marker = 7;\n");
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    char root_a[700], root_b[700], uri[700];
    snprintf(root_a, sizeof root_a, "file:///%s/lsp_symbol_contexts/a", dir);
    snprintf(root_b, sizeof root_b, "file:///%s/lsp_symbol_contexts/b", dir);
    snprintf(uri, sizeof uri, "file:///%s/lsp_symbol_contexts/pk/main.tide", dir);
    for (int reverse = 0; reverse < 2; reverse++) {
        start_in_with(reverse ? root_b : root_a, "{}");
        open_uri(uri, package);
        TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
        start_in_pair_with(reverse ? root_b : root_a, reverse ? root_a : root_b, "{}");
        for (int opened = 0; opened < 2; opened++) {
            if (opened) open_uri(uri, package);
            clear_sent();
            handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"\"}}");
            const char *symbols = last_sent();
            TIDE_CHECK(count(symbols, "\"name\":\"WithExtra\"") == 1);
            TIDE_CHECK(count(symbols, "\"name\":\"WithoutExtra\"") == 1);
            TIDE_CHECK(count(symbols, "\"name\":\"Marker\"") == 2);
            TIDE_CHECK(count(symbols, "\"name\":\"Shared\"") == 1);
            TIDE_CHECK(count(symbols, "\"name\":\"Read\"") == 1);
            TIDE_CHECK(count(symbols, "\"location\"") == 6);
        }
    }
    remove_game_file("lsp_symbol_contexts/a/tide.packages");
    remove_game_file("lsp_symbol_contexts/b/tide.packages");
    remove_game_file("lsp_symbol_contexts/pk/tide.packages");
    remove_game_file("lsp_symbol_contexts/pk/main.tide");
    remove_game_file("lsp_symbol_contexts/extra/tide.packages");
    remove_game_file("lsp_symbol_contexts/extra/main.tide");
    remove_folder("lsp_symbol_contexts/a");
    remove_folder("lsp_symbol_contexts/b");
    remove_folder("lsp_symbol_contexts/pk");
    remove_folder("lsp_symbol_contexts/extra");
    remove_folder("lsp_symbol_contexts");
    clear_sent();
    lsp_free(&server);
}

static void write_bytes(const char *name, const char *bytes, const size_t len)
{
    char path[640];
    game_path(path, sizeof path, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(bytes, 1, len, f);
    fclose(f);
}

static void move_game_file(const char *from, const char *to)
{
    char a[640], b[640];
    game_path(a, sizeof a, from);
    game_path(b, sizeof b, to);
    rename(a, b);
}

static bool game_file_exists(const char *name)
{
    char path[640];
    game_path(path, sizeof path, name);
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// The first message sent with `needle` in it, or NULL.
static const char *sent_with(const char *needle)
{
    const int i = sent_for(needle);
    return i >= 0 ? sent[i] : NULL;
}

typedef struct editor_asset_registrations {
    char *watchers;
    char *filters;
} editor_asset_registrations;

// Apply the dynamic capability requests as a client does. Unchanged
// registrations remain installed even when a request sends no new capability.
static void receive_asset_registrations(editor_asset_registrations *editor)
{
    for (int i = 0; i < sent_count; i++) {
        const json *message = json_parse(sent[i], strlen(sent[i]));
        const char *method = json_str(json_get(message, "method"));
        const bool registering = method && strcmp(method, "client/registerCapability") == 0;
        const bool removing = method && strcmp(method, "client/unregisterCapability") == 0;
        if (!registering && !removing) {
            json_release();
            continue;
        }
        const json *items = json_path(message, "params", registering ? "registrations" : "unregisterations", NULL);
        for (int k = 0; items && k < items->count; k++) {
            const char *id = json_str(json_get(items->items[k], "id"));
            char **out = id && strcmp(id, "tide-assets") == 0 ? &editor->watchers
                       : id && strcmp(id, "tide-rename") == 0 ? &editor->filters : NULL;
            if (!out) continue;
            free(*out);
            *out = NULL;
            if (registering) {
                jbuf options = {0};
                jb_json(&options, json_get(items->items[k], "registerOptions"));
                *out = options.data;
            }
        }
        json_release();
    }
}

static void receive_document_diagnostics(char **last, const char *uri)
{
    for (int i = 0; i < sent_count; i++) {
        const json *message = json_parse(sent[i], strlen(sent[i]));
        const char *method = json_str(json_get(message, "method"));
        const char *document = json_str(json_path(message, "params", "uri", NULL));
        if (method && strcmp(method, "textDocument/publishDiagnostics") == 0 && document && strcmp(document, uri) == 0) {
            jbuf diagnostics = {0};
            jb_json(&diagnostics, json_path(message, "params", "diagnostics", NULL));
            free(*last);
            *last = diagnostics.data;
        }
        json_release();
    }
}

TIDE_TEST(lsp_asset_registrations_cover_every_open_game)
{
    if (!find_game_dir()) return;
    make_folder("lsp_watch_games");
    make_folder("lsp_watch_games/a");
    make_folder("lsp_watch_games/b");
    make_folder("lsp_watch_games/pk");
    static const char game[] = "asset string rules = \"rules.txt\";\nscene Main { }\n";
    static const char package[] = "namespace Pk;\nasset string shared = \"shared.txt\";\n";
    write_file("lsp_watch_games/a/tide.packages", "../pk\n");
    write_file("lsp_watch_games/b/tide.packages", "../pk\n");
    write_file("lsp_watch_games/a/main.tide", game);
    write_file("lsp_watch_games/b/main.tide", game);
    write_file("lsp_watch_games/a/rules.txt", "a");
    write_file("lsp_watch_games/b/rules.txt", "b");
    write_file("lsp_watch_games/pk/tide.packages", "package Pk\n");
    write_file("lsp_watch_games/pk/main.tide", package);
    write_file("lsp_watch_games/pk/shared.txt", "shared");
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    char roots[2][700], uris[2][700], shared[700], asset[700];
    for (int i = 0; i < 2; i++) {
        snprintf(roots[i], sizeof roots[i], "file:///%s/lsp_watch_games/%c", dir, 'a' + i);
        snprintf(uris[i], sizeof uris[i], "file:///%s/lsp_watch_games/%c/main.tide", dir, 'a' + i);
    }
    snprintf(shared, sizeof shared, "file:///%s/lsp_watch_games/pk", dir);
    snprintf(asset, sizeof asset, "file:///%s/lsp_watch_games/a/rules.txt", dir);
    char *canonical_watchers = NULL, *canonical_filters = NULL;
    for (int reverse_roots = 0; reverse_roots < 2; reverse_roots++) {
        for (int reverse_open = 0; reverse_open < 2; reverse_open++) {
            editor_asset_registrations editor = {0};
            start_in_pair_with(roots[reverse_roots], roots[!reverse_roots], ASSET_EDITOR_CAPABILITIES);
            open_uri(uris[reverse_open], game);
            receive_asset_registrations(&editor);
            open_uri(uris[!reverse_open], game);
            receive_asset_registrations(&editor);
            TIDE_REQUIRE(editor.watchers && editor.filters);
            TIDE_CHECK(count(editor.watchers, "\"globPattern\"") == 3);
            TIDE_CHECK(count(editor.watchers, roots[0]) == 1 && count(editor.watchers, roots[1]) == 1);
            TIDE_CHECK(count(editor.watchers, shared) == 1);
            TIDE_CHECK(count(editor.filters, "/pk/shared.txt") == 1);
            TIDE_CHECK(has(editor.filters, "/a/rules.txt") && has(editor.filters, "/b/rules.txt"));
            if (!canonical_watchers) {
                canonical_watchers = malloc(strlen(editor.watchers) + 1);
                canonical_filters = malloc(strlen(editor.filters) + 1);
                strcpy(canonical_watchers, editor.watchers);
                strcpy(canonical_filters, editor.filters);
            }
            TIDE_CHECK(strcmp(editor.watchers, canonical_watchers) == 0);
            TIDE_CHECK(strcmp(editor.filters, canonical_filters) == 0);
            for (int i = 0; i < 2; i++) {
                request_at(uris[i], "textDocument/hover", 0, 14, "");
                TIDE_CHECK(sent_with("client/registerCapability") == NULL);
                request_at(uris[i], "textDocument/codeLens", 0, 0, "");
                TIDE_CHECK(sent_with("client/registerCapability") == NULL);
            }
            clear_sent();
            handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"\"}}");
            receive_asset_registrations(&editor);
            TIDE_CHECK(strcmp(editor.watchers, canonical_watchers) == 0 && strcmp(editor.filters, canonical_filters) == 0);
            char operation[1700];
            snprintf(operation, sizeof operation, "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"workspace/willRenameFiles\","
                     "\"params\":{\"files\":[{\"oldUri\":\"%s\",\"newUri\":\"%s/moved.txt\"}]}}", asset, roots[0]);
            clear_sent();
            handle(operation);
            receive_asset_registrations(&editor);
            TIDE_CHECK(has(last_sent(), "moved.txt"));
            TIDE_CHECK(strcmp(editor.watchers, canonical_watchers) == 0 && strcmp(editor.filters, canonical_filters) == 0);
            write_bytes("lsp_watch_games/a/rules.txt", "\xff", 1);
            snprintf(operation, sizeof operation, "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\","
                     "\"params\":{\"changes\":[{\"uri\":\"%s\",\"type\":2}]}}", asset);
            clear_sent();
            handle(operation);
            TIDE_CHECK(sent_with("UTF-8") != NULL);
            receive_asset_registrations(&editor);
            TIDE_CHECK(strcmp(editor.watchers, canonical_watchers) == 0);
            write_file("lsp_watch_games/a/rules.txt", "a");
            clear_sent();
            handle(operation);
            close_uri(uris[0]);
            receive_asset_registrations(&editor);
            TIDE_REQUIRE(editor.watchers && editor.filters);
            TIDE_CHECK(!has(editor.watchers, roots[0]) && has(editor.watchers, roots[1]) && count(editor.watchers, shared) == 1);
            TIDE_CHECK(!has(editor.filters, "/a/rules.txt") && has(editor.filters, "/b/rules.txt"));
            clear_sent();
            handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"\"}}");
            receive_asset_registrations(&editor);
            TIDE_CHECK(!has(editor.watchers, roots[0]));
            open_uri(uris[0], game);
            receive_asset_registrations(&editor);
            TIDE_CHECK(strcmp(editor.watchers, canonical_watchers) == 0 && strcmp(editor.filters, canonical_filters) == 0);
            open_uri(uris[1], "scene Main { }\n");
            receive_asset_registrations(&editor);
            TIDE_CHECK(!has(editor.watchers, roots[1]) && has(editor.watchers, roots[0]) && count(editor.watchers, shared) == 1);
            close_uri(uris[1]);
            receive_asset_registrations(&editor);
            snprintf(operation, sizeof operation, "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWorkspaceFolders\","
                     "\"params\":{\"event\":{\"removed\":[{\"uri\":\"%s\",\"name\":\"b\"}],\"added\":[]}}}", roots[1]);
            clear_sent();
            handle(operation);
            receive_asset_registrations(&editor);
            TIDE_CHECK(!has(editor.watchers, roots[1]) && has(editor.watchers, roots[0]));
            close_uri(uris[0]);
            receive_asset_registrations(&editor);
            TIDE_CHECK(editor.watchers == NULL && editor.filters == NULL);
            clear_sent();
            handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"\"}}");
            receive_asset_registrations(&editor);
            TIDE_CHECK(editor.watchers == NULL && editor.filters == NULL);
        }
    }
    free(canonical_watchers);
    free(canonical_filters);
    editor_asset_registrations broad = {0};
    start_in_pair_with(roots[0], roots[1], "{\"workspace\":{\"fileOperations\":{\"dynamicRegistration\":true},"
                                         "\"didChangeWatchedFiles\":{\"dynamicRegistration\":true}}}");
    open_uri(uris[0], game);
    receive_asset_registrations(&broad);
    open_uri(uris[1], game);
    receive_asset_registrations(&broad);
    TIDE_REQUIRE(broad.watchers != NULL);
    TIDE_CHECK(count(broad.watchers, "**/rules.txt") == 1 && count(broad.watchers, "**/shared.txt") == 1);
    close_uri(uris[0]);
    receive_asset_registrations(&broad);
    close_uri(uris[1]);
    receive_asset_registrations(&broad);
    TIDE_CHECK(broad.watchers == NULL && broad.filters == NULL);
    remove_game_file("lsp_watch_games/a/main.tide");
    remove_game_file("lsp_watch_games/a/rules.txt");
    remove_game_file("lsp_watch_games/a/tide.packages");
    remove_game_file("lsp_watch_games/b/main.tide");
    remove_game_file("lsp_watch_games/b/rules.txt");
    remove_game_file("lsp_watch_games/b/tide.packages");
    remove_game_file("lsp_watch_games/pk/main.tide");
    remove_game_file("lsp_watch_games/pk/shared.txt");
    remove_game_file("lsp_watch_games/pk/tide.packages");
    remove_folder("lsp_watch_games/a");
    remove_folder("lsp_watch_games/b");
    remove_folder("lsp_watch_games/pk");
    remove_folder("lsp_watch_games");
    clear_sent();
    lsp_free(&server);
}

// An explicit folder game can include a source whose document instead selects
// a manifest subset. Closing its other document must keep that subset's watches.
TIDE_TEST(lsp_asset_registrations_keep_manifest_and_folder_contexts)
{
    if (!find_game_dir()) return;
    make_folder("lsp_watch_manifest");
    make_folder("lsp_watch_manifest/b");
    make_folder("lsp_watch_manifest/extra");
    make_folder("lsp_watch_manifest/build");
    make_folder("lsp_watch_manifest/build/tools");
    static const char a[] = "asset string alpha = \"a.txt\";\n";
    char b[400];
    write_file("lsp_watch_manifest/tide.packages", "./extra\n");
    write_file("lsp_watch_manifest/extra/tide.packages", "package Extra\n");
    write_file("lsp_watch_manifest/extra/main.tide", "namespace Extra;\nconst int Marker = 7;\n");
    write_file("lsp_watch_manifest/a.tide", a);
    write_file("lsp_watch_manifest/a.txt", "a");
    write_file("lsp_watch_manifest/b/b.txt", "b");
    write_file("lsp_watch_manifest/b/with.txt", "with");
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    char root[700], uris[2][700], manifest[1400];
    snprintf(root, sizeof root, "file:///%s/lsp_watch_manifest", dir);
    snprintf(uris[0], sizeof uris[0], "file:///%s/lsp_watch_manifest/a.tide", dir);
    snprintf(uris[1], sizeof uris[1], "file:///%s/lsp_watch_manifest/b/b.tide", dir);
    for (int variant = 0; variant < 4; variant++) {
        const bool folder = (variant & 1) != 0;
        const bool invalid_folder_branch = (variant & 2) != 0;
        snprintf(b, sizeof b, "#if PACKAGE_EXTRA\nasset string beta = \"with.txt\";\nconst int Mode = %s;\n#else\n"
                 "asset string beta = \"b.txt\";\nconst int Mode = 22;\n#endif\nscene Main { }\n",
                 invalid_folder_branch ? "Missing" : "11");
        write_file("lsp_watch_manifest/b/b.tide", b);
        snprintf(manifest, sizeof manifest, "subset\t%s/lsp_watch_manifest/b/%s\n", game_dir, folder ? "" : "b.tide");
        write_file("lsp_watch_manifest/build/tools/games.txt", manifest);
        for (int scenario = 0; scenario < 4; scenario++) {
            const int reverse_open = scenario & 1;
            const bool deferred = (scenario & 2) != 0;
            editor_asset_registrations editor = {0};
            char *b_diagnostics = NULL;
            start_in_with(root, ASSET_EDITOR_CAPABILITIES);
            server.defer = deferred;
            open_uri(uris[reverse_open], reverse_open ? b : a);
            if (!deferred) {
                TIDE_REQUIRE(sent_for(uris[reverse_open]) >= 0);
                TIDE_CHECK(has(sent[sent_for(uris[reverse_open])], "\"diagnostics\":[]"));
                receive_asset_registrations(&editor);
                receive_document_diagnostics(&b_diagnostics, uris[1]);
            }
            open_uri(uris[!reverse_open], reverse_open ? a : b);
            if (deferred) lsp_settle(&server);
            TIDE_REQUIRE(sent_for(uris[!reverse_open]) >= 0);
            TIDE_CHECK(has(sent[sent_for(uris[!reverse_open])], "\"diagnostics\":[]"));
            receive_asset_registrations(&editor);
            receive_document_diagnostics(&b_diagnostics, uris[1]);
            TIDE_REQUIRE(b_diagnostics != NULL);
            TIDE_CHECK(strcmp(b_diagnostics, "[]") == 0);
            TIDE_REQUIRE(editor.watchers && editor.filters);
            TIDE_CHECK(count(editor.watchers, "\"globPattern\"") == 3);
            TIDE_CHECK(count(editor.watchers, "a.txt") == 1 && count(editor.watchers, "b.txt") == 1);
            TIDE_CHECK(count(editor.watchers, "with.txt") == 1);
            if (!deferred) {
                // b's own requests must still use its manifest's #if branch.
                TIDE_CHECK(has(request_at(uris[0], "textDocument/hover", 0, 14, ""), "alpha"));
                TIDE_CHECK(has(request_at(uris[1], "textDocument/hover", 5, 11, ""), "Mode = 22"));
                TIDE_CHECK(has(request_at(uris[0], "textDocument/hover", 0, 14, ""), "alpha"));
                TIDE_CHECK(has(request_at(uris[1], "textDocument/hover", 5, 11, ""), "Mode = 22"));
                clear_sent();
                handle("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/symbol\",\"params\":{\"query\":\"\"}}");
                TIDE_CHECK(count(last_sent(), "\"name\":\"alpha\"") == 1);
                TIDE_CHECK(count(last_sent(), "\"name\":\"beta\"") == 2);
                TIDE_CHECK(count(last_sent(), "\"name\":\"Mode\"") == 2);
                receive_asset_registrations(&editor);
            }
            close_uri(uris[0]);
            receive_asset_registrations(&editor);
            TIDE_REQUIRE(editor.watchers && editor.filters);
            TIDE_CHECK(count(editor.watchers, "\"globPattern\"") == 1);
            TIDE_CHECK(!has(editor.watchers, "a.txt") && !has(editor.watchers, "with.txt") && has(editor.watchers, "b.txt"));
            TIDE_CHECK(!has(editor.filters, "a.txt") && has(editor.filters, "b.txt"));
            close_uri(uris[1]);
            receive_asset_registrations(&editor);
            TIDE_CHECK(editor.watchers == NULL && editor.filters == NULL);
            free(b_diagnostics);
        }
    }
    remove_game_file("lsp_watch_manifest/a.tide");
    remove_game_file("lsp_watch_manifest/a.txt");
    remove_game_file("lsp_watch_manifest/tide.packages");
    remove_game_file("lsp_watch_manifest/b/b.tide");
    remove_game_file("lsp_watch_manifest/b/b.txt");
    remove_game_file("lsp_watch_manifest/b/with.txt");
    remove_game_file("lsp_watch_manifest/extra/main.tide");
    remove_game_file("lsp_watch_manifest/extra/tide.packages");
    remove_game_file("lsp_watch_manifest/build/tools/games.txt");
    remove_folder("lsp_watch_manifest/b");
    remove_folder("lsp_watch_manifest/extra");
    remove_folder("lsp_watch_manifest/build/tools");
    remove_folder("lsp_watch_manifest/build");
    remove_folder("lsp_watch_manifest");
    clear_sent();
    lsp_free(&server);
}

TIDE_TEST(lsp_open_contexts_keep_innermost_workspace_root)
{
    if (!find_game_dir()) return;
    make_folder("lsp_watch_nested");
    make_folder("lsp_watch_nested/inner");
    static const char parent[] = "asset string alpha = \"a.txt\";\nconst int ParentOnly = 17;\n";
    static const char child[] = "asset string beta = \"b.txt\";\nint Read() { return ParentOnly; }\nscene Main { }\n";
    write_file("lsp_watch_nested/a.tide", parent);
    write_file("lsp_watch_nested/a.txt", "a");
    write_file("lsp_watch_nested/inner/b.tide", child);
    write_file("lsp_watch_nested/inner/b.txt", "b");
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    char roots[2][700], uris[2][700];
    snprintf(roots[0], sizeof roots[0], "file:///%s/lsp_watch_nested", dir);
    snprintf(roots[1], sizeof roots[1], "file:///%s/lsp_watch_nested/inner", dir);
    snprintf(uris[0], sizeof uris[0], "file:///%s/lsp_watch_nested/a.tide", dir);
    snprintf(uris[1], sizeof uris[1], "file:///%s/lsp_watch_nested/inner/b.tide", dir);
    for (int scenario = 0; scenario < 4; scenario++) {
        const int reverse_roots = scenario & 1;
        const int reverse_open = (scenario >> 1) & 1;
        editor_asset_registrations editor = {0};
        char *child_diagnostics = NULL;
        start_in_pair_with(roots[reverse_roots], roots[!reverse_roots], ASSET_EDITOR_CAPABILITIES);
        open_uri(uris[reverse_open], reverse_open ? child : parent);
        receive_asset_registrations(&editor);
        receive_document_diagnostics(&child_diagnostics, uris[1]);
        open_uri(uris[!reverse_open], reverse_open ? parent : child);
        receive_asset_registrations(&editor);
        receive_document_diagnostics(&child_diagnostics, uris[1]);
        TIDE_REQUIRE(child_diagnostics != NULL && editor.watchers != NULL);
        TIDE_CHECK(has(child_diagnostics, "ParentOnly") && strcmp(child_diagnostics, "[]") != 0);
        TIDE_CHECK(count(editor.watchers, "\"globPattern\"") == 2);
        TIDE_CHECK(has(request_at(uris[0], "textDocument/hover", 1, 12, ""), "ParentOnly = 17"));
        TIDE_CHECK(has(request_at(uris[1], "textDocument/hover", 1, 22, ""), "\"result\":null"));
        close_uri(uris[0]);
        receive_asset_registrations(&editor);
        TIDE_REQUIRE(editor.watchers != NULL);
        TIDE_CHECK(count(editor.watchers, "\"globPattern\"") == 1 && has(editor.watchers, "b.txt"));
        close_uri(uris[1]);
        receive_asset_registrations(&editor);
        TIDE_CHECK(editor.watchers == NULL && editor.filters == NULL);
        free(child_diagnostics);
    }
    remove_game_file("lsp_watch_nested/a.tide");
    remove_game_file("lsp_watch_nested/a.txt");
    remove_game_file("lsp_watch_nested/inner/b.tide");
    remove_game_file("lsp_watch_nested/inner/b.txt");
    remove_folder("lsp_watch_nested/inner");
    remove_folder("lsp_watch_nested");
    clear_sent();
    lsp_free(&server);
}

// Fixed whole-second metadata lets replacements exercise explicit watch
// invalidation independently of the filesystem's timestamp resolution.
static void pin_file_time(const char *name)
{
    char path[640];
    game_path(path, sizeof path, name);
#ifdef _WIN32
    struct _utimbuf time = {1700000000, 1700000000};
    TIDE_CHECK(_utime(path, &time) == 0);
#else
    struct utimbuf time = {1700000000, 1700000000};
    TIDE_CHECK(utime(path, &time) == 0);
#endif
}

static void notify_file(const char *uri, const int type)
{
    char message[1024];
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\",\"params\":{\"changes\":"
             "[{\"uri\":\"%s\",\"type\":%d}]}}", uri, type);
    clear_sent();
    handle(message);
}

TIDE_TEST(lsp_asset_notifications_override_unchanged_metadata)
{
    if (!find_game_dir()) return;
    make_folder("lsp_asset_watch");
    static const char main[] = "asset string words = \"words.txt\";\n"
                               "struct Corner { float3 position; }\n"
                               "asset Mesh<Corner> triangle = \"shape.gltf\";\nscene Main { }\n";
    static const char gltf[] = "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"shape.bin\",\"byteLength\":42}],"
                               "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
                               "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                               "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
                               "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]}";
    unsigned char buffer[42] = {0};
    buffer[14] = buffer[30] = 0x80; // (0,0,0), (1,0,0), (0,1,0), as little-endian floats
    buffer[15] = buffer[31] = 0x3f;
    buffer[38] = 1;
    buffer[40] = 2;
    write_file("lsp_asset_watch/main.tide", main);
    write_file("lsp_asset_watch/words.txt", "one\n");
    write_file("lsp_asset_watch/shape.gltf", gltf);
    write_bytes("lsp_asset_watch/shape.bin", (const char *)buffer, sizeof buffer);
    pin_file_time("lsp_asset_watch/words.txt");
    pin_file_time("lsp_asset_watch/shape.bin");
    char root[700], uri_main[700], uri_words[700], uri_buffer[700], uri_spare[700], message[2048];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_asset_watch", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_asset_watch/main.tide", dir);
    snprintf(uri_words, sizeof uri_words, "file:///%s/lsp_asset_watch/words.txt", dir);
    snprintf(uri_buffer, sizeof uri_buffer, "file:///%s/lsp_asset_watch/shape.bin", dir);
    snprintf(uri_spare, sizeof uri_spare, "file:///%s/lsp_asset_watch/spare.txt", dir);
    start_in(root);
    open_uri(uri_main, main);
    TIDE_CHECK(has(sent_with(uri_main), "\"diagnostics\":[]"));

    // Changed bytes of the same size and time override a cached successful decode.
    write_bytes("lsp_asset_watch/words.txt", "\xffne\n", 4);
    pin_file_time("lsp_asset_watch/words.txt");
    notify_file(uri_words, 2);
    TIDE_CHECK(has(sent_with(uri_main), "'words.txt' isn't UTF-8 text"));
    write_file("lsp_asset_watch/words.txt", "one\n");
    pin_file_time("lsp_asset_watch/words.txt");
    notify_file(uri_words, 2);
    TIDE_CHECK(has(sent_with(uri_main), "\"diagnostics\":[]"));

    // The .gltf itself is unchanged; its external buffer's notification invalidates it too.
    buffer[40] = 3;
    write_bytes("lsp_asset_watch/shape.bin", (const char *)buffer, sizeof buffer);
    pin_file_time("lsp_asset_watch/shape.bin");
    notify_file(uri_buffer, 2);
    TIDE_CHECK(has(sent_with(uri_main), "\"severity\":1") && has(sent_with(uri_main), "shape.gltf"));
    buffer[40] = 2;
    write_bytes("lsp_asset_watch/shape.bin", (const char *)buffer, sizeof buffer);
    pin_file_time("lsp_asset_watch/shape.bin");
    notify_file(uri_buffer, 2);
    TIDE_CHECK(has(sent_with(uri_main), "\"diagnostics\":[]"));

    remove_game_file("lsp_asset_watch/words.txt");
    notify_file(uri_words, 3);
    TIDE_CHECK(has(sent_with(uri_main), "there's no file at 'words.txt'"));
    write_bytes("lsp_asset_watch/words.txt", "\xffne\n", 4);
    pin_file_time("lsp_asset_watch/words.txt");
    notify_file(uri_words, 1);
    TIDE_CHECK(has(sent_with(uri_main), "'words.txt' isn't UTF-8 text"));

    write_file("lsp_asset_watch/spare.txt", "two\n");
    pin_file_time("lsp_asset_watch/spare.txt");
    open_uri(uri_main, "asset string spare = \"spare.txt\";\nscene Main { }\n");
    TIDE_CHECK(has(sent_with(uri_main), "\"diagnostics\":[]"));
    remove_game_file("lsp_asset_watch/spare.txt");
    move_game_file("lsp_asset_watch/words.txt", "lsp_asset_watch/spare.txt");
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didRenameFiles\",\"params\":{\"files\":"
             "[{\"oldUri\":\"%s\",\"newUri\":\"%s\"}]}}", uri_words, uri_spare);
    clear_sent();
    handle(message);
    TIDE_CHECK(has(sent_with(uri_main), "'spare.txt' isn't UTF-8 text"));

    remove_game_file("lsp_asset_watch/main.tide");
    remove_game_file("lsp_asset_watch/spare.txt");
    remove_game_file("lsp_asset_watch/shape.gltf");
    remove_game_file("lsp_asset_watch/shape.bin");
    remove_game_file("lsp_asset_watch/.tide/assets.sha256");
    remove_game_file("lsp_asset_watch/.tide/.gitignore");
    remove_folder("lsp_asset_watch/.tide");
    remove_folder("lsp_asset_watch");
    clear_sent();
    lsp_free(&server);
}

// Closing a dependency discards its unsaved text. Its remaining consumers
// get disk-based diagnostics again, without refreshing unrelated games or
// reviving diagnostics for the document whose editor tab just closed.
TIDE_TEST(lsp_close_refreshes_dependent_diagnostics)
{
    if (!find_game_dir()) return;
    make_folder("lsp_close");
    make_folder("lsp_close/a");
    make_folder("lsp_close/b");
    static const char body[] = "singleton Body { int value; }\n";
    static const char changed[] = "singleton Changed { int value; }\n";
    static const char main[] = "scene Main { }\nsystem Tick(mut Body body) { body.value += 1; }\n";
    write_file("lsp_close/a/body.tide", body);
    write_file("lsp_close/a/main.tide", main);
    write_file("lsp_close/b/main.tide", "scene Main { }\n");
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    char root_a[700], root_b[700], uri_main[700], uri_body[700], uri_encoded[700], uri_other[700];
    snprintf(root_a, sizeof root_a, "file:///%s/lsp_close/a", dir);
    snprintf(root_b, sizeof root_b, "file:///%s/lsp_close/b", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_close/a/main.tide", dir);
    snprintf(uri_body, sizeof uri_body, "file:///%s/lsp_close/a/body.tide", dir);
    snprintf(uri_encoded, sizeof uri_encoded, "file:///%s/lsp_close/a/body%%2Etide", dir);
    snprintf(uri_other, sizeof uri_other, "file:///%s/lsp_close/b/main.tide", dir);
    for (int deferred = 0; deferred < 2; deferred++) {
        const char *closed = deferred ? uri_encoded : uri_body;
        start_in_pair_with(root_a, root_b, "{}");
        open_uri(uri_main, main);
        TIDE_CHECK(has(sent_with(uri_main), "\"diagnostics\":[]"));
        open_uri(uri_other, "scene Main { }\n");
        open_uri(closed, body);
        open_uri(closed, changed);
        TIDE_CHECK(server.doc_count == 3);
        TIDE_CHECK(has(sent_with(uri_main), "unknown component or singleton 'Body'"));
        server.defer = deferred != 0;
        close_uri(closed);
        if (deferred) {
            TIDE_CHECK(sent_count == 1 && has(sent[0], closed) && has(sent[0], "\"diagnostics\":[]"));
            TIDE_CHECK(server.unsettled_count == 2);
            lsp_settle(&server);
        }
        TIDE_CHECK(server.doc_count == 2 && server.unsettled_count == 0 && sent_count == 2);
        TIDE_REQUIRE(sent_with(uri_main) != NULL);
        TIDE_CHECK(has(sent_with(uri_main), "\"diagnostics\":[]"));
        TIDE_CHECK(sent_with(uri_other) == NULL);
        TIDE_CHECK(has(sent[0], closed) && has(sent[0], "\"diagnostics\":[]"));
        TIDE_CHECK(has(request_at(uri_main, "textDocument/hover", 1, 16, ""), "singleton Body"));
        const unsigned changes = server.changes;
        close_uri(closed);
        TIDE_CHECK(server.changes == changes && sent_count == 1 && has(sent[0], "\"diagnostics\":[]"));
        lsp_settle(&server);
        TIDE_CHECK(sent_count == 1);
    }
    // Disk errors in the closed file mustn't resurrect its diagnostics.
    write_file("lsp_close/a/body.tide", "singleton Body { int value = missing; }\n");
    start_in_pair_with(root_a, root_b, "{}");
    open_uri(uri_main, main);
    open_uri(uri_body, body);
    close_uri(uri_body);
    TIDE_CHECK(sent_count == 2 && has(sent[0], uri_body) && has(sent[0], "\"diagnostics\":[]"));
    TIDE_REQUIRE(sent_with(uri_main) != NULL);
    TIDE_CHECK(has(sent_with(uri_main), "\"diagnostics\":[]"));
    TIDE_CHECK(!has(sent[1], "missing") && !has(sent[1], uri_body));
    write_file("lsp_close/a/body.tide", body);
    open_uri(uri_body, body);
    open_uri(uri_encoded, changed);
    close_uri(uri_body);
    TIDE_CHECK(server.doc_count == 2 && sent_count == 3);
    TIDE_REQUIRE(sent_with(uri_main) != NULL && sent_with(uri_encoded) != NULL);
    TIDE_CHECK(has(sent_with(uri_main), "unknown component or singleton 'Body'"));
    TIDE_CHECK(has(sent_with(uri_encoded), "\"diagnostics\":[]"));
    close_uri(uri_encoded);
    TIDE_REQUIRE(sent_with(uri_main) != NULL);
    TIDE_CHECK(has(sent_with(uri_main), "\"diagnostics\":[]"));
    remove_game_file("lsp_close/a/body.tide");
    remove_game_file("lsp_close/a/main.tide");
    remove_game_file("lsp_close/b/main.tide");
    remove_folder("lsp_close/a");
    remove_folder("lsp_close/b");
    remove_folder("lsp_close");
    clear_sent();
    lsp_free(&server);
}

// Asks the server what to rewrite before `old` is renamed to `now` (paths from
// game_dir), and returns the reply.
static const char *will_rename(const char *old, const char *now)
{
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    char message[2048];
    snprintf(message, sizeof message,
             "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/willRenameFiles\",\"params\":{\"files\":[{\"oldUri\":"
             "\"file:///%s/%s\",\"newUri\":\"file:///%s/%s\"}]}}",
             dir, old, dir, now);
    clear_sent();
    handle(message);
    return last_sent();
}

// Renaming a file or folder rewrites every asset path to it, each relative to its own file; globs aren't rewritten.
TIDE_TEST(lsp_asset_rename_rewrites_paths)
{
    if (!find_game_dir()) return;
    make_folder("lsp_as");
    make_folder("lsp_as/game");
    make_folder("lsp_as/game/a");
    make_folder("lsp_as/game/a/art");
    make_folder("lsp_as/game/a/img");
    make_folder("lsp_as/game/b");
    make_folder("lsp_as/pk");
    make_folder("lsp_as/pk/docs");
    static const char x_file[] = "asset Grid2<Color> hero = \"art/hero.png\";\n"
                                 "asset List<Grid2<Color>> run = \"art/*.png\";\n"
                                 "scene Main { }\n";
    static const char y_file[] = "asset List<byte> bytes = \"../a/art/hero.png\";\n";
    static const char p_file[] = "namespace Pk;\nasset string notes = \"notes.txt\";\n";
    write_file("lsp_as/game/tide.packages", "../pk\n");
    write_file("lsp_as/game/a/x.tide", x_file);
    write_file("lsp_as/game/b/y.tide", y_file);
    write_file("lsp_as/pk/tide.packages", "package Pk\n");
    write_file("lsp_as/pk/p.tide", p_file);
    write_bytes("lsp_as/game/a/art/hero.png", "PNG one", 7);
    write_file("lsp_as/pk/notes.txt", "hello\n");
    char root[700], uri_x[700], uri_y[700], uri_p[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_as/game", dir);
    snprintf(uri_x, sizeof uri_x, "file:///%s/lsp_as/game/a/x.tide", dir);
    snprintf(uri_y, sizeof uri_y, "file:///%s/lsp_as/game/b/y.tide", dir);
    snprintf(uri_p, sizeof uri_p, "file:///%s/lsp_as/pk/p.tide", dir);

    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    TIDE_CHECK(!has(last_sent(), "fileOperations"));
    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
    TIDE_CHECK(sent_count == 1 && has(sent[0], "{\"globPattern\":\"**/*.tide\"}"));

    open_uri(uri_x, x_file);
    const char *registered = sent_with("\"method\":\"workspace/willRenameFiles\"");
    TIDE_REQUIRE(registered != NULL);
    TIDE_CHECK(has(registered, "\"id\":\"tide-rename\"") && has(registered, "\"id\":\"tide-renamed\""));
    TIDE_CHECK(has(registered, "\"method\":\"workspace/didRenameFiles\""));
    TIDE_CHECK(has(registered, "lsp_as/game/a/art/hero.png\",\"matches\":\"file\",\"options\":{\"ignoreCase\":true}"));
    TIDE_CHECK(has(registered, "lsp_as/game/a/art\",\"matches\":\"folder\""));
    TIDE_CHECK(has(registered, "lsp_as/game/a\",\"matches\":\"folder\""));
    TIDE_CHECK(has(registered, "lsp_as/game\",\"matches\":\"folder\""));
    TIDE_CHECK(has(registered, "lsp_as/pk/notes.txt\",\"matches\":\"file\""));
    TIDE_CHECK(has(registered, "lsp_as/pk\",\"matches\":\"folder\""));
    TIDE_CHECK(!has(registered, "lsp_as\",\"matches\":\"folder\""));
    TIDE_CHECK(!has(registered, "*.png"));
    TIDE_CHECK(count(registered, "lsp_as/game/a/art/hero.png\",\"matches\":\"file\"") == 2);
    const char *watched = sent_with("\"id\":\"tide-assets\"");
    TIDE_REQUIRE(watched != NULL);
    TIDE_CHECK(has(watched, "lsp_as/game/a/art\",\"pattern\":\"hero.png\"}"));
    TIDE_CHECK(has(watched, "lsp_as/game/a/art\",\"pattern\":\"*.png\"}"));
    TIDE_CHECK(has(watched, "lsp_as/pk\",\"pattern\":\"notes.txt\"}"));
    TIDE_CHECK(!has(sent_with(uri_x), "there's no file"));
    TIDE_CHECK(!has(sent_with(uri_y), "there's no file"));
    TIDE_CHECK(!has(sent_with(uri_p), "there's no file"));

    // Nothing changed: no registration again
    open_uri(uri_y, y_file);
    TIDE_CHECK(sent_with("client/registerCapability") == NULL);

    const char *reply = will_rename("lsp_as/game/a/art/hero.png", "lsp_as/game/a/img/hero.png");
    TIDE_CHECK(has(reply, uri_x) && has(reply, uri_y) && !has(reply, uri_p));
    TIDE_CHECK(has(reply, "\"newText\":\"img/hero.png\"") && has(reply, "\"newText\":\"../a/img/hero.png\""));
    TIDE_CHECK(count(reply, "\"newText\"") == 2);
    TIDE_CHECK(has(reply, "\"range\":{\"start\":{\"line\":0,\"character\":27},\"end\":{\"line\":0,\"character\":39}}"));

    reply = will_rename("lsp_as/game/a/art", "lsp_as/game/a/img");
    TIDE_CHECK(has(reply, "\"newText\":\"img/hero.png\"") && has(reply, "\"newText\":\"../a/img/hero.png\""));
    TIDE_CHECK(count(reply, "\"newText\"") == 2);

    reply = will_rename("lsp_as/game/a", "lsp_as/game/c");
    TIDE_CHECK(!has(reply, uri_x) && has(reply, uri_y));
    TIDE_CHECK(has(reply, "\"newText\":\"../c/art/hero.png\"") && count(reply, "\"newText\"") == 1);

    reply = will_rename("lsp_as/pk/notes.txt", "lsp_as/pk/docs/notes.txt");
    TIDE_CHECK(has(reply, uri_p) && has(reply, "\"newText\":\"docs/notes.txt\"") && count(reply, "\"newText\"") == 1);

    reply = will_rename("lsp_as/game/b/other.txt", "lsp_as/game/b/else.txt");
    TIDE_CHECK(has(reply, "\"result\":{\"changes\":{}}"));

    clear_sent();
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didRenameFiles\",\"params\":{\"files\":[]}}");
    TIDE_CHECK(sent_with(uri_x) != NULL && sent_with(uri_y) != NULL);

    remove_game_file("lsp_as/game/tide.packages");
    remove_game_file("lsp_as/game/a/x.tide");
    remove_game_file("lsp_as/game/b/y.tide");
    remove_game_file("lsp_as/pk/tide.packages");
    remove_game_file("lsp_as/pk/p.tide");
    remove_game_file("lsp_as/game/a/art/hero.png");
    remove_game_file("lsp_as/pk/notes.txt");
    remove_game_file("lsp_as/game/.tide/assets.sha256");
    remove_game_file("lsp_as/game/.tide/.gitignore");
    remove_folder("lsp_as/game/.tide");
    remove_folder("lsp_as/game/a/art");
    remove_folder("lsp_as/game/a/img");
    remove_folder("lsp_as/game/a");
    remove_folder("lsp_as/game/b");
    remove_folder("lsp_as/game");
    remove_folder("lsp_as/pk/docs");
    remove_folder("lsp_as/pk");
    remove_folder("lsp_as");
    clear_sent();
    lsp_free(&server);
}

// A batch rename keeps every named file's path, even past 64 files in one request.
TIDE_TEST(lsp_asset_rename_large_batch)
{
    if (!find_game_dir()) return;
    enum { FILES = 70 };
    make_folder("lsp_batch");
    make_folder("lsp_batch/before");
    make_folder("lsp_batch/after");
    jbuf source = {0};
    for (int i = 0; i < FILES; i++) {
        char file[128];
        snprintf(file, sizeof file, "lsp_batch/before/file%d.bin", i);
        write_bytes(file, "data", 4);
        jb_printf(&source, "asset List<byte> file%d = \"before/file%d.bin\";\n", i, i);
    }
    jb_put(&source, "scene Main { }\n");
    write_file("lsp_batch/main.tide", source.data);
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    char root[700], uri_main[700];
    snprintf(root, sizeof root, "file:///%s/lsp_batch", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_batch/main.tide", dir);
    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    open_uri(uri_main, source.data);
    const char *diagnostics = sent_with(uri_main);
    TIDE_REQUIRE(diagnostics != NULL);
    TIDE_CHECK(has(diagnostics, "\"diagnostics\":[]"));

    jbuf request = {0};
    jb_put(&request, "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/willRenameFiles\",\"params\":{\"files\":[");
    for (int i = 0; i < FILES; i++) {
        if (i) jb_put(&request, ",");
        jb_printf(&request, "{\"oldUri\":\"file:///%s/lsp_batch/before/file%d.bin\","
                            "\"newUri\":\"file:///%s/lsp_batch/after/file%d.bin\"}", dir, i, dir, i);
    }
    jb_put(&request, "]}}");
    clear_sent();
    handle(request.data);
    TIDE_CHECK(count(last_sent(), "\"newText\"") == FILES);
    const char *edited = apply_reply(source.data, uri_main);
    for (int i = 0; i < FILES; i++) {
        char expected[128];
        snprintf(expected, sizeof expected, "asset List<byte> file%d = \"after/file%d.bin\";", i, i);
        TIDE_CHECK(has(edited, expected));
    }
    TIDE_CHECK(!has(edited, "before/"));

    for (int i = 0; i < FILES; i++) {
        char file[128];
        snprintf(file, sizeof file, "lsp_batch/before/file%d.bin", i);
        remove_game_file(file);
    }
    remove_game_file("lsp_batch/main.tide");
    remove_game_file("lsp_batch/.tide/assets.sha256");
    remove_game_file("lsp_batch/.tide/.gitignore");
    remove_folder("lsp_batch/.tide");
    remove_folder("lsp_batch/before");
    remove_folder("lsp_batch/after");
    remove_folder("lsp_batch");
    jb_free(&source);
    jb_free(&request);
    clear_sent();
    lsp_free(&server);
}

// One rename request can include separate games. Their shared package's
// document gets one edit array, containing the paths affected by the whole
// batch, regardless of which game's rename comes first.
TIDE_TEST(lsp_asset_rename_multiple_games)
{
    if (!find_game_dir()) return;
    make_folder("lsp_batch_games");
    make_folder("lsp_batch_games/a");
    make_folder("lsp_batch_games/a/art");
    make_folder("lsp_batch_games/a/sprites");
    make_folder("lsp_batch_games/b");
    make_folder("lsp_batch_games/b/art");
    make_folder("lsp_batch_games/pk");
    make_folder("lsp_batch_games/extra");
    static const char game[] = "asset List<byte> bytes = \"art/old.bin\";\n"
                               "asset List<byte> shared = \"../pk/old.bin\";\n"
                               "scene Main { }\n";
    static const char package[] = "namespace Pk;\n"
                                  "asset List<byte> data = \"old.bin\";\n"
                                  "asset List<byte> fromA = \"../a/art/old.bin\";\n"
                                  "asset List<byte> fromB = \"../b/art/old.bin\";\n"
                                  "#if PACKAGE_EXTRA\n"
                                  "asset List<byte> extra = \"../b/art/old.bin\";\n"
                                  "#else\n"
                                  "asset List<byte> base = \"../a/art/old.bin\";\n"
                                  "#endif\n";
    write_file("lsp_batch_games/a/tide.packages", "../pk\n");
    write_file("lsp_batch_games/b/tide.packages", "../pk\n../extra\n");
    write_file("lsp_batch_games/pk/tide.packages", "package Pk\n");
    write_file("lsp_batch_games/extra/tide.packages", "package Extra\n");
    write_file("lsp_batch_games/extra/main.tide", "namespace Extra;\n");
    write_file("lsp_batch_games/a/main.tide", game);
    write_file("lsp_batch_games/b/main.tide", game);
    write_file("lsp_batch_games/pk/main.tide", package);
    write_bytes("lsp_batch_games/a/art/old.bin", "a", 1);
    write_bytes("lsp_batch_games/b/art/old.bin", "b", 1);
    write_bytes("lsp_batch_games/pk/old.bin", "pk", 2);
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    char root_a[700], root_b[700], uri_a[700], uri_b[700], uri_pk[700];
    snprintf(root_a, sizeof root_a, "file:///%s/lsp_batch_games/a", dir);
    snprintf(root_b, sizeof root_b, "file:///%s/lsp_batch_games/b", dir);
    snprintf(uri_a, sizeof uri_a, "file:///%s/lsp_batch_games/a/main.tide", dir);
    snprintf(uri_b, sizeof uri_b, "file:///%s/lsp_batch_games/b/main.tide", dir);
    snprintf(uri_pk, sizeof uri_pk, "file:///%s/lsp_batch_games/pk/main.tide", dir);
    start_in_pair_with(root_a, root_b, ASSET_EDITOR_CAPABILITIES);
    open_uri(uri_a, game);
    TIDE_REQUIRE(sent_with(uri_a) != NULL);
    TIDE_CHECK(has(sent_with(uri_a), "\"diagnostics\":[]"));
    open_uri(uri_b, game);
    TIDE_REQUIRE(sent_with(uri_b) != NULL);
    TIDE_CHECK(has(sent_with(uri_b), "\"diagnostics\":[]"));

    static const char *const old[] = {"a/art", "b/art/old.bin", "pk/old.bin"};
    static const char *const moved[] = {"a/sprites", "b/art/new.bin", "pk/new.bin"};
    for (int reverse = 0; reverse < 2; reverse++) {
        jbuf request = {0};
        jb_put(&request, "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/willRenameFiles\",\"params\":{\"files\":[");
        for (int i = 0; i < 3; i++) {
            const int rename = reverse && i < 2 ? 1 - i : i;
            if (i) jb_put(&request, ",");
            jb_printf(&request, "{\"oldUri\":\"file:///%s/lsp_batch_games/%s\","
                                "\"newUri\":\"file:///%s/lsp_batch_games/%s\"}",
                      dir, old[rename], dir, moved[rename]);
        }
        jb_put(&request, "]}}");
        clear_sent();
        handle(request.data);
        const char *reply = last_sent();
        TIDE_CHECK(count(reply, uri_a) == 1 && count(reply, uri_b) == 1 && count(reply, uri_pk) == 1);
        TIDE_CHECK(count(reply, "\"newText\"") == 9);
        const char *edited = apply_reply(game, uri_a);
        TIDE_CHECK(has(edited, "bytes = \"sprites/old.bin\";") && has(edited, "shared = \"../pk/new.bin\";"));
        edited = apply_reply(game, uri_b);
        TIDE_CHECK(has(edited, "bytes = \"art/new.bin\";") && has(edited, "shared = \"../pk/new.bin\";"));
        edited = apply_reply(package, uri_pk);
        TIDE_CHECK(has(edited, "data = \"new.bin\";") && has(edited, "fromA = \"../a/sprites/old.bin\";")
                   && has(edited, "fromB = \"../b/art/new.bin\";"));
        TIDE_CHECK(has(edited, "extra = \"../b/art/new.bin\";") && has(edited, "base = \"../a/sprites/old.bin\";"));
        jb_free(&request);
    }

    for (int reverse = 0; reverse < 2; reverse++) {
        start_in_pair_with(reverse ? root_b : root_a, reverse ? root_a : root_b, ASSET_EDITOR_CAPABILITIES);
        open_uri(uri_a, game);
        open_uri(uri_b, game);
        const char *reply = will_rename("lsp_batch_games/pk/old.bin", "lsp_batch_games/pk/new.bin");
        TIDE_CHECK(count(reply, uri_a) == 1 && count(reply, uri_b) == 1 && count(reply, uri_pk) == 1);
        TIDE_CHECK(count(reply, "\"newText\"") == 3);
        const char *edited = apply_reply(game, uri_a);
        TIDE_CHECK(has(edited, "bytes = \"art/old.bin\";") && has(edited, "shared = \"../pk/new.bin\";"));
        edited = apply_reply(game, uri_b);
        TIDE_CHECK(has(edited, "bytes = \"art/old.bin\";") && has(edited, "shared = \"../pk/new.bin\";"));
        edited = apply_reply(package, uri_pk);
        TIDE_CHECK(has(edited, "data = \"new.bin\";") && has(edited, "fromA = \"../a/art/old.bin\";")
                   && has(edited, "fromB = \"../b/art/old.bin\";"));
    }

    remove_game_file("lsp_batch_games/a/tide.packages");
    remove_game_file("lsp_batch_games/b/tide.packages");
    remove_game_file("lsp_batch_games/pk/tide.packages");
    remove_game_file("lsp_batch_games/extra/tide.packages");
    remove_game_file("lsp_batch_games/extra/main.tide");
    remove_game_file("lsp_batch_games/a/main.tide");
    remove_game_file("lsp_batch_games/b/main.tide");
    remove_game_file("lsp_batch_games/pk/main.tide");
    remove_game_file("lsp_batch_games/a/art/old.bin");
    remove_game_file("lsp_batch_games/b/art/old.bin");
    remove_game_file("lsp_batch_games/pk/old.bin");
    remove_game_file("lsp_batch_games/a/.tide/assets.sha256");
    remove_game_file("lsp_batch_games/a/.tide/.gitignore");
    remove_game_file("lsp_batch_games/b/.tide/assets.sha256");
    remove_game_file("lsp_batch_games/b/.tide/.gitignore");
    remove_folder("lsp_batch_games/a/.tide");
    remove_folder("lsp_batch_games/b/.tide");
    remove_folder("lsp_batch_games/a/art");
    remove_folder("lsp_batch_games/a/sprites");
    remove_folder("lsp_batch_games/b/art");
    remove_folder("lsp_batch_games/a");
    remove_folder("lsp_batch_games/b");
    remove_folder("lsp_batch_games/pk");
    remove_folder("lsp_batch_games/extra");
    remove_folder("lsp_batch_games");
    clear_sent();
    lsp_free(&server);
}

// Assets aren't source entries in a CMake game's manifest. Refactoring
// discovers the game through those entries, whether they list individual
// SOURCES or a folder, without treating the asset itself as a Tide file.
TIDE_TEST(lsp_asset_rename_manifest_games)
{
    if (!find_game_dir()) return;
    make_folder("lsp_batch_manifest");
    make_folder("lsp_batch_manifest/a");
    make_folder("lsp_batch_manifest/a/art");
    make_folder("lsp_batch_manifest/a/src");
    make_folder("lsp_batch_manifest/b");
    make_folder("lsp_batch_manifest/b/art");
    make_folder("lsp_batch_manifest/build");
    make_folder("lsp_batch_manifest/build/tools");
    static const char game[] = "asset List<byte> bytes = \"art/old.bin\";\nscene Main { }\n";
    write_file("lsp_batch_manifest/a/main.tide", game);
    write_file("lsp_batch_manifest/b/main.tide", game);
    write_bytes("lsp_batch_manifest/a/art/old.bin", "a", 1);
    write_bytes("lsp_batch_manifest/b/art/old.bin", "b", 1);
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    char root[700], uri_a[700], uri_b[700];
    snprintf(root, sizeof root, "file:///%s/lsp_batch_manifest", dir);
    snprintf(uri_a, sizeof uri_a, "file:///%s/lsp_batch_manifest/a/main.tide", dir);
    snprintf(uri_b, sizeof uri_b, "file:///%s/lsp_batch_manifest/b/main.tide", dir);
    for (int folders = 0; folders < 2; folders++) {
        jbuf manifest = {0};
        jb_printf(&manifest, "a\t%s/lsp_batch_manifest/a/%s\nb\t%s/lsp_batch_manifest/b/%s\n",
                  game_dir, folders ? "" : "main.tide", game_dir, folders ? "" : "main.tide");
        write_file("lsp_batch_manifest/build/tools/games.txt", manifest.data);
        jb_free(&manifest);
        start_in_with(root, ASSET_EDITOR_CAPABILITIES);
        // Discovery must work from the manifest before any file is open.
        const char *reply = will_rename("lsp_batch_manifest/a/art/old.bin", "lsp_batch_manifest/a/art/new.bin");
        TIDE_CHECK(count(reply, uri_a) == 1 && !has(reply, uri_b) && count(reply, "\"newText\"") == 1);
        TIDE_CHECK(has(apply_reply(game, uri_a), "bytes = \"art/new.bin\";"));
        open_uri(uri_a, game);
        TIDE_REQUIRE(sent_with(uri_a) != NULL);
        TIDE_CHECK(has(sent_with(uri_a), "\"diagnostics\":[]"));
        open_uri(uri_b, game);
        TIDE_REQUIRE(sent_with(uri_b) != NULL);
        TIDE_CHECK(has(sent_with(uri_b), "\"diagnostics\":[]"));

        reply = will_rename("lsp_batch_manifest/a/art/old.bin", "lsp_batch_manifest/a/art/new.bin");
        TIDE_CHECK(count(reply, uri_a) == 1 && !has(reply, uri_b) && count(reply, "\"newText\"") == 1);
        TIDE_CHECK(has(apply_reply(game, uri_a), "bytes = \"art/new.bin\";"));

        reply = will_rename("lsp_batch_manifest/a/main.tide", "lsp_batch_manifest/a/src/main.tide");
        TIDE_CHECK(count(reply, uri_a) == 1 && !has(reply, uri_b) && count(reply, "\"newText\"") == 1);
        TIDE_CHECK(has(apply_reply(game, uri_a), "bytes = \"../art/old.bin\";"));
    }
    remove_game_file("lsp_batch_manifest/a/main.tide");
    remove_game_file("lsp_batch_manifest/b/main.tide");
    remove_game_file("lsp_batch_manifest/a/art/old.bin");
    remove_game_file("lsp_batch_manifest/b/art/old.bin");
    remove_game_file("lsp_batch_manifest/build/tools/games.txt");
    remove_folder("lsp_batch_manifest/a/art");
    remove_folder("lsp_batch_manifest/a/src");
    remove_folder("lsp_batch_manifest/b/art");
    remove_folder("lsp_batch_manifest/a");
    remove_folder("lsp_batch_manifest/b");
    remove_folder("lsp_batch_manifest/build/tools");
    remove_folder("lsp_batch_manifest/build");
    remove_folder("lsp_batch_manifest");
    clear_sent();
    lsp_free(&server);
}

// The named files' hashes go in the game's .tide/ when it's checked; a missing file is found again by its bytes, or by name.
TIDE_TEST(lsp_asset_moved_file_is_found_by_its_bytes)
{
    if (!find_game_dir()) return;
    make_folder("lsp_mv");
    make_folder("lsp_mv/data");
    make_folder("lsp_mv/blobs");
    make_folder("lsp_mv/old");
    make_folder("lsp_mv/older");
    static const char main_file[] = "asset List<byte> table = \"data/table.bin\";\nscene Main { }\n";
    static const char bytes[] = {1, 2, 3, 4, 5, 6, 7, 0, 9};
    write_file("lsp_mv/main.tide", main_file);
    write_bytes("lsp_mv/data/table.bin", bytes, sizeof bytes);
    char root[700], uri_main[700], store[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_mv", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_mv/main.tide", dir);
    game_path(store, sizeof store, "lsp_mv/.tide/assets.sha256");

    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    open_uri(uri_main, main_file);
    TIDE_CHECK(!has(sent_with(uri_main), "there's no file"));
    char hex[65];
    asset_hash_of(bytes, sizeof bytes, hex);
    size_t len = 0;
    char *text = NULL;
    {
        FILE *f = fopen(store, "rb");
        TIDE_REQUIRE(f != NULL);
        char buffer[1024];
        len = fread(buffer, 1, sizeof buffer - 1, f);
        fclose(f);
        buffer[len] = '\0';
        text = buffer;
        char line[160];
        snprintf(line, sizeof line, "%s 9 data/table.bin\n", hex);
        TIDE_CHECK(has(text, line));
        TIDE_CHECK(has(text, "# The SHA-256 of each file an asset names"));
    }
    TIDE_CHECK(game_file_exists("lsp_mv/.tide/.gitignore"));

    move_game_file("lsp_mv/data/table.bin", "lsp_mv/blobs/numbers.bin");
    open_uri(uri_main, main_file);
    const char *diagnostics = sent_with(uri_main);
    TIDE_REQUIRE(diagnostics != NULL);
    TIDE_CHECK(has(diagnostics, "there's no file at 'data/table.bin' ("));
    TIDE_CHECK(has(diagnostics, "note: a file with the same bytes is at 'blobs/numbers.bin'"));
    TIDE_CHECK(has(diagnostics, "\"range\":{\"start\":{\"line\":0,\"character\":25}"));
    const char *actions = request_at(uri_main, "textDocument/codeAction", 0, 0,
                                     "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":0}},\"context\":{\"diagnostics\":[]}");
    TIDE_CHECK(has(actions, "\"title\":\"Change to 'blobs/numbers.bin'\",\"kind\":\"quickfix\",\"isPreferred\":true"));
    TIDE_CHECK(has(actions, "\"range\":{\"start\":{\"line\":0,\"character\":26},\"end\":{\"line\":0,\"character\":40}},\"newText\":\"blobs/numbers.bin\""));
    TIDE_CHECK(has(actions, "there's no file at"));
    {
        FILE *f = fopen(store, "rb");
        TIDE_REQUIRE(f != NULL);
        char buffer[1024];
        const size_t n = fread(buffer, 1, sizeof buffer - 1, f);
        fclose(f);
        buffer[n] = '\0';
        TIDE_CHECK(has(buffer, " 9 data/table.bin\n"));
    }

    remove_game_file("lsp_mv/blobs/numbers.bin");
    write_file("lsp_mv/old/table.bin", "other");
    write_file("lsp_mv/older/table.bin", "another");
    open_uri(uri_main, main_file);
    diagnostics = sent_with(uri_main);
    TIDE_REQUIRE(diagnostics != NULL);
    TIDE_CHECK(has(diagnostics, "note: files of that name: 'old/table.bin', 'older/table.bin'"));
    actions = request_at(uri_main, "textDocument/codeAction", 0, 0,
                         "\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":0}},\"context\":{\"diagnostics\":[]}");
    TIDE_CHECK(has(actions, "\"title\":\"Change to 'old/table.bin'\",\"kind\":\"quickfix\",\"isPreferred\":false"));
    TIDE_CHECK(has(actions, "\"title\":\"Change to 'older/table.bin'\",\"kind\":\"quickfix\",\"isPreferred\":false"));

    static const char glob_file[] = "asset List<Grid2<Color>> frames = \"data/*.png\";\nscene Main { }\n";
    open_uri(uri_main, glob_file);
    TIDE_CHECK(has(sent_with(uri_main), "'data/*.png' matches no file"));
    write_bytes("lsp_mv/data/a.png", "png", 3);
    open_uri(uri_main, glob_file);
    TIDE_CHECK(!has(sent_with(uri_main), "matches no file"));

    remove_game_file("lsp_mv/data/a.png");
    remove_game_file("lsp_mv/old/table.bin");
    remove_game_file("lsp_mv/older/table.bin");
    remove_game_file("lsp_mv/main.tide");
    remove_game_file("lsp_mv/.tide/assets.sha256");
    remove_game_file("lsp_mv/.tide/.gitignore");
    remove_folder("lsp_mv/.tide");
    remove_folder("lsp_mv/data");
    remove_folder("lsp_mv/blobs");
    remove_folder("lsp_mv/old");
    remove_folder("lsp_mv/older");
    remove_folder("lsp_mv");
    clear_sent();
    lsp_free(&server);
}

// One file where an asset's type holds many (a list or map): a single file's path to the editor.
TIDE_TEST(lsp_asset_one_file_for_a_list_or_a_map)
{
    if (!find_game_dir()) return;
    make_folder("lsp_one");
    make_folder("lsp_one/data");
    make_folder("lsp_one/moved");
    static const char main_file[] = "asset List<string> notes = \"data/a.txt\";\n"
                                    "asset Map<string, List<byte>> tables = \"data/t.bin\";\n"
                                    "scene Main { }\n"
                                    "singleton Seen { int notes; bool table; }\n"
                                    "system Look(mut Seen seen)\n{\n"
                                    "    seen.notes = notes.count;\n"
                                    "    seen.table = tables.Has(\"data/t.bin\");\n}\n";
    static const char bytes[] = {9, 8, 7, 6, 5, 0, 3};
    write_file("lsp_one/main.tide", main_file);
    write_file("lsp_one/data/a.txt", "alpha");
    write_bytes("lsp_one/data/t.bin", bytes, sizeof bytes);
    char root[700], uri_main[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_one", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_one/main.tide", dir);

    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    open_uri(uri_main, main_file);
    const char *diagnostics = sent_with(uri_main);
    TIDE_REQUIRE(diagnostics != NULL);
    TIDE_CHECK(has(diagnostics, "\"diagnostics\":[]"));

    const char *reply = will_rename("lsp_one/data/a.txt", "lsp_one/moved/a.txt");
    TIDE_CHECK(has(reply, "\"newText\":\"moved/a.txt\"") && count(reply, "\"newText\"") == 1);
    reply = will_rename("lsp_one/data", "lsp_one/moved");
    TIDE_CHECK(has(reply, "\"newText\":\"moved/a.txt\"") && has(reply, "\"newText\":\"moved/t.bin\""));
    TIDE_CHECK(count(reply, "\"newText\"") == 2);

    move_game_file("lsp_one/data/t.bin", "lsp_one/moved/t.bin");
    open_uri(uri_main, main_file);
    diagnostics = sent_with(uri_main);
    TIDE_REQUIRE(diagnostics != NULL);
    TIDE_CHECK(has(diagnostics, "there's no file at 'data/t.bin' ("));
    TIDE_CHECK(has(diagnostics, "note: a file with the same bytes is at 'moved/t.bin'"));
    const char *actions = request_at(uri_main, "textDocument/codeAction", 1, 0,
                                     "\"range\":{\"start\":{\"line\":1,\"character\":0},\"end\":{\"line\":1,\"character\":0}},\"context\":{\"diagnostics\":[]}");
    TIDE_CHECK(has(actions, "\"title\":\"Change to 'moved/t.bin'\",\"kind\":\"quickfix\",\"isPreferred\":true"));

    remove_game_file("lsp_one/main.tide");
    remove_game_file("lsp_one/data/a.txt");
    remove_game_file("lsp_one/moved/t.bin");
    remove_game_file("lsp_one/.tide/assets.sha256");
    remove_game_file("lsp_one/.tide/.gitignore");
    remove_folder("lsp_one/.tide");
    remove_folder("lsp_one/data");
    remove_folder("lsp_one/moved");
    remove_folder("lsp_one");
    clear_sent();
    lsp_free(&server);
}

// Completion inside an asset's path: folders and files where the path leads, the type's formats, and globs for a list or map.
TIDE_TEST(lsp_asset_path_completion)
{
    if (!find_game_dir()) return;
    make_folder("lsp_cp");
    make_folder("lsp_cp/art");
    make_folder("lsp_cp/art/sub");
    static const char main_file[] = "asset Grid2<Color> hero = \"\";\n"
                                    "asset List<Grid2<Color>> run = \"art/\";\n"
                                    "asset string notes = \"art/\";\n"
                                    "asset Clip shot = \"art/\";\n"
                                    "asset Texture tile = \"art/\";\n"
                                    "asset Map<string, Texture> images = \"art/\";\n"
                                    "asset Cubemap sky = \"art/\";\n"
                                    "asset List<Clip> sounds = \"art/\";\n"
                                    "scene Main { }\n";
    write_file("lsp_cp/main.tide", main_file);
    write_bytes("lsp_cp/art/hero.png", "png", 3);
    write_bytes("lsp_cp/art/hero.psd", "psd", 3);
    write_bytes("lsp_cp/art/tile.PSD", "psd", 3);
    static const char *const image_names[] = {"photo.jpg", "photo.JPEG", "tile.tga", "tile.bmp"};
    for (size_t i = 0; i < sizeof image_names / sizeof image_names[0]; i++) {
        char path[100];
        snprintf(path, sizeof path, "lsp_cp/art/%s", image_names[i]);
        write_bytes(path, "image", 5);
    }
    write_file("lsp_cp/art/other.txt", "text");
    write_bytes("lsp_cp/art/shot.wav", "wav", 3);
    write_bytes("lsp_cp/art/music.ogg", "ogg", 3);
    write_bytes("lsp_cp/art/music.MP3", "mp3", 3);
    write_bytes("lsp_cp/art/music.flac", "flac", 4);
    write_file("lsp_cp/readme.txt", "text");
    char root[700], uri_main[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_cp", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_cp/main.tide", dir);

    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    open_uri(uri_main, main_file);

    const char *completion = request_at(uri_main, "textDocument/completion", 0, 27, "");
    TIDE_CHECK(offers(completion, "art/") && offers(completion, "../"));
    TIDE_CHECK(!offers(completion, "readme.txt") && !offers(completion, "main.tide"));
    TIDE_CHECK(!offers(completion, "*.png") && !offers(completion, "*.psd"));
    TIDE_CHECK(has(completion, "\"kind\":19"));
    TIDE_CHECK(has(completion, "\"textEdit\":{\"range\":{\"start\":{\"line\":0,\"character\":27},\"end\":{\"line\":0,\"character\":27}},\"newText\":\"art/\"}"));

    completion = request_at(uri_main, "textDocument/completion", 1, 36, "");
    TIDE_CHECK(offers(completion, "hero.png") && offers(completion, "hero.psd") && offers(completion, "tile.PSD") && offers(completion, "sub/"));
    TIDE_CHECK(!offers(completion, "other.txt"));
    TIDE_CHECK(offers(completion, "*.png") && offers(completion, "**/*.png"));
    TIDE_CHECK(offers(completion, "*.psd") && offers(completion, "**/*.psd"));
    for (size_t i = 0; i < sizeof image_names / sizeof image_names[0]; i++) TIDE_CHECK(offers(completion, image_names[i]));
    TIDE_CHECK(offers(completion, "*.jpg") && offers(completion, "**/*.jpeg") && offers(completion, "*.tga") && offers(completion, "**/*.bmp"));
    TIDE_CHECK(has(completion, "\"textEdit\":{\"range\":{\"start\":{\"line\":1,\"character\":36},\"end\":{\"line\":1,\"character\":36}},\"newText\":\"hero.png\"}"));

    completion = request_at(uri_main, "textDocument/completion", 2, 26, "");
    TIDE_CHECK(offers(completion, "hero.png") && offers(completion, "hero.psd") && offers(completion, "other.txt"));
    TIDE_CHECK(!offers(completion, "*") && !offers(completion, "**/*"));

    completion = request_at(uri_main, "textDocument/completion", 3, 23, "");
    TIDE_CHECK(offers(completion, "shot.wav") && offers(completion, "music.ogg"));
    TIDE_CHECK(offers(completion, "music.MP3") && offers(completion, "music.flac"));
    TIDE_CHECK(!offers(completion, "hero.png") && !offers(completion, "hero.psd") && !offers(completion, "other.txt"));

    completion = request_at(uri_main, "textDocument/completion", 4, 26, "");
    TIDE_CHECK(offers(completion, "hero.png") && offers(completion, "hero.psd") && offers(completion, "tile.PSD"));
    for (size_t i = 0; i < sizeof image_names / sizeof image_names[0]; i++) TIDE_CHECK(offers(completion, image_names[i]));
    TIDE_CHECK(!offers(completion, "other.txt") && !offers(completion, "*.psd"));
    completion = request_at(uri_main, "textDocument/completion", 5, 41, "");
    TIDE_CHECK(offers(completion, "hero.psd") && offers(completion, "*.psd") && offers(completion, "**/*.psd"));
    TIDE_CHECK(offers(completion, "*.jpg") && offers(completion, "**/*.tga"));
    completion = request_at(uri_main, "textDocument/completion", 6, 25, "");
    TIDE_CHECK(offers(completion, "hero.png") && offers(completion, "hero.psd") && offers(completion, "*.psd"));
    TIDE_CHECK(offers(completion, "photo.JPEG") && offers(completion, "*.bmp"));
    TIDE_CHECK(!offers(completion, "other.txt"));

    completion = request_at(uri_main, "textDocument/completion", 7, 31, "");
    TIDE_CHECK(offers(completion, "*.wav") && offers(completion, "**/*.ogg")
               && offers(completion, "*.mp3") && offers(completion, "**/*.flac"));

    open_uri(uri_main, "asset Grid2<Color> hero = \"art/he\";\nscene Main { }\n");
    completion = request_at(uri_main, "textDocument/completion", 0, 33, "");
    TIDE_CHECK(offers(completion, "hero.png"));
    TIDE_CHECK(offers(completion, "hero.psd"));
    TIDE_CHECK(has(completion, "\"range\":{\"start\":{\"line\":0,\"character\":31},\"end\":{\"line\":0,\"character\":33}},\"newText\":\"hero.png\""));
    check_repeat_at(uri_main, "textDocument/completion", 0, 33, "");
    TIDE_CHECK(has(request_at(uri_main, "textDocument/hover", 1, 8, ""), "scene Main"));

    remove_game_file("lsp_cp/main.tide");
    remove_game_file("lsp_cp/art/hero.png");
    remove_game_file("lsp_cp/art/hero.psd");
    remove_game_file("lsp_cp/art/tile.PSD");
    for (size_t i = 0; i < sizeof image_names / sizeof image_names[0]; i++) {
        char path[100];
        snprintf(path, sizeof path, "lsp_cp/art/%s", image_names[i]);
        remove_game_file(path);
    }
    remove_game_file("lsp_cp/art/shot.wav");
    remove_game_file("lsp_cp/art/music.ogg");
    remove_game_file("lsp_cp/art/music.MP3");
    remove_game_file("lsp_cp/art/music.flac");
    remove_game_file("lsp_cp/art/other.txt");
    remove_game_file("lsp_cp/readme.txt");
    remove_game_file("lsp_cp/.tide/assets.sha256");
    remove_game_file("lsp_cp/.tide/.gitignore");
    remove_folder("lsp_cp/.tide");
    remove_folder("lsp_cp/art/sub");
    remove_folder("lsp_cp/art");
    remove_folder("lsp_cp");
    clear_sent();
    lsp_free(&server);
}

// Texture assets, and [Filter] and [Wrap] before them: hovers, brackets, PNGs in a Texture's path, and diagnostics.
TIDE_TEST(lsp_asset_textures_and_declared_sampling)
{
    if (!find_game_dir()) return;
    make_folder("lsp_tx");
    make_folder("lsp_tx/art");
    static const unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x12, 0x49,
        0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81, 0x34, 0x18, 0x00, 0x00, 0x49, 0xc8,
        0x09, 0xf7, 0x03, 0xd9, 0x64, 0xf1, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    static const char main_file[] = "[Filter(Filter.Point), Wrap(Wrap.Repeat)]\n"
                                    "asset Texture tiles = \"art/tile.png\";\n"
                                    "[Wrap(Wrap.Mirror)]\n"
                                    "asset Grid2<Color> glass = \"art/tile.png\";\n"
                                    "asset Texture plain = \"art/tile.png\";\n"
                                    "scene Main { }\n"
                                    "local singleton Art\n"
                                    "{\n"
                                    "    List<Vertex> flat;\n"
                                    "    [Filter(Filter.Point)] Grid2<Color> paint = Grid2(2, 2);\n"
                                    "    Texture kept;\n"
                                    "}\n"
                                    "view V(mut Art art)\n"
                                    "{\n"
                                    "    art.kept = tiles;\n"
                                    "    Draw.Mesh(art.flat, [0, 1, 2], tiles);\n"
                                    "    Draw.Mesh(art.flat, [0, 1, 2], glass);\n"
                                    "    Draw.Mesh(art.flat, [0, 1, 2], art.paint);\n"
                                    "    Draw.Mesh(art.flat, [0, 1, 2], plain);\n"
                                    "}\n";
    write_file("lsp_tx/main.tide", main_file);
    write_bytes("lsp_tx/art/tile.png", (const char *)png, sizeof png);
    write_file("lsp_tx/art/other.txt", "text");
    char root[700], uri_main[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_tx", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_tx/main.tide", dir);

    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    open_uri(uri_main, main_file);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *hover = request_at(uri_main, "textDocument/hover", 0, 3, "");
    TIDE_CHECK(has(hover, "[Filter(Filter.Point)]") && has(hover, "How this texture is read between its cells"));
    hover = request_at(uri_main, "textDocument/hover", 0, 17, "");
    TIDE_CHECK(has(hover, "Filter.Point = 0"));
    hover = request_at(uri_main, "textDocument/hover", 0, 24, "");
    TIDE_CHECK(has(hover, "[Wrap(Wrap.Repeat)]") && has(hover, "past this texture's edges"));
    hover = request_at(uri_main, "textDocument/hover", 0, 34, "");
    TIDE_CHECK(has(hover, "Wrap.Repeat = 1"));
    hover = request_at(uri_main, "textDocument/hover", 9, 7, "");
    TIDE_CHECK(has(hover, "[Filter(Filter.Point)]"));

    hover = request_at(uri_main, "textDocument/hover", 1, 16, "");
    TIDE_CHECK(has(hover, "asset Texture tiles") && has(hover, "an image to draw with"));
    TIDE_CHECK(has(hover, "it's sampled with `Filter.Point` and `Wrap.Repeat`."));
    hover = request_at(uri_main, "textDocument/hover", 3, 21, "");
    TIDE_CHECK(has(hover, "asset Grid2<Color> glass") && has(hover, "it's sampled with `Wrap.Mirror`.") && !has(hover, "an image to draw with"));
    hover = request_at(uri_main, "textDocument/hover", 4, 16, "");
    TIDE_CHECK(has(hover, "asset Texture plain") && !has(hover, "it's sampled with"));
    hover = request_at(uri_main, "textDocument/hover", 15, 39, "");
    TIDE_CHECK(has(hover, "asset Texture tiles") && has(hover, "`Filter.Point` and `Wrap.Repeat`"));

    static const char shader_assets[] = "asset Texture logo = \"art/tile.png\";\n"
                                        "asset List<Texture> frames = \"art/*.png\";\n"
                                        "asset Map<string, Texture> images = \"art/*.png\";\n"
                                        "asset string notes = \"art/other.txt\";\n"
                                        "const float SCALE = 1;\n"
                                        "scene Main { }\n";
    char asset_code[2048];
    snprintf(asset_code, sizeof asset_code, "%sfrag Frag(Vertex3 vertex, out Color color)\n{\n"
                                           "    Color logoColor = logo.Sample(vertex.position.xy);\n"
                                           "    color = logoColor * SCALE;\n}\n"
                                           "view V() { Draw.Mesh([Vertex3 { }], [0, 0, 0], float4x4.identity, Frag); }\n", shader_assets);
    open_uri(uri_main, asset_code);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    hover = request_at(uri_main, "textDocument/hover", 8, 24, "");
    TIDE_CHECK(has(hover, "asset Texture logo") && has(hover, "an image to draw with"));
    hover = request_at(uri_main, "textDocument/hover", 8, 30, "");
    TIDE_CHECK(has(hover, "Texture.Sample(float2 uv") && !has(hover, "entity.Sample"));
    const char *tokens = request_at(uri_main, "textDocument/semanticTokens/full", 0, 0, "");
    TIDE_CHECK(token_is(tokens, 0, 14, 6, 3));
    TIDE_CHECK(token_is(tokens, 8, 22, 6, 2));
    TIDE_CHECK(token_is(tokens, 8, 27, 10, 8));
    TIDE_CHECK(token_is(tokens, 4, 12, 8, 7));

    snprintf(asset_code, sizeof asset_code, "%sfrag Frag(Vertex3 vertex, out Color color)\n{\n"
                                           "    Color logoColor = Color.white;\n    lo\n}\n", shader_assets);
    open_uri(uri_main, asset_code);
    const char *asset_completion = request_at(uri_main, "textDocument/completion", 9, 6, "");
    TIDE_CHECK(offers(asset_completion, "logo") && offers(asset_completion, "logoColor"));
    TIDE_CHECK(has(asset_completion, "\"label\":\"logo\",\"kind\":6,\"detail\":\"asset Texture\""));
    TIDE_CHECK(!offers(asset_completion, "frames") && !offers(asset_completion, "images") && !offers(asset_completion, "notes"));
    snprintf(asset_code, sizeof asset_code, "%sfrag Frag(Vertex3 vertex, out Color color)\n{\n    color = logo.\n}\n", shader_assets);
    open_uri(uri_main, asset_code);
    asset_completion = request_at(uri_main, "textDocument/completion", 8, 17, "");
    TIDE_CHECK(offers(asset_completion, "Sample") && has(asset_completion, "Texture.Sample(float2 uv"));
    TIDE_CHECK(!offers(asset_completion, "Add") && !offers(asset_completion, "Clear"));
    snprintf(asset_code, sizeof asset_code, "%sfrag Frag(Vertex3 vertex, out Color color)\n{\n    color = logo.Sample(\n}\n", shader_assets);
    open_uri(uri_main, asset_code);
    TIDE_CHECK(has(request_at(uri_main, "textDocument/signatureHelp", 8, 24, ""), "Texture.Sample(float2 uv"));

    snprintf(asset_code, sizeof asset_code, "%sview V()\n{\n    var count = frames.\n}\n", shader_assets);
    open_uri(uri_main, asset_code);
    asset_completion = request_at(uri_main, "textDocument/completion", 8, 23, "");
    TIDE_CHECK(offers(asset_completion, "count") && offers(asset_completion, "Contains"));
    TIDE_CHECK(!offers(asset_completion, "Sample"));
    snprintf(asset_code, sizeof asset_code, "%sview V()\n{\n    var present = images.\n}\n", shader_assets);
    open_uri(uri_main, asset_code);
    asset_completion = request_at(uri_main, "textDocument/completion", 8, 25, "");
    TIDE_CHECK(offers(asset_completion, "Has") && offers(asset_completion, "TryGet"));
    TIDE_CHECK(!offers(asset_completion, "Sample"));

    static const char namespaced_asset[] = "namespace Art { asset Texture logo = \"art/tile.png\"; }\nscene Main { }\n";
    snprintf(asset_code, sizeof asset_code, "%sfrag Frag(Vertex3 vertex, out Color color)\n{\n    color = Art.logo.\n}\n", namespaced_asset);
    open_uri(uri_main, asset_code);
    TIDE_CHECK(offers(request_at(uri_main, "textDocument/completion", 4, 21, ""), "Sample"));
    snprintf(asset_code, sizeof asset_code, "%sfrag Frag(Vertex3 vertex, out Color color)\n{\n    color = Art.logo.Sample(\n}\n", namespaced_asset);
    open_uri(uri_main, asset_code);
    TIDE_CHECK(has(request_at(uri_main, "textDocument/signatureHelp", 4, 28, ""), "Texture.Sample(float2 uv"));

    char uri_icons[700];
    snprintf(uri_icons, sizeof uri_icons, "file:///%s/lsp_tx/icons.tide", dir);
    static const char icons_file[] = "namespace Icons;\nasset Texture badge = \"art/tile.png\";\n";
    write_file("lsp_tx/icons.tide", icons_file);
    open_uri(uri_icons, icons_file);
    open_uri(uri_main, "scene Main { }\nfrag Frag(Vertex3 vertex, out Color color)\n{\n    color = Icons.badge.\n}\n");
    TIDE_CHECK(offers(request_at(uri_main, "textDocument/completion", 3, 24, ""), "Sample"));
    open_uri(uri_main, "using Icons;\nscene Main { }\nfrag Frag(Vertex3 vertex, out Color color)\n{\n    color = badge.Sample(\n}\n");
    TIDE_CHECK(has(request_at(uri_main, "textDocument/signatureHelp", 4, 25, ""), "Texture.Sample(float2 uv"));

    open_uri(uri_main, "asset Texture missing = \"art/missing.png\";\nscene Main { }\n"
                       "frag Frag(Vertex3 vertex, out Color color)\n{\n    color = missing.\n}\n");
    TIDE_CHECK(has(sent_with(uri_main), "there's no file at 'art/missing.png'"));
    TIDE_CHECK(offers(request_at(uri_main, "textDocument/completion", 4, 20, ""), "Sample"));

    static const char paths[] = "asset Texture one = \"art/\";\n"
                                "asset List<Texture> many = \"art/\";\n"
                                "scene Main { }\n";
    open_uri(uri_main, paths);
    const char *completion = request_at(uri_main, "textDocument/completion", 0, 25, "");
    TIDE_CHECK(offers(completion, "tile.png") && !offers(completion, "other.txt") && !offers(completion, "*.png"));
    completion = request_at(uri_main, "textDocument/completion", 1, 32, "");
    TIDE_CHECK(offers(completion, "tile.png") && !offers(completion, "other.txt"));
    TIDE_CHECK(offers(completion, "*.png") && offers(completion, "**/*.png"));

    open_uri(uri_main, "scene Main { }\nlocal singleton Art\n{\n    [Filter(Filter.Point)] Texture kept;\n}\n");
    TIDE_CHECK(has(last_sent(), "a Texture says how it's sampled itself, and a field holds whichever it's given"));
    TIDE_CHECK(has(last_sent(), "say it where the texture is made"));
    open_uri(uri_main, "scene Main { }\nlocal singleton Art\n{\n    [Filter(Filter.Point)] RenderTarget small;\n"
                       "    [Wrap(Wrap.Repeat)] Grid2<float> heights = Grid2(2, 2);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    open_uri(uri_main, "[Wrap(Wrap.Repeat), Wrap(Wrap.Mirror)]\nasset Texture tiles = \"art/tile.png\";\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "'tiles' already has a Wrap"));
    open_uri(uri_main, "scene Main { }\nlocal singleton Art\n{\n    List<Texture> frames;\n}\n");
    TIDE_CHECK(has(last_sent(), "a list or a map can't hold rendering resources"));
    TIDE_CHECK(has(last_sent(), "asset List<Texture> frames"));

    static const char raw_file[] = "[Raw] asset Texture masks = \"art/tile.png\";\n"
                                   "scene Main { }\n"
                                   "local singleton Art\n"
                                   "{\n"
                                   "    [Raw] Grid2<Color> kept = Grid2(2, 2);\n"
                                   "}\n"
                                   "frag Read(Vertex vertex, out Color color, Art art)\n"
                                   "{\n"
                                   "    color = masks.Sample(vertex.uv) + art.kept.Sample(vertex.uv);\n"
                                   "}\n"
                                   "view V()\n"
                                   "{\n"
                                   "    Draw.Mesh([Vertex { }], [0, 0, 0], Read);\n"
                                   "}\n";
    open_uri(uri_main, raw_file);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    hover = request_at(uri_main, "textDocument/hover", 0, 2, "");
    TIDE_CHECK(has(hover, "[Raw]") && has(hover, "pixels are numbers, not colors to draw"));
    hover = request_at(uri_main, "textDocument/hover", 0, 21, "");
    TIDE_CHECK(has(hover, "asset Texture masks") && has(hover, "It's `[Raw]`"));
    hover = request_at(uri_main, "textDocument/hover", 4, 6, "");
    TIDE_CHECK(has(hover, "[Raw]") && has(hover, "no alpha worked into them"));
    open_uri(uri_main, "[] asset Texture masks = \"art/tile.png\";\nscene Main { }\nlocal singleton Art\n{\n    [] Grid2<Color> kept = Grid2(2, 2);\n}\n");
    completion = request_at(uri_main, "textDocument/completion", 0, 1, "");
    TIDE_CHECK(offers(completion, "Raw") && offers(completion, "Filter"));
    completion = request_at(uri_main, "textDocument/completion", 4, 5, "");
    TIDE_CHECK(offers(completion, "Raw") && offers(completion, "Wrap"));
    open_uri(uri_main, "[Raw] asset Texture masks = \"art/tile.png\";\nscene Main { }\nview V()\n{\n"
                       "    Draw.Mesh([Vertex { }], [0, 0, 0], masks);\n}\n");
    TIDE_CHECK(has(last_sent(), "'masks' is [Raw], its pixels numbers and not colors to draw"));
    TIDE_CHECK(has(last_sent(), "read it in a frag or a vert, by its name"));

    static const char default_assets[] = "asset Texture logo = \"art/tile.png\";\n"
                                         "asset string notes = \"art/other.txt\";\n"
                                         "scene Main { }\n"
                                         "struct Sprite { Texture image = logo; }\n"
                                         "component Box\n"
                                         "{\n"
                                         "    string text = notes;\n";
    snprintf(asset_code, sizeof asset_code, "%s    local Texture test = logo;\n    local Sprite art = Sprite { image = logo };\n}\n",
             default_assets);
    open_uri(uri_main, asset_code);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    hover = request_at(uri_main, "textDocument/hover", 7, 26, "");
    TIDE_CHECK(has(hover, "asset Texture logo") && has(hover, "an image to draw with"));
    snprintf(asset_code, sizeof asset_code, "%s    local Texture test = \n}\n", default_assets);
    open_uri(uri_main, asset_code);
    completion = request_at(uri_main, "textDocument/completion", 7, 25, "");
    TIDE_CHECK(offers(completion, "logo") && offers(completion, "notes") && offers(completion, "Math"));
    TIDE_CHECK(has(completion, "\"label\":\"logo\",\"kind\":6,\"detail\":\"asset Texture\""));
    open_uri(uri_main, "asset string notes = \"art/other.txt\";\nconst int SIZE = notes.length;\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "'notes' is an asset, not a constant: its value is the pack's, read as the game runs"));
    TIDE_CHECK(has(last_sent(), "a field's default can name an asset, like 'string value = notes;'"));

    remove_game_file("lsp_tx/main.tide");
    remove_game_file("lsp_tx/icons.tide");
    remove_game_file("lsp_tx/art/tile.png");
    remove_game_file("lsp_tx/art/other.txt");
    remove_game_file("lsp_tx/.tide/assets.sha256");
    remove_game_file("lsp_tx/.tide/.gitignore");
    remove_folder("lsp_tx/.tide");
    remove_folder("lsp_tx/art");
    remove_folder("lsp_tx");
    clear_sent();
    lsp_free(&server);

    start();
    const char *before = complete("[$]\nsingleton Rules\n{\n    int rounds;\n}\nscene Main { }\n");
    TIDE_CHECK(offers(before, "Filter") && offers(before, "Wrap") && offers(before, "Stage"));
    const char *field = complete("scene Main { }\nlocal singleton Art\n{\n    [$] Grid2<Color> paint = Grid2(2, 2);\n}\n");
    TIDE_CHECK(offers(field, "Filter") && offers(field, "Wrap") && offers(field, "Snap"));
}

// Cubemaps: the type, an asset's six files, a cube a view draws into, what a frag reads, and diagnostics.
TIDE_TEST(lsp_cubemaps)
{
    if (!find_game_dir()) return;
    make_folder("lsp_cm");
    make_folder("lsp_cm/sky");
    static const unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x12, 0x49,
        0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0c, 0x81, 0x34, 0x18, 0x00, 0x00, 0x49, 0xc8,
        0x09, 0xf7, 0x03, 0xd9, 0x64, 0xf1, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    static const char *const faces[] = {"px", "nx", "py", "ny", "pz", "nz"};
    char name[64];
    for (int i = 0; i < 6; i++) {
        snprintf(name, sizeof name, "lsp_cm/sky/day_%s.png", faces[i]);
        write_bytes(name, (const char *)png, sizeof png);
    }
    write_file("lsp_cm/sky/notes.txt", "text");
    static const char main_file[] = "asset Cubemap sky = \"sky/day_??.png\";\n"
                                    "scene Main { }\n"
                                    "local singleton Room\n"
                                    "{\n"
                                    "    Cubemap reflection;\n"
                                    "    List<Vertex3> ball;\n"
                                    "}\n"
                                    "frag Shine(Vertex3 vertex, out Color color, Room room)\n"
                                    "{\n"
                                    "    color = room.reflection.Sample(vertex.position) + sky.Sample(vertex.position, Filter.Trilinear);\n"
                                    "}\n"
                                    "view V(mut Room room)\n"
                                    "{\n"
                                    "    room.reflection = Cubemap(64, Format.Rgba16f);\n"
                                    "    room.reflection.faces[2].Clear(Color.black);\n"
                                    "    Draw.Mesh(room.ball, [0, 1, 2], float4x4.identity, Shine);\n"
                                    "    room.reflection.Resize(32);\n"
                                    "}\n";
    write_file("lsp_cm/main.tide", main_file);
    char root[700], uri_main[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_cm", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_cm/main.tide", dir);

    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    open_uri(uri_main, main_file);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *hover = request_at(uri_main, "textDocument/hover", 0, 15, "");
    TIDE_CHECK(has(hover, "asset Cubemap sky") && has(hover, "six images") && has(hover, "`px`, `nx`, `py`, `ny`, `pz`, `nz`"));
    hover = request_at(uri_main, "textDocument/hover", 4, 6, "");
    TIDE_CHECK(has(hover, "a cube of six images") && has(hover, "Cubemap(size)"));
    hover = request_at(uri_main, "textDocument/hover", 9, 30, "");
    TIDE_CHECK(has(hover, "Sample(float3 direction, Filter filter = Filter.Bilinear) -> Color") && has(hover, "no edge and no wrap"));
    hover = request_at(uri_main, "textDocument/hover", 16, 22, "");
    TIDE_CHECK(has(hover, "Resize(float size)"));
    const char *help = request_at(uri_main, "textDocument/signatureHelp", 13, 40, "");
    TIDE_CHECK(has(help, "Cubemap(float size, Format format)") && has(help, "cube.faces[0]"));

    static const char paths[] = "asset Cubemap sky = \"sky/\";\nscene Main { }\n";
    open_uri(uri_main, paths);
    const char *completion = request_at(uri_main, "textDocument/completion", 0, 25, "");
    TIDE_CHECK(offers(completion, "day_px.png") && !offers(completion, "notes.txt") && offers(completion, "*.png"));

    open_uri(uri_main, "asset Cubemap sky = \"sky/day_px.png\";\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "a Cubemap is six files, its faces") && has(last_sent(), "write a glob that matches the six"));
    open_uri(uri_main, "asset Cubemap sky = \"sky/day_p?.png\";\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "matches no file for the cube's -x face"));
    open_uri(uri_main, "asset List<Cubemap> skies = \"sky/*.png\";\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "a list or a map of cubes isn't there yet"));
    open_uri(uri_main, "[Filter(Filter.Point)]\nasset Cubemap sky = \"sky/day_??.png\";\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "a Cubemap's filter is said where it's sampled"));

    for (int i = 0; i < 6; i++) {
        snprintf(name, sizeof name, "lsp_cm/sky/day_%s.png", faces[i]);
        remove_game_file(name);
    }
    remove_game_file("lsp_cm/sky/notes.txt");
    remove_game_file("lsp_cm/main.tide");
    remove_game_file("lsp_cm/.tide/assets.sha256");
    remove_game_file("lsp_cm/.tide/.gitignore");
    remove_folder("lsp_cm/.tide");
    remove_folder("lsp_cm/sky");
    remove_folder("lsp_cm");
    clear_sent();
    lsp_free(&server);

    start();
    static const char types[] = "scene Main { }\nlocal singleton Room { Cubemap reflection; RenderTarget side; }\n";
    char program[2048];
    snprintf(program, sizeof program, "%sview V(mut Room room)\n{\n    room.reflection.$\n}\n", types);
    const char *members = complete(program);
    TIDE_CHECK(offers(members, "faces") && offers(members, "Resize") && offers(members, "Dispose") && !offers(members, "Sample"));
    TIDE_CHECK(has(members, "faces[$1]") && has(members, "+x, -x, +y, -y, +z, -z"));
    snprintf(program, sizeof program, "%sfrag F(Vertex3 vertex, out Color color, Room room)\n{\n    color = room.reflection.$\n}\n", types);
    members = complete(program);
    TIDE_CHECK(offers(members, "Sample") && !offers(members, "faces") && !offers(members, "Dispose"));
    snprintf(program, sizeof program, "%sview V(mut Room room)\n{\n    room.side = RenderTarget.$\n}\n", types);
    TIDE_CHECK(offers(complete(program), "Window") && !offers(complete(program), "Cube"));
    snprintf(program, sizeof program, "%sview V(mut Room room)\n{\n    var up = room.reflection.faces[2];\n    room.side = u$p;\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(request("textDocument/hover"), "RenderTarget"));
    snprintf(program, sizeof program, "%sview V(mut Room room)\n{\n    Cubem$\n}\n", types);
    TIDE_CHECK(offers(complete(program), "Cubemap"));

    snprintf(program, sizeof program, "%sview V(mut Room room)\n{\n    var all = room.reflection.faces;\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "a cube's faces are drawn into one at a time"));
    snprintf(program, sizeof program, "%sview V(mut Room room)\n{\n    room.side = room.reflection.faces[6];\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "a cube has six faces"));
    snprintf(program, sizeof program, "%sfrag F(Vertex3 vertex, out Color color, Room room)\n{\n"
                                      "    color = room.reflection.Sample(vertex.uv);\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "a Cubemap's Sample takes which way to read from its middle") && has(last_sent(), "a cube is read by a direction"));
    snprintf(program, sizeof program, "%sfrag F(Vertex3 vertex, out Color color, Cubemap sky)\n{\n    color = Color.white;\n}\n", types);
    open_document(program);
    TIDE_CHECK(has(last_sent(), "not a parameter of its own"));
    snprintf(program, sizeof program, "scene Main { }\ncomponent Prop { Cubemap around; }\nevent(Spawned) Setup(with Main) { Spawn(Prop); }\n");
    open_document(program);
    TIDE_CHECK(has(last_sent(), "use local placement and access for this component"));
}

// A game with no asset registers nothing and makes no .tide/; an editor that takes no registration is asked about every rename.
TIDE_TEST(lsp_game_without_assets_changes_nothing)
{
    if (!find_game_dir()) return;
    make_folder("lsp_na");
    static const char main_file[] = "scene Main { }\n";
    write_file("lsp_na/main.tide", main_file);
    char root[700], uri_main[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_na", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_na/main.tide", dir);

    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 1 && has(sent[0], "\"diagnostics\":[]"));
    TIDE_CHECK(!game_file_exists("lsp_na/.tide/assets.sha256"));
    TIDE_CHECK(has(will_rename("lsp_na/x.png", "lsp_na/y.png"), "\"result\":{\"changes\":{}}"));

    start_in_with(root, "{\"workspace\":{\"fileOperations\":{\"willRename\":true,\"didRename\":true}}}");
    TIDE_CHECK(has(last_sent(), "\"fileOperations\":{\"willRename\":{\"filters\":[{\"scheme\":\"file\",\"pattern\":{\"glob\":\"**/*\"}}]}"));
    open_uri(uri_main, main_file);
    TIDE_CHECK(sent_count == 1);
    start_in(root);
    TIDE_CHECK(has(last_sent(), "\"didRename\":{\"filters\":[{\"scheme\":\"file\",\"pattern\":{\"glob\":\"**/*\"}}]}"));

    remove_game_file("lsp_na/main.tide");
    remove_folder("lsp_na");
    clear_sent();
    lsp_free(&server);
}

// The path arithmetic the rewriting rests on (assets.h).
TIDE_TEST(lsp_asset_paths)
{
    char *joined = asset_path_join("D:/g/a/", "art/hero.png", 12);
    TIDE_CHECK(strcmp(joined, "D:/g/a/art/hero.png") == 0);
    free(joined);
    joined = asset_path_join("D:/g/a/", "../b\\x.png", 10);
    TIDE_CHECK(strcmp(joined, "D:/g/b/x.png") == 0);
    free(joined);
    joined = asset_path_join("D:/g/", "../../../x.png", 14);
    TIDE_CHECK(strcmp(joined, "D:/x.png") == 0);
    free(joined);
    joined = asset_path_join("/g/a/", "./art/", 6);
    TIDE_CHECK(strcmp(joined, "/g/a/art/") == 0);
    free(joined);
    joined = asset_path_join("/g/a", "", 0);
    TIDE_CHECK(strcmp(joined, "/g/a/") == 0);
    free(joined);

    char *relative = asset_path_relative("D:/g/a/", "D:/g/a/img/hero.png");
    TIDE_CHECK(strcmp(relative, "img/hero.png") == 0);
    free(relative);
    relative = asset_path_relative("D:/g/b/", "D:/g/a/img/hero.png");
    TIDE_CHECK(strcmp(relative, "../a/img/hero.png") == 0);
    free(relative);
    relative = asset_path_relative("D:/g/a/b/", "D:/x.png");
    TIDE_CHECK(strcmp(relative, "../../../x.png") == 0);
    free(relative);
    relative = asset_path_relative("D:/g/", "E:/x.png");
    TIDE_CHECK(strcmp(relative, "E:/x.png") == 0);
    free(relative);

    char *folder = asset_path_folder("D:/g/a/x.tide");
    TIDE_CHECK(strcmp(folder, "D:/g/a/") == 0);
    free(folder);
    TIDE_CHECK(asset_path_under("D:/g/a/art/hero.png", "D:/g/a/art"));
    TIDE_CHECK(asset_path_under("D:/g/a/art/hero.png", "D:/g/a/art/"));
    TIDE_CHECK(!asset_path_under("D:/g/a/artful/hero.png", "D:/g/a/art"));
    TIDE_CHECK(!asset_path_under("D:/g/a/art", "D:/g/a/art"));
    TIDE_CHECK(strcmp(asset_path_name("D:/g/a/art/hero.png"), "hero.png") == 0);
    TIDE_CHECK(asset_path_is_glob("art/*.png", 9) && asset_path_is_glob("a?.png", 6) && !asset_path_is_glob("art/hero.png", 12));
    TIDE_CHECK(asset_path_is_url("https://example.com/x", 21) && !asset_path_is_url("http/x", 6));
}

// Mesh assets: the checker's messages, hovers, mesh files in a Mesh's path, its two lists, and the mesh draw form.
TIDE_TEST(lsp_asset_meshes)
{
    if (!find_game_dir()) return;
    make_folder("lsp_mesh");
    make_folder("lsp_mesh/art");
    static const char main_file[] = "struct Corner\n"
                                    "{\n"
                                    "    float3 position;\n"
                                    "    float3 normal;\n"
                                    "}\n"
                                    "asset Mesh<Corner> tree = \"art/tree.obj\";\n"
                                    "asset List<Mesh<Corner>> props = \"art/tree.obj\";\n"
                                    "scene Main { }\n"
                                    "frag Paint(Corner corner, out Color color)\n"
                                    "{\n"
                                    "    color = Color.white;\n"
                                    "}\n"
                                    "view Show()\n"
                                    "{\n"
                                    "    Draw.Mesh(tree, float4x4.identity, Paint);\n"
                                    "    var count = tree.vertices.count;\n"
                                    "    Debug.Log(\"\" + count);\n"
                                    "}\n";
    write_file("lsp_mesh/main.tide", main_file);
    write_file("lsp_mesh/art/tree.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\n");
    write_file("lsp_mesh/art/rock.glb", "not one, but named as one");
    write_file("lsp_mesh/art/hill.gltf", "{}");
    write_file("lsp_mesh/art/hero.FBX", fbx_catalog_fixture);
    write_file("lsp_mesh/art/other.txt", "text");
    char root[700], uri_main[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_mesh", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_mesh/main.tide", dir);

    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    open_uri(uri_main, main_file);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    const char *hover = request_at(uri_main, "textDocument/hover", 5, 20, "");
    TIDE_CHECK(has(hover, "asset Mesh<Corner> tree") && has(hover, "as one mesh of its vertex struct"));
    TIDE_CHECK(has(hover, "The file has 3 vertices and 1 triangles, with normals, no uvs, no colors (white) and no tangents."));
    hover = request_at(uri_main, "textDocument/hover", 14, 15, "");
    TIDE_CHECK(has(hover, "asset Mesh<Corner> tree"));
    hover = request_at(uri_main, "textDocument/hover", 5, 13, "");
    TIDE_CHECK(has(hover, "struct Corner"));
    hover = request_at(uri_main, "textDocument/hover", 5, 7, "");
    TIDE_CHECK(has(hover, "An asset's mesh"));

    const char *completion = request_at(uri_main, "textDocument/completion", 5, 31, "");
    TIDE_CHECK(offers(completion, "tree.obj") && offers(completion, "rock.glb") && offers(completion, "hill.gltf") && offers(completion, "hero.FBX"));
    TIDE_CHECK(!offers(completion, "other.txt") && !offers(completion, "*.glb"));
    completion = request_at(uri_main, "textDocument/completion", 6, 38, "");
    TIDE_CHECK(offers(completion, "tree.obj") && offers(completion, "*.glb") && offers(completion, "**/*.glb"));
    TIDE_CHECK(offers(completion, "*.fbx") && offers(completion, "**/*.fbx") && offers(completion, "*.obj") && offers(completion, "**/*.obj"));

    static char dotted[sizeof main_file + 32];
    snprintf(dotted, sizeof dotted, "%.*s    tree.\n}\n", (int)(strstr(main_file, "    var count") - main_file), main_file);
    open_uri(uri_main, dotted);
    completion = request_at(uri_main, "textDocument/completion", 15, 9, "");
    TIDE_CHECK(offers(completion, "vertices") && offers(completion, "indices") && !offers(completion, "count"));

    static char drawing[sizeof main_file + 64];
    snprintf(drawing, sizeof drawing, "%.*s    Draw.Mesh(tree, \n}\n", (int)(strstr(main_file, "    Draw.Mesh") - main_file), main_file);
    open_uri(uri_main, drawing);
    const char *help = request_at(uri_main, "textDocument/signatureHelp", 14, 20, "");
    TIDE_CHECK(has(help, "Draw.Mesh(mesh, transform[, texture[, filter[, wrap]]][, vert][, frag][, values])"));
    TIDE_CHECK(has(help, "\"activeParameter\":1") && has(help, "A `Mesh<V>`: an asset's vertices and its indices as one"));
    snprintf(drawing, sizeof drawing, "%.*s    Draw.Instanced(tree, \n}\n", (int)(strstr(main_file, "    Draw.Mesh") - main_file), main_file);
    open_uri(uri_main, drawing);
    help = request_at(uri_main, "textDocument/signatureHelp", 14, 25, "");
    TIDE_CHECK(has(help, "Draw.Instanced(mesh, instances[, texture") && has(help, "\"activeParameter\":1"));

    static const char wrong[] = "struct Corner\n{\n    float3 position;\n}\n"
                                "asset Mesh<Corner> rock = \"art/rock.glb\";\n"
                                "asset Mesh<Corner> hill = \"art/hill.gltf\";\n"
                                "asset Mesh<Corner> text = \"art/other.txt\";\n"
                                "local singleton Kept\n{\n    Mesh<Corner> one;\n}\n"
                                "scene Main { }\n";
    open_uri(uri_main, wrong);
    TIDE_CHECK(has(last_sent(), "'art/rock.glb' has an invalid GLB header: its magic isn't glTF"));
    TIDE_CHECK(has(last_sent(), "'art/hill.gltf' is glTF of no version, and Tide reads glTF 2.0"));
    TIDE_CHECK(has(last_sent(), "'art/other.txt' isn't a mesh file Tide reads"));
    TIDE_CHECK(has(last_sent(), "a field can't be a Mesh"));

    remove_game_file("lsp_mesh/main.tide");
    remove_game_file("lsp_mesh/art/tree.obj");
    remove_game_file("lsp_mesh/art/rock.glb");
    remove_game_file("lsp_mesh/art/hill.gltf");
    remove_game_file("lsp_mesh/art/hero.FBX");
    remove_game_file("lsp_mesh/art/other.txt");
    remove_game_file("lsp_mesh/.tide/assets.sha256");
    remove_game_file("lsp_mesh/.tide/.gitignore");
    remove_folder("lsp_mesh/.tide");
    remove_folder("lsp_mesh/art");
    remove_folder("lsp_mesh");
    clear_sent();
    lsp_free(&server);
}

// A name catalog is independent of geometry. The editor reads no side buffer
// and keeps the JSON result across changed source documents and requests.
TIDE_TEST(lsp_skinned_catalogs_use_cached_json_and_preserve_inference)
{
    if (!find_game_dir()) return;
    model_metadata_forget();
    make_folder("lsp_skin");
    make_folder("lsp_skin/art");
    static const char prefix[] = "struct Corner { float3 position; }\n"
                                 "asset SkinnedMesh<Corner> hero = \"art/hero.gltf\";\n"
                                 "scene Main { }\n";
    static const char metadata[] = "{\"asset\":{\"version\":\"2.0\"},"
                                   "\"animations\":[{\"name\":\"Idle\"},{\"name\":\"Run\"},{\"name\":\"bad name\"},"
                                   "{\"name\":\"Jump\"},{\"name\":\"jump\"},{\"name\":\"for\"},{\"name\":\"Find\"}],"
                                   "\"nodes\":[{\"name\":\"Root\",\"children\":[1]},{\"name\":\"Hand\"}],"
                                   "\"skins\":[{\"joints\":[1]}],\"buffers\":[{\"uri\":\"must-not-read.bin\",\"byteLength\":99999999}]}";
    write_file("lsp_skin/main.tide", prefix);
    write_file("lsp_skin/art/hero.gltf", metadata);
    write_file("lsp_skin/art/other.obj", "not glTF");
    char root[700], uri_main[700], uri_model[700], text[2048];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_skin", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_skin/main.tide", dir);
    snprintf(uri_model, sizeof uri_model, "file:///%s/lsp_skin/art/hero.gltf", dir);
    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    snprintf(text, sizeof text, "%sview V()\n{\n    var model = hero;\n    model.animations.\n}\n", prefix);
    open_uri(uri_main, text);
    const char *completion = request_at(uri_main, "textDocument/completion", 6, 21, "");
    TIDE_CHECK(offers(completion, "idle") && offers(completion, "run"));
    TIDE_CHECK(offers(completion, "count") && offers(completion, "TryGetByName") && offers(completion, "TryGetAt"));
    TIDE_CHECK(!offers(completion, "Jump") && !offers(completion, "jump") && !offers(completion, "bad name")
               && !offers(completion, "for") && !offers(completion, "data"));
    TIDE_CHECK(has(completion, "original source name `Idle`"));
    TIDE_CHECK(offers(completion, "badName") && offers(completion, "jump_4") && offers(completion, "jump_5")
               && offers(completion, "_for"));
    model_metadata_stats warm = model_metadata_statistics();
    TIDE_CHECK(warm.parses == 1 && warm.file_reads == 1 && warm.bytes_read == sizeof metadata - 1);
    for (int i = 0; i < 20; i++) {
        open_uri(uri_main, text);
        completion = request_at(uri_main, "textDocument/completion", 6, 21, "");
        TIDE_CHECK(offers(completion, "idle"));
    }
    TIDE_CHECK(model_metadata_statistics().parses == warm.parses && model_metadata_statistics().file_reads == warm.file_reads);

    snprintf(text, sizeof text, "%svoid Helper(SkinnedMesh<Corner> model)\n{\n    model.animations.\n}\n", prefix);
    open_uri(uri_main, text);
    completion = request_at(uri_main, "textDocument/completion", 5, 21, "");
    TIDE_CHECK(offers(completion, "TryGetByName") && offers(completion, "TryGetAt") && !offers(completion, "idle"));

    snprintf(text, sizeof text, "%sstruct Locomotion { Animation idle; [Name(\"Run\")] Animation sprint; Bone hand; }\n"
                               "void Helper(SkinnedMesh<Corner, Locomotion> model)\n{\n    model.animations.\n}\n", prefix);
    open_uri(uri_main, text);
    completion = request_at(uri_main, "textDocument/completion", 6, 21, "");
    TIDE_CHECK(offers(completion, "idle") && offers(completion, "sprint") && offers(completion, "TryGetByName") && !offers(completion, "hand"));
    TIDE_CHECK(has(completion, "original source name `Run`"));

    snprintf(text, sizeof text, "%sview V()\n{\n    var pose = hero.animations.idle.Sample(0);\n"
                               "    Debug.Log(hero.animations.idle.name);\n    var hand = hero.skeleton.bones.hand;\n"
                               "    Debug.Log(\"\" + pose.Model(hand).c3.x);\n}\n", prefix);
    open_uri(uri_main, text);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request_at(uri_main, "textDocument/hover", 5, 34, "");
    TIDE_CHECK(has(hover, "Animation idle") && has(hover, "original source name `Idle`"));
    hover = request_at(uri_main, "textDocument/hover", 5, 40, "");
    TIDE_CHECK(has(hover, "Sample(float seconds, Playback playback = Playback.Clamp)") && has(hover, "deterministic math"));
    const char *help = request_at(uri_main, "textDocument/signatureHelp", 5, 44, "");
    TIDE_CHECK(has(help, "Playback playback = Playback.Clamp") && has(help, "activeParameter"));

    snprintf(text, sizeof text, "%sview V()\n{\n    hero.skeleton.bones.\n}\n", prefix);
    open_uri(uri_main, text);
    completion = request_at(uri_main, "textDocument/completion", 5, 24, "");
    TIDE_CHECK(offers(completion, "root") && offers(completion, "hand") && offers(completion, "TryGetByName") && !offers(completion, "data"));

    // A watched notification invalidates even the unchanged file's size/time.
    char changed[1024];
    snprintf(changed, sizeof changed, "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\","
                                    "\"params\":{\"changes\":[{\"uri\":\"%s\",\"type\":2}]}}", uri_model);
    warm = model_metadata_statistics();
    handle(changed);
    TIDE_CHECK(model_metadata_statistics().parses == warm.parses + 1 && model_metadata_statistics().file_reads == warm.file_reads + 1);
    completion = request_at(uri_main, "textDocument/completion", 5, 24, "");
    TIDE_CHECK(offers(completion, "hand"));
    TIDE_CHECK(model_metadata_statistics().parses == warm.parses + 1);

    completion = request_at(uri_main, "textDocument/completion", 1, 42, "");
    TIDE_CHECK(offers(completion, "hero.gltf") && !offers(completion, "other.obj"));
    remove_game_file("lsp_skin/main.tide");
    remove_game_file("lsp_skin/art/hero.gltf");
    remove_game_file("lsp_skin/art/other.obj");
    remove_game_file("lsp_skin/.tide/assets.sha256");
    remove_game_file("lsp_skin/.tide/.gitignore");
    remove_folder("lsp_skin/.tide");
    remove_folder("lsp_skin/art");
    remove_folder("lsp_skin");
    clear_sent();
    lsp_free(&server);
    model_metadata_forget();
}

TIDE_TEST(lsp_skinned_fbx_catalogs_complete_names_paths_and_refresh)
{
    if (!find_game_dir()) return;
    model_metadata_forget();
    make_folder("lsp_fbx");
    make_folder("lsp_fbx/art");
    static const char prefix[] = "struct Corner { float3 position; }\n"
                                 "asset SkinnedMesh<Corner> hero = \"art/hero.FBX\";\n"
                                 "asset List<SkinnedMesh<Corner>> models = \"art/hero.FBX\";\n"
                                 "scene Main { }\n";
    write_file("lsp_fbx/main.tide", prefix);
    write_file("lsp_fbx/art/hero.FBX", fbx_catalog_fixture);
    pin_file_time("lsp_fbx/art/hero.FBX");
    write_file("lsp_fbx/art/static.obj", "not a skinned format");
    char root[700], uri_main[700], uri_model[700], text[2048];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_fbx", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_fbx/main.tide", dir);
    snprintf(uri_model, sizeof uri_model, "file:///%s/lsp_fbx/art/hero.FBX", dir);
    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    handle("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
    TIDE_CHECK(sent_with("**/*.[fF][bB][xX]") != NULL);
    snprintf(text, sizeof text, "%sview V()\n{\n    hero.animations.\n}\n", prefix);
    open_uri(uri_main, text);
    const char *completion = request_at(uri_main, "textDocument/completion", 6, 20, "");
    TIDE_CHECK(offers(completion, "idle") && offers(completion, "run") && has(completion, "original source name `Idle`"));
    const model_metadata_stats warm = model_metadata_statistics();
    open_uri(uri_main, text);
    TIDE_CHECK(model_metadata_statistics().parses == warm.parses && model_metadata_statistics().file_reads == warm.file_reads);
    snprintf(text, sizeof text, "%sview V()\n{\n    hero.skeleton.bones.\n}\n", prefix);
    open_uri(uri_main, text);
    completion = request_at(uri_main, "textDocument/completion", 6, 24, "");
    TIDE_CHECK(offers(completion, "root") && offers(completion, "hand") && offers(completion, "TryGetByName"));
    completion = request_at(uri_main, "textDocument/completion", 1, 42, "");
    TIDE_CHECK(offers(completion, "hero.FBX") && !offers(completion, "static.obj"));
    completion = request_at(uri_main, "textDocument/completion", 2, 50, "");
    TIDE_CHECK(offers(completion, "*.fbx") && offers(completion, "**/*.fbx") && !offers(completion, "*.obj"));
    snprintf(text, sizeof text, "%sview V()\n{\n    Debug.Log(hero.animations.idle.name);\n}\n", prefix);
    open_uri(uri_main, text);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request_at(uri_main, "textDocument/hover", 6, 32, "");
    TIDE_CHECK(has(hover, "Animation idle") && has(hover, "original source name `Idle`"));
    char notification[1024];
    snprintf(notification, sizeof notification, "{\"jsonrpc\":\"2.0\",\"method\":\"workspace/didChangeWatchedFiles\","
             "\"params\":{\"changes\":[{\"uri\":\"%s\",\"type\":2}]}}", uri_model);
    handle(notification);
    TIDE_CHECK(model_metadata_statistics().parses == warm.parses + 1u && model_metadata_statistics().file_reads == warm.file_reads + 1u);
    static const char animation_prefix[] = "asset AnimationCatalog movement = \"art/hero.FBX\";\n"
                                           "asset List<AnimationCatalog> clips = \"art/hero.FBX\";\n"
                                           "scene Main { }\n";
    snprintf(text, sizeof text, "%sview V()\n{\n    movement.\n}\n", animation_prefix);
    open_uri(uri_main, text);
    completion = request_at(uri_main, "textDocument/completion", 5, 13, "");
    TIDE_CHECK(offers(completion, "idle") && offers(completion, "run") && offers(completion, "TryGetByName")
               && offers(completion, "TryGetAt") && offers(completion, "count") && !offers(completion, "skeleton"));
    TIDE_CHECK(has(completion, "original source name `Idle`"));
    const model_metadata_stats shared = model_metadata_statistics();
    TIDE_CHECK(shared.parses == warm.parses + 1u && shared.file_reads == warm.file_reads + 1u);
    completion = request_at(uri_main, "textDocument/completion", 0, (int)strlen("asset AnimationCatalog movement = \"art/"), "");
    TIDE_CHECK(offers(completion, "hero.FBX") && !offers(completion, "static.obj"));
    completion = request_at(uri_main, "textDocument/completion", 1, (int)strlen("asset List<AnimationCatalog> clips = \"art/"), "");
    TIDE_CHECK(offers(completion, "*.fbx") && offers(completion, "**/*.fbx") && !offers(completion, "*.obj"));
    hover = request_at(uri_main, "textDocument/hover", 0, (int)strlen("asset AnimationCatalog ") + 2, "");
    TIDE_CHECK(has(hover, "asset AnimationCatalog movement") && has(hover, "glTF or FBX") && has(hover, "without typed render geometry"));
    char replacement[sizeof fbx_catalog_fixture];
    memcpy(replacement, fbx_catalog_fixture, sizeof fbx_catalog_fixture);
    memcpy(strstr(replacement, "Idle"), "Walk", 4);
    write_file("lsp_fbx/art/hero.FBX", replacement);
    pin_file_time("lsp_fbx/art/hero.FBX");
    open_uri(uri_main, text);
    completion = request_at(uri_main, "textDocument/completion", 5, 13, "");
    TIDE_CHECK(offers(completion, "idle") && !offers(completion, "walk"));
    notify_file(uri_model, 2);
    completion = request_at(uri_main, "textDocument/completion", 5, 13, "");
    TIDE_CHECK(offers(completion, "walk") && !offers(completion, "idle"));
    TIDE_CHECK(model_metadata_statistics().parses == shared.parses + 1u && model_metadata_statistics().file_reads == shared.file_reads + 1u);
    remove_game_file("lsp_fbx/main.tide");
    remove_game_file("lsp_fbx/art/hero.FBX");
    remove_game_file("lsp_fbx/art/static.obj");
    remove_game_file("lsp_fbx/.tide/assets.sha256");
    remove_game_file("lsp_fbx/.tide/.gitignore");
    remove_folder("lsp_fbx/.tide");
    remove_folder("lsp_fbx/art");
    remove_folder("lsp_fbx");
    clear_sent();
    lsp_free(&server);
    model_metadata_forget();
}

TIDE_TEST(lsp_skinned_unavailable_catalogs_are_not_missing_members)
{
    if (!find_game_dir()) return;
    model_metadata_forget();
    make_folder("lsp_skin_status");
    static const char main_file[] = "asset SkinnedMesh<Vertex3> hero = \"hero.gltf\";\n"
                                    "scene Main { }\nview V()\n{\n    Debug.Log(hero.animations.walk.name);\n}\n";
    write_file("lsp_skin_status/main.tide", main_file);
    write_file("lsp_skin_status/hero.gltf", "{\"asset\":{\"version\":\"2.0\"},\"animations\":[{\"name\":\"Idle\"}]}");
    char root[700], uri_main[700], path[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_skin_status", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_skin_status/main.tide", dir);
    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    open_uri(uri_main, main_file);
    TIDE_CHECK(has(last_sent(), "AnimationCatalog has no checked member 'walk'") && !has(last_sent(), "metadata is unavailable"));
    write_file("lsp_skin_status/hero.gltf", "{broken JSON}");
    game_path(path, sizeof path, "lsp_skin_status/hero.gltf");
    model_metadata_invalidate(path);
    open_uri(uri_main, main_file);
    TIDE_CHECK(has(last_sent(), "animation catalog metadata is invalid") && !has(last_sent(), "has no checked member 'walk'"));
    TIDE_CHECK(diagnosed(0, (int)(strchr(main_file, '"') - main_file), 11, 1,
                         "model catalog metadata is invalid: has incomplete or invalid JSON"));
    remove_game_file("lsp_skin_status/hero.gltf");
    open_uri(uri_main, main_file);
    TIDE_CHECK(has(last_sent(), "animation catalog metadata is unavailable") && !has(last_sent(), "has no checked member 'walk'"));
    TIDE_CHECK(diagnosed(0, (int)(strchr(main_file, '"') - main_file), 11, 3, "model catalog metadata is unavailable:"));

    static const char dynamic[] = "asset SkinnedMesh<Vertex3> hero = \"https://must-never-be-contacted.invalid/model.glb\";\n"
                                 "scene Main { }\nview V() { if (hero.animations.TryGetByName(\"Idle\", out var clip)) Debug.Log(clip.name); }\n";
    open_uri(uri_main, dynamic);
    TIDE_CHECK(diagnosed(0, (int)(strchr(dynamic, '"') - dynamic), 51, 3, "model catalog metadata is unavailable:")
               && !has(last_sent(), "\"severity\":1"));

    model_metadata_forget();
    static const char uncached[] = "asset SkinnedMesh<Vertex3> hero = \"https://must-never-be-contacted.invalid/model.glb\";\n"
                                   "scene Main { }\nview V()\n{\n    Debug.Log(hero.animations.walk.name);\n}\n";
    open_uri(uri_main, uncached);
    TIDE_CHECK(has(last_sent(), "animation catalog metadata is unavailable") && !has(last_sent(), "has no checked member 'walk'"));
    TIDE_CHECK(diagnosed(0, (int)(strchr(uncached, '"') - uncached), 51, 3, "model catalog metadata is unavailable:"));
    TIDE_CHECK(model_metadata_statistics().file_reads == 0 && model_metadata_statistics().parses == 0);
    static const char contracted[] = "struct Locomotion { Animation idle; }\n"
                                     "asset SkinnedMesh<Vertex3, Locomotion> hero = \"https://must-never-be-contacted.invalid/model.glb\";\n"
                                     "scene Main { }\nview V()\n{\n    hero.animations.\n}\n";
    open_uri(uri_main, contracted);
    const char *completion = request_at(uri_main, "textDocument/completion", 5, 20, "");
    TIDE_CHECK(offers(completion, "idle") && offers(completion, "TryGetByName"));
    TIDE_CHECK(has(completion, "required normalized source identifier `idle`"));
    TIDE_CHECK(model_metadata_statistics().file_reads == 0 && model_metadata_statistics().parses == 0);
    remove_game_file("lsp_skin_status/main.tide");
    remove_game_file("lsp_skin_status/.tide/assets.sha256");
    remove_game_file("lsp_skin_status/.tide/.gitignore");
    remove_folder("lsp_skin_status/.tide");
    remove_folder("lsp_skin_status");
    clear_sent();
    lsp_free(&server);
    model_metadata_forget();
}

TIDE_TEST(lsp_skinned_obj_diagnostics_explain_the_supported_format)
{
    if (!find_game_dir()) return;
    model_metadata_forget();
    make_folder("lsp_skin_format");
    static const char obj[] = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    static const char local[] = "asset SkinnedMesh<Vertex3> hero = \"character.obj\";\nscene Main { }\n";
    static const char primary[] = "a SkinnedMesh asset requires glTF 2.0 (.gltf or .glb) or FBX (.fbx), not OBJ";
    static const char guidance[] = "use Mesh<V> and Draw.Mesh for a static OBJ, or export a rigged model as glTF 2.0 or FBX";
    write_file("lsp_skin_format/main.tide", local);
    write_file("lsp_skin_format/character.obj", obj);
    char root[700], uri_main[700];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_skin_format", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_skin_format/main.tide", dir);
    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    open_uri(uri_main, local);
    TIDE_CHECK(diagnosed(0, (int)(strchr(local, '"') - local), 15, 1, primary));
    TIDE_CHECK(has(last_sent(), guidance) && !has(last_sent(), "incomplete or invalid JSON")
               && !has(last_sent(), "model catalog metadata is invalid"));
    write_file("lsp_skin_format/character.obj", "{\"asset\":{\"version\":\"2.0\"}}");
    open_uri(uri_main, local);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));

    write_file("lsp_skin_format/character.DAE", "<COLLADA/>");
    static const char dae[] = "asset SkinnedMesh<Vertex3> hero = \"character.DAE\";\nscene Main { }\n";
    open_uri(uri_main, dae);
    TIDE_CHECK(diagnosed(0, (int)(strchr(dae, '"') - dae), 15, 1, "'character.DAE' isn't a supported SkinnedMesh format"));
    TIDE_CHECK(has(last_sent(), "export the rigged model as glTF 2.0") && !has(last_sent(), "invalid JSON"));

    model_metadata_forget();
    static const char url[] = "https://must-never-be-contacted.invalid/character.OBJ?revision=1#preview";
    char remote[512];
    snprintf(remote, sizeof remote, "asset SkinnedMesh<Vertex3> hero = \"%s\";\nscene Main { }\n", url);
    open_uri(uri_main, remote);
    TIDE_CHECK(diagnosed(0, (int)(strchr(remote, '"') - remote), (int)strlen(url) + 2, 3,
                         "model catalog metadata is unavailable:"));
    TIDE_CHECK(!has(last_sent(), "\"severity\":1") && model_metadata_statistics().file_reads == 0
               && model_metadata_statistics().parses == 0);
    make_folder("lsp_skin_format/.tide");
    make_folder("lsp_skin_format/.tide/assets");
    make_folder("lsp_skin_format/.tide/assets/urls");
    char digest[65], cache[160];
    sha256_hex(url, strlen(url), digest);
    snprintf(cache, sizeof cache, "lsp_skin_format/.tide/assets/urls/%s", digest);
    write_file(cache, obj);
    open_uri(uri_main, remote);
    TIDE_CHECK(diagnosed(0, (int)(strchr(remote, '"') - remote), (int)strlen(url) + 2, 1, primary));
    TIDE_CHECK(has(last_sent(), guidance) && !has(last_sent(), "incomplete or invalid JSON")
               && !has(last_sent(), "model catalog metadata is invalid"));
    TIDE_CHECK(model_metadata_statistics().file_reads == 1);
    remove_game_file(cache);
    remove_game_file("lsp_skin_format/main.tide");
    remove_game_file("lsp_skin_format/character.obj");
    remove_game_file("lsp_skin_format/character.DAE");
    remove_game_file("lsp_skin_format/.tide/assets.sha256");
    remove_game_file("lsp_skin_format/.tide/.gitignore");
    remove_folder("lsp_skin_format/.tide/assets/urls");
    remove_folder("lsp_skin_format/.tide/assets");
    remove_folder("lsp_skin_format/.tide");
    remove_folder("lsp_skin_format");
    clear_sent();
    lsp_free(&server);
    model_metadata_forget();
}

TIDE_TEST(lsp_float_bitcasts_and_float_hash_help)
{
    start();
    const char *math = complete("scene Main { }\nview V() { Math.$ }\n");
    TIDE_CHECK(offers(math, "AsInt") && offers(math, "AsFloat") && offers(math, "Hash"));
    open_document("scene Main { }\nview V() { var bits = Math.AsI$nt(float3(1, 2, 3)); Debug.Log(\"\" + bits); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *bits = request("textDocument/hover");
    TIDE_CHECK(has(bits, "Math.AsInt(float3 value) -> int3") && has(bits, "without a numeric conversion"));
    open_document("scene Main { }\nview V() { var hash = Math.Ha$sh(float4(1, 2, 3, 4)); Debug.Log(\"\" + hash); }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "float's exact bits"));
}

TIDE_TEST(lsp_skinned_static_decode_and_draw_forms)
{
    start();
    const char *completion = complete("void Helper($\n");
    TIDE_CHECK(offers(completion, "AnimationCatalog"));
    completion = complete("scene Main { }\nfrag Shade(Vertex3 vertex, out Color color)\n{\n    Anim$\n}\n");
    TIDE_CHECK(!offers(completion, "AnimationCatalog"));
    completion = complete("scene Main { }\nview V()\n{\n    var model = SkinnedMesh<Vertex3>.$\n}\n");
    TIDE_CHECK(offers(completion, "Decode") && has(completion, "self-contained GLB"));
    open_document("scene Main { }\nview V()\n{\n    var model = SkinnedMesh<Vertex3>.Dec$ode([byte(0)])!;\n"
                  "    Debug.Log(\"\" + model.indices.count);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request("textDocument/hover");
    TIDE_CHECK(has(hover, "SkinnedMesh<Vertex3>.Decode") && has(hover, "self-contained GLB") && has(hover, "fails IOError"));
    open_document("scene Main { }\nview V()\n{\n    var model = SkinnedMesh<Vertex3>.Decode($[byte(0)])!;\n"
                  "    Debug.Log(\"\" + model.indices.count);\n}\n");
    const char *help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "Decode(List<byte> bytes)") && has(help, "SkinnedMesh<Vertex3> fails IOError"));
    completion = complete("scene Main { }\nview V()\n{\n    SkinnedMesh<Vertex3> model = default;\n    model.$\n}\n");
    TIDE_CHECK(offers(completion, "animations") && offers(completion, "skeleton") && offers(completion, "defaultPose")
               && !offers(completion, "Decode") && !offers(completion, "data"));
    open_document("scene Main { }\nview V()\n{\n    SkinnedMesh<Vertex3> model = default;\n"
                  "    var pose = model.default$Pose;\n    Draw.SkinnedMesh(model, float4x4.identity, pose);\n"
                  "    Draw.SkinnedMesh(model, float4x4.identity);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *rest_hover = request("textDocument/hover");
    TIDE_CHECK(has(rest_hover, "Pose defaultPose") && has(rest_hover, "authored rest transforms")
               && has(rest_hover, "without animation clips"));
    open_document("scene Main { }\nview V()\n{\n    SkinnedMesh<Vertex3> model = default;\n"
                  "    Pose pose = default;\n    Draw.SkinnedMesh(model, float4x4.identity, $pose);\n}\n");
    help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "Draw.SkinnedMesh(mesh, transform[, pose]") && has(help, "\"activeParameter\":2"));
    TIDE_CHECK(has(help, "skinning applies its transforms before the user vertex stage"));
    open_document("scene Main { }\nstruct Item { Pose pose; }\nview V()\n{\n    SkinnedMesh<Vertex3> model = default;\n"
                  "    Draw.Instanced(model, $[Item { pose = model.defaultPose }]);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "Draw.Instanced(mesh, instances[") && has(help, "\"activeParameter\":1"));
    TIDE_CHECK(has(help, "optional Pose pose field") && has(help, "Invalid poses skip their item"));
    open_document("scene Main { }\nfrag Shade(Vertex3 vertex, out Color color) { color = vertex.color; }\n"
                  "view V()\n{\n    SkinnedMesh<Vertex3> model = default;\n"
                  "    Draw.SkinnedMesh(model, float4x4.identity, $Shade);\n}\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    help = request("textDocument/signatureHelp");
    TIDE_CHECK(has(help, "Draw.SkinnedMesh(mesh, transform[, pose]") && has(help, "\"activeParameter\":7")
               && has(help, "Omit it to draw the model's authored rest pose"));
    completion = complete("scene Main { }\nfrag Shade(Vertex3 vertex, out Color color) { color = vertex.color; }\n"
                          "view V()\n{\n    SkinnedMesh<Vertex3> model = default;\n"
                          "    Draw.SkinnedMesh(model, float4x4.identity, $\n}\n");
    TIDE_CHECK(offers(completion, "Shade"));
    completion = complete("scene Main { }\nfrag Shade(Vertex3 vertex, out Color color) { color = vertex.color; }\n"
                          "view V()\n{\n    SkinnedMesh<Vertex3> model = default;\n    Pose pose = default;\n"
                          "    Draw.SkinnedMesh(model, float4x4.identity, pose, $\n}\n");
    TIDE_CHECK(offers(completion, "Shade"));
    completion = complete("scene Main { }\nvert Shift(mut Vertex3 vertex)\n"
                          "{\n    BoneTransform offset = default;\n    vertex.position += offset.$\n}\n");
    TIDE_CHECK(offers(completion, "position") && offers(completion, "rotation") && offers(completion, "scale"));
    open_document("scene Main { }\nstruct EmptyAlias { [Name(\"\")] Bone socket; }\n"
                  "int Count(SkinnedMesh<Vertex3, EmptyAlias> model) { return model.skeleton.bones.count; }\n");
    TIDE_CHECK(has(last_sent(), "a model contract's Name alias cannot be empty") && has(last_sent(), "\"severity\":1"));
}

TIDE_TEST(lsp_animation_file_lists_clean_names_select_clips_and_refresh_all_sources)
{
    if (!find_game_dir()) return;
    model_metadata_forget();
    make_folder("lsp_animation_list");
    make_folder("lsp_animation_list/art");
    static const char walk[] = "{\"asset\":{\"version\":\"2.0\"},\"animations\":[{\"name\":\"Root|Walk\"},{\"name\":\"Root|0.Targeting Pose\"}]}";
    static const char run[] = "{\"asset\":{\"version\":\"2.0\"},\"animations\":[{\"name\":\"Root|Run\"},{\"name\":\"Root|0.Targeting Pose\"}]}";
    static const char prefix[] = "asset AnimationCatalog movement = [\"art/walk.gltf\", \"art/run.gltf\"];\n"
                                 "[Name(\"Root|Run\")] asset Animation run = \"art/run.gltf\";\n"
                                 "scene Main { }\n";
    write_file("lsp_animation_list/main.tide", prefix);
    write_file("lsp_animation_list/art/walk.gltf", walk);
    write_file("lsp_animation_list/art/run.gltf", run);
    pin_file_time("lsp_animation_list/art/run.gltf");
    char root[700], uri[700], uri_run[700], text[2048];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_animation_list", dir);
    snprintf(uri, sizeof uri, "file:///%s/lsp_animation_list/main.tide", dir);
    snprintf(uri_run, sizeof uri_run, "file:///%s/lsp_animation_list/art/run.gltf", dir);
    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    snprintf(text, sizeof text, "%sview V()\n{\n    movement.\n}\n", prefix);
    open_uri(uri, text);
    const char *completion = request_at(uri, "textDocument/completion", 5, 13, "");
    TIDE_CHECK(offers(completion, "rootWalk") && offers(completion, "rootRun")
               && offers(completion, "walkRoot0TargetingPose") && offers(completion, "runRoot0TargetingPose"));
    TIDE_CHECK(has(completion, "original source name `Root|Run`") && has(completion, "Imported from `art/run.gltf`"));
    TIDE_CHECK(!offers(completion, "root0TargetingPose") && !offers(completion, "data"));
    const model_metadata_stats warm = model_metadata_statistics();
    TIDE_CHECK(warm.parses == 2 && warm.file_reads == 2 && warm.bytes_read == sizeof walk + sizeof run - 2u);
    completion = request_at(uri, "textDocument/completion", 0, (int)strlen("asset AnimationCatalog movement = [\"art/walk.gltf\", \"art/"), "");
    TIDE_CHECK(offers(completion, "run.gltf") && offers(completion, "*.gltf"));
    completion = request_at(uri, "textDocument/completion", 1, (int)strlen("[Name(\"Root|Run\")] asset Animation run = \"art/"), "");
    TIDE_CHECK(offers(completion, "run.gltf") && !offers(completion, "*.gltf"));
    snprintf(text, sizeof text, "%sview V() { Debug.Log(movement.rootRun.name + run.name); }\n", prefix);
    open_uri(uri, text);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request_at(uri, "textDocument/hover", 3, (int)strlen("view V() { Debug.Log(movement.root"), "");
    TIDE_CHECK(has(hover, "Animation rootRun") && has(hover, "Root|Run") && has(hover, "art/run.gltf"));
    hover = request_at(uri, "textDocument/hover", 1, (int)strlen("[Name(\"Root|Run\")] asset Animation r"), "");
    TIDE_CHECK(has(hover, "asset Animation run") && has(hover, "exact clip selected"));
    char rename[2048];
    snprintf(rename, sizeof rename, "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"workspace/willRenameFiles\","
             "\"params\":{\"files\":[{\"oldUri\":\"%s\",\"newUri\":\"file:///%s/lsp_animation_list/art/jog.gltf\"}]}}", uri_run, dir);
    clear_sent();
    handle(rename);
    const char *edited = apply_reply(text, uri);
    TIDE_CHECK(count(last_sent(), "\"newText\"") == 2 && has(edited, "art/jog.gltf") && !has(edited, "art/run.gltf"));
    char replacement[sizeof run];
    memcpy(replacement, run, sizeof run);
    memcpy(strstr(replacement, "Root|Run"), "Root|Jog", 8);
    write_file("lsp_animation_list/art/run.gltf", replacement);
    pin_file_time("lsp_animation_list/art/run.gltf");
    notify_file(uri_run, 2);
    snprintf(text, sizeof text, "asset AnimationCatalog movement = \"art/*.gltf\";\nscene Main { }\nview V()\n{\n    movement.\n}\n");
    open_uri(uri, text);
    completion = request_at(uri, "textDocument/completion", 4, 13, "");
    TIDE_CHECK(offers(completion, "rootJog") && !offers(completion, "rootRun") && offers(completion, "rootWalk"));
    TIDE_CHECK(model_metadata_statistics().parses == warm.parses + 1u);
    write_file("lsp_animation_list/art/run.gltf", "{invalid JSON}");
    notify_file(uri_run, 2);
    open_uri(uri, text);
    completion = request_at(uri, "textDocument/completion", 4, 13, "");
    TIDE_CHECK(!offers(completion, "rootWalk") && !offers(completion, "rootJog") && offers(completion, "TryGetByName"));
    remove_game_file("lsp_animation_list/art/run.gltf");
    remove_game_file("lsp_animation_list/art/walk.gltf");
    remove_game_file("lsp_animation_list/main.tide");
    remove_game_file("lsp_animation_list/.tide/assets.sha256");
    remove_game_file("lsp_animation_list/.tide/.gitignore");
    remove_folder("lsp_animation_list/.tide");
    remove_folder("lsp_animation_list/art");
    remove_folder("lsp_animation_list");
    clear_sent();
    lsp_free(&server);
    model_metadata_forget();
}

TIDE_TEST(lsp_animation_assets_use_cached_catalogs_and_refresh_names)
{
    if (!find_game_dir()) return;
    model_metadata_forget();
    make_folder("lsp_animations");
    make_folder("lsp_animations/art");
    static const char prefix[] = "asset AnimationCatalog movement = \"art/movement.gltf\";\nscene Main { }\n";
    static const char metadata[] = "{\"asset\":{\"version\":\"2.0\"},"
                                   "\"animations\":[{\"name\":\"Idle\"},{\"name\":\"Run\"},{\"name\":\"bad name\"},"
                                   "{\"name\":\"Jump\"},{\"name\":\"jump\"},{\"name\":\"for\"}],"
                                   "\"nodes\":[{\"name\":\"Root\"}],"
                                   "\"buffers\":[{\"uri\":\"must-not-read.bin\",\"byteLength\":99999999}]}";
    write_file("lsp_animations/main.tide", prefix);
    write_file("lsp_animations/art/movement.gltf", metadata);
    pin_file_time("lsp_animations/art/movement.gltf");
    write_file("lsp_animations/art/other.glb", "not inspected by path completion");
    write_file("lsp_animations/art/other.obj", "not glTF");
    char root[700], uri_main[700], uri_model[700], text[2048];
    const char *dir = game_dir[0] == '/' ? game_dir + 1 : game_dir;
    snprintf(root, sizeof root, "file:///%s/lsp_animations", dir);
    snprintf(uri_main, sizeof uri_main, "file:///%s/lsp_animations/main.tide", dir);
    snprintf(uri_model, sizeof uri_model, "file:///%s/lsp_animations/art/movement.gltf", dir);
    start_in_with(root, ASSET_EDITOR_CAPABILITIES);
    snprintf(text, sizeof text, "%sview V()\n{\n    var clips = movement;\n    clips.\n}\n", prefix);
    open_uri(uri_main, text);
    const char *completion = request_at(uri_main, "textDocument/completion", 5, 10, "");
    TIDE_CHECK(offers(completion, "idle") && offers(completion, "run") && offers(completion, "count")
               && offers(completion, "TryGetByName") && offers(completion, "TryGetAt"));
    TIDE_CHECK(!offers(completion, "bad name") && !offers(completion, "jump") && !offers(completion, "for")
               && !offers(completion, "data") && !offers(completion, "skeleton") && !offers(completion, "Decode"));
    TIDE_CHECK(has(completion, "original source name `Idle`"));
    model_metadata_stats warm = model_metadata_statistics();
    TIDE_CHECK(warm.parses == 1 && warm.file_reads == 1 && warm.bytes_read == sizeof metadata - 1);
    open_uri(uri_main, text);
    completion = request_at(uri_main, "textDocument/completion", 5, 10, "");
    TIDE_CHECK(offers(completion, "idle") && model_metadata_statistics().parses == warm.parses
               && model_metadata_statistics().file_reads == warm.file_reads);

    snprintf(text, sizeof text, "%sview V() { Debug.Log(movement.idle.name); }\n", prefix);
    open_uri(uri_main, text);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    const char *hover = request_at(uri_main, "textDocument/hover", 2, 31, "");
    TIDE_CHECK(has(hover, "Animation idle") && has(hover, "original source name `Idle`"));
    hover = request_at(uri_main, "textDocument/hover", 0, (int)strlen("asset AnimationCatalog ") + 2, "");
    TIDE_CHECK(has(hover, "asset AnimationCatalog movement") && has(hover, "without typed render geometry"));
    completion = request_at(uri_main, "textDocument/completion", 0, 40, "");
    TIDE_CHECK(offers(completion, "movement.gltf") && offers(completion, "other.glb") && !offers(completion, "other.obj"));

    snprintf(text, sizeof text, "%svoid Helper(AnimationCatalog clips)\n{\n    clips.\n}\n", prefix);
    open_uri(uri_main, text);
    completion = request_at(uri_main, "textDocument/completion", 4, 10, "");
    TIDE_CHECK(offers(completion, "count") && offers(completion, "TryGetByName") && offers(completion, "TryGetAt")
               && !offers(completion, "idle"));
    snprintf(text, sizeof text, "%sfrag Shade(Vertex3 vertex, out Color color)\n{\n    movement.\n}\n", prefix);
    open_uri(uri_main, text);
    completion = request_at(uri_main, "textDocument/completion", 4, 13, "");
    TIDE_CHECK(!offers(completion, "count") && !offers(completion, "TryGetByName") && !offers(completion, "idle"));

    char replacement[sizeof metadata];
    memcpy(replacement, metadata, sizeof metadata);
    memcpy(strstr(replacement, "Idle"), "Walk", 4);
    memcpy(strstr(replacement, "Run"), "Jog", 3);
    write_file("lsp_animations/art/movement.gltf", replacement);
    pin_file_time("lsp_animations/art/movement.gltf");
    snprintf(text, sizeof text, "%sview V()\n{\n    movement.\n}\n", prefix);
    open_uri(uri_main, text);
    completion = request_at(uri_main, "textDocument/completion", 4, 13, "");
    TIDE_CHECK(offers(completion, "idle") && !offers(completion, "walk"));
    notify_file(uri_model, 2);
    completion = request_at(uri_main, "textDocument/completion", 4, 13, "");
    TIDE_CHECK(offers(completion, "walk") && offers(completion, "jog") && !offers(completion, "idle") && !offers(completion, "run"));
    TIDE_CHECK(model_metadata_statistics().parses == warm.parses + 1 && model_metadata_statistics().file_reads == warm.file_reads + 1);

    write_file("lsp_animations/art/movement.gltf", "{broken JSON}");
    notify_file(uri_model, 2);
    snprintf(text, sizeof text, "%sview V() { Debug.Log(movement.walk.name); }\n", prefix);
    open_uri(uri_main, text);
    TIDE_CHECK(has(last_sent(), "animation catalog metadata is invalid") && has(last_sent(), "has incomplete or invalid JSON")
               && !has(last_sent(), "has no checked member 'walk'"));
    remove_game_file("lsp_animations/art/movement.gltf");
    notify_file(uri_model, 3);
    open_uri(uri_main, text);
    TIDE_CHECK(has(last_sent(), "animation catalog metadata is unavailable") && !has(last_sent(), "has no checked member 'walk'"));
    model_metadata_forget();
    static const char remote[] = "asset AnimationCatalog movement = \"https://must-never-be-contacted.invalid/run.glb\";\n"
                                 "scene Main { }\nview V() { if (movement.TryGetByName(\"Run\", out var run)) Debug.Log(run.name); }\n";
    open_uri(uri_main, remote);
    TIDE_CHECK(has(last_sent(), "metadata is unavailable") && !has(last_sent(), "\"severity\":1"));
    TIDE_CHECK(model_metadata_statistics().file_reads == 0 && model_metadata_statistics().parses == 0);
    write_file("lsp_animations/art/rest.gltf", "{\"asset\":{\"version\":\"2.0\"},\"nodes\":[{\"name\":\"Root\"}]}");
    static const char rest[] = "asset SkinnedMesh<Vertex3> hero = \"art/rest.gltf\";\nscene Main { }\n"
                               "view V()\n{\n    var pose = hero.defaultPose;\n    Draw.SkinnedMesh(hero, float4x4.identity, pose);\n"
                               "    Draw.SkinnedMesh(hero, float4x4.identity);\n}\n";
    open_uri(uri_main, rest);
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    hover = request_at(uri_main, "textDocument/hover", 4, 24, "");
    TIDE_CHECK(has(hover, "Pose defaultPose") && has(hover, "without animation clips"));
    remove_game_file("lsp_animations/main.tide");
    remove_game_file("lsp_animations/art/rest.gltf");
    remove_game_file("lsp_animations/art/other.glb");
    remove_game_file("lsp_animations/art/other.obj");
    remove_game_file("lsp_animations/.tide/assets.sha256");
    remove_game_file("lsp_animations/.tide/.gitignore");
    remove_folder("lsp_animations/.tide");
    remove_folder("lsp_animations/art");
    remove_folder("lsp_animations");
    clear_sent();
    lsp_free(&server);
    model_metadata_forget();
}

TIDE_TEST(lsp_double_types_swizzles_parsing_and_signatures)
{
    start();
    open_document("component Body { [Pos$ition] double3 position; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "float or double scalar/vector"));
    open_document("singleton Data { dou$ble value = 1.25d; double3 position; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "\"diagnostics\":[]"));
    TIDE_CHECK(has(request("textDocument/hover"), "64-bit IEEE float"));
    const char *types = complete("singleton Data { $ }\nscene Main { }\n");
    TIDE_CHECK(offers(types, "double") && offers(types, "double2") && offers(types, "double3") && offers(types, "double4"));
    TIDE_CHECK(offers(types, "double2x2") && offers(types, "double3x3") && offers(types, "double4x4"));
    const char *members = complete("singleton Data { double3 position; }\nscene Main { }\nsystem S(Data data) { var x = data.position.$ }\n");
    TIDE_CHECK(offers(members, "x") && offers(members, "xz") && has(members, "double2"));
    const char *parsing = complete("scene Main { }\nsystem S() { var ok = double.$ }\n");
    TIDE_CHECK(offers(parsing, "TryParse") && has(parsing, "out double value"));
    TIDE_CHECK(offers(parsing, "PI") && offers(parsing, "TAU") && offers(parsing, "E"));
    const char *columns = complete("singleton Data { double3x3 basis; }\nscene Main { }\nsystem S(Data data) { var x = data.basis.$ }\n");
    TIDE_CHECK(offers(columns, "c0") && offers(columns, "c2") && has(columns, "double3"));
    open_document("singleton Data { double3x3 basis; }\nscene Main { }\nsystem S(Data data) { var x = data.basis.c0.$x; Debug.Log(x); }\n");
    TIDE_CHECK(has(request("textDocument/hover"), "double"));
    open_document("scene Main { }\nsystem S() { var v = double3($); Debug.Log(v); }\n");
    TIDE_CHECK(has(request("textDocument/signatureHelp"), "double3(double x, double y, double z)"));
    const char *gpu = complete("frag Shade(Vertex3 vertex, out Color color) { $ }\n");
    TIDE_CHECK(!offers(gpu, "double") && !offers(gpu, "double3") && !offers(gpu, "double4x4"));
    open_document("singleton Data { float value = 1d; }\nscene Main { }\n");
    TIDE_CHECK(has(last_sent(), "field 'value' is float, not double") && has(last_sent(), "float(x) converts explicitly"));
}
