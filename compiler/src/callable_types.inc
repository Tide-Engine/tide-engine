// Callable types have a closed, build-known set of targets. Their ordinary
// struct layout owns the selected target's captures and survives local storage.
static type resolve_callable(const checker *c, const type_ref *r, loc at);
static decl *shader_binding_type(const checker *c, decl *shader);
static type check_callable_value(checker *c, expr *e, type want);
static type check_callable_call(checker *c, expr *e, expr *receiver, type callable);
static void finish_callables(checker *c);

static bool is_callable(const type t)
{
    return t.kind == TY_STRUCT && t.decl && t.decl->callable;
}

static const field *callable_field_named(const decl *d, const str name)
{
    for (int i = 0; i < d->fields.count; i++) if (str_eq(d->fields.items[i].name, name)) return &d->fields.items[i];
    return NULL;
}

static bool holds_callable_at(const type t, const int depth)
{
    if (!t.decl || depth > 64) return false;
    if (t.decl->callable || t.decl->binding_of) return true;
    if (t.kind != TY_STRUCT && t.kind != TY_LIST && t.kind != TY_MAP && t.kind != TY_SET
        && t.kind != TY_ARRAY && t.kind != TY_GRID && t.kind != TY_OPTIONAL) return false;
    for (int i = 0; i < t.decl->fields.count; i++) {
        if (holds_callable_at(t.decl->fields.items[i].type, depth + 1)) return true;
    }
    return false;
}

static bool holds_callable(const type t) { return holds_callable_at(t, 0); }

static str callable_text(const char *prefix, const int number)
{
    sb text = {0};
    sb_printf(&text, "%s%d", prefix, number);
    return (str){text.data, (int)text.len};
}

static unsigned callable_hash(const char *text)
{
    unsigned h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) h = (h ^ *p) * 16777619u;
    return h & 0x7fffffffu;
}

static void callable_identity(decl *d, const char *prefix, const unsigned identity)
{
    sb name = {0}, qualified = {0};
    sb_printf(&name, "%s%08x", prefix, identity);
    d->name = (str){name.data, (int)name.len};
    sb_printf(&qualified, "TideGenerated.%s", name.data);
    d->qualified = (str){qualified.data, (int)qualified.len};
}

static type_ref *callable_type_ref(const type t, const loc at)
{
    type_ref *r = NEW(type_ref);
    r->name = r->text = t.decl ? t.decl->qualified : str_from(t.kind == TY_VOID ? "void" : type_name(t));
    r->at = r->name_at = at;
    return r;
}

static decl *callable_decl(const checker *c, const decl_kind kind, const char *prefix, const loc at)
{
    decl *d = NEW(decl);
    d->kind = kind;
    d->builtin = d->named = true;
    d->generated_callable = true;
    d->name = callable_text(prefix, c->prog->decls.count);
    sb q = {0};
    sb_printf(&q, "TideGenerated." STR_FMT, STR_ARG(d->name));
    d->qualified = (str){q.data, (int)q.len};
    unit *u = NEW(unit);
    if (c->unit) *u = *c->unit;
    u->ns = str_from("TideGenerated");
    d->unit = u;
    d->at = at;
    vec_push(c->prog->decls, d);
    return d;
}

static param callable_param(const str name, const type t, const loc at)
{
    param p = {0};
    p.name = name;
    p.type = t;
    p.type_ref = callable_type_ref(t, at);
    p.type_name = p.type_ref->text;
    p.at = p.name_at = p.type_at = p.type_qual_at = at;
    p.function_param = true;
    p.mode = PARAM_READ;
    return p;
}

static field callable_field(const str name, const type t, const loc at)
{
    field f = {0};
    f.name = name;
    f.type = t;
    f.type_ref = callable_type_ref(t, at);
    f.type_name = f.type_ref->text;
    f.at = f.type_at = f.type_qual_at = at;
    return f;
}

static type callable_of(const checker *c, const type *types, const int count, const unsigned mut, const loc at)
{
    for (int i = 0; i < c->prog->decls.count; i++) {
        decl *d = c->prog->decls.items[i];
        if (!d->callable || d->call_types.count != count || d->call_mut != mut) continue;
        bool same = true;
        for (int k = 0; k < count; k++) same &= same_type(d->call_types.items[k], types[k]);
        if (same) return (type){TY_STRUCT, d};
    }
    decl *d = callable_decl(c, DECL_STRUCT, "Callable", at);
    sb signature = {0};
    for (int i = 0; i < count; i++) sb_printf(&signature, "%s%s;", mut & (1u << i) ? "mut " : "", type_name(types[i]));
    const unsigned identity = callable_hash(signature.data);
    callable_identity(d, "Callable", identity);
    sb display = {0};
    sb_put(&display, "Func<");
    for (int i = 0; i < count; i++) sb_printf(&display, "%s%s%s", i ? ", " : "", mut & (1u << i) ? "mut " : "",
                                            types[i].kind == TY_VOID ? "void" : type_name(types[i]));
    sb_put(&display, ">");
    d->callable_display = display.data;
    d->callable = true;
    d->call_mut = mut;
    for (int i = 0; i < count; i++) vec_push(d->call_types, types[i]);
    vec_push(d->fields, callable_field(str_from("kind"), T_INT_, at));
    decl *fn = callable_decl(c, DECL_FUNCTION, "Invoke", at);
    callable_identity(fn, "Invoke", identity);
    fn->body_checked = true; // Its cases are complete only after all bindings are checked.
    fn->return_type = fn->result = types[count - 1];
    fn->fails = T_VOID_;
    fn->return_type_ref = callable_type_ref(fn->return_type, at);
    fn->return_type_name = fn->return_type_ref->text;
    fn->return_type_at = fn->return_type_qual_at = at;
    vec_push(fn->params, callable_param(str_from("binding"), (type){TY_STRUCT, d}, at));
    for (int i = 0; i < count - 1; i++) {
        param p = callable_param(callable_text("arg", i), types[i], at);
        if (mut & (1u << i)) p.mode = PARAM_MUT;
        vec_push(fn->params, p);
    }
    d->call_dispatch = fn;
    return (type){TY_STRUCT, d};
}

static type resolve_callable(const checker *c, const type_ref *r, const loc at)
{
    if (r->args.count < 1 || r->args.count > 17) {
        diag_error(at, "Func takes up to 16 argument types followed by its result type");
        return T_ERR;
    }
    type types[17];
    unsigned mut = 0;
    for (int i = 0; i < r->args.count; i++) {
        const bool result = i == r->args.count - 1;
        if (i < r->arg_mut.count && r->arg_mut.items[i].line) {
            if (result) {
                diag_error(r->arg_mut.items[i], "a Func's result is a value, not mut; put mut on an argument type instead");
                return T_ERR;
            }
            mut |= 1u << i;
        }
        types[i] = resolve_type(c, r->args.items[i], result ? PLACE_RETURN : PLACE_FUNCTION, at);
        if (types[i].kind == TY_ERROR) return T_ERR;
        if (types[i].kind == TY_WORLD || types[i].kind == TY_QUERY || types[i].kind == TY_INPUT
            || types[i].kind == TY_ACTION || types[i].kind == TY_COMPONENT || types[i].kind == TY_SINGLETON) {
            diag_error(at, "a Func signature uses value parameters; pass explicit values instead of %s", type_name(types[i]));
            return T_ERR;
        }
    }
    return callable_of(c, types, r->args.count, mut, at);
}

static decl *shader_binding_type(const checker *c, decl *shader)
{
    if (shader->binding_type) return shader->binding_type;
    decl *binding = callable_decl(c, DECL_STRUCT, "Stage", shader->at);
    sb identity = {0};
    sb_printf(&identity, STR_FMT, STR_ARG(shader->qualified));
    for (int i = 0; i < shader->params.count; i++) sb_printf(&identity, ";" STR_FMT, STR_ARG(shader->params.items[i].type_name));
    callable_identity(binding, "Stage", callable_hash(identity.data));
    binding->binding_of = shader;
    sb display = {0};
    sb_printf(&display, STR_FMT, STR_ARG(shader->qualified));
    binding->callable_display = display.data;
    shader->binding_type = binding;
    for (int i = 1; i < shader->params.count; i++) {
        const param *p = &shader->params.items[i];
        if (!p->type_ref || !str_eq_c(p->type_ref->name, "Func")) continue;
        const type t = resolve_callable(c, p->type_ref, p->type_at);
        vec_push(binding->fields, callable_field(p->name, t, p->at));
    }
    return binding;
}
