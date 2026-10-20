// GRAVEWAKE's balance, by playing it: bots play whole runs through the real
// input (aiming with the look as a player's mouse does, at a human's turn rate
// and with some error; backing off from the dead; going for orbs and chests
// when it's safe; using each survivor's skills as a player would; reviving a
// friend; stepping out of the fires on the ground, and rolling out of one
// about to burst; going for the night's boss when it rises), and each run's numbers are printed: how long
// it lasted, how many nights were kept, kills, downs, when each level came,
// the items taken (at levels and from chests), how many of a carbine's rounds
// to the body one of the ordinary dead took at 2, 5, 10 and 15 minutes (with
// the first survivor's might), and how long each night's end's fight went,
// from the clock running out to dawn. Not a test: run it from a release build.
//   gravewake_balance <roles> [runs] [minutes] [first seed] [quiet|loud] [items] [still]
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
    float c4Held;             // Seconds its C4 charges have waited
} Bot;

static Bot bots[SEATS_MOST];

// Items every survivor starts the run with (the harness's last argument: "GD2,PK1"), to weigh what one is worth.
#define ITEMS 30
static int startItems[ITEMS];
static const char *itemCodes[ITEMS] = {"HP", "HT", "KV", "SN", "LR", "FB", "EM", "LS", "SS", "RR", "BS", "LW", "PK", "GD", "ER", "FR", "ED", "KE", "ST", "BB",
                                       "AP", "SB", "TC", "MS", "MF", "SW", "HE", "DB", "TD", "RP"};

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
    bool reckoning;           // The run ended with the night's bosses up
    float nightAt[16];        // Seconds into the run each dawn broke
    int killsAll;
    int mostAlive;
    int chests;
    int shots;                // The first survivor's
    int reloading;            // Ticks the first survivor spent loading
    char itemNames[192];      // What the first survivor held at the end
    float itemsAt[SEATS_MOST][8]; // Items held at 2, 4, 6, 8, 10, 12, 14, 16 minutes
    float hitsAt[4];          // A carbine's rounds to the body one of the ordinary dead took at 2, 5, 10, 15 minutes (or -1)
    float fights[16];         // Seconds from each night's clock running out to its dawn
    int fightCount;
} Run;

// How many of a carbine's rounds to the body the toughest of the ordinary dead up would take from the first survivor:
// its health over the carbine's 25 times their might (items.tide's Might, kept in step by hand).
static float carbine_hits(void)
{
    float most = 0;
    for (uint32_t r = 0; r < world.arch5_Zombie.count; r++) {
        const Zombie z = TIDE_AT(&world, arch5_Zombie, Zombie, r);
        if (z.kind == ZombieKind_Walker && z.state == ZombieState_Hunting && z.health > most) most = z.health;
    }
    const Survivor s = get(0);
    const float might = (1 + 0.24f * (float)(s.level - 1)) * (1 + 0.15f * (float)s.items.at[Item_HollowPoints]);
    return most > 0 ? most / (25 * might) : -1;
}


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
            // Any of the three but a skill card (a bot uses its kit's right mouse skill well and the others badly: a
            // blink not at all, a leap into the thick of them)
            int k = (int)(frand() * 3);
            for (int tries = 0; tries < 6 && (int)s.offers.at[k] >= Item_CardPhaseRound; tries++) k = (int)(frand() * 3);
            h.pick1 = k == 0;
            h.pick2 = k == 1;
            h.pick3 = k == 2;
            bot->pickWait = 0.6f;
        }
        return h;
    }
    if (s.life == Life_Gone) return h;
    const tide_float3 gun = {s.feet.x, s.feet.y + 1.36f, s.feet.z};       // Where the shots leave from (hands.tide's GUN_UP)
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
    for (uint32_t r = 0; r < world.arch5_Zombie.count; r++) {
        const Zombie z = TIDE_AT(&world, arch5_Zombie, Zombie, r);
        if (z.state != ZombieState_Hunting && z.state != ZombieState_Fallen) continue;
        const tide_entity e = TIDE_ENTITY_AT(&world, arch5_Zombie, r);
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
    // A boss in range, and nothing on top of us (or it's close, or it's one of the night's own, keeping the dawn off):
    // it first. The nearest of the night's own, to go to
    bool bestBoss = false;
    tide_float3 own = {0};
    float toOwn = 1e9f;
    for (uint32_t r = 0; r < world.arch1_Boss.count; r++) {
        const Boss b = TIDE_AT(&world, arch1_Boss, Boss, r);
        if (b.move == BossMove_Dying || b.move == BossMove_Coming) continue;
        const tide_entity e = TIDE_ENTITY_AT(&world, arch1_Boss, r);
        const float d = flat_distance(b.at, s.feet);
        if (b.own && d < toOwn) {
            toOwn = d;
            own = b.at;
        }
        if (bot->targetBoss && e.index == bot->target.index && e.generation == bot->target.generation) targetAlive = d < 40;
        if (d < 35 && (nearest > 4.5f || d < 12 || b.own)) {
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

    float wantYaw = s.yaw, wantPitch = 0, far = 10;
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
        const float dx = at.x - gun.x, dy = at.y - gun.y, dz = at.z - gun.z;
        wantYaw = atan2f(dx, dz) + bot->aimError;
        wantPitch = atan2f(dy, sqrtf(dx * dx + dz * dz)) + bot->aimErrorPitch;
        far = fmaxf(1.5f, sqrtf(dx * dx + dy * dy + dz * dz));
        aiming = true;
    }
    // Blinking clear: a tick turned away from the dead first
    if (bot->blinkTurn) {
        wantYaw = atan2f(-pressX, -pressZ);
        wantPitch = 0;
    }
    // The cursor where it wants the shots, a little off as a hand is: the survivor turns to it
    const float level = cosf(wantPitch);
    h.aim = (tide_float3){gun.x + sinf(wantYaw) * level * far, gun.y + sinf(wantPitch) * far, gun.z + cosf(wantYaw) * level * far};
    const bool onTarget = aiming && fabsf(wrap(wantYaw - s.yaw)) < 0.3f && !bot->blinkTurn;
    h.attack = onTarget;
    h.reload = s.mag == 0;

    // The fires on the ground: out of any we're in or by (burning, or cracking to burst), and a roll out of one about to
    // burst under us
    float fireX = 0, fireZ = 0;
    bool inFire = false, bursting = false;
    for (uint32_t r = 0; r < world.arch8_Blaze.count; r++) {
        const Blaze b = TIDE_AT(&world, arch8_Blaze, Blaze, r);
        const float d = flat_distance(b.at, s.feet);
        if (d > b.reach + 1.2f) continue;
        inFire = true;
        fireX += (s.feet.x - b.at.x) / fmaxf(d, 0.1f);
        fireZ += (s.feet.z - b.at.z) / fmaxf(d, 0.1f);
        if (!b.burst && b.timer > 1.0f && d < b.reach + 0.4f) bursting = true;
    }
    // The night's boss: to it, when it's up and far off
    const bool hunting = world.Match.phase == Phase_Fighting && world.Match.reckoning && toOwn < 1e8f && toOwn > 22;

    // Where to go: out of a fire; else away from the dead pressing close; else for an orb or a chest when it's safe;
    // else to the night's boss; else round the clearing's middle, side to side
    float goX = 0, goZ = 0;
    const float press = sqrtf(pressX * pressX + pressZ * pressZ);
    // At a chest: stay and open it unless one of the dead is right on us
    bool atChest = false;
    for (uint32_t r = 0; r < world.arch6_Chest.count; r++)
        if (flat_distance(TIDE_AT(&world, arch6_Chest, Chest, r).at, s.feet) < 2.2f) atChest = true;
    if (inFire && (fireX != 0 || fireZ != 0)) {
        const float out = sqrtf(fireX * fireX + fireZ * fireZ);
        goX = fireX / out;
        goZ = fireZ / out;
    } else if (atChest && nearest > 2.5f) {
    } else if (nearest < 4.5f && press > 0.01f) {
        goX = -pressX / press;
        goZ = -pressZ / press;
    } else {
        bool going = false;
        if (nearest > 5) {
            float orbNear = 12;
            for (uint32_t r = 0; r < world.arch4_Orb.count; r++) {
                const Orb o = TIDE_AT(&world, arch4_Orb, Orb, r);
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
        if (!going && nearest > 5) {
            float chestNear = 40;
            for (uint32_t r = 0; r < world.arch6_Chest.count; r++) {
                const Chest c = TIDE_AT(&world, arch6_Chest, Chest, r);
                const float d = flat_distance(c.at, s.feet);
                if (d < chestNear) {
                    chestNear = d;
                    goX = (c.at.x - s.feet.x) / fmaxf(d, 0.1f);
                    goZ = (c.at.z - s.feet.z) / fmaxf(d, 0.1f);
                    going = true;
                    if (d < 2.0f) {
                        goX = goZ = 0;
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
            if (d >= 1.8f) {           // (by them, standing raises them)
                goX = (f.feet.x - s.feet.x) / d;
                goZ = (f.feet.z - s.feet.z) / d;
            }
            going = true;
        }
        if (!going && hunting) {
            goX = (own.x - s.feet.x) / toOwn;
            goZ = (own.z - s.feet.z) / toOwn;
            going = true;
        }
        if (!going) {
            // (round the clearing's middle; at the night's end, the wake-fire's ring's)
            const tide_float3 centre = world.Match.ringReach > 0 ? world.Match.ringAt : (tide_float3){0};
            const float home = flat_distance(s.feet, centre);
            if (home > (world.Match.ringReach > 0 ? 7.0f : 18.0f)) {
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
    // Stuck against something: a turn of the way
    const float moved = flat_distance(s.feet, bot->lastFeet);
    bot->lastFeet = s.feet;
    if ((goX != 0 || goZ != 0) && moved < 0.02f) bot->stuck += dt;
    else bot->stuck = fmaxf(0, bot->stuck - dt);
    if (bot->stuck > 0.6f) {
        const float t = goX;
        goX = -goZ;
        goZ = t;
        if (bot->stuck > 1.5f) bot->stuck = 0;
    }
    // Into the move: across (east) and up (north) the screen
    h.move.x = goX;
    h.move.y = goZ;

    // The roll, out of the dead pressing round, the way it's going; the secondary as a player would use it (the special
    // uses itself)
    bot->blinkTurn = false;
    h.roll = s.life == Life_Standing && s.rolls > 0 && s.roll <= 0 && ((nearest < 2.2f && press > 0.01f) || bursting);
    const bool ready0 = s.cooldowns.at[0] <= 0;
    // (the right mouse's skill: the kit's, or what a skill card made it)
    switch (s.secondary) {
    case Skill_PhaseRound: h.secondary = ready0 && onTarget && within20 >= 3; break;     // Down a line of them
    case Skill_Boot: h.secondary = ready0 && nearest < 2.2f && onTarget; break;          // At the one on us
    case Skill_Mine: {
        // C4: set down as they come, blown once four or more are in its reach (or it's waited 4 s)
        if (s.charges == 0) {
            bot->c4Held = 0;
            h.secondary = ready0 && within8 >= 3;
            break;
        }
        bot->c4Held += dt;
        int caught = 0;
        for (uint32_t m = 0; m < world.arch2_Missile.count; m++) {
            const Missile c4 = TIDE_AT(&world, arch2_Missile, Missile, m);
            if (c4.kind != Shot_Mine || c4.seat != seat) continue;
            for (uint32_t r = 0; r < world.arch5_Zombie.count; r++) {
                const Zombie z = TIDE_AT(&world, arch5_Zombie, Zombie, r);
                if (z.state == ZombieState_Hunting && flat_distance(z.at, c4.at) < c4.splash) caught++;
            }
        }
        h.secondary = ready0 && (caught >= 4 || bot->c4Held > 4.0f);
        break;
    }
    case Skill_Leap: h.secondary = ready0 && onTarget && within8 >= 4; break;            // Into a knot of them
    default: break;                                                                      // (a blink goes the way it aims: not used)
    }
    if (s.life == Life_Downed) h.secondary = false;
    (void)within4;
    (void)within12;
    return h;
}

// The ground never erupting (the harness's 7th argument, "still"): what the eruptions do to a run, by the difference.
static bool still;

static void tick(float dt)
{
    if (still) world.Match.eruptWait = 1e9f;
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
        s.rolls += startItems[Item_EnergyDrink];
        s.health += 45.0f * (float)startItems[Item_Kevlar];
        tide_set_Survivor(&world, survivor_of(seat), s);
    }
    int lastLevel = world.Match.level;
    int lastKept = 0;
    int mark = 0;
    int hitMark = 0;
    float fightFrom = -1;
    static const float hitMinutes[4] = {2, 5, 10, 15};
    for (int k = 0; k < 4; k++) run.hitsAt[k] = -1;
    const int most = (int)(minutes * 60 * 60);
    for (int t = 0; t < most && world.Match.phase != Phase_Over; t++) {
        tick(dt);
        const Survivor first = get(0);
        if (first.steady == 0) run.shots++;        // A shot this tick: its steadiness starts again
        if (first.act == Act_Reload) run.reloading++;
        if ((int)world.arch5_Zombie.count > run.mostAlive) run.mostAlive = (int)world.arch5_Zombie.count;
        while (world.Match.level > lastLevel && run.levels < 64) {
            lastLevel++;
            run.levelAt[run.levels++] = world.Match.clock;
        }
        if (world.Match.reckoning && fightFrom < 0) fightFrom = world.Match.clock;
        while (world.Match.kept > lastKept && run.nights < 16) {
            lastKept++;
            run.nightAt[run.nights++] = world.Match.clock;
            if (fightFrom >= 0 && run.fightCount < 16) run.fights[run.fightCount++] = world.Match.clock - fightFrom;
            fightFrom = -1;
        }
        while (hitMark < 4 && world.Match.clock >= hitMinutes[hitMark] * 60.0f) run.hitsAt[hitMark++] = carbine_hits();
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
    run.reckoning = world.Match.reckoning;
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
        fprintf(stderr, "gravewake_balance <roles: G B E, one a survivor> [runs] [minutes] [first seed] [quiet|loud] [items] [still]\n");
        return 2;
    }
    const char *roles = argv[1];
    int runs = argc > 2 ? atoi(argv[2]) : 4;
    if (runs > RUNS_MOST) runs = RUNS_MOST;
    const float minutes = argc > 3 ? (float)atof(argv[3]) : 16;
    const unsigned first = argc > 4 ? (unsigned)atoi(argv[4]) : 1;
    const bool quiet = argc > 5 && strcmp(argv[5], "loud") != 0;
    parse_items(argc > 6 ? argv[6] : "");
    still = argc > 7 && strcmp(argv[7], "still") == 0;
    extern const unsigned char tide_game_pack[];
    extern const size_t tide_game_pack_size;
    if (!tide_assets_load(tide_game_pack, tide_game_pack_size)) return 1;
    static float lasted[RUNS_MOST], rate[RUNS_MOST], items[RUNS_MOST], perMinute[RUNS_MOST], levels[RUNS_MOST], nights[RUNS_MOST], downs[RUNS_MOST];
    static float levelAt[8][RUNS_MOST];
    int levelRuns[8] = {0};
    static float hitsAt[4][RUNS_MOST];
    int hitRuns[4] = {0};
    static float fights[RUNS_MOST * 16];
    int fightCount = 0;
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
            printf(r.reckoning ? ", ended with the night's boss up" : ", ended before the night's end");
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
        for (int m = 0; m < 4; m++)
            if (r.hitsAt[m] > 0) hitsAt[m][hitRuns[m]++] = r.hitsAt[m];
        for (int f = 0; f < r.fightCount; f++) fights[fightCount++] = r.fights[f];
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
    printf("\n    carbine rounds to the body for one of the dead (middle, runs):");
    static const char *hitNames[4] = {"2 min", "5 min", "10 min", "15 min"};
    for (int m = 0; m < 4; m++)
        if (hitRuns[m] >= 1) {
            quartiles(hitsAt[m], hitRuns[m], q);
            printf(" %s %.1f (%d)", hitNames[m], q[1], hitRuns[m]);
        }
    if (fightCount >= 1) {
        quartiles(fights, fightCount, q);
        printf("\n    night's end fights: %.0f s [%.0f-%.0f] (%d)", q[1], q[0], q[2], fightCount);
    }
    printf("\n");
    return 0;
}
