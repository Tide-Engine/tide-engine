# GRAVEWAKE's clip catalogs: moves.glb on the men's skeleton and moves_women.glb on the women's,
# the same clips with the same names, lengths and timing in both.
#
#   blender -b --factory-startup --python make_moves.py -- --men <Farmer.gltf> --women <Medieval.gltf> --ual1 <AnimationLibrary_Godot_Standard.glb> --ual2 <UAL2_Standard.glb> --out <dir>
#
# Sources (all CC0 1.0, Quaternius):
# - the Ultimate Modular Men pack's own clips (any man of the pack: they share the rig and clips),
#   https://quaternius.com/packs/ultimatemodularcharacters.html;
# - Universal Animation Library 1 (Godot, "DEF-" bone names), https://quaternius.com/packs/universalanimationlibrary.html;
# - Universal Animation Library 2 (Unreal-Godot, UE mannequin bone names), https://quaternius.com/packs/universalanimationlibrary2.html.
# The women's skeleton comes from the Ultimate Modular Women pack (https://quaternius.com/packs/ultimatemodularwomen.html);
# its own clips aren't used, so men and women play the same motions.
#
# Every clip is retargeted the same way, men's clips included (they're re-solved for the FK feet):
# each frame, each mapped bone's world-space rotation delta from the source's reference pose is
# applied to the target's rest (target_world = source_world(frame) * source_world(ref)^-1 *
# target_rest_world), then turned into local pose rotations; bones without a source follow their
# parent. The reference is the UAL clip A_TPose for UAL, the rest pose for the men's clips; the
# targets' rest is a T-pose like A_TPose. The hip-joint midpoint moves as the source's, scaled by
# the ratio of hip-joint heights, then the hips are moved up or down so the lower ankle is at the
# source's height for it (scaled; less so as both feet leave the floor): feet stay on the floor
# whatever the leg proportions. Positions
# are root-relative, so root motion is dropped (all clips are in place). Loops end on their first
# pose. Keys are the sources' 30 fps.
#
# Each file is the armature only (no mesh), with a rotation channel on every bone, a translation
# channel on Body (the hips bone; Root never moves) and no scale channels (gw_rig.prune_glb).
# It prints a table of clips with durations and strides, which README.md records.
import math
import os
import statistics
import sys

import bpy
from mathutils import Quaternion, Vector

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gw_rig  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name):
    return argv[argv.index(name) + 1]


MEN, WOMEN, UAL1, UAL2, OUT = arg("--men"), arg("--women"), arg("--ual1"), arg("--ual2"), arg("--out")
DT = 1.0 / gw_rig.FPS

# (name in the catalog, source, source clip, loops)
CLIPS = [
    ("Idle", "men", "Idle", True),
    ("Walk", "men", "Walk", True),
    ("Run", "men", "Run", True),
    ("Idle_Sword", "men", "Idle_Sword", True),
    ("Sword_Slash", "men", "Sword_Slash", False),
    ("Idle_Gun", "men", "Idle_Gun", True),
    ("Gun_Shoot", "men", "Gun_Shoot", False),
    ("Punch_Right", "men", "Punch_Right", False),
    ("Punch_Left", "men", "Punch_Left", False),
    ("HitRecieve", "men", "HitRecieve", False),
    ("HitRecieve_2", "men", "HitRecieve_2", False),
    ("Death", "men", "Death", False),
    ("Interact", "men", "Interact", False),
    ("Roll", "men", "Roll", False),
    ("Z_Idle", "ual2", "Zombie_Idle_Loop", True),
    ("Z_Walk", "ual2", "Zombie_Walk_Fwd_Loop", True),
    ("Z_Scratch", "ual2", "Zombie_Scratch", False),
    ("Z_Knockback", "ual2", "Hit_Knockback", False),
    ("Z_Climb", "ual2", "ClimbUp_1m_RM", False),
    ("Sprint", "ual1", "Sprint_Loop", True),
    ("Death_B", "ual1", "Death01", False),
    ("Hit_Head", "ual1", "Hit_Head", False),
    ("Hit_Chest", "ual1", "Hit_Chest", False),
    ("Crouch_Idle", "ual1", "Crouch_Idle_Loop", True),
    ("Crouch_Walk", "ual1", "Crouch_Fwd_Loop", True),
]
LOCOMOTION = {"Walk", "Run", "Z_Walk", "Sprint", "Crouch_Walk"}
# Joints that mustn't go deeper into the floor than the source's (scaled) when the body is down.
UPPER = ["Abdomen", "Torso", "Chest", "Neck", "Head"] + [f"{b}.{s}" for b in ("Shoulder", "UpperArm", "LowerArm", "Wrist") for s in ("L", "R")]


def ual_map(spine, limb, finger, thumb):
    """Target bone -> source bone. Index1..Pinky1 (the palm) follow the wrist: UAL's hand bone holds the palm."""
    m = dict(zip(("Body", "Abdomen", "Torso", "Chest", "Neck", "Head"), spine))
    for side in ("L", "R"):
        for target, source in zip(("Shoulder", "UpperArm", "LowerArm", "Wrist", "UpperLeg", "LowerLeg", "Foot"), limb):
            m[f"{target}.{side}"] = source(side)
        for f in ("Index", "Middle", "Ring", "Pinky"):
            for k in (1, 2, 3):
                m[f"{f}{k + 1}.{side}"] = finger(f.lower(), k, side)
        for k in (1, 2, 3):
            m[f"Thumb{k}.{side}"] = thumb(k, side)
    return m


def lr(fmt):
    return lambda side: fmt.format(s=side.lower())


def LR(fmt):
    return lambda side: fmt.format(s=side)


SOURCES = {
    "ual2": {"file": UAL2, "root": "root", "thighs": ("thigh_l", "thigh_r"), "shins": ("calf_l", "calf_r"), "feet": ("foot_l", "foot_r"), "ref": "A_TPose",
             "map": ual_map(("pelvis", "spine_01", "spine_02", "spine_03", "neck_01", "Head"),
                            [lr(x) for x in ("clavicle_{s}", "upperarm_{s}", "lowerarm_{s}", "hand_{s}", "thigh_{s}", "calf_{s}", "foot_{s}")],
                            lambda f, k, side: f"{f}_0{k}_{side.lower()}", lambda k, side: f"thumb_0{k}_{side.lower()}")},
    "ual1": {"file": UAL1, "root": "root", "thighs": ("DEF-thigh.L", "DEF-thigh.R"), "shins": ("DEF-shin.L", "DEF-shin.R"), "feet": ("DEF-foot.L", "DEF-foot.R"), "ref": "A_TPose",
             "map": ual_map(("DEF-hips", "DEF-spine.001", "DEF-spine.002", "DEF-spine.003", "DEF-neck", "DEF-head"),
                            [LR(x) for x in ("DEF-shoulder.{s}", "DEF-upper_arm.{s}", "DEF-forearm.{s}", "DEF-hand.{s}", "DEF-thigh.{s}", "DEF-shin.{s}", "DEF-foot.{s}")],
                            lambda f, k, side: f"DEF-f_{f}.0{k}.{side}", lambda k, side: f"DEF-thumb.0{k}.{side}")},
    "men": {"file": MEN, "root": "Root", "thighs": ("UpperLeg.L", "UpperLeg.R"), "shins": ("LowerLeg.L", "LowerLeg.R"), "feet": ("Foot.L", "Foot.R"), "ref": None,
            "map": None},  # every bone by its own name, the rest pose as reference
}


def read_source(key):
    """Per clip and frame: world deltas per target bone, hip midpoint, ankles (shin tails), toes
    (foot tails), all root-relative, and the root's own position."""
    src = SOURCES[key]
    arm = gw_rig.load(src["file"])
    bones = arm.data.bones
    if src["map"] is None:
        src["map"] = {b.name: b.name for b in bones if b.name not in gw_rig.DROP_BONES and b.name != "Root"}
    lengths = {b.name: b.length for b in bones}
    root_rest = bones[src["root"]].matrix_local

    def relative(P):
        rel = root_rest @ P[src["root"]].inverted()
        return {n: rel @ m for n, m in P.items()}

    if src["ref"]:
        act = bpy.data.actions[src["ref"]]
        ref = relative(gw_rig.sample(arm, act, [int(act.frame_range[0])])[0])
    else:
        ref = {b.name: b.matrix_local.copy() for b in bones}
    ref_q = {n: gw_rig.rotq(m).inverted() for n, m in ref.items()}
    src["ref_hip"] = (ref[src["thighs"][0]].translation + ref[src["thighs"][1]].translation) / 2
    src["ref_ankle"] = sum((ref[s] @ Vector((0, lengths[s], 0))).z for s in src["shins"]) / 2
    src["ref_toe"] = sum((ref[s] @ Vector((0, lengths[s], 0))).z for s in src["feet"]) / 2
    src["clips"] = {}
    for _, source, clip, _ in CLIPS:
        if source != key:
            continue
        act = bpy.data.actions[clip]
        frames = []
        for P in gw_rig.sample(arm, act, gw_rig.clip_frames(act)):
            S = relative(P)
            frames.append({
                "D": {t: gw_rig.rotq(S[s]) @ ref_q[s] for t, s in src["map"].items()},
                "hip": (S[src["thighs"][0]].translation + S[src["thighs"][1]].translation) / 2,
                "ankles": [S[s] @ Vector((0, lengths[s], 0)) for s in src["shins"]],
                "toes": [S[s] @ Vector((0, lengths[s], 0)) for s in src["feet"]],
                "root": P[src["root"]].translation.copy(),
                "upper": [S[src["map"][n]].translation.copy() for n in UPPER]
                + [S[src["map"]["Head"]] @ Vector((0, lengths[src["map"]["Head"]], 0))],
            })
        src["clips"][clip] = frames
    return arm


def angle(a, b):
    t = abs(a.dot(b)) / (a.magnitude * b.magnitude)
    return 2 * math.acos(min(1.0, t)) * 57.29578


def max_angle(a, b):
    return max(angle(a[n], b[n]) for n in a)


def planted_speed(toes):
    """Forward speed a planted foot implies: its backward slide in an in-place cycle. toes: per
    frame [toe L, toe R]. A toe within 1.5 cm of its lowest in the clip and sliding backwards
    (+Y; Blender's -Y is forward) is planted; the median of those speeds is the cycle's speed.
    Toes rather than heels: they stay down through most of a stance, a sprint's included, and a
    zombie's dragged foot slides forwards, which this leaves out."""
    low = [min(t[k].z for t in toes) for k in (0, 1)]
    speeds = []
    for i in range(1, len(toes) - 1):
        for k in (0, 1):
            v = (toes[i + 1][k].y - toes[i - 1][k].y) / (2 * DT)
            if toes[i][k].z - low[k] < 0.015 and v > 0:
                speeds.append(v)
    return statistics.median(speeds) if speeds else 0.0


def build(arm, label, out_name):
    for obj in list(bpy.data.objects):
        if obj.type != "ARMATURE":
            bpy.data.objects.remove(obj, do_unlink=True)
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)
    gw_rig.clear_animation(arm)
    gw_rig.restructure(arm)
    T = gw_rig.Target(arm)
    rows = []
    for name, source, clip, loop in CLIPS:
        src = SOURCES[source]
        frames = src["clips"][clip]
        ratio = T.hipmid.z / src["ref_hip"].z
        n = len(frames)
        drift = frames[-1]["hip"] - frames[0]["hip"] if loop else Vector()
        drift.z = 0
        rots, locs, feet_t, fix = [], [], [], []
        for i, fr in enumerate(frames):
            hip = T.hipmid + ratio * (fr["hip"] - drift * (i / (n - 1)) - src["ref_hip"])
            rot, loc, ankles, toes = T.solve(fr["D"], hip)
            # The lowest heel or toe goes to the source's height for it, scaled; the fix fades out
            # while the source has both feet 5-15 cm or more off the floor (jumps, falls).
            lift = min(min(a.z for a in fr["ankles"]) - src["ref_ankle"], min(t.z for t in fr["toes"]) - src["ref_toe"])
            weight = min(1.0, max(0.0, 1.0 - (lift - 0.05) / 0.10))
            dz = (ratio * lift - T.contact(ankles, toes)) * weight
            hip.z += dz
            rot, loc, ankles, toes = T.solve(fr["D"], hip)
            # Body down (an upper-body joint of the source within 30 cm of the floor): no upper-body
            # joint goes lower than the source's, scaled. Proportions differ; the head mustn't sink.
            low_s = ratio * min(v.z for v in fr["upper"])
            low_t = min([T.pos[n].z for n in UPPER] + [T.head_top.z])
            guard = max(0.0, low_s - low_t) * min(1.0, max(0.0, (0.30 - low_s) / 0.10))
            if guard > 0:
                hip.z += guard
                dz += guard
                rot, loc, ankles, toes = T.solve(fr["D"], hip)
            rots.append(rot)
            locs.append(loc)
            feet_t.append(ankles + toes)
            fix.append(dz)
        closing = ""
        if loop:
            gap, step = max_angle(rots[-1], rots[0]), max_angle(rots[-2], rots[-1])
            if gap <= max(0.35 * step, 2.0):
                # Ends on (nearly) its first pose: spread what's left over the cycle.
                for b in rots[0]:
                    c = rots[-1][b].inverted() @ rots[0][b]
                    if c.w < 0:
                        c.negate()
                    for i in range(n):
                        rots[i][b] = rots[i][b] @ Quaternion().slerp(c, i / (n - 1))
                off = locs[0] - locs[-1]
                for i in range(n):
                    locs[i] = locs[i] + off * (i / (n - 1))
                closing = f"closed (end gap {gap:.1f} deg spread)"
            else:
                # Ends a frame before its first pose: the first pose goes on the end.
                rots.append({b: q.copy() for b, q in rots[0].items()})
                locs.append(locs[0].copy())
                feet_t.append(feet_t[0])
                closing = f"open (gap {gap:.1f} deg vs step {step:.1f}): first pose appended"
        gw_rig.write_action(arm, name, rots, locs)
        duration = (len(rots) - 1) * DT
        row = {"name": name, "source": f"{source}:{clip}", "duration": duration, "loop": loop, "frames": len(rots),
               "ratio": ratio, "max_fix": max(abs(x) for x in fix), "closing": closing}
        if name in LOCOMOTION:
            v_src = planted_speed([fr["toes"] for fr in frames])
            v_tgt = planted_speed([f[2:] for f in feet_t])
            row.update(src_stride=v_src * (n - 1) * DT, stride=v_tgt * duration, speed=v_tgt)
        travel = frames[-1]["root"] - frames[0]["root"]
        hip_move = (frames[-1]["hip"] - frames[0]["hip"]) * ratio
        row.update(root_fwd=-travel.y, root_up=travel.z, hip_fwd=-hip_move.y, hip_up=hip_move.z)
        if name == "Z_Climb":
            row["root_curve"] = [(round(i * DT, 3), round(-(fr["root"].y - frames[0]["root"].y), 3), round(fr["root"].z - frames[0]["root"].z, 3))
                                 for i, fr in enumerate(frames)]
        rows.append(row)
    os.makedirs(OUT, exist_ok=True)
    glb = os.path.join(OUT, out_name)
    bpy.ops.export_scene.gltf(
        filepath=glb, export_format='GLB', use_selection=False,
        export_animations=True, export_animation_mode='ACTIONS', export_force_sampling=True,
        export_frame_range=False, export_anim_slide_to_zero=True, export_optimize_animation_size=False,
        export_reset_pose_bones=True, export_rest_position_armature=True, export_yup=True,
        export_def_bones=False, export_skins=True, export_materials='NONE', export_morph=False,
        export_cameras=False, export_lights=False, export_extras=False)
    gw_rig.prune_glb(glb, translated=("Body",))
    print(f"CATALOG {label}: {glb} {os.path.getsize(glb)} bytes; Body rest head {tuple(round(x, 4) for x in T.h['Body'])} "
          f"(Blender Z up), hip joints z {T.hipmid.z:.4f}, ankles z {T.ankle:.4f}")
    for r in rows:
        extra = ""
        if "stride" in r:
            extra = f" stride {r['stride']:.3f} m (source {r['src_stride']:.3f} m) speed {r['speed']:.3f} m/s"
        print(f"CLIP {label} {r['name']:<13} {r['source']:<28} {r['duration']:.3f} s {r['frames']:>3} keys loop={r['loop']!s:<5}"
              f"{extra} | hips moved fwd {r['hip_fwd']:+.3f} up {r['hip_up']:+.3f} | source root moved fwd {r['root_fwd']:+.3f} up {r['root_up']:+.3f}"
              f" | ratio {r['ratio']:.4f} max foot fix {r['max_fix'] * 100:.1f} cm {r['closing']}")
        if "root_curve" in r:
            print(f"CURVE {label} {r['name']} (t, forward, up): {r['root_curve']}")


read_source("ual2")
read_source("ual1")
men_arm = read_source("men")
build(men_arm, "men", "moves.glb")
build(gw_rig.load(WOMEN), "women", "moves_women.glb")
