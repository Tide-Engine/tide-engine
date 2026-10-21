<script lang="ts">
    import Seo from '#lib/components/Seo.svelte';

    let { data } = $props();

    const external = (link: string) => /^https?:/.test(link);
</script>

<Seo title={data.title} description={data.description} path="/" />

<main id="content">
    <section class="hero">
        <div class="words">
            <h1>
                <span class="name">{data.hero.name}</span>
                <span class="text">{data.hero.text}</span>
            </h1>
            <p class="tagline">{data.hero.tagline}</p>
            <div class="actions">
                {#each data.hero.actions as action (action.link)}
                    <a class="button {action.theme}" href={action.link} target={external(action.link) ? '_blank' : undefined} rel={external(action.link) ? 'noreferrer' : undefined}>{action.text}</a>
                {/each}
            </div>
        </div>
        <div class="image" aria-hidden="true">
            <img src="/favicon.svg" alt="" width="320" height="320" />
        </div>
    </section>

    <section aria-label="Features">
        <dl class="features">
            {#each data.features as feature (feature.title)}
                <div class="feature">
                    <dt>{feature.title}</dt>
                    <dd>{feature.details}</dd>
                </div>
            {/each}
        </dl>
    </section>

    <section class="prose more">
        {@html data.html}
    </section>

    <footer class="site-links"><a href="/privacy">Privacy</a></footer>
</main>

<style>
    /* Under the hero the page keeps to two columns: names, and what's said
       of them. */
    main {
        --names: 280px;
        --gutter: 48px;

        max-width: 1152px;
        margin: 0 auto;
        padding: 0 48px 128px;
    }

    .hero {
        display: flex;
        align-items: center;
        gap: 48px;
        padding: 96px 0 64px;
    }

    .words {
        flex: 1;
        min-width: 0;
    }

    h1 {
        margin: 0;
        font-size: 56px;
        font-weight: 700;
        line-height: 1.12;
        letter-spacing: -0.03em;
    }

    h1 span {
        display: block;
    }

    .name {
        color: var(--brand-fill);
    }

    .tagline {
        max-width: 576px;
        margin: 16px 0 0;
        color: var(--text-2);
        font-size: 22px;
        font-weight: 500;
        line-height: 1.5;
    }

    .actions {
        display: flex;
        flex-wrap: wrap;
        gap: 12px;
        margin-top: 32px;
    }

    .button {
        display: inline-block;
        padding: 0 20px;
        border: 1px solid transparent;
        border-radius: 20px;
        font-size: 14px;
        font-weight: 600;
        line-height: 38px;
        transition: background-color 0.2s, border-color 0.2s, color 0.2s;
    }

    .button.brand {
        background: var(--brand-fill);
        color: var(--brand-text);
    }

    .button.brand:hover {
        background: var(--brand-fill-hover);
    }

    .button.alt {
        background: var(--bg-alt);
        border-color: var(--divider);
        color: var(--text-1);
    }

    .button.alt:hover {
        border-color: var(--border);
    }

    .image {
        flex: none;
        display: grid;
        place-items: center;
        width: 360px;
        height: 360px;
    }

    .image img {
        width: 280px;
        height: 280px;
    }

    .features {
        margin: 0;
        border-top: 1px solid var(--divider);
    }

    .feature {
        display: grid;
        grid-template-columns: var(--names) minmax(0, 1fr);
        column-gap: var(--gutter);
        padding: 20px 0;
        border-bottom: 1px solid var(--divider);
    }

    dt {
        font-size: 17px;
        font-weight: 600;
        line-height: 28px;
    }

    dd {
        max-width: 640px;
        margin: 0;
        color: var(--text-2);
        line-height: 28px;
    }

    /* What follows the list goes on as a row of it: its heading among the
       names, the rest beside it. */
    .more {
        position: relative;
        max-width: 720px;
        margin-left: calc(var(--names) + var(--gutter));
        padding-top: 20px;
    }

    .more :global(h2:first-child) {
        position: absolute;
        top: 20px;
        left: calc(-1 * (var(--names) + var(--gutter)));
        width: var(--names);
        margin: 0;
        padding: 0;
        border: 0;
        font-size: 17px;
        line-height: 28px;
        letter-spacing: 0;
    }

    .more :global(h2:first-child .anchor) {
        top: 0;
    }

    .more :global(h2:first-child + *) {
        margin-top: 0;
    }

    .site-links {
        margin-top: 64px;
        padding-top: 20px;
        border-top: 1px solid var(--divider);
        font-size: 13px;
    }

    .site-links a {
        color: var(--text-2);
    }

    .site-links a:hover {
        color: var(--brand-1);
    }

    @media (max-width: 959px) {
        .hero {
            flex-direction: column-reverse;
            align-items: flex-start;
            gap: 8px;
            padding: 48px 0;
        }

        .image {
            width: 200px;
            height: 200px;
        }

        .image img {
            width: 160px;
            height: 160px;
        }

        .feature {
            grid-template-columns: minmax(0, 1fr);
        }

        .more {
            margin-left: 0;
        }

        .more :global(h2:first-child) {
            position: static;
            width: auto;
            margin-bottom: 4px;
        }
    }

    @media (max-width: 639px) {
        main {
            padding: 0 24px 96px;
        }

        h1 {
            font-size: 40px;
        }

        .tagline {
            font-size: 18px;
        }
    }
</style>
