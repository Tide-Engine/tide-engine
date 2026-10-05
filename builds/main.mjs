// Runs the build service (builds.mjs): node main.mjs [port], or $PORT, 8080
// without one. What it needs is in the environment:
//
//   TIDE_BUILDS_ADMIN       the admin secret, which makes games
//   TIDE_BUILDS_MASTER_KEY  32 bytes in base64, which the games' private keys are encrypted with
//   TIDE_BUILDS_DATA        the folder everything is kept in (./data without it): on Fly, the app's volume
//   TIDE_BUILDS_URL         where the service is reached, by players and by its builders
//
// On Fly (fly.toml), builders are Machines of another app:
//
//   TIDE_BUILDERS_APP       that app
//   TIDE_BUILDERS_TOKEN     a token for it, and for nothing else
//   TIDE_BUILDERS_IMAGE     the builders' image, when it isn't that app's own for the tide of
//                           tide.version: registry.fly.io/<app>:tide-<version> (fly.builder.toml)
//
// Without them, builders are processes on this machine, which run $TIDE (or
// tide) as it is, with nothing kept from it: for trying the service, never
// for others' code.

import { readFileSync } from 'node:fs';

import { createBuilds } from './builds.mjs';
import { flyMachines, localBuilders } from './machines.mjs';
import { folderStore } from './store.mjs';

const port = Number(process.argv[2] || process.env.PORT || 8080);
const need = name => {
    if (!process.env[name]) {
        console.error(`builds: ${name} isn't set`);
        process.exit(2);
    }
    return process.env[name];
};
const tide = (process.env.TIDE_BUILDS_TIDE || readFileSync(new URL('./tide.version', import.meta.url), 'utf8')).trim();
const fly = process.env.TIDE_BUILDERS_APP;
const builders = fly
    ? flyMachines({
        app: fly,
        token: need('TIDE_BUILDERS_TOKEN'),
        image: process.env.TIDE_BUILDERS_IMAGE || `registry.fly.io/${fly}:tide-${tide}`,
        region: process.env.FLY_REGION || 'fra',
    })
    : localBuilders({ env: { TIDE_BUILD_ISOLATION: 'none' }, quiet: false });
const store = folderStore(process.env.TIDE_BUILDS_DATA || './data');
await store.tidy();
const service = createBuilds({
    store,
    builders,
    admin: need('TIDE_BUILDS_ADMIN'),
    masterKey: Buffer.from(need('TIDE_BUILDS_MASTER_KEY'), 'base64'),
    tide,
    url: process.env.TIDE_BUILDS_URL || `http://127.0.0.1:${port}`,
});
service.listen(port, () => console.log(`builds: listening on port ${port}, with tide ${tide}, builders ${fly ? `in ${fly}` : 'on this machine'}`));

// Fly stops machines with SIGINT or SIGTERM. Builds go on in their own
// machines, and say how they went when this one is back.
for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => process.exit(0));
