// A sharded world's dedicated server with --shard-journal, killed outright
// mid-run, goes on from its journal when it starts again; and a third run from
// the checkpoint that one made.
import { spawn } from 'node:child_process';
import { createSocket } from 'node:dgram';
import { once } from 'node:events';
import { mkdtempSync, readdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const [serverPath] = process.argv.slice(2);
const folder = mkdtempSync(join(tmpdir(), 'tide-shard-journal-'));
const env = { ...process.env, TIDE_HIDDEN: '1' };
const socket = createSocket('udp4');
socket.bind(0, '127.0.0.1');
await once(socket, 'listening');
const port = socket.address().port;
await new Promise(resolve => socket.close(resolve));

function serve(frames) {
  const server = spawn(serverPath, ['--anonymous', '--udp-only', '--port', String(port), '--frames', String(frames),
    '--shards', '2', '--shard-width', '40', '--shard-journal', folder], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], env });
  let output = '';
  server.stderr.on('data', chunk => { output += chunk; });
  return { server, output: () => output, ended: once(server, 'close') };
}

const deadline = setTimeout(() => { console.error('timed out'); process.exit(1); }, 25000);
try {
  // Running, then killed with no warning once it's kept a while of the world
  const first = serve(1000000);
  await new Promise(resolve => setTimeout(resolve, 2500));
  if (!first.output().includes('sharded world of 2 regions')) throw new Error(`first run: ${first.output()}`);
  first.server.kill('SIGKILL');
  await first.ended;
  const kept = readdirSync(folder);
  if (!kept.includes('checkpoint') || !kept.some(name => name.startsWith('shard-'))) throw new Error(`kept: ${kept}`);
  // Again: from the checkpoint and what it kept since
  for (const run of [1, 2]) {
    const again = serve(400);
    const [code] = await again.ended;
    if (code !== 0 || !again.output().includes('goes on from its journal'))
      throw new Error(`run ${run + 1} (${code}): ${again.output()}`);
  }
  process.stdout.write('a killed sharded server went on from its journal\n');
} finally {
  clearTimeout(deadline);
  rmSync(folder, { recursive: true, force: true });
}
