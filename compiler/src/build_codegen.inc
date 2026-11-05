// Build code (docs/spec.md, Build code; build_code.c runs it): in the game, the constants it made; and the program
// that runs it (codegen_options.build_program), which is the game's C with a main that calls each asset's and
// constant's code and writes what it returned for tidec to read (build_code.c reads it back).

static void put_c_bytes(sb *o, const uint8_t *data, const size_t size)
{
    for (size_t i = 0; i < size; i++) sb_printf(o, "%s0x%02x", i ? "," : "", (unsigned)data[i]);
    if (!size) sb_put(o, "0");
}

// The constants build code made: what reads them reads these (build_constant_cname).
static void gen_build_constants(gen *g)
{
    sb *o = &g->c;
    bool any = false;
    for (int i = 0; i < g->prog->build_roots.count; i++) {
        const decl *d = g->prog->build_roots.items[i];
        if (d->kind != DECL_CONST || !d->build_fn) continue;
        if (!any) sb_put(o, "// Constants build code made\n");
        any = true;
        const type t = d->return_type;
        const char *name = decl_cname(d);
        if (generating_build) { // Set by main as their code runs, in order
            sb_printf(o, "static %s tide_const_%s; // " STR_FMT "\n", c_type(t), name, STR_ARG(d->qualified));
            continue;
        }
        const build_value *v = d->built;
        if (t.kind == TY_LIST) continue; // The pack's: a hidden asset (build_code.c, build_code_apply)
        if (t.kind == TY_STRING) {
            sb_printf(o, "static const tide_str tide_const_%s = {", name);
            gen_c_raw_literal(o, (str){v && v->size ? (const char *)v->data : "", v ? (int)v->size : 0});
            sb_printf(o, ", %d, %d}; // " STR_FMT "\n", v ? (int)v->size : 0, v ? (int)v->chars : 0, STR_ARG(d->qualified));
            continue;
        }
        // A plain value: its bytes, as build code left them, which C reads as its type while compiling
        const int size = type_layout(t).size;
        sb_printf(o, "static const union { uint8_t bytes[%d]; %s value; } tide_const_%s = {{", size > 0 ? size : 1, c_type(t), name);
        if (v && (int)v->size == size) put_c_bytes(o, v->data, v->size);
        else put_c_bytes(o, NULL, 0);
        sb_printf(o, "}}; // " STR_FMT "\n", STR_ARG(d->qualified));
    }
    if (any) sb_put(o, "\n");
}

// The build program's main: each root's code, in the order build_code.c put them (what one reads, first), and what
// it returned, written for tidec.
static void gen_build_main(gen *g)
{
    sb *o = &g->c;
    const program *prog = g->prog;
    sb_put(o, "\n// ---------------------------------------------------------------------------\n"
              "// Build code's program (tidec's build_code.c): each asset's and constant's code, and what it made\n\n"
              "#include <stdio.h>\n#include <stdlib.h>\n\n");
    // Bold is supplied by the platform's optional font object, not engine/src. Build code cannot draw,
    // but the game's unused drawing routines are still emitted and must link without the platform.
    if (prog->names_bold) sb_put(o, "const tide_font tide_font_bold = {0};\n\n");
    sb_put(o, "static FILE *tide_build_out;\n\n"
              "static void tide_build_put(const void *data, const uint64_t size)\n{\n"
              "    fwrite(&size, sizeof size, 1, tide_build_out);\n"
              "    if (size) fwrite(data, 1, (size_t)size, tide_build_out);\n}\n\n"
              "static void tide_build_list(const tide_list l, const uint32_t size)\n{\n"
              "    const int32_t count = tide_list_count(l);\n"
              "    const uint64_t bytes = (uint64_t)(count > 0 ? count : 0) * size;\n"
              "    fwrite(&bytes, sizeof bytes, 1, tide_build_out);\n"
              "    for (int32_t i = 0; i < count; i++) fwrite(tide_list_at(l, i, size), 1, size, tide_build_out);\n}\n\n"
              "static void tide_build_grid(const tide_grid g, const tide_grid_shape *s)\n{\n"
              "    int32_t size[3];\n    tide_grid_size(g, size);\n"
              "    const int32_t wh[2] = {size[0] > 0 && size[1] > 0 ? size[0] : 0, size[0] > 0 && size[1] > 0 ? size[1] : 0};\n"
              "    tide_build_put(wh, sizeof wh);\n"
              "    const uint64_t bytes = (uint64_t)wh[0] * (uint64_t)wh[1] * s->cell;\n"
              "    fwrite(&bytes, sizeof bytes, 1, tide_build_out);\n"
              "    static const uint8_t zero[64];\n"
              "    for (int32_t y = 0; y < wh[1]; y++) {\n"
              "        for (int32_t x = 0; x < wh[0]; x++) {\n"
              "            const void *c = tide_grid_peek(g, x, y, 0, s);\n"
              "            fwrite(c ? c : zero, 1, s->cell, tide_build_out);\n"
              "        }\n    }\n}\n\n");
    if (prog->mesh_level->named) { // A mesh's levels, in one blob: each one's error, its count of indices, and them
        const decl *level = prog->mesh_level;
        const char *indices = field_cname(&level->fields.items[0]), *error = field_cname(&level->fields.items[1]);
        sb_printf(o, "static void tide_build_levels(const tide_list levels)\n{\n"
                     "    const int32_t count = tide_list_count(levels);\n    uint64_t bytes = 0;\n"
                     "    for (int32_t i = 0; i < count; i++) {\n"
                     "        const %s *l = tide_list_at(levels, i, sizeof *l);\n"
                     "        bytes += 8u + 4u * (uint64_t)tide_list_count(l->%s);\n    }\n"
                     "    fwrite(&bytes, sizeof bytes, 1, tide_build_out);\n"
                     "    for (int32_t i = 0; i < count; i++) {\n"
                     "        const %s *l = tide_list_at(levels, i, sizeof *l);\n"
                     "        const int32_t n = tide_list_count(l->%s);\n"
                     "        fwrite(&l->%s, 4, 1, tide_build_out);\n        fwrite(&n, 4, 1, tide_build_out);\n"
                     "        for (int32_t k = 0; k < n; k++) fwrite(tide_list_at(l->%s, k, 4u), 4, 1, tide_build_out);\n"
                     "    }\n}\n\n",
                  type_cname(level), indices, type_cname(level), indices, error, indices);
    }
    sb_put(o, "int main(int argc, char **argv)\n{\n"
              "    if (argc != 3) {\n        fprintf(stderr, \"usage: <pack> <results>\\n\");\n        return 2;\n    }\n");
    if (prog->assets.count) {
        sb_put(o, "    FILE *in = fopen(argv[1], \"rb\");\n"
                  "    if (!in) {\n        fprintf(stderr, \"can't read the pack '%s'\\n\", argv[1]);\n        return 2;\n    }\n"
                  "    fseek(in, 0, SEEK_END);\n    const long size = ftell(in);\n    fseek(in, 0, SEEK_SET);\n"
                  "    uint8_t *pack = malloc(size > 0 ? (size_t)size : 1u);\n"
                  "    if (!pack || size < 0 || fread(pack, 1, (size_t)size, in) != (size_t)size) return 2;\n"
                  "    fclose(in);\n"
                  "    if (!tide_assets_load(pack, (size_t)size)) {\n        fprintf(stderr, \"the pack doesn't load\\n\");\n        return 2;\n    }\n");
    }
    sb_put(o, "    static tide_heap tide_build_heap;\n    tide_text_use(&tide_build_heap, NULL);\n");
    if (prog->assets.count) sb_put(o, "    tide_pack_use();\n");
    sb_put(o, "    tide_build_out = fopen(argv[2], \"wb\");\n    if (!tide_build_out) return 2;\n");
    for (int i = 0; i < prog->build_roots.count; i++) {
        const decl *d = prog->build_roots.items[i];
        const decl *fn = d->build_fn;
        if (!fn) continue;
        const type t = fn->return_type;
        const char *call = routine_cname(fn);
        sb_printf(o, "    { // " STR_FMT "\n", STR_ARG(d->qualified));
        sb_printf(o, "        const uint32_t tide_index = %du;\n        fwrite(&tide_index, sizeof tide_index, 1, tide_build_out);\n", i);
        if (d->kind == DECL_CONST) {
            if (t.kind == TY_STRING) {
                sb_printf(o, "        const tide_str tide_v = %s();\n        tide_build_put(tide_v.ptr, (uint64_t)tide_v.bytes);\n", call);
            } else if (t.kind == TY_LIST) {
                sb_printf(o, "        const tide_list tide_v = %s();\n        tide_build_list(tide_v, (uint32_t)sizeof(%s));\n", call,
                          c_type(list_elem(t.decl)));
            } else {
                sb_printf(o, "        %s tide_v;\n        memset(&tide_v, 0, sizeof tide_v);\n        tide_v = %s();\n"
                             "        tide_build_put(&tide_v, sizeof tide_v);\n", c_type(t), call);
            }
            sb_printf(o, "        tide_const_%s = tide_v;\n    }\n", decl_cname(d));
            continue;
        }
        const char *global = asset_cname(d);
        switch (d->asset_kind) {
        case ASSET_TEXT:
            sb_printf(o, "        const tide_str tide_v = %s();\n        tide_build_put(tide_v.ptr, (uint64_t)tide_v.bytes);\n"
                         "        %s = tide_v;\n", call, global);
            break;
        case ASSET_BYTES:
            sb_printf(o, "        const tide_list tide_v = %s();\n        tide_build_list(tide_v, 1u);\n        %s = tide_v;\n", call, global);
            break;
        case ASSET_GRID: case ASSET_TEXTURE:
            sb_printf(o, "        const tide_grid tide_v = %s();\n        tide_build_grid(tide_v, &%s);\n", call, grid_shape_name(t.decl));
            if (d->asset_kind == ASSET_GRID) sb_printf(o, "        %s = tide_v;\n", global);
            break;
        case ASSET_MESH: {
            const decl *ref = t.kind == TY_STRUCT && t.decl->asset_ref_of ? t.decl : NULL;
            const decl *vertex = asset_vertex(d->return_type);
            if (!ref || !vertex) {
                sb_put(o, "        tide_build_put(NULL, 0);\n        tide_build_put(NULL, 0);\n        tide_build_put(NULL, 0);\n    }\n");
                continue;
            }
            sb_printf(o, "        const %s tide_v = %s();\n        const tide_mesh_lists tide_m = tide_resolve_%s(tide_v);\n"
                         "        tide_build_list(tide_m.vertices, (uint32_t)sizeof(%s));\n        tide_build_list(tide_m.indices, 4u);\n",
                      c_type(t), call, type_cname(ref), type_cname(vertex));
            if (build_levels_table) sb_put(o, "        tide_build_levels(tide_build_levels_of(tide_v.id));\n");
            else sb_put(o, "        tide_build_put(NULL, 0);\n");
            sb_printf(o, "        %s = tide_m;\n", global);
            break;
        }
        case ASSET_CLIP: case ASSET_CLIP_DATA: {
            const decl *clip = t.decl;
            sb_printf(o, "        const %s tide_v = %s();\n"
                         "        const int32_t tide_shape[2] = {tide_v.%s, tide_v.%s};\n        tide_build_put(tide_shape, sizeof tide_shape);\n"
                         "        tide_build_list(tide_v.%s, 4u);\n",
                      c_type(t), call, field_cname(&clip->fields.items[0]), field_cname(&clip->fields.items[1]),
                      field_cname(&clip->fields.items[2]));
            if (d->asset_kind == ASSET_CLIP_DATA) sb_printf(o, "        %s = tide_v;\n", global);
            break;
        }
        default: break;
        }
        sb_put(o, "    }\n");
    }
    sb_put(o, "    return fclose(tide_build_out) == 0 ? 0 : 2;\n}\n");
}
