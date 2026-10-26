static expr *callable_name(const str name, const loc at)
{
    expr *e = NEW(expr);
    e->kind = E_NAME;
    e->name = name;
    e->at = at;
    return e;
}

static expr *callable_member(expr *object, const str name, const loc at)
{
    expr *e = NEW(expr);
    e->kind = E_MEMBER;
    e->object = object;
    e->member = name;
    e->at = at;
    return e;
}

static bool callable_matches(const decl *signature, const decl *fn)
{
    if (fn->kind != DECL_FUNCTION || fn->is_extern || fn->is_async || fn->takes_action || fn->is_audio
        || fn->fails.kind != TY_VOID || fn->params.count != signature->call_types.count - 1) return false;
    if (!same_type(fn->return_type, signature->call_types.items[signature->call_types.count - 1])) return false;
    for (int i = 0; i < fn->params.count; i++) {
        const param *p = &fn->params.items[i];
        const param_mode mode = signature->call_mut & (1u << i) ? PARAM_MUT : PARAM_READ;
        if (p->mode != mode || p->is_out || !same_type(p->type, signature->call_types.items[i])) return false;
    }
    return true;
}

typedef struct callable_captures { VEC(expr *) values; } callable_captures;

static bool callable_contains(const type t, const decl *signature, const int depth)
{
    if (t.decl == signature) return true;
    if (depth > 64 || t.kind != TY_STRUCT) return false;
    for (int i = 0; i < t.decl->fields.count; i++)
        if (callable_contains(t.decl->fields.items[i].type, signature, depth + 1)) return true;
    return false;
}

static void callable_find_captures(checker *c, expr *e, decl *fn, callable_captures *captures, const decl *signature)
{
    if (!e) return;
    if (e->lambda) {
        diag_error(e->at, "a captured lambda cannot contain another lambda yet; pass a named helper");
        return;
    }
    if (e->kind == E_NAME || e->kind == E_CALL) {
        for (int i = 0; i < fn->params.count; i++) if (str_eq(fn->params.items[i].name, e->name)) return;
        if (find_local(c, e->name) || find_param(c, e->name) || field_in_scope(c, e->name)) {
            expr *value = callable_name(e->name, e->at);
            const type t = check_expr(c, value);
            if (t.kind == TY_ERROR) return;
            if (callable_contains(t, signature, 0)) {
                diag_error(e->at, "a callable cannot capture its own Func type; pass the callable explicitly to a helper");
                return;
            }
            if (holds_heap(t) || holds_grid(t) || holds_render_resource(t) || t.kind == TY_COMPONENT
                || t.kind == TY_SINGLETON || t.kind == TY_WORLD || t.kind == TY_QUERY || t.kind == TY_INPUT
                || t.kind == TY_ACTION || t.kind == TY_MESH) {
                diag_error(e->at, "a stored callable captures owned plain values; pass explicit data instead of %s", type_name(t));
                return;
            }
            param capture = callable_param(e->name, t, e->at);
            capture.capture_source = value;
            vec_push(fn->params, capture);
            vec_push(captures->values, value);
        }
    }
    callable_find_captures(c, e->object, fn, captures, signature);
    callable_find_captures(c, e->lhs, fn, captures, signature);
    callable_find_captures(c, e->rhs, fn, captures, signature);
    callable_find_captures(c, e->cond, fn, captures, signature);
    for (int i = 0; i < e->args.count; i++) callable_find_captures(c, e->args.items[i], fn, captures, signature);
    for (int i = 0; i < e->inits.count; i++) callable_find_captures(c, e->inits.items[i].value, fn, captures, signature);
}

static type check_callable_value(checker *c, expr *e, const type want)
{
    decl *signature = want.decl;
    decl *fn = NULL;
    callable_captures captures = {0};
    if (e->lambda) {
        const int arguments = signature->call_types.count - 1;
        if (e->lambda_params.count != arguments) {
            diag_error(e->at, "this Func requires %d arguments, and the lambda declares %d", arguments, e->lambda_params.count);
            return e->type = T_ERR;
        }
        fn = callable_decl(c, DECL_FUNCTION, "Lambda", e->at);
        sb identity = {0};
        const decl *owner = c->method ? c->method : c->system;
        sb_printf(&identity, STR_FMT ":%d:%d", STR_ARG(owner ? owner->qualified : c->unit->ns), e->at.line, e->at.col);
        callable_identity(fn, "Lambda", callable_hash(identity.data));
        fn->named_in = fn->unit;
        fn->unit = c->unit;
        fn->end = e->end;
        fn->return_type = fn->result = signature->call_types.items[arguments];
        fn->fails = T_VOID_;
        fn->return_type_ref = callable_type_ref(fn->return_type, e->at);
        fn->return_type_name = fn->return_type_ref->text;
        fn->return_type_at = fn->return_type_qual_at = e->at;
        for (int i = 0; i < arguments; i++) {
            param p = e->lambda_params.items[i];
            p.type = resolve_type(c, p.type_ref, PLACE_FUNCTION, p.type_at);
            if (!same_type(p.type, signature->call_types.items[i]))
                diag_error(p.type_at, "lambda argument %d is %s; this Func requires %s", i + 1,
                           type_name(p.type), type_name(signature->call_types.items[i]));
            const param_mode mode = signature->call_mut & (1u << i) ? PARAM_MUT : PARAM_READ;
            if (p.mode != mode)
                diag_error(p.at, "lambda argument %d must be %s to match this Func", i + 1,
                           mode == PARAM_MUT ? "mut" : "read-only (remove mut)");
            vec_push(fn->params, p);
        }
        const int capture_errors = diag_error_count();
        callable_find_captures(c, e->lambda_body, fn, &captures, signature);
        if (diag_error_count() != capture_errors) {
            fn->body_checked = true;
            return e->type = T_ERR;
        }
        stmt *body = NEW(stmt);
        body->kind = S_BLOCK;
        body->at = e->at;
        stmt *ret = NEW(stmt);
        ret->kind = fn->return_type.kind == TY_VOID ? S_EXPR : S_RETURN;
        ret->at = e->at;
        ret->value = e->lambda_body;
        vec_push(body->stmts, ret);
        fn->body = body;
        checker declaration = *c;
        check_function_decl(&declaration, fn);
    } else {
        str name;
        if (!qualified_text(e, &name)) return T_VOID_;
        const expr *root = chain_root(e);
        if (root->kind == E_NAME && (find_local(c, root->name) || find_param(c, root->name) || field_in_scope(c, root->name)))
            return T_VOID_;
        decl *first = find_named(c, name, e->at, NAME_FUNCTION);
        if (!first) return T_VOID_;
        decl_list choices = {0};
        sharing_name(c->prog, first, &choices);
        for (int i = 0; i < choices.count; i++) {
            decl *candidate = choices.items[i];
            if (candidate->return_type.kind == TY_ERROR) {
                checker declaration = *c;
                declaration.unit = candidate->unit;
                check_function_decl(&declaration, candidate);
            }
            if (!callable_matches(signature, candidate)) continue;
            if (fn) {
                diag_error(e->at, "function '" STR_FMT "' has more than one matching Func signature", STR_ARG(name));
                return e->type = T_ERR;
            }
            fn = candidate;
        }
        if (!fn) {
            diag_error(e->at, "function '" STR_FMT "' does not match this Func's argument and result types (including mut parameter modes)", STR_ARG(name));
            return e->type = T_ERR;
        }
    }
    int variant = -1;
    for (int i = 0; i < signature->call_targets.count; i++) if (signature->call_targets.items[i] == fn) variant = i;
    if (variant < 0) {
        variant = signature->call_targets.count;
        sb identity = {0};
        sb_printf(&identity, STR_FMT, STR_ARG(fn->qualified));
        for (int i = 0; i < fn->params.count; i++) sb_printf(&identity, ";%s%s",
            fn->params.items[i].mode == PARAM_MUT ? "mut " : "", type_name(fn->params.items[i].type));
        const int tag = (int)callable_hash(identity.data) + (callable_hash(identity.data) == 0);
        for (int i = 0; i < signature->call_tags.count; i++) {
            if (signature->call_tags.items[i] == tag) diag_error(e->at, "callable identity collision; rename one of these functions");
        }
        vec_push(signature->call_targets, fn);
        vec_push(signature->call_tags, tag);
        vec_push(signature->call_capture_first, signature->fields.count);
        const int arguments = signature->call_types.count - 1;
        for (int i = 0; i < captures.values.count; i++) {
            sb name = {0};
            sb_printf(&name, "capture%08x_" STR_FMT, (unsigned)tag, STR_ARG(fn->params.items[arguments + i].name));
            field f = callable_field((str){name.data, (int)name.len}, fn->params.items[arguments + i].type, e->at);
            vec_push(signature->fields, f);
        }
    }
    const loc at = e->at;
    expr *source = NULL;
    if (!e->lambda) {
        source = NEW(expr);
        *source = *e;
        source->method = fn;
    }
    *e = (expr){.kind = E_LITERAL, .at = at, .name = signature->qualified, .spawn_archetype = -1};
    e->callable_source = source;
    expr *tag = NEW(expr);
    tag->kind = E_INT;
    tag->at = at;
    tag->int_value = signature->call_tags.items[variant];
    field_init init = {0};
    init.name = str_from("kind");
    init.at = at;
    init.value = tag;
    vec_push(e->inits, init);
    const int first_capture = signature->call_capture_first.items[variant];
    for (int i = 0; i < captures.values.count; i++) {
        init.name = signature->fields.items[first_capture + i].name;
        init.value = captures.values.items[i];
        vec_push(e->inits, init);
    }
    return e->type = check_literal(c, e);
}

static type check_callable_call(checker *c, expr *e, expr *receiver, const type callable)
{
    e->callable_receiver = receiver;
    decl *fn = callable.decl->call_dispatch;
    expr **args = arena_alloc(sizeof(expr *) * (size_t)(e->args.count + 1));
    args[0] = receiver;
    for (int i = 0; i < e->args.count; i++) args[i + 1] = e->args.items[i];
    e->args.items = args;
    e->args.count++;
    e->args.cap = e->args.count;
    e->kind = E_CALL;
    e->object = NULL;
    e->name = fn->qualified;
    return check_function_call(c, e, fn);
}

static void finish_callables(checker *c)
{
    bool any = false;
    for (int i = 0; i < c->prog->decls.count; i++) any |= c->prog->decls.items[i]->generated_callable;
    if (!any) return;
    // Late lambda bodies may introduce additional callbacks. Finish every one
    // before building dispatchers and computing their transitive capabilities.
    for (int i = 0; i < c->prog->decls.count; i++) {
        decl *fn = c->prog->decls.items[i];
        if (fn->generated_callable && fn->kind == DECL_FUNCTION && !fn->body_checked && fn->body) {
            c->locals.count = c->scope_marks.count = 0;
            check_method_body(c, fn);
            if (fn->return_type.kind == TY_VOID && fn->body->stmts.count
                && fn->body->stmts.items[0]->value && fn->body->stmts.items[0]->value->type.kind != TY_VOID
                && fn->body->stmts.items[0]->value->type.kind != TY_ERROR)
                diag_error(fn->at, "a void lambda's expression must return void");
        }
    }
    for (int i = 0; i < c->prog->decls.count; i++) {
        decl *d = c->prog->decls.items[i];
        if (!d->callable) continue;
        decl *fn = d->call_dispatch;
        stmt *body = NEW(stmt);
        body->kind = S_BLOCK;
        body->at = d->at;
        if (fn->return_type.kind != TY_VOID) {
            stmt *value = NEW(stmt);
            value->kind = S_VAR;
            value->is_mut = true;
            value->name = str_from("result");
            value->at = value->name_at = value->type_at = value->type_qual_at = d->at;
            value->type_ref = callable_type_ref(fn->return_type, d->at);
            value->type_name = value->type_ref->text;
            value->value = NEW(expr);
            value->value->kind = E_DEFAULT;
            value->value->at = d->at;
            vec_push(body->stmts, value);
        }
        stmt *choose = NEW(stmt);
        choose->kind = S_SWITCH;
        choose->at = d->at;
        choose->cond = callable_member(callable_name(str_from("binding"), d->at), str_from("kind"), d->at);
        vec_push(body->stmts, choose);
        for (int k = 0; k < d->call_targets.count; k++) {
            decl *target = d->call_targets.items[k];
            switch_case branch = {0};
            expr *tag = NEW(expr);
            tag->kind = E_INT;
            tag->at = d->at;
            tag->int_value = d->call_tags.items[k];
            vec_push(branch.labels, tag);
            vec_push(branch.label_at, d->at);
            expr *call = NEW(expr);
            call->kind = E_CALL;
            call->at = d->at;
            call->name = target->qualified;
            const int arguments = d->call_types.count - 1;
            for (int a = 0; a < arguments; a++) vec_push(call->args, callable_name(fn->params.items[a + 1].name, d->at));
            for (int a = arguments; a < target->params.count; a++) {
                const field *capture = &d->fields.items[d->call_capture_first.items[k] + a - arguments];
                vec_push(call->args, callable_member(callable_name(str_from("binding"), d->at), capture->name, d->at));
            }
            stmt *ret = NEW(stmt);
            ret->kind = fn->return_type.kind == TY_VOID ? S_EXPR : S_ASSIGN;
            if (ret->kind == S_ASSIGN) {
                ret->target = callable_name(str_from("result"), d->at);
                ret->op = T_ASSIGN;
            }
            ret->at = d->at;
            ret->value = call;
            vec_push(branch.body, ret);
            ret = NEW(stmt);
            ret->kind = S_BREAK;
            ret->at = d->at;
            vec_push(branch.body, ret);
            vec_push(choose->cases, branch);
        }
        stmt *fallback = NEW(stmt);
        fallback->kind = S_RETURN;
        fallback->at = d->at;
        if (fn->return_type.kind != TY_VOID) {
            fallback->value = callable_name(str_from("result"), d->at);
        }
        vec_push(body->stmts, fallback);
        fn->body = body;
        fn->body_checked = false;
        c->locals.count = c->scope_marks.count = 0;
        check_method_body(c, fn);
    }
    // Captures were discovered in bodies, after the initial field ordering.
    // Reorder before any final C layout, shader packing or migration layout.
    const int ordered = c->prog->structs.count;
    decl **previous = c->prog->structs.items;
    c->prog->structs.items = NULL;
    c->prog->structs.count = c->prog->structs.cap = 0;
    for (int i = 0; i < ordered; i++) previous[i]->index = 0;
    for (int i = 0; i < c->prog->decls.count; i++)
        if (c->prog->decls.items[i]->kind == DECL_STRUCT) c->prog->decls.items[i]->index = 0;
    for (int i = 0; i < ordered; i++) order_struct(c->prog, previous[i]);
    for (int i = 0; i < c->prog->decls.count; i++)
        if (c->prog->decls.items[i]->kind == DECL_STRUCT
            && (c->prog->decls.items[i] != c->prog->random || c->prog->random->named)) order_struct(c->prog, c->prog->decls.items[i]);
}
