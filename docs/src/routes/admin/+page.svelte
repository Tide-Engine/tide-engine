<script lang="ts">
    import { onMount } from 'svelte';

    type Daily = {
        date: string;
        pageviews: number;
        downloads: number;
        relay: {
            samples: number;
            roomsAverage: number;
            roomsPeak: number;
            connectionsAverage: number;
            connectionsPeak: number;
            pendingJoinsAverage: number;
            pendingJoinsPeak: number;
        };
    };
    type Summary = {
        from: string;
        to: string;
        days: number;
        totals: { pageviews: number; downloads: number };
        daily: Daily[];
        pages: { path: string; views: number }[];
        referrers: { hostname: string; views: number }[];
        downloads: { version: string; platform: string; channel: string; count: number }[];
        relay: {
            status: 'unknown' | 'unconfigured' | 'online' | 'unavailable';
            lastAttempt: string | null;
            latest: null | {
                at: string;
                instance: string;
                uptimeSeconds: number;
                rooms: number;
                connections: number;
                pendingJoins: number;
            };
        };
        startedAt?: string;
    };

    const ranges = [{ days: 1, label: 'Today' }, { days: 7, label: '7 days' }, { days: 30, label: '30 days' }, { days: 90, label: '90 days' }];
    const numbers = new Intl.NumberFormat('en');
    const dates = new Intl.DateTimeFormat('en', { month: 'short', day: 'numeric', timeZone: 'UTC' });
    const times = new Intl.DateTimeFormat('en', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
    let selectedDays = $state(30);
    let summary = $state<Summary | null>(null);
    let loading = $state(true);
    let error = $state('');
    let updatedAt = $state<Date | null>(null);
    let now = $state(Date.now());
    let controller: AbortController | undefined;
    let disposed = false;

    const latest = $derived(summary?.relay.latest ?? null);
    const relayStatus = $derived(summary?.relay.status ?? 'unknown');
    const relayUnavailable = $derived(relayStatus === 'unavailable');
    const relayStale = $derived(latest !== null && now - new Date(latest.at).getTime() > 180_000);
    const relayBadge = $derived(relayUnavailable ? 'Unavailable' : relayStatus === 'unconfigured' ? 'Not configured' : relayStatus === 'unknown' ? latest ? 'Not checked' : 'No data' : !latest ? 'No data' : relayStale ? 'Stale' : 'Live');
    const platforms = $derived(groupDownloads('platform'));
    const versions = $derived(groupDownloads('version'));
    const maximum = $derived(Math.max(1, ...(summary?.daily.flatMap((day) => [day.pageviews, day.downloads]) ?? [])));
    const hasTraffic = $derived(summary !== null && summary.daily.some((day) => day.pageviews > 0 || day.downloads > 0));
    const relaySamples = $derived(summary?.daily.reduce((sum, day) => sum + day.relay.samples, 0) ?? 0);
    const roomsPeak = $derived(Math.max(0, ...(summary?.daily.map((day) => day.relay.roomsPeak) ?? [])));
    const connectionsPeak = $derived(Math.max(0, ...(summary?.daily.map((day) => day.relay.connectionsPeak) ?? [])));

    function groupDownloads(field: 'platform' | 'version') {
        const counts = new Map<string, number>();
        for (const item of summary?.downloads ?? []) {
            const label = field === 'version' ? `${item.version} · ${item.channel}` : platformName(item.platform);
            counts.set(label, (counts.get(label) ?? 0) + item.count);
        }
        return [...counts].map(([label, count]) => ({ label, count })).sort((a, b) => b.count - a.count);
    }

    function platformName(platform: string) {
        return platform.replace(/^windows/, 'Windows').replace(/^linux/, 'Linux').replace(/^macos/, 'macOS').replace(/[-_]/g, ' ');
    }

    function dayLabel(value: string) {
        return dates.format(new Date(`${value.slice(0, 10)}T00:00:00Z`));
    }

    function rangeLabel() {
        if (!summary) return 'All dates use UTC';
        return summary.from === summary.to ? `${dayLabel(summary.from)} · UTC` : `${dayLabel(summary.from)} – ${dayLabel(summary.to)} · UTC`;
    }

    function points(field: 'pageviews' | 'downloads') {
        const daily = summary?.daily ?? [];
        return daily.map((day, index) => {
            const x = daily.length === 1 ? 340 : 38 + (index / (daily.length - 1)) * 602;
            return `${x},${174 - (day[field] / maximum) * 144}`;
        }).join(' ');
    }

    function uptime(seconds: number) {
        if (seconds >= 86_400) return `${Math.floor(seconds / 86_400)}d ${Math.floor((seconds % 86_400) / 3600)}h`;
        if (seconds >= 3600) return `${Math.floor(seconds / 3600)}h ${Math.floor((seconds % 3600) / 60)}m`;
        return `${Math.floor(seconds / 60)}m`;
    }

    async function refresh() {
        controller?.abort();
        const request = new AbortController();
        controller = request;
        loading = true;
        try {
            const response = await fetch(`/admin/api?days=${selectedDays}`, { signal: request.signal, cache: 'no-store' });
            if (response.status === 401 || response.redirected) {
                window.location.assign('/admin/login');
                return;
            }
            if (!response.ok) throw new Error('Analytics could not be loaded.');
            const result: Summary = await response.json();
            if (disposed || request.signal.aborted) return;
            summary = result;
            updatedAt = new Date();
            now = Date.now();
            error = '';
        } catch (failure) {
            if (!disposed && !request.signal.aborted) {
                error = failure instanceof Error ? failure.message : 'Analytics could not be loaded.';
            }
        } finally {
            if (!disposed && controller === request) loading = false;
        }
    }

    function selectRange(days: number) {
        if (days === selectedDays) return;
        selectedDays = days;
        void refresh();
    }

    onMount(() => {
        void refresh();
        const interval = window.setInterval(() => {
            now = Date.now();
            void refresh();
        }, 60_000);
        return () => {
            disposed = true;
            controller?.abort();
            window.clearInterval(interval);
        };
    });
</script>

<svelte:head>
    <title>Analytics | Tide Engine</title>
    <meta name="robots" content="noindex, nofollow" />
</svelte:head>

<main id="content">
    <div class="heading">
        <div>
            <p class="eyebrow">Tide Engine / Admin</p>
            <h1>Analytics</h1>
            <p class="subtitle">Docs, downloads and the relay, in one place.</p>
        </div>
        <form method="POST" action="/admin/logout">
            <button class="quiet-button" type="submit">Sign out <span aria-hidden="true">↗</span></button>
        </form>
    </div>

    <div class="toolbar">
        <div class="ranges" role="group" aria-label="Date range">
            {#each ranges as range}
                <button type="button" class:active={selectedDays === range.days} aria-pressed={selectedDays === range.days} onclick={() => selectRange(range.days)}>{range.label}</button>
            {/each}
        </div>
        <div class="freshness">
            <span>{rangeLabel()}</span>
            <button class="refresh" type="button" aria-label="Refresh analytics" disabled={loading} onclick={() => void refresh()}>
                <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 7v5h-5M4 17v-5h5" /><path d="M6.1 6.1A8 8 0 0 1 20 12M4 12a8 8 0 0 0 13.9 5.9" /></svg>
            </button>
        </div>
    </div>

    {#if error}
        <div class="error" role="alert">
            <span>{error} {summary ? 'Showing the last loaded data.' : 'Try refreshing in a moment.'}</span>
            <button type="button" onclick={() => void refresh()} disabled={loading}>Retry</button>
        </div>
    {/if}

    <div class="cards" aria-busy={loading}>
        <section class="metric traffic">
            <h2>Page views</h2>
            <p class="value">{summary ? numbers.format(summary.totals.pageviews) : '—'}</p>
            <p class="metric-note">Across the public website</p>
        </section>
        <section class="metric downloads">
            <h2>Download requests</h2>
            <p class="value">{summary ? numbers.format(summary.totals.downloads) : '—'}</p>
            <p class="metric-note">Packages requested in this period</p>
        </section>
        <section class="metric">
            <div class="metric-heading"><h2>Registered rooms</h2><span class="live" class:stale={relayStale} class:unknown={!latest || relayStatus === 'unconfigured' || relayStatus === 'unknown'} class:unavailable={relayUnavailable}>{relayBadge}</span></div>
            <p class="value">{latest ? numbers.format(latest.rooms) : '—'}</p>
            <p class="metric-note">{relaySamples ? `${numbers.format(roomsPeak)} peak in this period` : relayStatus === 'unconfigured' ? 'Reporting is not configured' : 'Waiting for a relay report'}</p>
        </section>
        <section class="metric">
            <div class="metric-heading"><h2>Relay connections</h2><span class="live" class:stale={relayStale} class:unknown={!latest || relayStatus === 'unconfigured' || relayStatus === 'unknown'} class:unavailable={relayUnavailable}>{relayBadge}</span></div>
            <p class="value">{latest ? numbers.format(latest.connections) : '—'}</p>
            <p class="metric-note">{relaySamples ? `${numbers.format(connectionsPeak)} peak in this period` : relayStatus === 'unconfigured' ? 'Reporting is not configured' : 'Waiting for a relay report'}</p>
        </section>
    </div>

    <div class="overview">
        <section class="panel activity">
            <div class="panel-heading"><h2>Daily activity</h2><div class="legend"><span><i class="pageviews"></i>Page views</span><span><i class="downloadviews"></i>Downloads</span></div></div>
            {#if hasTraffic && summary}
                <svg class="chart" viewBox="0 0 670 214" role="img" aria-label="Daily page views and download requests. Exact values are available in the daily totals table below.">
                    {#each [0, 0.5, 1] as tick}
                        <line class="grid-line" x1="38" x2="640" y1={174 - tick * 144} y2={174 - tick * 144} />
                        <text class="axis-value" x="28" y={178 - tick * 144} text-anchor="end">{numbers.format(Math.round(maximum * tick))}</text>
                    {/each}
                    <polyline class="series page-series" points={points('pageviews')} />
                    <polyline class="series download-series" points={points('downloads')} />
                    {#if summary.daily.length === 1}
                        <circle class="page-dot" cx="340" cy={174 - (summary.daily[0].pageviews / maximum) * 144} r="4" />
                        <circle class="download-dot" cx="340" cy={174 - (summary.daily[0].downloads / maximum) * 144} r="4" />
                    {/if}
                    <text class="axis-date" x="38" y="202">{dayLabel(summary.from)}</text>
                    {#if summary.daily.length > 1}<text class="axis-date" x="640" y="202" text-anchor="end">{dayLabel(summary.to)}</text>{/if}
                </svg>
            {:else}
                <div class="chart-empty"><span class="empty-mark" aria-hidden="true">↗</span><p>{loading && !summary ? 'Loading analytics…' : 'No activity recorded in this period.'}</p><span>Visits and downloads will appear here as they happen.</span></div>
            {/if}
        </section>

        <section class="panel relay">
            <div class="panel-heading"><h2>Relay status</h2><span class="status-dot" class:stale={relayStale} class:unknown={!latest || relayStatus === 'unconfigured' || relayStatus === 'unknown'} class:unavailable={relayUnavailable} aria-hidden="true"></span></div>
            {#if latest}
                <p class="relay-summary">{relayUnavailable ? 'Relay unavailable · last reported counts' : relayStatus === 'unconfigured' ? 'Relay reporting is not configured' : relayStatus === 'unknown' ? 'Waiting for a new relay report' : relayStale ? 'Last report is out of date' : 'Relay reporting normally'}</p>
                <dl>
                    <div><dt>Pending joins</dt><dd>{numbers.format(latest.pendingJoins)}</dd></div>
                    <div><dt>Uptime</dt><dd>{uptime(latest.uptimeSeconds)}</dd></div>
                    <div><dt>Last report</dt><dd><time datetime={latest.at}>{times.format(new Date(latest.at))}</time></dd></div>
                </dl>
            {:else}
                <p class="relay-summary">{relayStatus === 'unconfigured' ? 'Relay reporting is not configured' : relayUnavailable ? 'The relay could not be reached' : 'Waiting for the first relay report'}</p>
                <p class="empty-note">{relayStatus === 'unconfigured' ? 'Connect the relay to enable room and connection statistics.' : 'No valid report yet. Room and connection counts will appear when the relay reports.'}</p>
            {/if}
            {#if relayUnavailable && summary?.relay.lastAttempt}
                <p class="last-attempt">Last attempt <time datetime={summary.relay.lastAttempt}>{times.format(new Date(summary.relay.lastAttempt))}</time></p>
            {/if}
            <p class="footnote">Connections measure signaling activity. Players connect directly once introduced.</p>
        </section>
    </div>

    <div class="breakdowns">
        <section class="panel">
            <div class="panel-heading"><h2>Top pages</h2><span class="column-label">Views</span></div>
            {#if summary?.pages.length}
                <ol class="rankings">
                    {#each summary.pages as page}
                        <li><a href={page.path} title={page.path}><span>{page.path}</span></a><strong>{numbers.format(page.views)}</strong></li>
                    {/each}
                </ol>
            {:else}<p class="empty-note">No page views in this period.</p>{/if}
        </section>
        <section class="panel">
            <div class="panel-heading"><h2>Referrers</h2><span class="column-label">Views</span></div>
            {#if summary?.referrers.length}
                <ol class="rankings">
                    {#each summary.referrers as referrer}
                        <li><span class="rank-label" title={referrer.hostname}>{referrer.hostname || 'Direct'}</span><strong>{numbers.format(referrer.views)}</strong></li>
                    {/each}
                </ol>
            {:else}<p class="empty-note">No referrals in this period.</p>{/if}
        </section>
        <section class="panel">
            <div class="panel-heading"><h2>Download platforms</h2><span class="column-label">Requests</span></div>
            {#if platforms.length}
                <ol class="rankings">
                    {#each platforms as platform}
                        <li><span class="rank-label">{platform.label}</span><strong>{numbers.format(platform.count)}</strong></li>
                    {/each}
                </ol>
            {:else}<p class="empty-note">No downloads in this period.</p>{/if}
            <p class="footnote">Requests include installs and upgrades. They don’t confirm a completed installation.</p>
        </section>
        <section class="panel">
            <div class="panel-heading"><h2>Downloaded versions</h2><span class="column-label">Requests</span></div>
            {#if versions.length}
                <ol class="rankings versions">
                    {#each versions as version}
                        <li><span class="rank-label">{version.label}</span><strong>{numbers.format(version.count)}</strong></li>
                    {/each}
                </ol>
            {:else}<p class="empty-note">No versions requested in this period.</p>{/if}
        </section>
    </div>

    {#if summary?.daily.length}
        <details class="daily-table">
        <summary>Daily totals <span>Page views, downloads and sampled relay peaks</span></summary>
            <div class="table-scroll">
                <table>
                    <caption class="visually-hidden">Daily analytics totals in UTC</caption>
                    <thead><tr><th scope="col">Date (UTC)</th><th scope="col">Page views</th><th scope="col">Downloads</th><th scope="col">Rooms peak</th><th scope="col">Connections peak</th></tr></thead>
                    <tbody>{#each summary.daily as day}<tr><th scope="row">{dayLabel(day.date)}</th><td>{numbers.format(day.pageviews)}</td><td>{numbers.format(day.downloads)}</td><td>{day.relay.samples ? numbers.format(day.relay.roomsPeak) : '—'}</td><td>{day.relay.samples ? numbers.format(day.relay.connectionsPeak) : '—'}</td></tr>{/each}</tbody>
                </table>
            </div>
        </details>
    {/if}

    <footer>
        <span role="status">{loading ? 'Refreshing…' : updatedAt ? `Updated ${times.format(updatedAt)} · refreshes every minute` : 'Waiting for analytics'}</span>
        {#if summary?.startedAt}<span>Tracking since {dayLabel(summary.startedAt)}</span>{/if}
    </footer>
</main>

<style>
    main { max-width: 1280px; margin: 0 auto; padding: 44px 32px 48px; }
    .heading { display: flex; align-items: center; justify-content: space-between; gap: 20px; margin-bottom: 28px; }
    .eyebrow { margin: 0 0 8px; color: var(--text-2); font-size: 12px; font-weight: 550; }
    h1 { margin: 0; font-size: 36px; font-weight: 650; line-height: 1.2; letter-spacing: -0.035em; }
    .subtitle { margin: 10px 0 0; color: var(--text-2); font-size: 14px; }
    .quiet-button { padding: 8px 12px; border: 1px solid var(--divider); border-radius: 7px; color: var(--text-2); font-size: 13px; }
    .quiet-button span { margin-left: 8px; }
    .quiet-button:hover { color: var(--text-1); border-color: var(--border); }
    .toolbar { display: flex; justify-content: space-between; align-items: center; gap: 16px; margin-bottom: 24px; }
    .ranges { display: flex; gap: 3px; padding: 4px; border: 1px solid var(--divider); border-radius: 9px; background: var(--bg-alt); }
    .ranges button { padding: 6px 14px; border-radius: 5px; color: var(--text-2); font-size: 13px; font-weight: 550; white-space: nowrap; }
    .ranges button:hover { color: var(--text-1); }
    .ranges button.active { background: var(--bg); color: var(--text-1); box-shadow: 0 1px 3px #00000012; }
    .freshness { display: flex; align-items: center; gap: 12px; color: var(--text-2); font-size: 12px; }
    .refresh { display: grid; place-items: center; width: 32px; height: 32px; border: 1px solid var(--divider); border-radius: 6px; }
    .refresh:hover { color: var(--text-1); border-color: var(--border); }
    .refresh:disabled { opacity: 0.4; cursor: default; }
    .refresh svg { width: 15px; height: 15px; stroke: currentColor; stroke-width: 1.6; fill: none; stroke-linecap: round; stroke-linejoin: round; }
    .cards { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 16px; margin-bottom: 20px; }
    .metric { position: relative; padding: 20px; border: 1px solid var(--divider); border-radius: 10px; background: var(--bg); }
    h2 { margin: 0; font-size: 13px; font-weight: 550; }
    .metric h2 { color: var(--text-2); }
    .metric-heading { display: flex; flex-wrap: wrap; align-items: center; justify-content: space-between; gap: 6px; }
    .value { margin: 14px 0 7px; font-size: 36px; line-height: 1; font-weight: 600; letter-spacing: -0.04em; font-variant-numeric: tabular-nums; }
    .traffic .value { color: var(--brand-1); }
    .downloads .value { color: var(--tip); }
    .metric-note { margin: 0; color: var(--text-2); font-size: 11px; }
    .live { display: inline-flex; align-items: center; gap: 5px; color: var(--tip); font-size: 10px; }
    .live::before { content: ''; width: 5px; height: 5px; border-radius: 50%; background: currentColor; }
    .live.stale { color: var(--warning); }
    .live.unknown { color: var(--text-2); }
    .live.unavailable { color: var(--danger); }
    .overview { display: grid; grid-template-columns: minmax(0, 2fr) minmax(0, 1fr); gap: 20px; margin-bottom: 20px; }
    .panel { padding: 22px; border: 1px solid var(--divider); border-radius: 10px; min-width: 0; }
    .panel-heading { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 22px; }
    .panel-heading h2 { color: var(--text-1); font-size: 14px; }
    .legend { display: flex; flex-wrap: wrap; align-items: center; gap: 14px; color: var(--text-2); font-size: 10px; }
    .legend span { display: flex; align-items: center; gap: 6px; }
    .legend i { width: 7px; height: 7px; border-radius: 50%; }
    .pageviews { background: var(--brand-1); }
    .downloadviews { background: var(--tip); }
    .chart { display: block; width: 100%; overflow: visible; }
    .grid-line { stroke: var(--divider); stroke-dasharray: 3 5; }
    .axis-value, .axis-date { fill: var(--text-2); font-family: var(--font); font-size: 10px; }
    .series { fill: none; stroke-width: 2.4; stroke-linecap: round; stroke-linejoin: round; }
    .page-series { stroke: var(--brand-1); }
    .download-series { stroke: var(--tip); stroke-dasharray: 5 4; }
    .page-dot { fill: var(--brand-1); }
    .download-dot { fill: var(--tip); }
    .chart-empty { min-height: 206px; display: flex; flex-direction: column; align-items: center; justify-content: center; text-align: center; }
    .empty-mark { display: grid; place-items: center; width: 34px; height: 34px; border: 1px solid var(--divider); border-radius: 9px; color: var(--text-2); font-size: 20px; }
    .chart-empty p { margin: 14px 0 4px; font-size: 13px; }
    .chart-empty > span:last-child { color: var(--text-2); font-size: 11px; }
    .status-dot { width: 7px; height: 7px; border-radius: 50%; background: var(--tip); }
    .status-dot.stale { background: var(--warning); }
    .status-dot.unknown { background: var(--text-3); }
    .status-dot.unavailable { background: var(--danger); }
    .relay-summary { margin: 0 0 16px; font-size: 13px; }
    .last-attempt { margin: 12px 0 0; color: var(--text-2); font-size: 11px; }
    dl { margin: 0; }
    dl div { display: flex; align-items: baseline; justify-content: space-between; gap: 12px; padding: 8px 0; border-bottom: 1px solid var(--divider); font-size: 12px; }
    dt { color: var(--text-2); }
    dd { margin: 0; font-variant-numeric: tabular-nums; text-align: right; }
    .footnote { margin: 18px 0 0; color: var(--text-2); font-size: 11px; line-height: 1.6; }
    .breakdowns { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 20px; }
    .column-label { color: var(--text-2); font-size: 11px; }
    .rankings { list-style: none; padding: 0; margin: 0; }
    .rankings li { display: flex; align-items: baseline; justify-content: space-between; gap: 16px; padding: 9px 0; border-bottom: 1px solid var(--divider); font-size: 12px; }
    .rankings li:last-child { border-bottom: 0; }
    .rankings a, .rank-label { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .rankings a { color: var(--text-2); font-family: var(--mono); font-size: 11px; }
    .rankings a:hover { color: var(--brand-1); }
    .rankings strong { flex-shrink: 0; font-weight: 550; font-variant-numeric: tabular-nums; }
    .versions .rank-label { font-family: var(--mono); font-size: 11px; }
    .empty-note { margin: 0; color: var(--text-2); font-size: 12px; }
    .daily-table { margin-top: 20px; padding: 16px 22px; border: 1px solid var(--divider); border-radius: 10px; }
    .daily-table summary { font-size: 12px; font-weight: 550; cursor: pointer; }
    .daily-table summary span { margin-left: 8px; color: var(--text-2); font-weight: 400; font-size: 11px; }
    .table-scroll { overflow-x: auto; }
    table { width: 100%; margin-top: 16px; border-collapse: collapse; text-align: right; font-size: 12px; white-space: nowrap; font-variant-numeric: tabular-nums; }
    th, td { padding: 9px 12px; border-bottom: 1px solid var(--divider); }
    th { font-weight: 500; color: var(--text-2); }
    th:first-child { padding-left: 0; text-align: left; }
    footer { display: flex; flex-wrap: wrap; justify-content: space-between; gap: 8px; margin-top: 24px; color: var(--text-2); font-size: 11px; }
    .error { display: flex; align-items: center; justify-content: space-between; gap: 16px; margin-bottom: 20px; padding: 12px 16px; border: 1px solid var(--danger); border-radius: 8px; color: var(--danger); background: var(--danger-soft); font-size: 13px; }
    .error button { font-weight: 600; text-decoration: underline; }
    @media (max-width: 1050px) { .cards { gap: 12px; } .metric { padding: 18px 16px; } .metric-heading { justify-content: start; } }
    @media (max-width: 850px) { .cards { grid-template-columns: repeat(2, minmax(0, 1fr)); } .overview { grid-template-columns: minmax(0, 1fr); } .relay dl { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 20px; } .relay dl div { flex-direction: column; gap: 3px; } }
    @media (max-width: 600px) {
        main { padding: 28px 20px 36px; }
        .heading { align-items: start; gap: 12px; }
        h1 { font-size: 30px; }
        .subtitle { max-width: 210px; font-size: 12px; }
        .quiet-button { font-size: 11px; padding: 6px 8px; white-space: nowrap; }
        .quiet-button span { margin-left: 4px; }
        .toolbar { flex-wrap: wrap; gap: 12px; }
        .ranges { width: 100%; }
        .ranges button { flex: 1; padding: 6px 8px; }
        .freshness { width: 100%; justify-content: space-between; }
        .metric { padding: 16px 14px; }
        .metric h2 { font-size: 11px; }
        .value { font-size: 32px; }
        .metric-note { font-size: 10px; }
        .overview, .breakdowns { gap: 16px; }
        .breakdowns { grid-template-columns: minmax(0, 1fr); }
        .panel { padding: 18px; }
        .activity .panel-heading { flex-wrap: wrap; }
        .chart-empty { min-height: 160px; }
        .relay dl { gap: 12px; }
        .relay dl div { font-size: 11px; }
        .daily-table { padding: 14px 18px; }
        .daily-table summary span { display: block; margin: 4px 0 0 16px; }
    }
</style>
