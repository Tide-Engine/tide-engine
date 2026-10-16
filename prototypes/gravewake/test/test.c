// GRAVEWAKE played with no window: two survivors hold round one with their
// swords, then buy their way round Ashwick: a door, a weapon off a wall, the
// chest, the beacon, a brew, the Runeforge; one goes down and the other raises
// them; a crossbow bolt finds one of the dead; a grave bell rings in the
// chapel; a forged blade's frost slows and freezes, and a wound bleeds; then
// both hold the square for rounds with every door open. What they
// press goes through the input, as a player's does; where they'd walk, the
// test puts them.
#include "gravewake_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "GRAVEWAKE check failed: %s (%s:%d), round %d phase %d\n", #value, __FILE__, __LINE__, world.Match.round, (int)world.Match.phase); \
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

// Both on their feet and whole, so the dead don't end a test that's about something else.
static void heal(void)
{
    for (int seat = 0; seat < 2; seat++) {
        Survivor s = get(seat);
        if (s.life != Life_Standing) continue;
        s.health = s.brews & (1 << Brew_Ironhide) ? 250.0f : 100.0f;
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

static void press_use(int seat)
{
    held[seat].use = true;
    tick_safe(1);
    held[seat].use = false;
    tick_safe(1);
}

static int count_zombies(void) { return (int)world.arch4_Zombie.count; }

static bool has_weapon(Survivor s, Weapon w)
{
    for (int i = 0; i < 3; i++)
        if (s.weapons.at[i] == w) return true;
    return false;
}

int main(void)
{
    tide_world_init(&world, 1.0f / 60.0f);
    tide_world_player_joined(&world, tide_player_from_index(0));
    tide_world_player_joined(&world, tide_player_from_index(1));
    held[0].ready = held[1].ready = true;
    tick(2);
    CHECK(world.Party.joined == 2);
    CHECK(world.Ashwick.made);
    CHECK(get(0).weapons.at[0] == Weapon_RustySword && get(0).weapons.at[1] == Weapon_HandCrossbow);

    // Where the dead come in is clear of everything solid: where one lands climbing through a window, and where one
    // rises out of the ground
    for (int w = 0; w < tide_list_read_count(&world.heap, world.Ashwick.windows); w++) {
        const Window window = *(const Window *)tide_list_read(&world.heap, world.Ashwick.windows, w, sizeof(Window));
        for (int k = 0; k < tide_list_read_count(&world.heap, world.Ashwick.solids); k++) {
            const Solid solid = *(const Solid *)tide_list_read(&world.heap, world.Ashwick.solids, k, sizeof(Solid));
            if (solid.gate >= 0) continue;
            const bool inside = fabsf(window.inside.x - solid.center.x) < solid.half.x + 0.3f &&
                                fabsf(window.inside.z - solid.center.z) < solid.half.z + 0.3f;
            if (inside) fprintf(stderr, "window %d lands at %.2f %.2f, in solid %d\n", w, window.inside.x, window.inside.z, k);
            CHECK(!inside);
        }
    }
    for (int r = 0; r < tide_list_read_count(&world.heap, world.Ashwick.risers); r++) {
        const Riser riser = *(const Riser *)tide_list_read(&world.heap, world.Ashwick.risers, r, sizeof(Riser));
        for (int k = 0; k < tide_list_read_count(&world.heap, world.Ashwick.solids); k++) {
            const Solid solid = *(const Solid *)tide_list_read(&world.heap, world.Ashwick.solids, k, sizeof(Solid));
            if (solid.gate >= 0) continue;
            const bool inside = fabsf(riser.at.x - solid.center.x) < solid.half.x + 0.3f &&
                                fabsf(riser.at.z - solid.center.z) < solid.half.z + 0.3f;
            if (inside) fprintf(stderr, "riser %d at %.2f %.2f, in solid %d\n", r, riser.at.x, riser.at.z, k);
            CHECK(!inside);
        }
    }

    // ---- Round one: the dead come, from the windows and the ground ----
    for (int i = 0; i < 60 * 15 && world.Match.phase != Phase_Fighting; i++) tick(1);
    CHECK(world.Match.phase == Phase_Fighting && world.Match.round == 1);
    tick_safe(60 * 6);
    CHECK(count_zombies() > 0);

    // Survivor 0 goes to each of them in turn and swings until round two comes
    int least_boards = 6, swung = 0;
    for (int t = 0; t < 60 * 240 && world.Match.round < 2; t++) {
        for (int w = 0; w < tide_list_read_count(&world.heap, world.Match.boards); w++) {
            const int boards = *(const int32_t *)tide_list_read(&world.heap, world.Match.boards, w, sizeof(int32_t));
            if (boards < least_boards) least_boards = boards;
        }
        float nearest = 1e9f;
        Zombie target = {0};
        bool found = false;
        for (uint32_t row = 0; row < world.arch4_Zombie.count; row++) {
            const Zombie z = TIDE_AT(&world, arch4_Zombie, Zombie, row);
            if (z.state != ZombieState_Hunting && z.state != ZombieState_Rising) continue;
            const float d = fabsf(z.at.x - get(0).feet.x) + fabsf(z.at.z - get(0).feet.z);
            if (d < nearest) { nearest = d; target = z; found = true; }
        }
        if (found && t % 20 == 0) {
            const tide_float3 at = {target.at.x - sinf(target.yaw) * -1.5f, 0, target.at.z - cosf(target.yaw) * -1.5f};
            put(0, at, (tide_float3){target.at.x, 1.3f, target.at.z});
        }
        held[0].attack = found && (t % 8) < 4;
        if (held[0].attack) swung++;
        heal();
        tick(1);
    }
    held[0].attack = false;
    CHECK(world.Match.round == 2);
    CHECK(swung > 0);
    CHECK(get(0).kills >= 1);
    CHECK(get(0).points > 500);
    CHECK(least_boards < 6);   // The dead tore at the windows
    printf("round one: %d kills, %d points, boards down to %d\n", get(0).kills, get(0).points, least_boards);

    // ---- The tavern's door ----
    Survivor s = get(0);
    s.points = 20000;
    set(0, s);
    put(0, (tide_float3){10.4f, 0, 1}, (tide_float3){12, 1.2f, 1});
    tick_safe(2);
    CHECK(get(0).aimed == 1000 + 0);     // Target.Gate, gate 0
    press_use(0);
    CHECK((world.Match.gatesOpen & 1) != 0);
    CHECK((world.Match.zonesOpen & (1 << Zone_Tavern)) != 0);
    CHECK(get(0).points == 20000 - 750);

    // ---- A light crossbow off the tavern's wall ----
    put(0, (tide_float3){15, 0, 4.3f}, (tide_float3){15, 1.5f, 5.65f});
    tick_safe(2);
    press_use(0);
    CHECK(has_weapon(get(0), Weapon_LightCrossbow));

    // ---- The Pilgrim's Chest, in the tavern to begin with ----
    put(0, (tide_float3){21.5f, 0, 3.2f}, (tide_float3){21.5f, 0.2f, 4.8f});
    tick_safe(2);
    press_use(0);
    CHECK(world.Match.chest == ChestState_Spinning);
    tick_safe(60 * 5);
    CHECK(world.Match.chest == ChestState_Offering);
    const Weapon prize = world.Match.chestWeapon;
    tick_safe(2);
    press_use(0);
    CHECK(has_weapon(get(0), prize));
    CHECK(world.Match.chest == ChestState_Shut);

    // ---- Through the alley to the keep, and the beacon ----
    put(0, (tide_float3){-15.5f, 0, 4}, (tide_float3){-18, 1.2f, 4});
    tick_safe(2);
    press_use(0);
    CHECK((world.Match.zonesOpen & (1 << Zone_Keep)) != 0);
    tick_safe(30);
    put(0, (tide_float3){-22.3f, 0, 3.6f}, (tide_float3){-23.7f, 1.3f, 3.6f});
    tick_safe(2);
    press_use(0);
    CHECK(world.Match.power);

    // ---- Ironhide Ale, at the tavern's bar ----
    put(0, (tide_float3){21.9f, 0, 3.4f}, (tide_float3){23.3f, 1.2f, 3.4f});
    tick_safe(2);
    press_use(0);
    CHECK(get(0).act == Act_Drink);
    tick_safe(60 * 3);
    CHECK((get(0).brews & (1 << Brew_Ironhide)) != 0);

    // ---- The smithy, and the Runeforge ----
    put(0, (tide_float3){0, 0, -12.6f}, (tide_float3){0, 1.2f, -15});
    tick_safe(2);
    press_use(0);
    CHECK((world.Match.zonesOpen & (1 << Zone_Smithy)) != 0);
    tick_safe(60 * 2);
    s = get(0);
    const Weapon forging = s.weapons.at[s.slot];
    CHECK(forging != Weapon_None);
    put(0, (tide_float3){-2.5f, 0, -19.5f}, (tide_float3){-2.5f, 0.9f, -21});
    tick_safe(2);
    press_use(0);
    CHECK(world.Match.forge == ForgeState_Working);
    tick_safe(60 * 5);
    CHECK(world.Match.forge == ForgeState_Offering);
    press_use(0);
    s = get(0);
    CHECK(s.weapons.at[s.slot] == forging && s.forged.at[s.slot]);

    // ---- Down, and raised ----
    Survivor friend = get(1);
    friend.life = Life_Downed;
    friend.bleed = 30;
    friend.health = 0;
    friend.feet = (tide_float3){-1, 0, 0};
    friend.placed = true;
    set(1, friend);
    put(0, (tide_float3){-1, 0, -1.2f}, (tide_float3){-1, 0.3f, 0});
    tick(2);
    CHECK(get(0).aimed == 3000 + 1);     // Target.Friend, seat 1
    held[0].use = true;
    for (int i = 0; i < 60 * 4 && get(1).life != Life_Standing; i++) {
        Survivor me = get(0);
        me.health = 250;
        set(0, me);
        tick(1);
    }
    held[0].use = false;
    CHECK(get(1).life == Life_Standing);
    CHECK(get(0).revives == 1);

    // ---- A crossbow bolt into one of the dead ----
    s = get(0);
    for (int i = 0; i < 3; i++)
        if (s.weapons.at[i] == Weapon_HandCrossbow) { s.slot = i; s.act = Act_Idle; s.mag.at[i] = 1; }
    set(0, s);
    const int kills_before = get(0).kills;
    const int hits_before = get(0).points;
    bool shot = false;
    for (int t = 0; t < 60 * 60 && !shot; t++) {
        for (uint32_t row = 0; row < world.arch4_Zombie.count && !shot; row++) {
            const Zombie z = TIDE_AT(&world, arch4_Zombie, Zombie, row);
            if (z.state != ZombieState_Hunting) continue;
            put(0, (tide_float3){z.at.x + 5, 0, z.at.z}, (tide_float3){z.at.x, 1.2f, z.at.z});
            tick_safe(1);
            held[0].attack = true;
            tick_safe(1);
            held[0].attack = false;
            tick_safe(20);
            shot = true;
        }
        if (!shot) tick_safe(1);
    }
    CHECK(shot);
    CHECK(get(0).kills > kills_before || get(0).points > hits_before);

    // ---- Up the chapel's street, grave bells off its wall, and one rung ----
    s = get(0);
    s.points = 20000;
    set(0, s);
    put(0, (tide_float3){0, 0, 12.6f}, (tide_float3){0, 1.2f, 14.5f});
    tick_safe(2);
    press_use(0);
    CHECK((world.Match.zonesOpen & (1 << Zone_Chapel)) != 0);
    tick_safe(60 * 2);
    put(0, (tide_float3){3.2f, 0, 20.5f}, (tide_float3){4.65f, 1.5f, 20.5f});
    tick_safe(2);
    const int before_bells = get(0).points;
    press_use(0);
    CHECK(get(0).lures == 2);
    CHECK(get(0).points == before_bells - 1000);
    put(0, (tide_float3){0, 0, 20}, (tide_float3){0, 0.5f, 26});
    held[0].lure = true;
    tick_safe(1);
    held[0].lure = false;
    bool rung = false;
    for (int t = 0; t < 60 * 3 && !rung; t++) {
        tick_safe(1);
        for (uint32_t row = 0; row < world.arch1_Missile.count; row++) {
            const Missile m = TIDE_AT(&world, arch1_Missile, Missile, row);
            if (m.kind == Shot_Bell && m.stuck) rung = true;
        }
    }
    CHECK(rung);
    CHECK(get(0).lures == 1);
    tick_safe(12);     // The way to it is worked out every few ticks
    int nearest_bell = 255;
    for (int c = 0; c < tide_list_read_count(&world.heap, world.Ashwick.lureField); c++) {
        const int there = *(const uint8_t *)tide_list_read(&world.heap, world.Ashwick.lureField, c, 1);
        if (there < nearest_bell) nearest_bell = there;
    }
    CHECK(nearest_bell == 0);

    // ---- A forged Falchion's frost: the first blow slows, the second freezes; a bleeding wound wears one down ----
    s = get(0);
    s.weapons.at[0] = Weapon_Falchion;
    s.forged.at[0] = true;
    s.slot = 0;
    s.act = Act_Idle;
    set(0, s);
    bool slowed = false, froze = false, bled = false;
    for (int t = 0; t < 60 * 90 && !(slowed && froze && bled); t++) {
        int row = -1;
        for (uint32_t r = 0; r < world.arch4_Zombie.count; r++)
            if (TIDE_AT(&world, arch4_Zombie, Zombie, r).state == ZombieState_Hunting) { row = (int)r; break; }
        if (row < 0) { tick_safe(1); continue; }
        const tide_entity e = TIDE_ENTITY_AT(&world, arch4_Zombie, row);
        Zombie z = tide_get_Zombie(&world, e);
        z.health = 100000;            // Struck, never killed
        CHECK(tide_set_Zombie(&world, e, z));
        if (!bled) {
            z.bleed = 3;
            z.bleeder = 0;
            CHECK(tide_set_Zombie(&world, e, z));
            tick_safe(60);
            bled = tide_get_Zombie(&world, e).health < 100000 - 2 * 30;
            continue;
        }
        put(0, (tide_float3){z.at.x + sinf(z.yaw) * 1.4f, 0, z.at.z + cosf(z.yaw) * 1.4f}, (tide_float3){z.at.x, 1.2f, z.at.z});
        held[0].attack = true;
        tick_safe(1);
        held[0].attack = false;
        tick_safe(50);
        z = tide_get_Zombie(&world, e);
        if (z.frozen > 0) froze = true;
        if (z.chill > 0) slowed = true;
    }
    CHECK(bled);
    CHECK(slowed);
    CHECK(froze);

    // ---- A kick: one of the dead goes over backwards, is stamped on as it lies, and gets up again; a heavy blow from
    // a maul takes another off its feet ----
    {
        int row = -1;
        for (int t = 0; t < 60 * 60 && row < 0; t++) {
            for (uint32_t r = 0; r < world.arch4_Zombie.count; r++)
                if (TIDE_AT(&world, arch4_Zombie, Zombie, r).state == ZombieState_Hunting) { row = (int)r; break; }
            if (row < 0) tick_safe(1);
        }
        CHECK(row >= 0);
        const tide_entity e = TIDE_ENTITY_AT(&world, arch4_Zombie, row);
        Zombie z = tide_get_Zombie(&world, e);
        z.health = 100000;
        CHECK(tide_set_Zombie(&world, e, z));
        put(0, (tide_float3){z.at.x + sinf(z.yaw) * 1.2f, 0, z.at.z + cosf(z.yaw) * 1.2f}, (tide_float3){z.at.x, 1.2f, z.at.z});
        s = get(0);
        s.stamina = 1;
        s.act = Act_Idle;
        set(0, s);
        held[0].kick = true;
        tick_safe(1);
        held[0].kick = false;
        tick_safe(20);
        z = tide_get_Zombie(&world, e);
        CHECK(z.state == ZombieState_Fallen);
        // Stamped on as it lies: standing at its hips, its head is under the kick
        tick_safe(40);
        z = tide_get_Zombie(&world, e);
        CHECK(z.state == ZombieState_Fallen);
        const float headX = z.at.x - sinf(z.yaw) * 0.95f, headZ = z.at.z - cosf(z.yaw) * 0.95f;
        put(0, (tide_float3){z.at.x, 0, z.at.z}, (tide_float3){headX, 0.2f, headZ});
        const float before = z.health;
        held[0].kick = true;
        tick_safe(1);
        held[0].kick = false;
        tick_safe(20);
        z = tide_get_Zombie(&world, e);
        CHECK(z.health < before - 300);
        // Up again, a few seconds later
        for (int t = 0; t < 60 * 8 && tide_get_Zombie(&world, e).state == ZombieState_Fallen; t++) tick_safe(1);
        CHECK(tide_get_Zombie(&world, e).state == ZombieState_Hunting);

        // A heavy blow from a maul
        s = get(0);
        s.weapons.at[0] = Weapon_Maul;
        s.forged.at[0] = false;
        s.slot = 0;
        s.act = Act_Idle;
        set(0, s);
        z = tide_get_Zombie(&world, e);
        z.health = 100000;
        CHECK(tide_set_Zombie(&world, e, z));
        put(0, (tide_float3){z.at.x + sinf(z.yaw) * 1.4f, 0, z.at.z + cosf(z.yaw) * 1.4f}, (tide_float3){z.at.x, 1.2f, z.at.z});
        held[0].aim = true;
        held[0].attack = true;
        tick_safe(1);
        held[0].attack = false;
        bool over = false;
        for (int t = 0; t < 150 && !over; t++) {
            tick_safe(1);
            over = tide_get_Zombie(&world, e).state == ZombieState_Fallen;
        }
        held[0].aim = false;
        CHECK(over);
    }

    // ---- Holding the square: with every door open the dead come from every window and grave, and every one of them
    // has to find its way to the survivors, or a round would never end. Both stand by the well and swing at what's near.
    {
        const int first = world.Match.round;
        const int last = first + 3;
        Survivor a = get(0), b = get(1);
        a.feet = (tide_float3){-1.5f, 0, -2};
        b.feet = (tide_float3){1.5f, 0, -2};
        a.placed = b.placed = true;
        // A runeforged greataxe each, so the rounds go quickly
        a.weapons.at[0] = b.weapons.at[0] = Weapon_Greataxe;
        a.forged.at[0] = b.forged.at[0] = true;
        a.slot = b.slot = 0;
        a.act = b.act = Act_Idle;
        set(0, a);
        set(1, b);
        world.Match.gatesOpen = (1 << tide_list_read_count(&world.heap, world.Ashwick.gates)) - 1;
        world.Match.zonesOpen = (1 << 7) - 1;
        int oldest = 0;
        for (int t = 0; t < 60 * 60 * 8 && world.Match.round <= last; t++) {
            // Each faces the nearest of the dead and swings when one is close
            for (int seat = 0; seat < 2; seat++) {
                Survivor s = get(seat);
                float nearest = 1e9f;
                tide_float3 at = {0};
                for (uint32_t row = 0; row < world.arch4_Zombie.count; row++) {
                    const Zombie z = TIDE_AT(&world, arch4_Zombie, Zombie, row);
                    if (z.state != ZombieState_Hunting && z.state != ZombieState_Rising) continue;
                    const float dx = z.at.x - s.feet.x, dz = z.at.z - s.feet.z;
                    const float d = sqrtf(dx * dx + dz * dz);
                    if (d < nearest) { nearest = d; at = z.at; }
                }
                if (nearest < 3.0f) {
                    s.yaw = atan2f(at.x - s.feet.x, at.z - s.feet.z);
                    s.pitch = -0.25f;
                    set(seat, s);
                }
                held[seat].attack = nearest < 2.4f && (t % 10) < 5;
            }
            // How long the oldest of the dead has been about
            for (uint32_t row = 0; row < world.arch4_Zombie.count; row++) {
                const Zombie z = TIDE_AT(&world, arch4_Zombie, Zombie, row);
                if (z.state == ZombieState_Hunting && (int)z.timer > oldest) oldest = (int)z.timer;
            }
            tick_safe(1);
        }
        held[0].attack = held[1].attack = false;
        printf("held the square to round %d; the longest any of the dead hunted: %d s\n", world.Match.round, oldest);
        CHECK(world.Match.round > last);
        CHECK(oldest < 120);
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
