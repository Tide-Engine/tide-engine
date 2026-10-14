<script lang="ts">
    import '@fontsource-variable/inter';
    import '../app.css';
    import Header from '#lib/components/Header.svelte';
    import Search from '#lib/components/Search.svelte';
    import { watchTheme } from '#lib/theme.svelte.ts';
    import { afterNavigate } from '$app/navigation';
    import { pages } from '#lib/nav.ts';

    let { children } = $props();

    let searching = $state(false);

    $effect(watchTheme);

    let firstPage = true;
    afterNavigate(() => {
        const path = window.location.pathname.replace(/\/$/, '') || '/';
        if (import.meta.env.DEV || window.top !== window || (path !== '/' && !pages.some((page) => page.link === path))) return;
        // Send only the referring hostname, and only for the first page.
        let referrer = '';
        if (firstPage && document.referrer) {
            try {
                const from = new URL(document.referrer);
                if (from.hostname !== window.location.hostname) referrer = `${from.protocol}//${from.hostname}`;
            } catch { /* Invalid referrers are direct traffic. */ }
        }
        firstPage = false;
        void fetch('/analytics/pageview', {
            method: 'POST', headers: { 'content-type': 'application/json' },
            body: JSON.stringify({ path, referrer }), keepalive: true, credentials: 'omit',
        }).catch(() => {});
    });
</script>

<svelte:head>
    <meta name="theme-color" content="#1b1b1f" media="(prefers-color-scheme: dark)" />
    <meta name="theme-color" content="#ffffff" media="(prefers-color-scheme: light)" />
</svelte:head>

<a class="skip" href="#content">Skip to content</a>
<Header onsearch={() => (searching = true)} />
{@render children()}
<Search bind:open={searching} />

<style>
    .skip {
        position: fixed;
        top: 8px;
        left: 8px;
        z-index: 50;
        padding: 8px 16px;
        border-radius: 8px;
        background: var(--bg);
        color: var(--brand-1);
        font-weight: 600;
        box-shadow: var(--shadow-3);
        transform: translateY(-200%);
    }

    .skip:focus {
        transform: none;
    }
</style>
