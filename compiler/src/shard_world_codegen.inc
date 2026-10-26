// Shards: what a shard takes from the others (tide_shard_receive), what it
// does after each tick (borrowing, lending, moving), and the tide_shard_game
// the runtime calls. Included by codegen.c after shard_messages_codegen.inc.

static void gen_shard_receive(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const client_codec *codec = &g->value_codec;
    // What changed of a tree this shard holds a ghost of (GHOSTS' form 1): its
    // bytes as this shard packs its ghost, which are what the sender sent it,
    // with the changes on them, taken as a whole tree would be. A base that
    // isn't the one it was sent changes nothing, and asks for the tree whole
    sb_put(o, "TIDE_SHARD_FN bool tide_shard_take_changes(tide_world *w, tide_reader *r, const uint32_t from)\n{\n"
              "    tide_entity root;\n    root.index = tide_read_u32(r);\n    root.generation = tide_read_u32(r);\n"
              "    const uint64_t hash = tide_read_u64(r);\n    const uint32_t n = tide_read_u32(r);\n"
              "    const uint8_t *changes = tide_read_bytes(r, n);\n"
              "    if (r->failed || !changes) return false;\n"
              "    bool took = false;\n"
              "    if (tide_entity_alive(&w->entities, root) && tide_has_tide_ghost(w, root)\n"
              "        && tide_get_tide_ghost(w, root).source == (int32_t)from) {\n"
              "        tide_writer was = {.grows = true};\n        tide_shard_put_tree(&was, w, root);\n"
              "        if (was.overflow) tide_out_of_memory();\n"
              "        if (tide_hash(was.data, was.size) == hash) {\n"
              "            tide_reader c = {changes, n, 0, false};\n            uint32_t at = 0;\n"
              "            while (c.at < c.size && !c.failed) {\n"
              "                const uint32_t same = tide_read_varint(&c), changed = tide_read_varint(&c);\n"
              "                if (c.failed || same > was.size - at || changed > was.size - at - same) { c.failed = true; break; }\n"
              "                at += same;\n"
              "                for (uint32_t i = 0; i < changed; i++) was.data[at++] ^= tide_read_u8(&c);\n            }\n"
              "            if (!c.failed && at == was.size) {\n"
              "                tide_reader tree = {was.data, was.size, 0, false};\n"
              "                took = tide_shard_take_tree(w, &tree, true, (int32_t)from, NULL) && tree.at == tree.size;\n            }\n        }\n"
              "        free(was.data);\n    }\n"
              "    if (!took) { // Asked for whole\n"
              "        tide_writer m = tide_shard_begin(w, TIDE_SHARD_STALE, true);\n"
              "        tide_write_u32(&m, root.index);\n        tide_write_u32(&m, root.generation);\n"
              "        tide_shard_post(w, TIDE_SHARD_TO_SHARD, from, (tide_entity){0}, &m);\n    }\n"
              "    return true;\n}\n\n");
    const char *use = prog->assets.count ? "    tide_pack_use();\n" : "";
    sb_printf(o, "// A message for the coming tick (tide/shard.h), applied before it on the server and on each of its\n"
                 "// players, in the order the server took them\n"
                 "TIDE_SHARD_FN bool tide_shard_receive(void *world, const uint8_t *data, const uint32_t size)\n{\n"
                 "    tide_world *w = world;\n%s    tide_text_use(&w->heap, NULL);\n"
                 "    tide_shard_state *s = &w->tide_shard_state;\n"
                 "    tide_reader r = {data, size, 0, false};\n"
                 "    const uint8_t kind = tide_read_u8(&r);\n    const uint32_t from = tide_read_u32(&r);\n"
                 "    const uint32_t number = tide_read_u32(&r);\n"
                 "    if (r.failed) return false;\n"
                 // A numbered message applies once, in its sender's order: one heard already is dropped
                 "    if (number) {\n"
                 "        const int32_t at = tide_shard_find(s->heard, (int64_t)from, 2);\n"
                 "        if (at >= 0 && tide_shard_long(s->heard, at + 1) >= (int64_t)number) return true;\n"
                 "        if (at >= 0) *(int64_t *)tide_list_at_mut(s->heard, at + 1, 8u) = (int64_t)number;\n"
                 "        else {\n            tide_shard_push(&s->heard, (int64_t)from);\n            tide_shard_push(&s->heard, (int64_t)number);\n        }\n"
                 "    }\n"
                 "    switch (kind) {\n", use);
    // CONFIG and MAP: who it is, and the regions
    sb_printf(o, "    case TIDE_SHARD_CONFIG:\n    case TIDE_SHARD_MAP: {\n"
                 "        uint32_t id = 0;\n        uint8_t role = 0;\n"
                 "        if (kind == TIDE_SHARD_CONFIG) {\n            id = tide_read_u32(&r);\n            role = tide_read_u8(&r);\n        }\n"
                 "        const uint32_t regions = tide_read_u32(&r);\n"
                 "        if (r.failed || regions > (r.size - r.at) / 60u) return false;\n"
                 "        double *values = malloc((size_t)regions * 7u * sizeof *values + 1u);\n"
                 "        uint64_t *matches = malloc((size_t)regions * sizeof *matches + 1u);\n"
                 "        if (!values || !matches) tide_out_of_memory();\n"
                 "        for (uint32_t i = 0; i < regions; i++) {\n"
                 "            values[i * 7u] = (double)tide_read_u32(&r);\n"
                 "            const uint8_t *box = tide_read_bytes(&r, 48u);\n"
                 "            if (box) memcpy(values + i * 7u + 1u, box, 48u);\n"
                 "            matches[i] = tide_read_u64(&r);\n        }\n"
                 "        if (r.failed) { free(values); free(matches); return false; }\n"
                 "        tide_list_clear(&s->regions, 8u);\n"
                 "        tide_list_clear(&s->matches, 8u);\n"
                 "        for (uint32_t i = 0; i < regions * 7u; i++) *(double *)tide_list_add(&s->regions, 8u, TIDE_IN_MATCH) = values[i];\n"
                 "        for (uint32_t i = 0; i < regions; i++) tide_shard_push(&s->matches, (int64_t)matches[i]);\n"
                 "        free(values);\n        free(matches);\n"
                 "        if (kind == TIDE_SHARD_MAP) return true;\n"
                 "        s->shard = (int32_t)id + 1;\n        s->role = role;\n"
                 "        if (role == TIDE_SHARD_ROOT_ROLE) s->singletons = (int64_t)%s;\n"
                 "        const uint32_t ranges = tide_read_u32(&r);\n"
                 "        if (r.failed || ranges > (r.size - r.at) / 8u) return false;\n"
                 "        for (uint32_t i = 0; i < ranges; i++) {\n"
                 "            const uint32_t first = tide_read_u32(&r), end = tide_read_u32(&r);\n"
                 "            if (end <= first) continue;\n"
                 "            tide_shard_push(&s->ranges, (int64_t)first);\n            tide_shard_push(&s->ranges, (int64_t)end);\n"
                 "            if (i == 0) {\n"
                 "                s->next = (int64_t)first > (int64_t)w->entities.next_unused ? (int64_t)first : (int64_t)w->entities.next_unused;\n"
                 "                s->end = (int64_t)end;\n            }\n        }\n"
                 "        return !r.failed;\n    }\n",
              shard_singleton_count(prog) >= 64 ? "UINT64_MAX" : "((UINT64_C(1) << TIDE_SHARD_SINGLETONS) - 1u)");
    sb_put(o, "    case TIDE_SHARD_RANGE: {\n"
              "        const uint32_t first = tide_read_u32(&r), end = tide_read_u32(&r);\n"
              "        if (r.failed || end <= first) return false;\n"
              "        tide_shard_push(&s->ranges, (int64_t)first);\n        tide_shard_push(&s->ranges, (int64_t)end);\n"
              "        return true;\n    }\n");
    // GHOSTS: copies near the border, what went from it, and the singletons
    sb_put(o, "    case TIDE_SHARD_GHOSTS: {\n"
              "        const uint32_t trees = tide_read_u32(&r);\n"
              "        for (uint32_t i = 0; i < trees && !r.failed; i++) {\n"
              "            const uint8_t form = tide_read_u8(&r); // 0: a tree whole; 1: what changed of one\n"
              "            if (form == 0u ? !tide_shard_take_tree(w, &r, true, (int32_t)from, NULL)\n"
              "                           : form != 1u || !tide_shard_take_changes(w, &r, from)) return false;\n        }\n"
              "        const uint32_t drops = tide_read_u32(&r);\n"
              "        if (r.failed || drops > (r.size - r.at) / 8u) return false;\n"
              "        for (uint32_t i = 0; i < drops; i++) {\n"
              "            tide_entity e;\n            e.index = tide_read_u32(&r);\n            e.generation = tide_read_u32(&r);\n"
              "            if (tide_entity_alive(&w->entities, e) && tide_has_tide_ghost(w, e) && tide_get_tide_ghost(w, e).source == (int32_t)from) {\n"
              "                tide_shard_remove_tree(w, e, true);\n            }\n        }\n"
              "        if (r.at < r.size && tide_read_u8(&r)) {\n"
              "            const uint32_t count = tide_read_u32(&r);\n"
              "            for (uint32_t k = 0; k < count && !r.failed; k++) {\n"
              "                const uint32_t i = tide_read_u32(&r);\n"
              "                const bool mine = (((uint64_t)s->singletons >> (i & 63u)) & 1u) != 0;\n"
              "                if (r.failed || i >= TIDE_SHARD_SINGLETONS || !tide_shard_take_singleton(w, &r, i, !mine)) return false;\n"
              "            }\n"
              "            // The root's fields of singletons holding [Native] state: kept, and in the world from now on, on any shard but the root\n"
              "            if (r.at < r.size) {\n"
              "                const uint32_t n = tide_read_u32(&r);\n"
              "                const uint8_t *shared = tide_read_bytes(&r, n);\n"
              "                if (!shared) return false;\n"
              "                if (s->role != TIDE_SHARD_ROOT_ROLE) {\n"
              "                    tide_shard_bytes_set(&s->shared, shared, n, TIDE_IN_MATCH);\n"
              "                    tide_shard_apply_shared(w);\n                }\n            }\n"
              "        }\n"
              "        return !r.failed;\n    }\n");
    // ARRIVE: an entity this shard owns from now on
    sb_put(o, "    case TIDE_SHARD_ARRIVE: {\n"
              "        const uint8_t flags = tide_read_u8(&r);\n        tide_read_u32(&r);\n"
              "        tide_entity root = {0};\n"
              "        if (r.failed || !tide_shard_take_tree(w, &r, false, 0, &root)) return false;\n"
              "        tide_shard_mark_tree(w, root, false, 0);\n"
              "        if (!tide_shard_take_tasks(w, &r)) return false;\n"
              "        tide_shard_unask(w, root);\n"
              "        if (flags & TIDE_SHARD_BORROWED) tide_shard_pin(w, tide_shard_key(root));\n"
              "        return true;\n    }\n");
    // BORROW: lent now, once nothing holds it, or the request goes on to its owner
    sb_put(o, "    case TIDE_SHARD_BORROW:\n    case TIDE_SHARD_BORROW_SINGLETON: {\n"
              "        const uint32_t asker = tide_read_u32(&r);\n"
              "        tide_entity e;\n        e.index = tide_read_u32(&r);\n"
              "        e.generation = kind == TIDE_SHARD_BORROW ? tide_read_u32(&r) : TIDE_ENTITY_TEMPORARY;\n"
              "        if (r.failed || (int32_t)asker == tide_shard_self(w)) return !r.failed;\n"
              "        tide_shard_ask_of(w, e, (int32_t)asker);\n        return true;\n    }\n");
    sb_put(o, "    case TIDE_SHARD_SHARED: return tide_shard_take_shared(w, &r);\n");
    sb_put(o, "    case TIDE_SHARD_STALE: return true; // The sender's agent's (tide_shard_agent_apply)\n");
    sb_put(o, "    case TIDE_SHARD_FREED: {\n"
              "        const uint32_t index = tide_read_u32(&r);\n        const uint32_t generation = tide_read_u32(&r);\n"
              "        if (r.failed) return false;\n"
              "        (void)tide_entity_release(&w->entities, index, generation);\n"
              "        return true;\n    }\n");
    sb_put(o, "    case TIDE_SHARD_GONE: {\n"
              "        tide_entity e;\n        e.index = tide_read_u32(&r);\n        e.generation = tide_read_u32(&r);\n"
              "        if (r.failed) return false;\n"
              "        tide_shard_unask(w, e);\n"
              "        if (tide_shard_find(s->gone, tide_shard_key(e), 1) < 0) tide_shard_push(&s->gone, tide_shard_key(e));\n"
              "        return true;\n    }\n");
    // EVENT: to an entity this shard owns, it's sent here; else it goes on
    sb_put(o, "    case TIDE_SHARD_EVENT: {\n"
              "        tide_entity target;\n        target.index = tide_read_u32(&r);\n        target.generation = tide_read_u32(&r);\n"
              "        const uint32_t event = tide_read_u32(&r);\n        const tide_player_id sender = {tide_read_u32(&r)};\n"
              "        (void)sender;\n        uint64_t budget = SIZE_MAX;\n        (void)budget;\n"
              "        if (r.failed) return false;\n"
              "        if (!tide_shard_owned(w, target) && !tide_entity_died_here(&w->entities, target)) {\n"
              "            tide_writer m = tide_shard_begin(w, TIDE_SHARD_EVENT, true);\n"
              "            tide_write_bytes(&m, data + 9u, size - 9u);\n"
              "            tide_shard_post(w, TIDE_SHARD_TO_OWNER, 0u, target, &m);\n"
              "            return true;\n        }\n"
              "        switch (event) {\n");
    for (int i = 0; i < prog->events.count; i++) {
        const decl *d = prog->events.items[i];
        if (d->is_local || !has_handlers(d, false) || !is_queued(prog, d)) continue;
        const char *name = type_cname(d);
        sb_printf(o, "        case %du: {\n            %s value;\n            memset(&value, 0, sizeof value);\n"
                     "            if (!tide_client_get%d(&r, &value, TIDE_IN_MATCH, 0, &budget)) {",
                  d->index, name, client_codec_id(codec, (type){TY_EVENT, (decl *)d}));
        if (decl_has_text(d)) sb_printf(o, " tide_release_%s(&value, TIDE_IN_MATCH);", name);
        sb_put(o, " return false; }\n");
        sb_printf(o, "            tide_command *c = tide_cmd_push(w, TIDE_CMD_EVENT, %d, target, TIDE_COMMAND_SIZE(event_%s));\n"
                     "            c->data.event_%s = value;\n", d->index, name, name);
        if (prog->command_types.count) sb_put(o, "            c->sender = sender;\n");
        sb_put(o, "            return true;\n        }\n");
    }
    sb_put(o, "        default: return false;\n        }\n    }\n");
    sb_put(o, "    case TIDE_SHARD_UNLOAD: {\n"
              "        tide_entity scene;\n        scene.index = tide_read_u32(&r);\n        scene.generation = tide_read_u32(&r);\n"
              "        if (r.failed) return false;\n");
    if (has_scenes(prog, false)) sb_put(o, "        tide_shard_unload_members(w, scene);\n");
    else sb_put(o, "        (void)scene;\n");
    sb_put(o, "        return true;\n    }\n"
              "    case TIDE_SHARD_ACK: {\n"
              "        const uint32_t through = tide_read_u32(&r);\n"
              "        if (r.failed) return false;\n        tide_shard_acknowledged(w, through);\n        return true;\n    }\n");
    sb_printf(o, "    case TIDE_SHARD_PLAYER: {\n"
                 "        const uint8_t joined = tide_read_u8(&r);\n        const tide_player_id player = {tide_read_u32(&r)};\n"
                 "        const int32_t slot = tide_player_index(player);\n"
                 "        if (r.failed || joined > 1u || slot < 0 || slot >= (int32_t)TIDE_GAME_PLAYERS) return false;\n"
                 "        const int32_t word = slot / 64;\n"
                 "        if (word >= tide_list_count(s->members)) {\n"
                 "            if (!joined) return true;\n"
                 "            tide_list_resize(&s->members, word + 1, 8u, TIDE_IN_MATCH);\n"
                 "        }\n"
                 "        uint64_t *members = tide_list_at_mut(s->members, word, 8u);\n"
                 "        const uint64_t mask = UINT64_C(1) << ((uint32_t)slot & 63u);\n"
                 "        if (((*members & mask) != 0) == (joined != 0)) return true;\n"
                 "        *members ^= mask;\n"
                 "        if (joined) tide_cmd_send_%s(w, (tide_entity){0}, (%s){.player = player});\n"
                 "        else tide_cmd_send_%s(w, (tide_entity){0}, (%s){.player = player});\n"
                 "        return true;\n    }\n",
              type_cname(prog->player_joined), type_cname(prog->player_joined), type_cname(prog->player_left),
              type_cname(prog->player_left));
    // ROUTE: a player's home shard and what their machine follows, a record each
    // (the player, the home, how many, then each shard and match)
    sb_put(o, "    case TIDE_SHARD_ROUTE: {\n"
              "        const uint32_t player = tide_read_u32(&r);\n        const uint32_t home = tide_read_u32(&r);\n"
              "        const uint32_t count = tide_read_u32(&r);\n"
              "        if (r.failed || count > (r.size - r.at) / 12u) return false;\n"
              "        const int32_t at = tide_shard_route_at(s->routes, (int64_t)player);\n"
              "        if (at >= 0) tide_shard_drop(&s->routes, at, 3 + 2 * (int32_t)tide_shard_long(s->routes, at + 2));\n"
              "        if (!count) return true;\n"
              "        tide_shard_push(&s->routes, (int64_t)player);\n        tide_shard_push(&s->routes, (int64_t)home);\n"
              "        tide_shard_push(&s->routes, (int64_t)count);\n"
              "        for (uint32_t i = 0; i < count; i++) {\n"
              "            tide_shard_push(&s->routes, (int64_t)tide_read_u32(&r));\n"
              "            tide_shard_push(&s->routes, (int64_t)tide_read_u64(&r));\n        }\n"
              "        return !r.failed;\n    }\n");
    sb_put(o, "    case TIDE_SHARD_RETIRE: {\n"
              "        const uint32_t into = tide_read_u32(&r);\n"
              "        if (r.failed || into == (uint32_t)tide_shard_self(w) || into > INT32_MAX - 1u) return false;\n"
              "        s->into = (int32_t)into + 1;\n        return true;\n    }\n");
    sb_put(o, "    case TIDE_SHARD_SINGLETON: {\n"
              "        const uint32_t i = tide_read_u32(&r);\n        tide_read_u32(&r);\n"
              "        if (r.failed || i >= TIDE_SHARD_SINGLETONS || !tide_shard_take_singleton(w, &r, i, true)) return false;\n"
              "        s->singletons = (int64_t)((uint64_t)s->singletons | ((uint64_t)1 << i));\n"
              "        const tide_entity mark = {i, TIDE_ENTITY_TEMPORARY};\n"
              "        tide_shard_unask(w, mark);\n"
              "        if (s->role != TIDE_SHARD_ROOT_ROLE) tide_shard_pin(w, tide_shard_key(mark));\n"
              "        return true;\n    }\n"
              "    default: return false;\n    }\n}\n\n");
}

// What another shard asked for: lent now; or once nothing here holds it; or
// asked of the shard that has it; or, gone, the asker is told so
static void gen_shard_lending(gen *g)
{
    sb *o = &g->c;
    sb_put(o, "TIDE_HELPER void tide_shard_wait_to_lend(tide_world *w, const tide_entity e, const int32_t asker)\n{\n"
              "    tide_writer r = {.grows = true};\n"
              "    tide_write_u32(&r, 28u);\n    tide_write_u32(&r, TIDE_SHARD_LEND);\n"
              "    tide_write_u32(&r, e.index);\n    tide_write_u32(&r, e.generation);\n"
              "    tide_write_u32(&r, (uint32_t)w->Time.tick);\n    tide_write_u32(&r, (uint32_t)asker);\n    tide_write_u32(&r, 0u);\n"
              "    if (r.overflow) tide_out_of_memory();\n"
              "    tide_shard_bytes_append(&w->tide_shard_state.waits, r.data, r.size, TIDE_IN_MATCH);\n    free(r.data);\n}\n\n"
              // Whether this shard keeps what it owns from a shard that asks for it, for now:
              // it's borrowed, or its own code that waits needs it, and the asker
              // comes after it. A shard with a lower number always gets what it
              // asks for, so code waiting on several shards' entities can't wait
              // on each other forever
              "TIDE_HELPER bool tide_shard_keeps(const tide_world *w, const tide_entity e, const int32_t asker)\n{\n"
              "    const int64_t key = tide_shard_key(e);\n"
              "    return asker > tide_shard_self(w) && (tide_shard_pinned(w, key) || tide_shard_find(w->tide_shard_state.held, key, 1) >= 0);\n}\n\n"
              "TIDE_SHARD_FN void tide_shard_ask_of(tide_world *w, const tide_entity e, const int32_t asker)\n{\n"
              "    if (tide_shard_is_singleton(e)) {\n"
              "        if (!tide_shard_owned(w, e) || tide_shard_keeps(w, e, asker)) tide_shard_wait_to_lend(w, e, asker);\n"
              "        else tide_shard_lend(w, e, asker);\n        return;\n    }\n"
              "    if (tide_entity_alive(&w->entities, e) && !tide_has_tide_ghost(w, e)) {\n"
              "        if (tide_shard_keeps(w, e, asker)) tide_shard_wait_to_lend(w, e, asker);\n"
              "        else tide_shard_lend(w, e, asker);\n        return;\n    }\n"
              "    if (tide_entity_died_here(&w->entities, e) || tide_shard_find(w->tide_shard_state.gone, tide_shard_key(e), 1) >= 0) {\n"
              "        tide_writer m = tide_shard_begin(w, TIDE_SHARD_GONE, true);\n"
              "        tide_write_u32(&m, e.index);\n        tide_write_u32(&m, e.generation);\n"
              "        tide_shard_post(w, TIDE_SHARD_TO_SHARD, (uint32_t)asker, e, &m);\n        return;\n    }\n"
              // Another shard has it: the request goes on there, still the asker's
              "    tide_writer m = tide_shard_begin(w, TIDE_SHARD_BORROW, true);\n"
              "    tide_write_u32(&m, (uint32_t)asker);\n    tide_write_u32(&m, e.index);\n    tide_write_u32(&m, e.generation);\n"
              "    tide_shard_post(w, TIDE_SHARD_TO_OWNER, 0u, e, &m);\n}\n\n");
}

// After each tick: what code that waited touched is asked for, what others
// asked for is lent once it can be, what was borrowed goes back once nothing
// held it a while, and entities that left this shard's region move
static void gen_shard_after(gen *g)
{
    sb *o = &g->c;
    // What acts on what (Shards.Together and Apart): the last run's, sorted so the
    // shard's list comes out the same whatever order its threads said them in,
    // summed into it; a pair is gone at 0, and when neither is here
    sb_put(o, "static int tide_shard_pair_order(const void *x, const void *y)\n{\n"
              "    const tide_shard_pair *p = x, *q = y;\n"
              "    const int64_t a[3] = {tide_shard_key(p->a), tide_shard_key(p->b), p->delta};\n"
              "    const int64_t b[3] = {tide_shard_key(q->a), tide_shard_key(q->b), q->delta};\n"
              "    for (int i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;\n"
              "    return 0;\n}\n\n"
              "TIDE_SHARD_FN void tide_shard_touching(tide_world *w, const tide_shard_tick *t)\n{\n"
              "    tide_shard_state *s = &w->tide_shard_state;\n"
              "    if (t->pair_count) {\n"
              "        tide_shard_pair *pairs = malloc((size_t)t->pair_count * sizeof *pairs);\n"
              "        if (!pairs) tide_out_of_memory();\n"
              "        for (uint32_t i = 0; i < t->pair_count; i++) {\n"
              "            pairs[i] = t->pairs[i];\n"
              "            if (tide_shard_key(pairs[i].b) < tide_shard_key(pairs[i].a)) {\n"
              "                pairs[i].a = t->pairs[i].b;\n                pairs[i].b = t->pairs[i].a;\n            }\n        }\n"
              "        qsort(pairs, t->pair_count, sizeof *pairs, tide_shard_pair_order);\n"
              "        for (uint32_t i = 0; i < t->pair_count; i++) {\n"
              "            const int64_t a = tide_shard_key(pairs[i].a), b = tide_shard_key(pairs[i].b);\n"
              "            int32_t at = -1;\n"
              "            for (int32_t k = 0; k + 3 <= tide_list_count(s->touching) && at < 0; k += 3) {\n"
              "                if (tide_shard_long(s->touching, k) == a && tide_shard_long(s->touching, k + 1) == b) at = k;\n            }\n"
              "            if (at >= 0) *(int64_t *)tide_list_at_mut(s->touching, at + 2, 8u) += pairs[i].delta;\n"
              "            else if (pairs[i].delta > 0) {\n"
              "                tide_shard_push(&s->touching, a);\n                tide_shard_push(&s->touching, b);\n"
              "                tide_shard_push(&s->touching, pairs[i].delta);\n            }\n        }\n"
              "        free(pairs);\n    }\n"
              "    for (int32_t k = tide_list_count(s->touching) - 3; k >= 0; k -= 3) {\n"
              "        const tide_entity a = tide_shard_unkey(tide_shard_long(s->touching, k));\n"
              "        const tide_entity b = tide_shard_unkey(tide_shard_long(s->touching, k + 1));\n"
              "        if (tide_shard_long(s->touching, k + 2) > 0\n"
              "            && (tide_entity_alive(&w->entities, a) || tide_entity_alive(&w->entities, b))) continue;\n"
              "        tide_shard_drop(&s->touching, k, 3);\n    }\n}\n\n");
    sb_put(o, "static int tide_shard_span_order(const void *x, const void *y)\n{\n"
              "    const int64_t a = tide_shard_key(((const tide_shard_span *)x)->e), b = tide_shard_key(((const tide_shard_span *)y)->e);\n"
              "    return a < b ? -1 : a > b ? 1 : 0;\n}\n\n"
              "TIDE_SHARD_FN void tide_shard_spanning(tide_world *w, const tide_shard_tick *t)\n{\n"
              "    tide_shard_state *s = &w->tide_shard_state;\n"
              "    tide_list_clear(&s->spanned, 8u);\n    tide_list_clear(&s->spans, 8u);\n"
              "    if (!t->span_count) return;\n"
              "    tide_shard_span *spans = malloc((size_t)t->span_count * sizeof *spans);\n"
              "    if (!spans) tide_out_of_memory();\n"
              "    memcpy(spans, t->spans, (size_t)t->span_count * sizeof *spans);\n"
              "    qsort(spans, t->span_count, sizeof *spans, tide_shard_span_order);\n"
              "    for (uint32_t i = 0; i < t->span_count;) {\n"
              "        tide_shard_span span = spans[i];\n"
              "        uint32_t next = i + 1u;\n"
              "        for (; next < t->span_count && tide_entity_equal(spans[next].e, span.e); next++) {\n"
              "            for (uint32_t k = 0; k < 3u; k++) {\n"
              "                if (spans[next].lo[k] < span.lo[k]) span.lo[k] = spans[next].lo[k];\n"
              "                if (spans[next].hi[k] > span.hi[k]) span.hi[k] = spans[next].hi[k];\n            }\n        }\n"
              "        i = next;\n"
              "        double p[3];\n"
              "        if (!tide_entity_alive(&w->entities, span.e) || !tide_shard_where(w, span.e, p)) continue;\n"
              "        bool past = false; // What its position shares already, within reach of it, needs no span\n"
              "        for (uint32_t k = 0; k < TIDE_SHARD_DIMS; k++) {\n"
              "            past |= span.lo[k] < p[k] - TIDE_SHARD_REACH || span.hi[k] > p[k] + TIDE_SHARD_REACH;\n        }\n"
              "        if (!past) continue;\n"
              "        tide_shard_push(&s->spanned, tide_shard_key(span.e));\n"
              "        for (uint32_t k = 0; k < 3u; k++) *(double *)tide_list_add(&s->spans, 8u, TIDE_IN_MATCH) = span.lo[k];\n"
              "        for (uint32_t k = 0; k < 3u; k++) *(double *)tide_list_add(&s->spans, 8u, TIDE_IN_MATCH) = span.hi[k];\n    }\n"
              "    free(spans);\n}\n\n");
    sb_put(o, "TIDE_SHARD_FN void tide_shard_after(tide_world *w, const tide_shard_tick *t)\n{\n"
              "    tide_shard_state *s = &w->tide_shard_state;\n"
              "    if (!s->shard) return;\n"
              "    tide_shard_touching(w, t);\n"
              "    tide_shard_spanning(w, t); // What reaches past its position, for the border\n"
              "    tide_shard_send_shared(w); // What its code wrote of the root's settings goes to the root\n"
              // What its code that waits needs: the entities it ran for, and what they touched
              "    tide_list_clear(&s->held, 8u);\n"
              "    for (uint32_t i = 0; i < t->skip_count; i++) {\n"
              "        const tide_entity both[2] = {t->skip[i].entity, t->skip[i].touched};\n"
              "        for (uint32_t k = 0; k < 2u; k++) {\n"
              "            if (!tide_entity_is_null(both[k]) && tide_shard_find(s->held, tide_shard_key(both[k]), 1) < 0) {\n"
              "                tide_shard_push(&s->held, tide_shard_key(both[k]));\n            }\n        }\n    }\n"
              "    if (tide_list_count(s->waits)) {\n"
              "        uint32_t size;\n        bool copied;\n"
              "        const uint8_t *flat = tide_shard_bytes_flat(s->waits, &size, &copied);\n"
              "        for (uint32_t at = 0; at + 4u <= size;) {\n"
              "            tide_reader r = {flat + at, size - at, 0, false};\n"
              "            const uint32_t record = tide_read_u32(&r);\n"
              "            if (record < 4u || record > size - at) break;\n"
              "            if (tide_read_u32(&r) != TIDE_SHARD_LEND) {\n"
              "                tide_entity e;\n                e.index = tide_read_u32(&r);\n                e.generation = tide_read_u32(&r);\n"
              "                tide_read_u32(&r);\n                tide_read_u32(&r);\n"
              "                const uint32_t needs = tide_read_u32(&r);\n"
              "                for (uint32_t k = 0; k <= needs && !r.failed; k++) {\n"
              "                    if (k) { e.index = tide_read_u32(&r); e.generation = tide_read_u32(&r); }\n"
              "                    if (!r.failed && !tide_entity_is_null(e) && tide_shard_find(s->held, tide_shard_key(e), 1) < 0) {\n"
              "                        tide_shard_push(&s->held, tide_shard_key(e));\n                    }\n                }\n            }\n"
              "            at += record;\n        }\n"
              "        tide_shard_bytes_done(s->waits, flat, copied);\n    }\n");
    // What its code that waits touched, asked for
    sb_put(o, "    for (uint32_t i = 0; i < t->skip_count; i++) {\n"
              "        const tide_entity e = t->skip[i].touched;\n"
              "        if (tide_shard_owned(w, e) || tide_shard_find(s->asked, tide_shard_key(e), 1) >= 0) continue;\n"
              "        tide_shard_push(&s->asked, tide_shard_key(e));\n"
              "        if (tide_shard_is_singleton(e)) {\n"
              "            tide_writer m = tide_shard_begin(w, TIDE_SHARD_BORROW_SINGLETON, true);\n"
              "            tide_write_u32(&m, (uint32_t)tide_shard_self(w));\n            tide_write_u32(&m, e.index);\n"
              "            tide_shard_post(w, TIDE_SHARD_TO_SHARD, TIDE_SHARD_ROOT, (tide_entity){0}, &m);\n"
              "        } else {\n"
              "            tide_writer m = tide_shard_begin(w, TIDE_SHARD_BORROW, true);\n"
              "            tide_write_u32(&m, (uint32_t)tide_shard_self(w));\n"
              "            tide_write_u32(&m, e.index);\n            tide_write_u32(&m, e.generation);\n"
              "            tide_shard_post(w, TIDE_SHARD_TO_OWNER, 0u, e, &m);\n        }\n    }\n");
    // Requests waiting to be lent, in the order they came
    sb_put(o, "    if (tide_list_count(s->waits)) {\n"
              "        uint32_t size;\n        bool copied;\n"
              "        const uint8_t *flat = tide_shard_bytes_flat(s->waits, &size, &copied);\n"
              "        uint8_t *all = malloc(size ? size : 1u);\n        if (!all) tide_out_of_memory();\n"
              "        memcpy(all, flat, size);\n        tide_shard_bytes_done(s->waits, flat, copied);\n"
              "        tide_writer keep = {.grows = true};\n        bool lent = false;\n"
              "        for (uint32_t at = 0; at + 4u <= size;) {\n"
              "            tide_reader r = {all + at, size - at, 0, false};\n"
              "            const uint32_t record = tide_read_u32(&r);\n"
              "            if (record < 4u || record > size - at) break;\n"
              "            const uint32_t code = tide_read_u32(&r);\n"
              "            tide_entity e;\n            e.index = tide_read_u32(&r);\n            e.generation = tide_read_u32(&r);\n"
              "            tide_read_u32(&r);\n            const int32_t asker = (int32_t)tide_read_u32(&r);\n"
              "            const bool held = tide_shard_owned(w, e) && tide_shard_keeps(w, e, asker);\n"
              "            if (code == TIDE_SHARD_LEND && !r.failed && !held && (!tide_shard_is_singleton(e) || tide_shard_owned(w, e))) {\n"
              "                lent = true;\n"
              "                tide_shard_ask_of(w, e, asker);\n"
              "            } else tide_write_bytes(&keep, all + at, record);\n"
              "            at += record;\n        }\n"
              "        if (lent) {\n"
              "            uint32_t added;\n            bool more;\n"
              "            const uint8_t *now = tide_shard_bytes_flat(s->waits, &added, &more);\n"
              // What lending put in waits meanwhile stays, after what was kept
              "            if (added > size) tide_write_bytes(&keep, now + size, added - size);\n"
              "            tide_shard_bytes_done(s->waits, now, more);\n"
              "            if (keep.overflow) tide_out_of_memory();\n"
              "            tide_shard_bytes_set(&s->waits, keep.data, keep.size, TIDE_IN_MATCH);\n        }\n"
              "        free(keep.data);\n        free(all);\n    }\n"
              // Borrowed singletons go back to the root once nothing held them a while
              "    for (int32_t at = 0; at + 2 <= tide_list_count(s->pins);) {\n"
              "        const int64_t key = tide_shard_long(s->pins, at);\n"
              "        if (tide_shard_long(s->pins, at + 1) > (int64_t)w->Time.tick) { at += 2; continue; }\n"
              "        tide_shard_drop(&s->pins, at, 2);\n"
              "        const tide_entity e = tide_shard_unkey(key);\n"
              "        if (tide_shard_is_singleton(e) && s->role != TIDE_SHARD_ROOT_ROLE && tide_shard_owned(w, e)) tide_shard_lend(w, e, TIDE_SHARD_ROOT);\n"
              "    }\n"
              "    tide_shard_handoff(w);\n}\n\n");
}

// What the runtime asks of a world (tide_shard_game)
static void gen_shard_queries(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const int ghost = prog->shard_ghost->index;
    // Whether an entity reaches into `box` past its position (Shards.Spans, as its shard kept it this tick)
    sb_put(o, "TIDE_SHARD_FN bool tide_shard_spans_into(const tide_world *w, const tide_entity e, const tide_shard_box *box)\n{\n"
              "    const tide_shard_state *s = &w->tide_shard_state;\n"
              "    const int32_t at = tide_shard_find(s->spanned, tide_shard_key(e), 1);\n"
              "    if (at < 0) return false;\n"
              "    for (int32_t k = 0; k < (int32_t)TIDE_SHARD_DIMS; k++) {\n"
              "        const double lo = *(const double *)tide_list_at(s->spans, at * 6 + k, 8u);\n"
              "        const double hi = *(const double *)tide_list_at(s->spans, at * 6 + 3 + k, 8u);\n"
              "        if (!(lo < box->hi[k] && hi >= box->lo[k])) return false;\n    }\n"
              "    return true;\n}\n\n");
    // ...and whether an entity is near `box`: its position in it, or its span reaching it
    sb_put(o, "TIDE_SHARD_FN bool tide_shard_near(const tide_world *w, const tide_entity e, const tide_shard_box *box, bool *placed)\n{\n"
              "    double p[3];\n    *placed = tide_shard_where(w, e, p);\n"
              "    return (*placed && tide_shard_box_holds(box, p, TIDE_SHARD_DIMS)) || tide_shard_spans_into(w, e, box);\n}\n\n");
    // The trees this shard owns near `box`: on the root, the ones with no position too, and elsewhere one with
    // no position that goes with an entity near it (a part with its body)
    sb_put(o, "TIDE_SHARD_FN void tide_shard_border(const void *world, const tide_shard_box *box, tide_shard_each each, void *context)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)(uintptr_t)&w->heap, NULL);\n"
              "    const bool root = tide_shard_root(w);\n    (void)root;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (%s) continue;\n", presence_cell(g, "w", a, ghost, "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        if (prog->hierarchy[0]) sb_put(o, "        if (!tide_entity_is_null(tide_parent_of(w, e))) continue;\n");
        sb_put(o, "        bool placed;\n        bool near = tide_shard_near(w, e, box, &placed);\n");
        sb named = {0};
        if (!shard_placed(g, a)) shard_named(g, a, &named);
        if (named.len) { // With no position: near where what it goes with is
            sb_printf(o, "        if (!near && !placed && !root) {\n"
                         "            const tide_entity first = tide_shard_first_named_%d(w, e, true);\n"
                         "            bool first_placed;\n"
                         "            near = !tide_entity_is_null(first) && tide_shard_near(w, first, box, &first_placed);\n        }\n", a);
        }
        sb_put(o, "        if (!near && (placed || !root)) continue;\n"
                  "        tide_writer out = {.grows = true};\n        tide_shard_put_tree(&out, w, e);\n"
                  "        if (out.overflow) tide_out_of_memory();\n"
                  "        each(context, e, out.data, out.size);\n        free(out.data);\n    }\n");
    }
    sb_put(o, "}\n\n");
    // Owned entities: how many, and the box around their positions
    sb_put(o, "TIDE_SHARD_FN uint32_t tide_shard_load(const void *world, tide_shard_box *box)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)(uintptr_t)&w->heap, NULL);\n"
              "    uint32_t count = 0;\n"
              "    for (uint32_t k = 0; k < 3u; k++) { box->lo[k] = 0.0; box->hi[k] = 0.0; }\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (%s) continue;\n", presence_cell(g, "w", a, ghost, "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "        double p[3];\n        const bool placed = tide_shard_where(w, e, p);\n"
                  "        if (placed) {\n"
                  "            for (uint32_t k = 0; k < 3u; k++) {\n"
                  "                if (!count || p[k] < box->lo[k]) box->lo[k] = p[k];\n"
                  "                if (!count || p[k] > box->hi[k]) box->hi[k] = p[k];\n            }\n        }\n"
                  "        count++;\n    }\n");
    }
    sb_put(o, "    return count;\n}\n\n");
    sb_put(o, "TIDE_SHARD_FN uint32_t tide_shard_sample(const void *world, const uint32_t stride, double (*out)[3], const uint32_t capacity)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)(uintptr_t)&w->heap, NULL);\n"
              "    uint32_t seen = 0, count = 0;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count && count < capacity; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (%s) continue;\n", presence_cell(g, "w", a, ghost, "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "        double p[3];\n        if (!tide_shard_where(w, e, p)) continue;\n"
                  "        if (stride > 1u && seen++ % stride) continue;\n"
                  "        memcpy(out[count++], p, sizeof p);\n    }\n");
    }
    sb_put(o, "    return count;\n}\n\n");
    // Each player owning entities here: how many, and where
    sb_put(o, "TIDE_SHARD_FN uint32_t tide_shard_presence_of(const void *world, tide_shard_presence *out, const uint32_t capacity)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)(uintptr_t)&w->heap, NULL);\n"
              "    uint32_t count = 0;\n    (void)out;\n    (void)capacity;\n");
    if (shard_uses(prog, prog->owner)) {
        const char *owner = type_cname(prog->owner);
        const char *player = field_cname(&prog->owner->fields.items[0]);
        for (int a = 0; a < prog->archetypes.count; a++) {
            if (arch_local(g, a) || !arch_stores(g, a, prog->owner->index)) continue;
            sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
            sb_printf(o, "        if (%s) continue;\n", presence_cell(g, "w", a, ghost, "row", false));
            if (comp_sparse(g, prog->owner->index)) sb_printf(o, "        if (!%s) continue;\n", presence_cell(g, "w", a, prog->owner->index, "row", false));
            sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
            sb_printf(o, "        const %s owner = tide_get_%s(w, e);\n", owner, owner);
            sb_printf(o, "        if (!owner.%s.id) continue;\n", player);
            sb_put(o, "        double p[3];\n        const bool placed = tide_shard_where(w, e, p);\n"
                      "        uint32_t i = 0;\n");
            sb_printf(o, "        while (i < count && i < capacity && out[i].player.id != owner.%s.id) i++;\n", player);
            sb_printf(o, "        if (i == count) {\n            if (count < capacity) out[count] = (tide_shard_presence){owner.%s, 0u, {{0}, {0}}};\n"
                         "            count++;\n        }\n", player);
            sb_put(o, "        if (i >= capacity) continue;\n"
                      "        if (placed) {\n"
                      "            for (uint32_t k = 0; k < 3u; k++) {\n"
                      "                if (!out[i].count || p[k] < out[i].box.lo[k]) out[i].box.lo[k] = p[k];\n"
                      "                if (!out[i].count || p[k] > out[i].box.hi[k]) out[i].box.hi[k] = p[k];\n            }\n"
                      "            out[i].count++;\n        }\n    }\n");
        }
    }
    sb_put(o, "    return count;\n}\n\n");
    sb_put(o, "TIDE_SHARD_FN void tide_shard_ghosts_of(const void *world, void (*each)(void *context, tide_entity e, int32_t source),\n"
              "                                       void *context)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)(uintptr_t)&w->heap, NULL);\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (!%s) continue;\n", presence_cell(g, "w", a, ghost, "row", false));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        if (prog->hierarchy[0]) sb_put(o, "        if (!tide_entity_is_null(tide_parent_of(w, e))) continue;\n");
        sb_put(o, "        each(context, e, tide_get_tide_ghost(w, e).source);\n    }\n");
    }
    sb_put(o, "}\n\n");
    sb_put(o, "TIDE_SHARD_FN void tide_shard_owned_of(const void *world, void (*each)(void *context, tide_entity e), void *context)\n{\n"
              "    const tide_world *w = world;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (!%s) each(context, *%s);\n    }\n", presence_cell(g, "w", a, ghost, "row", false),
                  arch_cell(g, "w", a, 0, "tide_entity", "row", false));
    }
    sb_put(o, "}\n\n");
    sb_put(o, "TIDE_SHARD_FN uint32_t tide_shard_route_of(const void *world, const tide_player_id player, uint32_t *home,\n"
              "                                          tide_shard_link *out, const uint32_t capacity)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)(uintptr_t)&w->heap, NULL);\n"
              "    const tide_list routes = w->tide_shard_state.routes;\n"
              "    const int32_t at = tide_shard_route_at(routes, (int64_t)player.id);\n"
              "    if (at < 0) return 0u;\n"
              "    *home = (uint32_t)tide_shard_long(routes, at + 1);\n"
              "    const uint32_t count = (uint32_t)tide_shard_long(routes, at + 2);\n"
              "    for (uint32_t i = 0; i < count && i < capacity; i++) {\n"
              "        out[i].shard = (uint32_t)tide_shard_long(routes, at + 3 + 2 * (int32_t)i);\n"
              "        out[i].match = (uint64_t)tide_shard_long(routes, at + 4 + 2 * (int32_t)i);\n    }\n"
              "    return count;\n}\n\n");
    // The regions, as a machine watching an area finds the shards owning it
    sb_put(o, "TIDE_SHARD_FN uint32_t tide_shard_regions_of(const void *world, tide_shard_region *out, const uint32_t capacity)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)(uintptr_t)&w->heap, NULL);\n"
              "    const tide_shard_state *s = &w->tide_shard_state;\n"
              "    const uint32_t count = (uint32_t)(tide_list_count(s->regions) / 7);\n"
              "    for (uint32_t i = 0; i < count && i < capacity; i++) {\n"
              "        const int32_t at = (int32_t)(i * 7u);\n"
              "        out[i].shard = (uint32_t)*(const double *)tide_list_at(s->regions, at, 8u);\n"
              "        for (int32_t k = 0; k < 3; k++) {\n"
              "            out[i].box.lo[k] = *(const double *)tide_list_at(s->regions, at + 1 + k, 8u);\n"
              "            out[i].box.hi[k] = *(const double *)tide_list_at(s->regions, at + 4 + k, 8u);\n        }\n"
              "        out[i].match = (uint64_t)tide_shard_long(s->matches, (int32_t)i);\n    }\n"
              "    return count;\n}\n\n");
    sb_put(o, "TIDE_SHARD_FN void tide_shard_status_of(const void *world, tide_shard_status *out)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)(uintptr_t)&w->heap, NULL);\n"
              "    const tide_shard_state *s = &w->tide_shard_state;\n"
              "    memset(out, 0, sizeof *out);\n"
              "    out->id = s->shard - 1;\n    out->role = (uint32_t)s->role;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        if (%s) out->ghosts++;\n        else out->owned++;\n    }\n", presence_cell(g, "w", a, ghost, "row", false));
    }
    sb_put(o, "    uint32_t size;\n    bool copied;\n"
              "    const uint8_t *flat = tide_shard_bytes_flat(s->waits, &size, &copied);\n"
              "    for (uint32_t at = 0; at + 8u <= size;) {\n"
              "        tide_reader r = {flat + at, size - at, 0, false};\n"
              "        const uint32_t record = tide_read_u32(&r);\n"
              "        if (record < 8u || record > size - at) break;\n"
              "        if (tide_read_u32(&r) == TIDE_SHARD_LEND) out->lent++;\n        else out->waiting++;\n"
              "        at += record;\n    }\n"
              "    tide_shard_bytes_done(s->waits, flat, copied);\n"
              "    flat = tide_shard_bytes_flat(s->outbox, &size, &copied);\n"
              "    tide_reader r = {flat, size, 0, false};\n    tide_shard_outgoing m;\n"
              "    while (tide_shard_next_outgoing(&r, &m)) out->outbox++;\n"
              "    tide_shard_bytes_done(s->outbox, flat, copied);\n"
              "    out->indices = s->end > s->next ? (uint64_t)(s->end - s->next) : 0u;\n"
              "    const int32_t n = tide_list_count(s->ranges);\n"
              "    for (int32_t i = 0; i + 2 <= n; i += 2) {\n"
              "        if (tide_shard_long(s->ranges, i) >= s->end) out->indices += (uint64_t)(tide_shard_long(s->ranges, i + 1) - tide_shard_long(s->ranges, i));\n"
              "        if ((uint64_t)tide_shard_long(s->ranges, i + 1) > out->ranges_end) out->ranges_end = (uint64_t)tide_shard_long(s->ranges, i + 1);\n"
              "    }\n}\n\n");
}

// Starting a shard: the root as a match starts, a region with nothing in it,
// at the world's tick, then told who it is (a CONFIG message)
static void gen_shard_start(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    sb_printf(o, "TIDE_SHARD_FN void tide_shard_start(void *world, const float dt, const void *start, const uint32_t seed, const uint32_t tick,\n"
                 "                             const uint8_t *config, const uint32_t size)\n{\n"
                 "    tide_world *w = world;\n"
                 "    tide_shard_starting_empty = !(size >= 14u && config[13] == TIDE_SHARD_ROOT_ROLE);\n"
                 "    %s;\n"
                 "    tide_shard_starting_empty = false;\n"
                 "    w->Time.tick = (int32_t)tick;\n    w->Time.t = (float)w->Time.tick * w->Time.dt;\n"
                 "    (void)tide_shard_receive(w, config, size);\n}\n\n",
              prog->uses_seed ? "tide_world_start_seeded(w, dt, start, seed)" : "(void)seed;\n    tide_world_start(w, dt, start)");
    sb_printf(o, "static const tide_shard_game tide_shard_api = {\n"
                 "    .range = TIDE_SHARD_REACH,\n    .dims = TIDE_SHARD_DIMS,\n    .start = tide_shard_start,\n"
                 "    .receive = tide_shard_receive,\n    .outbox = tide_shard_outbox,\n    .border = tide_shard_border,\n"
                 "    .singletons = tide_shard_singletons_of,\n    .load = tide_shard_load,\n    .sample = tide_shard_sample,\n"
                 "    .presence = tide_shard_presence_of,\n    .status = tide_shard_status_of,\n    .ghosts = tide_shard_ghosts_of,\n"
                 "    .route = tide_shard_route_of,\n    .regions = tide_shard_regions_of,\n    .owned = tide_shard_owned_of,\n};\n\n");
}

// A scene unloaded on the shard that owns it: its entities here go too
static void gen_shard_unload(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    if (!has_scenes(prog, false)) return;
    sb_put(o, "TIDE_SHARD_FN void tide_shard_unload_members(tide_world *w, const tide_entity scene)\n{\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        const char *name = arch_name(g, a);
        sb_printf(o, "    for (uint32_t i = w->%s.count; i-- > 0;) {\n", name);
        sb_printf(o, "        if (i >= w->%s.count) continue;\n", name);
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "i", false));
        sb_printf(o, "        if (!tide_entity_equal(*%s, scene) || tide_entity_equal(e, scene)) continue;\n",
                  arch_cell(g, "w", a, 1, "tide_entity", "i", false));
        sb_put(o, "        if (tide_has_tide_ghost(w, e)) tide_shard_remove_tree(w, e, true);\n"
                  "        else tide_destroy_now(w, e);\n    }\n");
    }
    sb_put(o, "}\n\n");
}

// A sharded world's tick: it runs, and when code touched what this shard
// doesn't own, the world goes back to how the tick found it and the tick runs
// again without that code, until a run touches nothing it shouldn't. Each run
// is the same on the server and its players, so they agree with nothing sent.
// Then what waited is asked for, and what left the region moves.
static void gen_shard_tick(gen *g)
{
    const program *prog = g->prog;
    sb *o = &g->c;
    const bool hears = program_hears(prog);
    sb_put(o, "static void tide_world_tick_once(tide_world *w, const tide_jobs *jobs);\n\n"
              "void tide_world_tick_on(tide_world *w, const tide_jobs *jobs)\n{\n"
              "    tide_shard_use(w, tide_shard_owns_world, tide_shard_moves_world);\n"
              "    if (!w->tide_shard_state.shard) {\n        tide_world_tick_once(w, jobs);\n        return;\n    }\n"
              "    tide_text_use(&w->heap, NULL); // Its lists are read in its own heap, whatever world this thread ran last\n"
              "    tide_shard_apply_shared(w); // The root's fields of singletons holding [Native] state, whatever was written here\n"
              "    tide_shard_tick t = {0};\n    tide_world before = {0};\n    tide_world_copy(&before, w);\n");
    if (hears) {
        sb_put(o, "    const uint32_t heard_size = tide_listening ? tide_listening->size : 0u;\n"
                  "    const uint32_t heard_count = tide_listening ? tide_listening->count : 0u;\n"
                  "    const uint32_t heard_first = tide_listening ? tide_listening->first_tick : 0u;\n");
    }
    sb_put(o, "    for (;;) {\n"
              "        tide_shard_tick_begin(&t, TIDE_SHARD_CODES);\n"
              "        w->tide_shard_now = &t;\n"
              "        tide_shard_code = 0u;\n        tide_shard_this = (tide_entity){0};\n        tide_shard_occurrence = 0u;\n"
              "        tide_world_tick_once(w, jobs);\n"
              "        w->tide_shard_now = NULL;\n        tide_shard_code = 0u;\n"
              "        if (!t.noted_count) break;\n"
              "        if (t.unknown || t.runs >= TIDE_SHARD_RUNS) { // No run can leave it out: what it changed across the border is dropped\n"
              "            tide_shard_note_dropped(t.noted_count);\n            break;\n        }\n"
              "        tide_shard_tick_again(&t);\n"
              "        tide_world_copy(w, &before);\n");
    if (hears) {
        sb_put(o, "        if (tide_listening) {\n"
                  "            tide_listening->size = heard_size;\n            tide_listening->count = heard_count;\n"
                  "            tide_listening->first_tick = heard_first;\n        }\n");
    }
    sb_put(o, "    }\n"
              "    tide_world_free(&before);\n"
              "    tide_text_use(&w->heap, NULL);\n"
              "    tide_shard_after(w, &t);\n"
              "    tide_heap_flush(&w->heap);\n    tide_heap_settle(&w->heap);\n"
              "    tide_shard_tick_free(&t);\n}\n\n");
}

// Tasks go with the entities they belong to (after gen_tasks: what a frame
// holds is only known once their code is): an entity that moves or is lent
// takes its waiting tasks with it, in the order they started, and they go on
// there. The frames go through value codecs of their own, appended to the rest.
static void gen_shard_tasks(gen *g)
{
    if (!sharded(g)) return;
    const program *prog = g->prog;
    sb *o = &g->c;
    client_codec *codec = &g->value_codec;
    const int first = codec->count;
    bool any = false;
    for (int i = 0; i < g->tasks.count; i++) {
        const task_info *ti = &g->tasks.items[i];
        if (!task_table_in(ti->fn, false)) continue;
        any = true;
        client_codec_type(codec, (type){TY_STRUCT, ti->frame});
    }
    sb_put(o, "// ---------------------------------------------------------------------------\n"
              "// Shards: tasks go with their entities\n\n");
    for (int i = first; i < codec->count; i++) {
        const char *ct = elem_ctype(codec->items[i]);
        sb_printf(o, "TIDE_HELPER void tide_client_put%d(tide_writer *, const %s *, uint32_t);\n", i, ct);
        sb_printf(o, "TIDE_HELPER bool tide_client_get%d(tide_reader *, %s *, uint32_t, uint32_t, uint64_t *);\n", i, ct);
    }
    for (int i = first; i < codec->count; i++) gen_client_codec_value(g, codec, i);
    if (!any) {
        sb_put(o, "TIDE_SHARD_FN void tide_shard_send_tasks(tide_writer *out, tide_world *w, const tide_entity root)\n{\n"
                  "    (void)w;\n    (void)root;\n    tide_write_u32(out, 0u);\n}\n\n"
                  "TIDE_SHARD_FN bool tide_shard_take_tasks(tide_world *w, tide_reader *in)\n{\n"
                  "    (void)w;\n    return tide_read_u32(in) == 0u && !in->failed;\n}\n\n");
        return;
    }
    // Whether `e` is `root` or one of its descendants
    sb_put(o, "TIDE_HELPER bool tide_shard_in_tree(const tide_world *w, const tide_entity root, tide_entity e)\n{\n");
    if (prog->hierarchy[0]) {
        sb_put(o, "    for (uint64_t left = (uint64_t)w->entities.next_unused + 1u; !tide_entity_is_null(e) && left--; e = tide_parent_of(w, e)) {\n"
                  "        if (tide_entity_equal(e, root)) return true;\n    }\n    return false;\n}\n\n");
    } else {
        sb_put(o, "    (void)w;\n    return tide_entity_equal(e, root);\n}\n\n");
    }
    // The tree's tasks, written and taken out of this world: how many, then
    // each one's kind, wake, owner, scene and frame
    sb_put(o, "TIDE_SHARD_FN void tide_shard_send_tasks(tide_writer *out, tide_world *w, const tide_entity root)\n{\n"
              "    uint32_t count = 0, room = 0;\n    tide_due *going = NULL;\n");
    for (int i = 0; i < g->tasks.count; i++) {
        const task_info *ti = &g->tasks.items[i];
        if (!task_table_in(ti->fn, false)) continue;
        const char *id = task_id(ti->fn);
        sb_printf(o, "    for (uint32_t row = 0; row < w->tide_tasks_%s.count; row++) {\n"
                     "        const tide_task_%s *t = tide_table_get(&w->tide_tasks_%s, &tide_columns_tasks_%s, row, 0);\n"
                     "        if (t->task.wake < 0 || !tide_shard_in_tree(w, root, t->task.owner)) continue;\n"
                     "        if (count == room) {\n            room = room ? room * 2u : 8u;\n"
                     "            going = realloc(going, room * sizeof *going);\n            if (!going) tide_out_of_memory();\n        }\n"
                     "        going[count++] = (tide_due){.order = t->task.order, .kind = %du, .row = row};\n    }\n",
                  id, id, id, id, i);
    }
    sb_put(o, "    if (count) qsort(going, count, sizeof *going, tide_due_compare);\n"
              "    tide_write_u32(out, count);\n"
              "    for (uint32_t k = 0; k < count; k++) {\n"
              "        tide_write_u32(out, going[k].kind);\n"
              "        switch (going[k].kind) {\n");
    for (int i = 0; i < g->tasks.count; i++) {
        const task_info *ti = &g->tasks.items[i];
        if (!task_table_in(ti->fn, false)) continue;
        const char *id = task_id(ti->fn);
        sb_printf(o, "        case %du: {\n"
                     "            const tide_task_%s *t = tide_table_get(&w->tide_tasks_%s, &tide_columns_tasks_%s, going[k].row, 0);\n"
                     "            tide_write_u32(out, (uint32_t)t->task.wake);\n"
                     "            tide_write_u32(out, t->task.owner.index);\n            tide_write_u32(out, t->task.owner.generation);\n"
                     "            tide_write_u32(out, t->task.scene.index);\n            tide_write_u32(out, t->task.scene.generation);\n"
                     "            tide_client_put%d(out, &t->frame, 0);\n            break;\n        }\n",
                  i, id, id, id, client_codec_id(codec, (type){TY_STRUCT, ti->frame}));
    }
    sb_put(o, "        default: break;\n        }\n    }\n    free(going);\n");
    // ...then out of this world, from the last row, as a row removed takes the last one's place
    for (int i = 0; i < g->tasks.count; i++) {
        const task_info *ti = &g->tasks.items[i];
        if (!task_table_in(ti->fn, false)) continue;
        const char *id = task_id(ti->fn);
        sb_printf(o, "    for (uint32_t row = w->tide_tasks_%s.count; row-- > 0;) {\n"
                     "        tide_task_%s *t = tide_table_cell(&w->tide_tasks_%s, &tide_columns_tasks_%s, row, 0);\n"
                     "        if (t->task.wake < 0 || !tide_shard_in_tree(w, root, t->task.owner)) continue;\n"
                     "        tide_release_%s(&t->frame, TIDE_IN_MATCH);\n"
                     "        tide_table_remove_row(&w->tide_tasks_%s, &tide_columns_tasks_%s, row);\n    }\n",
                  id, id, id, id, type_cname(ti->frame), id, id);
    }
    sb_put(o, "}\n\n");
    // Taken into this world, each started here now (a frame that isn't one changes nothing more)
    sb_put(o, "TIDE_SHARD_FN bool tide_shard_take_tasks(tide_world *w, tide_reader *in)\n{\n"
              "    const uint32_t count = tide_read_u32(in);\n"
              "    if (in->failed || count > (in->size - in->at) / 24u) return false;\n"
              "    uint64_t budget = SIZE_MAX;\n"
              "    for (uint32_t k = 0; k < count; k++) {\n"
              "        const uint32_t kind = tide_read_u32(in);\n        const int32_t wake = (int32_t)tide_read_u32(in);\n"
              "        tide_entity owner, scene;\n"
              "        owner.index = tide_read_u32(in);\n        owner.generation = tide_read_u32(in);\n"
              "        scene.index = tide_read_u32(in);\n        scene.generation = tide_read_u32(in);\n"
              "        if (in->failed || wake < 0) return false;\n"
              "        switch (kind) {\n");
    for (int i = 0; i < g->tasks.count; i++) {
        const task_info *ti = &g->tasks.items[i];
        if (!task_table_in(ti->fn, false)) continue;
        const char *id = task_id(ti->fn);
        const char *frame = type_cname(ti->frame);
        sb_printf(o, "        case %du: {\n"
                     "            %s f;\n            memset(&f, 0, sizeof f);\n"
                     "            if (!tide_client_get%d(in, &f, TIDE_IN_MATCH, 0, &budget)) {\n"
                     "                tide_release_%s(&f, TIDE_IN_MATCH);\n                return false;\n            }\n"
                     "            const uint32_t row = tide_table_add(&w->tide_tasks_%s, &tide_columns_tasks_%s);\n"
                     "            tide_task_%s *t = tide_table_cell(&w->tide_tasks_%s, &tide_columns_tasks_%s, row, 0);\n"
                     "            t->task = (tide_task){w->tide_task_order++, wake, owner, scene, 0u};\n"
                     "            t->frame = f;\n            break;\n        }\n",
                  i, frame, client_codec_id(codec, (type){TY_STRUCT, ti->frame}), frame, id, id, id, id, id);
    }
    sb_put(o, "        default: return false;\n        }\n    }\n    return true;\n}\n\n");
}

// Everything after the codecs and the dispatchers, in the order it's called
static void gen_shard_main(gen *g)
{
    if (!sharded(g)) return;
    sb *o = &g->c;
    sb_printf(o, "// ---------------------------------------------------------------------------\n"
                 "// Shards: records, messages and what the runtime calls (tide/shard.h)\n\n"
                 "#define TIDE_SHARD_REACH %.17g\n#define TIDE_SHARD_DIMS %uu\n#define TIDE_SHARD_SINGLETONS %du\n\n",
              g->prog->shard_range, shard_dims(g->prog), shard_singleton_count(g->prog));
    gen_shard_where(g);
    gen_shard_records(g);
    gen_shard_outbox(g);
    gen_shard_pins(g);
    gen_shard_singletons(g);
    gen_shard_moves(g);
    gen_shard_events(g);
    gen_shard_unload(g);
    sb_put(o, "TIDE_SHARD_FN void tide_shard_unloaded(tide_world *w, const tide_entity scene)\n{\n"
              "    if (!w->tide_shard_state.shard || !tide_entity_alive(&w->entities, scene) || tide_has_tide_ghost(w, scene)) return;\n"
              "    tide_writer m = tide_shard_begin(w, TIDE_SHARD_UNLOAD, true);\n"
              "    tide_write_u32(&m, scene.index);\n    tide_write_u32(&m, scene.generation);\n"
              "    tide_shard_post(w, TIDE_SHARD_TO_ALL, 0u, scene, &m);\n}\n\n");
    sb_put(o, "TIDE_SHARD_FN void tide_shard_ask_of(tide_world *w, tide_entity e, int32_t asker);\n\n");
    gen_shard_receive(g);
    gen_shard_lending(g);
    gen_shard_after(g);
    gen_shard_queries(g);
    gen_shard_start(g);
}
