#include "asset_usage.h"
#include "asset_files.h"
#include "version.h"
#include "types.h"
#include "tide/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct pointer_set {
    const void **items;
    uint32_t *ids;
    uint32_t mask, count;
} pointer_set;

typedef enum usage_kind { USAGE_DECL, USAGE_EXPR, USAGE_STMT } usage_kind;
typedef struct usage_node { usage_kind kind; const void *value; } usage_node;
typedef struct asset_usage {
    pointer_set seen, used;
    VEC(usage_node) pending;
    bool server;
    const program *prog;
    tide_sha256 *hash;
} asset_usage;

static uint32_t pointer_place(const void *p, const uint32_t mask)
{
    uint64_t n = (uint64_t)(uintptr_t)p;
    n ^= n >> 33;
    n *= UINT64_C(0xff51afd7ed558ccd);
    n ^= n >> 33;
    return (uint32_t)n & mask;
}

static bool contains(const pointer_set *set, const void *p)
{
    if (!p || !set->items) return false;
    uint32_t at = pointer_place(p, set->mask);
    while (set->items[at] && set->items[at] != p) at = (at + 1u) & set->mask;
    return set->items[at] != NULL;
}

static bool insert(pointer_set *set, const void *p)
{
    if (!p) return false;
    if (!set->items || (uint64_t)(set->count + 1u) * 2u > set->mask) {
        const uint32_t mask = set->items ? set->mask * 2u + 1u : 63u;
        const void **items = arena_alloc(((size_t)mask + 1u) * sizeof *items);
        uint32_t *ids = arena_alloc(((size_t)mask + 1u) * sizeof *ids);
        for (uint32_t i = 0; set->items && i <= set->mask; i++) {
            const void *had = set->items[i];
            if (!had) continue;
            uint32_t at = pointer_place(had, mask);
            while (items[at]) at = (at + 1u) & mask;
            items[at] = had;
            ids[at] = set->ids[i];
        }
        set->items = items;
        set->ids = ids;
        set->mask = mask;
    }
    uint32_t at = pointer_place(p, set->mask);
    while (set->items[at] && set->items[at] != p) at = (at + 1u) & set->mask;
    if (set->items[at]) return false;
    set->items[at] = p;
    set->count++;
    set->ids[at] = set->count;
    return true;
}

static void number(asset_usage *w, uint64_t n)
{
    if (!w->hash) return;
    uint8_t bytes[8];
    for (int i = 0; i < 8; i++) bytes[i] = (uint8_t)(n >> (i * 8));
    tide_sha256_add(w->hash, bytes, sizeof bytes);
}

static void text(asset_usage *w, const str s)
{
    if (!w->hash) return;
    number(w, (uint64_t)s.len);
    if (s.len) tide_sha256_add(w->hash, s.ptr, (size_t)s.len);
}

static void ctext(asset_usage *w, const char *s) { text(w, str_from(s ? s : "")); }

static void queue(asset_usage *w, const usage_kind kind, const void *value)
{
    const bool fresh = insert(&w->seen, value);
    number(w, kind);
    uint32_t id = 0;
    if (value) {
        uint32_t at = pointer_place(value, w->seen.mask);
        while (w->seen.items[at] != value) at = (at + 1u) & w->seen.mask;
        id = w->seen.ids[at];
    }
    number(w, id);
    if (fresh) vec_push(w->pending, ((usage_node){kind, value}));
}

static void value_type(asset_usage *w, const type t)
{
    number(w, t.kind);
    queue(w, USAGE_DECL, t.decl);
}

static void use_asset(asset_usage *w, const decl *d)
{
    if (!d || d->kind != DECL_ASSET) return;
    insert(&w->used, d);
    // Its vertex type can have defaults that read other assets, including
    // defaults inherited through nested types or evaluated by helpers.
    queue(w, USAGE_DECL, d);
}

static void attributes(asset_usage *w, const attribute *items, const int count)
{
    number(w, count);
    for (int i = 0; i < count; i++) {
        text(w, items[i].name);
        number(w, items[i].args.count);
        for (int k = 0; k < items[i].args.count; k++) {
            text(w, items[i].args.items[k].text);
            queue(w, USAGE_DECL, items[i].args.items[k].decl);
        }
        number(w, items[i].values.count);
        for (int k = 0; k < items[i].values.count; k++) queue(w, USAGE_EXPR, items[i].values.items[k]);
        // Retarget profiles are build inputs; their baked result and transitive
        // dependencies already contribute through the animation's asset bytes.
        // Keep option constants in the semantic digest without retaining rigs
        // in a stripped runtime pack merely because a bake used them.
        if (items[i].call) {
            for (int k = 2; k < items[i].call->args.count; k++) {
                if (k < items[i].call->arg_names.count) text(w, items[i].call->arg_names.items[k]);
                queue(w, USAGE_EXPR, items[i].call->args.items[k]);
            }
        }
    }
}

static void parameters(asset_usage *w, const param *items, const int count)
{
    number(w, count);
    for (int i = 0; i < count; i++) {
        text(w, items[i].name);
        number(w, items[i].mode);
        number(w, items[i].is_out);
        number(w, items[i].ownership);
        value_type(w, items[i].type);
        queue(w, USAGE_EXPR, items[i].default_value);
        parameters(w, items[i].query.items, items[i].query.count);
    }
}

typedef struct recipient_schema { VEC(type) types; } recipient_schema;

static void recipient_schema_type(asset_usage *w, recipient_schema *schema, const type t)
{
    int id = 0;
    for (; id < schema->types.count; id++) {
        const type had = schema->types.items[id];
        if (had.kind == t.kind && had.decl == t.decl) break;
    }
    if (id == schema->types.count) vec_push(schema->types, t);
    number(w, t.kind);
    number(w, (uint64_t)id + 1u);
}

// Match client_singleton, arch_scene_column, arch_stores and the logical type
// traversal in client_state_codegen.inc. These are wire records, not a dump of
// declarations reachable from executed game code: an unused helper can still
// change physical sparse storage, and synthetic match singletons are not all
// in program.decls. Recursive values use stable numbered type records.
static void recipient_state_schema(asset_usage *w)
{
    const program *prog = w->prog;
    recipient_schema schema = {0};
    ctext(w, "Tide recipient logical state schema 2");
    for (int i = 0; i < prog->singletons.count; i++) {
        decl *d = prog->singletons.items[i];
        if (d->is_local || d == prog->visibility_state || d == prog->hierarchy[0] || d == prog->task_slots[0]) continue;
        recipient_schema_type(w, &schema, (type){TY_SINGLETON, d});
    }
    number(w, UINT64_MAX);
    number(w, prog->input != NULL);
    if (prog->input) recipient_schema_type(w, &schema, (type){TY_INPUT, prog->input});
    number(w, prog->hierarchy[0] != NULL); // Filtered links and optional affine anchors.
    bool scene_column = false;
    for (int c = 0; c < prog->components.count; c++) {
        const decl *d = prog->components.items[c];
        if (d->is_scene && !d->is_local) scene_column = true;
    }
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (prog->archetype_local.items[a]) continue;
        number(w, a);
        number(w, scene_column);
        for (int c = 0; c < prog->components.count; c++) {
            const bool sparse = mask_has(prog->sparse_mask, c);
            if (!mask_has(prog->archetypes.items[a], c)
                && !(sparse && mask_has(prog->possible.items[a], c))) continue;
            number(w, c);
            number(w, sparse); // A presence byte precedes this component value.
            recipient_schema_type(w, &schema, (type){TY_COMPONENT, prog->components.items[c]});
        }
        number(w, UINT64_MAX);
    }
    number(w, UINT64_MAX);
    for (int id = 0; id < schema.types.count; id++) {
        const type t = schema.types.items[id];
        number(w, id + 1);
        number(w, t.kind);
        if (t.kind == TY_RECORD && t.decl->array_of) {
            number(w, t.decl->array_length);
            recipient_schema_type(w, &schema, (type){TY_RECORD, t.decl->array_of});
        } else if (t.kind == TY_STRUCT || t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT
            || t.kind == TY_EVENT || t.kind == TY_RECORD) {
            for (int k = 0; k < t.decl->fields.count; k++) {
                const field *f = &t.decl->fields.items[k];
                if (f->typed || (f->hidden && t.decl == prog->visibility)) continue;
                text(w, f->name);
                recipient_schema_type(w, &schema, f->type);
            }
            number(w, UINT64_MAX);
        } else if (t.kind == TY_ARRAY) {
            number(w, t.decl->array_length);
            recipient_schema_type(w, &schema, t.decl->fields.items[0].type);
        } else if (t.kind == TY_LIST || t.kind == TY_GRID || t.kind == TY_MAP || t.kind == TY_SET) {
            if (t.kind == TY_GRID) number(w, t.decl->dims);
            for (int k = 0; k < t.decl->fields.count; k++) recipient_schema_type(w, &schema, t.decl->fields.items[k].type);
            number(w, UINT64_MAX);
        } else if (t.kind != TY_STRING && t.kind != TY_RESIDENT) {
            // Only POD leaves are copied at their raw width. Logical records
            // recurse fields and must not fingerprint host pointer padding.
            number(w, type_layout(t).size);
        }
    }
    number(w, UINT64_MAX);
}

// Only visibility games put the server's listener journals on the wire. The
// wire contract depends on filters, payload/query layout and listener bit order,
// not on local handler bodies, local state or presentation-only helpers. Keep
// this surface aligned with listener_runs_for, heard_data, collect_heard_rows
// and gen_hear in codegen.c. A query's binding/filter order also fixes its row
// layout; the global listener order fixes shared query-record IDs.
static bool listener_wire_parameter(const param *p)
{
    if (p->type.kind == TY_QUERY) return p->query_rows && !p->query_local;
    return !p->placement_local && p->type.decl && !p->type.decl->is_local
        && (p->type.kind == TY_EVENT || p->type.kind == TY_COMPONENT || p->type.kind == TY_SINGLETON);
}

static void listener_wire_parameters(asset_usage *w, const param *items, const int count, const bool query)
{
    int kept = 0;
    for (int i = 0; i < count; i++) if (query || listener_wire_parameter(&items[i])) kept++;
    number(w, kept);
    for (int i = 0; i < count; i++) {
        const param *p = &items[i];
        if (!query && !listener_wire_parameter(p)) continue;
        // Read and mut both carry the same match payload; mut only permits local writes.
        number(w, p->mode == PARAM_MUT ? PARAM_READ : p->mode);
        number(w, p->ownership);
        number(w, p->verified);
        if (p->type.kind == TY_QUERY) {
            number(w, p->query_rows);
            number(w, p->query_local);
            for (int k = 0; k < COMP_WORDS; k++) {
                number(w, p->query_need_mask.w[k]);
                number(w, p->query_without_mask.w[k]);
            }
            listener_wire_parameters(w, p->query.items, p->query.count, true);
        } else value_type(w, p->type);
    }
}

// tidec --embed's taps (add_embed_taps in compile.c) listen to the game's own events alone, which servers never journal
// to recipients (they're each client's own simulation's), so they change nothing sent: a build for another program
// plays with every other build of the game.
static void listener_wire(asset_usage *w)
{
    ctext(w, "Tide recipient listener journals 1");
    int count = 0;
    for (int i = 0; i < w->prog->handlers.count; i++) {
        if (w->prog->handlers.items[i]->listens && !w->prog->handlers.items[i]->embed_tap) count++;
    }
    number(w, count);
    for (int i = 0; i < w->prog->handlers.count; i++) {
        const decl *h = w->prog->handlers.items[i];
        if (!h->listens || h->embed_tap) continue;
        text(w, h->qualified); // gen_hear includes names in TIDE_HEARD_BUILD.
        queue(w, USAGE_DECL, h->event);
        number(w, h->listens_verified);
        number(w, h->per_entity);
        for (int k = 0; k < COMP_WORDS; k++) {
            number(w, h->need_mask.w[k]);
            number(w, h->without_mask.w[k]);
        }
        listener_wire_parameters(w, h->params.items, h->params.count, false);
    }
}

static void declaration(asset_usage *w, const decl *d)
{
    number(w, d->kind);
    text(w, d->qualified);
    number(w, d->is_scene);
    if (d->kind == DECL_COMPONENT || d->kind == DECL_SINGLETON || d->kind == DECL_EVENT)
        number(w, d->kind == DECL_SINGLETON && !d->is_local ? d->match_singleton_index : d->index);
    number(w, d->is_sparse);
    number(w, d->visibility_optional);
    number(w, d->is_extern);
    number(w, d->extern_system);
    if (d->is_extern || d->extern_system) ctext(w, d->c_name);
    number(w, d->array_length);
    number(w, d->dims);
    number(w, d->is_set);
    number(w, d->is_command);
    number(w, d->asset_animation_index);
    number(w, d->width);
    number(w, d->is_mut_method);
    number(w, d->is_operator);
    number(w, d->op);
    number(w, d->is_async);
    number(w, d->per_entity);
    number(w, d->channel);
    number(w, d->unordered);
    queue(w, USAGE_DECL, d->order_enum);
    number(w, d->order_offset);
    if (d->order_member) text(w, d->order_member->name);
    else text(w, (str){0});
    number(w, d->after.count);
    for (int i = 0; i < d->after.count; i++) queue(w, USAGE_DECL, d->after.items[i]);
    value_type(w, d->fails);
    value_type(w, d->result);
    queue(w, USAGE_DECL, d->array_of);
    queue(w, USAGE_DECL, d->optional_of);
    queue(w, USAGE_DECL, d->asset_ref_of);
    queue(w, USAGE_DECL, d->event);
    if (w->hash && d->asset) {
        const asset_value *a = d->asset;
        number(w, a->kind);
        number(w, a->many);
        number(w, a->is_map);
        number(w, a->count);
        for (int i = 0; i < a->count; i++) {
            const asset_match *m = &a->items[i];
            ctext(w, m->key);
            tide_sha256_add(w->hash, m->hash, 32);
            // Imported side buffers and decoded canonical models participate too.
            if (m->skinned) tide_sha256_add(w->hash, m->skinned, m->skinned_size);
            for (int k = 0; k < m->side_count; k++) {
                FILE *f = fopen(m->sides[k], "rb");
                tide_sha256 side;
                tide_sha256_init(&side);
                if (f) {
                    uint8_t bytes[4096];
                    size_t n;
                    while ((n = fread(bytes, 1, sizeof bytes, f))) tide_sha256_add(&side, bytes, n);
                    if (ferror(f)) diag_error(d->at, "cannot fingerprint asset dependency '%s'", m->sides[k]);
                    fclose(f);
                } else diag_error(d->at, "cannot read asset dependency '%s'", m->sides[k]);
                uint8_t digest[32];
                tide_sha256_end(&side, digest);
                tide_sha256_add(w->hash, digest, sizeof digest);
            }
        }
    }
    // A match can receive an opaque asset reference through an input/command.
    // Its value can select any matching declaration, including one named only
    // by a client's UI; retain the complete resolver domain for that type.
    if (w->server && d->asset_ref_of) {
        for (int i = 0; i < w->prog->assets.count; i++) {
            const decl *asset = w->prog->assets.items[i];
            if (type_asset_reference_accepts(d, asset->return_type)) use_asset(w, asset);
        }
    }
    value_type(w, d->return_type);
    value_type(w, d->skin_vertex);
    if (!w->server) queue(w, USAGE_DECL, d->local_part);
    if (!w->server) queue(w, USAGE_DECL, d->interpolate);
    queue(w, USAGE_EXPR, d->value);
    if (!w->server || d->kind != DECL_INPUT) queue(w, USAGE_STMT, d->body);
    queue(w, USAGE_STMT, d->sanitize);
    if (!w->server || w->hash) queue(w, USAGE_STMT, d->predict);
    parameters(w, d->params.items, d->params.count);
    attributes(w, d->attributes.items, d->attributes.count);
    for (int i = 0; i < d->fields.count; i++) {
        const field *f = &d->fields.items[i];
        if (w->server && f->is_local) continue;
        text(w, f->name);
        value_type(w, f->type);
        queue(w, USAGE_EXPR, f->default_value);
        attributes(w, f->attributes.items, f->attributes.count);
    }
    number(w, UINT64_MAX);
    if (!w->server) for (int i = 0; i < d->methods.count; i++) queue(w, USAGE_DECL, d->methods.items[i]);
    number(w, d->members.count);
    for (int i = 0; i < d->members.count; i++) {
        text(w, d->members.items[i].name);
        queue(w, USAGE_EXPR, d->members.items[i].value);
    }
    number(w, d->handlers.count);
    for (int i = 0; i < d->handlers.count; i++) queue(w, USAGE_DECL, d->handlers.items[i]);
    for (int i = 0; i < d->samples.count; i++) use_asset(w, d->samples.items[i].asset);
    for (int i = 0; i < d->gpu_data.count; i++) use_asset(w, d->gpu_data.items[i]);
    for (int i = 0; i < d->gpu_samples.count; i++) queue(w, USAGE_EXPR, d->gpu_samples.items[i].asset);
}

static void expression(asset_usage *w, const expr *e)
{
    if (e->task_source) { queue(w, USAGE_EXPR, e->task_source); return; }
    number(w, e->kind);
    value_type(w, e->type);
    text(w, e->text);
    text(w, e->local && e->local->discard ? (str){0} : e->name);
    text(w, e->member);
    number(w, (uint64_t)e->int_value);
    uint32_t bits;
    memcpy(&bits, &e->float_value, sizeof bits);
    number(w, bits);
    number(w, e->bool_value);
    number(w, e->is_long);
    number(w, e->bind);
    number(w, e->edge);
    number(w, e->swizzle_len);
    for (int i = 0; i < e->swizzle_len; i++) number(w, e->swizzle[i]);
    ctext(w, e->c_constant);
    ctext(w, e->c_callee);
    ctext(w, e->c_pack);
    number(w, e->call);
    number(w, e->ctor);
    number(w, e->op);
    number(w, e->postfix);
    number(w, e->skin_operation);
    number(w, e->visibility_operation);
    number(w, e->asset_reference);
    number(w, e->asset_ref_value);
    number(w, e->is_out);
    number(w, e->looks_for);
    text(w, e->pattern);
    text(w, e->type_arg);
    number(w, e->arg_order.count);
    for (int i = 0; i < e->arg_order.count; i++) number(w, e->arg_order.items[i]);
    number(w, e->parts.count);
    for (int i = 0; i < e->parts.count; i++) text(w, e->parts.items[i]);
    number(w, e->formats.count);
    for (int i = 0; i < e->formats.count; i++) text(w, e->formats.items[i]);
    // Bindings and field positions are edges, never source addresses.
    if (e->param) { text(w, e->param->name); value_type(w, e->param->type); }
    else text(w, (str){0});
    queue(w, USAGE_STMT, e->local);
    if (e->field) { text(w, e->field->name); value_type(w, e->field->type); }
    else text(w, (str){0});
    if (e->enum_member) text(w, e->enum_member->name);
    else text(w, (str){0});
    if (e->prefab) {
        number(w, e->prefab->load);
        number(w, e->prefab->entities.count);
        for (int i = 0; i < e->prefab->entities.count; i++) {
            number(w, e->prefab->entities.items[i].parent);
            queue(w, USAGE_EXPR, e->prefab->entities.items[i].spawn);
        }
    }
    if (e->bind == BIND_ASSET) use_asset(w, e->constant);
    else if (e->bind == BIND_CONST) queue(w, USAGE_DECL, e->constant);
    queue(w, USAGE_DECL, e->type.decl);
    queue(w, USAGE_DECL, e->type_decl);
    queue(w, USAGE_DECL, e->method);
    queue(w, USAGE_DECL, e->frag);
    queue(w, USAGE_DECL, e->vert);
    queue(w, USAGE_EXPR, e->object);
    queue(w, USAGE_EXPR, e->lhs);
    queue(w, USAGE_EXPR, e->rhs);
    queue(w, USAGE_EXPR, e->cond);
    queue(w, USAGE_EXPR, e->skin_mesh);
    queue(w, USAGE_EXPR, e->skin_pose);
    queue(w, USAGE_EXPR, e->values);
    number(w, e->args.count);
    for (int i = 0; i < e->args.count; i++) queue(w, USAGE_EXPR, e->args.items[i]);
    number(w, e->written.count);
    for (int i = 0; i < e->written.count; i++) queue(w, USAGE_EXPR, e->written.items[i]);
    number(w, e->inits.count);
    for (int i = 0; i < e->inits.count; i++) {
        text(w, e->inits.items[i].name);
        queue(w, USAGE_EXPR, e->inits.items[i].value);
    }
    queue(w, USAGE_STMT, e->compare[0]);
    queue(w, USAGE_STMT, e->compare[1]);
    queue(w, USAGE_STMT, e->binding);
    queue(w, USAGE_STMT, e->out_local);
    queue(w, USAGE_STMT, e->step);
    queue(w, USAGE_STMT, e->block);
}

static void statement(asset_usage *w, const stmt *s)
{
    number(w, s->kind);
    // Discard storage has compiler-generated names containing source positions.
    // Its graph edges/order carry identity; moving it does not change simulation.
    text(w, s->discard ? (str){0} : s->name);
    number(w, s->is_mut);
    number(w, s->out_decl);
    number(w, s->delayed);
    number(w, s->discard);
    number(w, s->op);
    number(w, s->parallel);
    number(w, s->windowed);
    for (int i = 0; i < 3; i++) {
        number(w, s->block[i]); number(w, s->window_lo[i]); number(w, s->window_hi[i]);
    }
    value_type(w, s->type);
    queue(w, USAGE_DECL, s->operator_decl);
    queue(w, USAGE_EXPR, s->cond);
    queue(w, USAGE_EXPR, s->value);
    queue(w, USAGE_EXPR, s->target);
    queue(w, USAGE_EXPR, s->by);
    queue(w, USAGE_EXPR, s->offset);
    queue(w, USAGE_STMT, s->then_stmt);
    queue(w, USAGE_STMT, s->else_stmt);
    queue(w, USAGE_STMT, s->init);
    queue(w, USAGE_STMT, s->step);
    number(w, s->stmts.count);
    for (int i = 0; i < s->stmts.count; i++) queue(w, USAGE_STMT, s->stmts.items[i]);
    number(w, s->cases.count);
    for (int i = 0; i < s->cases.count; i++) {
        const switch_case *c = &s->cases.items[i];
        number(w, c->labels.count);
        for (int k = 0; k < c->labels.count; k++) queue(w, USAGE_EXPR, c->labels.items[k]);
        number(w, c->body.count);
        for (int k = 0; k < c->body.count; k++) queue(w, USAGE_STMT, c->body.items[k]);
    }
}

static void strip_assets(program *prog, const bool server)
{
    if (!prog->assets.count) return;
    asset_usage w = {.server = server, .prog = prog};
    if (prog->startup_create) queue(&w, USAGE_DECL, prog->startup_create);
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (server) {
            if (!d->is_local && !d->is_view && (d->kind == DECL_SYSTEM || (d->kind == DECL_COMPONENT && d->is_scene)
                || d->kind == DECL_SINGLETON || d->kind == DECL_INPUT
                || d->kind == DECL_EVENT)) queue(&w, USAGE_DECL, d);
        } else if (d->kind != DECL_ASSET) queue(&w, USAGE_DECL, d);
    }
    if (!server) {
        for (int i = 0; i < prog->local_parts.count; i++) queue(&w, USAGE_DECL, prog->local_parts.items[i]);
        // Generated type helpers may evaluate defaults without an explicit
        // expression at the call site (TryGet, out results and runtime Decode).
        for (int i = 0; i < prog->structs.count; i++) queue(&w, USAGE_DECL, prog->structs.items[i]);
        for (int i = 0; i < prog->lists.count; i++) queue(&w, USAGE_DECL, prog->lists.items[i]);
        for (int i = 0; i < prog->maps.count; i++) queue(&w, USAGE_DECL, prog->maps.items[i]);
        for (int i = 0; i < prog->arrays.count; i++) queue(&w, USAGE_DECL, prog->arrays.items[i]);
        for (int i = 0; i < prog->skinned_types.count; i++) queue(&w, USAGE_DECL, prog->skinned_types.items[i]);
        for (int i = 0; i < prog->shaders.count; i++) {
            const shader *s = prog->shaders.items[i];
            queue(&w, USAGE_DECL, s->vertex);
            queue(&w, USAGE_DECL, s->vert);
            queue(&w, USAGE_DECL, s->frag);
            for (int k = 0; k < s->textures.count; k++) use_asset(&w, s->textures.items[k].asset);
        }
    }
    while (w.pending.count) {
        const usage_node n = w.pending.items[--w.pending.count];
        if (n.kind == USAGE_DECL) declaration(&w, n.value);
        else if (n.kind == USAGE_EXPR) expression(&w, n.value);
        else statement(&w, n.value);
    }
    int count = 0;
    for (int i = 0; i < prog->assets.count; i++) {
        decl *d = prog->assets.items[i];
        const bool used = contains(&w.used, d);
        if (server) d->server_asset = used;
        if (used) prog->assets.items[count++] = d;
    }
    prog->assets.count = count;
}

void program_strip_unused_assets(program *prog) { strip_assets(prog, false); }
void program_strip_server_assets(program *prog) { strip_assets(prog, true); }

bool program_simulation_id(const program *prog, uint8_t out[32], const bool conservative_assets)
{
    tide_sha256 hash;
    tide_sha256_init(&hash);
    asset_usage w = {.server = true, .prog = prog, .hash = &hash};
    ctext(&w, "Tide simulation graph 1");
    ctext(&w, tide_version_text());
    ctext(&w, tide_simulation_runtime());
    number(&w, prog->tick_rate);
    number(&w, prog->max_players);
    uint32_t rewind;
    memcpy(&rewind, &prog->max_rewind, sizeof rewind);
    number(&w, rewind);
    number(&w, prog->host_migration);
    number(&w, prog->delay_prediction);
    number(&w, prog->visibility_state != NULL);
    if (prog->visibility_state) { recipient_state_schema(&w); listener_wire(&w); }
    number(&w, prog->start_scenes.count);
    for (int i = 0; i < prog->start_scenes.count; i++) queue(&w, USAGE_DECL, prog->start_scenes.items[i]);
    if (prog->startup) queue(&w, USAGE_DECL, prog->startup_create ? prog->startup_create : prog->startup);
    for (int i = 0; i < DEVICE_WORDS; i++) number(&w, prog->device_uses[i]);
    for (int i = 0; i < prog->archetypes.count; i++) {
        if (prog->archetype_local.items[i]) continue;
        number(&w, i);
        for (int k = 0; k < COMP_WORDS; k++) {
            number(&w, prog->archetypes.items[i].w[k]);
            number(&w, prog->possible.items[i].w[k]);
        }
        // This is arch_stores in codegen.c: sparse columns remain physical
        // storage even though they are absent from the solid archetype mask.
        // Match the canonical codec's component order and presence-byte shape,
        // including targets named only by an otherwise unreachable helper.
        for (int c = 0; c < prog->components.count; c++) {
            const bool sparse = mask_has(prog->sparse_mask, c);
            if (!mask_has(prog->archetypes.items[i], c)
                && !(sparse && mask_has(prog->possible.items[i], c))) continue;
            number(&w, c);
            number(&w, sparse);
            queue(&w, USAGE_DECL, prog->components.items[c]);
        }
        number(&w, UINT64_MAX);
    }
    number(&w, UINT64_MAX);
    // All match schemas participate, even if currently uninstantiated. Native
    // extern code can use them; component and command declaration order is wire ABI.
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (!d->is_local && !d->is_view && (d->kind == DECL_COMPONENT || d->kind == DECL_SYSTEM
            || d->kind == DECL_SINGLETON || d->kind == DECL_INPUT || d->kind == DECL_EVENT))
            queue(&w, USAGE_DECL, d);
    }
    number(&w, UINT64_MAX);
    // Custom C can read packed assets without a Tide reference. Until it can
    // declare a narrower capability, every asset is a compatibility input.
    if (conservative_assets) {
        for (int i = 0; i < prog->assets.count; i++) use_asset(&w, prog->assets.items[i]);
    }
    for (int at = 0; at < w.pending.count; at++) {
        const usage_node n = w.pending.items[at];
        number(&w, at + 1);
        number(&w, n.kind);
        if (n.kind == USAGE_DECL) declaration(&w, n.value);
        else if (n.kind == USAGE_EXPR) expression(&w, n.value);
        else statement(&w, n.value);
    }
    tide_sha256_end(&hash, out);
    return w.used.count != 0;
}
