// Shards: one match world split across servers by where things are
// (docs/spec.md, Shards; notes/sharding.md; the runtime's side is tide/shard.h).
// Included by codegen.c. A game without `settings { shards = true; }` gets
// none of this, and its generated C is what it was.
//
// A shard's world is an ordinary match world with a hidden singleton
// (tide_shard_state) and a hidden sparse component on its ghosts (tide_ghost:
// other shards' entities near its border, which it only reads). Everything
// it hears from other shards comes as messages applied before a tick
// (tide_shard_receive), and everything it says goes in its outbox, which is
// world state, so a shard's clients keep what its server keeps.
//
// Code that would change what its shard doesn't own is noted under its
// identity (tide_shard_code: a system's, or an event delivery's), and the
// tick runs again from where it started with that code left out (the tick
// wrapper in gen_shard_main): a skipped system row runs again the next tick,
// and a skipped event delivery waits, with its event, until this shard owns
// what it touched, then runs in full.

// The numbers code is noted by: a system's, then each event's deliveries. 0 is
// code that can't wait, whose changes across a border are dropped (counted:
// tide_shard_dropped), such as a task's resume.
static int shard_code_system(const decl *sys)
{
    return 1 + sys->index;
}

static int shard_code_event(const program *prog, const decl *event)
{
    return 1 + prog->systems.count + event->index;
}

static int shard_codes(const program *prog)
{
    int most = 0;
    for (int i = 0; i < prog->events.count; i++) {
        if (prog->events.items[i]->index + 1 > most) most = prog->events.items[i]->index + 1;
    }
    return 1 + prog->systems.count + most;
}

// The game's own match singletons: those that live with the root shard, read
// everywhere as copies and borrowed to be changed elsewhere. The engine's own
// (Time, the shard state, hierarchy links) are each shard's.
static bool shard_shares(const decl *d)
{
    return !d->is_local && !d->builtin;
}

static int shard_singleton(const program *prog, const decl *d)
{
    int n = 0;
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *s = prog->singletons.items[i];
        if (s == d) return shard_shares(s) ? n : -1;
        if (shard_shares(s)) n++;
    }
    return -1;
}

static int shard_singleton_count(const program *prog)
{
    int n = 0;
    for (int i = 0; i < prog->singletons.count; i++) n += shard_shares(prog->singletons.items[i]);
    return n;
}

// Whether an event's deliveries wait on their own: one that's queued (sent by
// code) and Spawned, which runs again for its entity. Destroyed's wait with
// the destroy they're part of (tide_shard_destroy_waits).
static bool shard_parks(const program *prog, const decl *event)
{
    return is_queued(prog, event) || event == prog->spawned;
}

// The value codecs a shard packs entities, singletons and events with.
static void collect_shard_codecs(gen *g)
{
    const program *prog = g->prog;
    if (!sharded(g)) return;
    for (int i = 0; i < prog->components.count; i++) {
        const decl *d = prog->components.items[i];
        if (!d->is_local) client_codec_type(&g->value_codec, (type){TY_COMPONENT, (decl *)d});
    }
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (shard_shares(d)) client_codec_type(&g->value_codec, (type){TY_SINGLETON, (decl *)d});
    }
    for (int i = 0; i < prog->events.count; i++) {
        const decl *d = prog->events.items[i];
        if (!d->is_local && is_queued(prog, d)) client_codec_type(&g->value_codec, (type){TY_EVENT, (decl *)d});
    }
}

// Declarations the code before gen_shard_early calls.
static void gen_shard_prelude(gen *g)
{
    if (!sharded(g)) return;
    sb *o = &g->c;
    sb_printf(o, "// Shards (tide/shard.h): what this world's shard owns, and the code that waits on its border\n"
                 "#define TIDE_SHARD_FN static __attribute__((unused))\n"
                 "#define TIDE_SHARD_CODES %du\n"
                 "#define TIDE_SHARD_RUNS 8u // Runs of a tick before code that keeps touching another shard's is dropped\n"
                 "#define TIDE_SHARD_LEND 0xFFFFFFFFu // A wait for what another shard asked for, to lend it\n"
                 "TIDE_HELPER bool tide_shard_may(tide_world *w, tide_entity e);\n"
                 "TIDE_HELPER bool tide_shard_singleton_may(tide_world *w, uint32_t i);\n"
                 "TIDE_SHARD_FN tide_entity tide_shard_create(void *world);\n"
                 "TIDE_SHARD_FN void tide_shard_resume(tide_world *w);\n"
                 "TIDE_SHARD_FN void tide_shard_unloaded(tide_world *w, tide_entity scene);\n"
                 "TIDE_SHARD_FN bool tide_shard_starting_empty; // A region's world starts with nothing in it\n"
                 "TIDE_SHARD_FN int32_t tide_shard_since; // While a waiting delivery runs: the tick it first tried\n"
                 "TIDE_SHARD_FN bool tide_shard_destroy_waits(tide_world *w, tide_entity e);\n"
                 "// The entity a destroy is of, as its tree's Destroyed handlers run: what they wait by\n"
                 "TIDE_SHARD_FN TIDE_THREAD_LOCAL tide_entity tide_shard_dying;\n\n",
              shard_codes(g->prog));
}

// What the change recorders and World.Set call: whether this shard owns an
// entity, noting it when not; and the handles a shard gives its spawns.
static void gen_shard_early(gen *g)
{
    if (!sharded(g)) return;
    sb *o = &g->c;
    sb_put(o, "// ---------------------------------------------------------------------------\n"
              "// Shards: what this shard owns, and its entities' handles\n\n"
              "TIDE_HELPER int64_t tide_shard_key(const tide_entity e)\n{\n"
              "    return (int64_t)(((uint64_t)e.generation << 32) | e.index);\n}\n\n"
              "TIDE_HELPER tide_entity tide_shard_unkey(const int64_t key)\n{\n"
              "    return (tide_entity){(uint32_t)(uint64_t)key, (uint32_t)((uint64_t)key >> 32)};\n}\n\n"
              // A list of longs as records of `stride`: the first record whose first is `key`
              "TIDE_HELPER int32_t tide_shard_find(const tide_list l, const int64_t key, const int32_t stride)\n{\n"
              "    const int32_t n = tide_list_count(l);\n"
              "    for (int32_t i = 0; i + stride <= n; i += stride) {\n"
              "        if (*(const int64_t *)tide_list_at(l, i, 8u) == key) return i;\n    }\n"
              "    return -1;\n}\n\n"
              "TIDE_HELPER int64_t tide_shard_long(const tide_list l, const int32_t i)\n{\n"
              "    return *(const int64_t *)tide_list_at(l, i, 8u);\n}\n\n"
              "TIDE_HELPER void tide_shard_push(tide_list *l, const int64_t value)\n{\n"
              "    *(int64_t *)tide_list_add(l, 8u, TIDE_IN_MATCH) = value;\n}\n\n"
              "TIDE_HELPER void tide_shard_drop(tide_list *l, const int32_t at, const int32_t stride)\n{\n"
              "    for (int32_t k = 0; k < stride; k++) tide_list_remove_at(l, at, 8u);\n}\n\n"
              // A player's route among the routes: a record each, the player, the
              // home, how many it follows, then each shard and match
              "TIDE_HELPER int32_t tide_shard_route_at(const tide_list l, const int64_t player)\n{\n"
              "    const int32_t n = tide_list_count(l);\n"
              "    for (int32_t at = 0; at + 3 <= n;) {\n"
              "        if (tide_shard_long(l, at) == player) return at;\n"
              "        const int64_t count = tide_shard_long(l, at + 2);\n"
              "        if (count < 0 || count > n) return -1;\n"
              "        at += 3 + 2 * (int32_t)count;\n    }\n"
              "    return -1;\n}\n\n"
              );
    sb_put(o, "TIDE_HELPER bool tide_shard_root(const tide_world *w)\n{\n"
              "    return !w->tide_shard_state.shard || w->tide_shard_state.role == TIDE_SHARD_ROOT_ROLE;\n}\n\n"
              "TIDE_HELPER int32_t tide_shard_self(const tide_world *w)\n{\n    return w->tide_shard_state.shard - 1;\n}\n\n"
              // A singleton's mark among the entities skipped code touched
              "TIDE_HELPER bool tide_shard_is_singleton(const tide_entity e)\n{\n"
              "    return e.generation == TIDE_ENTITY_TEMPORARY;\n}\n\n"
              // Whether this shard owns `e`: an entity alive here that's no ghost,
              // or one spawned this tick; or one that's gone, which changing does nothing
              "TIDE_HELPER bool tide_shard_owned(const tide_world *w, const tide_entity e)\n{\n"
              "    if (!w->tide_shard_state.shard || tide_entity_is_null(e) || tide_entity_is_temporary(e)) return true;\n"
              "    if (tide_shard_is_singleton(e)) return (((uint64_t)w->tide_shard_state.singletons >> e.index) & 1u) != 0;\n"
              "    if (!tide_entity_alive(&w->entities, e)) {\n"
              "        return tide_entity_died_here(&w->entities, e) || tide_shard_find(w->tide_shard_state.gone, tide_shard_key(e), 1) >= 0;\n"
              "    }\n"
              "    if (tide_entity_location(&w->entities, e).archetype == TIDE_ARCHETYPE_NONE) return true; // Spawned this tick\n"
              "    return !tide_has_tide_ghost(w, e);\n}\n\n"
              "TIDE_SHARD_FN bool tide_shard_owns_world(const void *world, const tide_entity e)\n{\n"
              "    return tide_shard_owned(world, e);\n}\n\n"
              "TIDE_HELPER bool tide_shard_may(tide_world *w, const tide_entity e)\n{\n"
              "    if (tide_shard_owned(w, e)) return true;\n"
              "    tide_shard_note(w->tide_shard_now, e); // The code waits on the border: its tick runs again without it\n"
              "    return false;\n}\n\n"
              "TIDE_HELPER bool tide_shard_singleton_may(tide_world *w, const uint32_t i)\n{\n"
              "    return tide_shard_may(w, (tide_entity){i, TIDE_ENTITY_TEMPORARY});\n}\n\n"
              );
    // Indices this shard gives: its ranges, every one it was given
    sb_put(o,
              "TIDE_HELPER bool tide_shard_in_range(const tide_world *w, const uint32_t index)\n{\n"
              "    const tide_list r = w->tide_shard_state.ranges;\n"
              "    const int32_t n = tide_list_count(r);\n"
              "    for (int32_t i = 0; i + 2 <= n; i += 2) {\n"
              "        if ((int64_t)index >= tide_shard_long(r, i) && (int64_t)index < tide_shard_long(r, i + 1)) return true;\n    }\n"
              "    return false;\n}\n\n"
              "TIDE_SHARD_FN tide_entity tide_shard_create(void *world)\n{\n"
              "    tide_world *w = world;\n    tide_shard_state *s = &w->tide_shard_state;\n"
              "    if (!s->shard || w->entities.free_head) return tide_entity_create(&w->entities); // Its own, freed here\n"
              "    for (;;) {\n"
              "        if (s->next >= s->end) { // The next range it was given\n"
              "            int64_t first = INT64_MAX, end = 0;\n"
              "            const int32_t n = tide_list_count(s->ranges);\n"
              "            for (int32_t i = 0; i + 2 <= n; i += 2) {\n"
              "                const int64_t f = tide_shard_long(s->ranges, i);\n"
              "                if (f >= s->end && f < first) { first = f; end = tide_shard_long(s->ranges, i + 1); }\n"
              "            }\n"
              "            if (first == INT64_MAX) {\n"
              "                fprintf(stderr, \"tide: shard %d has no entity indices left to give what it spawns\\n\", (int)s->shard - 1);\n"
              "                abort();\n            }\n"
              "            s->next = first;\n            s->end = end;\n        }\n"
              "        const tide_entity e = tide_entity_create_at(&w->entities, (uint32_t)s->next++);\n"
              "        if (!tide_entity_is_null(e)) return e;\n    }\n}\n\n"
              );
    // A slot destroyed here: listed free when its index is this shard's to give again
    sb_put(o,
              "TIDE_HELPER void tide_shard_free_slot(tide_world *w, const tide_entity e)\n{\n"
              "    if (!w->tide_shard_state.shard || tide_shard_in_range(w, e.index)) tide_entity_destroy(&w->entities, e);\n"
              "    else tide_entity_forget(&w->entities, e, false);\n}\n\n");
}

// Where a system starts: it runs for what this shard owns, and code for no
// entity runs on the root alone; a row whose last run touched another shard's
// waits; and a singleton it changes has to be this shard's.
static void gen_shard_system_entry(gen *g, const decl *sys)
{
    if (!sharded(g) || sys->is_view || sys->is_local) return;
    const program *prog = g->prog;
    sb *o = &g->c;
    bool singletons = false;
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        singletons |= p->type.kind == TY_SINGLETON && p->mode == PARAM_MUT && shard_shares(p->type.decl);
    }
    if (sys->is_handler && !singletons) return; // A handler's delivery is what waits (gen_shard_dispatch_entry)
    line(g, o, "if (tide_w->tide_shard_state.shard) {");
    g->indent++;
    if (!sys->is_handler) {
        if (!sys->per_entity) {
            line(g, o, "if (tide_w->tide_shard_state.role != TIDE_SHARD_ROOT_ROLE) return; // Code for no entity runs once, on the root");
        }
        line(g, o, "tide_shard_code = %du;", shard_code_system(sys));
        line(g, o, "tide_shard_this = %s;", sys->per_entity ? "tide_this" : "(tide_entity){0}");
        line(g, o, "tide_shard_occurrence = 0u;");
        line(g, o, "if (tide_shard_skips(tide_w->tide_shard_now, tide_shard_code, tide_shard_this, 0u)) return; // It waits on the border");
    }
    for (int i = 0; i < sys->params.count; i++) {
        const param *p = &sys->params.items[i];
        if (p->type.kind != TY_SINGLETON || p->mode != PARAM_MUT || !shard_shares(p->type.decl)) continue;
        line(g, o, "if (!tide_shard_singleton_may(tide_w, %du)) return; // " STR_FMT " is the root's", shard_singleton(prog, p->type.decl),
             STR_ARG(p->type.decl->name));
    }
    g->indent--;
    line(g, o, "}");
}

// Where an event's delivery starts: to another shard's entity, it goes there;
// one whose last run touched another shard's waits, with its event.
static void gen_shard_dispatch_entry(gen *g, const decl *event)
{
    if (!sharded(g) || event->is_local) return;
    const program *prog = g->prog;
    sb *o = &g->c;
    const char *name = type_cname(event);
    const char *sender = event->is_command ? "tide_sender" : "NULL";
    sb_put(o, "    if (tide_w->tide_shard_state.shard) {\n");
    if (is_queued(prog, event)) {
        sb_printf(o, "        if (!tide_entity_is_null(tide_target) && !tide_shard_owned(tide_w, tide_target)) {\n"
                     "            tide_shard_forward_%s(tide_w, tide_target, tide_event, %s); // Another shard's: it runs there\n"
                     "            return;\n        }\n", name, sender);
    }
    if (event == prog->destroyed) { // Its whole destroy waits (tide_shard_destroy_waits), by the entity it's of
        sb_printf(o, "        tide_shard_code = tide_entity_is_null(tide_shard_dying) ? 0u : %du;\n"
                     "        tide_shard_this = tide_shard_dying;\n        tide_shard_occurrence = 0u;\n    }\n",
                  shard_code_event(prog, event));
        return;
    }
    if (!shard_parks(prog, event)) {
        sb_put(o, "        tide_shard_code = 0u; // It can't wait: what it changes across a border is dropped\n    }\n");
        return;
    }
    sb_printf(o, "        tide_shard_code = %du;\n        tide_shard_this = tide_target;\n"
                 "        tide_shard_occurrence = tide_w->tide_shard_now ? tide_w->tide_shard_now->occurrences[%d]++ : 0u;\n"
                 "        if (tide_shard_skips(tide_w->tide_shard_now, tide_shard_code, tide_shard_this, tide_shard_occurrence)) {\n"
                 "            tide_shard_park_%s(tide_w, tide_target, tide_event, %s); // It waits on the border\n"
                 "            return;\n        }\n    }\n",
              shard_code_event(prog, event), shard_code_event(prog, event), name, sender);
}

// ---------------------------------------------------------------------------
// The rest: after the codecs and the dispatchers

// The position deciding an entity's shard: its [Partition] field, or its
// Transform's, for an entity in no hierarchy (one in one goes with its root).
static uint32_t shard_dims(const program *prog)
{
    if (!prog->partition) return 3;
    const type t = prog->partition->fields.items[prog->partition_field].type;
    return t.kind == TY_FLOAT2 || t.kind == TY_DOUBLE2 ? 2u : 3u;
}

static bool shard_uses(const program *prog, const decl *component)
{
    return component && component->index >= 0 && component->index < prog->components.count
        && prog->components.items[component->index] == component;
}

static void gen_shard_where(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    sb_put(o, "// Where an entity is, for which shard owns it: false for one that has no position of its own\n"
              "TIDE_HELPER bool tide_shard_where(const tide_world *w, const tide_entity e, double p[3])\n{\n"
              "    p[0] = p[1] = p[2] = 0.0;\n");
    if (prog->hierarchy[0]) sb_put(o, "    if (!tide_entity_is_null(tide_parent_of(w, e))) return false; // It goes with its root\n");
    if (prog->partition) {
        const char *comp = type_cname(prog->partition);
        const field *f = &prog->partition->fields.items[prog->partition_field];
        sb_printf(o, "    if (!tide_has_%s(w, e)) return false;\n    const %s v = tide_get_%s(w, e);\n", comp, comp, comp);
        for (uint32_t k = 0; k < shard_dims(prog); k++) sb_printf(o, "    p[%u] = (double)v.%s.%s;\n", k, field_cname(f), xyzw[k]);
        sb_put(o, "    return true;\n}\n\n");
    } else if (shard_uses(prog, prog->transform)) {
        sb_put(o, "    if (!tide_has_Transform(w, e)) return false;\n    const Transform t = tide_get_Transform(w, e);\n"
                  "    p[0] = (double)t.localPosition.x;\n    p[1] = (double)t.localPosition.y;\n    p[2] = (double)t.localPosition.z;\n"
                  "    return true;\n}\n\n");
    } else {
        sb_put(o, "    (void)w;\n    (void)e;\n    return false; // Nothing has a position: everything stays where it's spawned\n}\n\n");
    }
    // The regions, 7 doubles each: the shard, then its box's lo and hi
    sb_printf(o, "TIDE_HELPER bool tide_shard_holds(const tide_world *w, const int32_t at, const double p[3], const double margin)\n{\n"
                 "    const tide_list r = w->tide_shard_state.regions;\n"
                 "    for (uint32_t k = 0; k < %uu; k++) {\n"
                 "        const double lo = *(const double *)tide_list_at(r, at + 1 + (int32_t)k, 8u);\n"
                 "        const double hi = *(const double *)tide_list_at(r, at + 4 + (int32_t)k, 8u);\n"
                 "        if (!(p[k] >= lo - margin && p[k] < hi + margin)) return false;\n    }\n"
                 "    return true;\n}\n\n"
                 "// The shard whose region holds a position, or -1\n"
                 "TIDE_HELPER int32_t tide_shard_region_of(const tide_world *w, const double p[3])\n{\n"
                 "    const int32_t n = tide_list_count(w->tide_shard_state.regions);\n"
                 "    for (int32_t at = 0; at + 7 <= n; at += 7) {\n"
                 "        if (tide_shard_holds(w, at, p, 0.0)) return (int32_t)*(const double *)tide_list_at(w->tide_shard_state.regions, at, 8u);\n"
                 "    }\n    return -1;\n}\n\n"
                 "// Whether this shard's own region, grown by `margin`, holds a position\n"
                 "TIDE_HELPER bool tide_shard_mine(const tide_world *w, const double p[3], const double margin)\n{\n"
                 "    const int32_t n = tide_list_count(w->tide_shard_state.regions);\n"
                 "    for (int32_t at = 0; at + 7 <= n; at += 7) {\n"
                 "        if ((int32_t)*(const double *)tide_list_at(w->tide_shard_state.regions, at, 8u) == tide_shard_self(w)\n"
                 "            && tide_shard_holds(w, at, p, margin)) return true;\n    }\n"
                 "    return false;\n}\n\n", shard_dims(prog));
}

// An entity as a record: its size, its handle, archetype and snaps, its scene,
// each of its components (a sparse one after whether it's there), and its
// place in a hierarchy. A tree is its root's records and its descendants'.
static void gen_shard_records(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const client_codec *codec = &g->value_codec;
    const int ghost = prog->shard_ghost->index;
    sb_put(o, "TIDE_HELPER void tide_shard_patch(tide_writer *out, const uint32_t at, const uint32_t value)\n{\n"
              "    if (out->overflow || at + 4u > out->size) return;\n"
              "    for (uint32_t k = 0; k < 4u; k++) out->data[at + k] = (uint8_t)(value >> (8u * k));\n}\n\n");
    sb_put(o, "TIDE_HELPER void tide_shard_put_one(tide_writer *out, const tide_world *w, const tide_entity e)\n{\n"
              "    const tide_location loc = tide_entity_location(&w->entities, e);\n"
              "    const uint32_t at = out->size;\n    tide_write_u32(out, 0u); // Its size, once it's written\n"
              "    tide_write_u32(out, e.index);\n    tide_write_u32(out, e.generation);\n    tide_write_u32(out, loc.archetype);\n"
              "    tide_write_u32(out, tide_entity_snaps(&w->entities, e));\n"
              "    switch (loc.archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    case %d: {\n", a);
        if (arch_scene_column(g, a)) {
            sb_printf(o, "        const tide_entity scene = *%s;\n        tide_write_u32(out, scene.index);\n        tide_write_u32(out, scene.generation);\n",
                      arch_cell(g, "w", a, 1, "tide_entity", "loc.row", false));
        }
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c) || c == ghost) continue;
            const decl *d = prog->components.items[c];
            const int id = client_codec_id(codec, (type){TY_COMPONENT, (decl *)d});
            if (comp_sparse(g, c)) {
                sb_printf(o, "        if (%s) {\n            tide_write_u8(out, 1u);\n            %s v;\n            %s;\n"
                             "            tide_client_put%d(out, &v, 0);\n        } else tide_write_u8(out, 0u);\n",
                          presence_cell(g, "w", a, c, "loc.row", false), type_cname(d), comp_take(g, "w", a, c, "loc.row", "v"), id);
            } else {
                sb_printf(o, "        {\n            %s v;\n            %s;\n            tide_client_put%d(out, &v, 0);\n        }\n",
                          type_cname(d), comp_take(g, "w", a, c, "loc.row", "v"), id);
            }
        }
        sb_put(o, "        break;\n    }\n");
    }
    sb_put(o, "    default: break;\n    }\n");
    if (prog->hierarchy[0]) {
        sb_put(o, "    const tide_hierarchy_node node = tide_hierarchy_get(w, e);\n"
                  "    const tide_entity links[4] = {node.parent, node.first, node.next, node.previous};\n"
                  "    for (uint32_t k = 0; k < 4u; k++) { tide_write_u32(out, links[k].index); tide_write_u32(out, links[k].generation); }\n");
    }
    sb_put(o, "    tide_shard_patch(out, at, out->size - at - 4u);\n}\n\n");

    // A tree: how many, then the records, the root's first
    sb_put(o, "TIDE_HELPER void tide_shard_put_tree(tide_writer *out, const tide_world *w, const tide_entity root)\n{\n"
              "    const uint32_t at = out->size;\n    tide_write_u32(out, 0u);\n    uint32_t count = 0;\n");
    if (prog->hierarchy[0]) {
        sb_put(o, "    tide_entity e = root;\n    uint64_t left = (uint64_t)w->entities.next_unused + 1u;\n"
                  "    while (!tide_entity_is_null(e) && left--) {\n"
                  "        tide_shard_put_one(out, w, e);\n        count++;\n"
                  "        tide_hierarchy_node n = tide_hierarchy_get(w, e);\n"
                  "        if (!tide_entity_is_null(n.first)) { e = n.first; continue; }\n"
                  "        while (!tide_entity_equal(e, root) && tide_entity_is_null(n.next)) {\n"
                  "            e = n.parent;\n            n = tide_hierarchy_get(w, e);\n        }\n"
                  "        e = tide_entity_equal(e, root) ? (tide_entity){0} : n.next;\n    }\n");
    } else {
        sb_put(o, "    tide_shard_put_one(out, w, root);\n    count++;\n");
    }
    sb_put(o, "    tide_shard_patch(out, at, count);\n}\n\n");

    // Taking one out of the world without anything happening to it: no
    // handler runs, its text goes, and its slot is forgotten (`away`: it lives
    // on elsewhere) or freed
    sb_put(o, "TIDE_HELPER void tide_shard_drop_row(tide_world *w, const tide_entity e)\n{\n"
              "    const tide_location loc = tide_entity_location(&w->entities, e);\n    switch (loc.archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    case %d:", a);
        for (int c = 0; c < prog->components.count; c++) {
            const decl *comp = prog->components.items[c];
            if (!arch_stores(g, a, c) || !decl_has_text(comp)) continue;
            if (comp_sparse(g, c)) sb_printf(o, " if (%s)", presence_cell(g, "w", a, c, "loc.row", false));
            sb_printf(o, " { %s tide_old; %s; tide_release_%s(&tide_old, TIDE_IN_MATCH); }", type_cname(comp),
                      comp_take(g, "w", a, c, "loc.row", "tide_old"), type_cname(comp));
        }
        sb_printf(o, " tide_table_remove(&w->%s, %s, loc.row, &w->entities, %d); break;\n", arch_name(g, a), arch_columns(g, a), a);
    }
    sb_put(o, "    default: break;\n    }\n}\n\n");
    sb_put(o, "TIDE_HELPER void tide_shard_remove_one(tide_world *w, const tide_entity e, const bool away)\n{\n"
              "    if (!tide_entity_alive(&w->entities, e)) return;\n    tide_shard_drop_row(w, e);\n");
    if (prog->hierarchy[0]) sb_put(o, "    tide_hierarchy_put(w, e, (tide_hierarchy_node){0});\n");
    sb_put(o, "    tide_entity_forget(&w->entities, e, away);\n}\n\n");
    // ...and a whole tree, its descendants first
    sb_put(o, "TIDE_HELPER void tide_shard_remove_tree(tide_world *w, const tide_entity root, const bool away)\n{\n");
    if (prog->hierarchy[0]) {
        sb_put(o, "    uint64_t left = (uint64_t)w->entities.next_unused + 1u;\n"
                  "    while (left-- && tide_entity_alive(&w->entities, root)) {\n"
                  "        tide_entity e = root;\n"
                  "        for (tide_hierarchy_node n = tide_hierarchy_get(w, e); !tide_entity_is_null(n.first) && tide_entity_alive(&w->entities, n.first);\n"
                  "             n = tide_hierarchy_get(w, e)) e = n.first;\n"
                  "        tide_hierarchy_unlink(w, e);\n        tide_shard_remove_one(w, e, away);\n    }\n}\n\n");
    } else {
        sb_put(o, "    tide_shard_remove_one(w, root, away);\n}\n\n");
    }

    // Taking a record into the world: the entity here as another shard has it,
    // a ghost of `source` or this shard's own. False for bytes that aren't one,
    // changing nothing. One this shard owns isn't overwritten by a ghost of it.
    sb_put(o, "TIDE_HELPER bool tide_shard_take_one(tide_world *w, tide_reader *in, const bool ghost, const int32_t source)\n{\n"
              "    const uint32_t size = tide_read_u32(in);\n"
              "    if (in->failed || size > in->size - in->at) return false;\n"
              "    tide_reader r = {in->data + in->at, size, 0, false};\n    in->at += size;\n"
              "    tide_entity e;\n    e.index = tide_read_u32(&r);\n    e.generation = tide_read_u32(&r);\n"
              "    const uint32_t archetype = tide_read_u32(&r);\n    const uint32_t snaps = tide_read_u32(&r);\n"
              "    if (r.failed || !(e.generation & 1u) || (e.generation & TIDE_ENTITY_TEMPORARY) || e.index == UINT32_MAX) return false;\n"
              "    if (ghost && tide_entity_alive(&w->entities, e) && !tide_has_tide_ghost(w, e)\n"
              "        && tide_entity_location(&w->entities, e).archetype != TIDE_ARCHETYPE_NONE) return true; // This shard's own\n"
              "    uint64_t budget = SIZE_MAX;\n    (void)budget;\n"
              "    switch (archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    case %d: {\n", a);
        if (arch_scene_column(g, a)) {
            sb_put(o, "        tide_entity scene;\n        scene.index = tide_read_u32(&r);\n        scene.generation = tide_read_u32(&r);\n");
        }
        // Read every value first, so bytes that aren't a record change nothing
        sb release = {0};
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c) || c == ghost) continue;
            const decl *d = prog->components.items[c];
            const int id = client_codec_id(codec, (type){TY_COMPONENT, (decl *)d});
            sb_printf(o, "        %s v%d;\n        memset(&v%d, 0, sizeof v%d);\n        uint8_t p%d = 1u;\n", type_cname(d), c, c, c, c);
            if (comp_sparse(g, c)) sb_printf(o, "        p%d = tide_read_u8(&r);\n        if (p%d > 1u) r.failed = true;\n", c, c);
            sb_printf(o, "        if (p%d && !r.failed && !tide_client_get%d(&r, &v%d, TIDE_IN_MATCH, 0, &budget)) r.failed = true;\n", c, id, c);
            if (decl_has_text(d)) sb_printf(&release, " tide_release_%s(&v%d, TIDE_IN_MATCH);", type_cname(d), c);
        }
        if (prog->hierarchy[0]) {
            sb_put(o, "        tide_hierarchy_node node;\n        tide_entity *const links[4] = {&node.parent, &node.first, &node.next, &node.previous};\n"
                      "        for (uint32_t k = 0; k < 4u; k++) { links[k]->index = tide_read_u32(&r); links[k]->generation = tide_read_u32(&r); }\n");
        }
        sb_printf(o, "        if (r.failed || r.at != r.size) {%s return false; }\n", release.data ? release.data : "");
        // Its slot: this entity here already, or made it (another in it goes first)
        sb_printf(o, "        tide_location loc = tide_entity_location(&w->entities, e);\n"
                     "        if (loc.archetype != TIDE_ARCHETYPE_NONE && loc.archetype != %du) {\n"
                     "            tide_shard_drop_row(w, e);\n            loc.archetype = TIDE_ARCHETYPE_NONE;\n        }\n"
                     "        if (!tide_entity_alive(&w->entities, e)) {\n"
                     "            const tide_entity there = tide_entity_in_slot(&w->entities, e.index);\n"
                     "            if (!tide_entity_is_null(there)) tide_shard_remove_one(w, there, true);\n"
                     "            if (!tide_entity_install(&w->entities, e)) {%s return false; }\n        }\n"
                     "        uint32_t row = loc.row;\n", a, release.data ? release.data : "");
        sb_printf(o, "        if (loc.archetype == %du) {\n", a);
        for (int c = 0; c < prog->components.count; c++) { // The values it had go
            const decl *comp = prog->components.items[c];
            if (!arch_stores(g, a, c) || c == ghost || !decl_has_text(comp)) continue;
            if (comp_sparse(g, c)) sb_printf(o, "            if (%s)", presence_cell(g, "w", a, c, "row", false));
            else sb_put(o, "           ");
            sb_printf(o, " { %s tide_old; %s; tide_release_%s(&tide_old, TIDE_IN_MATCH); }\n", type_cname(comp),
                      comp_take(g, "w", a, c, "row", "tide_old"), type_cname(comp));
        }
        sb_printf(o, "        } else {\n            row = tide_table_add(&w->%s, %s);\n            *%s = e;\n"
                     "            tide_entity_set_location(&w->entities, e, (tide_location){%du, row});\n        }\n",
                  arch_name(g, a), arch_columns(g, a), arch_cell(g, "w", a, 0, "tide_entity", "row", true), a);
        if (arch_scene_column(g, a)) sb_printf(o, "        *%s = scene;\n", arch_cell(g, "w", a, 1, "tide_entity", "row", true));
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c) || c == ghost) continue;
            if (comp_sparse(g, c)) sb_printf(o, "        %s = p%d;\n", presence_cell(g, "w", a, c, "row", true), c);
            sb value = {0};
            sb_printf(&value, "v%d", c);
            sb_printf(o, "        %s;\n", comp_put(g, "w", a, c, "row", value.data));
        }
        sb_printf(o, "        %s = ghost ? 1u : 0u;\n        tide_ghost gv = {ghost ? source : 0};\n        %s;\n",
                  presence_cell(g, "w", a, ghost, "row", true), comp_put(g, "w", a, ghost, "row", "gv"));
        if (prog->hierarchy[0]) sb_put(o, "        tide_hierarchy_put(w, e, node);\n");
        sb_put(o, "        tide_entity_set_snaps(&w->entities, e, snaps);\n        return true;\n    }\n");
    }
    sb_put(o, "    default: return false;\n    }\n}\n\n");

    sb_put(o, "TIDE_HELPER bool tide_shard_take_tree(tide_world *w, tide_reader *in, const bool ghost, const int32_t source,\n"
              "                                      tide_entity *root)\n{\n"
              "    const uint32_t count = tide_read_u32(in);\n"
              "    if (in->failed || !count || count > (in->size - in->at) / 20u) return false;\n"
              "    for (uint32_t i = 0; i < count; i++) {\n"
              "        if (i == 0 && root) { // Its handle, ahead of reading it\n"
              "            tide_reader peek = *in;\n            tide_read_u32(&peek);\n"
              "            root->index = tide_read_u32(&peek);\n            root->generation = tide_read_u32(&peek);\n        }\n"
              "        if (!tide_shard_take_one(w, in, ghost, source)) return false;\n    }\n"
              "    return true;\n}\n\n");
    // An entity becoming a ghost of `source` (it went there, and what's near
    // here is copies), or this shard's own again
    sb_put(o, "TIDE_HELPER void tide_shard_mark_one(tide_world *w, const tide_entity e, const bool ghost, const int32_t source)\n{\n"
              "    const tide_location loc = tide_entity_location(&w->entities, e);\n"
              "    const tide_ghost gv = {ghost ? source : 0};\n    (void)gv;\n    switch (loc.archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    case %d: %s = ghost ? 1u : 0u; %s; break;\n", a, presence_cell(g, "w", a, ghost, "loc.row", true),
                  comp_put(g, "w", a, ghost, "loc.row", "gv"));
    }
    sb_put(o, "    default: break;\n    }\n}\n\n");
    sb_put(o, "TIDE_HELPER void tide_shard_mark_tree(tide_world *w, const tide_entity root, const bool ghost, const int32_t source)\n{\n");
    if (prog->hierarchy[0]) {
        sb_put(o, "    tide_entity e = root;\n    uint64_t left = (uint64_t)w->entities.next_unused + 1u;\n"
                  "    while (!tide_entity_is_null(e) && left--) {\n"
                  "        tide_shard_mark_one(w, e, ghost, source);\n"
                  "        tide_hierarchy_node n = tide_hierarchy_get(w, e);\n"
                  "        if (!tide_entity_is_null(n.first)) { e = n.first; continue; }\n"
                  "        while (!tide_entity_equal(e, root) && tide_entity_is_null(n.next)) {\n"
                  "            e = n.parent;\n            n = tide_hierarchy_get(w, e);\n        }\n"
                  "        e = tide_entity_equal(e, root) ? (tide_entity){0} : n.next;\n    }\n}\n\n");
    } else {
        sb_put(o, "    tide_shard_mark_one(w, root, ghost, source);\n}\n\n");
    }
}
