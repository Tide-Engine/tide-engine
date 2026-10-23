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
import { createTelemetry } from './telemetry.js';
import { prototypePatterns, readPrototypes } from './prototypes.js';
import { pages } from './src/lib/nav.ts';

// `npm run dev` serves what server.js does in production: the site's headers
// on every response, and the games in static/ at /sand/ and /duel/.
const asInProduction: Plugin = {
    name: 'as-in-production',
    configureServer(server) {
        const analytics = createAnalytics({ file: process.env.TIDE_ANALYTICS_FILE || '.svelte-kit/analytics/analytics.json', knownPaths: ['/', ...pages.map((page) => page.link)] });
        const telemetry = createTelemetry({ file: process.env.TIDE_TELEMETRY_FILE || '.svelte-kit/telemetry/telemetry.db' });
        const admin = createAdmin({ analytics, telemetry, password: process.env.TIDE_ADMIN_PASSWORD, prototypes: readPrototypes().map((card) => card.id),
            relayUrl: process.env.TIDE_RELAY_STATS_URL, relayToken: process.env.TIDE_RELAY_STATS_TOKEN });
        admin.start();
        telemetry.start();
        server.httpServer?.once('close', () => { void admin.stop(); telemetry.stop(); });
        server.middlewares.use((req, res, next) => {
            for (const [name, value] of Object.entries(headers)) res.setHeader(name, value);
            const [path, query] = (req.url ?? '').split('?');
            if (path.endsWith('/') && path !== '/' && fs.existsSync(`static${path}index.html`)) {
                req.url = `${path}index.html${query === undefined ? '' : `?${query}`}`;
            }
            next();
        });
        // Vite would otherwise serve guide/foo.md straight from the source
        // tree before SvelteKit sees its Markdown export endpoint.
        server.middlewares.use((req, res, next) => {
            const pathname = new URL(req.url ?? '/', 'http://localhost').pathname;
            const at = pathname === '/index.md' ? '/' : pathname.endsWith('.md') ? pathname.slice(0, -3) : undefined;
            if (!at || (at !== '/' && !pages.some((page) => page.link === at)) || !['GET', 'HEAD'].includes(req.method ?? 'GET')) return next();
            server.ssrLoadModule('/src/lib/server/exports.ts').then(({ markdownExport, markdownFilename, textResponse }) => {
                const response = textResponse(markdownExport(at), markdownFilename(at));
                res.statusCode = response.status;
                response.headers.forEach((value: string, name: string) => res.setHeader(name, value));
                return response.text();
            }).then((body) => res.end(req.method === 'HEAD' ? undefined : body)).catch(next);
        });
        server.middlewares.use((req, res, next) => {
            const origin = `http://${req.headers.host}`;
            telemetry.handle(req, res).then((answered) => answered || admin.handle(req, res, origin))
                .then((answered) => { if (!answered) next(); }).catch(next);
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

// Stage prototype files at their shareable URLs, keeping retired aliases out
// of new deployments. The catalog itself still belongs under /admin.
const prototypeAssets: Plugin = {
    name: 'prototype-assets',
    buildStart() {
        const { retired } = prototypePatterns(readPrototypes().map((card) => card.id));
        function check(directory: string) {
            for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
                const name = `${directory}/${entry.name}`;
                if (name === 'static/admin' || name === 'static/prototypes') continue;
                if (retired.test(name)) {
                    throw new Error(`Prototype asset must live under static/prototypes/: ${name}`);
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
        prototypeAssets,
        installers,
        sveltekit({
            adapter: adapter(),
            prerender: {
                handleHttpError: ({ path, message }) => {
                    // The games in static/ are built by CMake and copied in;
                    // a build without them still works.
                    if (['/sand/', '/duel/', '/towers/', '/sumo/', '/earth/'].includes(path) || /^\/(?:examples|samples)\/[a-z][a-z0-9-]*\/$/.test(path)) return;
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
