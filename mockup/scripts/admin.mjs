import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { openStore } from '../server/store.mjs';

// Run with the service stopped: the data directory has one writer.
export async function provisionAdmin({ dataDir, email }) {
    const address = typeof email === 'string' ? email.trim().toLowerCase() : '';
    if (address.length > 254 || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(address)) throw new Error('Use a valid email for an existing Google or Discord account.');
    const store = await openStore(dataDir);
    const result = await store.change(state => {
        const user = Object.values(state.users).find(user => user.email === address);
        const linked = user && Object.values(state.identities || {}).some(identity => identity.userId === user.id &&
            ['google', 'discord'].includes(identity.provider) && typeof identity.subject === 'string' && identity.subject.length > 0);
        if (!linked) throw new Error('Sign in with Google or Discord before granting owner access to this email.');
        user.admin = true;
        return { id: user.id, email: user.email };
    });
    await store.flush();
    return result;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const email = process.argv[2];
    if (!email) throw new Error('Stop the service, then run: node scripts/admin.mjs owner@example.com');
    const dataDir = resolve(process.env.MOCKUP_DATA_DIR || fileURLToPath(new URL('../.data', import.meta.url)));
    const user = await provisionAdmin({ dataDir, email });
    console.log(`Owner access granted to ${user.email}. Start the service and sign in with Google or Discord.`);
}
