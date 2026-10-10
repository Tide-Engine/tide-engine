# Exporter fixtures

These files come unchanged from the [ufbx test data](https://github.com/ufbx/ufbx/tree/26a482ae66871d7de36eb722aa060bce95bce274/data)
at commit `26a482ae66871d7de36eb722aa060bce95bce274` (v0.23.1).
The upstream license is included in `LICENSE`.

- `blender_272_cube_7400_binary.fbx`: Blender 2.72 binary mesh export.
- `maya_game_sausage_7500_binary.fbx`: Maya binary rigged mesh export.
- `maya_game_sausage_7500_binary_deform.fbx`: Maya animated skin export with
  inherited-scale compensation and a take that starts after time zero. Its
  sampled Tide skin is checked against ufbx's independently evaluated scene.
- `maya_anim_interpolation_7700_binary.fbx`: Maya binary animation interpolation export.

They exercise actual exporters alongside Tide's small, hand-authored fixtures.
No modelling application, converter executable, or network request is required
to build or run the tests.
