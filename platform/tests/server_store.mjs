// A sharded world's dedicated server whose match uses the store (Storage.Save, await Storage.Load), with
// --shard-journal: the store is the journal folder's own `store`, so the folder is all of the world; what the
// world's start and its ticks saved is there once the server has stopped, and again after a server killed
// outright has gone on from its journal.
import { spawn } from 'node:child_process';
import { createSocket } from 'node:dgram';
import { once } from 'node:events';
import { existsSync, mkdirSync, mkdtempSync, readdirSync, rmSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [serverPath] = process.argv.slice(2);
const folder = mkdtempSync(join(tmpdir(), 'tide-shard-store-'));
const env = { ...process.env, TIDE_HIDDEN: '1' };
const socket = createSocket('udp4');
socket.bind(0, '127.0.0.1');
await once(socket, 'listening');
const port = socket.address().port;
await new Promise(resolve => socket.close(resolve));

let current = null;
let timedOut = null;
function serve(frames) {
  const server = spawn(serverPath, ['--anonymous', '--udp-only', '--port', String(port), '--frames', String(frames),
    '--shards', '2', '--shard-width', '40', '--shard-journal', folder], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], env });
  let output = '';
  server.stderr.on('data', chunk => { output += chunk; });
  const deadline = setTimeout(() => {
    timedOut = new Error(`the server didn't end within a minute: ${output}`);
    server.kill('SIGKILL');
  }, 60000);
  server.once('close', () => clearTimeout(deadline));
  return current = { server, output: () => output, ended: once(server, 'close') };
}

// What the world saved: its start's key, and each mover's, of its id + 1 bytes
function checkStore(when) {
  const kept = join(folder, 'store', 'Tide', 'store');
  const size = name => existsSync(join(kept, name)) ? statSync(join(kept, name)).size : -1;
  if (size('started.bin') !== 4) throw new Error(`${when}: the start's save isn't in ${kept}: ${existsSync(kept) ? readdirSync(kept) : 'no folder'}`);
  for (let id = 0; id < 8; id++) {
    if (size(join('mover', `${id}.bin`)) !== id + 1)
      throw new Error(`${when}: mover ${id}'s save isn't in ${kept}: ${existsSync(join(kept, 'mover')) ? readdirSync(join(kept, 'mover')) : 'no folder'}`);
  }
}

try {
  // A run that stops by itself: everything it saved is in the folder, and the world is sealed there
  const first = serve(300);
  const [code] = await first.ended;
  if (timedOut) throw timedOut;
  if (code !== 0 || !first.output().includes('is sealed in')) throw new Error(`the first run (${code}): ${first.output()}`);
  checkStore('after a run that stopped');
  if (!readdirSync(folder).includes('checkpoint')) throw new Error(`no checkpoint beside the store: ${readdirSync(folder)}`);
  // Anew, killed outright once its ticks have saved, then gone on with: the saves are there, whichever of them
  // the killed server's disk had
  rmSync(folder, { recursive: true, force: true });
  mkdirSync(folder);
  const killed = serve(1000000);
  await new Promise(resolve => setTimeout(resolve, 1500));
  if (!killed.output().includes('sharded world of 2 regions')) throw new Error(`the killed run: ${killed.output()}`);
  killed.server.kill('SIGKILL');
  await killed.ended;
  const again = serve(200);
  const [recovered] = await again.ended;
  if (timedOut) throw timedOut;
  if (recovered !== 0 || !again.output().includes('goes on from its journal')) throw new Error(`the recovery (${recovered}): ${again.output()}`);
  checkStore('after a killed server went on');
  process.stdout.write("a sharded server kept its world's store in its journal folder\n");
} finally {
  current?.server.kill('SIGKILL');
  if (current) await Promise.allSettled([current.ended]);
  rmSync(folder, { recursive: true, force: true });
}
