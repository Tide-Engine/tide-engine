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
}
