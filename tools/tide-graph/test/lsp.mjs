// A small client of tidels for the tests: Content-Length JSON-RPC over its stdio, as the editors' clients speak it.
import { spawn } from 'node:child_process';

export function startServer(file, root) {
    const child = spawn(file, [], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true, env: { ...process.env, TIDE_HIDDEN: '1' } });
    let buffer = Buffer.alloc(0);
    let next = 1;
    const waiting = new Map();
    const notifications = [];
    const watchers = new Set();
    let stderr = '';
    child.stderr.on('data', d => { stderr = (stderr + d).slice(-8000); });
    child.stdout.on('data', data => {
        buffer = Buffer.concat([buffer, data]);
        for (;;) {
            const end = buffer.indexOf('\r\n\r\n');
            if (end < 0) return;
            const length = Number(/Content-Length: (\d+)/i.exec(buffer.subarray(0, end).toString())[1]);
            if (buffer.length < end + 4 + length) return;
            const message = JSON.parse(buffer.subarray(end + 4, end + 4 + length).toString('utf8'));
            buffer = buffer.subarray(end + 4 + length);
            if (message.id !== undefined && !message.method) {
                const w = waiting.get(message.id);
                waiting.delete(message.id);
                if (message.error) w?.reject(new Error(message.error.message));
                else w?.resolve(message.result);
            } else if (message.id !== undefined && message.method) {
                send({ jsonrpc: '2.0', id: message.id, result: null }); // registrations and the like
            } else {
                notifications.push(message);
                for (const w of watchers) w(message);
            }
        }
    });
    function send(message) {
        const body = Buffer.from(JSON.stringify(message), 'utf8');
        child.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
        child.stdin.write(body);
    }
    const server = {
        child,
        notifications,
        get stderr() { return stderr; },
        request(method, params) {
            const id = next++;
            send({ jsonrpc: '2.0', id, method, params });
            return new Promise((resolve, reject) => waiting.set(id, { resolve, reject }));
        },
        notify(method, params) {
            send({ jsonrpc: '2.0', method, params });
        },
        // The next notification `match` takes, or one already there
        until(match, timeout = 30000) {
            const had = notifications.find(match);
            if (had) return Promise.resolve(had);
            return new Promise((resolve, reject) => {
                const timer = setTimeout(() => { watchers.delete(w); reject(new Error('no notification came: ' + stderr)); }, timeout);
                const w = m => { if (match(m)) { clearTimeout(timer); watchers.delete(w); resolve(m); } };
                watchers.add(w);
            });
        },
        async stop() {
            try {
                await Promise.race([server.request('shutdown', null), new Promise(r => setTimeout(r, 2000))]);
                server.notify('exit', null);
            } catch {}
            await new Promise(resolve => {
                const t = setTimeout(() => { child.kill(); resolve(); }, 3000);
                child.on('close', () => { clearTimeout(t); resolve(); });
            });
        },
    };
    const rootUri = root ? 'file:///' + root.replace(/\\/g, '/').replace(/^\//, '') : null;
    server.ready = server.request('initialize', {
        processId: process.pid, rootUri, capabilities: {},
        workspaceFolders: rootUri ? [{ uri: rootUri, name: 'game' }] : null,
    }).then(result => { server.notify('initialized', {}); return result; });
    return server;
}

export function fileUri(file) {
    const p = file.replace(/\\/g, '/');
    return 'file:///' + (p.startsWith('/') ? p.slice(1) : p).replace(/^([A-Za-z]):/, (_, d) => d.toLowerCase() + '%3A');
}
