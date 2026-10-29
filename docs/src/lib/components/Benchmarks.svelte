<script lang="ts">
    import { benchmarks, milliseconds, resultsUrl, sourceUrl } from '#lib/benchmarks.ts';

    let reportId = $state(benchmarks.reports[0].id);
    let caseId = $state(benchmarks.reports[0].cases[0].id);
    const report = $derived(benchmarks.reports.find(({ id }) => id === reportId)!);
    const workload = $derived(report.cases.find(({ id }) => id === caseId) ?? report.cases[0]);
    const maximum = $derived(Math.max(...workload.results.map(({ maxMs }) => maxMs)));
</script>

<div class="benchmarks">
    {#if benchmarks.reports.length > 1}
        <label class="control">
            <span>Measurement report</span>
            <select bind:value={reportId}>
                {#each benchmarks.reports as item}<option value={item.id}>{item.title}</option>{/each}
            </select>
        </label>
    {/if}

    <div class="report-heading">
        <strong>{report.title}</strong>
        <span>{report.date} · {report.cpu} · {report.os}</span>
    </div>

    <div class="chart-card">
        <label class="control">
            <span>Workload</span>
            <select value={workload.id} onchange={(event) => caseId = event.currentTarget.value}>
                {#each report.cases as item}
                    <option value={item.id}>{item.label} · {item.threads} {item.threads === 1 ? 'thread' : 'threads'}</option>
                {/each}
            </select>
        </label>
        <p class="chart-caption">Milliseconds per ECS tick · lower is faster · bars start at zero</p>
        <div class="chart" aria-label={`${workload.label}, ${workload.threads} threads`}>
            {#each report.engines as engine}
                {@const result = workload.results.find((item) => item.engine === engine.id)!}
                <div class="chart-row">
                    <span class="engine">{engine.label}</span>
                    <div class="track" aria-hidden="true">
                        <div class:tide={engine.id === 'tide'} class="bar" style:width={`${result.medianMs / maximum * 100}%`}></div>
                        <div class="range" style:left={`${result.minMs / maximum * 100}%`} style:width={`${(result.maxMs - result.minMs) / maximum * 100}%`}></div>
                    </div>
                    <span class="value">{milliseconds(result.medianMs)}<span class="range-value">{milliseconds(result.minMs)}–{milliseconds(result.maxMs)}</span></span>
                </div>
            {/each}
        </div>
        <p class="chart-caption">Bars show medians. Lines and smaller values show the range of process medians.</p>
    </div>

    <p class="run-details">{report.rounds} processes per implementation and workload · {report.warmup} warmup ticks · {report.ticks} measured ticks per process</p>
    <p class="source-links">
        <a href={sourceUrl(report)}>Benchmark source</a>
        <a href={resultsUrl(report)}>Raw measurements</a>
        <a href={`https://github.com/Tide-Engine/tide-engine/commit/${report.tideRevision}`}>Tide {report.tideRevision.slice(0, 9)}</a>
    </p>

    <details class="implementations">
        <summary>Versions and implementations</summary>
        <ul>
            {#each report.engines as engine}<li><strong>{engine.label} {engine.version}</strong> — {engine.method}</li>{/each}
        </ul>
    </details>

    <!-- svelte-ignore a11y_no_noninteractive_tabindex (Keyboard users need to scroll the wide results table.) -->
    <div class="table-scroll" role="region" aria-label="All measured workloads" tabindex="0">
        <table>
            <caption>All results in this report · median milliseconds per tick, with min–max below</caption>
            <thead><tr><th scope="col">Workload</th><th scope="col">Threads</th>{#each report.engines as engine}<th scope="col">{engine.label}</th>{/each}</tr></thead>
            <tbody>
                {#each report.cases as item}
                    <tr>
                        <th scope="row">{item.label}</th><td>{item.threads}</td>
                        {#each report.engines as engine}
                            {@const result = item.results.find((value) => value.engine === engine.id)!}
                            <td class="timing">{milliseconds(result.medianMs)}<span class="range-value">{milliseconds(result.minMs)}–{milliseconds(result.maxMs)}</span></td>
                        {/each}
                    </tr>
                {/each}
            </tbody>
        </table>
    </div>

    {#each report.notes as note}<p class="report-note">{note}</p>{/each}
</div>

<style>
    .benchmarks { margin: 28px 0 40px; }
    .report-heading { display: flex; flex-direction: column; gap: 5px; margin-bottom: 18px; }
    .report-heading strong { font-size: 18px; }
    .report-heading > span, .run-details, .report-note { color: var(--text-2); font-size: 13px; }
    .chart-card { border: 1px solid var(--divider); border-radius: 12px; padding: 20px; background: var(--bg-alt); }
    .control { display: flex; flex-direction: column; gap: 6px; margin-bottom: 16px; font-size: 13px; color: var(--text-2); }
    select { width: 100%; padding: 10px 12px; border: 1px solid var(--border); border-radius: 6px; color: var(--text-1); background: var(--bg); font: inherit; font-size: 14px; }
    select:focus-visible, .table-scroll:focus-visible { outline: 2px solid var(--brand-1); outline-offset: 3px; }
    .chart-caption { margin: 0; color: var(--text-2); font-size: 12px; }
    .chart { display: grid; gap: 18px; margin: 24px 0; }
    .chart-row { display: grid; grid-template-columns: 52px minmax(0, 1fr) 126px; gap: 12px; align-items: center; }
    .engine { font-size: 14px; font-weight: 600; }
    .track { position: relative; height: 28px; border-left: 1px solid var(--border); }
    .bar { height: 100%; min-width: 1px; border-radius: 0 3px 3px 0; background: var(--text-3); }
    .bar.tide { background: var(--brand-fill); }
    .range { position: absolute; top: 11px; height: 6px; min-width: 1px; border: 1px solid var(--text-1); background: var(--bg); }
    .value { text-align: right; font-size: 14px; font-variant-numeric: tabular-nums; line-height: 1.5; }
    .range-value { display: block; font-size: 10px; color: var(--text-2); font-weight: 400; white-space: nowrap; }
    .source-links { display: flex; flex-wrap: wrap; gap: 8px 18px; margin-top: 8px; font-size: 13px; }
    .implementations { margin: 20px 0; padding: 12px 16px; border: 1px solid var(--divider); border-radius: 8px; font-size: 13px; }
    summary { cursor: pointer; font-weight: 600; }
    .implementations ul { margin-bottom: 0; }
    .table-scroll { overflow-x: auto; margin-top: 24px; }
    table { display: table; width: 100%; margin: 0; font-size: 12px; line-height: 1.6; }
    caption { padding: 0 0 12px; color: var(--text-2); text-align: left; font-size: 12px; }
    th, td { padding: 10px 12px; }
    th { white-space: nowrap; }
    tbody th { min-width: 130px; text-align: left; font-weight: 500; white-space: normal; }
    .timing { font-variant-numeric: tabular-nums; white-space: nowrap; }
    @media (max-width: 480px) {
        .chart-card { padding: 14px; }
        .chart-row { grid-template-columns: 42px minmax(0, 1fr) 102px; gap: 8px; }
        .value { font-size: 13px; }
        .range-value { font-size: 9px; }
        .engine { font-size: 13px; }
        th, td { padding: 9px; }
    }
</style>
