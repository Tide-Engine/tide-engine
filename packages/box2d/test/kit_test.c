// What a body is made of and what code asks of the world, with the Box2D
// package (kit.tide): parts, bodies that change what they are, rays, shapes
// that are cast and looked into, what's in a sensor, hits, and how the world
// is tuned.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kit.h"
#include "tide/list.h"
#include "tide/map.h"
#include "tide/math.h"
#include "tide/net.h"
#include "tide/time.h"
#include "tide_test.h"

#ifdef BOX2D_TEST_JOBS
#include "tide/jobs.h"
#include "tide/platform.h"
#endif

// bridge.c
void b2t_forget(void);
int32_t b2t_restores(void);
void b2t_share_from(int32_t bodies);
int32_t b2t_asking_threads(void);

#define DT (1.0f / 60.0f)

static tide_world a;
static tide_world b;
static tide_world kept;

static void start(tide_world *w, const Case which)
{
    b2t_forget();
    tide_world_init(w, DT);
    w->Kit.which = which;
}

static void tick(tide_world *w, const int ticks)
{
    for (int i = 0; i < ticks; i++) tide_world_tick(w);
}

typedef bool has_fn(const tide_world *, tide_entity);

// The one entity with this component
static tide_entity the(const tide_world *w, has_fn *has)
{
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&w->entities, i);
        if (has(w, e)) return e;
    }
    return (tide_entity){0};
}

static int count(const tide_world *w, has_fn *has)
{
    int found = 0;
    for (uint32_t i = 0; i < w->entities.next_unused; i++) {
        if (has(w, tide_entity_in_slot(&w->entities, i))) found++;
    }
    return found;
}

static Box2D_Body body_of(const tide_world *w, has_fn *has)
{
    return tide_get_Box2D_Body(w, the(w, has));
}

static void put(tide_world *w, has_fn *has, const Box2D_Body body)
{
    tide_set_Box2D_Body(w, the(w, has), body);
}

static Heard heard_by(const tide_world *w, has_fn *has)
{
    return tide_get_Heard(w, the(w, has));
}

static bool near(const float x, const float y)
{
    return tide_abs_f(x - y) < 0.01f;
}

static bool same(const tide_entity x, const tide_entity y)
{
    return tide_entity_equal(x, y);
}

static bool none(const tide_entity e)
{
    return e.index == 0 && e.generation == 0;
}

// Ticks until a tick steps nothing, and whether one came within `most`
static bool rests(tide_world *w, const int most)
{
    for (int i = 0; i < most; i++) {
        tide_world_tick(w);
        if (!w->Box2D_Physics.moved) return true;
    }
    return false;
}

// A world at rest that nothing is done to: no tick steps it
static bool stays(tide_world *w)
{
    const int64_t stamp = w->Box2D_Physics.stamp;
    bool still = true;
    for (int i = 0; i < 60; i++) {
        tide_world_tick(w);
        still = still && !w->Box2D_Physics.moved && w->Box2D_Physics.stamp == stamp;
    }
    return still;
}

// ---------------------------------------------------------------------------
// Parts

// The match of parts after its ticks, on every platform and in every
// configuration: its parts' shapes are in Box2D's image, and whose each is in
// the match.
#define PARTS_HASH UINT64_C(0x8df6177caaaf0e2b)
#define PARTS_IMAGES_HASH UINT64_C(0x834692ff48834364)

enum { PART_TICKS = 420, SNIP = 200, FELL = 260, LOOK = 180 };

// The match of parts, with every tick's hash: the tee's cap goes at one tick,
// and the doomed body at another. With `again`, C's world is made from the
// match's image before every tick. `at_look` is the world before either.
static void play_parts(tide_world *w, uint64_t *hashes, const bool again, tide_world *at_look)
{
    start(w, Case_Parts);
    for (int i = 0; i < PART_TICKS; i++) {
        w->Kit.snip = i == SNIP;
        w->Kit.fell = i == FELL;
        if (again) b2t_forget();
        tide_world_tick(w);
        hashes[i] = tide_world_hash(w);
        if (i == LOOK && at_look) tide_world_copy(at_look, w);
    }
}

TIDE_TEST(box2d_parts_are_shapes_of_their_body)
{
    static uint64_t went[PART_TICKS];
    play_parts(&a, went, false, &kept);

    // With everything at rest, and nothing gone yet
    const tide_world *w = &kept;
    const tide_entity tee = the(w, tide_has_Tee);
    const tide_entity cap = the(w, tide_has_Cap);
    const Box2D_Part part = tide_get_Box2D_Part(w, cap);
    TIDE_CHECK(same(part.of, tee) && part.box != 0 && part.circle == 0);
    // A bar of 2 with a cap of 1 weighs 3: thrown with an impulse of 3 where it was spawned, it goes at 1. The cap
    // was on it by the step that threw it
    const Box2D_Body thrown = body_of(w, tide_has_Thrown);
    printf("    a bar of 2 by 1 with a cap of 1 by 1 for a part, thrown with an impulse of 3, goes at %g\n", (double)thrown.velocity.x);
    TIDE_CHECK(near(thrown.velocity.x, 1.0f) && thrown.velocity.y == 0.0f && thrown.spin == 0.0f);
    // The cap is solid: a marble dropped on it rests on it, two meters up, and one dropped beside it on the bar
    const Box2D_Body on_cap = body_of(w, tide_has_OnCap);
    const Box2D_Body on_bar = body_of(w, tide_has_OnBar);
    printf("    a marble dropped on the cap rests at %g, and one beside it on the bar at %g\n", (double)on_cap.position.y,
           (double)on_bar.position.y);
    TIDE_CHECK(near(on_cap.position.y, 2.25f) && near(on_bar.position.y, 1.15f));
    // The events say which shapes met: the marble's own, and the tee's cap, which is an entity of its own
    const Heard marble = heard_by(w, tide_has_OnCap);
    const Heard heard = heard_by(w, tide_has_Tee);
    TIDE_CHECK(marble.touched == 1 && same(marble.last, tee) && same(marble.part, the(w, tide_has_OnCap)) && same(marble.otherPart, cap));
    // The tee heard the floor under its bar, then the two marbles: the one on its bar last
    TIDE_CHECK(heard.touched == 3 && same(heard.last, the(w, tide_has_OnBar)) && same(heard.part, tee));
    TIDE_CHECK(same(heard.otherPart, the(w, tide_has_OnBar)));
    const Heard on_bar_heard = heard_by(w, tide_has_OnBar);
    TIDE_CHECK(on_bar_heard.touched == 1 && same(on_bar_heard.part, the(w, tide_has_OnBar)) && same(on_bar_heard.otherPart, tee));
    // A part can be a sensor: the ball fell through the post's eye, and each heard of the other, by the eye
    const tide_entity eye = the(w, tide_has_Eye);
    const Heard post = heard_by(w, tide_has_Post);
    const Heard through = heard_by(w, tide_has_Through);
    TIDE_CHECK(post.touched == 1 && post.parted == 1 && same(post.last, the(w, tide_has_Through)) && same(post.part, eye));
    TIDE_CHECK(through.touched == 2 && through.parted == 1 && same(through.last, the(w, tide_has_Floor)));
    TIDE_CHECK(body_of(w, tide_has_Through).position.y < 0.5f);
    // A part that names no body went, and so did one that names an entity with none: the entity itself is there
    TIDE_CHECK(count(w, tide_has_Stray) == 1 && !tide_has_Box2D_Part(w, the(w, tide_has_Stray)));
    // Every shape is on a body: the floor's, the tee's two, the marbles', the thrown one's two, the doomed one's three,
    // the post's two and the ball's
    TIDE_CHECK(w->Box2D_Physics.bodies == 8 && w->Box2D_Physics.shapes == 13);
    TIDE_CHECK(tide_map_count(w->Box2D_Physics.parts) == 5 && count(w, tide_has_Box2D_Part) == 5);
    TIDE_CHECK(!w->Box2D_Physics.moved || body_of(w, tide_has_Thrown).velocity.x > 0.0f);

    // The cap went: its shape with it, and nothing else of the tee. The marble that was on it is on the bar now,
    // and heard the cap go: a parting from the tee, by no part
    w = &a;
    TIDE_CHECK(!tide_has_Cap(w, cap) && tide_has_Tee(w, tee));
    const Box2D_Body fallen = body_of(w, tide_has_OnCap);
    printf("    with the cap gone, the marble that was on it rests at %g\n", (double)fallen.position.y);
    TIDE_CHECK(near(fallen.position.y, 1.25f));
    const Heard after = heard_by(w, tide_has_OnCap);
    TIDE_CHECK(after.touched == 2 && after.parted == 1 && same(after.last, tee) && same(after.otherPart, tee));
    const Heard tee_after = heard_by(w, tide_has_Tee);
    TIDE_CHECK(tee_after.parted == 1 && tee_after.touched == 4);
    // ...and the doomed body, with both its parts: their entities too, the tick after its own
    TIDE_CHECK(count(w, tide_has_Doomed) == 0 && count(w, tide_has_Limb) == 0);
    TIDE_CHECK(tide_map_count(w->Box2D_Physics.parts) == 2 && count(w, tide_has_Box2D_Part) == 2);
    TIDE_CHECK(tide_list_read_count(&w->heap, w->Box2D_Physics.gone) == 0 && tide_list_read_count(&w->heap, w->Box2D_Physics.shed) == 0);

    uint64_t images = 0;
    for (int i = 0; i < PART_TICKS; i++) images = tide_hash_more(images, &went[i], sizeof went[i]);
    printf("    the world's hash is %016llx, and its %d images' %016llx\n", (unsigned long long)went[PART_TICKS - 1], (int)PART_TICKS,
           (unsigned long long)tide_hash_end(images));
    TIDE_CHECK(went[PART_TICKS - 1] == PARTS_HASH);
    TIDE_CHECK(tide_hash_end(images) == PARTS_IMAGES_HASH);

    // A part's shape is in Box2D's image like any other, and whose it is in the match: a world made from the
    // image again before every tick goes the same way, tick for tick
    static uint64_t again[PART_TICKS];
    play_parts(&b, again, true, NULL);
    for (int i = 0; i < PART_TICKS; i++) {
        if (again[i] != went[i]) printf("    tick %d came out different\n", i);
        TIDE_REQUIRE(again[i] == went[i]);
    }
    tide_world_free(&b);

    // ...and so does a snapshot taken before the parts went, and the world as a player who joins is sent it
    tide_world_copy(&b, &kept);
    const uint32_t size = tide_world_pack(&kept, NULL, 0);
    uint8_t *bytes = malloc(size);
    TIDE_REQUIRE(tide_world_pack(&kept, bytes, size) == size);
    static tide_world joined;
    b2t_forget();
    TIDE_REQUIRE(tide_world_unpack(&joined, bytes, size));
    for (int i = LOOK + 1; i < PART_TICKS; i++) {
        b.Kit.snip = joined.Kit.snip = i == SNIP;
        b.Kit.fell = joined.Kit.fell = i == FELL;
        tide_world_tick(&b);
        tide_world_tick(&joined);
        TIDE_REQUIRE(tide_world_hash(&b) == went[i]);
        TIDE_REQUIRE(tide_world_hash(&joined) == went[i]);
    }
    free(bytes);
    tide_world_free(&joined);
    tide_world_free(&a);
    tide_world_free(&b);
    tide_world_free(&kept);
}

// ---------------------------------------------------------------------------
// A body's type

static void retype(tide_world *w, has_fn *has, const Box2D_BodyType type)
{
    Box2D_Body body = body_of(w, has);
    body.type = type;
    put(w, has, body);
}

TIDE_TEST(box2d_a_body_changes_what_it_is)
{
    start(&a, Case_Kinds);
    TIDE_REQUIRE(rests(&a, 600));
    TIDE_CHECK(near(body_of(&a, tide_has_Shifter).position.y, 0.5f));

    // Dynamic to static, and put in the air: it hangs there, and the world is at rest again
    Box2D_Body body = body_of(&a, tide_has_Shifter);
    body.type = Box2D_BodyType_Static;
    body.position.y = 5.0f;
    put(&a, tide_has_Shifter, body);
    tide_world_tick(&a);
    TIDE_CHECK(a.Box2D_Physics.moved); // Written while everything slept: the world stepped
    TIDE_REQUIRE(rests(&a, 120));
    body = body_of(&a, tide_has_Shifter);
    TIDE_CHECK(body.position.y == 5.0f && body.velocity.y == 0.0f);
    TIDE_CHECK(stays(&a));

    // Static to kinematic, with a speed: it goes at that speed, whatever pulls on it
    body.type = Box2D_BodyType_Kinematic;
    body.velocity = tide_f2(1.0f, 0.0f);
    put(&a, tide_has_Shifter, body);
    tick(&a, 60);
    body = body_of(&a, tide_has_Shifter);
    printf("    static, then kinematic at 1 m/s: a second later it's at (%g, %g)\n", (double)body.position.x, (double)body.position.y);
    TIDE_CHECK(near(body.position.x, 1.0f) && body.position.y == 5.0f && body.velocity.x == 1.0f);

    // Kinematic to dynamic: it falls, with the speed it had
    retype(&a, tide_has_Shifter, Box2D_BodyType_Dynamic);
    tick(&a, 20);
    body = body_of(&a, tide_has_Shifter);
    printf("    then dynamic: a third of a second later it's at (%g, %g), going (%g, %g)\n", (double)body.position.x, (double)body.position.y,
           (double)body.velocity.x, (double)body.velocity.y);
    TIDE_CHECK(body.position.y < 4.6f && body.velocity.y < -3.0f && near(body.velocity.x, 1.0f));

    // Dynamic to kinematic, in the air, with a speed upwards: it rises steadily
    body.type = Box2D_BodyType_Kinematic;
    body.velocity = tide_f2(0.0f, 2.0f);
    put(&a, tide_has_Shifter, body);
    const float from = body.position.y;
    tick(&a, 30);
    body = body_of(&a, tide_has_Shifter);
    TIDE_CHECK(near(body.position.y, from + 1.0f) && body.velocity.y == 2.0f && body.velocity.x == 0.0f);

    // Kinematic to static: it stops where it is, and says so
    retype(&a, tide_has_Shifter, Box2D_BodyType_Static);
    const float hung = body_of(&a, tide_has_Shifter).position.y;
    TIDE_REQUIRE(rests(&a, 120));
    body = body_of(&a, tide_has_Shifter);
    TIDE_CHECK(body.position.y == hung && body.velocity.x == 0.0f && body.velocity.y == 0.0f);
    TIDE_CHECK(stays(&a));

    // Static to dynamic: it falls to the floor
    retype(&a, tide_has_Shifter, Box2D_BodyType_Dynamic);
    TIDE_REQUIRE(rests(&a, 600));
    body = body_of(&a, tide_has_Shifter);
    printf("    and dynamic from static, it comes to rest at %g\n", (double)body.position.y);
    TIDE_CHECK(near(body.position.y, 0.5f));
    TIDE_CHECK(a.Box2D_Physics.bodies == 6 && a.Box2D_Physics.shapes == 6); // The same bodies and shapes all along
    tide_world_free(&a);
}

TIDE_TEST(box2d_what_rests_on_a_body_that_changes_stays_or_falls)
{
    start(&a, Case_Kinds);
    TIDE_REQUIRE(rests(&a, 600));
    const float rode = body_of(&a, tide_has_Rider).position.y;
    TIDE_CHECK(near(rode, 1.5f));

    // The crate under another turns static, while both sleep: the one on it stays where it was, and they come to rest
    retype(&a, tide_has_Base, Box2D_BodyType_Static);
    tide_world_tick(&a);
    TIDE_CHECK(a.Box2D_Physics.moved);
    TIDE_REQUIRE(rests(&a, 600));
    printf("    a crate that rested at %g on one that turned static rests at %g\n", (double)rode, (double)body_of(&a, tide_has_Rider).position.y);
    TIDE_CHECK(near(body_of(&a, tide_has_Rider).position.y, rode) && near(body_of(&a, tide_has_Rider).position.x, 10.0f));
    TIDE_CHECK(body_of(&a, tide_has_Base).type == Box2D_BodyType_Static);
    TIDE_CHECK(stays(&a));
    // It's a body that never moves now: pushed, it stays, and so does what's on it
    Box2D_Body base = body_of(&a, tide_has_Base);
    const float stood = base.position.x;
    base.impulse = tide_f2(50.0f, 0.0f);
    put(&a, tide_has_Base, base);
    tick(&a, 30);
    TIDE_CHECK(body_of(&a, tide_has_Base).position.x == stood && near(body_of(&a, tide_has_Rider).position.x, 10.0f));
    TIDE_CHECK(body_of(&a, tide_has_Base).impulse.x == 0.0f); // Given, to a body it does nothing to
    // ...and dynamic again, the same push sends it off, from under the crate, which comes down to the floor
    TIDE_REQUIRE(rests(&a, 600));
    base = body_of(&a, tide_has_Base);
    base.type = Box2D_BodyType_Dynamic;
    base.impulse = tide_f2(50.0f, 0.0f);
    put(&a, tide_has_Base, base);
    TIDE_REQUIRE(rests(&a, 900));
    TIDE_CHECK(body_of(&a, tide_has_Base).position.x > 12.0f);
    TIDE_CHECK(near(body_of(&a, tide_has_Rider).position.y, 0.5f));

    // A ledge in the air that never moved, with a crate on it, turns dynamic: both fall
    TIDE_CHECK(near(body_of(&a, tide_has_Load).position.y, 5.75f) && body_of(&a, tide_has_Hung).position.y == 5.0f);
    retype(&a, tide_has_Hung, Box2D_BodyType_Dynamic);
    tick(&a, 20);
    TIDE_CHECK(body_of(&a, tide_has_Hung).velocity.y < -2.0f && body_of(&a, tide_has_Load).velocity.y < -2.0f);
    TIDE_REQUIRE(rests(&a, 900));
    printf("    a ledge that turned dynamic rests at %g, and the crate that was on it at %g\n", (double)body_of(&a, tide_has_Hung).position.y,
           (double)body_of(&a, tide_has_Load).position.y);
    TIDE_CHECK(body_of(&a, tide_has_Hung).position.y < 0.3f && body_of(&a, tide_has_Load).position.y < 1.3f);
    TIDE_CHECK(stays(&a));
    tide_world_free(&a);
}

// A body's type is in Box2D's image: a world that's made from it before every
// tick changes type as one that never was, to the byte.
TIDE_TEST(box2d_types_are_in_the_image)
{
    enum { TICKS = 240 };
    static uint64_t went[TICKS];
    for (int round = 0; round < 2; round++) {
        tide_world *w = round ? &b : &a;
        start(w, Case_Kinds);
        for (int i = 0; i < TICKS; i++) {
            if (round) b2t_forget();
            if (i == 40) retype(w, tide_has_Shifter, Box2D_BodyType_Kinematic);
            if (i == 60) retype(w, tide_has_Base, Box2D_BodyType_Static);
            if (i == 80) retype(w, tide_has_Hung, Box2D_BodyType_Dynamic);
            if (i == 100) retype(w, tide_has_Shifter, Box2D_BodyType_Dynamic);
            if (i == 120) retype(w, tide_has_Base, Box2D_BodyType_Kinematic);
            if (i == 140) retype(w, tide_has_Shifter, Box2D_BodyType_Static);
            tide_world_tick(w);
            if (!round) went[i] = tide_world_hash(w);
            else TIDE_REQUIRE(tide_world_hash(w) == went[i]);
        }
    }
    tide_world_free(&a);
    tide_world_free(&b);
}


// ---------------------------------------------------------------------------
// What code asks of the world

static tide_local local;
static tide_draw_list draw;
static tide_gui gui;

static void frame(const tide_world *w)
{
    tide_draw_reset(&draw);
    tide_frame(w, NULL, 1.0f, NULL, &local, &draw, &gui);
}

static bool same_bytes(const void *x, const void *y, const size_t size, const char *what)
{
    if (memcmp(x, y, size) == 0) return true;
    for (size_t i = 0; i < size; i++) {
        if (((const uint8_t *)x)[i] == ((const uint8_t *)y)[i]) continue;
        printf("    %s differ, from byte %d of %d\n", what, (int)i, (int)size);
        break;
    }
    return false;
}

// The match of questions after its ticks, and every answer the match was
// given along the way, on every platform and in every configuration.
#define ASKS_HASH UINT64_C(0xb3aac02bf5088cb6)
#define ANSWERS_HASH UINT64_C(0x53abf452b7c6e40b)

enum { ASK_TICKS = 150, FADE = 100 };

TIDE_TEST(box2d_answers_what_code_asks)
{
    start(&a, Case_Asks);
    tide_local_init(&local);
    memset(&gui, 0, sizeof gui);
    // Before anything was stepped there's nothing to find, and asking is fine
    tide_world_tick(&a);
    TIDE_CHECK(!a.Asked.before.wall.any && !a.Asked.before.through.any && a.Asked.before.pair.count == 0);
    TIDE_CHECK(!a.Asked.after.wall.any); // The bodies were spawned in that tick: they're in the next one's step
    tick(&a, 30);

    const tide_world *w = &a;
    const Answers got = w->Asked.after;
    const tide_entity floor = the(w, tide_has_Floor);
    const tide_entity tee = the(w, tide_has_Tee);
    const tide_entity wall = the(w, tide_has_Wall);
    // A ray meets the first thing in its way: where, how far along, and which way that faces
    printf("    a ray along the floor meets the wall at (%g, %g), %g of its way, facing (%g, %g)\n", (double)got.wall.point.x,
           (double)got.wall.point.y, (double)got.wall.fraction, (double)got.wall.normal.x, (double)got.wall.normal.y);
    TIDE_CHECK(got.wall.any && same(got.wall.entity, wall) && same(got.wall.part, wall));
    TIDE_CHECK(near(got.wall.point.x, 4.5f) && near(got.wall.point.y, 0.5f) && near(got.wall.fraction, 0.5625f));
    TIDE_CHECK(near(got.wall.normal.x, -1.0f) && near(got.wall.normal.y, 0.0f));
    TIDE_CHECK(got.ball.any && same(got.ball.entity, the(w, tide_has_Ball)) && near(got.ball.point.x, -4.134f));
    // A sensor stops nothing, a ray neither: this one goes through it, to the floor
    TIDE_CHECK(got.through.any && same(got.through.entity, floor) && near(got.through.fraction, 6.0f / 7.0f));
    TIDE_CHECK(near(got.through.normal.y, 1.0f));
    TIDE_CHECK(!got.miss.any && none(got.miss.entity) && got.miss.fraction == 0.0f);
    // What it met says which shape: the tee's cap is a part, an entity of its own, and its bar is the tee's
    TIDE_CHECK(got.cap.any && same(got.cap.entity, tee) && same(got.cap.part, the(w, tide_has_Cap)) && near(got.cap.point.y, 2.0f));
    TIDE_CHECK(got.bar.any && same(got.bar.entity, tee) && same(got.bar.part, tee) && near(got.bar.point.y, 1.0f));
    // One that starts inside a shape meets it there, with no way it faces
    TIDE_CHECK(got.inside.any && same(got.inside.entity, wall) && got.inside.fraction == 0.0f);
    TIDE_CHECK(got.inside.normal.x == 0.0f && got.inside.normal.y == 0.0f);
    // Down between two boxes that touch, which are as near as each other: the one that was made first, on
    // whichever side it is
    TIDE_CHECK(got.twins.any && same(got.twins.entity, the(w, tide_has_First)) && near(got.twins.fraction, 0.8f));
    TIDE_CHECK(got.others.any && same(got.others.entity, the(w, tide_has_Third)) && got.others.fraction == got.twins.fraction);
    // A shape that's cast stops where it first touches
    printf("    a box 4 wide let down from 5 onto two boxes 1 high touches at (%g, %g), %g of its way; one turned an eighth meets the "
           "floor %g of its way\n",
           (double)got.wide.point.x, (double)got.wide.point.y, (double)got.wide.fraction, (double)got.turned.fraction);
    TIDE_CHECK(got.wide.any && near(got.wide.fraction, 0.75f) && near(got.wide.point.y, 1.0f) && near(got.wide.normal.y, 1.0f));
    TIDE_CHECK(same(got.wide.entity, the(w, tide_has_First)) || same(got.wide.entity, the(w, tide_has_Second)));
    // A square turned an eighth of a turn reaches half its diagonal down: it touches the floor from 0.707 up
    TIDE_CHECK(got.turned.any && same(got.turned.entity, floor) && near(got.turned.fraction, (5.0f - 0.7071f) / 6.0f));
    TIDE_CHECK(near(got.turned.point.x, 60.0f) && near(got.turned.point.y, 0.0f));
    // A circle half a meter over where it's cast from, rolled into the wall
    TIDE_CHECK(got.round.any && same(got.round.entity, wall) && near(got.round.fraction, 0.5f) && near(got.round.point.x, 4.5f));
    TIDE_CHECK(near(got.round.point.y, 1.5f));
    // What's in a shape, each once, in the order the bodies were made in
    TIDE_CHECK(got.pair.count == 2 && same(got.pair.a, the(w, tide_has_First)) && same(got.pair.b, the(w, tide_has_Second)));
    TIDE_CHECK(none(got.pair.c));
    TIDE_CHECK(got.tee.count == 1 && same(got.tee.a, tee));
    TIDE_CHECK(got.none.count == 0 && none(got.none.a));
    // A body asks with itself left out: a ray from its middle meets itself at once, and past itself, the ball. Its
    // own box, a little smaller, is free to move to the left along the floor it rests on, and to the right as
    // far as the ball
    const tide_entity prober = the(w, tide_has_Prober);
    const Mine own = w->Asked.ownAfter;
    TIDE_CHECK(own.self.any && same(own.self.entity, prober) && own.self.fraction == 0.0f);
    TIDE_CHECK(own.past.any && same(own.past.entity, the(w, tide_has_Ball)) && near(own.past.point.x, -5.866f));
    printf("    a crate's own box is free to go 2 m left, and goes %g of 8 m to the right before it touches the ball\n", (double)own.reach.fraction);
    TIDE_CHECK(!own.step.any);
    TIDE_CHECK(own.reach.any && same(own.reach.entity, the(w, tide_has_Ball)) && own.reach.fraction > 0.4f && own.reach.fraction < 0.55f);
    // What's in an entity's sensors now: the floor the trap reaches and the two crates in it, and for its part, a
    // sensor of the same body that's an entity of its own, the floor and the one crate in that
    const Some trap = w->Asked.trapAfter;
    const Some eye = w->Asked.eyeAfter;
    printf("    the trap has %d bodies in its sensors, and its part %d\n", (int)trap.count, (int)eye.count);
    TIDE_CHECK(trap.count == 3 && same(trap.a, floor) && tide_has_Caught(w, trap.b) && tide_has_Caught(w, trap.c) && !same(trap.b, trap.c));
    TIDE_CHECK(eye.count == 2 && same(eye.a, floor) && tide_has_Caught(w, eye.b) && !same(eye.b, trap.b) && !same(eye.b, trap.c));

    // Asked by the match after its step, by a view of that tick, and by the match before the next tick's step:
    // the same world, so the same answers. And the same from a world that's made from the image to answer
    uint64_t answers = 0;
    int restored = 0;
    const tide_entity ghost = the(&a, tide_has_Ghost);
    for (int i = 0; i < ASK_TICKS; i++) {
        const Asked was = a.Asked;
        frame(&a);
        const Seen seen = local.Seen;
        Answers after = was.after;
        if (i == FADE + 1) {
            // The ghost's entity was destroyed at the end of the last tick, after the match asked. Its body is in
            // C's world until this tick's step, and nothing that asks meets it any more: not a view now, nor the
            // match before that step. What's under it is met
            TIDE_CHECK(same(after.ghost.entity, ghost) && near(after.ghost.point.y, 1.0f) && count(&a, tide_has_Ghost) == 0);
            TIDE_CHECK(seen.answers.ghost.any && same(seen.answers.ghost.entity, floor) && near(seen.answers.ghost.point.y, 0.0f));
            after.ghost = seen.answers.ghost;
        }
        TIDE_REQUIRE(same_bytes(&seen.answers, &after, sizeof after, "a view's answers and the match's after its step"));
        TIDE_REQUIRE(same_bytes(&seen.own, &was.ownAfter, sizeof seen.own, "a view's and the match's, for a body"));
        TIDE_REQUIRE(same_bytes(&seen.trap, &was.trapAfter, sizeof seen.trap, "a view's and the match's, of a sensor"));
        TIDE_REQUIRE(same_bytes(&seen.eye, &was.eyeAfter, sizeof seen.eye, "a view's and the match's, of a part that's a sensor"));
        // Every other tick with none of C's worlds: the view's question has one made, and then the match's
        const int32_t before = b2t_restores();
        if (i % 2 == 1) {
            b2t_forget();
            memset(&local.Seen, 0, sizeof local.Seen);
            frame(&a);
            TIDE_REQUIRE(same_bytes(&local.Seen, &seen, sizeof seen, "a view's answers from a world made from the image, and from the one that was stepped"));
            TIDE_REQUIRE(b2t_restores() == before + 1);
            b2t_forget();
        }
        a.Kit.fade = i == FADE;
        tide_world_tick(&a);
        if (i % 2 == 1) {
            TIDE_REQUIRE(b2t_restores() == before + 2); // Once more, for the match's first question: its step went on from that world
            restored++;
        }
        TIDE_REQUIRE(same_bytes(&a.Asked.before, &after, sizeof after, "the match's answers before a step and after the one before it"));
        TIDE_REQUIRE(same_bytes(&a.Asked.ownBefore, &was.ownAfter, sizeof was.ownAfter, "the match's before and after, for a body"));
        TIDE_REQUIRE(same_bytes(&a.Asked.trapBefore, &was.trapAfter, sizeof was.trapAfter, "the match's before and after, of a sensor"));
        TIDE_REQUIRE(same_bytes(&a.Asked.eyeBefore, &was.eyeAfter, sizeof was.eyeAfter, "the match's before and after, of a part"));
        answers = tide_hash_more(answers, &a.Asked, sizeof a.Asked);
    }
    TIDE_CHECK(restored == ASK_TICKS / 2);
    TIDE_CHECK(same(a.Asked.after.ghost.entity, floor));
    // The ball that fell is on the floor, and a ray up from under it meets it at once
    TIDE_CHECK(a.Asked.after.faller.any && tide_has_Faller(&a, a.Asked.after.faller.entity) && a.Asked.after.faller.fraction == 0.0f);
    TIDE_CHECK(got.faller.any && tide_has_Faller(&a, got.faller.entity) && got.faller.fraction > 0.3f);
    TIDE_CHECK(!a.Box2D_Physics.moved); // All of it from a world at rest, by the end
    printf("    the world's hash is %016llx, and its %d ticks' answers' %016llx\n", (unsigned long long)tide_world_hash(&a), (int)ASK_TICKS,
           (unsigned long long)tide_hash_end(answers));
    TIDE_CHECK(tide_world_hash(&a) == ASKS_HASH);
    TIDE_CHECK(tide_hash_end(answers) == ANSWERS_HASH);

    // What making a world from the image to answer costs: the least of a few
    uint64_t made = UINT64_MAX;
    uint64_t there = UINT64_MAX;
    for (int i = 0; i < 20; i++) {
        b2t_forget();
        uint64_t began = tide_time_now_ns();
        frame(&a);
        uint64_t took = tide_time_now_ns() - began;
        if (took < made) made = took;
        began = tide_time_now_ns();
        frame(&a);
        took = tide_time_now_ns() - began;
        if (took < there) there = took;
    }
    printf("    a frame whose views ask all of this takes %.3f ms, and %.3f when Box2D's world of %d bytes is made from the image first\n",
           (double)there / 1e6, (double)made / 1e6, (int)a.Box2D_Physics.size);
    tide_local_free(&local);
    tide_draw_free(&draw);
    tide_world_free(&a);
}

// ---------------------------------------------------------------------------
// Hits

TIDE_TEST(box2d_says_how_hard_bodies_meet)
{
    start(&a, Case_Hits);
    tick(&a, 120);
    const tide_world *w = &a;
    const Heard high = heard_by(w, tide_has_High);
    const Heard low = heard_by(w, tide_has_Low);
    const Heard floor = heard_by(w, tide_has_Floor);
    const Heard ram = heard_by(w, tide_has_Ram);
    const Heard struck = heard_by(w, tide_has_Struck);
    // A ball dropped two meters meets the floor at about 6.3 m/s: each hears of it once, with where, which way and
    // how fast. One let go three centimeters up meets it at under 1 m/s, and nothing is said
    printf("    a ball dropped 2 m hit the floor at (%g, %g), going %g m/s, facing (%g, %g)\n", (double)high.point.x, (double)high.point.y,
           (double)high.speed, (double)high.normal.x, (double)high.normal.y);
    TIDE_CHECK(high.hits == 1 && same(high.last, the(w, tide_has_Floor)) && same(high.part, the(w, tide_has_High)));
    TIDE_CHECK(same(high.otherPart, the(w, tide_has_Floor)));
    TIDE_CHECK(high.speed > 6.0f && high.speed < 6.5f && near(high.point.x, 0.0f) && tide_abs_f(high.point.y) < 0.05f);
    TIDE_CHECK(near(high.normal.x, 0.0f) && near(high.normal.y, -1.0f)); // From the ball into the floor
    TIDE_CHECK(low.hits == 0 && low.touched == 1);
    TIDE_CHECK(floor.hits == 1 && same(floor.last, the(w, tide_has_High)) && floor.speed == high.speed);
    TIDE_CHECK(near(floor.normal.y, 1.0f) && floor.point.x == high.point.x && floor.point.y == high.point.y);
    // A crate at 3 m/s runs into one that's still
    printf("    a crate at 3 m/s hit another at (%g, %g), going %g m/s, facing (%g, %g)\n", (double)ram.point.x, (double)ram.point.y,
           (double)ram.speed, (double)ram.normal.x, (double)ram.normal.y);
    TIDE_CHECK(ram.hits == 1 && struck.hits == 1 && same(ram.last, the(w, tide_has_Struck)) && same(struck.last, the(w, tide_has_Ram)));
    TIDE_CHECK(ram.speed > 2.9f && ram.speed < 3.1f && struck.speed == ram.speed);
    TIDE_CHECK(near(ram.normal.x, 1.0f) && near(struck.normal.x, -1.0f) && ram.point.x > 11.0f && ram.point.x < 13.0f);
    tide_world_free(&a);

    // How fast is fast enough is the world's to say: with the hit speed at half a meter a second, the ball that was
    // let go just over the floor is heard too, and at seven, neither
    for (int round = 0; round < 2; round++) {
        start(&a, Case_Hits);
        a.Box2D_Physics.hitSpeed = round ? 7.0f : 0.5f;
        tick(&a, 120);
        const Heard dropped = heard_by(&a, tide_has_High);
        const Heard let_go = heard_by(&a, tide_has_Low);
        printf("    with a hit speed of %g, the ball dropped 2 m sent %d and the one let go 3 cm up %d (at %g m/s)\n",
               (double)a.Box2D_Physics.hitSpeed, (int)dropped.hits, (int)let_go.hits, (double)let_go.speed);
        TIDE_CHECK(dropped.hits == (round ? 0 : 1) && let_go.hits == (round ? 0 : 1));
        if (!round) TIDE_CHECK(let_go.speed > 0.5f && let_go.speed < 1.0f);
        tide_world_free(&a);
    }
}

// ---------------------------------------------------------------------------
// Tuning

TIDE_TEST(box2d_is_tuned_by_its_singleton)
{
    // Whether bodies sleep: with it off, a world where everything lies still is stepped all the same
    start(&a, Case_Kinds);
    TIDE_REQUIRE(rests(&a, 900));
    TIDE_CHECK(stays(&a));
    a.Box2D_Physics.sleeping = false;
    tide_world_tick(&a);
    TIDE_CHECK(a.Box2D_Physics.moved);
    tick(&a, 120);
    TIDE_CHECK(a.Box2D_Physics.moved);
    a.Box2D_Physics.sleeping = true;
    TIDE_REQUIRE(rests(&a, 900));
    TIDE_CHECK(stays(&a));
    // A change that's made while the world sleeps is a step, so that it's in the world's image, as everything of
    // the world is: a world that's then made from the image is tuned the same, and has nothing to step for
    a.Box2D_Physics.contactStiffness = 12.0f;
    a.Box2D_Physics.contactDamping = 2.0f;
    a.Box2D_Physics.contactSpeed = 1.0f;
    a.Box2D_Physics.bounceSpeed = 0.25f;
    a.Box2D_Physics.hitSpeed = 3.0f;
    tide_world_tick(&a);
    TIDE_CHECK(a.Box2D_Physics.moved);
    TIDE_REQUIRE(rests(&a, 900));
    b2t_forget();
    TIDE_CHECK(stays(&a));
    tide_world_free(&a);

    // How fast a shape that bounces has to meet something to bounce: a ball let go 3 cm up meets the floor at
    // under 1 m/s, and only bounces once that's fast enough
    float rebound[2];
    for (int round = 0; round < 2; round++) {
        start(&a, Case_Tuning);
        if (round) a.Box2D_Physics.bounceSpeed = 0.25f;
        rebound[round] = 0.0f;
        for (int i = 0; i < 60; i++) {
            tide_world_tick(&a);
            const float up = body_of(&a, tide_has_Bouncy).velocity.y;
            if (up > rebound[round]) rebound[round] = up;
        }
        tide_world_free(&a);
    }
    printf("    a bouncy ball let go 3 cm up comes back at %g m/s, and at %g with a bounce speed of 0.25\n", (double)rebound[0], (double)rebound[1]);
    TIDE_CHECK(rebound[0] < 0.1f && rebound[1] > 0.3f);

    // How fast bodies that overlap are pushed apart: two crates that start half in each other
    float apart[2];
    for (int round = 0; round < 2; round++) {
        start(&a, Case_Tuning);
        if (round) a.Box2D_Physics.contactSpeed = 0.5f;
        tick(&a, 8);
        apart[round] = body_of(&a, tide_has_Right).position.x - body_of(&a, tide_has_Left).position.x;
        tide_world_free(&a);
    }
    printf("    two crates half in each other are %g apart a tenth of a second later, and %g with a push speed of 0.5\n", (double)apart[0],
           (double)apart[1]);
    TIDE_CHECK(apart[0] > apart[1] + 0.1f && apart[1] > 0.5f && apart[1] < 0.65f);

    // How stiff contacts are, and how much they spring back: a heavy crate dropped on a light one squeezes it
    // into the floor, less the stiffer they are, and it comes back out faster the less they're damped
    float sunk[3];
    float sprung[3];
    for (int round = 0; round < 3; round++) {
        start(&a, Case_Tuning);
        a.Box2D_Physics.substeps = 8; // Room for stiffer than the usual, which is as stiff as four substeps take
        a.Box2D_Physics.contactStiffness = round == 0 ? 15.0f : 60.0f;
        a.Box2D_Physics.contactDamping = round == 2 ? 0.2f : 10.0f;
        sunk[round] = 1.0f;
        sprung[round] = 0.0f;
        for (int i = 0; i < 180; i++) {
            tide_world_tick(&a);
            const Box2D_Body bottom = body_of(&a, tide_has_Bottom);
            if (bottom.position.y < sunk[round]) sunk[round] = bottom.position.y;
            if (bottom.velocity.y > sprung[round]) sprung[round] = bottom.velocity.y;
        }
        tide_world_free(&a);
    }
    printf("    a crate under a heavy one sinks to %g at a stiffness of 15 and %g at 60, and springs back at %g m/s, or %g with a "
           "damping of 0.2\n",
           (double)sunk[0], (double)sunk[1], (double)sprung[1], (double)sprung[2]);
    TIDE_CHECK(sunk[0] < sunk[1] - 0.002f && sprung[2] > sprung[1] * 1.5f);

    // How readily a body sleeps is its own: of two crates that slide at 0.2 m/s, the one that sleeps under 1 m/s
    // stops where it is
    start(&a, Case_Tuning);
    tick(&a, 120);
    const Box2D_Body slider = body_of(&a, tide_has_Slider);
    const Box2D_Body drowsy = body_of(&a, tide_has_Drowsy);
    printf("    two seconds on, a crate sliding at 0.2 m/s is at %g, and one that sleeps under 1 m/s at %g\n", (double)slider.position.x,
           (double)drowsy.position.x);
    TIDE_CHECK(near(slider.position.x, 20.4f) && slider.velocity.x == 0.2f);
    TIDE_CHECK(drowsy.position.x < 20.25f && drowsy.velocity.x == 0.0f);
    tide_world_free(&a);
}

// A tower of thirty crates, one on another, that a heavy crate is dropped on:
// how many of them are off it fifteen seconds on, and when the first went.
static int falls(const float stiffness, const int substeps, int *first)
{
    enum { FLOORS = 30 };
    start(&a, Case_Tower);
    a.Kit.floors = FLOORS;
    a.Kit.drop = 3.0f;
    a.Box2D_Physics.contactStiffness = stiffness;
    a.Box2D_Physics.substeps = substeps;
    int off = 0;
    *first = -1;
    for (int ticks = 0; ticks < 900; ticks++) {
        tide_world_tick(&a);
        off = 0;
        for (uint32_t i = 0; i < a.entities.next_unused; i++) {
            const tide_entity e = tide_entity_in_slot(&a.entities, i);
            if (!tide_has_Storey(&a, e)) continue;
            const Box2D_Body body = tide_get_Box2D_Body(&a, e);
            if (tide_abs_f(body.position.x) > 0.75f || body.position.y < (float)tide_get_Storey(&a, e).floor) off++;
        }
        if (off > 0 && *first < 0) *first = ticks;
    }
    tide_world_free(&a);
    return off;
}

TIDE_TEST(box2d_a_tall_stack_stands_longer_with_stiffer_contacts)
{
    int soft_first;
    int usual_first;
    int stiff_first;
    int capped_first;
    const int soft = falls(15.0f, 4, &soft_first);
    const int usual = falls(30.0f, 4, &usual_first);
    const int stiff = falls(60.0f, 8, &stiff_first);
    printf("    of a tower of 30 crates that a heavy one is dropped on, %d are off it 15 s on at a stiffness of 15 (the first at tick "
           "%d), %d as the world comes, at 30 (%d), and %d at 60 with 8 substeps\n",
           soft, soft_first, usual, usual_first, stiff);
    TIDE_CHECK(soft > usual && usual > 0 && stiff == 0 && stiff_first < 0);
    TIDE_CHECK(soft_first < usual_first);
    // Contacts are no stiffer than the substeps take, an eighth of how many there are a second: at four substeps
    // of a tick of a sixtieth, 30, which is what the world comes with. Asking for more changes nothing
    TIDE_CHECK(falls(60.0f, 4, &capped_first) == usual && capped_first == usual_first);
}

#ifdef BOX2D_TEST_JOBS
// A system that asks runs on the tick's threads, each with its own question
// of the same world: the answers, and the match, are what one thread's are.
// Every tick here starts with none of C's worlds, so the first thread to ask
// has one made from the match's image, for all of them.
TIDE_TEST(box2d_answers_on_every_thread)
{
    const tide_jobs *jobs = tide_platform_jobs();
    if (!jobs) {
        printf("    one core: nothing to compare\n");
        return;
    }
    enum { TICKS = 60 };
    start(&a, Case_Crowd);
    tide_world_init(&b, DT);
    b.Kit.which = Case_Crowd;
    b2t_share_from(64); // So that the steps are shared out, and with them the tick's systems, on every platform
    // Nothing falls asleep here: a world at rest isn't stepped, a tick that shares no step out runs on one thread,
    // and no other thread would ever have anything to answer
    a.Box2D_Physics.sleeping = false;
    b.Box2D_Physics.sleeping = false;
    // Both on this thread first, until there's an image to ask of: this thread has asked from here on, so the
    // threads counted after it are other ones
    for (int i = 0; i < 3; i++) {
        tide_world_tick(&a);
        tide_world_tick(&b);
    }
    const int32_t asked_before = b2t_asking_threads();
    const int32_t restored_before = b2t_restores();
    // As many ticks as it takes for another thread to have answered, which is the threads' to say: a thread only
    // takes a task when it has nothing else
    int ticks = 0;
    for (; ticks < 20000 && (ticks < TICKS || b2t_asking_threads() == asked_before); ticks++) {
        b2t_forget();
        tide_world_tick(&a);
        b2t_forget();
        tide_world_tick_on(&b, jobs);
        if (tide_world_hash(&a) != tide_world_hash(&b)) printf("    tick %d came out different\n", ticks);
        TIDE_REQUIRE(tide_world_hash(&a) == tide_world_hash(&b));
    }
    int askers = 0;
    int answered = 0;
    for (uint32_t i = 0; i < b.entities.next_unused; i++) {
        const tide_entity e = tide_entity_in_slot(&b.entities, i);
        if (!tide_has_Asker(&b, e)) continue;
        askers++;
        const Asker asker = tide_get_Asker(&b, e);
        if (asker.under >= 0.0f && (tide_has_Asker(&b, asker.on) || tide_has_Floor(&b, asker.on))) answered++;
    }
    printf("    %d crates each asked what's under it for %d ticks, on %d threads beside this one (of %u in all), with Box2D's world "
           "made from the image %d times to answer\n",
           askers, ticks, (int)(b2t_asking_threads() - asked_before), (unsigned)jobs->threads, (int)(b2t_restores() - restored_before));
    TIDE_CHECK(askers == 1200 && answered == askers);
    TIDE_CHECK(b2t_asking_threads() > asked_before);
    TIDE_CHECK(b2t_restores() - restored_before == 2 * ticks); // Each match, each tick: once, whichever thread asked first
    b2t_share_from(0);
    tide_world_free(&a);
    tide_world_free(&b);
}
#endif
