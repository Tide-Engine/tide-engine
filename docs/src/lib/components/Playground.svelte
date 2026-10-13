<script lang="ts">
    import { onMount, tick } from 'svelte';
    import original from '../../../playground/demo.tide?raw';

    let source = $state(original);
    let automatic = $state(true);
    let status = $state('Edit the code, then compile to see your changes.');
    let diagnostics = $state('');
    let logs = $state<string[]>([]);
    let failed = $state(false);
    let building = $state(false);
    let ready = $state(false);
    let frame: HTMLIFrameElement;
    let editor: HTMLTextAreaElement;
    let worker: Worker | undefined;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let revision = 0;
    let queued = false;
    let fresh = false;
    let disposed = false;
    let pending: { id: number; wasm: Uint8Array; pack: Uint8Array } | undefined;
    let previewUrl = $state<string | undefined>();

    function apply() {
        if (!ready || !pending) return;
        const build = pending;
        pending = undefined;
        if (fresh) {
            // Reset code is deliberately a full restart; normal edits migrate.
            frame.contentWindow?.postMessage({ type: 'tide-build', ...build, fresh: true }, location.origin);
            fresh = false;
        } else frame.contentWindow?.postMessage({ type: 'tide-build', ...build }, location.origin);
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
                    if (!building) return;
                    status = data.text;
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
                } else status = 'Code changed while building.';
                if (queued) compile();
            };
            worker.onerror = (event) => {
                building = false;
                failed = true;
                status = 'Compiler stopped · try compiling again.';
                diagnostics = event.message || 'This browser could not start the compiler.';
                worker?.terminate();
                worker = undefined;
            };
        }
        worker.postMessage({ id: revision, source });
    }

    function changed() {
        revision++;
        pending = undefined;
        clearTimeout(timer);
        if (automatic) { status = 'Code changed · waiting to compile…'; timer = setTimeout(compile, 700); }
        else status = 'Code changed · compile to apply it.';
    }

    function keyboard(event: KeyboardEvent) {
        if ((event.ctrlKey || event.metaKey) && event.key === 'Enter') {
            event.preventDefault();
            compile();
        } else if (event.key === 'Tab' && !event.shiftKey) {
            event.preventDefault();
            editor.setRangeText('    ', editor.selectionStart, editor.selectionEnd, 'end');
            source = editor.value;
            changed();
        }
    }

    function restart() {
        logs = [];
        frame.contentWindow?.postMessage({ type: 'tide-restart', id: revision }, location.origin);
    }

    async function reset() {
        source = original;
        revision++;
        fresh = true;
        await tick();
        editor.scrollTop = editor.scrollLeft = 0;
        compile();
    }

    onMount(() => {
        const backend = new URL(location.href).searchParams.get('backend');
        previewUrl = '/playground/runtime.html' + (backend === 'webgl' || backend === 'webgpu' ? `?backend=${backend}` : '');
        const message = ({ origin, source: sender, data }: MessageEvent) => {
            if (origin !== location.origin || sender !== frame?.contentWindow) return;
            if (data.type === 'tide-ready') {
                ready = true;
                apply();
            } else if (data.type === 'tide-applied' && data.id === revision) {
                status = data.operation === 'tide-restart'
                    ? `Restarted the last successful build.${failed ? ' Code still has errors.' : ''}`
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
        return () => {
            disposed = true;
            clearTimeout(timer);
            worker?.terminate();
            removeEventListener('message', message);
        };
    });
</script>

<section class="playground" aria-label="Interactive Tide playground">
    <div class="toolbar">
        <div class="actions">
            <button class="primary" onclick={compile} disabled={building}>{building ? 'Compiling…' : 'Compile & reload'}</button>
            <button onclick={restart} disabled={!ready}>Restart</button>
            <button onclick={reset} disabled={building}>Reset code</button>
        </div>
        <label class="automatic"><input type="checkbox" bind:checked={automatic} onchange={() => { if (!automatic) { clearTimeout(timer); queued = false; } }} /> Auto reload</label>
    </div>
    <div class="panels">
        <div class="source">
            <div class="panel-title"><label for="playground-source">game.tide</label><span>Ctrl / ⌘ Enter to compile</span></div>
            <textarea id="playground-source" bind:this={editor} bind:value={source} oninput={changed} onkeydown={keyboard}
                spellcheck="false" autocapitalize="off" autocomplete="off" aria-label="Tide source code"></textarea>
        </div>
        <div class="preview">
            <div class="panel-title"><span>Live preview</span><span class="live">{ready ? '● Running' : 'Starting…'}</span></div>
            <iframe bind:this={frame} src={previewUrl} title="Tide playground live preview" allow="cross-origin-isolated"></iframe>
            <div class="preview-hint">The bounce counter survives a hot reload.</div>
        </div>
    </div>
    <div class="status" class:error={failed} role="status"><span class:busy={building}>{building ? '◌' : failed ? '!' : '●'}</span>{status}</div>
    {#if diagnostics}
        <pre class="diagnostics" class:error={failed} aria-label="Compiler diagnostics">{diagnostics}</pre>
    {/if}
    {#if logs.length}
        <details class="console"><summary>Game output</summary><pre>{logs.join('\n')}</pre></details>
    {/if}
    <noscript>This playground needs JavaScript and WebAssembly. You can read the example source in the repository.</noscript>
</section>

<style>
    .playground { margin: 28px 0 36px; border: 1px solid var(--divider); border-radius: 12px; overflow: hidden; background: var(--bg-alt); }
    .toolbar { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 14px; border-bottom: 1px solid var(--divider); }
    .actions { display: flex; flex-wrap: wrap; gap: 8px; }
    button { padding: 8px 12px; border: 1px solid var(--border); border-radius: 6px; color: var(--text-1); background: var(--bg); font-size: 12px; font-weight: 600; cursor: pointer; }
    button.primary { background: var(--brand-fill); color: var(--brand-text); border-color: var(--brand-fill); }
    button:hover:enabled { border-color: var(--brand-1); }
    button:disabled { opacity: .5; cursor: default; }
    button:focus-visible, textarea:focus-visible { outline: 2px solid var(--brand-1); outline-offset: -2px; }
    .automatic { display: flex; align-items: center; gap: 6px; white-space: nowrap; color: var(--text-2); font-size: 12px; }
    input { accent-color: var(--brand-fill); }
    .panels { display: grid; grid-template-columns: 1fr 1fr; }
    .source, .preview { min-width: 0; }
    .source { border-right: 1px solid var(--divider); }
    .panel-title { display: flex; justify-content: space-between; align-items: center; gap: 12px; height: 42px; padding: 0 16px; color: var(--text-2); font-size: 12px; border-bottom: 1px solid var(--divider); }
    .panel-title label { font-family: var(--mono); font-weight: 600; color: var(--text-1); }
    .panel-title span + span { font-size: 11px; }
    .live { color: var(--tip); }
    textarea { display: block; width: 100%; height: 480px; resize: vertical; padding: 18px; border: 0; border-radius: 0; outline: none; background: var(--code-bg); color: var(--text-1); font-family: var(--mono); font-size: 12px; line-height: 1.65; tab-size: 4; white-space: pre; overflow: auto; }
    iframe { display: block; width: 100%; height: 440px; border: 0; background: #161618; }
    .preview-hint { display: grid; place-items: center; height: 40px; padding: 0 12px; font-size: 11px; color: var(--text-2); }
    .status { display: flex; align-items: center; gap: 8px; border-top: 1px solid var(--divider); padding: 12px 16px; font-size: 12px; color: var(--text-2); }
    .status > span { color: var(--tip); }
    .status.error, .status.error > span { color: var(--danger); }
    .busy { color: var(--brand-1) !important; }
    .diagnostics, .console pre { margin: 0; padding: 12px 16px; border-top: 1px solid var(--divider); background: var(--code-bg); color: var(--text-1); border-radius: 0; white-space: pre-wrap; overflow-wrap: anywhere; max-height: 240px; overflow-y: auto; font-size: 12px; }
    .diagnostics.error { color: var(--danger); }
    .console { border-top: 1px solid var(--divider); font-size: 12px; }
    .console summary { padding: 10px 16px; cursor: pointer; color: var(--text-2); }
    @media (max-width: 1050px) {
        .panels { grid-template-columns: 1fr; }
        .source { border-right: 0; border-bottom: 1px solid var(--divider); }
        textarea { height: 360px; }
        iframe { height: 350px; }
    }
    @media (max-width: 600px) {
        .toolbar { align-items: flex-start; flex-direction: column; gap: 10px; }
        .panel-title span:not(.live) { font-size: 10px; }
        textarea { padding: 12px; }
    }
</style>
