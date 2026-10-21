<script lang="ts">
    import { goto } from '$app/navigation';
    import { liveExamples } from '#lib/examples.ts';
    import Seo from '#lib/components/Seo.svelte';
    import Playground from '#lib/components/Playground.svelte';
    import AISetup from '#lib/components/AISetup.svelte';

    let { data } = $props();

    // A link to a section this page had before it went to another page still
    // lands on that section, there (`moved`, see server/content.ts).
    function movedSections() {
        const follow = () => {
            const to = data.moved?.[location.hash.slice(1)];
            if (to) goto(to, { replaceState: true });
        };
        follow();
        addEventListener('hashchange', follow);
        return () => removeEventListener('hashchange', follow);
    }

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
    <article class="prose" {@attach copyButtons} {@attach liveExamples} {@attach movedSections}>
        {#if data.path === '/guide/playground'}
            {@html data.html.split('<div data-tide-playground></div>')[0]}
            <Playground />
            {@html data.html.split('<div data-tide-playground></div>')[1]}
        {:else if data.path === '/guide/ai'}
            {@html data.html.split('<div data-tide-ai-setup></div>')[0]}
            <AISetup prompt={data.starterPrompt ?? ''} />
            {@html data.html.split('<div data-tide-ai-setup></div>')[1]}
        {:else}
            {@html data.html}
        {/if}
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
