#!/usr/bin/env python3
"""Compile Foldline's metric assembly maps and check actual built geometry.

This is a prototype tool, not a general level format. Tide remains responsible
for gameplay and procedural geometry. Checked-in generated Tide also works with
the ordinary Tide CLI; --check makes stale generated output a build failure.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
FACES = {"+x": (0, 1), "-x": (0, -1), "+y": (1, 1), "-y": (1, -1), "+z": (2, 1), "-z": (2, -1)}
YAWS = {"NORTH": 0.0, "EAST": math.pi / 2, "SOUTH": math.pi, "WEST": -math.pi / 2}
PAINTS = {"DOORS", "BRICK", "PANELS", "CONCRETE", "MARBLE", "PLANKS", "HAZARD", "RUST", "SHUTTER", "WALLPAPER", "CARPET", "CEILING_TILE", "PLASTER"}
PROP_SOLID_PARTS = {"Desk":6, "Chair":6, "PlasticChair":6, "Cabinet":1, "Locker":1, "Partition":4,
                    "Shelf":7, "Vending":1, "Cooler":2, "Monitor":2, "Bed":9, "Lounger":9,
                    "Curtain":1, "Plant":2, "CoatRack":3, "Pipe":3, "Ladder":11,
                    "Cardboard":1, "Pallet":7, "Cone":6, "Dollhouse":6}


class MapError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise MapError(message)


def number(value, where):
    require(type(value) in (int, float) and math.isfinite(value), f"{where}: expected a finite number")
    return float(value)


def vector(value, where, length=3):
    require(isinstance(value, (list, tuple)) and len(value) == length, f"{where}: expected {length} coordinates")
    return tuple(number(v, where) for v in value)


def ident(value, where):
    require(isinstance(value, str) and re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", value), f"{where}: expected an identifier")
    return value


def fields(value, allowed, where, required=()):
    require(isinstance(value, dict), f"{where}: expected an object")
    unknown = set(value) - set(allowed)
    require(not unknown, f"{where}: unknown fields {', '.join(sorted(unknown))}")
    missing = set(required) - set(value)
    require(not missing, f"{where}: missing fields {', '.join(sorted(missing))}")


def flags(value, names, where):
    for name in names:
        if name in value:
            require(type(value[name]) is bool, f"{where}.{name}: expected boolean")


def mask(values):
    require(isinstance(values, list), "need: expected a list of signal numbers")
    require(all(type(v) is int and 0 <= v < 32 for v in values), "need: signals must be 0..31")
    require(len(values) == len(set(values)), "need: duplicate signal")
    return " | ".join(f"(1 << {v})" for v in values) or "0"


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def length(a):
    return math.sqrt(sum(x * x for x in a))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def rotated(point, quaternion):
    x, y, z, w = quaternion
    t = tuple(2 * v for v in cross((x, y, z), point))
    c = cross((x, y, z), t)
    return tuple(p + w * a + b for p, a, b in zip(point, t, c))


def bounds(box):
    if "min" in box:
        low, high = vector(box["min"], "box.min"), vector(box["max"], "box.max")
    else:
        center, size = vector(box["center"], "box.center"), vector(box["size"], "box.size")
        low = tuple(c - s / 2 for c, s in zip(center, size))
        high = tuple(c + s / 2 for c, s in zip(center, size))
    require(all(a < b for a, b in zip(low, high)), "box: dimensions must be positive")
    return low, high


def interval(a, b, low, high):
    """Parameter interval where a segment lies inside an axis aligned box."""
    first, last = 0.0, 1.0
    for p, q, lo, hi in zip(a, b, low, high):
        d = q - p
        if abs(d) < 1e-9:
            if not lo - 1e-7 <= p <= hi + 1e-7:
                return None
        else:
            u, v = (lo - p) / d, (hi - p) / d
            first, last = max(first, min(u, v)), min(last, max(u, v))
            if first > last + 1e-7:
                return None
    return first, last


def local_point(point, box):
    """Inverse rotate an exported OBB into its unrotated world-space bounds."""
    if "rotation" not in box:
        return point
    q = vector(box["rotation"], "rotation", 4)
    center = box["center"]
    v = sub(point, center)
    r = rotated(v, (-q[0], -q[1], -q[2], q[3]))
    return tuple(c + p for c, p in zip(center, r))


def support_intervals(a, b, boxes):
    found = []
    for box in boxes:
        # Grille bars have empty space between them; their enclosing collider
        # cannot be used as a solid cable mounting surface.
        if box.get("kind") == 4 or box.get("wall_kind") == 4:
            continue
        low, high = bounds(box)
        p, q = local_point(a, box), local_point(b, box)
        for axis in range(3):
            for side in (-1, 1):
                lo, hi = list(low), list(high)
                for other in range(3):
                    if other != axis:
                        lo[other] -= .04
                        hi[other] += .04
                face = low[axis] if side < 0 else high[axis]
                lo[axis], hi[axis] = sorted((face + side * .004, face + side * .09))
                hit = interval(p, q, lo, hi)
                if hit:
                    found.append(hit)
    return found


def supported_segment(a, b, boxes):
    reached = 0.0
    for first, last in sorted(support_intervals(a, b, boxes)):
        if first > reached + 1e-5:
            return False
        reached = max(reached, last)
    return reached >= 1 - 1e-5


def intersects(first, second, tolerance=.045):
    """Separating-axis box overlap, including real rotated prop/ramp boxes."""
    def oriented(box):
        low, high = bounds(box)
        center = tuple((a + b) / 2 for a, b in zip(low, high))
        half = tuple(max(0, (b - a - tolerance) / 2) for a, b in zip(low, high))
        q = box.get("rotation", [0, 0, 0, 1])
        axes = [rotated(axis, q) for axis in ((1, 0, 0), (0, 1, 0), (0, 0, 1))]
        return center, half, axes
    ac, ah, aa = oriented(first)
    bc, bh, ba = oriented(second)
    delta = sub(bc, ac)
    axes = aa + ba + [cross(a, b) for a in aa for b in ba]
    for axis in axes:
        if length(axis) < 1e-8:
            continue
        reach_a = sum(h * abs(dot(axis, direction)) for h, direction in zip(ah, aa))
        reach_b = sum(h * abs(dot(axis, direction)) for h, direction in zip(bh, ba))
        if abs(dot(delta, axis)) >= reach_a + reach_b - 1e-8:
            return False
    return True


def turn(point, yaw):
    x, y, z = point
    return (math.cos(yaw) * x + math.sin(yaw) * z, y, -math.sin(yaw) * x + math.cos(yaw) * z)


def lifted(at, yaw, point):
    return tuple(x + y for x, y in zip(at, turn(point, yaw)))


def lift_boxes(lift):
    """Mirror LiftShell's actual boxes so structural checks include its jambs."""
    at, yaw = lift["at"], YAWS[lift["yaw"]]
    shapes = [((0, -.5, -.06), (8, 1, 4.32)), ((-3.5, 2.2, 0), (1, 4.4, 3.12)),
              ((3.5, 2.2, 0), (1, 4.4, 3.12)), ((0, 2.2, -2.72), (8, 4.4, 1)),
              ((0, 4.2, 2.1), (6, .4, 1)), ((0, 4.8, -.31), (8, 1, 3.82))]
    for side in (-1, 1):
        shapes.append(((side * 3.5, 2.2, 2.1), (1, 4.4, 1.08)))
        shapes.append(((side * 3.5, 2.2, -1.89), (1, 4.4, .66)))
    result = []
    for center, size in shapes:
        rotated = turn(size, yaw)
        result.append({"center": lifted(at, yaw, center), "size": tuple(abs(v) for v in rotated), "kind": 5})
    return result


def validate_lift(lift, boxes, label):
    yaw = YAWS[lift["yaw"]] if isinstance(lift["yaw"], str) else lift["yaw"]
    # Clear the whole 6 x 4 opening and two metres of approach. An opening
    # that only admits a single body is still a broken six-metre lift.
    a = lifted(lift["at"], yaw, (-2.95, .06, 1.95))
    stop = lift.get("role") == "stop"
    b = lifted(lift["at"], yaw, (2.95, 3.94, 3.15 if stop else 4.15))
    clear = {"min": tuple(min(x, y) for x, y in zip(a, b)), "max": tuple(max(x, y) for x, y in zip(a, b))}
    for box in boxes:
        description = box.get("id", f"built {box.get('kind', 'wall')} at {box.get('center')} size {box.get('size')}")
        require(not intersects(clear, box), f"{label}: lift opening/approach is blocked by {description}")
        if box.get("kind") in (4, 5) or "rotation" in box:
            continue
        low, high = bounds(box)
        center = turn(sub(tuple((a + b) / 2 for a, b in zip(low, high)), lift["at"]), -yaw)
        size = tuple(abs(v) for v in turn(sub(high, low), -yaw))
        inner = tuple(c - s / 2 for c, s in zip(center, size))
        outer = tuple(c + s / 2 for c, s in zip(center, size))
        if min(outer[1], 3.94) - max(inner[1], .06) <= 1e-4 or min(outer[2], 2.64) - max(inner[2], -2.22) <= 1e-4:
            continue
        coincident = abs(inner[0] - 3) < 1e-4 or abs(outer[0] + 3) < 1e-4
        require(not coincident, f"{label}: {description} is coplanar with the lift's inner side lining; leave the full eight-metre shell slot")
    # Exact rectangular union coverage: every boundary comes from an actual
    # floor edge. Membership is constant inside each resulting cell, so this
    # catches even a narrow gap instead of relying on a spacing of sample rays.
    floor_low, floor_high = clear["min"], clear["max"]
    x0, z0, x1, z1 = floor_low[0], floor_low[2], floor_high[0], floor_high[2]
    floor_y = lift["at"][1]
    rectangles = []
    for box in boxes:
        if box.get("kind") == 4:
            continue
        low, high = bounds(box)
        if "rotation" in box:
            q = box["rotation"]
            up = rotated((0, 1, 0), q)
            across = rotated((1, 0, 0), q)
            if up[1] < 1 - 1e-6 or min(abs(across[0]), abs(across[2])) > 1e-6:
                continue  # The lift contract is a flat, axis-aligned apron.
            center = box["center"]
            half = tuple(v / 2 for v in box["size"])
            corner = rotated(half, q)
            low = tuple(c - abs(v) for c, v in zip(center, corner))
            high = tuple(c + abs(v) for c, v in zip(center, corner))
        if abs(high[1] - floor_y) > .06:
            continue
        rectangle = (max(x0, low[0]), max(z0, low[2]), min(x1, high[0]), min(z1, high[2]))
        if rectangle[0] < rectangle[2] and rectangle[1] < rectangle[3]:
            rectangles.append(rectangle)
    xs = sorted({x0, x1} | {v for r in rectangles for v in (r[0], r[2])})
    zs = sorted({z0, z1} | {v for r in rectangles for v in (r[1], r[3])})
    for left, right in zip(xs, xs[1:]):
        for near, far in zip(zs, zs[1:]):
            if right - left < 1e-6 or far - near < 1e-6:
                continue
            x, z = (left + right) / 2, (near + far) / 2
            require(any(a - 1e-7 <= x <= c + 1e-7 and b - 1e-7 <= z <= d + 1e-7 for a, b, c, d in rectangles),
                    f"{label}: lift opening/approach has missing floor at x {left:.4g}..{right:.4g}, z {near:.4g}..{far:.4g}")


def resolve_anchor(anchor, boxes):
    require(isinstance(anchor, dict), "anchor: expected an object")
    if anchor.get("support") == "scene":
        fields(anchor, ("support", "at", "face"), "scene anchor", ("support", "at"))
        if "face" in anchor:
            require(isinstance(anchor["face"], str) and anchor["face"] in FACES, "anchor.face: unknown face")
        return vector(anchor["at"], "anchor.at")
    fields(anchor, ("support", "face", "uv", "offset"), "box anchor", ("support", "face", "uv"))
    face = anchor.get("face")
    require(isinstance(face, str) and face in FACES, "anchor.face: expected +x, -x, +y, -y, +z or -z")
    ident(anchor.get("support"), "anchor.support")
    require(anchor.get("support") in boxes, f"anchor: unknown support {anchor.get('support')!r}")
    low, high = bounds(boxes[anchor["support"]])
    axis, side = FACES[face]
    uv = vector(anchor["uv"], "anchor.uv", 2)
    offset = number(anchor.get("offset", .035), "anchor.offset")
    require(.004 <= offset <= .09, "anchor.offset: cable mounting offset must be .004..09 m")
    out = [0.0, 0.0, 0.0]
    out[axis] = (high[axis] if side > 0 else low[axis]) + side * offset
    for other, coordinate in zip((i for i in range(3) if i != axis), uv):
        require(low[other] - .04 <= coordinate <= high[other] + .04, "anchor: outside support face")
        out[other] = coordinate
    return tuple(out)


def point(value, anchors):
    if isinstance(value, str):
        require(value in anchors, f"unknown anchor {value!r}")
        return anchors[value]
    return vector(value, "point")


def validate_portal(portal, wall, label):
    low, high = bounds(wall)
    face = vector(portal["face"], "portal.face") if isinstance(portal["face"], list) else tuple(FACES[portal["face"]][1] if i == FACES[portal["face"]][0] else 0 for i in range(3))
    require(face[1] == 0 and sum(abs(v) for v in face) == 1, f"{label}: portal face must be an upright axis face")
    axis = 0 if face[0] else 2
    side = face[axis]
    size = number(portal.get("size", 1), "portal.size")
    require(size > 0, f"{label}: portal size must be positive")
    at = vector(portal["at"], "portal.at") if "at" in portal else tuple(v + (1.3 * size if i == 1 else 0) for i, v in enumerate(vector(portal["foot"], "portal.foot")))
    require(wall.get("takes", True), f"{label}: portal requires a receptive wall")
    require(high[axis] - low[axis] >= .6 - 1e-6, f"{label}: portal backing must be at least .6 m thick")
    plane = high[axis] if side > 0 else low[axis]
    require(abs(at[axis] - plane) <= .015, f"{label}: portal is not attached to its wall face")
    across = 2 if axis == 0 else 0
    require(low[across] - .015 <= at[across] - .68 * size and at[across] + .68 * size <= high[across] + .015,
            f"{label}: portal crosses the edge of its backing")
    require(low[1] - .015 <= at[1] - 1.3 * size and at[1] + 1.3 * size <= high[1] + .015,
            f"{label}: portal crosses floor/ceiling of its backing")


def validate_map(data):
    categories = ("boxes", "lifts", "doors", "machines", "plates", "wires", "ends", "frames")
    fields(data, ("version", "name", "function", "place_ids", "external_geometry", "anchors", *categories), "map", ("version", "name"))
    require(type(data.get("version")) is int and data["version"] == 1, "map.version: expected 1")
    ident(data.get("name"), "map.name")
    if "function" in data:
        ident(data["function"], "map.function")
    require(type(data.get("external_geometry", False)) is bool, "external_geometry: expected boolean")
    complete_geometry = not data.get("external_geometry", False)
    assembly_ids = set()
    for category in categories:
        entries = data.get(category, [])
        require(isinstance(entries, list), f"{category}: expected a list")
        for entry in entries:
            require(isinstance(entry, dict), f"{category}: expected objects")
            name = ident(entry.get("id"), f"{category}.id")
            require(name not in assembly_ids, f"duplicate assembly ID {name}")
            assembly_ids.add(name)
    if "place_ids" in data:
        values = data["place_ids"]
        require(isinstance(values, list) and values and all(type(i) is int and i >= 0 for i in values), "place_ids: expected nonempty list of nonnegative integers")
        require(len(values) == len(set(values)), "place_ids: duplicate ID")
    boxes = {}
    for box in data.get("boxes", []):
        fields(box, ("id", "min", "max", "paint", "takes", "kind"), "box", ("id", "min", "max", "paint"))
        name = ident(box.get("id"), "box.id")
        require(name not in boxes, f"duplicate box {name}")
        bounds(box)
        require(isinstance(box.get("paint"), str) and box["paint"] in PAINTS, f"{name}: unknown paint")
        require(type(box.get("takes", False)) is bool, f"{name}: takes must be boolean")
        require(type(box.get("kind", 0)) is int and 0 <= box.get("kind", 0) <= 7, f"{name}: kind must be an integer 0..7")
        boxes[name] = box
    require(isinstance(data.get("anchors", {}), dict), "anchors: expected a named object")
    anchors = {ident(name, "anchor name"): resolve_anchor(value, boxes) for name, value in data.get("anchors", {}).items()}
    assembly_boxes = list(boxes.values())
    for lift in data.get("lifts", []):
        role = lift.get("role")
        allowed = ("id", "role", "at", "yaw") + (("need", "any", "socket_x") if role == "exit" else ("stay",) if role == "stop" else ())
        fields(lift, allowed, "lift", ("id", "role", "at", "yaw"))
        ident(lift.get("id"), "lift.id")
        require(lift.get("role") in ("arrival", "exit", "stop"), "lift.role: expected arrival, exit or stop")
        vector(lift.get("at"), "lift.at")
        require(isinstance(lift.get("yaw"), str) and lift["yaw"] in YAWS, "lift.yaw: expected NORTH/EAST/SOUTH/WEST")
        mask(lift.get("need", []))
        flags(lift, ("any",), "lift")
        if role == "stop":
            require(number(lift.get("stay"), "lift.stay") > 0, "lift.stay: must be positive")
        if role == "exit" and lift.get("need"):
            # The signal lamps sit on the lintel beside the 0.95 m sign, never over it, and inside the doorway.
            socket = number(lift.get("socket_x"), "lift.socket_x")
            require(.9 <= abs(socket) <= 2.6, "lift.socket_x: exit lamps must sit 0.9 to 2.6 m along the lintel from its sign")
        assembly_boxes.extend(lift_boxes(lift))
        if boxes and complete_geometry:
            validate_lift(lift, assembly_boxes, lift["id"])
    for door in data.get("doors", []):
        fields(door, ("id", "at", "size", "need", "any", "speed", "socket", "lamp_depth"), "door", ("id", "at", "size"))
        ident(door.get("id"), "door.id")
        vector(door.get("at"), "door.at")
        require(all(v > 0 for v in vector(door.get("size"), "door.size")), "door.size: dimensions must be positive")
        mask(door.get("need", []))
        flags(door, ("any",), "door")
        if "socket" in door:
            point(door["socket"], anchors)
        if "lamp_depth" in door:
            require("socket" in door, "door.lamp_depth: requires an explicit socket")
            require(number(door["lamp_depth"], "lamp_depth") > 0, "lamp_depth: must be positive")
        if "speed" in door:
            require(door["speed"] is not None, "door.speed: expected a finite speed or named constant")
        speed(door.get("speed"))
    for machine in data.get("machines", []):
        fields(machine, ("id", "from", "to", "size", "need", "any", "speed", "lamp", "along", "lamp_depth"), "machine", ("id", "from", "to", "size", "lamp"))
        ident(machine.get("id"), "machine.id")
        vector(machine.get("from"), "machine.from")
        vector(machine.get("to"), "machine.to")
        require(all(v > 0 for v in vector(machine.get("size"), "machine.size")), "machine.size: dimensions must be positive")
        mask(machine.get("need", []))
        flags(machine, ("any",), "machine")
        point(machine.get("lamp"), anchors)
        along = vector(machine.get("along", [1, 0, 0]), "machine.along")
        require(along in ((1,0,0),(-1,0,0),(0,0,1),(0,0,-1)) or (not machine.get("need") and along == (0,0,0)),
                "machine.along: expected a unit horizontal cardinal axis for signal lamps")
        if "lamp_depth" in machine:
            require(number(machine["lamp_depth"], "machine.lamp_depth") > 0, "machine.lamp_depth: must be positive")
        if "speed" in machine:
            require(machine["speed"] is not None, "machine.speed: expected a finite speed or named constant")
        speed(machine.get("speed"), "MOVER_SPEED")
    for plate in data.get("plates", []):
        fields(plate, ("id", "at", "signal", "heavy", "need", "half", "linger", "invert"), "plate", ("id", "at", "signal"))
        ident(plate.get("id"), "plate.id")
        vector(plate.get("at"), "plate.at")
        mask([plate.get("signal")])
        flags(plate, ("heavy", "invert"), "plate")
        if "need" in plate:
            require(number(plate["need"], "plate.need") > 0, "plate.need: must be positive; omit it to use the standard threshold")
        require(number(plate.get("linger", 0), "plate.linger") >= 0, "plate.linger: must be nonnegative")
        if "half" in plate:
            require(all(v > 0 for v in vector(plate["half"], "plate.half", 2)), "plate.half: dimensions must be positive")
    for wire in data.get("wires", []):
        fields(wire, ("id", "signal", "from", "to", "points"), "wire", ("id", "signal", "from", "to", "points"))
        ident(wire.get("id"), "wire.id")
        ident(wire["from"], "wire.from")
        ident(wire["to"], "wire.to")
        mask([wire.get("signal")])
        require(isinstance(wire["points"], list), "wire.points: expected a list")
        points = [point(v, anchors) for v in wire.get("points", [])]
        require(2 <= len(points) <= 12, f"{wire['id']}: cable needs 2..12 points")
        for index, (a, b) in enumerate(zip(points, points[1:])):
            require(length(sub(a, b)) > 1e-5, f"{wire['id']}: duplicate cable point")
            # Scene-supported maps are checked after the actual game builds.
            if boxes and complete_geometry:
                require(supported_segment(a, b, assembly_boxes), f"{wire['id']}: segment {index + 1} is unsupported (floating or buried cable)")
        if "from" in wire:
            plates = {p["id"]: p for p in data.get("plates", [])}
            require(wire["from"] in plates, f"{wire['id']}: unknown source plate")
            plate = plates[wire["from"]]
            require(plate["signal"] == wire["signal"], f"{wire['id']}: source signal does not match")
            delta = sub(points[0], plate["at"])
            half = plate.get("half", [.8, .8] if plate.get("heavy", False) else [.6, .6])
            require(abs(delta[1]) <= .09 and abs(delta[0]) <= half[0] + .12 and abs(delta[2]) <= half[1] + .12,
                    f"{wire['id']}: cable misses its source plate rim")
        if "to" in wire:
            targets = {d["id"]: d for d in data.get("doors", []) + data.get("machines", [])}
            target = targets.get(wire["to"])
            if target is not None:
                require(wire["signal"] in target.get("need", []), f"{wire['id']}: target does not use this signal")
                socket = target.get("socket", target.get("lamp"))
                require(socket is not None and length(sub(points[-1], point(socket, anchors))) <= .12,
                        f"{wire['id']}: cable misses target socket")
            else:
                exits = {l["id"]: l for l in data.get("lifts", []) if l["role"] == "exit"}
                require(wire["to"] in exits, f"{wire['id']}: unknown target")
                target = exits[wire["to"]]
                require(wire["signal"] in target.get("need", []), f"{wire['id']}: exit does not use this signal")
                socket = lifted(target["at"], YAWS[target["yaw"]], (target.get("socket_x", -1), 4.2, 2.65))
                require(length(sub(points[-1], socket)) <= .12, f"{wire['id']}: cable misses exit socket")
    end_indices = set()
    for category in ("ends", "frames"):
        for entry in data.get(category, []):
            allowed = ("id", "wall", "face", "foot", "size") + (("end",) if category == "ends" else ())
            fields(entry, allowed, category, ("id", "wall", "face", "foot") + (("end",) if category == "ends" else ()))
            ident(entry["wall"], "portal.wall")
            require(isinstance(entry["face"], str) and entry["face"] in FACES, "portal.face: unknown face")
            if category == "ends":
                require(type(entry["end"]) is int and 0 <= entry["end"] < 4, "end: must be 0..3")
                require(entry["end"] not in end_indices, "end: duplicate operator-owned index")
                end_indices.add(entry["end"])
    for entry in data.get("ends", []) + data.get("frames", []):
        require(entry.get("wall") in boxes, "portal: unknown backing wall")
        validate_portal(entry, boxes[entry["wall"]], entry.get("id", "portal"))
    targets = data.get("lifts", []) + data.get("doors", []) + data.get("machines", [])
    for target in targets:
        for signal in target.get("need", []):
            require(any(w.get("to") == target["id"] and w.get("signal") == signal for w in data.get("wires", [])),
                    f"{target['id']}: signal {signal} has no incoming cable connection")
    return boxes, anchors


def scalar(value):
    text = format(float(value), ".9g")
    return "0" if text == "-0" else text


def v3(value):
    return "float3(" + ", ".join(scalar(v) for v in value) + ")"


def boolean(value):
    return "true" if value else "false"


def speed(value, default="DOOR_SPEED"):
    if value is None:
        return default
    if value in ("DOOR_SPEED", "MOVER_SPEED"):
        return value
    require(number(value, "speed") > 0, "speed: must be positive")
    return scalar(value)


def compile_map(data):
    boxes, anchors = validate_map(data)
    name = "".join(word[:1].upper() + word[1:] for word in data["name"].split("_"))
    function = ident(data.get("function", f"BuildMap{name}"), "map.function")
    fold_argument = ", mut Fold fold" if data.get("ends") else ""
    lines = [f"void {function}(Arena arena, mut Place place{fold_argument})", "{"]
    referenced = {p["wall"] for p in data.get("ends", [])}
    for box in boxes.values():
        low, high = bounds(box)
        args = ", ".join(scalar(v) for pair in zip(low, high) for v in pair)
        prefix = f"var map_{box['id']} = " if box["id"] in referenced else ""
        lines.append(f"    {prefix}Fill(arena, {args}, {box['paint']}, {boolean(box.get('takes', False))}, {box.get('kind', 0)});")
    for lift in data.get("lifts", []):
        args = f"arena, place, {v3(lift['at'])}, {lift['yaw']}"
        if lift["role"] == "arrival":
            lines.append(f"    ArrivalLift({args});")
        elif lift["role"] == "stop":
            lines.append(f"    StoppingLift({args}, {scalar(lift['stay'])});")
        else:
            lines.append(f"    ExitLift({args}, {mask(lift.get('need', []))}, {boolean(lift.get('any', False))}, {scalar(lift.get('socket_x', -1))});")
    for door in data.get("doors", []):
        args = f"arena, {v3(door['at'])}, {v3(door['size'])}, {mask(door.get('need', []))}, {boolean(door.get('any', False))}, {speed(door.get('speed'))}"
        if "socket" in door:
            dest = list(door["at"])
            dest[1] += door["size"][1] + .2
            along = (1, 0, 0) if door["size"][0] >= door["size"][2] else (0, 0, 1)
            args = f"arena, {v3(door['at'])}, {v3(dest)}, {v3(door['size'])}, {mask(door.get('need', []))}, {boolean(door.get('any', False))}, {speed(door.get('speed'))}, {v3(point(door['socket'], anchors))}, {v3(along)}"
            if "lamp_depth" in door:
                args += f", {scalar(door['lamp_depth'])}"
            lines.append(f"    Machine({args});")
        else:
            lines.append(f"    HangDoor({args});")
    for machine in data.get("machines", []):
        args = f"arena, {v3(machine['from'])}, {v3(machine['to'])}, {v3(machine['size'])}, {mask(machine.get('need', []))}, {boolean(machine.get('any', False))}, {speed(machine.get('speed'), 'MOVER_SPEED')}, {v3(point(machine['lamp'], anchors))}, {v3(machine.get('along', [1, 0, 0]))}"
        if "lamp_depth" in machine:
            args += f", {scalar(machine['lamp_depth'])}"
        lines.append(f"    Machine({args});")
    for plate in data.get("plates", []):
        if "half" in plate:
            half = ", ".join(scalar(v) for v in plate["half"])
            lines.append(f"    Spawn(Plate {{ chamber = arena.chamber, at = {v3(plate['at'])}, half = float2({half}), signal = {plate['signal']}, heavy = {boolean(plate.get('heavy', False))}, linger = {scalar(plate.get('linger', 0))}, invert = {boolean(plate.get('invert', False))}, need = {scalar(plate.get('need', 200 if plate.get('heavy') else 20))} }});")
        else:
            lines.append(f"    LayPlate(arena, {v3(plate['at'])}, {plate['signal']}, {boolean(plate.get('heavy', False))}, {scalar(plate.get('linger', 0))}, {boolean(plate.get('invert', False))}, {scalar(plate.get('need', 0))});")
    for wire in data.get("wires", []):
        points = ", ".join(v3(point(p, anchors)) for p in wire["points"])
        lines.append(f"    RunWire(arena, {wire['signal']}, [{points}]);")
    for entry in data.get("ends", []) + data.get("frames", []):
        axis, side = FACES[entry["face"]]
        face = tuple(side if i == axis else 0 for i in range(3))
        args = f"{v3(entry['foot'])}, {v3(face)}, {scalar(entry.get('size', 1))}"
        if "end" in entry:
            require(type(entry["end"]) is int and 0 <= entry["end"] < 4, "end: must be 0..3")
            lines.append(f"    PlaceEnd(fold, {entry['end']}, map_{entry['wall']}, {args});")
        else:
            lines.append(f"    PaintFrame(arena, {args});")
    lines.append("}")
    return "\n".join(lines)


def generate(paths):
    result = ["// Generated by tools/foldmap.py from maps/*.foldmap.json. Do not edit.", "// Run python prototypes/foldline/tools/foldmap.py to regenerate.", ""]
    names = set()
    for path in sorted(paths):
        data = json.loads(path.read_text(encoding="utf-8"))
        require(data.get("name") not in names, f"duplicate map {data.get('name')}")
        names.add(data.get("name"))
        result.extend((compile_map(data), ""))
    return "\n".join(result)


def close_record(expected, actual):
    """Compare exported float32 records while ignoring unrelated fields."""
    if isinstance(expected, dict):
        return isinstance(actual, dict) and all(k in actual and close_record(v, actual[k]) for k, v in expected.items())
    if isinstance(expected, (list, tuple)):
        return isinstance(actual, (list, tuple)) and len(expected) == len(actual) and all(close_record(a, b) for a, b in zip(expected, actual))
    if type(expected) in (int, float) and type(actual) in (int, float):
        return math.isfinite(actual) and abs(expected - actual) <= .0001
    return expected == actual


def signal_mask(signals):
    return sum(1 << signal for signal in signals)


def expected_machine(machine, anchors, door=False):
    if door:
        start = machine["at"]
        dest = list(start)
        dest[1] += machine["size"][1] + .2
        lamp = point(machine["socket"], anchors) if "socket" in machine else (start[0], start[1] + machine["size"][1] / 2 + .3, start[2])
        along = (1, 0, 0) if machine["size"][0] >= machine["size"][2] else (0, 0, 1)
    else:
        start, dest = machine["from"], machine["to"]
        lamp, along = point(machine["lamp"], anchors), machine.get("along", [1, 0, 0])
    velocity = machine.get("speed", "DOOR_SPEED" if door else "MOVER_SPEED")
    if isinstance(velocity, str):
        velocity = {"DOOR_SPEED":6.0, "MOVER_SPEED":2.2}[velocity]
    return {"from":start, "to":dest, "size":machine["size"], "need":signal_mask(machine.get("need", [])),
            "any":machine.get("any", False), "speed":velocity, "lamp":lamp, "lampAlong":along, "lampDepth":machine.get("lamp_depth", 0)}


def validate_bindings(sources, scene, campaign=False):
    """Prove that checked map assembly, not an uncalled recipe, was built."""
    errors = []
    by_id = {}
    for source in sources:
        validate_map(source)
        for place_id in source.get("place_ids", []):
            require(place_id not in by_id, f"place {place_id}: belongs to more than one source map")
            by_id[place_id] = source
    actual_ids = {room["id"] for room in scene.get("maps", [])}
    require(by_id, "map sources need place_ids for actual-scene binding checks")
    if campaign:
        require(set(by_id) == set(range(2, 16)), "source maps must cover all fourteen campaign IDs 2..15 exactly once")
    require(actual_ids == set(by_id), f"source/built map coverage differs: missing {sorted(set(by_id) - actual_ids)}, extra {sorted(actual_ids - set(by_id))}")

    def matching(expected, actual, label, exact=True):
        available = list(actual)
        if exact and len(expected) != len(actual):
            errors.append(f"{label}: expected {len(expected)} source assemblies, found {len(actual)} built")
        for item in expected:
            match = next((i for i, candidate in enumerate(available) if close_record(item, candidate)), None)
            if match is None:
                errors.append(f"{label}: source assembly was not built: {item}")
            else:
                available.pop(match)

    for room in scene.get("maps", []):
        source = by_id[room["id"]]
        boxes, anchors = validate_map(source)
        label = f"map {room['id']} ({source['name']})"
        expected_walls = []
        for box in boxes.values():
            low, high = bounds(box)
            expected_walls.append({"center":tuple((a + b) / 2 for a, b in zip(low, high)),
                                   "size":tuple(b - a for a, b in zip(low, high)),
                                   "takes":box.get("takes", False), "kind":box.get("kind", 0)})
        matching(expected_walls, room.get("walls", []), f"{label} structural boxes", exact=False)
        wires = [{"signal":w["signal"], "points":[point(p, anchors) for p in w["points"]]} for w in source.get("wires", [])]
        matching(wires, room.get("wires", []), f"{label} cables")
        plates = []
        for plate in source.get("plates", []):
            heavy = plate.get("heavy", False)
            plates.append({"at":plate["at"], "signal":plate["signal"], "half":plate.get("half", [.8, .8] if heavy else [.6, .6]),
                           "heavy":heavy, "need":plate.get("need", 200 if heavy else 20), "linger":plate.get("linger", 0), "invert":plate.get("invert", False)})
        matching(plates, room.get("plates", []), f"{label} plates")
        machines = [expected_machine(m, anchors) for m in source.get("machines", [])]
        machines.extend(expected_machine(m, anchors, door=True) for m in source.get("doors", []))
        matching(machines, room.get("movers", []), f"{label} machinery")
        for lift in source.get("lifts", []):
            role = "arrival" if lift["role"] == "stop" else lift["role"]
            expected = {"at":lift["at"], "yaw":YAWS[lift["yaw"]]}
            if role == "exit":
                expected.update(need=signal_mask(lift.get("need", [])), any=lift.get("any", False))
            if not close_record(expected, room.get(role, {})):
                errors.append(f"{label}: {role} lift does not match source slot")
            if lift["role"] == "stop":
                if room.get("kind") != 2 or not close_record(lift["stay"], room.get("stay")):
                    errors.append(f"{label}: stopping lift role/stay does not match source")
        ends = []
        for end in source.get("ends", []):
            axis, side = FACES[end["face"]]
            size = end.get("size", 1)
            low, high = bounds(boxes[end["wall"]])
            ends.append({"index":end["end"], "at":tuple(v + (1.3 * size if i == 1 else 0) for i, v in enumerate(end["foot"])),
                         "face":tuple(side if i == axis else 0 for i in range(3)), "size":size,
                         "wall_center":tuple((a + b) / 2 for a, b in zip(low, high)), "wall_size":tuple(b - a for a, b in zip(low, high))})
        matching(ends, room.get("ends", []), f"{label} preplaced folds", exact=False)
    return errors


def exposed_top_overlaps(boxes):
    """Find shared top-face areas that are not buried inside other structure."""
    solids = [(box, bounds(box)) for box in boxes if box.get("kind") != 4]
    conflicts = []
    for first, (a, (al, ah)) in enumerate(solids):
        for second in range(first + 1, len(solids)):
            b, (bl, bh) = solids[second]
            if abs(ah[1] - bh[1]) > 1e-4:
                continue
            x0, x1 = max(al[0], bl[0]), min(ah[0], bh[0])
            z0, z1 = max(al[2], bl[2]), min(ah[2], bh[2])
            if x1 - x0 <= 1e-4 or z1 - z0 <= 1e-4:
                continue
            covers = []
            for other, (_, (low, high)) in enumerate(solids):
                if other in (first, second) or not low[1] - 1e-4 <= ah[1] + .002 < high[1] - 1e-4:
                    continue
                left, right = max(x0, low[0]), min(x1, high[0])
                near, far = max(z0, low[2]), min(z1, high[2])
                if left < right and near < far:
                    covers.append((left, right, near, far))
            xs = sorted({x0, x1} | {v for rect in covers for v in rect[:2]})
            zs = sorted({z0, z1} | {v for rect in covers for v in rect[2:]})
            exposed = any(right - left > 1e-4 and far - near > 1e-4 and
                          not any(lx <= (left + right) / 2 <= hx and lz <= (near + far) / 2 <= hz
                                  for lx, hx, lz, hz in covers)
                          for left, right in zip(xs, xs[1:]) for near, far in zip(zs, zs[1:]))
            if exposed:
                conflicts.append((a, b, ah[1]))
    return conflicts


def validate_scene(scene):
    """Validate exported match geometry, including geometry made by Tide hooks.

    These checks prove assembly properties only. They cannot infer puzzle
    solvability, moving-body collisions or readable pixels; those have their
    ordinary input/GPU tests.
    """
    require(scene.get("version") == 1 and isinstance(scene.get("maps"), list) and scene["maps"], "scene: expected version 1 and at least one built map")
    require(len({room["id"] for room in scene["maps"]}) == len(scene["maps"]), "scene: duplicate map ID")
    errors = []
    for room in scene.get("maps", []):
        label = f"map {room['id']}"
        boxes = room.get("walls", []) + [b for b in room.get("solids", []) if b.get("kind") not in ("door", "gate")]
        for first, second, height in exposed_top_overlaps(room.get("walls", [])):
            errors.append(f"{label}: coplanar exposed top surfaces at y {height:.4g}: {first.get('center', first.get('id'))} and {second.get('center', second.get('id'))}")
        for prop in room.get("props", []):
            expected = PROP_SOLID_PARTS.get(prop.get("kind_name"), 0)
            if not expected:
                continue
            count = prop.get("solid_parts", 0)
            if count != expected:
                errors.append(f"{label}: {prop['kind_name']} at {prop['at']} needs {expected} attached solid parts, found {count}")
            actual = [b for b in room.get("solids", []) if b.get("kind") == "prop" and b.get("owner") == prop.get("entity")]
            if len(actual) != expected:
                errors.append(f"{label}: {prop['kind_name']} at {prop['at']} is missing its actual compound collision geometry")
        for index, wire in enumerate(room.get("wires", [])):
            points = wire["points"]
            for segment, (a, b) in enumerate(zip(points, points[1:])):
                if not supported_segment(a, b, boxes):
                    errors.append(f"{label}: wire {index} signal {wire['signal']} segment {segment + 1} unsupported: {a} -> {b}")
        for role in ("arrival", "exit"):
            if role == "exit" and room.get("kind") in ("Vignette", 2):
                continue
            if role not in room:
                continue
            try:
                lift = dict(room[role])
                if room.get("kind") in ("Vignette", 2):
                    lift["role"] = "stop"
                validate_lift(lift, boxes, f"{label} {role}")
            except MapError as error:
                errors.append(str(error))
        if room.get("kind") not in ("Vignette", 2) and "exit" in room:
            signs = [p for p in room.get("props", []) if p.get("kind_name") == "ExitSign"]
            expected_yaw = room["exit"]["yaw"]
            expected_at = lifted(room["exit"]["at"], expected_yaw, (0, 4.2, 2.65))
            matches = [s for s in signs if length(sub(s["at"], expected_at)) < .15]
            if len(matches) != 1:
                errors.append(f"{label}: exit needs exactly one sign on its room-side header socket")
            else:
                sign = matches[0]
                if abs(math.remainder(sign["yaw"] - expected_yaw, 2 * math.pi)) > .01:
                    errors.append(f"{label}: exit sign faces away from its room approach")
                if not supported_segment(sign["at"], sign["at"], boxes):
                    errors.append(f"{label}: exit sign is not mounted on a structural surface")
        if "exit" in room and room["exit"].get("need", 0) and abs(room["exit"].get("socket", 0)) < .9:
            errors.append(f"{label}: exit signal lamps cover its sign")
        for index, wire in enumerate(room.get("wires", [])):
            points = wire["points"]
            plates = [p for p in room.get("plates", []) if p["signal"] == wire["signal"]]
            source = False
            for plate in plates:
                d = sub(points[0], plate["at"])
                half = plate["half"]
                if abs(d[1]) <= .09 and abs(d[0]) <= half[0] + .12 and abs(d[2]) <= half[1] + .12:
                    source = True
            if not source:
                errors.append(f"{label}: wire {index} misses its signal's source plate")
            sockets = [m["lamp"] for m in room.get("movers", []) if m["need"] & (1 << wire["signal"])]
            if "exit" in room and room["exit"].get("need", 0) & (1 << wire["signal"]):
                sockets.append(lifted(room["exit"]["at"], room["exit"]["yaw"], (room["exit"].get("socket", -1), 4.2, 2.65)))
            if not any(length(sub(points[-1], socket)) <= .12 for socket in sockets):
                errors.append(f"{label}: wire {index} misses its signal's target socket")
        for index, portal in enumerate(room.get("ends", [])):
            try:
                wall = {"center": portal["wall_center"], "size": portal["wall_size"], "takes": portal.get("wall_takes", False)}
                validate_portal(portal, wall, f"{label} end {index}")
            except MapError as error:
                errors.append(str(error))
        for index, frame in enumerate(room.get("frames", [])):
            matching = []
            for wall in room.get("walls", []):
                try:
                    validate_portal(frame, wall, "frame")
                    matching.append(wall)
                except MapError:
                    pass
            if not matching:
                errors.append(f"{label}: frame {index} lacks a full receptive backing face")
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="reject stale checked-in generated Tide")
    parser.add_argument("--maps", type=Path, default=ROOT / "maps")
    parser.add_argument("--output", type=Path, default=ROOT / "maps_generated.tide")
    parser.add_argument("--scene", type=Path, help="validate JSON from foldline_test --export-layout")
    parser.add_argument("--scene-only", action="store_true", help="check exported geometry without generating/checking Tide")
    args = parser.parse_args(argv)
    try:
        if not args.scene_only:
            paths = list(args.maps.glob("*.foldmap.json"))
            require(paths, f"{args.maps}: no .foldmap.json source maps found")
            result = generate(paths)
            if args.check:
                require(args.output.exists() and args.output.read_text(encoding="utf-8") == result,
                        f"{args.output}: generated maps are stale; run python prototypes/foldline/tools/foldmap.py")
            else:
                args.output.write_text(result, encoding="utf-8", newline="\n")
        else:
            require(args.scene is not None, "--scene-only requires --scene PATH")
        if args.scene:
            scene = json.loads(args.scene.read_text(encoding="utf-8"))
            errors = validate_scene(scene)
            if not args.scene_only:
                sources = [json.loads(p.read_text(encoding="utf-8")) for p in paths]
                errors.extend(validate_bindings(sources, scene, campaign=True))
            require(not errors, "\n".join(errors))
    except (MapError, KeyError, TypeError, OSError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1
    print("Foldline maps checked" if args.check or args.scene_only else "Foldline maps generated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
