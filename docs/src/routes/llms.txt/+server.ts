import { building, dev } from '$app/env';
import { error } from '@sveltejs/kit';
import { llmsIndex, textResponse } from '#lib/server/exports.ts';

export const prerender = true;
export function GET() {
    if (!building && !dev) error(404, 'Documentation export not found');
    return textResponse(llmsIndex());
}
