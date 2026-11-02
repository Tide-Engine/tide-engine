#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "tide/list.h"
extern const unsigned char tide_game_pack[];
extern const size_t tide_game_pack_size;
static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static void matrix(const tide_float4x4 m)
{
    const float f[16] = {m.c0.x,m.c0.y,m.c0.z,m.c0.w,m.c1.x,m.c1.y,m.c1.z,m.c1.w,
        m.c2.x,m.c2.y,m.c2.z,m.c2.w,m.c3.x,m.c3.y,m.c3.z,m.c3.w};
    putchar('[');
    for (int i=0;i<16;i++) { uint32_t b; memcpy(&b,&f[i],sizeof b); printf("%s\"%08" PRIx32 "\"",i?",":"",b); }
    putchar(']');
}
static void words(const void *bytes,const uint32_t count)
{
    putchar('[');
    for(uint32_t i=0;i<count;i++) { uint32_t b;memcpy(&b,(const unsigned char *)bytes+i*4,sizeof b);printf("%s\"%08" PRIx32 "\"",i?",":"",b); }
    putchar(']');
}
static void frame(void)
{
    tide_draw_reset(&draw);
    tide_draw_screen_size(&draw,tide_f2(128,64));
    tide_frame(&world,NULL,1,NULL,NULL,1,&local,&draw,&gui);
}
int main(void)
{
    if (!tide_assets_load(tide_game_pack,tide_game_pack_size)) return 1;
    tide_world_init(&world,1.0f/60.0f);
    world.GIFixture.scenario=2;
    world.GIFixture.roomExtents=tide_f3(24,12,32);
    tide_world_tick(&world);
    const uint64_t hash=tide_world_hash(&world);
    tide_local_init(&local);
    local.GIProbe.settings.maxProbes=512;
    local.GIProbe.settings.captureDistance=128;
    local.GIProbe.settings.updatesPerFrame=0;
    local.GIProbe.cameraPosition=tide_f3(0,0,-2);
    gui.width=128;gui.height=64;
    frame();
    GlobalIllumination_Environment *e=&local.GlobalIllumination_Environment;
    if (!e->ready || e->layout.total<=60) return 2;
    for (int i=0;i<e->layout.total;i++) {
        const bool *a=tide_list_at(e->active,i,sizeof(bool));
        bool *c=tide_list_at_mut(e->captured,i,sizeof(bool));
        bool *d=tide_list_at_mut(e->dirty,i,sizeof(bool));
        int32_t *last=tide_list_at_mut(e->lastCapture,i,sizeof(int32_t));
        if (!a||!c||!d||!last) return 3;
        *c=*a && i!=60; *d=*a && i==60; *last=e->frame;
    }
    e->initialized=e->activeProbes-1;e->dirtyProbes=1;e->cursor=60;
    local.GIProbe.settings.updatesPerFrame=1;
    frame();
    const tide_float3 *a=tide_list_at(e->anchors,60,sizeof(tide_float3));
    const tide_float3 *o=tide_list_at(e->offsets,60,sizeof(tide_float3));
    const int32_t *last=tide_list_at(e->lastCapture,60,sizeof(int32_t));
    if (!a||!o||!last||*last!=e->frame||e->updates!=1) return 4;
    const tide_float3 eye=tide_add_f3(*a,*o);
    printf("{\"probe\":60,\"eye\":[%.9g,%.9g,%.9g],\"capacity\":%d,\"probes\":%d,\"blocks\":%d,\"frame\":%d,\"updates\":%d,\"world_hash\":\"%016" PRIx64 "\"}\n",
        (double)eye.x,(double)eye.y,(double)eye.z,e->layout.total,e->activeProbes,e->activeBlocks,e->frame,e->updates,hash);
    const tide_render_target face=e->resources.face4;
    const tide_target_formats formats=tide_render_target_formats(face.id);
    uint32_t current_target=0;
    int found=0;
    for (uint32_t i=0;i<draw.count;i++) {
        const tide_draw_command *c=&draw.commands[i];
        if(c->kind==TIDE_DRAW_TARGET) current_target=c->target;
        if(!current_target||current_target>draw.target_count||draw.targets[current_target-1].id!=face.id)continue;
        if(c->kind==TIDE_DRAW_CAMERA_3D && c->camera.matrix<draw.matrix_count) {
            const tide_draw_target_data *t=&draw.targets[current_target-1];
            printf("{\"face\":4,\"command\":%u,\"target_index\":%u,\"logical\":[%.9g,%.9g],\"scale\":%.9g,\"window\":%.9g,\"formats\":[%d,%d,%d],\"samples\":%u,\"fit\":%u,\"mirrored\":%u,\"matrix_bits\":",
                i,current_target,(double)t->size.width,(double)t->size.height,(double)t->size.scale,(double)t->size.window,
                formats.count,formats.color[0],formats.color[1],tide_render_target_samples(face.id),c->camera.fit,c->camera.mirrored);
            matrix(draw.matrices[c->camera.matrix]);puts("}");found++;
        }
        if(c->kind==TIDE_DRAW_SHADED && c->shaded.run<draw.run_count){
            const tide_draw_run *run=&draw.runs[c->shaded.run];
            if(!run->shader||!run->mesh||run->mesh>draw.mesh_count)return 7;
            const tide_draw_mesh_data *mesh=&draw.meshes[run->mesh-1];
            const uint32_t stride=tide_shader_instance_size(run->shader);
            if((uint64_t)run->first+(uint64_t)run->count*stride>draw.instance_used)return 8;
            printf("{\"face4_run\":%u,\"command\":%u,\"count\":%u,\"stride\":%u,\"instance_floats\":%u,\"instance_ints\":%u,\"instance_words\":",
                c->shaded.run,i,run->count,stride,run->shader->instance_floats,run->shader->instance_ints);
            words(draw.instances+run->first,run->count*stride/4);
            printf(",\"model_m33\":[");
            for(uint32_t n=0;n<run->count;n++){float value=0;memcpy(&value,draw.instances+run->first+n*stride+60,sizeof value);printf("%s%.9g",n?",":"",(double)value);}
            printf("],\"vertex_stride\":%u,\"vertex_count\":%u,\"vertex_words\":",mesh->vertex_size,mesh->vertex_count);
            if(mesh->bytes)words(mesh->bytes,mesh->vertex_size*mesh->vertex_count/4);else printf("[]");
            printf(",\"indices\":[");for(uint32_t n=0;n<mesh->index_count;n++)printf("%s%u",n?",":"",mesh->indices[n]);
            printf("],\"attributes\":[");for(uint32_t n=0;n<run->shader->attribute_count;n++)printf("%s[%u,%u]",n?",":"",run->shader->attributes[n].format,run->shader->attributes[n].offset);
            puts("]}");
        }
    }
    const GlobalIllumination_WorldBounds *bounds=&local.GlobalIllumination_WorldBounds;
    for(int n=0;n<tide_list_read_count(&local.heap,bounds->instances);n++){
        const GlobalIllumination_SceneInstance *s=tide_list_read(&local.heap,bounds->instances,n,sizeof *s);
        if(!s)return 9;
        printf("{\"scene_instance\":%d,\"entity\":[%u,%u],\"model_bits\":",n,s->entity.index,s->entity.generation);matrix(s->transform);puts("}");
    }
    tide_draw_render_target(&draw,face);
    const uint32_t ci=draw.place.camera_3d;
    if(!ci||ci>draw.camera_3d_count)return 5;
    const tide_draw_camera_3d_data cam=draw.cameras_3d[ci-1];
    printf("{\"face\":4,\"camera_index\":%u,\"projection_bits\":",ci);matrix(cam.projection);
    printf(",\"transform_bits\":");matrix(cam.transform);
    printf(",\"world_to_clip_bits\":");matrix(tide_draw_world_to_clip(&draw,tide_f2(128,64)));puts("}");
    for(int i=0;i<4;i++){
        const tide_float2 pixel=tide_f2(10.5f+(float)(i%2),20.5f+(float)(i/2));
        const tide_float3 ray=tide_draw_screen_ray_direction(&draw,pixel,tide_f2(128,64));
        const double length=(-12.0-(double)eye.y)/(double)ray.y;
        printf("{\"pixel\":[%.9g,%.9g],\"ray\":[%.9g,%.9g,%.9g],\"floor_delta\":[%.9g,%.9g,%.9g]}\n",
            (double)pixel.x,(double)pixel.y,(double)ray.x,(double)ray.y,(double)ray.z,
            (double)ray.x*length,(double)ray.y*length,(double)ray.z*length);
    }
    const bool valid=found>0&&tide_world_hash(&world)==hash;
    tide_draw_forget(&draw);tide_draw_free(&draw);tide_draw_free(&gui.list);tide_local_free(&local);tide_world_free(&world);tide_render_targets_free();
    return valid?0:6;
}
