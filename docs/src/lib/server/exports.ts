// Text exports of the site's existing pages. Only prerendering/development
// calls this module: deployed downloads need no checkout, git, or source files.
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import MarkdownIt from 'markdown-it';
import { stringify } from 'yaml';
import { pages, sidebar } from '#lib/nav.ts';
import { SITE_ORIGIN, siteUrl } from '#lib/seo.ts';
import { sourcePage, type SourcePage } from './content.ts';

export const exportPaths = ['/', ...pages.map(({ link }) => link)];
export const markdownPath = (at: string) => at === '/' ? '/index.md' : `${at}.md`;
export const markdownFilename = (at: string) => at === '/guide/ai-context' ? 'TIDE.md' : markdownPath(at).split('/').pop()!;
const documents = new Map<string, SourcePage>();
function document(at: string): SourcePage {
    let result = documents.get(at);
    if (!result) {
        result = sourcePage(at);
        if (!result) throw new Error(`No documentation page: ${at}`);
        documents.set(at, result);
    }
    return result;
}

export const sourceScope = 'Website source checkout; not matched to an installed Tide release. Check tide version and the project toolchain.';
let revision: { source_revision: string; source_worktree: string; documentation_sha256: string } | undefined;
export function sourceMetadata() {
    if (!revision) {
        let commit = 'unknown';
        let worktree = 'unknown';
        try {
            commit = execFileSync('git', ['rev-parse', 'HEAD'], { encoding: 'utf8', windowsHide: true, stdio: ['ignore', 'pipe', 'ignore'] }).trim();
            worktree = execFileSync('git', ['status', '--porcelain', '--untracked-files=normal'], { encoding: 'utf8', windowsHide: true, stdio: ['ignore', 'pipe', 'ignore'] }).trim() ? 'modified' : 'clean';
        } catch { /* A source archive may have no git metadata. Say so. */ }
        const hash = createHash('sha256');
        hash.update(JSON.stringify(sidebar));
        for (const at of exportPaths) {
            const doc = document(at);
            hash.update(JSON.stringify([doc.file, doc.title, doc.description, doc.markdown]));
        }
        revision = { source_revision: commit, source_worktree: worktree, documentation_sha256: hash.digest('hex') };
    }
    return revision;
}

const md = new MarkdownIt({ html: true });
type Edit = { start: number; end: number; text: string };
function edit(source: string, edits: Edit[]): string {
    for (const change of edits.sort((a, b) => b.start - a.start)) source = source.slice(0, change.start) + change.text + source.slice(change.end);
    return source;
}
function lineOffsets(source: string): number[] {
    const offsets = [0];
    for (let i = 0; i < source.length; i++) if (source[i] === '\n') offsets.push(i + 1);
    return offsets;
}
/** Apply markup changes without ever touching fenced or indented code. */
function prose(source: string, transform: (text: string) => string): string {
    const offsets = lineOffsets(source);
    let result = '', end = 0;
    for (const token of md.parse(source, {})) {
        if (!['fence', 'code_block'].includes(token.type) || !token.map) continue;
        const start = offsets[token.map[0]];
        result += transform(source.slice(end, start));
        end = offsets[token.map[1]] ?? source.length;
        result += source.slice(start, end);
    }
    return result + transform(source.slice(end));
}
const plainHtml = (html: string) => md.utils.unescapeAll(html.replace(/<[^>]*>/g, '')).trim();

function withoutWebsite(source: string): string {
    const containers: string[] = [];
    return prose(source, (text) => text
        .replace(/<div data-tide-ai-setup><\/div>/g, '')
        .replace(/<ul\b[^>]*class="sample-grid"[^>]*>([\s\S]*?)<\/ul>/g, (_, cards: string) =>
            [...cards.matchAll(/<li><a\b[^>]*href="([^"]+)"[^>]*>([\s\S]*?)<\/a>([\s\S]*?)<\/li>/g)].map(([, href, body, links]) => {
                const field = (name: string) => plainHtml(new RegExp(`<[^>]+class="${name}"[^>]*>([\\s\\S]*?)<\\/(?:span|strong)>`).exec(body)?.[1] ?? '');
                const title = field('sample-name');
                const source = /class="sample-source"[^>]*href="([^"]+)"/.exec(links)?.[1];
                return `- [${title}](${href}): ${field('sample-kind')}. ${field('sample-description')}${source ? ` [Source](${source}).` : ''}`;
            }).join('\n') + '\n')
        .replace(/<iframe\b([^>]*)>[\s\S]*?<\/iframe>/g, (_, attrs: string) => {
            const src = /\bsrc="([^"]+)"/.exec(attrs)?.[1];
            const title = /\btitle="([^"]+)"/.exec(attrs)?.[1] ?? 'Open game';
            if (!src) return '';
            const url = new URL(md.utils.unescapeAll(src), SITE_ORIGIN);
            url.searchParams.delete('embedded');
            url.searchParams.set('stats', '');
            return `[${title}](${url.href})`;
        })
        .replace(/^ {0,3}:{3,}[ \t]*(.*)$/gm, (line, info: string) => {
            if (!info.trim()) return containers.pop() ? '' : line;
            const [name, ...rest] = info.trim().split(/\s+/);
            if (!['tip', 'info', 'warning', 'danger', 'details', 'code-group', 'tide-example', 'tide-players', 'tide-match'].includes(name)) return line;
            containers.push(name);
            if (name === 'code-group') return '';
            if (name.startsWith('tide-')) {
                const id = rest.shift();
                if (rest[0]?.startsWith('lag=')) rest.shift();
                return `**${rest.join(' ')}** ([Run example](${siteUrl(`/examples/${id}/?stats`)}))`;
            }
            const label = name[0].toUpperCase() + name.slice(1);
            return `**${label}${rest.length ? `: ${rest.join(' ')}` : ''}**`;
        }));
}

const pageAnchor = (at: string) => `doc-${at === '/' ? 'index' : at.slice(1).replaceAll('/', '--')}`;
const headingAnchor = (at: string, id: string, bundle?: Set<string>) => bundle ? `${pageAnchor(at)}--${id}` : id;

function target(href: string, from: SourcePage, bundle?: Set<string>): string {
    const url = new URL(href, siteUrl(from.file.slice('docs'.length)));
    if (url.origin !== SITE_ORIGIN) return href;
    let at = decodeURI(url.pathname).replace(/\.md$/, '').replace(/\/index$/, '').replace(/\/$/, '') || '/';
    if (!exportPaths.includes(at)) return url.href;
    let destination = document(at);
    let id = decodeURIComponent(url.hash.slice(1));
    if (id && !destination.ids.includes(id)) {
        const moved = destination.moved?.[id];
        if (!moved) throw new Error(`${from.file}: ${href} has no heading on ${at}`);
        const relocated = new URL(moved, SITE_ORIGIN);
        at = relocated.pathname;
        destination = document(at);
        id = decodeURIComponent(relocated.hash.slice(1));
        if (id && !destination.ids.includes(id)) throw new Error(`${from.file}: ${href} moved to a missing heading`);
    }
    if (bundle?.has(at) && !url.search) return `#${id ? headingAnchor(at, id, bundle) : pageAnchor(at)}`;
    return `${siteUrl(markdownPath(at))}${url.search}${id ? `#${id}` : ''}`;
}

/** Let Markdown's own destination parser handle escapes and balanced parentheses.
 * The observing rule never consumes input; inline code remains opaque. */
function links(source: string, from: SourcePage, bundle?: Set<string>): string {
    return prose(source, (text) => {
        const edits: Edit[] = [];
        const parser = new MarkdownIt({ html: true });
        parser.inline.ruler.before('link', 'export-destinations', (state) => {
            const start = state.pos + (state.src[state.pos] === '!' ? 1 : 0);
            if (state.src[start] !== '[') return false;
            const close = parser.helpers.parseLinkLabel(state, start, false);
            if (close < 0) return false;
            const reference = state.src[close + 1] === ':' && /^ {0,3}$/.test(state.src.slice(state.src.lastIndexOf('\n', start - 1) + 1, start));
            if (!reference && state.src[close + 1] !== '(') return false;
            let pos = close + 2;
            while (/\s/.test(state.src[pos] ?? '') && pos < state.src.length) pos++;
            const parsed = parser.helpers.parseLinkDestination(state.src, pos, state.src.length);
            if (parsed.ok) {
                let end = parsed.pos;
                while (/\s/.test(state.src[end] ?? '') && end < state.src.length) end++;
                if (!reference && state.src[end] !== ')') {
                    const title = parser.helpers.parseLinkTitle(state.src, end, state.src.length);
                    if (!title.ok) return false;
                    end = title.pos;
                    while (/\s/.test(state.src[end] ?? '') && end < state.src.length) end++;
                    if (state.src[end] !== ')') return false;
                }
                const mapped = target(parsed.str, from, bundle);
                if (mapped !== parsed.str) edits.push({ start: pos, end: parsed.pos, text: mapped });
            }
            return false;
        });
        // Observe HTML only when the inline parser reaches a tag, outside code.
        parser.inline.ruler.before('html_inline', 'export-html', (state) => {
            if (state.src[state.pos] !== '<') return false;
            const tag = /^<\/?[a-z][^>]*>/i.exec(state.src.slice(state.pos))?.[0];
            if (tag) for (const match of tag.matchAll(/\b(?:href|src)="([^"]+)"/g)) {
                const start = state.pos + match.index + match[0].indexOf('"') + 1;
                edits.push({ start, end: start + match[1].length, text: target(md.utils.unescapeAll(match[1]), from, bundle) });
            }
            return false;
        });
        parser.inline.parse(text, parser, {}, []);
        return edit(text, edits);
    });
}

export function markdownBody(at: string, bundle?: Set<string>): string {
    const doc = document(at);
    let body = links(withoutWebsite(doc.markdown.replace(/\r\n/g, '\n')), doc, bundle);
    const offsets = lineOffsets(body);
    const edits: Edit[] = [];
    // Explicit anchors keep the site's punctuation/duplicate/leading-digit rules
    // valid in Markdown readers with different automatic heading slugs.
    const headings = md.parse(body, {}).filter((token) => token.type === 'heading_open');
    const ids = at === '/' ? ['tide-engine', ...doc.ids] : doc.ids;
    if (headings.length !== ids.length) throw new Error(`${doc.file}: export changed the page's headings`);
    headings.forEach((token, i) => {
        const start = offsets[token.map![0]];
        edits.push({ start, end: start, text: `<a id="${headingAnchor(at, ids[i], bundle)}"></a>\n\n` });
    });
    body = edit(body, edits).trim();
    return `${bundle ? `<a id="${pageAnchor(at)}"></a>\n\n` : ''}${body}\n`;
}

export function markdownExport(at: string): string {
    const doc = document(at);
    const metadata = { title: doc.title, description: doc.description, source_url: siteUrl(at), source_file: doc.file,
        ...sourceMetadata(), documentation_scope: sourceScope };
    return `---\n${stringify(metadata)}---\n\n${markdownBody(at)}`;
}

function provenance(): string {
    const info = sourceMetadata();
    return `Source revision: ${info.source_revision} (${info.source_worktree} working tree).\nDocumentation SHA-256: ${info.documentation_sha256}.\n${sourceScope}\n`;
}

export function llmsIndex(): string {
    return `# Tide Engine\n\n> Documentation for Tide's language, deterministic simulation, multiplayer and presentation.\n\n${provenance()}\n`
        + `Start with [Using Tide with AI](${siteUrl('/guide/ai.md')}) and the [quick reference](${siteUrl('/guide/ai-context.md')}).\n\n`
        + sidebar.map((group) => `## ${group.text}\n\n` + group.items.map(({ text, link }) => `- [${text}](${siteUrl(markdownPath(link))}): ${document(link).description}`).join('\n')).join('\n\n')
        + `\n\n## Optional\n\n- [Offline documentation bundle](${siteUrl('/llms-full.txt')}): all user pages, without the language spec.\n`
        + `- [Bundle including the spec](${siteUrl('/llms-full-with-spec.txt')}): retains Decided, Provisional and Open sections; proposals are not implemented promises.\n`
        + `- [Tide skill ZIP](${siteUrl('/ai/tide-skill.zip')}): optional skill and generated short overview.\n`;
}

export function fullExport(includeSpec = false): string {
    const selected = exportPaths.filter((at) => includeSpec || at !== '/spec');
    const bundle = new Set(selected);
    return `# Tide Engine documentation\n\n${provenance()}\n`
        + (includeSpec ? 'Includes the language spec, with its Decided, Provisional and Open labels intact.\n' : `The language spec is omitted. Read [the spec separately](${siteUrl('/spec.md')}) or download [the bundle with the spec](${siteUrl('/llms-full-with-spec.txt')}).\n`)
        + '\n' + selected.map((at) => `- [${document(at).title}](#${pageAnchor(at)})`).join('\n')
        + '\n\n' + selected.map((at) => {
            const doc = document(at);
            return `---\n\nSource: ${siteUrl(markdownPath(at))}\n${doc.description}\n\n${markdownBody(at, bundle)}`;
        }).join('\n');
}

// Two or three small UTF-8 files need only ZIP's stored method. Fixed timestamps
// make the archive reproducible; CRC-32 and the central directory are standard ZIP.
function zip(entries: [string, string][]): Uint8Array<ArrayBuffer> {
    const local: Buffer[] = [], central: Buffer[] = [];
    let offset = 0;
    for (const [name, text] of entries) {
        const filename = Buffer.from(name), bytes = Buffer.from(text);
        let crc = 0xffffffff;
        for (const byte of bytes) {
            crc ^= byte;
            for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
        crc = (crc ^ 0xffffffff) >>> 0;
        const header = Buffer.alloc(30);
        header.writeUInt32LE(0x04034b50); header.writeUInt16LE(20, 4); header.writeUInt16LE(0x800, 6);
        header.writeUInt16LE(33, 12); header.writeUInt32LE(crc, 14);
        header.writeUInt32LE(bytes.length, 18); header.writeUInt32LE(bytes.length, 22); header.writeUInt16LE(filename.length, 26);
        local.push(header, filename, bytes);
        const record = Buffer.alloc(46);
        record.writeUInt32LE(0x02014b50); record.writeUInt16LE(20, 4); header.copy(record, 6, 4, 30);
        record.writeUInt32LE(offset, 42);
        central.push(record, filename);
        offset += header.length + filename.length + bytes.length;
    }
    const directory = Buffer.concat(central), end = Buffer.alloc(22);
    end.writeUInt32LE(0x06054b50); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
    end.writeUInt32LE(directory.length, 12); end.writeUInt32LE(offset, 16);
    return new Uint8Array(Buffer.concat([...local, directory, end]));
}

export function skillArchive(): Uint8Array<ArrayBuffer> {
    const entries: [string, string][] = [
        ['tide/SKILL.md', fs.readFileSync('static/ai/tide/SKILL.md', 'utf8')],
        ['tide/references/overview.md', markdownExport('/guide/ai-context')],
    ];
    if (fs.existsSync('static/ai/tide/agents/openai.yaml')) entries.push(['tide/agents/openai.yaml', fs.readFileSync('static/ai/tide/agents/openai.yaml', 'utf8')]);
    return zip(entries);
}

export function textResponse(body: string, filename?: string): Response {
    return new Response(body, { headers: {
        'content-type': filename?.endsWith('.md') ? 'text/markdown; charset=utf-8' : 'text/plain; charset=utf-8',
        'x-content-type-options': 'nosniff',
        ...(filename ? { 'content-disposition': `attachment; filename="${filename}"` } : {}),
    } });
}
