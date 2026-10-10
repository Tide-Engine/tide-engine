import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';

const script=fileURLToPath(import.meta.url);
const scripts=path.dirname(script);
let root=scripts;
while(!fs.existsSync(path.join(root,'CMakeLists.txt'))||!fs.existsSync(path.join(root,'bench/skinned'))){
    const parent=path.dirname(root);if(parent===root)throw new Error('Run this script inside the Tide repository');root=parent;
}
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const assert=(ok,message)=>{if(!ok)throw new Error(message);};
const baselineRevision='711af03879d9bd687edc4f977ec92f80544e0c22';
const initialRevision='000a5986ba637eb9638f8b187b74736e95103378';
const historicalLedgerHash='0cdfc8ed2fe4cdc99ec2d6602870483dffe5c507630960e3338e829d3090ad1b';
const inputLedgerHash='e60cf4d74b6256fc4a8a02ea44ba9936dbef00f02e1e96946d3290c1203bea44';
const decode=bytes=>{
    let encoding='utf-8',offset=0;
    if(bytes[0]===0xff&&bytes[1]===0xfe){encoding='utf-16le';offset=2;}
    else if(bytes[0]===0xfe&&bytes[1]===0xff){encoding='utf-16be';offset=2;}
    else if(bytes[0]===0xef&&bytes[1]===0xbb&&bytes[2]===0xbf)offset=3;
    assert(encoding==='utf-8'||(bytes.length-offset)%2===0,'Truncated UTF-16 artifact');
    return new TextDecoder(encoding,{fatal:true,ignoreBOM:true}).decode(bytes.subarray(offset));
};
const json=file=>JSON.parse(decode(fs.readFileSync(file)));
const rowsFrom=bytes=>decode(bytes).split(/\r\n?|\n/).filter(line=>line.trim()).map(JSON.parse);
const normalize=bytes=>{
    const text=decode(bytes).replace(/\r\n?/g,'\n').replace(/[ \t]+$/gm,'');
    return Buffer.from(text&&!text.endsWith('\n')?text+'\n':text);
};
function safeRelative(name){assert(!path.isAbsolute(name)&&!name.split(/[\\/]/).includes('..'),'Unsafe manifest/reference path: '+name);}
function checkRows(rows,metadata,labelPrefix){
    assert(metadata.state==='verified'&&metadata.runs.length===6&&rows.length===48,'Incomplete paired evidence: '+labelPrefix);
    const labels=new Set();
    for(const run of metadata.runs){
        assert(['before','after'].includes(run.side)&&[1,2,3].includes(run.round)&&run.label===`${labelPrefix}-${run.side}-r${run.round}`,'Unexpected paired label');
        assert(!labels.has(run.label)&&run.state==='complete'&&run.exit_code===0&&run.source_revision===metadata['source_'+run.side]&&run.executable.sha256===metadata[run.side].sha256,'Unexpected process identity');
        labels.add(run.label);
        const selected=rows.filter(row=>row.label===run.label);
        assert(selected.length===8&&new Set(selected.map(row=>`${row.characters}/${row.mode}`)).size===8,'Missing or duplicated process cases');
        assert(selected.every(row=>row.revision===run.source_revision&&row.model_sha256===metadata.fixture_sha256&&[1000,10000].includes(row.characters)&&['static','omitted_rest','shared_pose','independent_poses'].includes(row.mode)),'Unexpected row identity');
    }
    assert(rows.every(row=>labels.has(row.label)&&row.frames===100&&row.gpu_samples===100&&row.warmup===20&&row.repeat===1),'Unexpected samples');
    assert(rows.reduce((sum,row)=>sum+row.frames,0)===4800&&rows.reduce((sum,row)=>sum+row.gpu_samples,0)===4800,'Unexpected total samples');
}
function checkComparison(comparison){
    assert(comparison.exact_expected_adapter_change&&comparison.expected_adapter_replacements===1&&comparison.before.replaced_line_count===2&&comparison.after.replaced_line_count===2&&comparison.expected_after_normalized_sha256===comparison.after.normalized_sha256,'Unexpected generated adapter changes');
    assert(comparison.line_number_adjustments?.length===2&&comparison.line_number_adjustments.every(line=>line.delta===2&&line.after===line.before+2),'Unexpected generated #line shifts');
    assert(comparison.before.debug_line_numbers.join(',')==='2755,2813'&&comparison.after.debug_line_numbers.join(',')==='2757,2815','Unexpected exact generated #line numbers');
}
function verify(directory){
    const manifest=json(path.join(directory,'export-manifest.json'));
    for(const file of manifest.files){
        safeRelative(file.path);
        const bytes=fs.readFileSync(path.join(directory,file.path));
        assert(bytes.length===file.bytes&&hash(bytes)===file.sha256,'Export hash mismatch: '+file.path);
        assert(bytes.equals(normalize(bytes)),'Export text normalization mismatch: '+file.path);
    }
    const reference=json(path.join(directory,'baseline-reference.json'));
    for(const file of reference.committed_references){safeRelative(file.path);const bytes=fs.readFileSync(path.join(root,file.path));assert(bytes.length===file.bytes&&hash(bytes)===file.sha256,'Prior sealed baseline reference changed: '+file.path);}
    const metadata=json(path.join(directory,'metadata.json')),summary=json(path.join(directory,'summary.json'));
    const rows=rowsFrom(fs.readFileSync(path.join(directory,'paired-rows.jsonl')));
    checkRows(rows,metadata,'skinned-cpu5-final');
    assert(metadata.state==='verified'&&rows.length===48&&summary.cases.length===8,'Incomplete CPU5 evidence');
    assert(rows.every(row=>row.frames===100&&row.gpu_samples===100&&row.warmup===20&&row.repeat===1),'Unexpected samples');
    assert(rows.reduce((sum,row)=>sum+row.frames,0)===4800&&rows.reduce((sum,row)=>sum+row.gpu_samples,0)===4800,'Unexpected total samples');
    assert(summary.source_before===metadata.source_before&&summary.source_after===metadata.source_after&&summary.fixture_sha256===metadata.fixture_sha256,'Summary identities differ');
    assert(reference.source_revision===metadata.source_before&&reference.executable.sha256===metadata.before.sha256,'Baseline reference differs from measurement');
    assert(summary.capture.png_bytes_equal&&summary.capture.differing_pixels===0&&summary.capture1000.png_bytes_equal&&summary.capture1000.differing_pixels===0,'Independent captures differ');
    assert(summary.rest_captures?.length===2&&summary.rest_captures.every(capture=>capture.png_bytes_equal&&capture.differing_pixels===0),'Default/rest captures differ');
    const comparison=json(path.join(directory,'snapshot-after/generated-source-comparison.json'));
    assert(comparison.exact_expected_adapter_change&&comparison.expected_adapter_replacements===1&&comparison.before.replaced_line_count===2&&comparison.after.replaced_line_count===2&&comparison.expected_after_normalized_sha256===comparison.after.normalized_sha256,'Unexpected generated adapter changes');
    assert(comparison.line_number_adjustments?.length===2&&comparison.line_number_adjustments.every(line=>line.delta===2&&line.after===line.before+2),'Unexpected generated #line shifts');
    assert(comparison.before.debug_line_numbers.join(',')==='2755,2813'&&comparison.after.debug_line_numbers.join(',')==='2757,2815','Unexpected exact generated #line numbers');
    const initial=json(path.join(directory,'superseded/metadata.json')),initialSummary=json(path.join(directory,'superseded/summary.json'));
    checkRows(rowsFrom(fs.readFileSync(path.join(directory,'superseded/paired-rows.jsonl'))),initial,'skinned-cpu5');
    assert(metadata.source_before===baselineRevision&&initial.source_before===baselineRevision&&initial.source_after===initialRevision&&metadata.source_after!==initialRevision,'Unexpected candidate history');
    assert(initialSummary.source_before===initial.source_before&&initialSummary.source_after===initial.source_after&&initialSummary.cases.length===8,'Superseded summary identity differs');
    checkComparison(json(path.join(directory,'superseded/snapshot-after/generated-source-comparison.json')));
    const ledger=json(path.join(directory,'ledger-integrity.json'));
    assert(ledger.historical_rows===408&&ledger.superseded_rows===48&&ledger.final_rows===48&&ledger.total_rows===504&&ledger.historical.sha256===historicalLedgerHash&&ledger.input.sha256===inputLedgerHash&&ledger.raw.sha256===metadata.raw_ledger.sha256,'Unexpected ledger history');
    assert(manifest.files.every(file=>! /\.(exe|obj|o|lib|a|dll|pdb|png)$/i.test(file.path)),'Binary artifact included in text export');
    console.log(`Verified ${manifest.files.length} normalized text files, prior sealed baseline references, 48 final plus 48 superseded rows, 408→456→504 ledger identities and exact final independent/default captures.`);
}
const args=process.argv.slice(2);
if(!args.length||args[0]==='--help'){
    console.log('Usage: node export.mjs <verified-run-directory> [--output <directory>] [--normalize-ledger] [--dry-run]\n       node export.mjs --verify <export-directory>');process.exit(0);
}
if(args[0]==='--verify'){assert(args.length===2,'Expected one export directory');verify(path.resolve(args[1]));process.exit(0);}
const runDirectory=path.resolve(args.shift());
let output=path.join(root,'bench/results/skinned-cpu5'),dryRun=false,normalizeLedger=false;
while(args.length){const option=args.shift();if(option==='--dry-run')dryRun=true;else if(option==='--normalize-ledger')normalizeLedger=true;else if(option==='--output'){assert(args.length&&!args[0].startsWith('--'),'Missing output');output=path.resolve(args.shift());}else throw new Error('Unknown option: '+option);}
assert(!fs.existsSync(output),'Refusing to overwrite an existing export: '+output);
const metadata=json(path.join(runDirectory,'metadata.json'));
assert(metadata.state==='verified'&&metadata.runs.length===6&&metadata.expected_rows===48&&metadata.expected_cpu_frames===4800&&metadata.expected_gpu_samples===4800,'Export requires a complete verified CPU5 run');
assert(metadata.source_before===baselineRevision&&metadata.source_after!==initialRevision&&metadata.ledger_before_rows===456&&metadata.ledger_before.sha256===inputLedgerHash,'Export requires the final candidate and exact 456-row input');
const originals=new Map(),exports=new Map();
function remember(source,declared,category='measured'){
    source=path.resolve(source);const bytes=fs.readFileSync(source),identity={path:source,bytes:bytes.length,sha256:hash(bytes),category};
    if(declared)assert(identity.bytes===declared.bytes&&identity.sha256===declared.sha256,'Original artifact changed: '+source);
    if(originals.has(source))assert(originals.get(source).sha256===identity.sha256,'Original changed during export: '+source);
    originals.set(source,identity);return {bytes,identity};
}
function add(source,destination,declared,category){safeRelative(destination);assert(!exports.has(destination),'Duplicate export: '+destination);const {bytes,identity}=remember(source,declared,category);exports.set(destination,{bytes:normalize(bytes),source:identity});}
function generated(destination,value){assert(!exports.has(destination),'Duplicate export: '+destination);exports.set(destination,{bytes:normalize(Buffer.from(typeof value==='string'?value:JSON.stringify(value,null,2)+'\n')),source:null});}
for(const name of ['metadata.json','summary.json','paired-rows.jsonl'])add(path.join(runDirectory,name),name,name==='summary.json'?metadata.summary:name==='paired-rows.jsonl'?metadata.rows:undefined);
add(metadata.runner.path,'reproduce/run_pairs.ps1',metadata.runner,'measurement-script');
add(metadata.analyzer.path,'reproduce/analyze.mjs',metadata.analyzer,'measurement-script');
add(script,'reproduce/export.mjs',undefined,'export-script');
for(const name of ['run-final.ps1','build-release.ps1','smoke.ps1','preserve-after.ps1','reference-before.mjs','compare-generated.mjs','restore-input-ledger.mjs'])add(path.join(scripts,name),'reproduce/'+name,undefined,'orchestration-script');
add(path.join(root,'build/skinned-cpu5-final-validation/run-validation.ps1'),'reproduce/run-validation.ps1',undefined,'validation-script');
add(metadata.baseline_reference.path,'baseline-reference.json',metadata.baseline_reference,'hash-pinned-prior-baseline');
const baseline=json(metadata.baseline_reference.path);
assert(baseline.source_revision===metadata.source_before&&baseline.executable.sha256===metadata.before.sha256,'Baseline reference identity differs');
for(const reference of baseline.committed_references)remember(path.join(root,reference.path),reference,'prior-sealed-baseline-reference');
for(const side of ['before','after'])remember(metadata[side].path,metadata[side],'excluded-executable');
const before=json(metadata.before_snapshot.path),after=json(metadata.after_snapshot.path);
assert(before.source_revision===metadata.source_before&&after.source_revision===metadata.source_after,'Snapshot source identity differs');
assert(after.files.find(file=>file.path==='bench_skinned.exe')?.sha256===metadata.after.sha256,'After snapshot executable differs');
for(const [side,snapshot] of [['before',before],['after',after]])for(const file of snapshot.files)remember(path.join(path.dirname(metadata[side+'_snapshot'].path),file.path),file,'preserved-'+side);
remember(metadata.before_snapshot.path,metadata.before_snapshot,'prior-baseline-raw-metadata');
remember(metadata.before_source_hashes.path,metadata.before_source_hashes,'prior-baseline-raw-source-hashes');
add(metadata.after_snapshot.path,'snapshot-after/metadata.json',metadata.after_snapshot,'candidate-snapshot');
const afterDirectory=path.dirname(metadata.after_snapshot.path);
for(const name of ['generated-source-comparison.json','generated-adapter.diff'])add(path.join(afterDirectory,name),'snapshot-after/'+name,after.files.find(file=>file.path===name),'generated-adapter-proof');
const comparison=json(path.join(afterDirectory,'generated-source-comparison.json'));
checkComparison(comparison);
assert(comparison.before.original_path===root.replaceAll('\\','/')+'/build/skinned-cpu4-release/bench/tide/bench_skinned/game.c'&&comparison.after.original_path===root.replaceAll('\\','/')+'/build/skinned-cpu5-final-release/bench/tide/bench_skinned/game.c','Unexpected final generated debug paths');
const validationDirectory=path.join(root,'build/skinned-cpu5-final-validation');
const validationHashes=path.join(validationDirectory,'source-hashes.json');
assert(hash(fs.readFileSync(validationHashes))===metadata.after_source_hashes.sha256,'Snapshot and validation source hash manifests differ');
const validationSummary=json(path.join(validationDirectory,'summary.json'));
assert(validationSummary.state==='passed'&&validationSummary.frozenProductionAndTestSourcesUnchanged&&validationSummary.sourceCount===2895&&json(validationHashes).length===2895,'Final validation is incomplete or has an unexpected frozen file set');
add(validationHashes,'validation/source-hashes.json',metadata.after_source_hashes,'one-current-source-manifest');
generated('snapshot-after/source-manifest-reference.json',{path:'validation/source-hashes.json',sha256:hash(normalize(fs.readFileSync(validationHashes))),raw_snapshot_source_hashes:metadata.after_source_hashes,note:'One current complete source/test hash manifest is shared by candidate preservation and validation.'});
for(const name of ['summary.json','summary.md'])add(path.join(validationDirectory,name),'validation/'+name,undefined,'validation-summary');
if(fs.existsSync(path.join(validationDirectory,'notes-hashes.json')))add(path.join(validationDirectory,'notes-hashes.json'),'validation/notes-hashes.json',undefined,'separate-notes-manifest');
for(const name of ['skinned-tests.log','pack-tests.log','native-focused-ctest.log','web-focused-ctest.log','release-smoke.log','release-smoke.json','release-smoke-stderr.log'])add(path.join(validationDirectory,'final',name),'validation/'+name,undefined,'focused-validation-or-unsaved-smoke');
const releaseDirectory=path.join(scripts,'release-build'),release=json(path.join(releaseDirectory,'metadata.json'));
assert(release.source_revision===metadata.source_after&&release.executable.sha256===metadata.after.sha256&&release.sourceFrozen,'Postcommit release build differs');
for(const name of ['metadata.json','configure.log','build.log'])add(path.join(releaseDirectory,name),'validation/release-build/'+name,undefined,'postcommit-release-build');
if(fs.existsSync(path.join(scripts,'preflight')))for(const name of ['metadata.json','initial-comparison-guard.txt','powershell5-preconfigure-error.txt','generated-raw.diff'])if(fs.existsSync(path.join(scripts,'preflight',name)))add(path.join(scripts,'preflight',name),'preflight/'+name,undefined,'pre-measurement-orchestration-diagnostic');
const summary=json(path.join(runDirectory,'summary.json'));
for(const side of ['before','after']){
    remember(path.join(runDirectory,side+'.png'),{bytes:fs.statSync(path.join(runDirectory,side+'.png')).size,sha256:summary.capture[side+'_png_sha256']},'excluded-10k-independent-capture');
    remember(path.join(runDirectory,side+'-1000.png'),{bytes:fs.statSync(path.join(runDirectory,side+'-1000.png')).size,sha256:summary.capture1000[side+'_png_sha256']},'excluded-1k-independent-capture');
    for(const capture of summary.rest_captures)remember(path.join(runDirectory,side+'-rest-'+capture.characters+'.png'),{bytes:fs.statSync(path.join(runDirectory,side+'-rest-'+capture.characters+'.png')).size,sha256:capture[side+'_png_sha256']},'excluded-default-capture');
}
assert(metadata.capture_runs?.length===6,'Expected six unsaved capture-only processes');
for(const capture of metadata.capture_runs){assert(capture.exit_code===0&&capture.saved===false,'Failed or saved capture-only process');for(const kind of ['stdout','stderr'])add(capture[kind].path,'logs/'+path.basename(capture[kind].path),capture[kind],'capture-only-log');}
const clocks=[];
for(const run of metadata.runs){
    assert(run.state==='complete'&&run.exit_code===0&&run.source_revision===metadata['source_'+run.side]&&run.executable.sha256===metadata[run.side].sha256&&run.label===`skinned-cpu5-final-${run.side}-r${run.round}`,'Unexpected run identity');
    const artifacts=new Map(run.evidence.map(file=>[path.basename(file.path),file]));
    for(const suffix of ['gpu.csv','gpu-errors.txt','stdout.txt','stderr.txt']){
        const name=run.label+'-'+suffix,artifact=artifacts.get(name);assert(artifact,'Missing measured artifact: '+name);
        const destination=(suffix==='gpu.csv'?'clocks/':'logs/')+name;add(artifact.path,destination,artifact);
        if(suffix==='gpu-errors.txt'||suffix==='stderr.txt')assert(artifact.bytes===0,'Measured errors: '+name);
        if(suffix==='gpu.csv'){
            const lines=exports.get(destination).bytes.toString('utf8').trim().split('\n'),header=lines.shift().split(',').map(value=>value.trim());
            const samples=lines.filter(Boolean).map(line=>Object.fromEntries(line.split(',').map((value,index)=>[header[index],value.trim()])));
            assert(samples.length&&samples.every(sample=>sample.uuid===metadata.gpu_uuid),'Unexpected GPU clock identity');
            const range=column=>{const values=samples.map(sample=>Number(sample[column]));assert(values.every(Number.isFinite),'Invalid clock column');return {minimum:Math.min(...values),maximum:Math.max(...values)};};
            clocks.push({label:run.label,samples:samples.length,first_timestamp:samples[0].timestamp,last_timestamp:samples.at(-1).timestamp,pstate_samples:Object.fromEntries([...new Set(samples.map(sample=>sample.pstate))].sort().map(state=>[state,samples.filter(sample=>sample.pstate===state).length])),graphics_mhz:range('clocks.current.graphics [MHz]'),memory_mhz:range('clocks.current.memory [MHz]'),utilization_percent:range('utilization.gpu [%]'),power_watts:range('power.draw [W]')});
        }
    }
}
generated('clock-summary.json',{note:'250 ms observations cover whole processes, not individual cases or clock-normalized throughput. NVIDIA timestamps are local wall time; run metadata boundaries are UTC. No power settings changes.',runs:clocks});
const initialDirectory=path.join(root,'build/release/skinned-cpu5/final-cpu5');
const initial=json(path.join(initialDirectory,'metadata.json'));
assert(metadata.superseded_comparison?.source_after===initialRevision&&metadata.superseded_comparison.executable_sha256===initial.after.sha256&&metadata.superseded_comparison.rows===48,'Final metadata does not pin the initial adverse comparison');
const initialRows=rowsFrom(fs.readFileSync(initial.rows.path));
checkRows(initialRows,initial,'skinned-cpu5');
assert(initial.source_before===baselineRevision&&initial.source_after===initialRevision&&initial.before.sha256===metadata.before.sha256&&initial.fixture_sha256===metadata.fixture_sha256,'Initial adverse comparison identity differs');
assert(initial.before.sha256==='786d7c0a0f19d036d11240e0917223bcf8d383de299f8822a9f55307a0c514c1'&&initial.after.sha256==='1f471332b38237fb6886aefcd311002e9084e18471b7b46e48216e6b919df6dc','Initial executable identity differs');
for(const name of ['metadata.json','summary.json','paired-rows.jsonl'])add(path.join(initialDirectory,name),'superseded/'+name,name==='summary.json'?metadata.superseded_comparison.summary:name==='paired-rows.jsonl'?initial.rows:metadata.superseded_comparison.metadata,'superseded-adverse-measurement');
remember(initial.summary.path,initial.summary,'superseded-summary-pinned-by-initial-run');
add(initial.runner.path,'superseded/reproduce/run_pairs.ps1',initial.runner,'superseded-measurement-script');
add(initial.analyzer.path,'superseded/reproduce/analyze.mjs',initial.analyzer,'superseded-measurement-script');
for(const run of initial.runs)for(const suffix of ['gpu.csv','gpu-errors.txt','stdout.txt','stderr.txt']){
    const name=run.label+'-'+suffix,artifact=run.evidence.find(file=>path.basename(file.path)===name);
    assert(artifact,'Missing initial measured artifact: '+name);
    add(artifact.path,'superseded/'+(suffix==='gpu.csv'?'clocks/':'logs/')+name,artifact,'superseded-adverse-process-evidence');
    if(suffix==='gpu-errors.txt'||suffix==='stderr.txt')assert(artifact.bytes===0,'Initial measured errors: '+name);
}
assert(initial.capture_runs?.length===6,'Expected six initial capture-only processes');
for(const capture of initial.capture_runs){assert(capture.exit_code===0&&capture.saved===false,'Failed or saved initial capture-only process');for(const kind of ['stdout','stderr'])add(capture[kind].path,'superseded/logs/'+path.basename(capture[kind].path),capture[kind],'superseded-capture-only-log');}
const initialSummary=json(initial.summary.path);
assert(initialSummary.capture.png_bytes_equal&&initialSummary.capture.differing_pixels===0&&initialSummary.capture1000.png_bytes_equal&&initialSummary.capture1000.differing_pixels===0&&initialSummary.rest_captures.length===2&&initialSummary.rest_captures.every(capture=>capture.png_bytes_equal&&capture.differing_pixels===0),'Initial captures differ');
generated('superseded/capture-hash-evidence.json',{independent_10000:initialSummary.capture,independent_1000:initialSummary.capture1000,default_rest:initialSummary.rest_captures,capture_only_processes:initial.capture_runs,note:'Exact capture comparisons and original PNG/RGB hashes are retained. PNG binaries remain in scratch; capture-only timings do not enter paired aggregates.'});
const initialSnapshot=json(initial.after_snapshot.path),initialSnapshotDirectory=path.dirname(initial.after_snapshot.path);
assert(initialSnapshot.source_revision===initial.source_after&&initialSnapshot.files.find(file=>file.path==='bench_skinned.exe')?.sha256===initial.after.sha256,'Initial snapshot identity differs');
add(initial.after_snapshot.path,'superseded/snapshot-after/metadata.json',initial.after_snapshot,'superseded-snapshot-identity');
for(const name of ['generated-source-comparison.json','generated-adapter.diff'])add(path.join(initialSnapshotDirectory,name),'superseded/snapshot-after/'+name,initialSnapshot.files.find(file=>file.path===name),'superseded-generated-adapter-proof');
const initialComparison=json(path.join(initialSnapshotDirectory,'generated-source-comparison.json'));checkComparison(initialComparison);
assert(comparison.old_adapter_sha256===initialComparison.old_adapter_sha256&&comparison.new_adapter_sha256===initialComparison.new_adapter_sha256&&comparison.diff_sha256===hash(fs.readFileSync(path.join(afterDirectory,'generated-adapter.diff'))),'Final generated adapter contract differs from the reviewed initial adapter');
const initialManifest=remember(initial.after_source_hashes.path,initial.after_source_hashes,'referenced-superseded-source-manifest').identity;
const initialValidationDirectory=path.join(root,'build/skinned-cpu5-validation');
const initialValidationManifest=remember(path.join(initialValidationDirectory,'source-hashes.json'),initial.after_source_hashes,'referenced-superseded-validation-manifest').identity;
const initialValidation=json(path.join(initialValidationDirectory,'summary.json'));
assert(initialValidation.state==='passed'&&initialValidation.frozenProductionAndTestSourcesUnchanged&&initialValidation.releaseSourceRevision===initialRevision&&initialValidation.releaseExecutable.sha256===initial.after.sha256,'Initial validation provenance differs');
for(const name of ['summary.json','summary.md'])add(path.join(initialValidationDirectory,name),'superseded/validation/'+name,undefined,'superseded-validation-summary');
add(path.join(initialValidationDirectory,'run-validation.ps1'),'superseded/reproduce/run-validation.ps1',undefined,'superseded-validation-script');
const initialValidationLogs=initialValidation.checks.map(check=>remember(path.join(initialValidationDirectory,'final',check.log),undefined,'referenced-superseded-validation-log').identity);
generated('superseded/validation/identity-reference.json',{source_revision:initialRevision,executable:initial.after,snapshot_source_manifest:initialManifest,validation_source_manifest:initialValidationManifest,source_count:initialValidation.sourceCount,validation_logs:initialValidationLogs,note:'The initial full source manifest and full-suite logs remain hash-identified in scratch. The exported initial validation summaries retain commands, outcomes and chronology; the final candidate complete source manifest is exported once separately.'});
for(const name of ['metadata.json','configure.log','build.log'])add(path.join(root,'build/release/skinned-cpu5/release-build',name),'superseded/validation/release-build/'+name,undefined,'superseded-postcommit-release-build');
for(const name of ['metadata.json','initial-comparison-guard.txt','powershell5-preconfigure-error.txt','generated-raw.diff'])add(path.join(root,'build/release/skinned-cpu5/preflight',name),'superseded/preflight/'+name,undefined,'superseded-pre-measurement-orchestration-diagnostic');
const initialQuietObservation=path.join(root,'build/release/skinned-cpu5/preflight/quiet-guard-observation.json');
if(fs.existsSync(initialQuietObservation))add(initialQuietObservation,'superseded/preflight/quiet-guard-observation.json',undefined,'superseded-pre-measurement-quiet-guard-observation');
generated('superseded/README.md',`# Initial CPU5 comparison retained in full\n\nThese 48 saved rows compare CPU4 ${baselineRevision} with the initial CPU5 candidate ${initialRevision}. They are part of the retained ledger history. The independent-pose view interval regressed at both 1k and 10k in all three pairs; the exact measurements, summaries, raw process logs, clocks, executable identities and validation provenance remain explicit here. This is a completed adverse comparison, not an excluded attempt or a discarded noise sample.\n\nThe initial candidate changes runtime default/rest handling and exactly one compiler-generated default-pose adapter. Its original metadata and summary retain the contemporaneous phrase "Runtime-only"; that wording omits the adapter change. The exact generated-source comparison and adapter diff are retained under snapshot-after/.\n\nThe final comparison uses distinct skinned-cpu5-final labels and a later candidate with a load-time all-channel nonnegative-first-key flag plus sampler-specific proof. Exploratory timer and layout diagnostics are separate under ../diagnostics/ and do not replace these measured rows.\n`);
const diagnosticsDirectory=path.join(root,'build/release/skinned-cpu5/causal-review');
for(const experiment of ['matched-phases-v2','layout48-v4']){
    const directory=path.join(diagnosticsDirectory,experiment),diagnostic=json(path.join(directory,'metadata.json'));
    assert(diagnostic.state==='complete'&&diagnostic.ledger_before_sha256===inputLedgerHash&&diagnostic.ledger_after_sha256===inputLedgerHash,'Incomplete or ledger-changing diagnostic: '+experiment);
    const analysisHashes=experiment==='matched-phases-v2'?json(path.join(directory,'analysis-hashes.json')):[];
    const declared=name=>analysisHashes.find(file=>file.path===name)||diagnostic.files.find(file=>path.resolve(file.path)===path.join(directory,name));
    for(const name of experiment==='matched-phases-v2'?['metadata.json','summary.json','analysis-hashes.json']:['metadata.json','summary.json','layout-check.json','layout-check.log','coff-patch.json'])add(path.join(directory,name),'diagnostics/'+experiment+'/'+name,declared(name),'exploratory-diagnostic-summary');
    for(const file of diagnostic.files){
        const name=path.relative(directory,file.path).replaceAll('\\','/');safeRelative(name);
        if(!/\.(log|txt|csv)$/i.test(name))continue;
        const destination='diagnostics/'+experiment+'/'+name;
        if(!exports.has(destination))add(file.path,destination,file,'exploratory-diagnostic-log-or-clock');
    }
}
for(const name of ['run-layout.ps1','patch-skinned-prefix.mjs','instrument.mjs','check-layout.mjs','build-run.ps1','analyze-phases.mjs','analyze-layout.mjs'])add(path.join(diagnosticsDirectory,name),'diagnostics/reproduce/'+name,undefined,'exploratory-diagnostic-script');
generated('diagnostics/README.md',`# Exploratory causal diagnostics\n\nThese bounded, unsaved experiments investigated the initial adverse comparison; none appends benchmark rows or establishes a production performance win. matched-phases-v2 retains the timer instrumentation protocol, logs, clocks and analysis. Adding timers changed code layout and reversed the sampling ranking, so phase results are diagnostic only.\n\nlayout48-v4 retains a private control relink with exactly matching candidate PE .text, a +48-byte unreachable prefix variant, preserved instruction/call/unwind checks and exact capture hashes. The reported view interval changed from about 30.458 to 28.511 ms, while observed graphics clock ranges differed (control 1110–1245 MHz, prefix variant 1005–1170 MHz). Several functions move together, and one short pair with different clocks cannot establish layout as the sole cause or justify production padding.\n\nRaw artifact hashes are retained separately from normalized text hashes. Binary objects, libraries, executables, PDBs, maps and PNGs stay in scratch with identities in metadata. Only completed matched-phases-v2 and layout48-v4 evidence is included; the reproduction scripts are supplied as recorded diagnostic tools, not as steps in the final performance protocol.\n`);
const historical=remember(initial.ledger_before_copy.path,initial.ledger_before_copy,'historical-408-row-ledger');
const initialLedger=remember(initial.raw_ledger.path,initial.raw_ledger,'superseded-456-row-raw-ledger');
const inputLedger=remember(metadata.ledger_before_copy.path,metadata.ledger_before_copy,'final-input-456-row-ledger');
const rawLedger=remember(metadata.raw_ledger.path,metadata.raw_ledger,'final-504-row-raw-ledger');
const finalRows=rowsFrom(fs.readFileSync(metadata.rows.path));checkRows(finalRows,metadata,'skinned-cpu5-final');
assert(historical.identity.sha256===historicalLedgerHash&&rowsFrom(historical.bytes).length===408,'Original 408-row history differs');
assert(initialLedger.identity.sha256===inputLedgerHash&&inputLedger.identity.sha256===inputLedgerHash&&rowsFrom(inputLedger.bytes).length===456,'Final 456-row input differs from initial comparison');
assert(initialLedger.bytes.subarray(0,historical.bytes.length).equals(historical.bytes)&&rawLedger.bytes.subarray(0,inputLedger.bytes.length).equals(inputLedger.bytes),'Raw historical ledger prefix changed');
const sameRows=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
assert(sameRows(rowsFrom(initialLedger.bytes).slice(408),initialRows)&&sameRows(rowsFrom(rawLedger.bytes).slice(456),finalRows)&&rowsFrom(rawLedger.bytes).length===504,'Ledger append differs from the retained 48+48 rows');
assert(metadata.ledger_after.sha256===rawLedger.identity.sha256,'Final ledger identity differs');
generated('ledger-integrity.json',{historical_rows:408,superseded_rows:48,final_rows:48,total_rows:504,historical:historical.identity,input:inputLedger.identity,raw:rawLedger.identity,note:'Original 408-row raw prefix is byte-identical, the completed adverse comparison appends 48 rows to the exact 456-row input, and 48 distinct final-label rows append to 504. Exported JSONL uses normalized UTF-8/LF; raw byte identities remain separate.'});
let committedLedger;
if(normalizeLedger){
    const current=remember(metadata.ledger_after.path,metadata.ledger_after,'raw-ledger');
    assert(rawLedger.identity.sha256===current.identity.sha256,'Retained raw ledger differs');committedLedger=normalize(rawLedger.bytes);
    generated('ledger-normalization.json',{note:'The original 408-row raw history plus 48 superseded adverse rows form the exact 456-row input; 48 distinct final rows bring the ledger to 504. Raw original hashes are separate from normalized committed bytes.',path:'bench/results/skinned.jsonl',raw:rawLedger.identity,committed_lf:{bytes:committedLedger.length,sha256:hash(committedLedger)}});
}
generated('README.md',`# Final fifth skinned CPU comparison evidence

Before source: @BEFORE@; after source: @AFTER@. The baseline is the immutable CPU4 candidate; baseline-reference.json pins its prior committed metadata and complete source hashes. Those files remain in bench/results/skinned-cpu4 and are reused instead of copied. Candidate source/test hashes appear once in validation/source-hashes.json.

The initial CPU5 candidate 000a5986ba637eb9638f8b187b74736e95103378 produced a completed adverse comparison: independent-pose view time regressed at both counts in all three pairs. Its 48 rows, metadata, summary, process logs, clocks and lightweight snapshot/validation identities remain under superseded/. Those rows remain in the main ledger. This final candidate also computes an all-channel nonnegative-first-key flag after full load-time validation and exposes a sampler-specific proof query; only proven eligible models skip redundant selected-clip preflight. Negative-first-key, unproven and invalidated models retain the existing fallback. The separate diagnostics/ directory records exploratory timer and layout experiments with their limitations. No diagnostic timing is mixed into the primary final comparison.

Timers perturbed code layout and reversed the sampling ranking; the private +48-byte pair moved several functions and observed different GPU clocks. Neither diagnostic establishes a production win or a sole cause for the initial regression.

Six uninstrumented RelWithDebInfo (-O2) processes run three alternating before/after pairs, counts 1000/10000 and all four modes, 20 warmup and 100 measured frames, repeat 1. The 48 rows contain 4800 CPU frames and 4800 completed GPU samples. Summaries use the median of three process medians. Per-process medians/p95s and within-round ratios remain distinct from aggregate ratios; the median of process p95s is not a pooled-frame p95.

Each case has 20 warmup frames; timings exclude first-load validation and initial allocation/pipeline setup. Palette computation remains in CPU submission on both sides. View recording and submission are distinct; summing their separate medians is only a derived CPU estimate. GPU queries exclude upfront preparation/geometry/instance uploads, but include on-demand palette uploads and drawing. Readback-frame timing does not establish pipelined FPS.

The host, Tide game, generated header/pack and fixture remain byte-identical. Generated C changes by one exact reviewed default-pose adapter replacement, two known #line debug paths naming the isolated directory, and the corresponding two line numbers increasing by exactly two added adapter lines. Both raw generated hashes, the explicit replacement contract and exact normalized diff are retained. All other generated bytes must match. Expected draw/upload counts remain equal before/after. Fixture SHA256 is @FIXTURE@: 3072 vertices, 6048 triangles, 64 joints and four influences per character. Fixed summed projected area makes 10k characters tiny; no LOD, culling or match simulation is measured.

The named gate and 90-second sustained quiet guard reject detected competing builds/tests/benchmarks; other desktop applications remain running. GPU clocks sampled at 250 ms cover whole processes, not individual cases or clock-normalized throughput. No system settings changes were made. Excluded attempts, if any, appear in metadata.

Final 10k independent captures use frame 119 from both sides of the last pair. Six additional unsaved capture-only processes verify 1k independent and default/rest at 1k/10k, all using 100+20 frames and excluded from timing aggregates. PNG/RGB pixels must match exactly at each count and mode. Binaries and captures remain in scratch with hashes. The original 408-row historical prefix and 48 superseded rows form the exact 456-row input (SHA256 e60cf4d74b6256fc4a8a02ea44ba9936dbef00f02e1e96946d3290c1203bea44); 48 new skinned-cpu5-final-before/after-rN rows bring the ledger to 504. Raw prefix bytes are checked before text normalization. Original and normalized exported artifact hashes remain separate. BOM-marked PowerShell 5 UTF-16LE/BE logs are strictly decoded, then exported as UTF-8/LF alongside UTF-8 inputs.

Verify inside a checkout containing the hash-pinned prior CPU4 references:

    node bench/results/skinned-cpu5/reproduce/export.mjs --verify bench/results/skinned-cpu5

Reproduction requires PowerShell 7 and rebuilding both stated revisions with RelWithDebInfo (-O2) and the identical host/game/fixture. Binaries are not committed. Restore the scripts under build/release/skinned-cpu5-final and the validation runner under build/skinned-cpu5-final-validation; the isolated release tree is build/skinned-cpu5-final-release. Adapt checkout-specific tool/machine paths. Rebuild and preserve the baseline at build/release/skinned-cpu4/after-snapshot using the prior CPU4 reproduction instructions. Recorded binary hashes identify the original measurements; rebuilds can differ, so adapt expected hashes only after independently establishing source/build identities.

Restore the exact 456-row input ledger, retaining the initial adverse rows. Run node reproduce/restore-input-ledger.mjs <committed-ledger> <new-output> to extract and hash-check the first 456 rows from the eventual 504-row ledger; it writes only a new explicit output. Passing --check as the second argument checks without writing. Restore those checked bytes as the input ledger, then run reference-before.mjs against that restored input. For the frozen candidate, run run-validation.ps1 -StartApproved -RunName final, commit the tested bytes, then build-release.ps1 -AfterRevision <tested-commit> and smoke.ps1 -AfterRevision <tested-commit>. Invoke preserve-after.ps1 before run-final.ps1 -AfterRevision <tested-commit> -RunName <fresh-name>. The wrapper rechecks the full validated file set, source hashes and release hash and holds the shared gate. Export with export.mjs <verified-run-directory> --normalize-ledger. Direct run_pairs.ps1 is a lower-level fallback requiring equivalent validation and preserved identities, not a substitute for correctness checks. validation/notes-hashes.json, when present, records notes separately from the compiled/test source manifest.
`.replaceAll('@BEFORE@',metadata.source_before).replaceAll('@AFTER@',metadata.source_after).replaceAll('@FIXTURE@',metadata.fixture_sha256));
generated('original-artifacts.json',{meaning:'Raw original artifact identities before text normalization. Unexported binaries/captures/generated sources remain in scratch; prior sealed baseline artifacts are referenced, not duplicated.',source_before:metadata.source_before,source_after:metadata.source_after,artifacts:[...originals.values()].sort((a,b)=>a.path.localeCompare(b.path))});
const manifest={format:1,normalization:'Strictly decode BOM-marked UTF-16LE/BE or UTF-8 (optional BOM); emit UTF-8 without BOM. CRLF and CR become LF; strip trailing spaces/tabs from every line; append LF to a nonempty file lacking a final newline; empty files remain empty. Raw original bytes/hashes remain separately recorded.',source_before:metadata.source_before,source_after:metadata.source_after,superseded_source_after:initial.source_after,referenced_baseline_files:baseline.committed_references,files:[...exports.entries()].map(([name,file])=>({path:name,bytes:file.bytes.length,sha256:hash(file.bytes),original:file.source})).sort((a,b)=>a.path.localeCompare(b.path))};
if(dryRun){console.log(JSON.stringify({output,normalize_main_ledger:normalizeLedger,text_files:manifest.files.length+1,bytes:[...exports.values()].reduce((sum,file)=>sum+file.bytes.length,0),files:manifest.files.map(file=>file.path)},null,2));}
else{
    fs.mkdirSync(output,{recursive:true});
    for(const [name,file] of exports){const target=path.join(output,name);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,file.bytes,{flag:'wx'});}
    fs.writeFileSync(path.join(output,'export-manifest.json'),JSON.stringify(manifest,null,2)+'\n',{flag:'wx'});
    verify(output);
    if(committedLedger){assert(hash(fs.readFileSync(metadata.ledger_after.path))===metadata.ledger_after.sha256,'Ledger changed during export');fs.writeFileSync(metadata.ledger_after.path,committedLedger);assert(hash(fs.readFileSync(metadata.ledger_after.path))===hash(committedLedger),'Ledger normalization failed');}
    console.log('Exported CPU5 evidence: '+output);
}
