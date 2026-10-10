import { existsSync } from 'node:fs';
import path from 'node:path';

export const prerender = false;

// Kept on the server: public search and client bundles contain no catalog.
const prototypes = [
    {
        id: 'aerial', title: 'Aerial', kind: 'Car soccer · Rocket League prototype', players: '1–6 players',
        description: 'Rocket-powered car soccer on the sport\'s own numbers: sprung wheels, flips, wall rides and flight. One, two or three a side, with bots in every seat nobody takes.',
        image: '/prototypes/aerial/thumbnail.png',
        horror: false,
    },
    {
        id: 'rocket-league', title: 'Afterburn', kind: 'Car soccer · Rocket League prototype', players: '1–2 players',
        description: 'Boost, powerslide and fly through a floodlit arena. Play a five-minute duel against a bot or a friend, with wall driving, aerials and sudden-death overtime.',
        image: '/prototypes/rocket-league/thumbnail.png',
        horror: false,
    },
    {
        id: 'two-forks', title: 'Two Forks', kind: 'Cooperative puzzle', players: '1–2 players',
        description: 'Two chefs, two forks, and kitchens that only yield to teamwork. A tactile puzzle journey through coast, fields, ice and fire.',
        image: '/prototypes/two-forks/thumbnail.png',
        horror: false,
    },
    {
        id: 'the-last-light', title: 'The Last Light', kind: 'PSX horror · Board game', players: '2 players',
        description: 'A candlelit table. A house that listens. Take turns stepping into the dark while your partner tries to keep you alive.',
        image: '/prototypes/the-last-light/thumbnail.png',
        horror: true,
    },
    {
        id: 'cabin-nine', title: 'Cabin Nine', kind: 'PSX horror · Deduction', players: '2 players',
        description: 'A night charter. Eight familiar passengers. Compare records with your partner and find the visitor who should never have boarded.',
        image: '/prototypes/cabin-nine/thumbnail.png',
        horror: true,
    },
    {
        id: 'dead-signal', title: 'Dead Signal', kind: 'PSX shooter · Cooperative roguelike', players: '2–4 players',
        description: 'Descend into a remade communications blacksite. Split firing angles, rescue your crew, and build an arsenal through six sectors before extracting together.',
        image: '/prototypes/dead-signal/thumbnail.png',
        horror: true,
    },
    {
        id: 'deadbell', title: 'Deadbell', kind: 'PSX roguelike · Horde combat', players: '2 players',
        description: 'Two lamplighters. A city that will not stay dead. Carve a path through the horde, bind relics, and bring your partner home.',
        image: '/prototypes/deadbell/thumbnail.png',
        horror: false,
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
