#!/usr/bin/env python3
"""Writes maps.tide and art/blocks/*.obj from the data extracted from the Unity game.

    python make_maps.py <extracted maps folder>            writes ../maps.tide and ../art/blocks/*.obj
    python make_maps.py <extracted maps folder> --check    checks the data and writes nothing

The folder holds Map1.json ... Map10.json and meshes/*.obj (SUMMARY.md there says what
they are). Every check runs either way, and a failed one writes nothing.
"""

import json
import math
import os
import struct
import sys

ROTATION = ['Map1', 'Map2', 'Map4', 'Map6', 'Map7', 'Map9', 'Map10']

MESHES = {
    'Plane.fbx': ('Plane', 'plane'),
    'Plane square.fbx': ('PlaneSquare', 'plane-square'),
    'Cube.fbx': ('Cube', 'cube'),
    'Cube U.fbx': ('CubeU', 'cube-u'),
    'Cube L.fbx': ('CubeL', 'cube-l'),
    'Cube Beam Long.fbx': ('BeamLong', 'beam-long'),
    'Cube Beam short.fbx': ('BeamShort', 'beam-short'),
    'Trapezoid narrow.fbx': ('TrapezoidNarrow', 'trapezoid-narrow'),
}
PAINTS = ['green', 'grey', 'orange', 'purple', 'red', 'blue', 'brown', 'yellow']

# The Trapezoid narrow mesh's own bounds (SUMMARY.md, section 5): its convex
# MeshCollider becomes a box of these.
TRAPEZOID_MIN = (-0.698609, -0.71724, -0.698609)
TRAPEZOID_MAX = (0.698609, 0.2869, 0.698609)

IDENTITY = (0.0, 0.0, 0.0, 1.0)


# ---------------------------------------------------------------- math

def add(a, b): return tuple(x + y for x, y in zip(a, b))
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def scaled(a, s): return tuple(x * s for x in a)
def mul(a, b): return tuple(x * y for x, y in zip(a, b))
def length(a): return math.sqrt(sum(x * x for x in a))
def dist(a, b): return length(sub(a, b))


def qnorm(q):
    n = math.sqrt(sum(x * x for x in q))
    return tuple(x / n for x in q)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qinv(q): return (-q[0], -q[1], -q[2], q[3])


def qrot(q, v):
    x, y, z, w = q
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), qinv(q))
    return (p[0], p[1], p[2])


def qangle(a, b):
    """The angle between two rotations, in radians."""
    d = abs(sum(x * y for x, y in zip(a, b)))
    return 2 * math.acos(min(1.0, d))


def axis_angle(axis, degrees):
    h = math.radians(degrees) / 2
    s = math.sin(h)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h))


def mat_point(m, p):
    return tuple(m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] for r in range(3))


def lerp(a, b, t): return a + (b - a) * t


def inverse_lerp(a, b, v):
    return 0.0 if a == b else min(1.0, max(0.0, (v - a) / (b - a)))


def hinge_rotation(kind, angle):
    """What MovingBridge gives a half, in the world."""
    if kind == 'HingeA':
        return axis_angle((1, 0, 0), angle)
    return qmul(axis_angle((1, 0, 0), -angle), axis_angle((0, 1, 0), 180))


# ---------------------------------------------------------------- text

def f32(x): return struct.unpack('f', struct.pack('f', x))[0]


def num(x):
    """The shortest text that reads back as the same float32, with no exponent."""
    x = f32(x)
    if x == 0:
        return '0'
    for digits in range(1, 10):
        text = '%.*g' % (digits, x)
        if f32(float(text)) == x:
            break
    if 'e' in text or 'E' in text:
        text = ('%.12f' % float(text)).rstrip('0').rstrip('.')
    return '0' if text in ('-0', '0') else text


def f3(v): return 'float3(%s, %s, %s)' % tuple(num(x) for x in v)
def quat(q): return 'quaternion(%s, %s, %s, %s)' % tuple(num(x) for x in q)
def color(c): return 'Color(%s, %s, %s)' % tuple(num(x) for x in c[:3])
def is_identity(q): return qangle(q, IDENTITY) < 1e-7


def literal(name, fields):
    return '%s { %s }' % (name, ', '.join('%s = %s' % field for field in fields))


# ---------------------------------------------------------------- a map

class Check:
    def __init__(self):
        self.failures = []
        self.count = 0

    def that(self, ok, text):
        self.count += 1
        if not ok:
            self.failures.append(text)


def behaviour(piece, kind):
    for b in piece['behaviours']:
        if b['type'] == kind and b.get('enabled', True):
            return b['fields']
    return None


def vec(d): return (d['x'], d['y'], d.get('z', 0.0)) if 'z' in d else (d['x'], d['y'])


class Map:
    def __init__(self, index, data, check):
        self.index = index
        self.name = data['map']
        self.data = data
        self.check = check
        self.pieces = []     # dicts
        self.movers = []
        self.blocks = []
        self.visuals = []
        self.spawns = []
        self.notes = []
        self.floor = None
        self.expected_pieces = 0
        self.by_id = {p['id']: p for p in data['pieces']}
        self.by_path = {p['path']: p for p in data['pieces']}
        self.transforms = {t['path']: t for t in data['otherTransforms']}
        self.owner = {}      # piece id -> ('mover'|'block'|'hinge'|'lift', index, frame)
        self.build()

    # -- transforms

    def node(self, path):
        return self.by_path.get(path) or self.transforms[path]

    def chain(self, path, root):
        """A node's place under `root`, from the local transforms between them:
        (position, rotation, scale), the scale being the product down the chain."""
        steps = []
        while path != root:
            n = self.node(path)
            steps.append(n['local'])
            path = n['parentPath']
        position, rotation, scale = (0.0, 0.0, 0.0), IDENTITY, (1.0, 1.0, 1.0)
        for local in reversed(steps):
            position = add(position, qrot(rotation, mul(scale, local['position'])))
            rotation = qmul(rotation, qnorm(local['rotation']))
            scale = mul(scale, local['scale'])
        return position, rotation, scale

    def body_of(self, piece):
        """The piece that owns the Rigidbody this one moves with."""
        if piece['rigidbodyId'] is not None:
            return self.by_id[piece['rigidbodyId']]
        parent = piece['parentId']
        while parent is not None:
            if self.by_id[parent]['rigidbody']:
                return self.by_id[parent]
            parent = self.by_id[parent]['parentId']
        return None

    # -- building

    def build(self):
        data = self.data
        pieces = data['pieces']

        # Movers and blocks, in the order their bodies come in the file
        slide, block = {}, {}
        for p in pieces:
            body = p['rigidbody']
            moving = behaviour(p, 'EnvironmentMoving')
            if body and moving:
                speed = vec(moving['_moveSpeed'])
                slide[p['id']] = len(self.movers)
                self.movers.append({
                    'kind': 'Slide', 'path': p['path'], 'origin': tuple(p['position']),
                    'travel': vec(moving['_endPoint']), 'speedMin': speed[0], 'speedMax': speed[1],
                    'randomStart': bool(moving['_randomizeStartPosition'])})
                self.check.that(body['isKinematic'], '%s: an EnvironmentMoving body that is not kinematic' % p['path'])
            elif body and not body['isKinematic']:
                physics = behaviour(p, 'EnvironmentPhysics')
                self.check.that(physics is not None, '%s: a dynamic body with no EnvironmentPhysics' % p['path'])
                block[p['id']] = len(self.blocks)
                self.blocks.append({
                    'path': p['path'], 'origin': tuple(p['position']), 'rotation': qnorm(p['rotation']),
                    'mass': body['mass'], 'damping': body['linearDamping'],
                    'knockback': physics['_knockbackMultiplier'] if physics else 0.5})

        # Bridges: three movers each
        hinged = {}   # path prefix -> (mover index, kind, pivot path)
        for p in pieces:
            bridge = behaviour(p, 'MovingBridge')
            if not bridge:
                continue
            angles = vec(bridge['_minMaxAngle'])
            heights = vec(bridge['_minMaxheight'])
            common = {'angleMin': angles[0], 'angleMax': angles[1], 'angleStart': bridge['_startingAngle'],
                      'angleSpeed': bridge['_speed'], 'heightMin': heights[0], 'heightMax': heights[1]}
            a = len(self.movers)
            for kind, half, visuals in (('HingeA', '_bridgeHalfA', '_bridgeHalfAVisuals'),
                                        ('HingeB', '_bridgeHalfB', '_bridgeHalfBVisuals')):
                pivot = self.node(bridge[half]['ref'])
                seen = self.node(bridge[visuals]['ref'])
                together = dist(pivot['position'], seen['position']) < 1e-4 and \
                    bridge[visuals]['ref'].startswith(bridge[half]['ref'] + '/')
                self.check.that(together, '%s: the visual pivot of %s is not at its collider pivot' % (p['path'], kind))
                # The serialized pose is one MovingBridge can give: angle -20
                serialized = hinge_rotation(kind, angles[0])
                self.check.that(qangle(serialized, qnorm(pivot['rotation'])) < 1e-4,
                                '%s: %s is not saved at its lowest angle' % (p['path'], kind))
                mover = dict(common, kind=kind, path=pivot['path'], origin=tuple(pivot['position']),
                             link=-1 if kind == 'HingeA' else a, visualsTogether=together)
                hinged[pivot['path']] = (len(self.movers), kind, pivot['path'])
                self.movers.append(mover)
            blocker = self.node(bridge['_blocker']['ref'])
            blocker_seen = self.node(bridge['_blockerVisuals']['ref'])
            parent = self.node(blocker['parentPath'])
            self.check.that(is_identity(qnorm(parent['rotation'])), '%s: the blocker rises along a turned parent' % p['path'])
            self.check.that(blocker_seen['parentPath'] == blocker['parentPath'] and
                            dist(blocker_seen['local']['position'], blocker['local']['position']) < 1e-5,
                            '%s: the blocker and what is drawn of it are not in the same place' % p['path'])
            lift = {'kind': 'Lift', 'path': blocker['path'], 'origin': tuple(parent['position']), 'link': a,
                    'height': blocker['local']['position'][1]}
            lift.update(common)
            for path in (blocker['path'], blocker_seen['path']):
                hinged[path] = (len(self.movers), 'Lift', path)
            self.movers.append(lift)

        def frame(p):
            """How a piece is held: (field, index, kind, extra)."""
            for path, (index, kind, pivot) in hinged.items():
                if p['path'] == path or p['path'].startswith(path + '/'):
                    return ('mover', index, kind, pivot)
            body = self.body_of(p)
            if body is not None:
                if body['id'] in slide:
                    return ('mover', slide[body['id']], 'Slide', body)
                if body['id'] in block:
                    return ('block', block[body['id']], 'Block', body)
            return None

        def place(p, held, position, rotation, local_center=None):
            """(local, localRotation, rest position, rest rotation) of something at
            `position`/`rotation` in the world, as the file has it."""
            if held is None:
                return None, None, position, rotation
            _, index, kind, extra = held
            if kind == 'Slide':
                # A slide only translates: the rest rotation is in the local data
                return sub(position, tuple(extra['position'])), rotation, position, rotation
            if kind == 'Block':
                inv = qinv(qnorm(extra['rotation']))
                return qrot(inv, sub(position, tuple(extra['position']))), qmul(inv, rotation), position, rotation
            mover = self.movers[index]
            if kind == 'Lift':
                # The blocker object is at origin + (0, height, 0)
                at = add(mover['origin'], (0.0, mover['height'], 0.0))
                local = sub(position, at)
                start = lerp(mover['heightMin'], mover['heightMax'],
                             inverse_lerp(mover['angleMax'], mover['angleMin'], mover['angleStart']))
                return local, rotation, add(add(mover['origin'], (0.0, start, 0.0)), local), rotation
            # A hinge: from the local transforms under the pivot, not from the saved world pose
            chain_position, chain_rotation, chain_scale = self.chain(p['path'], extra)
            local = add(chain_position, qrot(chain_rotation, mul(chain_scale, local_center or (0.0, 0.0, 0.0))))
            saved = qnorm(self.node(extra)['rotation'])
            self.check.that(dist(add(mover['origin'], qrot(saved, local)), position) < 2e-4 and
                            qangle(qmul(saved, chain_rotation), rotation) < 2e-4,
                            '%s: its place under the pivot does not give the saved world pose' % p['path'])
            start = hinge_rotation(kind, mover['angleStart'])
            return local, chain_rotation, add(mover['origin'], qrot(start, local)), qmul(start, chain_rotation)

        for p in pieces:
            held = frame(p)
            rotation = qnorm(p['rotation'])
            scale = tuple(abs(s) for s in p['scale'])
            self.check.that(not p['shear'], '%s: sheared' % p['path'])
            pad = behaviour(p, 'EnvironmentJumpPad')

            for c in p['colliders']:
                if not c['enabled']:
                    continue
                note = None
                if c['type'] == 'box':
                    center, size = tuple(c['center']), tuple(c['size'])
                    self.expected_pieces += 1
                elif c['type'] == 'mesh' and c['convex'] and c['mesh']['file'] == 'Trapezoid narrow.fbx':
                    center = scaled(add(TRAPEZOID_MIN, TRAPEZOID_MAX), 0.5)
                    size = sub(TRAPEZOID_MAX, TRAPEZOID_MIN)
                    note = 'a convex MeshCollider there: this box is its mesh\'s bounds'
                    self.expected_pieces += 1
                elif c['type'] == 'mesh' and not c['convex'] and c['mesh']['file'] == 'Map6-Floor.mesh':
                    self.floor_boxes(p)
                    continue
                else:
                    self.check.that(False, '%s: a %s collider this script has no rule for' % (p['path'], c['type']))
                    continue
                position = mat_point(p['matrix'], center)
                half = mul(scaled(size, 0.5), scale)
                local, local_rotation, rest, rest_rotation = place(p, held, position, rotation, center)
                piece = {'path': p['path'], 'position': rest, 'rotation': rest_rotation, 'half': half,
                         'solid': not c['isTrigger'], 'note': note,
                         'damageFloor': p['layer'] == 'Ground' and 'GroundDamage' in (p['prefab'], p['containerPrefab']),
                         'ignoreRaycast': p['layer'] == 'Ignore Raycast'}
                if pad and c['isTrigger']:
                    piece['jumpPad'] = True
                    piece['force'] = qrot(rotation, vec(pad['_knockbackForce']))
                if held:
                    piece[held[0]] = held[1]
                    piece['local'], piece['localRotation'] = local, local_rotation
                    piece['held'] = held[2]
                self.pieces.append(piece)

            mesh = p['mesh']
            if p['renderer'] and p['renderer']['enabled'] and mesh:
                if mesh.get('builtin') == 'Plane' or mesh.get('file') == 'Map6-Floor.mesh':
                    continue   # The floor's grid, which the game draws itself, and the baked Map6 floor
                if mesh.get('builtin') == 'Cube':
                    kind = 'Box'
                elif mesh.get('file') in MESHES:
                    kind = MESHES[mesh['file']][0]
                else:
                    self.check.that(False, '%s: a mesh this script has no rule for (%s)' % (p['path'], mesh))
                    continue
                position = tuple(p['position'])
                local, local_rotation, _, _ = place(p, held, position, rotation)
                source = p['colorSource'] or ''
                paint = source[len('palette:'):] if source.startswith('palette:') else None
                visual = {'path': p['path'], 'mesh': kind, 'scale': scale,
                          'position': local if held else position,
                          'rotation': local_rotation if held else rotation,
                          'world': position, 'worldRotation': rotation,
                          'paint': paint, 'own': tuple(p['baseColor'][:3])}
                if held:
                    visual[held[0]] = held[1]
                    visual['held'] = held[2]
                self.visuals.append(visual)

        for s in sorted(data['spawns'], key=lambda s: s['teamIndex']):
            if s['enabled'] and s['activeInHierarchy']:
                self.spawns.append({'team': s['teamIndex'], 'position': tuple(s['position']),
                                    'rotation': qnorm(s['rotation']), 'width': s['width']})

        self.verify()

    # -- Map6's floor

    def floor_boxes(self, collider_piece):
        """Map6's floor is a baked MudBun mesh: a slab less six box holes. Its
        brushes are still in the prefab, so the floor is built from them as
        boxes whose union is that shape."""
        slab, holes = None, []
        for p in self.data['pieces']:
            brush = behaviour(p, 'MudBox')
            if not brush:
                continue
            entry = (tuple(p['position']), qnorm(p['rotation']), tuple(p['scale']))
            if brush['m_operator'] == 0:
                slab = entry
            else:
                holes.append(entry)
        c, _, size = slab
        hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
        root2 = math.sqrt(2)
        slots = [h for h in holes if is_identity(h[1])]
        corners = [h for h in holes if not is_identity(h[1])]
        ok = len(slots) == 2 and len(corners) == 4 and is_identity(slab[1])
        self.check.that(ok, 'Map6 floor: not the slab, two slots and four corner holes this script knows')
        if not ok:
            return
        # Everything from here is about the slab's center
        xi = min(abs(h[0][0] - c[0]) - h[2][0] / 2 for h in slots)    # The slots' inner edge
        xo = max(abs(h[0][0] - c[0]) + h[2][0] / 2 for h in slots)    # ...and outer
        sl = max(abs(h[0][2] - c[2]) + h[2][2] / 2 for h in slots)    # Half their length
        # A corner hole leaves |x| + |z| <= k
        ks = []
        for position, rotation, scale in corners:
            dx, dz = position[0] - c[0], position[2] - c[2]
            inward = (-math.copysign(1 / root2, dx), 0.0, -math.copysign(1 / root2, dz))
            across = qrot(rotation, (1.0, 0.0, 0.0))
            self.check.that(abs(abs(sum(a * b for a, b in zip(across, inward))) - 1) < 1e-4,
                            'Map6 floor: a corner hole that does not cut its corner at 45 degrees')
            ks.append(abs(dx) + abs(dz) - scale[0] / 2 * root2)
        k = sum(ks) / 4
        self.check.that(max(ks) - min(ks) < 1e-4, 'Map6 floor: the corner holes are not alike')
        self.check.that(xo + sl < k < hx + hz and k - hz > xi, 'Map6 floor: the holes meet')

        boxes = []   # (center x, center z, half x, half z, turned 45 degrees)
        boxes.append((0, 0, xi, hz, False))                                     # Between the slots, end to end
        for s in (-1, 1):
            boxes.append((s * (xo + hx) / 2, 0, (hx - xo) / 2, k - hx, False))  # Outside a slot, to the edge
            boxes.append((0, s * (sl + hz) / 2, k - hz, (hz - sl) / 2, False))  # Past the slots' ends, to the edge
            zt = k - xo
            boxes.append((0, s * (sl + zt) / 2, xo, (zt - sl) / 2, False))      # Past the slots' ends, as wide as them
            xt = k - sl
            boxes.append((s * (xo + xt) / 2, 0, (xt - xo) / 2, sl, False))      # Outside a slot, as long as it
        # Along each cut corner, one turned 45 degrees: as deep as leaves a slot's corner alone
        depth = (k - (xo + sl)) / root2
        along = (hx + hz - k) * root2 / 2
        mid = ((k - hz + hx) / 2, (hz + k - hx) / 2)
        for sx in (-1, 1):
            for sz in (-1, 1):
                cx = sx * (mid[0] - depth / 2 / root2)
                cz = sz * (mid[1] - depth / 2 / root2)
                # Turned 45 degrees about y, a box's z axis lies along (1, 0, 1) and its x along (1, 0, -1)
                half = (along, depth / 2) if sx == sz else (depth / 2, along)
                boxes.append((cx, cz, half[0], half[1], True))

        turn = axis_angle((0, 1, 0), 45)
        own = None
        for p in self.data['pieces']:
            if p['mesh'] and p['mesh'].get('file') == 'Map6-Floor.mesh':
                own = tuple(p['baseColor'][:3])
        for cx, cz, bx, bz, turned in boxes:
            position = (c[0] + cx, c[1], c[2] + cz)
            rotation = turn if turned else IDENTITY
            half = (bx, hy, bz)
            self.pieces.append({'path': collider_piece['path'], 'position': position, 'rotation': rotation,
                                'half': half, 'solid': True, 'damageFloor': False, 'ignoreRaycast': False,
                                'note': None})
            self.visuals.append({'path': collider_piece['path'], 'mesh': 'Box', 'scale': scaled(half, 2),
                                 'position': position, 'rotation': rotation, 'world': position,
                                 'worldRotation': rotation, 'paint': None, 'own': own})
        self.expected_pieces += len(boxes)

        # Measure it against the slab less the holes, at the points of a 0.1 m grid
        def inside(box, x, z):
            position, rotation, scale = box
            p = qrot(qinv(rotation), (x - position[0], 0.0, z - position[2]))
            return min(scale[0] / 2 - abs(p[0]), scale[2] / 2 - abs(p[2]))   # > 0 inside, by this much

        def built(x, z):
            for cx, cz, bx, bz, turned in boxes:
                p = (x - c[0] - cx, 0.0, z - c[2] - cz)
                if turned:
                    p = qrot(qinv(turn), p)
                if abs(p[0]) <= bx and abs(p[2]) <= bz:
                    return True
            return False

        # (the grid is off the slab's own by a little, so no sample is on an edge)
        step, margin = 0.1, 1.0137
        area, wrong, deepest = 0, 0, 0.0
        nx = int(round((2 * hx + 2 * margin) / step))
        nz = int(round((2 * hz + 2 * margin) / step))
        for i in range(nx):
            x = c[0] - hx - margin + (i + 0.5) * step
            for j in range(nz):
                z = c[2] - hz - margin + (j + 0.5) * step
                into = max([inside(h, x, z) for h in holes])
                exact = inside(slab, x, z) > 0 and into <= 0
                area += exact
                if built(x, z) != exact:
                    wrong += 1
                    if not exact:
                        # Solid where there is none: how far inside a hole, or outside the slab
                        deepest = max(deepest, into if into > 0 else -inside(slab, x, z))
        self.floor = {'boxes': len(boxes), 'area': area * step * step, 'wrong': wrong * step * step,
                      'share': wrong / max(1, area), 'deepest': deepest, 'top': c[1] + hy,
                      'k': k, 'xi': xi, 'xo': xo, 'sl': sl, 'depth': depth, 'center': c, 'half': (hx, hy, hz)}
        self.check.that(wrong / max(1, area) < 0.01, 'Map6 floor: %.2f%% of it is wrong' % (100 * wrong / max(1, area)))
        self.check.that(deepest <= 0.15, 'Map6 floor: solid %.3f m into a hole' % deepest)

    # -- checks

    def verify(self):
        check, name = self.check, self.name
        check.that(len(self.pieces) == self.expected_pieces,
                   '%s: %d pieces for %d colliders' % (name, len(self.pieces), self.expected_pieces))
        check.that([s['team'] for s in self.spawns] == [0, 1], '%s: its spawn lines are not teams 0 and 1' % name)
        for p in self.pieces:
            check.that(abs(length(p['half']) - self.radius(p)) < 1e-9, 'radius')
            where = '%s: %s' % (name, p['path'])
            if 'mover' in p:
                m = self.movers[p['mover']]
                if m['kind'] == 'Slide':
                    at, turn = m['origin'], IDENTITY
                elif m['kind'] == 'Lift':
                    start = lerp(m['heightMin'], m['heightMax'],
                                 inverse_lerp(m['angleMax'], m['angleMin'], m['angleStart']))
                    at, turn = add(m['origin'], (0.0, start, 0.0)), IDENTITY
                else:
                    at, turn = m['origin'], hinge_rotation(m['kind'], m['angleStart'])
            elif 'block' in p:
                b = self.blocks[p['block']]
                at, turn = b['origin'], b['rotation']
            else:
                continue
            check.that(dist(add(at, qrot(turn, p['local'])), p['position']) < 1e-5 and
                       qangle(qmul(turn, p['localRotation']), p['rotation']) < 1e-6,
                       where + ': its place from its mover is not its rest pose')
        for v in self.visuals:
            if 'block' in v:
                b = self.blocks[v['block']]
                at, turn = b['origin'], b['rotation']
            elif v.get('held') == 'Slide':
                at, turn = self.movers[v['mover']]['origin'], IDENTITY
            else:
                continue
            check.that(dist(add(at, qrot(turn, v['position'])), v['world']) < 1e-5 and
                       qangle(qmul(turn, v['rotation']), v['worldRotation']) < 1e-6,
                       '%s: %s: what is drawn is not where the file has it' % (name, v['path']))
        for m in self.movers:
            if m['kind'] in ('HingeB', 'Lift'):
                check.that(self.movers[m['link']]['kind'] == 'HingeA', '%s: a link that is not to a HingeA' % name)

    @staticmethod
    def radius(piece): return length(piece['half'])

    # -- text

    def write_match(self, out):
        out.append('// %s' % self.name)
        out.append('void BuildMap%d(mut List<Piece> pieces, mut List<Mover> movers, mut List<Block> blocks, mut List<SpawnLine> spawns)' % self.index)
        out.append('{')
        for i, m in enumerate(self.movers):
            fields = [('kind', 'MoverKind.' + m['kind']), ('origin', f3(m['origin']))]
            if m['kind'] == 'Slide':
                fields += [('travel', f3(m['travel'])), ('speedMin', num(m['speedMin'])), ('speedMax', num(m['speedMax']))]
                if m['randomStart']:
                    fields.append(('randomStart', 'true'))
            else:
                if m['kind'] == 'HingeA':
                    fields += [('angleMin', num(m['angleMin'])), ('angleMax', num(m['angleMax'])),
                               ('angleStart', num(m['angleStart'])), ('angleSpeed', num(m['angleSpeed']))]
                if m['kind'] == 'Lift':
                    fields += [('heightMin', num(m['heightMin'])), ('heightMax', num(m['heightMax']))]
                if m['link'] >= 0:
                    fields.append(('link', str(m['link'])))
            out.append('    // %d: %s' % (i, m['path']))
            out.append('    movers.Add(%s);' % literal('Mover', fields))
        for i, b in enumerate(self.blocks):
            fields = [('origin', f3(b['origin']))]
            if not is_identity(b['rotation']):
                fields.append(('originRotation', quat(b['rotation'])))
            fields += [('mass', num(b['mass'])), ('damping', num(b['damping'])), ('knockback', num(b['knockback']))]
            out.append('    // %d: %s' % (i, b['path']))
            out.append('    blocks.Add(%s);' % literal('Block', fields))
        last = None
        for p in self.pieces:
            if p['path'] != last:
                out.append('    // %s' % p['path'])
                last = p['path']
            if p.get('note'):
                out.append('    // (%s)' % p['note'])
            fields = [('position', f3(p['position']))]
            if not is_identity(p['rotation']):
                fields.append(('rotation', quat(p['rotation'])))
            fields.append(('half', f3(p['half'])))
            if not p['solid']:
                fields.append(('solid', 'false'))
            if p.get('jumpPad'):
                fields += [('jumpPad', 'true'), ('force', f3(p['force']))]
            if p['damageFloor']:
                fields.append(('damageFloor', 'true'))
            if p['ignoreRaycast']:
                fields.append(('ignoreRaycast', 'true'))
            for held in ('mover', 'block'):
                if held in p:
                    fields += [(held, str(p[held])), ('local', f3(p['local']))]
                    if not is_identity(p['localRotation']):
                        fields.append(('localRotation', quat(p['localRotation'])))
            fields.append(('radius', num(self.radius(p))))
            out.append('    pieces.Add(%s);' % literal('Piece', fields))
        for s in self.spawns:
            fields = [('team', str(s['team'])), ('position', f3(s['position']))]
            if not is_identity(s['rotation']):
                fields.append(('rotation', quat(s['rotation'])))
            fields.append(('width', num(s['width'])))
            out.append('    spawns.Add(%s);' % literal('SpawnLine', fields))
        out.append('}')
        out.append('')

    def write_visuals(self, out):
        out.append('void BuildMapVisuals%d(mut List<Visual> visuals)' % self.index)
        out.append('{')
        last = None
        for v in self.visuals:
            if v['path'] != last:
                out.append('    // %s' % v['path'])
                last = v['path']
            fields = [('mesh', 'BlockMesh.' + v['mesh']), ('position', f3(v['position']))]
            if not is_identity(v['rotation']):
                fields.append(('rotation', quat(v['rotation'])))
            if max(abs(s - 1) for s in v['scale']) > 1e-7:
                fields.append(('scale', f3(v['scale'])))
            if v['paint']:
                fields.append(('paint', 'Paint.' + v['paint'].capitalize()))
            else:
                fields += [('paint', 'Paint.Own'), ('own', color(v['own']))]
            for held in ('mover', 'block'):
                if held in v:
                    fields.append((held, str(v[held])))
            out.append('    visuals.Add(%s);' % literal('Visual', fields))
        out.append('}')
        out.append('')

    def write_paint(self, out):
        palette = self.data['visuals']['palette']['colors']
        out.append('Color MapPaint%d(Paint paint)' % self.index)
        out.append('{')
        out.append('    switch (paint)')
        out.append('    {')
        for name in PAINTS:
            out.append('        case Paint.%s: return %s;' % (name.capitalize(), color(palette[name])))
        out.append('    }')
        out.append('    return Color.white;')
        out.append('}')
        out.append('')


def switch(out, signature, call, fallback, maps, returns=True):
    out.append(signature)
    out.append('{')
    out.append('    switch (map)')
    out.append('    {')
    for m in maps:
        text = call(m)
        out.append('        case %d: %s' % (m.index, ('return %s;' % text) if returns else ('%s; break;' % text)))
    out.append('    }')
    if returns:
        out.append('    return %s;' % fallback)
    out.append('}')
    out.append('')


def write_tide(maps):
    out = [
        '// GENERATED by tools/make_maps.py from the Unity game\'s map prefabs: do not edit.',
        '// To make it again: python tools/make_maps.py <the extracted maps folder>',
        '// (Map1.json ... Map10.json and meshes/*.obj; it writes this file and art/blocks/*.obj).',
        '//',
        '// The rotation\'s seven maps, as state.tide has a map: a piece for each',
        '// BoxCollider, what moves them, the blocks that can be pushed, the spawn',
        '// lines, and what is drawn. Positions and rotations are Unity\'s as they are.',
        '// A piece or a visual on a mover or a block is placed from it:',
        '//   Slide and Lift: the mover\'s position + local, turned as localRotation says',
        '//   HingeA, HingeB: pivot + the half\'s rotation * local, that rotation * localRotation',
        '//   a block: its position + its rotation * local, its rotation * localRotation',
        '// A visual has no local fields: on a mover or a block, its position and',
        '// rotation are those. A piece\'s position and rotation are where it starts',
        '// (a bridge\'s at its starting angle).',
        '',
        '// Fills what the match needs of a map (lists are appended to; the caller clears them).',
    ]
    switch(out, 'void BuildMap(int map, mut List<Piece> pieces, mut List<Mover> movers, mut List<Block> blocks, mut List<SpawnLine> spawns)',
           lambda m: 'BuildMap%d(pieces, movers, blocks, spawns)' % m.index, None, maps, returns=False)
    out.append('// Fills what is drawn of a map.')
    switch(out, 'void BuildMapVisuals(int map, mut List<Visual> visuals)',
           lambda m: 'BuildMapVisuals%d(visuals)' % m.index, None, maps, returns=False)
    out.append('// The map\'s palette color for a paint (Paint.Own gives white).')
    switch(out, 'Color MapPaint(int map, Paint paint)', lambda m: 'MapPaint%d(paint)' % m.index, 'Color.white', maps)
    switch(out, 'string MapName(int map)', lambda m: '"%s"' % m.name, '""', maps)
    out += ['// Which sky a map has: its place in the rotation.', 'int MapSky(int map)', '{', '    return map;', '}', '']
    out.append('// A map\'s light: the color its light override has, and that light\'s temperature and intensity.')
    light = lambda m: m.data['visuals']['lightOverride']
    switch(out, 'Color MapLight(int map)', lambda m: color(light(m)['color']), 'Color.white', maps)
    switch(out, 'float MapKelvin(int map)', lambda m: num(light(m)['kelvin']), '5000', maps)
    switch(out, 'float MapIntensity(int map)', lambda m: num(light(m)['intensity']), '1', maps)
    for m in maps:
        m.write_match(out)
        m.write_visuals(out)
        m.write_paint(out)
    while out[-1] == '':
        out.pop()
    return '\n'.join(out) + '\n'


# ---------------------------------------------------------------- meshes

def convert_obj(text, source):
    """Tide reads an OBJ as right-handed: it turns x around and winds each
    triangle the other way. These files hold Unity's own left-handed vertices,
    so both are done here first, and what Tide ends up with is Unity's mesh."""
    out = ['# GENERATED by tools/make_maps.py from "%s": the Unity mesh with x negated' % source,
           '# (positions and normals) and each face\'s corners in reverse, which Tide\'s',
           '# OBJ import undoes.']
    counts = {'v': 0, 'vn': 0, 'vt': 0, 'f': 0}
    for line in text.splitlines():
        parts = line.split()
        if not parts or parts[0] not in counts:
            continue
        counts[parts[0]] += 1
        if parts[0] in ('v', 'vn'):
            x = -float(parts[1])
            out.append('%s %s %s %s' % (parts[0], '0' if x == 0 else repr(x), parts[2], parts[3]))
        elif parts[0] == 'vt':
            out.append(line.strip())
        else:
            out.append('f ' + ' '.join(reversed(parts[1:])))
    return '\n'.join(out) + '\n', counts


def signed_volume(text):
    vertices, volume = [], 0.0
    for line in text.splitlines():
        parts = line.split()
        if parts[:1] == ['v']:
            vertices.append(tuple(float(x) for x in parts[1:4]))
        elif parts[:1] == ['f']:
            a, b, c = (vertices[int(corner.split('/')[0]) - 1] for corner in parts[1:4])
            volume += (a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) +
                       a[2] * (b[0] * c[1] - b[1] * c[0])) / 6
    return volume


# ---------------------------------------------------------------- main

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    only_check = '--check' in sys.argv[1:]
    if len(args) != 1:
        sys.exit(__doc__)
    folder = args[0]
    game = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    check = Check()
    maps = []
    for index, name in enumerate(ROTATION):
        with open(os.path.join(folder, name + '.json'), encoding='utf-8') as f:
            maps.append(Map(index, json.load(f), check))

    objs = {}
    for source, (_, name) in MESHES.items():
        source = source[:-len('.fbx')] + '.obj'
        with open(os.path.join(folder, 'meshes', source), encoding='utf-8') as f:
            original = f.read()
        text, counts = convert_obj(original, source)
        # Unity's mesh has cross(b - a, c - a) pointing out; mirrored and rewound, it still does
        before, after = signed_volume(original), signed_volume(text)
        check.that(before > 0 and abs(after - before) < 1e-6 * abs(before), '%s: its faces do not point out' % source)
        objs[name + '.obj'] = (text, counts)

    print('%-6s %7s %7s %7s %8s' % ('map', 'pieces', 'movers', 'blocks', 'visuals'))
    for m in maps:
        print('%-6s %7d %7d %7d %8d' % (m.name, len(m.pieces), len(m.movers), len(m.blocks), len(m.visuals)))
        if m.floor:
            f = m.floor
            print('       floor: %d boxes, top at y = %s, %.2f m2; %.2f m2 differ (%.3f%%), solid at most %.3f m into a hole'
                  % (f['boxes'], num(f['top']), f['area'], f['wrong'], 100 * f['share'], f['deepest']))
    print('%d checks, %d failed' % (check.count, len(check.failures)))
    for failure in check.failures[:40]:
        print('  FAILED ' + failure)
    if check.failures:
        sys.exit(1)
    if only_check:
        return

    with open(os.path.join(game, 'maps.tide'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(write_tide(maps))
    blocks = os.path.join(game, 'art', 'blocks')
    os.makedirs(blocks, exist_ok=True)
    for name, (text, counts) in objs.items():
        with open(os.path.join(blocks, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    print('wrote maps.tide and %d meshes in art/blocks' % len(objs))


if __name__ == '__main__':
    main()
