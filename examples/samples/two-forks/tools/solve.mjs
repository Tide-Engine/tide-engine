// The puzzle's rules a second time, in JavaScript, and searches over them: is
// a level cooked by two chefs, by either alone, and how often does the lead
// have to change hands. It's what the levels were designed with. The game's
// own rules are game.tide's, and its test (test/two_forks_test.c) checks that
// the two agree, position for position, on every level.
export const DX = [0, 1, 0, -1];
export const DY = [-1, 0, 1, 0];
export const NAMES = 'NESW';

export function parse(text) {
    const rows = text.split('/');
    const h = rows.length;
    const w = Math.max(...rows.map(r => r.length));
    const tiles = [];
    const chefs = [null, null];
    const sausages = [];
    const taken = new Set();
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const c = rows[y][x] ?? '~';
            let tile = c;
            const lower = 'nesw'.indexOf(c);
            const upper = 'NESW'.indexOf(c);
            if (lower >= 0) { chefs[0] = [x, y, lower]; tile = '.'; }
            else if (upper >= 0) { chefs[1] = [x, y, upper]; tile = '.'; }
            else if (c === '-' || c === '|') {
                tile = '.';
                if (!taken.has(x + ',' + y)) {
                    const v = c === '|' ? 1 : 0;
                    sausages.push({ x, y, v });
                    taken.add(x + ',' + y);
                    taken.add((x + (v ? 0 : 1)) + ',' + (y + (v ? 1 : 0)));
                }
            }
            tiles.push(tile);
        }
    }
    return { w, h, tiles, chefs, sausages, text };
}

export function start(L) {
    const c = [];
    for (const chef of L.chefs) c.push(...(chef ?? [-9, -9, 0]));
    const s = [];
    for (const sausage of L.sausages) s.push(sausage.x, sausage.y, 0, 0, 0, 0, 0); // x y flip c0 c1 c2 c3
    return { c, s };
}

const SS = 7;

function tileAt(L, x, y) {
    if (x < 0 || y < 0 || x >= L.w || y >= L.h) return '~';
    return L.tiles[y * L.w + x];
}

// A chef's move: who, and which way they pressed. Gives the state after it,
// null where nothing moves, or 'dead' where a sausage burns or sinks.
export function step(L, S, who, dir) {
    const o = who * 3, p = (1 - who) * 3;
    const x = S.c[o], y = S.c[o + 1], f = S.c[o + 2];
    const ox = S.c[p], oy = S.c[p + 1], of = S.c[p + 2];
    const fx = ox + DX[of], fy = oy + DY[of];
    const other = (cx, cy) => (cx === ox && cy === oy) || (cx === fx && cy === fy);
    const walk = (cx, cy) => { const t = tileAt(L, cx, cy); return t === '.' || t === 'o' || t === 'i'; };
    const rock = (cx, cy) => tileAt(L, cx, cy) === '^';
    let nx = x, ny = y, nf = f;
    const pushes = [];
    if (dir === f) {
        nx = x + DX[f]; ny = y + DY[f];
        const tx = nx + DX[f], ty = ny + DY[f];
        if (!walk(nx, ny) || other(nx, ny) || rock(tx, ty) || other(tx, ty)) return null;
        pushes.push([tx, ty, f]);
    } else if (dir === (f + 2) % 4) {
        nx = x + DX[dir]; ny = y + DY[dir];
        if (!walk(nx, ny) || other(nx, ny)) return null;
        pushes.push([nx, ny, dir]);
    } else {
        nf = dir;
        const cx = x + DX[f] + DX[dir], cy = y + DY[f] + DY[dir];
        const tx = x + DX[dir], ty = y + DY[dir];
        if (rock(cx, cy) || other(cx, cy) || rock(tx, ty) || other(tx, ty)) return null;
        pushes.push([cx, cy, dir]);
        pushes.push([tx, ty, (f + 2) % 4]);
    }
    const s = S.s.slice();
    const n = s.length / SS;
    const moved = new Array(n).fill(false);
    let pushed = new Set();
    const vert = i => L.sausages[i].v;
    const at = (cx, cy) => {
        for (let i = 0; i < n; i++) {
            const sx = s[i * SS], sy = s[i * SS + 1];
            if (sx === cx && sy === cy) return i;
            if (vert(i) ? (sx === cx && sy + 1 === cy) : (sx + 1 === cx && sy === cy)) return i;
        }
        return -1;
    };
    const shove = (i, d, depth) => {
        if (depth > n) return false;
        const sx = s[i * SS] + DX[d], sy = s[i * SS + 1] + DY[d];
        const bx = sx + (vert(i) ? 0 : 1), by = sy + (vert(i) ? 1 : 0);
        if (rock(sx, sy) || rock(bx, by) || other(sx, sy) || other(bx, by)) return false;
        // Out of the way first, so it isn't found where it was
        const keepX = s[i * SS], keepY = s[i * SS + 1];
        s[i * SS] = -99; s[i * SS + 1] = -99;
        for (const [cx, cy] of [[sx, sy], [bx, by]]) {
            const j = at(cx, cy);
            if (j >= 0 && !shove(j, d, depth + 1)) { s[i * SS] = keepX; s[i * SS + 1] = keepY; return false; }
        }
        s[i * SS] = sx; s[i * SS + 1] = sy;
        const along = vert(i) ? (d === 0 || d === 2) : (d === 1 || d === 3);
        if (!along) s[i * SS + 2] ^= 1;
        moved[i] = true;
        pushed.add(i);
        return true;
    };
    const glide = d => {
        for (let distance = 0; distance < L.w + L.h; distance++) {
            const sliding = new Set([...pushed].filter(i => {
                const sx = s[i * SS], sy = s[i * SS + 1];
                return tileAt(L, sx, sy) === 'i' && tileAt(L, sx + (vert(i) ? 0 : 1), sy + (vert(i) ? 1 : 0)) === 'i';
            }));
            if (!sliding.size) return;
            let before = -1;
            while (before !== sliding.size) {
                before = sliding.size;
                for (const i of [...sliding]) {
                    const sx = s[i * SS] + DX[d], sy = s[i * SS + 1] + DY[d];
                    const cells = [[sx, sy], [sx + (vert(i) ? 0 : 1), sy + (vert(i) ? 1 : 0)]];
                    for (const [cx, cy] of cells) {
                        if (rock(cx, cy) || other(cx, cy)) sliding.delete(i);
                        for (let j = 0; j < n; j++) {
                            if (i === j || sliding.has(j)) continue;
                            const xj = s[j * SS], yj = s[j * SS + 1];
                            if ((xj === cx && yj === cy) || (xj + (vert(j) ? 0 : 1) === cx && yj + (vert(j) ? 1 : 0) === cy)) sliding.delete(i);
                        }
                    }
                }
            }
            if (!sliding.size) return;
            for (const i of sliding) {
                s[i * SS] += DX[d]; s[i * SS + 1] += DY[d];
                if (vert(i) !== (d === 0 || d === 2 ? 1 : 0)) s[i * SS + 2] ^= 1;
            }
        }
    };
    for (const [cx, cy, d] of pushes) {
        const i = at(cx, cy);
        pushed = new Set();
        if (i >= 0) {
            if (!shove(i, d, 0)) return null;
            glide(d);
        }
    }
    const pedal = (cx, cy) => tileAt(L, cx, cy) === 'o';
    const gasBefore = !(pedal(x, y) || pedal(ox, oy));
    const gasAfter = !(pedal(nx, ny) || pedal(ox, oy));
    const bellowsBefore = pedal(x, y) && pedal(ox, oy);
    const bellowsAfter = pedal(nx, ny) && pedal(ox, oy);
    let dead = false;
    for (let i = 0; i < n; i++) {
        const sx = s[i * SS], sy = s[i * SS + 1];
        const bx = sx + (vert(i) ? 0 : 1), by = sy + (vert(i) ? 1 : 0);
        const cells = [[sx, sy], [bx, by]];
        if (moved[i] && tileAt(L, sx, sy) === '~' && tileAt(L, bx, by) === '~') dead = true;
        for (let h = 0; h < 2; h++) {
            const t = tileAt(L, cells[h][0], cells[h][1]);
            const hot = (t === '#' && moved[i]) || (t === '=' && gasAfter && (moved[i] || !gasBefore))
                || (t === '+' && bellowsAfter && (moved[i] || !bellowsBefore));
            if (!hot) continue;
            const k = i * SS + 3 + h * 2 + s[i * SS + 2];
            s[k] += 1;
            if (s[k] > 1) dead = true;
        }
    }
    if (dead) return 'dead';
    const c = S.c.slice();
    c[o] = nx; c[o + 1] = ny; c[o + 2] = nf;
    return { c, s };
}

export function won(S) {
    for (let i = 0; i < S.s.length; i += SS) {
        for (let k = 3; k < 7; k++) if (S.s[i + k] !== 1) return false;
    }
    return S.s.length > 0;
}

const key = S => String.fromCharCode(...S.c.map(v => v + 40), ...S.s.map(v => v + 40));

// The shortest way through, with these chefs moving: its moves, and how many
// states were seen. null when there's none.
export function search(L, who, limit = 3000000) {
    const first = start(L);
    const seen = new Map([[key(first), null]]);
    let frontier = [first];
    let depth = 0;
    while (frontier.length) {
        const next = [];
        for (const S of frontier) {
            if (won(S)) {
                const path = [];
                let k = key(S);
                while (seen.get(k)) { const [prev, move] = seen.get(k); path.push(move); k = prev; }
                return { moves: path.reverse(), states: seen.size, depth };
            }
            for (const chef of who) {
                for (let d = 0; d < 4; d++) {
                    const T = step(L, S, chef, d);
                    if (!T || T === 'dead') continue;
                    const k = key(T);
                    if (seen.has(k)) continue;
                    seen.set(k, [key(S), chef + NAMES[d]]);
                    next.push(T);
                }
            }
        }
        if (seen.size > limit) return { moves: null, states: seen.size, depth, gaveUp: true };
        frontier = next;
        depth++;
    }
    return { moves: null, states: seen.size, depth };
}

// The fewest times the lead changes hands in any way through.
export function handoffs(L, limit = 6000000) {
    const first = start(L);
    const best = new Map();
    let layer = [[first, 0], [first, 1]];
    for (const [S, a] of layer) best.set(key(S) + a, 0);
    let cost = 0;
    while (layer.length) {
        const later = [];
        // Everything reachable at this cost
        let frontier = layer;
        while (frontier.length) {
            const next = [];
            for (const [S, a] of frontier) {
                if (won(S)) return cost;
                for (let d = 0; d < 4; d++) {
                    const T = step(L, S, a, d);
                    if (!T || T === 'dead') continue;
                    const k = key(T) + a;
                    if (best.has(k)) continue;
                    best.set(k, cost);
                    next.push([T, a]);
                }
                const k = key(S) + (1 - a);
                if (!best.has(k)) later.push([S, 1 - a, k]);
            }
            frontier = next;
            if (best.size > limit) return -2;
        }
        layer = [];
        for (const [S, a, k] of later) {
            if (best.has(k)) continue;
            best.set(k, cost + 1);
            layer.push([S, a]);
        }
        cost++;
    }
    return -1;
}

// Every position these chefs' moves can get a level into with nothing
// spoiled, cooked ones included, which nothing moves on from: how many there
// are, and the fewest moves to a cooked one, or -1.
export function explore(L, who) {
    const first = start(L);
    const seen = new Set([key(first)]);
    let frontier = [first];
    let moves = -1;
    for (let depth = 1; frontier.length; depth++) {
        const next = [];
        for (const S of frontier) {
            if (won(S)) continue;
            for (const chef of who) {
                for (let d = 0; d < 4; d++) {
                    const T = step(L, S, chef, d);
                    if (!T || T === 'dead') continue;
                    const k = key(T);
                    if (seen.has(k)) continue;
                    seen.add(k);
                    next.push(T);
                    if (moves < 0 && won(T)) moves = depth;
                }
            }
        }
        frontier = next;
    }
    return { positions: seen.size, moves };
}

// The fewest moves this chef makes in any way through: what the level asks of them.
export function needs(L, who, limit = 3000000) {
    const first = start(L);
    const best = new Map([[key(first), 0]]);
    let layer = [first];
    let cost = 0;
    while (layer.length) {
        const later = [];
        let frontier = layer;
        while (frontier.length) {
            const next = [];
            for (const S of frontier) {
                if (won(S)) return cost;
                for (let d = 0; d < 4; d++) {
                    const free = step(L, S, 1 - who, d);
                    if (free && free !== 'dead') {
                        const k = key(free);
                        if (!best.has(k)) { best.set(k, cost); next.push(free); }
                    }
                    const paid = step(L, S, who, d);
                    if (paid && paid !== 'dead') later.push(paid);
                }
            }
            frontier = next;
            if (best.size > limit) return -2;
        }
        layer = [];
        for (const S of later) {
            const k = key(S);
            if (best.has(k)) continue;
            best.set(k, cost + 1);
            layer.push(S);
        }
        cost++;
    }
    return -1;
}

export function report(name, text) {
    const L = parse(text);
    const both = search(L, [0, 1]);
    const a = search(L, [0]);
    const b = search(L, [1]);
    const line = [`${name}:`];
    line.push(both.moves ? `two chefs ${both.moves.length} moves (${both.states} states)` : `${both.gaveUp ? 'UNKNOWN for two: search limit' : 'NO WAY for two'} (${both.states} states)`);
    line.push(a.moves ? `chef 1 ALONE in ${a.moves.length}` : `chef 1 alone: ${a.gaveUp ? 'unknown, search limit' : 'no'} (${a.states})`);
    line.push(b.moves ? `chef 2 ALONE in ${b.moves.length}` : `chef 2 alone: ${b.gaveUp ? 'unknown, search limit' : 'no'} (${b.states})`);
    if (both.moves) line.push(`handoffs ${handoffs(L)}`);
    console.log(line.join('  |  '));
    if (both.moves) console.log('   ' + both.moves.join(' '));
    return { L, both, a, b };
}
