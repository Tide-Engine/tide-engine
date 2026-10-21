import { slugs } from '#lib/nav.ts';
import { page } from '#lib/server/content.ts';
import fs from 'node:fs';

export const entries = () => [...slugs].map((slug) => ({ slug }));

export const load = ({ params }) => ({
    ...page(params.slug),
    starterPrompt: params.slug === 'guide/ai' ? fs.readFileSync('static/ai/starter-prompt.txt', 'utf8') : undefined,
});
