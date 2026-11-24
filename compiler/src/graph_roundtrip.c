// The test of graphs (notes/visual-scripting.md): a .tide file made a graph, and the graph's Tide, parse to the
// same syntax tree. Places in the source aren't compared, and what reads the same is the same: a statement
// written alone where a block goes and a block that holds it, `else if` and `else { if }`.

#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "lexer.h"

typedef struct rt {
    const char *path;
    int mismatches;
    loc at; // Where in the original the last thing compared is
} rt;

static bool differ(rt *r, const loc at, const char *what)
{
    if (r->mismatches++ < 20) {
        fprintf(stderr, "%s:%d:%d: the graph's Tide differs here: %s\n", r->path, at.line ? at.line : r->at.line,
                at.line ? at.col : r->at.col, what);
    }
    return false;
}

static bool same_str(const str a, const str b)
{
    return a.len == b.len && (!a.len || memcmp(a.ptr, b.ptr, (size_t)a.len) == 0);
}

static bool same_type(const type_ref *a, const type_ref *b)
{
    if (!a || !b) return a == b || (a ? !a->text.len : !b->text.len);
    return same_str(a->text, b->text);
}

#define SAME(cond, at, what) \
    do { \
        if (!(cond)) return differ(r, at, what); \
    } while (0)

static bool same_expr(rt *r, const expr *a, const expr *b);
static bool same_stmt(rt *r, const stmt *a, const stmt *b);

static bool same_exprs(rt *r, expr *const *a, const int an, expr *const *b, const int bn, const loc at)
{
    SAME(an == bn, at, "a different number of values");
    for (int i = 0; i < an; i++) {
        if (!same_expr(r, a[i], b[i])) return false;
    }
    return true;
}

static bool same_params(rt *r, const param *a, int an, const param *b, int bn, loc at);

static bool same_local_decl(rt *r, const stmt *a, const stmt *b)
{
    if (!a || !b) {
        SAME(a == b, a ? a->at : r->at, "a local declared in one and not the other");
        return true;
    }
    SAME(same_str(a->name, b->name), a->at, "a local's name");
    SAME(same_str(a->type_name, b->type_name), a->at, "a local's type");
    SAME(a->is_mut == b->is_mut, a->at, "a local's mut");
    return true;
}

static bool same_expr(rt *r, const expr *a, const expr *b)
{
    if (!a || !b) {
        SAME(a == b, a ? a->at : r->at, a ? "a value the graph's Tide doesn't have" : "a value the original doesn't have");
        return true;
    }
    r->at = a->at;
    SAME(a->kind == b->kind, a->at, "a different kind of expression");
    SAME(a->is_out == b->is_out, a->at, "'out' on one and not the other");
    SAME(a->placement_local == b->placement_local, a->at, "'local' before a call");
    SAME(a->lambda == b->lambda, a->at, "a lambda");
    switch (a->kind) {
    case E_INT:
    case E_FLOAT:
    case E_STRING:
        SAME(same_str(a->text, b->text), a->at, "a literal's text");
        SAME(a->is_char == b->is_char, a->at, "a character");
        return true;
    case E_BOOL: SAME(a->bool_value == b->bool_value, a->at, "true or false"); return true;
    case E_THIS:
    case E_DEFAULT:
    case E_NULL: return true;
    case E_NAME:
        SAME(same_str(a->name, b->name), a->at, "a name");
        SAME(same_type(a->type_arg_ref, b->type_arg_ref), a->at, "a type before '.'");
        if (a->out_local || b->out_local) return same_local_decl(r, a->out_local, b->out_local);
        return true;
    case E_MEMBER:
        SAME(same_str(a->member, b->member), a->at, "a member's name");
        SAME(a->enum_shorthand == b->enum_shorthand, a->at, "'.Member' on one and not the other");
        return a->enum_shorthand || same_expr(r, a->object, b->object);
    case E_CALL:
    case E_METHOD: {
        SAME(same_str(a->name, b->name), a->at, "the name called");
        SAME(a->type_args.count == b->type_args.count, a->at, "the types in <>");
        for (int i = 0; i < a->type_args.count; i++) SAME(same_type(a->type_args.items[i], b->type_args.items[i]), a->at, "a type in <>");
        if (a->kind == E_METHOD && !same_expr(r, a->object, b->object)) return false;
        bool an = false, bn = false;
        for (int i = 0; i < a->arg_names.count; i++) an |= a->arg_names.items[i].len > 0;
        for (int i = 0; i < b->arg_names.count; i++) bn |= b->arg_names.items[i].len > 0;
        SAME(an == bn, a->at, "named arguments");
        for (int i = 0; an && i < a->args.count; i++) {
            const str x = i < a->arg_names.count ? a->arg_names.items[i] : (str){NULL, 0};
            const str y = i < b->arg_names.count ? b->arg_names.items[i] : (str){NULL, 0};
            SAME(same_str(x, y), a->at, "an argument's name");
        }
        if (!same_exprs(r, a->args.items, a->args.count, b->args.items, b->args.count, a->at)) return false;
        SAME((a->block == NULL) == (b->block == NULL), a->at, "a call's block");
        SAME(a->stacked == b->stacked, a->at, "calls listed before one block");
        return !a->block || same_stmt(r, a->block, b->block);
    }
    case E_BINARY:
    case E_COALESCE:
        SAME(a->op == b->op, a->at, "an operator");
        return same_expr(r, a->lhs, b->lhs) && same_expr(r, a->rhs, b->rhs);
    case E_UNARY:
    case E_TRY:
    case E_DEFAULTED:
    case E_AWAIT:
        SAME(a->op == b->op, a->at, "an operator");
        return same_expr(r, a->lhs, b->lhs);
    case E_STEP:
        SAME(a->op == b->op && a->postfix == b->postfix, a->at, "++ or --");
        return same_expr(r, a->lhs, b->lhs);
    case E_LITERAL:
        if (a->lambda) {
            if (!same_params(r, a->lambda_params.items, a->lambda_params.count, b->lambda_params.items, b->lambda_params.count, a->at)) {
                return false;
            }
            return same_expr(r, a->lambda_body, b->lambda_body);
        }
        SAME(same_str(a->name, b->name), a->at, "a value's type");
        SAME(same_type(a->type_arg_ref, b->type_arg_ref), a->at, "a value's type arguments");
        SAME(a->inits.count == b->inits.count, a->at, "the fields a value sets");
        for (int i = 0; i < a->inits.count; i++) {
            SAME(same_str(a->inits.items[i].name, b->inits.items[i].name), a->at, "a field a value sets");
            if (!same_expr(r, a->inits.items[i].value, b->inits.items[i].value)) return false;
        }
        return true;
    case E_CONDITIONAL:
        return same_expr(r, a->cond, b->cond) && same_expr(r, a->lhs, b->lhs) && same_expr(r, a->rhs, b->rhs);
    case E_SWITCH:
        if (!same_expr(r, a->object, b->object)) return false;
        SAME(a->inits.count == b->inits.count, a->at, "a switch's arms");
        for (int i = 0; i < a->inits.count; i++) {
            if (!same_expr(r, a->inits.items[i].value, b->inits.items[i].value)) return false;
        }
        return same_exprs(r, a->args.items, a->args.count, b->args.items, b->args.count, a->at);
    case E_INTERP:
        SAME(a->parts.count == b->parts.count, a->at, "a text's parts");
        for (int i = 0; i < a->parts.count; i++) SAME(same_str(a->parts.items[i], b->parts.items[i]), a->at, "a text's part");
        for (int i = 0; i < a->formats.count && i < b->formats.count; i++) {
            SAME(same_str(a->formats.items[i], b->formats.items[i]), a->at, "a value's format");
        }
        return same_exprs(r, a->args.items, a->args.count, b->args.items, b->args.count, a->at);
    case E_INDEX:
        return same_expr(r, a->object, b->object) && same_expr(r, a->lhs, b->lhs);
    case E_LIST:
        return same_exprs(r, a->args.items, a->args.count, b->args.items, b->args.count, a->at);
    case E_IS:
        SAME(same_str(a->pattern, b->pattern), a->at, "the type after 'is'");
        SAME((a->binding == NULL) == (b->binding == NULL), a->at, "the name after 'is'");
        if (a->binding) SAME(same_str(a->binding->name, b->binding->name), a->at, "the name after 'is'");
        return same_expr(r, a->lhs, b->lhs);
    case E_HAS:
        SAME(same_str(a->pattern, b->pattern) && same_str(a->pattern_field, b->pattern_field), a->at, "a 'has'");
        return same_expr(r, a->lhs, b->lhs);
    }
    return differ(r, a->at, "an expression the test doesn't know");
}

// What a block or a statement written alone holds
static stmt *const *as_list(const stmt *s, int *count)
{
    if (!s) {
        *count = 0;
        return NULL;
    }
    if (s->kind == S_BLOCK) {
        *count = s->stmts.count;
        return s->stmts.items;
    }
    *count = 1;
    stmt **one = arena_alloc(sizeof(stmt *));
    one[0] = (stmt *)s;
    return one;
}

static bool same_list(rt *r, stmt *const *a, const int an, stmt *const *b, const int bn, const loc at)
{
    SAME(an == bn, an && a ? a[an < bn ? an ? an - 1 : 0 : bn ? bn - 1 : 0]->at : at, "a different number of statements");
    for (int i = 0; i < an; i++) {
        if (!same_stmt(r, a[i], b[i])) return false;
    }
    return true;
}

static bool same_code(rt *r, const stmt *a, const stmt *b, const loc at)
{
    int an, bn;
    stmt *const *x = as_list(a, &an);
    stmt *const *y = as_list(b, &bn);
    return same_list(r, x, an, y, bn, at);
}

static bool same_stmt(rt *r, const stmt *a, const stmt *b)
{
    if (!a || !b) {
        SAME(a == b, a ? a->at : r->at, "a statement in one and not the other");
        return true;
    }
    r->at = a->at;
    SAME(a->kind == b->kind, a->at, "a different kind of statement");
    switch (a->kind) {
    case S_BLOCK: return same_list(r, a->stmts.items, a->stmts.count, b->stmts.items, b->stmts.count, a->at);
    case S_IF:
        return same_expr(r, a->cond, b->cond) && same_code(r, a->then_stmt, b->then_stmt, a->at)
               && same_code(r, a->else_stmt, b->else_stmt, a->at);
    case S_RETURN:
    case S_FAIL:
    case S_EXPR: return same_expr(r, a->value, b->value);
    case S_BREAK:
    case S_CONTINUE: return true;
    case S_VAR:
        SAME(a->discard == b->discard, a->at, "'_ ='");
        if (!a->discard) {
            SAME(same_str(a->name, b->name), a->at, "a local's name");
            SAME(same_str(a->type_name, b->type_name), a->at, "a local's type");
            SAME(a->is_mut == b->is_mut, a->at, "a local's mut");
            SAME(a->is_shared == b->is_shared, a->at, "'shared'");
            SAME(a->delayed == b->delayed, a->at, "a local declared without its value");
        }
        return same_expr(r, a->value, b->value);
    case S_ASSIGN:
        SAME(a->op == b->op, a->at, "an assignment's operator");
        if (a->op == T_PLUS_PLUS || a->op == T_MINUS_MINUS) return same_expr(r, a->target, b->target);
        return same_expr(r, a->target, b->target) && same_expr(r, a->value, b->value);
    case S_WHILE: return same_expr(r, a->cond, b->cond) && same_code(r, a->then_stmt, b->then_stmt, a->at);
    case S_FOR:
        return same_stmt(r, a->init, b->init) && same_expr(r, a->cond, b->cond) && same_stmt(r, a->step, b->step)
               && same_code(r, a->then_stmt, b->then_stmt, a->at);
    case S_FOREACH:
    case S_PARALLEL:
        SAME(same_str(a->name, b->name) && same_str(a->type_name, b->type_name), a->at, "a loop's variable");
        SAME(a->indexed == b->indexed, a->at, "'indexed'");
        return same_expr(r, a->value, b->value) && same_expr(r, a->by, b->by) && same_expr(r, a->offset, b->offset)
               && same_code(r, a->then_stmt, b->then_stmt, a->at);
    case S_SWITCH:
        if (!same_expr(r, a->cond, b->cond)) return false;
        SAME(a->cases.count == b->cases.count, a->at, "a switch's cases");
        for (int i = 0; i < a->cases.count; i++) {
            const switch_case *x = &a->cases.items[i], *y = &b->cases.items[i];
            if (!same_exprs(r, x->labels.items, x->labels.count, y->labels.items, y->labels.count, a->at)) return false;
            if (!same_list(r, x->body.items, x->body.count, y->body.items, y->body.count, a->at)) return false;
        }
        return true;
    }
    return differ(r, a->at, "a statement the test doesn't know");
}

static bool same_attributes(rt *r, const attribute *a, const int an, const attribute *b, const int bn, const loc at)
{
    SAME(an == bn, at, "a different number of attributes");
    for (int i = 0; i < an; i++) {
        SAME(same_str(a[i].name, b[i].name), a[i].at, "an attribute's name");
        SAME(a[i].args.count == b[i].args.count, a[i].at, "an attribute's names");
        for (int k = 0; k < a[i].args.count; k++) {
            SAME(same_str(a[i].args.items[k].text, b[i].args.items[k].text), a[i].at, "a name in an attribute");
            SAME(a[i].args.items[k].has_signature == b[i].args.items[k].has_signature, a[i].at, "an attribute's signature");
            SAME(a[i].args.items[k].signature.count == b[i].args.items[k].signature.count, a[i].at, "an attribute's signature");
        }
        if (!same_exprs(r, a[i].values.items, a[i].values.count, b[i].values.items, b[i].values.count, a[i].at)) return false;
        if ((a[i].call == NULL) != (b[i].call == NULL)) return differ(r, a[i].at, "an attribute's call");
        if (a[i].call && !same_exprs(r, a[i].call->args.items, a[i].call->args.count, b[i].call->args.items, b[i].call->args.count, a[i].at)) {
            return false;
        }
    }
    return true;
}

static bool same_params(rt *r, const param *a, const int an, const param *b, const int bn, const loc at)
{
    SAME(an == bn, at, "a different number of parameters");
    for (int i = 0; i < an; i++) {
        const param *x = &a[i], *y = &b[i];
        SAME(x->mode == y->mode, x->at, "a parameter's mode");
        SAME(same_str(x->name, y->name), x->at, "a parameter's name");
        SAME(same_str(x->type_name, y->type_name), x->at, "a parameter's type");
        SAME(x->is_out == y->is_out && x->verified == y->verified && x->previous == y->previous && x->ownership == y->ownership
                 && x->scene == y->scene && x->placement_local == y->placement_local && x->changed == y->changed,
             x->at, "a word before a parameter");
        SAME(x->raw_role == y->raw_role && x->linear_role == y->linear_role && same_str(x->sample_role, y->sample_role), x->at,
             "an image parameter's role");
        if (!same_expr(r, x->default_value, y->default_value)) return false;
        if (x->mode == PARAM_QUERY && !same_params(r, x->query.items, x->query.count, y->query.items, y->query.count, x->at)) {
            return false;
        }
    }
    return true;
}

static bool same_fields(rt *r, const field *a, const int an, const field *b, const int bn, const loc at)
{
    SAME(an == bn, at, "a different number of fields");
    for (int i = 0; i < an; i++) {
        SAME(same_str(a[i].name, b[i].name), a[i].at, "a field's name");
        SAME(same_str(a[i].type_name, b[i].type_name), a[i].at, "a field's type");
        SAME(a[i].is_local == b[i].is_local, a[i].at, "a local field");
        if (!same_attributes(r, a[i].attributes.items, a[i].attributes.count, b[i].attributes.items, b[i].attributes.count, a[i].at)) {
            return false;
        }
        if (!same_expr(r, a[i].default_value, b[i].default_value)) return false;
    }
    return true;
}

static bool same_decl(rt *r, const decl *a, const decl *b);

static bool same_decls(rt *r, decl *const *a, const int an, decl *const *b, const int bn, const loc at)
{
    SAME(an == bn, at, "a different number of declarations");
    for (int i = 0; i < an; i++) {
        if (!same_decl(r, a[i], b[i])) return false;
    }
    return true;
}

static bool same_prefab(rt *r, const prefab_node *a, const prefab_node *b, const loc at)
{
    if (!a || !b) {
        SAME(a == b, at, "a prefab's entity");
        return true;
    }
    SAME(same_str(a->name, b->name) && same_str(a->base, b->base), a->at, "a prefab's entity");
    if (!same_exprs(r, a->components.items, a->components.count, b->components.items, b->components.count, a->at)) return false;
    SAME(a->children.count == b->children.count, a->at, "a prefab's children");
    for (int i = 0; i < a->children.count; i++) {
        if (!same_prefab(r, a->children.items[i], b->children.items[i], a->at)) return false;
    }
    return true;
}

static bool same_decl(rt *r, const decl *a, const decl *b)
{
    r->at = a->at;
    SAME(a->kind == b->kind, a->at, "a different kind of declaration");
    SAME(same_str(a->name, b->name), a->at, "a declaration's name");
    SAME(same_str(a->override_name, b->override_name), a->at, "what an override replaces");
    SAME(a->is_local == b->is_local && a->is_dev == b->is_dev && a->is_async == b->is_async && a->is_worker == b->is_worker
             && a->is_build == b->is_build && a->is_cached == b->is_cached && a->is_virtual == b->is_virtual
             && a->is_override == b->is_override && a->is_tweak == b->is_tweak && a->is_extern == b->is_extern
             && a->extern_system == b->extern_system && a->is_mut_method == b->is_mut_method,
         a->at, "a word before the declaration");
    SAME(a->is_view == b->is_view && a->is_handler == b->is_handler && a->is_command == b->is_command && a->is_scene == b->is_scene
             && a->is_frag == b->is_frag && a->is_compute == b->is_compute && a->is_audio == b->is_audio && a->is_operator == b->is_operator,
         a->at, "what kind of declaration it is");
    SAME(a->op == b->op, a->at, "an operator");
    SAME(same_str(a->return_type_name, b->return_type_name), a->at, "a return type");
    SAME(same_str(a->fails_name, b->fails_name), a->at, "what it fails with");
    SAME(same_str(a->backing, b->backing), a->at, "an enum's backing type");
    SAME(same_type(a->alias_ref, b->alias_ref), a->at, "an alias's type");
    SAME(a->type_params.count == b->type_params.count, a->at, "type parameters");
    for (int i = 0; i < a->type_params.count; i++) SAME(same_str(a->type_params.items[i], b->type_params.items[i]), a->at, "a type parameter");
    SAME(a->type_rules.count == b->type_rules.count, a->at, "'where' rules");
    for (int i = 0; i < a->type_rules.count; i++) {
        SAME(same_str(a->type_rules.items[i].param, b->type_rules.items[i].param)
                 && same_str(a->type_rules.items[i].rule, b->type_rules.items[i].rule), a->at, "a 'where' rule");
    }
    if (!same_attributes(r, a->attributes.items, a->attributes.count, b->attributes.items, b->attributes.count, a->at)) return false;
    if (!same_params(r, a->params.items, a->params.count, b->params.items, b->params.count, a->at)) return false;
    if (!same_fields(r, a->fields.items, a->fields.count, b->fields.items, b->fields.count, a->at)) return false;
    SAME(a->properties.count == b->properties.count, a->at, "properties");
    for (int i = 0; i < a->properties.count; i++) {
        SAME(same_str(a->properties.items[i]->name, b->properties.items[i]->name), a->at, "a property");
        SAME(same_type(a->properties.items[i]->type_ref, b->properties.items[i]->type_ref), a->at, "a property's type");
    }
    if (!same_decls(r, a->methods.items, a->methods.count, b->methods.items, b->methods.count, a->at)) return false;
    if (!same_decls(r, a->generic_methods.items, a->generic_methods.count, b->generic_methods.items, b->generic_methods.count, a->at)) {
        return false;
    }
    if (!same_decls(r, a->tweaks.items, a->tweaks.count, b->tweaks.items, b->tweaks.count, a->at)) return false;
    SAME(a->members.count == b->members.count, a->at, "an enum's members");
    for (int i = 0; i < a->members.count; i++) {
        SAME(same_str(a->members.items[i].name, b->members.items[i].name), a->members.items[i].at, "an enum's member");
        if (!same_expr(r, a->members.items[i].value, b->members.items[i].value)) return false;
    }
    if (!same_expr(r, a->value, b->value)) return false;
    if (!same_prefab(r, a->prefab_root, b->prefab_root, a->at)) return false;
    SAME((a->written_payload == NULL) == (b->written_payload == NULL), a->at, "a command's payload");
    if (a->written_payload && !same_fields(r, a->written_payload->fields.items, a->written_payload->fields.count,
                                           b->written_payload->fields.items, b->written_payload->fields.count, a->at)) {
        return false;
    }
    if (a->written_payload && !same_attributes(r, a->written_payload->attributes.items, a->written_payload->attributes.count,
                                               b->written_payload->attributes.items, b->written_payload->attributes.count, a->at)) {
        return false;
    }
    SAME((a->preparation == NULL) == (b->preparation == NULL), a->at, "a scene's prepare");
    if (a->preparation && !same_decl(r, a->preparation, b->preparation)) return false;
    if (!same_code(r, a->body, b->body, a->at)) return false;
    if (!same_code(r, a->sanitize, b->sanitize, a->at)) return false;
    if (!same_code(r, a->predict, b->predict, a->at)) return false;
    // A body's absence: `scene Arena;` and `extern` ones
    SAME((a->body == NULL) == (b->body == NULL), a->at, "a body in one and not the other");
    return true;
}

static program *parse_text(const char *path, const char *text, const size_t len)
{
    source *src = NEW(source);
    src->path = path;
    src->text = text;
    src->len = len;
    diag_add_source(src);
    token *toks = lex(src);
    if (!toks) return NULL;
    program *prog = program_new();
    return parse_file(prog, src, toks, false) ? prog : NULL;
}

static bool same_units(rt *r, const program *a, const program *b)
{
    SAME(a->units.count == b->units.count, r->at, "a different number of namespace blocks");
    for (int i = 0; i < a->units.count; i++) {
        const unit *x = a->units.items[i], *y = b->units.items[i];
        SAME(same_str(x->ns, y->ns), x->ns_at, "a namespace");
        SAME(x->usings.count == y->usings.count, x->ns_at, "the 'using' lines");
        for (int k = 0; k < x->usings.count; k++) SAME(same_str(x->usings.items[k], y->usings.items[k]), x->ns_at, "a 'using'");
    }
    return true;
}

static bool decl_unit_same(rt *r, const program *pa, const decl *a, const program *pb, const decl *b)
{
    int ia = -1, ib = -1;
    for (int i = 0; i < pa->units.count; i++) {
        if (pa->units.items[i] == a->unit) ia = i;
    }
    for (int i = 0; i < pb->units.count; i++) {
        if (pb->units.items[i] == b->unit) ib = i;
    }
    SAME(ia == ib, a->at, "the namespace a declaration is in");
    return true;
}

static void quiet(void *user, const diag_severity severity, const loc at, const char *message)
{
    (void)user;
    (void)severity;
    (void)at;
    (void)message;
}

static int roundtrip(const char *path, const bool verbose)
{
    rt r = {.path = path};
    diag_reset();
    // A file that doesn't parse (a test of what tidec says about one) has no graph to compare
    {
        size_t n;
        char *t = graph_read_file(path, &n);
        if (!t) return 2;
        if (!verbose) diag_set_sink(quiet, NULL);
        const program *p = parse_text(path, t, n);
        diag_reset();
        if (!p) {
            diag_set_sink(NULL, NULL);
            return 2;
        }
    }
    sb json = {0};
    const char *why = NULL;
    if (!graph_from_tide(path, &json, &why)) {
        fprintf(stderr, "%s: can't be made a graph: %s\n", path, why ? why : "see above");
        return 1;
    }
    // The graph's Tide, as tidec reads the graph
    source *g = NEW(source);
    sb graph_path = {0};
    sb_printf(&graph_path, "%s.tidegraph", path);
    g->path = graph_path.data;
    g->text = json.data;
    g->len = json.len;
    diag_add_source(g);
    if (!graph_lower(g)) {
        fprintf(stderr, "%s: the graph it was made into doesn't read\n", path);
        return 1;
    }
    size_t len;
    char *text = graph_read_file(path, &len);
    if (!text) return 1;
    program *original = parse_text(path, text, len);
    if (!original) {
        fprintf(stderr, "%s: doesn't parse\n", path);
        return 1;
    }
    program *again = parse_text(g->path, g->text, g->len);
    if (!again) {
        fprintf(stderr, "%s: the graph's Tide doesn't parse; it's above\n", path);
        if (verbose) fwrite(g->text, 1, g->len, stderr);
        return 1;
    }
    same_units(&r, original, again);
    // Declarations in the order they're written
    VEC(decl *) xa = {0};
    VEC(decl *) xb = {0};
    for (int i = 0; i < original->decls.count; i++) vec_push(xa, original->decls.items[i]);
    for (int i = 0; i < again->decls.count; i++) vec_push(xb, again->decls.items[i]);
    for (int i = 0; i < original->settings.count; i++) vec_push(xa, original->settings.items[i]);
    for (int i = 0; i < again->settings.count; i++) vec_push(xb, again->settings.items[i]);
    if (xa.count != xb.count) {
        differ(&r, (loc){1, 1, 0}, "a different number of declarations");
    } else {
        for (int i = 0; i < xa.count; i++) {
            if (!decl_unit_same(&r, original, xa.items[i], again, xb.items[i])) break;
            if (!same_decl(&r, xa.items[i], xb.items[i])) break;
        }
    }
    if (r.mismatches && verbose) fwrite(g->text, 1, g->len, stderr);
    return r.mismatches ? 1 : 0;
}

int graph_roundtrip(const char *path, const bool verbose)
{
    const int result = roundtrip(path, verbose);
    diag_set_sink(NULL, NULL);
    return result;
}
