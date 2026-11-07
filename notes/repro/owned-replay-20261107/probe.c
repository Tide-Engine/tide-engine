#include <stdio.h>
#include <stdlib.h>
#include "game.h"
static unsigned near_calls, near_equal, singleton_diff;
static bool target_update;
static bool compare(const void *p, const void *q) {
    if(!target_update)return tide_game_api.near_same(p,q);
    near_calls++;
    const tide_world *a=p,*b=q;
    if (memcmp(&a->Observed,&b->Observed,sizeof a->Observed)) singleton_diff++;
    bool same=tide_game_api.near_same(p,q); near_equal+=same; return same;
}
static void sample(void *u,uint32_t tick,void *out) {
    *(Controls *)out=(Controls){.move=u ? (int32_t)(tick%2) : 0};
}
int main(int argc,char **argv) {
    (void)argv;
    tide_game game=tide_game_api; game.near_same=compare;
    tide_loopback *net=tide_loopback_create(42);
    tide_loopback_set_conditions(net,(tide_net_conditions){.latency=0.04});
    tide_server_desc sd={.game=&game,.tick_rate=60,.transports={tide_loopback_endpoint(net,1)}};
    tide_server *server=tide_server_create(&sd,0);
    tide_client_desc cd={.game=&game,.sample=sample,.lead=2,.transport=tide_loopback_endpoint(net,2),.server=tide_loopback_address(1)};
    tide_client *client=tide_client_create(&cd,0), *other=NULL;
    if(argc>1){cd.transport=tide_loopback_endpoint(net,3);cd.user=argc>2?NULL:(void *)1;other=tide_client_create(&cd,0);}
    for(int frame=1;frame<=720;frame++) {
        double now=frame/60.0; tide_loopback_set_time(net,now);
        target_update=true;tide_client_update(client,now);target_update=false; if(other)tide_client_update(other,now); tide_server_update(server,now);
        if(argc>2){tide_client_mark_presented(client);if(other)tide_client_mark_presented(other);}
    }
    tide_client_status s=tide_client_status_of(client);
    printf("ticks=%u predicted=%u replayed=%u kept=%u near_calls=%u near_equal=%u singleton_diff=%u verified=%u repairs=%u resyncs=%u\n",s.run_ticks,s.ahead_ticks,s.again_ticks,s.near_kept,near_calls,near_equal,singleton_diff,s.verified_tick,s.repairs,s.resyncs);
    int failed=s.repairs||s.resyncs||s.verified_tick<650;
    tide_client_destroy(client);if(other)tide_client_destroy(other);tide_server_destroy(server);tide_loopback_destroy(net);return failed;
}
