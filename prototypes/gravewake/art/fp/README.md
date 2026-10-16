# GRAVEWAKE first-person arms

The arms the player sees, the bow and arrow in them, and every first-person clip, all on one skeleton.
`tools/make_fp.py` makes the four files from CoinCoin's first-person kit (CC BY 4.0: see
`../CREDITS.md`, which has the required credit) with Blender 4.4, headless:

    blender -b --factory-startup --python tools/make_fp.py -- --bow <arc_copie.fbx> \
        --axe <FPS_KIT.fbx> --knife <firesteel_copie.fbx> --out . [--preview <dir> [clip,clip]]

| File | What |
|---|---|
| `arms.glb` | the two arms, skinned, flat colours (gloves, bracers, sleeves) |
| `bow.glb` | the kit's bow and its string, one skinned mesh: the limbs flex and the string draws with the clips |
| `arrow.glb` | the nocked arrow, skinned |
| `moves.glb` | the clips, no meshes |

The skeleton is the kit bow file's, with the tomahawk file's weapon bone added as `weapon` and the
knife file's as `knife`. A held thing rides `weapon`: its grip at the bone, a blade (or a crossbow's
barrel) along the bone's +Z, the edge toward +X (fp.tide's `Seat`).

## Clips

| Clip | From | What |
|---|---|---|
| `One_Idle`, `One_Walk`, `One_Run`, `One_Swing1`, `One_Swing2` | the kit's tomahawk | a one-handed weapon at rest, walking, running, a forehand and a backhand blow (landing at 0.5 s and 0.73 s) |
| `Stab` | the kit's knife | a quick stab (landing at 0.16 s), the weapon kept in the right hand |
| `Bow_Idle`, `Bow_Empty`, `Bow_Draw`, `Bow_Hold`, `Bow_Loose`, `Bow_Nock`, `Bow_Walk`, `Bow_Run`, `Bow_Lower` | the kit's bow | the bow held, drawn, held at full draw, loosed and the next arrow nocked |
| `Two_Idle`, `Two_Swing1`, `Two_Swing2` | the kit's tomahawk, the left hand added | a two-handed weapon: the tomahawk's path moved in toward the middle, the left hand 0.17 m up the handle |
| `Pole_Idle`, `Pole_Thrust`, `Pole_Sweep` | made, and the kit's backhand | a polearm held by the hip pointed ahead across the body, a thrust (landing at 0.3 s), a sweep |
| `X_Idle`, `X_Reload` | made | a crossbow or the handgonne at the hip, both hands; a reload with the nose down |
| `X_Aim`, `X_Fire` | made | unused by the game, which raises the arms itself when aiming |
| `H_Idle`, `H_Fire`, `H_Reload` | made | the hand crossbow in one hand |
| `Staff_Idle`, `Staff_Cast`, `Jav_Idle`, `Jav_Throw`, `Toss`, `Drink` | made | a staff, a javelin held overhand and thrown, a pot or bell tossed, a brew drunk |

Clips made here are a weapon's path set by hand (where its grip is, where its blade points and its
edge faces, in the eye's space), the right hand taking the grip as the kit's hand holds its
tomahawk, the left (for two-handed holds) further along it, both by two-bone IK, and the elbows down
and out. The kit's arms are short (its eye is at its shoulders), so for a two-handed hold the left
shoulder comes 0.10 m in and 0.12 m forward, under and behind the eye's view. Made clips are drawn
with the eye 0.15 m higher above the arms than the kit's (fp.tide's `FpEye`, `LowFor`), so an arm
reaching across passes below it.
