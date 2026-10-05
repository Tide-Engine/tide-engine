// What makes a builder (builder.mjs) for each build of the build service
// (builds.mjs): a machine made for that one build, which is gone once the
// build is. The service only knows three things of them:
//
//   start({build, token, url})  a builder for that build, which asks the
//                               service at `url` what to build: its id
//   alive(id)                   whether it's still at it: a builder that's gone
//                               without an answer has failed
//   stop(id)                    ends it, for a build that took too long or was
//                               revoked
//
// On Fly, a builder is a Machine (flyMachines). On one machine, for tests and
// for trying the service, it's a process (localBuilders).

import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

// Builders as processes on this machine, each running builder.mjs. `env` is
// added to their environment: which tide they run, and that they don't
// isolate it.
export function localBuilders({ env = {}, quiet = true } = {}) {
    const running = new Map();
    let next = 1;
    return {
        async start({ build, token, url }) {
            const id = `local-${next++}`;
            const child = spawn(process.execPath, [fileURLToPath(new URL('./builder.mjs', import.meta.url))], {
                env: { ...process.env, ...env, TIDE_BUILDS_URL: url, TIDE_BUILD: build, TIDE_BUILD_TOKEN: token },
                stdio: quiet ? 'ignore' : 'inherit',
                windowsHide: true,
            });
            running.set(id, child);
            child.on('exit', () => running.delete(id));
            child.on('error', () => running.delete(id));
            return id;
        },
        async alive(id) {
            return running.has(id);
        },
        async stop(id) {
            const child = running.get(id);
            if (child) child.kill('SIGKILL');
        },
        // For tests: once every builder is gone
        async idle() {
            await Promise.all([...running.values()].map(child => new Promise(done => child.once('exit', done))));
        },
    };
}

// Builders as Fly Machines, through the Machines API: each made from `image`
// (the builder's: Dockerfile.builder) in `app`, with no service on it, so it
// has no address anyone reaches, and destroyed by Fly when its program ends.
// `token` is a token for that app alone. The builders' app holds no secret:
// Fly gives an app's secrets to every machine in it, so the service's own (the
// master key the games' keys are encrypted with) are another app's.
// `options.fetch` is for tests.
export function flyMachines({ app, token, image, region = 'fra', guest = { cpu_kind: 'shared', cpus: 1, memory_mb: 1024 }, fetch = globalThis.fetch }) {
    const api = `https://api.machines.dev/v1/apps/${encodeURIComponent(app)}/machines`;
    const headers = { authorization: `Bearer ${token}`, 'content-type': 'application/json' };
    return {
        async start({ build, token: buildToken, url }) {
            const response = await fetch(api, {
                method: 'POST',
                headers,
                body: JSON.stringify({
                    region,
                    config: {
                        image,
                        guest,
                        auto_destroy: true,
                        restart: { policy: 'no' },
                        env: { TIDE_BUILDS_URL: url, TIDE_BUILD: build, TIDE_BUILD_TOKEN: buildToken },
                        metadata: { tide_build: build },
                    },
                }),
            });
            if (!response.ok) throw new Error(`Fly answered ${response.status}: ${(await response.text()).slice(0, 300)}`);
            const machine = await response.json();
            if (typeof machine.id !== 'string') throw new Error('Fly made a machine with no id');
            return machine.id;
        },
        async alive(id) {
            const response = await fetch(`${api}/${encodeURIComponent(id)}`, { headers });
            if (response.status === 404) return false;
            if (!response.ok) throw new Error(`Fly answered ${response.status} about machine ${id}`);
            const machine = await response.json();
            return ['created', 'starting', 'started'].includes(machine.state);
        },
        async stop(id) {
            const response = await fetch(`${api}/${encodeURIComponent(id)}?force=true`, { method: 'DELETE', headers });
            if (!response.ok && response.status !== 404) throw new Error(`Fly answered ${response.status} to destroying machine ${id}`);
        },
    };
}
