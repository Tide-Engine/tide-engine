import { existsSync } from 'node:fs';
import path from 'node:path';

export const prerender = false;

// Kept on the server: public search and client bundles contain no catalog.
const prototypes = [
    {
        id: 'gravewake', title: 'GRAVEWAKE', kind: 'First-person cooperative survival · Zombies', players: '1–4 players',
        description: 'The dead rise in Ashwick, round after round. Hold the village with swords, axes, hammers, bows, crossbows and magic staffs off the walls and out of the Pilgrim’s Chest; board up the windows, open the way through the tavern, the smithy, the chapel and the graveyard, light the beacon, drink the alchemist’s brews and runeforge your weapon. Up to four together.',
        image: '/prototypes/gravewake/thumbnail.png',
    },
    {
        id: 'sunday-supper', title: 'SUNDAY SUPPER', kind: 'First-person cooperative horror · Story', players: '1–2 players',
        description: 'Your car dies on a back road in Kansas, and the only light for miles is the Hale farm. Walt and Ruth take you in for the weekend. A first playable slice: the road, supper with the Hales, a night in their guest room and Saturday morning in the barn. Best with a friend.',
        image: '/prototypes/sunday-supper/thumbnail.png',
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
