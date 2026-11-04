<script lang="ts">
    import { onMount } from 'svelte';
    import original from '../../../playground/demo.tide?raw';
    import { theme } from '#lib/theme.svelte.ts';
    import type { Editor, Problem } from './playground/editor';

    let automatic = $state(true);
    let status = $state('Edit the code, then compile to see your changes.');
    let diagnostics = $state('');
    let logs = $state<string[]>([]);
    let failed = $state(false);
    let building = $state(false);
    let ready = $state(false);
    let expanded = $state(false);
    let split = $state(60); // The code's share of the width, in percent
    let problems = $state<Problem[]>([]);
    let editorReady = $state(false);
    let section: HTMLElement;
    let host: HTMLDivElement;
    let panels: HTMLDivElement;
    let frame: HTMLIFrameElement;
    let editor = $state.raw<Editor | undefined>();
    let worker: Worker | undefined;
    let timer: ReturnType<typeof setTimeout> | undefined;
    let revision = 0;
    let queued = false;
    let fresh = false;
    let disposed = false;
    let pending: { id: number; wasm: Uint8Array; pack: Uint8Array } | undefined;
    let previewUrl = $state<string | undefined>();

    const SPLIT_KEY = 'tide-playground-split';
    const source = () => editor?.value ?? original;

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
        worker.postMessage({ id: revision, source: source() });
    }

    function changed() {
        revision++;
        pending = undefined;
        clearTimeout(timer);
        if (automatic) { status = 'Code changed · waiting to compile…'; timer = setTimeout(compile, 700); }
        else status = 'Code changed · compile to apply it.';
    }

    function restart() {
        logs = [];
        frame.contentWindow?.postMessage({ type: 'tide-restart', id: revision }, location.origin);
    }

    function reset() {
        fresh = true;
        if (editor && editor.value !== original) editor.setValue(original); // A change: the revision moves on
        else revision++;
        compile();
    }

    function toggleExpanded() {
        expanded = !expanded;
        editor?.focus();
    }

    function keyboard(event: KeyboardEvent) {
        if (event.key === 'Escape' && expanded && !event.defaultPrevented) {
            // Monaco's widgets (suggestions, find) take Escape first.
            const widget = document.querySelector('.monaco-editor .suggest-widget.visible, .monaco-editor .find-widget.visible');
            if (!widget) expanded = false;
        }
    }

    // Dragging the divider between the code and the game.
    function drag(event: PointerEvent) {
        const handle = event.currentTarget as HTMLElement;
        handle.setPointerCapture(event.pointerId);
        const box = panels.getBoundingClientRect();
        const move = (e: PointerEvent) => {
            split = Math.round(Math.min(78, Math.max(30, (e.clientX - box.left) / box.width * 100)));
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
        split = Math.min(78, Math.max(30, split + step));
        try { localStorage.setItem(SPLIT_KEY, String(split)); } catch {}
    }

    $effect(() => {
        const dark = theme.dark;
        editor?.setDark(dark);
    });

    onMount(() => {
        try {
            const saved = Number(localStorage.getItem(SPLIT_KEY));
            if (saved >= 30 && saved <= 78) split = saved;
        } catch {}
        const backend = new URL(location.href).searchParams.get('backend');
        previewUrl = '/playground/runtime.html?stats' + (backend === 'webgl' || backend === 'webgpu' ? `&backend=${backend}` : '');
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
        import('./playground/editor').then(({ createEditor }) => createEditor(host, {
            value: original, dark: theme.dark, changed, compile,
            problems: list => { problems = list; },
        })).then(created => {
            if (disposed) { created.dispose(); return; }
            editor = created;
            editorReady = true;
            // For the docs' browser tests: the code, as the editor has it.
            Object.assign(section, { tideEditor: created });
        });
        return () => {
            disposed = true;
            clearTimeout(timer);
            worker?.terminate();
            editor?.dispose();
            removeEventListener('message', message);
        };
    });
</script>

<svelte:window onkeydown={keyboard} />

<section class="playground" class:expanded bind:this={section} aria-label="Interactive Tide playground">
    <div class="toolbar">
        <div class="actions">
            <button class="primary" onclick={compile} disabled={building}>{building ? 'Compiling…' : 'Compile & reload'}</button>
            <button onclick={restart} disabled={!ready}>Restart</button>
            <button onclick={reset} disabled={building}>Reset code</button>
        </div>
        <div class="actions">
            <label class="automatic"><input type="checkbox" bind:checked={automatic} onchange={() => { if (!automatic) { clearTimeout(timer); queued = false; } }} /> Auto reload</label>
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
            <div class="panel-title"><span class="file">game.tide</span><span>Ctrl / ⌘ Enter to compile · Ctrl Space for suggestions</span></div>
            <div class="editor" bind:this={host}>
                {#if !editorReady}<pre class="loading" aria-hidden="true">{original}</pre>{/if}
            </div>
        </div>
        <!-- svelte-ignore a11y_no_noninteractive_tabindex, a11y_no_noninteractive_element_interactions -->
        <div class="divider" role="separator" aria-orientation="vertical" aria-label="Resize the code and the game" aria-valuenow={split}
            aria-valuemin={30} aria-valuemax={78} tabindex="0" onpointerdown={drag} onkeydown={nudge}></div>
        <div class="preview">
            <div class="panel-title"><span>Live preview</span><span class="live">{ready ? '● Running' : 'Starting…'}</span></div>
            <iframe bind:this={frame} src={previewUrl} title="Tide playground live preview" allow="cross-origin-isolated"></iframe>
            <div class="preview-hint">The bounce counter survives a hot reload.</div>
        </div>
    </div>
    {#if problems.length}
        <ul class="problems" aria-label="Problems in the code">
            {#each problems as problem, i (i)}
                <li>
                    <button class={problem.severity} onclick={() => editor?.reveal(problem.line, problem.column)}>
                        <span class="where">{problem.line}:{problem.column}</span>{problem.message}
                    </button>
                </li>
            {/each}
        </ul>
    {/if}
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
    .expanded .panels { flex: 1; min-height: 420px; }
    .source, .preview { min-width: 0; display: flex; flex-direction: column; }
    .divider { cursor: col-resize; touch-action: none; background: var(--divider); border-left: 2px solid var(--bg-alt); border-right: 2px solid var(--bg-alt); }
    .divider:hover { background: var(--brand-1); }
    .panel-title { display: flex; justify-content: space-between; align-items: center; gap: 12px; height: 42px; flex: none; padding: 0 16px; color: var(--text-2); font-size: 12px; border-bottom: 1px solid var(--divider); white-space: nowrap; overflow: hidden; }
    .panel-title .file { font-family: var(--mono); font-weight: 600; color: var(--text-1); }
    .panel-title span + span { font-size: 11px; overflow: hidden; text-overflow: ellipsis; }
    .live { color: var(--tip); }
    /* Monaco's layers stack among themselves, under the site's sticky bars. */
    .editor { position: relative; isolation: isolate; height: 560px; background: var(--code-bg); font-family: var(--mono); }
    .expanded .editor { height: auto; flex: 1; }
    .loading { position: absolute; inset: 0; margin: 0; padding: 14px 14px 14px 62px; overflow: hidden; border-radius: 0; background: none; color: var(--text-2); font-size: 13px; line-height: 21px; tab-size: 4; }
    iframe { display: block; width: 100%; height: 520px; border: 0; background: #161618; }
    .expanded iframe { height: auto; flex: 1; }
    .preview-hint { display: grid; place-items: center; height: 40px; flex: none; padding: 0 12px; font-size: 11px; color: var(--text-2); }
    .problems { list-style: none; margin: 0; padding: 6px 0; border-top: 1px solid var(--divider); max-height: 160px; overflow-y: auto; flex: none; }
    .problems li { margin: 0; }
    .problems button { display: flex; gap: 10px; width: 100%; padding: 4px 16px; border: 0; border-radius: 0; background: none; text-align: left; font-family: var(--mono); font-weight: 400; color: var(--text-1); }
    .problems button:hover { background: var(--bg-soft); }
    .problems button::before { content: '●'; }
    .problems .error::before { color: var(--danger); }
    .problems .warning::before { color: var(--warning); }
    .problems .info::before { color: var(--tip); }
    .problems .where { color: var(--text-2); min-width: 48px; }
    .status { display: flex; align-items: center; gap: 8px; flex: none; border-top: 1px solid var(--divider); padding: 12px 16px; font-size: 12px; color: var(--text-2); }
    .status > span { color: var(--tip); }
    .status.error, .status.error > span { color: var(--danger); }
    .busy { color: var(--brand-1) !important; }
    .diagnostics, .console pre { margin: 0; padding: 12px 16px; border-top: 1px solid var(--divider); background: var(--code-bg); color: var(--text-1); border-radius: 0; white-space: pre-wrap; overflow-wrap: anywhere; max-height: 240px; overflow-y: auto; font-size: 12px; flex: none; }
    .diagnostics.error { color: var(--danger); }
    .console { border-top: 1px solid var(--divider); font-size: 12px; flex: none; }
    .console summary { padding: 10px 16px; cursor: pointer; color: var(--text-2); }
    @media (max-width: 1050px) {
        .panels { grid-template-columns: 1fr; }
        .divider { display: none; }
        .source { border-bottom: 1px solid var(--divider); }
        .editor { height: 420px; }
        iframe { height: 350px; }
        .expanded .panels { display: flex; flex-direction: column; }
        .expanded .source, .expanded .preview { flex: 1; }
    }
    @media (max-width: 600px) {
        .toolbar { align-items: flex-start; flex-direction: column; gap: 10px; }
        .panel-title span:not(.live):not(.file) { font-size: 10px; }
        .editor { height: 360px; }
    }
</style>
