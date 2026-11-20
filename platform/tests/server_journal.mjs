// A sharded world's dedicated server with --shard-journal, killed outright
// mid-run, goes on from its journal when it starts again; and a third run from
// the checkpoint that one made. Between, it refuses to start from a journal it
// can't read all of, leaving the folder as it was. Then the world, sealed as
// its server stopped, is refused by the game's next build, verified by it, and
// taken up by it with --shard-upgrade, after which the first build is refused.
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createSocket } from 'node:dgram';
import { once } from 'node:events';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, renameSync, rmdirSync, rmSync,
  statSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [serverPath, nextPath] = process.argv.slice(2);
const folder = mkdtempSync(join(tmpdir(), 'tide-shard-journal-'));
const aside = mkdtempSync(join(tmpdir(), 'tide-shard-journal-aside-'));
const env = { ...process.env, TIDE_HIDDEN: '1' };
const socket = createSocket('udp4');
socket.bind(0, '127.0.0.1');
await once(socket, 'listening');
const port = socket.address().port;
await new Promise(resolve => socket.close(resolve));

// Every file's bytes, to see a refused start left the folder as it was
function folderBytes() {
  return readdirSync(folder).sort().map(name => {
    const path = join(folder, name);
    return statSync(path).isDirectory() ? `${name}/` : `${name} ${createHash('sha256').update(readFileSync(path)).digest('hex')}`;
  }).join('\n');
}

function journalState() {
  try {
    return readdirSync(folder).sort().map(name => `${name} ${statSync(join(folder, name)).size}`).join(', ');
  } catch (error) {
    return String(error);
  }
}

function serve(frames, more = [], path = serverPath) {
  const server = spawn(path, ['--anonymous', '--udp-only', '--port', String(port), '--frames', String(frames),
    '--shards', '2', '--shard-width', '40', '--shard-journal', folder, ...more], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], env });
  let output = '';
  const begun = Date.now();
  // When each thing came, for a run that times out
  let log = '';
  const at = what => { log += `  ${((Date.now() - begun) / 1000).toFixed(2)} s: ${what}\n`; };
  server.stderr.on('data', chunk => { output += chunk; at(String(chunk).trimEnd()); });
  server.on('exit', (code, signal) => at(`exited (${code ?? signal})`));
  // File-size changes distinguish slow durable updates from a server that
  // stopped progressing. Sample at most once a second, only for diagnostics.
  let previous = journalState();
  at(`journal initially: ${previous}`);
  const progress = setInterval(() => {
    const kept = journalState();
    if (kept !== previous) at(`journal: ${kept}`);
    previous = kept;
  }, 1000);
  server.once('close', () => clearInterval(progress));
  // Each recovery does 200 real-time updates, including durable disk flushes.
  // Hosted recovery was still writing at its 25-second cutoff even with this
  // test running alone. Allow one minute, still with a fixed deadline: progress
  // must never extend it, and earlier runs must not spend this one's time.
  clearTimeout(deadline);
  deadline = setTimeout(() => {
    timedOut = new Error(`timed out in ${phase}, ${((Date.now() - started) / 1000).toFixed(1)} s in; ` +
      `the folder has ${journalState()}; since the run began:\n${log}`);
    server.kill('SIGKILL');
  }, 60000);
  return { server, output: () => output, ended: once(server, 'close') };
}

const started = Date.now();
let phase = 'the first run';
let current = null;
let deadline = null;
let timedOut = null;
try {
  // Running, then killed with no warning once it's kept a while of the world
  const first = current = serve(1000000);
  await new Promise(resolve => setTimeout(resolve, 2500));
  if (!first.output().includes('sharded world of 2 regions')) throw new Error(`first run: ${first.output()}`);
  first.server.kill('SIGKILL');
  await first.ended;
  const kept = readdirSync(folder);
  if (!kept.includes('checkpoint') || !kept.some(name => name.startsWith('shard-'))) throw new Error(`kept: ${kept}`);
  const log = kept.find(name => name.startsWith('shard-'));

  // A journal file that's there and can't be read ends the start with the folder as it was: never a new
  // world (an unreadable checkpoint), nor the world short of a shard's ticks (an unreadable log), which is
  // what it did while a file that couldn't be opened was taken as one that isn't there.
  const refused = async (what, named, why = 'cannot read', more = [], path = serverPath) => {
    phase = `the start refused for ${what}`;
    const run = current = serve(200, more, path);
    const [code] = await run.ended;
    if (timedOut) throw timedOut;
    const said = run.output();
    if (code !== 1 || !said.includes(why) || !said.includes(named) || said.includes('goes on from its journal') ||
        said.includes('sharded world of') || said.includes('goes on as this build'))
      throw new Error(`${what}: the server should have refused to start, saying "${why}" and naming ${named} (${code}): ${said}`);
  };
  // A start that must leave every file as it was
  const untouched = async (what, start) => {
    const before = folderBytes();
    await start();
    if (folderBytes() !== before) throw new Error(`${what} changed the folder:\n${before}\nto\n${folderBytes()}`);
  };
  // A run that only looks: no world made, nothing listened on, every file as it was
  const verified = async (what, more, path, says) => {
    phase = `verifying ${what}`;
    await untouched(`verifying ${what}`, async () => {
      const run = current = serve(200, ['--shard-verify', ...more], path);
      const [code] = await run.ended;
      if (timedOut) throw timedOut;
      const said = run.output();
      if (code !== 0 || !said.includes(says) || !/shard 0 at tick \d+, world [0-9a-f]{16}/.test(said) ||
          !said.includes('nothing was written') || said.includes('listening on port'))
        throw new Error(`verifying ${what} (${code}): ${said}`);
    });
  };
  const logs = () => readdirSync(folder).filter(name => name.startsWith('shard-'));
  // A folder in a file's place can't be read as one on any platform
  for (const name of [log, 'checkpoint']) {
    renameSync(join(folder, name), join(aside, name));
    mkdirSync(join(folder, name));
    const before = folderBytes();
    await refused(`an unreadable ${name}`, name);
    if (folderBytes() !== before) throw new Error(`an unreadable ${name} changed the folder:\n${before}\nto\n${folderBytes()}`);
    rmdirSync(join(folder, name));
    renameSync(join(aside, name), join(folder, name));
  }
  // The shards a world has can be any distance apart (ids a merge ended are never given again), so a log
  // is found however far its id is from the last: this one was never looked at when 64 ids had no file.
  mkdirSync(join(folder, 'shard-900.log'));
  await refused('an unreadable log of a far shard', 'shard-900.log');
  rmdirSync(join(folder, 'shard-900.log'));
  if (process.platform === 'win32') {
    // As it was found: another program holds the log open, letting no one read it but letting it be deleted
    const before = folderBytes();
    const holder = spawn('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
      "$f = [IO.File]::Open($env:TIDE_HELD, 'Open', 'Read', 'Delete'); [Console]::Out.WriteLine('held'); " +
      '[Console]::In.ReadLine() | Out-Null; $f.Close()'],
      { windowsHide: true, stdio: ['pipe', 'pipe', 'inherit'], env: { ...env, TIDE_HELD: join(folder, log) } });
    const released = once(holder, 'close');
    try {
      let said = '';
      holder.stdout.on('data', chunk => { said += chunk; });
      while (!said.includes('held')) {
        if (holder.exitCode !== null) throw new Error(`couldn't hold ${log} open: ${said}`);
        await new Promise(resolve => setTimeout(resolve, 50));
      }
      await refused(`${log} held open by another program`, log);
      if (!readdirSync(folder).includes(log)) throw new Error(`${log} was removed while it couldn't be read`);
    } finally {
      holder.stdin.end();
      await released;
    }
    if (folderBytes() !== before) throw new Error(`a held ${log} changed the folder:\n${before}\nto\n${folderBytes()}`);
  }
  // A world has a checkpoint from its start, so records with none are a folder that lost it: nothing is made
  // over them (a new world would write its first checkpoint where the old one's belongs)
  renameSync(join(folder, 'checkpoint'), join(aside, 'checkpoint'));
  await untouched('records with no checkpoint', () => refused('records with no checkpoint', folder, 'records and no checkpoint'));
  renameSync(join(aside, 'checkpoint'), join(folder, 'checkpoint'));
  // A world goes on only with the build that kept it: the game's next build is refused, with or without
  // --shard-upgrade, as this world's journal has ticks only its own build can run again
  if (nextPath) {
    await untouched('another build', () => refused('another build', folder, 'kept by another build', [], nextPath));
    await untouched("another build's upgrade of a world that isn't sealed",
      () => refused("an upgrade of a world that isn't sealed", folder, "isn't sealed", ['--shard-upgrade'], nextPath));
  }
  // Whether it goes on, and from where, without touching it
  await verified('the killed world', [], serverPath, 'goes on with this build');
  // What a crash cut short at a log's end is passed over and said, and a log no shard of the checkpoint
  // has (a shard started since) goes with the rest at the checkpoint the start makes
  const records = readFileSync(join(folder, log));
  let whole = 32; // Past the log's head
  while (records.length - whole >= 4 && records.readUInt32LE(whole) <= records.length - whole - 4)
    whole += 4 + records.readUInt32LE(whole);
  writeFileSync(join(folder, log), Buffer.concat([records.subarray(0, whole), Buffer.from([0xff, 0xff, 0xff, 0x7f, 1])]));
  writeFileSync(join(folder, 'shard-900.log'), Buffer.from([1, 2, 3]));
  // Again: from the checkpoint and what it kept since
  for (const run of [1, 2]) {
    phase = `run ${run + 1} (begun ${((Date.now() - started) / 1000).toFixed(1)} s in)`;
    const again = current = serve(200);
    const [code] = await again.ended;
    if (timedOut) throw timedOut;
    if (code !== 0 || !again.output().includes('goes on from its journal'))
      throw new Error(`run ${run + 1} (${code}): ${again.output()}`);
    if (run === 1) {
      for (const name of [log, 'shard-900.log']) {
        if (!new RegExp(`${name.replace('.', '\\.')} ends in \\d+ bytes of a record cut short`).test(again.output()))
          throw new Error(`run 2 didn't say it passed over the end of ${name}: ${again.output()}`);
      }
      if (existsSync(join(folder, 'shard-900.log'))) throw new Error('shard-900.log outlived the checkpoint the start made');
    }
    // A server that stops seals its world: a checkpoint with nothing owed, and no log after it
    if (!again.output().includes('is sealed in') || logs().length)
      throw new Error(`run ${run + 1} didn't seal the world as it stopped (${logs()}): ${again.output()}`);
  }
  if (nextPath) {
    // The game's next build: refused as it is, verified with --shard-upgrade, then the world is its own
    const sealed = readFileSync(join(folder, 'checkpoint'));
    await untouched('another build, of a sealed world', () => refused('another build, of a sealed world', folder,
      'kept by another build', [], nextPath));
    await verified("the next build's upgrade", ['--shard-upgrade'], nextPath, 'takes up the sealed world');
    phase = "the next build's upgrade";
    const upgrade = current = serve(200, ['--shard-upgrade'], nextPath);
    const [code] = await upgrade.ended;
    if (timedOut) throw timedOut;
    if (code !== 0 || !upgrade.output().includes('goes on as this build') || !upgrade.output().includes('is sealed in'))
      throw new Error(`the upgrade (${code}): ${upgrade.output()}`);
    const left = readdirSync(folder).filter(name => /^checkpoint\.[0-9a-f]{16}$/.test(name));
    if (left.length !== 1 || !readFileSync(join(folder, left[0])).equals(sealed))
      throw new Error(`the upgrade didn't keep the sealed world beside its own: ${readdirSync(folder)}`);
    // The world is the next build's now: the build before it is refused, and the next goes on
    await untouched('the build before, after the upgrade', () => refused('the build before, after the upgrade', folder,
      'kept by another build'));
    phase = "the next build's run after its upgrade";
    const on = current = serve(1000000, [], nextPath);
    await new Promise(resolve => setTimeout(resolve, 1500));
    if (!on.output().includes('goes on from its journal')) throw new Error(`the next build's second run: ${on.output()}`);
    on.server.kill('SIGKILL');
    await on.ended;
    if (!logs().length) throw new Error(`the next build's killed run kept no records: ${readdirSync(folder)}`);
    // --shard-seal: a world left by a crash, gone on with and sealed, with no run between
    phase = '--shard-seal';
    const seal = current = serve(200, ['--shard-seal'], nextPath);
    const [sealCode] = await seal.ended;
    if (timedOut) throw timedOut;
    if (sealCode !== 0 || !seal.output().includes('is sealed in') || logs().length)
      throw new Error(`--shard-seal (${sealCode}, ${logs()}): ${seal.output()}`);
  }
  process.stdout.write('a killed sharded server went on from its journal\n');
} finally {
  clearTimeout(deadline);
  current?.server.kill('SIGKILL');
  if (current) await Promise.allSettled([current.ended]);
  rmSync(folder, { recursive: true, force: true });
  rmSync(aside, { recursive: true, force: true });
}
