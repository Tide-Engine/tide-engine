from pathlib import Path
import json
import shutil

root = Path(__file__).resolve().parents[2]
work = root / 'build/gi-quality-20261102'
report = root / 'bench/results/globalillumination-lighting-20261102'
summary = json.loads((report / 'summary.json').read_text(encoding='utf-8-sig'))
metadata = json.loads((report / 'metadata.json').read_text(encoding='utf-8-sig'))
analysis = json.loads((report / 'capture-analysis.json').read_text(encoding='utf-8'))
assert metadata['complete'] is True and metadata['status'] == 'passed'
assert summary['process_count'] == 18 and summary['gpu_samples'] == 1800
assert summary['source_sha'] == metadata['source_sha']
assert summary['binary_sha256'] == metadata['binary_sha256']
source = summary['source_sha']
captures = '''All four GI-disabled camera pairs are identical in every RGBA pixel
(960x720 each), and all sixteen world hashes match the earlier captures.
Every enabled run retains complete captured support and nonzero energy at all
fifteen fixed hero rear-wall witnesses. The two 2,048-cap hero views previously
had zero GI gain at all fifteen of these points; both now light all fifteen.
The courtyard's previous 512-cap witnesses were already lit, so that case is
not reported as a fifteen-point missing-light repair.

| Hero view | Cap | Physical probes | Blocks | Minimum / mean summed RGB gain |
|---|---:|---:|---:|---:|
'''
for row in analysis['groups']:
    if row['capture'].endswith(('-hero-512', '-hero-2048')):
        scene = row['capture'].split('-')[0].title()
        captures += f"| {scene} | {row['probe_capacity']:,} | {row['probes']:,} | {row['active_blocks']} | {row['regional_gain_min']:.1f} / {row['regional_gain_mean']:.1f} |\n"
captures += '''
These figures verify the missing-light failure rather than overall aesthetic
quality. The captures still show coarse spatial variation and hard dark regions;
this repair does not establish final production lighting quality.
'''
validation = '''All **17 native Windows ASan** and **35 optimized web** scoped CTests pass.
The final GI-only kernel refresh passes 11 native / 26 web checks; the unchanged
Render3D material/pixel/skinning matrix supplies the remaining 6 / 9.
The [collected validation receipt](final-validation-summary.json) identifies
each current registration's latest successful attempt and retains earlier
failed and retired attempts. The docs production build also passes.
'''
performance = '''The bounded quiet window opened. All 18 planned processes passed, retaining
1,800 measured whole-frame GPU samples with no retry or exclusion. Optimized,
unsanitized Vulkan at 960x720 used the frozen source and binary identities in
[metadata.json](metadata.json). Hardware: RTX 4080 SUPER / Ryzen 9 7900X.
The gate released/disposed at **06:44:25.368 UTC** before reporting or plots.

Each value below is the median of three 100-frame process medians, following
20 warmup frames. Ranges are the minimum and maximum process medians. Enabled
cases retain an updates-per-frame setting of four but perform **zero probe
captures, capture draws or texture uploads during timing**. This measures the
settled sampling path, not active updates or convergence speed.

| Scene | Case | Whole-frame GPU ms [range] | CPU recording ms | Resident probes |
|---|---|---:|---:|---:|
'''
labels = {'off512': 'GI off', 'update4-512': '512 cap', 'update4-2048': '2,048 cap'}
for row in summary['groups']:
    performance += f"| {row['scene'].title()} | {labels[row['case']]} | {row['gpu_ms']:.3f} [{row['gpu_ms_min']:.3f}, {row['gpu_ms_max']:.3f}] | {row['cpu_record_ms']:.4f} | {row['probes']:,} |\n"
performance += '''
The GI sampling cost is material even after convergence. Cold setup CPU
recording medians are 103.50/336.83 ms for atrium 512/2,048 and
170.77/566.16 ms for courtyard. Total untimed validation/convergence CPU
recording is 208.33/608.04 and 232.26/741.00 ms respectively; these are sums
over initialization frames, not individual active-frame or GPU times. They
include the fixed hero validation field, and overview invocations may later
build a different field. Cold topology/reveal spikes remain unresolved.

[Interactive cost comparison](performance-comparison.html),
[standalone chart PNG](performance.png), [SVG](performance.svg),
[summary and process ranges](summary.json), [per-process rows](runs.jsonl)
and [unabridged attempts](attempts.json) retain the measurements. All eighteen
raw JSONL/stdout/stderr/run records remain in `raw/`. Boundary clock samples
are instantaneous; clocks, temperature and unrelated desktop activity were
not controlled continuously. No active-update speedup or pipelined-FPS claim
is made.
'''
text = (work / 'report-template.md').read_text(encoding='utf-8')
for key, value in {'__SOURCE__': source, '__CAPTURE_RESULTS__': captures,
                   '__VALIDATION__': validation, '__PERFORMANCE__': performance}.items():
    text = text.replace(key, value)
assert not any(key in text for key in ('__SOURCE__', '__CAPTURE_RESULTS__', '__VALIDATION__', '__PERFORMANCE__'))
(report / 'README.md').write_text(text, encoding='utf-8')
(report / '.gitattributes').write_text('* -text -whitespace\n', encoding='utf-8')
shutil.copy2(__file__, report / 'diagnostics/finalize-report.py')
note = root / 'notes/global-illumination.md'
text = note.read_text(encoding='utf-8')
marker = '\n## Larger continuous examples (2026-11-02)'
assert marker in text
addition = f'''
Final matched Vulkan captures on source `{source}` retain all four direct-only
RGBA baselines and all sixteen world hashes. Both previously black 2,048-cap
hero rear-wall regions now pass all fifteen support/energy witnesses. Actual
images still show coarse spatial variation; these checks do not establish
production aesthetic quality. The quiet controlled cohort passed all eighteen
processes / 1,800 GPU samples. Settled whole-frame GPU medians on RTX 4080 SUPER
at 960x720 are 4.601/4.910 ms for atrium 512/2,048 and 2.685/2.631 ms for
courtyard (off .869/.595 ms). CPU recording is .199/.205 and .281/.294 ms.
No captures/uploads occur in those timed frames. First-frame setup CPU medians
span 103.50–566.16 ms; cold topology/reveal spikes remain unresolved. This is
no active-update speedup or FPS claim. The final shared gate was released and
disposed at 06:44:25.368 UTC, before offline evidence/plots; no GI processes or
gates remain queued. Full ranges, identities, raw failures and limits are in
the linked lighting repair report. No dev merge or CI dispatch occurred.
'''
text = text.replace(marker, '\n' + addition + marker, 1)
note.write_text(text, encoding='utf-8')
print('Final report and evidence note written from the successful frozen cohort.')
