// The prototype catalog. Its games live in a repository of their own, whose
// build stages the cards here as prototypes.json (never served) and each game
// beside its thumbnail under static/prototypes/<id>/. Without that file the
// catalog is empty and no address is a prototype's.
import { readFileSync } from 'node:fs';

/** @typedef {{id: string, title: string, kind: string, players: string, description: string}} Prototype */

/** @param {string} [file] @returns {Prototype[]} */
export function readPrototypes(file = 'prototypes.json') {
    let text;
    try { text = readFileSync(file, 'utf8'); }
    catch (error) {
        if (/** @type {NodeJS.ErrnoException} */ (error).code === 'ENOENT') return [];
        throw error;
    }
    const cards = JSON.parse(text);
    if (!Array.isArray(cards)) throw new Error(`${file} must hold a list of prototypes`);
    for (const card of cards) {
        if (typeof card?.id !== 'string' || !/^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$/.test(card.id)) {
            throw new Error(`${file}: a prototype's id must be lowercase words joined by dashes (${JSON.stringify(card?.id)})`);
        }
        for (const field of ['title', 'kind', 'players', 'description']) {
            if (typeof card[field] !== 'string') throw new Error(`${file}: prototype ${card.id} has no ${field}`);
        }
    }
    return cards;
}

/**
 * A game's one public root, and any other spelling of its id (a retired alias
 * such as /samples/<id>/ or /<id_with_underscores>.html), which stays unserved.
 * @param {string[]} ids
 */
export function prototypePatterns(ids) {
    if (!ids.length) return { path: /(?!)/, retired: /(?!)/ };
    const words = ids.map((id) => id.split('-').join('[-_]'));
    return {
        path: new RegExp(`^/prototypes/(?:${ids.join('|')})(/.*)?$`),
        retired: new RegExp(`(?:^|[/._-])(?:${words.join('|')})(?:[/._-]|$)`, 'i'),
    };
}
