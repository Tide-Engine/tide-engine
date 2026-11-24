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
    sb_put(o, "TIDE_HELPER bool tide_audience_equal(Audience a, Audience b)\n{\n    return ");
    for (int i = 0; i < prog->audience->fields.count; i++) {
        const char *name = str_to_cstr(prog->audience->fields.items[i].name);
        sb_printf(o, "%sa.%s == b.%s", i ? " && " : "", name, name);
    }
    sb_put(o, ";\n}\n\n");
    // In a sharded game the directory is the root's: another shard's code
    // changing it waits on the border, borrowing it (shard_codegen.inc)
    char create_border[96] = "", border_void[96] = "";
    if (sharded(g)) {
        const int n = shard_singleton(prog, prog->visibility_state);
        snprintf(create_border, sizeof create_border, "    if (!tide_shard_singleton_may(w, %du)) return group;\n", n);
        snprintf(border_void, sizeof border_void, "    if (!tide_shard_singleton_may(w, %du)) return;\n", n);
    }
    sb_printf(o,
        "TIDE_HELPER VisibilityGroup tide_visibility_create_group(tide_world *w, Audience audience)\n{\n"
        "    VisibilityGroup group = {.id = -1};\n"
        "    if (w->tide_visibility_state.client) {\n"
        "        // The server numbers groups. A client's prediction can only say whether what it puts in one is its own\n"
        "        // to see (0, everyone's as far as it knows) or not (-1, which hides it: tide_visibility_client_hidden)\n"
        "        if (tide_audience_contains(audience, (tide_player_id){(uint32_t)w->tide_visibility_state.client})) group.id = 0;\n"
        "        return group;\n    }\n"
        "    if (w->tide_visibility_state.next == INT64_MAX) return group;\n"
        "%s"
        "    group.id = ++w->tide_visibility_state.next;\n"
        "    tide_map%d_set(&w->tide_visibility_state.groups, group.id, tide_audience_intersect(audience, TIDE_AUDIENCE_ALL), TIDE_IN_MATCH);\n"
        "    return group;\n}\n\n"
        "TIDE_HELPER void tide_visibility_set_group(tide_world *w, VisibilityGroup group, Audience audience)\n{\n"
        "    if (w->tide_visibility_state.client || group.id <= 0) return;\n"
        "    // Found once, and written where it is: a group that's there keeps its entry (nothing is added, so no\n"
        "    // list grows or moves), and a game that changes a whole view's groups in a tick looks each up in a\n"
        "    // directory of tens of thousands\n"
        "    const int32_t found = tide_map_find(w->tide_visibility_state.groups, &tide_map%d_kind, &group.id);\n"
        "    if (found < 0) return;\n"
        "%s"
        "    audience = tide_audience_intersect(audience, TIDE_AUDIENCE_ALL);\n"
        "    const tide_map%d_entry *const old = tide_map_entry(w->tide_visibility_state.groups, found, &tide_map%d_kind);\n"
        "    if (!old || memcmp(&old->value, &audience, sizeof audience) == 0) return;\n"
        "    tide_list_prepare(&w->tide_visibility_state.groups.entries, sizeof(tide_map%d_entry), TIDE_IN_MATCH);\n"
        "    tide_map%d_entry *const entry = tide_map_entry_mut(w->tide_visibility_state.groups, found, &tide_map%d_kind);\n"
        "    if (entry) entry->value = audience;\n}\n\n"
        "TIDE_HELPER void tide_visibility_destroy_group(tide_world *w, VisibilityGroup group)\n{\n"
        "%s"
        "    if (!w->tide_visibility_state.client && group.id > 0)\n"
        "        (void)tide_map%d_remove(&w->tide_visibility_state.groups, group.id, TIDE_IN_MATCH);\n}\n\n"
        "TIDE_HELPER Audience tide_visibility_group_audience(const tide_world *w, VisibilityGroup group)\n{\n"
        "    if (!group.id) return TIDE_AUDIENCE_ALL;\n"
        "    return group.id > 0 ? tide_map%d_get(w->tide_visibility_state.groups, group.id) : (Audience){0};\n}\n\n",
        create_border, groups, groups, border_void, groups, groups, groups, groups, groups, border_void, groups, groups);
    // A system in runs whose rows set groups' audiences (system_logs_groups): where its rows are on threads
    // (tide_task_gather, tide/jobs.h) each logs what it sets, and its `done` sets them in its entities' order, its
    // tasks' logs (each in its rows' order) merged by entity. Nothing of the system reads the directory, so the
    // directory comes out as one thread, setting each where it is, leaves it.
    bool logs = false;
    for (int i = 0; i < prog->systems.count && !logs; i++) {
        logs = system_logs_groups(prog->systems.items[i]) && system_heap_runs(prog->systems.items[i]);
    }
    if (logs) {
        sb_put(o, "typedef struct tide_group_set { tide_entity e; int64_t id; Audience audience; } tide_group_set;\n\n"
                  "TIDE_HELPER void tide_visibility_set_group_logged(tide_world *w, const tide_entity e, const VisibilityGroup group,\n"
                  "                                                  const Audience audience)\n{\n"
                  "    if (!tide_task_gather) {\n"
                  "        // One piece: set where it is, the directory's heap (the match's own, which has no runs) this thread's\n"
                  "        // to change while it does, as it is outside runs\n"
                  "        const uint32_t was = tide_heap_run_enter(0u);\n"
                  "        tide_visibility_set_group(w, group, audience);\n"
                  "        tide_heap_run_leave(was);\n        return;\n    }\n"
                  "    tide_group_set set;\n    memset(&set, 0, sizeof set);\n"
                  "    set.e = e;\n    set.id = group.id;\n    set.audience = audience;\n"
                  "    tide_gather_push(1u, &set, (uint32_t)sizeof set);\n}\n\n"
                  "// A log's next entry of a group set at or after `*at`, or false\n"
                  "TIDE_HELPER bool tide_group_set_next(const tide_gather *log, uint32_t *at, tide_entity *e)\n{\n"
                  "    while (*at + 8u <= log->used) {\n"
                  "        uint32_t head[2];\n        memcpy(head, log->bytes + *at, sizeof head);\n"
                  "        if (head[0] == 1u && head[1] == (uint32_t)sizeof(tide_group_set) && log->used - *at - 8u >= head[1]) {\n"
                  "            memcpy(e, log->bytes + *at + 8u, sizeof *e);\n            return true;\n        }\n"
                  "        *at += 8u + ((head[1] + 7u) & ~7u);\n    }\n    return false;\n}\n\n"
                  "TIDE_HELPER void tide_visibility_set_logged(tide_world *w)\n{\n"
                  "    uint32_t tasks = 0;\n    const tide_gather *logs = tide_gathered(&tasks);\n    if (!logs || !tasks) return;\n"
                  "    uint32_t *at = calloc(tasks, sizeof *at);\n    if (!at) tide_out_of_memory();\n"
                  "    for (;;) {\n"
                  "        uint32_t pick = UINT32_MAX;\n        tide_entity least = {0};\n"
                  "        for (uint32_t t = 0; t < tasks; t++) {\n"
                  "            tide_entity e;\n"
                  "            if (!tide_group_set_next(&logs[t], &at[t], &e)) continue;\n"
                  "            // (An entity's rows are one task's: no two tasks' entries have one entity)\n"
                  "            if (pick == UINT32_MAX || tide_entity_compare(e, least) < 0) {\n                pick = t;\n                least = e;\n            }\n"
                  "        }\n"
                  "        if (pick == UINT32_MAX) break;\n"
                  "        tide_group_set set;\n        memcpy(&set, logs[pick].bytes + at[pick] + 8u, sizeof set);\n"
                  "        at[pick] += 8u + (((uint32_t)sizeof set + 7u) & ~7u);\n"
                  "        tide_visibility_set_group(w, (VisibilityGroup){.id = set.id}, set.audience);\n    }\n"
                  "    free(at);\n}\n\n");
    }
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
    // The same for a walk over an archetype's rows, which knows where each entity is: nothing is looked up by its
    // handle but a scene's root, once for the rows of one scene that come together. `base` is who may have the
    // entity, and tide_visibility_denied_at who may have one of its components, of that.
    sb_put(o, "typedef struct tide_visibility_walk { tide_entity scene; Audience audience; int64_t scene_group; tide_map_view groups; } tide_visibility_walk;\n\n"
              "TIDE_HELPER tide_entity tide_visibility_scene_at(const tide_world *w, const tide_location loc)\n{\n"
              "    (void)w;\n    switch (loc.archetype) {\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a) || !arch_scene_column(g, a)) continue;
        sb_printf(o, "    case %d: return *%s;\n", a, arch_cell(g, "w", a, 1, "tide_entity", "loc.row", false));
    }
    sb_put(o, "    default: return (tide_entity){0};\n    }\n}\n\n"
              "// (`groups`: its own group and its scene's, 0 for none, which its audience goes by)\n"
              "TIDE_HELPER Audience tide_visibility_base_at(const tide_world *w, const tide_entity e, const tide_location loc,\n"
              "                                             Visibility *visibility, bool *has, tide_visibility_walk *walk, int64_t *groups)\n{\n"
              "    groups[0] = groups[1] = 0;\n"
              "    const tide_entity scene = tide_visibility_scene_at(w, loc);\n"
              "    Audience audience = TIDE_AUDIENCE_ALL;\n"
              "    if (!tide_entity_is_null(scene) && !tide_entity_equal(scene, e)) {\n"
              "        if (!tide_entity_equal(scene, walk->scene)) {\n"
              "            Visibility root = {0};\n            walk->scene = scene;\n"
              "            const bool rooted = tide_load_Visibility(w, scene, &root);\n"
              "            walk->scene_group = rooted ? root.group.id : 0;\n"
              "            walk->audience = rooted\n"
              "                ? tide_audience_intersect(TIDE_AUDIENCE_ALL, tide_visibility_group_audience(w, root.group)) : TIDE_AUDIENCE_ALL;\n"
              "        }\n        audience = walk->audience;\n        groups[1] = walk->scene_group;\n    }\n"
              "    *has = tide_load_Visibility_at(w, loc, visibility);\n"
              "    // Its group's audience through the walk's view of the directory, found once for every row\n");
    sb_printf(o, "    if (*has) groups[0] = visibility->group.id;\n"
                 "    if (*has && visibility->group.id) {\n"
                 "        audience = tide_audience_intersect(audience, visibility->group.id > 0\n"
                 "            ? tide_map%d_cget(&walk->groups, w->tide_visibility_state.groups, visibility->group.id) : (Audience){0});\n"
                 "    }\n    return audience;\n}\n\n", groups);
    sb_printf(o, "TIDE_HELPER Audience tide_visibility_denied_at(const tide_world *w, const Visibility *visibility, const bool has,\n"
                 "                                               Audience base, const uint32_t component)\n{\n"
                 "    if (!has || !visibility->overrides.live) return base;\n"
                 "    const int64_t key = tide_visibility_component_key(component);\n"
                 "    if (key && tide_map%d_has(visibility->overrides, key)) {\n"
                 "        const tide_visibility_policy policy = tide_map%d_get(visibility->overrides, key);\n"
                 "        base = tide_audience_intersect(base, policy.direct ? policy.audience : tide_visibility_group_audience(w, policy.group));\n"
                 "    }\n    return base;\n}\n\n", overrides, overrides);
    sb_printf(o, "// Whether a Visibility restricts one of its components to a group, whose audience it then goes by\n"
                 "TIDE_HELPER bool tide_visibility_by_groups(const Visibility *v)\n{\n"
                 "    if (!v->overrides.live) return false;\n"
                 "    int64_t order = 0;\n    int32_t next = 0, i;\n"
                 "    while ((i = tide_map_next(v->overrides, &tide_map%d_kind, &order, &next)) >= 0) {\n"
                 "        const tide_map%d_entry *entry = tide_map_entry(v->overrides, i, &tide_map%d_kind);\n"
                 "        if (!entry->value.direct) return true;\n    }\n"
                 "    return false;\n}\n\n", overrides, overrides, overrides);
    // A client's world holds what it was sent, which it sees, and what its own
    // ticks made. Of those, what it put in a group hidden from it (above) is
    // the server's alone: not in what it says its world is (client_state, its
    // hashes), as it isn't in the server's projection for it.
    sb_put(o, "TIDE_HELPER bool tide_visibility_client_hidden(const tide_world *w, tide_entity e)\n{\n"
              "    Visibility visibility = {0};\n"
              "    const tide_entity scene = tide_visibility_scene(w, e);\n"
              "    if (!tide_entity_is_null(scene) && !tide_entity_equal(scene, e) && tide_load_Visibility(w, scene, &visibility)\n"
              "        && visibility.group.id < 0) return true;\n"
              "    return tide_load_Visibility(w, e, &visibility) && visibility.group.id < 0;\n}\n\n");
    sb_put(o, "TIDE_HELPER bool tide_visibility_entity(const tide_world *w, tide_entity e, tide_player_id player)\n{\n"
              "    if (w->tide_visibility_state.client) return tide_entity_alive(&w->entities, e) && !tide_visibility_client_hidden(w, e);\n"
              "    return tide_audience_contains(tide_visibility_audience(w, e, -1), player);\n}\n\n"
              "// tide_visibility_entity for a live entity at `loc`, in a walk over many (its scene's root and the directory\n"
              "// found once for the rows that share them): the same answer, without looking the entity up\n"
              "TIDE_HELPER bool tide_visibility_entity_at(const tide_world *w, const tide_entity e, const tide_location loc,\n"
              "                                          const tide_player_id player, tide_visibility_walk *walk)\n{\n"
              "    if (w->tide_visibility_state.client) return tide_visibility_entity(w, e, player);\n"
              "    Visibility visibility;\n    memset(&visibility, 0, sizeof visibility);\n"
              "    bool has = false;\n    int64_t groups[2];\n"
              "    return tide_audience_contains(tide_visibility_base_at(w, e, loc, &visibility, &has, walk, groups), player);\n}\n\n"
              "// What tide_visibility_entity says of an entity for every player, worked out once for a loop over them all: an\n"
              "// audience is a walk to the entity, its scene and their groups, which asking for each player walked again\n"
              "typedef struct tide_visibility_seers {\n    bool client;\n    bool seen;\n    Audience audience;\n} tide_visibility_seers;\n\n"
              "TIDE_HELPER tide_visibility_seers tide_visibility_seers_of(const tide_world *w, const tide_entity e)\n{\n"
              "    tide_visibility_seers s;\n    memset(&s, 0, sizeof s);\n"
              "    s.client = w->tide_visibility_state.client != 0;\n"
              "    if (s.client) s.seen = tide_entity_alive(&w->entities, e) && !tide_visibility_client_hidden(w, e);\n"
              "    else s.audience = tide_visibility_audience(w, e, -1);\n"
              "    return s;\n}\n\n"
              "TIDE_HELPER bool tide_visibility_seer(const tide_visibility_seers *s, const tide_player_id player)\n{\n"
              "    return s->client ? s->seen : tide_audience_contains(s->audience, player);\n}\n\n"
              "TIDE_HELPER bool tide_visibility_component(const tide_world *w, tide_entity e, uint32_t component, tide_player_id player)\n{\n"
              "    if (w->tide_visibility_state.client) return tide_client_has_component(w, e, component) && !tide_visibility_client_hidden(w, e);\n"
              "    return tide_client_has_component(w, e, component) && tide_audience_contains(tide_visibility_audience(w, e, (int32_t)component), player);\n}\n\n"
              "bool tide_world_visible(const tide_world *w, tide_entity e, tide_player_id player)\n{\n"
              "    tide_text_use((tide_heaps *)&w->heap, NULL);\n    return tide_visibility_entity(w, e, player);\n}\n\n"
              "bool tide_world_component_visible(const tide_world *w, tide_entity e, uint32_t component, tide_player_id player)\n{\n"
              "    tide_text_use((tide_heaps *)&w->heap, NULL);\n    return tide_visibility_component(w, e, component, player);\n}\n\n");
}
