// The Box2D package under what a match does to a world: ticks, rollbacks,
// joins and threads (stack.tide).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide/net.h"
#include "tide/profile.h"
#include "tide/time.h"
#include "tide_test.h"

#ifdef BOX2D_TEST_JOBS
#include "tide/platform.h"
#endif

// bridge.c
void b2t_forget(void);
int32_t b2t_shared_steps(void);
void b2t_share_from(int32_t bodies);
int32_t b2t_restores(void);
void b2t_measure(uint64_t *step, uint64_t *image);
void b2t_measure_blocks(uint64_t *changed, uint64_t *in_all, uint64_t *took);

#define DT (1.0f / 60.0f)

// The match after 300 ticks, on every platform and in every configuration:
// Box2D's own math, SIMD or not, and its image, byte for byte.
#define SETTLED_HASH UINT64_C(0xf7a01bafd461192b)
#define IMAGES_HASH UINT64_C(0xd1e1a842cbe98ddc)

static tide_world a;
static tide_world b;
static tide_world kept;
static tide_world kept_too;

static void start(tide_world *w, const int crates)
{
    tide_world_init(w, DT);
    w->Size.crates = crates;
}

static void tick(tide_world *w, const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(w);
}

TIDE_TEST(box2d_steps_a_match)
{
    b2t_forget();
    start(&a, 12);
    // Every tick's image, not only the last: a byte that differs somewhere and
    // then doesn't (a field one platform's solver leaves behind) shows here
    uint64_t images = 0;
    for (int t = 0; t < 300; t++) {
        tide_world_tick(&a);
        const uint64_t hash = tide_world_hash(&a); // The image is in it
        images = tide_hash_more(images, &hash, sizeof hash);
    }
    int crates = 0;
    int resting = 0;
    for (uint32_t i = 0; i < a.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&a.entities, i);
        if (!tide_has_Crate(&a, e)) continue;
        const Box2D_Body body = tide_get_Box2D_Body(&a, e);
        TIDE_CHECK(body.id != 0);
        TIDE_CHECK(body.position.y > 0.0f && body.position.y < 20.0f); // On the floor or on each other, not through it
        crates++;
        if (body.velocity.x == 0.0f && body.velocity.y == 0.0f) resting++;
    }
    printf("    %d crates, %d at rest; %d touches began and %d ended; the image is %d bytes\n", crates, resting,
           (int)a.Tally.touched, (int)a.Tally.parted, (int)a.Box2D_Physics.size);
    TIDE_CHECK(crates == 21);      // 12 stacked, 8 thrown, 1 pushed: the doomed one went
    TIDE_CHECK(a.Box2D_Physics.bodies == 23 && a.Box2D_Physics.shapes == 23);
    TIDE_CHECK(a.Tally.touched > 20 && a.Tally.parted > 0);
    printf("    the world's hash is %016llx, and its 300 images' %016llx\n", (unsigned long long)tide_world_hash(&a),
           (unsigned long long)tide_hash_end(images));
    TIDE_CHECK(tide_world_hash(&a) == SETTLED_HASH);
    TIDE_CHECK(tide_hash_end(images) == IMAGES_HASH);
    tide_world_free(&a);
}

// A snapshot taken at any tick goes on as the world it was taken of did,
// whatever C's worlds have been through since.
TIDE_TEST(box2d_rolls_back)
{
    b2t_forget();
    start(&a, 12);
    tick(&a, 100);
    tide_world_copy(&kept, &a);
    uint64_t went[80];
    for (int i = 0; i < 80; i++) {
        tide_world_tick(&a);
        went[i] = tide_world_hash(&a);
    }
    // Three times over, as a client that predicted wrong does
    for (int round = 0; round < 3; round++) {
        tide_world_copy(&b, &kept);
        for (int i = 0; i < 80; i++) {
            tide_world_tick(&b);
            if (tide_world_hash(&b) != went[i]) printf("    round %d: tick %d came out different\n", round, i);
            TIDE_REQUIRE(tide_world_hash(&b) == went[i]);
        }
    }
    TIDE_CHECK(b2t_restores() >= 3);
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&kept);
}

// A world that's made from its image again before every step is, byte for
// byte, the world that never was: the image says everything, and a restore
// leaves nothing of its own in the next one.
TIDE_TEST(box2d_images_say_everything)
{
    enum { TICKS = 240 };
    static uint64_t went[TICKS];
    b2t_forget();
    start(&a, 12);
    for (int i = 0; i < TICKS; i++) {
        tide_world_tick(&a);
        went[i] = tide_world_hash(&a);
    }
    start(&b, 12);
    for (int i = 0; i < TICKS; i++) {
        const int32_t before = b2t_restores();
        b2t_forget();
        tide_world_tick(&b);
        TIDE_REQUIRE(i == 0 || b2t_restores() == before + 1);
        if (tide_world_hash(&b) != went[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(tide_world_hash(&b) == went[i]);
    }
    tide_world_free(&a);
    tide_world_free(&b);
}

// A player who joins gets the world as bytes, and has none of C's.
TIDE_TEST(box2d_joins)
{
    b2t_forget();
    start(&a, 12);
    tick(&a, 100);
    const uint32_t size = tide_world_pack(&a, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(tide_world_pack(&a, bytes, size) == size);
    tick(&a, 100);
    const uint64_t went = tide_world_hash(&a);

    b2t_forget();
    TIDE_REQUIRE(tide_world_unpack(&b, bytes, size));
    tick(&b, 100);
    TIDE_CHECK(tide_world_hash(&b) == went);
    printf("    the world is %u bytes packed, %d of them Box2D's image\n", (unsigned)size, (int)b.Box2D_Physics.size);
    free(bytes);
    tide_world_free(&a);
    tide_world_free(&b);
}

// What a tick costs, and where it goes, with everything moving, with a little
// moving, and at rest: nothing checked but that rest costs next to nothing,
// as times are the machine's.
static void cost(tide_world *w, const char *what, const int ticks)
{
    uint64_t step;
    uint64_t image;
    uint64_t changed;
    uint64_t in_all;
    uint64_t counting; // What counting the image's changes took inside the ticks: the measuring's, not a match's
    b2t_measure(&step, &image);
    b2t_measure_blocks(&changed, &in_all, &counting);
    // As a match does: each tick's world is kept (a snapshot, which shares what
    // the tick left alone) and hashed (what the tick changed)
    uint64_t took = 0;
    uint64_t kept_ns = 0;
    static tide_profile profile;
    const tide_jobs alone = {.threads = 1, .profile = &profile};
    tide_profile_clear(&profile);
    for (int i = 0; i < ticks; i++) {
        const uint64_t began = tide_time_now_ns();
        tide_world_tick_on(w, &alone);
        const uint64_t ticked = tide_time_now_ns();
        tide_world_copy(&kept, w);
        (void)tide_world_hash(w);
        took += ticked - began;
        kept_ns += tide_time_now_ns() - ticked;
    }
    b2t_measure(&step, &image);
    b2t_measure_blocks(&changed, &in_all, &counting);
    took -= counting;
    printf("    %-19s a tick takes %.3f ms: %.3f Box2D's step, %.3f its image, %.3f the rest; keeping and hashing it %.3f, "
           "with %.0f%% of the image changed\n",
           what, (double)took / (ticks * 1e6), (double)step / (ticks * 1e6), (double)image / (ticks * 1e6),
           (double)(took - step - image) / (ticks * 1e6), (double)kept_ns / (ticks * 1e6),
           in_all ? 100.0 * (double)changed / (double)in_all : 0.0);
    if (getenv("B2T_SYSTEMS")) {
        for (uint32_t i = 0; i < profile.systems.count; i++) {
            printf("        %-24s %.3f ms\n", profile.systems.items[i].name, profile.systems.items[i].total / ticks);
        }
        printf("        %-24s %.3f ms\n", "(changes and events)", profile.changes_ms / ticks);
    }
}

TIDE_TEST(box2d_costs)
{
    enum { CRATES = 2400 };
    b2t_forget();
    start(&a, CRATES);
    cost(&a, "everything moving:", 120);
    tick(&a, 600); // The stacks settle and sleep; one crate is still pushed around
    cost(&a, "one crate pushed:", 240);
    a.Size.calm = true;
    tick(&a, 600);
    const uint64_t stamp = (uint64_t)a.Box2D_Physics.stamp;
    cost(&a, "at rest:", 240);
    TIDE_CHECK((uint64_t)a.Box2D_Physics.stamp == stamp && !a.Box2D_Physics.moved); // Nothing stepped, and nothing was written
    printf("    %d crates; the image is %d bytes\n", CRATES, (int)a.Box2D_Physics.size);
    tide_world_free(&a);
    tide_world_free(&kept);
}

#ifdef BOX2D_TEST_JOBS
// A step shared out over the tick's threads leaves the world a step on one
// thread does.
TIDE_TEST(box2d_threads_change_nothing)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs) {
        printf("    one core: nothing to compare\n");
        return;
    }
    enum { CRATES = 2400, TICKS = 120 };
    b2t_forget();
    start(&a, CRATES);
    start(&b, CRATES);
    b2t_share_from(128); // Fewer than it takes for sharing to pay, so that the steps here are shared on every platform
    const int32_t before = b2t_shared_steps();
    uint64_t ticks[2] = {0, 0};
    uint64_t steps[2] = {0, 0};
    uint64_t images[2] = {0, 0};
    uint64_t step;
    uint64_t image;
    uint64_t changed;
    uint64_t in_all;
    uint64_t counting; // The measuring's own time inside a tick, taken off it
    b2t_measure(&step, &image);
    b2t_measure_blocks(&changed, &in_all, &counting);
    for (int i = 0; i < TICKS; i++) {
        for (int on = 0; on < 2; on++) {
            const uint64_t t = tide_time_now_ns();
            if (on) tide_world_tick_on(&b, jobs);
            else tide_world_tick(&a);
            ticks[on] += tide_time_now_ns() - t;
            // As a client keeps each tick's world: the next tick writes what it changes apart from it
            tide_world_copy(on ? &kept_too : &kept, on ? &b : &a);
            b2t_measure(&step, &image);
            b2t_measure_blocks(&changed, &in_all, &counting);
            ticks[on] -= counting;
            steps[on] += step;
            images[on] += image;
        }
        if (tide_world_hash(&a) != tide_world_hash(&b)) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(tide_world_hash(&a) == tide_world_hash(&b));
    }
    // Where a tick goes: Box2D's step, its state into an image, and the rest,
    // which is the package's systems (bodies in and out, the image into the match)
    for (int on = 0; on < 2; on++) {
        printf("    %d crates on %u thread%s: a tick takes %.2f ms: %.2f Box2D's step, %.2f its image, %.2f the rest\n", CRATES,
               on ? (unsigned)jobs->threads : 1u, on ? "s" : "", (double)ticks[on] / (TICKS * 1e6), (double)steps[on] / (TICKS * 1e6),
               (double)images[on] / (TICKS * 1e6), (double)(ticks[on] - steps[on] - images[on]) / (TICKS * 1e6));
    }
    printf("    %d steps were shared out; the image is %d bytes\n", (int)(b2t_shared_steps() - before), (int)a.Box2D_Physics.size);
    TIDE_CHECK(b2t_shared_steps() - before > TICKS / 2);
    b2t_share_from(0);

    // What a rollback pays once: C's world made from the match's image
    b2t_forget();
    b2t_measure(&step, &image);
    const uint64_t t = tide_time_now_ns();
    tide_world_tick(&a);
    b2t_measure(&step, &image);
    b2t_measure_blocks(&changed, &in_all, &counting);
    const uint64_t ticked = tide_time_now_ns() - t - counting;
    printf("    a tick that makes Box2D's world from the image first takes %.2f ms, %.2f more than one that has it\n",
           (double)ticked / 1e6, ((double)ticked - (double)ticks[0] / TICKS) / 1e6);
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&kept);
    tide_world_free(&kept_too);
}
#endif
