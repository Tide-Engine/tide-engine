<script lang="ts">
    import type { PageProps } from './$types';
    let { form }: PageProps = $props();
</script>

<svelte:head>
    <title>Tide Engine access request</title>
    <meta name="description" content="Request access to Tide Engine during early development. Requests are reviewed manually." />
    <link rel="canonical" href="https://tide-engine.dev/request-access" />
</svelte:head>

<main id="content">
    <h1>Tide Engine access request</h1>
    <p>Tide Engine is in early development. Access is whitelisted, and we review each request manually. A request doesn’t guarantee an invitation.</p>

    {#if form?.success}
        <div class="notice success" role="status">
            <h2>Request received</h2>
            <p>{form.message}</p>
        </div>
        <a class="back" href="/guide">Explore the documentation →</a>
    {:else}
        <form method="POST">
            {#if form?.errors.form}
                <p class="notice error" role="alert">{form.errors.form}</p>
            {/if}
            <div class="field">
                <label for="github">GitHub username <span class="required">(required)</span></label>
                <p id="github-help" class="help">We use this to invite you if your request is approved.</p>
                <input id="github" name="github" type="text" required maxlength="39" autocomplete="off" autocapitalize="none" spellcheck="false" value={form?.github ?? ''} aria-invalid={form?.errors.github ? 'true' : undefined} aria-describedby={form?.errors.github ? 'github-help github-error' : 'github-help'} />
                {#if form?.errors.github}<p id="github-error" class="error field-error">{form.errors.github}</p>{/if}
            </div>
            <div class="field">
                <label for="project">What would you like to build? <span class="required">(optional)</span></label>
                <p id="project-help" class="help">A few words about your game or project, up to 1,000 characters.</p>
                <textarea id="project" name="project" rows="5" maxlength="1000" value={form?.project ?? ''} aria-invalid={form?.errors.project ? 'true' : undefined} aria-describedby={form?.errors.project ? 'project-help project-error' : 'project-help'}></textarea>
                {#if form?.errors.project}<p id="project-error" class="error field-error">{form.errors.project}</p>{/if}
            </div>
            <div class="trap" aria-hidden="true">
                <label for="website">Leave this field empty</label>
                <input id="website" name="website" type="text" tabindex="-1" autocomplete="off" />
            </div>
            <p class="privacy">Your request is private and used only to review access. No email address is needed.</p>
            <button type="submit">Request access</button>
        </form>
    {/if}
</main>

<style>
    main { max-width: 680px; margin: 0 auto; padding: 64px 24px 96px; }
    h1 { margin: 0 0 20px; font-size: 34px; line-height: 1.2; letter-spacing: -0.02em; }
    main > p { margin-bottom: 32px; color: var(--text-2); }
    .field { margin: 0 0 24px; }
    label { display: block; font-weight: 600; }
    .required { color: var(--text-2); font-size: 14px; font-weight: 400; }
    .help { margin: 4px 0 10px; color: var(--text-2); font-size: 14px; }
    input, textarea { width: 100%; padding: 10px 12px; border: 1px solid var(--border); border-radius: 6px; background: var(--bg); color: var(--text-1); font: inherit; }
    textarea { resize: vertical; }
    [aria-invalid="true"] { border-color: var(--danger); }
    .field-error { margin: 6px 0 0; font-size: 14px; }
    .error { color: var(--danger); }
    .notice { padding: 16px; border-radius: 6px; background: var(--bg-soft); }
    .notice.error { border: 1px solid var(--danger); }
    .success { border: 1px solid var(--tip); }
    .success h2 { margin: 0; font-size: 20px; }
    .success p { margin: 8px 0 0; }
    .privacy { color: var(--text-2); font-size: 14px; }
    button { padding: 10px 20px; border-radius: 6px; background: var(--brand-fill); color: var(--brand-text); font-weight: 600; }
    button:hover { background: var(--brand-fill-hover); }
    .back { display: inline-block; margin-top: 24px; color: var(--brand-1); }
    .trap { position: absolute; width: 1px; height: 1px; overflow: hidden; clip-path: inset(50%); white-space: nowrap; }
    @media (max-width: 639px) { main { padding-top: 40px; } h1 { font-size: 28px; } }
</style>
