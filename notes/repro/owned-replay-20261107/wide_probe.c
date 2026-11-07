#include <stdio.h>
#include "game.h"
unsigned diag_begin;
static void counted_begin(tide_table *t,const tide_columns *c,uint32_t chunk,uint32_t first,uint32_t count,bool apart) {
    diag_begin++; tide_table_lanes_begin_pieces(t,c,chunk,first,count,apart);
}
#define tide_table_lanes_begin_pieces counted_begin
#ifdef BASELINE
#include "game_before.c"
#else
#include "game.c"
#endif
int main(void) {
    tide_world w={0};
    tide_world_init(&w,1.0f/60.0f);
    for(int i=0;i<200;i++) {
        tide_game_api.predict_far(&w,tide_player_from_index(0));
        tide_world_tick(&w);
    }
    printf("writable preparations=%u calls=%d hash=%llu\n",diag_begin,w.Counter.calls,(unsigned long long)tide_world_hash(&w));
    int failed=w.Counter.calls!=0;
    tide_world_free(&w);
    return failed;
}
