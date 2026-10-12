// A specialized recorder per source spawn site. Handles are reserved together,
// followed by all rows, parenting and only then initialization notifications.
static void gen_prefab_recorders(gen *g)
{
    sb *o = &g->c;
    for (int s = 0; s < g->prog->prefab_sites.count; s++) {
        const expr *site = g->prog->prefab_sites.items[s];
        const prefab_instance *p = site->prefab;
        const bool local = site->local_world;
        const char *prefix = world_prefix(local);
        sb_printf(o, "TIDE_HELPER tide_entity tide_prefab%d(%s *w, tide_entity scene", s, world_type(local));
        for (int j = 0; j < p->overrides.count; j++) sb_printf(o, ", %s override%d", type_cname(p->overrides.items[j]->type_decl), j);
        if (p->load) sb_put(o, ", int32_t visibility");
        sb_put(o, ")\n{\n    (void)scene;\n");
        for (int j = 0; j < p->overrides.count; j++) sb_printf(o, "    (void)override%d;\n", j);
        sb_printf(o, "    tide_entity tide_nodes[%d];\n", p->entities.count);
        sb_printf(o, "    for (int i = 0; i < %d; i++) tide_nodes[i] = %s(&w->entities);\n", p->entities.count, local ? "tide_entity_create" : "tide_new_entity");
        if (p->load) sb_put(o, "    scene = tide_nodes[0];\n");
        for (int n = 0; n < p->entities.count; n++) {
            const expr *spawn = p->entities.items[n].spawn;
            const int a = spawn->spawn_archetype;
            sb_printf(o, "    tide_spawn%d values%d = {0};\n", a, n);
            for (int j = 0; j < spawn->args.count; j++) {
                const expr *value = spawn->args.items[j];
                const decl *comp = value->type_decl;
                sb_printf(o, "    values%d.%s = ", n, type_cname(comp));
                gen_value(g, o, comp, value->inits.items, value->inits.count);
                sb_put(o, ";\n");
                if (comp->is_sparse) sb_printf(o, "    values%d.tide_has_%s = 1;\n", n, type_cname(comp));
            }
            for (int j = 0; n == 0 && j < p->overrides.count; j++) {
                const expr *value = p->overrides.items[j];
                const decl *comp = value->type_decl;
                const decl *placed = comp->local_placement && local ? comp->local_placement : comp;
                if (value->kind != E_LITERAL) {
                    sb_printf(o, "    values0.%s = override%d;\n", type_cname(placed), j);
                } else {
                    for (int f = 0; f < value->inits.count; f++) {
                        const field *field = value->inits.items[f].field;
                        if (!field) continue;
                        sb_printf(o, "    values0.%s.%s = override%d.%s;\n", type_cname(placed), field_cname(field), j, field_cname(field));
                    }
                }
            }
            if (p->load && n == 0) sb_printf(o, "    values0.%s.tide_visibility = visibility;\n", type_cname(g->prog->prefab_scene[local]));
            sb_printf(o, "    tide_command *c%d = %scmd_push(w, TIDE_CMD_SPAWN, %d, tide_nodes[%d], TIDE_COMMAND_SIZE(spawn%d));\n", n, prefix, a, n, a);
            sb_printf(o, "    c%d->prefab_pending = 1;\n    c%d->data.spawn%d.values = values%d;\n    c%d->data.scene = scene;\n", n, n, a, n, n);
            if (spawn_has_text(g, a)) sb_printf(o, "    tide_own_spawn%d(&c%d->data.spawn%d.values, %s);\n", a, n, a, world_where(local));
        }
        for (int n = 1; n < p->entities.count; n++) {
            sb_printf(o, "    %scmd_parent(w, tide_nodes[%d], tide_nodes[%d], false);\n", prefix, n, p->entities.items[n].parent);
        }
        for (int n = 0; n < p->entities.count; n++) {
            sb_printf(o, "    %scmd_push(w, TIDE_CMD_PREFAB_READY, %d, tide_nodes[%d], TIDE_COMMAND_HEAD);\n", prefix, p->entities.items[n].spawn->spawn_archetype, n);
        }
        sb_put(o, "    return tide_nodes[0];\n}\n\n");
    }
}
