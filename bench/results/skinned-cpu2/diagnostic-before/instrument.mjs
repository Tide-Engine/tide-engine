import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const directory = path.dirname(fileURLToPath(import.meta.url));
const diagnostic = path.join(directory,'diagnostic');
fs.mkdirSync(diagnostic,{recursive:true});
const once = (text,before,after) => {
    if (text.indexOf(before)<0 || text.indexOf(before)!==text.lastIndexOf(before)) throw new Error(`Expected unique anchor: ${before}`);
    return text.replace(before,after);
};
let source=fs.readFileSync(path.join(directory,'sources/skinned.c'),'utf8').replace(/\r\n/g,'\n');
fs.writeFileSync(path.join(diagnostic,'skinned-original.c'),source);
const phases=['sample_total','sample_checks','sample_fill_rest','sample_interpolation','rest_total','rest_checks','rest_fill','global_total','global_validation','global_compute','palette_total','palette_inverse_bind','draw_total','draw_checks','draw_palette','draw_palette_capture','draw_hash_influence'];
const functions=['sample','rest','global','palette','draw'];
let helpers='#include <stdio.h>\n#include "tide/time.h"\n\nstatic struct {\n';
for(const phase of phases) helpers+=`    uint64_t ${phase}_ns;\n`;
for(const fn of functions) helpers+=`    uint64_t ${fn}_calls, ${fn}_failures;\n`;
helpers+='} tide_skin_diag;\n\nvoid tide_skin_diag_reset(void) { memset(&tide_skin_diag,0,sizeof tide_skin_diag); }\n\nvoid tide_skin_diag_report(const int characters, const int mode, const int frames)\n{\n';
helpers+='    printf("phase_diagnostic {\\\"characters\\\":%d,\\\"mode\\\":%d,\\\"frames\\\":%d,\\\"calls\\\":{", characters, mode, frames);\n';
for(let i=0;i<functions.length;i++) {const fn=functions[i];helpers+=`    printf("${i?',':''}\\\"${fn}\\\":%llu", (unsigned long long)tide_skin_diag.${fn}_calls);\n`;}
helpers+='    printf("},\\\"failures\\\":{");\n';
for(let i=0;i<functions.length;i++) {const fn=functions[i];helpers+=`    printf("${i?',':''}\\\"${fn}\\\":%llu", (unsigned long long)tide_skin_diag.${fn}_failures);\n`;}
helpers+='    printf("},\\\"phases_ns\\\":{");\n';
for(let i=0;i<phases.length;i++) {const phase=phases[i];helpers+=`    printf("${i?',':''}\\\"${phase}\\\":%llu", (unsigned long long)tide_skin_diag.${phase}_ns);\n`;}
helpers+='    printf("}}\\n");\n}\n\n';
source=once(source,'_Static_assert(sizeof(tide_bone_transform)',helpers+'_Static_assert(sizeof(tide_bone_transform)');
function instrument(start,end,name,kind,edit,signature,argumentsText,returnType='bool') {
    const from=source.indexOf(start),to=source.indexOf(end,from);
    if(from<0||to<0) throw new Error(`Missing section ${name}`);
    let section=source.slice(from,to);
    section=once(section,`${start}`,`${start.replace(name,'diag_'+name).replace(/^bool /,'static bool ')}`);
    section=edit(section);
    let wrapper=`\n${signature}\n{\n    const uint64_t began=tide_time_now_ns();\n    tide_skin_diag.${kind}_calls++;\n    ${returnType} out=diag_${name}(${argumentsText});\n    tide_skin_diag.${kind}_total_ns+=tide_time_now_ns()-began;\n`;
    const failed=returnType==='bool'?'!out':returnType==='tide_list'?'tide_list_count(out)!=(int32_t)v->palettes':'out==NULL';
    wrapper+=`    tide_skin_diag.${kind}_failures+=${failed};\n    return out;\n}\n\n`;
    source=source.slice(0,from)+section+wrapper+source.slice(to);
}
instrument('bool tide_skinned_rest(', 'static bool channel_valid(', 'tide_skinned_rest','rest',section=>{
    section=once(section,'    if (!transforms) return false;','    const uint64_t checked=tide_time_now_ns();\n    if (!transforms) return false;');
    section=once(section,'    if (valid) fill_rest(&m.view, transforms, where);','    tide_skin_diag.rest_checks_ns+=tide_time_now_ns()-checked;\n    const uint64_t fill=tide_time_now_ns();\n    if (valid) fill_rest(&m.view, transforms, where);\n    tide_skin_diag.rest_fill_ns+=tide_time_now_ns()-fill;');
    return section;
},'bool tide_skinned_rest(const tide_list data, tide_list *transforms, const uint32_t where)','data,transforms,where');
instrument('bool tide_skinned_sample(', 'typedef struct named_node', 'tide_skinned_sample','sample',section=>{
    section=once(section,'    if (!transforms || animation < 0) return false;','    const uint64_t checked=tide_time_now_ns();\n    if (!transforms || animation < 0) return false;');
    section=once(section,'    if (valid) {\n        fill_rest(&m.view, transforms, where);','    tide_skin_diag.sample_checks_ns+=tide_time_now_ns()-checked;\n    if (valid) {\n        const uint64_t fill=tide_time_now_ns();\n        fill_rest(&m.view, transforms, where);\n        tide_skin_diag.sample_fill_rest_ns+=tide_time_now_ns()-fill;\n        const uint64_t interpolation=tide_time_now_ns();');
    section=once(section,'    }\n    close_model(&m);','        tide_skin_diag.sample_interpolation_ns+=tide_time_now_ns()-interpolation;\n    }\n    close_model(&m);');
    return section;
},'bool tide_skinned_sample(const tide_list data, const int32_t animation, const float seconds, tide_list *transforms, const uint32_t where)','data,animation,seconds,transforms,where');
instrument('static tide_float4x4 *global_matrices(', 'tide_float4x4 tide_skinned_model(', 'global_matrices','global',section=>{
    section=once(section,'    if (!nodes_valid(v) || tide_list_count(transforms) != (int32_t)v->nodes) return NULL;','    const uint64_t checked=tide_time_now_ns();\n    const bool valid=nodes_valid(v) && tide_list_count(transforms)==(int32_t)v->nodes;\n    tide_skin_diag.global_validation_ns+=tide_time_now_ns()-checked;\n    if (!valid) return NULL;\n    const uint64_t compute=tide_time_now_ns();');
    section=once(section,'    return global;','    tide_skin_diag.global_compute_ns+=tide_time_now_ns()-compute;\n    return global;');
    return section;
},'static tide_float4x4 *global_matrices(const tide_skinned_data_view *v, const tide_list transforms)','v,transforms','tide_float4x4 *');
instrument('static tide_list palette_of(', 'tide_list tide_skinned_palette(', 'palette_of','palette',section=>{
    section=once(section,'    tide_list out = {0};','    const uint64_t inverse=tide_time_now_ns();\n    tide_list out = {0};');
    section=once(section,'    return out;','    tide_skin_diag.palette_inverse_bind_ns+=tide_time_now_ns()-inverse;\n    return out;');
    return section;
},'static tide_list palette_of(const tide_skinned_data_view *v, const tide_list transforms)','v,transforms','tide_list');
// Last function: append a sentinel so the same section wrapper can instrument it.
source+='\n/* diagnostic end */\n';
instrument('bool tide_draw_skinned_data(', '/* diagnostic end */', 'tide_draw_skinned_data','draw',section=>{
    section=once(section,'    if (!textures || !info) return false;','    const uint64_t checked=tide_time_now_ns();\n    if (!textures || !info) return false;');
    section=once(section,'    tide_list palette = valid ? palette_of(&m.view, transforms) : (tide_list){0};','    tide_skin_diag.draw_checks_ns+=tide_time_now_ns()-checked;\n    const uint64_t palette_began=tide_time_now_ns();\n    tide_list palette = valid ? palette_of(&m.view, transforms) : (tide_list){0};\n    tide_skin_diag.draw_palette_ns+=tide_time_now_ns()-palette_began;');
    section=once(section,'    if (valid) {\n        textures[0]','    const uint64_t capture=tide_time_now_ns();\n    if (valid) {\n        textures[0]');
    section=once(section,'    if (valid) {\n        const int32_t width','    tide_skin_diag.draw_palette_capture_ns+=tide_time_now_ns()-capture;\n    const uint64_t influence=tide_time_now_ns();\n    if (valid) {\n        const int32_t width');
    section=once(section,'    tide_scratch_reset(mark);','    tide_skin_diag.draw_hash_influence_ns+=tide_time_now_ns()-influence;\n    tide_scratch_reset(mark);');
    return section;
},'bool tide_draw_skinned_data(tide_draw_list *draw, const tide_list data, const tide_list transforms, uint32_t textures[2], int32_t info[3])','draw,data,transforms,textures,info');
fs.writeFileSync(path.join(diagnostic,'skinned.c'),source);
let host=fs.readFileSync(path.join(directory,'fixture/main.c'),'utf8').replace(/\r\n/g,'\n');
host=once(host,'#include "tide/time.h"','#include "tide/time.h"\nvoid tide_skin_diag_reset(void);\nvoid tide_skin_diag_report(int characters,int mode,int frames);');
host=once(host,'    if (!label) return;','    tide_skin_diag_report(counts[count_at],mode,measured_frames);\n    printf("host_diagnostic {\\\"characters\\\":%d,\\\"mode\\\":%d,\\\"frames\\\":%d,\\\"gpu_samples\\\":%d,\\\"record_median_ms\\\":%.9g,\\\"record_p95_ms\\\":%.9g,\\\"submit_median_ms\\\":%.9g,\\\"gpu_median_ms\\\":%.9g,\\\"readback_median_ms\\\":%.9g,\\\"total_median_ms\\\":%.9g}\\n",counts[count_at],mode,measured_frames,gpu_samples,values[RECORD].median,values[RECORD].p95,values[SUBMIT].median,values[GPU].median,values[READBACK].median,values[TOTAL].median);\n    if (!label) return;');
host=once(host,'    if (frame_at == warmup_frames) {','    if (frame_at == warmup_frames) {\n        tide_skin_diag_reset();');
fs.writeFileSync(path.join(diagnostic,'main.c'),host);
