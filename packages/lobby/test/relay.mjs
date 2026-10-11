// Runs the lobby package's native test (relay_test.c) with a relay on this
// machine for it to ask: the relay's own (relay/relay.mjs), with its lobbies
// on the machine's clock, and the program hears the port in TIDE_LOBBY_PORT.
//
//   node relay.mjs <tide_lobby_relay, after what runs it on this platform>

import { spawn } from 'node:child_process';

import { createRelay } from '../../../relay/relay.mjs';

const command = process.argv.slice(2);
// The test's four players ask from one address, more at once than a player would
const relay = createRelay({ lobbyLimits: { requestsPerSecond: 1000, requestBurst: 10000 } });
await new Promise(done => relay.listen(0, '127.0.0.1', done));
const child = spawn(command[0], command.slice(1), {
    stdio: 'inherit',
    env: { ...process.env, TIDE_LOBBY_PORT: String(relay.address().port) },
});
const code = await new Promise(done => {
    child.on('error', error => { console.error(`${command[0]}: ${error.message}`); done(1); });
    child.on('exit', (status, signal) => done(status ?? (signal ? 1 : 0)));
});
relay.closeAllConnections();
relay.close();
process.exit(code);
