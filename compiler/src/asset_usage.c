#include "asset_usage.h"

typedef struct pointer_set {
    const void **items;
    uint32_t mask, count;
} pointer_set;

typedef enum usage_kind { USAGE_DECL, USAGE_EXPR, USAGE_STMT } usage_kind;
typedef struct usage_node { usage_kind kind; const void *value; } usage_node;
typedef struct asset_usage {
    pointer_set seen, used;
    VEC(usage_node) pending;
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
        for (uint32_t i = 0; set->items && i <= set->mask; i++) {
            const void *had = set->items[i];
            if (!had) continue;
            uint32_t at = pointer_place(had, mask);
            while (items[at]) at = (at + 1u) & mask;
            items[at] = had;
        }
        set->items = items;
        set->mask = mask;
    }
    uint32_t at = pointer_place(p, set->mask);
    while (set->items[at] && set->items[at] != p) at = (at + 1u) & set->mask;
    if (set->items[at]) return false;
    set->items[at] = p;
    set->count++;
    return true;
}

static void queue(asset_usage *w, const usage_kind kind, const void *value)
{
    if (insert(&w->seen, value)) vec_push(w->pending, ((usage_node){kind, value}));
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
    for (int i = 0; i < count; i++)
        for (int k = 0; k < items[i].values.count; k++) queue(w, USAGE_EXPR, items[i].values.items[k]);
}

static void parameters(asset_usage *w, const param *items, const int count)
{
    for (int i = 0; i < count; i++) {
        queue(w, USAGE_DECL, items[i].type.decl);
        queue(w, USAGE_EXPR, items[i].default_value);
        parameters(w, items[i].query.items, items[i].query.count);
    }
}

static void declaration(asset_usage *w, const decl *d)
{
    queue(w, USAGE_DECL, d->return_type.decl);
    queue(w, USAGE_DECL, d->skin_vertex.decl);
    queue(w, USAGE_DECL, d->local_part);
    queue(w, USAGE_DECL, d->interpolate);
    queue(w, USAGE_EXPR, d->value);
    queue(w, USAGE_STMT, d->body);
    queue(w, USAGE_STMT, d->sanitize);
    queue(w, USAGE_STMT, d->predict);
    parameters(w, d->params.items, d->params.count);
    attributes(w, d->attributes.items, d->attributes.count);
    for (int i = 0; i < d->fields.count; i++) {
        const field *f = &d->fields.items[i];
        queue(w, USAGE_DECL, f->type.decl);
        queue(w, USAGE_EXPR, f->default_value);
        attributes(w, f->attributes.items, f->attributes.count);
    }
    for (int i = 0; i < d->methods.count; i++) queue(w, USAGE_DECL, d->methods.items[i]);
    for (int i = 0; i < d->members.count; i++) queue(w, USAGE_EXPR, d->members.items[i].value);
    for (int i = 0; i < d->samples.count; i++) use_asset(w, d->samples.items[i].asset);
    for (int i = 0; i < d->gpu_data.count; i++) use_asset(w, d->gpu_data.items[i]);
    for (int i = 0; i < d->gpu_samples.count; i++) queue(w, USAGE_EXPR, d->gpu_samples.items[i].asset);
}

static void expression(asset_usage *w, const expr *e)
{
    if (e->prefab) {
        for (int i = 0; i < e->prefab->entities.count; i++) queue(w, USAGE_EXPR, e->prefab->entities.items[i].spawn);
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
    for (int i = 0; i < e->args.count; i++) queue(w, USAGE_EXPR, e->args.items[i]);
    for (int i = 0; i < e->written.count; i++) queue(w, USAGE_EXPR, e->written.items[i]);
    for (int i = 0; i < e->inits.count; i++) queue(w, USAGE_EXPR, e->inits.items[i].value);
    queue(w, USAGE_STMT, e->compare[0]);
    queue(w, USAGE_STMT, e->compare[1]);
    queue(w, USAGE_STMT, e->binding);
    queue(w, USAGE_STMT, e->out_local);
    queue(w, USAGE_STMT, e->step);
    queue(w, USAGE_STMT, e->block);
}

static void statement(asset_usage *w, const stmt *s)
{
    queue(w, USAGE_DECL, s->type.decl);
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
    for (int i = 0; i < s->stmts.count; i++) queue(w, USAGE_STMT, s->stmts.items[i]);
    for (int i = 0; i < s->cases.count; i++) {
        const switch_case *c = &s->cases.items[i];
        for (int k = 0; k < c->labels.count; k++) queue(w, USAGE_EXPR, c->labels.items[k]);
        for (int k = 0; k < c->body.count; k++) queue(w, USAGE_STMT, c->body.items[k]);
    }
}

void program_strip_unused_assets(program *prog)
{
    if (!prog->assets.count) return;
    asset_usage w = {0};
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (d->kind != DECL_ASSET) queue(&w, USAGE_DECL, d);
    }
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
    while (w.pending.count) {
        const usage_node n = w.pending.items[--w.pending.count];
        if (n.kind == USAGE_DECL) declaration(&w, n.value);
        else if (n.kind == USAGE_EXPR) expression(&w, n.value);
        else statement(&w, n.value);
    }
    int count = 0;
    for (int i = 0; i < prog->assets.count; i++) {
        decl *d = prog->assets.items[i];
        if (contains(&w.used, d)) prog->assets.items[count++] = d;
    }
    prog->assets.count = count;
}
