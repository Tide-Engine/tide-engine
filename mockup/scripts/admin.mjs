import { scrypt, randomBytes, randomUUID } from 'node:crypto';
import { promisify } from 'node:util';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { openStore } from '../server/store.mjs';

// Run with the service stopped: the data directory has one writer.
export async function provisionAdmin({ dataDir, email, password, name = 'Owner' }) {
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email || '') || password?.length < 10 || password?.length > 256) throw new Error('Use a valid email and a password of 10–256 characters.');
    const store = await openStore(dataDir);
    const salt = randomBytes(16).toString('hex');
    const passwordHash = (await promisify(scrypt)(password, salt, 64)).toString('hex');
    await store.change(state => {
        const user = Object.values(state.users).find(user => user.email === email.toLowerCase());
        if (user) Object.assign(user, { admin: true, salt, passwordHash });
        else {
            const id = randomUUID();
            state.users[id] = { id, email: email.toLowerCase(), name, admin: true, salt, passwordHash, createdAt: new Date().toISOString() };
        }
    });
    await store.flush();
}

async function passwordPrompt() {
    if (!process.stdin.isTTY) {
        let value = ''; for await (const part of process.stdin) value += part;
        return value.replace(/\r?\n$/, '');
    }
    process.stdout.write('Owner password (10–256 characters; hidden): ');
    return new Promise((done, fail) => {
        let value = '';
        process.stdin.setRawMode(true); process.stdin.resume(); process.stdin.setEncoding('utf8');
        const finish = () => { process.stdin.off('data', read); process.stdin.setRawMode(false); process.stdin.pause(); process.stdout.write('\n'); };
        const read = part => {
            for (const char of part) {
                if (char === '\u0003') { finish(); fail(new Error('Cancelled.')); return; }
                if (char === '\r' || char === '\n') { finish(); done(value); return; }
                if (char === '\u007f' || char === '\b') value = value.slice(0, -1);
                else if (char >= ' ' && value.length < 257) value += char;
            }
        };
        process.stdin.on('data', read);
    });
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const email = process.argv[2];
    if (!email) throw new Error('Stop the service, then run: node scripts/admin.mjs owner@example.com');
    const dataDir = resolve(process.env.MOCKUP_DATA_DIR || fileURLToPath(new URL('../.data', import.meta.url)));
    await provisionAdmin({ dataDir, email, password: await passwordPrompt() });
    console.log(`Owner account provisioned for ${email}. Start the service and sign in.`);
}
