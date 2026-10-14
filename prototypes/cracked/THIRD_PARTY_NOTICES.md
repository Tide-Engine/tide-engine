# Where CRACKED's assets come from

Everything under `art/` and `sfx/` was copied or converted from the owner's
Unity project for CRACKED (`Purrguelike`), for this port. Some of it is that
project's own work and some came into it from asset packs and sound
libraries, under whatever terms the project has them. Nobody checked those
terms for this port: before this folder is anywhere public as source, the
rows marked third-party need their licenses read, or other files in their
place. A built page packs them as a game build does.

| Here | From (under the Unity project's `Assets/`) | Whose |
|---|---|---|
| `art/blocks/*.obj` | `Art/Models/Blocks/*.fbx` | The project's |
| `art/player/mage.glb` (mesh and rig) | `Art/Models/Player/Magician/Magician 24-07.fbx` | The project's |
| `art/player/mage.glb` (the six clips, retargeted to that rig) | `Art/Animations/Player/Anims/X Bot@*.fbx`, `Ch36_nonPBR@Throw.fbx`, `Hands up.fbx` | Third-party: Mixamo (Adobe) |
| `art/sky/sky*_*.jpg`, at 256 pixels | `Externals/Allsky/**` | Third-party: the Allsky pack |
| `art/ui/upgrades.png`, `upgrades-small.png` (37 icons as one sheet; Frost Guard's was drawn for the port) | `Art/Sprites/Upgrades/*.png` | The project's |
| `art/ui/kit.png` (ten button sprites as one sheet) | `Art/Rounds/button_round_*.png`, `Art/Sprites/UI/button_rectangle_*.png`, and `Packages/dev.purrnet.mainmenu/Art/Sprites/KenneyUi/button_*.png` beside `Assets/` | Third-party: Kenney's UI pack |
| `sfx/body*`, `concrete*`, `deflect`, `explode*`, `shield*`, `ready*` | `Externals/Feel/**` | Third-party: Feel (More Mountains) |
| `sfx/fire.ogg` | `Art/Sounds/Fire/108617__juskiddink__thud-dry.wav` | Third-party: Freesound, juskiddink |
| `sfx/pad.ogg` | `Art/Sounds/350906__cabled_mess__jump_c_04.wav` | Third-party: Freesound, cabled_mess |
| `sfx/freeze.ogg` | `Art/Sounds/683180__neartheatmoshphere__iceball.wav` | Third-party: Freesound, neartheatmoshphere |
| `sfx/jetpack.ogg` | `Art/Sounds/733987__velcronator__jetpack-sfx-unlooped.wav` | Third-party: Freesound, velcronator |
| `sfx/healing.ogg` | `Art/Sounds/787341__takimeko__healing.wav` | Third-party: Freesound, takimeko |
| `sfx/zap.ogg`, `ding*`, `shatter*`, `step*`, `splash*` | `Art/Sounds/zap.mp3`, `Ding/`, `Shatter/`, `Steps/`, `Liquid/` | Not known |
| `sfx/music.ogg` | `Art/Music/Dragon Dance - Loop + edit.wav` | Not known |

`tools/make_maps.py`, `tools/make_mage.py`, `tools/make_skies.py` and `tools/make_ui.py` make
the generated files again from the Unity project's own.
