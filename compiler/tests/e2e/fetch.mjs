// Runs the native fetch test (fetch_test.c) with a server on this machine for
// it to fetch from: platform/tests/fetch_routes.mjs answers, and the program
// hears the port in TIDE_FETCH_PORT.
//
//   node fetch.mjs <tidec_e2e_fetch, after what runs it on this platform>

import { spawn } from 'node:child_process';
import { createServer } from 'node:http';

import routes from '../../../platform/tests/fetch_routes.mjs';

const command = process.argv.slice(2);
const server = createServer((request, response) => {
    if (!routes(request, response)) {
        response.writeHead(404);
        response.end();
    }
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const child = spawn(command[0], command.slice(1), {
    stdio: 'inherit',
    env: { ...process.env, TIDE_FETCH_PORT: String(server.address().port) },
});
const code = await new Promise(done => {
    child.on('error', error => { console.error(`${command[0]}: ${error.message}`); done(1); });
    child.on('exit', (status, signal) => done(status ?? (signal ? 1 : 0)));
});
server.closeAllConnections();
server.close();
process.exit(code);
