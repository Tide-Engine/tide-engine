<script lang="ts">
    // Between the text playground and the graph playground.
    let { mode }: { mode: 'text' | 'graph' } = $props();
</script>

<nav class="modes" aria-label="Playground">
    <a href="/guide/playground" class:on={mode === 'text'} aria-current={mode === 'text' ? 'page' : undefined}>Text</a>
    <a href="/guide/graph-playground" class:on={mode === 'graph'} aria-current={mode === 'graph' ? 'page' : undefined}>Graph</a>
</nav>

<style>
    .modes { display: inline-flex; border: 1px solid var(--border); border-radius: 6px; overflow: hidden; }
    a { padding: 7px 12px; font-size: 12px; font-weight: 600; color: var(--text-2); background: var(--bg); text-decoration: none; }
    a + a { border-left: 1px solid var(--border); }
    a:hover { color: var(--brand-1); }
    a.on { color: var(--brand-text); background: var(--brand-fill); }
    a:focus-visible { outline: 2px solid var(--brand-1); outline-offset: -2px; }
</style>
