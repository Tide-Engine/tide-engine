import path from 'node:path';
import { fail } from '@sveltejs/kit';
import { createAccessService } from '#lib/server/access.ts';
import type { Actions, PageServerLoad } from './$types';

export const prerender = false;

const access = createAccessService({
    file: process.env.TIDE_ACCESS_FILE || path.resolve('.svelte-kit/access/access-requests.jsonl'),
});

export const load: PageServerLoad = ({ setHeaders }) => {
    setHeaders({ 'cache-control': 'private, no-store' });
};

export const actions: Actions = {
    default: async ({ request, getClientAddress }) => {
        const { status, ...result } = await access.submit(request, getClientAddress());
        return result.success ? result : fail(status, result);
    },
};
