"""Bake the globe's visual-only forest/dry fields with Tide's noise math.

Run from any directory after editing Climate in terrain.tide:
    python examples/earth/tools/climate.py --clang <clang>
The game regression checks the imported cube against the procedural fields.
"""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zlib


SOURCE = r'''
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include "tide/math.h"
static unsigned char channel(float v) {
    v = tide_clamp_f(v * 0.5f + 0.5f, 0, 1);
    return (unsigned char)(v * 255 + 0.5f);
}
int main(int argc, char **argv) {
    if (argc != 3) return 1;
    const int size = atoi(argv[2]);
    FILE *out = fopen(argv[1], "wb");
    if (!out || size < 1 || size > 1024) return 1;
    for (int face = 0; face < 6; face++) for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        const float u = 2 * ((x + 0.5f) / size) - 1, v = 2 * ((y + 0.5f) / size) - 1;
        const tide_float3 p = face == 0 ? tide_f3(1,-v,-u) : face == 1 ? tide_f3(-1,-v,u)
            : face == 2 ? tide_f3(u,1,v) : face == 3 ? tide_f3(u,-1,-v)
            : face == 4 ? tide_f3(u,-v,1) : tide_f3(-u,-v,-1);
        const tide_float3 n = tide_normalize_f3(p);
        const float forest = tide_simplex_f3(tide_mul_f3(n,tide_f3_splat(7)),0) * 0.6f
            + tide_simplex_f3(tide_mul_f3(n,tide_f3_splat(19)),0) * 0.3f;
        const float dry = tide_simplex_f3(tide_add_f3(tide_mul_f3(n,tide_f3_splat(3)),tide_f3(5,0,0)),0) * 0.7f
            + tide_simplex_f3(tide_mul_f3(n,tide_f3_splat(11)),0) * 0.3f;
        const unsigned char pixel[4] = {channel(forest),channel(dry),0,255};
        if (fwrite(pixel,1,4,out) != 4) return 1;
    }
    return fclose(out) != 0;
}
'''


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', default=shutil.which('clang'))
    args = parser.parse_args()
    if not args.clang:
        parser.error('pass --clang with the path to clang')
    root = Path(__file__).resolve().parents[3]
    build = root / 'build'
    build.mkdir(exist_ok=True)
    art = Path(__file__).resolve().parents[1] / 'art'
    art.mkdir(exist_ok=True)
    size = 256
    with tempfile.TemporaryDirectory(prefix='earth-climate-', dir=build) as scratch:
        scratch = Path(scratch)
        source = scratch / 'climate.c'
        source.write_text(SOURCE, encoding='utf-8')
        program, raw = scratch / 'climate.exe', scratch / 'climate.rgba'
        subprocess.run([args.clang, '-std=c17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                        '-fno-fast-math', '-ffp-contract=off', '-I' + str(root / 'engine/include'),
                        str(source), str(root / 'engine/src/noise.c'), '-o', str(program)]
                       + ([] if os.name == 'nt' else ['-lm']), check=True)
        subprocess.run([str(program), str(raw), str(size)], check=True)
        pixels = raw.read_bytes()
    changed = False
    for face, name in enumerate(['px', 'nx', 'py', 'ny', 'pz', 'nz']):
        start = face * size * size * 4
        rows = b''.join(b'\0' + pixels[start + y * size * 4:start + (y + 1) * size * 4] for y in range(size))
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
        png += chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')
        target = art / f'climate_{name}.png'
        if not target.exists() or target.read_bytes() != png:
            target.write_bytes(png)
            changed = True
        print(f'climate_{name}.png: {len(png):,} bytes')
    if changed:
        # CMake's game target follows the Tide sources, not new PNG contents.
        (art.parent / 'terrain.tide').touch()


if __name__ == '__main__':
    main()
