// Private native authoring module. Ordinary generated game code supplies all
// storage/drawing; only the compiled view selection and startup differ.
static void gen_editor_preview(gen *g)
{
    if (!g->opts->editor_prefab) return;
    sb *o = &g->c;
    const decl *factory = g->prog->editor_create;
    sb_put(o, "\n#include \"tide/run.h\"\n"
        "typedef struct tide_editor_state {\n"
        "    tide_world world; tide_local local; tide_draw_list draw; tide_gui gui;\n"
        "    float width, height;\n"
        "} tide_editor_state;\n\n"
        "static void *tide_editor_create(const uint8_t *pack, size_t size)\n{\n");
    sb_put(o, "    tide_editor_state *p = calloc(1, sizeof *p);\n    if (!p) return NULL;\n");
    if (g->prog->assets.count) sb_put(o, "    if (!tide_assets_load(pack, size)) { tide_run_unload(); free(p); return NULL; }\n");
    else sb_put(o, "    (void)pack; (void)size;\n");
    sb_put(o, "    tide_world_init(&p->world, 1.0f/60.0f);\n    tide_local_init(&p->local);\n");
    sb_printf(o, "    (void)tide_function_%s(", decl_cname(factory));
    bool comma = false;
    if (takes_world(factory)) { sb_put(o, "&p->world"); comma = true; }
    if (takes_local(factory)) { sb_printf(o, "%s&p->local", comma ? ", " : ""); comma = true; }
    if (takes_scene(g->prog, factory)) sb_printf(o, "%s(tide_entity){0}", comma ? ", " : "");
    sb_put(o, ");\n    tide_apply_commands(&p->world);\n    tide_local_apply_commands(&p->local);\n    return p;\n}\n\n"
        "static const tide_draw_list *tide_editor_frame(void *context, float seconds, tide_rect r,\n"
        "    tide_float4x4 camera, tide_float4x4 projection, tide_measure_fn measure)\n{\n"
        "    tide_editor_state *p = context;\n"
        "    if (!(r.width > 0 && r.height > 0)) return NULL;\n"
        "    if (!tide_editor_target.id) tide_editor_target = tide_render_target_make(r.width, r.height);\n"
        "    else if (p->width != r.width || p->height != r.height) tide_render_target_resize(tide_editor_target, r.width, r.height);\n"
        "    p->width = r.width; p->height = r.height;\n"
        "    tide_draw_reset(&p->draw);\n    tide_draw_screen_size(&p->draw, tide_f2(r.width, r.height));\n"
        "    tide_draw_render_target(&p->draw, tide_editor_target);\n"
        "    tide_draw_clear(&p->draw, (tide_color){.065f,.08f,.1f,1});\n"
        "    if (projection.c0.x > 0 && projection.c1.y > 0)\n"
        "        tide_draw_set_camera(&p->draw, tide_f2(-projection.c3.x/projection.c0.x, -projection.c3.y/projection.c1.y), 1/projection.c1.y);\n"
        "    tide_draw_set_camera_matrices(&p->draw, camera, projection);\n"
        "    const tide_devices devices = {0};\n"
        "    tide_gui_begin(&p->gui, &devices, tide_f2(r.width,r.height), measure);\n"
        "    tide_draw_render_target(&p->gui.list, tide_editor_target);\n"
        "    tide_local_frame_time(&p->local, seconds);\n"
        "    tide_frame(&p->world, NULL, 1, NULL, NULL, 1, &p->local, &p->draw, &p->gui);\n"
        "    tide_gui_end(&p->gui, &p->draw);\n"
        "    const uint32_t texture = tide_draw_texture_source(&p->draw, tide_render_target_color(tide_editor_target));\n"
        "    tide_draw_render_target(&p->draw, (tide_render_target){.id=TIDE_RENDER_WINDOW});\n"
        "    tide_draw_set_screen(&p->draw);\n"
        "    const tide_color white = {1,1,1,1};\n"
        "    const tide_vertex v[4] = {\n"
        "        {{r.x,r.y},{0,0},white}, {{r.x+r.width,r.y},{1,0},white},\n"
        "        {{r.x+r.width,r.y+r.height},{1,1},white}, {{r.x,r.y+r.height},{0,1},white}};\n"
        "    const uint32_t indices[6] = {0,1,2,0,2,3};\n"
        "    const uint32_t base = tide_draw_vertices(&p->draw, v, 4);\n"
        "    tide_draw_triangles_with(&p->draw, base, indices, 6, texture, TIDE_FILTER_BILINEAR, TIDE_WRAP_CLAMP);\n"
        "    tide_draw_prepare(&p->draw, NULL);\n    return &p->draw;\n}\n\n"
        "static void tide_editor_destroy(void *context)\n{\n"
        "    tide_editor_state *p = context;\n    if (!p) return;\n"
        "    tide_world_free(&p->world);\n    tide_local_free(&p->local);\n"
        "    tide_draw_free(&p->draw);\n    tide_draw_free(&p->gui.list);\n"
        "    tide_run_unload();\n    tide_editor_target = (tide_render_target){0};\n    free(p);\n}\n\n"
        "TIDE_HOST_EXPORT const tide_editor_preview_api *tide_editor_preview(void)\n{\n");
    sb_printf(o, "    static const tide_editor_preview_api api = {TIDE_EDITOR_PREVIEW_VERSION, %du, tide_editor_create, tide_editor_frame, tide_editor_destroy};\n    return &api;\n}\n", g->prog->views.count);
}
