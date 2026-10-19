"""Assembly regressions, including the owner's broken Reception screenshot."""
import contextlib
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest

import foldmap


RECEPTION = foldmap.ROOT / "maps/reception.foldmap.json"


class AssemblyTests(unittest.TestCase):
    def setUp(self):
        self.map = json.loads(RECEPTION.read_text(encoding="utf-8"))

    def reject(self, text):
        with self.assertRaisesRegex(foldmap.MapError, text):
            foldmap.validate_map(self.map)

    def test_reception_assembly_and_both_folds(self):
        tide = foldmap.compile_map(self.map)
        self.assertIn("void BuildMapReception(Arena arena, mut Place place, mut Fold fold)", tide)
        self.assertIn("map_west", tide)
        self.assertIn("map_east", tide)
        self.assertIn("float3(10.8, 4.1, 1.965)", tide)

    def test_exit_lamps_sit_beside_the_sign(self):
        exit = next(l for l in self.map["lifts"] if l["role"] == "exit")
        exit["socket_x"] = .3
        self.reject("lift.socket_x: exit lamps must sit")
        del exit["socket_x"]
        self.reject("lift.socket_x: expected a finite number")

    def test_original_half_blocked_exit_is_rejected(self):
        cupboard = next(b for b in self.map["boxes"] if b["id"] == "cupboard_west")
        cupboard["min"][0], cupboard["max"][0] = 5.2, 5.8
        self.reject("exit: lift opening/approach is blocked by cupboard_west")

    def test_floating_vertical_wire_is_rejected(self):
        self.map["anchors"]["exit_jamb_foot"]["at"][0] = 4
        self.map["anchors"]["exit_jamb_top"]["at"][0] = 4
        self.reject("unsupported")

    def test_wire_buried_in_header_is_rejected(self):
        self.map["anchors"]["cupboard_socket"] = {"support":"scene", "face":"-z", "at":[10.8,4.1,2.35]}
        self.reject("unsupported")

    def test_free_wire_crossing_air_is_rejected_even_when_endpoints_are_supported(self):
        # Two supported endpoints do not prove a supported segment.
        wire = self.map["wires"][0]
        wire["points"] = ["cupboard_plate_rim", "cupboard_socket"]
        self.reject("unsupported")

    def test_cable_can_turn_from_floor_up_wall_and_onto_ceiling(self):
        foldmap.validate_map(self.map)

    def test_source_contact_must_belong_to_same_signal(self):
        self.map["wires"][0]["signal"] = 1
        self.reject("source signal does not match")

    def test_target_must_use_the_wire_signal(self):
        self.map["doors"][0]["need"] = [1]
        self.reject("target does not use this signal")

    def test_cable_must_reach_target_socket(self):
        self.map["doors"][0]["socket"] = "header_top"
        self.reject("misses target socket")

    def test_cable_must_reach_source_plate(self):
        self.map["plates"][0]["at"] = [-9,0,4]
        self.reject("misses its source plate rim")

    def test_out_of_face_anchor_is_rejected(self):
        self.map["anchors"]["divider_top"]["uv"][0] = 8
        self.reject("outside support face")

    def test_portal_must_fit_full_backing_width(self):
        self.map["ends"][0]["foot"][2] = -8.8
        self.reject("crosses the edge")

    def test_portal_must_fit_full_backing_height(self):
        self.map["ends"][0]["size"] = 2
        self.reject("crosses floor/ceiling")

    def test_portal_must_be_on_its_named_face(self):
        self.map["ends"][0]["face"] = "-x"
        self.reject("not attached")

    def test_thin_portal_backing_is_rejected(self):
        wall = next(b for b in self.map["boxes"] if b["id"] == "west")
        wall["min"][0] = -12.5
        self.reject("at least .6 m")

    def test_nonreceptive_portal_backing_is_rejected(self):
        next(b for b in self.map["boxes"] if b["id"] == "west")["takes"] = False
        self.reject("receptive wall")

    def test_scene_hook_and_metric_geometry_are_preserved(self):
        data = {"version":1, "name":"free_geometry", "function":"BuildMapFreeGeometry", "lifts":[
            {"id":"arrival", "role":"arrival", "at":[1.25,8.125,-13.7], "yaw":"EAST"}]}
        tide = foldmap.compile_map(data)
        self.assertIn("float3(1.25, 8.125, -13.7), EAST", tide)
        self.assertNotIn("Fold fold", tide)

    def test_machine_and_custom_plate_keep_their_behavior(self):
        data = {"version":1, "name":"cargo", "lifts":[{"id":"a", "role":"arrival", "at":[0,0,0], "yaw":"NORTH"}],
                "machines":[{"id":"deck", "from":[0,-.15,0], "to":[0,7.85,0], "size":[3,.3,3], "speed":2.2,
                             "need":[0], "lamp":[4,2,0], "along":[1,0,0], "lamp_depth":.18}],
                "plates":[{"id":"weight", "signal":0, "at":[0,0,0], "heavy":True, "half":[2.3,2.3], "need":2500}],
                "wires":[{"id":"wire", "signal":0, "from":"weight", "to":"deck", "points":[[2.3,.025,0],[4,.025,0],[4,2,0]]}]}
        tide = foldmap.compile_map(data)
        self.assertIn("float3(0, 7.85, 0)", tide)
        self.assertIn("float2(2.3, 2.3)", tide)
        self.assertIn("need = 2500", tide)

    def test_no_silent_wire_point_truncation(self):
        self.map["wires"][0]["points"] *= 2
        self.reject("2..12 points")

    def test_nonfinite_coordinate_is_rejected(self):
        self.map["lifts"][0]["at"][0] = float("nan")
        self.reject("finite number")

    def test_each_target_input_needs_an_incoming_connection(self):
        self.map["wires"] = self.map["wires"][1:]
        self.reject("no incoming cable connection")

    def test_external_geometry_flag_is_explicit_boolean(self):
        self.map["external_geometry"] = "yes"
        self.reject("expected boolean")

    def test_generation_is_stable_and_stale_output_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            maps = root / "maps"
            maps.mkdir()
            (maps / RECEPTION.name).write_text(json.dumps(self.map), encoding="utf-8")
            output = root / "maps_generated.tide"
            args = ["--maps", str(maps), "--output", str(output)]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(foldmap.main(args), 0)
                first = output.read_bytes()
                self.assertEqual(foldmap.main(args + ["--check"]), 0)
                self.assertEqual(output.read_bytes(), first)
                output.write_text("// accidentally hand edited\n", encoding="utf-8")
                self.assertEqual(foldmap.main(args + ["--check"]), 1)


class ActualBuiltSceneTests(unittest.TestCase):
    def setUp(self):
        source = json.loads(RECEPTION.read_text(encoding="utf-8"))
        boxes, _ = foldmap.validate_map(source)
        self.room = {"id":2, "kind":0, "walls":list(boxes.values()), "solids":[], "wires":[], "frames":[], "ends":[],
                     "exit":{"at":[6,0,10.1], "yaw":foldmap.math.pi, "need":2, "socket":1}, "props":[
                         {"kind_name":"ExitSign", "at":[6,4.2,7.45], "yaw":foldmap.math.pi, "scale":[1,1,1]}]}
        for lift in source["lifts"]:
            self.room["walls"].extend(foldmap.lift_boxes(lift))

    def errors(self):
        return "\n".join(foldmap.validate_scene({"version":1, "maps":[self.room]}))

    def test_actual_geometry_is_validated(self):
        self.assertEqual(self.errors(), "")

    def test_hook_geometry_cannot_block_a_generated_lift(self):
        self.room["solids"].append({"center":[6,1.5,7.6], "size":[1,3,1], "kind":"prop", "rotation":[0,0,0,1]})
        self.assertIn("lift opening/approach is blocked", self.errors())

    def test_wrong_facing_exit_sign_is_rejected(self):
        self.room["props"][0]["yaw"] = 0
        self.assertIn("sign faces away", self.errors())

    def test_floating_exit_sign_is_rejected(self):
        self.room["props"][0]["at"][2] -= .11
        self.assertIn("not mounted", self.errors())

    def test_missing_exit_sign_is_rejected(self):
        self.room["props"] = []
        self.assertIn("exactly one sign", self.errors())

    def test_exit_signal_lamps_over_the_sign_are_rejected(self):
        self.room["exit"]["socket"] = 0
        self.assertIn("exit signal lamps cover its sign", self.errors())

    def test_actual_preplaced_fold_requires_a_receptive_backing(self):
        self.room["ends"] = [{"at":[-12,1.3,-3], "face":[1,0,0], "size":1,
                              "wall_center":[-12.5,1.2,0], "wall_size":[1,6.4,18], "wall_takes":False}]
        self.assertIn("requires a receptive wall", self.errors())

    def test_visible_furniture_cannot_have_no_collider(self):
        self.room["props"].append({"kind_name":"Cabinet", "at":[-11.65,0,2.8], "entity":100, "solid_parts":0})
        self.assertIn("needs 1 attached solid parts, found 0", self.errors())

    def test_compound_furniture_shapes_must_be_exported_for_clearance_checks(self):
        self.room["props"].append({"kind_name":"Cabinet", "at":[-11.65,0,2.8], "entity":100, "solid_parts":1})
        self.assertIn("missing its actual compound collision geometry", self.errors())
        self.room["solids"].append({"kind":"prop", "owner":100, "center":[-11.65,.7,2.8], "size":[.62,1.4,.64]})
        self.assertEqual(self.errors(), "")

    def test_single_enclosing_box_cannot_substitute_for_a_chair_compound(self):
        self.room["props"].append({"kind_name":"PlasticChair", "at":[-10.7,0,1], "entity":100, "solid_parts":1})
        self.room["solids"].append({"kind":"prop", "owner":100, "center":[-10.7,.5,1], "size":[.52,1,.52]})
        self.assertIn("needs 6 attached solid parts, found 1", self.errors())

    def test_entire_lift_approach_needs_floor_support(self):
        self.room["walls"] = [b for b in self.room["walls"] if b.get("id") != "floor"]
        self.assertIn("missing floor", self.errors())

    def test_narrow_gap_in_approach_floor_is_rejected(self):
        self.room["walls"] = [b for b in self.room["walls"] if b.get("id") != "floor"]
        self.room["walls"].extend([{"min":[-12,-1,-8],"max":[12,0,6.2]}, {"min":[-12,-1,6.4],"max":[12,0,8]}])
        self.assertIn("missing floor", self.errors())

    def test_rotated_prop_uses_actual_orientation_for_clearance(self):
        q = [0, foldmap.math.sqrt(.5), 0, foldmap.math.sqrt(.5)]
        box = {"center":[0,0,1], "size":[2,.2,.2], "rotation":q}
        self.assertTrue(foldmap.intersects({"center":[0,0,0], "size":[1,1,1]}, box))
        self.assertFalse(foldmap.intersects({"center":[.8,0,1], "size":[.4,.4,.4]}, box))

    def test_cable_can_mount_to_a_real_rotated_prop_face(self):
        q = [0, foldmap.math.sqrt(.5), 0, foldmap.math.sqrt(.5)]
        box = {"center":[0,0,0], "size":[2,2,.2], "rotation":q}
        self.assertTrue(foldmap.supported_segment([.135,-.5,0],[.135,.5,0], [box]))

    def test_own_shutter_does_not_count_as_an_obstructed_opening(self):
        self.room["solids"].append({"kind":"gate", "center":[6,2,8], "size":[6,4,.24]})
        self.assertEqual(self.errors(), "")

    def test_grille_bounding_box_is_not_a_cable_mount(self):
        box = {"center":[0,1,0], "size":[2,2,.2], "kind":4}
        self.assertFalse(foldmap.supported_segment([-.5,1,-.13], [.5,1,-.13], [box]))

    def test_full_segment_is_checked_including_small_unsupported_gaps(self):
        boxes = [{"min":[-2,-1,-1], "max":[-.01,0,1]}, {"min":[.01,-1,-1], "max":[2,0,1]}]
        # Tangential mounting tolerance permits millimetre seams, not a span
        # over a gap that a player can see through.
        self.assertTrue(foldmap.supported_segment([-1,.025,0],[1,.025,0], boxes))
        boxes[0]["max"][0], boxes[1]["min"][0] = -.1, .1
        self.assertFalse(foldmap.supported_segment([-1,.025,0],[1,.025,0], boxes))


class SourceBindingTests(unittest.TestCase):
    def setUp(self):
        self.source = {"version":1, "name":"wiring", "place_ids":[2], "external_geometry":True,
                       "lifts":[{"id":"arrival", "role":"arrival", "at":[0,0,-2.1], "yaw":"NORTH"}],
                       "plates":[{"id":"plate", "signal":0, "at":[0,0,2]}],
                       "machines":[{"id":"door", "from":[2,2,5], "to":[2,6.2,5], "size":[2,4,.3], "need":[0],
                                    "speed":6, "lamp":[2,4.3,4.8], "along":[1,0,0], "lamp_depth":.18}],
                       "wires":[{"id":"wire", "signal":0, "from":"plate", "to":"door",
                                 "points":[[.6,.025,2],[2,.025,2],[2,4.3,2],[2,4.3,4.8]]}]}
        self.room = {"id":2, "kind":0, "walls":[], "arrival":{"at":[0,0,-2.1], "yaw":0},
                     "wires":[{"signal":0,"points":[[.6,.025,2],[2,.025,2],[2,4.3,2],[2,4.3,4.8]]}],
                     "plates":[{"signal":0,"at":[0,0,2],"half":[.6,.6],"need":20,"heavy":False,"linger":0,"invert":False}],
                     "movers":[{"from":[2,2,5],"to":[2,6.2,5],"size":[2,4,.3],"need":1,"any":False,"speed":6,
                                "lamp":[2,4.3,4.8],"lampAlong":[1,0,0],"lampDepth":.18}]}

    def errors(self):
        return "\n".join(foldmap.validate_bindings([self.source], {"version":1,"maps":[self.room]}))

    def test_matching_source_is_actually_built(self):
        self.assertEqual(self.errors(), "")

    def test_omitted_wire_cannot_silently_pass_geometry_checks(self):
        self.room["wires"] = []
        self.assertIn("cables: expected 1 source assemblies, found 0 built", self.errors())

    def test_omitted_map_function_is_rejected(self):
        self.room["wires"], self.room["plates"], self.room["movers"] = [], [], []
        errors = self.errors()
        self.assertIn("cables: expected 1", errors)
        self.assertIn("plates: expected 1", errors)
        self.assertIn("machinery: expected 1", errors)

    def test_leftover_old_wire_is_rejected(self):
        self.room["wires"].append(copy.deepcopy(self.room["wires"][0]))
        self.assertIn("found 2 built", self.errors())

    def test_wrong_machine_range_is_rejected(self):
        self.room["movers"][0]["to"][1] = 4.2
        self.assertIn("machinery: source assembly was not built", self.errors())

    def test_wrong_plate_requirement_is_rejected(self):
        self.room["plates"][0]["need"] = 1
        self.assertIn("plates: source assembly was not built", self.errors())

    def test_unexported_map_is_rejected(self):
        with self.assertRaisesRegex(foldmap.MapError, "coverage differs"):
            foldmap.validate_bindings([self.source], {"version":1,"maps":[]})

    def test_duplicate_source_place_id_is_rejected(self):
        with self.assertRaisesRegex(foldmap.MapError, "more than one source"):
            foldmap.validate_bindings([self.source,copy.deepcopy(self.source)], {"version":1,"maps":[self.room]})

    def test_campaign_check_requires_all_fourteen_places(self):
        with self.assertRaisesRegex(foldmap.MapError, "all fourteen"):
            foldmap.validate_bindings([self.source], {"version":1,"maps":[self.room]}, campaign=True)


class StrictSchemaTests(unittest.TestCase):
    def setUp(self):
        self.source = json.loads(RECEPTION.read_text(encoding="utf-8"))

    def reject(self, source, message):
        with self.assertRaisesRegex(foldmap.MapError, message):
            foldmap.validate_map(source)

    def machine(self, source):
        value = {"id":"test_machine", "from":[20,0,0], "to":[20,2,0], "size":[1,1,1], "need":[], "lamp":[20,0,0], "along":[1,0,0]}
        source["machines"] = [value]
        return value

    def test_unknown_top_level_field_is_rejected(self):
        self.source["externel_geometry"] = True
        self.reject(self.source, "unknown fields externel_geometry")

    def test_defined_tide_paints_are_accepted(self):
        for paint in ("DOORS", "BRICK", "PANELS", "CONCRETE", "MARBLE", "PLANKS", "HAZARD", "RUST", "SHUTTER", "WALLPAPER", "CARPET", "CEILING_TILE", "PLASTER"):
            with self.subTest(paint=paint):
                self.source["boxes"][0]["paint"] = paint
                foldmap.validate_map(self.source)

    def test_unknown_paints_are_rejected(self):
        for paint in ("TILES", "STONE", "METAL", "not_a_paint"):
            with self.subTest(paint=paint):
                self.source["boxes"][0]["paint"] = paint
                self.reject(self.source, "unknown paint")

    def test_misspelled_fields_in_each_object_kind_are_rejected(self):
        for category, field in (("boxes","knd"),("lifts","neeed"),("doors","speeed"),("plates","hevy"),("wires","signel"),("ends","sze")):
            with self.subTest(category=category):
                source = copy.deepcopy(self.source)
                source[category][0][field] = 1
                self.reject(source, "unknown fields")
        source = copy.deepcopy(self.source)
        self.machine(source)["speeed"] = 5
        self.reject(source, "unknown fields speeed")
        source = copy.deepcopy(self.source)
        source["frames"] = [{"id":"frame", "wall":"west", "face":"+x", "foot":[-12,0,3], "scale":1}]
        self.reject(source, "unknown fields scale")

    def test_unknown_anchor_fields_are_rejected(self):
        for name in ("divider_top", "exit_socket"):
            with self.subTest(anchor=name):
                source = copy.deepcopy(self.source)
                source["anchors"][name]["offest"] = .04
                self.reject(source, "unknown fields offest")

    def test_flags_require_json_booleans(self):
        for category, field, bad in (("plates","heavy","false"),("plates","invert",0),("doors","any","false"),("boxes","takes",1)):
            with self.subTest(category=category,field=field):
                source = copy.deepcopy(self.source)
                source[category][0][field] = bad
                self.reject(source, "boolean")
        source = copy.deepcopy(self.source)
        source["lifts"][1]["any"] = 1
        self.reject(source, "boolean")
        source = copy.deepcopy(self.source)
        self.machine(source)["any"] = "false"
        self.reject(source, "boolean")

    def test_explicit_plate_requirement_is_positive_and_finite(self):
        for value in (float("nan"),float("inf"),-1,0,True,None,"20"):
            with self.subTest(value=value):
                source = copy.deepcopy(self.source)
                source["plates"][0]["need"] = value
                self.reject(source, "plate.need")

    def test_plate_linger_is_nonnegative_and_finite(self):
        for value in (float("nan"),float("inf"),-1,True,None,"0"):
            with self.subTest(value=value):
                source = copy.deepcopy(self.source)
                source["plates"][0]["linger"] = value
                self.reject(source, "plate.linger")

    def test_stopping_lift_stay_is_positive_and_finite(self):
        for value in (float("nan"),float("inf"),-1,0,True,None,"10"):
            with self.subTest(value=value):
                source = {"version":1,"name":"stop","lifts":[{"id":"stop","role":"stop","at":[0,0,0],"yaw":"NORTH","stay":value}]}
                self.reject(source, "lift.stay")

    def test_door_and_machine_lamp_depth_are_positive_and_finite(self):
        for category in ("doors","machines"):
            for value in (float("nan"),float("inf"),-1,0,True,None):
                with self.subTest(category=category,value=value):
                    source = copy.deepcopy(self.source)
                    target = self.machine(source) if category == "machines" else source["doors"][0]
                    target["lamp_depth"] = value
                    self.reject(source, "lamp_depth")

    def test_explicit_speed_cannot_silently_default_from_null(self):
        for category in ("doors","machines"):
            source = copy.deepcopy(self.source)
            target = self.machine(source) if category == "machines" else source["doors"][0]
            target["speed"] = None
            self.reject(source, "speed")

    def test_signal_lamp_axis_must_be_unit_horizontal_cardinal(self):
        for value in ([0,0,0],[0,1,0],[2,0,0],[1,0,1],[float("nan"),0,0]):
            with self.subTest(value=value):
                source = copy.deepcopy(self.source)
                machine = self.machine(source)
                machine.update(need=[0],along=value)
                self.reject(source, "machine.along")

    def test_uncontrolled_vignette_prop_may_omit_lamp_axis(self):
        machine = self.machine(self.source)
        machine["along"] = [0,0,0]
        foldmap.validate_map(self.source)

    def test_kind_requires_supported_integer(self):
        for value in (True,1.0,-1,8,float("nan"),"4"):
            with self.subTest(value=value):
                source = copy.deepcopy(self.source)
                source["boxes"][0]["kind"] = value
                self.reject(source, "kind must be an integer 0..7")

    def test_assembly_ids_cannot_collide_across_categories(self):
        for category in ("boxes","doors","plates","wires","ends"):
            with self.subTest(category=category):
                source = copy.deepcopy(self.source)
                source[category][0]["id"] = "exit"
                self.reject(source, "duplicate assembly ID exit")
        source = copy.deepcopy(self.source)
        self.machine(source)["id"] = "exit"
        self.reject(source, "duplicate assembly ID exit")

    def test_anchor_names_require_identifiers(self):
        self.source["anchors"]["not an identifier"] = {"support":"scene","at":[0,0,0]}
        self.reject(self.source, "anchor name: expected an identifier")

    def test_duplicate_owned_end_assignment_is_rejected(self):
        self.source["ends"][1]["end"] = 0
        self.reject(self.source, "duplicate operator-owned index")


if __name__ == "__main__":
    unittest.main()
