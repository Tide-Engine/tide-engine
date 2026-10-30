<script lang="ts">
    import { onMount } from 'svelte';

    type Summary = {
        from: string;
        to: string;
        days: number;
        totals: { events: number; failed: number; installs: number; installsToday: number; installsWeek: number; ci: number };
        daily: { date: string; events: number; installs: number; failed: number }[];
        weekly: { from: string; to: string; installs: number }[];
        commands: { command: string; events: number; failed: number; installs: number }[];
        failures: { command: string; error: string; events: number }[];
        versions: { version: string; installs: number; events: number }[];
        platforms: { os: string; arch: string; installs: number; events: number }[];
        builds: { target: string; count: number; p50: number; p90: number; p99: number }[];
    };

    const ranges = [{ days: 1, label: 'Today' }, { days: 7, label: '7 days' }, { days: 30, label: '30 days' }, { days: 90, label: '90 days' }];
    const numbers = new Intl.NumberFormat('en');
    const percent = new Intl.NumberFormat('en', { style: 'percent', maximumFractionDigits: 1 });
    const dates = new Intl.DateTimeFormat('en', { month: 'short', day: 'numeric', timeZone: 'UTC' });
    const times = new Intl.DateTimeFormat('en', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
    let selectedDays = $state(30);
    let summary = $state<Summary | null>(null);
    let loading = $state(true);
    let error = $state('');
    let updatedAt = $state<Date | null>(null);
    let controller: AbortController | undefined;
    let disposed = false;

    const maximum = $derived(Math.max(1, ...(summary?.daily.flatMap((day) => [day.events, day.installs]) ?? [])));
    const hasEvents = $derived(summary !== null && summary.daily.some((day) => day.events > 0));

    function dayLabel(value: string) {
        return dates.format(new Date(`${value.slice(0, 10)}T00:00:00Z`));
    }

    function rangeLabel() {
        if (!summary) return 'All dates use UTC';
        return summary.from === summary.to ? `${dayLabel(summary.from)} · UTC` : `${dayLabel(summary.from)} – ${dayLabel(summary.to)} · UTC`;
    }

    function points(field: 'events' | 'installs') {
        const daily = summary?.daily ?? [];
        return daily.map((day, index) => {
            const x = daily.length === 1 ? 340 : 38 + (index / (daily.length - 1)) * 602;
            return `${x},${174 - (day[field] / maximum) * 144}`;
        }).join(' ');
    }

    function seconds(ms: number) {
        return ms >= 60_000 ? `${(ms / 60_000).toFixed(1)} min` : `${(ms / 1000).toFixed(ms < 10_000 ? 2 : 1)} s`;
    }

    function platformName(os: string, arch: string) {
        return `${{ windows: 'Windows', linux: 'Linux', macos: 'macOS' }[os] ?? 'Other'} ${arch}`;
    }

    async function refresh() {
        controller?.abort();
        const request = new AbortController();
        controller = request;
        loading = true;
        try {
            const response = await fetch(`/admin/api/telemetry?days=${selectedDays}`, { signal: request.signal, cache: 'no-store' });
            if (response.status === 401 || response.redirected) {
                window.location.assign('/admin/login');
                return;
            }
            if (!response.ok) throw new Error('CLI usage data could not be loaded.');
            const result: Summary = await response.json();
            if (disposed || request.signal.aborted) return;
            summary = result;
            updatedAt = new Date();
            error = '';
        } catch (failure) {
            if (!disposed && !request.signal.aborted) error = failure instanceof Error ? failure.message : 'CLI usage data could not be loaded.';
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
        const interval = window.setInterval(() => void refresh(), 60_000);
        return () => {
            disposed = true;
            controller?.abort();
            window.clearInterval(interval);
        };
    });
</script>

<svelte:head>
    <title>CLI usage | Tide Engine</title>
    <meta name="robots" content="noindex, nofollow" />
</svelte:head>

<main id="content">
    <div class="heading">
        <div>
            <p class="eyebrow">Tide Engine / Admin</p>
            <h1>CLI usage</h1>
            <p class="subtitle">Anonymous events from the tide command, excluding CI. <a href="/tools/telemetry">What is sent</a></p>
        </div>
        <form method="POST" action="/admin/logout">
            <button class="quiet-button" type="submit">Sign out <span aria-hidden="true">↗</span></button>
        </form>
    </div>

    <nav class="admin-nav" aria-label="Admin sections">
        <a href="/admin">Analytics</a>
        <a href="/admin/telemetry" aria-current="page">CLI usage</a>
        <a href="/admin/prototypes">Prototypes</a>
    </nav>

    <div class="toolbar">
        <div class="ranges" role="group" aria-label="Date range">
            {#each ranges as range}
                <button type="button" class:active={selectedDays === range.days} aria-pressed={selectedDays === range.days} onclick={() => selectRange(range.days)}>{range.label}</button>
            {/each}
        </div>
        <div class="freshness">
            <span>{rangeLabel()}</span>
            <button class="refresh" type="button" aria-label="Refresh CLI usage" disabled={loading} onclick={() => void refresh()}>
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
        <section class="metric">
            <h2>Active installs today</h2>
            <p class="value brand">{summary ? numbers.format(summary.totals.installsToday) : '—'}</p>
            <p class="metric-note">{summary ? `${numbers.format(summary.totals.installsWeek)} in the last 7 days` : 'Distinct install IDs'}</p>
        </section>
        <section class="metric">
            <h2>Active installs</h2>
            <p class="value">{summary ? numbers.format(summary.totals.installs) : '—'}</p>
            <p class="metric-note">Distinct install IDs in this period</p>
        </section>
        <section class="metric">
            <h2>Commands</h2>
            <p class="value tip">{summary ? numbers.format(summary.totals.events) : '—'}</p>
            <p class="metric-note">{summary ? `${numbers.format(summary.totals.ci)} CI commands excluded` : 'Excludes CI commands'}</p>
        </section>
        <section class="metric">
            <h2>Failure rate</h2>
            <p class="value">{summary && summary.totals.events ? percent.format(summary.totals.failed / summary.totals.events) : '—'}</p>
            <p class="metric-note">{summary ? `${numbers.format(summary.totals.failed)} failed commands` : 'Of all commands'}</p>
        </section>
    </div>

    <section class="panel activity">
        <div class="panel-heading"><h2>Daily activity</h2><div class="legend"><span><i class="events"></i>Commands</span><span><i class="installs"></i>Active installs</span></div></div>
        {#if hasEvents && summary}
            <svg class="chart" viewBox="0 0 670 214" role="img" aria-label="Daily commands and active installs. Exact values are in the daily totals table below.">
                {#each [0, 0.5, 1] as tick}
                    <line class="grid-line" x1="38" x2="640" y1={174 - tick * 144} y2={174 - tick * 144} />
                    <text class="axis-value" x="28" y={178 - tick * 144} text-anchor="end">{numbers.format(Math.round(maximum * tick))}</text>
                {/each}
                <polyline class="series event-series" points={points('events')} />
                <polyline class="series install-series" points={points('installs')} />
                {#if summary.daily.length === 1}
                    <circle class="event-dot" cx="340" cy={174 - (summary.daily[0].events / maximum) * 144} r="4" />
                    <circle class="install-dot" cx="340" cy={174 - (summary.daily[0].installs / maximum) * 144} r="4" />
                {/if}
                <text class="axis-date" x="38" y="202">{dayLabel(summary.from)}</text>
                {#if summary.daily.length > 1}<text class="axis-date" x="640" y="202" text-anchor="end">{dayLabel(summary.to)}</text>{/if}
            </svg>
        {:else}
            <div class="chart-empty"><p>{loading && !summary ? 'Loading CLI usage…' : 'No commands recorded in this period.'}</p><span>Events arrive from installed tide versions that send usage data.</span></div>
        {/if}
    </section>

    <div class="breakdowns">
        <section class="panel">
            <div class="panel-heading"><h2>Commands and failure rate</h2><span class="column-label">Runs · failed</span></div>
            {#if summary?.commands.length}
                <ol class="rankings">
                    {#each summary.commands as row}
                        <li><span class="rank-label mono">{row.command}</span><strong>{numbers.format(row.events)} · {percent.format(row.failed / row.events)}</strong></li>
                    {/each}
                </ol>
            {:else}<p class="empty-note">No commands in this period.</p>{/if}
        </section>
        <section class="panel">
            <div class="panel-heading"><h2>Build times</h2><span class="column-label">Median · p90 · p99</span></div>
            {#if summary?.builds.length}
                <ol class="rankings">
                    {#each summary.builds as row}
                        <li><span class="rank-label">{row.target} <small>({numbers.format(row.count)} builds)</small></span><strong>{seconds(row.p50)} · {seconds(row.p90)} · {seconds(row.p99)}</strong></li>
                    {/each}
                </ol>
            {:else}<p class="empty-note">No successful builds in this period.</p>{/if}
            <p class="footnote">Successful <code>tide build</code> runs, from start to end of the command.</p>
        </section>
        <section class="panel">
            <div class="panel-heading"><h2>Versions in use</h2><span class="column-label">Installs</span></div>
            {#if summary?.versions.length}
                <ol class="rankings">
                    {#each summary.versions as row}
                        <li><span class="rank-label mono">{row.version}</span><strong>{numbers.format(row.installs)}</strong></li>
                    {/each}
                </ol>
            {:else}<p class="empty-note">No versions in this period.</p>{/if}
        </section>
        <section class="panel">
            <div class="panel-heading"><h2>Operating systems</h2><span class="column-label">Installs</span></div>
            {#if summary?.platforms.length}
                <ol class="rankings">
                    {#each summary.platforms as row}
                        <li><span class="rank-label">{platformName(row.os, row.arch)}</span><strong>{numbers.format(row.installs)}</strong></li>
                    {/each}
                </ol>
            {:else}<p class="empty-note">No platforms in this period.</p>{/if}
        </section>
        <section class="panel">
            <div class="panel-heading"><h2>Failures by kind</h2><span class="column-label">Runs</span></div>
            {#if summary?.failures.length}
                <ol class="rankings">
                    {#each summary.failures as row}
                        <li><span class="rank-label mono">{row.command} · {row.error}</span><strong>{numbers.format(row.events)}</strong></li>
                    {/each}
                </ol>
            {:else}<p class="empty-note">No failures in this period.</p>{/if}
        </section>
        <section class="panel">
            <div class="panel-heading"><h2>Weekly active installs</h2><span class="column-label">Installs</span></div>
            {#if summary?.weekly.length}
                <ol class="rankings">
                    {#each summary.weekly as row}
                        <li><span class="rank-label">{row.from === row.to ? dayLabel(row.to) : `${dayLabel(row.from)} – ${dayLabel(row.to)}`}</span><strong>{numbers.format(row.installs)}</strong></li>
                    {/each}
                </ol>
            {:else}<p class="empty-note">No installs in this period.</p>{/if}
        </section>
    </div>

    {#if summary?.daily.length}
        <details class="daily-table">
            <summary>Daily totals <span>Commands, active installs and failures</span></summary>
            <div class="table-scroll">
                <table>
                    <caption class="visually-hidden">Daily CLI usage totals in UTC</caption>
                    <thead><tr><th scope="col">Date (UTC)</th><th scope="col">Commands</th><th scope="col">Active installs</th><th scope="col">Failed</th></tr></thead>
                    <tbody>{#each summary.daily as day}<tr><th scope="row">{dayLabel(day.date)}</th><td>{numbers.format(day.events)}</td><td>{numbers.format(day.installs)}</td><td>{numbers.format(day.failed)}</td></tr>{/each}</tbody>
                </table>
            </div>
        </details>
    {/if}

    <footer>
        <span role="status">{loading ? 'Refreshing…' : updatedAt ? `Updated ${times.format(updatedAt)} · refreshes every minute` : 'Waiting for CLI usage'}</span>
        <span>Events are kept for 13 months</span>
    </footer>
</main>

<style>
    main { max-width: 1280px; margin: 0 auto; padding: 44px 32px 48px; }
    .heading { display: flex; align-items: center; justify-content: space-between; gap: 20px; margin-bottom: 28px; }
    .eyebrow { margin: 0 0 8px; color: var(--text-2); font-size: 12px; font-weight: 550; }
    h1 { margin: 0; font-size: 36px; font-weight: 650; line-height: 1.2; letter-spacing: -0.035em; }
    .subtitle { margin: 10px 0 0; color: var(--text-2); font-size: 14px; }
    .subtitle a { color: var(--brand-1); }
    .quiet-button { padding: 8px 12px; border: 1px solid var(--divider); border-radius: 7px; color: var(--text-2); font-size: 13px; }
    .quiet-button span { margin-left: 8px; }
    .quiet-button:hover { color: var(--text-1); border-color: var(--border); }
    .admin-nav { display: flex; gap: 24px; border-bottom: 1px solid var(--divider); margin: 0 0 28px; }
    .admin-nav a { padding: 0 0 12px; color: var(--text-2); font-size: 13px; }
    .admin-nav a:hover { color: var(--text-1); }
    .admin-nav a[aria-current] { color: var(--brand-1); border-bottom: 2px solid var(--brand-1); }
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
    .metric { padding: 20px; border: 1px solid var(--divider); border-radius: 10px; background: var(--bg); }
    h2 { margin: 0; font-size: 13px; font-weight: 550; }
    .metric h2 { color: var(--text-2); }
    .value { margin: 14px 0 7px; font-size: 36px; line-height: 1; font-weight: 600; letter-spacing: -0.04em; font-variant-numeric: tabular-nums; }
    .value.brand { color: var(--brand-1); }
    .value.tip { color: var(--tip); }
    .metric-note { margin: 0; color: var(--text-2); font-size: 11px; }
    .panel { padding: 22px; border: 1px solid var(--divider); border-radius: 10px; min-width: 0; }
    .activity { margin-bottom: 20px; }
    .panel-heading { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 22px; }
    .panel-heading h2 { color: var(--text-1); font-size: 14px; }
    .legend { display: flex; flex-wrap: wrap; align-items: center; gap: 14px; color: var(--text-2); font-size: 10px; }
    .legend span { display: flex; align-items: center; gap: 6px; }
    .legend i { width: 7px; height: 7px; border-radius: 50%; }
    .events { background: var(--tip); }
    .installs { background: var(--brand-1); }
    .chart { display: block; width: 100%; overflow: visible; }
    .grid-line { stroke: var(--divider); stroke-dasharray: 3 5; }
    .axis-value, .axis-date { fill: var(--text-2); font-family: var(--font); font-size: 10px; }
    .series { fill: none; stroke-width: 2.4; stroke-linecap: round; stroke-linejoin: round; }
    .event-series { stroke: var(--tip); stroke-dasharray: 5 4; }
    .install-series { stroke: var(--brand-1); }
    .event-dot { fill: var(--tip); }
    .install-dot { fill: var(--brand-1); }
    .chart-empty { min-height: 206px; display: flex; flex-direction: column; align-items: center; justify-content: center; text-align: center; }
    .chart-empty p { margin: 0 0 4px; font-size: 13px; }
    .chart-empty span { color: var(--text-2); font-size: 11px; }
    .breakdowns { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 20px; }
    .column-label { color: var(--text-2); font-size: 11px; }
    .rankings { list-style: none; padding: 0; margin: 0; }
    .rankings li { display: flex; align-items: baseline; justify-content: space-between; gap: 16px; padding: 9px 0; border-bottom: 1px solid var(--divider); font-size: 12px; }
    .rankings li:last-child { border-bottom: 0; }
    .rank-label { min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .rank-label small { color: var(--text-2); }
    .mono { font-family: var(--mono); font-size: 11px; }
    .rankings strong { flex-shrink: 0; font-weight: 550; font-variant-numeric: tabular-nums; }
    .empty-note { margin: 0; color: var(--text-2); font-size: 12px; }
    .footnote { margin: 18px 0 0; color: var(--text-2); font-size: 11px; line-height: 1.6; }
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
    @media (max-width: 850px) { .cards { grid-template-columns: repeat(2, minmax(0, 1fr)); } }
    @media (max-width: 600px) {
        main { padding: 28px 20px 36px; }
        .heading { align-items: start; gap: 12px; }
        h1 { font-size: 30px; }
        .subtitle { font-size: 12px; }
        .toolbar { flex-wrap: wrap; gap: 12px; }
        .ranges { width: 100%; }
        .ranges button { flex: 1; padding: 6px 8px; }
        .freshness { width: 100%; justify-content: space-between; }
        .metric { padding: 16px 14px; }
        .value { font-size: 32px; }
        .breakdowns { grid-template-columns: minmax(0, 1fr); gap: 16px; }
        .panel { padding: 18px; }
    }
</style>
