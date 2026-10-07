// Runs tidels over pipes, as an editor does, a message at a time: what it
// sends waits for nothing but tidels' own answers, so nothing here is timed.
//
// A pipe is where tidels can tell that nothing more has arrived
// (compiler/lsp/main.c): a change is analysed once input goes quiet, and a
// message that arrives in pieces is waited for.
//
//   node pipe_test.mjs <tidels>

import { spawn } from 'node:child_process';

const child = spawn(process.argv[2], [], { stdio: ['pipe', 'pipe', 'inherit'] });

const received = [];
let wanted = null; // { match, resolve }: what the test waits for
let pending = Buffer.alloc(0);

function deliver() {
    if (!wanted) return;
    const at = received.findIndex(wanted.match);
    if (at < 0) return;
    const { resolve } = wanted;
    wanted = null;
    resolve(received.splice(at, 1)[0]);
}

child.stdout.on('data', (chunk) => {
    pending = Buffer.concat([pending, chunk]);
    for (;;) {
        const end = pending.indexOf('\r\n\r\n');
        if (end < 0) break;
        const length = Number(/Content-Length: (\d+)/.exec(pending.subarray(0, end).toString())[1]);
        if (pending.length < end + 4 + length) break;
        received.push(JSON.parse(pending.subarray(end + 4, end + 4 + length).toString()));
        pending = pending.subarray(end + 4 + length);
    }
    deliver();
});

const exited = new Promise((resolve) => child.on('exit', (code) => resolve(code)));
exited.then((code) => {
    if (wanted) {
        console.error(`tidels exited with ${code} before it answered: ${wanted.what}`);
        process.exit(1);
    }
});

// The first message tidels sent, or sends, that `match` takes.
function answer(what, match) {
    return new Promise((resolve) => {
        wanted = { what, match, resolve };
        deliver();
    });
}

function frame(message) {
    const body = Buffer.from(JSON.stringify(message));
    return Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]);
}

function send(message) {
    child.stdin.write(frame(message));
}

const uri = 'file:///pipe.tide';
const diagnostics = (message) => message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri;
const broken = 'component Body { float2 position; }\nscene Main { }\nevent(Spawned) Setup(with Main) { Spawn(Body); }\nsystem Move(mut Body body)\n{\n    body.pos = 1;\n}\n';
const fixed = broken.replace('body.pos', 'body.position.x');

send({ jsonrpc: '2.0', id: 1, method: 'initialize', params: {} });
const started = await answer('initialize', (message) => message.id === 1);
if (!started.result?.capabilities) throw new Error('no capabilities');
send({ jsonrpc: '2.0', method: 'initialized', params: {} });

// A change alone: its diagnostics come once nothing more has arrived
send({ jsonrpc: '2.0', method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: 'tide', version: 1, text: broken } } });
const first = await answer('the opened document\'s diagnostics', diagnostics);
if (!JSON.stringify(first).includes("Body has no field 'pos'")) throw new Error(`unexpected diagnostics: ${JSON.stringify(first)}`);

// A request in two pieces, the second sent once the first is written
const hover = frame({
    jsonrpc: '2.0',
    id: 2,
    method: 'textDocument/hover',
    params: { textDocument: { uri }, position: { line: 5, character: 6 } },
});
const half = hover.length >> 1;
await new Promise((resolve) => child.stdin.write(hover.subarray(0, half), resolve));
await new Promise((resolve) => child.stdin.write(hover.subarray(half), resolve));
const hovered = await answer('the hover', (message) => message.id === 2);
if (!hovered.result) throw new Error(`no hover: ${JSON.stringify(hovered)}`);

// Another change: the diagnostics clear
send({ jsonrpc: '2.0', method: 'textDocument/didChange', params: { textDocument: { uri, version: 2 }, contentChanges: [{ text: fixed }] } });
const second = await answer('the changed document\'s diagnostics', diagnostics);
if (second.params.diagnostics.length !== 0) throw new Error(`unexpected diagnostics: ${JSON.stringify(second)}`);

send({ jsonrpc: '2.0', id: 3, method: 'shutdown' });
await answer('shutdown', (message) => message.id === 3);
send({ jsonrpc: '2.0', method: 'exit' });
const code = await exited;
if (code !== 0) throw new Error(`tidels exited with ${code}`);
console.log('tidels answered over pipes');
