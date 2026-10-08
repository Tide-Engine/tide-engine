// Design witnesses for the meatball yards. The C test replays these through
// the actual match, including automatic kitchen entry and bridge latches.
import fs from 'node:fs';

const W = 12, DX = [0, 1, 0, -1], DY = [-1, 0, 1, 0];
const source = fs.readFileSync(new URL('../journey.tide', import.meta.url), 'utf8');
const body = source.slice(source.indexOf('string MapPlan('), source.indexOf('void StartTrail('));
const plans = [...body.matchAll(/return "([^"]+)"/g)].map(m => m[1]);
const slot = i => [i < 3 ? 2 : 6, 1 + (i % 3) * 2];
const index = (x, y) => y * W + x;
const chefCode = (x, y, f) => index(x, y) * 4 + f;
const code = (a, b, ball, selected) => a | b << 9 | ball << 18 | (selected + 1) << 25;
const initial = code(chefCode(1, 3, 1), chefCode(7, 3, 3), index(4, 3), -1);
const decodeChef = value => [((value >>> 2) % W), Math.floor((value >>> 2) / W), value & 3];
const holds = (c, x, y) => c[0] === x && c[1] === y || c[0] + DX[c[2]] === x && c[1] + DY[c[2]] === y;
const together = (a, b, x, y) => {
  const da = Math.abs(a[0] - x) + Math.abs(a[1] - y);
  const db = Math.abs(b[0] - x) + Math.abs(b[1] - y);
  return da <= 2 && db <= 2 && Math.min(da, db) <= 1;
};

function yard(region) {
  const rows = plans[region].split('/');
  const tiles = Array(W * 9).fill('~');
  rows.forEach((row, y) => [...row].forEach((mark, x) => tiles[index(x, y)] = mark));
  if (region < 3) {
    tiles[index(1, 2)] = 'o';
    tiles[index(7, 2)] = 'o';
    tiles[index(7, 4)] = '^';
  }
  return tiles;
}

function walk(tiles, state, who, dir) {
  const chefs = [decodeChef(state & 511), decodeChef(state >>> 9 & 511)];
  const c = chefs[who], p = chefs[1 - who], f = c[2], back = (f + 2) & 3;
  let bx = (state >>> 18 & 127) % W, by = Math.floor((state >>> 18 & 127) / W);
  let selected = (state >>> 25) - 1;
  const tile = (x, y) => x < 0 || y < 0 || x >= W || y >= 9 ? '~' : tiles[index(x, y)];
  const standing = (x, y) => ['.', 'o', 'i'].includes(tile(x, y));
  const roll = (x, y, way) => {
    if (bx !== x || by !== y) return true;
    let nx = bx + DX[way], ny = by + DY[way];
    const blocked = () => !standing(nx, ny) || holds(c, nx, ny) || holds(p, nx, ny);
    if (blocked()) return false;
    bx = nx; by = ny;
    while (tile(bx, by) === 'i') {
      nx = bx + DX[way]; ny = by + DY[way];
      if (blocked()) break;
      bx = nx; by = ny;
    }
    selected = -1;
    for (let i = 0; i < 6; i++) { const [sx, sy] = slot(i); if (bx === sx && by === sy) selected = i; }
    return true;
  };
  let nx = c[0], ny = c[1], nf = f;
  if (dir === f) {
    nx += DX[f]; ny += DY[f];
    const fx = nx + DX[f], fy = ny + DY[f];
    if (bx === nx && by === ny || !roll(fx, fy, dir)) return null;
    if (!standing(nx, ny) || holds(p, nx, ny) || tile(fx, fy) === '^' || holds(p, fx, fy)) return null;
  } else if (dir === back) {
    nx += DX[dir]; ny += DY[dir];
    if (!roll(nx, ny, dir) || !standing(nx, ny) || holds(p, nx, ny)) return null;
  } else {
    const cx = c[0] + DX[f] + DX[dir], cy = c[1] + DY[f] + DY[dir];
    const sx = c[0] + DX[dir], sy = c[1] + DY[dir];
    if (!roll(cx, cy, dir) || !roll(sx, sy, back)) return null;
    if (tile(cx, cy) === '^' || holds(p, cx, cy) || tile(sx, sy) === '^' || holds(p, sx, sy)) return null;
    nf = dir;
  }
  chefs[who] = [nx, ny, nf];
  if (holds(chefs[0], bx, by) || holds(chefs[1], bx, by)) return null;
  return code(chefCode(...chefs[0]), chefCode(...chefs[1]), index(bx, by), selected);
}

function find(region, gates = false) {
  const tiles = yard(region);
  const queue = [initial], parents = [-1], moves = [-1], seen = new Map([[initial, 0]]), found = [];
  const path = at => {
    const result = [];
    while (parents[at] >= 0) { const move = moves[at]; result.push(`${move >>> 2}${'NESW'[move & 3]}`); at = parents[at]; }
    return result.reverse().join(' ');
  };
  for (let head = 0; head < queue.length; head++) {
    const state = queue[head], a = decodeChef(state & 511), b = decodeChef(state >>> 9 & 511);
    const selected = (state >>> 25) - 1;
    const entered = selected >= 0 && together(a, b, ...slot(selected));
    if (gates) {
      if (a[0] === 1 && a[1] === 2 && b[0] === 7 && b[1] === 2
          || a[0] === 7 && a[1] === 2 && b[0] === 1 && b[1] === 2) return [path(head)];
    } else if (entered) {
      if (found[selected] === undefined) found[selected] = path(head);
      if (found.filter(p => p !== undefined).length === 6) return found;
    }
    if (entered) continue; // The real match leaves the yard here.
    for (let move = 0; move < 8; move++) {
      const next = walk(tiles, state, move >>> 2, move & 3);
      if (next === null || seen.has(next)) continue;
      seen.set(next, queue.length); queue.push(next); parents.push(head); moves.push(move);
    }
    if (queue.length > 1500000) throw new Error(`Region ${region} exceeded design search bound`);
  }
  throw new Error(`Unreachable map destination in region ${region}`);
}

for (let region = 0; region < 4; region++) {
  console.log(`REGION ${region}`);
  find(region).forEach((path, i) => console.log(`  SLOT ${i}: "${path}"`));
  if (region < 3) console.log(`  GATE: "${find(region, true)[0]}"`);
}
