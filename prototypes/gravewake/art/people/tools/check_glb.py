# Checks GRAVEWAKE's people files (plain Python 3, no Blender):
#
#   python check_glb.py <file.glb> [<file.glb> ...] [--tree]
#
# For each file: size, skeleton (bone count, the Body bone's rest height), meshes with their
# materials and the TEXCOORD_1 values found on each, and clips with their duration and channels.
# Then it compares the skeletons of all files: every file must have the same bone names with the
# same parents (rest positions may differ between the men's and the women's rig). --tree prints
# the hierarchy.
import json
import os
import struct
import sys

COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
FORMATS = {5126: "f", 5123: "H", 5121: "B", 5125: "I"}


def read_glb(path):
    data = open(path, "rb").read()
    jlen = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + jlen])
    off = 20 + jlen
    binary = data[off + 8:off + 8 + struct.unpack_from("<I", data, off)[0]] if off < len(data) else b""
    return gltf, binary


def values(gltf, binary, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    n, fmt = COMPONENTS[acc["type"]], FORMATS[acc["componentType"]]
    stride = view.get("byteStride", n * struct.calcsize(fmt))
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    return [struct.unpack_from("<" + fmt * n, binary, base + i * stride) for i in range(acc["count"])]


def skeleton(gltf):
    nodes = gltf["nodes"]
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    if gltf.get("skins"):
        joints = set(gltf["skins"][0]["joints"])
    else:  # a clip catalog: every node under the armature node
        arm = next(i for i, n in enumerate(nodes) if n.get("name") == "CharacterArmature")
        joints, todo = set(), list(nodes[arm].get("children", []))
        while todo:
            j = todo.pop()
            joints.add(j)
            todo += nodes[j].get("children", [])
    return {nodes[j]["name"]: (nodes[parent[j]]["name"] if j in parent else None, nodes[j]) for j in joints}


def world_y(gltf, name):
    """Rest height of a joint (translations summed up the chain; fine for these rigs at rest,
    whose upper chain carries no rotation that moves the hips)."""
    nodes = gltf["nodes"]
    parent = {c: i for i, n in enumerate(nodes) for c in n.get("children", [])}

    def mat(n):
        t = n.get("translation", [0, 0, 0])
        x, y, z, w = n.get("rotation", [0, 0, 0, 1])
        s = n.get("scale", [1, 1, 1])
        r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
             [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
             [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
        return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)] + [[0, 0, 0, 1]]

    def mul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

    i = next(k for k, n in enumerate(nodes) if n.get("name") == name)
    m = mat(nodes[i])
    while i in parent:
        i = parent[i]
        m = mul(mat(nodes[i]), m)
    return [m[0][3], m[1][3], m[2][3]]


def report(path, tree):
    gltf, binary = read_glb(path)
    sk = skeleton(gltf)
    print(f"== {os.path.basename(path)}: {os.path.getsize(path)} bytes, {len(sk)} bones, "
          f"Body (hips) rest {['%.4f' % v for v in world_y(gltf, 'Body')]}, "
          f"UpperLeg.L {['%.4f' % v for v in world_y(gltf, 'UpperLeg.L')]}")
    if tree:
        children = {}
        for name, (p, _) in sk.items():
            children.setdefault(p, []).append(name)

        def show(name, depth):
            print("   " + "  " * depth + name)
            for c in sorted(children.get(name, [])):
                show(c, depth + 1)

        for root in children.get("CharacterArmature", []) or children.get(None, []):
            show(root, 0)
    for mesh in gltf.get("meshes", []):
        for prim in mesh["primitives"]:
            attrs = prim["attributes"]
            mat = gltf["materials"][prim["material"]]["name"] if "material" in prim else "-"
            uv2 = sorted({tuple(round(x, 4) for x in v) for v in values(gltf, binary, attrs["TEXCOORD_1"])}) if "TEXCOORD_1" in attrs else None
            print(f"   mesh {mesh['name']:<12} {mat:<16} verts {gltf['accessors'][attrs['POSITION']]['count']:>5} "
                  f"attrs {','.join(sorted(attrs))} uv2 {uv2}")
    for anim in gltf.get("animations", []):
        paths = {}
        end = 0
        for ch in anim["channels"]:
            node = gltf["nodes"][ch["target"]["node"]]["name"]
            paths.setdefault(ch["target"]["path"], []).append(node)
            end = max(end, gltf["accessors"][anim["samplers"][ch["sampler"]]["input"]]["max"][0])
        desc = ", ".join(f"{p} {len(n)}" + (f" {n}" if p != "rotation" else "") for p, n in sorted(paths.items()))
        print(f"   clip {anim['name']:<14} {end:.3f} s  {desc}")
    return sk


def main():
    files = [a for a in sys.argv[1:] if not a.startswith("--")]
    tree = "--tree" in sys.argv
    skeletons = {f: report(f, tree) for f in files}
    first = files[0]
    ref = {n: p for n, (p, _) in skeletons[first].items()}
    ok = True
    for f in files[1:]:
        other = {n: p for n, (p, _) in skeletons[f].items()}
        if other != ref:
            ok = False
            print(f"!! {os.path.basename(f)} differs from {os.path.basename(first)}: "
                  f"missing {sorted(set(ref) - set(other))}, extra {sorted(set(other) - set(ref))}, "
                  f"reparented {sorted(n for n in ref if n in other and other[n] != ref[n])}")
    print("SKELETONS: same bone names and parents in all files" if ok else "SKELETONS DIFFER")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
