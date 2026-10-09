<script lang="ts">
    import type { PageData } from './$types';
    let { data }: { data: PageData } = $props();
</script>

<svelte:head>
    <title>Private prototypes | Tide Engine</title>
    <meta name="robots" content="noindex,nofollow" />
</svelte:head>

<main id="content">
    <div class="heading">
        <div>
            <p class="eyebrow">Tide Engine / Admin</p>
            <h1>Private prototypes</h1>
            <p class="subtitle">A place to try what is taking shape.</p>
        </div>
        <form method="POST" action="/admin/logout"><button class="quiet-button" type="submit">Sign out ↗</button></form>
    </div>
    <nav class="admin-nav" aria-label="Admin sections">
        <a href="/admin">Analytics</a>
        <a href="/admin/prototypes" aria-current="page">Private prototypes</a>
    </nav>

    <ul class="prototypes" aria-label="Private games">
        {#each data.prototypes as prototype}
            <li class="prototype">
                <div class="picture" class:horror={prototype.horror}>
                    {#if prototype.image}
                        <img src={prototype.image} alt="" width="960" height="540" />
                    {:else}
                        <div class="candle" aria-hidden="true"><span class="flame"></span><span class="wick"></span><span class="wax"></span></div>
                        <span class="picture-label">Keep the light.</span>
                    {/if}
                    <span class="private-label">Private</span>
                </div>
                <div class="copy">
                    <p class="kind">{prototype.kind}</p>
                    <h2>{prototype.title}</h2>
                    <p class="description">{prototype.description}</p>
                    <div class="card-footer">
                        <span class="players">{prototype.players}</span>
                        {#if prototype.available}
                            <a class="play" href={prototype.href} target="_blank" rel="noopener" data-prototype={prototype.id}>Play prototype <span aria-hidden="true">↗</span><span class="visually-hidden"> (opens in a new tab)</span></a>
                        {:else}
                            <span class="pending">In development</span>
                        {/if}
                    </div>
                </div>
            </li>
        {/each}
    </ul>
    <p class="note">To play together, each player signs in here and opens the same prototype. Host a room, then share its code.</p>
</main>

<style>
    main { max-width: 1120px; margin: 0 auto; padding: 44px 32px 64px; }
    .heading { display: flex; align-items: center; justify-content: space-between; gap: 20px; margin-bottom: 28px; }
    .eyebrow { margin: 0 0 8px; color: var(--text-2); font-size: 12px; }
    h1 { margin: 0; font-size: 36px; font-weight: 650; letter-spacing: -.035em; line-height: 1.2; }
    .subtitle { margin: 10px 0 0; color: var(--text-2); font-size: 14px; }
    .quiet-button { padding: 8px 12px; border: 1px solid var(--divider); border-radius: 7px; color: var(--text-2); font-size: 13px; white-space: nowrap; }
    .quiet-button:hover { color: var(--text-1); border-color: var(--border); }
    .admin-nav { display: flex; gap: 24px; border-bottom: 1px solid var(--divider); margin: 0 0 32px; }
    .admin-nav a { padding: 0 0 12px; color: var(--text-2); font-size: 13px; }
    .admin-nav a:hover { color: var(--text-1); }
    .admin-nav a[aria-current] { color: var(--brand-1); border-bottom: 2px solid var(--brand-1); }
    .prototypes { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 24px; list-style: none; margin: 0; padding: 0; }
    .prototype { border: 1px solid var(--divider); border-radius: 12px; overflow: hidden; background: var(--bg-alt); }
    .picture { position: relative; aspect-ratio: 16 / 9; background: #171922; overflow: hidden; }
    .picture img { display: block; width: 100%; height: 100%; object-fit: cover; }
    .private-label { position: absolute; top: 16px; left: 16px; padding: 4px 9px; border: 1px solid #ffffff2b; border-radius: 4px; background: #101114d9; color: #e6e6e1; font-size: 10px; font-weight: 600; letter-spacing: .06em; text-transform: uppercase; }
    .horror { display: grid; place-items: center; background: radial-gradient(ellipse at 50% 58%, #5b3b2638, transparent 58%), #111013; }
    .candle { position: relative; width: 26px; height: 84px; margin-top: 16px; filter: drop-shadow(0 0 24px #d99c5940); }
    .wax { position: absolute; bottom: 0; left: 2px; width: 22px; height: 45px; background: linear-gradient(90deg, #8e775d, #c1aa85 45%, #a18a67); }
    .wick { position: absolute; top: 34px; left: 12px; width: 2px; height: 7px; background: #34251d; }
    .flame { position: absolute; top: 6px; left: 7px; width: 13px; height: 29px; border-radius: 60% 50% 45% 45%; background: #e8c697; box-shadow: 0 0 18px 8px #d5933740; transform: rotate(7deg); }
    .picture-label { position: absolute; bottom: 28px; color: #a68e75; letter-spacing: .25em; font-family: var(--mono); font-size: 10px; text-transform: uppercase; }
    .copy { padding: 24px; }
    .kind { color: var(--brand-1); font-size: 11px; margin: 0 0 10px; }
    h2 { font-size: 24px; line-height: 1.2; letter-spacing: -.02em; margin: 0 0 12px; }
    .description { margin: 0; color: var(--text-2); font-size: 13px; line-height: 1.7; min-height: 66px; }
    .card-footer { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; margin-top: 24px; }
    .players { color: var(--text-2); font-size: 11px; }
    .play { color: var(--text-1); border: 1px solid var(--border); border-radius: 6px; padding: 8px 12px; font-size: 12px; font-weight: 550; }
    .play:hover { color: var(--brand-1); border-color: var(--brand-1); }
    .play span[aria-hidden] { margin-left: 12px; }
    .pending { color: var(--text-3); font-size: 12px; }
    .note { margin: 24px 0 0; color: var(--text-2); font-size: 12px; line-height: 1.7; }
    @media (max-width: 650px) { main { padding: 28px 20px 40px; } .prototypes { grid-template-columns: 1fr; } h1 { font-size: 28px; } .heading { align-items: start; } .subtitle { font-size: 12px; } .quiet-button { font-size: 11px; } .copy { padding: 20px; } .description { min-height: 0; } }
</style>
