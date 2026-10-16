// GRAVEWAKE played with no window: two survivors hold the first wave round
// the fire with their swords, then take their cards: a weapon, a brew, and
// every kind of keepsake in turn. One goes down and keeps all of it, and the
// other raises them; one bleeds out and comes back at the wave's end with
// everything they had; a crossbow bolt finds one of the dead; a grave bell
// rings; a forged blade's frost slows and freezes, and a wound bleeds; a kick
// and a maul knock the dead over; then both hold the fire for waves, taking a
// card after each. What they press goes through the input, as a player's
// does; where they'd walk, the test puts them.
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

static float max_health(Survivor s) { return (s.brews & (1 << Brew_Ironhide) ? 250.0f : 100.0f) + 40.0f * (float)s.vigor; }

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

// A card taken: 1, 2 or 3, pressed for a tick.
static void pick(int seat, int which)
{
    held[seat].pick1 = which == 1;
    held[seat].pick2 = which == 2;
    held[seat].pick3 = which == 3;
    tick_safe(1);
    held[seat].pick1 = held[seat].pick2 = held[seat].pick3 = false;
    tick_safe(1);
}

// Three cards of the test's choosing, and the first taken.
static Survivor take(int seat, Card card, int item)
{
    Survivor s = get(seat);
    s.cards.at[0] = card;
    s.cardItems.at[0] = item;
    s.cards.at[1] = s.cards.at[2] = Card_Bells;
    s.choosing = true;
    s.act = Act_Idle;
    set(seat, s);
    pick(seat, 1);
    CHECK(!get(seat).choosing);
    return get(seat);
}

static bool has_weapon(Survivor s, Weapon w)
{
    for (int i = 0; i < 3; i++)
        if (s.weapons.at[i] == w) return true;
    return false;
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
    const int cx = (int)floorf((at.x + 42.0f) / 0.5f), cz = (int)floorf((at.z + 38.0f) / 0.5f);
    if (cx < 0 || cz < 0 || cx >= 168 || cz >= 160) return false;
    return *(const uint8_t *)tide_list_read(&world.heap, world.Ashwick.cells, cz * 168 + cx, 1) == 0;
}

// Ends the wave now: the dead all go.
static void end_wave(void)
{
    world.Match.toSpawn = 0;
    world.Match.culling = true;
    for (int i = 0; i < 60 * 3 && world.Match.phase != Phase_Break; i++) tick_safe(1);
    CHECK(world.Match.phase == Phase_Break);
}

// The next wave, once both have taken a card (the first, if any's on offer).
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
    for (uint32_t r = 0; r < world.arch4_Zombie.count; r++)
        if (TIDE_AT(&world, arch4_Zombie, Zombie, r).state == ZombieState_Hunting) return (int)r;
    return -1;
}

int main(void)
{
    // The game's assets: the dead's poses place their heads, which blows and missiles strike
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
    CHECK(get(0).weapons.at[0] == Weapon_HandCrossbow && get(0).weapons.at[1] == Weapon_None);
    CHECK(get(0).points == 0);

    // Where the dead rise is clear of everything solid and on ground they can walk; so is where the survivors start
    const int risers = tide_list_read_count(&world.heap, world.Ashwick.risers);
    CHECK(risers >= 16);
    for (int r = 0; r < risers; r++) {
        const Riser riser = *(const Riser *)tide_list_read(&world.heap, world.Ashwick.risers, r, sizeof(Riser));
        int k = -1;
        if (solid_at(riser.at, 0.3f, &k)) fprintf(stderr, "riser %d at %.2f %.2f, in solid %d\n", r, riser.at.x, riser.at.z, k);
        CHECK(k < 0);
        CHECK(walkable(riser.at));
    }
    for (int seat = 0; seat < 4; seat++) {
        const tide_float3 start = *(const tide_float3 *)tide_list_read(&world.heap, world.Ashwick.starts, seat, sizeof(tide_float3));
        int k = -1;
        CHECK(!solid_at(start, 0.35f, &k));
        CHECK(walkable(start));
    }

    // ---- The first wave: the dead rise out of the graves and the roots ----
    for (int i = 0; i < 60 * 15 && world.Match.phase != Phase_Fighting; i++) tick(1);
    CHECK(world.Match.phase == Phase_Fighting && world.Match.round == 1);
    tick_safe(60 * 6);
    CHECK(world.arch4_Zombie.count > 0);
    // A sword for the first wave (the cards give them in play; the test hands it over)
    Survivor armed = get(0);
    armed.weapons.at[0] = Weapon_RustySword;
    armed.mag.at[0] = armed.ammo.at[0] = 0;
    armed.slot = 0;
    armed.act = Act_Idle;
    set(0, armed);

    // Survivor 0 goes to each of them in turn and swings until the wave is held
    int swung = 0;
    for (int t = 0; t < 60 * 240 && world.Match.phase == Phase_Fighting; t++) {
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
    CHECK(world.Match.phase == Phase_Break && world.Match.round == 1);
    CHECK(swung > 0);
    CHECK(get(0).kills >= 1);
    CHECK(get(0).points > 0);
    printf("first wave: %d kills, a score of %d\n", get(0).kills, get(0).points);

    // ---- The cards: a weapon, a brew, a keepsake; the next wave waits till both have taken one ----
    tick_safe(2);
    for (int seat = 0; seat < 2; seat++) {
        const Survivor s = get(seat);
        CHECK(s.choosing && s.dealt == 1);
        CHECK(s.cards.at[0] == Card_Weapon && s.cardItems.at[0] > 0 && s.cardItems.at[0] < 100);
        CHECK(!has_weapon(s, (Weapon)s.cardItems.at[0]));
        CHECK(s.cards.at[1] == Card_Brew && s.cardItems.at[1] >= 0 && s.cardItems.at[1] < 6);
        CHECK(s.cards.at[2] != Card_Weapon && s.cards.at[2] != Card_Brew);
    }
    const Weapon offered = (Weapon)get(0).cardItems.at[0];
    pick(0, 1);
    CHECK(!get(0).choosing && get(0).weapons.at[0] == offered && get(0).picks == 1);
    tick_safe(60 * 3);
    CHECK(world.Match.phase == Phase_Break && world.Match.timer < 0.05f);     // Still waiting for the other
    const int brew = get(1).cardItems.at[1];
    pick(1, 2);
    CHECK(get(1).act == Act_Drink);
    tick_safe(60 * 3);
    CHECK((get(1).brews & (1 << brew)) != 0);
    for (int i = 0; i < 60 * 4 && world.Match.phase == Phase_Break; i++) tick_safe(1);
    CHECK(world.Match.phase == Phase_Fighting && world.Match.round == 2);
    CHECK(!get(0).choosing && !get(1).choosing);

    // ---- Every keepsake ----
    Survivor s = get(0);
    s.weapons.at[0] = Weapon_LightCrossbow;
    s.forged.at[0] = false;
    s.slot = 0;
    s.ammo.at[0] = 3;
    set(0, s);
    s = take(0, Card_Runeforge, 0);
    CHECK(s.forged.at[0] && s.weapons.at[0] == Weapon_LightCrossbow);
    const int forged_reserve = s.ammo.at[0];
    CHECK(forged_reserve == 80);                       // 50 forged is 80
    s = take(0, Card_Quiver, 0);
    CHECK(s.quiver == 1 && s.ammo.at[0] == 120);       // Half again
    s = take(0, Card_Vigor, 0);
    CHECK(s.vigor == 1 && s.health == 140.0f);
    s = take(0, Card_Whetstone, 0);
    CHECK(s.edge == 1);
    s = take(0, Card_Boots, 0);
    CHECK(s.boots == 1);
    s = take(0, Card_Pyre, 0);
    CHECK(s.pyre == 1 && s.firePots == 4);
    s = take(0, Card_Bells, 0);
    CHECK(s.lures >= 2);
    s = take(0, Card_Brew, Brew_Packmule);
    tick_safe(60 * 3);
    s = take(0, Card_Weapon, Weapon_Halberd);
    CHECK(s.weapons.at[1] == Weapon_Halberd && s.slot == 1 && s.weapons.at[0] == Weapon_LightCrossbow);  // Packmule: a second hand
    s = take(0, Card_Weapon, Weapon_Longsword + 100);
    CHECK(s.weapons.at[1] == Weapon_Longsword && s.forged.at[1]);  // In place of what's in hand, forged

    // ---- Down, and raised: one of the dead puts a sword-carrier down, who fights on with the last stand's crossbow,
    // and keeps every brew ----
    Survivor friend = get(1);
    friend.weapons.at[0] = Weapon_Longsword;
    friend.mag.at[0] = friend.ammo.at[0] = 0;
    friend.slot = 0;
    friend.act = Act_Idle;
    friend.edge = 2;
    set(1, friend);
    const int brews = get(1).brews;
    for (int t = 0; t < 60 * 60 && get(1).life == Life_Standing; t++) {
        const int row = hunting_row();
        if (row >= 0) {
            const Zombie z = TIDE_AT(&world, arch4_Zombie, Zombie, row);
            friend = get(1);
            friend.health = 1;
            friend.feet = (tide_float3){z.at.x + sinf(z.yaw) * 0.8f, 0, z.at.z + cosf(z.yaw) * 0.8f};
            friend.placed = true;
            set(1, friend);
            put(0, (tide_float3){z.at.x + 9, 0, z.at.z}, (tide_float3){z.at.x + 12, 1.6f, z.at.z});
        }
        Survivor me = get(0);
        me.health = max_health(me);
        set(0, me);
        tick(1);
    }
    friend = get(1);
    CHECK(friend.life == Life_Downed);
    CHECK(friend.slot == 2 && friend.weapons.at[2] == Weapon_HandCrossbow && friend.mag.at[2] == 1);
    CHECK(friend.brews == brews && friend.edge == 2);
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
    CHECK(get(1).slot == 0 && get(1).weapons.at[0] == Weapon_Longsword && get(1).weapons.at[2] == Weapon_None);

    // ---- Bled out: back at the fire when the wave is done, with everything they had ----
    friend = get(1);
    friend.life = Life_Downed;
    friend.bleed = 0.05f;
    friend.health = 0;
    set(1, friend);
    tick(10);
    CHECK(get(1).life == Life_Gone);
    end_wave();
    tick_safe(2);
    friend = get(1);
    CHECK(friend.life == Life_Standing && friend.health == max_health(friend));
    CHECK(friend.brews == brews && friend.edge == 2 && friend.weapons.at[0] == Weapon_Longsword);
    CHECK(friend.choosing);
    next_wave();
    tick_safe(60 * 6);

    // ---- A crossbow bolt into one of the dead ----
    s = get(0);
    s.weapons.at[0] = Weapon_HandCrossbow;
    s.forged.at[0] = false;
    s.mag.at[0] = 1;
    s.ammo.at[0] = 40;
    s.slot = 0;
    s.act = Act_Idle;
    set(0, s);
    const int kills_before = get(0).kills;
    const int score_before = get(0).points;
    bool shot = false;
    for (int t = 0; t < 60 * 60 && !shot; t++) {
        const int row = hunting_row();
        if (row < 0) { tick_safe(1); continue; }
        const Zombie z = TIDE_AT(&world, arch4_Zombie, Zombie, row);
        put(0, (tide_float3){z.at.x + 5, 0, z.at.z}, (tide_float3){z.at.x, 1.2f, z.at.z});
        tick_safe(1);
        held[0].attack = true;
        tick_safe(1);
        held[0].attack = false;
        tick_safe(20);
        shot = true;
    }
    CHECK(shot);
    CHECK(get(0).kills > kills_before || get(0).points > score_before);

    // ---- A grave bell rung by the ruin ----
    put(0, (tide_float3){0, 0, 4}, (tide_float3){0, 0.5f, 10});
    s = get(0);
    s.lures = 2;
    s.act = Act_Idle;
    set(0, s);
    held[0].lure = true;
    tick_safe(1);
    held[0].lure = false;
    bool rung = false;
    for (int t = 0; t < 60 * 3 && !rung; t++) {
        tick_safe(1);
        for (uint32_t row = 0; row < world.arch2_Missile.count; row++) {
            const Missile m = TIDE_AT(&world, arch2_Missile, Missile, row);
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
        const int row = hunting_row();
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
            row = hunting_row();
            if (row < 0) tick_safe(1);
        }
        CHECK(row >= 0);
        const tide_entity e = TIDE_ENTITY_AT(&world, arch4_Zombie, row);
        Zombie z = tide_get_Zombie(&world, e);
        z.health = 100000;
        CHECK(tide_set_Zombie(&world, e, z));
        // Its head is where its pose has it: up on its shoulders while it hunts
        tick_safe(1);
        z = tide_get_Zombie(&world, e);
        printf("a hunting head %.2f m up, %.2f m out from its feet\n", z.headAt.y - z.at.y, hypotf(z.headAt.x - z.at.x, z.headAt.z - z.at.z));
        CHECK(z.headAt.y - z.at.y > 1.2f && z.headAt.y - z.at.y < 1.95f);
        CHECK(hypotf(z.headAt.x - z.at.x, z.headAt.z - z.at.z) < 0.6f);
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
        tick_safe(40);
        z = tide_get_Zombie(&world, e);
        CHECK(z.state == ZombieState_Fallen);
        // Down, its head is on the ground; stamped on as it lies, standing at its hips
        printf("a fallen head %.2f m up\n", z.headAt.y - z.at.y);
        CHECK(z.headAt.y - z.at.y < 0.6f);
        put(0, (tide_float3){z.at.x, 0, z.at.z}, (tide_float3){z.headAt.x, z.headAt.y, z.headAt.z});
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

    // ---- Holding the fire: the dead come from every grave and every root, and every one of them has to find its way
    // to the survivors, or a wave would never end. Both stand by the fire and swing at what's near, and take the third
    // card after each wave.
    {
        const int first = world.Match.round;
        const int last = first + 3;
        Survivor a = get(0), b = get(1);
        a.feet = (tide_float3){-1.6f, 0, -3.1f};
        b.feet = (tide_float3){1.6f, 0, -3.1f};
        a.placed = b.placed = true;
        // A runeforged greataxe each, so the waves go quickly
        a.weapons.at[0] = b.weapons.at[0] = Weapon_Greataxe;
        a.weapons.at[1] = b.weapons.at[1] = Weapon_None;
        a.forged.at[0] = b.forged.at[0] = true;
        a.slot = b.slot = 0;
        a.act = b.act = Act_Idle;
        set(0, a);
        set(1, b);
        int oldest = 0, cards = 0;
        for (int t = 0; t < 60 * 60 * 8 && world.Match.round <= last; t++) {
            for (int seat = 0; seat < 2; seat++) {
                Survivor s = get(seat);
                if (s.choosing) {
                    // The third card: never a weapon in place of the greataxe
                    held[seat].pick3 = true;
                    cards++;
                }
                // Each faces the nearest of the dead and swings when one is close
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
            held[0].pick3 = held[1].pick3 = false;
        }
        held[0].attack = held[1].attack = false;
        printf("held the fire to wave %d, %d cards taken; the longest any of the dead hunted: %d s\n", world.Match.round, cards, oldest);
        CHECK(world.Match.round > last);
        CHECK(cards >= 6);
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
