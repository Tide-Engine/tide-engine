// Audience/group policies use generated containers and ordinary world ownership.
// Nothing retained here is a pointer into simulation state.
static void gen_visibility(gen *g)
{
    const program *prog = g->prog;
    if (!prog->visibility_state) return;
    sb *o = &g->c;
    const int groups = prog->visibility_state->fields.items[1].type.decl->index;
    const int overrides = prog->visibility->fields.items[1].type.decl->index;
    sb_put(o, "TIDE_HELPER Audience tide_audience_all(void) { return TIDE_AUDIENCE_ALL; }\n\n");
    sb_put(o, "TIDE_HELPER bool tide_audience_contains(Audience a, tide_player_id player)\n{\n"
              "    const int32_t i = tide_player_index(player);\n"
              "    if (i < 0 || i >= (int32_t)TIDE_GAME_PLAYERS) return false;\n"
              "    const uint32_t words[] = {");
    for (int i = 0; i < prog->audience->fields.count; i++)
        sb_printf(o, "%s(uint32_t)a.%s", i ? ", " : "", str_to_cstr(prog->audience->fields.items[i].name));
    sb_put(o, "};\n    return (words[(uint32_t)i >> 5] & (1u << (i & 31))) != 0;\n}\n\n");
    sb_put(o, "TIDE_HELPER void tide_audience_change(Audience *a, tide_player_id player, bool add)\n{\n"
              "    const int32_t i = tide_player_index(player);\n"
              "    if (!a || i < 0 || i >= (int32_t)TIDE_GAME_PLAYERS) return;\n"
              "    const int32_t bit = (int32_t)(1u << (i & 31));\n    switch ((uint32_t)i >> 5) {\n");
    for (int i = 0; i < prog->audience->fields.count; i++) {
        const char *name = str_to_cstr(prog->audience->fields.items[i].name);
        sb_printf(o, "    case %d: if (add) a->%s |= bit; else a->%s &= ~bit; break;\n", i, name, name);
    }
    sb_put(o, "    default: break;\n    }\n}\n\n"
              "TIDE_HELPER void tide_audience_add(Audience *a, tide_player_id p) { tide_audience_change(a, p, true); }\n"
              "TIDE_HELPER void tide_audience_remove(Audience *a, tide_player_id p) { tide_audience_change(a, p, false); }\n"
              "TIDE_HELPER Audience tide_audience_only(tide_player_id p)\n{\n"
              "    Audience a = {0}; tide_audience_add(&a, p); return a;\n}\n\n");
    for (int i = 0; i < prog->lists.count; i++) {
        if (list_elem(prog->lists.items[i]).kind != TY_PLAYER) continue;
        sb_printf(o, "TIDE_HELPER Audience tide_audience_from(tide_list players)\n{\n"
                     "    Audience a = {0};\n    const int32_t count = tide_list_count(players);\n"
                     "    for (int32_t i = 0; i < count; i++) tide_audience_add(&a, tide_list%d_get(players, i));\n"
                     "    return a;\n}\n\n", i);
        break;
    }
    sb_put(o, "TIDE_HELPER Audience tide_audience_intersect(Audience a, Audience b)\n{\n");
    for (int i = 0; i < prog->audience->fields.count; i++) {
        const char *name = str_to_cstr(prog->audience->fields.items[i].name);
        sb_printf(o, "    a.%s &= b.%s;\n", name, name);
    }
    sb_put(o, "    return a;\n}\n\n");
    sb_printf(o,
        "TIDE_HELPER VisibilityGroup tide_visibility_create_group(tide_world *w, Audience audience)\n{\n"
        "    VisibilityGroup group = {.id = -1};\n"
        "    if (w->tide_visibility_state.client || w->tide_visibility_state.next == INT64_MAX) return group;\n"
        "    group.id = ++w->tide_visibility_state.next;\n"
        "    tide_map%d_set(&w->tide_visibility_state.groups, group.id, tide_audience_intersect(audience, TIDE_AUDIENCE_ALL), TIDE_IN_MATCH);\n"
        "    return group;\n}\n\n"
        "TIDE_HELPER void tide_visibility_set_group(tide_world *w, VisibilityGroup group, Audience audience)\n{\n"
        "    if (w->tide_visibility_state.client || group.id <= 0 || !tide_map%d_has(w->tide_visibility_state.groups, group.id)) return;\n"
        "    audience = tide_audience_intersect(audience, TIDE_AUDIENCE_ALL);\n"
        "    const Audience old = tide_map%d_get(w->tide_visibility_state.groups, group.id);\n"
        "    if (memcmp(&old, &audience, sizeof audience) == 0) return;\n"
        "    tide_map%d_set(&w->tide_visibility_state.groups, group.id, audience, TIDE_IN_MATCH);\n}\n\n"
        "TIDE_HELPER void tide_visibility_destroy_group(tide_world *w, VisibilityGroup group)\n{\n"
        "    if (!w->tide_visibility_state.client && group.id > 0)\n"
        "        (void)tide_map%d_remove(&w->tide_visibility_state.groups, group.id, TIDE_IN_MATCH);\n}\n\n"
        "TIDE_HELPER Audience tide_visibility_group_audience(const tide_world *w, VisibilityGroup group)\n{\n"
        "    if (!group.id) return TIDE_AUDIENCE_ALL;\n"
        "    return group.id > 0 ? tide_map%d_get(w->tide_visibility_state.groups, group.id) : (Audience){0};\n}\n\n",
        groups, groups, groups, groups, groups, groups);
    sb_printf(o,
        "TIDE_HELPER void tide_visibility_set_policy(Visibility *v, int64_t component, tide_visibility_policy policy, uint32_t where)\n{\n"
        "    if (!v) return;\n"
        "    if (tide_map%d_has(v->overrides, component)) {\n"
        "        const tide_visibility_policy old = tide_map%d_get(v->overrides, component);\n"
        "        if (memcmp(&old, &policy, sizeof policy) == 0) return;\n    }\n"
        "    tide_map%d_set(&v->overrides, component, policy, where);\n}\n\n"
        "TIDE_HELPER void tide_visibility_set_audience(Visibility *v, int64_t component, Audience audience, uint32_t where)\n{\n"
        "    tide_visibility_policy policy = {0};\n"
        "    policy.audience = tide_audience_intersect(audience, TIDE_AUDIENCE_ALL);\n    policy.direct = true;\n"
        "    tide_visibility_set_policy(v, component, policy, where);\n}\n\n"
        "TIDE_HELPER void tide_visibility_set_component_group(Visibility *v, int64_t component, VisibilityGroup group, uint32_t where)\n{\n"
        "    tide_visibility_policy policy = {0};\n    policy.group = group;\n"
        "    tide_visibility_set_policy(v, component, policy, where);\n}\n\n"
        "TIDE_HELPER void tide_visibility_reset(Visibility *v, int64_t component, uint32_t where)\n{\n"
        "    if (v) (void)tide_map%d_remove(&v->overrides, component, where);\n}\n\n",
        overrides, overrides, overrides, overrides);
    sb_put(o, "TIDE_HELPER tide_entity tide_visibility_scene(const tide_world *w, tide_entity e)\n{\n"
              "    const tide_location loc = tide_entity_location(&w->entities, e);\n    (void)loc;\n    switch (loc.archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a) || !arch_scene_column(g, a)) continue;
        sb_printf(o, "    case %d: return *%s;\n", a, arch_cell(g, "w", a, 1, "tide_entity", "loc.row", false));
    }
    sb_put(o, "    default: return (tide_entity){0};\n    }\n}\n\n"
              "TIDE_HELPER int64_t tide_visibility_component_key(uint32_t component)\n{\n    switch (component) {\n");
    for (int i = 0; i < prog->components.count; i++) {
        const decl *d = prog->components.items[i];
        if (!d->is_local) sb_printf(o, "    case %d: return %lldLL;\n", i, (long long)visibility_key(d));
        for (int k = 0; k < i; k++) {
            const decl *before = prog->components.items[k];
            if (!d->is_local && !before->is_local && visibility_key(d) == visibility_key(before))
                diag_error(d->at, "component visibility identities collide; rename one of the components");
        }
    }
    sb_put(o, "    default: return 0;\n    }\n}\n\n"
              "TIDE_HELPER bool tide_client_has_component(const tide_world *w, tide_entity e, uint32_t component)\n{\n"
              "    switch (component) {\n");
    for (int i = 0; i < prog->components.count; i++) {
        const decl *d = prog->components.items[i];
        if (!d->is_local) sb_printf(o, "    case %d: return tide_has_%s(w, e);\n", i, type_cname(d));
    }
    sb_put(o, "    default: return false;\n    }\n}\n\n"
              "TIDE_HELPER Audience tide_visibility_audience(const tide_world *w, tide_entity e, int32_t component)\n{\n"
              "    if (!tide_entity_alive(&w->entities, e)) return (Audience){0};\n"
              "    const tide_entity scene = tide_visibility_scene(w, e);\n"
              "    Audience audience = TIDE_AUDIENCE_ALL;\n"
              "    Visibility visibility = {0};\n"
              "    if (!tide_entity_is_null(scene) && !tide_entity_equal(scene, e) && tide_load_Visibility(w, scene, &visibility))\n"
              "        audience = tide_audience_intersect(audience, tide_visibility_group_audience(w, visibility.group));\n"
              "    if (!tide_load_Visibility(w, e, &visibility)) return audience;\n"
              "    audience = tide_audience_intersect(audience, tide_visibility_group_audience(w, visibility.group));\n"
              "    if (component >= 0) {\n    const int64_t key = tide_visibility_component_key((uint32_t)component);\n");
    sb_printf(o, "        if (key && tide_map%d_has(visibility.overrides, key)) {\n"
                 "            const tide_visibility_policy policy = tide_map%d_get(visibility.overrides, key);\n"
                 "            audience = tide_audience_intersect(audience, policy.direct ? policy.audience : tide_visibility_group_audience(w, policy.group));\n"
                 "        }\n    }\n    return audience;\n}\n\n", overrides, overrides);
    sb_put(o, "TIDE_HELPER bool tide_visibility_entity(const tide_world *w, tide_entity e, tide_player_id player)\n{\n"
              "    if (w->tide_visibility_state.client) return tide_entity_alive(&w->entities, e);\n"
              "    return tide_audience_contains(tide_visibility_audience(w, e, -1), player);\n}\n\n"
              "TIDE_HELPER bool tide_visibility_component(const tide_world *w, tide_entity e, uint32_t component, tide_player_id player)\n{\n"
              "    if (w->tide_visibility_state.client) return tide_client_has_component(w, e, component);\n"
              "    return tide_client_has_component(w, e, component) && tide_audience_contains(tide_visibility_audience(w, e, (int32_t)component), player);\n}\n\n"
              "bool tide_world_visible(const tide_world *w, tide_entity e, tide_player_id player)\n{\n"
              "    tide_text_use((tide_heap *)&w->heap, NULL);\n    return tide_visibility_entity(w, e, player);\n}\n\n"
              "bool tide_world_component_visible(const tide_world *w, tide_entity e, uint32_t component, tide_player_id player)\n{\n"
              "    tide_text_use((tide_heap *)&w->heap, NULL);\n    return tide_visibility_component(w, e, component, player);\n}\n\n");
}
