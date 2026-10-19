import { join } from 'node:path';
import { Store, requireValue } from './store.mjs';
import { configuration } from './config.mjs';

// Operator-only local command. There is no HTTP route for credit adjustments,
// invitations, global capacity changes or account unfreezing.
const config = configuration();
const store = new Store(join(config.folder, 'hosting.sqlite'));
try {
    const [action, value] = process.argv.slice(2);
    if (action === 'invite') {
        store.invite(value); console.log(`Invited GitHub account ${value}.`);
    } else if (action === 'allocations') {
        requireValue(['on', 'off'].includes(value), 400, 'Use allocations on or allocations off.');
        store.setAllocations(value === 'on');
        console.log(value === 'off' ? 'New allocations are blocked; ongoing matches continue within prepaid leases.'
            : 'Operator allocation block removed. Environment capacity/payment limits still apply.');
    } else if (['freeze', 'unfreeze'].includes(action)) {
        requireValue(/^\d{1,20}$/.test(value || ''), 400, 'Supply the numeric GitHub account ID.');
        const account = store.db.prepare('SELECT id FROM accounts WHERE github_id=?').get(value);
        requireValue(account, 404, 'Account not found.');
        store.db.prepare('UPDATE accounts SET disabled=? WHERE id=?').run(action === 'freeze' ? 1 : 0, account.id);
        store.incident(account.id, null, 'operator', action === 'freeze' ? 'Hosting account frozen by the operator.' : 'Operator review completed; hosting account unfrozen.');
        console.log(`Account ${value}: ${action}.`);
    } else if (action === 'status' && !value) {
        console.log(JSON.stringify({ accepting: config.accepting && store.allocationsEnabled(),
            maximumMatches: config.hostMatches, reservedMemoryCeiling: config.hostMemoryBytes,
            active: store.activeMatches().length,
            accounts: store.db.prepare('SELECT COUNT(*) AS n FROM accounts').get().n,
            artifactBytes: store.db.prepare('SELECT COALESCE(SUM(bytes),0) AS n FROM releases').get().n }, null, 2));
    } else throw new Error('Usage: node admin.mjs invite <numeric-github-id> | allocations on|off | freeze|unfreeze <numeric-github-id> | status');
} catch (error) { console.error(`tide: ${error.message}`); process.exitCode = 1; }
finally { store.close(); }
