import { defineParams } from '@sveltejs/kit/params';
export const params = defineParams({ console: value => ['dashboard', 'device'].includes(value) ? value : undefined });
