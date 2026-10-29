import dataset from './data/benchmarks.json';

export interface BenchmarkEngine {
    id: string;
    label: string;
    version: string;
    method: string;
}

export interface BenchmarkResult {
    engine: string;
    medianMs: number;
    minMs: number;
    maxMs: number;
}

export interface BenchmarkCase {
    id: string;
    label: string;
    threads: number;
    results: BenchmarkResult[];
}

export interface BenchmarkReport {
    id: string;
    title: string;
    date: string;
    cpu: string;
    os: string;
    tideRevision: string;
    benchmarkRevision: string;
    resultPath: string;
    rounds: number;
    warmup: number;
    ticks: number;
    engines: BenchmarkEngine[];
    cases: BenchmarkCase[];
    notes: string[];
}

export const benchmarks: { schema: number; repository: string; reports: BenchmarkReport[] } = dataset;

// Invalid or incomplete measurements must fail the docs build, not become a
// zero-length bar or a misleading comparison on the public page.
if (benchmarks.schema !== 1 || benchmarks.reports.length === 0) throw new Error('Invalid benchmark dataset');
for (const report of benchmarks.reports) {
    const engines = new Set(report.engines.map(({ id }) => id));
    if (engines.size !== report.engines.length || !engines.size || !report.cases.length
        || !Number.isInteger(report.rounds) || report.rounds < 2
        || !/^[a-f0-9]{40}$/.test(report.tideRevision)
        || !/^[a-f0-9]{40}$/.test(report.benchmarkRevision)
        || !/^results\/[a-zA-Z0-9_./-]+$/.test(report.resultPath) || report.resultPath.includes('..')) {
        throw new Error(`Invalid benchmark report: ${report.id}`);
    }
    for (const item of report.cases) {
        if (item.results.length !== engines.size || new Set(item.results.map(({ engine }) => engine)).size !== engines.size
            || !Number.isInteger(item.threads) || item.threads < 1) throw new Error(`Incomplete benchmark case: ${item.id}`);
        for (const result of item.results) {
            if (!engines.has(result.engine) || ![result.minMs, result.medianMs, result.maxMs].every((n) => Number.isFinite(n) && n > 0)
                || result.minMs > result.medianMs || result.maxMs < result.medianMs) {
                throw new Error(`Invalid benchmark result: ${item.id}/${result.engine}`);
            }
        }
    }
}

export function milliseconds(value: number): string {
    return value.toFixed(5);
}

export function sourceUrl(report: BenchmarkReport): string {
    return `${benchmarks.repository}/tree/${report.benchmarkRevision}`;
}

export function resultsUrl(report: BenchmarkReport): string {
    return `${sourceUrl(report)}/${report.resultPath}`;
}

/** Plain-text exports carry the same measurements as the interactive page. */
export function benchmarkMarkdown(): string {
    return benchmarks.reports.map((report) => {
        const table = [
            `| Workload | Threads | ${report.engines.map(({ label }) => `${label}, ms (min–max)`).join(' | ')} |`,
            `| --- | --- | ${report.engines.map(() => '---').join(' | ')} |`,
            ...report.cases.map((item) => `| ${item.label} | ${item.threads} | ${report.engines.map(({ id }) => {
                const result = item.results.find(({ engine }) => engine === id)!;
                return `${milliseconds(result.medianMs)} (${milliseconds(result.minMs)}–${milliseconds(result.maxMs)})`;
            }).join(' | ')} |`),
        ].join('\n');
        return [
            `**${report.title}**`,
            `${report.date} · ${report.cpu} · ${report.os}.`,
            `${report.rounds} processes per implementation and workload; ${report.warmup} warmup ticks and ${report.ticks} measured ticks per process.`,
            `Tide revision: [${report.tideRevision}](https://github.com/Tide-Engine/tide-engine/commit/${report.tideRevision}). [Benchmark source](${sourceUrl(report)}). [Raw measurements](${resultsUrl(report)}).`,
            report.engines.map((engine) => `- ${engine.label} ${engine.version}: ${engine.method}`).join('\n'),
            table,
            report.notes.join('\n\n'),
        ].join('\n\n');
    }).join('\n\n');
}
