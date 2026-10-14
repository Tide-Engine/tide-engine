import fs from 'node:fs';
import path from 'node:path';
const [snapshot,out]=process.argv.slice(2);
fs.mkdirSync(out,{recursive:true});
function once(s,a,b){if(s.indexOf(a)<0||s.indexOf(a)!==s.lastIndexOf(a))throw Error('Anchor not unique: '+a);return s.replace(a,b)}
let game=fs.readFileSync(path.join(snapshot,'generated/game.c'),'utf8').replace(/\r\n/g,'\n');
game=once(game,'// Determinism: never fuse a*b + c, whatever flags this file is built with.',`
#include "tide/time.h"
static uint64_t diag_sample_ns,diag_draw_ns,diag_calls;
void tide_game_diag_reset(void){diag_sample_ns=diag_draw_ns=diag_calls=0;}
void tide_game_diag_report(int count,int mode,int frames){printf("game_phase {\\"characters\\":%d,\\"mode\\":%d,\\"frames\\":%d,\\"calls\\":%llu,\\"sample_expression_ns\\":%llu,\\"draw_adapter_ns\\":%llu}\\n",count,mode,frames,(unsigned long long)diag_calls,(unsigned long long)diag_sample_ns,(unsigned long long)diag_draw_ns);}
// Determinism: never fuse a*b + c, whatever flags this file is built with.`);
const expression='                const tide_skinned_type3 pose = tide_skin_animation_sample';
game=once(game,expression,'                const uint64_t diag_sample_start=tide_time_now_ns();\n'+expression);
game=once(game,'                (void)pose;','                diag_sample_ns+=tide_time_now_ns()-diag_sample_start;\n                diag_calls++;\n                (void)pose;');
const draw='                tide_shade_1(tide_draw_render_target(tide_draw, (tide_render_target){.id = TIDE_RENDER_WINDOW}), tide_read5.vertices, tide_read5.indices, tide_read6, (tide_read5).data, (tide_read7).data, (tide_read7).transforms, false);';
game=once(game,draw,'                const uint64_t diag_draw_start=tide_time_now_ns();\n'+draw+'\n                diag_draw_ns+=tide_time_now_ns()-diag_draw_start;');
fs.writeFileSync(path.join(out,'game.c'),game);
let host=fs.readFileSync(path.join(snapshot,'fixture/main.c'),'utf8').replace(/\r\n/g,'\n');
host=once(host,'#include "tide/time.h"','#include "tide/time.h"\nvoid tide_game_diag_reset(void);\nvoid tide_game_diag_report(int,int,int);');
host=once(host,'    if (frame_at == warmup_frames) {','    if (frame_at == warmup_frames) {\n        tide_game_diag_reset();');
host=once(host,'    if (!label) return;','    tide_game_diag_report(counts[count_at],mode,measured_frames);\n    printf("host_diagnostic {\\"characters\\":%d,\\"mode\\":%d,\\"frames\\":%d,\\"gpu_samples\\":%d,\\"view_median_ms\\":%.9g,\\"view_p95_ms\\":%.9g,\\"submit_median_ms\\":%.9g,\\"gpu_median_ms\\":%.9g,\\"total_median_ms\\":%.9g}\\n",counts[count_at],mode,measured_frames,gpu_samples,values[RECORD].median,values[RECORD].p95,values[SUBMIT].median,values[GPU].median,values[TOTAL].median);\n    if (!label) return;');
fs.writeFileSync(path.join(out,'main.c'),host);
