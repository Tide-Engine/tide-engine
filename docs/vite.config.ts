// The docs site: SvelteKit, every page prerendered from the Markdown next to
// this file, and served on Fly.io by server.js (see fly.toml).
// `npm run dev` serves it locally.

import fs from 'node:fs';
import { sveltekit } from '@sveltejs/kit/vite';
import adapter from '@sveltejs/adapter-node';
import { defineConfig, type Plugin } from 'vite';
import { headers } from './headers.js';
import { createAnalytics } from './analytics.js';
import { createAdmin } from './admin.js';
import { pages } from './src/lib/nav.ts';

// `npm run dev` serves what server.js does in production: the site's headers
// on every response, and the games in static/ at /sand/ and /duel/.
const asInProduction: Plugin = {
    name: 'as-in-production',
    configureServer(server) {
        const analytics = createAnalytics({ file: process.env.TIDE_ANALYTICS_FILE || '.svelte-kit/analytics/analytics.json', knownPaths: ['/', ...pages.map((page) => page.link)] });
        const admin = createAdmin({ analytics, password: process.env.TIDE_ADMIN_PASSWORD,
            relayUrl: process.env.TIDE_RELAY_STATS_URL, relayToken: process.env.TIDE_RELAY_STATS_TOKEN });
        admin.start();
        server.httpServer?.once('close', () => admin.stop());
        server.middlewares.use((req, res, next) => {
            for (const [name, value] of Object.entries(headers)) res.setHeader(name, value);
            const [path, query] = (req.url ?? '').split('?');
            if (path.endsWith('/') && path !== '/' && fs.existsSync(`static${path}index.html`)) {
                req.url = `${path}index.html${query === undefined ? '' : `?${query}`}`;
            }
            next();
        });
        server.middlewares.use((req, res, next) => {
            const origin = `http://${req.headers.host}`;
            admin.handle(req, res, origin).then((answered) => { if (!answered) next(); }).catch(next);
        });
    },
};

// The installers are the repo's (install.sh, install.ps1), served at the site's
// root: `curl -fsSL https://tide-engine.dev/install.sh | sh`.
const installers: Plugin = {
    name: 'installers',
    buildStart() {
        for (const name of ['install.sh', 'install.ps1']) fs.copyFileSync(`../${name}`, `static/${name}`);
    },
};

// A mistaken staging destination must fail the build before it can publish
// a private game's HTML, binary or thumbnail at an unprotected address.
const privatePrototypes: Plugin = {
    name: 'private-prototypes',
    buildStart() {
        function check(directory: string) {
            for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
                const name = `${directory}/${entry.name}`;
                if (name === 'static/admin') continue;
                if (/(?:^|[\/._-])(?:two[-_]forks|the[-_]last[-_]light|cabin[-_]nine|deadbell)(?:[\/._-]|$)/i.test(name)) {
                    throw new Error(`Private prototype asset must live under static/admin/prototypes/: ${name}`);
                }
                if (entry.isDirectory()) check(name);
            }
        }
        check('static');
    },
};

export default defineConfig(({ command }) => ({
    // Only prerendering uses the native PNG renderer; Vite must load its
    // platform binding from node_modules rather than bundle the binary.
    ssr: { external: ['@resvg/resvg-js'] },
    plugins: [
        asInProduction,
        privatePrototypes,
        installers,
        sveltekit({
            adapter: adapter(),
            prerender: {
                handleHttpError: ({ path, message }) => {
                    // The games in static/ are built by CMake and copied in;
                    // a build without them still works.
                    if (['/sand/', '/duel/', '/towers/', '/sumo/'].includes(path) || /^\/(?:examples|samples)\/[a-z][a-z0-9-]*\/$/.test(path)) return;
                    throw new Error(message);
                },
            },
            // Warnings are errors, as everywhere in the repo.
            onwarn: (warning, handler) => {
                if (command === 'build') throw new Error(`${warning.filename}:${warning.start?.line}: ${warning.message}`);
                handler(warning);
            },
        }),
    ],
    // Pages show code from the rest of the repo (`<<<`), and the grammar
    // comes from tools/tide-syntax.
    server: { fs: { allow: ['..'], deny: ['.env', '.env.*', '*.{crt,pem}', '**/.git/**', '**/static/admin/**'] } },
}));
