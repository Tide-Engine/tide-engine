<script lang="ts">
    let { prompt }: { prompt: string } = $props();

    let status = $state('');

    async function copyPrompt() {
        try {
            await navigator.clipboard.writeText(prompt.trim());
            status = 'Starter prompt copied. Add your task before sending it.';
        } catch {
            status = 'Copy the starter prompt from the text below.';
        }
    }
</script>

<div class="ai-setup">
    <p>Give your agent the overview, then describe your game.</p>
    <div class="actions">
        <button type="button" onclick={copyPrompt}>Copy starter prompt</button>
        <a href="/guide/ai-context.md" download="TIDE.md">Download AI guide</a>
        <a href="/ai/tide-skill.zip" download>Download Tide skill</a>
    </div>
    <p class="status" role="status">{status}</p>
</div>

<style>
    .ai-setup {
        margin: 24px 0;
        padding: 20px;
        border: 1px solid var(--divider);
        border-radius: 8px;
        background: var(--bg-soft);
    }

    .ai-setup p { margin: 0 0 14px; }

    .actions { display: flex; flex-wrap: wrap; gap: 10px; }

    .actions button, .actions a {
        display: inline-flex;
        align-items: center;
        padding: 9px 14px;
        border: 1px solid var(--divider);
        border-radius: 6px;
        color: var(--text-1);
        background: var(--bg);
        font: inherit;
        font-size: 14px;
        text-decoration: none;
        cursor: pointer;
    }

    .actions button { background: var(--brand-1); color: var(--bg); border-color: var(--brand-1); }
    .actions a:hover, .actions button:hover { border-color: var(--brand-1); }
    .actions :focus-visible { outline: 2px solid var(--brand-1); outline-offset: 3px; }
    .ai-setup .status { margin: 12px 0 0; color: var(--text-2); font-size: 13px; }
    .status:empty { display: none; }
</style>
