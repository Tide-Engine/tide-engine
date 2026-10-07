<script lang="ts">
    import { liveExamples } from '#lib/examples.ts';
    import Seo from '#lib/components/Seo.svelte';

    let { data } = $props();

    // The copy buttons on code blocks.
    function copyButtons(article: HTMLElement) {
        const click = (event: MouseEvent) => {
            const button = (event.target as HTMLElement).closest('button.copy');
            const code = button?.parentElement?.querySelector('pre code');
            if (!button || !code) return;
            navigator.clipboard.writeText(code.textContent ?? '').then(() => {
                button.classList.add('copied');
                button.setAttribute('aria-label', 'Copied');
                setTimeout(() => {
                    button.classList.remove('copied');
                    button.setAttribute('aria-label', 'Copy code');
                }, 2000);
            });
        };
        article.addEventListener('click', click);
        return () => article.removeEventListener('click', click);
    }
</script>

<Seo title={`${data.title} | Tide Engine`} description={data.description} path={data.path} documentation />

{#key data.path}
    <article class="prose" {@attach copyButtons} {@attach liveExamples}>
        {@html data.html}
    </article>
{/key}

<footer>
    {#if data.prev || data.next}
        <nav class="pager" aria-label="Pages">
            {#if data.prev}
                <a class="prev" href={data.prev.link}>
                    <span>Previous page</span>
                    {data.prev.text}
                </a>
            {/if}
            {#if data.next}
                <a class="next" href={data.next.link}>
                    <span>Next page</span>
                    {data.next.text}
                </a>
            {/if}
        </nav>
    {/if}
</footer>

<style>
    footer {
        margin-top: 64px;
    }

    .pager {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 16px;
        margin-top: 24px;
        padding-top: 24px;
        border-top: 1px solid var(--divider);
    }

    .pager a {
        display: flex;
        flex-direction: column;
        gap: 2px;
        padding: 12px 16px;
        border: 1px solid var(--divider);
        border-radius: 8px;
        color: var(--brand-1);
        font-size: 14px;
        font-weight: 500;
        transition: border-color 0.2s;
    }

    .pager a:hover {
        border-color: var(--brand-1);
    }

    .pager span {
        color: var(--text-2);
        font-size: 12px;
    }

    .next {
        grid-column: 2;
        text-align: right;
    }

    @media (max-width: 639px) {
        .pager {
            grid-template-columns: 1fr;
        }

        .next {
            grid-column: 1;
        }
    }
</style>
