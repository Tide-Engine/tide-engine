# Shared helpers for the Crawlmaster ("crawler") scripts: rest-pose math for procedural posing, two-bone IK,
# the 128 px skin, and headless Workbench rendering. Imported by make_crawler.py and preview_crawler.py, which
# put this folder on sys.path themselves. Runs inside Blender 4.4 (bpy, mathutils, numpy).
import math

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector


# ---------------------------------------------------------------------------------------------------------------
# Posing. Rotations are worked out in armature space (Blender: +X the creature's left, -Y its front, +Z up) and
# turned into each pose bone's local quaternion. A bone's "delta" D is how its rest orientation is turned in the
# pose (posed = D * rest). Specs, by bone name:
#   ('D', q)                  the bone's delta outright (for the root)
#   ('A', q)                  turn q, in rest armature axes, relative to the parent: D = D_parent * q
#   ('aim', dir, ref, twist)  swing the bone so `ref` (its tail, or ('head', child)) points along world `dir`,
#                             then twist about `dir` by `twist` radians
#   ('ik', target, pole, mid, end, twist)  two-bone IK: this bone and `mid` so that `end`'s head reaches `target`,
#                             bending toward world direction `pole`
# Bones without a spec keep their parent's delta (they don't move against it).
# ---------------------------------------------------------------------------------------------------------------

def quat(axis, degrees):
    return Quaternion(Vector(axis).normalized(), math.radians(degrees))


class Rig:
    def __init__(self, arm):
        self.arm = arm
        self.head, self.tail, self.rest, self.parent = {}, {}, {}, {}
        for b in arm.data.bones:
            self.head[b.name] = b.head_local.copy()
            self.tail[b.name] = b.tail_local.copy()
            self.rest[b.name] = b.matrix_local.to_quaternion()
            self.parent[b.name] = b.parent.name if b.parent else None
        self.order = []
        seen = set()

        def visit(n):
            if n in seen:
                return
            if self.parent[n]:
                visit(self.parent[n])
            seen.add(n)
            self.order.append(n)

        for n in self.head:
            visit(n)

    def ref(self, name, ref):
        if ref is None or ref == 'tail':
            return self.tail[name] - self.head[name]
        kind, child = ref
        return self.head[child] - self.head[name]

    def evaluate(self, spec, root_offset=Vector()):
        """Returns ({bone: (local quaternion, local location)}, deltas, posed heads)."""
        D, H, out, pending = {}, {}, {}, {}
        for n in self.order:
            p = self.parent[n]
            if p is None:
                Dp = Quaternion()
                H[n] = self.head[n] + root_offset
            else:
                Dp = D[p]
                H[n] = H[p] + Dp @ (self.head[n] - self.head[p])
            s = pending.pop(n, None) or spec.get(n)
            if s is None:
                Dn = Dp.copy()
            elif s[0] == 'D':
                Dn = s[1].copy()
            elif s[0] == 'A':
                Dn = Dp @ s[1]
            elif s[0] == 'aim':
                _, direction, ref, twist = s
                v0 = Dp @ self.ref(n, ref)
                Dn = v0.rotation_difference(direction) @ Dp
                if twist:
                    Dn = Quaternion(direction.normalized(), twist) @ Dn
            elif s[0] == 'ik':
                _, target, pole, mid, end, twist = s
                L1 = (self.head[mid] - self.head[n]).length
                L2 = (self.head[end] - self.head[mid]).length
                knee = two_bone(H[n], target, L1, L2, pole)
                v0 = Dp @ (self.head[mid] - self.head[n])
                Dn = v0.rotation_difference(knee - H[n]) @ Dp
                if twist:
                    Dn = Quaternion((knee - H[n]).normalized(), twist) @ Dn
                pending[mid] = ('aim', target - knee, ('head', end), 0.0)
            else:
                raise ValueError(s)
            D[n] = Dn
            R = self.rest[n]
            q = R.inverted() @ Dp.inverted() @ Dn @ R
            q.normalize()
            loc = R.inverted() @ (H[n] - self.head[n]) if p is None else Vector()
            out[n] = (q, loc)
        return out, D, H

    def tip(self, D, H, name):
        """Posed tail of a bone."""
        return H[name] + D[name] @ (self.tail[name] - self.head[name])


def two_bone(h, t, L1, L2, pole):
    d = t - h
    dist = max(abs(L1 - L2) + 1e-4, min(L1 + L2 - 1e-4, d.length))
    u = d.normalized()
    a = (L1 * L1 - L2 * L2 + dist * dist) / (2 * dist)
    hh = math.sqrt(max(L1 * L1 - a * a, 0.0))
    v = pole - u * pole.dot(u)
    v.normalize()
    return h + u * a + v * hh


def apply_pose(arm, posed):
    for n, (q, loc) in posed.items():
        pb = arm.pose.bones[n]
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = q
        pb.location = loc
        pb.scale = (1, 1, 1)


def key_pose(arm, posed, frame, previous):
    """Keys every bone's rotation (and location) at `frame`, keeping quaternions in one hemisphere per bone."""
    for n, (q, loc) in posed.items():
        pb = arm.pose.bones[n]
        q = q.copy()
        if n in previous and previous[n].dot(q) < 0:
            q.negate()
        previous[n] = q
        pb.rotation_quaternion = q
        pb.location = loc
        pb.keyframe_insert('rotation_quaternion', frame=frame, group=n)
        pb.keyframe_insert('location', frame=frame, group=n)


# ---------------------------------------------------------------------------------------------------------------
# Skin
# ---------------------------------------------------------------------------------------------------------------

def load_pixels(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    a = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, img.channels)
    bpy.data.images.remove(img)
    return a  # rows bottom to top, values as stored (sRGB bytes / 255)


def save_pixels(a, path):
    """Writes rows bottom to top (as load_pixels reads them) of RGB or RGBA floats to a PNG."""
    h, w, c = a.shape
    if c == 3:
        a = np.concatenate([a, np.ones((h, w, 1), dtype=np.float32)], axis=2)
    img = bpy.data.images.new("out", w, h, alpha=False)
    img.pixels = np.clip(a, 0, 1).astype(np.float32).ravel()
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


def box_down(a, f):
    h, w, c = a.shape
    return a.reshape(h // f, f, w // f, f, c).mean(axis=(1, 3))


def srgb_to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def build_skin(base_path, detail_path, mask_path, out_path, size=128, detail_strength=1.0, contrast=1.15,
               lift=1.0, levels=32):
    """The Crawlmaster's Unity Standard material flattened into one small texture:
    albedo = base * lerp(1, detail(uv * 2) * 4.59 (linear-space MULX2), mask), then downscaled with an area filter,
    contrast raised a little and quantized to `levels` per channel."""
    base = load_pixels(base_path)[..., :3]
    H, W, _ = base.shape
    detail = load_pixels(detail_path)[..., :3]
    dh, dw, _ = detail.shape
    # the detail tiles twice across the uv square
    detail = box_down(detail, 2) if dh == H else detail
    detail = np.tile(detail, (2, 2, 1))[:H, :W]
    mask = load_pixels(mask_path)
    mask = mask[..., 0:1]  # grey; the file's alpha is solid
    f = H // mask.shape[0]
    mask = np.repeat(np.repeat(mask, f, axis=0), f, axis=1)
    mask = mask * detail_strength
    bl = srgb_to_linear(base)
    dl = srgb_to_linear(detail) * 4.59479380
    albedo = bl * (1.0 - mask + mask * dl)
    small = box_down(albedo, H // size)
    small = linear_to_srgb(small * lift)
    lum = small.mean(axis=2, keepdims=True)
    mean = lum.mean()
    small = np.clip((small - mean) * contrast + mean, 0.0, 1.0)
    if levels:
        small = np.round(small * (levels - 1)) / (levels - 1)
    img = bpy.data.images.new("crawler_skin", size, size, alpha=False)
    rgba = np.concatenate([small, np.ones((size, size, 1), dtype=np.float32)], axis=2)
    img.pixels = rgba.astype(np.float32).ravel()
    img.filepath_raw = out_path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


# ---------------------------------------------------------------------------------------------------------------
# Rendering (Workbench, no window)
# ---------------------------------------------------------------------------------------------------------------

def skin_material(png_path, name="crawler_skin"):
    img = bpy.data.images.load(png_path, check_existing=False)
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    node = nt.nodes.new('ShaderNodeTexImage')
    node.image = img
    node.interpolation = 'Closest'
    nt.links.new(node.outputs['Color'], nt.nodes['Principled BSDF'].inputs['Base Color'])
    return mat


def setup_render(scene, res=512, background=(0.16, 0.16, 0.17)):
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'TEXTURE'
    scene.display.shading.show_cavity = False
    scene.render.resolution_x = res
    scene.render.resolution_y = res
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.display_settings.display_device = 'sRGB'
    scene.view_settings.view_transform = 'Standard'
    if scene.world is None:
        scene.world = bpy.data.worlds.new("bg")
    scene.world.color = background
    scene.display.shading.background_type = 'WORLD'


def ortho_camera(scene, ortho_scale):
    cam_data = bpy.data.cameras.new("preview_cam")
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = ortho_scale
    cam = bpy.data.objects.new("preview_cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    return cam


def aim_camera(cam, center, direction, distance=8.0):
    d = Vector(direction).normalized()
    cam.location = Vector(center) + d * distance
    up = 'Y' if abs(d.z) < 0.99 else 'Y'
    cam.rotation_euler = (Vector(center) - cam.location).to_track_quat('-Z', up).to_euler()


def ground_plane(scene, size=6.0, color=(0.35, 0.33, 0.30, 1.0)):
    me = bpy.data.meshes.new("ground")
    s = size / 2
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    ob = bpy.data.objects.new("ground", me)
    mat = bpy.data.materials.new("ground")
    mat.diffuse_color = color
    me.materials.append(mat)
    scene.collection.objects.link(ob)
    return ob
