// What a game declares, file by file, as JSON for tools that show it (`tidec --outline`, and tide unity's
// outline.json, which the Unity package's inspectors read): each declaration's kind, name and place, a type's
// fields, and for the systems, views and handlers what they read and write, and in the tick what they wait for and
// why, as the schedule says it.

#include <string.h>

#include "ast.h"
#include "types.h"

static void put_json_text(sb *out, const char *s, size_t n)
{
    sb_put(out, "\"");
    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            const char escaped[3] = {'\\', (char)c, '\0'};
            sb_put(out, escaped);
        } else if (c < 0x20) {
            sb_printf(out, "\\u%04x", c);
        } else {
            sb_putn(out, (const char *)&s[i], 1);
        }
    }
    sb_put(out, "\"");
}

static void put_json_c(sb *out, const char *s)
{
    put_json_text(out, s, strlen(s));
}

static void put_json_str(sb *out, const str s)
{
    put_json_text(out, s.ptr, (size_t)s.len);
}

// A declaration's name as code outside its namespace writes it, with an overload's parameters
static void put_name(sb *out, const decl *d)
{
    sb name = {0};
    put_decl_name(&name, d, "", NULL);
    put_json_text(out, name.data, name.len);
}

static const char *kind_of(const decl *d)
{
    switch (d->kind) {
    case DECL_PREFAB: return "prefab";
    case DECL_COMPONENT: return d->is_scene ? "scene" : "component";
    case DECL_SINGLETON: return "singleton";
    case DECL_SYSTEM:
        if (d->is_view) return "view";
        if (d->is_handler) return d->listens ? "localHandler" : "handler";
        return "system";
    case DECL_INPUT: return "input";
    case DECL_STRUCT: return "struct";
    case DECL_FUNCTION: return d->is_audio ? "audio" : "function";
    case DECL_EVENT: return d->is_command ? "command" : "event";
    case DECL_ENUM: return "enum";
    case DECL_CONST: return "const";
    case DECL_SETTINGS: return "settings";
    case DECL_SHADER: return "shader";
    case DECL_ASSET: return "asset";
    case DECL_ALIAS: return "alias";
    default: return NULL;
    }
}

// tidec --embed's taps, which nobody wrote, are left out
static void put_names(sb *out, const char *key, const decl *const *items, const int count)
{
    int written = 0;
    for (int i = 0; i < count; i++) {
        if (items[i]->embed_tap) continue;
        if (written++) sb_put(out, ", ");
        else sb_printf(out, ", \"%s\": [", key);
        put_name(out, items[i]);
    }
    if (written) sb_put(out, "]");
}

// Its fields, then a match component's or singleton's local ones, which are its local part's
static void put_fields(sb *out, const decl *d)
{
    int count = 0;
    for (int part = 0; part < 2; part++) {
        const decl *of = part ? d->local_part : d;
        for (int i = 0; of && i < of->fields.count; i++) {
            const field *f = &of->fields.items[i];
            if (f->hidden) continue;
            sb_put(out, count++ ? ", " : ", \"fields\": [");
            sb_put(out, "{\"name\": ");
            put_json_str(out, f->name);
            sb_put(out, ", \"type\": ");
            put_json_c(out, type_name(f->type));
            if (part || f->is_local) sb_put(out, ", \"local\": true");
            // A text field's default, as written between its quotes, which the schema's default bytes don't hold
            if (f->default_value && f->default_value->kind == E_STRING) {
                sb_put(out, ", \"text\": ");
                put_json_str(out, f->default_value->text);
            }
            sb_put(out, "}");
        }
    }
    if (count) sb_put(out, "]");
}

// What a system, view or handler reads and writes: its parameters' components and singletons, then what it reaches
// through World or queries, which is any entity's (`every`)
static void put_access(sb *out, const decl *sys)
{
    int count = 0;
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (p->mode != PARAM_READ && p->mode != PARAM_MUT) continue;
        if (p->type.kind != TY_COMPONENT && p->type.kind != TY_SINGLETON) continue;
        sb_put(out, count++ ? ", " : ", \"access\": [");
        sb_put(out, "{\"name\": ");
        put_name(out, p->type.decl);
        if (p->mode == PARAM_MUT) sb_put(out, ", \"writes\": true");
        sb_put(out, "}");
    }
    for (int i = 0; i < sys->world_uses.count; i++) {
        const world_use *u = &sys->world_uses.items[i];
        sb_put(out, count++ ? ", " : ", \"access\": [");
        sb_put(out, "{\"name\": ");
        put_name(out, u->component);
        if (u->writes) sb_put(out, ", \"writes\": true");
        sb_put(out, ", \"every\": true}");
    }
    if (count) sb_put(out, "]");
    // Its filters: `with` and `without` a component, a scene, or ownership
    for (int pass = 0; pass < 2; pass++) {
        const param_mode mode = pass ? PARAM_WITHOUT : PARAM_WITH;
        int filters = 0;
        for (int i = 0; i < sys->params.count; i++) {
            const param *p = &sys->params.items[i];
            if (p->mode != mode) continue;
            sb_printf(out, filters++ ? ", " : ", \"%s\": [", pass ? "without" : "with");
            if (p->ownership) put_json_c(out, "ownership");
            else if (p->scene && p->scene_of) {
                sb scene = {0};
                sb_put(&scene, "scene ");
                put_decl_name(&scene, p->scene_of, "", NULL);
                put_json_text(out, scene.data, scene.len);
            } else if (p->type.decl) put_name(out, p->type.decl);
            else put_json_str(out, p->type_name);
        }
        if (filters) sb_put(out, "]");
    }
}

// Where it is in the tick: its stage, what it waits for and why, and what can run alongside it
static void put_schedule(sb *out, const decl *sys)
{
    sb_printf(out, ", \"stage\": %d", sys->stage);
    if (sys->waits.count) {
        sb_put(out, ", \"after\": [");
        for (int i = 0; i < sys->waits.count; i++) {
            const system_wait *w = &sys->waits.items[i];
            sb why = {0};
            describe_wait(sys, w, "", &why);
            if (w->through) {
                sb_put(&why, " (and through ");
                put_decl_name(&why, w->through, "", NULL);
                sb_put(&why, ")");
            }
            sb_put(out, i ? ", {\"name\": " : "{\"name\": ");
            put_name(out, w->on);
            sb_put(out, ", \"why\": ");
            put_json_text(out, why.data ? why.data : "", why.len);
            sb_put(out, "}");
        }
        sb_put(out, "]");
    }
    if (sys->waits_unlisted) sb_printf(out, ", \"afterMore\": %d", sys->waits_unlisted);
    put_names(out, "alongside", (const decl *const *)sys->alongside.items, sys->alongside.count);
    if (sys->alongside_count > sys->alongside.count) {
        sb_printf(out, ", \"alongsideMore\": %d", sys->alongside_count - sys->alongside.count);
    }
}

void print_outline(const program *prog, sb *out)
{
    sb_put(out, "{\"declarations\": [");
    int count = 0;
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        const char *kind = kind_of(d);
        if (!kind || d->builtin || !d->unit || !d->unit->src || d->embed_tap || d == prog->startup_create) continue;
        sb_put(out, count++ ? ",\n  {" : "\n  {");
        sb_printf(out, "\"kind\": \"%s\", \"name\": ", kind);
        put_name(out, d);
        sb_put(out, ", \"file\": ");
        put_json_c(out, d->unit->src->path);
        sb_printf(out, ", \"line\": %d, \"column\": %d", d->at.line, d->at.col);
        if (d->is_local) sb_put(out, ", \"local\": true");
        if (d->is_async) sb_put(out, ", \"async\": true");
        if (d->is_extern || d->extern_system) sb_put(out, ", \"extern\": true");
        switch (d->kind) {
        case DECL_ALIAS:
            sb_put(out, ", \"type\": ");
            put_json_c(out, type_name(d->alias_type));
            break;
        case DECL_COMPONENT:
        case DECL_SINGLETON:
        case DECL_STRUCT:
        case DECL_INPUT:
            put_fields(out, d);
            break;
        case DECL_EVENT:
            put_fields(out, d);
            put_names(out, "handlers", (const decl *const *)d->handlers.items, d->handlers.count);
            put_names(out, "localHandlers", (const decl *const *)d->listeners.items, d->listeners.count);
            break;
        case DECL_ENUM:
            if (d->members.count) {
                sb_put(out, ", \"members\": [");
                for (int k = 0; k < d->members.count; k++) {
                    if (k) sb_put(out, ", ");
                    put_json_str(out, d->members.items[k].name);
                }
                sb_put(out, "]");
            }
            break;
        case DECL_SYSTEM:
            if (!d->is_view && !d->is_handler) sb_put(out, d->per_entity ? ", \"perEntity\": true" : ", \"perEntity\": false");
            else if (d->per_entity) sb_put(out, ", \"perEntity\": true");
            if (d->is_handler && d->event) {
                sb_put(out, ", \"event\": ");
                put_name(out, d->event);
            }
            put_access(out, d);
            if (d->stage > 0) put_schedule(out, d);
            break;
        default:
            break;
        }
        sb_put(out, "}");
    }
    sb_put(out, count ? "\n]}\n" : "]}\n");
}
