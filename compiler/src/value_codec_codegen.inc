// Value codecs shared by recipient state and recorded local-event fields.
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

// Whether the codec's bytes for `t` are its memory, byte for byte: plain
// numbers, vectors, entities, players and enums, and structs and arrays of them
// with no padding (their fields side by side, in order). A run of such list
// elements goes in and out as one copy, the same bytes as one at a time.
// Bools aren't (a read checks for 0 or 1), nor are T?, text or containers.
static bool client_codec_raw(const program *prog, const type t)
{
    switch (t.kind) {
    case TY_BYTE: case TY_CHAR: case TY_INT: case TY_LONG: case TY_INT2: case TY_INT3: case TY_INT4:
    case TY_FLOAT: case TY_FLOAT2: case TY_FLOAT3: case TY_FLOAT4: case TY_QUATERNION:
    case TY_FLOAT2X2: case TY_FLOAT3X3: case TY_FLOAT4X4:
    case TY_DOUBLE: case TY_DOUBLE2: case TY_DOUBLE3: case TY_DOUBLE4:
    case TY_DOUBLE2X2: case TY_DOUBLE3X3: case TY_DOUBLE4X4:
    case TY_COLOR: case TY_RECT: case TY_ENTITY: case TY_LOCAL_ENTITY: case TY_PLAYER: case TY_ENUM: return true;
    case TY_ARRAY: return client_codec_raw(prog, t.decl->fields.items[0].type);
    case TY_STRUCT: {
        int64_t bytes = 0;
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            if (f->typed || (f->hidden && t.decl == prog->visibility) || !client_codec_raw(prog, f->type)) return false;
            bytes += type_layout(f->type).size;
        }
        return t.decl->fields.count && bytes == type_layout(t).size && bytes < LAYOUT_MOST;
    }
    default: return false;
    }
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
                  "    tide_client_write(out, text.ptr, (uint32_t)text.bytes);\n");
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
                  "    tide_write_varint(out, count);\n"
                  "    tide_list_cache cache = {0};\n");
        if (client_codec_raw(g->prog, et)) {
            // Each run of elements side by side as one copy of its bytes
            sb_printf(o, "    _Static_assert(sizeof(%s) == %d, \"a list element's codec bytes are its memory\");\n",
                      elem_ctype(et), type_layout(et).size);
            sb_printf(o, "    for (uint32_t i = 0; i < count;) {\n"
                         "        const %s *element = tide_list_cached_at(&cache, *v, (int32_t)i, sizeof *element);\n"
                         "        uint32_t n = cache.at == v->at && cache.at && i - cache.first < cache.count ? cache.first + cache.count - i : 1u;\n"
                         "        if (n > count - i) n = count - i;\n"
                         "        if (n > (1u << 30) / sizeof *element) n = (uint32_t)((1u << 30) / sizeof *element);\n"
                         "        tide_client_write(out, element, (uint32_t)(n * sizeof *element));\n"
                         "        i += n;\n    }\n", elem_ctype(et));
        } else {
            sb_printf(o, "    for (uint32_t i = 0; i < count; i++) tide_client_put%d(out, tide_list_cached_at(&cache, *v, (int32_t)i, sizeof(%s)), depth + 1u);\n",
                      client_codec_id(codec, et), elem_ctype(et));
        }
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
        sb_printf(o, "            const uint8_t *cells = tide_grid_chunk_at(tide_heap_read(tide_handle_where(v->at)), records[i].block);\n"
                     "            for (uint32_t k = 0; k < tide_grid_chunk_bytes(&%s) / sizeof(%s); k++)\n"
                     "                tide_client_put%d(out, (const %s *)(const void *)(cells + (size_t)k * sizeof(%s)), depth + 1u);\n",
                  grid_shape_name(t.decl), elem_ctype(cell), client_codec_id(codec, cell), elem_ctype(cell), elem_ctype(cell));
        sb_put(o, "        }\n        free(records);\n    }\n");
    } else if (t.kind == TY_RESIDENT) {
        // Opaque C state may cache hidden entities; only its visible ordinary
        // component outcomes belong in a recipient's projection.
        sb_put(o, "    // Opaque resident storage remains authoritative server state.\n");
    } else {
        sb_put(o, "    tide_client_write(out, v, (uint32_t)sizeof *v);\n");
    }
    sb_put(o, "}\n\n");

    sb_printf(o, "TIDE_HELPER bool tide_client_get%d(tide_reader *in, %s *v, uint32_t where, uint32_t depth, uint64_t *budget)\n{\n", id, ct);
    sb_put(o, "    if (in->failed || depth > 256u) return false;\n    (void)where; (void)v; (void)budget;\n");
    if (t.kind == TY_STRING) {
        sb_put(o, "    const uint32_t size = tide_read_varint(in);\n"
                  "    const uint8_t *bytes = tide_client_read(in, size);\n"
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
        // Added as tide_list_add would, one at a time (tide_client_list_grow), so the heap comes out the same
        sb_put(o, "    const uint32_t base = (uint32_t)tide_list_count(*v);\n"
                  "    if (count > (uint32_t)INT32_MAX - base) return false;\n"
                  "    tide_list_cache cache = {0};\n");
        if (client_codec_raw(g->prog, et)) {
            // The room the list has is filled, a run at a time, before it grows: a block it outgrows is let go
            // of with its elements in it, as adding them one by one leaves it, and the heap's bytes are hashed
            sb_printf(o, "    _Static_assert(sizeof(%s) == %d, \"a list element's codec bytes are its memory\");\n",
                      elem_ctype(et), type_layout(et).size);
            sb_printf(o, "    for (uint32_t i = 0; i < count;) {\n"
                         "        if (!tide_client_list_grow(v, base + i + 1u, base + count, sizeof(%s), where)) return false;\n"
                         "        const uint32_t room = (uint32_t)tide_list_count(*v) - base;\n"
                         "        while (i < room) {\n"
                         "            %s *element = tide_list_cached_at_mut(&cache, *v, (int32_t)(base + i), sizeof *element);\n"
                         "            if (!element) return false;\n"
                         "            uint32_t n = cache.at == v->at && cache.at && base + i - cache.first < cache.count ? cache.first + cache.count - (base + i) : 1u;\n"
                         "            if (n > room - i) n = room - i;\n"
                         "            if (n > (1u << 30) / sizeof *element) n = (uint32_t)((1u << 30) / sizeof *element);\n"
                         "            const uint8_t *bytes = tide_client_read(in, (uint32_t)(n * sizeof *element));\n"
                         "            if (!bytes) return false;\n"
                         "            memcpy(element, bytes, (size_t)n * sizeof *element);\n"
                         "            i += n;\n        }\n    }\n", elem_ctype(et), elem_ctype(et));
        } else {
            sb_printf(o, "    for (uint32_t i = 0; i < count; i++) {\n"
                         "        if (!tide_client_list_grow(v, base + i + 1u, base + count, sizeof(%s), where)) return false;\n"
                         "        %s *element = tide_list_cached_at_mut(&cache, *v, (int32_t)(base + i), sizeof *element);\n", elem_ctype(et), elem_ctype(et));
            sb_printf(o, "        if (!element || !tide_client_get%d(in, element, where, depth + 1u, budget)) return false;\n    }\n", client_codec_id(codec, et));
        }
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
        sb_put(o, "    const uint8_t *bytes = tide_client_read(in, 1u);\n"
                  "    if (!bytes || *bytes > 1u) return false;\n    *v = *bytes != 0;\n");
    } else {
        sb_put(o, "    const uint8_t *bytes = tide_client_read(in, (uint32_t)sizeof *v);\n"
                  "    if (!bytes) return false;\n    memcpy(v, bytes, sizeof *v);\n");
    }
    sb_put(o, "    return !in->failed;\n}\n\n");
}

static void gen_client_codecs(gen *g, client_codec *codec)
{
    sb *o = &g->c;
    sb_put(o, "TIDE_HELPER bool tide_client_budget(uint64_t *remaining, uint64_t bytes)\n{\n"
              "    if (bytes > *remaining) return false;\n    *remaining -= bytes;\n    return true;\n}\n\n");
    // tide_write_bytes and tide_read_bytes with their usual case (room, no
    // failure) inline: the codecs go field by field, a call each otherwise
    sb_put(o, "TIDE_HELPER void tide_client_write(tide_writer *out, const void *v, const uint32_t size)\n{\n"
              "    if (!out->overflow && out->capacity - out->size >= size) {\n"
              "        if (size) memcpy(out->data + out->size, v, size);\n        out->size += size;\n"
              "    } else tide_write_bytes(out, v, size);\n}\n\n"
              "TIDE_HELPER const uint8_t *tide_client_read(tide_reader *in, const uint32_t size)\n{\n"
              "    if (in->failed || in->size - in->at < size) return tide_read_bytes(in, size);\n"
              "    const uint8_t *at = in->data + in->at;\n    in->at += size;\n    return at;\n}\n\n");
    // A list read element by element grows as tide_list_add grows it: an add
    // that finds no room left (or a block that isn't the list's own) takes
    // room as that add would, through the same reserve; adds that find room
    // take nothing, so they're done at once, as far as `most`. The heap gets
    // the same blocks in the same order, and callers fill the room they have
    // before asking for more, so it comes out byte for byte the same.
    sb_put(o, "TIDE_HELPER bool tide_client_list_grow(tide_list *v, const uint32_t need, const uint32_t most, const uint32_t size, const uint32_t where)\n{\n"
              "    uint32_t count = (uint32_t)tide_list_count(*v);\n"
              "    while (count < need) {\n"
              "        const tide_block *b = tide_block_at(v->at);\n"
              "        uint32_t to = count + 1u;\n"
              "        if (b && b->b != TIDE_LIST_ASSET && !tide_pack_holds(v->at) && tide_handle_where(v->at) == where) {\n"
              "            const uint64_t room = b->b & TIDE_LIST_CHUNKED ? (uint64_t)(b->b & ~TIDE_LIST_CHUNKED) * (TIDE_LIST_CHUNK / size) : b->b;\n"
              "            if (room > count) to = room < most ? (uint32_t)room : most;\n"
              "        }\n"
              "        tide_list_resize(v, (int32_t)to, size, where);\n"
              "        if ((uint32_t)tide_list_count(*v) != to) return false;\n"
              "        count = to;\n"
              "    }\n    return true;\n}\n\n");
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
