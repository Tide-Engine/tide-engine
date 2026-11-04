// Prediction near this player alone (`settings { predictionRange = 40; }`):
// tide_game.predict_far, which a client calls on the world a predicted tick
// starts from (notes/netcode.md, Predicting near the player alone). Included
// by codegen.c.

// tide_game_predict_far(world, player): tags each entity with a position
// farther than predictionRange from every one of the player's own (Owner) as
// tide_far, and takes the tag from the rest. Where an entity is goes by its
// root's position (tide_shard_where), as shards go by it. A player who owns
// nothing with a position has nothing to be near: nothing is tagged. True if
// anything is.
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
        const char *owner = type_cname(prog->owner);
        const char *player = field_cname(&prog->owner->fields.items[0]);
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a) || !arch_stores(g, a, prog->owner->index)) continue;
            sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
            sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n",
                      arch_cell(g, "w", a, 0, "tide_entity", "row", false));
            if (comp_sparse(g, prog->owner->index)) {
                sb_printf(o, "        if (!%s) continue;\n", presence_cell(g, "w", a, prog->owner->index, "row", false));
            }
            sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
            sb_printf(o, "        if (!tide_player_equal(tide_get_%s(w, e).%s, player)) continue;\n", owner, player);
            sb_put(o, "        double p[3];\n        if (!tide_far_where(w, e, p)) continue;\n"
                      "        for (uint32_t k = 0; k < 3u; k++) {\n"
                      "            if (!owned || p[k] < lo[k]) lo[k] = p[k];\n"
                      "            if (!owned || p[k] > hi[k]) hi[k] = p[k];\n        }\n"
                      "        owned++;\n    }\n");
        }
    }
    sb_printf(o, "    const double range = %.17g;\n"
                 "    for (uint32_t k = 0; k < 3u; k++) {\n        lo[k] -= range;\n        hi[k] += range;\n    }\n"
                 "    bool any = false;\n",
              prog->predict_range);
    // Each entity that can be far: tagged as it is, a page written only where that changes
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a) || !arch_stores(g, a, far)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (tide_row_gone(%s)) continue; // A hole (tide_table_remove)\n",
                  arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "        double p[3];\n"
                  "        bool is_far = false;\n"
                  "        if (owned && tide_far_where(w, e, p)) {\n"
                  "            for (uint32_t k = 0; k < 3u; k++) is_far |= !(p[k] >= lo[k] && p[k] <= hi[k]);\n        }\n");
        sb_printf(o, "        if ((%s != 0) != is_far) %s = is_far ? 1u : 0u;\n", presence_cell(g, "w", a, far, "row", false),
                  presence_cell(g, "w", a, far, "row", true));
        sb_put(o, "        any |= is_far;\n    }\n");
    }
    sb_put(o, "    return any;\n}\n\n");
}
