<script lang="ts">
    // The graph playground: the IDEs' graph editor (tools/tide-graph, its own
    // files) in a frame, with this page as its host (playground/graph-host.mjs):
    // the browser tidels checks the graph, the browser compiler builds it as
    // game.tidegraph, and the preview runs it as the text playground's does.
    import { goto } from '$app/navigation';
    import { onMount } from 'svelte';
    import { GraphHost, GRAPH_URI } from '../../../playground/graph-host.mjs';
    import { theme } from '#lib/theme.svelte.ts';
    import PlaygroundModes from './PlaygroundModes.svelte';

    // The editor's own files, in the order its files.json gives
    const editorFiles = import.meta.glob<string>(['../../../../tools/tide-graph/src/*.js', '../../../../tools/tide-graph/style.css'],
        { query: '?raw', import: 'default' });
    const EDITOR = '../../../../tools/tide-graph/';

    let automatic = $state(true);
    let status = $state('Edit the graph, then press Play to see your changes.');
    let diagnostics = $state('');
    let logs = $state<string[]>([]);
    let failed = $state(false);
    let building = $state(false);
    let ready = $state(false);
    let editorReady = $state(false);
    let expanded = $state(false);
    let split = $state(70); // The graph's share of the width, in percent
    let tide = $state<string | null>(null); // Show as Tide's text, while it's shown
    let editorPage = $state<string | undefined>();
    let previewUrl = $state<string | undefined>();
    let section: HTMLElement;
    let panels: HTMLDivElement;
    let frame: HTMLIFrameElement;
    let editorFrame = $state<HTMLIFrameElement>();
    let dialog: HTMLDialogElement;
    let host: GraphHost | undefined;
    let starter = ''; // The starter graph (playground/stars.tidegraph), loaded with the editor
    let worker: Worker | undefined;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let revision = 0;
    let queued = false;
    let fresh = false;
    let disposed = false;
    let pending: { id: number; wasm: Uint8Array; pack: Uint8Array } | undefined;

    const SPLIT_KEY = 'tide-graph-playground-split';
    // The Tide a graph stands for, handed to the text playground (Playground.svelte)
    const HANDOFF_KEY = 'tide-playground-from-graph';

    // tidels in its worker, as GraphHost speaks to it
    class WorkerServer {
        worker = new Worker(new URL('../../../playground/language-worker.mjs', import.meta.url), { type: 'module' });
        waiting = new Map<number, (result: unknown) => void>();
        next = 1;
        owner: () => GraphHost | undefined;
        constructor(owner: () => GraphHost | undefined) {
            this.owner = owner;
            this.worker.onmessage = ({ data }) => {
                if (data.failed) this.owner()?.stopped(data.failed);
                else if (data.diagnostics) this.owner()?.published(data.diagnostics);
                if (data.id !== undefined) {
                    const answer = this.waiting.get(data.id);
                    this.waiting.delete(data.id);
                    answer?.(data.result);
                }
            };
            this.worker.onerror = event => this.owner()?.stopped(event.message || 'the language server could not start');
        }
        open(text: string) { this.worker.postMessage({ method: 'open', params: { text, uri: GRAPH_URI } }); }
        change(text: string) { this.worker.postMessage({ method: 'textDocument/didChange', params: { text } }); }
        request(method: string, params: object | null) {
            return new Promise(answer => {
                const id = this.next++;
                this.waiting.set(id, answer);
                this.worker.postMessage({ id, method, params: params ?? {} });
            });
        }
        dispose() { this.worker.terminate(); }
    }
    let server: WorkerServer | undefined;

    function toEditor(message: object) {
        editorFrame?.contentWindow?.postMessage({ tideGraph: message }, location.origin);
    }

    // The site's colours, as the editor's (its theme message, as JetBrains sends)
    function sendTheme() {
        const css = getComputedStyle(document.documentElement);
        const v = (name: string) => css.getPropertyValue(name).trim();
        toEditor({
            type: 'theme', dark: theme.dark, font: v('--font'), colors: {
                bg: v('--bg'), panel: v('--bg-alt'), fg: v('--text-1'), muted: v('--text-2'), border: v('--divider'),
                accent: v('--brand-1'), select: v('--brand-soft'), hover: v('--bg-soft'), 'input-bg': v('--bg'),
                'input-border': v('--border'), 'node-bg': v('--bg-elv'), 'node-border': v('--border'),
                error: v('--danger'), warning: v('--warning'), mark: v('--brand-soft'), mono: v('--mono'),
            },
        });
    }

    // The editor's page: its style and scripts, then the frame's side of the host
    async function makeEditorPage() {
        const files = (await import('../../../../tools/tide-graph/files.json')).default as { styles: string[]; scripts: string[] };
        const read = (name: string) => {
            const load = editorFiles[EDITOR + name];
            if (!load) throw new Error(`The site has no ${name} of the graph editor`);
            return load();
        };
        const [styles, scripts] = await Promise.all([Promise.all(files.styles.map(read)), Promise.all(files.scripts.map(read))]);
        const inline = (text: string) => text.replace(/<\/(script|style)/gi, '<\\/$1');
        const glue = `(function () {
    var origin = ${JSON.stringify(location.origin)};
    var app = TideGraph.start({ post: function (m) { parent.postMessage({ tideGraph: m }, origin); } });
    addEventListener('message', function (e) {
        if (e.source !== parent || e.origin !== origin || !e.data) return;
        if (e.data.tideGraph) TideGraph.receive(e.data.tideGraph);
        if (e.data.tideGraphStart) requestAnimationFrame(start);
    });
    // The first view: the start of the graph shown, at a size to read
    function start() {
        var g = app.editor && app.editor.graph;
        var nodes = g && g.nodes;
        if (!nodes || !nodes.length) return;
        var x0 = Math.min.apply(null, nodes.map(function (n) { return Number(n.x) || 0; }));
        var y0 = Math.min.apply(null, nodes.map(function (n) { return Number(n.y) || 0; }));
        var c = app.canvas;
        c.zoom = 0.75;
        c.pan = { x: 24 - x0 * c.zoom, y: 24 - y0 * c.zoom };
        c.applyTransform();
    }
    // The panes' sizes for the frame's size (the editor's are a whole window's)
    var narrow = null;
    function fit() {
        var now = innerWidth < 640;
        if (now === narrow) return;
        narrow = now;
        app.sizes.left = now ? 0 : Math.max(140, Math.min(220, Math.round(innerWidth * 0.18)));
        app.sizes.right = now ? 0 : Math.max(200, Math.min(290, Math.round(innerWidth * 0.24)));
        app.sizes.bottom = now ? 140 : Math.max(110, Math.min(200, Math.round(innerHeight * 0.24)));
        // A phone's frame is the graph's: the Tide panel waits for its button
        app.showPanel = !now;
        app.layoutPanes();
        if (app.editor) app.renderPanel();
    }
    fit();
    addEventListener('resize', fit);
})();`;
        // Phones: the declarations above the graph, what's selected below it.
        const narrow = `@media (max-width: 639px) {
    .tg-main { grid-template-columns: 1fr; grid-template-rows: 84px minmax(0, 1fr) 120px; }
    .tg-main > .tg-split { display: none; }
    .tg-outline { border-bottom: 1px solid var(--tg-border); }
    .tg-inspector { border-top: 1px solid var(--tg-border); }
    .tg-toolbar { flex-wrap: wrap; }
    .tg-search { width: calc(100vw - 16px); }
}`;
        return `<!doctype html><html lang="en" class="${theme.dark ? 'tg-dark' : 'tg-light'}"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>Tide graph</title>
${styles.map(s => `<style>${inline(s)}</style>`).join('\n')}<style>${narrow}</style></head><body>
${scripts.map(s => `<script>${inline(s)}<\/script>`).join('\n')}
<script>${glue}<\/script></body></html>`;
    }

    // The editor's page said something: GraphHost answers, and the page is ready once it asked for the document
    function fromEditor(message: { type?: string }) {
        if (!host) return;
        if (message.type === 'ready') {
            sendTheme();
            host.receive(message);
            editorReady = true;
            Object.assign(section, { tideGraph: { host, frame: editorFrame } });
            reveal();
            return;
        }
        host.receive(message);
    }

    // The starter's most telling code on first sight
    function reveal() {
        try {
            const fall = JSON.parse(host!.text).decls?.find((d: { kind: string; name: string }) => d.kind === 'system' && d.name === 'Fall');
            if (!fall) return;
            toEditor({ type: 'reveal', node: fall.id });
            editorFrame?.contentWindow?.postMessage({ tideGraphStart: true }, location.origin);
        } catch {}
    }

    // A draft from an earlier visit isn't the starter the preview starts with
    function runDraft() {
        if (ready && host && host.text !== starter) {
            fresh = true;
            compile();
        }
    }

    function apply() {
        if (!ready || !pending) return;
        const build = pending;
        pending = undefined;
        frame.contentWindow?.postMessage({ type: 'tide-build', ...build, fresh: fresh || undefined }, location.origin);
        fresh = false;
    }

    function compile() {
        clearTimeout(timer);
        if (building) { queued = true; return; }
        queued = false;
        building = true;
        failed = false;
        diagnostics = '';
        status = 'Loading compiler…';
        if (!worker) {
            worker = new Worker(new URL('../../../playground/worker.mjs', import.meta.url), { type: 'module' });
            worker.onmessage = ({ data }) => {
                if (disposed) return;
                if (data.type === 'progress') {
                    if (building) status = data.text;
                    return;
                }
                building = false;
                if (data.id === revision) {
                    if (data.type === 'built') {
                        diagnostics = data.diagnostics;
                        status = `Built in ${data.seconds.toFixed(2)}s · applying changes…`;
                        pending = { id: data.id, wasm: data.wasm, pack: data.pack };
                        apply();
                    } else {
                        failed = true;
                        status = 'Build failed · the previous game is still running.';
                        diagnostics = data.text;
                    }
                } else status = 'Graph changed while building.';
                if (queued) compile();
            };
            worker.onerror = event => {
                building = false;
                failed = true;
                status = 'Compiler stopped · try again.';
                diagnostics = event.message || 'This browser could not start the compiler.';
                worker?.terminate();
                worker = undefined;
            };
        }
        worker.postMessage({ id: revision, source: host?.text, name: 'game.tidegraph' });
    }

    function changed() {
        revision++;
        pending = undefined;
        clearTimeout(timer);
        if (automatic) { status = 'Graph changed · waiting to build…'; timer = setTimeout(compile, 700); }
        else status = 'Graph changed · press Play to run it.';
    }

    function restart() {
        logs = [];
        frame.contentWindow?.postMessage({ type: 'tide-restart', id: revision }, location.origin);
    }

    function reset() {
        fresh = true;
        if (host && host.text !== starter) host.replace(starter); // An edit: the revision moves on
        else revision++;
        compile();
    }

    function showTide(text: string | null) {
        tide = text;
        dialog.showModal();
    }

    function openAsText() {
        if (!tide) return;
        try { sessionStorage.setItem(HANDOFF_KEY, tide); } catch {}
        dialog.close();
        goto('/guide/playground');
    }

    function toggleExpanded() {
        expanded = !expanded;
    }

    function keyboard(event: KeyboardEvent) {
        if (event.key === 'Escape' && expanded && !event.defaultPrevented && !dialog.open) expanded = false;
    }

    function drag(event: PointerEvent) {
        const handle = event.currentTarget as HTMLElement;
        handle.setPointerCapture(event.pointerId);
        const box = panels.getBoundingClientRect();
        const move = (e: PointerEvent) => {
            split = Math.round(Math.min(80, Math.max(35, (e.clientX - box.left) / box.width * 100)));
        };
        const up = () => {
            handle.removeEventListener('pointermove', move);
            handle.removeEventListener('pointerup', up);
            try { localStorage.setItem(SPLIT_KEY, String(split)); } catch {}
        };
        handle.addEventListener('pointermove', move);
        handle.addEventListener('pointerup', up);
    }

    function nudge(event: KeyboardEvent) {
        const step = event.key === 'ArrowLeft' ? -2 : event.key === 'ArrowRight' ? 2 : 0;
        if (!step) return;
        event.preventDefault();
        split = Math.min(80, Math.max(35, split + step));
        try { localStorage.setItem(SPLIT_KEY, String(split)); } catch {}
    }

    $effect(() => {
        void theme.dark;
        if (editorReady) sendTheme();
    });

    onMount(() => {
        try {
            const saved = Number(localStorage.getItem(SPLIT_KEY));
            if (saved >= 35 && saved <= 80) split = saved;
        } catch {}
        let storage: Storage | null = null;
        try { storage = localStorage; } catch {}
        previewUrl = '/playground/runtime.html?stats&demo=stars';
        const message = ({ origin, source, data }: MessageEvent) => {
            if (origin !== location.origin || !data) return;
            if (editorFrame && source === editorFrame.contentWindow && data.tideGraph) { fromEditor(data.tideGraph); return; }
            if (source !== frame?.contentWindow) return;
            if (data.type === 'tide-ready') {
                ready = true;
                runDraft();
                apply();
            } else if (data.type === 'tide-applied' && data.id === revision) {
                status = data.operation === 'tide-restart'
                    ? `Restarted the last successful build.${failed ? ' The graph still has errors.' : ''}`
                    : 'Running · your changes are live.';
            } else if (data.type === 'tide-error') {
                failed = true;
                diagnostics = data.text;
                status = 'Preview could not load this build.';
            } else if (data.type === 'tide-log') {
                logs = [...logs.slice(-11), data.text];
            }
        };
        addEventListener('message', message);
        Promise.all([import('../../../playground/stars.tidegraph?raw'), makeEditorPage(), import('../../../../compiler/src/graph_schema.json')]).then(([graph, page, schema]) => {
            if (disposed) return;
            starter = graph.default;
            server = new WorkerServer(() => host);
            host = new GraphHost({ server, toPage: toEditor, starter, schema: schema.default, storage, edited: changed, showTide });
            editorPage = page;
            runDraft();
        }, error => {
            status = 'The graph editor could not load.';
            failed = true;
            diagnostics = error?.message ?? String(error);
        });
        return () => {
            disposed = true;
            clearTimeout(timer);
            worker?.terminate();
            server?.dispose();
            removeEventListener('message', message);
        };
    });
</script>

<svelte:window onkeydown={keyboard} />

<section class="playground graph-playground" class:expanded bind:this={section} aria-label="Interactive Tide graph playground">
    <div class="toolbar">
        <div class="actions">
            <button class="primary" onclick={compile} disabled={building || !editorReady}>{building ? 'Building…' : '▶ Play'}</button>
            <button onclick={restart} disabled={!ready}>Restart</button>
            <button onclick={reset} disabled={building || !editorReady}>Reset graph</button>
        </div>
        <div class="actions">
            <label class="automatic"><input type="checkbox" bind:checked={automatic} onchange={() => { if (!automatic) { clearTimeout(timer); queued = false; } }} /> Auto reload</label>
            <PlaygroundModes mode="graph" />
            <button class="expand" onclick={toggleExpanded} aria-pressed={expanded} title={expanded ? 'Back to the page (Esc)' : 'Fill the window'}>
                <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
                    {#if expanded}
                        <path d="M6 2v4H2M10 2v4h4M6 14v-4H2M10 14v-4h4" fill="none" stroke="currentColor" stroke-width="1.5" />
                    {:else}
                        <path d="M2 6V2h4M14 6V2h-4M2 10v4h4M14 10v4h-4" fill="none" stroke="currentColor" stroke-width="1.5" />
                    {/if}
                </svg>
                {expanded ? 'Exit' : 'Expand'}
            </button>
        </div>
    </div>
    <div class="panels" bind:this={panels} style:--split="{split}%">
        <div class="source">
            <div class="panel-title"><span class="file">game.tidegraph</span><span>Space or a right click adds a node · Show as Tide for its text</span></div>
            <div class="editor">
                {#if editorPage}
                    <iframe class="graph" bind:this={editorFrame} srcdoc={editorPage} title="Tide graph editor"></iframe>
                {/if}
                {#if !editorReady}<div class="loading" aria-hidden="true">Loading the graph editor…</div>{/if}
            </div>
        </div>
        <!-- svelte-ignore a11y_no_noninteractive_tabindex, a11y_no_noninteractive_element_interactions -->
        <div class="divider" role="separator" aria-orientation="vertical" aria-label="Resize the graph and the game" aria-valuenow={split}
            aria-valuemin={35} aria-valuemax={80} tabindex="0" onpointerdown={drag} onkeydown={nudge}></div>
        <div class="preview">
            <div class="panel-title"><span>Live preview</span><span class="live">{ready ? '● Running' : 'Starting…'}</span></div>
            <iframe bind:this={frame} src={previewUrl} title="Tide graph playground live preview" allow="cross-origin-isolated"></iframe>
            <div class="preview-hint">Move the pointer, or click the game and press A and D.</div>
        </div>
    </div>
    <div class="status" class:error={failed} role="status"><span class:busy={building}>{building ? '◌' : failed ? '!' : '●'}</span>{status}</div>
    {#if diagnostics}
        <pre class="diagnostics" class:error={failed} aria-label="Compiler diagnostics">{diagnostics}</pre>
    {/if}
    {#if logs.length}
        <details class="console"><summary>Game output</summary><pre>{logs.join('\n')}</pre></details>
    {/if}
    <dialog class="tide" bind:this={dialog} aria-label="The graph as Tide" onclose={() => { tide = null; }}>
        <div class="dialog-title"><span class="file">game.tide</span><span>The Tide this graph stands for</span></div>
        <pre>{tide ?? 'The graph has not been checked yet: wait a moment, then try again.'}</pre>
        <div class="dialog-actions">
            <button class="primary" onclick={openAsText} disabled={!tide}>Open in the text playground</button>
            <button onclick={() => tide && navigator.clipboard?.writeText(tide)} disabled={!tide}>Copy</button>
            <button onclick={() => dialog.close()}>Close</button>
        </div>
    </dialog>
    <noscript>This playground needs JavaScript and WebAssembly.</noscript>
</section>

<style>
    .playground { margin: 28px 0 36px; border: 1px solid var(--divider); border-radius: 12px; overflow: hidden; background: var(--bg-alt); }
    .playground.expanded { position: fixed; inset: var(--header-h) 0 0 0; z-index: 35; margin: 0; border: 0; border-radius: 0; display: flex; flex-direction: column; overflow-y: auto; }
    .toolbar { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 14px; border-bottom: 1px solid var(--divider); }
    .actions { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; }
    button { padding: 8px 12px; border: 1px solid var(--border); border-radius: 6px; color: var(--text-1); background: var(--bg); font-size: 12px; font-weight: 600; cursor: pointer; }
    button.primary { background: var(--brand-fill); color: var(--brand-text); border-color: var(--brand-fill); }
    button:hover:enabled { border-color: var(--brand-1); }
    button:disabled { opacity: .5; cursor: default; }
    button:focus-visible, .divider:focus-visible { outline: 2px solid var(--brand-1); outline-offset: -2px; }
    .expand { display: flex; align-items: center; gap: 6px; }
    .automatic { display: flex; align-items: center; gap: 6px; margin-right: 6px; white-space: nowrap; color: var(--text-2); font-size: 12px; }
    input { accent-color: var(--brand-fill); }
    .panels { display: grid; grid-template-columns: minmax(0, var(--split)) 6px minmax(0, 1fr); }
    .expanded .panels { flex: 1; min-height: 460px; }
    .source, .preview { min-width: 0; display: flex; flex-direction: column; }
    .divider { cursor: col-resize; touch-action: none; background: var(--divider); border-left: 2px solid var(--bg-alt); border-right: 2px solid var(--bg-alt); }
    .divider:hover { background: var(--brand-1); }
    .panel-title { display: flex; justify-content: space-between; align-items: center; gap: 12px; height: 42px; flex: none; padding: 0 16px; color: var(--text-2); font-size: 12px; border-bottom: 1px solid var(--divider); white-space: nowrap; overflow: hidden; }
    .panel-title .file, .dialog-title .file { font-family: var(--mono); font-weight: 600; color: var(--text-1); }
    .panel-title span + span { font-size: 11px; overflow: hidden; text-overflow: ellipsis; }
    .live { color: var(--tip); }
    .editor { position: relative; height: 620px; background: var(--bg); }
    .expanded .editor { height: auto; flex: 1; }
    .editor iframe { position: absolute; inset: 0; height: 100%; background: var(--bg); }
    .loading { position: absolute; inset: 0; display: grid; place-items: center; color: var(--text-2); font-size: 13px; }
    iframe { display: block; width: 100%; height: 520px; border: 0; background: #161618; }
    .preview iframe { flex: 1 0 auto; } /* As tall as the graph beside it */
    .expanded .preview iframe { height: auto; flex: 1; }
    .preview-hint { display: grid; place-items: center; height: 40px; flex: none; padding: 0 12px; font-size: 11px; color: var(--text-2); text-align: center; }
    .status { display: flex; align-items: center; gap: 8px; flex: none; border-top: 1px solid var(--divider); padding: 12px 16px; font-size: 12px; color: var(--text-2); }
    .status > span { color: var(--tip); }
    .status.error, .status.error > span { color: var(--danger); }
    .busy { color: var(--brand-1) !important; }
    .diagnostics, .console pre { margin: 0; padding: 12px 16px; border-top: 1px solid var(--divider); background: var(--code-bg); color: var(--text-1); border-radius: 0; white-space: pre-wrap; overflow-wrap: anywhere; max-height: 240px; overflow-y: auto; font-size: 12px; flex: none; }
    .diagnostics.error { color: var(--danger); }
    .console { border-top: 1px solid var(--divider); font-size: 12px; flex: none; }
    .console summary { padding: 10px 16px; cursor: pointer; color: var(--text-2); }
    dialog.tide { width: min(880px, calc(100vw - 32px)); max-height: calc(100vh - 64px); padding: 0; border: 1px solid var(--divider); border-radius: 12px; background: var(--bg-alt); color: var(--text-1); box-shadow: var(--shadow-3); }
    dialog.tide[open] { display: flex; flex-direction: column; }
    dialog.tide::backdrop { background: var(--backdrop); }
    .dialog-title { display: flex; justify-content: space-between; gap: 12px; padding: 12px 16px; font-size: 12px; color: var(--text-2); border-bottom: 1px solid var(--divider); }
    dialog.tide pre { flex: 1; min-height: 0; margin: 0; padding: 14px 16px; overflow: auto; border-radius: 0; background: var(--code-bg); font-family: var(--mono); font-size: 13px; line-height: 1.5; tab-size: 4; }
    .dialog-actions { display: flex; flex-wrap: wrap; gap: 8px; padding: 12px 16px; border-top: 1px solid var(--divider); }
    @media (max-width: 1050px) {
        .panels { grid-template-columns: 1fr; }
        .divider { display: none; }
        .source { border-bottom: 1px solid var(--divider); }
        .editor { height: 560px; }
        iframe { height: 350px; }
        .expanded .panels { display: flex; flex-direction: column; }
        .expanded .source, .expanded .preview { flex: 1; }
    }
    @media (max-width: 600px) {
        .toolbar { align-items: flex-start; flex-direction: column; gap: 10px; }
        .panel-title span:not(.live):not(.file) { font-size: 10px; }
        .editor { height: 600px; }
    }
</style>
