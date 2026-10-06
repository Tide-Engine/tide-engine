// Shared reporting of the browser and native per-frame netcode samples.
// A frame as a page said it: when, then the numbers in tide_run_netcode's order.
const F = { at: 0, state: 1, server: 2, ping: 3, answer: 4, verified: 5, predicted: 6, heard: 7, delay: 8, early: 9, late: 10,
    again: 11, ran: 12, ahead: 13, resyncs: 14, corrections: 15, resets: 16, update: 17, work: 18 };
const CONNECTED = 2;

const sorted = values => [...values].sort((a, b) => a - b);
const part = (ordered, share) => ordered.length ? ordered[Math.min(ordered.length - 1, Math.floor(ordered.length * share))] : 0;
const spread = values => {
    const ordered = sorted(values);
    const mean = ordered.reduce((sum, v) => sum + v, 0) / (ordered.length || 1);
    return { mean: +mean.toFixed(3), median: +part(ordered, 0.5).toFixed(3), p95: +part(ordered, 0.95).toFixed(3),
        p99: +part(ordered, 0.99).toFixed(3), max: +(ordered[ordered.length - 1] ?? 0).toFixed(3) };
};
const most = values => values.reduce((a, b) => Math.max(a, b), 0);

// What a player's frames say, from when it was in the match.
function summary(frames) {
    const first = frames.findIndex(f => f[F.state] === CONNECTED);
    if (first < 0 || frames.length - first < 3) return null;
    const played = frames.slice(first);
    const end = played[played.length - 1];
    const start = played[0];
    const took = (end[F.at] - start[F.at]) / 1000;
    const between = [], again = [], update = [], work = [], ahead = [];
    const delays = {};
    let delayTime = 0;
    for (let i = 1; i < played.length; i++) {
        const span = played[i][F.at] - played[i - 1][F.at];
        between.push(span);
        again.push(played[i][F.again] - played[i - 1][F.again]);
        update.push(played[i][F.update]);
        work.push(played[i][F.work]);
        ahead.push(played[i][F.predicted] - played[i][F.verified]);
        delays[played[i][F.delay]] = (delays[played[i][F.delay]] || 0) + span;
        delayTime += played[i][F.delay] * span;
    }
    // The delay every five seconds, to see it move
    const timeline = [];
    for (let t = 0, i = 0; i < played.length; i++) {
        if ((played[i][F.at] - start[F.at]) / 1000 >= t) {
            timeline.push(played[i][F.delay]);
            t += 5;
        }
    }
    // Its first second with the world
    const second = played.filter(f => f[F.at] - start[F.at] <= 1000);
    const before = first > 0 ? frames[first - 1] : start;
    const then = second[second.length - 1];
    const leveled = played.find(f => f[F.ahead] > before[F.ahead]);
    let frame = 0;
    for (let i = 0; i < second.length; i++) frame = Math.max(frame, second[i][F.ran] - (i ? second[i - 1][F.ran] : before[F.ran]));
    const joining = {
        behind: start[F.heard] - start[F.verified],
        ran: then[F.ran] - before[F.ran],
        ahead: then[F.ahead] - before[F.ahead],
        again: then[F.again] - before[F.again],
        late: then[F.late] - before[F.late],
        most_in_a_frame: frame,
        longest_update_ms: +most(second.map(f => f[F.update])).toFixed(2),
        longest_frame_ms: +most(second.slice(1).map((f, i) => f[F.at] - second[i][F.at])).toFixed(2),
        first_ahead_ms: leveled ? +(leveled[F.at] - start[F.at]).toFixed(0) : null,
    };
    const span = end[F.at] - start[F.at];
    return {
        seconds: +took.toFixed(1),
        verified_ticks: { first: start[F.verified], last: end[F.verified] },
        ping_ms: spread(played.map(f => f[F.ping])).median,
        delay: { mean: +(delayTime / span).toFixed(2), max: most(played.map(f => f[F.delay])),
            share: Object.fromEntries(Object.entries(delays).map(([d, ms]) => [d, +(ms / span).toFixed(3)])),
            every_5s: timeline },
        ahead_ticks: spread(ahead),
        again_a_frame: spread(again),
        again_a_second: +((end[F.again] - start[F.again]) / took).toFixed(1),
        frame_ms: spread(between),
        frames_over_25ms: between.filter(ms => ms > 25).length,
        frames_over_50ms: between.filter(ms => ms > 50).length,
        update_ms: spread(update),
        work_ms: spread(work),
        corrections_a_second: +((end[F.corrections] - start[F.corrections]) / took).toFixed(1),
        view_resets: end[F.resets] - start[F.resets],
        late_ticks: end[F.late] - start[F.late],
        early_ms: spread(played.map(f => f[F.early])).median,
        resyncs: end[F.resyncs],
        joining,
    };
}

function show(who, s) {
    if (!s) return console.log(`  ${who}: never in the match`);
    const shares = Object.entries(s.delay.share).filter(([, share]) => share >= 0.02).map(([d, share]) => `${d}: ${(share * 100).toFixed(0)}%`).join(', ');
    console.log(`  ${who}: ping ${s.ping_ms} ms; input delay ${s.delay.mean} ticks on the whole, ${s.delay.max} at the most (${shares})`);
    console.log(`    the delay every 5 s: ${s.delay.every_5s.join(' ')}`);
    console.log(`    ${s.ahead_ticks.median} ticks ahead of what's confirmed (median; ${s.ahead_ticks.max} at the most); ran again `
        + `${s.again_a_frame.mean} a frame (p95 ${s.again_a_frame.p95}, most ${s.again_a_frame.max}), ${s.again_a_second} a second`);
    console.log(`    frames ${s.frame_ms.median} ms (p95 ${s.frame_ms.p95}, p99 ${s.frame_ms.p99}, longest ${s.frame_ms.max}; ${s.frames_over_25ms} over 25 ms,`
        + ` ${s.frames_over_50ms} over 50); the session's update ${s.update_ms.median} ms (p95 ${s.update_ms.p95}, p99 ${s.update_ms.p99}, longest`
        + ` ${s.update_ms.max}); a frame's work ${s.work_ms.median} ms (p95 ${s.work_ms.p95})`);
    console.log(`    views corrected ${s.corrections_a_second} times a second, replaced ${s.view_resets} times; ${s.late_ticks} inputs late; sent the world again ${s.resyncs} times`);
    const j = s.joining;
    console.log(`    joining, ${j.behind} ticks behind: in its first second it ran ${j.ran} ticks, ${j.ahead} of them ahead and ${j.again} again;`
        + ` ${j.late} counted late; ${j.most_in_a_frame} in one frame at the most; longest update ${j.longest_update_ms} ms, longest frame`
        + ` ${j.longest_frame_ms} ms; first tick ahead after ${j.first_ahead_ms} ms`);
}


export { F, CONNECTED, summary, show };
