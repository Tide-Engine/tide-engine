// GRAVEWAKE played with no window: two survivors pick theirs (a Gunner and a
// Breacher), and once both have the run starts and never stops: the Gunner
// shoots the dead down, the party levels up, and each survivor is offered
// three items and takes one while the fighting goes on; then every kind of
// item in turn. Every survivor's every skill is used; the items that fight on
// their own (the drone, the ember ring) and the ones a hit or a kill sets off
// (Live Wire, Frostbite, the Mortar Beacon, the Powder Keg) are seen to. One
// goes down, fights on with the pistol, keeps everything and is raised; one
// gets straight back up with a Second Wind; one bleeds out and is back at the
// fire half a minute later with everything they had. A Brute claws up, slams,
// charges and roars, and felled gives both an item to pick. Then both hold the
// fire for minutes, every one of the dead finding its way to them. What they
// press goes through the input, as a player's does; where they'd stand and
// look, the test puts them.
#include "gravewake_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "GRAVEWAKE check failed: %s (%s:%d), %.0f s in, phase %d\n", #value, __FILE__, __LINE__, world.Match.clock, (int)world.Match.phase); \
    exit(1); } } while (0)

static tide_world world;
static Hands held[2];

static tide_entity survivor_of(int seat) { return world.Party.survivors.at[seat]; }
static Survivor get(int seat) { return tide_get_Survivor(&world, survivor_of(seat)); }
static void set(int seat, Survivor s) { CHECK(tide_set_Survivor(&world, survivor_of(seat), s)); }

static void tick(int count)
{
    for (int i = 0; i < count; i++) {
        for (int seat = 0; seat < 2; seat++) {
            tide_world_set_input(&world, tide_player_from_index(seat), held[seat]);
            if (seat == 0) tide_world_set_server_input(&world, held[seat]);
        }
        tide_world_tick(&world);
    }
}

static float max_health(Survivor s) { return 100.0f + 15.0f * (float)(s.level - 1) + 30.0f * (float)s.items.at[Item_Kevlar]; }

// Both on their feet and whole, so the dead don't end a test that's about something else.
static void heal(void)
{
    for (int seat = 0; seat < 2; seat++) {
        Survivor s = get(seat);
        if (s.life != Life_Standing) continue;
        s.health = max_health(s);
        set(seat, s);
    }
}

// Ticks with whoever's standing far too tough to go down (a crowd's claws can land together for more than anyone's
// health, and the run ends when nobody's standing), and whole after each.
static void tick_whole(int count)
{
    for (int i = 0; i < count; i++) {
        for (int seat = 0; seat < 2; seat++) {
            Survivor s = get(seat);
            if (s.life != Life_Standing) continue;
            s.health = 1e6f;
            set(seat, s);
        }
        tick(1);
        heal();
    }
}

// Every offer turned down and every pick owed forgotten, so the run doesn't wait on a choice nobody's making.
static void decline(void)
{
    for (int seat = 0; seat < 2; seat++) {
        Survivor s = get(seat);
        s.choosing = false;
        s.pending = 0;
        set(seat, s);
    }
}

// Whole ticks for checks that aren't about the items: what a level offers is turned down.
static void tick_safe(int count)
{
    for (int i = 0; i < count; i++) {
        tick_whole(1);
        decline();
    }
}

// Stand a survivor at a place, looking at a point.
static void put(int seat, tide_float3 feet, tide_float3 look)
{
    Survivor s = get(seat);
    s.feet = feet;
    s.velocity = (tide_float3){0};
    const float dx = look.x - feet.x, dz = look.z - feet.z, dy = look.y - (feet.y + 1.62f);
    s.yaw = atan2f(dx, dz);
    s.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
    s.placed = true;
    set(seat, s);
}

// Turn a survivor where they stand to look at a point.
static void look_at(int seat, tide_float3 look) { put(seat, get(seat).feet, look); }

// Turn a survivor so the over-the-shoulder camera's crosshair is on a point (hands.tide's ShoulderFrom, on open
// ground): the camera moves as they turn, so a few rounds of turning toward the point from where it is.
static void aim_over_shoulder(int seat, tide_float3 at)
{
    Survivor s = get(seat);
    const tide_float3 eye = {s.feet.x, s.feet.y + 1.62f, s.feet.z};
    for (int round = 0; round < 6; round++) {
        const float flat = cosf(s.pitch);
        const tide_float3 look = {sinf(s.yaw) * flat, sinf(s.pitch), cosf(s.yaw) * flat};
        const float rx = sinf(s.yaw + 1.5707964f), rz = cosf(s.yaw + 1.5707964f);
        const tide_float3 camera = {eye.x + rx * 0.7f - look.x * 3.2f, eye.y + 0.35f - look.y * 3.2f, eye.z + rz * 0.7f - look.z * 3.2f};
        const float dx = at.x - camera.x, dy = at.y - camera.y, dz = at.z - camera.z;
        s.yaw = atan2f(dx, dz);
        s.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
    }
    set(seat, s);
}

// An item or a survivor taken: 1, 2 or 3, pressed for a tick.
static void pick(int seat, int which)
{
    held[seat].pick1 = which == 1;
    held[seat].pick2 = which == 2;
    held[seat].pick3 = which == 3;
    tick_whole(1);
    held[seat].pick1 = held[seat].pick2 = held[seat].pick3 = false;
    tick_whole(1);
}

// Three items of the test's choosing, and the first taken (with no level's pick owed, so none is offered meanwhile).
static Survivor take(int seat, Item item)
{
    Survivor s = get(seat);
    const int had = s.items.at[item];
    s.offers.at[0] = item;
    s.offers.at[1] = s.offers.at[2] = Item_HollowPoints;
    s.choosing = true;
    s.pending = 0;
    set(seat, s);
    pick(seat, 1);
    CHECK(get(seat).items.at[item] == had + 1);
    return get(seat);
}

// A survivor made into another, their gun loaded and their skills ready (for the skills' tests).
static void become(int seat, Role role, Weapon gun)
{
    Survivor s = get(seat);
    s.role = role;
    s.gun = gun;
    s.mag = 30;
    s.act = Act_Idle;
    s.shotWait = 0;
    for (int k = 0; k < 3; k++) s.cooldowns.at[k] = 0;
    set(seat, s);
}

static bool solid_at(tide_float3 at, float room, int *which)
{
    for (int k = 0; k < tide_list_read_count(&world.heap, world.Ashwick.solids); k++) {
        const Solid solid = *(const Solid *)tide_list_read(&world.heap, world.Ashwick.solids, k, sizeof(Solid));
        if (fabsf(at.x - solid.center.x) < solid.half.x + room && fabsf(at.z - solid.center.z) < solid.half.z + room) {
            *which = k;
            return true;
        }
    }
    return false;
}

static bool walkable(tide_float3 at)
{
    const int cx = (int)floorf(at.x + 56.0f), cz = (int)floorf(at.z + 56.0f);
    if (cx < 0 || cz < 0 || cx >= 112 || cz >= 112) return false;
    return *(const uint8_t *)tide_list_read(&world.heap, world.Ashwick.cells, cz * 112 + cx, 1) == 0;
}

// Every one of the dead gone, for a fresh start (none come while they're going).
static void cull(void)
{
    world.Match.culling = true;
    for (int i = 0; i < 60 * 3 && world.Match.culling; i++) tick_safe(1);
    CHECK(!world.Match.culling && world.Match.alive == 0);
}

// Only a few of the dead about (`count` or so), and no more coming till spawning(): a round meant for one of them
// doesn't find another in the way.
static void few(int count)
{
    world.Match.spawnWait = 1e9f;
    cull();
    world.Match.spawnWait = 0;
    for (int i = 0; i < 60 * 5 && (int)world.Match.alive < count; i++) tick_safe(1);
    world.Match.spawnWait = 1e9f;
    tick_safe(60 * 2);     // Up out of the ground
}

// The dead coming again as the run has them.
static void spawning(void)
{
    world.Match.spawnWait = 0;
}

static int hunting_row(void)
{
    for (uint32_t r = 0; r < world.arch5_Zombie.count; r++)
        if (TIDE_AT(&world, arch5_Zombie, Zombie, r).state == ZombieState_Hunting) return (int)r;
    return -1;
}

// One of the dead on its feet, made to take any number of blows, and its entity.
static tide_entity sturdy(void)
{
    int row = -1;
    for (int t = 0; t < 60 * 60 && row < 0; t++) {
        row = hunting_row();
        if (row < 0) tick_safe(1);
    }
    CHECK(row >= 0);
    const tide_entity e = TIDE_ENTITY_AT(&world, arch5_Zombie, row);
    Zombie z = tide_get_Zombie(&world, e);
    z.health = 100000;
    CHECK(tide_set_Zombie(&world, e, z));
    return e;
}

static Zombie zombie(tide_entity e) { return tide_get_Zombie(&world, e); }

// Stand `near` metres in front of one of the dead, settled on the ground, looking at its chest as it stands now.
static void face(int seat, tide_entity e, float near)
{
    Zombie z = zombie(e);
    put(seat, (tide_float3){z.at.x + sinf(z.yaw) * near, z.at.y, z.at.z + cosf(z.yaw) * near}, z.chestAt);
    tick_safe(24);         // Settled on the ground there (it may be lower than the zombie's feet)
    look_at(seat, zombie(e).chestAt);
}

// A key held for a tick, then let go.
static void press(bool *key)
{
    *key = true;
    tick_safe(1);
    *key = false;
}

static int missiles_of(Shot kind)
{
    int count = 0;
    for (uint32_t row = 0; row < world.arch2_Missile.count; row++)
        if (TIDE_AT(&world, arch2_Missile, Missile, row).kind == kind) count++;
    return count;
}

int main(void)
{
    extern const unsigned char tide_game_pack[];
    extern const size_t tide_game_pack_size;
    CHECK(tide_assets_load(tide_game_pack, tide_game_pack_size));
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, tide_player_from_index(0));
    tide_world_player_joined(&world, tide_player_from_index(1));
    held[0].ready = held[1].ready = true;
    tick(2);
    CHECK(world.Party.joined == 2);
    CHECK(world.Ashwick.made);
    CHECK(get(0).role == Role_None && get(0).gun == Weapon_None);
    CHECK(get(0).points == 0);

    // Where the dead rise is clear of everything solid and on ground they can walk; so is where the survivors start
    const int risers = tide_list_read_count(&world.heap, world.Ashwick.risers);
    CHECK(risers >= 16);
    for (int r = 0; r < risers; r++) {
        const Riser riser = *(const Riser *)tide_list_read(&world.heap, world.Ashwick.risers, r, sizeof(Riser));
        int k = -1;
        if (solid_at(riser.at, 0.3f, &k)) fprintf(stderr, "riser %d at %.2f %.2f, in solid %d\n", r, riser.at.x, riser.at.z, k);
        CHECK(k < 0);
        if (!walkable(riser.at)) fprintf(stderr, "riser %d at %.2f %.2f %.2f (grave %d) on a cell the dead can't walk\n", r, riser.at.x, riser.at.y, riser.at.z, riser.grave);
        CHECK(walkable(riser.at));
    }
    for (int seat = 0; seat < 4; seat++) {
        const tide_float3 start = *(const tide_float3 *)tide_list_read(&world.heap, world.Ashwick.starts, seat, sizeof(tide_float3));
        int k = -1;
        CHECK(!solid_at(start, 0.35f, &k));
        CHECK(walkable(start));
    }

    // ---- The survivors: nothing starts till both have picked ----
    tick(60 * 4);
    CHECK(world.Match.phase == Phase_Waiting && get(0).role == Role_None);
    pick(0, 1);
    CHECK(get(0).role == Role_Gunner && get(0).gun == Weapon_Rifle && get(0).mag == 30);
    tick(60 * 4);
    CHECK(world.Match.phase == Phase_Waiting);
    pick(1, 2);
    CHECK(get(1).role == Role_Breacher && get(1).gun == Weapon_Shotgun && get(1).mag == 6);

    // ---- The run: a breath, then the dead, which the Gunner shoots down for a minute and a half ----
    for (int i = 0; i < 60 * 15 && world.Match.phase != Phase_Fighting; i++) tick(1);
    CHECK(world.Match.phase == Phase_Fighting && world.Match.clock < 0.1f);
    tick_safe(60 * 6);
    CHECK(world.arch5_Zombie.count > 0);
    int fired = 0;
    bool offered = false;
    for (int t = 0; t < 60 * 60 * 5 && world.Match.clock < 90; t++) {
        float nearest = 1e9f;
        Zombie target = {0};
        bool found = false;
        for (uint32_t row = 0; row < world.arch5_Zombie.count; row++) {
            const Zombie z = TIDE_AT(&world, arch5_Zombie, Zombie, row);
            if (z.state != ZombieState_Hunting) continue;
            const float d = fabsf(z.at.x - get(0).feet.x) + fabsf(z.at.z - get(0).feet.z);
            if (d < nearest) { nearest = d; target = z; found = true; }
        }
        if (found && t % 12 == 0) put(0, (tide_float3){target.at.x - sinf(target.yaw) * -6.0f, target.at.y, target.at.z - cosf(target.yaw) * -6.0f}, target.chestAt);
        else if (found) look_at(0, target.chestAt);
        held[0].attack = found;
        if (found && get(0).shotWait <= 0 && get(0).mag > 0) fired++;
        // Each level's offer taken, the third item, pressed and let go
        for (int seat = 0; seat < 2; seat++) {
            if (get(seat).choosing) offered = true;
            held[seat].pick3 = get(seat).choosing && t % 2 == 0;
        }
        heal();
        tick(1);
    }
    held[0].attack = held[0].pick3 = held[1].pick3 = false;
    tick_whole(1);
    CHECK(world.Match.phase == Phase_Fighting && world.Match.clock >= 90);
    CHECK(world.Match.danger > 1.9f);
    CHECK(fired > 0);
    CHECK(get(0).kills >= 1);
    CHECK(get(0).points > 0);
    CHECK(world.Match.level > 1 && get(0).level == world.Match.level && get(1).level == world.Match.level);
    printf("a minute and a half: %d kills, a score of %d, the party at level %d, danger %.2f\n", get(0).kills, get(0).points, world.Match.level, world.Match.danger);

    // ---- A level's offer: three items, none twice; the run waits, the clock and the dead still, till both have taken
    // one; the next owed is offered then ----
    CHECK(offered);
    decline();
    tick_whole(2);
    world.Match.level += 1;
    tick_whole(2);
    {
        CHECK(world.Match.paused);
        const float clock = world.Match.clock;
        const int row = hunting_row();
        CHECK(row >= 0);
        const tide_entity e = TIDE_ENTITY_AT(&world, arch5_Zombie, row);
        const tide_float3 at = tide_get_Zombie(&world, e).at;
        tick_whole(60);
        CHECK(world.Match.clock == clock && world.Match.paused);
        CHECK(tide_get_Zombie(&world, e).at.x == at.x && tide_get_Zombie(&world, e).at.z == at.z);
    }
    for (int seat = 0; seat < 2; seat++) {
        const Survivor s = get(seat);
        CHECK(s.choosing);
        for (int k = 0; k < 3; k++) CHECK((int)s.offers.at[k] >= 0 && (int)s.offers.at[k] < 16);
        CHECK(s.offers.at[0] != s.offers.at[1] && s.offers.at[1] != s.offers.at[2] && s.offers.at[0] != s.offers.at[2]);
    }
    {
        const Item first = get(0).offers.at[0];
        const int owed = get(0).pending;
        const int had = get(0).items.at[first];
        const int picks = get(0).picks;
        const float clock = world.Match.clock;
        pick(0, 1);
        CHECK(get(0).items.at[first] == had + 1 && get(0).picks == picks + 1);
        CHECK(owed == 0 ? !get(0).choosing : get(0).choosing && get(0).pending == owed - 1);
        // Still waiting for the other
        CHECK(world.Match.paused && world.Match.clock == clock);
        decline();
        tick_whole(30);
        CHECK(!world.Match.paused && world.Match.clock > clock);
    }

    // ---- Every item ----
    {
        Survivor s = get(0);
        memset(&s.items, 0, sizeof s.items);
        s.selfRevives = 0;
        set(0, s);
        s = take(0, Item_Kevlar);
        CHECK(s.items.at[Item_Kevlar] == 1 && s.health == max_health(s) && max_health(s) == 130.0f + 15.0f * (float)(s.level - 1));
        s = take(0, Item_ExtendedMag);
        CHECK(s.mag == 45);                             // Half again a carbine's 30, loaded now
        s = take(0, Item_SecondWind);
        CHECK(s.selfRevives == 1);
        const Item rest[] = {Item_HollowPoints, Item_HairTrigger, Item_Sneakers, Item_Leech, Item_Battery, Item_LuckyShot, Item_LiveWire,
                             Item_PowderKeg, Item_GunDrone, Item_EmberRing, Item_Frostbite, Item_SpringBoots, Item_Mortar};
        for (size_t k = 0; k < sizeof rest / sizeof rest[0]; k++) {
            const int before = get(0).items.at[rest[k]];
            CHECK(take(0, rest[k]).items.at[rest[k]] == before + 1);
        }
        // Field Batteries: skills come back sooner
        become(0, Role_Gunner, Weapon_Rifle);
        put(0, (tide_float3){0, 0, -6}, (tide_float3){0, 1.6f, -12});
        tick_safe(8);
        press(&held[0].utility);
        CHECK(get(0).cooldowns.at[1] > 3.3f && get(0).cooldowns.at[1] < 3.45f);     // A slide's 4 s, 15% sooner
        // Spring Boots: a jump in the air
        tick_safe(30);
        press(&held[0].jump);
        tick_safe(12);
        const float up = get(0).feet.y;
        press(&held[0].jump);
        tick_safe(8);
        CHECK(get(0).airJumps == 1 && get(0).feet.y > up);
        tick_safe(60);
        s = get(0);
        memset(&s.items, 0, sizeof s.items);
        s.selfRevives = 0;
        set(0, s);
    }

    // ---- A Second Wind: down, and straight back up ----
    {
        Survivor s = take(0, Item_SecondWind);
        const tide_entity e = sturdy();
        for (int t = 0; t < 60 * 30 && get(0).items.at[Item_SecondWind] > 0; t++) {
            const Zombie z = zombie(e);
            s = get(0);
            s.health = 1;
            s.feet = (tide_float3){z.at.x + sinf(z.yaw) * 0.8f, z.at.y, z.at.z + cosf(z.yaw) * 0.8f};
            s.placed = true;
            set(0, s);
            Survivor other = get(1);
            other.health = max_health(other);
            set(1, other);
            tick(1);
        }
        CHECK(get(0).items.at[Item_SecondWind] == 0 && get(0).selfRevives == 0);
        CHECK(get(0).life == Life_Standing && get(0).gun == Weapon_Rifle);
    }

    // ---- Down, and raised: one of the dead puts the Breacher down, who fights on with the pistol and keeps their
    // items; raised, the shotgun's back in hand as loaded as it was ----
    {
        Survivor friend = get(1);
        friend.items.at[Item_HollowPoints] = 2;
        friend.mag = 4;
        friend.act = Act_Idle;
        set(1, friend);
        const tide_entity e = sturdy();
        for (int t = 0; t < 60 * 60 && get(1).life == Life_Standing; t++) {
            const Zombie z = zombie(e);
            friend = get(1);
            friend.health = 1;
            friend.feet = (tide_float3){z.at.x + sinf(z.yaw) * 0.8f, z.at.y, z.at.z + cosf(z.yaw) * 0.8f};
            friend.placed = true;
            set(1, friend);
            put(0, (tide_float3){z.at.x + 9, z.at.y, z.at.z}, (tide_float3){z.at.x + 12, z.at.y + 1.6f, z.at.z});
            Survivor me = get(0);
            me.health = max_health(me);
            set(0, me);
            tick(1);
        }
        friend = get(1);
        CHECK(friend.life == Life_Downed);
        CHECK(friend.gun == Weapon_Pistol && friend.mag == 12 && friend.stowed == 4);
        CHECK(friend.items.at[Item_HollowPoints] == 2);
        // The pistol fires, down
        held[1].attack = true;
        tick(30);
        held[1].attack = false;
        CHECK(get(1).mag < 12);
        friend = get(1);
        friend.feet = (tide_float3){-1, 0, -5};
        friend.placed = true;
        set(1, friend);
        put(0, (tide_float3){-1, 0, -6.2f}, (tide_float3){-1, 0.3f, -5});
        tick(2);
        CHECK(get(0).aimed == 1);
        held[0].use = true;
        for (int i = 0; i < 60 * 4 && get(1).life != Life_Standing; i++) {
            Survivor me = get(0);
            me.health = max_health(me);
            set(0, me);
            tick(1);
        }
        held[0].use = false;
        CHECK(get(1).life == Life_Standing);
        CHECK(get(0).revives == 1);
        CHECK(get(1).gun == Weapon_Shotgun && get(1).mag == 4);
    }

    // ---- Bled out: back at the fire half a minute later, with everything they had ----
    {
        Survivor friend = get(1);
        friend.life = Life_Downed;
        friend.bleed = 0.05f;
        friend.health = 0;
        friend.stowed = friend.mag;
        friend.gun = Weapon_Pistol;
        set(1, friend);
        tick(10);
        CHECK(get(1).life == Life_Gone);
        tick_safe(60 * 25);
        CHECK(get(1).life == Life_Gone);
        tick_safe(60 * 6);
        friend = get(1);
        CHECK(friend.life == Life_Standing && friend.gone == 0);
        CHECK(friend.items.at[Item_HollowPoints] >= 2 && friend.gun == Weapon_Shotgun && friend.mag >= 6);
        tick_safe(60 * 2);
    }

    // ---- The guns: a carbine's round, a shotgun's pellets, into one of the dead; and the carbine again from over the
    // shoulder, the round going where the camera's crosshair is ----
    few(3);
    {
        const Weapon guns[3] = {Weapon_Rifle, Weapon_Shotgun, Weapon_Rifle};
        const Role roles[3] = {Role_Gunner, Role_Breacher, Role_Gunner};
        for (int k = 0; k < 3; k++) {
            become(0, roles[k], guns[k]);
            held[0].third = k == 2;
            const tide_entity e = sturdy();
            face(0, e, 6);
            if (k == 2) aim_over_shoulder(0, zombie(e).chestAt);
            const float before = zombie(e).health;
            press(&held[0].attack);
            tick_safe(2);
            if (zombie(e).health >= before) fprintf(stderr, "the %s missed\n", k == 0 ? "carbine" : k == 1 ? "shotgun" : "carbine from over the shoulder");
            CHECK(zombie(e).health < before);
        }
        held[0].third = false;
    }

    // ---- Every skill ----
    few(4);
    {
        // The Gunner: a phase round, a slide, a frag grenade
        become(0, Role_Gunner, Weapon_Rifle);
        tide_entity e = sturdy();
        face(0, e, 6);
        float before = zombie(e).health;
        press(&held[0].secondary);
        tick_safe(2);
        CHECK(zombie(e).health < before && get(0).cooldowns.at[0] > 0);
        put(0, (tide_float3){0, 0, -6}, (tide_float3){0, 1.6f, -12});
        tick_safe(8);
        tide_float3 was = get(0).feet;
        held[0].move = (tide_float2){0, 1};
        press(&held[0].utility);
        held[0].move = (tide_float2){0, 0};
        tick_safe(12);
        CHECK(hypotf(get(0).feet.x - was.x, get(0).feet.z - was.z) > 3 && get(0).cooldowns.at[1] > 0);
        e = sturdy();
        face(0, e, 3);           // Well within the blast however far the throw carries
        before = zombie(e).health;
        press(&held[0].special);
        for (int t = 0; t < 150 && zombie(e).health >= before; t++) tick_safe(1);
        CHECK(zombie(e).health < before && get(0).cooldowns.at[2] > 0);

        // The Breacher: a boot, a leap and its slam, a firebomb
        become(0, Role_Breacher, Weapon_Shotgun);
        const char *names[3] = {"boot", "leap", "firebomb"};
        const float nears[3] = {1.4f, 7.0f, 6};
        for (int k = 0; k < 3; k++) {
            e = sturdy();
            face(0, e, nears[k]);
            before = zombie(e).health;
            bool *keys[3] = {&held[0].secondary, &held[0].utility, &held[0].special};
            press(keys[k]);
            bool struck = false;
            for (int t = 0; t < 150 && !struck; t++) {
                tick_safe(1);
                const Zombie now = zombie(e);
                struck = k == 2 ? now.burn > 0 : now.state == ZombieState_Fallen;
            }
            if (!struck)
                fprintf(stderr, "the %s didn't land: at %.1f %.1f %.1f, the zombie at %.1f %.1f %.1f state %d, cooldown %.2f, leaping %d, grounded %d\n", names[k], get(0).feet.x,
                        get(0).feet.y, get(0).feet.z, zombie(e).at.x, zombie(e).at.y, zombie(e).at.z, (int)zombie(e).state, get(0).cooldowns.at[k], get(0).leaping,
                        get(0).grounded);
            CHECK(struck);
            CHECK(get(0).cooldowns.at[k] > 0);
        }
        // The boot again, at one already down: stamped on
        {
            Survivor s = get(0);
            s.cooldowns.at[0] = 0;
            set(0, s);
            const Zombie z = zombie(e);
            if (z.state == ZombieState_Fallen) {
                put(0, z.at, z.headAt);
                before = z.health;
                press(&held[0].secondary);
                tick_safe(20);
                CHECK(zombie(e).health < before - 100);
            }
        }

        // The Engineer: a mine, a blink, a turret
        become(0, Role_Engineer, Weapon_Smg);
        e = sturdy();
        face(0, e, 3);
        const int mines = missiles_of(Shot_Mine);
        press(&held[0].secondary);
        CHECK(missiles_of(Shot_Mine) == mines + 1);
        // The dead come to it, and it bursts
        before = zombie(e).health;
        for (int t = 0; t < 60 * 10 && missiles_of(Shot_Mine) > mines; t++) tick_safe(1);
        CHECK(missiles_of(Shot_Mine) == mines);
        put(0, (tide_float3){0, 0, -6}, (tide_float3){0, 1.6f, -12});
        tick_safe(8);
        was = get(0).feet;
        press(&held[0].utility);
        tick_safe(2);
        CHECK(hypotf(get(0).feet.x - was.x, get(0).feet.z - was.z) > 3);
        e = sturdy();
        face(0, e, 4);
        before = zombie(e).health;
        press(&held[0].special);
        CHECK(world.arch3_Turret.count == 1);
        for (int t = 0; t < 60 * 5 && zombie(e).health >= before; t++) tick_safe(1);
        CHECK(zombie(e).health < before);
        tick_safe(60 * 16);
        CHECK(world.arch3_Turret.count == 0);           // Its time up
    }

    // (A crowd, for what sets off on the dead near the one hit)
    spawning();
    tick_safe(60 * 10);

    // ---- What the items set off: Frostbite freezes, Live Wire arcs, the Mortar Beacon calls a shell, a Powder Keg
    // bursts; the drone and the ember ring need no aim ----
    {
        become(0, Role_Gunner, Weapon_Pistol);
        Survivor s = get(0);
        memset(&s.items, 0, sizeof s.items);
        s.items.at[Item_Frostbite] = 13;              // Always
        s.items.at[Item_Mortar] = 10;                 // Always
        s.items.at[Item_LiveWire] = 1;
        set(0, s);
        // A round into the crowd: whichever of the dead it meets first freezes, and a shell comes down on it
        tide_entity e = sturdy();
        bool froze = false, called = false;
        for (int shot = 0; shot < 10 && !(froze && called); shot++) {
            e = sturdy();
            face(0, e, 5);
            const int shells = missiles_of(Shot_Shell);
            s = get(0);
            s.shotWait = 0;
            s.mag = 12;
            set(0, s);
            press(&held[0].attack);
            tick_safe(2);
            for (uint32_t row = 0; row < world.arch5_Zombie.count; row++)
                if (TIDE_AT(&world, arch5_Zombie, Zombie, row).frozen > 0) froze = true;
            called = missiles_of(Shot_Shell) > shells;
        }
        CHECK(froze);
        CHECK(called);
        // Live Wire: some of the dead near the one shot are struck too, sooner or later
        bool arced = false;
        for (int shot = 0; shot < 60 && !arced; shot++) {
            for (uint32_t row = 0; row < world.arch5_Zombie.count; row++) {
                const tide_entity other = TIDE_ENTITY_AT(&world, arch5_Zombie, row);
                Zombie z = tide_get_Zombie(&world, other);
                if (other.index == e.index || z.state == ZombieState_Dying) continue;
                z.health = 100000;
                CHECK(tide_set_Zombie(&world, other, z));
            }
            look_at(0, zombie(e).chestAt);
            s = get(0);
            s.shotWait = 0;
            s.mag = 12;
            set(0, s);
            press(&held[0].attack);
            tick_safe(1);
            for (uint32_t row = 0; row < world.arch5_Zombie.count; row++) {
                const Zombie z = TIDE_AT(&world, arch5_Zombie, Zombie, row);
                if (TIDE_ENTITY_AT(&world, arch5_Zombie, row).index != e.index && z.state != ZombieState_Dying && z.health < 100000) arced = true;
            }
        }
        CHECK(arced);
        // A Powder Keg: a kill bursts, and the dead near it are burned
        s = get(0);
        memset(&s.items, 0, sizeof s.items);
        s.items.at[Item_PowderKeg] = 3;
        set(0, s);
        bool burst = false;
        for (int tries = 0; tries < 40 && !burst; tries++) {
            // One of the dead with another close by
            int a = -1, b = -1;
            for (uint32_t i = 0; i < world.arch5_Zombie.count && a < 0; i++) {
                const Zombie zi = TIDE_AT(&world, arch5_Zombie, Zombie, i);
                if (zi.state != ZombieState_Hunting) continue;
                for (uint32_t j = 0; j < world.arch5_Zombie.count; j++) {
                    const Zombie zj = TIDE_AT(&world, arch5_Zombie, Zombie, j);
                    if (i == j || zj.state != ZombieState_Hunting) continue;
                    if (hypotf(zi.at.x - zj.at.x, zi.at.z - zj.at.z) < 2.5f) { a = (int)i; b = (int)j; break; }
                }
            }
            if (a < 0) { tick_safe(30); continue; }
            const tide_entity ea = TIDE_ENTITY_AT(&world, arch5_Zombie, a), eb = TIDE_ENTITY_AT(&world, arch5_Zombie, b);
            Zombie za = zombie(ea), zb = zombie(eb);
            za.health = 1;
            zb.health = 100000;
            CHECK(tide_set_Zombie(&world, ea, za));
            CHECK(tide_set_Zombie(&world, eb, zb));
            face(0, ea, 5);
            s = get(0);
            s.shotWait = 0;
            s.mag = 12;
            set(0, s);
            press(&held[0].attack);
            tick_safe(3);
            if (zombie(ea).state == ZombieState_Dying) burst = zombie(eb).health < 100000;
        }
        CHECK(burst);
        // The drone and the ember ring: one of the dead near, never shot
        few(1);
        s = get(0);
        memset(&s.items, 0, sizeof s.items);
        s.items.at[Item_GunDrone] = 1;
        set(0, s);
        e = sturdy();
        face(0, e, 6);
        float before = zombie(e).health;
        for (int t = 0; t < 60 * 3 && zombie(e).health >= before; t++) tick_safe(1);
        CHECK(zombie(e).health < before);
        s = get(0);
        s.items.at[Item_GunDrone] = 0;
        s.items.at[Item_EmberRing] = 1;
        set(0, s);
        face(0, e, 1.5f);
        before = zombie(e).health;
        for (int t = 0; t < 60 * 3 && zombie(e).health >= before; t++) {
            look_at(0, (tide_float3){0, 0, 0});
            tick_safe(1);
        }
        CHECK(zombie(e).health < before);
        s = get(0);
        memset(&s.items, 0, sizeof s.items);
        set(0, s);
    }

    // ---- A Brute: it claws up, slams the survivor close to it, charges the one further off, roars up the dead; felled,
    // both have an item to pick ----
    {
        cull();
        spawning();
        world.Match.bossNext = world.Match.clock;
        for (int t = 0; t < 60 * 2 && world.arch4_Boss.count == 0; t++) tick_safe(1);
        CHECK(world.arch4_Boss.count == 1);
        const tide_entity e = TIDE_ENTITY_AT(&world, arch4_Boss, 0);
        CHECK(tide_get_Boss(&world, e).health > 1000);
        tick_safe(60 * 3);     // Up out of the ground
        CHECK(tide_get_Boss(&world, e).move != BossMove_Coming && tide_get_Boss(&world, e).move != BossMove_Dying);
        bool slammed = false, charged = false, roared = false;
        for (int t = 0; t < 60 * 40 && !(slammed && charged && roared); t++) {
            const uint32_t before = world.arch5_Zombie.count;
            const Boss b = tide_get_Boss(&world, e);
            // One survivor close, the other further off, both kept whole and on their feet
            for (int seat = 0; seat < 2; seat++) {
                Survivor s = get(seat);
                if (t % 60 == 0) {
                    const float off = seat == 0 && !slammed ? 4.0f : 15.0f;      // Close for a slam, then both off for a charge
                    s.feet = (tide_float3){b.at.x + (seat == 0 ? off : -off), b.at.y, b.at.z};
                    s.feet.y = 0;
                    s.placed = true;
                }
                s.health = 1e6f;
                s.life = Life_Standing;
                set(seat, s);
            }
            decline();
            const float was0 = get(0).health, was1 = get(1).health;
            tick(1);
            if (b.move == BossMove_SlamWindup && get(0).health < was0) slammed = true;
            if (b.move == BossMove_Charge && (get(0).health < was0 || get(1).health < was1)) charged = true;
            if (b.move == BossMove_Roar && world.arch5_Zombie.count > before + 10) roared = true;
        }
        printf("the Brute: slammed %d, charged %d, roared %d\n", slammed, charged, roared);
        CHECK(slammed && charged && roared);
        // Felled
        decline();
        const int picks0 = 0, picks1 = 0;
        Boss b = tide_get_Boss(&world, e);
        b.health = 1;
        CHECK(tide_set_Boss(&world, e, b));
        become(0, Role_Gunner, Weapon_Rifle);
        bool felled = false;
        for (int t = 0; t < 60 * 5 && !felled; t++) {
            b = tide_get_Boss(&world, e);
            put(0, (tide_float3){b.at.x, 0, b.at.z - 8}, (tide_float3){b.at.x, b.at.y + 3, b.at.z});
            Survivor s = get(0);
            s.shotWait = 0;
            s.mag = 30;
            set(0, s);
            held[0].attack = true;
            tick_whole(1);
            held[0].attack = false;
            tick_whole(1);
            felled = tide_get_Boss(&world, e).move == BossMove_Dying;
        }
        CHECK(felled);
        tick_whole(2);
        CHECK(get(0).pending + (get(0).choosing ? 1 : 0) >= picks0 + 1);
        CHECK(get(1).pending + (get(1).choosing ? 1 : 0) >= picks1 + 1);
        tick_safe(60 * 6);
        CHECK(world.arch4_Boss.count == 0);
    }

    // ---- Holding the fire: the dead come from every side, and every one of them has to find its way to the survivors.
    // Both stand by the fire and shoot the nearest for three minutes, and take the third item at each level.
    {
        cull();
        spawning();
        world.Match.bossNext = 1e9f;
        const float last = world.Match.clock + 180;
        become(0, Role_Gunner, Weapon_Rifle);
        become(1, Role_Breacher, Weapon_Shotgun);
        Survivor a = get(0), b = get(1);
        a.feet = (tide_float3){-1.6f, 0, -3.1f};
        b.feet = (tide_float3){1.6f, 0, -3.1f};
        a.placed = b.placed = true;
        a.items.at[Item_HollowPoints] = b.items.at[Item_HollowPoints] = 6;       // So the waves go quickly
        set(0, a);
        set(1, b);
        int oldest = 0;
        const int picked = get(0).picks + get(1).picks;
        for (int t = 0; t < 60 * 60 * 8 && world.Match.clock < last; t++) {
            for (int seat = 0; seat < 2; seat++) {
                Survivor s = get(seat);
                // The third item, pressed and let go
                held[seat].pick3 = s.choosing && t % 2 == 0;
                // Each faces the nearest of the dead and shoots when one is near
                float nearest = 1e9f;
                tide_float3 at = {0};
                for (uint32_t row = 0; row < world.arch5_Zombie.count; row++) {
                    const Zombie z = TIDE_AT(&world, arch5_Zombie, Zombie, row);
                    if (z.state != ZombieState_Hunting) continue;
                    const float dx = z.at.x - s.feet.x, dz = z.at.z - s.feet.z;
                    const float d = sqrtf(dx * dx + dz * dz);
                    if (d < nearest) { nearest = d; at = z.chestAt; }
                }
                if (nearest < 20.0f) look_at(seat, at);
                held[seat].attack = nearest < 20.0f;
            }
            // How long the oldest of the dead has been about
            for (uint32_t row = 0; row < world.arch5_Zombie.count; row++) {
                const Zombie z = TIDE_AT(&world, arch5_Zombie, Zombie, row);
                if (z.state == ZombieState_Hunting && (int)z.timer > oldest) oldest = (int)z.timer;
            }
            // A crowd's claws can land together for more than anyone's health: kept whole, as this is about the dead
            for (int seat = 0; seat < 2; seat++) {
                Survivor s = get(seat);
                s.health = 1e6f;
                set(seat, s);
            }
            tick(1);
            held[0].pick3 = held[1].pick3 = false;
        }
        held[0].attack = held[1].attack = false;
        for (uint32_t row = 0; row < world.arch5_Zombie.count; row++) {
            const Zombie z = TIDE_AT(&world, arch5_Zombie, Zombie, row);
            if (z.state != ZombieState_Hunting || z.timer < 120) continue;
            const int cx = (int)floorf(z.at.x + 56.0f), cz = (int)floorf(z.at.z + 56.0f);
            const int field = *(const uint8_t *)tide_list_read(&world.heap, world.Ashwick.field, cz * 112 + cx, 1);
            fprintf(stderr, "stuck for %.0f s at %.2f %.2f %.2f, walkable %d, field %d\n", z.timer, z.at.x, z.at.y, z.at.z, walkable(z.at), field);
        }
        const int taken = get(0).picks + get(1).picks - picked;
        printf("held the fire to %.0f s, %d items taken, level %d, %d of the dead up; the longest any hunted: %d s\n", world.Match.clock, taken, world.Match.level,
               world.Match.alive, oldest);
        CHECK(world.Match.clock >= last && world.Match.phase == Phase_Fighting);
        CHECK(taken >= 4);
        CHECK(oldest < 180);
    }

    // ---- The match is the same from a snapshot, and through its packed bytes ----
    tide_world saved = {0}, packed = {0};
    tide_world_copy(&saved, &world);
    tick(120);
    const uint64_t expected = tide_world_hash(&world);
    tide_world_copy(&world, &saved);
    tick(120);
    CHECK(tide_world_hash(&world) == expected);
    const uint32_t size = tide_world_pack(&world, NULL, 0);
    uint8_t *bytes = malloc(size);
    CHECK(bytes != NULL && tide_world_pack(&world, bytes, size) == size);
    CHECK(tide_world_unpack(&packed, bytes, size) && tide_world_hash(&packed) == expected);
    free(bytes);
    tide_world_free(&packed);
    tide_world_free(&saved);
    tide_world_free(&world);
    puts("GRAVEWAKE simulation checks passed");
    return 0;
}
