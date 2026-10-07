import { building, dev } from '$app/env';
import { error } from '@sveltejs/kit';
import { renderSocialImage, socialCard, socialEntries } from '#lib/server/social-image.ts';
import type { RequestHandler } from './$types';

// All cards are PNG files in the deployed build. Crawlers need no JavaScript
// and the production server never needs a font or a native image renderer.
export const prerender = true;
export const entries = () => socialEntries.map((slug) => ({ slug }));

export const GET: RequestHandler = async ({ params }) => {
    const card = socialCard(params.slug);
    if (!card || (!building && !dev)) error(404, 'Social preview not found');
    return new Response(await renderSocialImage(card), {
        headers: {
            'content-type': 'image/png',
            'cache-control': 'public, max-age=3600',
            'x-content-type-options': 'nosniff',
        },
    });
};
