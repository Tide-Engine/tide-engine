// Presentation availability is state, rather than an irreversible game event.
// Compare the endpoints actually delivered to local code, including rollback.
static void gen_availability_delivery(gen *g)
{
    const program *prog = g->prog;
    if (!hears_visibility(prog)) return;
    sb *o = &g->c;
    sb_put(o, "static int tide_available_cause_compare(const void *a, const void *b)\n{\n"
              "    const tide_heard *x = a, *y = b;\n"
              "    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;\n"
              "    if (x->born != y->born) return x->born < y->born ? -1 : 1;\n"
              "    if (x->target.index != y->target.index) return x->target.index < y->target.index ? -1 : 1;\n"
              "    return x->target.generation == y->target.generation ? 0 : x->target.generation < y->target.generation ? -1 : 1;\n}\n\n"
              "static void tide_local_clear_availability(tide_local *l)\n{\n"
              "    if (l->tide_available_predicted) { tide_world_free(l->tide_available_predicted); free(l->tide_available_predicted); }\n"
              "    if (l->tide_available_verified) { tide_world_free(l->tide_available_verified); free(l->tide_available_verified); }\n"
              "    l->tide_available_predicted = l->tide_available_verified = NULL;\n"
              "    tide_event_log_free(&l->tide_available_causes);\n}\n\n"
              "static void tide_local_reconcile_availability(tide_local *l, const tide_world *predicted, const tide_world *verified, bool confirmed)\n{\n"
              "    // Keep genuine causes until both presentation clocks have reached them.\n"
              "    const uint32_t kept_causes = l->tide_available_causes.count;\n"
              "    uint32_t at = 0;\n"
              "    for (const tide_heard *r; (r = tide_event_log_next(&l->tide_heard, &at));) {\n");
    sb_printf(o, "        if (r->event != TIDE_HEARD_DESTROYED_CAUSE\n"
                 "            && !(r->event == %du && (r->wants & TIDE_HEARD_LIFECYCLE) && r->handlers)) continue;\n", prog->destroyed->index);
    sb_put(o, "        if (tide_entity_is_null(r->target)) continue;\n"
              "        tide_heard *cause = tide_event_log_push(&l->tide_available_causes, sizeof *cause, TIDE_HEARD_BUILD);\n"
              "        cause->event = TIDE_HEARD_DESTROYED_CAUSE;\n"
              "        cause->tick = r->tick; cause->born = r->born; cause->target = r->target;\n"
              "        cause->as = r->as ? r->as : (TIDE_HEARD_PREDICTED | TIDE_HEARD_VERIFIED);\n"
              "        if (!r->as || (r->as & TIDE_HEARD_VERIFIED)) cause->wants |= TIDE_HEARD_VERIFIED;\n    }\n"
              "    // Private fixed-size metadata can reorder; game event order remains untouched.\n"
              "    if (l->tide_available_causes.count > 1u && l->tide_available_causes.count != kept_causes) {\n"
              "        tide_heard *causes = (tide_heard *)l->tide_available_causes.bytes;\n"
              "        qsort(causes, l->tide_available_causes.count, sizeof *causes, tide_available_cause_compare);\n"
              "        uint32_t count = 1;\n"
              "        for (uint32_t i = 1; i < l->tide_available_causes.count; i++) {\n"
              "            if (!tide_available_cause_compare(&causes[count - 1u], &causes[i])) {\n"
              "                causes[count - 1u].as |= causes[i].as;\n"
              "                causes[count - 1u].wants |= causes[i].wants;\n"
              "            } else causes[count++] = causes[i];\n        }\n"
              "        l->tide_available_causes.count = count;\n"
              "        l->tide_available_causes.size = count * (uint32_t)sizeof *causes;\n"
              "        l->tide_available_causes.first_tick = causes[0].tick;\n    }\n"
              "    for (uint32_t i = 0; i < l->tide_available_causes.size;) {\n"
              "        tide_heard *cause = (tide_heard *)(l->tide_available_causes.bytes + i);\n"
              "        i += cause->size;\n"
              "        if (confirmed && verified && (uint32_t)verified->Time.tick > cause->tick\n"
              "            && !(cause->wants & TIDE_HEARD_VERIFIED)) cause->as &= (uint8_t)~TIDE_HEARD_PREDICTED;\n    }\n"
              "    tide_event_log delivery = {0};\n");
    for (int side = 0; side < 2; side++) {
        bool listens = false;
        const decl *events[] = {prog->shown, prog->hidden};
        for (int e = 0; e < 2; e++) {
            for (int k = 0; k < events[e]->listeners.count; k++)
                listens |= events[e]->listeners.items[k]->listens_verified == (side != 0);
        }
        const char *which = side ? "verified" : "predicted";
        const char *flag = side ? "TIDE_HEARD_VERIFIED" : "TIDE_HEARD_PREDICTED";
        if (!listens) {
            sb_printf(o, "    (void)%s;\n"
                         "    for (uint32_t i = 0; i < l->tide_available_causes.size;) {\n"
                         "        tide_heard *cause = (tide_heard *)(l->tide_available_causes.bytes + i);\n"
                         "        i += cause->size; cause->as &= (uint8_t)~%s;\n    }\n", which, flag);
            continue;
        }
        sb_printf(o, "    {\n        const tide_world *world = %s;\n"
                     "        tide_world **kept = &l->tide_available_%s;\n"
                     "        const uint32_t room = l->tide_available_causes.count;\n"
                     "        tide_entity *destroyed = room ? tide_alloc_zeroed(room, sizeof *destroyed) : NULL;\n"
                     "        uint32_t count = 0;\n"
                     "        for (uint32_t i = 0; i < l->tide_available_causes.size;) {\n"
                     "            tide_heard *cause = (tide_heard *)(l->tide_available_causes.bytes + i);\n"
                     "            i += cause->size;\n"
                     "            if (!(cause->as & %s)) continue;\n"
                     "            if (world && (uint32_t)world->Time.tick <= cause->tick) continue;\n"
                     "            if (world && tide_entity_alive(&world->entities, cause->target)) continue;\n"
                     "            destroyed[count++] = cause->target;\n"
                     "            cause->as &= (uint8_t)~%s;\n        }\n"
                     "        bool unchanged = world && *kept && world->Time.tick == (*kept)->Time.tick\n"
                     "            && world->entities.pages == (*kept)->entities.pages\n"
                     "            && tide_world_hash(world) == tide_world_hash(*kept);\n"
                     "        for (uint32_t p = 0; unchanged && p < world->entities.pages; p++) {\n"
                     "            // Birth identities are intentionally outside the match hash.\n"
                     "            const tide_page *now = world->entities.page[world->entities.room + p];\n"
                     "            const tide_page *was = (*kept)->entities.page[(*kept)->entities.room + p];\n"
                     "            if (now != was) unchanged = false;\n        }\n"
                     "        tide_event_log changes = {0};\n"
                     "        if (!unchanged) tide_world_hear_availability(world, *kept, destroyed, count, &changes, true);\n"
                     "        free(destroyed);\n"
                     "        uint32_t change_at = 0;\n"
                     "        for (const tide_heard *r; (r = tide_event_log_next(&changes, &change_at));) {\n"
                     "            const uint32_t copied = delivery.size;\n"
                     "            tide_event_log_copy(&delivery, &changes, r, %s);\n"
                     "            ((tide_heard *)(delivery.bytes + copied))->wants &= (uint8_t)~TIDE_HEARD_AVAILABILITY;\n        }\n"
                     "        tide_event_log_free(&changes);\n"
                     "        if (world && !unchanged) {\n"
                     "            if (!*kept) *kept = tide_alloc_zeroed(1, sizeof **kept);\n"
                     "            tide_world_copy(*kept, world);\n"
                     "        } else if (!world && *kept) { tide_world_free(*kept); free(*kept); *kept = NULL; }\n    }\n",
                  which, which, flag, flag, flag);
    }
    sb_put(o, "    tide_event_log pending = {0};\n"
              "    for (uint32_t i = 0; i < l->tide_available_causes.size;) {\n"
              "        const tide_heard *cause = (const tide_heard *)(l->tide_available_causes.bytes + i);\n"
              "        i += cause->size;\n"
              "        if (cause->as) tide_event_log_copy(&pending, &l->tide_available_causes, cause, cause->as);\n    }\n"
              "    tide_event_log_free(&l->tide_available_causes);\n"
              "    l->tide_available_causes = pending;\n"
              "    // Availability initializes/releases presentation before ordinary handlers.\n"
              "    tide_event_log_take(&delivery, &l->tide_heard);\n"
              "    tide_event_log_free(&l->tide_heard);\n"
              "    l->tide_heard = delivery;\n}\n\n");
}
