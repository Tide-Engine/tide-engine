import { spawn } from 'node:child_process';
import { createSocket } from 'node:dgram';
import { once } from 'node:events';
import { createRelay } from '../../relay/relay.mjs';

const [serverPath, clientPath, mode] = process.argv.slice(2);
const room = mode === 'room';
const relay = room ? createRelay({ iceServers: [] }) : null;
if (relay) await new Promise(resolve => relay.listen(0, '127.0.0.1', resolve));
const env = { ...process.env, TIDE_HIDDEN: '1', ...(relay ? {
  TIDE_RELAY: `ws://127.0.0.1:${relay.address().port}`, TIDE_RTC_LOCAL: '1', TIDE_RTC_DEBUG: '1'
} : {}) };
const socket = createSocket('udp4');
socket.bind(0, '127.0.0.1');
await once(socket, 'listening');
const port = socket.address().port;
await new Promise(resolve => socket.close(resolve));
const server = spawn(serverPath, ['--anonymous', ...(!room ? ['--udp-only'] : []), '--port', String(port), '--frames', '5000'],
  { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], env });
let output = '';
server.stderr.on('data', chunk => { output += chunk; });
const serverEnded = once(server, 'close');
let client = null, clientEnded = null, timedOut = false;
const deadline = setTimeout(() => {
  timedOut = true;
  server.kill();
  client?.kill();
}, 15000);
try {
  await new Promise((resolve, reject) => {
    server.on('error', reject);
    server.on('exit', code => reject(new Error(`server ended before listening (${code}): ${output}`)));
    server.stderr.on('data', () => {
      if (room ? /server room [A-Z2-9]{6}/.test(output) : output.includes('dedicated server listening')) resolve();
    });
  });
  const address = room ? /server room ([A-Z2-9]{6})/.exec(output)[1] : `127.0.0.1:${port}`;
  client = spawn(clientPath, [address], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], env });
  clientEnded = once(client, 'close');
  let clientOutput = '';
  client.stderr.on('data', chunk => { clientOutput += chunk; });
  const [code] = await clientEnded;
  if (timedOut) throw new Error(`server/client timed out: ${clientOutput}\n${output}`);
  if (code !== 0) throw new Error(`client failed (${code}): ${clientOutput}\n${output}`);
  process.stdout.write(clientOutput);
  // The client has verified the transferred pack and twenty ticks. Stop our
  // service now instead of waiting for 5,000 real-time service iterations;
  // their duration depends on the host's timer resolution and scheduling.
  const stopped = server.kill();
  const [serverCode, signal] = await serverEnded;
  if (timedOut) throw new Error(`server/client timed out: ${clientOutput}\n${output}`);
  if (serverCode !== 0 && !(stopped && signal === 'SIGTERM'))
    throw new Error(`server failed (${serverCode}, ${signal}): ${output}`);
} finally {
  clearTimeout(deadline);
  client?.kill();
  server.kill();
  await Promise.allSettled([serverEnded, ...(clientEnded ? [clientEnded] : [])]);
  if (relay) await new Promise(resolve => relay.close(resolve));
}
