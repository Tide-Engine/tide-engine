// The Box3D package under what a match does to a world: ticks, rollbacks,
// joins and threads (stack.tide).

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "tide/jobs.h"
#include "tide/net.h"
#include "tide/profile.h"
#include "tide/time.h"
#include "tide/resident.h"
#include "tide_test.h"

// What the match holds of Box3D's state: the pages its resident keeps (tide/resident.h), in bytes
static int physics_bytes(const tide_world *w)
{
    return w->Box3D_Physics.state.pages ? (int)w->Box3D_Physics.state.pages->count * TIDE_ARENA_PAGE : 0;
}


#ifdef BOX3D_TEST_JOBS
#include "tide/platform.h"
#endif

// bridge.c
void b3t_forget(void);
void b3t_narrow(bool on);
void b3t_check(bool on, bool whenever);
void b3t_images(int32_t *whole, int32_t *in_parts);
uint64_t b3t_image_hash(void); // Of the resident digests committed so far
int32_t b3t_shared_steps(void);
int32_t b3t_restores(void);
void b3t_measure(uint64_t *step, uint64_t *image);
void b3t_measure_blocks(uint64_t *changed, uint64_t *in_all, uint64_t *took);

#define DT (1.0f / 60.0f)

// The match after 300 ticks, on every platform and in every configuration:
// Box3D's own math, SIMD or not, and its resident stamp and digest.
#define SETTLED_HASH UINT64_C(0xfa23a8f4eba064ff)
#define IMAGES_HASH UINT64_C(0x80a9e27b24968326)
// Box3D's own digest at each of the 300 ticks, apart from everything else the
// match holds. A change to what the match keeps
// beside it (a field of Physics, a component of the package's) moves the two
// above and leaves this one.
#define OWN_HASH UINT64_C(0x2c0f0b8554356083)
// Where the crates are and how fast, at each of the 300 ticks: the simulation
// alone. A change to the resident stamp or digest moves the three above and
// leaves this one; a change to how the world steps moves all four.
#define BODIES_HASH UINT64_C(0xace1f5d330399dd5)

static tide_world a;
static tide_world b;
static tide_world kept;
#ifdef BOX3D_TEST_JOBS
static tide_world kept_too;
#endif

static void start(tide_world *w, const int crates)
{
    tide_world_init(w, DT);
    w->Size.crates = crates;
}

static void tick(tide_world *w, const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(w);
}

// How many resident commits used whole or sparse marks since `*whole` and
// `*in_parts` were last read, which they are again.
static void images_since(int32_t *whole, int32_t *in_parts)
{
    int32_t now_whole;
    int32_t now_in_parts;
    b3t_images(&now_whole, &now_in_parts);
    const int32_t were_whole = *whole;
    const int32_t were_in_parts = *in_parts;
    *whole = now_whole - were_whole;
    *in_parts = now_in_parts - were_in_parts;
}

// Where each crate is and how fast, as its component says, into `hash`.
static uint64_t places(const tide_world *w, uint64_t hash)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (!tide_has_Crate(w, e)) continue;
        const Box3D_Body body = tide_get_Box3D_Body(w, e);
        const float place[13] = {body.position.x,        body.position.y,        body.position.z,        body.rotation.value.x, body.rotation.value.y,
                                 body.rotation.value.z,  body.rotation.value.w,  body.velocity.x,        body.velocity.y,       body.velocity.z,
                                 body.angularVelocity.x, body.angularVelocity.y, body.angularVelocity.z};
        hash = tide_hash_more(hash, place, sizeof place);
    }
    return hash;
}

TIDE_TEST(box3d_steps_a_match)
{
    for (int narrow = 0; narrow < 2; narrow++) {
        int32_t whole = 0;
        int32_t in_parts = 0;
        images_since(&whole, &in_parts);
        b3t_forget();
        b3t_narrow(narrow);
        // Every committed resident page is compared with the live arena,
        // here and in every test but the timed ones. Once with sparse marks
        // whenever possible, and once as a match uses them: whole while most
        // of the world is awake.
        b3t_check(true, narrow == 0);
        start(&a, 12);
        uint64_t images = 0;
        uint64_t bodies = 0;
        uint64_t bytes = 0;
        for (int t = 0; t < 300; t++) {
            tide_world_tick(&a);
            const uint64_t hash = tide_world_hash(&a); // The resident stamp and digest are in it
            images = tide_hash_more(images, &hash, sizeof hash);
            bodies = places(&a, bodies);
            const uint64_t image = b3t_image_hash();
            bytes = tide_hash_more(bytes, &image, sizeof image);
        }
        int crates = 0;
        int resting = 0;
        for (uint32_t i = 0; i < a.entities.next_unused; i++) {
            const tide_entity e = tide_entity_in_slot(&a.entities, i);
            if (!tide_has_Crate(&a, e)) continue;
            const Box3D_Body body = tide_get_Box3D_Body(&a, e);
            TIDE_CHECK(body.id != 0);
            TIDE_CHECK(body.position.y > 0.0f && body.position.y < 20.0f); // On the floor or on each other, not through it
            crates++;
            if (body.velocity.x == 0.0f && body.velocity.y == 0.0f && body.velocity.z == 0.0f) resting++;
        }
        printf("    %s: %d crates, %d at rest; %d touches began and %d ended; the resident arena is %d bytes\n",
               narrow ? "4 at a time" : "as this CPU does", crates, resting, (int)a.Tally.touched, (int)a.Tally.parted,
               physics_bytes(&a));
        TIDE_CHECK(crates == 22); // 12 stacked, 8 thrown, 1 pushed, 1 capsule: the doomed one went
        TIDE_CHECK(a.Box3D_Physics.bodies == 24 && a.Box3D_Physics.shapes == 24);
        TIDE_CHECK(a.Tally.touched > 20 && a.Tally.parted > 0);
        printf("    the world's hash is %016llx, its 300 ticks' %016llx, and its bodies' %016llx\n",
               (unsigned long long)tide_world_hash(&a), (unsigned long long)tide_hash_end(images), (unsigned long long)tide_hash_end(bodies));
        printf("    Box3D's own resident digests, apart from the match: %016llx\n", (unsigned long long)tide_hash_end(bytes));
        TIDE_CHECK(tide_world_hash(&a) == SETTLED_HASH);
        TIDE_CHECK(tide_hash_end(images) == IMAGES_HASH);
        TIDE_CHECK(tide_hash_end(bytes) == OWN_HASH);
        TIDE_CHECK(tide_hash_end(bodies) == BODIES_HASH);
        images_since(&whole, &in_parts);
        printf("    %d resident commits used whole marks, and %d sparse marks\n", (int)whole, (int)in_parts);
        TIDE_CHECK(whole > 0 && in_parts > (narrow ? 20 : 250)); // Both ways are in the pinned hashes
        tide_world_free(&a);
    }
    b3t_narrow(false);
}

// A snapshot taken at any tick goes on as the world it was taken of did,
// whatever C's worlds have been through since.
TIDE_TEST(box3d_rolls_back)
{
    b3t_check(true, true);
    b3t_forget();
    start(&a, 12);
    tick(&a, 100);
    tide_world_copy(&kept, &a);
    uint64_t went[80];
    for (int i = 0; i < 80; i++) {
        tide_world_tick(&a);
        went[i] = tide_world_hash(&a);
    }
    for (int round = 0; round < 3; round++) {
        tide_world_copy(&b, &kept);
        for (int i = 0; i < 80; i++) {
            tide_world_tick(&b);
            if (tide_world_hash(&b) != went[i]) printf("    round %d: tick %d came out different\n", round, i);
            TIDE_REQUIRE(tide_world_hash(&b) == went[i]);
        }
    }
    TIDE_CHECK(b3t_restores() >= 3);
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&kept);
}

// Restoring resident pages before every step gives the same world as running
// straight through: the pages keep everything, and a restore leaves nothing
// of its own in the next step.
TIDE_TEST(box3d_images_say_everything)
{
    enum { TICKS = 240 };
    static uint64_t went[TICKS];
    b3t_check(true, true);
    b3t_forget();
    start(&a, 12);
    for (int i = 0; i < TICKS; i++) {
        tide_world_tick(&a);
        went[i] = tide_world_hash(&a);
    }
    start(&b, 12);
    for (int i = 0; i < TICKS; i++) {
        const int32_t before = b3t_restores();
        b3t_forget();
        tide_world_tick(&b);
        TIDE_REQUIRE(i == 0 || b3t_restores() == before + 1);
        if (tide_world_hash(&b) != went[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(tide_world_hash(&b) == went[i]);
    }
    tide_world_free(&a);
    tide_world_free(&b);
}

// A player who joins gets the world as bytes, and has none of C's.
TIDE_TEST(box3d_joins)
{
    b3t_check(true, true);
    b3t_forget();
    start(&a, 12);
    tick(&a, 100);
    const uint32_t size = tide_world_pack(&a, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(tide_world_pack(&a, bytes, size) == size);
    tick(&a, 100);
    const uint64_t went = tide_world_hash(&a);

    b3t_forget();
    TIDE_REQUIRE(tide_world_unpack(&b, bytes, size));
    tick(&b, 100);
    TIDE_CHECK(tide_world_hash(&b) == went);
    printf("    the world is %u bytes packed; Box3D's resident arena is %d bytes\n", (unsigned)size, physics_bytes(&b));
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
    uint64_t counting; // Optional full-arena check time inside the ticks, which no match pays
    b3t_measure(&step, &image);
    b3t_measure_blocks(&changed, &in_all, &counting);
    int32_t whole = 0;
    int32_t in_parts = 0;
    images_since(&whole, &in_parts);
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
    b3t_measure(&step, &image);
    b3t_measure_blocks(&changed, &in_all, &counting);
    took -= counting;
    images_since(&whole, &in_parts);
    printf("    %-19s a tick takes %.3f ms: %.3f Box3D's step, %.3f resident restore/commit, %.3f the rest; keeping and hashing it %.3f, "
           "with %.0f%% of arena pages touched; %d commits whole and %d sparse\n",
           what, (double)took / (ticks * 1e6), (double)step / (ticks * 1e6), (double)image / (ticks * 1e6),
           (double)(took - step - image) / (ticks * 1e6), (double)kept_ns / (ticks * 1e6),
           in_all ? 100.0 * (double)changed / (double)in_all : 0.0, (int)whole, (int)in_parts);
    if (getenv("B3T_SYSTEMS")) {
        for (uint32_t i = 0; i < profile.systems.count; i++) {
            printf("        %-24s %.3f ms\n", profile.systems.items[i].name, profile.systems.items[i].total / ticks);
        }
        printf("        %-24s %.3f ms\n", "(changes and events)", profile.changes_ms / ticks);
    }
}

TIDE_TEST(box3d_costs)
{
    enum { CRATES = 2400 };
    b3t_check(false, false); // Timed: as a match runs it
    b3t_forget();
    start(&a, CRATES);
    cost(&a, "everything moving:", 120);
    tick(&a, 600); // The stacks settle and sleep; one crate is still pushed around
    cost(&a, "one crate pushed:", 240);
    a.Size.calm = true;
    tick(&a, 600);
    const uint64_t stamp = (uint64_t)a.Box3D_Physics.stamp;
    cost(&a, "at rest:", 240);
    TIDE_CHECK((uint64_t)a.Box3D_Physics.stamp == stamp && !a.Box3D_Physics.moved); // Nothing stepped, and nothing was written
    // What the sparse tracking doesn't follow marks the whole arena: one crate's type, changed
    // every tick, in a world that's otherwise asleep
    a.Size.flip = true;
    cost(&a, "a type changed:", 60);
    a.Size.flip = false;
    a.Size.lull = true;
    cost(&a, "a crate put to sleep:", 60);
    a.Size.lull = false;
    printf("    %d crates; the resident arena is %d bytes\n", CRATES, physics_bytes(&a));
    tide_world_free(&a);
    tide_world_free(&kept);
}

#ifdef BOX3D_TEST_JOBS
// A step shared out over the tick's threads leaves the world a step on one
// thread does.
TIDE_TEST(box3d_threads_change_nothing)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs) {
        printf("    one core: nothing to compare\n");
        return;
    }
    enum { CRATES = 2400, TICKS = 120 };
    b3t_check(false, false); // Timed: as a match runs it. parts_test.c checks images in parts on threads
    b3t_forget();
    start(&a, CRATES);
    start(&b, CRATES);
    const int32_t before = b3t_shared_steps();
    uint64_t ticks[2] = {0, 0};
    uint64_t steps[2] = {0, 0};
    uint64_t images[2] = {0, 0};
    uint64_t step;
    uint64_t image;
    uint64_t changed;
    uint64_t in_all;
    uint64_t counting; // The measuring's own time inside a tick, taken off it
    b3t_measure(&step, &image);
    b3t_measure_blocks(&changed, &in_all, &counting);
    for (int i = 0; i < TICKS; i++) {
        for (int on = 0; on < 2; on++) {
            const uint64_t t = tide_time_now_ns();
            if (on) tide_world_tick_on(&b, jobs);
            else tide_world_tick(&a);
            ticks[on] += tide_time_now_ns() - t;
            tide_world_copy(on ? &kept_too : &kept, on ? &b : &a);
            b3t_measure(&step, &image);
            b3t_measure_blocks(&changed, &in_all, &counting);
            ticks[on] -= counting;
            steps[on] += step;
            images[on] += image;
        }
        if (tide_world_hash(&a) != tide_world_hash(&b)) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(tide_world_hash(&a) == tide_world_hash(&b));
    }
    for (int on = 0; on < 2; on++) {
        printf("    %d crates on %u thread%s: a tick takes %.2f ms: %.2f Box3D's step, %.2f resident restore/commit, %.2f the rest\n", CRATES,
               on ? (unsigned)jobs->threads : 1u, on ? "s" : "", (double)ticks[on] / (TICKS * 1e6), (double)steps[on] / (TICKS * 1e6),
               (double)images[on] / (TICKS * 1e6), (double)(ticks[on] - steps[on] - images[on]) / (TICKS * 1e6));
    }
    printf("    %d steps were shared out; the resident arena is %d bytes\n", (int)(b3t_shared_steps() - before), physics_bytes(&a));
    TIDE_CHECK(b3t_shared_steps() - before > TICKS / 2);

    b3t_forget();
    b3t_measure(&step, &image);
    const uint64_t t = tide_time_now_ns();
    tide_world_tick(&a);
    b3t_measure(&step, &image);
    b3t_measure_blocks(&changed, &in_all, &counting);
    const uint64_t ticked = tide_time_now_ns() - t - counting;
    printf("    a tick that restores Box3D's resident state first takes %.2f ms, %.2f more than one that has it\n",
           (double)ticked / 1e6, ((double)ticked - (double)ticks[0] / TICKS) / 1e6);
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&kept);
    tide_world_free(&kept_too);
}
#endif
