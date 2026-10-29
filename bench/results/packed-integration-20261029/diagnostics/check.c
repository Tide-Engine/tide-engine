#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wide.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static const tide_columns *cols = &tide_columns_arch1_Crate;
static uint64_t checks;
static uint64_t validation_hash;

static void equal(const tide_world *a, const tide_world *b)
{
    CHECK(a->arch1_Crate.count == b->arch1_Crate.count);
    const tide_table *x=&a->arch1_Crate, *y=&b->arch1_Crate;
    for (uint32_t r=0;r<x->count;r++) {
        CHECK(tide_table_logical_row(y,tide_table_physical_row(y,r))==r);
        for (uint32_t k=0;k<cols->count;k++) {
            CHECK(memcmp(tide_table_get(x,cols,r,k),tide_table_get(y,cols,r,k),cols->sizes[k])==0);
            checks++;
        }
    }
    CHECK(tide_world_hash(a)==tide_world_hash(b));
    validation_hash=validation_hash*UINT64_C(6364136223846793005)+tide_world_hash(a);
    const uint32_t n=tide_world_pack(a,NULL,0), m=tide_world_pack(b,NULL,0);
    CHECK(n==m);
    uint8_t *pa=malloc(n), *pb=malloc(n);
    CHECK(tide_world_pack(a,pa,n)==n && tide_world_pack(b,pb,n)==n);
    CHECK(memcmp(pa,pb,n)==0);
    tide_world round={0};
    CHECK(tide_world_unpack(&round,pb,n));
    CHECK(tide_world_hash(&round)==tide_world_hash(a));
    tide_world_free(&round);
    uint32_t delta_size=0;
    uint8_t *delta=tide_world_pack_delta(b,a,NULL,0,&delta_size);
    CHECK(tide_world_unpack_delta(&round,a,delta,delta_size));
    CHECK(tide_world_hash(&round)==tide_world_hash(a));
    tide_world_free(&round); free(delta);
    free(pa);free(pb);
}

static void init(tide_world *w, uint32_t n, uint32_t frozen)
{
    tide_world_init(w, 1.0f/60.0f);
    w->Yard.count=(int)n; w->Yard.frozen=(int)frozen; w->Yard.tagged=true;
    tide_world_tick(w);
}

static void validate(void)
{
    const uint32_t sizes[]={0,1,3,17,2047,2048,2049,4099};
    const uint32_t densities[]={0,50,95,99,100};
    for(size_t s=0;s<sizeof sizes/sizeof *sizes;s++) for(size_t d=0;d<sizeof densities/sizeof *densities;d++) {
        tide_world a={0},b={0}, kept={0}, base={0};
        init(&a,sizes[s],densities[d]); tide_world_copy(&b,&a);
        const uint32_t presence=cols->count-2u;
        uint32_t active=0;
        for(uint32_t r=0;r<a.arch1_Crate.count;r++) active+=*(const uint8_t *)tide_table_get(&a.arch1_Crate,cols,r,presence)==0;
        CHECK(tide_table_partition(&b.arch1_Crate,cols,presence,false)==active);
        equal(&a,&b);
        for(uint32_t tick=0;tick<12;tick++) {
            tide_world_copy(&kept,&b); tide_world_copy(&base,&a);
            tide_world_tick(&a); tide_world_tick(&b);
            equal(&a,&b); equal(&base,&kept);
            uint32_t delta_size=0;
            uint8_t *delta=tide_world_pack_delta(&b,&kept,NULL,0,&delta_size);
            tide_world received={0};
            CHECK(tide_world_unpack_delta(&received,&base,delta,delta_size));
            equal(&a,&received);
            tide_world_free(&received); free(delta);
            if(a.arch1_Crate.count) {
                const uint32_t r=(tick*257u)%a.arch1_Crate.count;
                const uint8_t old=*(const uint8_t *)tide_table_get(&a.arch1_Crate,cols,r,presence);
                *(uint8_t *)tide_table_cell(&a.arch1_Crate,cols,r,presence)=!old;
                *(uint8_t *)tide_table_cell(&b.arch1_Crate,cols,r,presence)=!old;
                tide_table_partition(&b.arch1_Crate,cols,presence,tick%2u!=0u);
                equal(&a,&b);
            }
        }
        // Grow a nonidentity table and move across two separately packed tables.
        tide_table moved_a={0}, moved_b={0};
        int32_t *map=malloc((size_t)cols->count*sizeof *map);
        for(uint32_t k=0;k<cols->count;k++) map[k]=(int32_t)k;
        for(uint32_t n=0;n<25u;n++) {
            const tide_entity ea=tide_entity_create(&a.entities), eb=tide_entity_create(&b.entities);
            CHECK(tide_entity_equal(ea,eb));
            const uint32_t ra=tide_table_add(&a.arch1_Crate,cols), rb=tide_table_add(&b.arch1_Crate,cols);
            CHECK(ra==rb);
            *(tide_entity *)tide_table_cell(&a.arch1_Crate,cols,ra,0u)=ea;
            *(tide_entity *)tide_table_cell(&b.arch1_Crate,cols,rb,0u)=eb;
            tide_entity_set_location(&a.entities,ea,(tide_location){1u,ra});
            tide_entity_set_location(&b.entities,eb,(tide_location){1u,rb});
            *(uint8_t *)tide_table_cell(&a.arch1_Crate,cols,ra,presence)=(uint8_t)(n%2u);
            *(uint8_t *)tide_table_cell(&b.arch1_Crate,cols,rb,presence)=(uint8_t)(n%2u);
        }
        equal(&a,&b);
        for(uint32_t n=0;n<17u;n++) {
            const uint32_t r=(n*31u)%a.arch1_Crate.count;
            tide_table_move(&a.arch1_Crate,cols,r,1u,&moved_a,cols,3u,map,&a.entities);
            tide_table_move(&b.arch1_Crate,cols,r,1u,&moved_b,cols,3u,map,&b.entities);
        }
        tide_table_partition(&moved_b,cols,presence,false);
        while(moved_a.count) {
            const uint32_t r=moved_a.count/2u;
            tide_table_move(&moved_a,cols,r,3u,&a.arch1_Crate,cols,1u,map,&a.entities);
            tide_table_move(&moved_b,cols,r,3u,&b.arch1_Crate,cols,1u,map,&b.entities);
        }
        tide_table_free(&moved_a,cols); tide_table_free(&moved_b,cols); free(map);
        equal(&a,&b);
        // Logical tail removal must preserve the existing entity location/order contract.
        while(a.arch1_Crate.count) {
            const uint32_t r=(a.arch1_Crate.count*17u+3u)%a.arch1_Crate.count;
            const tide_entity removed=*(const tide_entity *)tide_table_get(&a.arch1_Crate,cols,r,0u);
            tide_entity_destroy(&a.entities,removed); tide_entity_destroy(&b.entities,removed);
            tide_table_remove(&a.arch1_Crate,cols,r,&a.entities,1u);
            tide_table_remove(&b.arch1_Crate,cols,r,&b.entities,1u);
            if(a.arch1_Crate.count<20u || a.arch1_Crate.count%511u==0u) equal(&a,&b);
        }
        tide_world_free(&a); tide_world_free(&b); tide_world_free(&kept); tide_world_free(&base);
    }
    printf("PASS %llu exact column checks, logical hashes/raw packs/restore/COW/churn; digest=%llu\n",(unsigned long long)checks,(unsigned long long)validation_hash);
}

static double now(void) { return (double)tide_time_now_ns()/1000000.0; }
int main(int argc, char **argv)
{
    if(argc==1) { validate(); return 0; }
    const uint32_t n=(uint32_t)strtoul(argv[1],NULL,10);
    const uint32_t frozen=argc>2?(uint32_t)strtoul(argv[2],NULL,10):95u;
    const int packed=argc>3?atoi(argv[3]):0;
    const int hash=argc>4?atoi(argv[4]):0;
    const int snapshot=argc>5?atoi(argv[5]):0;
    tide_world w={0}, kept={0}; init(&w,n,frozen);
    if(packed) tide_table_partition(&w.arch1_Crate,cols,cols->count-2u,false);
    uint64_t digest=0;
    for(int i=0;i<100;i++) { if(snapshot) tide_world_copy(&kept,&w); tide_world_tick(&w); if(hash) digest^=tide_world_hash(&w); }
    double samples[200];
    for(int i=0;i<200;i++) { const double start=now();
        if(snapshot) tide_world_copy(&kept,&w);
        tide_world_tick(&w);
        if(hash) digest^=tide_world_hash(&w);
        samples[i]=now()-start;
    }
    printf("{\"count\":%u,\"frozen\":%u,\"packed\":%d,\"hash\":%d,\"snapshot\":%d,\"digest\":\"%llu\",\"final\":\"%llu\",\"samples_ms\":[",n,frozen,packed,hash,snapshot,(unsigned long long)digest,(unsigned long long)tide_world_hash(&w));
    for(int i=0;i<200;i++) printf("%s%.6f",i?",":"",samples[i]);
    puts("]}");
    tide_world_free(&w); tide_world_free(&kept); return 0;
}

