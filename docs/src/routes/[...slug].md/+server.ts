import { building, dev } from '$app/env';
import { error } from '@sveltejs/kit';
import { exportPaths, markdownExport, markdownFilename, textResponse } from '#lib/server/exports.ts';
import type { RequestHandler } from './$types';

export const prerender = true;
export const entries = () => exportPaths.map((at) => ({ slug: at === '/' ? 'index' : at.slice(1) }));

export const GET: RequestHandler = ({ params }) => {
    const at = params.slug === 'index' ? '/' : `/${params.slug}`;
    if (!exportPaths.includes(at) || (!building && !dev)) error(404, 'Documentation export not found');
    return textResponse(markdownExport(at), markdownFilename(at));
};
