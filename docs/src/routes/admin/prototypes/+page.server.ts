import { existsSync } from 'node:fs';
import path from 'node:path';

export const prerender = false;

// Kept on the server: public search and client bundles contain no catalog.
const prototypes = [
    {
        id: 'copper-coil', title: 'Copper & Coil', kind: 'Cooperative puzzle · Logic circuits', players: '1–2 players',
        description: 'Two maintenance robots restore a forgotten power station. Rewire AND, OR, XOR and NOT consoles, test every signal combination, and commission the bridge together. Six chambers with online, shared-keyboard and solo play.',
        image: '/prototypes/copper-coil/thumbnail.png',
    },
    {
        id: 'deadweight', title: 'DEADWEIGHT', kind: 'Cooperative salvage · Physics', players: '1–2 players',
        description: 'Two workers extract enormous machinery from an abandoned pumping station. Rig winches, balance lifting lines, walk on your cargo and use a loose ballast as a counterweight. Three physical salvage jobs, online or on a shared keyboard.',
        image: '/prototypes/deadweight/thumbnail.png',
    },
    {
        id: 'swindle', title: 'SWINDLE', kind: 'Competitive heist · Bluff and steal', players: '1–4 players',
        description: 'A very civil rooftop robbery. Steal a prism whose value grows while you carry it, dash into a friend to snatch it, and send a fake carrier running while you vanish. Cash out at the moving getaway. Ninety-second duels, free-for-all and two-versus-two, with bots, friend rooms and same-keyboard play.',
        image: '/prototypes/swindle/thumbnail.png',
    },
    {
        id: 'foldline', title: 'FOLDLINE', kind: 'First-person cooperative puzzle · Portals', players: '1–2 players',
        description: 'Fold space together in a sunlit research atrium. Place linked portals, look through them into the other side, and carry a resonance cube across the divide. Play both operators alone or solve the chambers with a partner.',
        image: '/prototypes/foldline/thumbnail.png',
    },
    {
        id: 'underfoot', title: 'Underfoot', kind: 'Puzzle · Carry the floor', players: '1–2 players',
        description: 'Two pavers over a long drop, and never quite enough stone. Lift a loose stone, lay it over nothing, and get both to their lanterns. Eighteen puzzles with crates, ice, plates and portals you carry; alone you work both pavers.',
        image: '/prototypes/underfoot/thumbnail.png',
    },
    {
        id: 'inside-out', title: 'Inside Out', kind: 'Cooperative puzzle · Nested rooms', players: '2 players',
        description: 'Two small robots start inside a box within another room and find their way out together. One rule moves everything: whatever you walk into is pushed, a whole row at a time, and only a wall or your partner stops it, which is how you get inside a box. Nine puzzles with batteries, sockets and rooms that hold copies of themselves, on a painted tabletop model.',
        image: '/prototypes/inside-out/thumbnail.png',
    },
    {
        id: 'cracked', title: 'CRACKED', kind: 'Arena shooter · roguelike upgrades', players: '1–8 players',
        description: 'Two teams of fishbowl-headed mages trade bouncing spells across arenas that rotate between rounds. The losing side picks upgrade cards. A port of the Unity game, with bots when you\'re alone.',
        image: '/prototypes/cracked/thumbnail.png',
    },
    {
        id: 'two-forks', title: 'Two Forks', kind: 'Cooperative puzzle', players: '1–2 players',
        description: 'Two chefs, two forks, and kitchens that only yield to teamwork. A tactile puzzle journey through coast, fields, ice and fire.',
        image: '/prototypes/two-forks/thumbnail.png',
    },
];

export function load() {
    return {
        prototypes: prototypes.map((prototype) => ({
            ...prototype,
            href: `/prototypes/${prototype.id}/`,
            available: ['build/client', 'static'].some((root) => existsSync(path.resolve(root, `prototypes/${prototype.id}/index.html`))),
        })),
    };
}
