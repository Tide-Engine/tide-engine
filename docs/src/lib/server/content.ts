// The docs' pages, from the Markdown files in docs/. Every page is
// prerendered, so this only runs at build time (and in `npm run dev`).

import fs from 'node:fs';
import path from 'node:path';
import { parse } from 'yaml';
import { pages, type Link } from '#lib/nav.ts';
import { language, render, type Heading } from './markdown.ts';

const sources = import.meta.glob(['/**/*.md', '!/node_modules/**', '!/.svelte-kit/**', '!/build/**', '!/static/**'], {
    query: '?raw',
    import: 'default',
    eager: true,
}) as Record<string, string>;

/** `/guide/index.md` is `/guide`, `/spec.md` is `/spec`. */
function pathOf(file: string): string {
    return file.replace(/\.md$/, '').replace(/\/index$/, '') || '/';
}

const files = new Map(Object.keys(sources).map((file) => [pathOf(file), file]));

// A page missing from the sidebar couldn't be reached, and a sidebar link
// without a page would be dead.
const problems = [
    ...[...files.keys()].filter((p) => p !== '/' && !pages.some((page) => page.link === p)).map((p) => `${files.get(p)} isn't in the sidebar (src/lib/nav.ts)`),
    ...pages.filter((page) => !files.has(page.link)).map((page) => `the sidebar's ${page.link} has no Markdown file`),
];
if (problems.length) throw new Error(`The docs' pages don't match their sidebar:\n  ${problems.join('\n  ')}`);

const isPage = (p: string) => files.has(p);

function frontmatter(source: string): { data: Record<string, unknown>; body: string } {
    const match = /^---\r?\n([\s\S]*?)\r?\n---\r?\n/.exec(source);
    return match ? { data: parse(match[1]) ?? {}, body: source.slice(match[0].length) } : { data: {}, body: source };
}

// `<<< @/../demo/demo.tide` shows a file of the repo as code (`@` is docs/).
function snippets(source: string, file: string): string {
    return source.replace(/^<<< @\/(\S+)[ \t]*\r?$/gm, (_, relative: string) => {
        const where = path.resolve(relative);
        if (!fs.existsSync(where)) throw new Error(`${file}: <<< @/${relative}: no such file`);
        const code = fs.readFileSync(where, 'utf8').replace(/\r\n/g, '\n').replace(/\n$/, '');
        const longest = Math.max(0, ...(code.match(/^`+/gm) ?? []).map((run) => run.length));
        const fence = '`'.repeat(Math.max(3, longest + 1));
        return `${fence}${language(path.extname(where).slice(1))}\n${code}\n${fence}`;
    });
}

function renderFile(file: string) {
    const { data, body } = frontmatter(sources[file]);
    return { data, ...render(snippets(body, file), file.slice(1), isPage) };
}

export interface SourcePage {
    path: string;
    file: string;
    title: string;
    description: string;
    markdown: string;
    ids: string[];
    moved?: Record<string, string>;
}

/** The same authored page and real source includes used by the website. */
export function sourcePage(at: string): SourcePage | undefined {
    const file = files.get(at);
    if (!file) return undefined;
    const { body } = frontmatter(sources[file]);
    // The browser supplies this editor's source separately from the Markdown.
    let markdown = snippets(body.replace(/<div data-tide-playground><\/div>/g, '<<< @/playground/demo.tide'), file);
    const rendered = renderFile(file);
    const info = at === '/' ? home() : page(at.slice(1));
    if (at === '/') {
        const { hero, features } = info as Home;
        markdown = `# ${hero.name}\n\n${hero.text}. ${hero.tagline}\n\n`
            + hero.actions.map((action) => `[${action.text}](${action.link})`).join(' · ') + '\n\n'
            + features.map((feature) => `**${feature.title}**: ${feature.details}`).join('\n\n') + '\n\n' + markdown;
    }
    return { path: at, file: `docs${file}`, title: info.title, description: info.description,
        markdown, ids: rendered.ids, moved: at === '/' ? undefined : (info as Page).moved };
}

/** A sentence or two from the start of the page, for search engines. */
function describe(text: string): string {
    text = text.replace(/\s+/g, ' ').trim();
    if (text.length <= 160) return text;
    const cut = text.slice(0, 160);
    return `${cut.slice(0, cut.lastIndexOf(' '))}…`;
}

// A page that was split keeps its address, and the sections that left it are
// on the pages after `to`. A link from before the split, `/game/views#stencil`,
// names an anchor its page no longer has, so the page sends it on to the one
// that has that heading now (the docs' +page.svelte). `gone` is for a heading
// that became its page's title: the anchor it had, and that page.
const split: Record<string, { to: string[]; gone: Record<string, string> }> = {
    '/multiplayer/matches': {
        to: ['/packages/rooms'],
        gone: {},
    },
    '/game/views': {
        to: ['/game/views-and-the-match', '/game/draw-settings', '/game/meshes-and-textures', '/game/3d', '/game/cameras', '/game/render-targets'],
        gone: { 'targets-and-post-processing': '/game/render-targets', _3d: '/game/3d' },
    },
};

/** Where the anchors a split page no longer has lead, by anchor. */
function moved(at: string, own: string[]): Record<string, string> | undefined {
    const parts = split[at];
    if (!parts) return undefined;
    const result: Record<string, string> = {};
    for (const [id, to] of Object.entries(parts.gone)) {
        if (!files.has(to)) throw new Error(`${at}#${id} moved to ${to}, which is no page`);
        if (own.includes(id)) throw new Error(`${at}#${id} is said to have moved to ${to}, and is still a heading of ${at}`);
        result[id] = to;
    }
    for (const to of parts.to) {
        if (!files.has(to)) throw new Error(`${at}'s sections moved to ${to}, which is no page`);
        for (const id of renderFile(files.get(to)!).ids) {
            if (!own.includes(id)) result[id] ??= `${to}#${id}`;
        }
    }
    return result;
}

export interface Page {
    path: string;
    title: string;
    description: string;
    html: string;
    headings: Heading[];
    /** Anchors this page had before its sections went to other pages, and where each is now. */
    moved?: Record<string, string>;
    prev?: Link;
    next?: Link;
}

const cache = new Map<string, Page>();

export function page(slug: string): Page {
    const at = `/${slug}`;
    let result = cache.get(at);
    if (!result) {
        const file = files.get(at)!;
        const rendered = renderFile(file);
        const index = pages.findIndex((p) => p.link === at);
        result = {
            path: at,
            title: rendered.title || pages[index].text,
            description: typeof rendered.data.description === 'string' && rendered.data.description.trim()
                ? rendered.data.description.trim()
                : describe(rendered.sections.find((section) => section.text.trim())?.text ?? `Learn about ${rendered.title} in Tide Engine.`),
            html: rendered.html,
            headings: rendered.headings,
            moved: moved(at, rendered.ids),
            prev: pages[index - 1],
            next: pages[index + 1],
        };
        cache.set(at, result);
    }
    return result;
}

export interface Home {
    title: string;
    description: string;
    hero: {
        name: string;
        text: string;
        tagline: string;
        actions: { theme: 'brand' | 'alt'; text: string; link: string }[];
    };
    features: { title: string; details: string }[];
    html: string;
}

/** The home page: `index.md`, its hero and features in its front matter. */
export function home(): Home {
    const { data, html } = renderFile('/index.md');
    const hero = data.hero as Home['hero'];
    return {
        title: typeof data.title === 'string' && data.title.trim() ? data.title.trim() : `${hero.name}: ${hero.text.toLowerCase()}`,
        description: typeof data.description === 'string' ? data.description.trim() : describe(`${hero.text}. ${hero.tagline}`),
        hero, features: data.features as Home['features'], html,
    };
}

export interface Entry {
    /** The page's path, and the section's anchor. */
    id: string;
    titles: string[];
    text: string;
}

/** What search looks through: every page, a section per heading. */
export function searchIndex(): Entry[] {
    return [...files.entries()]
        .filter(([at]) => at !== '/')
        .flatMap(([at, file]) =>
            renderFile(file).sections.map((section) => ({ id: section.id ? `${at}#${section.id}` : at, titles: section.titles, text: section.text })),
        );
}
