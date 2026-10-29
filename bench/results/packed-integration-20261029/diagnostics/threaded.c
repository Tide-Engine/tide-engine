#define main probe_benchmark_main
#include "check.c"
#undef main
#include <stdatomic.h>

__declspec(dllimport) void *__stdcall CreateThread(void *, size_t, unsigned long (__stdcall *)(void *), void *, unsigned long, unsigned long *);
__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void *, unsigned long);
__declspec(dllimport) int __stdcall CloseHandle(void *);
typedef struct task { void (*work)(void *,uint32_t); void *context; uint32_t id; } task;
static _Atomic uint32_t entered;
static unsigned long __stdcall worker(void *arg)
{
    task *t=arg;
    atomic_fetch_add(&entered,1u);
    t->work(t->context,t->id);
    return 0;
}
static void pool(void *self,void (*work)(void *,uint32_t),void *context)
{
    (void)self;
    task tasks[3]; void *threads[3];
    for(uint32_t i=0;i<3u;i++) {
        tasks[i]=(task){work,context,i+1u};
        threads[i]=CreateThread(NULL,0u,worker,&tasks[i],0u,NULL);
        CHECK(threads[i]);
    }
    work(context,0u);
    for(uint32_t i=0;i<3u;i++) { CHECK(WaitForSingleObject(threads[i],UINT32_MAX)==0u); CHECK(CloseHandle(threads[i])); }
}
int main(void)
{
    const tide_jobs jobs={.threads=4u,.run=pool};
    tide_world a={0},b={0},kept={0};
    init(&a,1000003u,95u); tide_world_copy(&b,&a);
    tide_table_partition(&b.arch1_Crate,cols,cols->count-2u,false);
    for(uint32_t tick=0;tick<12u;tick++) {
        tide_world_copy(&kept,&b);
        tide_world_tick(&a); tide_world_tick_on(&b,&jobs);
        equal(&a,&b);
        const uint32_t row=(tick*97u)%a.arch1_Crate.count;
        const uint8_t old=*(const uint8_t *)tide_table_get(&a.arch1_Crate,cols,row,cols->count-2u);
        *(uint8_t *)tide_table_cell(&a.arch1_Crate,cols,row,cols->count-2u)=!old;
        *(uint8_t *)tide_table_cell(&b.arch1_Crate,cols,row,cols->count-2u)=!old;
        tide_table_partition(&b.arch1_Crate,cols,cols->count-2u,false);
    }
    CHECK(atomic_load(&entered)>0u);
    printf("PASS %llu column comparisons; %u native helper entries; digest=%llu\n",(unsigned long long)checks,atomic_load(&entered),(unsigned long long)validation_hash);
    tide_world_free(&a); tide_world_free(&b); tide_world_free(&kept);
    return 0;
}

