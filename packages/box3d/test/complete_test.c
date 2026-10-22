#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "complete.h"
#include "tide_test.h"

void b3t_check(bool on, bool whenever);
void b3t_forget(void);

_Static_assert(sizeof(Box3D_Physics) == 296 && sizeof(Box3D_Collider) == 192 &&
    sizeof(Box3D_HullGeometry) == 3592 && sizeof(Box3D_ShapeRecord) == 32 && sizeof(Box3D_Part) == 64,
    "live collider, history and native bookkeeping ABIs must agree");

static const Observed_local *probe(tide_world *w, tide_local *local)
{
    const uint64_t hash = tide_world_hash(w);
    tide_draw_list draw = {0}; tide_gui gui = {0};
    tide_frame(w,NULL,1,NULL,NULL,1,local,&draw,&gui);
    tide_draw_free(&draw);
    TIDE_CHECK(tide_world_hash(w) == hash);
    return &local->Observed_local;
}
static void start(tide_world *w)
{
    b3t_check(true,true);
    tide_world_init(w,1.0f/60.0f);
    for (int i=0;i<4;i++) tide_world_tick(w);
    w->Request.time = (ViewTime){.tick=2,.alpha=1,.valid=true};
}
TIDE_TEST(box3d_hulls_height_fields_filters_and_body_controls)
{
    tide_world w = {0}; tide_local local = {0}; start(&w);
    const Observed_local *o=probe(&w,&local);
    TIDE_CHECK(o->hull && !o->hullGap && o->hullCast && o->hullOverlap);
    TIDE_CHECK(o->terrain && !o->terrainGap && o->prism && o->masked && !o->rejected);
    TIDE_CHECK(o->prepared && o->historicalHull && o->historicalTerrain && o->all && !o->box);
    TIDE_CHECK(o->inside && o->historicalInside && o->mesh);
    TIDE_CHECK(fabsf(o->currentFraction-o->historicalFraction)<0.0001f);
    const Box3D_Body body=tide_get_Box3D_Body(&w,w.Objects.controlled);
    TIDE_CHECK(fabsf(body.position.y-3)<0.0001f && body.position.x>30 && body.velocity.x>0.9f);
    TIDE_CHECK(tide_get_Box3D_BodySettings(&w,w.Objects.controlled).force.x==0);
    tide_world_free(&w); tide_local_free(&local);
}

TIDE_TEST(box3d_live_lists_filters_parts_and_component_removal)
{
    tide_world w={0}, saved={0}; tide_local local={0}; start(&w);
    tide_world_copy(&saved,&w);
    Box3D_MeshCollider mesh=tide_get_Box3D_MeshCollider(&w,w.Objects.mesh);
    for(int i=0;i<4;i++) ((tide_float3 *)tide_list_at_mut(mesh.vertices,i,sizeof(tide_float3)))->y=1;
    Box3D_HeightFieldCollider terrain=tide_get_Box3D_HeightFieldCollider(&w,w.Objects.terrain);
    *(uint8_t *)tide_list_at_mut(terrain.holes,0,1)=0;
    Box3D_Part part=tide_get_Box3D_Part(&w,w.Objects.part);
    const int64_t old_shape=part.box;
    part.of=w.Objects.terrain;
    tide_set_Box3D_Part(&w,w.Objects.part,part);
    tide_world_tick(&w);
    const Observed_local *o=probe(&w,&local);
    TIDE_CHECK(o->terrainGap && o->mesh && fabsf(o->meshFraction-0.5f)<0.0001f);
    TIDE_CHECK(tide_get_Box3D_Part(&w,w.Objects.part).box!=old_shape);
    w.Request.removeSphere=w.Objects.sphere; w.Request.removeCapsule=w.Objects.capsule;
    w.Request.removeMesh=w.Objects.mesh; w.Request.removeHull=w.Objects.hull;
    w.Request.removeHeight=w.Objects.terrain; w.Request.removePart=w.Objects.part;
    w.Request.removeSettings=w.Objects.controlled; w.Request.removeFilter=w.Objects.filtered;
    tide_world_tick(&w); tide_world_tick(&w);
    o=probe(&w,&local);
    TIDE_CHECK(!o->hull && !o->terrain && !o->terrainGap && !o->mesh && !o->masked);
    TIDE_CHECK(o->historicalHull && o->historicalTerrain);
    for(int i=0;i<30;i++) tide_world_tick(&w);
    TIDE_CHECK(tide_get_Box3D_Body(&w,w.Objects.controlled).position.y<2.5f);
    tide_world_copy(&w,&saved);
    o=probe(&w,&local); TIDE_CHECK(o->hull && o->terrain && !o->terrainGap && o->masked);
    TIDE_CHECK(fabsf(o->meshFraction-0.75f)<0.0001f);
    tide_world_free(&saved); tide_world_free(&w); tide_local_free(&local);
}

TIDE_TEST(box3d_static_surface_removal_allows_a_dynamic_body_in_the_same_step)
{
    tide_world w={0}; start(&w);
    const tide_entity entities[]={w.Objects.mesh,w.Objects.terrain};
    w.Request.removeMesh=w.Objects.mesh; w.Request.removeHeight=w.Objects.terrain;
    tide_world_tick(&w); // Apply the language's deferred component removals.
    for(int i=0;i<2;i++) {
        Box3D_Body body=tide_get_Box3D_Body(&w,entities[i]);
        body.type=Box3D_BodyType_Dynamic;
        tide_set_Box3D_Body(&w,entities[i],body);
    }
    tide_world_tick(&w);
    for(int i=0;i<2;i++) {
        const Box3D_Body body=tide_get_Box3D_Body(&w,entities[i]);
        TIDE_CHECK(body.type==Box3D_BodyType_Dynamic);
    }
    tide_world_free(&w);
}

TIDE_TEST(box3d_collision_masks_and_groups_affect_physics)
{
    tide_world w={0}; start(&w);
    for(int i=0;i<120;i++) tide_world_tick(&w);
    TIDE_CHECK(fabsf(tide_get_Box3D_Body(&w,w.Objects.blocked).position.y-0.25f)<0.02f);
    TIDE_CHECK(fabsf(tide_get_Box3D_Body(&w,w.Objects.grouped).position.y-0.25f)<0.02f);
    TIDE_CHECK(tide_get_Box3D_Body(&w,w.Objects.passed).position.y<-10);
    TIDE_CHECK(tide_get_Box3D_Body(&w,w.Objects.ungrouped).position.y<-10);
    tide_world_free(&w);
}

TIDE_TEST(box3d_invalid_live_geometry_disables_and_can_be_repaired)
{
    for(int kind=0;kind<5;kind++) {
        tide_world w={0}; tide_local local={0}; start(&w);
        Box3D_HullCollider hull=tide_get_Box3D_HullCollider(&w,w.Objects.hull);
        if(kind==0) hull.scale.x=NAN;
        if(kind==1) hull.density=-1;
        if(kind==2) hull.offset.z=INFINITY;
        if(kind==3) hull.scale.y=0;
        if(kind==4) { tide_list_release(&hull.vertices,TIDE_IN_MATCH); hull.vertices=(tide_list){0}; }
        tide_set_Box3D_HullCollider(&w,w.Objects.hull,hull);
        tide_world_tick(&w);
        TIDE_CHECK(!probe(&w,&local)->hull);
        hull.scale=(tide_float3){1,1,1}; hull.offset=(tide_float3){0}; hull.density=1000;
        const tide_float3 points[]={{-1,0,-1},{1,0,-1},{0,0,1},{0,2,0}};
        const uint32_t mark=tide_scratch_mark();
        Box3D_HullCollider current=tide_get_Box3D_HullCollider(&w,w.Objects.hull);
        tide_list_release(&current.vertices,TIDE_IN_MATCH);
        hull.vertices=tide_list_from(points,4,sizeof *points);
        tide_list_own(&hull.vertices,sizeof *points,TIDE_IN_MATCH);
        tide_set_Box3D_HullCollider(&w,w.Objects.hull,hull);
        tide_scratch_reset(mark);
        tide_world_tick(&w);
        TIDE_CHECK(probe(&w,&local)->hull);
        tide_world_free(&w); tide_local_free(&local);
    }
}
TIDE_TEST(box3d_live_collider_edits_removal_and_retained_geometry)
{
    tide_world w={0}, saved={0}, decoded={0}; tide_local local={0}; start(&w);
    w.Request.capture=true;
    TIDE_CHECK(probe(&w,&local)->savedHull);
    w.Request.capture=false;
    tide_world_copy(&saved,&w);
    Box3D_Box box=tide_get_Box3D_Box(&w,w.Objects.box);
    box.size.x=3;
    tide_set_Box3D_Box(&w,w.Objects.box,box);
    Box3D_HullCollider hull=tide_get_Box3D_HullCollider(&w,w.Objects.hull);
    hull.scale.y=0.5f;
    tide_set_Box3D_HullCollider(&w,w.Objects.hull,hull);
    tide_world_tick(&w);
    const Observed_local *o=probe(&w,&local);
    TIDE_CHECK(o->box && o->hull && o->savedHull && o->historicalHull && o->historicalFraction<o->currentFraction);
    w.Request.removeBox=w.Objects.box;
    w.Request.removeBody=w.Objects.hull;
    tide_world_tick(&w); tide_world_tick(&w);
    o=probe(&w,&local);
    TIDE_CHECK(!o->hull && !o->box && o->savedHull && o->historicalHull);
    const uint32_t size=tide_world_pack(&w,NULL,0);
    uint8_t *bytes=malloc(size); TIDE_REQUIRE(bytes);
    TIDE_REQUIRE(tide_world_pack(&w,bytes,size)==size);
    b3t_forget(); TIDE_REQUIRE(tide_world_unpack(&decoded,bytes,size)); free(bytes);
    TIDE_CHECK(tide_world_hash(&decoded)==tide_world_hash(&w));
    tide_world_tick(&decoded); tide_world_tick(&w);
    TIDE_CHECK(tide_world_hash(&decoded)==tide_world_hash(&w));
    tide_world_copy(&w,&saved);
    TIDE_CHECK(probe(&w,&local)->hull && !probe(&w,&local)->box);
    tide_world_free(&decoded); tide_world_free(&saved); tide_world_free(&w); tide_local_free(&local);
}
TIDE_TEST(box3d_disabled_bodies_are_absent_from_live_and_detached_queries)
{
    tide_world w={0}; tide_local local={0}; start(&w);
    TIDE_CHECK(probe(&w,&local)->controlled && probe(&w,&local)->historicalControlled);
    Box3D_BodySettings settings=tide_get_Box3D_BodySettings(&w,w.Objects.controlled);
    settings.enabled=false; tide_set_Box3D_BodySettings(&w,w.Objects.controlled,settings);
    tide_world_tick(&w);
    TIDE_CHECK(!probe(&w,&local)->controlled && !probe(&w,&local)->historicalControlled);
    settings.enabled=true; tide_set_Box3D_BodySettings(&w,w.Objects.controlled,settings);
    tide_world_tick(&w);
    TIDE_CHECK(probe(&w,&local)->controlled && probe(&w,&local)->historicalControlled);
    Box3D_Body body=tide_get_Box3D_Body(&w,w.Objects.controlled);
    body.rewind=true; tide_set_Box3D_Body(&w,w.Objects.controlled,body);
    tide_world_tick(&w); w.Request.time=(ViewTime){.tick=7,.alpha=1,.valid=true};
    settings.enabled=false; tide_set_Box3D_BodySettings(&w,w.Objects.controlled,settings);
    tide_world_tick(&w);
    TIDE_CHECK(!probe(&w,&local)->controlled && probe(&w,&local)->historicalControlled);
    tide_world_free(&w); tide_local_free(&local);
}
TIDE_TEST(box3d_height_compression_rejects_overflowing_ranges)
{
    tide_world w={0}; tide_local local={0}; start(&w);
    Box3D_HeightFieldCollider terrain=tide_get_Box3D_HeightFieldCollider(&w,w.Objects.terrain);
    terrain.minimumHeight=-3e38f; terrain.maximumHeight=3e38f; terrain.scale.y=1e-33f;
    tide_set_Box3D_HeightFieldCollider(&w,w.Objects.terrain,terrain);
    tide_world_tick(&w);
    TIDE_CHECK(!probe(&w,&local)->terrain);
    terrain.minimumHeight=-1; terrain.maximumHeight=1; terrain.scale.y=1;
    tide_set_Box3D_HeightFieldCollider(&w,w.Objects.terrain,terrain);
    tide_world_tick(&w);
    TIDE_CHECK(probe(&w,&local)->terrain);
    tide_world_free(&w); tide_local_free(&local);
}
TIDE_TEST(box3d_complete_snapshot_replay)
{
    tide_world w={0}, saved={0}; start(&w); tide_world_copy(&saved,&w);
    uint64_t sequence=0;
    for (int i=0;i<100;i++) { tide_world_tick(&w); const uint64_t h=tide_world_hash(&w); sequence=tide_hash_more(sequence,&h,sizeof h); }
    TIDE_CHECK(sequence==UINT64_C(0x865b46ee1c28e494));
    printf("  complete sequence: %016llx\n",(unsigned long long)sequence);
    for (int i=0;i<100;i++) tide_world_tick(&saved);
    TIDE_CHECK(tide_world_hash(&saved)==tide_world_hash(&w));
    tide_world_free(&saved); tide_world_free(&w);
}
