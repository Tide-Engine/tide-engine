import test from 'node:test';
import assert from 'node:assert/strict';
import { buildLeafMaps, leafCardHull } from './foliage-assets.mjs';
import { makeTree, makeTerrain, windBounds } from './make-assets.mjs';

const maps = buildLeafMaps(128);
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const sub = (a, b) => a.map((x, i) => x - b[i]);
const dot = (a, b) => a.reduce((sum, x, i) => sum + x * b[i], 0);

test('original leaf spray has padded cutout color, unit normals and independent ORM/transmittance', () => {
  assert.equal(maps.leaves, 13);
  assert.deepEqual(buildLeafMaps(128), maps);
  let occupied = 0, greenLeaves = 0, minX = 255, maxX = 0, minY = 255, maxY = 0, minTransmission = 255, maxTransmission = 0;
  for (let y = 0; y < maps.height; y++) for (let x = 0; x < maps.width; x++) {
    const i = (y * maps.width + x) * 4;
    assert.ok(maps.albedo[i] > 0 && maps.albedo[i + 1] >= maps.albedo[i]);
    assert.equal(maps.normal[i + 3], 255);
    assert.equal(maps.mask[i + 2], 0);
    assert.ok(maps.mask[i] >= 220 && maps.mask[i + 1] >= 220);
    if (x === 0 || y === 0 || x === maps.width - 1 || y === maps.height - 1) assert.equal(maps.albedo[i + 3], 0);
    if (maps.albedo[i + 3] <= .32 * 255) continue;
    occupied++;
    if (maps.albedo[i + 1] > maps.albedo[i]) greenLeaves++;
    const normal = [0, 1, 2].map(c => maps.normal[i + c] / 127.5 - 1);
    assert.ok(Math.abs(Math.hypot(...normal) - 1) < .012);
    minX = Math.min(minX, maps.normal[i]); maxX = Math.max(maxX, maps.normal[i]);
    minY = Math.min(minY, maps.normal[i + 1]); maxY = Math.max(maxY, maps.normal[i + 1]);
    minTransmission = Math.min(minTransmission, maps.mask[i + 3]);
    maxTransmission = Math.max(maxTransmission, maps.mask[i + 3]);
  }
  assert.ok(occupied > maps.width * maps.height * .10 && occupied < maps.width * maps.height * .5);
  assert.ok(greenLeaves > occupied * .8); // The narrow brown stems are not leaves.
  assert.ok(maxX - minX > 70 && maxY - minY > 70);
  assert.ok(minTransmission === 0 && maxTransmission > 180);
});

test('fitted card polygons preserve every covered texel while removing blank space', () => {
  for (const sides of [8, 6]) {
    const hull = leafCardHull(maps, sides);
    let area = 0;
    for (let i = 0; i < hull.length; i++) {
      const a = hull[i], b = hull[(i + 1) % hull.length];
      area += a[0] * b[1] - a[1] * b[0];
    }
    assert.ok(area < -.4 && area > -1.8); // Clockwise image UV, below 90% of a quad.
    for (let y = 0; y < maps.height; y++) for (let x = 0; x < maps.width; x++) {
      if (maps.albedo[(y * maps.width + x) * 4 + 3] <= .32 * 255) continue;
      const u = (x + .5) / maps.width, v = (y + .5) / maps.height;
      for (let i = 0; i < hull.length; i++) {
        const a = hull[i], b = hull[(i + 1) % hull.length];
        assert.ok((b[0] - a[0]) * (v - a[1]) - (b[1] - a[1]) * (u - a[0]) <= 1e-10);
      }
    }
  }
});

test('three source representations retain winding, authored response, smooth normals and wind bounds', () => {
  let previous = Infinity;
  for (let level = 0; level < 3; level++) {
    const { bark, leaves } = makeTree(level, maps);
    assert.equal(makeTree(level, maps).leaves.text(), leaves.text());
    assert.ok(bark.triangles.length + leaves.triangles.length < previous);
    previous = bark.triangles.length + leaves.triangles.length;
    for (const mesh of [bark, leaves]) {
      const bounds = windBounds(mesh, mesh === leaves ? .012 : 0);
      assert.ok(bounds.horizontal < 8 && bounds.minY >= -2.5 && bounds.maxY <= 14.5);
      for (const t of mesh.triangles) {
        const face = cross(sub(t.points[1], t.points[0]), sub(t.points[2], t.points[0]));
        assert.ok(Math.hypot(...face) > 1e-8);
        for (const n of t.normals) assert.ok(Math.abs(Math.hypot(...n) - 1) < 1e-10);
        assert.ok(t.wind.every(x => x >= 0 && x <= 1));
        if (mesh === bark) {
          assert.deepEqual(t.wind, [1, 0, 0]);
          assert.ok(dot(face, t.normals.reduce((a, b) => a.map((x, i) => x + b[i]), [0, 0, 0])) > 0);
        } else {
          const [a, b, c] = t.uvs;
          assert.ok((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) > 0);
          assert.ok(t.wind[0] >= .75);
        }
      }
    }
    const phases = new Set(leaves.triangles.map(t => t.wind[2]));
    assert.ok(phases.size >= 19);
    assert.notDeepEqual(leaves.triangles[0].normals[0], leaves.triangles[0].normals[1]);
  }
});

test('terrain stays indexed and keeps the existing upward analytic surface', () => {
  const terrain = makeTerrain();
  assert.equal(terrain.triangles.length, 12800);
  const vertices = terrain.text().split('\n').filter(line => line.startsWith('v '));
  assert.equal(vertices.length, 6561);
  for (const t of terrain.triangles) {
    const face = cross(sub(t.points[1], t.points[0]), sub(t.points[2], t.points[0]));
    assert.ok(face[1] > 0 && t.normals.every(n => n[1] > .99));
    assert.deepEqual(t.wind, [1, 1, 1]);
  }
});
