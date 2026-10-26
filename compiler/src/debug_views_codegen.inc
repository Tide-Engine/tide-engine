// Debugger views of a game's values (tidec --visualizers): <name>.natvis for
// Visual Studio, VS Code's cppvsdbg, CLion's debugger on Windows and WinDbg,
// and <name>_lldb.py for LLDB. Included by codegen.c.
//
// What a debugger can't know from C's types alone, they say: a list's element
// type (every List<T> is a tide_list, a handle into a heap), a map's entries,
// an enum's member names (C has them as ints), and where heap-held text, lists
// and maps are. Text, lists and maps are handles (tide/text.h): the top two
// bits say which heap, and the heaps code runs with are thread-locals
// (tide_world_heaps, tide_scratch_slots), which both read in the stopped
// thread. Nothing here changes generated code: these files sit beside it, and
// a Windows build embeds the natvis in its .pdb (compiler/cli/build.c).
//
// Natvis can't tell one tide_list local from another, so a list's elements
// show where a struct's field (a component's, a singleton's, a struct's) says
// what they are; a local list shows its count. LLDB's views also know locals
// and parameters by their function (TIDE_LOCALS in the .py).

#include "tide/heap.h"
#include "tide/list.h"

// XML text of `s`.
static void xml_put(sb *o, const char *s)
{
    for (; *s; s++) {
        if (*s == '&') sb_put(o, "&amp;");
        else if (*s == '<') sb_put(o, "&lt;");
        else if (*s == '>') sb_put(o, "&gt;");
        else if (*s == '"') sb_put(o, "&quot;");
        else sb_printf(o, "%c", *s);
    }
}

static void xml_printf(sb *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[8192];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    xml_put(o, buf);
}

// A value's C type as a debugger's expressions name it: C's own names for the
// fixed-width ints, which only typedefs name (and Windows' debug info keeps no
// typedef), and an enum's int.
static const char *view_ctype(const type t)
{
    if (t.kind == TY_ENUM) return t.decl->width == 1 ? "unsigned char" : t.decl->width == 2 ? "unsigned short" : "int";
    if (t.kind == TY_STRING) return "tide_text";
    const char *c = c_type(t);
    if (strcmp(c, "int32_t") == 0) return "int";
    if (strcmp(c, "uint32_t") == 0) return "unsigned int";
    if (strcmp(c, "int64_t") == 0) return "long long";
    if (strcmp(c, "uint64_t") == 0) return "unsigned long long";
    if (strcmp(c, "uint8_t") == 0) return "unsigned char";
    if (strcmp(c, "uint16_t") == 0) return "unsigned short";
    if (strcmp(c, "int16_t") == 0) return "short";
    if (strcmp(c, "int8_t") == 0) return "signed char";
    return c;
}

static int view_size(const type t)
{
    return type_layout(t).size;
}

// The block a handle `at` names (tide_block_at), as an expression.
static char *view_block(const char *at)
{
    sb b = {0};
    sb_printf(&b,
              "((%s) >> 30 == %u ? (tide_block*)(tide_scratch_slots[((%s) & 0x3FFFFFFFu) >> %u] + (((%s) & 0x3FFFFFFFu) & 0x%Xu))"
              " : (tide_block*)((char*)(tide_world_heaps[(%s) >> 30]->page[((%s) & 0x3FFFFFFFu) >> %u] + 1)"
              " + (((%s) & 0x3FFFFFFFu) - (tide_world_heaps[(%s) >> 30]->page[((%s) & 0x3FFFFFFFu) >> %u]->first << %u))))",
              at, TIDE_IN_SCRATCH, at, TIDE_SCRATCH_SHIFT, at, TIDE_SCRATCH_BYTES - 1u, at, at, TIDE_HEAP_PAGE_SHIFT, at,
              at, at, TIDE_HEAP_PAGE_SHIFT, TIDE_HEAP_PAGE_SHIFT);
    return b.data;
}

// Element `index` of the list `at` (an expression), of C type `ctype` and
// `size` bytes, side by side or in chunks (tide_list_at). Asset lists aren't
// read: their elements are in the pack's pools.
static void view_list_items(sb *o, const char *at, const char *ctype, const int size, const char *index, const bool natvis)
{
    const char *blk = view_block(at);
    const unsigned per = TIDE_LIST_CHUNK / (unsigned)(size > 0 ? size : 1);
    if (natvis) {
        sb_put(o, "          <ValueNode Condition=\"");
        xml_printf(o, "(%s->b & 0x%Xu) == 0", blk, TIDE_LIST_CHUNKED);
        sb_put(o, "\">");
        xml_printf(o, "((%s*)(%s + 1))[%s]", ctype, blk, index);
        sb_put(o, "</ValueNode>\n          <ValueNode>");
        xml_printf(o, "*(%s*)((char*)(tide_world_heaps[(%s) >> 30]->page[((unsigned int*)(%s + 1))[%s / %u] >> %u] + 1) + %u + (%s %% %u) * %d)",
                   ctype, at, blk, index, per, TIDE_HEAP_PAGE_SHIFT, (unsigned)sizeof(tide_block), index, per, size);
        sb_put(o, "</ValueNode>\n");
    }
}

static bool view_container(const type t)
{
    return t.kind == TY_LIST || t.kind == TY_MAP || t.kind == TY_SET;
}

static bool view_needs_type(const decl *d)
{
    for (int i = 0; i < d->fields.count; i++) {
        const type t = d->fields.items[i].type;
        if (view_container(t) || t.kind == TY_ENUM) return true;
    }
    return false;
}

// The natvis of one field of a struct, as `path` (the field's C member) reads it.
static void natvis_field(sb *o, const gen *g, const field *f, const char *path)
{
    const type t = f->type;
    sb name = {0};
    sb_printf(&name, STR_FMT, STR_ARG(f->name));
    if (t.kind == TY_LIST) {
        const type e = list_elem(t.decl);
        sb at = {0};
        sb_printf(&at, "%s.at", path);
        const char *blk = view_block(at.data);
        sb_put(o, "      <Synthetic Name=\"");
        xml_put(o, name.data);
        sb_put(o, "\">\n        <DisplayString Condition=\"");
        xml_printf(o, "%s == 0", at.data);
        sb_put(o, "\">{{ count = 0 }}</DisplayString>\n        <DisplayString Condition=\"");
        xml_printf(o, "%s->b == 0x%Xu", blk, TIDE_LIST_ASSET);
        sb_put(o, "\">{{ count = {");
        xml_printf(o, "%s->a", blk);
        sb_put(o, "}, an asset's }}</DisplayString>\n        <DisplayString>{{ count = {");
        xml_printf(o, "%s->a", blk);
        sb_put(o, "} }}</DisplayString>\n        <Expand>\n          <IndexListItems Condition=\"");
        xml_printf(o, "%s != 0 && %s->b != 0x%Xu", at.data, blk, TIDE_LIST_ASSET);
        sb_put(o, "\">\n          <Size>");
        xml_printf(o, "%s->a", blk);
        sb_put(o, "</Size>\n");
        view_list_items(o, at.data, view_ctype(e), view_size(e), "$i", true);
        sb_put(o, "          </IndexListItems>\n        </Expand>\n      </Synthetic>\n");
        return;
    }
    if (t.kind == TY_MAP || t.kind == TY_SET) {
        const bool set = t.kind == TY_SET;
        sb entry = {0};
        sb_printf(&entry, "tide_map%d_entry", t.decl->index);
        sb at = {0};
        sb_printf(&at, "%s.entries.at", path);
        const char *blk = view_block(at.data);
        sb ed = {0};
        const decl entry_decl = map_entry_decl(t.decl);
        const int size = decl_layout(&entry_decl).size;
        const unsigned per = TIDE_LIST_CHUNK / (unsigned)(size > 0 ? size : 1);
        sb_printf(&ed, "((%s->b & 0x%Xu) == 0 ? ((%s*)(%s + 1)) + i : (%s*)((char*)(tide_world_heaps[(%s) >> 30]->page[((unsigned int*)(%s + 1))[i / %u] >> %u] + 1) + %u + (i %% %u) * %d))",
                  blk, TIDE_LIST_CHUNKED, entry.data, blk, entry.data, at.data, blk, per, TIDE_HEAP_PAGE_SHIFT,
                  (unsigned)sizeof(tide_block), per, size);
        sb_put(o, "      <Synthetic Name=\"");
        xml_put(o, name.data);
        sb_put(o, "\">\n        <DisplayString>{{ count = {");
        xml_printf(o, "%s.live", path);
        sb_put(o, "} }}</DisplayString>\n        <Expand>\n          <CustomListItems Condition=\"");
        xml_printf(o, "%s != 0", at.data);
        sb_put(o, "\" MaxItemsPerView=\"5000\">\n            <Variable Name=\"i\" InitialValue=\"0\"/>\n");
        sb_put(o, "            <Variable Name=\"n\" InitialValue=\"");
        xml_printf(o, "(int)%s->a", blk);
        sb_put(o, "\"/>\n            <Variable Name=\"e\" InitialValue=\"");
        xml_printf(o, "(%s*)0", entry.data);
        sb_put(o, "\"/>\n            <Loop Condition=\"i &lt; n\">\n              <Exec>");
        xml_printf(o, "e = %s", ed.data);
        sb_put(o, "</Exec>\n              <If Condition=\"e-&gt;tide_order &gt; 0\">\n");
        if (set) sb_put(o, "                <Item>e-&gt;key</Item>\n");
        else sb_put(o, "                <Item Name=\"[{e-&gt;key}]\">e-&gt;value</Item>\n");
        sb_put(o, "              </If>\n              <Exec>i++</Exec>\n            </Loop>\n          </CustomListItems>\n"
                  "        </Expand>\n      </Synthetic>\n");
        return;
    }
    if (t.kind == TY_ENUM) {
        sb_put(o, "      <Synthetic Name=\"");
        xml_put(o, name.data);
        sb_put(o, "\">\n");
        for (int k = 0; k < t.decl->members.count; k++) {
            sb_put(o, "        <DisplayString Condition=\"");
            xml_printf(o, "%s == %lld", path, (long long)t.decl->members.items[k].number);
            sb_put(o, "\">");
            xml_printf(o, STR_FMT, STR_ARG(t.decl->members.items[k].name));
            sb_put(o, "</DisplayString>\n");
        }
        sb_put(o, "        <DisplayString>{");
        xml_put(o, path);
        sb_put(o, "}</DisplayString>\n      </Synthetic>\n");
        return;
    }
    (void)g;
    sb_put(o, "      <Item Name=\"");
    xml_put(o, name.data);
    sb_put(o, "\">");
    xml_put(o, path);
    sb_put(o, "</Item>\n");
}

static void natvis_struct(sb *o, const gen *g, const decl *d, const char *cname)
{
    sb_put(o, "  <Type Name=\"");
    xml_put(o, cname);
    sb_put(o, "\">\n    <Expand>\n");
    for (int i = 0; i < d->fields.count; i++) natvis_field(o, g, &d->fields.items[i], field_cname(&d->fields.items[i]));
    sb_put(o, "    </Expand>\n  </Type>\n");
}

// One per C name: the program's lists name some types more than once.
typedef struct view_seen {
    VEC(const char *) names;
} view_seen;

static bool view_first(view_seen *seen, const char *name)
{
    for (int i = 0; i < seen->names.count; i++) {
        if (strcmp(seen->names.items[i], name) == 0) return false;
    }
    vec_push(seen->names, name);
    return true;
}

// The struct types whose fields views describe: every type with a list, a
// map, a set or an enum in it. A natvis type that matches nothing is ignored,
// so a type the program doesn't emit costs nothing.
typedef struct view_type {
    const decl *d;
    const char *cname;
} view_type;

typedef VEC(view_type) view_types;

static void view_add(view_types *out, view_seen *seen, const decl *d, const char *cname)
{
    if (!d || !view_needs_type(d) || !view_first(seen, cname)) return;
    vec_push(*out, ((view_type){d, cname}));
}

static view_types view_collect(const gen *g)
{
    const program *prog = g->prog;
    view_types out = {0};
    view_seen seen = {0};
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (d->kind == DECL_COMPONENT || d->kind == DECL_SINGLETON || d->kind == DECL_STRUCT || d->kind == DECL_EVENT
            || d->kind == DECL_INPUT)
            view_add(&out, &seen, d, type_cname(d));
    }
    for (int i = 0; i < prog->structs.count; i++) view_add(&out, &seen, prog->structs.items[i], type_cname(prog->structs.items[i]));
    for (int i = 0; i < prog->local_parts.count; i++) view_add(&out, &seen, prog->local_parts.items[i], type_cname(prog->local_parts.items[i]));
    for (int i = 0; i < prog->maps.count; i++) {
        decl *entry = arena_alloc(sizeof(decl));
        *entry = map_entry_decl(prog->maps.items[i]);
        view_add(&out, &seen, entry, type_cname(entry));
    }
    return out;
}

static void write_natvis(const gen *g, sb *o)
{
    const program *prog = g->prog;
    sb_put(o, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    sb_put(o, "<!-- Generated by tidec: how debuggers show this game's values. Do not edit. -->\n");
    sb_put(o, "<AutoVisualizer xmlns=\"http://schemas.microsoft.com/vstudio/debugger/natvis/2010\">\n");
    // The engine's values
    sb_put(o, "  <Type Name=\"tide_entity\">\n"
              "    <DisplayString Condition=\"generation == 0\">none</DisplayString>\n"
              "    <DisplayString>Entity({index}:{generation})</DisplayString>\n"
              "  </Type>\n"
              "  <Type Name=\"tide_str\">\n"
              "    <DisplayString>{ptr,[bytes]s8}</DisplayString>\n"
              "    <StringView>ptr,[bytes]s8</StringView>\n"
              "  </Type>\n");
    const char *text = view_block("at");
    sb_put(o, "  <Type Name=\"tide_text\">\n    <DisplayString Condition=\"at == 0\">\"\"</DisplayString>\n    <DisplayString>{");
    xml_printf(o, "(char*)(%s + 1),[%s->a]s8", text, text);
    sb_put(o, "}</DisplayString>\n    <StringView>");
    xml_printf(o, "(char*)(%s + 1),[%s->a]s8", text, text);
    sb_put(o, "</StringView>\n  </Type>\n");
    sb_put(o, "  <Type Name=\"tide_list\">\n    <DisplayString Condition=\"at == 0\">{{ count = 0 }}</DisplayString>\n"
              "    <DisplayString>{{ count = {");
    xml_printf(o, "%s->a", text);
    sb_put(o, "} }}</DisplayString>\n  </Type>\n");
    sb_put(o, "  <Type Name=\"tide_map\">\n    <DisplayString>{{ count = {live} }}</DisplayString>\n  </Type>\n");
    // What generated code keeps a function's list and map lookups in: the engine's, nothing of the game's
    sb_put(o, "  <Type Name=\"tide_list_cache\">\n    <DisplayString>the engine's</DisplayString>\n    <Expand HideRawView=\"true\"/>\n  </Type>\n"
              "  <Type Name=\"tide_map_view\">\n    <DisplayString>the engine's</DisplayString>\n    <Expand HideRawView=\"true\"/>\n  </Type>\n");
    static const char *const vectors[][2] = {
        {"tide_float2", "({x}, {y})"},         {"tide_float3", "({x}, {y}, {z})"},
        {"tide_float4", "({x}, {y}, {z}, {w})"}, {"tide_int2", "({x}, {y})"},
        {"tide_int3", "({x}, {y}, {z})"},       {"tide_int4", "({x}, {y}, {z}, {w})"},
        {"tide_double2", "({x}, {y})"},        {"tide_double3", "({x}, {y}, {z})"},
        {"tide_double4", "({x}, {y}, {z}, {w})"}, {"tide_quaternion", "({x}, {y}, {z}, {w})"},
        {"tide_color", "Color({r}, {g}, {b}, {a})"},
    };
    for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++) {
        sb_printf(o, "  <Type Name=\"%s\">\n    <DisplayString>%s</DisplayString>\n  </Type>\n", vectors[i][0], vectors[i][1]);
    }
    // T? and `T fails E`: the value, or that there's none
    for (int i = 0; i < prog->results.count; i++) {
        const decl *r = prog->results.items[i];
        const bool fails = r->fields.count > 1;
        sb_printf(o, "  <Type Name=\"%s\">\n", result_cname(r));
        if (fails) sb_put(o, "    <DisplayString Condition=\"!ok\">fails {error}</DisplayString>\n");
        else sb_put(o, "    <DisplayString Condition=\"!ok\">none</DisplayString>\n");
        sb_put(o, "    <DisplayString>{value}</DisplayString>\n    <Expand>\n      <ExpandedItem Condition=\"ok\">value</ExpandedItem>\n");
        if (fails) sb_put(o, "      <Item Name=\"error\" Condition=\"!ok\">error</Item>\n");
        sb_put(o, "    </Expand>\n  </Type>\n");
    }
    for (int i = 0; i < prog->optional_fields.count; i++) {
        const decl *r = prog->optional_fields.items[i];
        sb_put(o, "  <Type Name=\"");
        xml_put(o, type_cname(r));
        sb_put(o, "\">\n    <DisplayString Condition=\"!ok\">none</DisplayString>\n    <DisplayString>{value}</DisplayString>\n"
                  "    <Expand>\n      <ExpandedItem Condition=\"ok\">value</ExpandedItem>\n    </Expand>\n  </Type>\n");
    }
    // T[N]: its elements, without the struct C wraps them in
    for (int i = 0; i < prog->arrays.count; i++) {
        sb_printf(o, "  <Type Name=\"%s\">\n    <DisplayString>{at}</DisplayString>\n    <Expand>\n"
                     "      <ExpandedItem>at</ExpandedItem>\n    </Expand>\n  </Type>\n",
                  array_cname(prog->arrays.items[i]));
    }
    // The game's own types
    const view_types types = view_collect(g);
    for (int i = 0; i < types.count; i++) natvis_struct(o, g, types.items[i].d, types.items[i].cname);
    sb_put(o, "</AutoVisualizer>\n");
}

// ---------------------------------------------------------------------------
// LLDB: the runtime part is debug_views.py, embedded in tidec; this writes the
// game's tables after it.

extern const unsigned char tide_debug_views_py[];
extern const unsigned int tide_debug_views_py_size;

// A Python string literal of `s`.
static void py_str(sb *o, const char *s)
{
    sb_put(o, "\"");
    for (; *s; s++) {
        if (*s == '\\' || *s == '"') sb_printf(o, "\\%c", *s);
        else sb_printf(o, "%c", *s);
    }
    sb_put(o, "\"");
}

// What a field or local is to the views: ("list", element type, element size),
// ("map", entry type, set?), or nothing for what C's own types show.
static bool py_kind(sb *o, const type t)
{
    if (t.kind == TY_LIST) {
        const type e = list_elem(t.decl);
        sb_put(o, "(\"list\", ");
        py_str(o, view_ctype(e));
        sb_printf(o, ", %d)", view_size(e));
        return true;
    }
    if (t.kind == TY_MAP || t.kind == TY_SET) {
        const decl entry = map_entry_decl(t.decl);
        sb_printf(o, "(\"map\", \"tide_map%d_entry\", %d, %s)", t.decl->index, decl_layout(&entry).size,
                  t.kind == TY_SET ? "True" : "False");
        return true;
    }
    if (t.kind == TY_ENUM) {
        sb_put(o, "(\"enum\", ");
        py_str(o, type_cname(t.decl));
        sb_put(o, ")");
        return true;
    }
    return false;
}

// The locals and parameters of one function, as its C name says them.
typedef struct py_locals {
    sb *o;
    const gen *g;
    const char *function;
} py_locals;

static void py_local(py_locals *l, const str name, const type t)
{
    if (!view_container(t) && t.kind != TY_ENUM) return;
    sb_put(l->o, "    (");
    py_str(l->o, l->function);
    sb_put(l->o, ", ");
    py_str(l->o, local_cname(l->g, name));
    sb_put(l->o, "): ");
    py_kind(l->o, t);
    sb_put(l->o, ",\n");
}

static void py_stmt_locals(py_locals *l, const stmt *s)
{
    if (!s) return;
    if (s->kind == S_VAR || s->kind == S_FOREACH) py_local(l, s->name, s->type);
    for (int i = 0; i < s->stmts.count; i++) py_stmt_locals(l, s->stmts.items[i]);
    py_stmt_locals(l, s->then_stmt);
    py_stmt_locals(l, s->else_stmt);
    py_stmt_locals(l, s->init);
    for (int c = 0; c < s->cases.count; c++) {
        for (int i = 0; i < s->cases.items[c].body.count; i++) py_stmt_locals(l, s->cases.items[c].body.items[i]);
    }
}

static void py_function(sb *o, const gen *g, const char *function, const decl *d, const stmt *body)
{
    py_locals l = {o, g, function};
    for (int i = 0; i < d->params.count; i++) py_local(&l, d->params.items[i].name, d->params.items[i].type);
    if (body) py_stmt_locals(&l, body);
}

static void write_lldb(const gen *g, sb *o)
{
    const program *prog = g->prog;
    sb_printf(o, "# Generated by tidec from %s: how LLDB shows this game's values. Do not edit.\n", g->from);
    sb_put(o, "# Load it with `command script import <this file>`, as the editors' debug configurations do.\n\n");
    sb_putn(o, (const char *)tide_debug_views_py, tide_debug_views_py_size);
    sb_printf(o, "\n# The runtime's layout of heaps and lists (tide/text.h, tide/heap.h, tide/list.h)\n"
                 "TIDE_IN_SCRATCH = %u\nTIDE_SCRATCH_SHIFT = %u\nTIDE_SCRATCH_BYTES = %u\nTIDE_HEAP_PAGE_SHIFT = %u\n"
                 "TIDE_LIST_CHUNKED = 0x%X\nTIDE_LIST_ASSET = 0x%X\nTIDE_LIST_CHUNK = %u\n",
              TIDE_IN_SCRATCH, TIDE_SCRATCH_SHIFT, TIDE_SCRATCH_BYTES, TIDE_HEAP_PAGE_SHIFT, TIDE_LIST_CHUNKED,
              TIDE_LIST_ASSET, (unsigned)TIDE_LIST_CHUNK);
    sb_put(o, "\n# The game's own types: their fields that C's types don't say enough about\nTIDE_TYPES = {\n");
    const view_types types = view_collect(g);
    for (int i = 0; i < types.count; i++) {
        const decl *d = types.items[i].d;
        sb_put(o, "    ");
        py_str(o, types.items[i].cname);
        sb_put(o, ": [\n");
        for (int k = 0; k < d->fields.count; k++) {
            const field *f = &d->fields.items[k];
            sb tide = {0};
            sb_printf(&tide, STR_FMT, STR_ARG(f->name));
            sb_put(o, "        (");
            py_str(o, tide.data);
            sb_put(o, ", ");
            py_str(o, field_cname(f));
            sb_put(o, ", ");
            if (!py_kind(o, f->type)) sb_put(o, "None");
            sb_put(o, "),\n");
        }
        sb_put(o, "    ],\n");
    }
    sb_put(o, "}\n\n# Enums: each member's name by its number\nTIDE_ENUMS = {\n");
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (d->kind != DECL_ENUM) continue;
        sb_put(o, "    ");
        py_str(o, type_cname(d));
        sb_put(o, ": {");
        for (int k = 0; k < d->members.count; k++) {
            sb name = {0};
            sb_printf(&name, STR_FMT, STR_ARG(d->members.items[k].name));
            sb_printf(o, "%s%lld: ", k ? ", " : "", (long long)d->members.items[k].number);
            py_str(o, name.data);
        }
        sb_put(o, "},\n");
    }
    sb_put(o, "}\n\n# Locals and parameters, by their function's C name and their own\nTIDE_LOCALS = {\n");
    for (int i = 0; i < prog->systems.count; i++) {
        const decl *d = prog->systems.items[i];
        if (d->extern_system || !d->body) continue;
        sb n = {0};
        sb_printf(&n, "tide_system_%s", decl_cname(d));
        py_function(o, g, n.data, d, d->body);
    }
    for (int i = 0; i < prog->views.count; i++) {
        const decl *d = prog->views.items[i];
        sb n = {0};
        sb_printf(&n, "tide_view_%s", decl_cname(d));
        py_function(o, g, n.data, d, d->body);
    }
    for (int i = 0; i < prog->handlers.count; i++) {
        const decl *d = prog->handlers.items[i];
        if (d->is_async) continue;
        sb n = {0};
        sb_printf(&n, "tide_handler_%s", decl_cname(d));
        py_function(o, g, n.data, d, d->body);
    }
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (d->kind != DECL_FUNCTION || d->is_extern || !d->body) continue;
        py_function(o, g, routine_cname(d), d, d->body);
    }
    for (int i = 0; i < prog->decls.count; i++) {
        const decl *d = prog->decls.items[i];
        if (d->kind == DECL_FUNCTION) continue;
        for (int k = 0; k < d->methods.count; k++) {
            const decl *m = d->methods.items[k];
            if (m->body && !m->is_extern) py_function(o, g, routine_cname(m), m, m->body);
        }
    }
    sb_put(o, "}\n\n\ndef __lldb_init_module(debugger, internal_dict):\n    tide_register(debugger, __name__)\n");
}

// Writes <out_dir>/<name>.natvis and <out_dir>/<name>_lldb.py.
static bool write_debug_views(gen *g)
{
    sb natvis = {0};
    write_natvis(g, &natvis);
    sb lldb = {0};
    write_lldb(g, &lldb);
    sb natvis_path = {0};
    sb_printf(&natvis_path, "%s/%s.natvis", g->opts->out_dir, g->opts->name);
    sb lldb_path = {0};
    sb_printf(&lldb_path, "%s/%s_lldb.py", g->opts->out_dir, g->opts->name);
    return write_file(natvis_path.data, &natvis) && write_file(lldb_path.data, &lldb);
}
