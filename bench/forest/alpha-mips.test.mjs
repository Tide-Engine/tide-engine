import assert from 'node:assert/strict';
import test from 'node:test';
import { buildCoverageMips, buildMipChain, encodeMipDDS } from './alpha-mips.mjs';

function coverage(rgba, cutoff) {
  let passed = 0;
  for (let i = 3; i < rgba.length; i += 4) passed += Number(rgba[i] / 255 >= cutoff);
  return passed / (rgba.length / 4);
}

test('coverage is the closest possible uniform-alpha result at every finite mip', () => {
  const width = 64, height = 32, cutoff = 0.32;
  const rgba = new Uint8Array(width * height * 4);
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const i = (y * width + x) * 4;
    rgba.set([61, 140, 24, (x * 31 + y * 71 + x * y * 13) % 256], i);
  }
  const original = new Uint8Array(rgba), ordinary = buildMipChain(width, height, rgba);
  const fixed = buildCoverageMips(width, height, rgba, cutoff), wanted = coverage(rgba, cutoff);
  let improved = false;
  for (let level = 1; level < fixed.length; level++) {
    const source = ordinary[level].rgba;
    let best = wanted;
    // Independent exhaustive test of every possible threshold in the byte
    // domain: scaling cannot distinguish source texels with equal alpha.
    for (let threshold = 1; threshold <= 255; threshold++) {
      let passed = 0;
      for (let i = 3; i < source.length; i += 4) passed += Number(source[i] >= threshold);
      best = Math.min(best, Math.abs(passed / (source.length / 4) - wanted));
    }
    const error = Math.abs(coverage(fixed[level].rgba, cutoff) - wanted);
    assert.ok(Math.abs(error - best) < 1e-12, `level ${level}: ${error} versus optimal ${best}`);
    improved ||= error + 1e-12 < Math.abs(coverage(source, cutoff) - wanted);
  }
  assert.ok(improved);
  assert.deepEqual(rgba, original);
  assert.deepEqual(fixed[0].rgba, original);
});

test('color mips filter premultiplied color; raw maps retain independent channels', () => {
  const rgba = new Uint8Array(4 * 4 * 4);
  for (let i = 0; i < rgba.length; i += 4) rgba.set([0, 0, 255, 0], i);
  rgba.set([255, 0, 0, 255], 0);
  const color = buildMipChain(4, 4, rgba), raw = buildMipChain(4, 4, rgba, { raw: true });
  assert.deepEqual([...color[1].rgba.slice(0, 4)], [255, 0, 0, 64]);
  assert.deepEqual([...raw[1].rgba.slice(0, 4)], [64, 0, 191, 64]);
  assert.equal(color.at(-1).rgba[0], 255);
});

test('atlas views preserve their own finite coverage until their tiles reach one texel', () => {
  const width = 32, height = 16, tile = 8, cutoff = 0.32;
  const rgba = new Uint8Array(width * height * 4);
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const view = Math.floor(y / tile) * (width / tile) + Math.floor(x / tile);
    const alpha = (x * 73 + y * 41 + x * y * 19) % 256;
    rgba.set([20, 140, 50, alpha > view * 29 ? alpha : 0], (y * width + x) * 4);
  }
  const ordinary = buildMipChain(width, height, rgba);
  const fixed = buildCoverageMips(width, height, rgba, cutoff, { tileWidth: tile, tileHeight: tile });
  const region = (level, tx, ty, side) => {
    const pixels = new Uint8Array(side * side * 4);
    for (let y = 0; y < side; y++) for (let x = 0; x < side; x++) {
      const at = ((ty * side + y) * level.width + tx * side + x) * 4;
      pixels.set(level.rgba.subarray(at, at + 4), (y * side + x) * 4);
    }
    return pixels;
  };
  let differsFromGlobal = false;
  const global = buildCoverageMips(width, height, rgba, cutoff);
  for (let level = 1; level <= Math.log2(tile); level++) {
    const side = tile >> level;
    for (let ty = 0; ty < height / tile; ty++) for (let tx = 0; tx < width / tile; tx++) {
      const wanted = coverage(region(ordinary[0], tx, ty, tile), cutoff);
      const source = region(ordinary[level], tx, ty, side);
      let best = wanted;
      for (let threshold = 1; threshold <= 255; threshold++) {
        let passed = 0;
        for (let i = 3; i < source.length; i += 4) passed += Number(source[i] >= threshold);
        best = Math.min(best, Math.abs(passed / (side * side) - wanted));
      }
      const actual = region(fixed[level], tx, ty, side);
      assert.ok(Math.abs(Math.abs(coverage(actual, cutoff) - wanted) - best) < 1e-12,
        `level ${level} view ${tx},${ty}: ${coverage(actual, cutoff)} target ${wanted}, optimal error ${best}`);
      differsFromGlobal ||= Math.abs(coverage(region(global[level], tx, ty, side), cutoff) - wanted) > best + 1e-12;
    }
  }
  assert.ok(differsFromGlobal, 'a shared atlas-wide target loses one view\'s coverage');
  for (let level = Math.log2(tile) + 1; level < fixed.length; level++)
    assert.deepEqual(fixed[level], global[level], 'mandatory atlas tail uses aggregate coverage');
  assert.throws(() => buildCoverageMips(width, height, rgba, cutoff, { tileWidth: 3 }), /tile sizes/);
});

test('DDS bytes describe exactly one rectangular complete RGBA8 chain', () => {
  const rgba = new Uint8Array(8 * 4 * 4).fill(255);
  const levels = buildCoverageMips(8, 4, rgba, 0.5), dds = encodeMipDDS(levels);
  assert.equal(dds.subarray(0, 4).toString('ascii'), 'DDS ');
  assert.equal(dds.readUInt32LE(4), 124);
  assert.equal(dds.readUInt32LE(12), 4);
  assert.equal(dds.readUInt32LE(16), 8);
  assert.equal(dds.readUInt32LE(28), 4);
  assert.equal(dds.readUInt32LE(76), 32);
  assert.equal(dds.readUInt32LE(92), 0xff);
  assert.equal(dds.length, 128 + (32 + 8 + 2 + 1) * 4);
  assert.ok(dds.subarray(128).every(byte => byte === 255));
  assert.throws(() => encodeMipDDS(levels.slice(0, -1)), /complete/);
  assert.throws(() => buildCoverageMips(7, 4, rgba, 0.5), /powers of two/);
  assert.throws(() => buildCoverageMips(8, 4, rgba, 0), /cutoff/);
  assert.throws(() => buildCoverageMips(8, 4, rgba, NaN), /cutoff/);
});
