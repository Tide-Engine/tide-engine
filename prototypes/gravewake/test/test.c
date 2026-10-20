// GRAVEWAKE played with no window: two survivors pick theirs (a Gunner and a
// Breacher), and once both have the first night falls, on its clock, and its
// chests are hidden about. The Gunner shoots the dead
// down, the party levels up, and each survivor is offered three items and
// takes one while the run waits; then every kind of item in turn. Every
// survivor's every skill is used; the items that fight on their own (the
// drone, the ember ring, the Engineer's turret with its maker's items) and the
// ones a hit, a kill or a dash sets off are seen to. One goes down, fights on
// with the pistol, keeps everything and is raised; one gets straight back up
// with a Second Wind. A Spitter spits, a Warden's mail turns a round, a
// Bloater bursts. The Mauler claws up, slams, charges and stands spent, a round
// to the sores on its back doing three times what one to its hips does, and
// felled gives both an item to pick; the Broodmother lobs bile and births
// runners; the Stalker leaps, shrieks, and is cut short. Both hold the fire for minutes, every one of the dead
// finding its way to them. A plague pit's walked round and shot over; the
// ground erupts under the dead and a survivor, and burns on. Then a night is
// kept: one bleeds out and stays gone; the night's clock runs out, the
// wake-fire's ring rises round them (nobody walks out of it), its boss claws
// up in it and the one who bled out is back; its boss down, dawn burns the
// dead away; and the next night falls, its clock from the start, with new
// chests, more dangerous; one who bleeds out then is back a minute later. What they press
// goes through the input, as a player's does; where they'd stand and look, the
// test puts them.
#include "gravewake_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "GRAVEWAKE check failed: %s (%s:%d), %.0f s in, phase %d\n", #value, __FILE__, __LINE__, world.Match.clock, (int)world.Match.phase); \
    exit(1); } } while (0)

// The navigation grid's: its west and south edges are NAV_HALF metres from the fire, NAV_SIDE cells of a metre across
// (ashwick.tide's NAV_X0, NAV_W).
#define NAV_HALF 78.0f
#define NAV_SIDE 156

static tide_world world;
static Hands held[2];
static bool autos;          // The special left to use itself (else held off: it'd come into checks about other things)
static bool missions;       // ...and the missions left to come
static bool timed;          // ...and the night's end left to come (else its clock's held 100 s short of it)

static tide_entity survivor_of(int seat) { return world.Party.survivors.at[seat]; }
static Survivor get(int seat) { return tide_get_Survivor(&world, survivor_of(seat)); }
static void set(int seat, Survivor s) { CHECK(tide_set_Survivor(&world, survivor_of(seat), s)); }

static void tick(int count)
{
    for (int i = 0; i < count; i++) {
        for (int seat = 0; seat < 2 && !autos; seat++) {
            if (!world.Party.taken.at[seat]) continue;
            Survivor s = get(seat);
            if (s.cooldowns.at[2] >= 50) continue;
            s.cooldowns.at[2] = 100;
            set(seat, s);
        }
        if (!missions && world.Match.mission == MissionKind_None) world.Match.missionWait = 1e9f;
        if (!timed && !world.Match.reckoning && world.Match.nightClock > world.Match.nightEnds - 100) world.Match.nightClock = world.Match.nightEnds - 100;
        for (int seat = 0; seat < 2; seat++) {
            tide_world_set_input(&world, tide_player_from_index(seat), held[seat]);
            if (seat == 0) tide_world_set_server_input(&world, held[seat]);
        }
        tide_world_tick(&world);
    }
}

static float max_health(Survivor s) { return 100.0f + 18.0f * (float)(s.level - 1) + 45.0f * (float)s.items.at[Item_Kevlar]; }

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
// health, and the run ends when nobody's standing), and whole after each. (Their health held off coming back meanwhile:
// it comes back no higher than whole, which had made them whole, not tough, in the tick before the blows.)
static void tick_whole(int count)
{
    for (int i = 0; i < count; i++) {
        for (int seat = 0; seat < 2; seat++) {
            Survivor s = get(seat);
            if (s.life != Life_Standing) continue;
            s.health = 1e6f;
            s.calm = 0;
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

// Stand a survivor at a place, the cursor on a point (they turn to it, and their shots and throws go to it).
static void put(int seat, tide_float3 feet, tide_float3 look)
{
    Survivor s = get(seat);
    s.feet = feet;
    s.velocity = (tide_float3){0};
    const float dx = look.x - feet.x, dz = look.z - feet.z, dy = look.y - (feet.y + 1.36f);
    s.yaw = atan2f(dx, dz);
    s.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
    s.aim = look;
    s.placed = true;
    set(seat, s);
    held[seat].aim = look;
}

// The cursor on a point, where a survivor stands.
static void look_at(int seat, tide_float3 look) { put(seat, get(seat).feet, look); }

// The skills that use themselves held back (`slot` -1), or one of them let go (0 the secondary, 2 the special).
static void only_skill(int seat, int slot)
{
    Survivor s = get(seat);
    s.cooldowns.at[0] = slot == 0 ? 0 : 100;
    s.cooldowns.at[2] = slot == 2 ? 0 : 100;
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
    s.rolls = 2;
    s.rollWait = 0;
    s.secondaryRank = 1;
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
    const int cx = (int)floorf(at.x + NAV_HALF), cz = (int)floorf(at.z + NAV_HALF);
    if (cx < 0 || cz < 0 || cx >= NAV_SIDE || cz >= NAV_SIDE) return false;
    return *(const uint8_t *)tide_list_read(&world.heap, world.Ashwick.cells, cz * NAV_SIDE + cx, 1) == 0;
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
    world.Match.scattered = true;      // Alone, as they come: no horde
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
    world.Match.scattered = false;
    world.Match.spawnWait = 0;
}

// Whether a place and the cells round it are open, flat ground (no cliff the dead would be climbing, no wall).
static bool flat_ground(tide_float3 at)
{
    for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
            const int cx = (int)floorf(at.x + NAV_HALF) + dx, cz = (int)floorf(at.z + NAV_HALF) + dz;
            if (cx < 0 || cz < 0 || cx >= NAV_SIDE || cz >= NAV_SIDE) return false;
            if (*(const uint8_t *)tide_list_read(&world.heap, world.Ashwick.cells, cz * NAV_SIDE + cx, 1) != 0) return false;
        }
    return true;
}

// Whether the ground is open all round a place, out to `reach` metres (no cliff or wall between it and where a
// survivor is stood to face it).
static bool open_round(tide_float3 at, float reach)
{
    for (int k = 0; k < 16; k++) {
        const float turn = (float)k * 0.3926991f;
        for (float r = 1; r <= reach; r += 1) {
            const tide_float3 p = {at.x + sinf(turn) * r, at.y, at.z + cosf(turn) * r};
            if (!flat_ground(p)) return false;
        }
    }
    return true;
}

// One of the dead on its feet, with open ground round it if any has.
static int hunting_row(void)
{
    int any = -1, flat = -1;
    for (uint32_t r = 0; r < world.arch6_Zombie.count; r++) {
        const Zombie z = TIDE_AT(&world, arch6_Zombie, Zombie, r);
        if (z.state != ZombieState_Hunting) continue;
        if (open_round(z.at, 8)) return (int)r;
        if (flat < 0 && flat_ground(z.at)) flat = (int)r;
        if (any < 0) any = (int)r;
    }
    return flat >= 0 ? flat : any;
}

// One of the dead on its feet, an ordinary one (not a Bloater, which bursts and is gone), made to take any number of
// blows, and its entity.
static tide_entity sturdy(void)
{
    int row = -1;
    for (int t = 0; t < 60 * 60 && row < 0; t++) {
        row = hunting_row();
        if (row < 0) tick_safe(1);
    }
    CHECK(row >= 0);
    const tide_entity e = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
    Zombie z = tide_get_Zombie(&world, e);
    z.health = 100000;
    z.kind = ZombieKind_Walker;
    CHECK(tide_set_Zombie(&world, e, z));
    return e;
}

static Zombie zombie(tide_entity e) { return tide_get_Zombie(&world, e); }

// Every other one of the dead moved well off to one side of `e` and held there: a round meant for it stops in the first
// it strikes, and one walking into its line had taken it.
static void others_away(tide_entity e)
{
    const Zombie z = zombie(e);
    for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
        const tide_entity o = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
        if (o.index == e.index) continue;
        Zombie away = zombie(o);
        away.at = (tide_float3){z.at.x + sinf(z.yaw + 1.6f) * 30, z.at.y, z.at.z + cosf(z.yaw + 1.6f) * 30};
        away.pace = 0;
        CHECK(tide_set_Zombie(&world, o, away));
    }
}

// Whether one of the dead has been killed: torn apart and gone, or (a Bloater) lying swelling to burst.
static bool killed(tide_entity e) { return !tide_entity_alive(&world.entities, e) || zombie(e).state == ZombieState_Dying; }

// One of the dead moved to open ground near the fire, turned so the 9 m in front of it (where a survivor is stood to
// face it) is flat and clear of every box, and held there (no pace), so a throw or a leap lands where it was aimed.
static void to_open(tide_entity e)
{
    Zombie z = zombie(e);
    bool found = false;
    for (float cz = -6; cz >= -18 && !found; cz -= 2) {
        for (float cx = -10; cx <= 10 && !found; cx += 2) {
            for (int k = 0; k < 8 && !found; k++) {
                const float yaw = (float)k * 0.7853982f;
                bool clear = true;
                for (float r = 0; r <= 9 && clear; r += 0.5f) {
                    const tide_float3 p = {cx + sinf(yaw) * r, 0, cz + cosf(yaw) * r};
                    int which = -1;
                    if (!flat_ground(p) || solid_at(p, 0.8f, &which)) clear = false;
                }
                if (!clear) continue;
                z.at = (tide_float3){cx, 0.0f, cz};
                z.yaw = yaw;
                found = true;
            }
        }
    }
    CHECK(found);
    z.pace = 0;
    CHECK(tide_set_Zombie(&world, e, z));
}

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

// A skill card taken (it's no item: the right mouse's skill another, or a rank stronger).
static Survivor take_card(int seat, Item card)
{
    Survivor s = get(seat);
    s.offers.at[0] = card;
    s.offers.at[1] = s.offers.at[2] = Item_HollowPoints;
    s.choosing = true;
    s.pending = 0;
    set(seat, s);
    pick(seat, 1);
    return get(seat);
}

// One of the dead held at a place, on its feet, as tough as a hundred thousand, an ordinary one.
static void hold_at(tide_entity e, tide_float3 at)
{
    Zombie z = zombie(e);
    z.at = at;
    z.pace = 0;
    z.state = ZombieState_Hunting;
    z.health = 100000;
    z.kind = ZombieKind_Walker;
    z.frenzy = 0;
    z.shove = (tide_float3){0, 0, 0};
    z.swing = 0;
    z.windup = 0;
    z.leap = 0;
    z.burn = 0;
    z.flinch = 0;
    CHECK(tide_set_Zombie(&world, e, z));
}

// A survivor with only so many of one item, its timers run out.
static void only_item(int seat, Item item, int count)
{
    Survivor s = get(seat);
    memset(&s.items, 0, sizeof s.items);
    s.items.at[item] = count;
    s.droneWait = s.sawWait = s.teslaWait = s.molotovWait = s.shockWait = s.rocketWait = 0;
    set(seat, s);
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
    CHECK(get(0).kills == 0);

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
    CHECK(get(1).role == Role_Breacher && get(1).gun == Weapon_Shotgun && get(1).mag == 8);

    // ---- The run: a breath, then the first night, three minutes to its boss, and its chests hidden about; and the dead,
    // which the Gunner shoots down for a minute and a half ----
    for (int i = 0; i < 60 * 15 && world.Match.phase != Phase_Fighting; i++) tick(1);
    CHECK(world.Match.phase == Phase_Fighting && world.Match.clock < 0.1f);
    CHECK(world.Match.night == 1 && world.Match.nightEnds == 180 && !world.Match.reckoning);
    tick_safe(60 * 6);
    CHECK(world.arch6_Zombie.count > 0);
    CHECK(world.Match.chestsDue == 0 && world.Match.chestsPlaced == 4 && world.arch7_Chest.count == 4);
    int fired = 0;
    bool offered = false;
    for (int t = 0; t < 60 * 60 * 5 && world.Match.clock < 90; t++) {
        float nearest = 1e9f;
        Zombie target = {0};
        bool found = false;
        for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
            const Zombie z = TIDE_AT(&world, arch6_Zombie, Zombie, row);
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
        tick_whole(1);
    }
    held[0].attack = held[0].pick3 = held[1].pick3 = false;
    tick_whole(1);
    CHECK(world.Match.phase == Phase_Fighting && world.Match.clock >= 90);
    CHECK(world.Match.danger > 1.55f);
    CHECK(fired > 0);
    CHECK(get(0).kills >= 1);
    CHECK(world.Match.level > 1 && get(0).level == world.Match.level && get(1).level == world.Match.level);
    printf("a minute and a half: %d kills, the party at level %d, danger %.2f\n", get(0).kills, world.Match.level, world.Match.danger);

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
        const tide_entity e = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
        const tide_float3 at = tide_get_Zombie(&world, e).at;
        tick_whole(60);
        CHECK(world.Match.clock == clock && world.Match.paused);
        CHECK(tide_get_Zombie(&world, e).at.x == at.x && tide_get_Zombie(&world, e).at.z == at.z);
    }
    for (int seat = 0; seat < 2; seat++) {
        const Survivor s = get(seat);
        CHECK(s.choosing);
        for (int k = 0; k < 3; k++) CHECK((int)s.offers.at[k] >= 0 && (int)s.offers.at[k] < Item_BigTank + 1);
        // A gun card for their own gun first (the Gunner's carbine, the Breacher's shotgun); a skill card in the last place only
        const Item cards = s.role == Role_Gunner ? Item_Burst : Item_Buckshot;
        CHECK((int)s.offers.at[0] >= (int)cards && (int)s.offers.at[0] < (int)cards + 5);
        CHECK((int)s.offers.at[1] < Item_CardPhaseRound && (int)s.offers.at[2] < Item_Burst);
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
        CHECK(s.items.at[Item_Kevlar] == 1 && s.health == max_health(s) && max_health(s) == 145.0f + 18.0f * (float)(s.level - 1));
        s = take(0, Item_ExtendedMag);
        CHECK(s.mag == 45);                             // Half again a carbine's 30, loaded now
        s = take(0, Item_SecondWind);
        CHECK(s.selfRevives == 1);
        s = take(0, Item_EnergyDrink);
        CHECK(s.rolls == 3);                           // A third roll, ready now
        const Item rest[] = {Item_HollowPoints, Item_HairTrigger, Item_Sneakers, Item_Leech, Item_Battery, Item_LuckyShot, Item_Salts, Item_LiveWire,
                             Item_PowderKeg, Item_GunDrone, Item_EmberRing, Item_Frostbite, Item_Kerosene, Item_SteelToes, Item_BileBomb,
                             Item_ApRounds, Item_Mortar, Item_HeRounds, Item_DragonsBreath};
        for (size_t k = 0; k < sizeof rest / sizeof rest[0]; k++) {
            const int before = get(0).items.at[rest[k]];
            CHECK(take(0, rest[k]).items.at[rest[k]] == before + 1);
        }
        // Field Batteries: skills come back sooner
        become(0, Role_Gunner, Weapon_Rifle);
        put(0, (tide_float3){0, 0, -6}, (tide_float3){0, 1.6f, -12});
        tick_safe(8);
        press(&held[0].roll);
        CHECK(get(0).rollWait > 2.1f && get(0).rollWait < 2.15f);      // A spent roll back in 2.5 s, 15% sooner
        tick_safe(60);
        s = get(0);
        memset(&s.items, 0, sizeof s.items);
        s.selfRevives = 0;
        set(0, s);
    }

    // ---- A Second Wind: down, and straight back up (a few of the dead about, not a crowd to claw them down again) ----
    {
        few(3);
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
        if (friend.life != Life_Downed)
            fprintf(stderr, "not down: life %d health %.0f, the zombie state %d at %.1f %.1f %.1f, swing %.2f, %u up\n", (int)friend.life, friend.health, (int)zombie(e).state,
                    zombie(e).at.x, zombie(e).at.y, zombie(e).at.z, zombie(e).swing, world.arch6_Zombie.count);
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
        for (int i = 0; i < 60 * 4 && get(1).life != Life_Standing; i++) {
            Survivor me = get(0);
            me.health = max_health(me);
            set(0, me);
            tick(1);
        }
        CHECK(get(1).life == Life_Standing);
        CHECK(get(0).revives == 1);
        CHECK(get(1).gun == Weapon_Shotgun && get(1).mag == 4);
    }

    // ---- The guns: a carbine's round, a shotgun's pellets, into one of the dead, each from the gun to where the cursor
    // is; and the carbine at the legs, below the hips ----
    few(3);
    {
        const Weapon guns[3] = {Weapon_Rifle, Weapon_Shotgun, Weapon_Rifle};
        const Role roles[3] = {Role_Gunner, Role_Breacher, Role_Gunner};
        for (int k = 0; k < 3; k++) {
            become(0, roles[k], guns[k]);
            const tide_entity e = sturdy();
            to_open(e);
            others_away(e);
            face(0, e, 6);
            if (k == 2) look_at(0, (tide_float3){zombie(e).at.x, zombie(e).at.y + 0.35f, zombie(e).at.z});
            const float before = zombie(e).health;
            press(&held[0].attack);
            tick_safe(2);
            if (zombie(e).health >= before)
                fprintf(stderr, "the %s missed: survivor at %.2f %.2f %.2f yaw %.2f pitch %.2f; zombie at %.2f %.2f %.2f chest %.2f %.2f %.2f state %d; %u up\n",
                        k == 0 ? "carbine" : k == 1 ? "shotgun" : "carbine at the legs", get(0).feet.x, get(0).feet.y, get(0).feet.z, get(0).yaw, get(0).pitch,
                        zombie(e).at.x, zombie(e).at.y, zombie(e).at.z, zombie(e).chestAt.x, zombie(e).chestAt.y, zombie(e).chestAt.z, (int)zombie(e).state,
                        world.arch6_Zombie.count);
            CHECK(zombie(e).health < before);
        }
        // The cursor's point high over one of the dead (as from a rise, or past it on rising ground): the round goes level
        // and strikes it all the same
        become(0, Role_Gunner, Weapon_Rifle);
        const tide_entity e = sturdy();
        to_open(e);
        others_away(e);
        face(0, e, 6);
        look_at(0, (tide_float3){zombie(e).at.x, zombie(e).at.y + 3.5f, zombie(e).at.z});
        const float before = zombie(e).health;
        press(&held[0].attack);
        tick_safe(2);
        printf("a round aimed 3.5 m over one of the dead's feet: %.1f to it\n", before - zombie(e).health);
        CHECK(zombie(e).health < before);
    }

    // ---- From above: turned to what the cursor's on, the carbine's round into the chest it's on; moving up the screen
    // whichever way they face ----
    few(2);
    {
        become(0, Role_Gunner, Weapon_Rifle);
        const tide_entity e = sturdy();
        to_open(e);
        face(0, e, 6);
        const tide_float3 chest = zombie(e).chestAt;
        look_at(0, (tide_float3){get(0).feet.x + 10, get(0).feet.y + 1.6f, get(0).feet.z});      // Turned away
        held[0].aim = chest;
        tick_safe(1);
        float off = get(0).yaw - atan2f(chest.x - get(0).feet.x, chest.z - get(0).feet.z);
        while (off > 3.14159265f) off -= 6.2831853f;
        while (off < -3.14159265f) off += 6.2831853f;
        Survivor s = get(0);
        s.shotWait = 0;
        s.mag = 30;
        set(0, s);
        const float before = zombie(e).health;
        press(&held[0].attack);
        tick_safe(2);
        printf("from above: turned %.3f from the chest the cursor's on, the round doing %.1f\n", fabsf(off), before - zombie(e).health);
        CHECK(fabsf(off) < 0.05f && zombie(e).health < before);
        // Up the screen (north), turned east
        cull();
        put(1, (tide_float3){30, 0, 30}, (tide_float3){30, 1.6f, 40});
        put(0, (tide_float3){0, 0, -14}, (tide_float3){10, 1.6f, -14});
        tick_safe(8);
        const tide_float3 was = get(0).feet;
        held[0].aim = (tide_float3){was.x + 10, was.y + 1.36f, was.z};
        held[0].move = (tide_float2){0, 1};
        tick_safe(30);
        held[0].move = (tide_float2){0, 0};
        printf("from above, half a second up the screen turned east: %.2f m north, %.2f m east, facing %.2f\n", get(0).feet.z - was.z, get(0).feet.x - was.x, get(0).yaw);
        const float facing = atan2f(held[0].aim.x - get(0).feet.x, held[0].aim.z - get(0).feet.z);      // Still at the cursor
        CHECK(get(0).feet.z - was.z > 1.5f && fabsf(get(0).feet.x - was.x) < 0.3f && fabsf(get(0).yaw - facing) < 0.05f);
    }

    // ---- A horde: what's owed claws up together out of one patch of ground, 22 to 32 m from whom it's for, within a
    // second ----
    {
        cull();
        world.Match.scattered = false;
        world.Match.spawnWait = 1e9f;      // Nothing more owed: the horde alone
        world.Match.hordePool = 30;
        world.Match.hordeWait = 0;
        world.Match.hordeLeft = 0;
        tick_safe(1);
        CHECK(world.Match.hordeSize == 30 && world.Match.hordePool == 0);
        const tide_float3 middle = world.Match.hordeAt;
        const tide_float3 focus = world.Match.hordeFocus;
        const float radius = world.Match.hordeRadius;
        int risen = 0;
        float farthest = 0;
        for (int t = 0; t < 62 && risen < 30; t++) {
            tick_safe(1);
            risen = (int)world.arch6_Zombie.count;
        }
        for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
            const Zombie z = TIDE_AT(&world, arch6_Zombie, Zombie, row);
            farthest = fmaxf(farthest, hypotf(z.at.x - middle.x, z.at.z - middle.z));
        }
        const float off = hypotf(middle.x - focus.x, middle.z - focus.z);
        printf("a horde of 30: up within a second, %d of them, %.1f m from its middle at most (its patch %.1f m), %.1f m from whom it's for\n", risen, farthest,
               radius, off);
        CHECK(risen == 30 && world.Match.hordeLeft == 0 && farthest <= radius + 1.0f && farthest > 1.0f && off >= 21.9f && off <= 32.1f);
        spawning();
    }

    // ---- Every skill: the roll and the secondary on their keys, the special by itself where it does the most ----
    few(4);
    {
        autos = true;
        // The Gunner: a phase round at the cursor (the grenade held back)
        become(0, Role_Gunner, Weapon_Rifle);
        only_skill(0, -1);
        tide_entity e = sturdy();
        to_open(e);
        face(0, e, 6);
        only_skill(0, 0);
        float before = zombie(e).health;
        press(&held[0].secondary);
        tick_safe(2);
        CHECK(zombie(e).health < before && get(0).cooldowns.at[0] > 1);
        only_skill(0, -1);
        // The roll, its two charges, the first back after the cooldown
        put(0, (tide_float3){0, 0, -6}, (tide_float3){0, 1.6f, -12});
        tick_safe(8);
        tide_float3 was = get(0).feet;
        press(&held[0].roll);              // No way pressed: the way they face
        tick_safe(30);
        CHECK(hypotf(get(0).feet.x - was.x, get(0).feet.z - was.z) > 3 && get(0).rolls == 1);
        tick_safe(20);
        was = get(0).feet;
        press(&held[0].roll);
        tick_safe(30);
        printf("a roll: %.1f m\n", hypotf(get(0).feet.x - was.x, get(0).feet.z - was.z));
        CHECK(hypotf(get(0).feet.x - was.x, get(0).feet.z - was.z) > 3.5f && get(0).rolls == 0 && get(0).cooldowns.at[1] > 0);
        tick_safe(60 * 5);
        CHECK(get(0).rolls >= 1);
        // Through one of the dead: past it, and it isn't touched (a roll's a dodge)
        e = sturdy();
        to_open(e);
        face(0, e, 2.5f);
        before = zombie(e).health;
        const tide_float3 stood = get(0).feet;
        press(&held[0].roll);
        tick_safe(30);
        CHECK(hypotf(get(0).feet.x - stood.x, get(0).feet.z - stood.z) > 3.5f && zombie(e).health == before);
        // A grenade into the thickest of the dead, 8 m off, by itself: the rest gathered round the one
        e = sturdy();
        to_open(e);
        face(0, e, 8);
        for (uint32_t row = 0, n = 0; row < world.arch6_Zombie.count; row++) {
            const tide_entity other = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
            if (other.index == e.index || zombie(other).state != ZombieState_Hunting) continue;
            Zombie o = zombie(other);
            o.at = (tide_float3){zombie(e).at.x + (n % 2 ? 1.2f : -1.2f), zombie(e).at.y, zombie(e).at.z + (n / 2) * 1.2f};
            o.pace = 0;
            o.health = 100000;
            CHECK(tide_set_Zombie(&world, other, o));
            n++;
        }
        before = zombie(e).health;
        only_skill(0, 2);
        int thrown = 0;
        for (int t = 0; t < 150 && zombie(e).health >= before; t++) {
            tick_safe(1);
            if (missiles_of(Shot_Grenade) > 0) thrown = 1;
        }
        CHECK(thrown && zombie(e).health < before && get(0).cooldowns.at[2] > 0);

        // The Breacher: the Thumper's grenade at one 7 m off, at the cursor; the boot at one close, by itself
        become(0, Role_Breacher, Weapon_Shotgun);
        only_skill(0, -1);
        e = sturdy();
        to_open(e);
        face(0, e, 7);
        before = zombie(e).health;
        only_skill(0, 0);
        press(&held[0].secondary);
        CHECK(missiles_of(Shot_Grenade) == 1 && get(0).cooldowns.at[0] > 1);
        only_skill(0, -1);
        for (int t = 0; t < 90 && zombie(e).health >= before; t++) tick_safe(1);
        printf("the Thumper: %.0f to one 7 m off at the cursor\n", before - zombie(e).health);
        CHECK(zombie(e).health < before - 100 && missiles_of(Shot_Grenade) == 0);
        tick_safe(60 * 3);
        e = sturdy();
        to_open(e);
        face(0, e, 1.4f);
        only_skill(0, 2);
        bool kicked = false;
        for (int t = 0; t < 60 && !kicked; t++) {
            tick_safe(1);
            kicked = killed(e) || zombie(e).state == ZombieState_Fallen;
        }
        CHECK(kicked && get(0).cooldowns.at[2] > 1);
        // The boot again, at one already down: stamped on
        {
            // Kicked over first, then held down where it lies (done sliding, not getting up yet)
            only_skill(0, -1);
            tick_safe(40);
            Zombie z = zombie(e);
            if (z.state == ZombieState_Fallen) {
                z.shove = (tide_float3){0};
                z.timer = 0.9f;
                z.downFor = 3;
                CHECK(tide_set_Zombie(&world, e, z));
                tick_safe(2);
                z = zombie(e);
            }
            if (z.state == ZombieState_Fallen) {
                // The rest of the dead well off, so this is the one within the boot's reach
                for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
                    const tide_entity other = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
                    if (other.index == e.index) continue;
                    Zombie o = tide_get_Zombie(&world, other);
                    if (hypotf(o.at.x - z.at.x, o.at.z - z.at.z) > 8) continue;
                    o.at.x += 12;
                    CHECK(tide_set_Zombie(&world, other, o));
                }
                put(0, z.at, z.headAt);
                tick_safe(1);            // (the herd as it is now, the rest gone off)
                before = zombie(e).health;
                only_skill(0, 2);
                tick_safe(5);
                if (zombie(e).health >= before - 100)
                    fprintf(stderr, "the stamp: health %.0f from %.0f, state %d, zombie at %.2f %.2f %.2f head %.2f %.2f %.2f, survivor at %.2f %.2f %.2f, boot cooldown %.2f\n",
                            zombie(e).health, before, (int)zombie(e).state, zombie(e).at.x, zombie(e).at.y, zombie(e).at.z, zombie(e).headAt.x, zombie(e).headAt.y, zombie(e).headAt.z,
                            get(0).feet.x, get(0).feet.y, get(0).feet.z, get(0).cooldowns.at[2]);
                CHECK(zombie(e).health < before - 100);
                only_skill(0, -1);
            }
        }

        // The Engineer: napalm at one 6 m off, at the cursor: a fire where it splashed, burning the dead in it, out a
        // while later; and the drone by itself
        become(0, Role_Engineer, Weapon_Flamer);
        only_skill(0, -1);
        e = sturdy();
        to_open(e);
        face(0, e, 6);
        before = zombie(e).health;
        only_skill(0, 0);
        press(&held[0].secondary);
        CHECK(missiles_of(Shot_Napalm) == 1);
        only_skill(0, -1);
        for (int t = 0; t < 90 && world.arch4_Blaze.count == 0; t++) tick_safe(1);
        CHECK(world.arch4_Blaze.count == 1);
        {
            const Blaze fire = TIDE_AT(&world, arch4_Blaze, Blaze, 0);
            Zombie z = zombie(e);
            z.burn = 0;
            z.at = (tide_float3){fire.at.x + 0.5f, z.at.y, fire.at.z};
            CHECK(tide_set_Zombie(&world, e, z));
            const float splashed = before - zombie(e).health;
            before = zombie(e).health;
            tick_safe(40);
            printf("napalm: %.0f in its splash, a fire %.1f m round for %.1f s, %.0f more and burning (%d) in it\n", splashed, fire.reach, fire.seconds,
                   before - zombie(e).health, zombie(e).burn > 0);
            CHECK(splashed > 0 && zombie(e).burn > 0 && zombie(e).health < before && fire.seat == 0);
            // Its maker standing in it is burned too (the owner, 2026-10-20: "my own napalm fire should hurt me")
            put(0, (tide_float3){fire.at.x - 0.5f, fire.at.y, fire.at.z}, (tide_float3){fire.at.x - 0.5f, 1, fire.at.z + 5});
            const float whole = get(0).health;
            tick(40);
            printf("napalm's maker standing in its fire: %.1f off them\n", whole - get(0).health);
            CHECK(get(0).health < whole);
            heal();
            for (int t = 0; t < 60 * 8 && world.arch4_Blaze.count > 0; t++) tick_safe(1);
            CHECK(world.arch4_Blaze.count == 0);
        }
        few(2);
        e = sturdy();
        to_open(e);
        face(0, e, 4);
        before = zombie(e).health;
        only_skill(0, 2);
        tick_safe(1);
        CHECK(world.arch3_Turret.count == 1);
        for (int t = 0; t < 60 * 5 && zombie(e).health >= before; t++) tick_safe(1);
        CHECK(zombie(e).health < before);
        only_skill(0, -1);
        // Held for a pick, it holds its fire: nothing it was shooting is hurt (the owner, 2026-10-20: "i think the drone
        // kept killing things while game was paused")
        {
            Survivor s = get(0);
            s.offers.at[0] = s.offers.at[1] = s.offers.at[2] = Item_HollowPoints;
            s.choosing = true;
            set(0, s);
            tick_whole(2);
            CHECK(world.Match.paused);
            float hurt = 0;
            for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) hurt += TIDE_AT(&world, arch6_Zombie, Zombie, row).health;
            const float life = TIDE_AT(&world, arch3_Turret, Turret, 0).life;
            tick_whole(60 * 2);
            float after = 0;
            for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) after += TIDE_AT(&world, arch6_Zombie, Zombie, row).health;
            printf("held for a pick two seconds: the drone's life %.2f to %.2f, the dead's health %.0f to %.0f\n", life, TIDE_AT(&world, arch3_Turret, Turret, 0).life,
                   hurt, after);
            CHECK(after == hurt && TIDE_AT(&world, arch3_Turret, Turret, 0).life == life);
            s = get(0);
            s.choosing = false;
            set(0, s);
        }
        // Its maker 10 m off: it flies after them, to their shoulder, at its height
        {
            const tide_float3 from = TIDE_AT(&world, arch3_Turret, Turret, 0).at;
            tide_float3 there = get(0).feet;
            for (int k = 0; k < 16; k++) {
                const float turn = (float)k * 0.3926991f;
                const tide_float3 a = {from.x + sinf(turn) * 10, 0, from.z + cosf(turn) * 10};
                int which = -1;
                if (!flat_ground(a) || solid_at(a, 1.0f, &which)) continue;
                there = a;
                break;
            }
            put(0, there, (tide_float3){there.x, 1.6f, there.z + 5});
            tick_safe(60 * 3);
            const tide_float3 at = TIDE_AT(&world, arch3_Turret, Turret, 0).at;
            const float went = hypotf(at.x - from.x, at.z - from.z);
            const float heel = hypotf(at.x - get(0).feet.x, at.z - get(0).feet.z);
            printf("the drone flew %.1f m after its maker, to %.1f m from them, %.1f m up\n", went, heel, at.y - get(0).feet.y);
            CHECK(went > 6 && heel < 3 && at.y - get(0).feet.y > 2 && at.y - get(0).feet.y < 3);
        }
        tick_safe(60 * 13);
        CHECK(world.arch3_Turret.count == 0);           // Its time up        tick_safe(60 * 13);
        CHECK(world.arch3_Turret.count == 0);           // Its time up
        autos = false;
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
            to_open(e);
            face(0, e, 5);
            const int shells = missiles_of(Shot_Shell);
            s = get(0);
            s.shotWait = 0;
            s.mag = 12;
            set(0, s);
            press(&held[0].attack);
            tick_safe(2);
            for (uint32_t row = 0; row < world.arch6_Zombie.count; row++)
                if (TIDE_AT(&world, arch6_Zombie, Zombie, row).frozen > 0) froze = true;
            called = missiles_of(Shot_Shell) > shells;
        }
        CHECK(froze);
        CHECK(called);
        // Live Wire: some of the dead near the one shot are struck too, sooner or later
        bool arced = false;
        for (int shot = 0; shot < 60 && !arced; shot++) {
            for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
                const tide_entity other = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
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
            for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
                const Zombie z = TIDE_AT(&world, arch6_Zombie, Zombie, row);
                if (TIDE_ENTITY_AT(&world, arch6_Zombie, row).index != e.index && z.state != ZombieState_Dying && z.health < 100000) arced = true;
            }
        }
        CHECK(arced);
        // A Powder Keg: a kill bursts, and the nearest of the dead to it are burned. Two of the dead alone, one beside the
        // other out of the line of fire (with the dead in hordes, others stood in it)
        few(2);
        s = get(0);
        memset(&s.items, 0, sizeof s.items);
        s.items.at[Item_PowderKeg] = 3;
        set(0, s);
        bool burst = false;
        for (int tries = 0; tries < 10 && !burst; tries++) {
            const tide_entity ea = sturdy();
            to_open(ea);
            tide_entity eb = {0};
            bool other = false;
            for (uint32_t row = 0; row < world.arch6_Zombie.count && !other; row++) {
                const tide_entity e2 = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
                if (e2.index == ea.index || zombie(e2).state != ZombieState_Hunting) continue;
                eb = e2;
                other = true;
            }
            if (!other) {
                few(2);
                continue;
            }
            Zombie za = zombie(ea), zb = zombie(eb);
            zb.at = (tide_float3){za.at.x + cosf(za.yaw) * 1.2f, za.at.y, za.at.z - sinf(za.yaw) * 1.2f};
            zb.pace = 0;
            zb.kind = ZombieKind_Walker;
            zb.health = 100000;
            CHECK(tide_set_Zombie(&world, eb, zb));
            face(0, ea, 5);
            za = zombie(ea);
            za.health = 1;
            CHECK(tide_set_Zombie(&world, ea, za));
            s = get(0);
            s.shotWait = 0;
            s.mag = 12;
            set(0, s);
            press(&held[0].attack);
            tick_safe(3);
            if (killed(ea)) burst = zombie(eb).health < 100000;
        }
        CHECK(burst);
        // The drone and the ember ring: one of the dead near, never shot
        few(1);
        s = get(0);
        memset(&s.items, 0, sizeof s.items);
        s.items.at[Item_GunDrone] = 1;
        set(0, s);
        e = sturdy();
        to_open(e);
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

    // ---- Wading through the dead: three pressed against a survivor slow them to well under half their pace; none near,
    // full pace ----
    {
        few(3);
        tide_entity pressed[3];
        int found = 0;
        for (uint32_t row = 0; row < world.arch6_Zombie.count && found < 3; row++) {
            if (TIDE_AT(&world, arch6_Zombie, Zombie, row).state != ZombieState_Hunting) continue;
            pressed[found] = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
            Zombie z = zombie(pressed[found]);
            z.health = 100000;
            z.pace = 0;
            z.kind = ZombieKind_Walker;
            CHECK(tide_set_Zombie(&world, pressed[found], z));
            found++;
        }
        CHECK(found == 3);
        float went[2];
        for (int close = 0; close < 2; close++) {
            put(0, (tide_float3){0, 0, -6}, (tide_float3){0, 1.6f, -12});
            tick_safe(10);
            const tide_float3 from = get(0).feet;
            held[0].move = (tide_float2){1, 0};
            for (int t = 0; t < 60; t++) {
                // Kept against them (beside, either side, and behind) or well away, every tick
                const tide_float3 feet = get(0).feet;
                const tide_float2 offsets[3] = {{0.62f, 0}, {-0.62f, 0}, {0, 0.62f}};
                for (int k = 0; k < 3; k++) {
                    Zombie z = zombie(pressed[k]);
                    const float away = close ? 1.0f : 8.0f;
                    z.at = (tide_float3){feet.x + offsets[k].x * away, feet.y, feet.z + offsets[k].y * away};
                    CHECK(tide_set_Zombie(&world, pressed[k], z));
                }
                Survivor s = get(0);
                s.health = 100;
                s.roll = 0;
                set(0, s);
                tick_safe(1);
            }
            held[0].move = (tide_float2){0, 0};
            went[close] = hypotf(get(0).feet.x - from.x, get(0).feet.z - from.z);
        }
        printf("a second's walk: %.2f m alone, %.2f m with three of the dead pressed against them\n", went[0], went[1]);
        CHECK(went[0] > 2 && went[1] < went[0] * 0.5f);
        spawning();
    }

    // ---- A Bloater: near a survivor it swells, then bursts, hurting them and the dead beside it; shot dead from afar, it
    // falls and swells, not bursting at once, then bursts, the dead beside it hurt and the survivor not ----
    {
        few(4);
        become(0, Role_Gunner, Weapon_Rifle);
        tide_entity dead[3];
        int found = 0;
        for (uint32_t row = 0; row < world.arch6_Zombie.count && found < 3; row++) {
            if (TIDE_AT(&world, arch6_Zombie, Zombie, row).state != ZombieState_Hunting) continue;
            dead[found++] = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
        }
        CHECK(found == 3);
        for (int k = 0; k < 3; k++) {
            Zombie z = zombie(dead[k]);
            z.health = 100000;
            z.pace = 0;
            z.kind = k == 1 ? ZombieKind_Walker : ZombieKind_Bloater;
            CHECK(tide_set_Zombie(&world, dead[k], z));
        }
        // Swelling, then the burst
        put(0, (tide_float3){0, 0, -6}, (tide_float3){0, 1.6f, -12});
        tick_safe(10);
        const tide_float3 feet = get(0).feet;
        const tide_float3 places[2] = {{feet.x, feet.y, feet.z - 1.8f}, {feet.x + 1.0f, feet.y, feet.z - 2.4f}};
        for (int k = 0; k < 2; k++) {
            Zombie z = zombie(dead[k]);
            z.at = places[k];
            CHECK(tide_set_Zombie(&world, dead[k], z));
        }
        Survivor s = get(0);
        s.health = 100;
        set(0, s);
        int burst = -1;
        float before = 0;
        for (int t = 0; t < 160 && burst < 0; t++) {
            before = get(0).health;
            tick(1);        // Not tick_safe: it keeps the survivors whole
            decline();
            if (!tide_entity_alive(&world.entities, dead[0])) burst = t;
        }
        if (burst < 80 || burst > 92) fprintf(stderr, "the Bloater burst after %d ticks (fuse %.2f)\n", burst, burst < 0 ? zombie(dead[0]).fuse : 0.0f);
        CHECK(burst >= 80 && burst <= 92);
        for (int t = 0; t < 3; t++) tick(1);        // The burst's blows are taken
        const float hurt = before - get(0).health;
        const float wounded = 100000 - zombie(dead[1]).health;
        printf("a Bloater burst %.2f s after a survivor came near: the survivor took %.0f, the one beside it %.0f\n", burst / 60.0f, hurt, wounded);
        CHECK(hurt > 15 && wounded > 0);
        // Shot dead from 8 m off: it bursts, the one beside it hurt, the survivor not
        Zombie z = zombie(dead[2]);
        z.at = (tide_float3){feet.x, feet.y, feet.z - 8};
        z.health = 1;
        CHECK(tide_set_Zombie(&world, dead[2], z));
        Zombie w = zombie(dead[1]);
        w.at = (tide_float3){feet.x + 1.0f, feet.y, feet.z - 8.3f};
        w.health = 100000;
        w.state = ZombieState_Hunting;
        CHECK(tide_set_Zombie(&world, dead[1], w));
        s = get(0);
        s.shotWait = 0;
        s.mag = 30;
        set(0, s);
        tick_safe(2);
        look_at(0, zombie(dead[2]).chestAt);
        const float whole = get(0).health;
        held[0].attack = true;
        tick(1);
        held[0].attack = false;
        for (int t = 0; t < 3; t++) tick(1);
        CHECK(tide_entity_alive(&world.entities, dead[2]) && zombie(dead[2]).state == ZombieState_Dying && zombie(dead[2]).fuse > 0);
        int fell = 4;
        for (; fell < 120 && tide_entity_alive(&world.entities, dead[2]); fell++) tick(1);
        if (tide_entity_alive(&world.entities, dead[2])) fprintf(stderr, "the shot Bloater didn't burst: health %.1f, state %d\n", zombie(dead[2]).health, (int)zombie(dead[2]).state);
        printf("a Bloater shot dead from 8 m: it burst %.2f s after\n", fell / 60.0f);
        CHECK(!tide_entity_alive(&world.entities, dead[2]) && fell >= 56 && fell <= 66);
        CHECK(zombie(dead[1]).health < 100000 && get(0).health >= whole);
        // Its orb taken (any lying far off from before, a level round's kills down its whole line, left to the orbs' check)
        int stale = 0;
        for (uint32_t r = 0; r < world.arch5_Orb.count; r++) {
            const Orb o = TIDE_AT(&world, arch5_Orb, Orb, r);
            if (hypotf(o.at.x - feet.x, o.at.z - (feet.z - 8)) > 6) stale++;
        }
        put(0, (tide_float3){feet.x, feet.y, feet.z - 7}, (tide_float3){feet.x, 1.6f, feet.z - 12});
        for (int t = 0; t < 180 && (int)world.arch5_Orb.count > stale; t++) tick_safe(1);
        CHECK((int)world.arch5_Orb.count <= stale);
        spawning();
    }

    // ---- A Spitter: from 9 m off with a clear line, its throat swells and its bile lands on the survivor, hurting them and
    // slowing them a while. A Warden: a round to its body does a quarter of what it does to one of the walking dead, one
    // to its head all of it (twice a body's) ----
    {
        few(2);
        become(0, Role_Gunner, Weapon_Rifle);
        const tide_entity e = sturdy();
        to_open(e);
        Zombie z = zombie(e);
        z.kind = ZombieKind_Spitter;
        z.spit = 0;
        CHECK(tide_set_Zombie(&world, e, z));
        face(0, e, 9);
        Survivor s = get(0);
        s.health = 100;
        s.calm = 0;
        set(0, s);
        bool spat = false, slimed = false;
        float least = 100, flight = 0, markOff = 1e9f;
        for (int t = 0; t < 60 * 6 && !slimed; t++) {
            if (missiles_of(Shot_Spit) > 0) spat = true;
            for (uint32_t row = 0; row < world.arch2_Missile.count; row++) {
                const Missile m = TIDE_AT(&world, arch2_Missile, Missile, row);
                if (m.kind != Shot_Spit || m.age > 0.02f) continue;
                flight = m.flight;
                markOff = hypotf(m.mark.x - get(0).feet.x, m.mark.z - get(0).feet.z);
            }
            tick(1);        // Not tick_safe: it keeps the survivors whole
            decline();
            least = fminf(least, get(0).health);
            if (get(0).slimed > 0) slimed = true;
        }
        printf("a Spitter's bile: spat %d, in the air %.2f s, marked %.2f m from the survivor, the survivor slimed %d, down to %.0f health\n", spat, flight, markOff,
               slimed, least);
        CHECK(spat && slimed && least < 100);
        CHECK(flight >= 0.9f && markOff < 1.5f);
        heal();
        others_away(e);
        // Its body and head as a Warden's, and as one of the walking dead's
        float hits[3] = {0};
        const ZombieKind kinds[3] = {ZombieKind_Warden, ZombieKind_Walker, ZombieKind_Warden};
        for (int k = 0; k < 3; k++) {
            z = zombie(e);
            z.kind = kinds[k];
            z.health = 100000;
            z.windup = 0;
            z.spit = 99;
            z.flinch = 0;
            CHECK(tide_set_Zombie(&world, e, z));
            tick_safe(1);           // Its head and chest where its kind's body has them (a Spitter's head is low, by its chest)
            s = get(0);
            s.shotWait = 0;
            s.mag = 30;
            set(0, s);
            look_at(0, k == 2 ? zombie(e).headAt : zombie(e).chestAt);
            const float before = zombie(e).health;
            press(&held[0].attack);
            tick_safe(2);
            hits[k] = before - zombie(e).health;
        }
        printf("a carbine round: %.1f to a Warden's mail, %.1f to one of the walking dead, %.1f to a Warden's head\n", hits[0], hits[1], hits[2]);
        CHECK(hits[0] > 0 && hits[0] < hits[1] * 0.3f && hits[2] > hits[1] * 1.9f);
        z = zombie(e);
        z.kind = ZombieKind_Walker;
        CHECK(tide_set_Zombie(&world, e, z));
        // From behind, the cursor on its head: a headshot, though the round goes into its back first
        {
            z = zombie(e);
            put(0, (tide_float3){z.at.x - sinf(z.yaw) * 5, z.at.y, z.at.z - cosf(z.yaw) * 5}, z.headAt);
            tick_safe(10);
            look_at(0, zombie(e).headAt);
            s = get(0);
            s.shotWait = 0;
            s.mag = 30;
            set(0, s);
            const float before = zombie(e).health;
            press(&held[0].attack);
            tick_safe(2);
            const float behind = before - zombie(e).health;
            printf("a carbine round from behind, the cursor on its head: %.1f (a headshot's %.1f)\n", behind, hits[1] * 2);
            CHECK(behind > hits[1] * 1.9f);
        }
    }

    // ---- A kind of the dead first come is told, once: a Howler raised, its kind marked seen ----
    {
        world.Match.seenKinds &= ~(1 << ZombieKind_Howler);
        world.Match.special = ZombieKind_Howler;
        world.Match.specials = 1;
        world.Match.rush = 1;
        tick_safe(2);
        CHECK((world.Match.seenKinds & (1 << ZombieKind_Howler)) != 0);
    }

    // ---- A Runner: 4.5 m off, it crouches, then leaps and lands its blow, within a second. A Brute:
    // kicked, it stays on its feet; its blow throws the survivor back. A Howler 8 m off: it stands and howls, and the dead
    // round it are frenzied ----
    {
        few(4);
        become(0, Role_Gunner, Weapon_Rifle);
        const tide_entity r = sturdy();
        to_open(r);
        Zombie z = zombie(r);
        z.kind = ZombieKind_Runner;
        z.gait = Gait_Sprint;
        z.spit = 0;
        CHECK(tide_set_Zombie(&world, r, z));
        // (the other survivor well behind it, and the rest of the dead well away)
        put(1, (tide_float3){z.at.x - sinf(z.yaw) * 15, z.at.y, z.at.z - cosf(z.yaw) * 15}, z.at);
        for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
            const tide_entity o = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
            if (o.index == r.index) continue;
            Zombie away = zombie(o);
            away.at = (tide_float3){z.at.x + sinf(z.yaw + 1.6f) * 30, z.at.y, z.at.z + cosf(z.yaw + 1.6f) * 30};
            away.pace = 0;
            away.kind = ZombieKind_Walker;
            CHECK(tide_set_Zombie(&world, o, away));
        }
        face(0, r, 4.5f);
        Survivor s = get(0);
        s.health = 100;
        set(0, s);
        bool crouched = false, leapt = false;
        int t = 0;
        for (; t < 60 * 3 && get(0).health >= 100; t++) {
            world.Match.xp = 0;           // (no level: it makes the survivors whole)
            tick(1);
            decline();
            crouched |= zombie(r).windup > 0;
            leapt |= zombie(r).leap > 0;
        }
        printf("a Runner 4.5 m off: crouched %d, leapt %d, its blow %.2f s after, %.0f health left\n", crouched, leapt, t / 60.0f, get(0).health);
        CHECK(crouched && leapt && get(0).health < 100 && t < 66);
        heal();
        // The Brute, kicked by the Breacher, then left to swing
        become(0, Role_Breacher, Weapon_Shotgun);
        z = zombie(r);
        z.kind = ZombieKind_Brute;
        z.gait = Gait_Shamble;
        z.health = 100000;
        z.leap = 0;
        z.windup = 0;
        z.spit = 99;
        z.swing = 0;
        CHECK(tide_set_Zombie(&world, r, z));
        to_open(r);
        face(0, r, 1.5f);
        const float hale = zombie(r).health;
        autos = true;
        only_skill(0, 2);
        tick_safe(20);
        only_skill(0, -1);
        autos = false;
        printf("a Brute kicked: %.0f off it, %s\n", hale - zombie(r).health, zombie(r).state == ZombieState_Fallen ? "over" : "on its feet");
        CHECK(zombie(r).health < hale && zombie(r).state == ZombieState_Hunting);
        face(0, r, 1.5f);
        s = get(0);
        s.health = 400;
        set(0, s);
        float thrown = 0;
        for (t = 0; t < 60 * 3 && get(0).health >= 400; t++) {
            world.Match.xp = 0;
            tick(1);
            decline();
            thrown = fmaxf(thrown, hypotf(get(0).velocity.x, get(0).velocity.z));
        }
        for (int k = 0; k < 6; k++) {
            tick(1);
            thrown = fmaxf(thrown, hypotf(get(0).velocity.x, get(0).velocity.z));
        }
        printf("a Brute's blow: %.0f, thrown back at %.1f m/s\n", 400 - get(0).health, thrown);
        CHECK(get(0).health < 400 - 30 && thrown > 5);
        heal();
        // The Howler, three of the walking dead round it
        become(0, Role_Gunner, Weapon_Rifle);
        z = zombie(r);
        z.kind = ZombieKind_Howler;
        z.gait = Gait_Run;
        z.spit = 0;
        z.swing = 0;
        CHECK(tide_set_Zombie(&world, r, z));
        to_open(r);
        const tide_float3 howler = zombie(r).at;
        int round = 0;
        for (uint32_t row = 0; row < world.arch6_Zombie.count && round < 3; row++) {
            const tide_entity o = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
            if (o.index == r.index || TIDE_AT(&world, arch6_Zombie, Zombie, row).state != ZombieState_Hunting) continue;
            Zombie near = zombie(o);
            // Behind it (the survivor's in front), side by side
            const float yaw = zombie(r).yaw, side = (float)(round - 1) * 1.2f;
            near.at = (tide_float3){howler.x - sinf(yaw) * 2.0f + cosf(yaw) * side, howler.y, howler.z - cosf(yaw) * 2.0f - sinf(yaw) * side};
            near.pace = 0;
            near.frenzy = 0;
            near.kind = ZombieKind_Walker;
            CHECK(tide_set_Zombie(&world, o, near));
            round++;
        }
        CHECK(round == 3);
        face(0, r, 8);
        bool howled = false;
        int frenzied = 0;
        const tide_float3 stood = zombie(r).at;
        for (t = 0; t < 60 * 3 && frenzied == 0; t++) {
            tick_safe(1);
            howled |= zombie(r).windup > 0;
            for (uint32_t row = 0; row < world.arch6_Zombie.count; row++)
                if (TIDE_AT(&world, arch6_Zombie, Zombie, row).frenzy > 0) frenzied++;
        }
        const float moved = hypotf(zombie(r).at.x - stood.x, zombie(r).at.z - stood.z);
        printf("a Howler 8 m off: howled %d, %d of the dead frenzied %.2f s after, it moved %.2f m\n", howled, frenzied, t / 60.0f, moved);
        CHECK(howled && frenzied >= 3 && moved < 0.3f);
        z = zombie(r);
        z.kind = ZombieKind_Walker;
        CHECK(tide_set_Zombie(&world, r, z));
    }

    // ---- What the newer items do: AP Rounds through one more (four in a row, the carbine stopping in the first); HE Rounds burst on the first they hit, wounding the one
    // beside it; Dragon's Breath sets the dead alight; Kerosene sets the ones a dash goes through alight; a Bloater shot dead
    // by one with a Bile Bomb bursts wider; the Engineer's turret sets off its maker's items (Frostbite, here) ----
    {
        few(5);
        become(0, Role_Gunner, Weapon_Rifle);
        tide_entity line[4];
        int found = 0;
        for (uint32_t row = 0; row < world.arch6_Zombie.count && found < 4; row++) {
            if (TIDE_AT(&world, arch6_Zombie, Zombie, row).state != ZombieState_Hunting) continue;
            line[found++] = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
        }
        CHECK(found == 4);
        put(0, (tide_float3){0, 0, -6}, (tide_float3){0, 1.6f, -16});
        tick_safe(10);
        const tide_float3 feet = get(0).feet;
        // Four in a row down the carbine's line: it stops in the first, and with an AP Round goes on into the second
        for (int ap = 0; ap < 2; ap++) {
            float before[4];
            for (int k = 0; k < 4; k++) {
                Zombie z = zombie(line[k]);
                z.at = (tide_float3){feet.x, feet.y, feet.z - 5.0f - 1.2f * (float)k};
                z.yaw = 0;
                z.pace = 0;
                z.health = 100000;
                z.kind = ZombieKind_Walker;
                z.state = ZombieState_Hunting;
                CHECK(tide_set_Zombie(&world, line[k], z));
            }
            tick_safe(2);
            for (int k = 0; k < 4; k++) before[k] = zombie(line[k]).health;
            Survivor s = get(0);
            memset(&s.items, 0, sizeof s.items);
            s.items.at[Item_ApRounds] = ap;
            s.shotWait = 0;
            s.mag = 30;
            set(0, s);
            look_at(0, zombie(line[0]).chestAt);
            press(&held[0].attack);
            tick_safe(2);
            int struck = 0;
            for (int k = 0; k < 4; k++) struck += zombie(line[k]).health < before[k];
            CHECK(struck == 1 + ap);
        }
        // The Engineer's flamethrower: held two seconds, it burns the two in its cone (3 m ahead, and 4 m ahead 1.2 m
        // aside), through each other, and sets at least one alight (a puff in 25 does); not the one past its 4.8 m, nor
        // the one behind
        {
            become(0, Role_Engineer, Weapon_Flamer);
            Survivor s = get(0);
            memset(&s.items, 0, sizeof s.items);
            s.mag = 140;
            set(0, s);
            const tide_float3 spots[4] = {{feet.x, feet.y, feet.z - 3.0f}, {feet.x + 1.2f, feet.y, feet.z - 4.0f},
                                          {feet.x, feet.y, feet.z - 10.0f}, {feet.x, feet.y, feet.z + 2.5f}};
            float before[4];
            for (int k = 0; k < 4; k++) {
                Zombie z = zombie(line[k]);
                z.at = spots[k];
                z.health = 100000;
                z.burn = 0;
                z.state = ZombieState_Hunting;
                CHECK(tide_set_Zombie(&world, line[k], z));
            }
            tick_safe(2);
            for (int k = 0; k < 4; k++) before[k] = zombie(line[k]).health;
            look_at(0, (tide_float3){feet.x, feet.y + 1.3f, feet.z - 6.0f});
            held[0].attack = true;
            tick_safe(120);
            held[0].attack = false;
            tick_safe(1);
            printf("a flamethrower held two seconds: %.0f and %.0f to the two in its cone (burning %d, %d), %.0f past it, %.0f behind, %d of its tank used\n",
                   before[0] - zombie(line[0]).health, before[1] - zombie(line[1]).health, zombie(line[0]).burn > 0, zombie(line[1]).burn > 0,
                   before[2] - zombie(line[2]).health, before[3] - zombie(line[3]).health, 140 - get(0).mag);
            CHECK(zombie(line[0]).health < before[0] && zombie(line[1]).health < before[1]);
            CHECK(zombie(line[0]).burn > 0 || zombie(line[1]).burn > 0);
            CHECK(zombie(line[2]).health == before[2] && zombie(line[3]).health == before[3]);
            CHECK(get(0).mag < 120 && get(0).mag > 100);
            for (int k = 0; k < 4; k++) {
                Zombie z = zombie(line[k]);
                z.at = (tide_float3){feet.x, feet.y, feet.z - 5.0f - 1.2f * (float)k};
                z.burn = 0;
                z.health = 100000;
                CHECK(tide_set_Zombie(&world, line[k], z));
            }
            become(0, Role_Gunner, Weapon_Rifle);
            put(0, feet, (tide_float3){0, 1.6f, -16});
            tick_safe(10);
        }
        // HE Rounds and Dragon's Breath: one shot at the first, the one beside it wounded, the first burning
        {
            Zombie side = zombie(line[1]);
            side.at = (tide_float3){feet.x + 1.0f, feet.y, feet.z - 5.0f};
            CHECK(tide_set_Zombie(&world, line[1], side));
            for (int k = 2; k < 4; k++) {
                Zombie back = zombie(line[k]);
                back.at.x += 20 + 3 * k;
                CHECK(tide_set_Zombie(&world, line[k], back));
            }
            tick_safe(2);
            const float besides = zombie(line[1]).health;
            Survivor s = get(0);
            memset(&s.items, 0, sizeof s.items);
            s.items.at[Item_HeRounds] = 1;
            s.items.at[Item_DragonsBreath] = 4;          // Always
            s.shotWait = 0;
            s.mag = 30;
            set(0, s);
            look_at(0, zombie(line[0]).chestAt);
            press(&held[0].attack);
            tick_safe(2);
            CHECK(zombie(line[1]).health < besides);
            CHECK(zombie(line[0]).burn > 0);
        }
        // The gun cards: Burst Fire I a second round in a fan, 60% of the first, into the one 5 m off; Incendiary Rounds III
        // setting it alight; Marksman I taking a headshot on through into the one behind; a Slug Core's slug through three
        // in a line where the pellets stop in the first; a Long Nozzle II reaching one 6.5 m off; Gas Piston, a magazine a
        // fifth more, loaded
        {
            for (int k = 0; k < 4; k++) {
                Zombie z = zombie(line[k]);
                z.at = (tide_float3){feet.x, feet.y, feet.z - 5.0f - 1.2f * (float)k};
                z.yaw = 0;
                z.pace = 0;
                z.health = 100000;
                z.burn = 0;
                z.kind = ZombieKind_Walker;
                z.state = ZombieState_Hunting;
                CHECK(tide_set_Zombie(&world, line[k], z));
            }
            tick_safe(2);
            float body[2];
            for (int burst = 0; burst < 2; burst++) {
                Survivor s = get(0);
                memset(&s.items, 0, sizeof s.items);
                s.items.at[Item_Burst] = burst;
                s.shotWait = 0;
                s.mag = 30;
                set(0, s);
                look_at(0, zombie(line[0]).chestAt);
                const float before = zombie(line[0]).health;
                press(&held[0].attack);
                tick_safe(2);
                body[burst] = before - zombie(line[0]).health;
            }
            Survivor s = get(0);
            memset(&s.items, 0, sizeof s.items);
            s.items.at[Item_Incendiary] = 3;
            s.shotWait = 0;
            set(0, s);
            press(&held[0].attack);
            tick_safe(2);
            const bool lit = zombie(line[0]).burn > 0;
            Zombie first = zombie(line[0]);
            first.burn = 0;
            CHECK(tide_set_Zombie(&world, line[0], first));
            // Marksman I: the cursor on the first one's head
            s = get(0);
            memset(&s.items, 0, sizeof s.items);
            s.items.at[Item_Marksman] = 1;
            s.shotWait = 0;
            set(0, s);
            tick_safe(1);
            float before[4];
            for (int k = 0; k < 4; k++) before[k] = zombie(line[k]).health;
            look_at(0, zombie(line[0]).headAt);
            press(&held[0].attack);
            tick_safe(2);
            const float head = before[0] - zombie(line[0]).health;
            int struck = 0;
            for (int k = 0; k < 4; k++) struck += zombie(line[k]).health < before[k];
            printf("gun cards: a round %.0f, with Burst Fire I %.0f; Incendiary III alight %d; Marksman I's headshot %.0f, striking %d\n", body[0], body[1], lit, head,
                   struck);
            CHECK(fabsf(body[1] - 1.6f * body[0]) < 0.05f * body[0]);
            CHECK(lit);
            CHECK(fabsf(head - 4 * body[0]) < 0.05f * body[0] && struck == 2);
            // The Breacher's slug
            become(0, Role_Breacher, Weapon_Shotgun);
            look_at(0, zombie(line[0]).chestAt);
            tick_safe(10);
            int through[2];
            for (int slug = 0; slug < 2; slug++) {
                s = get(0);
                memset(&s.items, 0, sizeof s.items);
                s.items.at[Item_Slug] = slug;
                s.shotWait = 0;
                s.mag = 8;
                set(0, s);
                for (int k = 0; k < 4; k++) before[k] = zombie(line[k]).health;
                look_at(0, zombie(line[0]).chestAt);
                press(&held[0].attack);
                tick_safe(2);
                through[slug] = 0;
                for (int k = 0; k < 4; k++) through[slug] += zombie(line[k]).health < before[k];
            }
            printf("gun cards: a shotgun blast strikes %d in a line, with a Slug Core %d\n", through[0], through[1]);
            CHECK(through[0] == 1 && through[1] == 3);
            // The Engineer's nozzle: one 6.5 m off, out of the flames' reach, then in it
            become(0, Role_Engineer, Weapon_Flamer);
            for (int k = 1; k < 4; k++) {
                Zombie z = zombie(line[k]);
                z.at.x += 20 + 3 * k;
                CHECK(tide_set_Zombie(&world, line[k], z));
            }
            first = zombie(line[0]);
            first.at = (tide_float3){feet.x, feet.y, feet.z - 6.5f};
            CHECK(tide_set_Zombie(&world, line[0], first));
            tick_safe(2);
            float burned[2];
            for (int nozzle = 0; nozzle < 2; nozzle++) {
                s = get(0);
                memset(&s.items, 0, sizeof s.items);
                s.items.at[Item_LongNozzle] = 2 * nozzle;
                s.mag = 140;
                set(0, s);
                const float was = zombie(line[0]).health;
                look_at(0, zombie(line[0]).chestAt);
                held[0].attack = true;
                tick_safe(30);
                held[0].attack = false;
                tick_safe(1);
                burned[nozzle] = was - zombie(line[0]).health;
            }
            printf("gun cards: one 6.5 m off the flamethrower takes %.0f, with a Long Nozzle II %.0f\n", burned[0], burned[1]);
            CHECK(burned[0] == 0 && burned[1] > 0);
            // Gas Piston, taken: a fifth more rounds, loaded
            become(0, Role_Gunner, Weapon_Rifle);
            s = get(0);
            memset(&s.items, 0, sizeof s.items);
            s.mag = 10;
            set(0, s);
            s = take(0, Item_GasPiston);
            CHECK(s.mag == 36);
            s.items.at[Item_GasPiston] = 0;
            s.mag = 30;
            set(0, s);
            for (int k = 0; k < 4; k++) {
                Zombie z = zombie(line[k]);
                z.at = (tide_float3){feet.x + 20.0f + 3.0f * (float)k, feet.y, feet.z - 5.0f};
                z.burn = 0;
                z.health = 100000;
                CHECK(tide_set_Zombie(&world, line[k], z));
            }
            put(0, feet, (tide_float3){0, 1.6f, -16});
            tick_safe(10);
        }
        // Kerosene: the roll past one of the dead sets it alight
        {
            Survivor s = get(0);
            memset(&s.items, 0, sizeof s.items);
            s.items.at[Item_Kerosene] = 1;
            set(0, s);
            become(0, Role_Gunner, Weapon_Rifle);
            const tide_entity e = sturdy();
            to_open(e);
            face(0, e, 3.5f);
            press(&held[0].roll);
            bool lit = false;
            for (int t = 0; t < 30 && !lit; t++) {
                tick_safe(1);
                lit = zombie(e).burn > 0;
            }
            held[0].move = (tide_float2){0, 0};
            CHECK(lit);
        }
        // A Bile Bomb: a Bloater shot dead by its holder bursts wider, the one 4.2 m from it (past a burst's 3.6) wounded
        {
            few(2);
            become(0, Role_Gunner, Weapon_Rifle);
            Survivor s = get(0);
            memset(&s.items, 0, sizeof s.items);
            s.items.at[Item_BileBomb] = 1;
            set(0, s);
            put(0, (tide_float3){0, 0, -6}, (tide_float3){0, 1.6f, -16});
            tick_safe(10);
            const tide_float3 feet = get(0).feet;
            tide_entity pair[2];
            int found = 0;
            for (uint32_t row = 0; row < world.arch6_Zombie.count && found < 2; row++)
                if (TIDE_AT(&world, arch6_Zombie, Zombie, row).state == ZombieState_Hunting) pair[found++] = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
            CHECK(found == 2);
            hold_at(pair[0], (tide_float3){feet.x, feet.y, feet.z - 8});
            hold_at(pair[1], (tide_float3){feet.x + 4.2f, feet.y, feet.z - 8});
            Zombie z = zombie(pair[0]);
            z.kind = ZombieKind_Bloater;
            z.health = 1;
            CHECK(tide_set_Zombie(&world, pair[0], z));
            tick_safe(2);
            look_at(0, zombie(pair[0]).chestAt);
            s = get(0);
            s.shotWait = 0;
            s.mag = 30;
            set(0, s);
            const float whole = zombie(pair[1]).health;
            press(&held[0].attack);
            for (int t = 0; t < 120 && tide_entity_alive(&world.entities, pair[0]); t++) tick_safe(1);
            tick_safe(3);
            printf("a Bile Bomb: a Bloater shot dead bursts on one 4.2 m off for %.0f\n", whole - zombie(pair[1]).health);
            CHECK(!tide_entity_alive(&world.entities, pair[0]) && zombie(pair[1]).health < whole);
            s = get(0);
            memset(&s.items, 0, sizeof s.items);
            set(0, s);
        }
        // The turret, with its maker's Frostbite: what it shoots freezes
        {
            become(0, Role_Engineer, Weapon_Flamer);
            Survivor s = get(0);
            memset(&s.items, 0, sizeof s.items);
            s.items.at[Item_Frostbite] = 14;              // Always, even at the turret's lesser chance
            set(0, s);
            few(2);
            const tide_entity e = sturdy();
            to_open(e);
            face(0, e, 4);
            autos = true;
            only_skill(0, 2);
            bool froze = false;
            for (int t = 0; t < 60 * 5 && !froze; t++) {
                tick_safe(1);
                froze = zombie(e).frozen > 0;
            }
            autos = false;
            CHECK(froze);
            s = get(0);
            memset(&s.items, 0, sizeof s.items);
            set(0, s);
            tick_safe(60 * 16);         // The turret's time up
        }
        spawning();
    }

    // ---- Upgrades that show (the owner, 2026-10-20): three Gun Drones fire in turn; a Saw Blade cuts what it passes; a
    // Tesla Coil strikes the two nearest; a Thunder Drum throws one down; a Rocket Pod's rockets fly and burst; Ricochet
    // Rounds bounce on; Bone Splinters fly from a kill; a Molotov Satchel throws. Skill cards: the right mouse a leap,
    // then a leap a rank stronger that lands and knocks one down; a phase round at its third rank, five in a fan, three
    // of the dead struck; a blink at its second rank, the one by where it left thrown down ----
    {
        few(4);
        become(0, Role_Gunner, Weapon_Rifle);
        only_skill(0, -1);
        put(1, (tide_float3){24, 0, -14}, (tide_float3){24, 1, -20});
        put(0, (tide_float3){0, 0, -6}, (tide_float3){0, 1.6f, -16});
        tick_safe(10);
        const tide_float3 feet = get(0).feet;
        tide_entity d[4];
        int found = 0;
        for (uint32_t row = 0; row < world.arch6_Zombie.count && found < 4; row++)
            if (TIDE_AT(&world, arch6_Zombie, Zombie, row).state == ZombieState_Hunting) d[found++] = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
        CHECK(found == 4);
        const tide_float3 away[4] = {{feet.x - 30, feet.y, feet.z + 14}, {feet.x - 32, feet.y, feet.z + 14}, {feet.x - 34, feet.y, feet.z + 14}, {feet.x - 36, feet.y, feet.z + 14}};
        for (int k = 0; k < 4; k++) hold_at(d[k], away[k]);
        // Three Gun Drones, firing in turn
        only_item(0, Item_GunDrone, 3);
        hold_at(d[0], (tide_float3){feet.x, feet.y, feet.z - 7});
        bool fired[3] = {false, false, false};
        float whole = zombie(d[0]).health;
        for (int t = 0; t < 90; t++) {
            tick_safe(1);
            const int n = get(0).droneNext;
            if (n >= 0 && n < 3) fired[n] = true;
        }
        printf("three Gun Drones: each fired %d, %.0f to one 7 m off in 1.5 s\n", fired[0] && fired[1] && fired[2], whole - zombie(d[0]).health);
        CHECK(fired[0] && fired[1] && fired[2] && zombie(d[0]).health < whole);
        // A Saw Blade: one of the dead on its circle
        only_item(0, Item_SawBlades, 1);
        hold_at(d[0], (tide_float3){feet.x + 2.3f, feet.y, feet.z});
        whole = zombie(d[0]).health;
        int t = 0;
        for (; t < 150 && zombie(d[0]).health >= whole; t++) tick_safe(1);
        printf("a Saw Blade: one of the dead on its circle cut after %.2f s\n", t / 60.0f);
        CHECK(zombie(d[0]).health < whole);
        // A Tesla Coil: the two nearest (the dead where they're put before it's armed)
        only_item(0, Item_HollowPoints, 0);
        hold_at(d[0], (tide_float3){feet.x - 3, feet.y, feet.z - 4});
        hold_at(d[1], (tide_float3){feet.x + 3, feet.y, feet.z - 4});
        tick_safe(2);
        only_item(0, Item_TeslaCoil, 1);
        const float a0 = zombie(d[0]).health, a1 = zombie(d[1]).health;
        tick_safe(20);
        CHECK(zombie(d[0]).health < a0 && zombie(d[1]).health < a1);
        // A Thunder Drum: one 3 m off thrown down
        only_item(0, Item_HollowPoints, 0);
        hold_at(d[1], away[1]);
        hold_at(d[0], (tide_float3){feet.x, feet.y, feet.z - 3});
        tick_safe(2);
        only_item(0, Item_Shockwave, 1);
        bool over = false;
        for (t = 0; t < 30 && !over; t++) {
            tick_safe(1);
            over = zombie(d[0]).state == ZombieState_Fallen;
        }
        CHECK(over);
        // A Rocket Pod: rockets at one 10 m off
        only_item(0, Item_HollowPoints, 0);
        hold_at(d[0], (tide_float3){feet.x, feet.y, feet.z - 10});
        tick_safe(2);
        only_item(0, Item_RocketPod, 1);
        whole = zombie(d[0]).health;
        int rockets = 0;
        for (t = 0; t < 90; t++) {
            tick_safe(1);
            rockets = rockets > missiles_of(Shot_Rocket) ? rockets : missiles_of(Shot_Rocket);
        }
        printf("a Rocket Pod: %d rockets in the air at once, %.0f to one 10 m off\n", rockets, whole - zombie(d[0]).health);
        CHECK(rockets == 2 && zombie(d[0]).health < whole);
        // Ricochet Rounds: shot at one, the one beside it hurt too
        only_item(0, Item_Ricochet, 1);
        hold_at(d[0], (tide_float3){feet.x, feet.y, feet.z - 7});
        hold_at(d[1], (tide_float3){feet.x + 2.5f, feet.y, feet.z - 7});
        tick_safe(2);
        look_at(0, zombie(d[0]).chestAt);
        Survivor s = get(0);
        s.shotWait = 0;
        s.mag = 30;
        set(0, s);
        whole = zombie(d[1]).health;
        press(&held[0].attack);
        tick_safe(3);
        printf("Ricochet Rounds: %.0f to the one beside the one shot\n", whole - zombie(d[1]).health);
        CHECK(zombie(d[1]).health < whole);
        // Bone Splinters: one shot dead, the one beside it struck by a shard
        only_item(0, Item_Splinters, 1);
        hold_at(d[1], (tide_float3){feet.x + 2.0f, feet.y, feet.z - 7});
        Zombie z = zombie(d[0]);
        z.health = 1;
        CHECK(tide_set_Zombie(&world, d[0], z));
        tick_safe(2);
        look_at(0, zombie(d[0]).chestAt);
        s = get(0);
        s.shotWait = 0;
        s.mag = 30;
        set(0, s);
        whole = zombie(d[1]).health;
        press(&held[0].attack);
        tick_safe(3);
        CHECK(killed(d[0]) && zombie(d[1]).health < whole);
        // (the one shot dead: four of the dead again)
        few(4);
        found = 0;
        for (uint32_t row = 0; row < world.arch6_Zombie.count && found < 4; row++)
            if (TIDE_AT(&world, arch6_Zombie, Zombie, row).state == ZombieState_Hunting) d[found++] = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
        CHECK(found == 4);
        for (int k = 0; k < 4; k++) hold_at(d[k], away[k]);
        put(0, feet, (tide_float3){feet.x, 1.6f, feet.z - 10});
        // A Molotov Satchel: a firebomb at three of the dead together
        only_item(0, Item_HollowPoints, 0);
        hold_at(d[0], (tide_float3){feet.x, feet.y, feet.z - 8});
        hold_at(d[1], (tide_float3){feet.x + 1, feet.y, feet.z - 8});
        hold_at(d[2], (tide_float3){feet.x - 1, feet.y, feet.z - 8});
        tick_safe(2);
        only_item(0, Item_Molotovs, 1);
        int pots = 0;
        for (t = 0; t < 30 && pots == 0; t++) {
            tick_safe(1);
            pots = missiles_of(Shot_Pot);
        }
        CHECK(pots > 0);
        only_item(0, Item_HollowPoints, 0);
        for (int k = 0; k < 4; k++) hold_at(d[k], away[k]);
        tick_safe(120);              // (the fire out)
        // Skill cards: the right mouse's skill (the gun's own) a rank stronger each        // A phase round again, to its third rank: five in a fan
        s = take_card(0, Item_CardPhaseRound);
        CHECK(s.secondaryRank == 2);
        s = take_card(0, Item_CardPhaseRound);
        CHECK(s.secondaryRank == 3);
        put(0, feet, (tide_float3){feet.x, 1.6f, feet.z - 12});
        tick_safe(10);
        for (int k = 0; k < 3; k++) {
            const float turn = (float)(k - 1) * 0.16f;
            hold_at(d[k], (tide_float3){feet.x + 12.0f * sinf(turn), feet.y, feet.z - 12.0f * cosf(turn)});
        }
        tick_safe(2);
        look_at(0, zombie(d[1]).chestAt);
        const float h0 = zombie(d[0]).health, h1 = zombie(d[1]).health, h2 = zombie(d[2]).health;
        only_skill(0, 0);
        press(&held[0].secondary);
        only_skill(0, -1);
        tick_safe(3);
        printf("a phase round, rank III: %.0f, %.0f and %.0f to three of the dead across its fan\n", h0 - zombie(d[0]).health, h1 - zombie(d[1]).health, h2 - zombie(d[2]).health);
        CHECK(zombie(d[0]).health < h0 && zombie(d[1]).health < h1 && zombie(d[2]).health < h2);
        s = get(0);
        s.secondaryRank = 1;
        memset(&s.items, 0, sizeof s.items);
        set(0, s);
        spawning();
    }

    // ---- The night's chests: four, on open ground, apart; standing by one opens it, and everyone's owed a pick; a chest
    // left alone shuts again ----
    {
        heal();
        CHECK(world.Match.chestsPlaced == 4 && world.arch7_Chest.count <= 4 && world.arch7_Chest.count >= 1);
        for (uint32_t r = 0; r < world.arch7_Chest.count; r++) {
            const Chest a = TIDE_AT(&world, arch7_Chest, Chest, r);
            CHECK(a.night == world.Match.night && walkable(a.at));
            for (uint32_t q = r + 1; q < world.arch7_Chest.count; q++) {
                const Chest b = TIDE_AT(&world, arch7_Chest, Chest, q);
                CHECK(hypotf(a.at.x - b.at.x, a.at.z - b.at.z) >= 16.0f);
            }
        }
        const Chest chest = TIDE_AT(&world, arch7_Chest, Chest, 0);
        const uint32_t chests = world.arch7_Chest.count;
        put(1, (tide_float3){chest.at.x + 30, chest.at.y, chest.at.z}, chest.at);
        put(0, (tide_float3){chest.at.x + 1.0f, chest.at.y, chest.at.z}, chest.at);
        tick_safe(20);
        put(0, (tide_float3){chest.at.x + 9.0f, chest.at.y, chest.at.z}, chest.at);
        tick_safe(60);
        CHECK(world.arch7_Chest.count == chests && TIDE_AT(&world, arch7_Chest, Chest, 0).pried == 0);
        put(0, (tide_float3){chest.at.x + 1.0f, chest.at.y, chest.at.z}, chest.at);
        int t = 0;
        for (; t < 150 && world.arch7_Chest.count == chests; t++) tick_whole(1);
        tick_whole(2);
        CHECK(world.arch7_Chest.count == chests - 1);
        // A pick for each, the one who wasn't there too
        CHECK(get(0).choosing || get(0).pending > 0);
        CHECK(get(1).choosing || get(1).pending > 0);
        printf("a chest opened in %.2f s, a pick for both\n", t / 60.0);
        decline();
    }

    // ---- Missions: a hold, a cull and the wisps, each done owing everyone a pick of the rarer items ----
    {
        missions = true;
        few(3);
        heal();
        // A hold: both in the circle
        world.Match.devMission = MissionKind_Hold;
        world.Match.missionWait = 0;
        tick_safe(1);
        CHECK(world.Match.mission == MissionKind_Hold);
        const tide_float3 ring = world.Match.missionAt;
        put(0, (tide_float3){ring.x + 1.5f, ring.y, ring.z}, (tide_float3){ring.x + 9, ring.y + 1.4f, ring.z});
        put(1, (tide_float3){ring.x - 1.5f, ring.y, ring.z}, (tide_float3){ring.x - 9, ring.y + 1.4f, ring.z});
        int t = 0;
        for (; t < 60 * 15 && world.Match.mission == MissionKind_Hold; t++) {
            decline();
            tick_whole(1);
        }
        tick_whole(2);
        CHECK(world.Match.mission == MissionKind_None);
        CHECK(get(0).boons + (get(0).choosing ? 1 : 0) >= 1 && get(1).boons + (get(1).choosing ? 1 : 0) >= 1);
        printf("a hold with both in the circle, done in %.1f s, a rarer pick for both\n", t / 60.0);
        decline();
        Survivor s = get(0);
        s.boons = 0;
        set(0, s);
        s = get(1);
        s.boons = 0;
        set(1, s);
        // A cull: three of the dead shot down in its circle (of a goal of three, for the test)
        world.Match.devMission = MissionKind_Cull;
        world.Match.missionWait = 0;
        tick_safe(1);
        CHECK(world.Match.mission == MissionKind_Cull);
        world.Match.missionGoal = 3;
        const tide_float3 circle = world.Match.missionAt;
        become(0, Role_Gunner, Weapon_Rifle);
        // Stood 7 m off it on whichever side has nothing standing between (a tree's trunk took the rounds, once, from the
        // south), the dead lined up across its middle
        float side = 0;
        for (int k = 0; k < 16; k++) {
            const float turn = (float)k * 0.3926991f;
            bool clear = true;
            for (float r = 0; r <= 8 && clear; r += 0.5f) {
                int which = -1;
                for (int across = -1; across <= 1 && clear; across++)
                    if (solid_at((tide_float3){circle.x + sinf(turn) * r + cosf(turn) * 1.5f * (float)across, circle.y, circle.z + cosf(turn) * r - sinf(turn) * 1.5f * (float)across},
                                 0.6f, &which)) clear = false;
            }
            if (!clear) continue;
            // (and on the ground there, not up on something: a round goes level at the gun's height)
            side = turn;
            put(0, (tide_float3){circle.x + sinf(side) * 7, circle.y, circle.z + cosf(side) * 7}, circle);
            tick_safe(10);
            if (fabsf(get(0).feet.y - circle.y) < 0.4f) break;
        }
        int culled = 0;
        for (int k = 0; k < 6 && world.Match.mission == MissionKind_Cull; k++) {
            const tide_entity e = sturdy();
            Zombie z = zombie(e);
            z.at = (tide_float3){circle.x + cosf(side) * (k % 3 - 1) * 1.5f, circle.y, circle.z - sinf(side) * (k % 3 - 1) * 1.5f};
            z.health = 1;
            z.pace = 0;
            CHECK(tide_set_Zombie(&world, e, z));
            // The rest of the dead well off: a round stops in the first it strikes
            for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
                const tide_entity other = TIDE_ENTITY_AT(&world, arch6_Zombie, row);
                if (other.index == e.index) continue;
                Zombie o = tide_get_Zombie(&world, other);
                if (hypotf(o.at.x - circle.x, o.at.z - circle.z) > 12) continue;
                o.at.x += circle.x > 0 ? -25 : 25;
                CHECK(tide_set_Zombie(&world, other, o));
            }
            tick_safe(2);
            look_at(0, zombie(e).chestAt);
            s = get(0);
            s.shotWait = 0;
            s.mag = 30;
            set(0, s);
            press(&held[0].attack);
            tick_safe(3);
            if (!tide_entity_alive(&world.entities, e) || zombie(e).state == ZombieState_Dying) culled++;
            if (world.Match.alive < 2) few(3);
        }
        tick_whole(1);
        printf("a cull: %d shot down in its circle, the mission %s\n", culled, world.Match.mission == MissionKind_None ? "done" : "still going");
        CHECK(world.Match.mission == MissionKind_None && world.Match.missionDone >= world.Match.missionGoal);
        decline();
        s = get(0);
        s.boons = 0;
        set(0, s);
        s = get(1);
        s.boons = 0;
        set(1, s);
        // The wisps: scattered round its place, apart; walked to, each caught; all caught, done
        world.Match.devMission = MissionKind_Wisps;
        world.Match.missionWait = 0;
        tick_safe(1);
        CHECK(world.Match.mission == MissionKind_Wisps);
        const int wisps = tide_list_count(world.Match.wisps);
        CHECK(wisps >= 3 && wisps <= 5 && (int)world.Match.missionGoal == wisps);
        tide_float3 spots[5];
        for (int k = 0; k < wisps; k++) spots[k] = *(const tide_float3 *)tide_list_at(world.Match.wisps, k, sizeof(tide_float3));
        float nearest = 1000, farthest = 0;
        for (int k = 0; k < wisps; k++) {
            const float off = hypotf(spots[k].x - world.Match.missionAt.x, spots[k].z - world.Match.missionAt.z);
            nearest = fminf(nearest, off);
            farthest = fmaxf(farthest, off);
        }
        printf("the wisps: %d, %.1f to %.1f m from its place\n", wisps, nearest, farthest);
        CHECK(nearest >= 5.9f && farthest <= 18.1f);
        // One walked near: caught, and only it
        put(0, (tide_float3){spots[0].x + 1.0f, spots[0].y, spots[0].z}, (tide_float3){spots[0].x + 9, 1.4f, spots[0].z});
        tick_safe(2);
        CHECK(tide_list_count(world.Match.wisps) == wisps - 1 && (int)world.Match.missionDone == 1);
        for (int k = 1; k < wisps; k++) {
            put(0, (tide_float3){spots[k].x - 1.0f, spots[k].y, spots[k].z}, (tide_float3){spots[k].x - 9, 1.4f, spots[k].z});
            tick_whole(2);
        }
        tick_whole(2);
        CHECK(world.Match.mission == MissionKind_None && world.Match.missionDone >= world.Match.missionGoal && tide_list_count(world.Match.wisps) == 0);
        CHECK(get(0).boons + (get(0).choosing ? 1 : 0) >= 1 && get(1).boons + (get(1).choosing ? 1 : 0) >= 1);
        printf("the wisps: all caught, done, a rarer pick for both\n");
        decline();
        s = get(0);
        s.boons = 0;
        set(0, s);
        s = get(1);
        s.boons = 0;
        set(1, s);
        missions = false;
        spawning();
    }

    // ---- Experience as orbs: a kill leaves one where it fell; it lies there while nobody's near, flies to a survivor
    // who comes within 5 m and is the party's when it reaches them; one nobody comes for is gone after 40 s ----
    {
        few(2);
        become(0, Role_Gunner, Weapon_Rifle);
        // Orbs left lying from before gone, so the one this makes is the only one
        for (uint32_t r = 0; r < world.arch5_Orb.count; r++) {
            const tide_entity eo = TIDE_ENTITY_AT(&world, arch5_Orb, r);
            Orb o = tide_get_Orb(&world, eo);
            o.age = 1000;
            o.seat = -1;
            CHECK(tide_set_Orb(&world, eo, o));
        }
        tick_safe(2);
        CHECK(world.arch5_Orb.count == 0);
        const uint32_t orbs = world.arch5_Orb.count;
        for (int k = 0; k < 2; k++) {
            const tide_entity e = sturdy();
            to_open(e);
            Zombie z = zombie(e);
            z.health = 1;
            CHECK(tide_set_Zombie(&world, e, z));
            face(0, e, 8);           // Out of an orb's reach
            const Survivor shooter = get(0);
            const tide_float3 fell = zombie(e).at;
            put(1, shooter.feet, zombie(e).chestAt);
            press(&held[0].attack);
            tick_safe(4);
            CHECK(!tide_entity_alive(&world.entities, e));      // Torn apart: nothing left lying
            CHECK(world.arch5_Orb.count == orbs + 1);
            const float xp = world.Match.xp;
            const int level = world.Match.level;
            if (k == 0) {
                // Lying while nobody's near; taken from 4 m
                tick_safe(60);
                CHECK(world.arch5_Orb.count == orbs + 1 && world.Match.xp == xp);
                put(0, (tide_float3){fell.x + (shooter.feet.x - fell.x) * 0.5f, shooter.feet.y, fell.z + (shooter.feet.z - fell.z) * 0.5f}, fell);
                int t = 0;
                for (; t < 120 && world.arch5_Orb.count > orbs; t++) tick_safe(1);
                CHECK(world.arch5_Orb.count == orbs && (world.Match.xp > xp || world.Match.level > level));
                printf("an orb taken from 4 m in %.2f s\n", t / 60.0);
            } else {
                // Left alone, gone
                tick_safe(60 * 41);
                if (world.arch5_Orb.count != orbs || world.Match.xp != xp)
                    fprintf(stderr, "a lone orb: %u orbs (were %u), experience %.2f (was %.2f), %u of the dead\n", world.arch5_Orb.count, orbs, world.Match.xp, xp, world.arch6_Zombie.count);
                CHECK(world.arch5_Orb.count == orbs && world.Match.xp == xp);
            }
        }
        spawning();
    }

    // ---- The bosses. The Mauler claws up, slams the survivor close to it and charges the one further off, then stands
    // spent a moment; a round to the sores on its back does three times what one to its hips does; felled, as one of the
    // night's own, both have an item to pick. The Broodmother lobs a spread of bile and births a pack of runners. The
    // Stalker leaps to where it aimed; its shriek frenzies the dead near it, or, with enough to its throat, is cut short ----
    {
        cull();
        spawning();
        world.Match.devBoss = BossKind_Mauler;
        for (int t = 0; t < 60 * 2 && world.arch1_Boss.count == 0; t++) tick_safe(1);
        CHECK(world.arch1_Boss.count == 1);
        tide_entity e = TIDE_ENTITY_AT(&world, arch1_Boss, 0);
        CHECK(!tide_get_Boss(&world, e).own && tide_get_Boss(&world, e).kind == BossKind_Mauler);       // The key's, not one of the night's own
        CHECK(tide_get_Boss(&world, e).health > 1000);
        tick_safe(60 * 3);     // Up out of the ground
        CHECK(tide_get_Boss(&world, e).move != BossMove_Coming && tide_get_Boss(&world, e).move != BossMove_Dying);
        bool slammed = false, charged = false, spent = false;
        for (int t = 0; t < 60 * 40 && !(slammed && charged && spent); t++) {
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
            if (b.move == BossMove_Recover) spent = true;
        }
        printf("the Mauler: slammed %d, charged %d, stood spent %d\n", slammed, charged, spent);
        CHECK(slammed && charged && spent);
        // A slam rolled through: nothing it does lands while the roll's guard is up
        {
            Boss b = tide_get_Boss(&world, e);
            b.at = (tide_float3){0, 0, -14};
            b.yaw = 0;
            b.aim = (tide_float3){0, 0, -11.5f};
            b.move = BossMove_SlamWindup;
            b.timer = 1.1f - 0.03f;          // Coming down
            CHECK(tide_set_Boss(&world, e, b));
            put(1, (tide_float3){30, 0, 30}, (tide_float3){30, 1.6f, 40});
            put(0, (tide_float3){0, 0, -10.5f}, (tide_float3){5, 1.36f, -10.5f});
            Survivor s = get(0);
            s.health = 100;
            s.roll = 0.5f;                   // A roll just begun (ROLL_SECONDS)
            s.rollWay = (tide_float3){1, 0, 0};
            set(0, s);
            tick(4);
            decline();
            printf("a slam rolled through: %.0f of 100 health left, the Mauler's move %d\n", get(0).health, (int)tide_get_Boss(&world, e).move);
            CHECK(get(0).health >= 100 && tide_get_Boss(&world, e).move != BossMove_SlamWindup);     // (none lost; a little back, perhaps)
            tick_safe(30);           // The roll done (no shot in one)
        }
        // Its weak spot: held still on open ground, facing north, none of the dead about to take a round meant for it; a
        // round to its hips from the front, one to its back's sores from behind
        become(0, Role_Gunner, Weapon_Rifle);
        world.Match.spawnWait = 1e9f;
        cull();
        float hits[3] = {0};
        for (int k = 0; k < 3; k++) {
            Boss b = tide_get_Boss(&world, e);
            b.at = (tide_float3){0, 0, -14};
            b.yaw = 0;
            b.health = b.healthMost = 1e7f;
            b.move = BossMove_Stagger;
            b.timer = 0;
            CHECK(tide_set_Boss(&world, e, b));
            // (the third at its feet, in front: its bulk takes it)
            const tide_float3 aim = k == 0 ? (tide_float3){0, 0.78f * 2.3f, -14 + 0.05f * 2.3f} : k == 1 ? (tide_float3){0, 1.29f * 2.3f, -14 + 0.19f * 2.3f}
                                                                        : (tide_float3){0.4f, 0.25f, -13.3f};
            put(0, (tide_float3){0, 0, k == 1 ? -23.0f : -5.0f}, aim);
            tick_safe(2);
            b = tide_get_Boss(&world, e);
            b.timer = 0;
            CHECK(tide_set_Boss(&world, e, b));
            Survivor s = get(0);
            s.shotWait = 0;
            s.mag = 30;
            set(0, s);
            look_at(0, aim);
            const float before = tide_get_Boss(&world, e).health;
            press(&held[0].attack);
            tick_safe(1);
            hits[k] = before - tide_get_Boss(&world, e).health;
        }
        printf("a carbine round to the Mauler: %.1f to its hips, %.1f to the sores on its back, %.1f at its feet\n", hits[0], hits[1], hits[2]);
        CHECK(hits[0] > 0 && hits[1] > hits[0] * 2.5f && hits[2] > 0);
        spawning();
        // Felled: one of the night's own (as if it had risen at the night's end) owes each a pick
        decline();
        const int picks0 = 0, picks1 = 0;
        Boss b = tide_get_Boss(&world, e);
        b.health = 1;
        b.own = true;
        b.at = (tide_float3){0, 0, -14};      // On open ground south of the fire, nothing between it and the gun
        CHECK(tide_set_Boss(&world, e, b));
        bool felled = false;
        for (int t = 0; t < 60 * 5 && !felled; t++) {
            b = tide_get_Boss(&world, e);
            put(0, (tide_float3){b.at.x, 0, b.at.z + 8}, (tide_float3){b.at.x, b.at.y + 2, b.at.z});
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
        if (!felled) {
            b = tide_get_Boss(&world, e);
            fprintf(stderr, "the Mauler wasn't felled: health %.1f, move %d, at %.2f %.2f %.2f; survivor at %.2f %.2f %.2f, life %d, act %d, %u of the dead\n", b.health, (int)b.move,
                    b.at.x, b.at.y, b.at.z, get(0).feet.x, get(0).feet.y, get(0).feet.z, (int)get(0).life, (int)get(0).act, world.arch6_Zombie.count);
        }
        CHECK(felled);
        tick_whole(2);
        CHECK(get(0).pending + (get(0).choosing ? 1 : 0) >= picks0 + 1);
        CHECK(get(1).pending + (get(1).choosing ? 1 : 0) >= picks1 + 1);
        // Its pick offered only once its fall's been seen (BOSS_REWARD_WAIT, 2.5 s)
        CHECK(!get(0).choosing && get(0).rewardWait > 2);
        tick_whole(60 * 3);
        CHECK(get(0).choosing && get(0).rewardWait <= 0);
        tick_safe(60 * 6);
        CHECK(world.arch1_Boss.count == 0);

        // The Broodmother: a volley, five globs of bile; a birth, a pack of runners
        few(2);
        world.Match.devBoss = BossKind_Brood;
        for (int t = 0; t < 60 * 2 && world.arch1_Boss.count == 0; t++) tick_safe(1);
        CHECK(world.arch1_Boss.count == 1);
        e = TIDE_ENTITY_AT(&world, arch1_Boss, 0);
        CHECK(tide_get_Boss(&world, e).kind == BossKind_Brood);
        tick_safe(60 * 3);
        b = tide_get_Boss(&world, e);
        put(0, (tide_float3){b.at.x + 12, 0, b.at.z}, b.at);
        put(1, (tide_float3){b.at.x - 12, 0, b.at.z}, b.at);
        b.move = BossMove_VolleyWindup;
        b.timer = 0.85f;      // About to lob
        CHECK(tide_set_Boss(&world, e, b));
        const int globs = missiles_of(Shot_Spit);
        tick_safe(6);
        CHECK(missiles_of(Shot_Spit) >= globs + 5);
        b = tide_get_Boss(&world, e);
        b.move = BossMove_Birth;
        b.timer = 2.35f;      // About to give birth
        CHECK(tide_set_Boss(&world, e, b));
        const uint32_t up = world.arch6_Zombie.count;
        tick_safe(6);
        printf("the Broodmother: %d globs of bile, %u runners born\n", missiles_of(Shot_Spit) - globs, world.arch6_Zombie.count - up);
        CHECK(world.arch6_Zombie.count >= up + 6);
        b = tide_get_Boss(&world, e);
        b.move = BossMove_Dying;
        b.timer = 0;
        CHECK(tide_set_Boss(&world, e, b));
        tick_safe(60 * 6);
        CHECK(world.arch1_Boss.count == 0);

        // The Stalker: a leap to where it aimed; a shriek that frenzies the dead near it; one cut short by a round to its
        // throat
        few(2);
        world.Match.devBoss = BossKind_Stalker;
        for (int t = 0; t < 60 * 2 && world.arch1_Boss.count == 0; t++) tick_safe(1);
        CHECK(world.arch1_Boss.count == 1);
        e = TIDE_ENTITY_AT(&world, arch1_Boss, 0);
        CHECK(tide_get_Boss(&world, e).kind == BossKind_Stalker);
        tick_safe(60 * 3);
        bool leapt = false;
        tide_float3 aimed = {0}, began = {0};
        for (int t = 0; t < 60 * 20 && !leapt; t++) {
            b = tide_get_Boss(&world, e);
            if (t % 120 == 0) {
                put(0, (tide_float3){b.at.x + 14, 0, b.at.z}, b.at);
                put(1, (tide_float3){b.at.x + 15, 0, b.at.z + 2}, b.at);
            }
            const BossMove was = b.move;
            tick_safe(1);
            b = tide_get_Boss(&world, e);
            if (was == BossMove_LeapWindup && b.move == BossMove_Leap) {
                aimed = b.aim;
                began = b.from;
            }
            if (was == BossMove_Leap && b.move != BossMove_Leap) leapt = true;
        }
        b = tide_get_Boss(&world, e);
        printf("the Stalker leapt %.1f m, landing %.2f m from where it aimed\n", hypotf(aimed.x - began.x, aimed.z - began.z), hypotf(b.at.x - aimed.x, b.at.z - aimed.z));
        CHECK(leapt && hypotf(aimed.x - began.x, aimed.z - began.z) > 5 && hypotf(b.at.x - aimed.x, b.at.z - aimed.z) < 0.5f);
        // A shriek, one of the dead beside it
        tide_entity near = sturdy();
        Zombie z = zombie(near);
        z.at = (tide_float3){b.at.x + 3, b.at.y, b.at.z};
        z.pace = 0;
        CHECK(tide_set_Zombie(&world, near, z));
        put(0, (tide_float3){b.at.x + 20, 0, b.at.z + 20}, b.at);
        put(1, (tide_float3){b.at.x + 21, 0, b.at.z + 20}, b.at);
        b.move = BossMove_Shriek;
        b.timer = 0;
        b.hurt = 0;
        CHECK(tide_set_Boss(&world, e, b));
        tick_safe(60 * 2);
        CHECK(zombie(near).frenzy > 0);
        // Cut short: a round to its throat, worth more than a tenth of it
        b = tide_get_Boss(&world, e);
        b.at = (tide_float3){0, 0, -14};
        b.yaw = 3.14159265f;      // Facing south
        b.health = b.healthMost = 1000;
        b.move = BossMove_Shriek;
        b.timer = 0;
        b.hurt = 0;
        CHECK(tide_set_Boss(&world, e, b));
        const tide_float3 throat = {0, 1.51f * 2.0f, -14 - 0.44f * 2.0f};
        put(0, (tide_float3){0, 0, -22}, throat);
        put(1, (tide_float3){30, 0, 30}, (tide_float3){30, 1.6f, 40});
        tick_safe(2);
        b = tide_get_Boss(&world, e);
        b.timer = 0;
        CHECK(tide_set_Boss(&world, e, b));
        Survivor s = get(0);
        s.shotWait = 0;
        s.mag = 30;
        set(0, s);
        look_at(0, throat);
        press(&held[0].attack);
        tick_safe(2);
        b = tide_get_Boss(&world, e);
        printf("a round to the Stalker's throat as it shrieked: %.0f of its 1000, its move %d (a stagger is %d)\n", 1000 - b.health, (int)b.move, (int)BossMove_Stagger);
        CHECK(b.move == BossMove_Stagger);
        b.move = BossMove_Dying;
        b.timer = 0;
        CHECK(tide_set_Boss(&world, e, b));
        tick_safe(60 * 6);
        CHECK(world.arch1_Boss.count == 0);
        spawning();
    }

    // ---- Holding the fire: the dead come from every side, and every one of them has to find its way to the survivors.
    // Both stand by the fire and shoot the nearest for three minutes, and take the third item at each level.
    {
        cull();
        spawning();
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
                for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
                    const Zombie z = TIDE_AT(&world, arch6_Zombie, Zombie, row);
                    if (z.state != ZombieState_Hunting) continue;
                    const float dx = z.at.x - s.feet.x, dz = z.at.z - s.feet.z;
                    const float d = sqrtf(dx * dx + dz * dz);
                    if (d < nearest) { nearest = d; at = z.chestAt; }
                }
                if (nearest < 20.0f) look_at(seat, at);
                held[seat].attack = nearest < 20.0f;
            }
            // How long the oldest of the dead has been about (not a Spitter or a Howler, which hang back on purpose)
            for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
                const Zombie z = TIDE_AT(&world, arch6_Zombie, Zombie, row);
                if (z.state == ZombieState_Hunting && z.kind != ZombieKind_Spitter && z.kind != ZombieKind_Howler && (int)z.timer > oldest) oldest = (int)z.timer;
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
        for (uint32_t row = 0; row < world.arch6_Zombie.count; row++) {
            const Zombie z = TIDE_AT(&world, arch6_Zombie, Zombie, row);
            if (z.state != ZombieState_Hunting || z.timer < 120 || z.kind == ZombieKind_Spitter || z.kind == ZombieKind_Howler) continue;
            const int cx = (int)floorf(z.at.x + NAV_HALF), cz = (int)floorf(z.at.z + NAV_HALF);
            const int field = *(const uint8_t *)tide_list_read(&world.heap, world.Ashwick.field, cz * NAV_SIDE + cx, 1);
            fprintf(stderr, "stuck for %.0f s at %.2f %.2f %.2f, walkable %d, field %d, kind %d lift %.2f climbing %d speed %.2f yaw %.2f\n", z.timer, z.at.x, z.at.y, z.at.z, walkable(z.at), field,
                    (int)z.kind, z.lift, z.climbing, z.speed, z.yaw);
        }
        const int taken = get(0).picks + get(1).picks - picked;
        printf("held the fire to %.0f s, %d items taken, level %d, %d of the dead up; the longest any hunted: %d s\n", world.Match.clock, taken, world.Match.level,
               world.Match.alive, oldest);
        CHECK(world.Match.clock >= last && world.Match.phase == Phase_Fighting);
        CHECK(taken >= 4);
        CHECK(oldest < 180);
    }

    // ---- The hazards. A plague pit: a survivor walking at it stops at its edge, the dead's way goes round it (its cells
    // walls), and a round goes over it into one of the dead beyond. The bramble thickets: a survivor wades through one at
    // half pace ----
    {
        decline();
        cull();
        become(0, Role_Gunner, Weapon_Rifle);
        become(1, Role_Breacher, Weapon_Shotgun);
        // The pits: the plan's low boxes (their half height (PIT_RIM + 1) / 2), the nearest the fire
        int pits = 0;
        Solid pit = {0};
        for (int k = 0; k < tide_list_read_count(&world.heap, world.Ashwick.solids); k++) {
            const Solid solid = *(const Solid *)tide_list_read(&world.heap, world.Ashwick.solids, k, sizeof(Solid));
            if (fabsf(solid.half.y - 0.85f) > 0.001f || solid.half.x < 1.0f || solid.half.z < 1.0f) continue;
            if (pits == 0 || hypotf(solid.center.x, solid.center.z) < hypotf(pit.center.x, pit.center.z)) pit = solid;
            pits++;
        }
        printf("%d plague pits; the nearest the fire %.1f by %.1f m, %.0f m off\n", pits, pit.half.x * 2, pit.half.z * 2, hypotf(pit.center.x, pit.center.z));
        CHECK(pits >= 8);
        const float west = pit.center.x - pit.half.x, east = pit.center.x + pit.half.x;
        CHECK(!walkable(pit.center) && walkable((tide_float3){west - 1.5f, 0, pit.center.z}) && walkable((tide_float3){east + 1.5f, 0, pit.center.z}));
        // Walking at it from the west along its middle
        put(1, (tide_float3){west - 3, 0, pit.center.z + 30}, (tide_float3){west, 1, pit.center.z});
        put(0, (tide_float3){west - 3, 0, pit.center.z}, (tide_float3){east + 20, 1, pit.center.z});
        held[0].move = (tide_float2){1, 0};
        float most = -1e9f;
        for (int t = 0; t < 60 * 3; t++) {
            tick_safe(1);
            most = fmaxf(most, get(0).feet.x);
        }
        held[0].move = (tide_float2){0, 0};
        printf("walking into a pit: stopped %.2f m short of its edge\n", west - most);
        CHECK(most < west - 0.2f && most > west - 1.0f);
        // A round over it, into one of the dead beyond
        few(2);
        const tide_entity beyond = sturdy();
        Zombie z = zombie(beyond);
        z.at = (tide_float3){east + 3, 0, pit.center.z};
        z.pace = 0;
        CHECK(tide_set_Zombie(&world, beyond, z));
        put(0, (tide_float3){west - 2, 0, pit.center.z}, (tide_float3){east + 3, 1.2f, pit.center.z});
        tick_safe(24);
        look_at(0, zombie(beyond).chestAt);
        const float whole = zombie(beyond).health;
        press(&held[0].attack);
        tick_safe(2);
        printf("a round over the pit: %.1f to one of the dead beyond it\n", whole - zombie(beyond).health);
        CHECK(zombie(beyond).health < whole);

        // The thickets: a dozen or more, and walking through one goes half as far as on open ground
        const int thickets = tide_list_read_count(&world.heap, world.Ashwick.brambles);
        CHECK(thickets >= 10);
        const tide_float3 thicket = *(const tide_float3 *)tide_list_read(&world.heap, world.Ashwick.brambles, 0, sizeof(tide_float3));
        float walked[2];
        for (int k = 0; k < 2; k++) {
            // (in it, walking across its middle; then the same away on the open ground by the fire)
            const tide_float3 from = k == 0 ? (tide_float3){thicket.x, 0, thicket.y - thicket.z * 0.6f} : (tide_float3){4, 0, -6};
            put(0, from, (tide_float3){from.x, 1, from.z + 10});
            tick_safe(30);
            const tide_float3 start = get(0).feet;
            held[0].move = (tide_float2){0, 1};
            tick_safe(30);
            held[0].move = (tide_float2){0, 0};
            walked[k] = hypotf(get(0).feet.x - start.x, get(0).feet.z - start.z);
        }
        printf("%d bramble thickets (one %.1f m round at %.1f, %.1f); half a second's walk %.2f m in one, %.2f m on open ground\n", thickets, thicket.z, thicket.x,
               thicket.y, walked[0], walked[1]);
        CHECK(walked[0] < walked[1] * 0.65f && walked[0] > walked[1] * 0.35f);
        spawning();
    }

    // ---- A night kept, on its clock: one bleeds out and stays gone; the night's end comes: the wake-fire's ring rises
    // round the one standing, its boss claws up in it, and the one who bled out is back by its middle; walking at the
    // ring, they stop at it; not dawn while the boss stands; down, dawn and the ring's gone: the dead burn away and none
    // rise, the orbs fly in, everyone's up and whole; then the next night falls, its clock from the start, with new
    // chests, a step more dangerous. One who bleeds out with no boss coming is back beside a friend a minute later ----
    {
        decline();
        become(0, Role_Gunner, Weapon_Rifle);
        become(1, Role_Breacher, Weapon_Shotgun);
        put(0, (tide_float3){0, 0, -14}, (tide_float3){0, 1.6f, -24});
        // Bled out: still gone forty seconds on
        Survivor friend = get(1);
        friend.life = Life_Downed;
        friend.bleed = 0.05f;
        friend.health = 0;
        friend.stowed = friend.mag;
        friend.gun = Weapon_Pistol;
        set(1, friend);
        tick(10);
        CHECK(get(1).life == Life_Gone);
        tick_safe(60 * 40);
        CHECK(get(1).life == Life_Gone);
        // The night's clock runs out
        const int night = world.Match.night;
        CHECK(!world.Match.reckoning && world.Match.nightClock < world.Match.nightEnds);
        timed = true;
        world.Match.nightClock = world.Match.nightEnds - 0.5f;
        for (int t = 0; t < 60 * 2 && !world.Match.reckoning; t++) tick_safe(1);
        CHECK(world.Match.reckoning);
        tick_safe(2);
        // The ring, round the one standing; back by its middle, with the shotgun in hand
        const tide_float3 ring = world.Match.ringAt;
        const float reach = world.Match.ringReach;
        CHECK(reach > 10 && hypotf(ring.x - get(0).feet.x, ring.z - get(0).feet.z) < 0.5f);
        friend = get(1);
        CHECK(friend.life == Life_Standing && hypotf(friend.feet.x - ring.x, friend.feet.z - ring.z) < 4 && friend.gun == Weapon_Shotgun);
        // Its boss, one of the night's own, in the ring with them; held off where it rose (this is about the night)
        tide_float3 rose[4];
        int brutes = 0;
        for (uint32_t r = 0; r < world.arch1_Boss.count && brutes < 4; r++)
            if (TIDE_AT(&world, arch1_Boss, Boss, r).own) rose[brutes++] = TIDE_AT(&world, arch1_Boss, Boss, r).at;
        CHECK(brutes == 1);
        const float off = hypotf(rose[0].x - ring.x, rose[0].z - ring.z);
        printf("the night's end: the wake-fire's ring %.0f m round them, its boss clawing up in it %.1f m from its middle\n", reach, off);
        CHECK(off > 7.5f && off < reach - 3.5f);
        // Walking out at the ring from a metre inside it, on open ground, for a second: stopped at it
        float wx = 1, wz = 0;
        for (int k = 0; k < 16; k++) {
            const float turn = (float)k * 0.3926991f;
            const tide_float3 a = {ring.x + sinf(turn) * (reach - 1), 0, ring.z + cosf(turn) * (reach - 1)};
            const tide_float3 b = {ring.x + sinf(turn) * (reach + 1.5f), 0, ring.z + cosf(turn) * (reach + 1.5f)};
            int which = -1;
            if (!flat_ground(a) || !flat_ground(b) || solid_at(a, 1.0f, &which) || solid_at(b, 1.0f, &which)) continue;
            wx = sinf(turn);
            wz = cosf(turn);
            break;
        }
        put(0, (tide_float3){ring.x + wx * (reach - 1), 0, ring.z + wz * (reach - 1)}, (tide_float3){ring.x, 1, ring.z});
        held[0].move = (tide_float2){wx, wz};
        float farthest = 0;
        for (int i = 0; i < 60; i++) {
            for (uint32_t r = 0; r < world.arch1_Boss.count; r++) {
                const tide_entity eb = TIDE_ENTITY_AT(&world, arch1_Boss, r);
                Boss b = tide_get_Boss(&world, eb);
                b.at = rose[0];
                b.cooldown = 9;
                CHECK(tide_set_Boss(&world, eb, b));
            }
            tick_safe(1);
            farthest = fmaxf(farthest, hypotf(get(0).feet.x - ring.x, get(0).feet.z - ring.z));
        }
        held[0].move = (tide_float2){0, 0};
        printf("walking at the ring: %.2f m from its middle at most\n", farthest);
        CHECK(farthest <= reach && farthest > reach - 1.5f);
        // Not dawn while it stands
        for (int i = 0; i < 30; i++) {
            for (uint32_t r = 0; r < world.arch1_Boss.count; r++) {
                const tide_entity eb = TIDE_ENTITY_AT(&world, arch1_Boss, r);
                Boss b = tide_get_Boss(&world, eb);
                if (!b.own) continue;
                b.at = rose[0];
                b.cooldown = 9;
                CHECK(tide_set_Boss(&world, eb, b));
            }
            tick_safe(1);
        }
        CHECK(world.Match.phase == Phase_Fighting && world.Match.reckoning);
        // Its boss down: dawn
        const float before = world.Match.danger;
        for (uint32_t r = 0; r < world.arch1_Boss.count; r++) {
            const tide_entity eb = TIDE_ENTITY_AT(&world, arch1_Boss, r);
            Boss b = tide_get_Boss(&world, eb);
            b.move = BossMove_Dying;
            b.timer = 0;
            CHECK(tide_set_Boss(&world, eb, b));
        }
        tick_safe(2);
        CHECK(world.Match.phase == Phase_Dawn && world.Match.kept == night && world.Match.ringReach == 0);
        tick_safe(60 * 3);
        int up = 0;
        for (uint32_t r = 0; r < world.arch6_Zombie.count; r++)
            if (TIDE_AT(&world, arch6_Zombie, Zombie, r).state != ZombieState_Dying) up++;
        CHECK(up == 0 && world.Match.alive == 0);
        tick_safe(60 * 3);
        CHECK(world.arch5_Orb.count == 0);
        CHECK(get(0).life == Life_Standing && get(1).life == Life_Standing);
        // The next night, its clock from the start
        for (int i = 0; i < 60 * 8 && world.Match.phase == Phase_Dawn; i++) tick_safe(1);
        tick_safe(30);
        CHECK(world.Match.phase == Phase_Fighting && world.Match.night == night + 1 && !world.Match.reckoning);
        CHECK(world.Match.nightClock < 1 && world.Match.nightEnds == 180);
        CHECK(world.Match.chestsPlaced == 4 && world.arch7_Chest.count == 4);
        for (uint32_t r = 0; r < world.arch7_Chest.count; r++) CHECK(TIDE_AT(&world, arch7_Chest, Chest, r).night == night + 1);
        printf("night %d kept; night %d falls, danger %.2f to %.2f\n", night, world.Match.night, before, world.Match.danger);
        CHECK(world.Match.danger > before * 1.15f);
        timed = false;
        // Bled out with no boss coming: back beside a friend a minute later
        friend = get(1);
        friend.life = Life_Downed;
        friend.bleed = 0.05f;
        friend.health = 0;
        set(1, friend);
        tick(10);
        CHECK(get(1).life == Life_Gone);
        tick_safe(60 * 55);
        CHECK(get(1).life == Life_Gone);
        tick_safe(60 * 6);
        friend = get(1);
        if (friend.life != Life_Standing || hypotf(friend.feet.x - get(0).feet.x, friend.feet.z - get(0).feet.z) >= 6)
            fprintf(stderr, "bled out: life %d, gone %.1f s, health %.0f, %.1f m from the other (life %d), paused %d, choosing %d %d\n", (int)friend.life, friend.gone,
                    friend.health, hypotf(friend.feet.x - get(0).feet.x, friend.feet.z - get(0).feet.z), (int)get(0).life, world.Match.paused, get(0).choosing,
                    friend.choosing);
        // (set down 2.4 m from where the other stood, who may have walked on a little since)
        CHECK(friend.life == Life_Standing && hypotf(friend.feet.x - get(0).feet.x, friend.feet.z - get(0).feet.z) < 6);
        puts("bled out with no boss coming: back beside a friend a minute later");
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
