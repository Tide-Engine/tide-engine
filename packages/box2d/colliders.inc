// Included by bridge.c. Fixture records are match values; no native pointers
// or heap addresses participate in configuration keys or world stamps.
typedef struct fixture {
    int64_t id, body, signature;
    int32_t kind;
    uint8_t padding[4];
} fixture;
typedef struct collision_filter { int64_t category, mask; int32_t group; uint8_t padding[4]; } collision_filter;
typedef struct capsule { tide_float2 a, b; float radius, density, friction, bounce; bool sensor; uint8_t padding[3]; } capsule;
typedef struct polygon_shape { tide_float2 points[8]; int32_t count; float radius, density, friction, bounce; bool sensor; uint8_t padding[3]; } polygon_shape;
typedef struct segment_shape { tide_float2 a, b; float friction, bounce; bool sensor; uint8_t padding[3]; } segment_shape;
typedef struct chain_shape { tide_list points; float friction, bounce; bool loop; uint8_t padding[7]; } chain_shape;
_Static_assert(sizeof(fixture) == 32 && sizeof(collision_filter) == 24 && sizeof(capsule) == 36 &&
    sizeof(polygon_shape) == 88 && sizeof(segment_shape) == 28 && sizeof(chain_shape) == 24, "fixture ABI changed");

static b2ChainId chain_of(int64_t id)
{
    return (b2ChainId){(int32_t)(uint32_t)id, (uint16_t)(active->id.index1 - 1), (uint16_t)((uint64_t)id >> 32)};
}

void b2t_fixture_shapes(const fixture *f, tide_list_out *out)
{
    tide_list_out_count(out, 0);
    if (!active || !f->id) return;
    if (f->kind != 5) {
        if (!b2Shape_IsValid(shape_of(active, f->id))) return;
        *(int64_t *)tide_list_out_room(out, 1) = f->id;
        tide_list_out_count(out, 1);
        return;
    }
    const b2ChainId id = chain_of(f->id);
    if (!b2Chain_IsValid(id)) return;
    const int count = b2Chain_GetSegmentCount(id);
    if (count <= 0 || (size_t)count > SIZE_MAX / sizeof(b2ShapeId)) return;
    b2ShapeId *segments = tide_alloc((size_t)count * sizeof *segments);
    const int got = b2Chain_GetSegments(id, segments, count);
    int64_t *dest = tide_list_out_room(out, (uint32_t)got);
    for (int i = 0; i < got; i++) dest[i] = shape_handle(segments[i]);
    tide_list_out_count(out, (uint32_t)got);
    free(segments);
}

void b2t_drop_fixture(const fixture *f)
{
    if (!active || !f->id) return;
    if (f->kind != 5) { b2t_shed(&f->id, 1); return; }
    const b2ChainId id = chain_of(f->id);
    if (!b2Chain_IsValid(id)) return;
    // Remember segment ownership for the end-contact events of this step.
    const int count = b2Chain_GetSegmentCount(id);
    b2ShapeId *segments = tide_alloc((size_t)count * sizeof *segments);
    const int got = b2Chain_GetSegments(id, segments, count);
    for (int i = 0; i < got; i++) {
        if (shed_count == shed_room) {
            const int32_t room = shed_room ? shed_room * 2 : 16;
            sheds = tide_realloc(sheds, (size_t)shed_room * sizeof *sheds, (size_t)room * sizeof *sheds);
            shed_room = room;
        }
        sheds[shed_count++] = (shed_shape){shape_handle(segments[i]), f->body};
    }
    free(segments);
    b2DestroyChain(id);
    note('H', &f->id, sizeof f->id);
}

static bool geometry_float(float x) { return real(x) && x >= -1e6f && x <= 1e6f; }
static bool geometry_point(tide_float2 p) { return geometry_float(p.x) && geometry_float(p.y); }
static bool material_valid(float density, float friction, float bounce)
{
    return geometry_float(density) && density >= 0 && geometry_float(friction) && friction >= 0 &&
        geometry_float(bounce) && bounce >= 0;
}
static b2ShapeDef fixture_def(float density, float friction, float bounce, bool sensor,
                             const collision_filter *filter, bool filtered)
{
    b2ShapeDef def = shape_def(density, friction, bounce, sensor);
    if (sensor && filtered) def.enableCustomFiltering = true;
    if (filtered) def.filter = (b2Filter){(uint64_t)filter->category, (uint64_t)filter->mask, filter->group};
    return def;
}

static uint64_t fixture_key(int kind, int64_t body_id, const b2ShapeDef *def, const void *geometry, size_t size)
{
    uint64_t h = tide_hash_more(UINT64_C(14695981039346656037), &kind, sizeof kind);
    h = tide_hash_more(h, &body_id, sizeof body_id);
    const float material[] = {def->density, def->material.friction, def->material.restitution};
    h = tide_hash_more(h, material, sizeof material);
    h = tide_hash_more(h, &def->isSensor, sizeof def->isSensor);
    h = tide_hash_more(h, &def->filter.categoryBits, sizeof def->filter.categoryBits);
    h = tide_hash_more(h, &def->filter.maskBits, sizeof def->filter.maskBits);
    h = tide_hash_more(h, &def->filter.groupIndex, sizeof def->filter.groupIndex);
    return tide_hash_more(h, geometry, size);
}

static bool fixture_kept(const fixture *old, int64_t body_id, uint64_t key, int kind)
{
    return old->body == body_id && old->signature == (int64_t)key && old->kind == kind && old->id &&
        (kind == 5 ? b2Chain_IsValid(chain_of(old->id)) : b2Shape_IsValid(shape_of(active, old->id)));
}
static fixture fixture_made(int64_t id, int64_t body_id, uint64_t key, int kind)
{
    if (!id) return (fixture){0};
    fixture f = {.id = id, .body = body_id, .signature = (int64_t)key, .kind = kind};
    note('F', &id, sizeof id); note('F', &body_id, sizeof body_id); note('F', &key, sizeof key);
    return f;
}
static fixture fixture_invalid(const fixture *old) { b2t_drop_fixture(old); return (fixture){0}; }

fixture b2t_sync_box(const fixture *old, int64_t on, const box *s, const collision_filter *filter, bool filtered)
{
    const b2BodyId body_id = body_of(active, on);
    if (!b2Body_IsValid(body_id) || !geometry_point(s->size) || !geometry_point(s->offset) ||
        s->size.x <= 0 || s->size.y <= 0 || !material_valid(s->density, s->friction, s->bounce)) return fixture_invalid(old);
    const b2ShapeDef def = fixture_def(s->density, s->friction, s->bounce, s->sensor, filter, filtered);
    const float geometry[] = {s->size.x, s->size.y, s->offset.x, s->offset.y};
    const uint64_t key = fixture_key(0, on, &def, geometry, sizeof geometry);
    if (fixture_kept(old, on, key, 0)) return *old;
    b2t_drop_fixture(old);
    const b2Polygon shape = box_shape(s);
    return fixture_made(shape_handle(b2CreatePolygonShape(body_id, &def, &shape)), on, key, 0);
}

fixture b2t_sync_circle(const fixture *old, int64_t on, const circle *s, const collision_filter *filter, bool filtered)
{
    const b2BodyId body_id = body_of(active, on);
    if (!b2Body_IsValid(body_id) || !geometry_point(s->offset) || !geometry_float(s->radius) ||
        s->radius <= 0 || !material_valid(s->density, s->friction, s->bounce)) return fixture_invalid(old);
    const b2ShapeDef def = fixture_def(s->density, s->friction, s->bounce, s->sensor, filter, filtered);
    const float geometry[] = {s->radius, s->offset.x, s->offset.y};
    const uint64_t key = fixture_key(1, on, &def, geometry, sizeof geometry);
    if (fixture_kept(old, on, key, 1)) return *old;
    b2t_drop_fixture(old);
    const b2Circle shape = {{s->offset.x, s->offset.y}, s->radius};
    return fixture_made(shape_handle(b2CreateCircleShape(body_id, &def, &shape)), on, key, 1);
}

static bool capsule_proxy(const capsule *s, float angle, b2ShapeProxy *proxy)
{
    if (!geometry_point(s->a) || !geometry_point(s->b) || !geometry_float(s->radius) || s->radius <= 0 || !real(angle)) return false;
    const b2Vec2 points[] = {scene_vec(s->a), scene_vec(s->b)};
    *proxy = b2MakeOffsetProxy(points, 2, s->radius, b2Vec2_zero, b2MakeRot(angle));
    return true;
}
fixture b2t_sync_capsule(const fixture *old, int64_t on, const capsule *s, const collision_filter *filter, bool filtered)
{
    b2ShapeProxy proxy;
    const b2BodyId body_id = body_of(active, on);
    if (!b2Body_IsValid(body_id) || !capsule_proxy(s, 0, &proxy) || !material_valid(s->density, s->friction, s->bounce)) return fixture_invalid(old);
    const b2ShapeDef def = fixture_def(s->density, s->friction, s->bounce, s->sensor, filter, filtered);
    const float geometry[] = {s->a.x, s->a.y, s->b.x, s->b.y, s->radius};
    const uint64_t key = fixture_key(2, on, &def, geometry, sizeof geometry);
    if (fixture_kept(old, on, key, 2)) return *old;
    b2t_drop_fixture(old);
    const b2Capsule shape = {scene_vec(s->a), scene_vec(s->b), s->radius};
    return fixture_made(shape_handle(b2CreateCapsuleShape(body_id, &def, &shape)), on, key, 2);
}

static bool polygon_geometry(const polygon_shape *s, b2Polygon *shape)
{
    const int count = s->count;
    if (count < 3 || count > 8 || !geometry_float(s->radius) || s->radius < 0) return false;
    b2Vec2 points[8];
    for (int i = 0; i < count; i++) {
        const tide_float2 *p = &s->points[i];
        if (!p || !geometry_point(*p)) return false;
        points[i] = scene_vec(*p);
    }
    const b2Hull hull = b2ComputeHull(points, count);
    if (hull.count < 3 || !b2ValidateHull(&hull)) return false;
    *shape = b2MakePolygon(&hull, s->radius);
    return true;
}
static bool polygon_proxy(const polygon_shape *s, float angle, b2ShapeProxy *proxy)
{
    b2Polygon shape;
    if (!real(angle) || !polygon_geometry(s, &shape)) return false;
    *proxy = b2MakeOffsetProxy(shape.vertices, shape.count, shape.radius, b2Vec2_zero, b2MakeRot(angle));
    return true;
}
fixture b2t_sync_polygon(const fixture *old, int64_t on, const polygon_shape *s, const collision_filter *filter, bool filtered)
{
    b2Polygon shape;
    const b2BodyId body_id = body_of(active, on);
    if (!b2Body_IsValid(body_id) || !polygon_geometry(s, &shape) || !material_valid(s->density, s->friction, s->bounce)) return fixture_invalid(old);
    const b2ShapeDef def = fixture_def(s->density, s->friction, s->bounce, s->sensor, filter, filtered);
    uint64_t key = fixture_key(3, on, &def, shape.vertices, (size_t)shape.count * sizeof *shape.vertices);
    key = tide_hash_more(key, &shape.radius, sizeof shape.radius);
    if (fixture_kept(old, on, key, 3)) return *old;
    b2t_drop_fixture(old);
    return fixture_made(shape_handle(b2CreatePolygonShape(body_id, &def, &shape)), on, key, 3);
}

static bool segment_proxy(const segment_shape *s, float angle, b2ShapeProxy *proxy)
{
    if (!geometry_point(s->a) || !geometry_point(s->b) || !real(angle)) return false;
    const b2Vec2 points[] = {scene_vec(s->a), scene_vec(s->b)};
    if (b2DistanceSquared(points[0], points[1]) <= 0.005f * 0.005f) return false;
    *proxy = b2MakeOffsetProxy(points, 2, 0, b2Vec2_zero, b2MakeRot(angle));
    return true;
}
fixture b2t_sync_segment(const fixture *old, int64_t on, const segment_shape *s, const collision_filter *filter, bool filtered)
{
    b2ShapeProxy proxy;
    const b2BodyId body_id = body_of(active, on);
    if (!b2Body_IsValid(body_id) || !segment_proxy(s, 0, &proxy) || !material_valid(0, s->friction, s->bounce)) return fixture_invalid(old);
    const b2ShapeDef def = fixture_def(0, s->friction, s->bounce, s->sensor, filter, filtered);
    const float geometry[] = {s->a.x, s->a.y, s->b.x, s->b.y};
    const uint64_t key = fixture_key(4, on, &def, geometry, sizeof geometry);
    if (fixture_kept(old, on, key, 4)) return *old;
    b2t_drop_fixture(old);
    const b2Segment shape = {scene_vec(s->a), scene_vec(s->b)};
    return fixture_made(shape_handle(b2CreateSegmentShape(body_id, &def, &shape)), on, key, 4);
}

fixture b2t_sync_chain(const fixture *old, int64_t on, const tide_float2 *source, int32_t count, float friction, float bounce, bool loop, const collision_filter *filter, bool filtered)
{
    const b2BodyId body_id = body_of(active, on);

    if (!b2Body_IsValid(body_id) || b2Body_GetType(body_id) == b2_dynamicBody || count < (loop ? 3 : 2) ||
        count > INT32_MAX / (int)sizeof(b2ChainSegment) || !material_valid(0, friction, bounce)) return fixture_invalid(old);
    const b2ShapeDef material_def = fixture_def(0, friction, bounce, false, filter, filtered);
    uint64_t key = fixture_key(5, on, &material_def, &loop, sizeof loop);
    b2Vec2 previous = {0}, first = {0};
    for (int i = 0; i < count; i++) {
        const tide_float2 *p = &source[i];
        if (!p || !geometry_point(*p)) return fixture_invalid(old);
        const b2Vec2 point = scene_vec(*p);
        if (i && b2DistanceSquared(previous, point) <= 0.005f * 0.005f) return fixture_invalid(old);
        if (!i) first = point;
        previous = point;
        key = tide_hash_more(key, &point, sizeof point);
    }
    if (loop && b2DistanceSquared(previous, first) <= 0.005f * 0.005f) return fixture_invalid(old);
    if (fixture_kept(old, on, key, 5)) return *old;
    b2t_drop_fixture(old);
    b2Vec2 *points = tide_alloc((size_t)count * sizeof *points);
    for (int i = 0; i < count; i++) points[i] = scene_vec(source[i]);
    b2ChainDef def = b2DefaultChainDef();
    def.points = points; def.pointCount = count; def.isLoop = loop;
    if (!loop) {
        def.ghost1 = b2Sub(b2MulSV(2, points[0]), points[1]);
        def.ghost2 = b2Sub(b2MulSV(2, points[count - 1]), points[count - 2]);
    }
    def.materials = &material_def.material; def.materialCount = 1;
    def.filter = material_def.filter; def.enableSensorEvents = true;
    const b2ChainId id = b2CreateChain(body_id, &def);
    free(points);
    if (id.index1) {
        const int segments_count = b2Chain_GetSegmentCount(id);
        b2ShapeId *segments = tide_alloc((size_t)segments_count * sizeof *segments);
        const int got = b2Chain_GetSegments(id, segments, segments_count);
        for (int i = 0; i < got; i++) {
            b2Shape_EnableContactEvents(segments[i], true);
            b2Shape_EnableHitEvents(segments[i], true);
        }
        free(segments);
    }
    return fixture_made(handle(id.index1, id.generation), on, key, 5);
}

met b2t_cast_capsule(const capsule *shape, tide_float2 from, float angle, tide_float2 to, int64_t ignore)
{
    b2ShapeProxy proxy;
    if (!asked || !real2(from) || !real2(to) || !capsule_proxy(shape, angle, &proxy)) return (met){0};
    return cast(&proxy, from, to, ignore);
}
void b2t_cast_capsule_all(tide_list_out *out, const capsule *shape, tide_float2 from, float angle, tide_float2 to, int64_t ignore, bool sensors)
{
    tide_list_out_count(out, 0);
    b2ShapeProxy proxy;
    if (!capsule_proxy(shape, angle, &proxy)) return;
    cast_all(out, &proxy, from, to, ignore, sensors);
}
int32_t b2t_overlap_capsule(const capsule *shape, tide_float2 at, float angle, tide_list_out *out)
{
    b2ShapeProxy proxy;
    if (!asked || !real2(at) || !capsule_proxy(shape, angle, &proxy)) return 0;
    return overlap(&proxy, at, out);
}
met b2t_scene_cast_capsule(const collider *c, const capsule *shape, tide_float2 from, float angle, tide_float2 to)
{
    b2ShapeProxy proxy;
    if (!capsule_proxy(shape, angle, &proxy)) return (met){0};
    return scene_cast(c, proxy, from, to);
}
bool b2t_scene_overlap_capsule(const collider *c, const capsule *shape, tide_float2 at, float angle)
{
    b2ShapeProxy proxy;
    if (!capsule_proxy(shape, angle, &proxy)) return false;
    return scene_overlap(c, proxy, at);
}
scene_bounds b2t_capsule_bounds(const capsule *shape, float rotation, tide_float2 from, tide_float2 to)
{
    b2ShapeProxy proxy;
    if (!capsule_proxy(shape, rotation, &proxy)) return (scene_bounds){0};
    return proxy_bounds(proxy.points, proxy.count, proxy.radius, from, to);
}

met b2t_cast_polygon(const polygon_shape *shape, tide_float2 from, float angle, tide_float2 to, int64_t ignore)
{
    b2ShapeProxy proxy;
    if (!asked || !real2(from) || !real2(to) || !polygon_proxy(shape, angle, &proxy)) return (met){0};
    return cast(&proxy, from, to, ignore);
}
void b2t_cast_polygon_all(tide_list_out *out, const polygon_shape *shape, tide_float2 from, float angle, tide_float2 to, int64_t ignore, bool sensors)
{
    tide_list_out_count(out, 0);
    b2ShapeProxy proxy;
    if (!polygon_proxy(shape, angle, &proxy)) return;
    cast_all(out, &proxy, from, to, ignore, sensors);
}
int32_t b2t_overlap_polygon(const polygon_shape *shape, tide_float2 at, float angle, tide_list_out *out)
{
    b2ShapeProxy proxy;
    if (!asked || !real2(at) || !polygon_proxy(shape, angle, &proxy)) return 0;
    return overlap(&proxy, at, out);
}
met b2t_scene_cast_polygon(const collider *c, const polygon_shape *shape, tide_float2 from, float angle, tide_float2 to)
{
    b2ShapeProxy proxy;
    if (!polygon_proxy(shape, angle, &proxy)) return (met){0};
    return scene_cast(c, proxy, from, to);
}
bool b2t_scene_overlap_polygon(const collider *c, const polygon_shape *shape, tide_float2 at, float angle)
{
    b2ShapeProxy proxy;
    if (!polygon_proxy(shape, angle, &proxy)) return false;
    return scene_overlap(c, proxy, at);
}
scene_bounds b2t_polygon_bounds(const polygon_shape *shape, float rotation, tide_float2 from, tide_float2 to)
{
    b2ShapeProxy proxy;
    if (!polygon_proxy(shape, rotation, &proxy)) return (scene_bounds){0};
    return proxy_bounds(proxy.points, proxy.count, proxy.radius, from, to);
}

met b2t_cast_segment(const segment_shape *shape, tide_float2 from, float angle, tide_float2 to, int64_t ignore)
{
    b2ShapeProxy proxy;
    if (!asked || !real2(from) || !real2(to) || !segment_proxy(shape, angle, &proxy)) return (met){0};
    return cast(&proxy, from, to, ignore);
}
void b2t_cast_segment_all(tide_list_out *out, const segment_shape *shape, tide_float2 from, float angle, tide_float2 to, int64_t ignore, bool sensors)
{
    tide_list_out_count(out, 0);
    b2ShapeProxy proxy;
    if (!segment_proxy(shape, angle, &proxy)) return;
    cast_all(out, &proxy, from, to, ignore, sensors);
}
int32_t b2t_overlap_segment(const segment_shape *shape, tide_float2 at, float angle, tide_list_out *out)
{
    b2ShapeProxy proxy;
    if (!asked || !real2(at) || !segment_proxy(shape, angle, &proxy)) return 0;
    return overlap(&proxy, at, out);
}
met b2t_scene_cast_segment(const collider *c, const segment_shape *shape, tide_float2 from, float angle, tide_float2 to)
{
    b2ShapeProxy proxy;
    if (!segment_proxy(shape, angle, &proxy)) return (met){0};
    return scene_cast(c, proxy, from, to);
}
bool b2t_scene_overlap_segment(const collider *c, const segment_shape *shape, tide_float2 at, float angle)
{
    b2ShapeProxy proxy;
    if (!segment_proxy(shape, angle, &proxy)) return false;
    return scene_overlap(c, proxy, at);
}
scene_bounds b2t_segment_bounds(const segment_shape *shape, float rotation, tide_float2 from, tide_float2 to)
{
    b2ShapeProxy proxy;
    if (!segment_proxy(shape, rotation, &proxy)) return (scene_bounds){0};
    return proxy_bounds(proxy.points, proxy.count, proxy.radius, from, to);
}

static bool source_span(tide_list stored, uint32_t offset, const void *bytes, uint32_t count)
{
    tide_list_cache cache = {0};
    const uint8_t *p = bytes;
    for (uint32_t done = 0; done < count;) {
        const uint32_t at = offset + done;
        const void *to = tide_list_cached_at(&cache, stored, (int32_t)at, 1);
        if (!to) return false;
        uint32_t n = cache.first + cache.count - at;
        if (n > count - done) n = count - done;
        if (memcmp(to, p + done, n)) return false;
        done += n;
    }
    return true;
}
// Exact metadata comparison is a per-world proof that no entity changed its
// component membership. No process identity, probabilistic hash or pointer is
// stored in simulation state. A changed table is copied only on structural edits.
void b2t_topology(tide_rows *rows, physics *p)
{
    // In a sharded world, an entity that came or went is looked over as one that changed
    const int64_t moves=tide_shard_moves_now();
    const bool moved=moves!=p->shard_moves;
    p->shard_moves=moves;
    const tide_entities *entities=rows->entities;
    if(!entities || moved) { p->topology_changed=true; if(!entities) return; }
    const uint64_t total=(uint64_t)entities->next_unused*sizeof(tide_entity_slot);
    if(total>(1u<<30)) tide_out_of_memory();
    bool same=total==(uint64_t)tide_list_count(p->topology);
    const uint32_t page_slots=1u<<TIDE_ENTITY_PAGE_SHIFT;
    static const tide_entity_slot absent[1u<<TIDE_ENTITY_PAGE_SHIFT]={0};
    for(uint32_t i=0;same && i<entities->next_unused;) {
        const uint32_t page=i>>TIDE_ENTITY_PAGE_SHIFT;
        const tide_entity_slot *slots=entities->page[page] ? tide_page_data(entities->page[page]) : absent;
        uint32_t n=entities->next_unused-i; if(n>page_slots) n=page_slots;
        same=source_span(p->topology,i*sizeof(tide_entity_slot),slots,n*sizeof(tide_entity_slot));
        i+=n;
    }
    p->topology_changed=!same || moved;
    if(same) return;
    uint8_t *bytes=tide_alloc((size_t)total);
    for(uint32_t i=0;i<entities->next_unused;) {
        const uint32_t page=i>>TIDE_ENTITY_PAGE_SHIFT;
        const tide_entity_slot *slots=entities->page[page] ? tide_page_data(entities->page[page]) : absent;
        uint32_t n=entities->next_unused-i; if(n>page_slots) n=page_slots;
        memcpy(bytes+(size_t)i*sizeof(tide_entity_slot),slots,(size_t)n*sizeof(tide_entity_slot));
        i+=n;
    }
    const uint32_t mark=tide_scratch_mark();
    const tide_list copy=tide_list_from(bytes,(int32_t)total,1);
    tide_list_set(&p->topology,copy,1,TIDE_IN_MATCH);
    tide_scratch_reset(mark);
    free(bytes);
}

void b2t_check_bodies(tide_rows *rows, physics *p)
{
    if (p->topology_changed) return;
    const int32_t *lo=tide_rows_ints(rows,0,offsetof(body,id));
    const int32_t *hi=tide_rows_ints(rows,0,offsetof(body,id)+4);
    for(uint32_t i=0;i<tide_rows_count(rows);i++) {
        if (!lo[i] && !hi[i]) { p->topology_changed=true; return; }
    }
}

typedef struct fixture_record { fixture state; tide_list shapes, source; } fixture_record;
typedef struct fixture_entry { int64_t order; tide_entity key; tide_list shapes; } fixture_entry;
typedef struct fixture_change { tide_entity entity; fixture state; int32_t kind; bool sensor; uint8_t padding[3]; } fixture_change;
typedef struct pending_fixture { fixture_change change; uint8_t source[64]; uint32_t size; } pending_fixture;
_Static_assert(sizeof(fixture_record) == 48 && sizeof(fixture_entry) == 24 && sizeof(fixture_change) == 48, "fixture record ABI changed");
static uint32_t fixture_entity_hash(const void *key) { return tide_map_hash_bytes(key, sizeof(tide_entity)); }
static bool fixture_entity_equal(const void *a, const void *b) { return memcmp(a, b, sizeof(tide_entity)) == 0; }
static const tide_map_kind fixture_map_kind = {sizeof(fixture_entry), offsetof(fixture_entry, key), fixture_entity_hash, fixture_entity_hash, fixture_entity_equal};
static pending_fixture *fixture_changes;
static uint32_t fixture_changes_count, fixture_changes_room;

void b2t_begin_fixture_changes(bool topology_changed)
{
    fixture_changes_count = 0;
    if (topology_changed) {
        active->shape_epoch += 1;
        if (!active->shape_epoch) {
            if (active->shape_sources) memset(active->shape_sources, 0, (size_t)active->shape_sources_room * sizeof *active->shape_sources);
            active->shape_epoch = 1;
        }
    }
}
int32_t b2t_count_fixture_changes(void) { return (int32_t)fixture_changes_count; }
fixture_change b2t_read_fixture_change(int32_t i)
{
    return i >= 0 && (uint32_t)i < fixture_changes_count ? fixture_changes[i].change : (fixture_change){0};
}
void b2t_read_fixture_source(int32_t i, tide_list_out *out)
{
    tide_list_out_count(out, 0);
    if (i < 0 || (uint32_t)i >= fixture_changes_count) return;
    const pending_fixture *p = &fixture_changes[i];
    memcpy(tide_list_out_room(out, p->size), p->source, p->size);
    tide_list_out_count(out, p->size);
}

// Common boxes and circles compare their source bytes in place. Only edits
// cross back through Tide's ownership-aware list/map writers.
static void attach_shape_rows(tide_rows *rows, physics *p, int kind, bool filtered)
{
    const uint32_t count = tide_rows_count(rows);
    const tide_entity *entities = tide_rows_entities(rows);
    const int32_t *lo = tide_rows_ints(rows, 0, offsetof(body, id));
    const int32_t *hi = tide_rows_ints(rows, 0, offsetof(body, id) + 4);
    const uint32_t shape_size = kind == 0 ? sizeof(box) : sizeof(circle);
    const uint32_t lanes_count = shape_size / 4;
    const int32_t *lanes[8];
    const int32_t *filter_lanes[6] = {0};
    for (uint32_t j = 0; j < lanes_count; j++) lanes[j] = tide_rows_ints(rows, 1, j * 4);
    if (filtered) for (uint32_t j = 0; j < 6; j++) filter_lanes[j] = tide_rows_ints(rows, 2, j * 4);
    for (uint32_t i = 0; i < count; i++) {
        const int64_t on = (int64_t)((uint64_t)(uint32_t)lo[i] | (uint64_t)(uint32_t)hi[i] << 32);
        const uint32_t at = (uint32_t)lo[i] * 2u + (uint32_t)kind;
        shape_source *cached = at < active->shape_sources_room ? &active->shape_sources[at] : NULL;
        if (cached && cached->body == on && cached->epoch == active->shape_epoch && cached->words[14] == (uint32_t)filtered) {
            bool same = true;
            for (uint32_t j = 0; same && j < lanes_count; j++) same = cached->words[j] == (uint32_t)lanes[j][i];
            if (filtered) for (uint32_t j = 0; same && j < 6; j++) same = cached->words[8 + j] == (uint32_t)filter_lanes[j][i];
            if (same) continue;
        }
        if (!b2Body_IsValid(body_of(active, on))) continue;
        if (at >= active->shape_sources_room) {
            uint32_t room = active->shape_sources_room ? active->shape_sources_room : 128;
            while (room <= at) {
                if (room > (1u << 30) / (2 * sizeof(shape_source))) tide_out_of_memory();
                room *= 2;
            }
            active->shape_sources = tide_realloc(active->shape_sources, (size_t)active->shape_sources_room * sizeof *active->shape_sources,
                                                (size_t)room * sizeof *active->shape_sources);
            memset(active->shape_sources + active->shape_sources_room, 0, (size_t)(room - active->shape_sources_room) * sizeof *active->shape_sources);
            active->shape_sources_room = room;
        }
        cached = &active->shape_sources[at];
        cached->body = on; cached->epoch = active->shape_epoch; cached->words[14] = filtered;
        for (uint32_t j = 0; j < lanes_count; j++) cached->words[j] = (uint32_t)lanes[j][i];
        if (filtered) for (uint32_t j = 0; j < 6; j++) cached->words[8 + j] = (uint32_t)filter_lanes[j][i];
        box shape = {0};
        collision_filter filter = {0};
        tide_rows_get(rows, 1, i, &shape);
        if (filtered) tide_rows_get(rows, 2, i, &filter);
        uint8_t source[64] = {0};
        memcpy(source, &shape, shape_size);
        memcpy(source + shape_size, &filter, sizeof filter);
        source[shape_size + sizeof filter] = filtered;
        const uint32_t size = shape_size + sizeof filter + 1;
        const int32_t entry_index = tide_map_find(p->fixtures, &fixture_map_kind, &entities[i]);
        const fixture_entry *entry = entry_index >= 0 ? tide_map_entry(p->fixtures, entry_index, &fixture_map_kind) : NULL;
        const fixture_record *record = entry ? tide_list_at(entry->shapes, kind, sizeof(fixture_record)) : NULL;
        const fixture old = record ? record->state : (fixture){0};
        if (record && old.body == on && old.id && tide_list_count(record->source) == (int32_t)size && source_span(record->source, 0, source, size)) continue;
        fixture current;
        bool sensor;
        if (kind == 0) { current = b2t_sync_box(&old, on, &shape, &filter, filtered); sensor = shape.sensor; }
        else {
            circle round; memcpy(&round, &shape, sizeof round);
            current = b2t_sync_circle(&old, on, &round, &filter, filtered); sensor = round.sensor;
        }
        if (current.id == old.id && current.signature == old.signature) continue;
        if (fixture_changes_count == fixture_changes_room) {
            uint32_t room = fixture_changes_room ? fixture_changes_room * 2 : 64;
            if (room > (1u << 30) / sizeof(pending_fixture)) tide_out_of_memory();
            fixture_changes = tide_realloc(fixture_changes, (size_t)fixture_changes_room * sizeof *fixture_changes,
                                          (size_t)room * sizeof *fixture_changes);
            fixture_changes_room = room;
        }
        pending_fixture *change = &fixture_changes[fixture_changes_count++];
        memset(change, 0, sizeof *change);
        change->change.entity = entities[i]; change->change.state = current; change->change.kind = kind;
        change->change.sensor = sensor;
        memcpy(change->source, source, size); change->size = size;
    }
}
void b2t_attach_box_rows(tide_rows *rows, physics *p) { attach_shape_rows(rows, p, 0, false); }
void b2t_attach_filtered_box_rows(tide_rows *rows, physics *p) { attach_shape_rows(rows, p, 0, true); }
void b2t_attach_circle_rows(tide_rows *rows, physics *p) { attach_shape_rows(rows, p, 1, false); }
void b2t_attach_filtered_circle_rows(tide_rows *rows, physics *p) { attach_shape_rows(rows, p, 1, true); }
