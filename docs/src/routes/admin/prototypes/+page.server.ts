import { existsSync } from 'node:fs';
import path from 'node:path';

export const prerender = false;

// Kept on the server: public search and client bundles contain no catalog.
const prototypes = [
    {
        id: 'two-forks', title: 'Two Forks', kind: 'Cooperative puzzle', players: '1–2 players',
        description: 'Two chefs, two forks, and kitchens that only yield to teamwork. A tactile puzzle journey through coast, fields, ice and fire.',
        image: '/admin/prototypes/two-forks/thumbnail.png',
        horror: false,
    },
    {
        id: 'the-last-light', title: 'The Last Light', kind: 'PSX horror · Board game', players: '2 players',
        description: 'A candlelit table. A house that listens. Take turns stepping into the dark while your partner tries to keep you alive.',
        image: '/admin/prototypes/the-last-light/thumbnail.png',
        horror: true,
    },
];

export function load() {
    return {
        prototypes: prototypes.map((prototype) => ({
            ...prototype,
            href: `/admin/prototypes/${prototype.id}/`,
            available: ['build/client', 'static'].some((root) => existsSync(path.resolve(root, `admin/prototypes/${prototype.id}/index.html`))),
        })),
    };
}
