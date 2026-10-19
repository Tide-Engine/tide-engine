// Per-recipient state uses values rather than storage offsets. In particular,
// an otherwise identical client's heap can have a different allocation history.
// This file is included by codegen.c after the ordinary storage helpers.

typedef struct client_codec {
    type *items;
    int count;
    int cap;
} client_codec;

static int client_codec_type(client_codec *codec, const type t)
{
    for (int i = 0; i < codec->count; i++) {
        if (codec->items[i].kind == t.kind && codec->items[i].decl == t.decl) return i;
    }
    const int id = codec->count;
    vec_push(*codec, t);
    if (t.kind == TY_STRUCT || t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_EVENT || t.kind == TY_RECORD
        || t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET
        || t.kind == TY_ARRAY || t.kind == TY_GRID) {
        if (t.decl->array_of) client_codec_type(codec, (type){TY_RECORD, t.decl->array_of});
        for (int i = 0; i < t.decl->fields.count; i++) if (!t.decl->fields.items[i].typed)
            client_codec_type(codec, t.decl->fields.items[i].type);
    }
    return id;
}

static int client_codec_id(const client_codec *codec, const type t)
{
    for (int i = 0; i < codec->count; i++) {
        if (codec->items[i].kind == t.kind && codec->items[i].decl == t.decl) return i;
    }
    abort();
    return 0;
}

static uint64_t client_codec_minbytes(const program *prog, const type t)
{
    if (t.kind == TY_STRING || t.kind == TY_LIST) return 1;
    if (t.kind == TY_MAP || t.kind == TY_SET) return 9;
    if (t.kind == TY_GRID) return 13;
    if (t.kind == TY_RESIDENT) return 0;
    if (t.kind == TY_ARRAY) return (uint64_t)t.decl->array_length * client_codec_minbytes(prog, t.decl->fields.items[0].type);
    if (t.kind == TY_RECORD && t.decl->array_of)
        return (uint64_t)t.decl->array_length * client_codec_minbytes(prog, (type){TY_RECORD, t.decl->array_of});
    if (t.kind == TY_STRUCT || t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_EVENT || t.kind == TY_RECORD) {
        uint64_t bytes = 0;
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            if (f->typed || (f->hidden && t.decl == prog->visibility)) continue;
            bytes += client_codec_minbytes(prog, f->type);
        }
        return bytes;
    }
    return (uint64_t)type_layout(t).size;
}

static void gen_client_codec_value(gen *g, const client_codec *codec, const int id)
{
    const type t = codec->items[id];
    const char *ct = elem_ctype(t);
    sb *o = &g->c;
    sb_printf(o, "TIDE_HELPER void tide_client_put%d(tide_writer *out, const %s *v, uint32_t depth)\n{\n", id, ct);
    sb_put(o, "    (void)out; (void)v;\n    if (depth > 256u) { fputs(\"tide: visibility value nesting exceeds 256; split recursive data across entities\\n\", stderr); abort(); }\n");
    if (t.kind == TY_STRING) {
        sb_put(o, "    const tide_str text = tide_text_view(*v);\n"
                  "    tide_write_varint(out, text.bytes);\n"
                  "    tide_write_bytes(out, text.ptr, text.bytes);\n");
    } else if (t.kind == TY_RECORD && t.decl->array_of) {
        sb_printf(o, "    for (uint32_t i = 0; i < %du; i++) tide_client_put%d(out, &v->at[i], depth + 1u);\n",
                  t.decl->array_length, client_codec_id(codec, (type){TY_RECORD, t.decl->array_of}));
    } else if (t.kind == TY_STRUCT || t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_EVENT || t.kind == TY_RECORD) {
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            if (f->typed || (f->hidden && t.decl == g->prog->visibility)) continue;
            sb_printf(o, "    tide_client_put%d(out, &v->%s, depth + 1u);\n", client_codec_id(codec, f->type), field_cname(f));
        }
        if (!t.decl->fields.count) sb_put(o, "    (void)out; (void)v;\n");
    } else if (t.kind == TY_ARRAY) {
        const type et = t.decl->fields.items[0].type;
        sb_printf(o, "    for (uint32_t i = 0; i < %du; i++) tide_client_put%d(out, &v->at[i], depth + 1u);\n",
                  t.decl->array_length, client_codec_id(codec, et));
    } else if (t.kind == TY_LIST) {
        const type et = t.decl->fields.items[0].type;
        sb_put(o, "    const uint32_t count = (uint32_t)tide_list_count(*v);\n"
                  "    tide_write_varint(out, count);\n");
        sb_printf(o, "    for (uint32_t i = 0; i < count; i++) tide_client_put%d(out, tide_list_at(*v, (int32_t)i, sizeof(%s)), depth + 1u);\n",
                  client_codec_id(codec, et), elem_ctype(et));
    } else if (t.kind == TY_MAP || t.kind == TY_SET) {
        const int map = t.decl->index;
        sb_put(o, "    tide_write_varint(out, v->live);\n"
                  "    tide_write_u64(out, (uint64_t)v->last_order);\n"
                  "    int64_t order = 0;\n    int32_t next = 0;\n    int32_t i;\n");
        sb_printf(o, "    while ((i = tide_map_next(*v, &tide_map%d_kind, &order, &next)) >= 0) {\n", map);
        sb_printf(o, "        const tide_map%d_entry *entry = tide_map_entry(*v, i, &tide_map%d_kind);\n", map, map);
        sb_put(o, "        tide_write_u64(out, (uint64_t)order);\n");
        sb_printf(o, "        tide_client_put%d(out, &entry->key, depth + 1u);\n", client_codec_id(codec, t.decl->fields.items[0].type));
        if (t.kind == TY_MAP) sb_printf(o, "        tide_client_put%d(out, &entry->value, depth + 1u);\n", client_codec_id(codec, t.decl->fields.items[1].type));
        sb_put(o, "    }\n");
    } else if (t.kind == TY_GRID) {
        const type cell = t.decl->fields.items[0].type;
        sb_put(o, "    const tide_grid_dir *dir = tide_grid_dir_of(*v);\n"
                  "    int32_t size[3] = {0};\n    tide_grid_size(*v, size);\n"
                  "    tide_write_bytes(out, size, sizeof size);\n"
                  "    tide_write_varint(out, dir ? dir->records : 0u);\n"
                  "    if (dir) {\n"
                  "        if ((uint64_t)dir->records * sizeof(tide_grid_record) > SIZE_MAX) tide_out_of_memory();\n"
                  "        tide_grid_record *records = malloc((size_t)dir->records * sizeof *records);\n"
                  "        if (!records && dir->records) tide_out_of_memory();\n"
                  "        if (dir->records) memcpy(records, tide_grid_records(dir), (size_t)dir->records * sizeof *records);\n"
                  "        if (dir->records > 1u) qsort(records, dir->records, sizeof *records, tide_client_grid_compare);\n"
                  "        for (uint32_t i = 0; i < dir->records; i++) {\n"
                  "            tide_write_bytes(out, records[i].chunk, sizeof records[i].chunk);\n");
        sb_printf(o, "            const uint8_t *cells = tide_grid_chunk_at(tide_heap_read(v->at >> 30), records[i].block);\n"
                     "            for (uint32_t k = 0; k < tide_grid_chunk_bytes(&%s) / sizeof(%s); k++)\n"
                     "                tide_client_put%d(out, (const %s *)(const void *)(cells + (size_t)k * sizeof(%s)), depth + 1u);\n",
                  grid_shape_name(t.decl), elem_ctype(cell), client_codec_id(codec, cell), elem_ctype(cell), elem_ctype(cell));
        sb_put(o, "        }\n        free(records);\n    }\n");
    } else if (t.kind == TY_RESIDENT) {
        // Opaque C state may cache hidden entities; only its visible ordinary
        // component outcomes belong in a recipient's projection.
        sb_put(o, "    // Opaque resident storage remains authoritative server state.\n");
    } else {
        sb_put(o, "    tide_write_bytes(out, v, sizeof *v);\n");
    }
    sb_put(o, "}\n\n");

    sb_printf(o, "TIDE_HELPER bool tide_client_get%d(tide_reader *in, %s *v, uint32_t where, uint32_t depth, uint64_t *budget)\n{\n", id, ct);
    sb_put(o, "    if (in->failed || depth > 256u) return false;\n    (void)where; (void)v; (void)budget;\n");
    if (t.kind == TY_STRING) {
        sb_put(o, "    const uint32_t size = tide_read_varint(in);\n"
                  "    const uint8_t *bytes = tide_read_bytes(in, size);\n"
                  "    if (!bytes || !tide_client_budget(budget, (uint64_t)size + 64u)) return false;\n"
                  "    if (size > INT32_MAX || !tide_utf8_valid((const char *)bytes, (int32_t)size)) return false;\n"
                  "    const tide_str text = {(const char *)bytes, (int32_t)size, tide_utf8_chars((const char *)bytes, (int32_t)size)};\n"
                  "    tide_text_set(v, text, where);\n");
    } else if (t.kind == TY_RECORD && t.decl->array_of) {
        sb_printf(o, "    for (uint32_t i = 0; i < %du; i++) if (!tide_client_get%d(in, &v->at[i], where, depth + 1u, budget)) return false;\n",
                  t.decl->array_length, client_codec_id(codec, (type){TY_RECORD, t.decl->array_of}));
    } else if (t.kind == TY_STRUCT || t.kind == TY_COMPONENT || t.kind == TY_SINGLETON || t.kind == TY_INPUT || t.kind == TY_EVENT || t.kind == TY_RECORD) {
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            if (f->typed || (f->hidden && t.decl == g->prog->visibility)) continue;
            sb_printf(o, "    if (!tide_client_get%d(in, &v->%s, where, depth + 1u, budget)) return false;\n",
                      client_codec_id(codec, f->type), field_cname(f));
        }
        if (!t.decl->fields.count) sb_put(o, "    (void)v;\n");
    } else if (t.kind == TY_ARRAY) {
        const type et = t.decl->fields.items[0].type;
        sb_printf(o, "    for (uint32_t i = 0; i < %du; i++) if (!tide_client_get%d(in, &v->at[i], where, depth + 1u, budget)) return false;\n",
                  t.decl->array_length, client_codec_id(codec, et));
    } else if (t.kind == TY_LIST) {
        const type et = t.decl->fields.items[0].type;
        sb_put(o, "    const uint32_t count = tide_read_varint(in);\n"
                  "    if (count > INT32_MAX) return false;\n");
        const uint64_t minbytes = client_codec_minbytes(g->prog, et);
        if (minbytes) sb_printf(o, "    if (count > (in->size - in->at) / %lluULL) return false;\n", (unsigned long long)minbytes);
        sb_printf(o, "    if (!tide_client_budget(budget, (uint64_t)count * (sizeof(%s) + 64u))) return false;\n", elem_ctype(et));
        sb_printf(o, "    for (uint32_t i = 0; i < count; i++) {\n        %s *element = tide_list_add(v, sizeof *element, where);\n", elem_ctype(et));
        sb_printf(o, "        if (!element || !tide_client_get%d(in, element, where, depth + 1u, budget)) return false;\n    }\n", client_codec_id(codec, et));
    } else if (t.kind == TY_MAP || t.kind == TY_SET) {
        const int map = t.decl->index;
        const type kt = t.decl->fields.items[0].type;
        sb_put(o, "    const uint32_t count = tide_read_varint(in);\n"
                  "    const int64_t last = (int64_t)tide_read_u64(in);\n"
                  "    if (last < 0 || count > INT32_MAX || count > (in->size - in->at) / 8u) return false;\n");
        sb_printf(o, "    if (!tide_client_budget(budget, (uint64_t)count * (sizeof(tide_map%d_entry) + 64u))) return false;\n", map);
        sb_put(o, "    int64_t previous = 0;\n"
                  "    for (uint32_t i = 0; i < count; i++) {\n"
                  "        const int64_t order = (int64_t)tide_read_u64(in);\n"
                  "        if (order <= previous || order > last) return false;\n"
                  "        previous = order;\n");
        sb_printf(o, "        %s key = {0};\n        if (!tide_client_get%d(in, &key, where, depth + 1u, budget)) return false;\n", elem_ctype(kt), client_codec_id(codec, kt));
        sb_put(o, "        bool added = false;\n");
        if (kt.kind == TY_STRING) {
            sb_put(o, "        const tide_str probe = tide_text_view(key);\n");
            sb_printf(o, "        tide_map%d_entry *entry = tide_map_insert(v, &tide_map%d_kind, &probe, where, &added);\n", map, map);
        } else sb_printf(o, "        tide_map%d_entry *entry = tide_map_insert(v, &tide_map%d_kind, &key, where, &added);\n", map, map);
        sb_put(o, "        if (!entry || !added) return false;\n"
                  "        entry->key = key;\n        entry->tide_order = order;\n");
        if (t.kind == TY_MAP) sb_printf(o, "        if (!tide_client_get%d(in, &entry->value, where, depth + 1u, budget)) return false;\n", client_codec_id(codec, t.decl->fields.items[1].type));
        sb_put(o, "    }\n    v->last_order = last;\n");
    } else if (t.kind == TY_GRID) {
        const type cell = t.decl->fields.items[0].type;
        const uint64_t cell_min = client_codec_minbytes(g->prog, cell);
        sb_put(o, "    const uint8_t *sizes = tide_read_bytes(in, 3u * sizeof(int32_t));\n"
                  "    if (!sizes) return false;\n"
                  "    int32_t size[3];\n    memcpy(size, sizes, sizeof size);\n"
                  "    if (size[0] < 0 || size[1] < 0 || size[2] < 0) return false;\n");
        if (t.decl->dims == 2) sb_put(o, "    if (size[2]) return false;\n");
        sb_printf(o, "    *v = tide_grid_new(%du, size[0], size[1], size[2]);\n", t.decl->dims);
        sb_printf(o, "    tide_grid_own(v, &%s, where);\n", grid_shape_name(t.decl));
        sb_put(o, "    const uint32_t count = tide_read_varint(in);\n");
        sb_printf(o, "    const tide_grid_shape *shape = &%s;\n", grid_shape_name(t.decl));
        sb_put(o, "    const uint32_t bytes = tide_grid_chunk_bytes(shape);\n");
        sb_printf(o, "    if (count > (in->size - in->at) / (12u + (uint64_t)(bytes / sizeof(%s)) * %lluULL)) return false;\n",
                  elem_ctype(cell), (unsigned long long)cell_min);
        sb_put(o, "    if (!tide_client_budget(budget, (uint64_t)count * ((uint64_t)bytes + 128u))) return false;\n"
                  "    int32_t previous[3] = {0};\n"
                  "    for (uint32_t i = 0; i < count; i++) {\n"
                  "        const uint8_t *position = tide_read_bytes(in, 12);\n"
                  "        if (!position) return false;\n"
                  "        int32_t chunk[3];\n        memcpy(chunk, position, sizeof chunk);\n"
                  "        if (i && tide_client_grid_position_compare(previous, chunk) >= 0) return false;\n"
                  "        memcpy(previous, chunk, sizeof previous);\n"
                  "        if (shape->dims == 2u && chunk[2]) return false;\n"
                  "        int64_t first[3];\n"
                  "        for (uint32_t k = 0; k < 3; k++) {\n"
                  "            first[k] = (int64_t)chunk[k] * ((int64_t)1 << shape->shift[k]);\n"
                  "            if (first[k] < INT32_MIN || first[k] > INT32_MAX) return false;\n"
                  "        }\n"
                  "        uint8_t *target = tide_grid_poke(v, (int32_t)first[0], (int32_t)first[1], (int32_t)first[2], shape, true, where);\n"
                  "        if (!target) return false;\n");
        sb_printf(o, "        for (uint32_t k = 0; k < bytes / sizeof(%s); k++)\n"
                     "            if (!tide_client_get%d(in, (%s *)(void *)(target + (size_t)k * sizeof(%s)), where, depth + 1u, budget)) return false;\n    }\n",
                  elem_ctype(cell), client_codec_id(codec, cell), elem_ctype(cell), elem_ctype(cell));
    } else if (t.kind == TY_RESIDENT) {
        sb_put(o, "    *v = (tide_resident){0};\n");
    } else if (t.kind == TY_BOOL) {
        sb_put(o, "    const uint8_t value = tide_read_u8(in);\n"
                  "    if (value > 1u) return false;\n    *v = value != 0;\n");
    } else {
        sb_put(o, "    const uint8_t *bytes = tide_read_bytes(in, sizeof *v);\n"
                  "    if (!bytes) return false;\n    memcpy(v, bytes, sizeof *v);\n");
    }
    sb_put(o, "    return !in->failed;\n}\n\n");
}

static void gen_client_codecs(gen *g, client_codec *codec)
{
    sb *o = &g->c;
    sb_put(o, "TIDE_HELPER bool tide_client_budget(uint64_t *remaining, uint64_t bytes)\n{\n"
              "    if (bytes > *remaining) return false;\n    *remaining -= bytes;\n    return true;\n}\n\n");
    for (int i = 0; i < codec->count; i++) {
        if (codec->items[i].kind != TY_GRID) continue;
        sb_put(o, "TIDE_HELPER int tide_client_grid_position_compare(const int32_t a[3], const int32_t b[3])\n{\n"
                  "    for (int k = 2; k >= 0; k--) { if (a[k] < b[k]) return -1; if (a[k] > b[k]) return 1; }\n"
                  "    return 0;\n}\n\n"
                  "static int tide_client_grid_compare(const void *left, const void *right)\n{\n"
                  "    const tide_grid_record *a = left, *b = right;\n"
                  "    return tide_client_grid_position_compare(a->chunk, b->chunk);\n}\n\n");
        break;
    }
    for (int i = 0; i < codec->count; i++) {
        const char *ct = elem_ctype(codec->items[i]);
        sb_printf(o, "TIDE_HELPER void tide_client_put%d(tide_writer *, const %s *, uint32_t);\n", i, ct);
        sb_printf(o, "TIDE_HELPER bool tide_client_get%d(tide_reader *, %s *, uint32_t, uint32_t, uint64_t *);\n", i, ct);
    }
    sb_put(o, "\n");
    for (int i = 0; i < codec->count; i++) gen_client_codec_value(g, codec, i);
}

static bool client_singleton(const program *prog, const decl *d)
{
    return !d->is_local && d != prog->visibility_state && d != prog->hierarchy[0];
}

static void gen_client_hierarchy_codec(gen *g)
{
    const program *prog = g->prog;
    if (!prog->hierarchy[0]) return;
    sb *o = &g->c;
    sb_printf(o, "TIDE_HELPER bool tide_client_hierarchy_cut(const tide_world *w, tide_entity e, tide_player_id recipient)\n{\n"
                 "    const tide_entity parent = tide_hierarchy_get(w, e).parent;\n"
                 "    return !tide_entity_is_null(parent) && (!tide_visibility_entity(w, parent, recipient)\n"
                 "        || (tide_has_Transform(w, parent) && !tide_visibility_component(w, parent, %du, recipient)));\n}\n\n",
              prog->transform->index);
    sb_put(o, "TIDE_HELPER tide_entity tide_client_visible_sibling(const tide_world *w, tide_entity e, tide_player_id recipient, bool next)\n{\n"
              "    for (uint32_t left = w->entities.next_unused; !tide_entity_is_null(e) && left; left--) {\n"
              "        if (tide_visibility_entity(w, e, recipient) && !tide_client_hierarchy_cut(w, e, recipient)) return e;\n"
              "        const tide_hierarchy_node node = tide_hierarchy_get(w, e);\n"
              "        e = next ? node.next : node.previous;\n    }\n    return (tide_entity){0};\n}\n\n"
              "TIDE_HELPER tide_hierarchy_node tide_client_hierarchy_node(const tide_world *w, tide_entity e, tide_player_id recipient)\n{\n"
              "    tide_hierarchy_node node = tide_hierarchy_get(w, e);\n"
              "    node.first = tide_client_visible_sibling(w, node.first, recipient, true);\n"
              "    if (tide_entity_is_null(node.parent) || tide_client_hierarchy_cut(w, e, recipient)) {\n"
              "        node.parent = node.next = node.previous = (tide_entity){0};\n"
              "    } else {\n"
              "        node.next = tide_client_visible_sibling(w, node.next, recipient, true);\n"
              "        node.previous = tide_client_visible_sibling(w, node.previous, recipient, false);\n    }\n    return node;\n}\n\n");
    sb_printf(o, "TIDE_HELPER bool tide_client_hierarchy_needs_anchor(const tide_world *w, tide_entity root, tide_player_id recipient)\n{\n"
                 "    tide_entity at = root;\n    uint64_t left = (uint64_t)w->entities.next_unused * 2u + 1u;\n"
                 "    while (left--) {\n"
                 "        if (tide_visibility_component(w, at, %du, recipient)) return true;\n"
                 "        tide_hierarchy_node node = tide_client_hierarchy_node(w, at, recipient);\n"
                 "        if (!tide_entity_is_null(node.first)) { at = node.first; continue; }\n"
                 "        while (!tide_entity_equal(at, root) && tide_entity_is_null(node.next)) {\n"
                 "            if (!left || tide_entity_is_null(node.parent)) return false;\n"
                 "            left--;\n            at = node.parent;\n            node = tide_client_hierarchy_node(w, at, recipient);\n        }\n"
                 "        if (tide_entity_equal(at, root)) return false;\n        at = node.next;\n    }\n    return false;\n}\n\n",
              prog->transform->index);
    const int anchors = prog->visibility_state->fields.items[3].type.decl->index;
    sb_put(o, "TIDE_HELPER tide_float4x4 tide_client_parent_transform(const tide_world *w, tide_entity e, tide_quaternion *rotation)\n{\n"
              "    tide_float4x4 result = tide_identity_f4x4();\n"
              "    tide_quaternion q = tide_identity_q();\n"
              "    uint32_t count = 0;\n"
              "    for (tide_entity at = e; !tide_entity_is_null(at); at = tide_parent_of(w, at)) {\n"
              "        if (!tide_entity_alive(&w->entities, at) || count == w->entities.next_unused) {\n"
              "            *rotation = q;\n            return result;\n        }\n        count++;\n    }\n"
              "    if ((uint64_t)count * sizeof(tide_entity) > SIZE_MAX) { *rotation = q; return result; }\n"
              "    const uint32_t mark = tide_scratch_mark();\n"
              "    tide_entity *chain = tide_scratch_memory((size_t)count * sizeof *chain);\n"
              "    tide_entity at = e;\n"
              "    for (uint32_t i = 0; i < count; i++) { chain[i] = at; at = tide_parent_of(w, at); }\n"
              "    if (count) (void)tide_hierarchy_anchor(w, chain[count - 1u], &result, &q);\n"
              "    for (uint32_t i = count; i-- > 0;) {\n"
              "        Transform t;\n        if (!tide_load_Transform(w, chain[i], &t)) continue;\n"
              "        const tide_quaternion local = tide_normalize_q(t.localRotation);\n"
              "        result = tide_mul_f4x4(result, tide_trs_f4x4(t.localPosition, local, t.localScale));\n"
              "        q = tide_normalize_q(tide_mul_q(q, local));\n    }\n"
              "    tide_scratch_reset(mark);\n    *rotation = q;\n    return result;\n}\n\n");
    sb_printf(o, "TIDE_HELPER bool tide_client_hierarchy_anchor(const tide_world *w, tide_entity e, tide_player_id recipient, tide_visibility_anchor *anchor)\n{\n"
                 "    if (tide_client_hierarchy_cut(w, e, recipient)) {\n"
                 "        if (!tide_client_hierarchy_needs_anchor(w, e, recipient)) return false;\n"
                 "        anchor->matrix = tide_client_parent_transform(w, tide_hierarchy_get(w, e).parent, &anchor->rotation);\n"
                 "        return true;\n    }\n"
                 "    if (!tide_map%d_has(w->tide_visibility_state.anchors, e)) return false;\n"
                 "    *anchor = tide_map%d_get(w->tide_visibility_state.anchors, e);\n    return true;\n}\n\n", anchors, anchors);
    sb_put(o,
              "TIDE_HELPER size_t tide_client_entity_number(const tide_world *w, tide_entity e, const size_t *offsets)\n{\n"
              "    const tide_location loc = tide_entity_location(&w->entities, e);\n"
              "    if (loc.archetype == TIDE_ARCHETYPE_NONE) return SIZE_MAX;\n"
              "    return offsets[loc.archetype] + loc.row;\n}\n\n"
              "TIDE_HELPER bool tide_client_hierarchy_valid(const tide_world *w)\n{\n");
    sb_printf(o, "    size_t offsets[%d] = {0};\n    size_t total = 0;\n", prog->archetypes.count ? prog->archetypes.count : 1);
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    offsets[%d] = total;\n    total += w->%s.count;\n", a, arch_name(g, a));
    }
    sb_put(o, "    uint8_t *colors = total ? calloc(total, 1) : NULL;\n"
              "    if (total && !colors) tide_out_of_memory();\n    bool valid = true;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; valid && row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "        const tide_hierarchy_node node = tide_hierarchy_get(w, e);\n"
                  "        if (!tide_entity_is_null(node.parent) && !tide_entity_alive(&w->entities, node.parent)) { valid = false; break; }\n"
                  "        if (tide_entity_is_null(node.parent) && (!tide_entity_is_null(node.next) || !tide_entity_is_null(node.previous))) { valid = false; break; }\n"
                  "        if (!tide_entity_is_null(node.first)) {\n"
                  "            const tide_hierarchy_node child = tide_hierarchy_get(w, node.first);\n"
                  "            if (!tide_entity_alive(&w->entities, node.first) || !tide_entity_equal(child.parent, e) || !tide_entity_is_null(child.previous)) { valid = false; break; }\n        }\n"
                  "        if (!tide_entity_is_null(node.previous)) {\n"
                  "            const tide_hierarchy_node before = tide_hierarchy_get(w, node.previous);\n"
                  "            if (!tide_entity_alive(&w->entities, node.previous) || !tide_entity_equal(before.parent, node.parent) || !tide_entity_equal(before.next, e)) { valid = false; break; }\n"
                  "        } else if (!tide_entity_is_null(node.parent) && !tide_entity_equal(tide_hierarchy_get(w, node.parent).first, e)) { valid = false; break; }\n"
                  "        if (!tide_entity_is_null(node.next)) {\n"
                  "            const tide_hierarchy_node after = tide_hierarchy_get(w, node.next);\n"
                  "            if (!tide_entity_alive(&w->entities, node.next) || !tide_entity_equal(after.parent, node.parent) || !tide_entity_equal(after.previous, e)) { valid = false; break; }\n        }\n"
                  "        tide_entity at = e;\n"
                  "        while (!tide_entity_is_null(at)) {\n"
                  "            const size_t n = tide_client_entity_number(w, at, offsets);\n"
                  "            if (n == SIZE_MAX || n >= total || (colors[n] & 3u) == 1u) { valid = false; break; }\n"
                  "            if ((colors[n] & 3u) == 2u) break;\n"
                  "            colors[n] = (uint8_t)((colors[n] & 4u) | 1u);\n"
                  "            at = tide_hierarchy_get(w, at).parent;\n        }\n"
                  "        at = e;\n"
                  "        while (valid && !tide_entity_is_null(at)) {\n"
                  "            const size_t n = tide_client_entity_number(w, at, offsets);\n"
                  "            if ((colors[n] & 3u) != 1u) break;\n"
                  "            colors[n] = (uint8_t)((colors[n] & 4u) | 2u);\n"
                  "            at = tide_hierarchy_get(w, at).parent;\n        }\n"
                  "        at = node.first;\n"
                  "        while (valid && !tide_entity_is_null(at)) {\n"
                  "            const size_t n = tide_client_entity_number(w, at, offsets);\n"
                  "            if (n == SIZE_MAX || n >= total || (colors[n] & 4u)) { valid = false; break; }\n"
                  "            colors[n] |= 4u;\n            at = tide_hierarchy_get(w, at).next;\n        }\n    }\n");
    }
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; valid && row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_printf(o, "        if (!tide_entity_is_null(tide_hierarchy_get(w, e).parent) && !(colors[offsets[%d] + row] & 4u)) valid = false;\n    }\n", a);
    }
    sb_put(o, "    free(colors);\n    return valid;\n}\n\n");
}

static void gen_client_input_filter(gen *g)
{
    const program *prog = g->prog;
    if (!prog->input) return;
    sb *o = &g->c;
    sb_put(o, "TIDE_HELPER void tide_client_input_mask(const tide_world *w, tide_player_id recipient, bool allowed[TIDE_GAME_PLAYERS + 1u])\n{\n"
              "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) allowed[slot] = true;\n"
              "    if (w->tide_visibility_state.client) return;\n"
              "    tide_text_use((tide_heap *)&w->heap, NULL);\n"
              "    bool found[TIDE_GAME_PLAYERS + 1u] = {0};\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        if (arch_stores(g, a, prog->owner->index)) {
            sb_printf(o, "        const %s owner = tide_get_%s(w, e);\n", type_cname(prog->owner), type_cname(prog->owner));
            sb_put(o, "        const int32_t owner_index = tide_player_index(owner.player);\n"
                      "        const uint32_t slot = owner_index >= 0 && (uint32_t)owner_index < TIDE_GAME_PLAYERS ? (uint32_t)owner_index : TIDE_GAME_PLAYERS;\n");
        } else sb_put(o, "        const uint32_t slot = TIDE_GAME_PLAYERS;\n");
        sb_put(o, "        found[slot] = true;\n"
                  "        if (slot == (uint32_t)tide_player_index(recipient) || !allowed[slot]) continue;\n"
                  "        if (!tide_visibility_entity(w, e, recipient)) { allowed[slot] = false; continue; }\n");
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const decl *d = prog->components.items[c];
            if (!d->visibility_optional) continue;
            sb_printf(o, "        if (tide_has_%s(w, e) && !tide_visibility_component(w, e, %du, recipient)) allowed[slot] = false;\n",
                      type_cname(d), c);
        }
        sb_put(o, "    }\n");
    }
    sb_put(o, "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++)\n"
              "        allowed[slot] = slot == (uint32_t)tide_player_index(recipient) || (found[slot] && allowed[slot]);\n}\n\n"
              "static void tide_game_filter_client_input(const void *world, tide_player_id recipient, uint32_t slot, void *input)\n{\n"
              "    if (slot > TIDE_GAME_PLAYERS) return;\n"
              "    bool allowed[TIDE_GAME_PLAYERS + 1u];\n"
              "    tide_client_input_mask(world, recipient, allowed);\n"
              "    if (allowed[slot]) return;\n    *(" );
    sb_printf(o, "%s *)input = ", type_cname(prog->input));
    gen_value(g, o, prog->input, NULL, 0);
    sb_put(o, ";\n}\n\n");
    sb_printf(o, "static void tide_game_filter_client_inputs(const void *world, tide_player_id recipient, void *all_slots)\n{\n"
                 "    %s *inputs = all_slots;\n"
                 "    bool allowed[TIDE_GAME_PLAYERS + 1u];\n"
                 "    tide_client_input_mask(world, recipient, allowed);\n"
                 "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n"
                 "        if (!allowed[slot]) inputs[slot] = ", type_cname(prog->input));
    gen_value(g, o, prog->input, NULL, 0);
    sb_put(o, ";\n    }\n}\n\n");
}

static void gen_client_state(gen *g)
{
    const program *prog = g->prog;
    if (!prog->visibility_state) return;
    sb *o = &g->c;
    client_codec codec = {0};
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (client_singleton(prog, d)) client_codec_type(&codec, (type){TY_SINGLETON, (decl *)d});
    }
    for (int i = 0; i < prog->components.count; i++) {
        const decl *d = prog->components.items[i];
        if (!d->is_local) client_codec_type(&codec, (type){TY_COMPONENT, (decl *)d});
    }
    if (prog->input) client_codec_type(&codec, (type){TY_INPUT, prog->input});
    gen_client_codecs(g, &codec);
    gen_client_hierarchy_codec(g);
    gen_client_input_filter(g);

    sb_put(o, "static uint8_t *tide_game_client_state(const void *world, tide_player_id recipient, uint32_t *size)\n{\n"
              "    const tide_world *w = world;\n    tide_text_use((tide_heap *)&w->heap, NULL);\n"
              "    tide_writer out = {.grows = true};\n"
              "    tide_write_u32(&out, 0x31564954u);\n    tide_write_u32(&out, recipient.id);\n");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        if (d->builtin && str_eq_c(d->name, "tide_command_state")) {
            sb_printf(o, "    %s permitted_commands = {0};\n", type_cname(d));
            for (int k = 0; k < d->fields.count; k++) {
                const field *f = &d->fields.items[k];
                uint32_t player = 0;
                int start = 0;
                while (start < f->name.len && (f->name.ptr[start] < '0' || f->name.ptr[start] > '9')) start++;
                for (; start < f->name.len && f->name.ptr[start] >= '0' && f->name.ptr[start] <= '9'; start++)
                    player = player * 10u + (uint32_t)(f->name.ptr[start] - '0');
                sb_printf(o, "    if (recipient.id == %uu) permitted_commands.%s = w->%s.%s;\n", player + 1u,
                          field_cname(f), type_cname(d), field_cname(f));
            }
            sb_printf(o, "    tide_client_put%d(&out, &permitted_commands, 0);\n", client_codec_id(&codec, (type){TY_SINGLETON, (decl *)d}));
        } else sb_printf(o, "    tide_client_put%d(&out, &w->%s, 0);\n", client_codec_id(&codec, (type){TY_SINGLETON, (decl *)d}), type_cname(d));
    }
    if (prog->input) {
        const int input = client_codec_id(&codec, (type){TY_INPUT, prog->input});
        sb_put(o, "    bool allowed[TIDE_GAME_PLAYERS + 1u];\n    tide_client_input_mask(w, recipient, allowed);\n"
                  "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n");
        sb_printf(o, "        %s current = w->inputs[slot], previous = w->previous_inputs[slot];\n", type_cname(prog->input));
        sb_put(o, "        if (!allowed[slot]) { current = ");
        gen_value(g, o, prog->input, NULL, 0);
        sb_put(o, "; previous = current; }\n"
                  "        if (slot != (uint32_t)tide_player_index(recipient)) previous = current;\n");
        sb_printf(o, "        tide_client_put%d(&out, &current, 0);\n        tide_client_put%d(&out, &previous, 0);\n    }\n", input, input);
    }
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_put(o, "    {\n        uint32_t count = 0;\n");
        sb_printf(o, "        for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "            const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "            if (tide_visibility_entity(w, e, recipient)) count++;\n"
                  "        }\n        tide_write_varint(&out, count);\n");
        sb_printf(o, "        for (uint32_t row = 0; row < w->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "            const tide_entity e = *%s;\n", arch_cell(g, "w", a, 0, "tide_entity", "row", false));
        sb_put(o, "            if (!tide_visibility_entity(w, e, recipient)) continue;\n"
                  "            tide_write_u32(&out, e.index);\n            tide_write_u32(&out, e.generation);\n"
                  "            tide_write_u32(&out, tide_entity_snaps(&w->entities, e));\n");
        if (arch_scene_column(g, a)) {
            sb_printf(o, "            const tide_entity scene = *%s;\n", arch_cell(g, "w", a, 1, "tide_entity", "row", false));
            sb_put(o, "            tide_write_bytes(&out, &scene, sizeof scene);\n");
        }
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const decl *d = prog->components.items[c];
            sb_put(o, "            {\n");
            if (comp_sparse(g, c)) {
                sb_printf(o, "                const bool present = tide_visibility_component(w, e, %du, recipient);\n", c);
                sb_put(o, "                tide_write_u8(&out, present ? 1u : 0u);\n                if (present) {\n");
            }
            sb_printf(o, "                const %s value = tide_get_%s(w, e);\n", type_cname(d), type_cname(d));
            sb_printf(o, "                tide_client_put%d(&out, &value, 0);\n", client_codec_id(&codec, (type){TY_COMPONENT, (decl *)d}));
            if (comp_sparse(g, c)) sb_put(o, "                }\n");
            sb_put(o, "            }\n");
        }
        if (prog->hierarchy[0]) {
            sb_put(o, "            const tide_hierarchy_node node = tide_client_hierarchy_node(w, e, recipient);\n"
                      "            tide_write_bytes(&out, &node, sizeof node);\n"
                      "            tide_visibility_anchor anchor = {0};\n"
                      "            const bool anchored = tide_client_hierarchy_anchor(w, e, recipient, &anchor);\n"
                      "            tide_write_u8(&out, anchored ? 1u : 0u);\n"
                      "            if (anchored) tide_write_bytes(&out, &anchor, sizeof anchor);\n");
        }
        sb_put(o, "        }\n    }\n");
    }
    sb_put(o, "    if (out.overflow) tide_out_of_memory();\n    *size = out.size;\n    return out.data;\n}\n\n"
              "static uint64_t tide_game_client_hash(const void *world, tide_player_id recipient)\n{\n"
              "    uint32_t size;\n    uint8_t *bytes = tide_game_client_state(world, recipient, &size);\n"
              "    const uint64_t hash = tide_hash(bytes, size);\n    free(bytes);\n    return hash;\n}\n\n");

    sb_put(o, "TIDE_HELPER bool tide_client_reserve_entity(tide_entities *entities, tide_entity e, uint64_t *budget)\n{\n"
              "    if (!(e.generation & 1u) || (e.generation & TIDE_ENTITY_TEMPORARY) || e.index == UINT32_MAX) return false;\n"
              "    const uint32_t pages = (e.index >> TIDE_ENTITY_PAGE_SHIFT) + 1u;\n"
              "    if (pages > entities->room) {\n"
              "        if (!tide_client_budget(budget, ((uint64_t)pages * 2u + 1u) * sizeof(tide_page *))) return false;\n"
              "        tide_page **base = calloc((size_t)pages * 2u + 1u, sizeof *base);\n"
              "        if (!base) tide_out_of_memory();\n    tide_page **list = base + 1;\n"
              "        if (entities->pages) memcpy(list, entities->page, entities->pages * sizeof *list);\n"
              "        if (entities->page) free(entities->page - 1);\n        entities->page = list;\n        entities->room = pages;\n"
              "    }\n"
              "    if (entities->pages < pages) entities->pages = pages;\n"
              "    if (!entities->page[e.index >> TIDE_ENTITY_PAGE_SHIFT]) {\n"
              "        if (!tide_client_budget(budget, (1u << TIDE_ENTITY_PAGE_SHIFT) * sizeof(tide_entity_slot))) return false;\n"
              "        entities->page[e.index >> TIDE_ENTITY_PAGE_SHIFT] = tide_page_new((1u << TIDE_ENTITY_PAGE_SHIFT) * sizeof(tide_entity_slot));\n    }\n"
              "    if (e.index >= entities->next_unused) entities->next_unused = e.index + 1u;\n"
              "    tide_entity_slot *slot = (tide_entity_slot *)tide_page_data(entities->page[e.index >> TIDE_ENTITY_PAGE_SHIFT])\n"
              "        + (e.index & ((1u << TIDE_ENTITY_PAGE_SHIFT) - 1u));\n"
              "    if (slot->generation & 1u) return false;\n"
              "    slot->generation = e.generation;\n    return true;\n}\n\n"
              "static bool tide_game_load_client_state(void *world, const uint8_t *data, uint32_t size)\n{\n"
              "    tide_world *w = world;\n    tide_world next = {0};\n"
              "    tide_reader in = {data, size, 0, false};\n"
              "    uint64_t budget = SIZE_MAX;\n"
              "    if (tide_read_u32(&in) != 0x31564954u) return false;\n"
              "    const uint32_t recipient = tide_read_u32(&in);\n"
              "    if (!recipient || recipient > TIDE_GAME_PLAYERS) return false;\n"
              "    tide_text_use(&next.heap, NULL);\n    next.tide_visibility_state.client = (int32_t)recipient;\n");
    for (int i = 0; i < prog->singletons.count; i++) {
        const decl *d = prog->singletons.items[i];
        if (!client_singleton(prog, d)) continue;
        sb_printf(o, "    if (!tide_client_get%d(&in, &next.%s, TIDE_IN_MATCH, 0, &budget)) goto invalid;\n",
                  client_codec_id(&codec, (type){TY_SINGLETON, (decl *)d}), type_cname(d));
    }
    if (prog->input) {
        const int input = client_codec_id(&codec, (type){TY_INPUT, prog->input});
        sb_printf(o, "    for (uint32_t slot = 0; slot <= TIDE_GAME_PLAYERS; slot++) {\n"
                     "        if (!tide_client_get%d(&in, &next.inputs[slot], TIDE_IN_MATCH, 0, &budget)\n"
                     "            || !tide_client_get%d(&in, &next.previous_inputs[slot], TIDE_IN_MATCH, 0, &budget)) goto invalid;\n    }\n", input, input);
    }
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_put(o, "    {\n        const uint32_t count = tide_read_varint(&in);\n"
                  "        if (count > (in.size - in.at) / 12u) goto invalid;\n"
                  "        for (uint32_t i = 0; i < count; i++) {\n"
                  "            tide_entity e;\n            e.index = tide_read_u32(&in);\n            e.generation = tide_read_u32(&in);\n"
                  "            const uint32_t snaps = tide_read_u32(&in);\n"
                  "            if (in.failed || !tide_client_reserve_entity(&next.entities, e, &budget)) goto invalid;\n");
        sb_printf(o, "            const uint32_t row = tide_table_add(&next.%s, %s);\n", arch_name(g, a), arch_columns(g, a));
        sb_printf(o, "            *%s = e;\n", arch_cell(g, "(&next)", a, 0, "tide_entity", "row", true));
        sb_printf(o, "            tide_entity_set_location(&next.entities, e, (tide_location){%du, row});\n", a);
        sb_put(o, "            tide_entity_slot *slot = (tide_entity_slot *)tide_page_data(next.entities.page[e.index >> TIDE_ENTITY_PAGE_SHIFT])\n"
                  "                + (e.index & ((1u << TIDE_ENTITY_PAGE_SHIFT) - 1u));\n            slot->snaps = snaps;\n");
        if (arch_scene_column(g, a)) {
            sb_put(o, "            const uint8_t *scene = tide_read_bytes(&in, sizeof(tide_entity));\n            if (!scene) goto invalid;\n");
            sb_printf(o, "            memcpy(%s, scene, sizeof(tide_entity));\n", arch_cell(g, "(&next)", a, 1, "tide_entity", "row", true));
        }
        for (int c = 0; c < prog->components.count; c++) {
            if (!arch_stores(g, a, c)) continue;
            const decl *d = prog->components.items[c];
            sb_put(o, "            {\n");
            if (comp_sparse(g, c)) {
                sb_put(o, "                const uint8_t present = tide_read_u8(&in);\n                if (present > 1u) goto invalid;\n");
                sb_printf(o, "                %s = present;\n", presence_cell(g, "(&next)", a, c, "row", true));
                sb_put(o, "                if (present) {\n");
            }
            sb_printf(o, "                %s value = {0};\n", type_cname(d));
            sb_printf(o, "                if (!tide_client_get%d(&in, &value, TIDE_IN_MATCH, 0, &budget)) goto invalid;\n",
                      client_codec_id(&codec, (type){TY_COMPONENT, (decl *)d}));
            sb_printf(o, "                %s;\n", comp_put(g, "(&next)", a, c, "row", "value"));
            if (comp_sparse(g, c)) sb_put(o, "                }\n");
            sb_put(o, "            }\n");
        }
        if (prog->hierarchy[0]) {
            sb_put(o, "            const uint8_t *bytes = tide_read_bytes(&in, sizeof(tide_hierarchy_node));\n"
                      "            if (!bytes) goto invalid;\n"
                      "            tide_hierarchy_node node = {0};\n            memcpy(&node, bytes, sizeof node);\n"
                      "            tide_hierarchy_put(&next, e, node);\n"
                      "            const uint8_t anchored = tide_read_u8(&in);\n"
                      "            if (anchored > 1u || (anchored && !tide_entity_is_null(node.parent))) goto invalid;\n"
                      "            if (anchored) {\n"
                      "                const uint8_t *value = tide_read_bytes(&in, sizeof(tide_visibility_anchor));\n"
                      "                if (!value) goto invalid;\n"
                      "                tide_visibility_anchor anchor = {0};\n"
                      "                memcpy(&anchor, value, sizeof anchor);\n"
                      "                if (!tide_matrix_finite(anchor.matrix) || !tide_is_finite_f(anchor.rotation.value.x)\n"
                      "                    || !tide_is_finite_f(anchor.rotation.value.y) || !tide_is_finite_f(anchor.rotation.value.z)\n"
                      "                    || !tide_is_finite_f(anchor.rotation.value.w)) goto invalid;\n");
            sb_printf(o, "                tide_map%d_set(&next.tide_visibility_state.anchors, e, anchor, TIDE_IN_MATCH);\n            }\n",
                      prog->visibility_state->fields.items[3].type.decl->index);
        }
        sb_put(o, "        }\n    }\n");
    }
    if (prog->hierarchy[0]) sb_put(o, "    if (!tide_client_hierarchy_valid(&next)) goto invalid;\n");
    sb_put(o, "    if (in.failed || in.at != in.size) goto invalid;\n"
              "    next.entities.free_head = 0; // Unseen slots are reserved, not available for prediction.\n"
              "    tide_world_free(w);\n    *w = next;\n    tide_text_use(&w->heap, NULL);\n    return true;\n"
              "invalid:\n    tide_world_free(&next);\n    tide_text_use(&w->heap, NULL);\n    return false;\n}\n\n"
              "static uint32_t tide_game_client_destroyed(const void *world, const void *previous, tide_player_id recipient, tide_entity *out, uint32_t capacity)\n{\n"
              "    const tide_world *w = world, *had = previous;\n    if (!had) return 0;\n"
              "    tide_text_use((tide_heap *)&had->heap, NULL);\n    uint32_t count = 0;\n");
    for (int a = 0; a < prog->archetypes.count; a++) {
        if (arch_local(g, a)) continue;
        sb_printf(o, "    for (uint32_t row = 0; row < had->%s.count; row++) {\n", arch_name(g, a));
        sb_printf(o, "        const tide_entity e = *%s;\n", arch_cell(g, "had", a, 0, "tide_entity", "row", false));
        sb_put(o, "        if (!tide_entity_alive(&w->entities, e) && tide_visibility_entity(had, e, recipient)) {\n"
                  "            if (out && count < capacity) out[count] = e;\n            count++;\n        }\n    }\n");
    }
    sb_put(o, "    return count;\n}\n\n");
}
