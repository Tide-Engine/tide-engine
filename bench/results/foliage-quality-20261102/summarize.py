"""Validate and summarize a retained two-round forest representation cohort."""
import argparse
import json
from pathlib import Path
import statistics

parser = argparse.ArgumentParser()
parser.add_argument('attempt', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
status = json.loads((args.attempt / 'status.json').read_text(encoding='utf-8-sig'))
assert status['status'] == 'passed' and status['processes'] == 12
assert status['gpu_samples'] == 1200 and not status['contention']
rows = {}
settings = ('benchmark', 'renderer', 'revision', 'machine', 'compiler', 'optimized', 'platform',
            'gpu_timer_scope', 'trees', 'mesh_entities', 'camera', 'extent_metres', 'distance_levels',
            'shadows', 'wind', 'shadow_resolution', 'shadow_views', 'width', 'height', 'warmup', 'frames',
            'gpu_samples', 'texture_upload_bytes_mean', 'world_unchanged', 'parallax', 'fog',
            'travel_metres_per_second', 'travel_start_metres', 'orbit', 'start_frame', 'orbit_start_degrees',
            'orbit_degrees_per_second', 'orbit_radius_metres', 'orbit_height_metres', 'camera_eye_metres',
            'capture_interval_frames')
for round_ in (1, 2):
    for camera in ('ground', 'canopy', 'wide'):
        for mode in (0, 1):
            path = args.attempt / f'r{round_}-{camera}-mode{mode}.jsonl'
            lines = path.read_text(encoding='utf-8-sig').splitlines()
            assert len(lines) == 1, path
            row = json.loads(lines[0])
            assert row['impostor_mode'] == mode and row['camera'] == camera
            assert row['trees'] == 100000 and row['frames'] == row['gpu_samples'] == 100
            assert row['world_unchanged'] and row['texture_upload_bytes_mean'] == 0
            assert sum(row['primary_level_min']) == sum(row['primary_level_max']) == 100000
            rows[round_, camera, mode] = row
        a, b = rows[round_, camera, 0], rows[round_, camera, 1]
        assert all(a[key] == b[key] for key in settings), (round_, camera)
for camera in ('ground', 'canopy', 'wide'):
    for mode in (0, 1):
        a, b = rows[1, camera, mode], rows[2, camera, mode]
        assert all(a[key] == b[key] for key in (*settings, 'world_hash', 'primary_level_min', 'primary_level_max'))

result = dict(attempt=args.attempt.as_posix(), binary_sha256=status['executable_sha256'],
              source_head=status['source_head'], processes=12, gpu_samples=1200,
              common_settings_match=True, world_hash_repeats_within_each_mode=True,
              scope='100000 trees; mesh-only vs distant atlas; two paired process medians per camera; not FPS',
              metrics={})
for metric in ('view_record_ms', 'cpu_submit_ms', 'completion_readback_ms', 'gpu_ms'):
    cameras = {}
    for camera in ('ground', 'canopy', 'wide'):
        data = {}
        for mode in (0, 1):
            medians = [rows[r, camera, mode][metric]['median'] for r in (1, 2)]
            p95 = [rows[r, camera, mode][metric]['p95'] for r in (1, 2)]
            data[str(mode)] = dict(process_medians=medians, median_of_process_medians=statistics.median(medians),
                                   process_p95=p95)
        data['paired_percent_change'] = [100 * (b / a - 1) for a, b in zip(data['0']['process_medians'], data['1']['process_medians'])]
        cameras[camera] = data
    result['metrics'][metric] = cameras
args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result['metrics'], indent=2))
