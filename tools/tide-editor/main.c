// A native authoring tool. It inspects compiler data, never a live match.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "document.h"
#include "asset_files.h"
#include "model.h"
#include "preview.h"
#include "play.h"
#include "startup.h"
#include "interaction.h"
#include "embed.h"
#include "packages.h"
#include "sys.h"
#include "tide/platform.h"
#include "tide/text.h"
#include "tide/time.h"

typedef enum action { NONE, SAVE, RELOAD, DISCARD, UNDO, REDO, SET, RESET, ADD, REMOVE, CHILD, DELETE_NODE, PREFAB, RECOVER, DROP_RECOVERY,
    CREATE_FILE, OPEN_FILE, RENAME_NODE, DUPLICATE_NODE, REPARENT_NODE, EXTRACT_NODE, NAMED_CHILD, PLAY, STOP, SET_STARTUP, PLACE_MODEL, INITIALIZE_ROOT } action;
typedef enum dialog_kind { DIALOG_NONE, DIALOG_NEW_SCENE, DIALOG_NEW_PREFAB, DIALOG_OPEN, DIALOG_RENAME, DIALOG_DUPLICATE, DIALOG_EXTRACT, DIALOG_CHILD } dialog_kind;
typedef enum inspector_kind { INSPECTOR_STRING, INSPECTOR_INT, INSPECTOR_FLOAT, INSPECTOR_VECTOR, INSPECTOR_EULER, INSPECTOR_EXPRESSION } inspector_kind;
typedef struct inspector_edit {
    char *file, *prefab, *path, *component, *field, *initial, *draft;
    uint64_t revision;
    uint32_t id;
    tide_rect rect;
    inspector_kind kind;
    int count, part;
    int32_t integer;
    float numbers[4];
    struct inspector_edit *next;
} inspector_edit;
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
    action following;
    int argument;
    char *pending_name;
    bool models;
    int model_scroll;
    char *model_asset, *model_key;
    editor_model model;
    uint64_t model_revision;
    uint64_t asset_stamp;
    bool model_lighting;
    float model_yaw, model_pitch, model_zoom;
    editor_model *scene_models;
    int scene_model_count;
    uint64_t scene_model_revision;
    editor_preview preview;
    float frame_seconds;
    bool bounded_preview;
    bool embedded;
    uint64_t embedded_time;
    int tool; // 0 select, 1 move, 2 rotate, 3 scale.
    editor_play play;
    char *workspace, *pending_path, *dialog_name, *dialog_path, *search, *root_name;
    dialog_kind dialog;
    bool dialog_open, value_picker;
    bool folded[1024];
    int drag_node;
    tide_float2 drag_origin;
    bool tree_dragging;
    uint32_t text_selection;
    inspector_edit *edit, *edit_first, *edit_last;
    uint64_t inspector_folded[128];
    int inspector_folded_count;
    int menu, project_tab, project_filter, project_scroll;
    bool preview_failure_seen;
    char *hierarchy_search, *project_search;
    struct {
        bool active;
        int node, axis, tool;
        uint64_t revision;
        tide_float2 start, center;
        tide_float3 position, scale, current_position, current_scale, direction, a, b;
        tide_quaternion rotation, current_rotation;
        float angle;
    } drag;
} editor;

static editor *closing_editor;
static void close_preview(void) { if (closing_editor) { editor_play_close(&closing_editor->play); editor_preview_close(&closing_editor->preview, &closing_editor->draw); } }

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
static const tide_color dim = {0.62f, 0.62f, 0.62f, 1}, ink = {0.85f, 0.85f, 0.85f, 1};

static bool contains(tide_rect r, tide_float2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
static void panel(editor *e, tide_rect r, tide_color color)
{
    tide_draw_rect(&e->draw, tide_f2(r.x + r.width / 2, r.y + r.height / 2), tide_f2(r.width, r.height), color);
}
static void label(editor *e, float x, float y, const char *s, tide_color color) { tide_draw_text(&e->draw, s, tide_f2(x, y), 14, color); }
static bool button(editor *e, uint32_t id, tide_rect r, const char *s) { return tide_gui_button(&e->gui, id, r, s); }
static bool tab(editor *e, uint32_t id, tide_rect r, const char *s, bool active)
{
    const int theme=tide_gui_begin_theme(&e->gui,(tide_gui_theme){.text=ink,.control=active ? rgb(.25f,.25f,.25f) : rgb(.18f,.18f,.18f),.hot=rgb(.3f,.3f,.3f),.pressed=rgb(.24f,.48f,.72f),.accent=rgb(.24f,.48f,.72f),.radius=0});
    const bool clicked=button(e,id,r,s);
    tide_gui_close(&e->gui,theme);
    if(active) panel(e,(tide_rect){r.x,r.y+r.height-2,r.width,2},rgb(.24f,.48f,.72f));
    return clicked;
}
static bool list_item(editor *e, uint32_t id, tide_rect r, const char *s, bool active, tide_color color)
{
    const tide_control control=tide_gui_control(&e->gui,id,r);
    const tide_color background=active ? rgb(.24f,.39f,.56f) : control.hovered ? rgb(.28f,.28f,.28f) : rgb(.22f,.22f,.22f);
    tide_draw_rect(&e->gui.list,tide_f2(r.x+r.width/2,r.y+r.height/2),tide_f2(r.width,r.height),background);
    const uint32_t clip=tide_draw_push_clip(&e->gui.list,r);
    tide_draw_text(&e->gui.list,s,tide_f2(r.x+5,r.y+(r.height-14)/2),14,color);
    tide_draw_pop(&e->gui.list,clip);
    return control.clicked;
}
static void queue(editor *e, action a) { if (e->pending == NONE) e->pending = a; }
static void queue_name(editor *e, action a, str name)
{
    if (e->pending != NONE) return;
    e->pending_name = own(str_to_cstr(name)); e->pending = a;
}
static void inspector_cancel(editor *e);
static void clear_field(editor *e)
{
    inspector_cancel(e);
    free(e->component); free(e->field); free(e->value); e->component = e->field = e->value = NULL;
    e->value_selected = false;
}

static bool text_field(editor *e, uint32_t id, tide_rect r, const char *label_text, char **value, bool *entered)
{
    if (!*value) *value = own("");
    const bool ctrl = e->devices.keyboard.leftCtrl.pressed || e->devices.keyboard.rightCtrl.pressed;
    if (e->gui.editing == id && ctrl && e->devices.keyboard.a.down) e->text_selection = id;
    if (e->gui.editing == id && e->text_selection == id && (e->gui.text.count || e->devices.keyboard.backspace.down)) {
        replace(value, ""); e->gui.edit_len = 0; e->gui.edit[0] = '\0'; e->text_selection = 0;
    }
    tide_str text = tide_str_from_cstr(*value);
    bool done;
    const bool changed = tide_gui_text_field_entered(&e->gui,id,r,label_text,(tide_textref){.local=&text},&done);
    if (changed) replace(value,tide_str_c(text));
    if (entered) *entered = done;
    return changed;
}
static void search_field(editor *e, uint32_t id, tide_rect r, char **value)
{
    text_field(e,id,r,"",value,NULL);
    if(!(*value)[0] && e->gui.editing!=id) tide_draw_text(&e->gui.list,"Search",tide_f2(r.x+7,r.y+(r.height-14)/2),14,dim);
}

static void open_dialog(editor *e, dialog_kind kind, const char *name, const char *path)
{
    e->dialog = kind; e->dialog_open = true;
    replace(&e->dialog_name,name ? name : ""); replace(&e->dialog_path,path ? path : "");
    e->drag.active = e->tree_dragging = false;
}

static bool layout_text_field(editor *e, uint32_t id, const char *label_text, char **value, bool *entered)
{
    if (!*value) *value=own("");
    const bool ctrl=e->devices.keyboard.leftCtrl.pressed || e->devices.keyboard.rightCtrl.pressed;
    if(e->gui.editing==id && ctrl && e->devices.keyboard.a.down) e->text_selection=id;
    if(e->gui.editing==id && e->text_selection==id && (e->gui.text.count || e->devices.keyboard.backspace.down)) {
        replace(value,""); e->gui.edit_len=0; e->gui.edit[0]='\0'; e->text_selection=0;
    }
    tide_str text=tide_str_from_cstr(*value); bool done;
    const bool changed=tide_gui_layout_text_field_entered(&e->gui,id,label_text,(tide_textref){.local=&text},&done);
    if(changed) replace(value,tide_str_c(text));
    if(entered) *entered=done;
    return changed;
}

static int scroll_rows(float value, int factor)
{
    return isfinite(value) ? (int)tide_clamp_f(value * (float)factor, -100, 100) : 0;
}

// Preview understands literal TRS, constants and basic float arithmetic. An
// unsupported transform is reported and its subtree omitted, never shown at
// a guessed position. This fallback does not execute game code; compiled
// opted-in views use their own world and rendering path instead.
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
    if (v->kind == E_CALL && (str_eq_c(v->name, "float2") || str_eq_c(v->name, "float3") || str_eq_c(v->name, "float4") || str_eq_c(v->name, "quaternion"))) {
        if (v->args.count == 1 && count != 4) {
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

static bool local_trs(const editor_node *n, tide_float3 *position, tide_quaternion *rotation, tide_float3 *scaling)
{
    if (!n->effective) return false;
    expr *c = editor_component(n, str_from("Transform"));
    float pos[3] = {0}, rot[4] = {0, 0, 0, 1}, scale[3] = {1, 1, 1};
    if (c && c->type_decl) for (int i = 0; i < c->type_decl->fields.count; i++) {
        field *f = &c->type_decl->fields.items[i];
        float *out = str_eq_c(f->name, "localPosition") ? pos : str_eq_c(f->name, "localRotation") ? rot : scale;
        if (!vector(editor_value(c, f), out, out == rot ? 4 : 3, 0)) return false;
    }
    *position = tide_f3(pos[0],pos[1],pos[2]); *rotation = tide_q(rot[0],rot[1],rot[2],rot[3]); *scaling = tide_f3(scale[0],scale[1],scale[2]);
    return true;
}

static bool local_matrix(const editor_node *n, tide_float4x4 *m)
{
    tide_float3 p, s; tide_quaternion q;
    if (!local_trs(n,&p,&q,&s)) return false;
    *m = tide_trs_f4x4(p,q,s); return true;
}

static void asset_stamp_path(void *user, const char *path)
{
    uint64_t *stamp = user;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) *stamp = (*stamp ^ *p) * UINT64_C(1099511628211);
    *stamp = (*stamp ^ sys_file_stamp(path)) * UINT64_C(1099511628211);
}

static uint64_t asset_stamp(const editor_document *doc)
{
    if (!doc->prog) return 0;
    const arena_mark mark = arena_checkpoint();
    uint64_t stamp = UINT64_C(14695981039346656037);
    for (int a = 0; a < doc->prog->assets.count; a++) {
        const decl *asset = doc->prog->assets.items[a];
        if (asset->asset_kind != ASSET_MESH) continue;
        for (int p = 0; p < asset->asset_paths.count; p++)
            asset_watched(asset->unit->src->path, asset->asset_paths.items[p]->text, asset_stamp_path, &stamp);
        if (asset->asset) for (int m = 0; m < asset->asset->count; m++) {
            const asset_match *item = &asset->asset->items[m];
            asset_stamp_path(&stamp, item->abs);
            for (int s = 0; s < item->side_count; s++) asset_stamp_path(&stamp, item->sides[s]);
        }
    }
    arena_rewind(mark); return stamp;
}

static decl *selected_asset_type(editor *e)
{
    if (!e->component || e->selected < 0 || e->selected >= e->doc.nodes.count) return NULL;
    expr *comp = editor_component(&e->doc.nodes.items[e->selected], str_from(e->component));
    if (!comp || !comp->type_decl) return NULL;
    for (int i = 0; i < comp->type_decl->fields.count; i++) {
        field *f = &comp->type_decl->fields.items[i];
        if (str_eq_c(f->name, e->field) && f->type.kind == TY_STRUCT) return f->type.decl->asset_ref_of;
    }
    return NULL;
}

static void models(editor *e, tide_rect r, tide_float2 size)
{
    const float list_width = fminf(230, r.width * .4f);
    const tide_rect rows = {r.x + 8, r.y + 58, list_width - 16, r.height - 120};
    if (!e->menu && contains(rows, e->gui.mouse)) e->model_scroll -= scroll_rows(e->devices.mouse.poll_scroll.y, 3);
    if (e->model_scroll < 0) e->model_scroll = 0;
    const int visible = (int)(rows.height / 30);
    int row = 0;
    const asset_match *selected = NULL;
    const decl *selected_decl = NULL;
    tide_draw_set_clip(&e->draw, rows); tide_draw_set_clip(&e->gui.list, rows);
    for (int a = 0; a < e->doc.prog->assets.count; a++) {
        const decl *asset = e->doc.prog->assets.items[a];
        if (asset->asset_kind != ASSET_MESH || !asset->asset) continue;
        const char *name = str_to_cstr(asset->qualified);
        for (int m = 0; m < asset->asset->count; m++, row++) {
            const asset_match *item = &asset->asset->items[m];
            const bool active = e->model_asset && strcmp(e->model_asset, name) == 0 && strcmp(e->model_key, item->key) == 0;
            if (active) { selected = item; selected_decl = asset; }
            if (row < e->model_scroll || row >= e->model_scroll + visible) continue;
            sb title = {0};
            sb_printf(&title, "%s%s%s", active ? "> " : "", name, asset->asset_many ? " / " : "");
            if (asset->asset_many) sb_put(&title, path_base(item->key));
            if (button(e, 400000u + (uint32_t)row, (tide_rect){rows.x,rows.y+(float)(row-e->model_scroll)*30,rows.width,26}, title.data)) {
                replace(&e->model_asset, name); replace(&e->model_key, item->key);
                e->model_revision = UINT64_MAX; selected = item; selected_decl = asset;
                e->model_zoom = 1; e->model_pitch = .3f; e->model_yaw = -.6f;
            }
        }
    }
    if (e->model_scroll > (row > visible ? row - visible : 0)) e->model_scroll = row > visible ? row - visible : 0;
    tide_draw_unset_clip(&e->draw); tide_draw_unset_clip(&e->gui.list);
    const decl *wanted = selected_asset_type(e);
    if (wanted && selected_decl && selected_decl->return_type.kind == TY_MESH && selected_decl->return_type.decl == wanted) {
        if (button(e, 77, (tide_rect){r.x+r.width-222,r.y+r.height-76,210,28}, "Use for selected field")) {
            replace(&e->value, str_to_cstr(selected_decl->qualified));
            queue(e, SET); e->models = false;
        }
    } else if (wanted) label(e, r.x+12, r.y+r.height-92, "Choose a matching singular mesh asset for the field.", dim);
    else if(selected_decl && !selected_decl->asset_many && selected_decl->return_type.kind==TY_MESH) {
        int offered=0;
        for(int c=0;c<e->doc.prog->components.count && offered<2;c++) {
            decl *component=e->doc.prog->components.items[c];
            if(component->is_scene || component->placement_of) continue;
            for(int f=0;f<component->fields.count && offered<2;f++) {
                field *item=&component->fields.items[f];
                if(item->hidden || item->is_local || item->type.kind!=TY_STRUCT || !item->type.decl || item->type.decl->asset_ref_of!=selected_decl->return_type.decl) continue;
                sb title={0}; sb_printf(&title,"Place as " STR_FMT "." STR_FMT,STR_ARG(component->qualified),STR_ARG(item->name));
                const int disabled=tide_gui_begin_disabled(&e->gui,!editor_can_structure(&e->doc,e->selected));
                if(button(e,410000u+(uint32_t)c*1024u+(uint32_t)f,(tide_rect){r.x+12,r.y+r.height-76-(float)offered*32,r.width-24,28},title.data)) {
                    replace(&e->component,str_to_cstr(component->qualified)); replace(&e->field,str_to_cstr(item->name));
                    replace(&e->value,str_to_cstr(selected_decl->qualified)); queue(e,PLACE_MODEL);
                }
                tide_gui_close(&e->gui,disabled); offered++;
            }
        }
    }
    if (e->model_revision != e->doc.revision) {
        if (selected) editor_model_set(&e->model, &selected->mesh, e->model_lighting);
        else editor_model_free(&e->model);
        e->model_revision = e->doc.revision;
    }
    const tide_rect view = {r.x+list_width+4,r.y+58,r.width-list_width-12,r.height-142};
    if (button(e, 75, (tide_rect){view.x,r.y+28,100,24}, "Frame model")) e->model_zoom = 1;
    if (button(e, 76, (tide_rect){view.x+110,r.y+28,100,24}, e->model_lighting ? "Lit" : "Unlit")) {
        e->model_lighting = !e->model_lighting; e->model_revision = UINT64_MAX;
    }
    tide_draw_set_clip(&e->draw, view); tide_draw_set_clip(&e->gui.list, view);
    panel(e, view, rgb(.16f,.16f,.16f));
    if (!row) label(e, rows.x, rows.y, "No static Mesh assets.", dim);
    if (e->model.mesh.vertex_count) {
        if (!e->menu && contains(view, e->gui.mouse)) {
            e->model_zoom = tide_clamp_f(e->model_zoom * (1 - e->devices.mouse.poll_scroll.y * .1f), .08f, 20);
            if (e->devices.mouse.right.pressed) {
                e->model_yaw += e->devices.mouse.poll_delta.x * .008f;
                e->model_pitch = tide_clamp_f(e->model_pitch - e->devices.mouse.poll_delta.y * .008f, -1.5f, 1.5f);
            }
        }
        const float inverse = 1 / e->model.radius;
        const tide_float4x4 placed = tide_mul_f4x4(tide_trs_f4x4(tide_f3(0,0,0), tide_euler_q(tide_f3(e->model_pitch,e->model_yaw,0)), tide_f3_splat(inverse)), tide_translate_f4x4(tide_neg_f3(e->model.center)));
        const float height = 3.2f*e->model_zoom*fmaxf(1, view.height/view.width);
        tide_float4x4 projection = tide_ortho_f4x4(height*view.width/view.height, height, .1f, 20);
        // Matrix cameras include their aspect. Map their clip coordinates into
        // this rectangle of the window without changing its other panels.
        projection.c0.x *= view.width / size.x;
        projection.c1.y *= view.height / size.y;
        projection.c3.x = 2*(view.x+view.width/2)/size.x-1;
        projection.c3.y = 1-2*(view.y+view.height/2)/size.y;
        const uint32_t camera = tide_draw_push_camera_matrices(&e->draw, tide_translate_f4x4(tide_f3(0,0,-5)), projection);
        tide_draw_mesh_3d(&e->draw, &e->model.mesh, placed, NULL, TIDE_FILTER_POINT);
        tide_draw_pop(&e->draw, camera);
        char info[120]; snprintf(info, sizeof info, "%u vertices / %u triangles", e->model.mesh.vertex_count, e->model.mesh.index_count / 3);
        label(e, view.x+10,view.y+10, info, dim);
    } else label(e, view.x+10,view.y+14, selected ? "Geometry could not be previewed." : "Select a model asset.", dim);
    tide_draw_unset_clip(&e->draw); tide_draw_unset_clip(&e->gui.list);
    tide_draw_set_clip(&e->draw, r);
    label(e, r.x+12,r.y+r.height-40, "Right drag: orbit  |  Wheel: zoom", dim);
    label(e, r.x+12,r.y+r.height-18, "Static Mesh assets only; SkinnedMesh previews are not available yet.", dim);
    tide_draw_unset_clip(&e->draw);
}

static tide_float2 project(editor *e, tide_rect r, tide_float3 p)
{
    return editor_project((editor_camera){tide_f2(r.x+r.width/2+e->pan.x,r.y+r.height/2+e->pan.y),e->zoom,e->xy},p);
}

#include "interaction_ui.inc"

static void viewport(editor *e, tide_rect r, tide_float2 size)
{
    if (e->scene_model_revision != e->doc.revision) {
        for (int i = 0; i < e->scene_model_count; i++) editor_model_free(&e->scene_models[i]);
        free(e->scene_models);
        e->scene_model_count = e->doc.prog->assets.count;
        e->scene_models = calloc((size_t)e->scene_model_count, sizeof(*e->scene_models));
        if (e->scene_model_count && !e->scene_models) abort();
        e->scene_model_revision = e->doc.revision;
    }
    tide_draw_set_clip(&e->draw, r);
    tide_draw_set_clip(&e->gui.list, r);
    if (button(e, 70, (tide_rect){r.x + 14, r.y + 28, 82, 24}, e->xy ? "2D / XY" : "Iso")) e->xy = !e->xy;
    if (button(e, 71, (tide_rect){r.x + 106, r.y + 28, 82, 24}, "Frame")) e->pan = tide_f2(0, 0);
    if (e->preview.enabled && !e->preview.builder && button(e, 73, (tide_rect){r.x+198,r.y+28,90,24}, e->preview.local ? "Local" : "Match")) {
        const bool local = !e->preview.local;
        editor_preview_close(&e->preview, &e->draw);
        e->preview.enabled = true; e->preview.local = local;
    }
    const tide_rect content = {r.x,r.y+56,r.width,r.height-80};
    panel(e,content,rgb(.16f,.16f,.16f));
    tide_draw_set_clip(&e->draw,content);
    if (!e->menu && contains(content, e->gui.mouse) && !e->drag.active) {
        e->zoom *= 1 + e->devices.mouse.poll_scroll.y * 0.1f;
        e->zoom = tide_clamp_f(e->zoom, 8, 256);
        if (e->devices.mouse.middle.pressed) {
            e->pan.x += e->devices.mouse.poll_delta.x; e->pan.y -= e->devices.mouse.poll_delta.y;
        }
    }
    for (int i = -20; i <= 20; i++) {
        const tide_color c = i == 0 ? rgb(.32f,.32f,.32f) : rgb(.215f,.215f,.215f);
        if (e->xy) {
            tide_draw_line(&e->draw, project(e, r, tide_f3((float)i, -20, 0)), project(e, r, tide_f3((float)i, 20, 0)), c);
            tide_draw_line(&e->draw, project(e, r, tide_f3(-20, (float)i, 0)), project(e, r, tide_f3(20, (float)i, 0)), c);
        } else {
            tide_draw_line(&e->draw, project(e, r, tide_f3((float)i, 0, -20)), project(e, r, tide_f3((float)i, 0, 20)), c);
            tide_draw_line(&e->draw, project(e, r, tide_f3(-20, 0, (float)i)), project(e, r, tide_f3(20, 0, (float)i)), c);
        }
    }
    const int count = e->doc.nodes.count;
    tide_float4x4 preview_projection = tide_identity_f4x4();
    preview_projection.c0.x = 2*e->zoom/r.width; preview_projection.c1.y = 2*e->zoom/r.height;
    // SetCameraMatrices turns view-space z for the renderer's convention;
    // compensate here so the wire markers and compiled geometry coincide.
    preview_projection.c2.x = e->xy ? 0 : -.6f*preview_projection.c0.x;
    preview_projection.c2.y = e->xy ? 0 : .35f*preview_projection.c1.y;
    preview_projection.c0.z = -.0001f; preview_projection.c1.z = -.0001f; preview_projection.c2.z = -.0001f;
    preview_projection.c3 = tide_f4(2*e->pan.x/r.width,-2*e->pan.y/r.height,.5f,1);
    const bool compiled_preview = editor_preview_draw(&e->preview, &e->draw, e->frame_seconds, r, tide_identity_f4x4(), preview_projection, tide_platform_measure_text);
    // Appended view commands can finish with their own clip state. Authoring
    // markers and labels still belong inside the scene panel.
    tide_draw_set_clip(&e->draw,content);
    tide_float4x4 *matrices = arena_alloc((size_t)count * sizeof(*matrices));
    bool *valid = arena_alloc((size_t)count * sizeof(*valid));
    int omitted = 0, omitted_models = 0;
    int picked = -1;
    double picked_depth = INFINITY;
    const editor_camera picking_camera = {tide_f2(r.x+r.width/2+e->pan.x,r.y+r.height/2+e->pan.y),e->zoom,e->xy};
    for (int i = 0; i < count; i++) {
        editor_node *n = &e->doc.nodes.items[i];
        const int parent = n->parent;
        valid[i] = local_matrix(n, &matrices[i]) && (parent < 0 || (parent < i && valid[parent]));
        if (!valid[i]) { omitted++; continue; }
        if (e->drag.active && e->drag.node == i) matrices[i] = tide_trs_f4x4(e->drag.current_position,e->drag.current_rotation,e->drag.current_scale);
        if (parent >= 0) matrices[i] = tide_mul_f4x4(matrices[parent], matrices[i]);
        if (n->effective) for (int c = 0; c < n->effective->args.count; c++) {
            const expr *comp = n->effective->args.items[c];
            if (!comp->type_decl) continue;
            for (int f = 0; f < comp->type_decl->fields.count; f++) {
                const field *field = &comp->type_decl->fields.items[f];
                if (field->hidden || field->is_local || field->type.kind != TY_STRUCT || !field->type.decl->asset_ref_of) continue;
                const expr *value = editor_value(comp, field);
                int depth = 0;
                while (value && value->bind == BIND_CONST && depth++ < 64) value = value->constant->value;
                if (!value || value->kind == E_DEFAULT) continue;
                if (value->bind != BIND_ASSET || !value->asset_reference) { omitted_models++; continue; }
                bool drawn = false;
                for (int a = 0; a < e->scene_model_count; a++) {
                    const decl *asset = e->doc.prog->assets.items[a];
                    if (asset != value->constant || !asset->asset || asset->asset->count != 1) continue;
                    editor_model *model = &e->scene_models[a];
                    if (!model->mesh.vertex_count) editor_model_set(model, &asset->asset->items[0].mesh, true);
                    if (!model->mesh.vertex_count) continue;
                    if (e->gui.mouse_pressed && !e->drag.active && contains(content,e->gui.mouse)) {
                        editor_camera relative = picking_camera; relative.origin = tide_f2(0,0);
                        const tide_float2 center = editor_project(picking_camera,tide_transform_f4x4(matrices[i],model->center));
                        const tide_float2 a = editor_project(relative,direction(matrices[i],tide_f3(1,0,0)));
                        const tide_float2 b = editor_project(relative,direction(matrices[i],tide_f3(0,1,0)));
                        const tide_float2 c = editor_project(relative,direction(matrices[i],tide_f3(0,0,1)));
                        // Conservative sphere projection rejects off-pointer models
                        // before reading their triangles. Picking runs only on press.
                        const double width = (double)model->radius*(fabs(a.x)+fabs(b.x)+fabs(c.x));
                        const double height = (double)model->radius*(fabs(a.y)+fabs(b.y)+fabs(c.y));
                        if (fabs((double)e->gui.mouse.x-center.x) <= width && fabs((double)e->gui.mouse.y-center.y) <= height &&
                            editor_pick_mesh(&model->mesh,matrices[i],picking_camera,e->gui.mouse,&picked_depth)) picked = i;
                    }
                    tide_float4x4 projection = tide_identity_f4x4();
                    projection.c0.x = 2*e->zoom/size.x;
                    projection.c1.y = 2*e->zoom/size.y;
                    // Compensate the matrix camera's view-space z turn, as the
                    // compiled preview does, so surfaces and picking coincide.
                    projection.c2.x = e->xy ? 0 : -.6f*projection.c0.x;
                    projection.c2.y = e->xy ? 0 : .35f*projection.c1.y;
                    projection.c0.z = -.0001f; projection.c1.z = -.0001f; projection.c2.z = -.0001f;
                    projection.c3 = tide_f4(2*(r.x+r.width/2+e->pan.x)/size.x-1,1-2*(r.y+r.height/2+e->pan.y)/size.y,.5f,1);
                    if (!compiled_preview) {
                        const uint32_t camera = tide_draw_push_camera_matrices(&e->draw, tide_identity_f4x4(), projection);
                        // Authoring proxies/picking are double-sided; a flat
                        // imported mesh stays inspectable from either projection.
                        tide_draw_push_cull(&e->draw,TIDE_CULL_NONE);
                        tide_draw_mesh_3d(&e->draw, &model->mesh, matrices[i], NULL, TIDE_FILTER_POINT);
                        tide_draw_pop(&e->draw, camera);
                    }
                    drawn = true;
                }
                if (!drawn) omitted_models++;
            }
        }
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
        if (contains(content, screen)) {
            if (picked < 0 && fabsf(screen.x-e->gui.mouse.x) <= 10 && fabsf(screen.y-e->gui.mouse.y) <= 10) picked = i;
            tide_draw_circle(&e->draw, screen, i == e->selected ? 5 : 3, color);
            label(e, screen.x + 10, screen.y - 8, n->path.len ? str_to_cstr(n->path) : "Root", color);
        }
        if (i == e->selected && !e->tool) {
            static const tide_float3 axes[] = {{1,0,0}, {0,1,0}, {0,0,1}};
            static const tide_color colors[] = {{.95f,.35f,.35f,1}, {.4f,.87f,.4f,1}, {.4f,.6f,.95f,1}};
            for (int axis = 0; axis < 3; axis++) tide_draw_thick_line(&e->draw, screen, project(e, r, tide_transform_f4x4(matrices[i], axes[axis])), 2, colors[axis]);
        }
    }
    const bool gizmo_used = gizmo(e,r,matrices,valid);
    if (!gizmo_used && !e->menu && picked >= 0 && e->gui.mouse_pressed && !e->picker && !e->discard && !e->recovery && !e->dialog_open && !e->value_picker && e->pending == NONE) {
        e->selected = picked; e->inspector_scroll = 0; clear_field(e);
    }
    tide_draw_unset_clip(&e->draw);
    panel(e, (tide_rect){r.x,r.y+r.height-24,r.width,24}, rgb(.18f,.18f,.18f));
    char message[180];
    if (compiled_preview && omitted) snprintf(message, sizeof message, "%d marker(s) need constant transforms; views can still render them.", omitted);
    else if (omitted || omitted_models) snprintf(message, sizeof message, "%d transform(s), %d model field(s) not previewed. Inspect expressions/diagnostics.", omitted, omitted_models);
    else snprintf(message, sizeof message, "%s | Q/W/E/R tools | Shift snap | Esc cancel", compiled_preview ? "Views (authored picking)" : "Static models");
    label(e, r.x + 12, r.y + r.height - 19, message, omitted || omitted_models ? rgb(.95f,.65f,.3f) : dim);
    if (e->preview.enabled && r.width>650) label(e, r.x+300, r.y+33, e->preview.builder ? "Building views..." : e->preview.revision != e->doc.revision ? "Preview stale / build needed" : e->preview.api && !e->preview.api->views ? "No opted-in views" : "Game shaders / authored state", dim);
    tide_draw_unset_clip(&e->draw); tide_draw_unset_clip(&e->gui.list);
}

#include "hierarchy_ui.inc"

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

#include "inspector_ui.inc"

static void picker(editor *e, tide_float2 screen)
{
    const int modal = tide_gui_begin_modal(&e->gui, 100, TIDE_ANCHOR_MIDDLE_CENTER, &e->picker);
    if (modal < 0) return;
    tide_gui_layout_label(&e->gui, e->nested_picker ? "Nest a prefab under the selected entity" : "Add a component from this project");
    if(layout_text_field(e,109,"Search",&e->search,NULL)) e->argument=0;
    int offered = 0;
    // Bound the modal to fit; paging follows the same scroll wheel as panels.
    if (e->devices.mouse.poll_scroll.y) e->argument -= scroll_rows(e->devices.mouse.poll_scroll.y, 3);
    if (e->argument < 0) e->argument = 0;
    const int visible = (int)((screen.y - 220) / 30);
    for (int i = 0; i < e->doc.prog->decls.count; i++) {
        decl *d = e->doc.prog->decls.items[i];
        if (e->nested_picker ? d->kind != DECL_PREFAB : (d->kind != DECL_COMPONENT || d->is_scene || d->placement_of)) continue;
        if (e->nested_picker && e->doc.prefabs.count && d == e->doc.prefabs.items[e->doc.active]) continue;
        if(e->search && e->search[0] && !strstr(str_to_cstr(d->qualified),e->search)) continue;
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

#include "workspace_ui.inc"

static int frame(void *user, float seconds)
{
    editor *e = user;
    editor_play_poll(&e->play,0);
    if (e->embedded) {
        const int result = editor_embed_input();
        if (result != -1) return result;
        const uint64_t now = tide_time_now_ns();
        seconds = e->embedded_time ? tide_clamp_f((float)((double)(now-e->embedded_time)/1e9),0,1) : 1.0f/60;
        e->embedded_time = now;
    }
    e->frame_seconds = seconds;
    char *selected_path = e->selected >= 0 && e->selected < e->doc.nodes.count ? own_slice(e->doc.nodes.items[e->selected].path) : NULL;
    const uint64_t revision = e->doc.revision;
    e->poll += seconds;
    if (e->doc.path && e->poll >= .5f) {
        e->poll = 0; editor_poll(&e->doc);
        const uint64_t current = asset_stamp(&e->doc);
        if (current != e->asset_stamp && e->doc.revision == revision) editor_refresh(&e->doc);
        e->asset_stamp = asset_stamp(&e->doc);
    }
    if (e->doc.revision != revision) {
        e->drag.active = false;
        e->gui.editing=e->gui.focus=0; e->text_selection=0;
        e->selected = 0;
        for (int i = 0; selected_path && i < e->doc.nodes.count; i++) if (str_eq_c(e->doc.nodes.items[i].path, selected_path)) e->selected = i;
        clear_field(e);
    }
    free(selected_path);
    if (e->selected >= e->doc.nodes.count) e->selected = 0;
    // Polling can replace the analysis/revision too: bounded output must build
    // that current document, rather than the state from before the poll.
    if(e->doc.path) editor_preview_update(&e->preview, &e->doc, &e->draw, 0);
    if (e->bounded_preview && e->preview.enabled) {
        while (e->preview.builder) editor_preview_update(&e->preview, &e->doc, &e->draw, 50);
        if (!e->preview.api || e->preview.revision != e->doc.revision) {
            fprintf(stderr, "tide editor: %s\n", e->preview.message);
            if (e->doc.errors) for (int i = 0; i < e->doc.diagnostics.count; i++) fprintf(stderr, "%s\n", e->doc.diagnostics.items[i]);
            return 1;
        }
    }
    tide_platform_poll(&e->devices);
    const uint32_t scratch = tide_scratch_mark();
    const arena_mark mark = arena_checkpoint();
    const tide_float2 size = tide_platform_screen_size();
    tide_draw_reset(&e->draw); tide_draw_set_screen(&e->draw);
    tide_draw_clear(&e->draw, rgb(.13f,.13f,.13f));
    if (size.x < 1100 || size.y < 650) {
        label(e, 24, 30, "Resize the scene editor to at least 1100 x 650 to show all panels.", ink);
        tide_platform_draw(&e->draw); tide_devices_consume(&e->devices);
        arena_rewind(mark); tide_scratch_reset(scratch); return TIDE_KEEP_RUNNING;
    }
    tide_gui_begin(&e->gui, &e->devices, size, tide_platform_measure_text);
    inspector_prepare(e);
    tide_gui_theme theme = tide_gui_theme_default();
    theme.text=ink; theme.panel=rgb(.22f,.22f,.22f); theme.control=rgb(.28f,.28f,.28f);
    theme.hot=rgb(.34f,.34f,.34f); theme.pressed=rgb(.24f,.39f,.56f); theme.accent=rgb(.25f,.5f,.75f);
    theme.field=rgb(.15f,.15f,.15f); theme.field_hot=rgb(.19f,.19f,.19f); theme.radius=2;
    const int themed=tide_gui_begin_theme(&e->gui,theme);
    panel(e,(tide_rect){0,0,size.x,22},rgb(.18f,.18f,.18f));
    if(tab(e,15,(tide_rect){4,0,48,22},"File",e->menu==1)) e->menu=e->menu==1 ? 0 : 1;
    if(tab(e,16,(tide_rect){54,0,48,22},"Edit",e->menu==2)) e->menu=e->menu==2 ? 0 : 2;
    if(tab(e,17,(tide_rect){104,0,112,22},"GameObject",e->menu==5)) e->menu=e->menu==5 ? 0 : 5;
    const bool has_document=e->doc.path!=NULL;
    const bool has_root=e->doc.prefabs.count>0;
    sb title={0}; sb_printf(&title,"%s%s",has_document ? path_base(e->doc.path) : "Tide Scene Editor",has_document && editor_dirty(&e->doc) ? " *" : "");
    tide_draw_set_clip(&e->draw,(tide_rect){230,0,size.x-240,22});
    label(e,230,4,title.data,dim); tide_draw_unset_clip(&e->draw);
    const int workspace_disabled=tide_gui_begin_disabled(&e->gui,e->menu!=0);
    panel(e,(tide_rect){0,24,size.x,28},rgb(.235f,.235f,.235f));
    label(e,18,31,"TIDE",ink);
    if(button(e,8,(tide_rect){90,26,64,24},"New")) open_dialog(e,DIALOG_NEW_SCENE,"Level",NULL);
    if(button(e,9,(tide_rect){162,26,64,24},"Open")) open_dialog(e,DIALOG_OPEN,NULL,NULL);
    const int toolbar_disabled=tide_gui_begin_disabled(&e->gui,!has_document);
    int root_disabled=tide_gui_begin_disabled(&e->gui,!has_root && (!has_document || !editor_dirty(&e->doc)));
    if(button(e,1,(tide_rect){234,26,64,24},"Save")) queue(e,SAVE);
    tide_gui_close(&e->gui,root_disabled);
    if(button(e,2,(tide_rect){306,26,64,24},"Undo")) queue(e,UNDO);
    if(button(e,3,(tide_rect){378,26,64,24},"Redo")) queue(e,REDO);
    static const char *tool_names[]={"Q","W","E","R"};
    for(int t=0;t<4;t++) if(tab(e,700u+(uint32_t)t,(tide_rect){450+(float)t*28,26,26,24},tool_names[t],e->tool==t)) { e->tool=t; e->drag.active=false; }
    root_disabled=tide_gui_begin_disabled(&e->gui,!has_root && !playing(e));
    if(tab(e,10,(tide_rect){fmaxf(620,size.x/2)-40,26,80,24},playing(e) ? "Stop" : "Play",playing(e))) queue(e,playing(e) ? STOP : PLAY);
    tide_gui_close(&e->gui,root_disabled);
    root_disabled=tide_gui_begin_disabled(&e->gui,!has_root);
    if(button(e,11,(tide_rect){size.x-130,26,120,24},"Set startup")) queue(e,SET_STARTUP);
    tide_gui_close(&e->gui,root_disabled);
    tide_gui_close(&e->gui,toolbar_disabled);
    if(e->models || e->source) e->drag.active=false;
    // Shortcuts read presses before GUI consumption, except while typing/modal.
    const bool ctrl = e->devices.keyboard.leftCtrl.pressed || e->devices.keyboard.rightCtrl.pressed;
    const bool modal_open=e->menu || e->picker || e->discard || e->recovery || e->dialog_open || e->value_picker;
    if (ctrl && !tide_gui_typing(&e->gui) && !modal_open) {
        if(e->devices.keyboard.n.down) open_dialog(e,DIALOG_NEW_SCENE,"Level",NULL);
        if(e->devices.keyboard.o.down) open_dialog(e,DIALOG_OPEN,NULL,NULL);
    }
    if (has_document && ctrl && !tide_gui_typing(&e->gui) && !modal_open) {
        if ((has_root || editor_dirty(&e->doc)) && e->devices.keyboard.s.down) queue(e, SAVE);
        if (e->devices.keyboard.z.down) queue(e, e->devices.keyboard.leftShift.pressed ? REDO : UNDO);
        if (e->devices.keyboard.y.down) queue(e, REDO);
    }
    if (has_document && !ctrl && !tide_gui_typing(&e->gui) && !modal_open) {
        if((has_root || playing(e)) && e->devices.keyboard.f5.down) queue(e,playing(e) ? STOP : PLAY);
        if(e->selected>0 && editor_can_structure(&e->doc,e->selected)) {
            if(e->devices.keyboard.f2.down) open_dialog(e,DIALOG_RENAME,node_name(&e->doc.nodes.items[e->selected]),NULL);
            if(e->devices.keyboard.delete.down) queue(e,DELETE_NODE);
        }
        int tool = -1;
        if (e->devices.keyboard.q.down) tool = 0;
        if (e->devices.keyboard.w.down) tool = 1;
        if (e->devices.keyboard.e.down) tool = 2;
        if (e->devices.keyboard.r.down) tool = 3;
        if (tool >= 0) { e->tool = tool; e->drag.active = false; }
    }
    const float height=size.y-230,right=size.x-390;
    panel(e,(tide_rect){0,56,250,height},rgb(.22f,.22f,.22f));
    panel(e,(tide_rect){right,56,390,size.y-78},rgb(.22f,.22f,.22f));
    panel(e,(tide_rect){254,56,right-258,height},rgb(.22f,.22f,.22f));
    if(has_document && !has_root) {
        empty_document(e,(tide_rect){0,56,right,height});
    } else if(has_document) {
        tree(e,(tide_rect){0,56,250,height});
        const tide_rect scene={254,56,right-258,height};
        if(e->models && !e->source) models(e,scene,size);
        else if(!e->source) viewport(e,scene,size);
        else {
            const tide_rect r={266,90,right-278,height-44};
            if(!e->menu && contains(r,e->gui.mouse)) e->source_scroll-=scroll_rows(e->devices.mouse.poll_scroll.y,3);
            if(e->source_scroll<0) e->source_scroll=0;
            const char *p=e->doc.text; int line=0;
            tide_draw_set_clip(&e->draw,r);
            while(*p) {
                const char *end=strchr(p,'\n'); if(!end) end=p+strlen(p);
                if(line>=e->source_scroll && line<e->source_scroll+(int)(r.height/20)) {
                    sb text={0}; sb_printf(&text,"%3d  ",line+1); sb_putn(&text,p,(size_t)(end-p));
                    label(e,r.x,r.y+(float)(line-e->source_scroll)*20,text.data,ink);
                }
                line++; p=*end ? end+1 : end;
            }
            tide_draw_unset_clip(&e->draw);
        }
        // Tabs are drawn after the viewport so scene content stays below them.
        panel(e,(tide_rect){scene.x,scene.y,scene.width,26},rgb(.18f,.18f,.18f));
        if(tab(e,5,(tide_rect){scene.x,scene.y,82,26},"Scene",!e->source && !e->models)) e->source=e->models=false;
        if(tab(e,6,(tide_rect){scene.x+82,scene.y,82,26},"Source",e->source)) { e->source=true; e->models=false; }
        if(tab(e,18,(tide_rect){scene.x+164,scene.y,82,26},"Models",e->models)) { e->models=true; e->source=false; }
        if(!e->models && !e->source && button(e,7,(tide_rect){scene.x+scene.width-112,scene.y+28,106,24},e->preview.enabled ? "Views: on" : "Views: off")) e->preview.enabled=!e->preview.enabled;
        inspector(e,(tide_rect){right,56,390,size.y-78});
    } else {
        label(e,70,130,"Create a scene, add objects, then press Play.",ink);
        label(e,70,164,e->workspace,dim);
        if(button(e,12,(tide_rect){70,218,200,38},"New Scene")) open_dialog(e,DIALOG_NEW_SCENE,"Level",NULL);
        if(button(e,13,(tide_rect){286,218,200,38},"New Prefab")) open_dialog(e,DIALOG_NEW_PREFAB,"Object",NULL);
        if(button(e,14,(tide_rect){502,218,200,38},"Open file")) open_dialog(e,DIALOG_OPEN,NULL,NULL);
    }
    const bool preview_failure=preview_failed(e);
    if(preview_failure && !e->preview_failure_seen) { e->project_tab=1; e->diagnostic_scroll=0; }
    e->preview_failure_seen=preview_failure;
    project_dock(e,(tide_rect){0,size.y-170,right,148});
    panel(e,(tide_rect){0,size.y-22,size.x,22},rgb(.16f,.16f,.16f));
    tide_draw_set_clip(&e->draw,(tide_rect){10,size.y-22,size.x-260,22});
    label(e,10,size.y-18,preview_failure ? e->preview.message : e->play.message[0] ? e->play.message : e->doc.message[0] ? e->doc.message : "Ctrl+N New   Ctrl+O Open   Ctrl+S Save   F5 Play / Stop",preview_failure ? rgb(.95f,.55f,.45f) : dim);
    tide_draw_unset_clip(&e->draw);
    char status[120]; snprintf(status,sizeof status,"%d errors  %d warnings%s",e->doc.errors,e->doc.warnings,e->doc.conflict ? "  FILE CONFLICT" : "");
    label(e,size.x-244,size.y-18,status,e->doc.errors || e->doc.conflict ? rgb(.95f,.55f,.45f) : dim);
    tide_gui_close(&e->gui,workspace_disabled);
    workspace_menu(e);
    if (e->picker) picker(e, size);
    if(e->value_picker) value_picker(e,size);
    if(e->dialog_open) file_dialog(e,size);
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
    const bool inspector_committed = inspector_apply(e);
    const int selected = e->selected;
    selected_path = selected >= 0 && selected < e->doc.nodes.count ? own_slice(e->doc.nodes.items[selected].path) : NULL;
    char *next_path=NULL;
    if(e->pending==RENAME_NODE || e->pending==DUPLICATE_NODE || e->pending==NAMED_CHILD || e->pending==REPARENT_NODE) {
        int parent=e->pending==NAMED_CHILD ? selected : e->pending==REPARENT_NODE ? e->argument : selected>0 && selected<e->doc.nodes.count ? e->doc.nodes.items[selected].parent : -1;
        if(parent>=0 && parent<e->doc.nodes.count) {
            const str prefix=e->doc.nodes.items[parent].path;
            const char *leaf=e->pending==REPARENT_NODE ? node_name(&e->doc.nodes.items[selected]) : e->pending_name;
            sb path={0}; sb_printf(&path,STR_FMT "%s%s",STR_ARG(prefix),prefix.len ? "." : "",leaf ? leaf : ""); next_path=own(path.data);
        }
    }
    switch (e->pending) {
    case SAVE: editor_save(&e->doc); break;
    case RELOAD: editor_reload(&e->doc, false); clear_field(e); break;
    case DISCARD: editor_reload(&e->doc, true); clear_field(e); break;
    case UNDO: editor_undo(&e->doc, false); clear_field(e); break;
    case REDO: editor_undo(&e->doc, true); clear_field(e); break;
    case SET: if (!inspector_committed && e->component) editor_set_field(&e->doc, selected, str_from(e->component), str_from(e->field), e->value); break;
    case RESET: if (e->component) editor_reset_field(&e->doc, selected, str_from(e->component), str_from(e->field)); clear_field(e); break;
    case ADD: editor_add_component(&e->doc, selected, str_from(e->pending_name)); break;
    case REMOVE: editor_remove_component(&e->doc, selected, str_from(e->pending_name)); clear_field(e); break;
    case CHILD: editor_add_child(&e->doc, selected, e->pending_name); break;
    case DELETE_NODE: editor_remove_node(&e->doc, selected); e->selected = 0; clear_field(e); break;
    case PREFAB: e->doc.active = e->argument; editor_refresh(&e->doc); e->selected = 0; clear_field(e); break;
    case RECOVER: editor_recover(&e->doc, true); clear_field(e); break;
    case DROP_RECOVERY: editor_recover(&e->doc, false); break;
    default: apply_workspace_action(e,selected); break;
    }
    if(e->following!=NONE) {
        const action next=e->following; e->following=NONE;
        if(next==SAVE) editor_save(&e->doc);
        else { e->pending=next; apply_workspace_action(e,e->selected); }
    }
    if (e->pending != NONE && e->pending != PREFAB && e->pending != DELETE_NODE && e->pending != CREATE_FILE && e->pending != OPEN_FILE) {
        bool found = false;
        for (int i=0;next_path && i<e->doc.nodes.count;i++) if(str_eq_c(e->doc.nodes.items[i].path,next_path)) { e->selected=i; found=true; break; }
        for (int i = 0; !found && selected_path && i < e->doc.nodes.count; i++) if (str_eq_c(e->doc.nodes.items[i].path, selected_path)) { e->selected = i; found = true; }
        if (!found) { e->selected = 0; clear_field(e); }
    }
    free(selected_path);
    free(next_path);
    free(e->pending_name); e->pending_name = NULL; e->pending = NONE;
    free(e->pending_path); e->pending_path=NULL;
    if (e->embedded && !editor_embed_frame(e->doc.path && editor_dirty(&e->doc),e->doc.revision,e->doc.errors,e->doc.warnings)) return 1;
    return TIDE_KEEP_RUNNING;
}

static void usage(void)
{
    puts("usage: tide open [file.tprefab|project-folder] [--project <folder>] [--check]\n"
         "A native scene/prefab editor. Root: nearest tide.packages, otherwise the file's folder.\n"
         "--preview runs opted-in views on authored match entities; --local-preview uses local placement.\n"
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
    const char *path = NULL, *project_root = NULL; bool check_only = false, preview = false, preview_local = false, embedded = false;
    uint32_t embed_port = 0; const char *embed_token = NULL;
    tide_headless_desc hidden = {.width=1440,.height=900};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) { usage(); return 0; }
        if (strcmp(argv[i], "--project") == 0 && i + 1 < argc) project_root = argv[++i];
        else if (strcmp(argv[i], "--check") == 0) check_only = true;
        else if (strcmp(argv[i], "--embed") == 0 && i + 2 < argc) {
            embedded = true;
            if (!positive(argv[++i],&embed_port) || embed_port > 65535) { usage(); return 2; }
            embed_token = argv[++i];
        }
        else if (strcmp(argv[i], "--preview") == 0) preview = true;
        else if (strcmp(argv[i], "--local-preview") == 0) preview = preview_local = true;
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
    if(path && sys_is_dir(path)) { if(!project_root) project_root=path; path=NULL; }
    bool prefab_file = path && strlen(path) >= 8;
    if (prefab_file) {
#ifdef _WIN32
        prefab_file = _stricmp(path + strlen(path) - 8, ".tprefab") == 0;
#else
        prefab_file = strcmp(path + strlen(path) - 8, ".tprefab") == 0;
#endif
    }
    if (path && !prefab_file) { fputs("tide open: choose a .tprefab file or a project folder\n", stderr); return 2; }
    if(!path && (check_only || embedded || preview)) { fputs("tide open: --check, --embed and --preview need an authored file\n",stderr); return 2; }
    if (embedded && (check_only || hidden.frames || hidden.capture || hidden.script)) { fputs("tide open: --embed cannot be combined with --check/--frames/--capture/--script\n",stderr); return 2; }
    if (sys_hidden() && !embedded && !check_only && !hidden.frames && !hidden.capture && !hidden.script) { fputs("tide open: TIDE_HIDDEN requires --check, --frames, --capture or --script\n", stderr); return 2; }
    if (embedded && !editor_embed_open((uint16_t)embed_port,embed_token)) { fputs("tide open: could not connect to the IDE frame transport\n",stderr); return 2; }
    char *bin = sys_exe_dir(), *root = path_dir(bin), *builtin = path_join(root, "src/packages");
    if (!sys_is_dir(builtin)) { free(builtin); builtin = own(TIDE_SOURCE_DIR "/packages"); }
    packages_set_builtin_folder(builtin); free(builtin); free(root); free(bin);
    editor *e = calloc(1, sizeof(*e)); if (!e) abort(); e->zoom = 48; e->model_lighting = true;
    e->drag_node=-1;
    if (path && !editor_open(&e->doc, path, project_root)) { fprintf(stderr, "tide open: %s\n", e->doc.message); editor_close(&e->doc); free(e); return 2; }
    e->workspace=e->doc.project ? own(e->doc.project) : path_absolute(project_root ? project_root : ".");
    if(!sys_is_dir(e->workspace)) { fputs("tide open: project folder does not exist\n",stderr); free(e->workspace); free(e); return 2; }
    e->preview.enabled = preview; e->preview.local = preview_local;
    e->bounded_preview = hidden.capture || hidden.frames || hidden.script;
    e->embedded = embedded;
    if (check_only) {
        printf("project: %s\n", e->doc.project);
        for (int i = 0; i < e->doc.diagnostics.count; i++) puts(e->doc.diagnostics.items[i]);
        printf("%d prefab(s), %d error(s), %d warning(s)\n", e->doc.prefabs.count, e->doc.errors, e->doc.warnings);
        const int code = e->doc.errors ? 1 : 0; editor_close(&e->doc); free(e); return code;
    }
    e->recovery = e->doc.recovery != NULL;
    closing_editor = e; atexit(close_preview);
    e->asset_stamp = asset_stamp(&e->doc);
    if (embedded) { hidden.frames = UINT32_MAX; tide_platform_open_headless(&hidden); }
    else if (hidden.capture || hidden.frames || hidden.script) {
        if (!hidden.frames && !hidden.script) hidden.frames = 2;
        tide_platform_open_headless(&hidden);
    } else tide_platform_open(&(tide_window_desc){.title="Tide Scene Editor", .width=hidden.width, .height=hidden.height});
    tide_platform_run(frame, e);
    editor_play_close(&e->play);
    editor_preview_close(&e->preview, &e->draw);
    editor_model_free(&e->model);
    free(e->model_asset); free(e->model_key);
    for (int i = 0; i < e->scene_model_count; i++) editor_model_free(&e->scene_models[i]);
    free(e->scene_models);
    editor_close(&e->doc);
    free(e->workspace); free(e->dialog_name); free(e->dialog_path); free(e->search); free(e->root_name);
    free(e->hierarchy_search); free(e->project_search);
    clear_field(e); closing_editor=NULL; free(e);
}
