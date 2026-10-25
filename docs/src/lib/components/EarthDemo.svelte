<script lang="ts">
    import { onMount } from 'svelte';
    import { game } from '#lib/examples.ts';

    const shared = 'https://play.tide-engine.dev/g/29c727eab929afcbef23455a6bf73125/';
    let place: HTMLElement;
    let frame: HTMLIFrameElement | undefined;
    let opening = 0;
    let running = $state(false);
    let shown = $state(false);
    let message = $state('A little planet to explore together');
    let full = $state(shared);
    let big = $state(false);

    $effect(() => {
        if (!big) return;
        const overflow = document.body.style.overflow;
        document.body.style.overflow = 'hidden';
        const out = (event: KeyboardEvent) => {
            if (event.key === 'Escape') big = false;
        };
        document.addEventListener('keydown', out);
        return () => {
            document.body.style.overflow = overflow;
            document.removeEventListener('keydown', out);
        };
    });

    async function open() {
        if (running) return;
        const asked = ++opening;
        running = true;
        message = 'Loading planet…';
        const backend = new URLSearchParams(location.search).get('backend');
        let hosted = false;
        try {
            hosted = (await fetch(`${shared}ready`, { signal: AbortSignal.timeout(4000) })).ok;
        } catch { /* Use the site's local build when hosting is unavailable. */ }
        if (asked !== opening) return;
        const opened = document.createElement('iframe');
        opened.title = 'Earth: a shared planet to walk and drive around';
        opened.allow = 'cross-origin-isolated; fullscreen';
        const url = new URL(hosted ? shared : '/earth/', location.href);
        full = url.href;
        url.searchParams.set('embedded', '');
        if (backend) url.searchParams.set('backend', backend);
        if (!hosted) url.searchParams.set('wait', '');
        opened.src = url.href;
        opened.addEventListener('load', async () => {
            if (frame !== opened) return;
            try {
                if (hosted) await new Promise(done => setTimeout(done, 1500));
                else {
                    const page = game(opened);
                    if (typeof page?.start !== 'function') throw new Error('No game in this build');
                    await page.start();
                }
                if (frame === opened) shown = true;
            } catch {
                if (frame === opened) {
                    close();
                    message = 'This build has no playable demo. Try Open separately.';
                }
            }
        }, { once: true });
        frame = opened;
        place.append(opened);
    }

    function close() {
        opening++;
        frame?.remove();
        frame = undefined;
        running = shown = big = false;
        message = 'A little planet to explore together';
    }

    onMount(() => {
        const elsewhere = () => {
            if (frame && document.activeElement === frame) frame.blur();
        };
        document.addEventListener('pointerdown', elsewhere, true);
        return () => {
            document.removeEventListener('pointerdown', elsewhere, true);
            close();
        };
    });
</script>

<section class="demo" aria-label="Play Earth">
    <div class="slot">
        <div class="earth" class:shown class:big bind:this={place}>
            <div class="placeholder" aria-hidden={shown}>
                <div class="planet" aria-hidden="true"></div>
                <span role="status">{message}</span>
            </div>
        </div>
    </div>
    <div class="controls">
        <button type="button" onclick={open} disabled={running}>Play Earth</button>
        <button type="button" onclick={close} disabled={!running}>Stop</button>
        <button type="button" onclick={() => (big = true)} disabled={!shown}>Expand</button>
        <a href={full} target="_blank" rel="noreferrer">Open separately</a>
    </div>
</section>
{#if big}
    <button type="button" class="backdrop" aria-label="Back to the docs" onclick={() => (big = false)}></button>
    <button type="button" class="shrink" onclick={() => (big = false)}>Close ✕</button>
{/if}

<style>
    .demo { margin: 24px 0; }
    .slot { aspect-ratio: 4 / 3; }
    .earth { position: relative; width: 100%; height: 100%; overflow: hidden; border-radius: 12px; background: #161618; }
    .placeholder { position: absolute; inset: 0; display: grid; place-items: center; }
    .planet { width: 46%; aspect-ratio: 1; border-radius: 50%; background: radial-gradient(circle at 35% 30%, #41464d, #272d35 55%, #1a1e24 80%); box-shadow: 0 0 28px #596b8226; }
    .placeholder span { position: absolute; bottom: 7%; padding: 0 12px; color: #a8adb5; font-size: 13px; text-align: center; }
    .earth :global(iframe) { position: absolute; inset: 0; width: 100%; height: 100%; border: 0; opacity: 0; }
    .earth.shown .placeholder { visibility: hidden; }
    .earth.shown :global(iframe) { opacity: 1; }
    .controls { display: flex; flex-wrap: wrap; align-items: center; gap: 8px 16px; padding-top: 12px; font-size: 14px; }
    .controls button, .shrink { padding: 6px 14px; border: 1px solid var(--divider); border-radius: 8px; background: var(--bg-alt); color: var(--text-1); font: inherit; cursor: pointer; }
    .controls button:disabled { opacity: 0.5; cursor: default; }
    .earth.big { position: fixed; inset: 4vh 4vw; z-index: 101; width: auto; height: auto; box-shadow: 0 24px 80px #0008; }
    .backdrop { position: fixed; inset: 0; z-index: 100; border: 0; background: #0009; cursor: zoom-out; }
    .shrink { position: fixed; top: calc(4vh + 10px); left: 50%; z-index: 102; transform: translateX(-50%); background: #161618e6; color: #eeeef0; font-size: 13px; }
    @media (max-width: 639px) {
        .slot { aspect-ratio: 1; }
        .earth.big { inset: 0; border-radius: 0; }
        .shrink { top: 10px; }
    }
</style>
