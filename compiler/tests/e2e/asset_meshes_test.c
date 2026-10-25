#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/math.h"
#include "tide/pack.h"
#include "tide/sha256.h"
#include "tide_test.h"

// The pack, as C (tide_add_game's --pack-c for HOST programs).
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void start(void)
{
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    local.Art.target = (tide_render_target){.id = 11, .size = {.width = 16, .height = 12}};
    tide_text_use(&world.heap, &local.heap); // For the lists the checks read
}

static void finish(void)
{
    tide_local_free(&local);
    tide_world_free(&world);
    tide_draw_free(&draw);
    draw = (tide_draw_list){0};
    tide_pack_unload();
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_frame(&world, NULL, 1.0f, NULL, NULL, 1.0f, &local, &draw, &gui);
    tide_text_use(&world.heap, &local.heap);
}

static bool near(const float a, const float b)
{
    return fabsf(a - b) < 1e-5f;
}

static bool at(const tide_float3 v, const float x, const float y, const float z)
{
    return near(v.x, x) && near(v.y, y) && near(v.z, z);
}

static bool colored(const tide_color c, const float r, const float g, const float b, const float a)
{
    return near(c.r, r) && near(c.g, g) && near(c.b, b) && near(c.a, a);
}

static const Corner *corner(const tide_list list, const int32_t i)
{
    return tide_list_at(list, i, sizeof(Corner));
}

// Whether a list of ints is exactly these.
static bool faces(const tide_list list, const int32_t *expected, const int32_t count)
{
    if (tide_list_count(list) != count) return false;
    for (int32_t i = 0; i < count; i++) {
        const int32_t *index = tide_list_at(list, i, sizeof(int32_t));
        if (!index || *index != expected[i]) return false;
    }
    return true;
}

// A color a file has as light, as Tide has one: the reader's arithmetic, which is the engine's.
static float display(const float linear)
{
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * tide_pow_f(linear, 1.0f / 2.4f) - 0.055f;
}

static bool defaults_are(const char *name, const uint32_t kind, const uint32_t meshes, const uint32_t vertices,
                         const int32_t text_length)
{
    const tide_pack_entry *e = tide_pack_find(name, kind);
    if (!e) return false;
    uint32_t total = 0, k = 0;
    for (;; k++) {
        uint32_t count;
        const DefaultCorner *v = tide_pack_mesh_open(e, k, (uint32_t)sizeof(DefaultCorner), &count);
        if (!v) break;
        total += count;
        for (uint32_t i = 0; i < count; i++) {
            if (v[i].textLength != text_length || v[i].byteCount != 300 || v[i].listCount != 2 || v[i].mapCount != 2
                || v[i].meshVertices != 4 || v[i].glow != (float)text_length) return false;
        }
    }
    return k == meshes && total == vertices;
}

static void check_defaults(const int32_t text_length)
{
    TIDE_CHECK(defaults_are("defaultOne", TIDE_PACK_MESH, 1, 4, text_length));
    TIDE_CHECK(defaults_are("defaultMany", TIDE_PACK_MESH_LIST, 2, 10, text_length));
    TIDE_CHECK(defaults_are("defaultNamed", TIDE_PACK_MESHES, 4, 18, text_length));
}

// Replace only the text dependency: mesh source/layout/version bytes stay as
// they were in the file, so defaults must refresh both values and draw caches.
static uint8_t *pack_with_rules(const char *changed)
{
    uint8_t *other = malloc(tide_game_pack_size);
    if (!other) return NULL;
    memcpy(other, tide_game_pack, tide_game_pack_size);
    const tide_pack_header *h = (const tide_pack_header *)(const void *)other;
    tide_pack_entry *entries = (tide_pack_entry *)(void *)(other + sizeof *h);
    uint8_t name[32];
    tide_sha256_of("laterRules", strlen("laterRules"), name);
    tide_pack_entry *rules = NULL;
    for (uint32_t i = 0; i < h->entries; i++) {
        if (entries[i].kind == TIDE_PACK_TEXT && memcmp(entries[i].name, name, sizeof name) == 0) rules = &entries[i];
    }
    const size_t size = strlen(changed);
    if (!rules || size > rules->size) {
        free(other);
        return NULL;
    }
    tide_block *block = (tide_block *)(void *)(other + h->region_at + rules->offset);
    block->a = block->b = (uint32_t)size;
    memcpy(block + 1, changed, size + 1u);
    rules->size = rules->extra = (uint32_t)size;
    return other;
}

// Mesh defaults can read later assets: every global must name the current
// pack first. Replacing its text catches defaults reading the previous load,
// even if another test already initialized those globals.
TIDE_TEST(mesh_assets_defaults_read_forward_assets_of_the_current_pack)
{
    uint8_t *other = pack_with_rules("updated");
    TIDE_REQUIRE(other);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    check_defaults(26); // The UTF-8 text has 27 bytes and 26 characters
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    check_defaults(26);
    TIDE_REQUIRE(tide_assets_load(other, tide_game_pack_size));
    check_defaults(7);
    TIDE_REQUIRE(tide_assets_load(other, tide_game_pack_size));
    check_defaults(7);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    check_defaults(26);
    free(other);
    tide_pack_unload();
}

static uint64_t check_drawn_defaults(const int32_t text_length)
{
    uint64_t version = 0;
    uint32_t drawn = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind != TIDE_DRAW_SHADED) continue;
        const tide_draw_run *r = &draw.runs[c->shaded.run];
        const tide_draw_mesh_data *m = &draw.meshes[r->mesh - 1u];
        if (m->vertex_size != sizeof(DefaultCorner)) continue;
        drawn += r->count;
        const DefaultCorner *vertices = m->bytes;
        for (uint32_t k = 0; k < m->vertex_count; k++) {
            TIDE_CHECK(vertices[k].textLength == text_length);
            TIDE_CHECK(vertices[k].glow == (float)text_length);
            TIDE_CHECK(vertices[k].byteCount == 300 && vertices[k].listCount == 2 && vertices[k].mapCount == 2
                       && vertices[k].meshVertices == 4);
        }
        // The first default draw is the single mesh; the same quad in the
        // list and map is shared and must not cumulatively change its version.
        if (!version) version = m->version;
    }
    TIDE_CHECK(drawn == 7); // One mesh, two list items and four map entries
    return version;
}

TIDE_TEST(mesh_assets_refresh_drawn_defaults_when_another_asset_changes)
{
    uint8_t *other = pack_with_rules("updated");
    uint8_t *same_length = pack_with_rules("another");
    TIDE_REQUIRE(other && same_length);
    start();
    local.Art.showDefaults = true;
    frame();
    const uint64_t original = check_drawn_defaults(26);
    const uint32_t meshes = draw.mesh_count;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    frame();
    TIDE_CHECK(check_drawn_defaults(26) == original && draw.mesh_count == meshes);
    TIDE_REQUIRE(tide_assets_load(other, tide_game_pack_size));
    check_defaults(7);
    frame();
    const uint64_t changed = check_drawn_defaults(7);
    TIDE_CHECK(changed != original);
    const uint32_t changed_meshes = draw.mesh_count;
    TIDE_REQUIRE(tide_assets_load(other, tide_game_pack_size));
    frame();
    TIDE_CHECK(check_drawn_defaults(7) == changed && draw.mesh_count == changed_meshes);
    // Different dependency bytes with the same computed defaults reuse the
    // copy too: the actual vertex values decide the version.
    TIDE_REQUIRE(tide_assets_load(same_length, tide_game_pack_size));
    frame();
    TIDE_CHECK(check_drawn_defaults(7) == changed && draw.mesh_count == changed_meshes);
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    frame();
    TIDE_CHECK(check_drawn_defaults(26) == original);
    free(other);
    free(same_length);
    finish();
}

// A .glb with everything a vertex has: each field by its name, with x turned
// around and its triangles wound the other way, as Tide has both; and the
// fields no file fills, as the struct says they start.
TIDE_TEST(mesh_assets_fill_a_vertex_struct_by_its_fields_names)
{
    start();
    const Seen *seen = &world.Seen;
    TIDE_REQUIRE(tide_list_count(seen->quad) == 4);
    static const float places[4][3] = {{-10, 20, 30}, {-11, 20, 30}, {-11, 21, 30}, {-10, 21, 30}};
    static const float uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    static const float colors[4][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 1, 128.0f / 255.0f}};
    for (int32_t i = 0; i < 4; i++) {
        const Corner *c = corner(seen->quad, i);
        TIDE_CHECK(at(c->position, places[i][0], places[i][1], places[i][2]));
        TIDE_CHECK(at(c->normal, 0, 0, 1));
        TIDE_CHECK(near(c->uv.x, uvs[i][0]) && near(c->uv.y, uvs[i][1]));
        TIDE_CHECK(colored(c->color, colors[i][0], colors[i][1], colors[i][2], colors[i][3]));
        TIDE_CHECK(near(c->tangent.x, -1) && near(c->tangent.y, 0) && near(c->tangent.z, 0) && near(c->tangent.w, -1));
        TIDE_CHECK(c->glow == 0.5f && c->kind == 7 && c->spare == 0.0f);
    }
    static const int32_t wound[6] = {0, 2, 1, 0, 3, 2};
    TIDE_CHECK(faces(seen->quadFaces, wound, 6));
    TIDE_CHECK(at(seen->second, -11, 20, 30)); // Read where the pack has it
    // The same file as the built-in vertex, and as positions alone
    TIDE_REQUIRE(tide_list_count(seen->plain) == 4 && tide_list_count(seen->points) == 4);
    for (int32_t i = 0; i < 4; i++) {
        const tide_vertex3 *v = tide_list_at(seen->plain, i, sizeof(tide_vertex3));
        const Point *p = tide_list_at(seen->points, i, sizeof(Point));
        TIDE_CHECK(at(v->position, places[i][0], places[i][1], places[i][2]) && near(v->uv.x, uvs[i][0]) && near(v->uv.y, uvs[i][1]));
        TIDE_CHECK(colored(v->color, colors[i][0], colors[i][1], colors[i][2], colors[i][3]));
        TIDE_CHECK(at(p->position, places[i][0], places[i][1], places[i][2]));
    }
    // A copy is the code's own: one more in it, and the mesh as it was
    TIDE_CHECK(seen->mine == 54);
    finish();
}

// Two primitives under a node in a node, each with a material's color: one
// mesh. Positions and normals as bytes; and a primitive with no normals and
// no indices, whose triangle gets vertices of its own and the way it faces.
TIDE_TEST(mesh_assets_merge_a_files_parts_where_its_nodes_place_them)
{
    start();
    const Seen *seen = &world.Seen;
    TIDE_REQUIRE(tide_list_count(seen->pair) == 6);
    static const float places[6][3] = {{-2, 0, 0}, {-6, 0, 0}, {-2, 4, 0}, {-2, 0, 2}, {-2, 2, 2}, {-4, 0, 2}};
    const float gray = display(0.5f);
    for (int32_t i = 0; i < 6; i++) {
        const Corner *c = corner(seen->pair, i);
        TIDE_CHECK(at(c->position, places[i][0], places[i][1], places[i][2]));
        TIDE_CHECK(at(c->normal, 0, 0, 1)); // The file's for the first three, and the triangle's own for the rest
        TIDE_CHECK(i < 3 ? colored(c->color, 1, 0, 0, 1) : colored(c->color, gray, gray, gray, 0.5f));
        TIDE_CHECK(c->uv.x == 0.0f && c->uv.y == 0.0f && c->tangent.w == 1.0f && c->glow == 0.5f && c->kind == 7);
        TIDE_CHECK(near(c->tangent.x, 1) && near(c->tangent.y, 0) && near(c->tangent.z, 0));
    }
    TIDE_CHECK(gray > 0.73f && gray < 0.74f);
    static const int32_t wound[6] = {0, 2, 1, 3, 4, 5};
    TIDE_CHECK(faces(seen->pairFaces, wound, 6));
    finish();
}

// A .gltf whose buffer is a file beside it: vertices with a stride, a strip,
// sparse index replacements, and a node that mirrors, whose triangles keep
// the file's order. And one with its buffer inside it: sparse positions and
// indices starting from zeros, a fan, and a node that turns.
TIDE_TEST(mesh_assets_read_gltf_with_its_buffers_beside_it_and_inside_it)
{
    start();
    const Seen *seen = &world.Seen;
    TIDE_REQUIRE(tide_list_count(seen->strip) == 4 && tide_list_count(seen->inside) == 4);
    static const float strip[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    static const float inside[4][3] = {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}};
    static const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const float gray = display(0.5f);
    for (int32_t i = 0; i < 4; i++) {
        const Corner *s = corner(seen->strip, i), *n = corner(seen->inside, i);
        TIDE_CHECK(at(s->position, strip[i][0], strip[i][1], strip[i][2]) && at(s->normal, 0, 0, 1));
        TIDE_CHECK(colored(s->color, 1, 1, 1, 1)); // No color and no material: white
        TIDE_CHECK(at(n->position, inside[i][0], inside[i][1], inside[i][2]) && at(n->normal, 0, 0, 1));
        TIDE_CHECK(near(n->uv.x, uvs[i][0]) && near(n->uv.y, uvs[i][1]) && colored(n->color, gray, gray, gray, 1));
    }
    static const int32_t strip_faces[6] = {0, 1, 2, 1, 3, 2};
    static const int32_t fan_faces[6] = {0, 2, 1, 0, 3, 2};
    TIDE_CHECK(faces(seen->stripFaces, strip_faces, 6));
    TIDE_CHECK(faces(seen->insideFaces, fan_faces, 6));
    finish();
}

// An OBJ: a face of four corners as two triangles that share vertices, a uv
// counted from the top, colors after positions, and a face with no normal.
TIDE_TEST(mesh_assets_read_obj)
{
    start();
    const Seen *seen = &world.Seen;
    TIDE_REQUIRE(tide_list_count(seen->box) == 7);
    static const float places[7][3] = {{0, 0, 0}, {-1, 1, 0}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 0}, {0, 0, 2}, {-1, 0, 0}};
    static const float uvs[7][2] = {{0, 1}, {1, 0}, {1, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 0}};
    static const float colors[7][3] = {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}, {1, 1, 1}, {1, 0, 0}, {1, 1, 1}, {0, 1, 0}};
    for (int32_t i = 0; i < 7; i++) {
        const Corner *c = corner(seen->box, i);
        TIDE_CHECK(at(c->position, places[i][0], places[i][1], places[i][2]));
        TIDE_CHECK(i < 4 ? at(c->normal, 0, 0, 1) : at(c->normal, 0, -1, 0));
        TIDE_CHECK(near(c->uv.x, uvs[i][0]) && near(c->uv.y, uvs[i][1]));
        TIDE_CHECK(colored(c->color, colors[i][0], colors[i][1], colors[i][2], 1));
    }
    static const int32_t wound[9] = {0, 1, 2, 0, 3, 1, 4, 5, 6};
    TIDE_CHECK(faces(seen->boxFaces, wound, 9));
    finish();
}

TIDE_TEST(mesh_assets_float4_tangents_do_not_change_same_source_position_layouts)
{
    start();
    const tide_list plain[2] = {world.Seen.seamPlainFirst, world.Seen.seamPlainSecond};
    const tide_list plain_faces[2] = {world.Seen.seamPlainFirstFaces, world.Seen.seamPlainSecondFaces};
    const tide_list mapped[2] = {world.Seen.seamMappedFirst, world.Seen.seamMappedSecond};
    const tide_list mapped_faces[2] = {world.Seen.seamMappedFirstFaces, world.Seen.seamMappedSecondFaces};
    static const float places[4][3] = {{0, 0, 0}, {-1, 1, 0}, {-1, 0, 0}, {0, 1, 0}};
    static const int32_t original[6] = {0, 1, 2, 0, 3, 1};
    for (uint32_t order = 0; order < 2u; order++) {
        TIDE_REQUIRE(tide_list_count(plain[order]) == 4 && tide_list_count(mapped[order]) == 6);
        TIDE_CHECK(faces(plain_faces[order], original, 6));
        for (int32_t i = 0; i < 4; i++) {
            const Point *p = tide_list_at(plain[order], i, sizeof(Point));
            TIDE_REQUIRE(p);
            TIDE_CHECK(memcmp(&p->position, places[i], sizeof places[i]) == 0);
        }
        for (int32_t i = 0; i < 6; i++) {
            const int32_t *index = tide_list_at(mapped_faces[order], i, sizeof(int32_t));
            TIDE_REQUIRE(index);
            const Corner *c = corner(mapped[order], *index);
            TIDE_REQUIRE(c);
            TIDE_CHECK(memcmp(&c->position, places[original[i]], sizeof places[0]) == 0);
            TIDE_CHECK(at(c->normal, 0, 0, 1) && near(c->tangent.x, i < 3 ? -1 : 1));
            TIDE_CHECK(c->tangent.y == 0 && c->tangent.z == 0 && c->tangent.w == (i < 3 ? 1 : -1));
            TIDE_CHECK(c->glow == 0.5f && c->kind == 7 && c->spare == 0.0f);
        }
    }
    finish();
}

// A glob's meshes as a list, in order of their paths, and as a map by them;
// one file where the type holds many; a Mesh in a local, passed to a function
// and given back by one.
TIDE_TEST(mesh_assets_are_lists_and_maps_of_a_globs_files)
{
    start();
    const Seen *seen = &world.Seen;
    TIDE_CHECK(seen->all == 2 && seen->allCorners == 10);
    TIDE_CHECK(seen->named == 4 && seen->namedCorners == 18);
    TIDE_CHECK(seen->defaultCorners == 32);
    const tide_str keys = tide_text_view(seen->keys);
    const char *expected = "assets/meshes/inline.gltf;assets/meshes/pair.glb;assets/meshes/quad.glb;assets/meshes/strip.gltf;";
    TIDE_CHECK(keys.bytes == (int32_t)strlen(expected) && memcmp(keys.ptr, expected, strlen(expected)) == 0);
    TIDE_CHECK(seen->picked == 64); // Pick(1) is all[0], the pair; all[1] is the quad
    TIDE_CHECK(seen->missing == 0 && seen->has);
    TIDE_CHECK(seen->one == 109);
    finish();
}

// The run a command that draws through a shader is of, or NULL.
static const tide_draw_run *run_of(const tide_draw_command *c)
{
    return c->kind == TIDE_DRAW_SHADED ? &draw.runs[c->shaded.run] : NULL;
}

// Drawn: a Mesh where the vertices go is its two lists. The draw list keeps
// one copy of each, told by what's in its file, so two names of one file and
// one vertex are one mesh, and a pack loaded again with the same file in it
// draws the mesh the list has.
TIDE_TEST(mesh_assets_draw_as_their_two_lists)
{
    start();
    frame();
    frame();
    TIDE_CHECK(local.Art.turn == 2); // `all[art.turn++ % 2]`: once a frame, though the Mesh is written out twice
    const tide_draw_command *shaded[16];
    uint32_t targets[16];
    int runs = 0, plain = 0, plain_instances = 0;
    uint32_t target = 0;
    for (uint32_t i = 0; i < draw.count; i++) {
        const tide_draw_command *c = &draw.commands[i];
        if (c->kind == TIDE_DRAW_TARGET) target = c->target;
        if (c->kind == TIDE_DRAW_SHADED && runs < 16) targets[runs] = target, shaded[runs++] = c;
        if (c->kind != TIDE_DRAW_MESH_3D) continue;
        const tide_draw_mesh_data *m = &draw.meshes[c->mesh3.mesh - 1u];
        TIDE_CHECK(c->mesh3.mesh && m->space == TIDE_MESH_LISTS && m->vertex_count == 4 && m->index_count == 6);
        plain++;
        plain_instances += (int)c->mesh3.count;
    }
    TIDE_CHECK(plain == 2 && plain_instances == 3); // Draw.Mesh(plain, ...), then two instances of it
    // quad three times (by its name, by another name of the same file, by its lists) as one run of one mesh; the
    // pair; the box with no frag; two instances of the quad; all[1], the quad; the strip; Pick(0), the quad; the
    // one with its buffer inside it, into the target; the box through the frag. The key the map hasn't got draws nothing.
    TIDE_REQUIRE(runs == 9);
    static const uint32_t vertices[9] = {4, 6, 7, 4, 4, 4, 4, 4, 7};
    static const uint32_t instances[9] = {3, 1, 1, 2, 1, 1, 1, 1, 1};
    for (int i = 0; i < 9; i++) {
        const tide_draw_run *r = run_of(shaded[i]);
        const tide_draw_mesh_data *m = &draw.meshes[r->mesh - 1u];
        TIDE_CHECK(m->space == TIDE_MESH_BYTES && m->vertex_size == sizeof(Corner) && m->vertex_count == vertices[i]);
        TIDE_CHECK(r->count == instances[i]);
        TIDE_CHECK((targets[i] != 0) == (i == 7));
    }
    // One mesh however it's named: the quad's every time
    const uint32_t quad = run_of(shaded[0])->mesh;
    TIDE_CHECK(run_of(shaded[3])->mesh == quad && run_of(shaded[4])->mesh == quad && run_of(shaded[6])->mesh == quad);
    TIDE_CHECK(run_of(shaded[2])->mesh == run_of(shaded[8])->mesh); // The box, with a frag and without
    // What the list copied is the pack's vertices, as the match read them
    const tide_draw_mesh_data *copied = &draw.meshes[quad - 1u];
    TIDE_CHECK(memcmp(copied->bytes, corner(world.Seen.quad, 0), 4u * sizeof(Corner)) == 0);
    TIDE_CHECK(copied->indices[0] == 0 && copied->indices[1] == 2 && copied->indices[2] == 1);
    // The pack again: the same meshes to the list, which has them already
    const uint32_t meshes = draw.mesh_count;
    const uint64_t version = copied->version;
    TIDE_REQUIRE(tide_assets_load(tide_game_pack, tide_game_pack_size));
    frame();
    frame();
    TIDE_CHECK(draw.mesh_count == meshes && draw.meshes[quad - 1u].version == version && draw.meshes[quad - 1u].frame == draw.frame);
    finish();
}

// A pack another build made: a mesh it hasn't got, or has laid out as another
// vertex, reads as an empty one, which draws nothing.
TIDE_TEST(mesh_assets_missing_from_the_pack_read_as_empty)
{
    tide_pack_builder b = {0};
    // The quad's name, with vertices of another layout: 12 bytes each, where the game's Corner is 76
    static const float old_vertices[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    static const uint32_t old_indices[3] = {0, 1, 2};
    uint8_t content[32] = {7};
    uint32_t blocks[2];
    tide_pack_mesh(&b, content, old_vertices, 12u, 3u, old_indices, 3u, blocks);
    tide_pack_name(&b, "quad", TIDE_PACK_MESH, blocks[0], blocks[1], 0x12345678u);
    size_t size;
    uint8_t *other = tide_pack_end(&b, &size);
    TIDE_REQUIRE(tide_assets_load(other, size));
    free(other);
    tide_world_init(&world, 1.0f);
    tide_world_tick(&world);
    tide_local_init(&local);
    tide_text_use(&world.heap, &local.heap);
    const Seen *seen = &world.Seen;
    TIDE_CHECK(tide_list_count(seen->quad) == 0 && tide_list_count(seen->quadFaces) == 0 && tide_list_count(seen->box) == 0);
    TIDE_CHECK(seen->all == 0 && seen->named == 0 && seen->one == 0 && seen->picked == 0 && !seen->has);
    TIDE_CHECK(seen->defaultCorners == 0);
    TIDE_CHECK(seen->mine == 10); // The copy is the one vertex the code added
    frame();
    for (uint32_t i = 0; i < draw.count; i++) {
        TIDE_CHECK(draw.commands[i].kind != TIDE_DRAW_SHADED && draw.commands[i].kind != TIDE_DRAW_MESH_3D);
    }
    finish();
}
