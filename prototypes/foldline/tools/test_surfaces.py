"""Visible structural joints must meet instead of fighting for the same depth."""
import copy
import math
import unittest

import foldmap


def box(low, high, kind=0):
    return {"min": low, "max": high, "kind": kind}


class SurfaceTests(unittest.TestCase):
    def test_duplicate_floor_is_rejected(self):
        floor = box([-4, -1, 0], [4, 0, 8])
        self.assertEqual(len(foldmap.exposed_top_overlaps([floor, copy.deepcopy(floor)])), 1)

    def test_abutting_floors_have_no_shared_area(self):
        self.assertEqual(foldmap.exposed_top_overlaps([
            box([-4, -1, 0], [4, 0, 8]), box([-4, -1, 8], [4, 0, 12])]), [])

    def test_buried_joint_does_not_fail(self):
        lower = box([-4, -1, 0], [4, 0, 8])
        covering = box([-4, 0, 0], [4, 2, 8])
        self.assertEqual(foldmap.exposed_top_overlaps([lower, copy.deepcopy(lower), covering]), [])

    def test_partial_cover_does_not_hide_a_visible_strip(self):
        lower = box([-4, -1, 0], [4, 0, 8])
        covering = box([-4, 0, 0], [3.99, 2, 8])
        self.assertEqual(len(foldmap.exposed_top_overlaps([lower, copy.deepcopy(lower), covering])), 1)

    def test_adjoining_cover_rectangles_hide_the_whole_joint(self):
        lower = box([-4, -1, 0], [4, 0, 8])
        covers = [box([-4, 0, 0], [0, 2, 8]), box([0, 0, 0], [4, 2, 8])]
        self.assertEqual(foldmap.exposed_top_overlaps([lower, copy.deepcopy(lower), *covers]), [])

    def test_grille_envelope_does_not_hide_a_visible_overlap(self):
        lower = box([-4, -1, 0], [4, 0, 8])
        self.assertEqual(len(foldmap.exposed_top_overlaps([
            lower, copy.deepcopy(lower), box([-4, 0, 0], [4, 2, 8], 4)])), 1)

    def test_room_wall_cannot_share_the_cabin_side_face(self):
        lift = {"at": [0, 0, 0], "yaw": "NORTH"}
        boxes = foldmap.lift_boxes(lift) + [box([-4, -1, 2.1], [4, 0, 8]), box([3, 0, 1.1], [8, 4.4, 2.1])]
        with self.assertRaisesRegex(foldmap.MapError, "coplanar with the lift's inner side"):
            foldmap.validate_lift(lift, boxes, "arrival")
        boxes[-1]["min"][0] = 4
        foldmap.validate_lift(lift, boxes, "arrival")

    def test_rotated_room_slot_uses_the_lift_frame(self):
        lift = {"at": [8, 5.4, 16.1], "yaw": "SOUTH"}
        yaw = math.pi
        boxes = foldmap.lift_boxes(lift)
        for center, size in [((0, -.5, 5), (8, 1, 6)), ((3.5, 2.2, 1.6), (1, 4.4, 1))]:
            boxes.append({"center": foldmap.lifted(lift["at"], yaw, center), "size": size})
        with self.assertRaisesRegex(foldmap.MapError, "coplanar with the lift's inner side"):
            foldmap.validate_lift(lift, boxes, "exit")


if __name__ == "__main__":
    unittest.main()
