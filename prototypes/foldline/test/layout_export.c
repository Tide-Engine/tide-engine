// Inspection of the real match builders, consumed by the map validator on
// native and WASI test hosts with an explicitly supplied writable output path.
// The export never substitutes authored metadata for actual room geometry.
#include <errno.h>

static void layout_vec3(FILE *file, const tide_float3 value)
{
    CHECK(isfinite(value.x) && isfinite(value.y) && isfinite(value.z));
    fprintf(file, "[%.9g,%.9g,%.9g]", value.x, value.y, value.z);
}

static void layout_rotation(FILE *file, const tide_quaternion value)
{
    CHECK(isfinite(value.value.x) && isfinite(value.value.y) &&
          isfinite(value.value.z) && isfinite(value.value.w));
    fprintf(file, "[%.9g,%.9g,%.9g,%.9g]", value.value.x, value.value.y,
            value.value.z, value.value.w);
}

static void layout_separator(FILE *file, bool *first)
{
    fputs(*first ? "\n" : ",\n", file);
    *first = false;
}

static const char *layout_prop_name(const PropKind kind)
{
    static const char *const names[] = {
        "Desk", "Chair", "Cabinet", "Partition", "Shelf", "Locker", "Vending", "Cooler", "Clock", "Monitor",
        "Bed", "Curtain", "Lounger", "PlasticChair", "Plant", "CoatRack", "Pipe", "Ladder", "ExitSign",
        "Arrow", "HandSign", "EyeSign", "FigureSign", "Switch", "Cardboard", "Pallet", "Cone", "Dollhouse", "Web"
    };
    const int index = (int)kind;
    return index >= 0 && (size_t)index < sizeof names / sizeof names[0] ? names[index] : "Unknown";
}

static void layout_lift(FILE *file, const tide_float3 at, const float yaw)
{
    CHECK(isfinite(yaw));
    fputs("{\"at\":", file);
    layout_vec3(file, at);
    fprintf(file, ",\"yaw\":%.9g}", yaw);
}

static void layout_map(FILE *file, const tide_world *world)
{
    const int chamber = world->Arena.chamber;
    CHECK(isfinite(world->Place.stay));
    fprintf(file, "{\"id\":%d,\"kind\":%d,\"stay\":%.9g,\"arrival\":",
            world->Place.id, (int)world->Place.kind, world->Place.stay);
    layout_lift(file, world->Place.arrival, world->Place.arrivalYaw);
    fputs(",\"exit\":{\"at\":", file);
    layout_vec3(file, world->Place.exit);
    CHECK(isfinite(world->Place.exitYaw));
    fprintf(file, ",\"yaw\":%.9g,\"need\":%d,\"any\":%s},\n\"walls\":[",
            world->Place.exitYaw, world->Place.exitNeed, world->Place.exitAny ? "true" : "false");

    bool first = true;
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Wall(world, entity)) continue;
        const Wall wall = tide_get_Wall(world, entity);
        if (wall.chamber != chamber) continue;
        layout_separator(file, &first);
        fputs("{\"center\":", file);
        layout_vec3(file, wall.center);
        fputs(",\"size\":", file);
        layout_vec3(file, wall.size);
        CHECK(isfinite(wall.paint));
        fprintf(file, ",\"kind\":%d,\"takes\":%s,\"paint\":%.9g}", wall.kind, wall.takes ? "true" : "false", wall.paint);
    }

    fputs("\n],\n\"solids\":[", file);
    first = true;
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Box3D_Box(world, entity)) continue;
        tide_entity owner = entity;
        if (tide_has_Box3D_Part(world, entity)) {
            const Box3D_Part part = tide_get_Box3D_Part(world, entity);
            if (part.box == 0) continue;
            owner = part.of;
        }
        if (!tide_has_Box3D_Body(world, owner) || tide_has_Piece(world, owner) ||
            tide_has_Cube(world, owner) || tide_has_Operator(world, owner) ||
            tide_has_Mover(world, owner)) continue;
        const Box3D_Body body = tide_get_Box3D_Body(world, owner);
        const Box3D_Box box = tide_get_Box3D_Box(world, entity);
        if (box.sensor) continue;
        const char *kind = "static";
        if (tide_has_Prop(world, owner)) {
            if (tide_get_Prop(world, owner).chamber != chamber) continue;
            kind = "prop";
        } else if (tide_has_Ramp(world, owner)) {
            if (tide_get_Ramp(world, owner).chamber != chamber) continue;
            kind = "ramp";
        } else if (tide_has_Door(world, owner)) {
            if (tide_get_Door(world, owner).chamber != chamber) continue;
            kind = "door";
        } else if (tide_has_Gate(world, owner)) {
            if (tide_get_Gate(world, owner).chamber != chamber) continue;
            kind = "gate";
        } else if (body.type != Box3D_BodyType_Static) continue;
        layout_separator(file, &first);
        fprintf(file, "{\"kind\":\"%s\",\"owner\":%u,\"center\":", kind, owner.index);
        layout_vec3(file, tide_add_f3(body.position, tide_rotate_q(body.rotation, box.offset)));
        fputs(",\"size\":", file);
        layout_vec3(file, box.size);
        fputs(",\"rotation\":", file);
        layout_rotation(file, body.rotation);
        fputc('}', file);
    }

    fputs("\n],\n\"wires\":[", file);
    first = true;
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Wire(world, entity)) continue;
        const Wire wire = tide_get_Wire(world, entity);
        if (wire.chamber != chamber) continue;
        CHECK(wire.count >= 0 && wire.count <= 12);
        layout_separator(file, &first);
        fprintf(file, "{\"signal\":%d,\"points\":[", wire.signal);
        for (int index = 0; index < wire.count; index++) {
            if (index != 0) fputc(',', file);
            layout_vec3(file, wire.points.at[index]);
        }
        fputs("]}", file);
    }

    fputs("\n],\n\"plates\":[", file);
    first = true;
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Plate(world, entity)) continue;
        const Plate plate = tide_get_Plate(world, entity);
        if (plate.chamber != chamber) continue;
        layout_separator(file, &first);
        fprintf(file, "{\"signal\":%d,\"at\":", plate.signal);
        layout_vec3(file, plate.at);
        CHECK(isfinite(plate.half.x) && isfinite(plate.half.y));
        CHECK(isfinite(plate.need) && isfinite(plate.linger));
        fprintf(file, ",\"half\":[%.9g,%.9g],\"need\":%.9g,\"heavy\":%s,\"linger\":%.9g,\"invert\":%s}",
                plate.half.x, plate.half.y, plate.need, plate.heavy ? "true" : "false", plate.linger,
                plate.invert ? "true" : "false");
    }

    fputs("\n],\n\"frames\":[", file);
    first = true;
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_FoldFrame(world, entity)) continue;
        const FoldFrame frame = tide_get_FoldFrame(world, entity);
        if (frame.chamber != chamber) continue;
        layout_separator(file, &first);
        fputs("{\"at\":", file);
        layout_vec3(file, frame.at);
        fputs(",\"face\":", file);
        layout_vec3(file, frame.face);
        CHECK(isfinite(frame.size));
        fprintf(file, ",\"size\":%.9g}", frame.size);
    }

    fputs("\n],\n\"props\":[", file);
    first = true;
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Prop(world, entity)) continue;
        const Prop prop = tide_get_Prop(world, entity);
        if (prop.chamber != chamber) continue;
        layout_separator(file, &first);
        int solids = tide_has_Box3D_Box(world, entity) ? 1 : 0;
        for (uint32_t shape_slot = 0; shape_slot < world->entities.next_unused; shape_slot++) {
            const tide_entity shape_entity = tide_entity_in_slot(&world->entities, shape_slot);
            if (!tide_has_Box3D_Part(world, shape_entity) || !tide_has_Box3D_Box(world, shape_entity)) continue;
            const Box3D_Part part = tide_get_Box3D_Part(world, shape_entity);
            if (part.of.index == entity.index && part.of.generation == entity.generation && part.box != 0) solids++;
        }
        fprintf(file, "{\"kind\":%d,\"kind_name\":\"%s\",\"entity\":%u,\"solid_parts\":%d,\"at\":", (int)prop.kind, layout_prop_name(prop.kind), entity.index, solids);
        layout_vec3(file, prop.at);
        CHECK(isfinite(prop.yaw));
        fprintf(file, ",\"yaw\":%.9g,\"scale\":", prop.yaw);
        layout_vec3(file, prop.scale);
        fputc('}', file);
    }

    fputs("\n],\n\"movers\":[", file);
    first = true;
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Mover(world, entity)) continue;
        const Mover mover = tide_get_Mover(world, entity);
        if (mover.chamber != chamber) continue;
        layout_separator(file, &first);
        fputs("{\"at\":", file);
        layout_vec3(file, mover.at);
        fputs(",\"from\":", file);
        layout_vec3(file, mover.from);
        fputs(",\"to\":", file);
        layout_vec3(file, mover.to);
        fputs(",\"size\":", file);
        layout_vec3(file, mover.size);
        CHECK(isfinite(mover.speed) && isfinite(mover.lampDepth));
        fprintf(file, ",\"need\":%d,\"any\":%s,\"speed\":%.9g,\"lampDepth\":%.9g,\"lamp\":",
                mover.need, mover.any ? "true" : "false", mover.speed, mover.lampDepth);
        layout_vec3(file, mover.lamp);
        fputs(",\"lampAlong\":", file);
        layout_vec3(file, mover.lampAlong);
        fputc('}', file);
    }

    fputs("\n],\n\"pads\":[", file);
    first = true;
    for (uint32_t slot = 0; slot < world->entities.next_unused; slot++) {
        const tide_entity entity = tide_entity_in_slot(&world->entities, slot);
        if (!tide_has_Pad(world, entity)) continue;
        const Pad pad = tide_get_Pad(world, entity);
        if (pad.chamber != chamber) continue;
        layout_separator(file, &first);
        fputs("{\"at\":", file);
        layout_vec3(file, pad.at);
        fputs(",\"launch\":", file);
        layout_vec3(file, pad.launch);
        fputc('}', file);
    }

    fputs("\n],\n\"ends\":[", file);
    first = true;
    for (int index = 0; index < 4; index++) {
        const Portal end = world->Fold.ends.at[index];
        if (!end.open) continue;
        CHECK(tide_has_Wall(world, end.wall));
        const Wall wall = tide_get_Wall(world, end.wall);
        CHECK(wall.chamber == chamber);
        layout_separator(file, &first);
        fprintf(file, "{\"index\":%d,\"at\":", index);
        layout_vec3(file, end.at);
        fputs(",\"face\":", file);
        layout_vec3(file, end.face);
        CHECK(isfinite(end.size));
        fprintf(file, ",\"size\":%.9g,\"wall_center\":", end.size);
        layout_vec3(file, wall.center);
        fputs(",\"wall_size\":", file);
        layout_vec3(file, wall.size);
        fprintf(file, ",\"wall_kind\":%d,\"wall_takes\":%s}", wall.kind, wall.takes ? "true" : "false");
    }
    fputs("\n]}", file);
}

static int export_layout(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "FOLDLINE cannot export layout to '%s': %s\n", path, strerror(errno));
        return 1;
    }
    fputs("{\"version\":1,\"maps\":[\n", file);
    for (int id = RECEPTION; id < PLACES; id++) {
        tide_world world = {0};
        start(&world);
        choose(&world, 0, id);
        ticks(&world, 10);
        CHECK(world.Place.id == id);
        if (id != RECEPTION) fputs(",\n", file);
        layout_map(file, &world);
        tide_world_free(&world);
    }
    fputs("\n]}\n", file);
    const bool failed = ferror(file) != 0;
    const int closed = fclose(file);
    if (failed || closed != 0) {
        fprintf(stderr, "FOLDLINE failed writing layout to '%s'\n", path);
        return 1;
    }
    printf("FOLDLINE exported %d built maps to %s\n", PLACES - RECEPTION, path);
    return 0;
}
