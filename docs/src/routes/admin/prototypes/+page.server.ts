import { existsSync } from 'node:fs';
import path from 'node:path';

export const prerender = false;

// Kept on the server: public search and client bundles contain no catalog.
const prototypes = [
    {
        id: 'underfoot', title: 'Underfoot', kind: 'Puzzle · Carry the floor', players: '1–2 players',
        description: 'Two pavers over a long drop, and never quite enough stone. Lift a loose stone, lay it over nothing, and get both to their lanterns. Eighteen puzzles with crates, ice, plates and portals you carry; alone you work both pavers.',
        image: '/prototypes/underfoot/thumbnail.png',
    },
    {
        id: 'inside-out', title: 'Inside Out', kind: 'Cooperative puzzle · Nested rooms', players: '2 players',
        description: 'Step into a pocket room and see the workshop beyond its walls. Move and nest rooms together, follow recursive doorways, and solve twelve puzzles built around planning and shared spaces.',
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
