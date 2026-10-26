<script lang="ts">
    import Seo from '#lib/components/Seo.svelte';
</script>

<Seo title="Privacy | Tide Engine" description="What the Tide Engine website, the tide command and the multiplayer relay record about you, and what they never keep." path="/privacy" />

<main id="content" class="prose">
    <h1>Privacy</h1>
    <p class="lead">Tide Engine keeps as little as it can about the people who use it. This page covers this website, the <code>tide</code> command and the relay that introduces players to each other. Games you make with Tide are yours: the engine adds no analytics or tracking to them.</p>

    <h2>This website</h2>
    <p>The site counts page views and package downloads, as daily totals, with no cookies for visitors, no visitor IDs and no third-party analytics:</p>
    <ul>
        <li>A page view records the page's path, and on the first page of a visit only the hostname of the site that linked to it, never its full address.</li>
        <li>A download records the release's version, platform and channel.</li>
    </ul>
    <p>Daily totals are kept for 400 days. Your IP address is used only to limit how many requests one address can make, held in memory for minutes and never stored. Requests sent through the early-access form keep the GitHub username and the description you enter, privately, to review them.</p>

    <h2>The tide command</h2>
    <p><code>tide</code> sends one small anonymous event per command: which command, its target, the version, the operating system and CPU, whether CI runs it, how long it took, whether it worked and if not the kind of failure, and how many files and packages the game has, in ranges. Events carry a random ID for the installation. They never hold paths, project or file names, code, error messages, or user or machine names. They're kept on this site's server for 13 months, with the time they arrived rounded to the hour.</p>
    <p>Turn it off with <code>tide telemetry off</code>, <code>TIDE_TELEMETRY=0</code> or <code>DO_NOT_TRACK=1</code>. <a href="/tools/telemetry">Usage data</a> lists every field.</p>

    <h2>The multiplayer relay</h2>
    <p>When players host or join a room, the relay introduces them so their games can connect to each other. It sees each player's IP address to do that, and counts connections per address to limit abuse. It keeps addresses in memory only while they're connected, or for about a minute after their last request, and doesn't write them to disk or to its logs. Rooms and lobbies live in its memory while they're open; it keeps no lasting record of them or of who played with whom.</p>
    <p>Once introduced, players' games connect directly, so the players in a match can see each other's addresses, as in most online games. Where a direct connection isn't possible, their traffic goes through a relay server (TURN) run by Cloudflare.</p>

    <h2>Who runs it</h2>
    <p>The website and the relay run on Fly.io, and the domain's DNS and the TURN servers on Cloudflare. They handle network traffic under their own privacy policies.</p>
</main>

<style>
    main { max-width: 720px; margin: 0 auto; padding: 64px 24px 96px; }
    .lead { color: var(--text-2); }
    @media (max-width: 639px) { main { padding-top: 40px; } }
</style>
