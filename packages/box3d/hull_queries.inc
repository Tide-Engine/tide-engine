static bool hull_geometry_valid(const hull_geometry *h)
{
    if (h->count < 4 || h->count > 128 || h->faces < 4 || h->faces > 128) return false;
    for (int i=0;i<h->count;i++) if (!bounded_point(h->points[i])) return false;
    for (int i=0;i<h->faces;i++) {
        const tide_float4 p = h->planes[i];
        if (!finite3((tide_float3){p.x,p.y,p.z}) || !finite1(p.w)) return false;
    }
    return true;
}
static b3ShapeProxy hull_proxy(const collider *c, const hull_geometry *h, b3Vec3 points[128])
{
    for (int i=0;i<h->count;i++) points[i] = b3RotateVector(quat(c->rotation),vec(h->points[i]));
    return (b3ShapeProxy){points,h->count,0};
}
static bool hull_overlap(const collider *c, const hull_geometry *h, b3ShapeProxy query, tide_float3 at_place)
{
    if (!scene_real(c) || !hull_geometry_valid(h) || !finite3(at_place)) return false;
    b3Vec3 points[128];
    const b3Vec3 relative = b3Sub(vec(at_place),vec(c->position));
    if (!finite3(float3(relative))) return false;
    const b3DistanceInput input = {.proxyA=hull_proxy(c,h,points), .proxyB=query,
        .transform={relative,b3Quat_identity}, .useRadii=true};
    b3SimplexCache cache = {0};
    return b3ShapeDistance(&input,&cache,NULL,0).distance <= 0;
}
static found hull_cast(const collider *c, const hull_geometry *h, b3ShapeProxy query, tide_float3 from, tide_float3 to)
{
    if (!scene_real(c) || !hull_geometry_valid(h) || !finite3(from) || !finite3(to)) return (found){0};
    const b3Vec3 relative = b3Sub(vec(from),vec(c->position)), translation = b3Sub(vec(to),vec(from));
    if (!finite3(float3(relative)) || !finite3(float3(translation))) return (found){0};
    found result = {0};
    if (hull_overlap(c,h,query,from)) {
        result.body=c->body; result.shape=c->shape; result.point=from; return result;
    }
    b3Vec3 points[128];
    const b3ShapeCastPairInput input = {.proxyA=hull_proxy(c,h,points), .proxyB=query,
        .transform={relative,b3Quat_identity}, .translationB=translation, .maxFraction=1};
    const b3CastOutput hit = b3ShapeCast(&input);
    if (!hit.hit) return result;
    result.body=c->body; result.shape=c->shape; result.fraction=hit.fraction;
    result.point=float3(b3Add(vec(c->position),hit.point)); result.normal=float3(hit.normal);
    return result;
}
found b3t_hull_ray(const collider *c, const hull_geometry *h, tide_float3 from, tide_float3 to)
{
    found result = {0};
    if (!scene_real(c) || !hull_geometry_valid(h) || !finite3(from) || !finite3(to)) return result;
    const b3Quat rotation = quat(c->rotation);
    const b3Vec3 start = b3InvRotateVector(rotation,b3Sub(vec(from),vec(c->position)));
    const b3Vec3 finish = b3InvRotateVector(rotation,b3Sub(vec(to),vec(c->position)));
    const b3Vec3 translation = b3Sub(finish,start);
    if (!finite3(float3(start)) || !finite3(float3(translation))) return result;
    typedef struct ray_hull { b3HullData data; b3Plane planes[128]; } ray_hull;
    ray_hull shape = {0};
    shape.data.faceCount=h->faces; shape.data.planeOffset=offsetof(ray_hull,planes);
    for (int i=0;i<h->faces;i++) shape.planes[i] = (b3Plane){{h->planes[i].x,h->planes[i].y,h->planes[i].z},h->planes[i].w};
    const b3RayCastInput input = {start,translation,1};
    const b3CastOutput hit = b3RayCastHull(&shape.data,&input);
    if (!hit.hit || hit.fraction <= 0) return result;
    result.body=c->body; result.shape=c->shape; result.fraction=hit.fraction;
    result.point=float3(b3Add(vec(c->position),b3RotateVector(rotation,hit.point)));
    result.normal=float3(b3RotateVector(rotation,hit.normal));
    return result;
}
bool b3t_hull_touch(const collider *a, const hull_geometry *ha, const collider *b, const hull_geometry *hb)
{
    if (!scene_real(a) || !scene_real(b) || (a->geometry && !hull_geometry_valid(ha)) || (b->geometry && !hull_geometry_valid(hb))) return false;
    b3Vec3 points_a[128], points_b[128];
    const b3Vec3 relative = b3Sub(vec(b->position),vec(a->position));
    if (!finite3(float3(relative))) return false;
    const b3DistanceInput input = {.proxyA=a->geometry ? hull_proxy(a,ha,points_a) : scene_proxy(a,points_a),
        .proxyB=b->geometry ? hull_proxy(b,hb,points_b) : scene_proxy(b,points_b),
        .transform={relative,b3Quat_identity}, .useRadii=true};
    b3SimplexCache cache = {0};
    return b3ShapeDistance(&input,&cache,NULL,0).distance <= 0;
}
#define HULL_QUERIES(name,type) \
found b3t_hull_cast_##name(const collider *c,const hull_geometry *h,const type *s,tide_float3 from,tide_quaternion rotation,tide_float3 to) { \
    probe p = name##_probe(s,rotation); if (!p.good || !sound(rotation)) return (found){0}; \
    p.proxy.points=p.points; return hull_cast(c,h,p.proxy,from,to); \
} \
bool b3t_hull_overlap_##name(const collider *c,const hull_geometry *h,const type *s,tide_float3 at_place,tide_quaternion rotation) { \
    probe p = name##_probe(s,rotation); if (!p.good || !sound(rotation)) return false; \
    p.proxy.points=p.points; return hull_overlap(c,h,p.proxy,at_place); \
}
HULL_QUERIES(box,box)
HULL_QUERIES(sphere,sphere)
HULL_QUERIES(capsule,capsule)
#undef HULL_QUERIES
