// The graph playground's host for the graph editor page (tools/tide-graph,
// whose messages are listed at the top of its src/app.js): the document in
// memory and in the browser's storage, the browser tidels behind the page's
// requests, its diagnostics sent on. Shared by the page and its Node test.
//
// `server` speaks to tidels: open(text) and change(text) send the document,
// request(method, params) resolves with tidels's answer; the server calls
// host.published(diagnostics) for what tidels publishes, and host.stopped(why)
// when it can't go on. `toPage(message)` sends to the editor page.
export const GRAPH_URI = 'file:///game.tidegraph';
export const DRAFT_KEY = 'tide-graph-playground-draft';

/**
 * @typedef {{ open(text: string): void, change(text: string): void, request(method: string, params: object | null): Promise<unknown> }} GraphServer
 * @typedef {Pick<Storage, 'getItem' | 'setItem' | 'removeItem'>} DraftStorage
 * @typedef {{ type?: string, text?: string, id?: number, method?: string, params?: object | null, message?: string }} PageMessage
 * @typedef {{
 *   server: GraphServer, toPage: (message: object) => void, starter: string, schema?: object | null,
 *   storage?: DraftStorage | null, edited?: (text: string) => void, showTide?: (text: string | null) => void,
 * }} GraphHostOptions
 */
export class GraphHost {
    /** @param {GraphHostOptions} options */
    constructor({ server, toPage, starter, schema = null, storage = null, edited = () => {}, showTide = () => {} }) {
        this.server = server;
        this.toPage = toPage;
        this.starter = starter;
        this.schema = schema;
        this.storage = storage;
        this.edited = edited;
        this.showTide = showTide;
        this.text = this.draft() ?? starter;
        this.opened = false;
        /** @type {unknown[]} */
        this.diagnostics = [];
    }

    // The visitor's draft, or null when there's none (or no storage)
    draft() {
        try {
            const saved = this.storage?.getItem(DRAFT_KEY);
            return typeof saved === 'string' && saved.trim() ? saved : null;
        } catch {
            return null;
        }
    }

    keep() {
        try {
            if (this.text === this.starter) this.storage?.removeItem(DRAFT_KEY);
            else this.storage?.setItem(DRAFT_KEY, this.text);
        } catch {}
    }

    /** A message from the page @param {PageMessage} m */
    receive(m) {
        if (!m || typeof m !== 'object') return;
        switch (m.type) {
            case 'ready':
                // tidels has the document before the page's first request
                if (!this.opened) {
                    this.opened = true;
                    this.server.open(this.text);
                }
                this.toPage({ type: 'load', text: this.text, uri: GRAPH_URI, name: 'game.tidegraph', schema: this.schema });
                if (this.diagnostics.length) this.toPage({ type: 'diagnostics', diagnostics: this.diagnostics });
                return;
            case 'edit':
                if (typeof m.text !== 'string' || m.text === this.text) return;
                this.text = m.text;
                this.keep();
                this.server.change(m.text);
                this.edited(m.text);
                return;
            case 'request':
                this.answer(m.id, m.method, m.params);
                return;
            case 'clipboardWrite':
                if (typeof m.text === 'string') globalThis.navigator?.clipboard?.writeText(m.text).catch(() => {});
                return;
            case 'showTide':
                this.showTide(m.text ?? null);
                return;
            case 'save':
                this.keep();
                return;
            case 'error':
                console.warn(`Tide graph: ${m.message}`);
                return;
            default:
                return;
        }
    }

    /** @param {number | undefined} id @param {string | undefined} method @param {object | null | undefined} params */
    async answer(id, method, params) {
        try {
            let result;
            if (method === 'host/clipboardRead') {
                const clipboard = globalThis.navigator?.clipboard;
                if (!clipboard?.readText) throw new Error('no clipboard here');
                result = await clipboard.readText();
            } else if (typeof method === 'string' && method.startsWith('tide/')) {
                result = await this.server.request(method, params ?? null);
            } else {
                throw new Error(`the playground has no ${method}`);
            }
            this.toPage({ type: 'response', id, result: result ?? null });
        } catch (error) {
            this.toPage({ type: 'response', id, error: error instanceof Error ? error.message : String(error) });
        }
    }

    /** What tidels published for the document @param {unknown[] | undefined} diagnostics */
    published(diagnostics) {
        this.diagnostics = diagnostics ?? [];
        this.toPage({ type: 'diagnostics', diagnostics: this.diagnostics });
    }

    /** @param {string} why */
    stopped(why) {
        this.toPage({ type: 'server', available: false, message: why });
    }

    /** The document replaced from outside the page: Reset, or a graph given to it @param {string} text */
    replace(text) {
        if (text === this.text) return;
        this.text = text;
        this.keep();
        this.server.change(text);
        this.toPage({ type: 'text', text });
        this.edited(text);
    }
}
