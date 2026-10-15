#!/usr/bin/env python3
"""Validate the actual compiled Foldline builders against their source maps."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exporter", type=Path, required=True)
    parser.add_argument("--scene", type=Path, required=True)
    parser.add_argument("--node", type=Path)
    args = parser.parse_args()
    scene = args.scene.resolve()
    scene.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.exporter.resolve())]
    if args.node:
        command = [str(args.node.resolve()), "--no-warnings", str(ROOT.parents[1] / "cmake/run_wasi.mjs"), *command]
    try:
        # WASI exposes this working directory as '.', so pass a relative path.
        subprocess.run([*command, "--export-layout", scene.name], cwd=scene.parent, check=True)
        subprocess.run([sys.executable, str(ROOT / "tools/foldmap.py"), "--check", "--scene", str(scene)], check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Foldline layout validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
