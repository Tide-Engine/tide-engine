import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const dir=path.dirname(fileURLToPath(import.meta.url)),out=path.join(dir,'diagnostic');fs.mkdirSync(out,{recursive:true});
const once=(s,a,b)=>{if(!s.includes(a)||s.indexOf(a)!==s.lastIndexOf(a))throw Error('Expected unique anchor '+a);return s.replace(a,b)};
let source=fs.readFileSync(path.join(dir,'sources/skinned.c'),'utf8').replace(/\r\n/g,'\n');
const phases=['sample_total','sample_checks','sample_fill_rest','sample_interpolation','rest_total','rest_checks','rest_fill','rest_resize','rest_decode_write','compatible_total','duration_total','atlas_total','atlas_checks','atlas_pose_copy','atlas_model_compare','atlas_pose_hash','atlas_capture','atlas_influences','prepare_total'];
const kinds=['sample','rest','compatible','duration','atlas','prepare'];
let helper='#include <stdio.h>\n#include "tide/time.h"\nstatic struct {\n';
for(const p of phases)helper+=`    uint64_t ${p}_ns;\n`;
for(const p of kinds)helper+=`    uint64_t ${p}_calls;\n`;
helper+='} tide_skin_diag;\nvoid tide_skin_diag_reset(void){memset(&tide_skin_diag,0,sizeof tide_skin_diag);}\nvoid tide_skin_diag_report(int characters,int mode,int frames){\n    printf("phase_diagnostic {\\\"characters\\\":%d,\\\"mode\\\":%d,\\\"frames\\\":%d,\\\"calls\\\":{",characters,mode,frames);\n';
for(const[i,p]of kinds.entries())helper+=`    printf("${i?',':''}\\\"${p}\\\":%llu",(unsigned long long)tide_skin_diag.${p}_calls);\n`;
helper+='    printf("},\\\"phases_ns\\\":{");\n';
for(const[i,p]of phases.entries())helper+=`    printf("${i?',':''}\\\"${p}\\\":%llu",(unsigned long long)tide_skin_diag.${p}_ns);\n`;
helper+='    printf("}}\\n");\n}\n';
source=once(source,'_Static_assert(sizeof(tide_bone_transform)',helper+'\n_Static_assert(sizeof(tide_bone_transform)');
function section(start,end,edit){const a=source.indexOf(start),b=source.indexOf(end,a);if(a<0||b<0)throw Error('Missing section '+start);source=source.slice(0,a)+edit(source.slice(a,b))+source.slice(b)}
function wrap(start,end,name,kind,args,type='bool',edit=s=>s){section(start,end,s=>{const brace=s.indexOf('{'),signature=s.slice(0,brace).trim();s=once(s,name+'(',`diag_${name}(`);if(!s.startsWith('static '))s='static '+s;s=edit(s);const invoke=type==='void'?`diag_${name}(${args});`:`${type} result=diag_${name}(${args});`;return s+`\n${signature}\n{\n    const uint64_t began=tide_time_now_ns();\n    tide_skin_diag.${kind}_calls++;\n    ${invoke}\n    tide_skin_diag.${kind}_total_ns+=tide_time_now_ns()-began;\n${type==='void'?'':'    return result;\n'}}\n\n`})}
section('static void fill_rest(', 'bool tide_skinned_valid(',s=>{
 s=once(s,'    tide_list_resize(transforms,','    const uint64_t resize_began=tide_time_now_ns();\n    tide_list_resize(transforms,');
 s=once(s,'    tide_bone_transform *flat = scratch_transforms','    tide_skin_diag.rest_resize_ns+=tide_time_now_ns()-resize_began;\n    const uint64_t write_began=tide_time_now_ns();\n    tide_bone_transform *flat = scratch_transforms');
 const at=s.lastIndexOf('\n}');s=s.slice(0,at)+'\n    tide_skin_diag.rest_decode_write_ns+=tide_time_now_ns()-write_began;'+s.slice(at);return s;
});
wrap('bool tide_skinned_compatible(', 'int32_t tide_skinned_bone_count(', 'tide_skinned_compatible','compatible','a,b');
wrap('float tide_skinned_animation_duration(', 'bool tide_skinned_rest(', 'tide_skinned_animation_duration','duration','data,animation','float');
wrap('bool tide_skinned_rest(', 'static bool channel_valid(', 'tide_skinned_rest','rest','data,transforms,where','bool',s=>{
 s=once(s,'    if (!transforms) return false;','    const uint64_t checks=tide_time_now_ns();\n    if (!transforms) return false;');
 s=once(s,'    if (valid) fill_rest(&m.view, transforms, where);','    tide_skin_diag.rest_checks_ns+=tide_time_now_ns()-checks;\n    const uint64_t fill=tide_time_now_ns();\n    if (valid) fill_rest(&m.view, transforms, where);\n    tide_skin_diag.rest_fill_ns+=tide_time_now_ns()-fill;');return s;
});
wrap('bool tide_skinned_sample(', 'typedef struct named_node', 'tide_skinned_sample','sample','data,animation,seconds,transforms,where','bool',s=>{
 s=once(s,'    if (!transforms || animation < 0) return false;','    const uint64_t checks=tide_time_now_ns();\n    if (!transforms || animation < 0) return false;');
 s=once(s,'    if (valid) {\n        fill_rest(&m.view, transforms, where);','    tide_skin_diag.sample_checks_ns+=tide_time_now_ns()-checks;\n    if (valid) {\n        const uint64_t fill=tide_time_now_ns();\n        fill_rest(&m.view, transforms, where);\n        tide_skin_diag.sample_fill_rest_ns+=tide_time_now_ns()-fill;\n        const uint64_t interpolate=tide_time_now_ns();');
 s=once(s,'    }\n    close_model(&m);','        tide_skin_diag.sample_interpolation_ns+=tide_time_now_ns()-interpolate;\n    }\n    close_model(&m);');return s;
});
wrap('bool tide_draw_skinned_atlas(', 'typedef struct skin_prepare_work', 'tide_draw_skinned_atlas','atlas','draw,data,transforms,textures,info','bool',s=>{
 s=once(s,'    if (!textures || !info) return false;','    const uint64_t checks=tide_time_now_ns();\n    if (!textures || !info) return false;');
 s=once(s,'    tide_bone_transform *trs = valid ?','    tide_skin_diag.atlas_checks_ns+=tide_time_now_ns()-checks;\n    const uint64_t copy=tide_time_now_ns();\n    tide_bone_transform *trs = valid ?');
 s=once(s,'    valid = valid && trs != NULL;','    tide_skin_diag.atlas_pose_copy_ns+=tide_time_now_ns()-copy;\n    valid = valid && trs != NULL;');
 s=once(s,'        const uint32_t model = skin_cache_model','        const uint64_t model_began=tide_time_now_ns();\n        const uint32_t model = skin_cache_model');
 s=once(s,'        const size_t bytes =','        tide_skin_diag.atlas_model_compare_ns+=tide_time_now_ns()-model_began;\n        const uint64_t hash_began=tide_time_now_ns();\n        const size_t bytes =');
 s=once(s,'        const cached_skin_pose *pose = atlas_capture','        tide_skin_diag.atlas_pose_hash_ns+=tide_time_now_ns()-hash_began;\n        const uint64_t capture=tide_time_now_ns();\n        const cached_skin_pose *pose = atlas_capture');
 s=once(s,'        valid = pose != NULL;','        tide_skin_diag.atlas_capture_ns+=tide_time_now_ns()-capture;\n        valid = pose != NULL;');
 s=once(s,'            textures[1] = capture_influences','            const uint64_t influence=tide_time_now_ns();\n            textures[1] = capture_influences');
 s=once(s,'            info[0] =','            tide_skin_diag.atlas_influences_ns+=tide_time_now_ns()-influence;\n            info[0] =');return s;
});
source+='\n/* diagnostic end */\n';
wrap('void tide_draw_prepare(', '/* diagnostic end */', 'tide_draw_prepare','prepare','draw,jobs','void');
fs.writeFileSync(path.join(out,'skinned.c'),source);
let host=fs.readFileSync(path.join(dir,'fixture/main.c'),'utf8').replace(/\r\n/g,'\n');
host=once(host,'#include "tide/time.h"','#include "tide/time.h"\nvoid tide_skin_diag_reset(void);\nvoid tide_skin_diag_report(int characters,int mode,int frames);');
host=once(host,'    if (!label) return;','    tide_skin_diag_report(counts[count_at],mode,measured_frames);\n    printf("host_diagnostic {\\\"characters\\\":%d,\\\"mode\\\":%d,\\\"frames\\\":%d,\\\"gpu_samples\\\":%d,\\\"view_median_ms\\\":%.9g,\\\"view_p95_ms\\\":%.9g,\\\"submit_median_ms\\\":%.9g,\\\"gpu_median_ms\\\":%.9g,\\\"total_median_ms\\\":%.9g}\\n",counts[count_at],mode,measured_frames,gpu_samples,values[RECORD].median,values[RECORD].p95,values[SUBMIT].median,values[GPU].median,values[TOTAL].median);\n    if (!label) return;');
host=once(host,'    if (frame_at == warmup_frames) {','    if (frame_at == warmup_frames) {\n        tide_skin_diag_reset();');
fs.writeFileSync(path.join(out,'main.c'),host);
console.log('Instrumented immutable skinned source and host.');
