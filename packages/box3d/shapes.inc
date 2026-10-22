// Live attachments. Records and their source bytes belong to the match heap;
// no native pointers or process caches are needed to detect a geometry edit.
typedef struct shape_record {
    tide_entity owner;
    int64_t body, shape;
    tide_list source;
    int32_t kind;
} shape_record;
typedef struct shape_slots { int32_t slot[6]; } shape_slots;
typedef struct shape_entry { int64_t order; tide_entity key; shape_slots value; } shape_entry;
typedef struct shape_change {
    tide_entity owner;
    int64_t body, before, after;
    int32_t kind;
    bool sensor;
    uint8_t padding[3];
} shape_change;
_Static_assert(sizeof(shape_record) == 32 && sizeof(shape_entry) == 40 && sizeof(shape_change) == 40,
               "live collider ABI changed");
static physics *shape_physics; // Only between BeginShapes and the end of this tick
static shape_change *shape_changes;
static int32_t shape_change_count, shape_change_room;
static uint32_t shape_key_hash(const void *key) { return tide_map_hash_bytes(key, sizeof(tide_entity)); }
static bool shape_key_equal(const void *a, const void *b) { return memcmp(a, b, sizeof(tide_entity)) == 0; }
static const tide_map_kind shape_kind = {sizeof(shape_entry), offsetof(shape_entry, key), shape_key_hash, shape_key_hash, shape_key_equal};

static void changed_shape(const shape_record *r, int64_t before, bool sensor)
{
    shape_changes = grown(shape_changes, shape_change_count, &shape_change_room, sizeof *shape_changes);
    shape_change c = {0};
    c.owner = r->owner; c.body = r->body; c.before = before; c.after = r->shape; c.kind = r->kind & 7; c.sensor = sensor;
    shape_changes[shape_change_count++] = c;
}
void b3t_begin_shapes(tide_rows *rows, physics *p) { (void)rows; shape_physics = p; shape_change_count = 0; }
int32_t b3t_shape_changes(void) { return shape_change_count; }
shape_change b3t_shape_change(int32_t i) { return i >= 0 && i < shape_change_count ? shape_changes[i] : (shape_change){0}; }

static void delete_native_shape(int64_t h)
{
    const b3ShapeId id = shape_of(active, h);
    if (!b3Shape_IsValid(id)) return;
    const b3BodyId on = b3Shape_GetBody(id);
    parteds = grown(parteds, parted_count, &parted_room, sizeof *parteds);
    parteds[parted_count++] = (parted){h, handle(on.index1, on.generation)};
    b3DestroyShape(id, true);
    note('h', &h, sizeof h);
}

void b3t_drop_collider(int32_t index, tide_resident *state)
{
    (void)state;
    physics *p = shape_physics;
    if (!p || !active) return;
    const shape_record *read = tide_list_at(p->colliders, index, sizeof *read);
    if (!read || !read->body || (read->kind & 7) >= 6) return;
    shape_record r = *read;
    delete_native_shape(r.shape);
    const int64_t before = r.shape;
    r.shape = 0;
    changed_shape(&r, before, false);
    const int32_t entry_index = tide_map_find(p->collider_index, &shape_kind, &r.owner);
    if (entry_index >= 0) {
        shape_entry *entry = tide_list_at_mut(p->collider_index.entries, entry_index, sizeof *entry);
        if (entry) {
            entry->value.slot[r.kind & 7] = 0;
            bool empty = true;
            for (int i = 0; i < 6; i++) empty &= entry->value.slot[i] == 0;
            if (empty) tide_map_erase(&p->collider_index, &shape_kind, entry_index, TIDE_IN_MATCH);
        }
    }
    tide_list_release(&r.source, TIDE_IN_MATCH);
    memset(tide_list_at_mut(p->colliders, index, sizeof r), 0, sizeof r);
    int32_t *slot = tide_list_add(&p->free_colliders, sizeof *slot, TIDE_IN_MATCH);
    if (!slot) tide_out_of_memory();
    *slot = index;
}

static int32_t collider_slot(physics *p, tide_entity entity, int kind, int64_t on)
{
    int32_t found = tide_map_find(p->collider_index, &shape_kind, &entity);
    if (found >= 0) {
        const shape_entry *entry = tide_map_entry(p->collider_index, found, &shape_kind);
        if (entry && entry->value.slot[kind] > 0) {
            const int32_t index = entry->value.slot[kind]-1;
            const shape_record *r = tide_list_at(p->colliders,index,sizeof *r);
            if (r && (r->kind & 7) == kind && tide_entity_equal(r->owner,entity)) {
                if (r->body == on) return index;
                b3t_drop_collider(index,&p->state);
            }
        }
    }
    int32_t index;
    const int32_t free_count = tide_list_count(p->free_colliders);
    bool reused = false;
    if (free_count) {
        index = *(const int32_t *)tide_list_at(p->free_colliders, free_count - 1, sizeof index);
        tide_list_remove_at(&p->free_colliders, free_count - 1, sizeof index);
        const shape_record *r = tide_list_at(p->colliders,index,sizeof *r);
        reused = r && !r->body;
    }
    if (!reused) {
        index = tide_list_count(p->colliders);
        if (!tide_list_add(&p->colliders, sizeof(shape_record), TIDE_IN_MATCH)) tide_out_of_memory();
    }
    bool added;
    shape_entry *entry = tide_map_insert(&p->collider_index, &shape_kind, &entity, TIDE_IN_MATCH, &added);
    if (!entry) tide_out_of_memory();
    if (added) entry->key = entity;
    entry->value.slot[kind] = index + 1;
    shape_record r = {0}; r.owner = entity; r.body = on; r.kind = kind;
    memcpy(tide_list_at_mut(p->colliders, index, sizeof r), &r, sizeof r);
    return index;
}

typedef struct shape_source {
    const void *header;
    uint32_t header_size;
    tide_list lists[2];
    uint32_t sizes[2];
} shape_source;

// Compare runs, avoiding flattening or allocation on an unchanged mesh. Both
// endpoints can cross different chunk boundaries. Equality is exact bytes.
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
static bool source_equal(tide_list stored, const shape_source *s)
{
    uint64_t total = s->header_size;
    for (int j = 0; j < 2; j++) total += (uint64_t)tide_list_count(s->lists[j]) * s->sizes[j];
    if (total != (uint64_t)tide_list_count(stored)) return false;
    // Every source header fits in the first contiguous byte-list run.
    _Static_assert(TIDE_LIST_CHUNK >= 64,"source header must fit the first chunk");
    const void *header=tide_list_at(stored,0,1);
    if(!header || memcmp(header,s->header,s->header_size)) return false;
    uint32_t offset = s->header_size;
    for (int j = 0; j < 2; j++) {
        tide_list_cache cache = {0};
        const uint32_t count = (uint32_t)tide_list_count(s->lists[j]);
        for (uint32_t i = 0; i < count;) {
            const void *data = tide_list_cached_at(&cache, s->lists[j], (int32_t)i, s->sizes[j]);
            if (!data) return false;
            uint32_t n = cache.first + cache.count - i;
            if (n > count - i) n = count - i;
            const uint32_t bytes = n * s->sizes[j];
            if (!source_span(stored, offset, data, bytes)) return false;
            i += n; offset += bytes;
        }
    }
    return true;
}
static void source_save(shape_record *r, const shape_source *s)
{
    uint64_t total = s->header_size;
    for (int j = 0; j < 2; j++) total += (uint64_t)tide_list_count(s->lists[j]) * s->sizes[j];
    if (total > (1u << 26)) tide_out_of_memory();
    uint8_t *bytes = tide_alloc((size_t)total);
    memcpy(bytes, s->header, s->header_size);
    uint32_t offset = s->header_size;
    for (int j = 0; j < 2; j++) {
        tide_list_cache cache = {0};
        const uint32_t count = (uint32_t)tide_list_count(s->lists[j]);
        for (uint32_t i = 0; i < count;) {
            const void *data = tide_list_cached_at(&cache, s->lists[j], (int32_t)i, s->sizes[j]);
            if (!data) tide_out_of_memory();
            uint32_t n = cache.first + cache.count - i;
            if (n > count - i) n = count - i;
            const uint32_t size = n * s->sizes[j];
            memcpy(bytes + offset, data, size); offset += size; i += n;
        }
    }
    const uint32_t mark = tide_scratch_mark();
    const tide_list copy = tide_list_from(bytes, (int32_t)total, 1);
    tide_list_set(&r->source, copy, 1, TIDE_IN_MATCH);
    tide_scratch_reset(mark);
    free(bytes);
}

static bool valid_material(float density, float friction, float bounce)
{
    return finite1(density) && density >= 0 && finite1(friction) && friction >= 0 &&
        finite1(bounce) && bounce >= 0 && bounce <= 1;
}
static bool bounded_point(tide_float3 p)
{
    return finite3(p) && p.x >= -1e6f && p.x <= 1e6f && p.y >= -1e6f && p.y <= 1e6f && p.z >= -1e6f && p.z <= 1e6f;
}
static void live_material(b3ShapeId id, float density, float friction, float bounce)
{
    if (b3Shape_GetDensity(id) != density) { b3Shape_SetDensity(id, density, true); note('d', &density, sizeof density); }
    if (b3Shape_GetFriction(id) != friction) { b3Shape_SetFriction(id, friction); note('f', &friction, sizeof friction); }
    if (b3Shape_GetRestitution(id) != bounce) { b3Shape_SetRestitution(id, bounce); note('r', &bounce, sizeof bounce); }
}

static int64_t live_primitive(int64_t on, int64_t old, int kind, const void *data)
{
    b3ShapeId id = shape_of(active, old);
    bool exists = b3Shape_IsValid(id);
    float density, friction, bounce;
    bool sensor;
    if (kind == 0) {
        const box *s = data;
        if (!bounded_point(s->size) || s->size.x <= 0 || s->size.y <= 0 || s->size.z <= 0 || !bounded_point(s->offset) ||
            !valid_material(s->density, s->friction, s->bounce)) return 0;
        density = s->density; friction = s->friction; bounce = s->bounce; sensor = s->sensor;
    } else if (kind == 1) {
        const sphere *s = data;
        if (!finite1(s->radius) || s->radius <= 0 || s->radius > 1e6f || !bounded_point(s->offset) ||
            !valid_material(s->density, s->friction, s->bounce)) return 0;
        density = s->density; friction = s->friction; bounce = s->bounce; sensor = s->sensor;
    } else {
        const capsule *s = data;
        if (!finite1(s->radius) || s->radius <= 0 || s->radius > 1e6f || !finite1(s->height) || s->height < 0 || s->height > 1e6f ||
            !bounded_point(s->offset) || !valid_material(s->density, s->friction, s->bounce)) return 0;
        density = s->density; friction = s->friction; bounce = s->bounce; sensor = s->sensor;
    }
    if (exists && b3Shape_IsSensor(id) != sensor) { delete_native_shape(old); exists = false; }
    if (!exists) return kind == 0 ? b3t_box(on, data) : kind == 1 ? b3t_sphere(on, data) : b3t_capsule(on, data);
    // Called only when the source bytes changed, retaining the shape handle.
    if (kind == 0) {
        const box *s = data;
        const b3BoxHull hull = b3MakeOffsetBoxHull(s->size.x * 0.5f, s->size.y * 0.5f, s->size.z * 0.5f, vec(s->offset));
        b3Shape_SetHull(id, &hull.base);
    } else if (kind == 1) {
        const sphere *s = data;
        const b3Sphere shape = {vec(s->offset), s->radius}; b3Shape_SetSphere(id, &shape);
    } else {
        const capsule *s = data;
        const float half = b3MaxFloat(0, s->height * 0.5f - s->radius);
        const b3Capsule shape = {{s->offset.x, s->offset.y-half, s->offset.z}, {s->offset.x, s->offset.y+half, s->offset.z}, s->radius};
        b3Shape_SetCapsule(id, &shape);
    }
    live_material(id, density, friction, bounce);
    const b3BodyId body_id = body_of(active, on);
    b3Body_ApplyMassFromShapes(body_id); wake_moved(body_id);
    note('G', &old, sizeof old);
    return old;
}

typedef struct hull_collider { tide_list vertices; tide_float3 scale, offset; float density, friction, bounce; bool sensor; uint8_t padding[3]; int32_t cache; } hull_collider;
typedef struct height_collider { tide_list heights, holes; int32_t columns, rows; tide_float3 scale; float minimum, maximum, friction, bounce; int32_t cache; } height_collider;
_Static_assert(sizeof(hull_collider) == 48 && sizeof(height_collider) == 48, "new collider ABI changed");

static int64_t live_hull(int64_t on, int64_t old, const hull_collider *s)
{
    const int32_t count = tide_list_count(s->vertices);
    if (count < 4 || count > 65536 || !finite3(s->scale) || s->scale.x == 0 || s->scale.y == 0 || s->scale.z == 0 ||
        !bounded_point(s->offset) || !valid_material(s->density, s->friction, s->bounce)) return 0;
    b3Vec3 *points = tide_alloc((size_t)count * sizeof *points);
    for (int32_t i = 0; i < count; i++) {
        const tide_float3 *p = tide_list_at(s->vertices, i, sizeof *p);
        const tide_float3 point = {p->x*s->scale.x+s->offset.x, p->y*s->scale.y+s->offset.y, p->z*s->scale.z+s->offset.z};
        if (!bounded_point(point)) { free(points); return 0; }
        points[i] = vec(point);
    }
    b3HullData *hull = b3CreateHull(points, count, B3_MAX_HULL_VERTICES);
    free(points);
    if (!hull) return 0;
    b3ShapeId id = shape_of(active, old);
    if (b3Shape_IsValid(id) && b3Shape_IsSensor(id) != s->sensor) { delete_native_shape(old); id = (b3ShapeId){0}; }
    if (b3Shape_IsValid(id)) {
        b3Shape_SetHull(id, hull); live_material(id, s->density, s->friction, s->bounce);
        b3Body_ApplyMassFromShapes(body_of(active, on)); wake_moved(body_of(active, on));
    } else {
        const b3ShapeDef def = shape_def(s->density, s->friction, s->bounce, s->sensor);
        id = b3CreateHullShape(body_of(active, on), &def, hull);
    }
    b3DestroyHull(hull);
    return handle(id.index1, id.generation);
}

static int64_t live_height(int64_t on, int64_t old, const height_collider *s)
{
    if (b3Body_GetType(body_of(active, on)) != b3_staticBody || s->columns < 2 || s->rows < 2 || s->columns > 4096 || s->rows > 4096 ||
        (int64_t)s->columns * s->rows > 65536 || tide_list_count(s->heights) != (int64_t)s->columns * s->rows ||
        !finite3(s->scale) || s->scale.x <= 0 || s->scale.y <= 0 || s->scale.z <= 0 ||
        !finite1(s->minimum) || !finite1(s->maximum) || s->minimum > s->maximum || !finite1(s->maximum-s->minimum) ||
        !valid_material(0, s->friction, s->bounce)) return 0;
    const int cells = (s->columns-1)*(s->rows-1);
    if (tide_list_count(s->holes) != 0 && tide_list_count(s->holes) != cells) return 0;
    if (!bounded_point((tide_float3){s->scale.x*(s->columns-1), s->scale.y*s->minimum, s->scale.z*(s->rows-1)}) ||
        !bounded_point((tide_float3){0, s->scale.y*s->maximum, 0})) return 0;
    const int count = s->columns*s->rows;
    float *heights = tide_alloc((size_t)count * sizeof *heights);
    uint8_t *materials = tide_alloc((size_t)cells);
    for (int i = 0; i < count; i++) {
        heights[i] = *(const float *)tide_list_at(s->heights, i, sizeof(float));
        if (!finite1(heights[i])) { free(heights); free(materials); return 0; }
    }
    for (int i = 0; i < cells; i++) {
        const uint8_t *hole = tide_list_at(s->holes, i, 1);
        materials[i] = hole && *hole ? B3_HEIGHT_FIELD_HOLE : 0;
    }
    const b3HeightFieldDef def = {.heights=heights, .materialIndices=materials, .scale=vec(s->scale),
        .countX=s->columns, .countZ=s->rows, .globalMinimumHeight=s->minimum, .globalMaximumHeight=s->maximum};
    b3HeightFieldData *cooked = b3CreateHeightField(&def);
    free(heights); free(materials);
    if (!cooked) return 0;
    const b3HeightFieldData *kept = b3World_InternHeightField(active->id, cooked);
    b3DestroyHeightField(cooked);
    if (old) delete_native_shape(old);
    const b3ShapeDef material = shape_def(0, s->friction, s->bounce, false);
    const b3ShapeId id = b3CreateHeightFieldShape(body_of(active, on), &material, kept);
    return handle(id.index1, id.generation);
}

static int32_t live_shape(physics *p, tide_entity entity, int64_t on, int kind, const void *data, int32_t cached)
{
    if (!b3Body_IsValid(body_of(active, on))) return 0;
    shape_source source = {0};
    bool acceptable = true;
    uint8_t header[64] = {0};
    if (kind < 3) {
        source.header_size = kind == 0 ? offsetof(box,cache) : kind == 1 ? offsetof(sphere,cache) : offsetof(capsule,cache);
        memcpy(header, data, source.header_size);
        // Do not record arbitrary C padding after a bool.
        const size_t flag = kind == 0 ? offsetof(box,sensor) : kind == 1 ? offsetof(sphere,sensor) : offsetof(capsule,sensor);
        memset(header + flag + 1, 0, source.header_size - flag - 1);
    } else if (kind == 3) {
        const mesh_collider *s = data;
        source.lists[0] = s->vertices; source.sizes[0] = sizeof(tide_float3);
        source.lists[1] = s->indices; source.sizes[1] = sizeof(int32_t);
        source.header_size = offsetof(mesh_collider,cache) - 8;
        memcpy(header, (const char *)s+8, source.header_size);
        if (tide_list_count(s->vertices) > 65536 || tide_list_count(s->indices) > 3*65536) acceptable = false;
    } else if (kind == 4) {
        const hull_collider *s = data;
        source.lists[0] = s->vertices; source.sizes[0] = sizeof(tide_float3);
        source.header_size = offsetof(hull_collider,padding)-4;
        memcpy(header, (const char *)s+4, source.header_size);
        if (tide_list_count(s->vertices) > 65536) acceptable = false;
    } else {
        const height_collider *s = data;
        source.lists[0] = s->heights; source.sizes[0] = sizeof(float);
        source.lists[1] = s->holes; source.sizes[1] = 1;
        source.header_size = offsetof(height_collider,cache)-8;
        memcpy(header, (const char *)s+8, source.header_size);
        if (tide_list_count(s->heights) > 65536 || tide_list_count(s->holes) > 65536) acceptable = false;
    }
    for (int j = 0; j < 2; j++) {
        const int32_t count = tide_list_count(source.lists[j]);
        memcpy(header + source.header_size, &count, sizeof count); source.header_size += sizeof count;
    }
    if (kind == 3 || kind == 5) {
        const int32_t body_type = (int32_t)b3Body_GetType(body_of(active, on));
        memcpy(header + source.header_size, &body_type, sizeof body_type); source.header_size += sizeof body_type;
    }
    if (!acceptable) source.lists[0] = source.lists[1] = (tide_list){0};
    source.header = header;
    const shape_record *cached_record = cached > 0 ? tide_list_at(p->colliders,cached-1,sizeof(shape_record)) : NULL;
    const bool cache_valid = cached_record && cached_record->body == on && (cached_record->kind & 7) == kind && tide_entity_equal(cached_record->owner,entity);
    const int32_t slot = cache_valid ? cached-1 : collider_slot(p, entity, kind, on);
    shape_record r = *(const shape_record *)tide_list_at(p->colliders, slot, sizeof r);
    if (source_equal(r.source, &source)) return slot+1;
    const int64_t before = r.shape;
    int64_t made = 0;
    static const uint32_t material_offset[6] = {24,16,20,24,24,28};
    static const uint32_t material_size[6] = {13,13,13,8,13,8};
    uint8_t geometry_header[64];
    memcpy(geometry_header,header,source.header_size);
    bool geometry_same = false;
    if (tide_list_count(r.source) >= (int32_t)source.header_size) {
        for (uint32_t i=material_offset[kind];i<material_offset[kind]+material_size[kind];i++)
            geometry_header[i] = *(const uint8_t *)tide_list_at(r.source,(int32_t)i,1);
        shape_source geometry = source; geometry.header=geometry_header;
        geometry_same = source_equal(r.source,&geometry);
    }
    b3ShapeId previous = shape_of(active,before);
    const bool desired_sensor = kind == 0 ? ((const box *)data)->sensor : kind == 1 ? ((const sphere *)data)->sensor :
        kind == 2 ? ((const capsule *)data)->sensor : kind == 4 ? ((const hull_collider *)data)->sensor : false;
    if (!acceptable) made = 0;
    else if (geometry_same && b3Shape_IsValid(previous) && b3Shape_IsSensor(previous) == desired_sensor) {
        float density, friction, bounce;
        if (kind == 0) { const box *v=data; density=v->density; friction=v->friction; bounce=v->bounce; }
        else if (kind == 1) { const sphere *v=data; density=v->density; friction=v->friction; bounce=v->bounce; }
        else if (kind == 2) { const capsule *v=data; density=v->density; friction=v->friction; bounce=v->bounce; }
        else if (kind == 3) { const mesh_collider *v=data; density=0; friction=v->friction; bounce=v->bounce; }
        else if (kind == 4) { const hull_collider *v=data; density=v->density; friction=v->friction; bounce=v->bounce; }
        else { const height_collider *v=data; density=0; friction=v->friction; bounce=v->bounce; }
        if (valid_material(density,friction,bounce)) {
            live_material(previous,density,friction,bounce); wake_moved(body_of(active,on)); made=before;
        }
    } else
    if (kind < 3) made = live_primitive(on, before, kind, data);
    else if (kind == 3) made = attach_mesh_component(on, data);
    else if (kind == 4) made = live_hull(on, before, data);
    else made = live_height(on, before, data);
    if (before && before != made) delete_native_shape(before);
    r.shape = made;
    // Rejected geometry remains tracked, so a later edit can make it valid.
    source_save(&r, &source);
    memcpy(tide_list_at_mut(p->colliders, slot, sizeof r), &r, sizeof r);
    if (made && made != before) p->shapes = tide_add_i(p->shapes, 1);
    bool sensor = false;
    if (made) sensor = b3Shape_IsSensor(shape_of(active, made));
    if (before != made) changed_shape(&r, before, sensor);
    if (made) {
        note('g', &made, sizeof made);
        // Geometry and settings are explicit portable source bytes.
        note('g', header, source.header_size);
        bool copied = false;
        void *source_bytes = tide_list_flatten(r.source, 1, false, &copied);
        note('g', source_bytes, (size_t)tide_list_count(r.source));
        tide_list_unflatten(r.source, source_bytes, 1, false, copied);
    }
    return slot+1;
}

// Read each SoA lane once per run, rather than resolving a whole Body and
// every shape lane separately for every row.
#define LIVE_ROWS(name, type, number) \
void b3t_live_##name(tide_rows *rows, physics *p) { \
    const tide_entity *entities = tide_rows_entities(rows); \
    const int32_t *lo=tide_rows_ints(rows,0,offsetof(body,id)), *hi=tide_rows_ints(rows,0,offsetof(body,id)+4); \
    const int32_t *lanes[sizeof(type)/4]; \
    for(size_t j=0;j<sizeof(type)/4;j++) lanes[j]=tide_rows_ints(rows,1,j*4); \
    for (uint32_t i=0;i<tide_rows_count(rows);i++) { type s; \
        for(size_t j=0;j<sizeof(type)/4;j++) memcpy((uint8_t *)&s+j*4,&lanes[j][i],4); \
        const int64_t on=(int64_t)((uint64_t)(uint32_t)lo[i] | (uint64_t)(uint32_t)hi[i]<<32); \
        const int32_t cached=s.cache; s.cache=live_shape(p,entities[i],on,number,&s,cached); if(s.cache!=cached) tide_rows_set(rows,1,i,&s); } \
} \
void b3t_live_##name##_parts(tide_rows *rows, physics *p) { \
    const tide_entity *entities = tide_rows_entities(rows); \
    const int32_t *lo=tide_rows_ints(rows,0,offsetof(part,body)), *hi=tide_rows_ints(rows,0,offsetof(part,body)+4); \
    const int32_t *lanes[sizeof(type)/4]; \
    for(size_t j=0;j<sizeof(type)/4;j++) lanes[j]=tide_rows_ints(rows,1,j*4); \
    for (uint32_t i=0;i<tide_rows_count(rows);i++) { type s; \
        for(size_t j=0;j<sizeof(type)/4;j++) memcpy((uint8_t *)&s+j*4,&lanes[j][i],4); \
        const int64_t on=(int64_t)((uint64_t)(uint32_t)lo[i] | (uint64_t)(uint32_t)hi[i]<<32); \
        const int32_t cached=s.cache; s.cache=live_shape(p,entities[i],on,number,&s,cached); if(s.cache!=cached) tide_rows_set(rows,1,i,&s); } \
}
LIVE_ROWS(box, box, 0)
LIVE_ROWS(sphere, sphere, 1)
LIVE_ROWS(capsule, capsule, 2)
LIVE_ROWS(mesh, mesh_collider, 3)
LIVE_ROWS(hull, hull_collider, 4)
LIVE_ROWS(height, height_collider, 5)
#undef LIVE_ROWS

typedef struct collision_filter { int64_t category, mask; int32_t group; uint8_t padding[4]; } collision_filter;
void b3t_collider_filter(int32_t index, const collision_filter *filter, bool present, tide_resident *state)
{
    (void)state;
    if (!shape_physics || !active) return;
    const shape_record *r = tide_list_at(shape_physics->colliders, index, sizeof *r);
    if (!r || !r->shape) return;
    shape_record changed = *r;
    const int32_t flags = present ? r->kind | 64 : r->kind & ~64;
    if (flags != r->kind) { changed.kind=flags; memcpy(tide_list_at_mut(shape_physics->colliders,index,sizeof changed),&changed,sizeof changed); }
    r=&changed;
    const b3ShapeId id = shape_of(active, r->shape);
    if (!b3Shape_IsValid(id)) return;
    const b3Filter desired = {(uint64_t)filter->category, (uint64_t)filter->mask,
        b3Shape_IsSensor(id) ? -1 : filter->group};
    const b3Filter old = b3Shape_GetFilter(id);
    if (old.categoryBits == desired.categoryBits && old.maskBits == desired.maskBits && old.groupIndex == desired.groupIndex) return;
    b3Shape_SetFilter(id, desired, true);
    wake_moved(b3Shape_GetBody(id));
    note('c', &r->shape, sizeof r->shape);
    note('c', &desired.categoryBits, sizeof desired.categoryBits);
    note('c', &desired.maskBits, sizeof desired.maskBits);
    note('c', &desired.groupIndex, sizeof desired.groupIndex);
}

void b3t_apply_filters(tide_rows *rows, physics *p)
{
    const tide_entity *entities=tide_rows_entities(rows);
    for(uint32_t i=0;i<tide_rows_count(rows);i++) {
        collision_filter filter; tide_rows_get(rows,0,i,&filter);
        const int32_t at=tide_map_find(p->collider_index,&shape_kind,entities+i);
        const shape_entry *entry=at>=0 ? tide_map_entry(p->collider_index,at,&shape_kind) : NULL;
        if(!entry) continue;
        const shape_slots slots=entry->value;
        for(int j=0;j<6;j++) if(slots.slot[j]>0) b3t_collider_filter(slots.slot[j]-1,&filter,true,&p->state);
    }
}

// Exact metadata comparison is a per-world proof that no entity changed its
// component membership. No process identity, probabilistic hash or pointer is
// stored in simulation state. A changed table is copied only on structural edits.
void b3t_topology(tide_rows *rows, physics *p)
{
    const tide_entities *entities=rows->entities;
    if(!entities) { p->topology_changed=true; return; }
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
    p->topology_changed=!same;
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
