// Original authored procedural broadleaf reference, not scanned tree art.
// Regenerate with Node 24: node bench/forest/make-assets.mjs
import { writeFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { buildLeafMaps, leafCardHull, writeFoliageAssets } from './foliage-assets.mjs';

const add = (a, b) => a.map((x, i) => x + b[i]);
const sub = (a, b) => a.map((x, i) => x - b[i]);
const mul = (a, s) => a.map(x => x * s);
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const unit = a => mul(a, 1 / Math.hypot(...a));
function generator(seed) {
  let state = seed;
  return () => { state = (Math.imul(state, 1664525) + 1013904223) >>> 0; return state / 4294967296; };
}

export class Mesh {
  constructor(name) { this.name = name; this.triangles = []; this.points = []; }
  triangle(points, normals, uvs, wind = [1, 0, 0]) {
    this.points.push(...points);
    this.triangles.push({ points, normals, uvs, wind });
  }
  polygon(points, normals, uvs, wind) {
    for (let i = 1; i + 1 < points.length; i++)
      this.triangle([points[0], points[i], points[i + 1]], [normals[0], normals[i], normals[i + 1]],
        [uvs[0], uvs[i], uvs[i + 1]], wind);
  }
  text() {
    const lines = ['# Original procedural forest reference; make-assets.mjs',
      '# Tree vertex RGB is explicit wind data: response, branch phase, leaf phase.'];
    let count = 0;
    const vertices = new Map();
    for (const triangle of this.triangles) {
      const ids = [];
      for (let i = 0; i < 3; i++) {
        const position = [...triangle.points[i], ...triangle.wind].map(x => x.toFixed(6)).join(' ');
        const normal = triangle.normals[i].map(x => x.toFixed(6)).join(' ');
        const uv = triangle.uvs[i].map(x => x.toFixed(6)).join(' ');
        const key = `${position}|${normal}|${uv}`;
        if (!vertices.has(key)) {
          lines.push(`v ${position}`, `vn ${normal}`, `vt ${uv}`);
          vertices.set(key, ++count);
        }
        ids.push(vertices.get(key));
      }
      lines.push('f ' + ids.map(i => `${i}/${i}/${i}`).join(' '));
    }
    return lines.join('\n') + '\n';
  }
}

function tube(mesh, path, radii, sides) {
  const overall = unit(sub(path.at(-1), path[0]));
  const reference = Math.abs(overall[1]) > .9 ? [1, 0, 0] : [0, 1, 0];
  const rings = path.map((point, i) => {
    const axis = unit(sub(path[Math.min(i + 1, path.length - 1)], path[Math.max(0, i - 1)]));
    const right = unit(cross(axis, reference)), forward = cross(right, axis);
    const from = Math.max(0, i - 1), to = Math.min(path.length - 1, i + 1);
    const taper = (radii[to] - radii[from]) / Math.hypot(...sub(path[to], path[from]));
    return Array.from({ length: sides }, (_, j) => {
      const angle = j * 2 * Math.PI / sides;
      const radial = add(mul(right, Math.cos(angle)), mul(forward, Math.sin(angle)));
      return { point: add(point, mul(radial, radii[i] * (1 + .045 * Math.cos(angle * 3)))),
        normal: unit(sub(radial, mul(axis, taper))) };
    });
  });
  for (let i = 0; i + 1 < path.length; i++) for (let j = 0; j < sides; j++) {
    const k = (j + 1) % sides;
    const corners = [rings[i][j], rings[i + 1][j], rings[i + 1][k], rings[i][k]];
    mesh.polygon(corners.map(p => p.point), corners.map(p => p.normal),
      [[j / sides, i], [j / sides, i + 1], [(j + 1) / sides, i + 1], [(j + 1) / sides, i]], [1, 0, 0]);
  }
}

function trunk(y) { return [.11 * Math.sin(y * .55) + y * .012, y, -.08 * Math.sin(y * .4)]; }
function skeleton() {
  const random = generator(1731), branches = [], clusters = [];
  for (let i = 0; i < 11; i++) {
    const angle = i * 2.399963 + (random() - .5) * .24;
    const y = 3.05 + i * .49, radial = 2.65 + Math.sin((i + .5) * Math.PI / 11) * .8;
    const start = trunk(y), end = add(start, [Math.cos(angle) * radial, 2.05 - i * .045, Math.sin(angle) * radial]);
    const middle = add(start, [Math.cos(angle) * radial * .5, .55, Math.sin(angle) * radial * .5]);
    branches.push({ points: [start, middle, end], radius: .10 - i * .003 });
    const phase = random();
    clusters.push({ center: end, phase });
    for (const side of [-1, 1]) {
      const origin = add(mul(middle, .35), mul(end, .65));
      const twigAngle = angle + side * .57;
      const tip = add(origin, [Math.cos(twigAngle) * 1.15, .65 + random() * .35, Math.sin(twigAngle) * 1.15]);
      branches.push({ points: [origin, tip], radius: .032 });
      clusters.push({ center: tip, phase });
    }
  }
  for (let i = 0; i < 5; i++) {
    const angle = i * 2.399963;
    clusters.push({ center: add(trunk(9.5), [Math.cos(angle) * .85, .45 + (i % 2) * .35, Math.sin(angle) * .85]), phase: random() });
  }
  return { branches, clusters };
}

export function makeTree(level, maps) {
  if (![0, 1, 2].includes(level)) throw new Error('Tree level must be 0, 1 or 2');
  const bark = new Mesh(`bark-${level}`), leaves = new Mesh(`leaves-${level}`), random = generator(8281);
  const { branches, clusters } = skeleton();
  const trunkHeights = level === 0 ? [0, 1.6, 3.4, 5.4, 7.3, 9.7] : level === 1 ? [0, 3.4, 6.3, 9.7] : [0, 5, 9.7];
  tube(bark, trunkHeights.map(trunk), trunkHeights.map(y => .31 * Math.pow(1 - y / 11, .8) + .025), [9, 6, 4][level]);
  for (let i = 0; i < branches.length; i++) {
    const branch = branches[i], twig = branch.points.length === 2;
    if (level === 1 && twig) continue;
    if (level === 2 && (twig || i % 6 !== 0)) continue;
    const path = level === 2 ? [branch.points[0], branch.points.at(-1)] : branch.points;
    const radii = path.map((_, n) => n === 0 ? branch.radius : n + 1 === path.length ? .009 : branch.radius * .45);
    tube(bark, path, radii, level === 0 ? (twig ? 4 : 6) : level === 1 ? 4 : 3);
  }
  const hull = level < 2 ? leafCardHull(maps, level === 0 ? 8 : 6) : [[0, 1], [1, 1], [1, 0], [0, 0]];
  const perCluster = [5, 2, 1][level];
  for (let clusterIndex = 0; clusterIndex < clusters.length; clusterIndex++) {
    const cluster = clusters[clusterIndex];
    for (let card = 0; card < 5; card++) {
      const yaw = random() * Math.PI * 2, tilt = (random() - .5) * 1.8;
      const jitter = [(random() - .5) * .85, (random() - .5) * .65, (random() - .5) * .85];
      const detail = random(), phase = random(), response = .75 + random() * .25;
      if (card >= perCluster || (level === 2 && clusterIndex % 2)) continue;
      const center = add(cluster.center, level === 0 ? jitter : mul(jitter, .35));
      const right = [Math.cos(yaw), 0, Math.sin(yaw)];
      const up = unit([Math.sin(yaw) * Math.sin(tilt), Math.cos(tilt), -Math.cos(yaw) * Math.sin(tilt)]);
      const scale = [.84, 1.3, 2.25][level] * (.9 + detail * .2);
      const points = hull.map(([u, v]) => add(center, add(mul(right, (u - .5) * 2 * scale), mul(up, (.5 - v) * 2 * scale))));
      const normals = points.map(point => unit([point[0] * .55, .55 + (point[1] - 7.7) * .5, point[2] * .55]));
      leaves.polygon(points, normals, hull.map(([u, v]) => [u, 1 - v]), [response, cluster.phase, phase]);
    }
  }
  return { bark, leaves };
}

export function windBounds(mesh, flutter) {
  let horizontal = 0, low = Infinity, high = -Infinity;
  for (const [x, y, z] of mesh.points) {
    const h = Math.max(0, y), displacement = (.45 * 1.5 * .012 * h * h + .45 * flutter * h) / .8;
    horizontal = Math.max(horizontal, Math.abs(x) + displacement, Math.abs(z) + displacement);
    low = Math.min(low, y); high = Math.max(high, y);
    if (horizontal >= 8 - .001 || low <= -2.5 + .001 || high >= 14.5 - .001)
      throw new Error(`${mesh.name}: wind exceeds center(0,6,0), size(16,17,16)`);
  }
  return { horizontal, minY: low, maxY: high };
}

export function makeTerrain() {
  // Import reflects X; use source -X so ForestHeight agrees in world space.
  const terrain = new Mesh('terrain'), steps = 80, side = 2400;
  const height = (x, z) => Math.sin(-x * .008) * 5 + Math.sin(z * .012 - x * .003) * 3;
  const point = (x, z) => [x, height(x, z), z];
  const normal = (x, z) => unit([.04 * Math.cos(-x * .008) + .009 * Math.cos(z * .012 - x * .003), 1, -.036 * Math.cos(z * .012 - x * .003)]);
  for (let z = 0; z < steps; z++) for (let x = 0; x < steps; x++) {
    const x0 = (x / steps - .5) * side, x1 = ((x + 1) / steps - .5) * side;
    const z0 = (z / steps - .5) * side, z1 = ((z + 1) / steps - .5) * side;
    const p = [[x0, z0], [x0, z1], [x1, z1], [x1, z0]];
    terrain.polygon(p.map(([a, b]) => point(a, b)), p.map(([a, b]) => normal(a, b)), p.map(([a, b]) => [a / 20, b / 20]), [1, 1, 1]);
  }
  return terrain;
}

export function generateForestAssets(directory) {
  const maps = writeFoliageAssets(directory, buildLeafMaps());
  const levels = [];
  for (let level = 0; level < 3; level++) {
    const { bark, leaves } = makeTree(level, maps);
    const bounds = { bark: windBounds(bark, 0), leaves: windBounds(leaves, .012) };
    for (const mesh of [bark, leaves]) writeFileSync(`${directory}/${mesh.name}.obj`, mesh.text());
    levels.push({ level, barkTriangles: bark.triangles.length, leafTriangles: leaves.triangles.length,
      barkIndices: bark.triangles.length * 3, leafIndices: leaves.triangles.length * 3, bounds });
  }
  writeFileSync(`${directory}/terrain.obj`, makeTerrain().text());
  writeFileSync(`${directory}/forest-meshes.json`, JSON.stringify({ source: 'Original authored procedural broadleaf reference',
    leafSize: maps.width, sourceLeavesPerSpray: maps.leaves, alphaCutoff: .32,
    leafMaskRGBA: ['ambientOcclusion', 'roughness', 'metallic', 'opticalTransmittance'],
    windVertexRGB: ['response', 'branchPhase', 'leafPhase'], levels }, null, 2) + '\n');
  writeFileSync(`${directory}/forest-meshes.h`, '#ifndef FOREST_MESHES_H\n#define FOREST_MESHES_H\n#include <stdint.h>\n'
    + '// Generated by make-assets.mjs; exact submitted source geometry indices.\n'
    + `static const uint32_t forest_bark_index_counts[3] = {${levels.map(row => row.barkIndices).join(', ')}};\n`
    + `static const uint32_t forest_leaf_index_counts[3] = {${levels.map(row => row.leafIndices).join(', ')}};\n#endif\n`);
  return levels;
}

if (process.argv[1] && pathToFileURL(process.argv[1]).href === import.meta.url) {
  const levels = generateForestAssets(fileURLToPath(new URL('.', import.meta.url)));
  for (const row of levels) console.log(`level ${row.level}: ${row.barkTriangles} bark + ${row.leafTriangles} leaf triangles; wind bounds checked`);
}
