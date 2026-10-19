import { defineConfig } from 'vite';
import { sveltekit } from '@sveltejs/kit/vite';
import adapter from '@sveltejs/adapter-node';

export default defineConfig({
    plugins: [sveltekit({
        files: { params: 'src/params.js' },
        adapter: adapter({ envPrefix: 'TIDE_CONSOLE_' }),
        csp: { mode: 'nonce', directives: {
            'default-src': ['self'], 'script-src': ['self'], 'style-src': ['self'],
            'connect-src': ['self'], 'img-src': ['self'], 'font-src': ['self'],
            'base-uri': ['none'], 'frame-ancestors': ['none'], 'form-action': ['self'],
        } },
    })],
});
