// Makes up small levels, keeps the ones two chefs can do and one can't, and
// takes out of them whatever they don't need.
//   node mine.mjs <seed> <count> <mode> <out> [minMoves] [maxMoves]
import { parse, search, handoffs, needs, step, start } from './solve.mjs';
import fs from 'node:fs';

const [, , seedArg = '1', countArg = '3000', modeArg = 'plain', outArg = 'mined.txt', minArg = '7', maxArg = '18'] = process.argv;
let seed = Number(seedArg) >>> 0;
const rnd = n => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return Math.floor(seed / 4294967296 * n); };
const count = Number(countArg);
const gas = modeArg.includes('gas');
const two = modeArg.includes('two');      // two sausages
const one = modeArg.includes('one');      // one island
const LIMIT = 30000;
const MIN = Number(minArg), MAX = Number(maxArg);

function make() {
    const w = 5 + rnd(5), h = 4 + rnd(3);
    const g = Array.from({ length: h }, () => Array(w).fill('~'));
    const rect = (ch) => {
        const rw = 2 + rnd(3), rh = 2 + rnd(3);
        const x0 = rnd(w - rw + 1), y0 = rnd(h - rh + 1);
        for (let y = y0; y < y0 + rh; y++) for (let x = x0; x < x0 + rw; x++) g[y][x] = ch;
    };
    const islands = one ? 1 + rnd(2) : 2 + rnd(2);
    for (let i = 0; i < islands; i++) rect('.');
    if (!one && rnd(2)) { const x = 2 + rnd(w - 4); for (let y = 0; y < h; y++) g[y][x] = '~'; }
    const grills = 1 + rnd(4);
    for (let i = 0; i < grills; i++) {
        const x = rnd(w), y = rnd(h);
        g[y][x] = gas && rnd(2) ? '=' : '#';
        if (rnd(2)) { const x2 = Math.min(w - 1, x + rnd(2)), y2 = Math.min(h - 1, y + (x2 === x ? 1 : 0)); g[y2][x2] = g[y][x]; }
    }
    if (gas) {
        for (let tries = 0; tries < 20; tries++) { const x = rnd(w), y = rnd(h); if (g[y][x] === '.') { g[y][x] = 'o'; break; } }
        if (!g.flat().includes('=')) return null;
        if (!g.flat().includes('o')) return null;
    }
    if (rnd(5) === 0) { const x = rnd(w), y = rnd(h); if (g[y][x] === '.') g[y][x] = '^'; }
    const free = (x, y) => x >= 0 && y >= 0 && x < w && y < h && g[y][x] === '.';
    const sausages = two ? 2 : 1;
    for (let i = 0; i < sausages; i++) {
        let ok = false;
        for (let tries = 0; tries < 30 && !ok; tries++) {
            const x = rnd(w), y = rnd(h), v = rnd(2);
            const x2 = x + (v ? 0 : 1), y2 = y + (v ? 1 : 0);
            if (!free(x, y) || !free(x2, y2)) continue;
            g[y][x] = v ? '|' : '-'; g[y2][x2] = v ? '|' : '-';
            ok = true;
        }
        if (!ok) return null;
    }
    const DX = [0, 1, 0, -1], DY = [-1, 0, 1, 0];
    for (let c = 0; c < 2; c++) {
        let ok = false;
        for (let tries = 0; tries < 40 && !ok; tries++) {
            const x = rnd(w), y = rnd(h), d = rnd(4);
            const fx = x + DX[d], fy = y + DY[d];
            if (!free(x, y)) continue;
            const ahead = fx < 0 || fy < 0 || fx >= w || fy >= h ? '~' : g[fy][fx];
            if (ahead !== '.' && ahead !== '~' && ahead !== '#' && ahead !== '=') continue;
            g[y][x] = (c ? 'NESW' : 'nesw')[d];
            if (ahead === '.') g[fy][fx] = '!';
            ok = true;
        }
        if (!ok) return null;
    }
    return g.map(r => r.join('').replaceAll('!', '.')).join('/');
}

// How often each chef's move moves a sausage, and whether the gas ever changes, along a way through
function along(L, moves) {
    let S = start(L);
    const pushes = [0, 0];
    let gasChanges = 0;
    const pedal = (x, y) => x >= 0 && y >= 0 && x < L.w && y < L.h && L.tiles[y * L.w + x] === 'o';
    const lit = T => !(pedal(T.c[0], T.c[1]) || pedal(T.c[3], T.c[4]));
    for (const move of moves) {
        const who = Number(move[0]), d = 'NESW'.indexOf(move[1]);
        const T = step(L, S, who, d);
        for (let i = 0; i < S.s.length; i += 7) if (S.s[i] !== T.s[i] || S.s[i + 1] !== T.s[i + 1]) { pushes[who]++; break; }
        if (lit(S) !== lit(T)) gasChanges++;
        S = T;
    }
    return { pushes, gasChanges };
}

function measure(text, wanted) {
    const L = parse(text);
    if (L.sausages.length !== wanted || !L.chefs[0] || !L.chefs[1]) return null;
    const both = search(L, [0, 1], LIMIT);
    if (!both.moves || both.moves.length < MIN || both.moves.length > MAX) return null;
    if (search(L, [0], LIMIT).moves) return null;
    if (search(L, [1], LIMIT).moves) return null;
    if (gas) {
        // The pedal has to matter: with grass in its place, there's no way through
        const plain = parse(text.replaceAll('o', '.'));
        if (search(plain, [0, 1], LIMIT).moves) return null;
    }
    const seen = along(L, both.moves);
    if (seen.pushes[0] === 0 || seen.pushes[1] === 0) {
        if (!gas) return null;
    }
    return { text, moves: both.moves.length, states: both.states, path: both.moves.join(' '), L, pushes: seen.pushes, gasChanges: seen.gasChanges };
}

function grade(m) {
    m.hand = handoffs(m.L, 600000);
    m.need0 = needs(m.L, 0, 600000);
    m.need1 = needs(m.L, 1, 600000);
    m.score = m.hand * 100 + Math.min(m.need0, m.need1) * 10 - m.states / 2000;
    return m;
}

// Whatever a level doesn't need goes: a tile at a time becomes water, while
// the level stays as hard and asks as much of both.
function trim(m, wanted) {
    let best = m;
    let changed = true;
    while (changed) {
        changed = false;
        const rows = best.text.split('/').map(r => r.split(''));
        for (let y = 0; y < rows.length; y++) {
            for (let x = 0; x < rows[y].length; x++) {
                const c = rows[y][x];
                if (c !== '.' && c !== '#' && c !== '=' && c !== '^') continue;
                rows[y][x] = '~';
                const t = measure(rows.map(r => r.join('')).join('/'), wanted);
                if (t) {
                    grade(t);
                    if (t.hand >= best.hand && Math.min(t.need0, t.need1) >= Math.min(best.need0, best.need1) && t.moves >= best.moves) {
                        best = t;
                        changed = true;
                        continue;
                    }
                }
                rows[y][x] = c;
            }
        }
    }
    let rows = best.text.split('/');
    const wet = r => r.split('').every(c => c === '~');
    while (rows.length && wet(rows[0])) rows.shift();
    while (rows.length && wet(rows[rows.length - 1])) rows.pop();
    while (rows.every(r => r[0] === '~')) rows = rows.map(r => r.slice(1));
    while (rows.every(r => r[r.length - 1] === '~')) rows = rows.map(r => r.slice(0, -1));
    const out = measure(rows.join('/'), wanted);
    return out ? grade(out) : best;
}

const found = [];
let tried = 0;
const wanted = two ? 2 : 1;
for (let i = 0; i < count; i++) {
    const text = make();
    if (!text) continue;
    tried++;
    const m = measure(text, wanted);
    if (m) found.push(grade(m));
}
found.sort((a, b) => b.score - a.score);
const seenTexts = new Set();
const top = [];
for (const m of found.slice(0, 40)) {
    const t = trim(m, wanted);
    if (seenTexts.has(t.text)) continue;
    seenTexts.add(t.text);
    top.push(t);
}
top.sort((a, b) => a.moves - b.moves || b.hand - a.hand);
const lines = top.map(f => `hand ${f.hand} needs ${f.need0}/${f.need1} pushes ${f.pushes.join('/')} gas ${f.gasChanges} moves ${f.moves} states ${f.states}\n${f.text.split('/').join('\n')}\n${f.path}\n`);
fs.writeFileSync(outArg, `mode ${modeArg} seed ${seedArg}: tried ${tried}, kept ${found.length}\n\n` + lines.join('\n'));
console.log(`${modeArg}: tried ${tried}, kept ${found.length}`);
