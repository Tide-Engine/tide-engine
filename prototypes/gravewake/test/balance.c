// GRAVEWAKE's balance, by playing it: bots play whole runs through the real
// input (aiming with the look as a player's mouse does, at a human's turn rate
// and with some error; backing off from the dead; going for orbs and chests
// when it's safe; using each survivor's skills as a player would; reviving a
// friend; lighting the night's beacon once its chests are found, or the night
// has gone on a while, or a friend has bled out; keeping by it through the
// vigil and shooting its boss), and each run's numbers are printed: how long
// it lasted, how many nights were kept, kills, downs, when each level came,
// the items taken (at levels and from chests). Not a test: run it from a
// release build.
//   gravewake_balance <roles> [runs] [minutes] [first seed] [quiet|loud] [items] [roam seconds]
// `roles` is one letter a survivor: G Gunner, B Breacher, E Engineer ("G" alone,
// "GB" a pair). Each run's line, then the runs' means.
#include "gravewake_balance.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEATS_MOST 4
#define PI 3.14159265f

static tide_world world;
static Hands held[SEATS_MOST];
static int seats;
static unsigned rng = 1;

static float frand(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)((rng >> 8) & 0xFFFFFF) / (float)0x1000000;
}

static tide_entity survivor_of(int seat) { return world.Party.survivors.at[seat]; }
static Survivor get(int seat) { return tide_get_Survivor(&world, survivor_of(seat)); }

static float wrap(float a)
{
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}

static float flat_distance(tide_float3 a, tide_float3 b) { return hypotf(a.x - b.x, a.z - b.z); }

// What a bot keeps between ticks.
typedef struct Bot {
    Role role;
    tide_entity target;
    float targetFor;          // Seconds on this target
    float aimError;           // Radians off, changing slowly
    float aimErrorPitch;
    bool aimHead;
    float stuck;              // Seconds trying to move and not moving
    tide_float3 lastFeet;
    float pickWait;
    bool blinkTurn;           // Turned away from the dead to blink clear
    bool targetBoss;          // The target's a boss
} Bot;

static Bot bots[SEATS_MOST];

// Items every survivor starts the run with (the harness's last argument: "GD2,PK1"), to weigh what one is worth.
#define ITEMS 24
static int startItems[ITEMS];
static const char *itemCodes[ITEMS] = {"HP", "HT", "KV", "SN", "LR", "FB", "EM", "LS", "SS", "LW", "PK", "GD", "ER", "FR", "SB", "ED", "KE", "ST", "GM", "AP",
                                       "MF", "SW", "HE", "DB"};

static void parse_items(const char *list)
{
    memset(startItems, 0, sizeof startItems);
    for (const char *p = list; *p;) {
        for (int i = 0; i < ITEMS; i++)
            if (p[0] == itemCodes[i][0] && p[1] == itemCodes[i][1]) startItems[i] += p[2] >= '1' && p[2] <= '9' ? p[2] - '0' : 1;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
}

// Run numbers
typedef struct Run {
    float lasted;
    int kills[SEATS_MOST];
    int downs[SEATS_MOST];
    int items[SEATS_MOST];
    int picks[SEATS_MOST];
    float levelAt[64];        // Seconds into the run each level came
    int levels;
    int nights;               // Kept to their dawn
    bool vigil;               // The run ended in a vigil...
    float charge;             // ...this far kept
    float nightAt[16];        // Seconds into the run each dawn broke
    int killsAll;
    int mostAlive;
    int chests;
    int shots;                // The first survivor's
    int reloading;            // Ticks the first survivor spent loading
    char itemNames[192];      // What the first survivor held at the end
    float itemsAt[SEATS_MOST][8]; // Items held at 2, 4, 6, 8, 10, 12, 14, 16 minutes
} Run;

// How long into a night the bots look about before they light its beacon whatever's left (the harness's 7th argument,
// seconds), and how near it they keep while it burns (it's kept from 11 m).
static float roam = 150;
#define VIGIL_KEEP 8.0f

// The input a bot gives for a tick.
static Hands think(int seat, float dt)
{
    Bot *bot = &bots[seat];
    Hands h = {0};
    h.ready = true;
    const Survivor s = get(seat);
    if (s.role == Role_None) {
        // Pressed every other tick: a pick is a key going down
        bot->pickWait = bot->pickWait > 0 ? 0 : 1;
        if (bot->pickWait == 0) return h;
        h.pick1 = bot->role == Role_Gunner;
        h.pick2 = bot->role == Role_Breacher;
        h.pick3 = bot->role == Role_Engineer;
        return h;
    }
    if (s.choosing) {
        bot->pickWait -= dt;
        if (bot->pickWait <= 0) {
            const int k = (int)(frand() * 3);
            h.pick1 = k == 0;
            h.pick2 = k == 1;
            h.pick3 = k == 2;
            bot->pickWait = 0.6f;
        }
        return h;
    }
    if (s.life == Life_Gone) return h;
    const float eyeHigh = s.life == Life_Downed ? 0.55f : 1.62f;
    const tide_float3 eye = {s.feet.x, s.feet.y + eyeHigh, s.feet.z};
    const WeaponInfo info = (WeaponInfo){0};
    (void)info;
    const float range = s.gun == Weapon_Shotgun ? 18.0f : s.gun == Weapon_Smg ? 35.0f : 55.0f;

    // The dead about: the nearest, how many near, which way they press from
    float nearest = 1e9f;
    int within4 = 0, within8 = 0, within12 = 0, within20 = 0;
    float pressX = 0, pressZ = 0;
    bool targetAlive = false;
    tide_entity best = {0};
    float bestScore = 1e9f;
    for (uint32_t r = 0; r < world.arch6_Zombie.count; r++) {
        const Zombie z = TIDE_AT(&world, arch6_Zombie, Zombie, r);
        if (z.state != ZombieState_Hunting && z.state != ZombieState_Fallen) continue;
        const tide_entity e = TIDE_ENTITY_AT(&world, arch6_Zombie, r);
        const float d = sqrtf((z.at.x - s.feet.x) * (z.at.x - s.feet.x) + (z.at.y - s.feet.y) * (z.at.y - s.feet.y) + (z.at.z - s.feet.z) * (z.at.z - s.feet.z));
        if (d < nearest) nearest = d;
        if (d < 4) within4++;
        if (d < 8) {
            within8++;
            pressX += (z.at.x - s.feet.x) / fmaxf(d, 0.1f) * (8 - d);
            pressZ += (z.at.z - s.feet.z) / fmaxf(d, 0.1f) * (8 - d);
        }
        if (d < 12) within12++;
        if (d < 20) within20++;
        if (!bot->targetBoss && e.index == bot->target.index && e.generation == bot->target.generation) targetAlive = d < range * 1.2f;
        // A target: the near and the ones ahead favoured, so it doesn't spin about
        const float yawTo = atan2f(z.at.x - s.feet.x, z.at.z - s.feet.z);
        const float score = d + fabsf(wrap(yawTo - s.yaw)) * 3.0f;
        if (d < range && score < bestScore) {
            bestScore = score;
            best = e;
        }
    }
    // A boss in range, and nothing on top of us (or it's close, or the beacon's full and it's what's keeping the dawn
    // off): it first
    bool bestBoss = false;
    bool full = false;
    for (uint32_t r = 0; r < world.arch4_Beacon.count; r++)
        if (TIDE_AT(&world, arch4_Beacon, Beacon, r).night == world.Match.night && TIDE_AT(&world, arch4_Beacon, Beacon, r).charge >= 1) full = true;
    for (uint32_t r = 0; r < world.arch1_Boss.count; r++) {
        const Boss b = TIDE_AT(&world, arch1_Boss, Boss, r);
        if (b.move == BossMove_Dying || b.move == BossMove_Coming) continue;
        const tide_entity e = TIDE_ENTITY_AT(&world, arch1_Boss, r);
        const float d = flat_distance(b.at, s.feet);
        if (bot->targetBoss && e.index == bot->target.index && e.generation == bot->target.generation) targetAlive = d < 40;
        if (d < 35 && (nearest > 4.5f || d < 12 || full)) {
            best = e;
            bestBoss = true;
            bestScore = -1;
        }
    }
    bot->targetFor += dt;
    if (!targetAlive || bot->targetFor > 1.5f) {
        bot->target = best;
        bot->targetBoss = bestBoss;
        bot->targetFor = 0;
        bot->aimHead = frand() < 0.35f;
    }
    // Aim error drifts: a human's hand
    bot->aimError += (frand() - 0.5f) * 0.02f - bot->aimError * 0.08f;
    bot->aimErrorPitch += (frand() - 0.5f) * 0.015f - bot->aimErrorPitch * 0.08f;

    float wantYaw = s.yaw, wantPitch = 0;
    bool aiming = false;
    if ((bot->target.index != 0 || bot->target.generation != 0) && tide_entity_alive(&world.entities, bot->target)) {
        tide_float3 at;
        if (bot->targetBoss) {
            const Boss b = tide_get_Boss(&world, bot->target);
            at = (tide_float3){b.at.x, b.at.y + 3.0f, b.at.z};
        } else {
            const Zombie z = tide_get_Zombie(&world, bot->target);
            at = bot->aimHead ? z.headAt : z.chestAt;
        }
        const float dx = at.x - eye.x, dy = at.y - eye.y, dz = at.z - eye.z;
        wantYaw = atan2f(dx, dz) + bot->aimError;
        wantPitch = atan2f(dy, sqrtf(dx * dx + dz * dz)) + bot->aimErrorPitch;
        aiming = true;
    }
    // Blinking clear: a tick turned away from the dead first
    if (bot->blinkTurn) {
        wantYaw = atan2f(-pressX, -pressZ);
        wantPitch = 0;
    }
    const float turn = 0.2f;   // Radians a tick at most: about 12 a second
    const float dyaw = wrap(wantYaw - s.yaw);
    h.look.x = fmaxf(-turn, fminf(turn, dyaw));
    h.look.y = fmaxf(-turn, fminf(turn, wantPitch - s.pitch));
    const bool onTarget = aiming && fabsf(dyaw) < 0.08f && !bot->blinkTurn;
    h.attack = onTarget;
    h.reload = s.mag == 0;

    // The night's beacon: lit, kept by; cold, lit once the night's chests are found, or it's gone on a while, or a
    // friend's bled out
    bool lit = false, haveBeacon = false;
    tide_float3 beacon = {0};
    for (uint32_t r = 0; r < world.arch4_Beacon.count; r++) {
        const Beacon b = TIDE_AT(&world, arch4_Beacon, Beacon, r);
        if (b.night != world.Match.night) continue;
        beacon = b.at;
        lit = b.lit;
        haveBeacon = true;
    }
    int chestsLeft = 0;
    for (uint32_t r = 0; r < world.arch7_Chest.count; r++)
        if (TIDE_AT(&world, arch7_Chest, Chest, r).night == world.Match.night) chestsLeft++;
    bool friendGone = false;
    for (int other = 0; other < seats; other++)
        if (other != seat && get(other).life == Life_Gone) friendGone = true;
    const bool fighting = world.Match.phase == Phase_Fighting;
    const bool keeping = haveBeacon && lit && fighting;
    const bool lighting = haveBeacon && !lit && fighting && (chestsLeft == 0 || world.Match.nightClock > roam || friendGone);
    const float toBeacon = haveBeacon ? flat_distance(beacon, s.feet) : 1e9f;
    // A high beacon (the spire's top, the ruin's) is reached up the ramps: the way's next ramp end, or the beacon itself
    tide_float3 goal = beacon;
    if (haveBeacon && s.feet.y < beacon.y - 1.5f) {
        tide_float3 way[4];
        int legs = 0;
        if (world.Match.place == 0) {
            if (s.feet.y < 3) {
                way[legs++] = (tide_float3){22, 0, -15};
                way[legs++] = (tide_float3){31.5f, 4, -9};
            }
            way[legs++] = (tide_float3){36, 4, -11};
            way[legs++] = (tide_float3){43.5f, 9, -4.5f};
        } else if (world.Match.place == 1) {
            const bool west = s.feet.x < 0;
            way[legs++] = west ? (tide_float3){-21, 0, 24} : (tide_float3){20, 0, 25};
            way[legs++] = west ? (tide_float3){-8, 6, 35} : (tide_float3){8, 6, 35};
        }
        if (legs > 0) {
            // The first end not yet reached, from the one nearest on: a bot thrown off the ramp starts again lower
            int at = 0;
            float best = 1e9f;
            for (int k = 0; k < legs; k++) {
                const float d = flat_distance(way[k], s.feet) + fabsf(way[k].y - s.feet.y) * 4;
                if (d < best) {
                    best = d;
                    at = k;
                }
            }
            if (flat_distance(way[at], s.feet) < 2.5f && at + 1 < legs) at++;
            else if (flat_distance(way[at], s.feet) < 2.5f) at = legs;
            if (at < legs) goal = way[at];
        }
    }
    const float toGoal = haveBeacon ? flat_distance(goal, s.feet) : 1e9f;

    // Where to go: to the beacon to light it (through the dead, unless they're right on us), or back to it if it's lit
    // and they're far from it; else away from the dead pressing close; else for an orb or a chest when it's safe; else
    // round the clearing's middle (or the beacon), side to side
    float goX = 0, goZ = 0;
    const float press = sqrtf(pressX * pressX + pressZ * pressZ);
    // At a chest: stay and open it unless one of the dead is right on us
    bool atChest = false;
    for (uint32_t r = 0; r < world.arch7_Chest.count; r++)
        if (flat_distance(TIDE_AT(&world, arch7_Chest, Chest, r).at, s.feet) < 2.2f) atChest = true;
    if (atChest && nearest > 2.5f) {
        h.use = true;
    } else if (lighting && toBeacon < 2.4f) {
        // Held through the hits: lighting it is worth a few
        h.use = true;
    } else if (lighting) {
        goX = (goal.x - s.feet.x) / fmaxf(toGoal, 0.1f);
        goZ = (goal.z - s.feet.z) / fmaxf(toGoal, 0.1f);
    } else if (nearest < 4.5f && press > 0.01f && !(keeping && toBeacon > VIGIL_KEEP)) {
        goX = -pressX / press;
        goZ = -pressZ / press;
    } else if (keeping && toBeacon > VIGIL_KEEP) {
        goX = (goal.x - s.feet.x) / fmaxf(toGoal, 0.1f);
        goZ = (goal.z - s.feet.z) / fmaxf(toGoal, 0.1f);
    } else {
        bool going = false;
        if (nearest > 5) {
            float orbNear = 12;
            for (uint32_t r = 0; r < world.arch5_Orb.count; r++) {
                const Orb o = TIDE_AT(&world, arch5_Orb, Orb, r);
                if (o.seat >= 0) continue;
                const float d = flat_distance(o.at, s.feet);
                if (d < orbNear) {
                    orbNear = d;
                    goX = (o.at.x - s.feet.x) / fmaxf(d, 0.1f);
                    goZ = (o.at.z - s.feet.z) / fmaxf(d, 0.1f);
                    going = true;
                }
            }
        }
        if (!going && nearest > 5 && !keeping) {
            float chestNear = 40;
            for (uint32_t r = 0; r < world.arch7_Chest.count; r++) {
                const Chest c = TIDE_AT(&world, arch7_Chest, Chest, r);
                const float d = flat_distance(c.at, s.feet);
                if (d < chestNear) {
                    chestNear = d;
                    goX = (c.at.x - s.feet.x) / fmaxf(d, 0.1f);
                    goZ = (c.at.z - s.feet.z) / fmaxf(d, 0.1f);
                    going = true;
                    if (d < 2.0f) {
                        goX = goZ = 0;
                        h.use = true;
                    }
                }
            }
        }
        // A friend down: to them, and raise them when the dead aren't on top of us
        for (int other = 0; other < seats && !going; other++) {
            if (other == seat) continue;
            const Survivor f = get(other);
            if (f.life != Life_Downed || nearest < 3.5f) continue;
            const float d = flat_distance(f.feet, s.feet);
            if (d < 1.8f) h.use = true;
            else {
                goX = (f.feet.x - s.feet.x) / d;
                goZ = (f.feet.z - s.feet.z) / d;
            }
            going = true;
        }
        if (!going && lighting) {
            goX = (goal.x - s.feet.x) / fmaxf(toGoal, 0.1f);
            goZ = (goal.z - s.feet.z) / fmaxf(toGoal, 0.1f);
            going = true;
        }
        if (!going) {
            const tide_float3 centre = keeping ? beacon : (tide_float3){0};
            const float home = flat_distance(s.feet, centre);
            if (home > (keeping ? 6.0f : 18.0f)) {
                goX = (centre.x - s.feet.x) / home;
                goZ = (centre.z - s.feet.z) / home;
            } else if (press > 0.01f) {
                // Round the dead, not into them
                goX = -pressZ / press;
                goZ = pressX / press;
                if (((int)(world.Match.clock / 4)) % 2) { goX = -goX; goZ = -goZ; }
            }
        }
    }
    // Stuck against something: a jump and a turn of the way
    const float moved = flat_distance(s.feet, bot->lastFeet);
    bot->lastFeet = s.feet;
    if ((goX != 0 || goZ != 0) && moved < 0.02f) bot->stuck += dt;
    else bot->stuck = fmaxf(0, bot->stuck - dt);
    if (bot->stuck > 0.6f) {
        h.jump = true;
        const float t = goX;
        goX = -goZ;
        goZ = t;
        if (bot->stuck > 1.5f) bot->stuck = 0;
    }
    // Into the move, in the survivor's own axes
    const float fx = sinf(s.yaw), fz = cosf(s.yaw);
    const float rx = cosf(s.yaw), rz = -sinf(s.yaw);
    h.move.x = goX * rx + goZ * rz;
    h.move.y = goX * fx + goZ * fz;

    // The skills, as a player would use them
    const bool ready0 = s.cooldowns.at[0] <= 0, ready1 = s.cooldowns.at[1] <= 0, ready2 = s.cooldowns.at[2] <= 0;
    bot->blinkTurn = false;
    switch (s.role) {
    case Role_Gunner:
        h.secondary = ready0 && onTarget && within20 >= 3;                          // Phase Round down a line of them
        h.utility = ready1 && nearest < 2.5f && press > 0.01f;                      // Slide clear (the move's way)
        h.special = ready2 && onTarget && within12 >= 5 && nearest > 4;             // A grenade into the thick
        break;
    case Role_Breacher:
        h.secondary = ready0 && nearest < 2.2f && onTarget;                         // The boot
        // Shoulder through a crowd ahead, or out of one pressing round
        if (ready1 && onTarget && within8 >= 4 && nearest > 1.2f && s.dashes > 0) {
            h.utility = true;
            h.move = (tide_float2){0, 1};
        } else {
            h.utility = ready1 && nearest < 2.5f && press > 0.01f; (void)within4;
        }
        h.special = ready2 && onTarget && within12 >= 5 && nearest > 3.5f;          // Firebomb
        break;
    case Role_Engineer:
        h.secondary = ready0 && within8 >= 3;                                       // A mine at our feet as they come
        h.utility = ready1 && nearest < 2.5f && press > 0.01f;                      // Dash clear (the move's way)
        h.special = ready2 && within20 >= 2;                                        // A turret whenever there's work
        break;
    default:
        break;
    }
    if (s.life == Life_Downed) h.secondary = h.utility = h.special = false;
    return h;
}

static void tick(float dt)
{
    for (int seat = 0; seat < seats; seat++) {
        held[seat] = think(seat, dt);
        tide_world_set_input(&world, tide_player_from_index(seat), held[seat]);
        if (seat == 0) tide_world_set_server_input(&world, held[seat]);
    }
    tide_world_tick(&world);
}

static Run play(const char *roles, unsigned seed, float minutes)
{
    Run run;
    memset(&run, 0, sizeof run);
    seats = (int)strlen(roles);
    rng = seed * 2654435761u + 7;
    memset(bots, 0, sizeof bots);
    memset(held, 0, sizeof held);
    for (int seat = 0; seat < seats; seat++)
        bots[seat].role = roles[seat] == 'B' ? Role_Breacher : roles[seat] == 'E' ? Role_Engineer : Role_Gunner;
    const float dt = 1.0f / 60.0f;
    tide_world_start_seeded(&world, dt, NULL, seed);
    for (int seat = 0; seat < seats; seat++) tide_world_player_joined(&world, tide_player_from_index(seat));
    for (int i = 0; i < 60 * 20 && world.Match.phase != Phase_Fighting; i++) tick(dt);
    for (int seat = 0; seat < seats; seat++) {
        Survivor s = get(seat);
        for (int i = 0; i < ITEMS; i++) s.items.at[i] += startItems[i];
        s.selfRevives += startItems[Item_SecondWind];
        s.dashes += startItems[Item_EnergyDrink];
        s.health += 45.0f * (float)startItems[Item_Kevlar];
        tide_set_Survivor(&world, survivor_of(seat), s);
    }
    int lastLevel = world.Match.level;
    int lastKept = 0;
    int mark = 0;
    const int most = (int)(minutes * 60 * 60);
    for (int t = 0; t < most && world.Match.phase != Phase_Over; t++) {
        tick(dt);
        const Survivor first = get(0);
        if (first.steady == 0) run.shots++;        // A shot this tick: its steadiness starts again
        if (first.act == Act_Reload) run.reloading++;
        if ((int)world.arch6_Zombie.count > run.mostAlive) run.mostAlive = (int)world.arch6_Zombie.count;
        while (world.Match.level > lastLevel && run.levels < 64) {
            lastLevel++;
            run.levelAt[run.levels++] = world.Match.clock;
        }
        while (world.Match.kept > lastKept && run.nights < 16) {
            lastKept++;
            run.nightAt[run.nights++] = world.Match.clock;
        }
        while (mark < 8 && world.Match.clock >= (mark + 1) * 120.0f) {
            for (int seat = 0; seat < seats; seat++) {
                int n = 0;
                for (int i = 0; i < ITEMS; i++) n += get(seat).items.at[i];
                run.itemsAt[seat][mark] = (float)n;
            }
            mark++;
        }
    }
    run.lasted = world.Match.phase == Phase_Over ? world.Match.lasted : world.Match.clock;
    for (uint32_t r = 0; r < world.arch4_Beacon.count; r++) {
        const Beacon b = TIDE_AT(&world, arch4_Beacon, Beacon, r);
        if (b.night != world.Match.night) continue;
        run.vigil = b.lit;
        run.charge = b.charge;
    }
    {
        const Survivor first = get(0);
        run.itemNames[0] = 0;
        for (int i = 0; i < ITEMS; i++)
            if (first.items.at[i] > 0) {
                char one[16];
                snprintf(one, sizeof one, " %s%d", itemCodes[i], first.items.at[i]);
                strncat(run.itemNames, one, sizeof run.itemNames - strlen(run.itemNames) - 1);
            }
    }
    for (; mark < 8; mark++)
        for (int seat = 0; seat < seats; seat++) run.itemsAt[seat][mark] = -1;
    for (int seat = 0; seat < seats; seat++) {
        const Survivor s = get(seat);
        run.kills[seat] = s.kills;
        run.downs[seat] = s.downs;
        run.picks[seat] = s.picks;
        for (int i = 0; i < ITEMS; i++) run.items[seat] += s.items.at[i];
        run.killsAll += s.kills;
    }
    return run;
}

static int by_value(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

// The quarter, the middle and three quarters of `n` values.
static void quartiles(float *v, int n, float out[3])
{
    qsort(v, (size_t)n, sizeof(float), by_value);
    for (int k = 0; k < 3; k++) {
        const float at = (float)(n - 1) * (float)(k + 1) / 4.0f;
        const int lo = (int)at;
        const int hi = lo + 1 < n ? lo + 1 : lo;
        out[k] = v[lo] + (v[hi] - v[lo]) * (at - (float)lo);
    }
}

#define RUNS_MOST 256

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "gravewake_balance <roles: G B E, one a survivor> [runs] [minutes] [first seed] [quiet|loud] [items] [roam seconds]\n");
        return 2;
    }
    const char *roles = argv[1];
    int runs = argc > 2 ? atoi(argv[2]) : 4;
    if (runs > RUNS_MOST) runs = RUNS_MOST;
    const float minutes = argc > 3 ? (float)atof(argv[3]) : 16;
    const unsigned first = argc > 4 ? (unsigned)atoi(argv[4]) : 1;
    const bool quiet = argc > 5 && strcmp(argv[5], "loud") != 0;
    parse_items(argc > 6 ? argv[6] : "");
    if (argc > 7) roam = (float)atof(argv[7]);
    extern const unsigned char tide_game_pack[];
    extern const size_t tide_game_pack_size;
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    static float lasted[RUNS_MOST], rate[RUNS_MOST], items[RUNS_MOST], perMinute[RUNS_MOST], levels[RUNS_MOST], nights[RUNS_MOST], downs[RUNS_MOST];
    static float levelAt[8][RUNS_MOST];
    int levelRuns[8] = {0};
    for (int k = 0; k < runs; k++) {
        const Run r = play(roles, first + (unsigned)k, minutes);
        tide_world_free(&world);
        int killsAll = 0, downsAll = 0, itemsAll = 0, picksAll = 0;
        for (int seat = 0; seat < seats; seat++) {
            killsAll += r.kills[seat];
            downsAll += r.downs[seat];
            itemsAll += r.items[seat];
            picksAll += r.picks[seat];
        }
        if (!quiet) {
            printf("%s seed %u: lasted %5.0f s, nights %d (", roles, first + (unsigned)k, r.lasted, r.nights);
            for (int n = 0; n < r.nights; n++) printf(n ? " %.0f" : "%.0f", r.nightAt[n]);
            printf(r.vigil ? ", ended in a vigil %.0f%% kept" : ", ended before a vigil", 100.0f * r.charge);
            printf("), kills %4d (%.1f a s), downs %d, levels %d, items %d (%d at levels), %d up at most, shots %.1f/s, loading %.0f%%, items:%s\n", killsAll,
                   killsAll / fmaxf(r.lasted, 1), downsAll, r.levels, itemsAll, picksAll, r.mostAlive, r.shots / fmaxf(r.lasted, 1), 100.0f * r.reloading / fmaxf(r.lasted * 60, 1),
                   r.itemNames);
            fflush(stdout);
        }
        lasted[k] = r.lasted;
        rate[k] = killsAll / fmaxf(r.lasted, 1);
        items[k] = (float)itemsAll / (float)seats;
        perMinute[k] = 60.0f * (float)itemsAll / (float)seats / fmaxf(r.lasted, 1);
        levels[k] = (float)r.levels;
        nights[k] = (float)r.nights;
        downs[k] = (float)downsAll;
        for (int l = 0; l < 8; l++) {
            const int at = (l + 1) * 2 - 1;
            if (at < r.levels) levelAt[l][levelRuns[l]++] = r.levelAt[at];
        }
    }
    float q[3];
    printf("%s%s%s x%d:", roles, argc > 6 ? " +" : "", argc > 6 ? argv[6] : "", runs);
    quartiles(lasted, runs, q);
    printf(" lasted %.0f s [%.0f-%.0f]", q[1], q[0], q[2]);
    quartiles(rate, runs, q);
    printf(", kills %.1f/s [%.1f-%.1f]", q[1], q[0], q[2]);
    quartiles(items, runs, q);
    printf(", items %.1f", q[1]);
    quartiles(perMinute, runs, q);
    printf(" (%.2f/min)", q[1]);
    quartiles(levels, runs, q);
    printf(", levels %.0f", q[1]);
    quartiles(nights, runs, q);
    printf(", nights %.1f [%.0f-%.0f]", q[1], q[0], q[2]);
    quartiles(downs, runs, q);
    printf(", downs %.1f\n", q[1]);
    printf("    levels (middle time, runs that got there):");
    for (int l = 0; l < 8; l++)
        if (levelRuns[l] >= 2) {
            quartiles(levelAt[l], levelRuns[l], q);
            printf(" L%d %.0f s (%d)", (l + 1) * 2 + 1, q[1], levelRuns[l]);
        }
    printf("\n");
    return 0;
}
