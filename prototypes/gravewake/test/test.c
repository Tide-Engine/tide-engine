// GRAVEWAKE played with no window: two survivors pick theirs (a Gunner and a
// Breacher), hold the first wave round the fire with their guns, then take an
// item each from the three offered, and every kind of item in turn. Every
// survivor's every skill is used; the items that fight on their own (the
// drone, the ember ring) and the ones a hit or a kill sets off (Live Wire,
// Frostbite, the Mortar Beacon, the Powder Keg) are seen to. One goes down,
// fights on with the pistol, keeps everything and is raised; one gets
// straight back up with a Second Wind; one bleeds out and comes back at the
// wave's end with everything they had; then both hold the fire for waves,
// taking an item after each. What they press goes through the input, as a
// player's does; where they'd stand and look, the test puts them.
#include "gravewake_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "GRAVEWAKE check failed: %s (%s:%d), wave %d phase %d\n", #value, __FILE__, __LINE__, world.Match.round, (int)world.Match.phase); \
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

static void tick_safe(int count)
{
    for (int i = 0; i < count; i++) {
        heal();
        tick(1);
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

// An item or a survivor taken: 1, 2 or 3, pressed for a tick.
static void pick(int seat, int which)
{
    held[seat].pick1 = which == 1;
    held[seat].pick2 = which == 2;
    held[seat].pick3 = which == 3;
    tick_safe(1);
    held[seat].pick1 = held[seat].pick2 = held[seat].pick3 = false;
    tick_safe(1);
}

// Three items of the test's choosing, and the first taken.
static Survivor take(int seat, Item item)
{
    Survivor s = get(seat);
    s.offers.at[0] = item;
    s.offers.at[1] = s.offers.at[2] = Item_HollowPoints;
    s.choosing = true;
    set(seat, s);
    pick(seat, 1);
    CHECK(!get(seat).choosing);
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

// Ends the wave now: the dead all go.
static void end_wave(void)
{
    world.Match.toSpawn = 0;
    world.Match.culling = true;
    for (int i = 0; i < 60 * 3 && world.Match.phase != Phase_Break; i++) tick_safe(1);
    CHECK(world.Match.phase == Phase_Break);
}

// The next wave, once both have taken an item (the third).
static void next_wave(void)
{
    const int round = world.Match.round;
    for (int i = 0; i < 60 * 12 && world.Match.round == round; i++) {
        for (int seat = 0; seat < 2; seat++)
            if (get(seat).choosing) pick(seat, 3);
        tick_safe(1);
    }
    CHECK(world.Match.round == round + 1 && world.Match.phase == Phase_Fighting);
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
    tick_safe(8);
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

    // ---- The first wave: the dead rise out of the graves and the roots, and the Gunner shoots them down ----
    for (int i = 0; i < 60 * 15 && world.Match.phase != Phase_Fighting; i++) tick(1);
    CHECK(world.Match.phase == Phase_Fighting && world.Match.round == 1);
    tick_safe(60 * 6);
    CHECK(world.arch5_Zombie.count > 0);
    int fired = 0;
    for (int t = 0; t < 60 * 240 && world.Match.phase == Phase_Fighting; t++) {
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
        heal();
        tick(1);
    }
    held[0].attack = false;
    CHECK(world.Match.phase == Phase_Break && world.Match.round == 1);
    CHECK(fired > 0);
    CHECK(get(0).kills >= 1);
    CHECK(get(0).points > 0);
    CHECK(world.Match.level > 1 && get(0).level == world.Match.level && get(1).level == world.Match.level);
    printf("first wave: %d kills, a score of %d, the party at level %d\n", get(0).kills, get(0).points, world.Match.level);

    // ---- The offer: three items each, none twice; the next wave waits till both have taken one ----
    tick_safe(2);
    for (int seat = 0; seat < 2; seat++) {
        const Survivor s = get(seat);
        CHECK(s.choosing && s.dealt == 1);
        for (int k = 0; k < 3; k++) CHECK((int)s.offers.at[k] >= 0 && (int)s.offers.at[k] < 16);
        CHECK(s.offers.at[0] != s.offers.at[1] && s.offers.at[1] != s.offers.at[2] && s.offers.at[0] != s.offers.at[2]);
    }
    const Item offered = get(0).offers.at[0];
    pick(0, 1);
    CHECK(!get(0).choosing && get(0).items.at[offered] == 1 && get(0).picks == 1);
    tick_safe(60 * 3);
    CHECK(world.Match.phase == Phase_Break && world.Match.timer < 0.05f);     // Still waiting for the other
    pick(1, 3);
    CHECK(!get(1).choosing);
    for (int i = 0; i < 60 * 7 && world.Match.phase == Phase_Break; i++) tick_safe(1);
    CHECK(world.Match.phase == Phase_Fighting && world.Match.round == 2);

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

    // ---- Bled out: back at the fire when the wave is done, with everything they had ----
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
        end_wave();
        tick_safe(2);
        friend = get(1);
        CHECK(friend.life == Life_Standing && friend.health == max_health(friend));
        CHECK(friend.items.at[Item_HollowPoints] == 2 && friend.gun == Weapon_Shotgun && friend.mag == 6);
        CHECK(friend.choosing);
        next_wave();
        tick_safe(60 * 6);
    }

    // ---- The guns: a carbine's round, a shotgun's pellets, into one of the dead ----
    {
        const Weapon guns[2] = {Weapon_Rifle, Weapon_Shotgun};
        const Role roles[2] = {Role_Gunner, Role_Breacher};
        for (int k = 0; k < 2; k++) {
            become(0, roles[k], guns[k]);
            const tide_entity e = sturdy();
            face(0, e, 5);
            const float before = zombie(e).health;
            press(&held[0].attack);
            tick_safe(2);
            if (zombie(e).health >= before) fprintf(stderr, "the %s missed\n", k == 0 ? "carbine" : "shotgun");
            CHECK(zombie(e).health < before);
        }
    }

    // ---- Every skill ----
    {
        // The Gunner: a phase round, a slide, a frag grenade
        become(0, Role_Gunner, Weapon_Rifle);
        tide_entity e = sturdy();
        face(0, e, 12);
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
        face(0, e, 6);
        before = zombie(e).health;
        press(&held[0].special);
        for (int t = 0; t < 150 && zombie(e).health >= before; t++) tick_safe(1);
        CHECK(zombie(e).health < before && get(0).cooldowns.at[2] > 0);

        // The Breacher: a boot, a leap and its slam, a firebomb
        become(0, Role_Breacher, Weapon_Shotgun);
        const char *names[3] = {"boot", "leap", "firebomb"};
        const float nears[3] = {1.4f, 4.5f, 6};
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
            if (!struck) fprintf(stderr, "the %s didn't land\n", names[k]);
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

    // ---- Holding the fire: the dead come from every grave and every root, and every one of them has to find its way
    // to the survivors, or a wave would never end. Both stand by the fire and shoot the nearest, and take the third
    // item after each wave.
    {
        // A fresh wave: the dead made sturdy above go
        end_wave();
        next_wave();
        const int first = world.Match.round;
        const int last = first + 2;
        become(0, Role_Gunner, Weapon_Rifle);
        become(1, Role_Breacher, Weapon_Shotgun);
        Survivor a = get(0), b = get(1);
        a.feet = (tide_float3){-1.6f, 0, -3.1f};
        b.feet = (tide_float3){1.6f, 0, -3.1f};
        a.placed = b.placed = true;
        a.items.at[Item_HollowPoints] = b.items.at[Item_HollowPoints] = 6;       // So the waves go quickly
        set(0, a);
        set(1, b);
        int oldest = 0, taken = 0;
        for (int t = 0; t < 60 * 60 * 8 && world.Match.round <= last; t++) {
            for (int seat = 0; seat < 2; seat++) {
                Survivor s = get(seat);
                if (s.choosing) {
                    held[seat].pick3 = true;
                    taken++;
                }
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
        printf("held the fire to wave %d, %d items taken, level %d; the longest any of the dead hunted: %d s\n", world.Match.round, taken, world.Match.level, oldest);
        CHECK(world.Match.round > last);
        CHECK(taken >= 6);
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
