import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
const [beforeFile,afterFile,root,output]=process.argv.slice(2);
if(!output)throw new Error('Usage: compare-generated.mjs <before-game.c> <after-game.c> <checkout> <snapshot>');
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const assert=(ok,message)=>{if(!ok)throw new Error(message);};
const oldAdapter=`    const uint32_t tide_skin_mark = tide_scratch_mark();
    tide_list tide_skin_transforms = tide_pose_transforms;
    if (tide_default_pose) {
        if (!tide_skinned_rest(tide_model_data, &tide_skin_transforms, TIDE_IN_SCRATCH)) { tide_scratch_reset(tide_skin_mark); return; }
    } else if (!tide_skinned_compatible(tide_model_data, tide_pose_data)) {
        tide_skin_transforms = (tide_list){0};
        if (!tide_skinned_pose_into(tide_model_data, tide_pose_data, tide_pose_transforms, &tide_skin_transforms, TIDE_IN_SCRATCH)) { tide_scratch_reset(tide_skin_mark); return; }
    }
    if (!tide_draw_skinned_atlas(tide_draw, tide_model_data, tide_skin_transforms, tide_skin_textures, tide_skin_info)) { tide_scratch_reset(tide_skin_mark); return; }
`;
const newAdapter=`    const uint32_t tide_skin_mark = tide_scratch_mark();
    if (tide_default_pose) {
        if (!tide_draw_skinned_rest_atlas(tide_draw, tide_model_data, tide_skin_textures, tide_skin_info)) { tide_scratch_reset(tide_skin_mark); return; }
    } else {
        tide_list tide_skin_transforms = tide_pose_transforms;
        if (!tide_skinned_compatible(tide_model_data, tide_pose_data)) {
            tide_skin_transforms = (tide_list){0};
            if (!tide_skinned_pose_into(tide_model_data, tide_pose_data, tide_pose_transforms, &tide_skin_transforms, TIDE_IN_SCRATCH)) { tide_scratch_reset(tide_skin_mark); return; }
        }
        if (!tide_draw_skinned_atlas(tide_draw, tide_model_data, tide_skin_transforms, tide_skin_textures, tide_skin_info)) { tide_scratch_reset(tide_skin_mark); return; }
    }
`;
const proof={contract:'Only the exact reviewed default-pose adapter replacement and two known #line debug paths/numbers may differ. Each debug line number must increase by the exact number of lines added by that adapter replacement. Both raw generated C files remain preserved; expected code is compared ordinally, not broadly normalized.',before:{},after:{},expected_adapter_replacements:1,old_adapter_sha256:hash(oldAdapter),new_adapter_sha256:hash(newAdapter)};
let before,after;
for(const side of ['before','after']){
    const file=side==='before'?beforeFile:afterFile;
    const bytes=fs.readFileSync(file),text=bytes.toString('utf8');
    const originalPath=root.replaceAll('\\','/')+`/build/${side==='before'?'skinned-cpu4-release':'skinned-cpu5-final-release'}/bench/tide/bench_skinned/game.c`;
    const escaped=originalPath.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');
    const pattern=new RegExp('^(#line [0-9]+ )"'+escaped+'"$','gm');
    const matches=[...text.matchAll(pattern)];
    assert(matches.length===2,'Expected exactly two known #line debug paths: '+side);
    const normalized=text.replace(pattern,'$1"<generated-game-c>"');
    proof[side]={raw_sha256:hash(bytes),normalized_sha256:hash(normalized),replaced_line_count:2,original_path:originalPath,debug_line_numbers:matches.map(match=>Number(match[1].match(/[0-9]+/)[0]))};
    fs.writeFileSync(path.join(output,'generated',side+'-normalized.c'),normalized);
    if(side==='before')before=normalized;else after=normalized;
}
assert(before.split(oldAdapter).length===2,'Expected exactly one original default adapter');
assert(proof.before.debug_line_numbers.join(',')==='2755,2813'&&proof.after.debug_line_numbers.join(',')==='2757,2815','Expected exactly the reviewed original/new debug line numbers');
const lineDelta=newAdapter.split('\n').length-oldAdapter.split('\n').length;
assert(lineDelta===2,'Expected the reviewed adapter to add exactly two lines');
let expected=before.replace(oldAdapter,newAdapter);
proof.line_number_adjustments=proof.before.debug_line_numbers.map((number,index)=>{
    const adjusted=number+lineDelta;
    assert(proof.after.debug_line_numbers[index]===adjusted,'Unexpected generated debug line number');
    const original=`#line ${number} "<generated-game-c>"`;
    assert(expected.split(original).length===2,'Missing/duplicate known debug directive');
    expected=expected.replace(original,`#line ${adjusted} "<generated-game-c>"`);
    return {before:number,after:adjusted,delta:lineDelta};
});
assert(after===expected,'Generated C differs beyond the exact reviewed adapter replacement and two #line paths/numbers');
proof.expected_after_normalized_sha256=hash(expected);
proof.exact_expected_adapter_change=true;
const diff=spawnSync('git',['-c','core.autocrlf=false','diff','--no-index','--',path.join(output,'generated/before-normalized.c'),path.join(output,'generated/after-normalized.c')],{encoding:'utf8',windowsHide:true});
assert(diff.status===1 && !diff.stderr,'Could not capture the exact generated C adapter diff');
fs.writeFileSync(path.join(output,'generated-adapter.diff'),diff.stdout);
proof.diff_sha256=hash(diff.stdout);
fs.writeFileSync(path.join(output,'generated-source-comparison.json'),JSON.stringify(proof,null,2)+'\n');
console.log('Verified exactly one expected default adapter replacement and two corresponding #line paths/numbers.');
