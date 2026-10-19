"""Helpers for make_spider.py that need no Blender: a small PNG writer, a .glb reader, writer and checker, the
source copy whose clips state every held channel (hold_channels), and the tidec import probe.

Runs inside Blender's Python (imported by make_spider.py) and on its own:
    python common_spider.py verify <spider.glb>
    python common_spider.py probe <tidec.exe> <dir holding spider.glb and spider.png> <scratch dir>
The probe writes a .tide that declares the model with a contract naming every clip and the eye-carrying
bone, plus its point-filtered skin, and compiles it: a missing clip or bone is a build error.
"""
import json
import os
import struct
import subprocess
import sys
import zlib

CLIPS = ["Idle", "Walk", "Attack", "Jump", "Death"]


def write_png(path, rows):
    """Writes an 8-bit RGB PNG. `rows` is a list of rows, top first, each a list of (r, g, b) ints."""
    h = len(rows)
    w = len(rows[0])
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for r, g, b in row:
            raw += bytes((r, g, b))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)


def read_glb(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != b"glTF":
        raise ValueError("not a .glb: " + path)
    _magic, _version, length = struct.unpack_from("<III", data, 0)
    off = 12
    js = None
    binbytes = b""
    while off < length:
        clen, ctype = struct.unpack_from("<II", data, off)
        chunk = data[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:
            js = json.loads(chunk.decode("utf-8"))
        elif ctype == 0x004E4942:
            binbytes = chunk
        off += 8 + clen
    return js, binbytes


def write_glb(path, g, binbytes):
    js = json.dumps(g, separators=(",", ":")).encode("utf-8")
    js += b" " * (-len(js) % 4)
    binbytes = bytes(binbytes) + b"\0" * (-len(binbytes) % 4)
    out = struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(binbytes), 0x004E4942) + binbytes
    with open(path, "wb") as fh:
        fh.write(struct.pack("<4sII", b"glTF", 2, 12 + len(out)) + out)


def hold_channels(src, dst, skip=()):
    """Writes a copy of the source .glb in which every clip animates every skin joint's translation, rotation and
    scale, and returns how many channels it added.

    glTF holds a path a clip has no channel for at the node's own TRS. Blender's importer rests the bones on the
    inverse binds and holds such a path there instead, which differs in this source: it would drop the abdomen's
    0.88 scale from every clip and Idle's 4.3 degree body roll (whose baked leg chains then end 6-10 cm off their
    feet). Constant channels with the node's values carry glTF's meaning through Blender."""
    g, binbytes = read_glb(src)
    blob = bytearray(binbytes)
    nodes = g["nodes"]
    joints = [j for skin in g.get("skins", []) for j in skin["joints"]]
    rest = {"translation": [0.0, 0.0, 0.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]}
    kinds = {"translation": "VEC3", "rotation": "VEC4", "scale": "VEC3"}

    def add(data, count, kind, lo=None, hi=None):
        blob.extend(b"\0" * (-len(blob) % 4))
        g["bufferViews"].append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)})
        blob.extend(data)
        acc = {"bufferView": len(g["bufferViews"]) - 1, "componentType": 5126, "count": count, "type": kind}
        if lo is not None:
            acc["min"], acc["max"] = lo, hi
        g["accessors"].append(acc)
        return len(g["accessors"]) - 1

    added = 0
    for anim in g.get("animations", []):
        have = {(c["target"].get("node"), c["target"]["path"]) for c in anim["channels"]}
        t0 = min(g["accessors"][s["input"]]["min"][0] for s in anim["samplers"])
        t1 = max(g["accessors"][s["input"]]["max"][0] for s in anim["samplers"])
        keys = [t0, t1] if t1 > t0 else [t0]
        times = None
        for j in joints:
            if nodes[j].get("name") in skip:
                continue
            assert "matrix" not in nodes[j], nodes[j].get("name")
            for path in ("translation", "rotation", "scale"):
                if (j, path) in have:
                    continue
                if times is None:
                    times = add(struct.pack("<%df" % len(keys), *keys), len(keys), "SCALAR", [keys[0]], [keys[-1]])
                value = [float(x) for x in nodes[j].get(path, rest[path])]
                out = add(struct.pack("<%df" % (len(value) * len(keys)), *(value * len(keys))), len(keys), kinds[path])
                anim["samplers"].append({"input": times, "output": out, "interpolation": "LINEAR"})
                anim["channels"].append({"sampler": len(anim["samplers"]) - 1, "target": {"node": j, "path": path}})
                have.add((j, path))
                added += 1
    g["buffers"][0]["byteLength"] = len(blob)
    write_glb(dst, g, blob)
    return added


_COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
_FORMATS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}


def accessor(g, binbytes, index):
    """The accessor's elements as a list of tuples (dequantized when normalized)."""
    a = g["accessors"][index]
    bv = g["bufferViews"][a["bufferView"]]
    n = _COMPONENTS[a["type"]]
    fmt = _FORMATS[a["componentType"]]
    size = struct.calcsize("<" + fmt)
    stride = bv.get("byteStride", n * size)
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    scale = None
    if a.get("normalized"):
        scale = {"B": 255.0, "H": 65535.0, "b": 127.0, "h": 32767.0}[fmt]
    out = []
    for i in range(a["count"]):
        v = struct.unpack_from("<%d%s" % (n, fmt), binbytes, base + i * stride)
        if scale:
            v = tuple(x / scale for x in v)
        out.append(v)
    return out


def verify_glb(path, clips=CLIPS):
    """Checks what FOLDLINE's importer needs and returns a summary; raises AssertionError on a problem."""
    g, binbytes = read_glb(path)
    problems = []
    nodes = g.get("nodes", [])
    if g.get("extensionsRequired"):
        problems.append("required extensions: %s" % g["extensionsRequired"])
    skins = g.get("skins", [])
    if len(skins) != 1:
        problems.append("expected one skin, found %d" % len(skins))
    joints = skins[0]["joints"] if skins else []
    joint_set = set(joints)
    names = [n.get("name") for n in nodes]
    joint_names = [names[j] for j in joints]
    if len(set(joint_names)) != len(joint_names) or None in joint_names:
        problems.append("joint names not unique or missing")
    if len(set(names)) != len(names):
        dup = sorted({n for n in names if names.count(n) > 1})
        problems.append("duplicate node names: %s" % dup)
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    # Every joint's parent chain stays inside the joints up to one root joint.
    roots = [j for j in joints if parent.get(j) not in joint_set]
    if len(roots) != 1:
        problems.append("joint hierarchy has %d roots: %s" % (len(roots), [names[r] for r in roots]))
    # Mesh: one mesh, its attributes, triangle count.
    meshes = g.get("meshes", [])
    tris = 0
    attrs = set()
    for m in meshes:
        for p in m["primitives"]:
            attrs.update(p["attributes"].keys())
            if p.get("targets"):
                problems.append("morph targets present")
            if p.get("mode", 4) != 4:
                problems.append("non-triangle primitive")
            tris += g["accessors"][p["indices"]]["count"] // 3
            for key in ("JOINTS_1", "WEIGHTS_1", "TEXCOORD_1", "COLOR_0"):
                if key in p["attributes"]:
                    problems.append("unexpected attribute " + key)
            uvs = accessor(g, binbytes, p["attributes"]["TEXCOORD_0"]) if "TEXCOORD_0" in p["attributes"] else []
            if not uvs:
                problems.append("no TEXCOORD_0")
            elif min(min(u) for u in uvs) < 0 or max(max(u) for u in uvs) > 1:
                problems.append("uvs outside 0..1")
            if "WEIGHTS_0" in p["attributes"]:
                for w in accessor(g, binbytes, p["attributes"]["WEIGHTS_0"]):
                    if abs(sum(w) - 1) > 0.02:
                        problems.append("weights not normalized")
                        break
    for need in ("POSITION", "NORMAL", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0"):
        if need not in attrs:
            problems.append("missing attribute " + need)
    # Animations: every channel targets a joint, names are the clean clip names.
    anims = g.get("animations", [])
    found = {}
    for a in anims:
        dur = 0.0
        for s in a["samplers"]:
            dur = max(dur, g["accessors"][s["input"]]["max"][0])
        bad = [c["target"].get("node") for c in a["channels"] if c["target"].get("node") not in joint_set]
        if bad:
            problems.append("%s: channels target non-joint nodes %s" % (a.get("name"), [names[b] if b is not None else None for b in bad]))
        paths = sorted({c["target"]["path"] for c in a["channels"]})
        if "weights" in paths:
            problems.append("%s: morph weight channels" % a.get("name"))
        found[a.get("name")] = {"duration": round(dur, 4), "channels": len(a["channels"]), "paths": paths,
                                "nodes": len({c["target"]["node"] for c in a["channels"]})}
    if sorted(found) != sorted(clips):
        problems.append("clips %s, expected %s" % (sorted(found), sorted(clips)))
    summary = {
        "triangles": tris,
        "joints": len(joints),
        "joint_names": joint_names,
        "root_joint": names[roots[0]] if len(roots) == 1 else None,
        "clips": found,
        "attributes": sorted(attrs),
        "nodes": len(nodes),
        "materials": len(g.get("materials", [])),
        "images": len(g.get("images", [])),
        "problems": problems,
    }
    return summary


PROBE = """// Import probe for FOLDLINE's spider: the contract makes every clip and bone below a build check.
struct StoneVertex
{{
    float3 position;
    float3 normal;
    float2 uv;
}}

struct SpiderParts
{{
    Animation idle;
    Animation walk;
    Animation attack;
    Animation jump;
    Animation death;
    Bone head;
    Bone body;
    Bone abdomen;
}}

asset SkinnedMesh<StoneVertex, SpiderParts> spider = "{glb}";
[Filter(Filter.Point)] asset Texture skinSpider = "{png}";

component Crawler {{ float seconds; }}

scene Main {{ }}

event(Spawned) Setup(with Main)
{{
    Spawn(Crawler {{ seconds = 0.5 }});
}}

view Crawlers(Crawler crawler)
{{
    mut var pose = spider.animations.idle.Sample(crawler.seconds, Playback.Loop);
    pose.Blend(spider.animations.walk.Sample(crawler.seconds, Playback.Loop), 0.5);
    pose.Blend(spider.animations.attack.Sample(crawler.seconds), 0.25);
    pose.Blend(spider.animations.jump.Sample(crawler.seconds), 0.25);
    pose.Blend(spider.animations.death.Sample(crawler.seconds), 0.25);
    var head = pose.Model(spider.skeleton.bones.head);
    Draw.SkinnedMesh(spider, head, pose, skinSpider);
}}
"""


def probe(tidec, asset_dir, scratch):
    os.makedirs(scratch, exist_ok=True)
    rel = os.path.relpath(asset_dir, scratch).replace("\\", "/")
    tide = os.path.join(scratch, "spider_probe.tide")
    with open(tide, "w", encoding="utf-8") as fh:
        fh.write(PROBE.format(glb=rel + "/spider.glb", png=rel + "/spider.png"))
    out = os.path.join(scratch, "out")
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, TIDE_HIDDEN="1")
    r = subprocess.run([tidec, tide, "-o", out, "--game", scratch], capture_output=True, text=True, env=env)
    print("tidec exit", r.returncode)
    if r.stdout.strip():
        print(r.stdout.strip())
    if r.stderr.strip():
        print(r.stderr.strip())
    for f in sorted(os.listdir(out)):
        print("  wrote", f, os.path.getsize(os.path.join(out, f)), "bytes")
    return r.returncode


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "verify":
        s = verify_glb(sys.argv[2])
        print(json.dumps(s, indent=1))
        sys.exit(1 if s["problems"] else 0)
    if len(sys.argv) >= 5 and sys.argv[1] == "probe":
        sys.exit(probe(sys.argv[2], sys.argv[3], sys.argv[4]))
    print(__doc__)
    sys.exit(2)
