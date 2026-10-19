// Received plain lifecycle values must not supply heap locations or native
// pointers. Inspect their object bytes before any generated typed read: even a
// bool load is undefined C for a representation other than zero or one.
static bool client_event_value_checked(const program *prog, const type t)
{
    if (t.kind == TY_BOOL || t.kind == TY_STRING || t.kind == TY_LIST || t.kind == TY_MAP
        || t.kind == TY_SET || t.kind == TY_GRID || t.kind == TY_RESIDENT) return true;
    if (t.kind == TY_ARRAY) return client_event_value_checked(prog, t.decl->fields.items[0].type);
    if (t.kind == TY_RECORD && t.decl->array_of)
        return client_event_value_checked(prog, (type){TY_RECORD, t.decl->array_of});
    if (t.kind != TY_STRUCT && t.kind != TY_COMPONENT && t.kind != TY_SINGLETON && t.kind != TY_INPUT
        && t.kind != TY_EVENT && t.kind != TY_RECORD) return false;
    for (int i = 0; i < t.decl->fields.count; i++) {
        const field *f = &t.decl->fields.items[i];
        if (f->typed) continue; // Keyboard.text is synthetic, not a stored Tide text handle.
        if ((f->hidden && t.decl == prog->visibility)
            || client_event_value_checked(prog, f->type)) return true;
    }
    return false;
}

static void gen_client_event_value_check(gen *g, const type t, const char *bytes, const int depth)
{
    if (!client_event_value_checked(g->prog, t)) return;
    sb *o = &g->c;
    if (t.kind == TY_BOOL) {
        sb_printf(o, "                if (*(%s) > 1u) return false;\n", bytes);
    } else if (t.kind == TY_STRING || t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET
               || t.kind == TY_GRID || t.kind == TY_RESIDENT) {
        sb_printf(o, "                if (!tide_client_event_zero(%s, sizeof(%s))) return false;\n", bytes, elem_ctype(t));
    } else if (t.kind == TY_ARRAY || (t.kind == TY_RECORD && t.decl->array_of)) {
        const type et = t.kind == TY_ARRAY ? t.decl->fields.items[0].type : (type){TY_RECORD, t.decl->array_of};
        sb_printf(o, "                for (uint32_t tide_plain%d = 0; tide_plain%d < %du; tide_plain%d++) {\n",
                  depth, depth, t.decl->array_length, depth);
        sb element = {0};
        sb_printf(&element, "(%s + offsetof(%s, at) + (size_t)tide_plain%d * sizeof(%s))", bytes, elem_ctype(t), depth, elem_ctype(et));
        gen_client_event_value_check(g, et, element.data, depth + 1);
        sb_put(o, "                }\n");
    } else {
        for (int i = 0; i < t.decl->fields.count; i++) {
            const field *f = &t.decl->fields.items[i];
            if (f->typed) continue;
            sb member = {0};
            sb_printf(&member, "(%s + offsetof(%s, %s))", bytes, elem_ctype(t), field_cname(f));
            if (f->hidden && t.decl == g->prog->visibility) {
                sb_printf(o, "                if (!tide_client_event_zero(%s, sizeof(((%s *)0)->%s))) return false;\n",
                          member.data, elem_ctype(t), field_cname(f));
            } else gen_client_event_value_check(g, f->type, member.data, depth);
        }
    }
}

static void gen_client_event_payload_check(gen *g, const decl *event)
{
    const type_kind kinds[] = {TY_COMPONENT, TY_SINGLETON};
    sb bytes = {0};
    sb_printf(&bytes, "(data + at + sizeof h + offsetof(tide_heard_%s, event))", type_cname(event));
    gen_client_event_value_check(g, (type){TY_EVENT, (decl *)event}, bytes.data, 0);
    for (int n = 0; n < 2; n++) {
        const decl_list values = heard_data(event, kinds[n]);
        for (int i = 0; i < values.count; i++) {
            const decl *d = values.items[i];
            sb place = {0};
            sb_printf(&place, "(data + at + sizeof h + offsetof(tide_heard_%s, tide_%c_%s))",
                      type_cname(event), kinds[n] == TY_COMPONENT ? 'c' : 's', type_cname(d));
            gen_client_event_value_check(g, (type){kinds[n], (decl *)d}, place.data, 0);
        }
    }
}
