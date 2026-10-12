// A native authoring tool. It inspects compiler data, never a live match.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "document.h"
#include "packages.h"
#include "sys.h"
#include "tide/platform.h"
#include "tide/text.h"

typedef enum action { NONE, SAVE, RELOAD, DISCARD, UNDO, REDO, SET, RESET, ADD, REMOVE, CHILD, DELETE_NODE, PREFAB, RECOVER, DROP_RECOVERY } action;
typedef struct editor {
    editor_document doc;
    tide_draw_list draw;
    tide_gui gui;
    tide_devices devices;
    int selected, inspector_scroll, tree_scroll, diagnostic_scroll, source_scroll;
    bool source, xy, picker, nested_picker, discard, recovery, value_selected;
    float zoom, poll;
    tide_float2 pan;
    char *component, *field, *value;
    action pending;
    int argument;
    char *pending_name;
} editor;

static char *own(const char *s)
{
    char *p = malloc(strlen(s) + 1);
    if (!p) abort();
    strcpy(p, s); return p;
}
static char *own_slice(const str s)
{
    char *p = malloc((size_t)s.len + 1);
    if (!p) abort();
    if (s.len) memcpy(p, s.ptr, (size_t)s.len);
    p[s.len] = '\0'; return p;
}
static void replace(char **p, const char *s) { free(*p); *p = own(s); }
static tide_color rgb(float r, float g, float b) { return (tide_color){r, g, b, 1}; }
static const tide_color dim = {0.55f, 0.61f, 0.67f, 1}, ink = {0.86f, 0.9f, 0.93f, 1};

static bool contains(tide_rect r, tide_float2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
static void panel(editor *e, tide_rect r, tide_color color)
{
    tide_draw_rect(&e->draw, tide_f2(r.x + r.width / 2, r.y + r.height / 2), tide_f2(r.width, r.height), color);
}
static void label(editor *e, float x, float y, const char *s, tide_color color) { tide_draw_text(&e->draw, s, tide_f2(x, y), 16, color); }
static bool button(editor *e, uint32_t id, tide_rect r, const char *s) { return tide_gui_button(&e->gui, id, r, s); }
static void queue(editor *e, action a) { if (e->pending == NONE) e->pending = a; }
static void queue_name(editor *e, action a, str name)
{
    if (e->pending != NONE) return;
    e->pending_name = own(str_to_cstr(name)); e->pending = a;
}
static void clear_field(editor *e)
{
    free(e->component); free(e->field); free(e->value); e->component = e->field = e->value = NULL;
    e->value_selected = false;
}

static int scroll_rows(float value, int factor)
{
    return isfinite(value) ? (int)tide_clamp_f(value * (float)factor, -100, 100) : 0;
}

// Preview understands literal TRS, constants and basic float arithmetic. An
// unsupported transform is reported and its subtree omitted, never shown at
// a guessed position. No script, extern or user view executes in the editor.
static bool scalar(const expr *v, float *out, int depth)
{
    if (!v || depth > 64) return false;
    if (v->kind == E_INT) *out = (float)v->int_value;
    else if (v->kind == E_FLOAT) {
        if (v->is_degrees) *out = v->float_value;
        else {
            char *digits = arena_alloc((size_t)v->text.len + 1); int n = 0;
            for (int i = 0; i < v->text.len; i++) if (v->text.ptr[i] != '_') digits[n++] = v->text.ptr[i];
            *out = strtof(digits, NULL);
        }
    } else if (v->bind == BIND_CONST) return scalar(v->constant->value, out, depth + 1);
    else if (v->kind == E_UNARY && (v->op == T_MINUS || v->op == T_PLUS)) {
        if (!scalar(v->lhs, out, depth + 1)) return false;
        if (v->op == T_MINUS) *out = -*out;
    } else if (v->kind == E_BINARY && v->type.kind == TY_FLOAT) {
        float a, b;
        if (!scalar(v->lhs, &a, depth + 1) || !scalar(v->rhs, &b, depth + 1)) return false;
        switch (v->op) {
        case T_PLUS: *out = a + b; break;
        case T_MINUS: *out = a - b; break;
        case T_STAR: *out = a * b; break;
        case T_SLASH: *out = a / b; break;
        default: return false;
        }
    } else return false;
    return isfinite(*out);
}

static bool vector(const expr *v, float *out, int count, int depth)
{
    if (!v) return true;
    if (v->kind == E_DEFAULT) { for (int i = 0; i < count; i++) out[i] = 0; return true; }
    if (depth > 64) return false;
    if (v->bind == BIND_CONST) return vector(v->constant->value, out, count, depth + 1);
    if (v->kind == E_CALL && (str_eq_c(v->name, "float3") || str_eq_c(v->name, "quaternion"))) {
        if (v->args.count == 1 && count == 3) {
            float f; if (!scalar(v->args.items[0], &f, 0)) return false;
            for (int i = 0; i < count; i++) out[i] = f;
            return true;
        }
        if (v->args.count != count) return false;
        for (int i = 0; i < count; i++) if (!scalar(v->args.items[i], &out[i], 0)) return false;
        return true;
    }
    if (count == 4 && v->kind == E_MEMBER && str_eq_c(v->member, "identity")) return true;
    if (count == 4 && v->kind == E_METHOD && str_eq_c(v->name, "Euler") && v->args.count == 1) {
        float xyz[3] = {0}; if (!vector(v->args.items[0], xyz, 3, depth + 1)) return false;
        tide_quaternion q = tide_euler_q(tide_f3(xyz[0], xyz[1], xyz[2]));
        memcpy(out, &q, sizeof q); return true;
    }
    return false;
}

static bool local_matrix(const editor_node *n, tide_float4x4 *m)
{
    if (!n->effective) return false;
    expr *c = editor_component(n, str_from("Transform"));
    *m = tide_identity_f4x4();
    if (!c || !c->type_decl) return true;
    float pos[3] = {0}, rot[4] = {0, 0, 0, 1}, scale[3] = {1, 1, 1};
    for (int i = 0; i < c->type_decl->fields.count; i++) {
        field *f = &c->type_decl->fields.items[i];
        float *out = str_eq_c(f->name, "localPosition") ? pos : str_eq_c(f->name, "localRotation") ? rot : scale;
        if (!vector(editor_value(c, f), out, out == rot ? 4 : 3, 0)) return false;
    }
    *m = tide_trs_f4x4(tide_f3(pos[0], pos[1], pos[2]), tide_q(rot[0], rot[1], rot[2], rot[3]), tide_f3(scale[0], scale[1], scale[2]));
    return true;
}

static tide_float2 project(editor *e, tide_rect r, tide_float3 p)
{
    return tide_f2(r.x + r.width / 2 + e->pan.x + (p.x + (e->xy ? 0 : p.z * 0.6f)) * e->zoom,
                   r.y + r.height / 2 + e->pan.y + (-p.y + (e->xy ? 0 : p.z * 0.35f)) * e->zoom);
}

static void viewport(editor *e, tide_rect r)
{
    tide_draw_set_clip(&e->draw, r);
    tide_draw_set_clip(&e->gui.list, r);
    if (button(e, 70, (tide_rect){r.x + 14, r.y + 42, 82, 26}, e->xy ? "XY" : "Isometric")) e->xy = !e->xy;
    if (button(e, 71, (tide_rect){r.x + 106, r.y + 42, 82, 26}, "Center")) e->pan = tide_f2(0, 0);
    if (contains(r, e->gui.mouse)) {
        e->zoom *= 1 + e->devices.mouse.poll_scroll.y * 0.1f;
        e->zoom = tide_clamp_f(e->zoom, 8, 256);
        if (e->devices.mouse.middle.pressed) {
            e->pan.x += e->devices.mouse.poll_delta.x; e->pan.y -= e->devices.mouse.poll_delta.y;
        }
    }
    for (int i = -20; i <= 20; i++) {
        const tide_color c = i == 0 ? rgb(0.23f, 0.34f, 0.38f) : rgb(0.13f, 0.17f, 0.20f);
        if (e->xy) {
            tide_draw_line(&e->draw, project(e, r, tide_f3((float)i, -20, 0)), project(e, r, tide_f3((float)i, 20, 0)), c);
            tide_draw_line(&e->draw, project(e, r, tide_f3(-20, (float)i, 0)), project(e, r, tide_f3(20, (float)i, 0)), c);
        } else {
            tide_draw_line(&e->draw, project(e, r, tide_f3((float)i, 0, -20)), project(e, r, tide_f3((float)i, 0, 20)), c);
            tide_draw_line(&e->draw, project(e, r, tide_f3(-20, 0, (float)i)), project(e, r, tide_f3(20, 0, (float)i)), c);
        }
    }
    const int count = e->doc.nodes.count;
    tide_float4x4 *matrices = arena_alloc((size_t)count * sizeof(*matrices));
    bool *valid = arena_alloc((size_t)count * sizeof(*valid));
    int omitted = 0;
    for (int i = 0; i < count; i++) {
        editor_node *n = &e->doc.nodes.items[i];
        const int parent = n->parent;
        valid[i] = local_matrix(n, &matrices[i]) && (parent < 0 || (parent < i && valid[parent]));
        if (!valid[i]) { omitted++; continue; }
        if (parent >= 0) matrices[i] = tide_mul_f4x4(matrices[parent], matrices[i]);
        const tide_float3 p = tide_transform_f4x4(matrices[i], tide_f3(0, 0, 0));
        const tide_float2 screen = project(e, r, p);
        if (!isfinite(screen.x) || !isfinite(screen.y) || fabsf(screen.x) > 1e6f || fabsf(screen.y) > 1e6f) { valid[i] = false; omitted++; continue; }
        if (parent >= 0) tide_draw_line(&e->draw, project(e, r, tide_transform_f4x4(matrices[parent], tide_f3(0, 0, 0))), screen, rgb(0.27f, 0.42f, 0.47f));
        tide_color color = i == e->selected ? rgb(0.3f, 0.83f, 0.93f) : rgb(0.43f, 0.53f, 0.59f);
        // A unit wire cube is a transform marker, not an assumed game mesh.
        tide_float2 corners[8];
        for (int j = 0; j < 8; j++) corners[j] = project(e, r, tide_transform_f4x4(matrices[i], tide_f3(j & 1 ? .5f : -.5f, j & 2 ? .5f : -.5f, j & 4 ? .5f : -.5f)));
        for (int j = 0; j < 8; j++) for (int bit = 1; bit <= 4; bit *= 2) if (!(j & bit)) {
            const tide_float2 a = corners[j], b = corners[j | bit];
            if (isfinite(a.x) && isfinite(a.y) && isfinite(b.x) && isfinite(b.y) && fabsf(a.x) < 1e6f && fabsf(a.y) < 1e6f && fabsf(b.x) < 1e6f && fabsf(b.y) < 1e6f)
                tide_draw_line(&e->draw, a, b, color);
        }
        if (contains(r, screen) && screen.y > r.y + 85) {
            const tide_rect hit = {screen.x - 10, screen.y - 10, 20, 20};
            if (tide_gui_control(&e->gui, 10000u + (uint32_t)i, hit).clicked) { e->selected = i; e->inspector_scroll = 0; clear_field(e); }
            tide_draw_circle(&e->draw, screen, i == e->selected ? 5 : 3, color);
            label(e, screen.x + 10, screen.y - 8, n->path.len ? str_to_cstr(n->path) : "Root", color);
        }
        if (i == e->selected) {
            static const tide_float3 axes[] = {{1,0,0}, {0,1,0}, {0,0,1}};
            static const tide_color colors[] = {{.95f,.35f,.35f,1}, {.4f,.87f,.4f,1}, {.4f,.6f,.95f,1}};
            for (int axis = 0; axis < 3; axis++) tide_draw_thick_line(&e->draw, screen, project(e, r, tide_transform_f4x4(matrices[i], axes[axis])), 2, colors[axis]);
        }
    }
    panel(e, (tide_rect){r.x,r.y,r.width,34}, rgb(.075f,.095f,.115f));
    label(e, r.x + 16, r.y + 10, "TRANSFORM PREVIEW", dim);
    panel(e, (tide_rect){r.x,r.y+r.height-42,r.width,42}, rgb(.075f,.095f,.115f));
    char message[180];
    snprintf(message, sizeof message, omitted ? "%d transform(s) could not be previewed. Inspect their Tide expressions." : "Transform markers  |  Wheel: zoom  |  Middle drag: pan", omitted);
    label(e, r.x + 16, r.y + r.height - 30, message, omitted ? rgb(.95f,.65f,.3f) : dim);
    tide_draw_unset_clip(&e->draw); tide_draw_unset_clip(&e->gui.list);
}

static void tree(editor *e, tide_rect r)
{
    label(e, r.x + 14, r.y + 12, "HIERARCHY", dim);
    if (e->doc.prefabs.count) {
        decl *p = e->doc.prefabs.items[e->doc.active];
        label(e, r.x + 14, r.y + 43, str_to_cstr(p->name), ink);
        if (e->doc.prefabs.count > 1 && button(e, 80, (tide_rect){r.x + r.width - 74, r.y + 38, 60, 26}, "Next")) {
            queue(e, PREFAB); e->argument = (e->doc.active + 1) % e->doc.prefabs.count;
        }
    }
    const tide_rect content = {r.x + 8, r.y + 76, r.width - 16, r.height - 154};
    if (contains(content, e->gui.mouse)) e->tree_scroll -= scroll_rows(e->devices.mouse.poll_scroll.y, 3);
    const int visible = (int)(content.height / 30);
    const int max = e->doc.nodes.count > visible ? e->doc.nodes.count - visible : 0;
    e->tree_scroll = e->tree_scroll < 0 ? 0 : e->tree_scroll > max ? max : e->tree_scroll;
    for (int i = e->tree_scroll; i < e->doc.nodes.count && i < e->tree_scroll + visible; i++) {
        editor_node *n = &e->doc.nodes.items[i]; int depth = 0;
        for (int p = n->parent; p >= 0 && depth < 128; p = e->doc.nodes.items[p].parent) depth++;
        sb b = {0}; const char *name = n->path.len ? str_to_cstr(n->path) : "Root";
        const char *last = strrchr(name, '.'); if (last) name = last + 1;
        sb_printf(&b, "%s%s", name, n->authored ? "" : "  (inherited)");
        const float indent = (float)(depth < 6 ? depth : 6) * 12;
        tide_rect row = {content.x + indent, content.y + (float)(i - e->tree_scroll) * 30, content.width - indent, 26};
        const int theme = tide_gui_begin_theme(&e->gui, (tide_gui_theme){.text = ink, .control = i == e->selected ? rgb(.14f,.32f,.38f) : rgb(.11f,.14f,.17f), .hot = rgb(.19f,.26f,.3f), .pressed = rgb(.14f,.32f,.38f), .accent = rgb(.3f,.83f,.93f), .radius = 3});
        if (button(e, 2000u + (uint32_t)i, row, b.data)) { e->selected = i; e->inspector_scroll = 0; clear_field(e); }
        tide_gui_close(&e->gui, theme);
    }
    if (button(e, 81, (tide_rect){r.x + 14, r.y + r.height - 66, 100, 26}, "+ Child")) queue(e, CHILD);
    if (button(e, 82, (tide_rect){r.x + 124, r.y + r.height - 66, 106, 26}, "+ Prefab")) { e->picker = true; e->nested_picker = true; e->argument = 0; }
    if (button(e, 83, (tide_rect){r.x + 14, r.y + r.height - 34, 216, 26}, "Delete selected subtree")) queue(e, DELETE_NODE);
}

static char *field_text(editor *e, expr *c, field *f)
{
    expr *v = editor_value(c, f);
    char *reference = editor_reference_expression(&e->doc, e->selected, v);
    if (reference) return reference;
    if (v && v->bind == BIND_CONST && v->constant) return str_to_cstr(v->constant->qualified);
    if (v && v->at.line > 0) return editor_expression(v);
    if (str_eq_c(c->type_decl->name, "Transform")) {
        if (str_eq_c(f->name, "localRotation")) return str_to_cstr(str_from("quaternion.identity"));
        if (str_eq_c(f->name, "localScale")) return str_to_cstr(str_from("float3(1)"));
        return str_to_cstr(str_from("float3(0)"));
    }
    return str_to_cstr(str_from("default"));
}

static void inspector(editor *e, tide_rect r)
{
    label(e, r.x + 14, r.y + 12, "INSPECTOR", dim);
    if (e->selected < 0 || e->selected >= e->doc.nodes.count) return;
    editor_node *n = &e->doc.nodes.items[e->selected];
    label(e, r.x + 14, r.y + 44, n->path.len ? str_to_cstr(n->path) : "Root entity", ink);
    label(e, r.x + 14, r.y + 70, n->authored ? "Values are Tide expressions. Enter to apply." : "Inherited child: edit its source prefab.", dim);
    const tide_rect content = {r.x + 14, r.y + 106, r.width - 28, r.height - 292};
    if (contains(content, e->gui.mouse)) e->inspector_scroll -= scroll_rows(e->devices.mouse.poll_scroll.y, 3);
    if (e->inspector_scroll < 0) e->inspector_scroll = 0;
    int row = 0;
    if (n->effective) for (int c = 0; c < n->effective->args.count; c++) {
        expr *comp = n->effective->args.items[c]; decl *type = comp->type_decl;
        if (!type) continue;
        float y = content.y + (float)(row++ - e->inspector_scroll) * 30;
        if (y >= content.y && y + 28 <= content.y + content.height) {
            label(e, content.x, y + 3, str_to_cstr(type->qualified), rgb(.3f,.83f,.93f));
            if (button(e, 3000u + (uint32_t)c, (tide_rect){content.x + content.width - 64, y, 64, 24}, "Remove")) queue_name(e, REMOVE, type->qualified);
        }
        for (int f = 0; f < type->fields.count; f++) {
            field *item = &type->fields.items[f]; if (item->hidden || item->is_local) continue;
            y = content.y + (float)(row++ - e->inspector_scroll) * 30;
            if (y < content.y || y + 28 > content.y + content.height) continue;
            sb b = {0}; sb_printf(&b, STR_FMT " = %s", STR_ARG(item->name), field_text(e, comp, item));
            if (button(e, 100000u + (uint32_t)row, (tide_rect){content.x, y, content.width, 26}, b.data)) {
                replace(&e->component, str_to_cstr(type->qualified)); replace(&e->field, str_to_cstr(item->name)); replace(&e->value, field_text(e, comp, item));
                e->value_selected = false;
            }
        }
        row++;
    }
    if (n->authored) for (int c = 0; c < n->authored->components.count; c++) {
        expr *raw = n->authored->components.items[c]; if (raw->type_decl) continue;
        const float y = content.y + (float)(row++ - e->inspector_scroll) * 30;
        if (y < content.y || y + 28 > content.y + content.height) continue;
        sb b = {0}; sb_printf(&b, STR_FMT " (unresolved)", STR_ARG(raw->name));
        label(e, content.x, y + 3, b.data, rgb(.98f,.53f,.4f));
        if (button(e, 200000u + (uint32_t)c, (tide_rect){content.x+content.width-64,y,64,24}, "Remove")) queue_name(e, REMOVE, raw->name);
    }
    const int visible = (int)(content.height / 30), maximum = row > visible ? row - visible : 0;
    if (e->inspector_scroll > maximum) e->inspector_scroll = maximum;
    if (e->component) {
        sb b = {0}; sb_printf(&b, "%s.%s", e->component, e->field);
        label(e, r.x + 14, r.y + r.height - 177, b.data, ink);
        const bool ctrl = e->devices.keyboard.leftCtrl.pressed || e->devices.keyboard.rightCtrl.pressed;
        if (e->gui.editing == 90 && ctrl && e->devices.keyboard.a.down) e->value_selected = true;
        if (e->gui.editing == 90 && e->value_selected && (e->gui.text.count || e->devices.keyboard.backspace.down)) {
            replace(&e->value, ""); e->gui.edit_len = 0; e->gui.edit[0] = '\0'; e->value_selected = false;
        }
        tide_str value = tide_str_from_cstr(e->value); bool entered = false;
        const bool too_long = strlen(e->value) >= sizeof e->gui.edit;
        const int disabled = tide_gui_begin_disabled(&e->gui, too_long);
        if (tide_gui_text_field_entered(&e->gui, 90, (tide_rect){r.x + 14, r.y + r.height - 146, r.width - 28, 30}, "Value", (tide_textref){.local = &value}, &entered)) replace(&e->value, tide_str_c(value));
        tide_gui_close(&e->gui, disabled);
        label(e, r.x + 14, r.y + r.height - 68, too_long ? "Long expression: edit it in your code editor." : e->value_selected ? "Entire value selected; type to replace." : "Ctrl+A selects the whole value.", dim);
        if (button(e, 91, (tide_rect){r.x + 14, r.y + r.height - 106, 110, 28}, "Apply") || entered) queue(e, SET);
        if (button(e, 92, (tide_rect){r.x + 134, r.y + r.height - 106, 110, 28}, "Reset override")) queue(e, RESET);
    }
    if (button(e, 93, (tide_rect){r.x + 14, r.y + r.height - 48, r.width - 28, 30}, "+ Add component")) { e->picker = true; e->nested_picker = false; e->argument = 0; }
}

static void picker(editor *e, tide_float2 screen)
{
    const int modal = tide_gui_begin_modal(&e->gui, 100, TIDE_ANCHOR_MIDDLE_CENTER, &e->picker);
    if (modal < 0) return;
    tide_gui_layout_label(&e->gui, e->nested_picker ? "Nest a prefab under the selected entity" : "Add a component from this project");
    int offered = 0;
    // Bound the modal to fit; paging follows the same scroll wheel as panels.
    if (e->devices.mouse.poll_scroll.y) e->argument -= scroll_rows(e->devices.mouse.poll_scroll.y, 3);
    if (e->argument < 0) e->argument = 0;
    const int visible = (int)((screen.y - 220) / 30);
    for (int i = 0; i < e->doc.prog->decls.count; i++) {
        decl *d = e->doc.prog->decls.items[i];
        if (e->nested_picker ? d->kind != DECL_PREFAB : (d->kind != DECL_COMPONENT || d->is_scene || d->placement_of)) continue;
        if (e->nested_picker && e->doc.prefabs.count && d == e->doc.prefabs.items[e->doc.active]) continue;
        if (offered++ < e->argument || offered > e->argument + visible) continue;
        if (tide_gui_layout_button(&e->gui, 12000u + (uint32_t)i, str_to_cstr(d->qualified))) {
            queue_name(e, e->nested_picker ? CHILD : ADD, d->qualified); e->picker = false;
        }
    }
    // Transform is built in but not always in prog.decls until named.
    if (!e->nested_picker && tide_gui_layout_button(&e->gui, 101, "Transform")) { queue_name(e, ADD, str_from("Transform")); e->picker = false; }
    tide_gui_layout_label(&e->gui, "Wheel to scroll. Escape or Cancel to return.");
    if (tide_gui_layout_button(&e->gui, 102, "Cancel")) e->picker = false;
    tide_gui_close(&e->gui, modal);
}

static int frame(void *user, float seconds)
{
    editor *e = user;
    char *selected_path = e->selected >= 0 && e->selected < e->doc.nodes.count ? own_slice(e->doc.nodes.items[e->selected].path) : NULL;
    const uint64_t revision = e->doc.revision;
    e->poll += seconds;
    if (e->poll >= .5f) { e->poll = 0; editor_poll(&e->doc); }
    if (e->doc.revision != revision) {
        e->selected = 0;
        for (int i = 0; selected_path && i < e->doc.nodes.count; i++) if (str_eq_c(e->doc.nodes.items[i].path, selected_path)) e->selected = i;
        clear_field(e);
    }
    free(selected_path);
    if (e->selected >= e->doc.nodes.count) e->selected = 0;
    tide_platform_poll(&e->devices);
    const uint32_t scratch = tide_scratch_mark();
    const arena_mark mark = arena_checkpoint();
    const tide_float2 size = tide_platform_screen_size();
    tide_draw_reset(&e->draw); tide_draw_set_screen(&e->draw);
    tide_draw_clear(&e->draw, rgb(.075f,.095f,.115f));
    if (size.x < 1100 || size.y < 650) {
        label(e, 24, 30, "Resize the scene editor to at least 1100 x 650 to show all panels.", ink);
        tide_platform_draw(&e->draw); tide_devices_consume(&e->devices);
        arena_rewind(mark); tide_scratch_reset(scratch); return TIDE_KEEP_RUNNING;
    }
    tide_gui_begin(&e->gui, &e->devices, size, tide_platform_measure_text);
    tide_gui_theme theme = tide_gui_theme_default();
    theme.text = ink; theme.panel = rgb(.11f,.14f,.17f); theme.control = rgb(.16f,.20f,.24f);
    theme.hot = rgb(.22f,.29f,.34f); theme.pressed = rgb(.13f,.32f,.39f); theme.accent = rgb(.3f,.83f,.93f);
    theme.field = rgb(.065f,.085f,.10f); theme.field_hot = rgb(.10f,.15f,.18f); theme.radius = 3;
    const int themed = tide_gui_begin_theme(&e->gui, theme);
    panel(e, (tide_rect){0,0,size.x,52}, rgb(.11f,.14f,.17f));
    label(e, 18, 16, "TIDE", rgb(.3f,.83f,.93f));
    if (button(e, 1, (tide_rect){90,12,72,28}, "Save")) queue(e, SAVE);
    if (button(e, 2, (tide_rect){172,12,72,28}, "Undo")) queue(e, UNDO);
    if (button(e, 3, (tide_rect){254,12,72,28}, "Redo")) queue(e, REDO);
    if (button(e, 4, (tide_rect){336,12,86,28}, "Reload")) { if (editor_dirty(&e->doc)) e->discard = true; else queue(e, RELOAD); }
    if (button(e, 5, (tide_rect){432,12,86,28}, e->source ? "Viewport" : "Source")) e->source = !e->source;
    sb title = {0}; sb_printf(&title, "%s%s", path_base(e->doc.path), editor_dirty(&e->doc) ? " *" : "");
    label(e, 540, 18, title.data, ink);
    // Shortcuts read presses before GUI consumption, except while typing/modal.
    const bool ctrl = e->devices.keyboard.leftCtrl.pressed || e->devices.keyboard.rightCtrl.pressed;
    if (ctrl && !tide_gui_typing(&e->gui) && !e->picker && !e->discard && !e->recovery) {
        if (e->devices.keyboard.s.down) queue(e, SAVE);
        if (e->devices.keyboard.z.down) queue(e, e->devices.keyboard.leftShift.pressed ? REDO : UNDO);
        if (e->devices.keyboard.y.down) queue(e, REDO);
    }
    const float height = size.y - 230, right = size.x - 390;
    panel(e, (tide_rect){0,56,250,height}, rgb(.10f,.125f,.15f));
    panel(e, (tide_rect){right,56,390,height}, rgb(.10f,.125f,.15f));
    tree(e, (tide_rect){0,56,250,height});
    if (!e->source) viewport(e, (tide_rect){254,56,right-258,height});
    else {
        tide_rect r = {266,70,right-278,height-28};
        if (contains(r, e->gui.mouse)) e->source_scroll -= scroll_rows(e->devices.mouse.poll_scroll.y, 3);
        if (e->source_scroll < 0) e->source_scroll = 0;
        const char *p = e->doc.text; int line = 0;
        tide_draw_set_clip(&e->draw, r);
        while (*p) {
            const char *end = strchr(p, '\n'); if (!end) end = p + strlen(p);
            if (line >= e->source_scroll && line < e->source_scroll + (int)(r.height / 22)) {
                sb text = {0}; sb_printf(&text, "%3d  ", line + 1); sb_putn(&text, p, (size_t)(end - p));
                label(e, r.x, r.y + (float)(line - e->source_scroll) * 22, text.data, ink);
            }
            line++; p = *end ? end + 1 : end;
        }
        tide_draw_unset_clip(&e->draw);
    }
    inspector(e, (tide_rect){right,56,390,height});
    panel(e, (tide_rect){0,size.y-170,size.x,170}, rgb(.095f,.115f,.14f));
    char status[150]; snprintf(status, sizeof status, "PROJECT   %d error(s)   %d warning(s)%s", e->doc.errors, e->doc.warnings, e->doc.conflict ? "   FILE CONFLICT" : "");
    label(e, 16, size.y-158, status, e->doc.errors || e->doc.conflict ? rgb(.98f,.53f,.4f) : rgb(.3f,.83f,.93f));
    label(e, 420, size.y-158, e->doc.project, dim);
    tide_rect diagnostics = {12,size.y-130,size.x-24,82};
    if (contains(diagnostics, e->gui.mouse)) e->diagnostic_scroll -= scroll_rows(e->devices.mouse.poll_scroll.y, 2);
    const int maximum = e->doc.diagnostics.count > 3 ? e->doc.diagnostics.count - 3 : 0;
    e->diagnostic_scroll = e->diagnostic_scroll < 0 ? 0 : e->diagnostic_scroll > maximum ? maximum : e->diagnostic_scroll;
    tide_draw_set_clip(&e->draw, diagnostics);
    if (!e->doc.diagnostics.count) label(e, 16, size.y-126, "Project checked. Save writes only authored overrides; game code remains in .tide files.", dim);
    for (int i = e->diagnostic_scroll; i < e->doc.diagnostics.count && i < e->diagnostic_scroll + 3; i++) label(e, 16, size.y-126 + (float)(i-e->diagnostic_scroll)*25, e->doc.diagnostics.items[i], ink);
    tide_draw_unset_clip(&e->draw);
    label(e, 16, size.y-34, e->doc.message[0] ? e->doc.message : "Ctrl+S save   |   Ctrl+Z undo   |   Ctrl+Y redo   |   Edits stay in memory until saved", dim);
    if (e->picker) picker(e, size);
    const int modal = tide_gui_begin_modal(&e->gui, 103, TIDE_ANCHOR_MIDDLE_CENTER, &e->discard);
    if (modal >= 0) {
        tide_gui_layout_label(&e->gui, "Discard unsaved changes and reload the file?");
        if (tide_gui_layout_button(&e->gui, 104, "Discard + reload")) { queue(e, DISCARD); e->discard = false; }
        if (tide_gui_layout_button(&e->gui, 105, "Keep editing")) e->discard = false;
        tide_gui_close(&e->gui, modal);
    }
    const int recovery = tide_gui_begin_modal(&e->gui, 106, TIDE_ANCHOR_MIDDLE_CENTER, &e->recovery);
    if (recovery >= 0) {
        tide_gui_layout_label(&e->gui, "An unsaved draft from the previous editor session is available.");
        if (tide_gui_layout_button(&e->gui, 107, "Recover draft")) { queue(e, RECOVER); e->recovery = false; }
        if (tide_gui_layout_button(&e->gui, 108, "Keep source, discard draft")) { queue(e, DROP_RECOVERY); e->recovery = false; }
        tide_gui_close(&e->gui, recovery);
    }
    tide_gui_close(&e->gui, themed); tide_gui_end(&e->gui, &e->draw);
    tide_platform_draw(&e->draw); tide_devices_consume(&e->devices);
    arena_rewind(mark); tide_scratch_reset(scratch);
    // Apply after drawing: compiler refresh invalidates every AST pointer.
    const int selected = e->selected;
    selected_path = selected >= 0 && selected < e->doc.nodes.count ? own_slice(e->doc.nodes.items[selected].path) : NULL;
    switch (e->pending) {
    case SAVE: editor_save(&e->doc); break;
    case RELOAD: editor_reload(&e->doc, false); clear_field(e); break;
    case DISCARD: editor_reload(&e->doc, true); clear_field(e); break;
    case UNDO: editor_undo(&e->doc, false); clear_field(e); break;
    case REDO: editor_undo(&e->doc, true); clear_field(e); break;
    case SET: if (e->component) editor_set_field(&e->doc, selected, str_from(e->component), str_from(e->field), e->value); break;
    case RESET: if (e->component) editor_reset_field(&e->doc, selected, str_from(e->component), str_from(e->field)); clear_field(e); break;
    case ADD: editor_add_component(&e->doc, selected, str_from(e->pending_name)); break;
    case REMOVE: editor_remove_component(&e->doc, selected, str_from(e->pending_name)); clear_field(e); break;
    case CHILD: editor_add_child(&e->doc, selected, e->pending_name); break;
    case DELETE_NODE: editor_remove_node(&e->doc, selected); e->selected = 0; clear_field(e); break;
    case PREFAB: e->doc.active = e->argument; editor_refresh(&e->doc); e->selected = 0; clear_field(e); break;
    case RECOVER: editor_recover(&e->doc, true); clear_field(e); break;
    case DROP_RECOVERY: editor_recover(&e->doc, false); break;
    case NONE: break;
    }
    if (e->pending != NONE && e->pending != PREFAB && e->pending != DELETE_NODE) {
        bool found = false;
        for (int i = 0; selected_path && i < e->doc.nodes.count; i++) if (str_eq_c(e->doc.nodes.items[i].path, selected_path)) { e->selected = i; found = true; }
        if (!found) { e->selected = 0; clear_field(e); }
    }
    free(selected_path);
    free(e->pending_name); e->pending_name = NULL; e->pending = NONE;
    return TIDE_KEEP_RUNNING;
}

static void usage(void)
{
    puts("usage: tide open <file.tprefab> [--project <folder>] [--check]\n"
         "A native scene/prefab editor. Root: nearest tide.packages, otherwise the file's folder.\n"
         "--check validates without a window. --frames N, --capture file.png and --script file\n"
         "use the standard hidden input/capture loop; --size W H defaults to 1440 900.");
}

static bool positive(const char *text, uint32_t *out)
{
    char *end; const unsigned long long n = strtoull(text, &end, 10);
    if (!text[0] || *end || text[0] == '-' || n == 0 || n > UINT32_MAX) return false;
    *out = (uint32_t)n; return true;
}

int main(int argc, char **argv)
{
    const char *path = NULL, *project_root = NULL; bool check_only = false;
    tide_headless_desc hidden = {.width=1440,.height=900};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) { usage(); return 0; }
        if (strcmp(argv[i], "--project") == 0 && i + 1 < argc) project_root = argv[++i];
        else if (strcmp(argv[i], "--check") == 0) check_only = true;
        else if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) hidden.capture = argv[++i];
        else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) hidden.script = argv[++i];
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) { if (!positive(argv[++i], &hidden.frames)) { usage(); return 2; } }
        else if (strcmp(argv[i], "--size") == 0 && i + 2 < argc) {
            uint32_t w, h;
            if (!positive(argv[++i], &w) || !positive(argv[++i], &h) || w < 1100 || h < 650 || w > 8192 || h > 8192) { fputs("tide open: size must be 1100..8192 by 650..8192\n", stderr); return 2; }
            hidden.width = (int)w; hidden.height = (int)h;
        } else if (!path && argv[i][0] != '-') path = argv[i];
        else { usage(); return 2; }
    }
    bool prefab_file = path && strlen(path) >= 8;
    if (prefab_file) {
#ifdef _WIN32
        prefab_file = _stricmp(path + strlen(path) - 8, ".tprefab") == 0;
#else
        prefab_file = strcmp(path + strlen(path) - 8, ".tprefab") == 0;
#endif
    }
    if (!prefab_file) { fputs("tide open: first editor supports .tprefab files\n", stderr); return 2; }
    if (sys_hidden() && !check_only && !hidden.frames && !hidden.capture && !hidden.script) { fputs("tide open: TIDE_HIDDEN requires --check, --frames, --capture or --script\n", stderr); return 2; }
    char *bin = sys_exe_dir(), *root = path_dir(bin), *builtin = path_join(root, "src/packages");
    if (!sys_is_dir(builtin)) { free(builtin); builtin = own(TIDE_SOURCE_DIR "/packages"); }
    packages_set_builtin_folder(builtin); free(builtin); free(root); free(bin);
    editor *e = calloc(1, sizeof(*e)); if (!e) abort(); e->zoom = 48;
    if (!editor_open(&e->doc, path, project_root)) { fprintf(stderr, "tide open: %s\n", e->doc.message); editor_close(&e->doc); free(e); return 2; }
    if (check_only) {
        printf("project: %s\n", e->doc.project);
        for (int i = 0; i < e->doc.diagnostics.count; i++) puts(e->doc.diagnostics.items[i]);
        printf("%d prefab(s), %d error(s), %d warning(s)\n", e->doc.prefabs.count, e->doc.errors, e->doc.warnings);
        const int code = e->doc.errors ? 1 : 0; editor_close(&e->doc); free(e); return code;
    }
    e->recovery = e->doc.recovery != NULL;
    if (hidden.capture || hidden.frames || hidden.script) {
        if (!hidden.frames && !hidden.script) hidden.frames = 2;
        tide_platform_open_headless(&hidden);
    } else tide_platform_open(&(tide_window_desc){.title="Tide Scene Editor", .width=hidden.width, .height=hidden.height});
    tide_platform_run(frame, e);
}
