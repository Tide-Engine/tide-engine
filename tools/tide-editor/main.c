// A native authoring tool. It inspects compiler data, never a live match.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "document.h"
#include "asset_files.h"
#include "model.h"
#include "preview.h"
#include "interaction.h"
#include "embed.h"
#include "packages.h"
#include "sys.h"
#include "tide/platform.h"
#include "tide/text.h"
#include "tide/time.h"

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
static void close_preview(void) { if (closing_editor) editor_preview_close(&closing_editor->preview, &closing_editor->draw); }

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
    const tide_rect rows = {r.x + 8, r.y + 48, list_width - 16, r.height - 110};
    label(e, r.x + 12, r.y + 12, "MODEL ASSETS", dim);
    if (contains(rows, e->gui.mouse)) e->model_scroll -= scroll_rows(e->devices.mouse.poll_scroll.y, 3);
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
    if (e->model_revision != e->doc.revision) {
        if (selected) editor_model_set(&e->model, &selected->mesh, e->model_lighting);
        else editor_model_free(&e->model);
        e->model_revision = e->doc.revision;
    }
    const tide_rect view = {r.x+list_width+4,r.y+48,r.width-list_width-12,r.height-132};
    if (button(e, 75, (tide_rect){view.x,r.y+10,100,26}, "Frame model")) e->model_zoom = 1;
    if (button(e, 76, (tide_rect){view.x+110,r.y+10,100,26}, e->model_lighting ? "Lit" : "Unlit")) {
        e->model_lighting = !e->model_lighting; e->model_revision = UINT64_MAX;
    }
    tide_draw_set_clip(&e->draw, view); tide_draw_set_clip(&e->gui.list, view);
    panel(e, view, rgb(.065f,.08f,.1f));
    if (!row) label(e, rows.x, rows.y, "No static Mesh assets.", dim);
    if (e->model.mesh.vertex_count) {
        if (contains(view, e->gui.mouse)) {
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
    label(e, r.x+12,r.y+r.height-66, "Right drag: orbit  |  Wheel: zoom", dim);
    label(e, r.x+12,r.y+r.height-40, "Preview lighting / imported colors; game materials are not rendered.", dim);
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
    if (button(e, 70, (tide_rect){r.x + 14, r.y + 42, 82, 26}, e->xy ? "XY" : "Isometric")) e->xy = !e->xy;
    if (button(e, 71, (tide_rect){r.x + 106, r.y + 42, 82, 26}, "Center")) e->pan = tide_f2(0, 0);
    if (e->preview.enabled && !e->preview.builder && button(e, 73, (tide_rect){r.x+198,r.y+42,90,26}, e->preview.local ? "Local" : "Match")) {
        const bool local = !e->preview.local;
        editor_preview_close(&e->preview, &e->draw);
        e->preview.enabled = true; e->preview.local = local;
    }
    const tide_rect content = {r.x,r.y+112,r.width,r.height-154};
    if (contains(content, e->gui.mouse) && !e->drag.active) {
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
    tide_float4x4 preview_projection = tide_identity_f4x4();
    preview_projection.c0.x = 2*e->zoom/r.width; preview_projection.c1.y = 2*e->zoom/r.height;
    // SetCameraMatrices turns view-space z for the renderer's convention;
    // compensate here so the wire markers and compiled geometry coincide.
    preview_projection.c2.x = e->xy ? 0 : -.6f*preview_projection.c0.x;
    preview_projection.c2.y = e->xy ? 0 : .35f*preview_projection.c1.y;
    preview_projection.c0.z = -.0001f; preview_projection.c1.z = -.0001f; preview_projection.c2.z = -.0001f;
    preview_projection.c3 = tide_f4(2*e->pan.x/r.width,-2*e->pan.y/r.height,.5f,1);
    const bool compiled_preview = editor_preview_draw(&e->preview, &e->draw, e->frame_seconds, r, tide_identity_f4x4(), preview_projection, tide_platform_measure_text);
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
    if (!gizmo_used && picked >= 0 && e->gui.mouse_pressed && !e->picker && !e->discard && !e->recovery && e->pending == NONE) {
        e->selected = picked; e->inspector_scroll = 0; clear_field(e);
    }
    panel(e, (tide_rect){r.x,r.y,r.width,34}, rgb(.075f,.095f,.115f));
    label(e, r.x + 16, r.y + 10, e->preview.enabled ? "VIEWS PREVIEW" : "SCENE PREVIEW", dim);
    static const char *tools[] = {"Select", "Move", "Rotate", "Scale"};
    for (int t = 0; t < 4; t++) if (button(e, 700u+(uint32_t)t,(tide_rect){r.x+14+(float)t*90,r.y+78,82,26},tools[t])) { e->tool = t; e->drag.active = false; }
    panel(e, (tide_rect){r.x,r.y+r.height-42,r.width,42}, rgb(.075f,.095f,.115f));
    char message[180];
    if (compiled_preview && omitted) snprintf(message, sizeof message, "%d marker(s) need constant transforms; views can still render them.", omitted);
    else if (omitted || omitted_models) snprintf(message, sizeof message, "%d transform(s), %d model field(s) not previewed. Inspect expressions/diagnostics.", omitted, omitted_models);
    else snprintf(message, sizeof message, "%s | Q/W/E/R tools | Shift snap | Esc cancel", compiled_preview ? "Views (authored picking)" : "Static models");
    label(e, r.x + 16, r.y + r.height - 30, message, omitted || omitted_models ? rgb(.95f,.65f,.3f) : dim);
    if (e->preview.enabled) label(e, r.x+300, r.y+48, e->preview.builder ? "Building views..." : e->preview.revision != e->doc.revision ? "Preview stale / build needed" : e->preview.api && !e->preview.api->views ? "No opted-in views" : "Game shaders / authored state", dim);
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
        if (selected_asset_type(e) && button(e, 94, (tide_rect){r.x+254,r.y+r.height-106,90,28}, "Browse")) {
            e->models = true; e->source = false;
        }
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
    if (e->poll >= .5f) {
        e->poll = 0; editor_poll(&e->doc);
        const uint64_t current = asset_stamp(&e->doc);
        if (current != e->asset_stamp && e->doc.revision == revision) editor_refresh(&e->doc);
        e->asset_stamp = asset_stamp(&e->doc);
    }
    if (e->doc.revision != revision) {
        e->drag.active = false;
        e->selected = 0;
        for (int i = 0; selected_path && i < e->doc.nodes.count; i++) if (str_eq_c(e->doc.nodes.items[i].path, selected_path)) e->selected = i;
        clear_field(e);
    }
    free(selected_path);
    if (e->selected >= e->doc.nodes.count) e->selected = 0;
    // Polling can replace the analysis/revision too: bounded output must build
    // that current document, rather than the state from before the poll.
    editor_preview_update(&e->preview, &e->doc, &e->draw, 0);
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
    if (button(e, 6, (tide_rect){528,12,86,28}, e->models ? "Scene" : "Models")) { e->models = !e->models; e->source = false; }
    if (button(e, 7, (tide_rect){624,12,86,28}, e->preview.enabled ? "Views: on" : "Views: off")) {
        e->preview.enabled = !e->preview.enabled;
        e->models = e->source = false;
    }
    if (e->models || e->source) e->drag.active = false;
    sb title = {0}; sb_printf(&title, "%s%s", path_base(e->doc.path), editor_dirty(&e->doc) ? " *" : "");
    label(e, 730, 18, title.data, ink);
    // Shortcuts read presses before GUI consumption, except while typing/modal.
    const bool ctrl = e->devices.keyboard.leftCtrl.pressed || e->devices.keyboard.rightCtrl.pressed;
    if (ctrl && !tide_gui_typing(&e->gui) && !e->picker && !e->discard && !e->recovery) {
        if (e->devices.keyboard.s.down) queue(e, SAVE);
        if (e->devices.keyboard.z.down) queue(e, e->devices.keyboard.leftShift.pressed ? REDO : UNDO);
        if (e->devices.keyboard.y.down) queue(e, REDO);
    }
    if (!ctrl && !tide_gui_typing(&e->gui) && !e->picker && !e->discard && !e->recovery) {
        int tool = -1;
        if (e->devices.keyboard.q.down) tool = 0;
        if (e->devices.keyboard.w.down) tool = 1;
        if (e->devices.keyboard.e.down) tool = 2;
        if (e->devices.keyboard.r.down) tool = 3;
        if (tool >= 0) { e->tool = tool; e->drag.active = false; }
    }
    const float height = size.y - 230, right = size.x - 390;
    panel(e, (tide_rect){0,56,250,height}, rgb(.10f,.125f,.15f));
    panel(e, (tide_rect){right,56,390,height}, rgb(.10f,.125f,.15f));
    tree(e, (tide_rect){0,56,250,height});
    if (e->models && !e->source) models(e, (tide_rect){254,56,right-258,height}, size);
    else if (!e->source) viewport(e, (tide_rect){254,56,right-258,height}, size);
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
    const bool preview_failure = e->preview.enabled && !e->preview.builder && !e->doc.errors && e->preview.revision != e->doc.revision;
    if (preview_failure || !e->doc.diagnostics.count) label(e, 16, size.y-126, e->preview.enabled ? e->preview.message : "Project checked. Save writes only authored overrides; game code remains in .tide files.", preview_failure ? rgb(.98f,.53f,.4f) : dim);
    if (!preview_failure) for (int i = e->diagnostic_scroll; i < e->doc.diagnostics.count && i < e->diagnostic_scroll + 3; i++) label(e, 16, size.y-126 + (float)(i-e->diagnostic_scroll)*25, e->doc.diagnostics.items[i], ink);
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
    if (e->embedded && !editor_embed_frame(editor_dirty(&e->doc),e->doc.revision,e->doc.errors,e->doc.warnings)) return 1;
    return TIDE_KEEP_RUNNING;
}

static void usage(void)
{
    puts("usage: tide open <file.tprefab> [--project <folder>] [--check]\n"
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
    bool prefab_file = path && strlen(path) >= 8;
    if (prefab_file) {
#ifdef _WIN32
        prefab_file = _stricmp(path + strlen(path) - 8, ".tprefab") == 0;
#else
        prefab_file = strcmp(path + strlen(path) - 8, ".tprefab") == 0;
#endif
    }
    if (!prefab_file) { fputs("tide open: first editor supports .tprefab files\n", stderr); return 2; }
    if (embedded && (check_only || hidden.frames || hidden.capture || hidden.script)) { fputs("tide open: --embed cannot be combined with --check/--frames/--capture/--script\n",stderr); return 2; }
    if (sys_hidden() && !embedded && !check_only && !hidden.frames && !hidden.capture && !hidden.script) { fputs("tide open: TIDE_HIDDEN requires --check, --frames, --capture or --script\n", stderr); return 2; }
    if (embedded && !editor_embed_open((uint16_t)embed_port,embed_token)) { fputs("tide open: could not connect to the IDE frame transport\n",stderr); return 2; }
    char *bin = sys_exe_dir(), *root = path_dir(bin), *builtin = path_join(root, "src/packages");
    if (!sys_is_dir(builtin)) { free(builtin); builtin = own(TIDE_SOURCE_DIR "/packages"); }
    packages_set_builtin_folder(builtin); free(builtin); free(root); free(bin);
    editor *e = calloc(1, sizeof(*e)); if (!e) abort(); e->zoom = 48; e->model_lighting = true;
    if (!editor_open(&e->doc, path, project_root)) { fprintf(stderr, "tide open: %s\n", e->doc.message); editor_close(&e->doc); free(e); return 2; }
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
    editor_preview_close(&e->preview, &e->draw);
    editor_model_free(&e->model);
    free(e->model_asset); free(e->model_key);
    for (int i = 0; i < e->scene_model_count; i++) editor_model_free(&e->scene_models[i]);
    free(e->scene_models);
}
