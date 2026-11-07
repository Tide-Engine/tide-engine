// Prediction near this player alone (`settings { predictionRange = 40; }`):
// tide_game.predict_far, which a client calls on the world a predicted tick
// starts from (notes/netcode.md, Predicting near the player alone). Included
// by codegen.c.

// Where archetype `a`'s rows have their position, the first of what
// tide_shard_where looks for that they have: a [Position] field's component
// index (its field in *field), the Transform's index, FAR_NOWHERE for rows
// with none, or FAR_LOOKUP where it depends on the row (a sparse component
// that may or may not be there), which goes through tide_shard_where.
#define FAR_NOWHERE (-1)
#define FAR_LOOKUP (-2)

static int far_source(const gen *g, const int a, int *field)
{
    const program *prog = g->prog;
    const comp_mask mask = prog->archetypes.items[a];
    for (int i = 0; shard_position(prog, i); i++) {
        const struct field_mark *m = shard_position(prog, i);
        if (!shard_uses(prog, m->component)) continue;
        const int c = m->component->index;
        if (has_component(mask, c) && !comp_sparse(g, c)) {
            *field = m->field;
            return c;
        }
        if (arch_stores(g, a, c)) return FAR_LOOKUP;
    }
    if (shard_uses(prog, prog->transform)) {
        const int c = prog->transform->index;
        if (has_component(mask, c) && !comp_sparse(g, c)) return c;
        if (arch_stores(g, a, c)) return FAR_LOOKUP;
    }
    return FAR_NOWHERE;
}

// The start of a loop over archetype `a`'s rows chunk by chunk, with `ent`,
// `i` and, where its rows have a position of their own, `p` and `placed`:
// read from its lanes, a child in a hierarchy by its root's (tide_far_where).
// The caller closes both loops.
static void gen_far_rows(gen *g, const int a, const char *pad)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    int field = 0;
    const int source = far_source(g, a, &field);
    sb_printf(o, "%s{\n%s    tide_table *const t = (tide_table *)(uintptr_t)&w->%s;\n%s    const tide_columns *const cols = %s;\n", pad, pad,
              arch_name(g, a), pad, arch_columns(g, a));
    sb_printf(o, "%s    for (uint32_t c = 0; c < t->chunks; c++) {\n%s        const uint32_t n = tide_table_rows(t, %d, c);\n"
                 "%s        const tide_entity *const ent = tide_table_column(t, cols, c, 0);\n", pad, pad, arch_shift(g, a), pad);
    if (source >= 0) {
        const decl *d = prog->components.items[source];
        sb_printf(o, "%s        const uint8_t *const at[%d] = {", pad, comp_lanes(d));
        for (int k = 0; k < comp_lanes(d); k++) {
            sb_printf(o, "%stide_table_column(t, cols, c, %d)", k ? ", " : "", arch_column(g, a, source) + k);
        }
        sb_put(o, "};\n");
    }
    sb_printf(o, "%s        for (uint32_t i = 0; i < n; i++) {\n%s            if (tide_row_gone(&ent[i])) continue; // A hole (tide_table_remove)\n"
                 "%s            double p[3] = {0.0, 0.0, 0.0};\n%s            bool placed = false;\n", pad, pad, pad, pad);
    if (source == FAR_NOWHERE) {
        sb_printf(o, "%s            (void)p;\n", pad);
        if (prog->hierarchy[0]) sb_printf(o, "%s            placed = tide_far_where(w, ent[i], p);\n", pad);
        return;
    }
    if (source == FAR_LOOKUP) {
        sb_printf(o, "%s            placed = tide_far_where(w, ent[i], p);\n", pad);
        return;
    }
    const char *in = pad;
    if (prog->hierarchy[0]) {
        sb_printf(o, "%s            if (!tide_entity_is_null(tide_parent_of(w, ent[i]))) placed = tide_far_where(w, ent[i], p);\n"
                     "%s            else {\n", pad, pad);
    }
    const decl *d = prog->components.items[source];
    sb_printf(o, "%s            %s v;\n%s            tide_gather_%s(&v, at, i);\n", in, type_cname(d), in, type_cname(d));
    const bool transform = d == prog->transform;
    const char *name = transform ? "localPosition" : field_cname(&d->fields.items[field]);
    const uint32_t dims = transform ? shard_dims(prog) : (d->fields.items[field].type.kind == TY_FLOAT2
                                                          || d->fields.items[field].type.kind == TY_DOUBLE2 ? 2u : 3u);
    for (uint32_t k = 0; k < dims && k < shard_dims(prog); k++) sb_printf(o, "%s            p[%u] = (double)v.%s.%s;\n", in, k, name, xyzw[k]);
    sb_printf(o, "%s            placed = true;\n", in);
    if (prog->hierarchy[0]) sb_printf(o, "%s            }\n", pad);
}

// Whether one component's value in two worlds is the same but for its
// [Native] fields, which a package's C keeps (a physics world's handles and
// stamps): those differ between a world whose step held far bodies and one
// that stepped them all, whatever the match says.
static void gen_near_same_value(gen *g, const decl *d, const char *a, const char *b, const char *pad)
{
    sb *o = &g->c;
    for (int k = 0; k < d->fields.count; k++) {
        const field *f = &d->fields.items[k];
        if (f->native) continue;
        sb_printf(o, "%sif (memcmp(&%s.%s, &%s.%s, sizeof %s.%s) != 0) return false;\n", pad, a, field_cname(f), b,
                  field_cname(f), a, field_cname(f));
    }
}

// tide_game_near_same(predicted, confirmed): whether the confirmed world is the
// predicted one of the same tick but for its far rows (notes/netcode.md,
// Predicting near the player alone). Pages both share from the world before
// are the same without a look; the rest is compared, rows that are far in the
// predicted world left out. Anything else that differs, an entity made or
// destroyed, a list, a command, says they aren't.
static void gen_near_same(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const int far = prog->predict_far->index;
    sb_put(o, "static bool tide_game_near_same(const void *predicted_world, const void *confirmed_world)\n{\n"
              "    const tide_world *p = predicted_world, *q = confirmed_world;\n");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (d->is_local) continue;
        char a[256], b[256];
        snprintf(a, sizeof a, "p->%s", type_cname(d));
        snprintf(b, sizeof b, "q->%s", type_cname(d));
        gen_near_same_value(g, d, a, b, "    ");
    }
    sb_put(o, "    if (tide_entities_hash(TIDE_HASH_START, &p->entities) != tide_entities_hash(TIDE_HASH_START, &q->entities)) return false;\n"
              "    if (tide_queue_hash(TIDE_HASH_START, &p->commands) != tide_queue_hash(TIDE_HASH_START, &q->commands)) return false;\n");
    for (int i = 0; i < g->tasks.count; i++) {
        if (!task_table_in(g->tasks.items[i].fn, false)) continue;
        const char *id = task_id(g->tasks.items[i].fn);
        sb_printf(o, "    if (tide_table_hash(TIDE_HASH_START, &p->tide_tasks_%s, &tide_columns_tasks_%s)\n"
                     "        != tide_table_hash(TIDE_HASH_START, &q->tide_tasks_%s, &tide_columns_tasks_%s)) return false;\n", id, id, id, id);
    }
    if (world_has_tasks(g, false)) sb_put(o, "    if (memcmp(&p->tide_task_order, &q->tide_task_order, sizeof p->tide_task_order) != 0) return false;\n");
    if (prog->input) {
        sb_put(o, "    if (memcmp(p->inputs, q->inputs, sizeof p->inputs) != 0) return false;\n"
                  "    if (memcmp(p->previous_inputs, q->previous_inputs, sizeof p->previous_inputs) != 0) return false;\n");
    }
    if (prog->uses_heap) sb_put(o, "    if (tide_heap_hash(TIDE_HASH_START, &p->heap) != tide_heap_hash(TIDE_HASH_START, &q->heap)) return false;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const char *cols = arch_columns(g, a);
        sb_printf(o, "    {\n        const tide_table *const s = &p->%s, *const t = &q->%s;\n"
                     "        if (s->chunks != t->chunks) return false;\n"
                     "        for (uint32_t c = 0; c < s->chunks; c++) {\n"
                     "            const uint32_t n = tide_table_rows(s, %d, c);\n"
                     "            if (n != tide_table_rows(t, %d, c)) return false;\n"
                     "            const tide_entity *const ent = tide_table_column(s, %s, c, 0);\n"
                     "            const tide_entity *const ent2 = tide_table_column(t, %s, c, 0);\n"
                     "            if (ent != ent2 && memcmp(ent, ent2, n * sizeof *ent) != 0) return false;\n",
                  arch_name(g, a), arch_name(g, a), arch_shift(g, a), arch_shift(g, a), cols, cols);
        const bool tagged = arch_stores(g, a, far);
        if (tagged) sb_printf(o, "            const uint8_t *const tags = tide_table_column(s, %s, c, %d);\n", cols, arch_presence(g, a, far));
        const char *skip = tagged ? "tide_row_gone(&ent[i]) || tags[i]" : "tide_row_gone(&ent[i])";
        if (arch_scene_column(g, a)) {
            sb_printf(o, "            {\n                const tide_entity *const x = tide_table_column(s, %s, c, 1), *const y = tide_table_column(t, %s, c, 1);\n"
                         "                for (uint32_t i = 0; x != y && i < n; i++) {\n"
                         "                    if (%s) continue;\n"
                         "                    if (memcmp(&x[i], &y[i], sizeof x[i]) != 0) return false;\n"
                         "                }\n            }\n", cols, cols, skip);
        }
        for (int k = 0; k < prog->components.count; k++) {
            if (k == far || !arch_stores(g, a, k)) continue;
            const decl *d = prog->components.items[k];
            if (d->is_local) continue;
            const bool sparse = comp_sparse(g, k);
            const int lanes = comp_lanes(d);
            sb_put(o, "            {\n");
            if (sparse) {
                sb_printf(o, "                const uint8_t *const here = tide_table_column(s, %s, c, %d);\n"
                             "                const uint8_t *const there = tide_table_column(t, %s, c, %d);\n",
                          cols, arch_presence(g, a, k), cols, arch_presence(g, a, k));
            }
            if (lanes > 0) {
                sb_printf(o, "                const uint8_t *const x[%d] = {", lanes);
                for (int l = 0; l < lanes; l++) sb_printf(o, "%stide_table_column(s, %s, c, %d)", l ? ", " : "", cols, arch_column(g, a, k) + l);
                sb_printf(o, "};\n                const uint8_t *const y[%d] = {", lanes);
                for (int l = 0; l < lanes; l++) sb_printf(o, "%stide_table_column(t, %s, c, %d)", l ? ", " : "", cols, arch_column(g, a, k) + l);
                sb_put(o, "};\n                bool shared = true;\n");
                sb_printf(o, "                for (int l = 0; l < %d; l++) shared &= x[l] == y[l];\n", lanes);
            } else {
                sb_put(o, "                const bool shared = true;\n");
            }
            sb_printf(o, "                if (%sshared) goto next_%d_%d;\n", sparse ? "here == there && " : "", a, k);
            if (!sparse && lanes == 0) sb_printf(o, "                (void)n;\n");
            sb_put(o, "                for (uint32_t i = 0; i < n; i++) {\n");
            sb_printf(o, "                    if (%s) continue;\n", skip);
            if (sparse) sb_put(o, "                    if (here[i] != there[i]) return false;\n                    if (!here[i]) continue;\n");
            if (lanes > 0) {
                sb_printf(o, "                    %s u, v;\n                    tide_gather_%s(&u, x, i);\n                    tide_gather_%s(&v, y, i);\n",
                          type_cname(d), type_cname(d), type_cname(d));
                gen_near_same_value(g, d, "u", "v", "                    ");
            }
            sb_printf(o, "                }\n            }\n            next_%d_%d:;\n", a, k);
        }
        sb_put(o, "        }\n    }\n");
    }
    sb_put(o, "    return true;\n}\n\n");
}

// tide_game_predict_far(world, player): tags each entity with a position
// farther than predictionRange from every one of the player's own (Owner) as
// tide_far, and takes the tag from the rest. Where an entity is goes by its
// root's position (tide_shard_where), as shards go by it. A player who owns
// nothing with a position has nothing to be near: nothing is tagged. True if
// anything is. Positions are read from their lanes, chunk by chunk; a
// chunk's tags are written (its page made its own) only where one changes.
static void gen_predict_far(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    if (!predicts_near(g)) return;
    if (prog->owned_prediction) {
        sb_put(o, "TIDE_HELPER bool tide_predict_owned(const tide_world *w, tide_entity e, tide_player_id player)\n{\n"
                  "    (void)w; (void)e; (void)player;\n");
        if (prog->owner) {
            const decl *owner = prog->owner;
            sb_put(o, "    for (uint32_t depth = 0; depth < 4096u; depth++) {\n");
            sb_printf(o, "        if (tide_has_%s(w, e)) return tide_player_equal(tide_get_%s(w, e).%s, player);\n",
                      type_cname(owner), type_cname(owner), field_cname(&owner->fields.items[0]));
            if (prog->hierarchy[0]) {
                sb_put(o, "        e = tide_parent_of(w, e);\n        if (tide_entity_is_null(e)) break;\n");
            } else sb_put(o, "        break;\n");
            sb_put(o, "    }\n");
        }
        sb_put(o, "    return false;\n}\n\nstatic bool tide_game_predict_far(void *world, const tide_player_id player)\n{\n"
                  "    tide_world *w = world;\n    bool any = false;\n    (void)w; (void)player;\n");
        const int far = prog->predict_far->index;
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a) || !arch_stores(g, a, far)) continue;
            sb_printf(o, "    {\n        tide_table *t = &w->%s;\n        const tide_columns *cols = %s;\n"
                         "        for (uint32_t c = 0; c < t->chunks; c++) {\n"
                         "            const uint32_t n = tide_table_rows(t, %d, c);\n"
                         "            const tide_entity *ent = tide_table_column(t, cols, c, 0);\n"
                         "            for (uint32_t i = 0; i < n; i++) {\n"
                         "                if (tide_row_gone(&ent[i])) continue;\n"
                         "                const bool is_far = !tide_predict_owned(w, ent[i], player);\n"
                         "                any |= is_far;\n"
                         "                const uint8_t *tags = tide_table_column(t, cols, c, %d);\n"
                         "                if ((tags[i] != 0) == is_far) continue;\n"
                         "                ((uint8_t *)tide_table_column_mut(t, cols, c, %d))[i] = is_far ? 1u : 0u;\n"
                         "            }\n        }\n    }\n",
                      arch_name(g, a), arch_columns(g, a), arch_shift(g, a), arch_presence(g, a, far), arch_presence(g, a, far));
        }
        sb_put(o, "    return any;\n}\n\n");
        gen_near_same(g);
        return;
    }
    if (!sharded(g)) gen_position_where(g);
    const int far = prog->predict_far->index;
    sb_put(o, "// Where an entity's root is: what a child of it is near or far by\n"
              "TIDE_HELPER bool tide_far_where(const tide_world *w, tide_entity e, double p[3])\n{\n");
    if (prog->hierarchy[0]) {
        sb_put(o, "    for (uint32_t depth = 0; depth < 4096u; depth++) { // A hierarchy is never deeper, but nothing loops forever\n"
                  "        const tide_entity parent = tide_parent_of(w, e);\n"
                  "        if (tide_entity_is_null(parent)) break;\n"
                  "        e = parent;\n    }\n");
    }
    sb_put(o, "    return tide_shard_where(w, e, p);\n}\n\n");
    sb_put(o, "static bool tide_game_predict_far(void *world, const tide_player_id player)\n{\n"
              "    tide_world *w = world;\n"
              "    double lo[3] = {0.0, 0.0, 0.0}, hi[3] = {0.0, 0.0, 0.0};\n"
              "    uint32_t owned = 0;\n    (void)player;\n");
    // The box around the player's own entities
    if (prog->owner && prog->owner->index >= 0) {
        const decl *owner = prog->owner;
        const char *player = field_cname(&owner->fields.items[0]);
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a) || !arch_stores(g, a, owner->index)) continue;
            int unused = 0;
            if (far_source(g, a, &unused) == FAR_NOWHERE && !prog->hierarchy[0]) continue; // Nothing of it has a position
            gen_far_rows(g, a, "    ");
            const char *pad = "                ";
            sb_printf(o, "%sif (!placed) continue;\n", pad);
            if (comp_sparse(g, owner->index)) {
                sb_printf(o, "%sif (!((const uint8_t *)tide_table_column(t, cols, c, %d))[i]) continue;\n", pad, arch_presence(g, a, owner->index));
            }
            sb_printf(o, "%sconst uint8_t *const owners[%d] = {", pad, comp_lanes(owner));
            for (int k = 0; k < comp_lanes(owner); k++) {
                sb_printf(o, "%stide_table_column(t, cols, c, %d)", k ? ", " : "", arch_column(g, a, owner->index) + k);
            }
            sb_printf(o, "};\n%s%s mine;\n%stide_gather_%s(&mine, owners, i);\n", pad, type_cname(owner), pad, type_cname(owner));
            sb_printf(o, "%sif (!tide_player_equal(mine.%s, player)) continue;\n", pad, player);
            sb_printf(o, "%sfor (uint32_t k = 0; k < 3u; k++) {\n"
                         "%s    if (!owned || p[k] < lo[k]) lo[k] = p[k];\n"
                         "%s    if (!owned || p[k] > hi[k]) hi[k] = p[k];\n%s}\n%sowned++;\n"
                         "            }\n        }\n    }\n", pad, pad, pad, pad, pad);
        }
    }
    sb_printf(o, "    const double range = %.17g;\n"
                 "    for (uint32_t k = 0; k < 3u; k++) {\n        lo[k] -= range;\n        hi[k] += range;\n    }\n"
                 "    bool any = false;\n",
              prog->predict_range);
    // Each entity that can be far: tagged as it is
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a) || !arch_stores(g, a, far)) continue;
        gen_far_rows(g, a, "    ");
        const char *pad = "                ";
        sb_printf(o, "%sbool is_far = false;\n"
                     "%sif (owned && placed) for (uint32_t k = 0; k < 3u; k++) is_far |= !(p[k] >= lo[k] && p[k] <= hi[k]);\n"
                     "%sany |= is_far;\n", pad, pad, pad);
        sb_printf(o, "%sconst uint8_t *const tags = tide_table_column(t, cols, c, %d);\n"
                     "%sif ((tags[i] != 0) == is_far) continue;\n"
                     "%s((uint8_t *)tide_table_column_mut(t, cols, c, %d))[i] = is_far ? 1u : 0u;\n"
                     "            }\n        }\n    }\n",
                  pad, arch_presence(g, a, far), pad, pad, arch_presence(g, a, far));
    }
    sb_put(o, "    return any;\n}\n\n");
    gen_near_same(g);
}
