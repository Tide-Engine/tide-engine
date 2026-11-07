import { strict as assert } from 'node:assert';
import { test } from 'node:test';
import { bakeImpostor, cardOBJ, decodePNG, encodePNG, meshBounds, parseOBJ, viewBasis } from './impostor-bake.mjs';

function square(z, leaf) {
  const points = [[-1, -1, z], [1, -1, z], [1, 1, z], [-1, 1, z]], uv = [[0, 1], [1, 1], [1, 0], [0, 0]];
  return [[0, 1, 2], [0, 2, 3]].map(indices => ({ leaf, vertices: indices.map(i => ({ position: points[i], normal: [0, 0, 1], uv: uv[i] })) }));
}

test('OBJ bake coordinates, source weights and negative indices match importer conventions', () => {
  const triangles = parseOBJ('v 1 2 3 .2 .3 .4\nv 2 2 3 .5 .6 .7\nv 1 3 3 .8 .9 1\n'
    + 'vt 0 .25\nvt 1 .25\nvt 0 .75\nvn 0 0 1\nf -3/-3/1 -2/-2/1 -1/-1/1\n', { leaf: true });
  assert.equal(triangles.length, 1);
  assert.deepEqual(triangles[0].vertices.map(v => v.position), [[-1, 2, 3], [-1, 3, 3], [-2, 2, 3]]);
  assert.deepEqual(triangles[0].vertices.map(v => v.uv), [[0, .75], [0, .25], [1, .75]]);
  assert.ok(triangles[0].vertices.every(v => v.normal[2] === 1 && !('color' in v)));
  assert.throws(() => parseOBJ('v 0 0 0\nf 1 2 3\n'), /out of range/);
  assert.throws(() => parseOBJ('v 0 NaN 0\nf 1 1 1\n'), /Invalid OBJ/);
});

test('PNG roundtrip keeps straight RGB at alpha zero and rejects corrupt bytes', () => {
  const rgba = Uint8Array.from([255, 0, 5, 255, 0, 250, 10, 0, 10, 20, 30, 128, 100, 110, 120, 64]);
  const png = encodePNG(2, 2, rgba), decoded = decodePNG(png);
  assert.equal(decoded.width, 2); assert.equal(decoded.height, 2);
  assert.deepEqual(decoded.rgba, rgba);
  const bad = Buffer.from(png); bad[bad.length - 15] ^= 1;
  assert.throws(() => decodePNG(bad), /CRC/);
  assert.throws(() => decodePNG(png.subarray(0, -3)), /Incomplete|Truncated/);
});

test('all hemisphere bases are orthonormal, include horizon and overhead, and use 24 views', () => {
  const dot = (a, b) => a.reduce((sum, x, i) => sum + x * b[i], 0);
  for (let i = 0; i < 24; i++) {
    const { right, up, toward } = viewBasis(i);
    for (const vector of [right, up, toward]) assert.ok(Math.abs(dot(vector, vector) - 1) < 1e-12);
    assert.ok(Math.abs(dot(right, up)) < 1e-12 && Math.abs(dot(up, toward)) < 1e-12 && Math.abs(dot(right, toward)) < 1e-12);
    assert.ok(toward[1] >= 0 && toward[1] <= 1);
  }
  assert.deepEqual(viewBasis(0).toward, [0, 0, 1]);
  assert.ok(viewBasis(16).toward[1] === 1);
  assert.throws(() => viewBasis(24), /24/);
});

test('actual source depth, leaf alpha and two-sided normals determine the atlas', () => {
  // Green front leaves cover the left half; red bark behind must remain visible
  // through the right cutout, with a different depth. No analytic tree is used.
  const leaf = { width: 2, height: 2, rgba: Uint8Array.from([0, 255, 0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 0, 255, 0, 0]) };
  const mesh = [...square(-.5, false), ...square(.5, true)];
  const options = { layout: { tile: 8 }, supersample: 1, center: [0, 0, 0], span: 4, depth: 2,
    barkColor: [1, 0, 0], leafColor: [1, 1, 1], cutoff: .5 };
  const baked = bakeImpostor(mesh, leaf, options);
  assert.equal(baked.width, 64); assert.equal(baked.height, 32); assert.equal(baked.metadata.views.length, 24);
  const pixel = (data, x, y) => [...data.subarray((y * baked.width + x) * 4, (y * baked.width + x) * 4 + 4)];
  assert.deepEqual(pixel(baked.albedo, 3, 3), [0, 255, 0, 255]);
  assert.deepEqual(pixel(baked.albedo, 4, 3), [255, 0, 0, 255]);
  assert.equal(pixel(baked.normalDepth, 3, 3)[3], 159);
  assert.equal(pixel(baked.normalDepth, 4, 3)[3], 96);
  assert.deepEqual(pixel(baked.mask, 3, 3), [255, 255, 0, 255]);
  assert.deepEqual(pixel(baked.mask, 4, 3), [255, 255, 0, 0]);
  // Frame 4 looks from -Z: the double-sided green leaves' normal faces -Z.
  assert.equal(pixel(baked.normalDepth, 4 * 8 + 4, 3)[2], 0);
  assert.ok(baked.albedo.subarray(24 * baked.width * 4).every(x => x === 0));
  const again = bakeImpostor(mesh, leaf, options);
  assert.deepEqual(again.albedo, baked.albedo); assert.deepEqual(again.normalDepth, baked.normalDepth);
  assert.throws(() => bakeImpostor(mesh, leaf, { ...options, span: 1 }), /bounds/);
});

test('generated source card reimports around the baked model center', () => {
  const center = [1, 6, -2], span = 16;
  const bounds = meshBounds(parseOBJ(cardOBJ(center, span)));
  assert.deepEqual(bounds.minimum, [-7, -2, -2]);
  assert.deepEqual(bounds.maximum, [9, 14, -2]);
  assert.deepEqual(bounds.center, center);
});
