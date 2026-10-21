import { building, dev } from '$app/env';
import { error } from '@sveltejs/kit';
import { skillArchive } from '#lib/server/exports.ts';

export const prerender = true;
export function GET() {
    if (!building && !dev) error(404, 'Skill archive not found');
    return new Response(skillArchive(), { headers: {
        'content-type': 'application/zip',
        'content-disposition': 'attachment; filename="tide-skill.zip"',
        'x-content-type-options': 'nosniff',
    } });
}
