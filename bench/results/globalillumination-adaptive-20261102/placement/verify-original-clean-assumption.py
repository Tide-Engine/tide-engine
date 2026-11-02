"""Validate exported production CPU placement; never opens a GPU device."""
import json
import math
import pathlib

root = pathlib.Path(__file__).resolve().parent
expected = {
    "room.json": (7, 48),
    "district64-near.json": (448, 48),
    "district64-overview.json": (448, 400),
}
for name, (instances, distance) in expected.items():
    data = json.loads((root / name).read_text())
    assert data["source_sha"] == "b1c69748f47a800269fae11fbf7d4f3370df7c8c"
    assert data["scene_instances"] == instances == len(data["receiver_bounds"])
    assert data["view_distance"] == distance
    assert data["initialized"] == data["active_probes"] == len(data["probes"])
    assert data["active_blocks"] == len(data["blocks"])
    probes = {probe["id"]: probe for probe in data["probes"]}
    assert len(probes) == len(data["probes"])
    used = set()
    for probe in probes.values():
        assert probe["captured"] and not probe["dirty"]
        assert all(math.isfinite(value) for value in probe["position"])
        assert math.sqrt(sum(value * value for value in probe["offset"])) <= data["base_spacing"] * 0.450001
        assert all(math.isclose(position, anchor + offset, rel_tol=1e-6, abs_tol=1e-5)
                   for position, anchor, offset in zip(probe["position"], probe["anchor"], probe["offset"], strict=True))
    for block in data["blocks"]:
        assert len(block["probe_ids"]) == 27
        for logical, physical in enumerate(block["probe_ids"]):
            assert bool(block["probe_mask"] & (1 << logical)) == (physical >= 0)
            if physical < 0:
                continue
            assert physical in probes
            used.add(physical)
            coordinate = (logical % 3, logical // 3 % 3, logical // 9)
            assert all(math.isclose(actual, lower + axis * block["spacing"], rel_tol=1e-6, abs_tol=1e-5)
                       for actual, lower, axis in zip(probes[physical]["anchor"], block["minimum"], coordinate, strict=True))
    assert used == set(probes)
    print(name, "instances", instances, "probes", len(probes), "blocks", len(data["blocks"]),
          "viewDistance", distance, "block spacings", sorted({block["spacing"] for block in data["blocks"]}))
