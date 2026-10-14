const { spawn } = require('node:child_process');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const role = process.argv[2];
// Failure containment belongs to the test runner, not to the adapter's parent
// process. It cannot make an adapter cleanup assertion pass while tests run.
const holder = Number(process.env.TIDE_HOST_HOLDER ?? process.env.TIDE_HOST_RUNNER_PID ?? process.ppid);
const env = { ...process.env, TIDE_HOST_HOLDER: String(holder) };
setInterval(() => {
    try { process.kill(holder, 0); } catch { process.exit(0); }
}, 500);
if (role === 'grandchild') {
    setInterval(() => {}, 1000);
} else if (role === 'child') {
    const child = spawn(process.execPath, [__filename, 'grandchild'], { env, windowsHide: true, stdio: 'ignore' });
    process.send([process.pid, child.pid]);
    setInterval(() => {}, 1000);
} else {
    const child = spawn(process.execPath, [__filename, 'child'], { env, windowsHide: true, stdio: ['ignore', 'ignore', 'ignore', 'ipc'] });
    child.on('message', pids => {
        fs.writeFileSync(path.join(__dirname, `pids-${process.pid}.json`), JSON.stringify([process.pid, ...pids]));
        if (process.argv.includes('--web')) {
            const server = http.createServer((req, res) => {
                if (req.url === '/ready') fs.writeFileSync(path.join(__dirname, `page-${process.pid}`), 'ready');
                res.end('<!doctype html><title>Tide host test</title><h1>Tide host test</h1><script>fetch("/ready")</script>');
            });
            server.listen(0, '127.0.0.1', () => {
                // Deliberately split the URL at its hostname.
                process.stdout.write('tide: the game is at http://127.0.0.1');
                setTimeout(() => process.stdout.write(`:${server.address().port}/\n`), 50);
            });
        }
    });
    process.stdin.resume();
}
