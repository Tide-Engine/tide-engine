// Reads the game's levels out of levels.tide, and says of each what the
// game's test pins: the fewest moves, and how many positions there are.
//   node levels.mjs [--ways]
import fs from 'node:fs';
import { parse, search, explore, handoffs, needs } from './solve.mjs';

const text = fs.readFileSync(new URL('../levels.tide', import.meta.url), 'utf8');
const ways = process.argv.includes('--ways');
const body = text.slice(text.indexOf('string Plan(int level)'), text.indexOf('string Name(int level)'));
const plans = [];
for (const block of body.split(/case \d+:/).slice(1)) {
    plans.push([...block.matchAll(/"([^"]*)"/g)].map(m => m[1]).join(''));
}
const names = [...text.slice(text.indexOf('string Name(int level)'), text.indexOf('string Hint(int level)')).matchAll(/case \d+: return "([^"]*)"/g)].map(m => m[1]);
const DX = [0, 1, 0, -1], DY = [-1, 0, 1, 0];
plans.forEach((plan, i) => {
    const L = parse(plan);
    // A chef's fork starts on nothing, and nobody starts on anybody
    const cells = new Map();
    const claim = (x, y, what) => {
        const k = x + ',' + y;
        if (cells.has(k)) console.log(`   !! ${what} starts on ${cells.get(k)}`);
        cells.set(k, what);
    };
    L.sausages.forEach((s, n) => { claim(s.x, s.y, 'sausage ' + n); claim(s.x + (s.v ? 0 : 1), s.y + (s.v ? 1 : 0), 'sausage ' + n); });
    L.chefs.forEach((c, n) => {
        claim(c[0], c[1], 'chef ' + n);
        claim(c[0] + DX[c[2]], c[1] + DY[c[2]], 'fork ' + n);
        const under = L.tiles[c[1] * L.w + c[0]];
        const ahead = L.tiles[(c[1] + DY[c[2]]) * L.w + c[0] + DX[c[2]]];
        if (under !== '.' && under !== 'o') console.log(`   !! chef ${n} stands on '${under}'`);
        if (ahead === '^') console.log(`   !! fork ${n} is in a rock`);
    });
    const both = explore(L, [0, 1]);
    const first = explore(L, [0]);
    const second = explore(L, [1]);
    const ok = both.moves > 0 && first.moves < 0 && second.moves < 0;
    console.log(`    {${both.moves}, ${both.positions}, ${first.positions}, ${second.positions}},   // ${i + 1}. ${names[i] ?? '?'}${ok ? '' : '   <-- NOT A LEVEL FOR TWO'}`
        + `  (${L.w} by ${L.h}; the lead changes hands ${handoffs(L)} times at least; each chef makes ${needs(L, 0)} and ${needs(L, 1)} moves at least)`);
    if (ways) console.log('       ' + search(L, [0, 1]).moves.join(' '));
});
