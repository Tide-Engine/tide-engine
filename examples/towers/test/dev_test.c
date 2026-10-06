// DEV ONLY, not part of the game's tests: how rounds go by themselves, over a few seeds.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "tide/list.h"
#include "tide/time.h"
#include "tide_test.h"

void b2t_forget(void);

static tide_world dev;

static Seat dev_seat(const tide_world *w, const int seat)
{
    return tide_get_Seat(w, *(const tide_entity *)tide_list_read(&w->heap, w->Towers.plinths, seat, sizeof(tide_entity)));
}

TIDE_TEST(dev_rounds)
{
    const char *seeds = getenv("TOWERS_SEEDS");
    const int count = seeds ? atoi(seeds) : 0;
    int all_lost = 0;
    int all_ticks = 0;
    int worst = 0;
    for (int seed = 0; seed <= count; seed++) {
        if (seed == count) {
            if (count > 0) printf("    over %d seeds: %.1f s a round, %.1f bricks lost a tower, %d the most\n", count, all_ticks / 60.0 / count, all_lost / 4.0 / count, worst);
            break;
        }
        b2t_forget();
        tide_world_free(&dev);
        tide_world_start_seeded(&dev, 1.0f / 60.0f, NULL, (uint32_t)seed * 7919u);
        int ticks = 0;
        int most = 0;
        int lost[4] = {0, 0, 0, 0};
        float half[4] = {0, 0, 0, 0};
        uint64_t spent = 0;
        while (ticks < 20000) {
            const uint64_t began = tide_time_now_ns();
            tide_world_tick(&dev);
            spent += tide_time_now_ns() - began;
            ticks++;
            if (dev.Towers.phase != Phase_Play && ticks > 10) break;
            int bodies = 0;
            for (uint32_t i = 0; i < dev.entities.next_unused; i++) {
                if (tide_has_Box2D_Body(&dev, tide_entity_in_slot(&dev.entities, i))) bodies++;
            }
            if (bodies > most) most = bodies;
            if (ticks == 3600) {
                for (int s = 0; s < 4; s++) half[s] = dev_seat(&dev, s).top;
            }
            if (getenv("TOWERS_DRIFT") && atoi(getenv("TOWERS_DRIFT")) == seed && ticks % 300 == 0) {
                // How far off the half-cell grid the bricks near each tower's top are, and how turned
                printf("      tick %d:", ticks);
                for (int s = 0; s < 4; s++) {
                    const Seat seat = dev_seat(&dev, s);
                    float sum = 0, most = 0, turned = 0;
                    int n = 0;
                    for (uint32_t i = 0; i < dev.entities.next_unused; i++) {
                        const tide_entity e = tide_entity_in_slot(&dev.entities, i);
                        if (!tide_has_Brick(&dev, e) || tide_get_Brick(&dev, e).down) continue;
                        const Box2D_Body b = tide_get_Box2D_Body(&dev, e);
                        if (b.position.x < seat.home - 4 || b.position.x > seat.home + 4 || b.position.y < seat.top - 4) continue;
                        const float cells = (b.position.x - seat.home) * 2.0f;
                        const float off = (cells - (float)(int)(cells + (cells < 0 ? -0.5f : 0.5f))) * 0.5f;
                        float a = b.angle * 0.636619772f; // In quarter turns
                        a = (a - (float)(int)(a + (a < 0 ? -0.5f : 0.5f))) * 90.0f;
                        sum += off;
                        if ((off < 0 ? -off : off) > most) most = off < 0 ? -off : off;
                        if ((a < 0 ? -a : a) > turned) turned = a < 0 ? -a : a;
                        n++;
                    }
                    printf("  [%4.1f m: off %+.2f, most %.2f, turned %4.1f]", (double)seat.top, n ? (double)(sum / (float)n) : 0.0, (double)most, (double)turned);
                }
                printf("\n");
            }
            if (getenv("TOWERS_TRACE") && atoi(getenv("TOWERS_TRACE")) == seed) {
                for (int s = 0; s < 4; s++) {
                    const int now = dev_seat(&dev, s).lost;
                    if (now > lost[s] && now <= 4) printf("      tick %d: seat %d lost its brick %d, at %.1f m\n", ticks, s, now, (double)dev_seat(&dev, s).top);
                    lost[s] = now;
                }
            }
        }
        for (int s = 0; s < 4; s++) {
            lost[s] = dev_seat(&dev, s).lost;
            all_lost += lost[s];
            if (lost[s] > worst) worst = lost[s];
        }
        all_ticks += ticks;
        printf("    seed %d: %5.1f s, winner %d, line %.1f; at 60 s: %4.1f %4.1f %4.1f %4.1f; lost %d %d %d %d; %d bodies, %d B physics, %.3f ms a tick\n",
               seed, ticks / 60.0, dev.Towers.winner, (double)dev.Towers.line, (double)half[0], (double)half[1], (double)half[2],
               (double)half[3], lost[0], lost[1], lost[2], lost[3], most, dev.Box2D_Physics.size, (double)spent / ticks / 1e6);
    }
    tide_world_free(&dev);
}
