//! Matching CPU workloads for bench/versus, with normal Bevy change detection.
//! Build with the documented baseline RUSTFLAGS; no fused arithmetic is used.

use bevy_ecs::prelude::*;
use bevy_ecs::schedule::SingleThreadedExecutor;
use bevy_ecs::system::ParallelCommands;
use bevy_tasks::{ComputeTaskPool, TaskPoolBuilder};
use std::{env, process, time::Instant};

const DT: f32 = 1.0 / 60.0;
const BEVY_VERSION: &str = "0.20.0";

#[derive(Clone, Copy)]
enum Workload {
    Particles,
    Wide,
    Churn,
}

impl Workload {
    fn name(self) -> &'static str {
        match self {
            Self::Particles => "particles",
            Self::Wide => "wide",
            Self::Churn => "churn",
        }
    }
}

struct Options {
    workload: Workload,
    count: u32,
    rate: u32,
    tagged: bool,
    frozen: u32,
    threads: usize,
    warmup: u32,
    ticks: u32,
}

fn number(text: &str, flag: &str) -> Result<u32, String> {
    if text.is_empty() || !text.bytes().all(|c| c.is_ascii_digit()) {
        return Err(format!("{flag} requires a nonnegative integer"));
    }
    text.parse::<u32>()
        .ok()
        .filter(|&n| n <= i32::MAX as u32)
        .ok_or_else(|| format!("{flag} must be at most {}", i32::MAX))
}

fn options() -> Result<Options, String> {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.iter().any(|a| a == "--help" || a == "-h") {
        println!(
            "tide-bevy-bench --workload particles|wide|churn [--count N] [--rate N] \
                  [--tagged 0|1] [--frozen N] [--threads N] [--warmup N] [--ticks N]\n\
                  Threads is the upper budget including the caller; parallel queries use N-1 pool workers.\n\
                  The first particles/wide warmup tick only spawns; timing excludes setup and final digest."
        );
        process::exit(0);
    }
    let mut o = Options {
        workload: Workload::Particles,
        count: 1_000_000,
        rate: 2_000,
        tagged: false,
        frozen: 0,
        threads: 1,
        warmup: 200,
        ticks: 400,
    };
    if args.len() % 2 != 0 {
        return Err("every option requires a value; use --help".into());
    }
    for pair in args.chunks_exact(2) {
        let flag = pair[0].as_str();
        let value = pair[1].as_str();
        if flag == "--workload" {
            o.workload = match value {
                "particles" => Workload::Particles,
                "wide" => Workload::Wide,
                "churn" => Workload::Churn,
                _ => return Err("--workload must be particles, wide, or churn".into()),
            };
            continue;
        }
        let n = number(value, flag)?;
        match flag {
            "--count" => o.count = n,
            "--rate" => o.rate = n,
            "--tagged" if n <= 1 => o.tagged = n != 0,
            "--frozen" => o.frozen = n,
            "--threads" => o.threads = n as usize,
            "--warmup" => o.warmup = n,
            "--ticks" => o.ticks = n,
            _ => return Err(format!("unknown or invalid option {flag}; use --help")),
        }
    }
    if o.warmup == 0 || o.ticks == 0 || o.threads == 0 || o.rate == 0 {
        return Err("warmup, ticks, threads, and rate must each be at least 1".into());
    }
    if matches!(o.workload, Workload::Particles) && o.count == 0 {
        return Err("particles count must be at least 1".into());
    }
    Ok(o)
}

// engine/include/tide/math.h: short xxHash32 over two little-endian words,
// masked to a nonnegative Tide int. Arithmetic wraps explicitly.
fn hash_i2(x: i32, y: i32) -> i32 {
    fn word(h: u32, value: i32) -> u32 {
        h.wrapping_add((value as u32).wrapping_mul(0xc2b2ae3d))
            .rotate_left(17)
            .wrapping_mul(0x27d4eb2f)
    }
    let mut h = word(word(0x165667b1u32.wrapping_add(8), x), y);
    h ^= h >> 15;
    h = h.wrapping_mul(0x85ebca77);
    h ^= h >> 13;
    h = h.wrapping_mul(0xc2b2ae3d);
    h ^= h >> 16;
    (h & 0x7fff_ffff) as i32
}

// bench/versus/versus.c: FNV-1a, then the SplitMix64 finish. Serialize each
// logical lane explicitly, so no Rust layout or padding enters the digest.
fn item(words: &[u32]) -> u64 {
    let mut h = 0xcbf29ce484222325u64;
    for word in words {
        for byte in word.to_le_bytes() {
            h = (h ^ u64::from(byte)).wrapping_mul(0x100000001b3);
        }
    }
    h = (h ^ (h >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
    h = (h ^ (h >> 27)).wrapping_mul(0x94d049bb133111eb);
    h ^ (h >> 31)
}

#[derive(Resource)]
struct Setup {
    count: u32,
    tagged: bool,
    frozen: u32,
    ready: bool,
}

#[derive(Component)]
struct Particle {
    x: f32,
    y: f32,
    vx: f32,
    vy: f32,
}

fn setup_particles(mut setup: ResMut<Setup>, mut commands: Commands) {
    if setup.ready {
        return;
    }
    setup.ready = true;
    commands.spawn_batch((0..setup.count).map(|i| {
        let h = hash_i2(i as i32, 0);
        let g = hash_i2(i as i32, 1);
        Particle {
            x: (h % 10000) as f32 * 0.01,
            y: (g % 10000) as f32 * 0.01,
            vx: (h % 201 - 100) as f32 * 0.1,
            vy: (g % 201 - 100) as f32 * 0.1,
        }
    }));
}

#[inline]
fn step_particle(p: &mut Particle) {
    let v = p.vy - 9.81 * DT;
    let h = p.y + v * DT;
    let floor = h < 0.0;
    p.y = if floor { -h } else { h };
    p.vy = if floor { -v * 0.9 } else { v };
    let u = p.vx;
    let w = p.x + u * DT;
    let left = w < 0.0;
    let right = w > 100.0;
    p.x = if left {
        -w
    } else if right {
        200.0 - w
    } else {
        w
    };
    p.vx = if left || right { -u } else { u };
}

fn particles_serial(mut q: Query<&mut Particle>) {
    q.iter_mut().for_each(|mut p| step_particle(&mut p));
}

fn particles_parallel(mut q: Query<&mut Particle>) {
    q.par_iter_mut().for_each(|mut p| step_particle(&mut p));
}

#[derive(Component)]
struct Motion {
    position: [f32; 3],
    velocity: [f32; 3],
    push: [f32; 3],
    drag: f32,
    bounce: f32,
    radius: f32,
}

#[derive(Component)]
// Experimental table storage partitions active/frozen Motion rows.
struct Frozen;

fn setup_wide(mut setup: ResMut<Setup>, mut commands: Commands) {
    if setup.ready {
        return;
    }
    setup.ready = true;
    for i in 0..setup.count {
        let h = hash_i2(i as i32, 0);
        let g = hash_i2(i as i32, 1);
        let f = hash_i2(i as i32, 2);
        let m = Motion {
            position: [
                (h % 10000) as f32 * 0.01,
                (g % 10000) as f32 * 0.01 + 1.0,
                (f % 10000) as f32 * 0.01,
            ],
            velocity: [
                (h % 201 - 100) as f32 * 0.1,
                (g % 201 - 100) as f32 * 0.1,
                (f % 201 - 100) as f32 * 0.1,
            ],
            push: [
                (f % 21 - 10) as f32 * 0.05,
                -9.81,
                (h % 21 - 10) as f32 * 0.05,
            ],
            drag: (g % 50) as f32 * 0.001,
            bounce: 0.5 + (h % 40) as f32 * 0.01,
            radius: 0.25 + (f % 4) as f32 * 0.25,
        };
        let mut entity = commands.spawn(m);
        if setup.tagged && hash_i2(i as i32, 3) % 100 < setup.frozen as i32 {
            entity.insert(Frozen);
        }
    }
}

#[inline]
fn step_motion(m: &mut Motion) {
    let keep = 1.0 - m.drag * DT;
    let u = (m.velocity[0] + m.push[0] * DT) * keep;
    let v = (m.velocity[1] + m.push[1] * DT) * keep;
    let s = (m.velocity[2] + m.push[2] * DT) * keep;
    let y = m.position[1] + v * DT;
    let floor = y < m.radius;
    m.position[0] += u * DT;
    m.position[1] = if floor { m.radius * 2.0 - y } else { y };
    m.position[2] += s * DT;
    m.velocity[0] = u;
    m.velocity[1] = if floor { -v * m.bounce } else { v };
    m.velocity[2] = s;
}

fn wide_serial(mut q: Query<&mut Motion>) {
    q.iter_mut().for_each(|mut m| step_motion(&mut m));
}

fn wide_parallel(mut q: Query<&mut Motion>) {
    q.par_iter_mut().for_each(|mut m| step_motion(&mut m));
}

fn wide_filtered_serial(mut q: Query<&mut Motion, Without<Frozen>>) {
    q.iter_mut().for_each(|mut m| step_motion(&mut m));
}

fn wide_filtered_parallel(mut q: Query<&mut Motion, Without<Frozen>>) {
    q.par_iter_mut().for_each(|mut m| step_motion(&mut m));
}

#[derive(Resource)]
struct Gun {
    tick: i32,
    rate: u32,
}

#[derive(Component)]
struct Bullet {
    x: f32,
    y: f32,
    vx: f32,
    vy: f32,
    life: i32,
}

fn fire(mut gun: ResMut<Gun>, mut commands: Commands) {
    let tick = gun.tick;
    commands.spawn_batch((0..gun.rate).map(move |i| {
        let h = hash_i2(tick, i as i32);
        Bullet {
            x: 0.0,
            y: 0.0,
            vx: (h % 201 - 100) as f32,
            vy: (h / 201 % 201 - 100) as f32,
            life: 20 + h % 160,
        }
    }));
    gun.tick = gun.tick.wrapping_add(1);
}

#[inline]
fn step_bullet(b: &mut Bullet) -> bool {
    b.x += b.vx * DT;
    b.y += b.vy * DT;
    b.life -= 1;
    b.life <= 0
}

fn churn_serial(mut q: Query<(Entity, &mut Bullet)>, mut commands: Commands) {
    q.iter_mut().for_each(|(entity, mut b)| {
        if step_bullet(&mut b) {
            commands.entity(entity).despawn();
        }
    });
}

fn churn_parallel(mut q: Query<(Entity, &mut Bullet)>, commands: ParallelCommands) {
    q.par_iter_mut().for_each(|(entity, mut b)| {
        if step_bullet(&mut b) {
            commands.command_scope(|mut c| {
                c.entity(entity).despawn();
            });
        }
    });
}

fn world_and_schedule(o: &Options) -> (World, Schedule) {
    let mut world = World::new();
    // The scope caller also helps with parallel queries. N-1 pool-owned
    // workers plus that caller bounds execution to --threads N. This is a
    // budget, not a guarantee that all threads participate. Bevy's own
    // par_iter falls back to serial with <=1 owned worker (thus N=2).
    ComputeTaskPool::get_or_init(|| {
        TaskPoolBuilder::new()
            .num_threads(o.threads.saturating_sub(1))
            .thread_name("TideBevyBench".into())
            .build()
    });
    let mut schedule = Schedule::default();
    schedule.set_executor(SingleThreadedExecutor::new());
    world.insert_resource(Setup {
        count: o.count,
        tagged: o.tagged,
        frozen: o.frozen,
        ready: false,
    });
    let parallel = o.threads > 1;
    match o.workload {
        Workload::Particles => {
            if parallel {
                schedule.add_systems((setup_particles, particles_parallel).chain_ignore_deferred());
            } else {
                schedule.add_systems((setup_particles, particles_serial).chain_ignore_deferred());
            }
        }
        Workload::Wide => match (o.tagged, parallel) {
            (false, false) => {
                schedule.add_systems((setup_wide, wide_serial).chain_ignore_deferred());
            }
            (false, true) => {
                schedule.add_systems((setup_wide, wide_parallel).chain_ignore_deferred());
            }
            (true, false) => {
                schedule.add_systems((setup_wide, wide_filtered_serial).chain_ignore_deferred());
            }
            (true, true) => {
                schedule.add_systems((setup_wide, wide_filtered_parallel).chain_ignore_deferred());
            }
        },
        Workload::Churn => {
            world.insert_resource(Gun {
                tick: 0,
                rate: o.rate,
            });
            if parallel {
                schedule.add_systems((fire, churn_parallel).chain_ignore_deferred());
            } else {
                schedule.add_systems((fire, churn_serial).chain_ignore_deferred());
            }
        }
    }
    (world, schedule)
}

fn digest(world: &mut World, workload: Workload) -> (u64, usize, usize) {
    let mut sum = 0u64;
    let mut count = 0;
    let mut frozen = 0;
    match workload {
        Workload::Particles => {
            for p in world.query::<&Particle>().iter(world) {
                sum = sum.wrapping_add(item(&[
                    p.x.to_bits(),
                    p.y.to_bits(),
                    p.vx.to_bits(),
                    p.vy.to_bits(),
                ]));
                count += 1;
            }
        }
        Workload::Wide => {
            for (m, tag) in world.query::<(&Motion, Option<&Frozen>)>().iter(world) {
                let mut words = [0u32; 12];
                for (out, value) in words.iter_mut().zip(
                    m.position
                        .iter()
                        .chain(&m.velocity)
                        .chain(&m.push)
                        .chain([&m.drag, &m.bounce, &m.radius]),
                ) {
                    *out = value.to_bits();
                }
                sum = sum.wrapping_add(item(&words));
                count += 1;
                frozen += usize::from(tag.is_some());
            }
        }
        Workload::Churn => {
            for b in world.query::<&Bullet>().iter(world) {
                sum = sum.wrapping_add(item(&[
                    b.x.to_bits(),
                    b.y.to_bits(),
                    b.vx.to_bits(),
                    b.vy.to_bits(),
                    b.life as u32,
                ]));
                count += 1;
            }
        }
    }
    (sum, count, frozen)
}

fn json_string(text: &str) -> String {
    let mut out = String::from("\"");
    for c in text.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c < ' ' => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

fn main() {
    let o = options().unwrap_or_else(|e| {
        eprintln!("{e}");
        process::exit(2);
    });
    let (mut world, mut schedule) = world_and_schedule(&o);
    for _ in 0..o.warmup {
        schedule.run(&mut world);
    }
    let mut ms = Vec::with_capacity(o.ticks as usize);
    for _ in 0..o.ticks {
        let start = Instant::now();
        schedule.run(&mut world);
        ms.push(start.elapsed().as_secs_f64() * 1000.0);
    }
    let (sum, count, frozen) = digest(&mut world, o.workload);
    let mean = ms.iter().sum::<f64>() / f64::from(o.ticks);
    let mut sorted = ms.clone();
    sorted.sort_unstable_by(f64::total_cmp);
    let mid = sorted.len() / 2;
    let median = if sorted.len() % 2 == 0 {
        (sorted[mid - 1] + sorted[mid]) * 0.5
    } else {
        sorted[mid]
    };
    let samples = ms
        .iter()
        .map(|x| format!("{x:.9}"))
        .collect::<Vec<_>>()
        .join(",");
    let method = if o.threads == 1 {
        "schedule_single_thread_query_for_each"
    } else {
        "schedule_single_thread_query_par_iter"
    };
    let compiler =
        option_env!("TIDE_BENCH_RUSTC_VERSION").unwrap_or("unrecorded; see build metadata");
    println!(
        "{{\"benchmark\":\"versus_{}\",\"workload\":\"{}\",\"count\":{},\"rate\":{},\"tagged\":{},\"frozen\":{},\"threads\":{},\"pool_workers\":{},\"warmup\":{},\"ticks\":{},\"median_ms\":{:.9},\"mean_ms\":{:.9},\"digest\":\"0x{:016x}\",\"entity_count\":{},\"moving_count\":{},\"frozen_count\":{},\"method\":\"{}\",\"bevy_version\":\"{}\",\"compiler\":{},\"samples_ms\":[{}]}}",
        o.workload.name(),
        o.workload.name(),
        o.count,
        o.rate,
        u8::from(o.tagged),
        o.frozen,
        o.threads,
        ComputeTaskPool::get().thread_num(),
        o.warmup,
        o.ticks,
        median,
        mean,
        sum,
        count,
        count - frozen,
        frozen,
        method,
        BEVY_VERSION,
        json_string(compiler),
        samples
    );
}
