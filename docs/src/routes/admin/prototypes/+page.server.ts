import { existsSync } from 'node:fs';
import path from 'node:path';
import { readPrototypes } from '../../../../prototypes.js';

export const prerender = false;

// Kept on the server: public search and client bundles contain no catalog.
// The cards come from the prototypes' own repository (prototypes.js).
export function load() {
    return {
        prototypes: readPrototypes().map((prototype) => ({
            ...prototype,
            image: `/prototypes/${prototype.id}/thumbnail.png`,
            href: `/prototypes/${prototype.id}/`,
            available: ['build/client', 'static'].some((root) => existsSync(path.resolve(root, `prototypes/${prototype.id}/index.html`))),
        })),
    };
}
