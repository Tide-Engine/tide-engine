// tide's anonymous usage data (compiler/cli/telemetry.c), against servers on
// this machine: what an event holds and never holds, the one-time notice,
// every way it's off, the queue's cap and what happens offline.
//
//   node telemetry_test.mjs --tide <tide> --work <folder>
//
// Each case copies tide into an installation of its own, whose telemetry/
// folder is where the settings and the queue go. A development build sends
// only with TIDE_TELEMETRY_TEST beside TIDE_TELEMETRY_URL, which the cases that
// send set; one case checks that without it nothing is sent.

import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import net from "node:net";
import os from "node:os";
import { basename, join, resolve } from "node:path";
import test from "node:test";

const option = (name) => {
    const at = process.argv.indexOf(name);
    if (at < 0 || !process.argv[at + 1]) throw new Error(`usage: node telemetry_test.mjs --tide <tide> --work <folder>`);
    return process.argv[at + 1];
};
const tideBuilt = resolve(option("--tide"));
const work = resolve(option("--work"));
rmSync(work, { recursive: true, force: true });
mkdirSync(work, { recursive: true });

// What an event may hold, and nothing else.
const fields = ["id", "command", "target", "version", "os", "arch", "ci", "ms", "outcome", "error", "files", "packages"];
const commands = new Set(["add", "build", "deploy", "editors", "eject", "fetch", "help", "ide", "login", "logout", "open", "publish",
    "run", "schedule", "shaders", "unity", "update", "upgrade", "version", "other"]);
const errors = new Set(["usage", "setup", "packages", "compile", "c", "link", "network", "game", "other"]);
// Words of this machine and this test that must never be in what's sent.
const secretGame = "QuietHarborSecretGame";
const privateWords = [secretGame, "game.tide", work, work.replaceAll("\\", "/"), os.hostname(), os.userInfo().username]
    .filter((word) => word.length >= 4);

// A server that keeps every request's body.
async function collector(t, status = 204) {
    const bodies = [];
    const server = createServer((req, res) => {
        let body = "";
        req.setEncoding("utf8");
        req.on("data", (chunk) => { body += chunk; });
        req.on("end", () => {
            bodies.push({ method: req.method, url: req.url, type: req.headers["content-type"], body });
            res.writeHead(status).end();
        });
    });
    server.listen(0, "127.0.0.1");
    await new Promise((resolve) => server.once("listening", resolve));
    t.after(() => new Promise((resolve) => server.close(resolve)));
    const events = () => bodies.flatMap(({ body }) => JSON.parse(body).events);
    return { url: `http://127.0.0.1:${server.address().port}/telemetry`, bodies, events };
}

// A server that's there but answers nothing: each connection is cut, as a
// network that's down fails, and counted, so a case knows a sender tried.
async function unreachable(t) {
    const tried = { count: 0 };
    const server = net.createServer((socket) => { tried.count++; socket.destroy(); });
    server.listen(0, "127.0.0.1");
    await new Promise((resolve) => server.once("listening", resolve));
    t.after(() => new Promise((resolve) => server.close(resolve)));
    return { url: `http://127.0.0.1:${server.address().port}/telemetry`, tried };
}

let installs = 0;
// A copy of tide in an installation of its own, with a game beside it.
function install() {
    const root = join(work, `install-${++installs}`);
    mkdirSync(join(root, "bin"), { recursive: true });
    const tide = join(root, "bin", basename(tideBuilt));
    copyFileSync(tideBuilt, tide);
    const game = join(root, secretGame);
    mkdirSync(game);
    writeFileSync(join(game, "game.tide"), "scene Main { }\n");
    const broken = join(root, `${secretGame}Broken`);
    mkdirSync(broken);
    writeFileSync(join(broken, "game.tide"), "scene Main { nonsense here }\n");
    const folder = join(root, "telemetry");
    return { root, tide, game, broken, folder, settings: join(folder, "settings"), queue: join(folder, "queue") };
}

const ciNames = ["CI", "GITHUB_ACTIONS", "GITLAB_CI", "BUILDKITE", "CIRCLECI", "TRAVIS", "JENKINS_URL", "TF_BUILD", "TEAMCITY_VERSION",
    "BITBUCKET_BUILD_NUMBER", "APPVEYOR", "CODEBUILD_BUILD_ID", "DRONE", "BUILD_ID", "CI_NAME"];
// The environment of a run that would send: not hidden, not in CI, nothing
// turned off, sending to `url` with the test's override.
function environment(url, extra = {}) {
    const env = { ...process.env };
    for (const name of [...ciNames, "TIDE_HIDDEN", "DO_NOT_TRACK", "TIDE_TELEMETRY", "TIDE_TELEMETRY_URL", "TIDE_TELEMETRY_TEST"]) {
        for (const key of Object.keys(env)) if (key.toUpperCase() === name) delete env[key];
    }
    env.TIDE_NO_UPDATE_CHECK = "1";
    if (url) {
        env.TIDE_TELEMETRY_URL = url;
        env.TIDE_TELEMETRY_TEST = "1";
    }
    for (const [key, value] of Object.entries(extra)) {
        if (value === undefined) delete env[key];
        else env[key] = value;
    }
    return env;
}

function tide(setup, args, env) {
    const started = Date.now();
    const result = spawnSync(setup.tide, args, { cwd: setup.root, env, encoding: "utf8", timeout: 60_000 });
    if (result.error) throw result.error;
    const text = (said) => said.replaceAll("\r\n", "\n"); // Windows' text mode
    return { code: result.status, stdout: text(result.stdout), stderr: text(result.stderr), ms: Date.now() - started };
}

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(what, check, ms = 20_000) {
    const end = Date.now() + ms;
    for (;;) {
        if (check()) return;
        if (Date.now() > end) throw new Error(`timed out waiting for ${what}`);
        await sleep(50);
    }
}
const queued = (setup) => existsSync(setup.queue) ? readFileSync(setup.queue, "utf8").split("\n").filter(Boolean) : [];
const sending = (setup) => existsSync(setup.folder) ? readdirSync(setup.folder).filter((name) => /^(sending|body)-/.test(name)) : [];
const installId = (setup) => /^id ([0-9a-f]{32})$/m.exec(readFileSync(setup.settings, "utf8"))?.[1];

// The first run that would send shows the notice and sends nothing, so the
// installation's ID and settings exist from then on.
function noticed(setup, env) {
    const first = tide(setup, ["version"], env);
    assert.equal(first.code, 0, first.stderr);
    return first;
}

const version = /^tide (\S+) /.exec(spawnSync(tideBuilt, ["version"], { encoding: "utf8" }).stdout)[1];

// CI builds the tide it tests with the release's version, which isn't a
// development build: there only TIDE_HIDDEN's half applies.
const development = version.endsWith("-dev");

test("a development build sends nothing without the test's override, and TIDE_HIDDEN sends nothing", async (t) => {
    const server = await collector(t);
    const setup = install();
    const silent = [environment(server.url, { TIDE_HIDDEN: "1" })];
    if (development) silent.unshift(environment(null, { TIDE_TELEMETRY_URL: server.url }));
    for (const env of silent) {
        for (const args of [["version"], ["version"], ["schedule", setup.game]]) {
            const run = tide(setup, args, env);
            assert.equal(run.code, 0, run.stderr);
            assert.doesNotMatch(run.stderr, /usage data/);
        }
        assert.equal(existsSync(setup.folder), false, "nothing is written");
    }
    if (development) {
        const status = tide(setup, ["telemetry", "status"], environment(null, { TIDE_TELEMETRY_URL: server.url }));
        assert.match(status.stdout, /^Usage data: off, as this is a development build/);
    }
    const hidden = tide(setup, ["telemetry", "status"], environment(server.url, { TIDE_HIDDEN: "1" }));
    assert.match(hidden.stdout, /^Usage data: off, as TIDE_HIDDEN is set/);
    await sleep(1500);
    assert.equal(server.bodies.length, 0);
});

test("the notice shows once, on one line, and that run sends nothing", async (t) => {
    const server = await collector(t);
    const setup = install();
    const env = environment(server.url);
    const first = noticed(setup, env);
    const lines = first.stderr.split("\n").filter(Boolean);
    assert.equal(lines.length, 1, first.stderr);
    assert.match(lines[0], /^tide: sends anonymous usage data .*https:\/\/tide-engine\.dev\/tools\/telemetry.*`tide telemetry off`$/);
    assert.match(installId(setup) ?? "", /^[0-9a-f]{32}$/);
    assert.deepEqual(queued(setup), []);
    for (let i = 0; i < 2; i++) {
        const next = tide(setup, ["version"], env);
        assert.equal(next.code, 0);
        assert.equal(next.stderr, "", "no second notice");
    }
    await until("both events", () => server.events().length === 2);
    await sleep(500);
    assert.equal(server.events().length, 2, "the notice's run sent nothing");
    // Another installation is another random ID.
    const other = install();
    noticed(other, env);
    assert.notEqual(installId(other), installId(setup));
});

test("an event holds the allowed fields only, and nothing private", async (t) => {
    const server = await collector(t);
    const setup = install();
    const env = environment(server.url);
    noticed(setup, env);
    const runs = [
        { args: ["schedule", setup.game], code: 0 },
        { args: ["schedule", setup.broken], code: 1 },
        { args: ["build", "--web", "--no-such-option", setup.game], code: 2 },
        { args: ["frobnicate", secretGame], code: 2 },
        { args: ["--version"], code: 0 },
        { args: ["help", "run"], code: 0 },
        { args: ["schedule", "--help"], code: 0 },
        { args: ["version"], code: 0, env: { CI: "true" } },
    ];
    for (const [i, { args, code, env: extra }] of runs.entries()) {
        const run = tide(setup, args, extra ? environment(server.url, extra) : env);
        assert.equal(run.code, code, `${args.join(" ")}: ${run.stderr}`);
        // Each sender has sent before the next command, so events arrive in order.
        await until(`the event of ${args.join(" ")}`, () => server.events().length === i + 1);
    }
    await until("every event", () => server.events().length === runs.length);
    for (const request of server.bodies) {
        assert.equal(request.method, "POST");
        assert.equal(request.url, "/telemetry");
        assert.equal(request.type, "application/json");
        assert.ok(request.body.length < 4096);
        for (const word of privateWords) assert.ok(!request.body.includes(word), `the payload names ${word}`);
    }
    const events = server.events();
    const id = installId(setup);
    for (const event of events) {
        for (const key of Object.keys(event)) assert.ok(fields.includes(key), `unexpected field ${key}`);
        assert.equal(event.id, id);
        assert.ok(commands.has(event.command), event.command);
        assert.ok(["native", "web", "android", "none"].includes(event.target));
        assert.equal(event.version, version);
        assert.ok(["windows", "linux", "macos", "other"].includes(event.os));
        assert.ok(["x64", "arm64", "other"].includes(event.arch));
        assert.equal(typeof event.ci, "boolean");
        assert.ok(Number.isInteger(event.ms) && event.ms >= 0 && event.ms < 60_000);
        assert.ok(["ok", "failed"].includes(event.outcome));
        assert.equal(event.outcome === "failed", "error" in event);
        if (event.error) assert.ok(errors.has(event.error), event.error);
    }
    const [schedule, broken, build, other, dashVersion, help, commandHelp, ci] = events;
    assert.deepEqual({ ...schedule, ms: 0 }, { id, command: "schedule", target: "none", version, os: schedule.os, arch: schedule.arch,
        ci: false, ms: 0, outcome: "ok", files: "1", packages: "0" });
    assert.equal(broken.outcome, "failed");
    assert.equal(broken.error, "compile");
    assert.equal(broken.files, "1");
    assert.equal(build.command, "build");
    assert.equal(build.target, "web");
    assert.equal(build.error, "usage");
    assert.equal(build.files, undefined, "no counts for a command that read no game");
    assert.equal(other.command, "other");
    assert.equal(other.error, "usage");
    assert.equal(dashVersion.command, "version");
    assert.equal(help.command, "help");
    assert.equal(commandHelp.command, "help");
    assert.equal(ci.ci, true);
    assert.deepEqual(queued(setup), []);
});

test("TIDE_TELEMETRY=0, DO_NOT_TRACK and `tide telemetry off` send nothing; on sends again", async (t) => {
    const server = await collector(t);
    const setup = install();
    noticed(setup, environment(server.url));
    // DO_NOT_TRACK set to anything but 0 is off, even a value that reads as no.
    for (const extra of [{ TIDE_TELEMETRY: "0" }, { TIDE_TELEMETRY: "off" }, { DO_NOT_TRACK: "1" }, { DO_NOT_TRACK: "true" }, { DO_NOT_TRACK: "no" }]) {
        const run = tide(setup, ["version"], environment(server.url, extra));
        assert.equal(run.code, 0);
        assert.equal(run.stderr, "");
        assert.deepEqual(queued(setup), []);
        const status = tide(setup, ["telemetry", "status"], environment(server.url, extra));
        assert.match(status.stdout, /^Usage data: off, as (TIDE_TELEMETRY is 0|DO_NOT_TRACK is set)/);
    }
    // DO_NOT_TRACK of 0 or nothing turns nothing off. In an installation of its own: the sender these runs start
    // goes on looking at its queue after its event arrives, and would take the one written below before `off`
    const other = install();
    noticed(other, environment(server.url));
    for (const extra of [{ DO_NOT_TRACK: "0" }, { DO_NOT_TRACK: "" }]) {
        const before = server.events().length;
        tide(other, ["version"], environment(server.url, extra));
        await until("an event with DO_NOT_TRACK unset", () => server.events().length === before + 1);
    }
    // Off removes what's waiting, so a queue left by an offline run is never sent.
    writeFileSync(setup.queue, `{"id":"${installId(setup)}","command":"version"}\n`);
    const off = tide(setup, ["telemetry", "off"], environment(server.url));
    assert.equal(off.code, 0);
    assert.equal(off.stdout, "Usage data is off: tide sends nothing.\n");
    assert.equal(existsSync(setup.queue), false);
    const sent = server.events().length;
    for (const args of [["version"], ["schedule", setup.game]]) {
        const run = tide(setup, args, environment(server.url));
        assert.equal(run.stderr, "");
    }
    assert.deepEqual(queued(setup), []);
    const status = tide(setup, ["telemetry", "status"], environment(server.url));
    assert.match(status.stdout, /^Usage data: off, as it was turned off with `tide telemetry off`/);
    assert.match(status.stdout, new RegExp(`Install ID: ${installId(setup)}`));
    for (const word of ["the command", "tide's version", "the OS and CPU", "Never paths", server.url, "tide-engine.dev/tools/telemetry"])
        assert.ok(status.stdout.includes(word), `status says ${word}`);
    await sleep(1500);
    assert.equal(server.events().length, sent);
    const on = tide(setup, ["telemetry", "on"], environment(server.url));
    assert.equal(on.stdout, "Usage data is on; `tide telemetry status` says what is sent.\n");
    tide(setup, ["version"], environment(server.url));
    await until("an event once it's on again", () => server.events().length === sent + 1);
    assert.equal(tide(setup, ["telemetry", "maybe"], environment(server.url)).code, 2);
});

test("offline, events wait in a capped queue without slowing commands, and go once the site answers", async (t) => {
    const down = await unreachable(t);
    const setup = install();
    noticed(setup, environment(down.url));
    const id = installId(setup);
    // 150 events waiting already: the queue keeps the newest 100.
    const seeded = Array.from({ length: 150 }, (_, i) => JSON.stringify({ id, command: "build", target: "native", version, os: "linux",
        arch: "x64", ci: false, ms: 1_000_000 + i, outcome: "ok" }));
    mkdirSync(setup.folder, { recursive: true });
    writeFileSync(setup.queue, `${seeded.join("\n")}\n`);
    const run = tide(setup, ["version"], environment(down.url));
    assert.equal(run.code, 0);
    assert.equal(run.stderr, "");
    assert.ok(run.ms < 5_000, `the command took ${run.ms} ms`);
    await until("a sender to try", () => down.tried.count >= 1);
    await until("the sender to give up", () => sending(setup).length === 0 && queued(setup).length === 100);
    let kept = queued(setup).map((line) => JSON.parse(line));
    assert.equal(kept.length, 100);
    assert.equal(kept.filter((event) => event.command === "version").length, 1, "the run's own event waits too");
    const seededMs = kept.filter((event) => event.command === "build").map((event) => event.ms);
    assert.equal(Math.min(...seededMs), 1_000_000 + 51, "the oldest were dropped");

    // A second offline run: still capped.
    tide(setup, ["version"], environment(down.url));
    await until("the second sender to try", () => down.tried.count >= 2);
    await until("the second sender to give up", () => sending(setup).length === 0 && queued(setup).length === 100);

    // Back online, the next command's sender takes all that waits.
    const server = await collector(t);
    const online = tide(setup, ["version"], environment(server.url));
    assert.equal(online.code, 0);
    await until("the queue to be sent", () => server.events().length === 100);
    await until("the queue to empty", () => sending(setup).length === 0 && queued(setup).length === 0);
    assert.ok(server.bodies.length >= 1);
    kept = server.events();
    assert.equal(kept.filter((event) => event.command === "version").length >= 1, true);

    // A site that refuses a batch (an HTTP error) has it dropped, not retried.
    const refusing = await collector(t, 400);
    tide(setup, ["version"], environment(refusing.url));
    await until("the refused batch", () => refusing.bodies.length === 1);
    await until("the refused batch to be dropped", () => sending(setup).length === 0 && queued(setup).length === 0);
});
