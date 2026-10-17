# GRAVEWAKE people

Skinned characters and their clips, made headless in Blender 4.4 by the scripts in `tools/`.
All files are glTF binary, Y up, metres, flat material colours (no textures). Characters face +Z
with their left side at +X (glTF's convention).

## Files

| File | What | Source (CC0 1.0, Quaternius) | Size |
|---|---|---|---|
| `zombie_farmer.glb` | zombie: grey-green skin, bloodied shirt, filthy straw hat | Ultimate Modular Men, `Farmer.gltf` | 736,200 |
| `zombie_worker.glb` | zombie: bluish-grey, bloodied tunic, iron cap | Ultimate Modular Men, `Worker.gltf` | 706,488 |
| `zombie_king.glb` | the rare plague king: jaundiced, tarnished crown, armour | Ultimate Modular Men, `King.gltf` | 1,433,908 |
| `zombie_wanderer.glb` | zombie: sallow green, bearded, leather (backpack removed) | Ultimate Modular Men, `Adventurer.gltf` | 1,122,996 |
| `zombie_peasant.glb` | zombie: pale grey-green, bloodied apron, linen cap | Ultimate Modular Women, `Worker.gltf` | 818,684 |
| `zombie_maid.glb` | zombie: bluish-grey, blood-soaked bodice, grimy skirt | Ultimate Modular Women, `Formal.gltf` | 815,884 |
| `hero_ranger.glb` | hero, **green** tunic, with backpack | Ultimate Modular Men, `Adventurer.gltf` | 1,404,688 |
| `hero_smith.glb` | hero, blacksmith: **blue** shirt, leather apron and cap | Ultimate Modular Men, `Worker.gltf` | 706,508 |
| `hero_huntress.glb` | hero, grey-haired, **red** hood | Ultimate Modular Women, `Medieval.gltf` | 859,744 |
| `hero_witch.glb` | hero, ink-dark dress and hat, **gold** trim | Ultimate Modular Women, `Witch.gltf` | 916,844 |
| `moves.glb` | clip catalog on the **men's** skeleton (armature only) | see Clips | 760,240 |
| `moves_women.glb` | the same clips on the **women's** skeleton | see Clips | 762,868 |

Bodies have mesh, skin and skeleton and no clips. Held props (sword, pistol) and the packs'
icosphere helper are removed. Materials are repainted per body (`BODIES` in
`tools/make_bodies.py`); a material only some meshes should change is copied as `<Material>_<Part>`.

### Which catalog plays which body

Men's bodies (`zombie_farmer`, `zombie_worker`, `zombie_king`, `zombie_wanderer`, `hero_ranger`,
`hero_smith`) play `moves.glb`; women's (`zombie_peasant`, `zombie_maid`, `hero_huntress`,
`hero_witch`) play `moves_women.glb`. Both catalogs have the same clip names, lengths and timing,
and each catalog's skeleton is identical (names, parents and rest transforms) to its bodies'.

One catalog can't serve both: Tide's in-place sampling copies the animation file's own local
transforms for every bone a clip animates and its ancestors (`tide_skinned_sample_into` in
`engine/src/skinned.c`; spec, "Separate-file binding"), so a woman playing `moves.glb` gets the
men's bone offsets. And the two packs' rigs differ more than in length: rest offsets differ by up
to 12 cm (Body) and rest local rotations by up to 90 degrees (UpperArm, UpperLeg and LowerLeg
rolls), so even a rotation-only catalog would twist women's limbs. No per-body hips rescale is
needed: each catalog's hips motion is already scaled for its skeleton.

## Skeleton

60 bones, the same names and parents in all twelve files (`python tools/check_glb.py *.glb --tree`
prints it). Under the armature node `CharacterArmature`:

```
Root                         ground point; never animated
  Body                       the hips: the only bone with a translation channel
    Hips                     lower spine
      Abdomen
        Torso
          Chest
            Neck
              Head
            Shoulder.L / .R
              UpperArm.L / .R
                LowerArm.L / .R
                  Wrist.L / .R          the hand (hand sockets: Wrist.R right, Wrist.L left)
                    Index1-4, Middle1-4, Ring1-4, Pinky1-4, Thumb1-3 (.L / .R)
    UpperLeg.L / .R
      LowerLeg.L / .R
        Foot.L / .R
```

`.L` is the character's left (+X). This is the packs' CharacterArmature with one change, made in
the bodies and the catalogs alike: the packs hang Foot.L/R and the IK pole helpers PT.L/R off Root
and place the feet by translation keys. Here Foot.L/R hang off LowerLeg.L/R with their pivot at the
ankle (the lower leg's tail) and PT.L/R are gone (they had no weights), so the legs are plain FK
and every bone but Body is animated by rotation alone.

Rest positions (glTF, metres):

| Skeleton (bodies) | Body (hips) bone | hip joints (UpperLeg heads) height | ankles height | mesh height |
|---|---|---|---|---|
| men's (`moves.glb`, zombie_farmer/worker/king/wanderer, hero_ranger/smith) | (0.0005, 0.8654, 0.0267) | 0.9642 | 0.1008 | 1.856-1.903 (king's crown) |
| women's (`moves_women.glb`, zombie_peasant/maid, hero_huntress/witch) | (0.0005, 0.9736, 0.0683) | 1.0369 | 0.0757 | 1.844-1.877; witch 2.045 with hat |

## Eyes: the second UV set

Every body has `TEXCOORD_1`. On zombies it is (1, 0) on the eye vertices and (0, 0) everywhere
else; on heroes it is (0, 0) everywhere. The game makes `uv2.x > 0.5` glow. Zombie eyes also have
their own material `Eye`, warm amber (#ffa526). `TEXCOORD_0` is the packs' UVs, unused by flat
colours (the ranger's backpack had none: zeros).

## Clips

Both catalogs: 30 keys per second, linear. Every bone has a rotation channel (constant ones cut to
two keys), Body also a translation channel; no scale channels. Root motion is removed: every clip
is in place. Loops end on their first pose, so they loop on their duration.

| Clip | Source | s | Loop | Men: stride m / speed m/s | Women: stride m / speed m/s | Notes |
|---|---|---|---|---|---|---|
| Idle | Men pack Idle | 1.667 | yes | | | |
| Walk | Men pack Walk | 1.333 | yes | 1.777 / 1.333 | 1.972 / 1.479 | source 1.821 |
| Run | Men pack Run | 0.833 | yes | 2.305 / 2.766 | 2.537 / 3.044 | source 2.456 in 0.8 s; the source's last frame isn't its first pose (8 degrees off), so the first pose was added at the end |
| Idle_Sword | Men pack Idle_Sword | 1.667 | yes | | | |
| Sword_Slash | Men pack Sword_Slash | 1.033 | | | | |
| Idle_Gun | Men pack Idle_Gun | 1.667 | yes | | | |
| Gun_Shoot | Men pack Gun_Shoot | 0.600 | | | | |
| Punch_Right | Men pack Punch_Right | 0.833 | | | | |
| Punch_Left | Men pack Punch_Left | 0.833 | | | | |
| HitRecieve | Men pack HitRecieve | 0.567 | | | | the pack's spelling |
| HitRecieve_2 | Men pack HitRecieve_2 | 0.567 | | | | |
| Death | Men pack Death | 1.067 | | | | falls on its back: hips end 0.79 m back and 0.84 m down (women 0.85, 0.90) |
| Interact | Men pack Interact | 1.267 | | | | |
| Roll | Men pack Roll | 1.333 | | | | ends where it began (the pack's roll is in place) |
| Z_Idle | UAL2 Zombie_Idle_Loop | 1.333 | yes | | | |
| Z_Walk | UAL2 Zombie_Walk_Fwd_Loop | 1.333 | yes | 1.361 / 1.020 | 1.472 / 1.104 | source 1.354; a hunched shamble, arms hanging, one foot dragged |
| Z_Scratch | UAL2 Zombie_Scratch | 1.800 | | | | wind-up with a raised arm, then a lunging downward swipe |
| Z_Knockback | UAL2 Hit_Knockback | 0.833 | | | | starts hunched, knocked off its feet onto its back, ends lying: hips 0.12 m back, 0.51 m down (women 0.13, 0.55) |
| Z_Climb | UAL2 ClimbUp_1m_RM | 0.667 | | | | a running climb onto a 1 m ledge; the source's root moved 1.676 m forward and 1.000 m up, evenly over the clip (0.0838 m and 0.05 m per 1/30 s) |
| Z_GetUp | UAL2 LayToIdle | 1.533 | | | | from lying on its back (as Z_Knockback ends) to standing: hips end 0.25 m forward and 0.90 m up of where they start |
| Sprint | UAL1 Sprint_Loop | 0.667 | yes | 5.900 / 8.849 | 6.406 / 9.609 | source 5.548 |
| Death_B | UAL1 Death01 | 2.400 | | | | staggers, falls on its back: hips end 0.54 m back and 0.81 m down (women 0.59, 0.87) |
| Hit_Head | UAL1 Hit_Head | 0.433 | | | | |
| Hit_Chest | UAL1 Hit_Chest | 0.333 | | | | |
| Crouch_Idle | UAL1 Crouch_Idle_Loop | 2.933 | yes | | | |
| Crouch_Walk | UAL1 Crouch_Fwd_Loop | 2.000 | yes | 1.400 / 0.700 | 1.560 / 0.780 | source 1.442 |
| Jog | UAL1 Jog_Fwd_Loop | 0.933 | yes | 5.528 / 5.923 | 5.863 / 6.281 | source 5.505; a survivor's legs at a walk or a run, turned toward the way they go and played backwards going back (people.tide) |
| Pistol_Idle | UAL1 Pistol_Idle_Loop | 1.667 | yes | | | a survivor's stance standing, the arms put on the gun |
| Jump_Loop | UAL1 Jump_Loop | 2.500 | yes | | | in the air |
| Jump_Land | UAL1 Jump_Land | 1.267 | | | | its first half-second, coming down |

Stride is the ground a cycle covers, from the speed at which a planted toe slides backwards in the
in-place cycle (median over the frames a toe is down), measured on each exported skeleton; moving
the character at that speed keeps feet from sliding. "Source" is the same measure on the source
rig. Strides scale with leg length, so women's are longer.

### How the clips were made (`tools/make_moves.py`)

Every clip, the men pack's own included, goes through one retarget: each frame, a target bone's
world rotation is its mapped source bone's world rotation delta from the source's reference pose
applied to the target's rest (`target_world = source_world(frame) * source_world(ref)^-1 *
target_rest_world`), then converted to local rotations; bones without a source follow their
parent. The reference is UAL's `A_TPose` clip and the men pack's rest pose (both T-poses, like the
targets' rest). Mapping (UAL2 / UAL1 names):

| Target | UAL2 | UAL1 (Godot) |
|---|---|---|
| Body (Hips follows it) | pelvis | DEF-hips |
| Abdomen, Torso, Chest | spine_01, spine_02, spine_03 | DEF-spine.001, .002, .003 |
| Neck, Head | neck_01, Head | DEF-neck, DEF-head |
| Shoulder, UpperArm, LowerArm, Wrist | clavicle, upperarm, lowerarm, hand | DEF-shoulder, DEF-upper_arm, DEF-forearm, DEF-hand |
| Index/Middle/Ring/Pinky 2, 3, 4 (1 is the palm and follows Wrist) | index_01-03 etc. | DEF-f_index.01-03 etc. |
| Thumb1-3 | thumb_01-03 | DEF-thumb.01-03 |
| UpperLeg, LowerLeg, Foot | thigh, calf, foot | DEF-thigh, DEF-shin, DEF-foot |

The hip-joint midpoint follows the source's (root-relative), scaled by the ratio of hip-joint
heights (men/UAL 1.034, women/UAL 1.113, women/men 1.076). Then the hips go up or down so the
lowest heel or toe is at the source's height for it (scaled); this fades out while the source has
both feet 5-15 cm or more off the floor, and once the body is down no upper-body joint goes deeper
than the source's. The fixes are at most 7 cm on the men's skeleton and 10 cm on the women's, in
crouches and falls. Because the men pack's clips are re-solved for FK feet, feet now follow the
legs; the pack's IK feet sat up to 5 cm off the leg's end (15 cm in Roll's tuck).

Known limits: while falling backwards in Death_B the head (and the farmer's hat brim) dips a few
cm into the floor for a couple of frames, as the source's head does; Z_Walk's dragged foot floats
2-5 cm, as in the source.

## Regenerating

Sources: the glTF exports of the packs (`.gltf` with embedded buffers) and the two animation
libraries. Every path is an argument; nothing is read from the repository but the scripts.

```sh
B="C:/Program Files/Blender Foundation/Blender 4.4/blender.exe"
P=prototypes/gravewake/art/people
# the ten bodies (Farmer, Worker, King, Adventurer in --men; Worker, Formal, Medieval, Witch in --women folders)
"$B" -b --factory-startup --python $P/tools/make_bodies.py -- --men <men glTF dir> --women <women glTF dir> [--women <another dir>] --out $P [--only zombie_king,hero_smith]
# moves.glb and moves_women.glb, with the clip table above printed
"$B" -b --factory-startup --python $P/tools/make_moves.py -- --men <men dir>/Farmer.gltf --women <women dir>/Medieval.gltf \
    --ual1 "<UAL1>/Animation Library[Standard]/Godot/AnimationLibrary_Godot_Standard.glb" \
    --ual2 "<UAL2>/Universal Animation Library 2 [Standard]/Unreal-Godot/UAL2_Standard.glb" --out $P
# checks: sizes, skeletons (same names and parents everywhere), TEXCOORD_1 values, clips and channels
python $P/tools/check_glb.py $P/*.glb [--tree]
# preview sheets (rows: body and view; columns: frames)
"$B" -b --factory-startup --python $P/tools/preview.py -- --out <dir> --pair $P/zombie_farmer.glb=$P/moves.glb --pair $P/zombie_maid.glb=$P/moves_women.glb --clips Z_Walk,Sprint [--views front,side] [--frames 5] [--head]
```

`tools/gw_rig.py` holds what the scripts share: loading, the skeleton change, the retarget solve
and the catalog's channel pruning.

## Licences

All sources are CC0 1.0 (public domain) by Quaternius:

- Ultimate Modular Men: https://quaternius.com/packs/ultimatemodularcharacters.html
- Ultimate Modular Women: https://quaternius.com/packs/ultimatemodularwomen.html
- Universal Animation Library: https://quaternius.com/packs/universalanimationlibrary.html
- Universal Animation Library 2: https://quaternius.com/packs/universalanimationlibrary2.html
