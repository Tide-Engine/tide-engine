import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const directory=path.dirname(fileURLToPath(import.meta.url));
const diagnostic=path.join(directory,'diagnostic');
const revision=JSON.parse(fs.readFileSync(path.join(directory,'metadata.json'),'utf8')).source_revision;
const privateHeaders=path.join(directory,'private-headers');
fs.mkdirSync(privateHeaders,{recursive:true});
const git=(...args)=>{const result=spawnSync('git',args);if(result.status)throw new Error(result.stderr.toString());return result.stdout;};
for(const file of git('ls-tree','-r','--name-only',revision,'--','platform/src').toString().trim().split(/\r?\n/).filter(x=>x.endsWith('.h'))){
    const relative=file.slice('platform/src/'.length);
    const destination=path.join(privateHeaders,relative);
    fs.mkdirSync(path.dirname(destination),{recursive:true});
    fs.writeFileSync(destination,git('show',`${revision}:${file}`));
}
let source=fs.readFileSync(path.join(directory,'sources/platform.c'),'utf8').replace(/\r\n/g,'\n');
const once=(text,before,after)=>{if(text.indexOf(before)<0||text.indexOf(before)!==text.lastIndexOf(before))throw new Error(`Expected unique anchor: ${before}`);return text.replace(before,after);};
const phases=['texture_for','made_texture','free_texture','texture_write','texture_write_part','upload_shaded','forget_textures','draw_shaded','list_expand','resource_prepare','render_steps','retire','platform_draw'];
const counts=['texture_for','cache_id_hit','cache_id_miss','cache_content_hit','cache_content_changed','made_texture','free_texture','texture_write','texture_write_part','draw_shaded','platform_draw'];
let helpers='static struct {\n';
for(const phase of phases)helpers+=`    uint64_t ${phase}_ns;\n`;
for(const count of counts)helpers+=`    uint64_t ${count}_calls;\n`;
helpers+='} tide_platform_diag;\nvoid tide_platform_diag_reset(void) { memset(&tide_platform_diag,0,sizeof tide_platform_diag); }\nvoid tide_platform_diag_report(int characters,int mode,int frames)\n{\n';
helpers+='    printf("platform_diagnostic {\\\"characters\\\":%d,\\\"mode\\\":%d,\\\"frames\\\":%d,\\\"calls\\\":{",characters,mode,frames);\n';
for(let i=0;i<counts.length;i++)helpers+=`    printf("${i?',':''}\\\"${counts[i]}\\\":%llu",(unsigned long long)tide_platform_diag.${counts[i]}_calls);\n`;
helpers+='    printf("},\\\"phases_ns\\\":{");\n';
for(let i=0;i<phases.length;i++)helpers+=`    printf("${i?',':''}\\\"${phases[i]}\\\":%llu",(unsigned long long)tide_platform_diag.${phases[i]}_ns);\n`;
helpers+='    printf("}}\\n");\n}\n\n';
source=once(source,'#include "window.h"','#include "window.h"\n\n'+helpers);
// Time full driver update entry points; these are nested in texture_for/draw_shaded/render_steps.
source=source.replaceAll('gpu->texture_write(', 'diag_texture_write(').replaceAll('gpu->texture_write_part(', 'diag_texture_write_part(');
source=once(source,'static const tide_gpu *gpu;',`static const tide_gpu *gpu;
static void diag_texture_write(const tide_gpu_id texture,const void *pixels)
{
    const uint64_t began=tide_time_now_ns();
    tide_platform_diag.texture_write_calls++;
    gpu->texture_write(texture,pixels);
    tide_platform_diag.texture_write_ns+=tide_time_now_ns()-began;
}
static void diag_texture_write_part(const tide_gpu_id texture,const void *pixels,const int x,const int y,const int width,const int height)
{
    const uint64_t began=tide_time_now_ns();
    tide_platform_diag.texture_write_part_calls++;
    gpu->texture_write_part(texture,pixels,x,y,width,height);
    tide_platform_diag.texture_write_part_ns+=tide_time_now_ns()-began;
}`);
function section(start,end,edit){const from=source.indexOf(start),to=source.indexOf(end,from);if(from<0||to<0)throw new Error(start);source=source.slice(0,from)+edit(source.slice(from,to))+source.slice(to);}
section('static tide_gpu_id made_texture(', 'static tide_gpu_id made_cube(',s=>{
    s=s.replace('{\n','{\n    const uint64_t began=tide_time_now_ns();\n    tide_platform_diag.made_texture_calls++;\n');
    return once(s,'    return id;','    tide_platform_diag.made_texture_ns+=tide_time_now_ns()-began;\n    return id;');
});
section('static void free_texture(', 'static tide_gpu_id made_target(',s=>{
    s=s.replace('{\n','{\n    const uint64_t began=tide_time_now_ns();\n    tide_platform_diag.free_texture_calls++;\n');
    return once(s,'    gpu->texture_free(id);','    gpu->texture_free(id);\n    tide_platform_diag.free_texture_ns+=tide_time_now_ns()-began;');
});
section('static gpu_texture *texture_for(', '// Lets go of the textures the frame',s=>{
    s=once(s,'    gpu_texture *g = find_gpu_texture(t->space, t->id);','    const uint64_t began=tide_time_now_ns();\n    tide_platform_diag.texture_for_calls++;\n    gpu_texture *g = find_gpu_texture(t->space, t->id);');
    s=once(s,'    if (!g) {','    if (g) tide_platform_diag.cache_id_hit_calls++;\n    else tide_platform_diag.cache_id_miss_calls++;\n    if (!g) {');
    s=once(s,'    if (shaped && g->version == t->version && g->epoch == t->epoch) return g;',`    if (shaped && g->version == t->version && g->epoch == t->epoch) {
        tide_platform_diag.cache_content_hit_calls++;
        tide_platform_diag.texture_for_ns+=tide_time_now_ns()-began;
        return g;
    }
    tide_platform_diag.cache_content_changed_calls++;`);
    return once(s,'    return g;\n}', '    tide_platform_diag.texture_for_ns+=tide_time_now_ns()-began;\n    return g;\n}');
});
for(const [start,end,phase,anchor] of [
    ['static bool upload_shaded(', '// This list\'s shapes','upload_shaded','    return any;'],
    ['static void forget_textures(', '// Whether a list\'s texture','forget_textures','    gpu_frame++;'],
    ['static void draw_shaded(', 'static void draw_mesh_3d(', 'draw_shaded','    gpu->draw_indexed(g->index_count, run->count);']
])section(start,end,s=>{
    s=s.replace('{\n',`{\n    const uint64_t began=tide_time_now_ns();\n${phase==='draw_shaded'?'    tide_platform_diag.draw_shaded_calls++;\n':''}`);
    return once(s,anchor,phase==='upload_shaded'?`    tide_platform_diag.${phase}_ns+=tide_time_now_ns()-began;\n${anchor}`:`${anchor}\n    tide_platform_diag.${phase}_ns+=tide_time_now_ns()-began;`);
});
section('static void draw_list(', '// What frames draw into:',s=>{
    s=once(s,'    const tide_gpu_id target = to.gpu;','    const uint64_t expand=tide_time_now_ns();\n    const tide_gpu_id target = to.gpu;');
    s=once(s,'    // What the steps draw from goes to the GPU first','    tide_platform_diag.list_expand_ns+=tide_time_now_ns()-expand;\n    const uint64_t prepare=tide_time_now_ns();\n    // What the steps draw from goes to the GPU first');
    s=once(s,'    if (timed && gpu->time_begin) gpu->time_begin();','    tide_platform_diag.resource_prepare_ns+=tide_time_now_ns()-prepare;\n    const uint64_t render=tide_time_now_ns();\n    if (timed && gpu->time_begin) gpu->time_begin();');
    s=once(s,'    if (timed && gpu->time_end) gpu->time_end();','    if (timed && gpu->time_end) gpu->time_end();\n    tide_platform_diag.render_steps_ns+=tide_time_now_ns()-render;\n    const uint64_t retire=tide_time_now_ns();');
    return once(s,'        forget_textures();\n    }','        forget_textures();\n    }\n    tide_platform_diag.retire_ns+=tide_time_now_ns()-retire;');
});
section('void tide_platform_draw(', 'void tide_platform_gpu_timing(',s=>{
    s=s.replace('{\n','{\n    const uint64_t began=tide_time_now_ns();\n    tide_platform_diag.platform_draw_calls++;\n');
    return once(s,'        draw_list(list, frame_destination(), false, gpu_timing);','        draw_list(list, frame_destination(), false, gpu_timing);\n        tide_platform_diag.platform_draw_ns+=tide_time_now_ns()-began;');
});
fs.writeFileSync(path.join(diagnostic,'platform.c'),source);
let host=fs.readFileSync(path.join(diagnostic,'main.c'),'utf8');
host=once(host,'void tide_skin_diag_reset(void);','void tide_skin_diag_reset(void);\nvoid tide_platform_diag_reset(void);\nvoid tide_platform_diag_report(int characters,int mode,int frames);');
host=once(host,'    tide_skin_diag_report(counts[count_at],mode,measured_frames);','    tide_skin_diag_report(counts[count_at],mode,measured_frames);\n    tide_platform_diag_report(counts[count_at],mode,measured_frames);');
host=once(host,'        tide_skin_diag_reset();','        tide_skin_diag_reset();\n        tide_platform_diag_reset();');
fs.writeFileSync(path.join(diagnostic,'main.c'),host);
