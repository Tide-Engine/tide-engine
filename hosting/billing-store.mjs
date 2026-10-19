import { identifier, requireValue } from './store.mjs';

const units = 1_000_000;
export class BillingLedger {
    constructor(store) {
        this.store = store; this.db = store.db; this.required = false;
        this.db.exec(`
            CREATE TABLE IF NOT EXISTS billing_accounts (
                account TEXT PRIMARY KEY REFERENCES accounts(id), customer TEXT UNIQUE,
                customer_key TEXT NOT NULL, subscription TEXT UNIQUE, status TEXT NOT NULL DEFAULT 'none',
                cancel_at_end INTEGER NOT NULL DEFAULT 0, payment_method TEXT,
                extra INTEGER NOT NULL DEFAULT 0, spend_cap INTEGER NOT NULL DEFAULT 0,
                topup INTEGER NOT NULL DEFAULT 500, issue TEXT NOT NULL DEFAULT ''
            );
            CREATE TABLE IF NOT EXISTS allowances (
                invoice TEXT PRIMARY KEY, account TEXT NOT NULL REFERENCES accounts(id), subscription TEXT NOT NULL,
                starts INTEGER NOT NULL, ends INTEGER NOT NULL, amount INTEGER NOT NULL,
                remaining INTEGER NOT NULL CHECK(remaining>=0), expired INTEGER NOT NULL DEFAULT 0,
                UNIQUE(subscription,starts)
            );
            CREATE TABLE IF NOT EXISTS billing_purchases (
                id TEXT PRIMARY KEY, account TEXT NOT NULL REFERENCES accounts(id), purpose TEXT NOT NULL,
                amount INTEGER NOT NULL, currency TEXT NOT NULL, starts INTEGER NOT NULL, ends INTEGER NOT NULL,
                status TEXT NOT NULL DEFAULT 'pending', provider TEXT UNIQUE, url TEXT, parameters TEXT, created INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS lease_funds (
                reference TEXT NOT NULL, source TEXT NOT NULL, account TEXT NOT NULL, match_id TEXT NOT NULL,
                generation INTEGER NOT NULL, until INTEGER NOT NULL, amount INTEGER NOT NULL,
                PRIMARY KEY(reference,source)
            );
        `);
    }
    ensure(account) {
        this.db.prepare('INSERT OR IGNORE INTO billing_accounts(account,customer_key) VALUES (?,?)').run(account, identifier());
        return this.db.prepare('SELECT * FROM billing_accounts WHERE account=?').get(account);
    }
    period(account, at = this.store.now()) {
        return this.db.prepare(`SELECT * FROM allowances WHERE account=? AND starts<=? AND ends>? AND expired=0
            ORDER BY ends,starts DESC LIMIT 1`).get(account, at, at);
    }
    state(account, at = this.store.now()) {
        const row = this.db.prepare('SELECT * FROM billing_accounts WHERE account=?').get(account);
        const period = this.period(account, at), purchased = this.store.account(account).balance;
        const active = !this.required || Boolean(period);
        const included = period?.remaining || 0;
        const spent = period ? this.db.prepare(`SELECT COALESCE(SUM(amount),0) AS n FROM billing_purchases
            WHERE account=? AND starts=? AND purpose IN ('manual','auto') AND status<>'failed'`).get(account, period.starts).n : 0;
        return { required: this.required, active, included, purchased,
            balance: active ? included + ((!this.required || row?.extra) ? purchased : 0) : 0,
            periodStart: period?.starts || null, periodEnd: period?.ends || null,
            status: row?.status || 'none', cancelAtEnd: Boolean(row?.cancel_at_end),
            extraEnabled: Boolean(row?.extra), spendCap: row?.spend_cap || 0, spent,
            topup: row?.topup || 500, issue: row?.issue || '', hasCustomer: Boolean(row?.customer) };
    }
    settings(account, { enabled, cap, amount }, topups) {
        if (enabled === false && !topups.includes(amount)) amount = this.ensure(account).topup;
        requireValue(typeof enabled === 'boolean' && Number.isSafeInteger(cap) && cap >= 0 && cap <= 100000
            && (!enabled || (topups.includes(amount) && cap >= amount)), 400,
        'Choose a top-up amount and a monthly extra-spend cap at least that large.');
        this.ensure(account);
        this.db.prepare('UPDATE billing_accounts SET extra=?,spend_cap=?,topup=?,issue=? WHERE account=?')
            .run(enabled ? 1 : 0, cap, amount, '', account);
    }
    grant(account, subscription, invoice, starts, ends, amount) {
        requireValue(Number.isSafeInteger(starts) && Number.isSafeInteger(ends) && starts < ends
            && ends - starts >= 27 * 86400_000 && ends - starts <= 32 * 86400_000
            && Number.isSafeInteger(amount) && amount > 0, 400, 'Invalid monthly allowance period or amount.');
        return this.store.transaction(() => {
            const inserted = this.db.prepare(`INSERT OR IGNORE INTO allowances(invoice,account,subscription,starts,ends,amount,remaining)
                VALUES (?,?,?,?,?,?,?)`).run(invoice, account, subscription, starts, ends, amount, amount);
            if (!inserted.changes) return false;
            this.db.prepare('INSERT INTO ledger(account,amount,reference,created) VALUES (?,?,?,?)')
                .run(account, amount, `allowance/${invoice}`, this.store.now());
            return true;
        });
    }
    expire() {
        this.store.transaction(() => {
            for (const grant of this.db.prepare('SELECT * FROM allowances WHERE ends<=? AND expired=0').all(this.store.now())) {
                this.db.prepare('UPDATE allowances SET expired=1,remaining=0 WHERE invoice=?').run(grant.invoice);
                if (grant.remaining) this.db.prepare('INSERT INTO ledger(account,amount,reference,created) VALUES (?,?,?,?)')
                    .run(grant.account, -grant.remaining, `expiry/${grant.invoice}`, this.store.now());
            }
            this.db.prepare('DELETE FROM lease_funds WHERE until<=?').run(this.store.now());
        });
    }
    lease(account, from, maximum, rate) {
        const period = this.required ? this.period(account, from) : null;
        requireValue(!this.required || period, 402, 'An active paid hosting subscription is required.');
        const seconds = period ? Math.min(maximum, Math.ceil((period.ends - from) / 1000)) : maximum;
        requireValue(seconds > 0, 402, 'The paid subscription period is ending.');
        return { until: period ? Math.min(from + seconds * 1000, period.ends) : from + seconds * 1000, charge: seconds * rate };
    }
    // Called inside the Store's allocation transaction. Record the source so a
    // stopped match cannot turn an expired monthly reservation into bought credit.
    debit(account, amount, reference, at = this.store.now()) {
        requireValue(!this.store.account(account).disabled && this.state(account, at).balance >= amount,
            402, 'Insufficient hosting credit or no active paid subscription.');
        const grant = this.period(account, at), included = Math.min(grant?.remaining || 0, amount);
        const parts = reference.split('/'), generation = parts.length === 4 ? Number(parts[2]) : 0;
        const record = (source, value) => {
            if (!value) return;
            this.db.prepare('INSERT INTO lease_funds VALUES (?,?,?,?,?,?,?)')
                .run(reference, source, account, parts[1], generation, Number(parts.at(-1)), value);
        };
        if (included) {
            this.db.prepare('UPDATE allowances SET remaining=remaining-? WHERE invoice=?').run(included, grant.invoice);
            record(grant.invoice, included);
        }
        if (amount > included) {
            this.db.prepare('UPDATE accounts SET balance=balance-? WHERE id=?').run(amount - included, account);
            record('purchased', amount - included);
        }
        this.db.prepare('INSERT INTO ledger(account,amount,reference,created) VALUES (?,?,?,?)')
            .run(account, -amount, reference, this.store.now());
    }
    refund(account, match, amount) {
        const funds = this.db.prepare(`SELECT rowid,* FROM lease_funds WHERE match_id=? AND generation=?
            ORDER BY until DESC,rowid DESC`).all(match.id, match.generation);
        // Allocations made before this schema used purchased credit only.
        if (!funds.length) {
            this.db.prepare('UPDATE accounts SET balance=balance+? WHERE id=?').run(amount, account);
            return amount;
        }
        let left = amount, refunded = 0;
        for (const fund of funds) {
            const value = Math.min(left, fund.amount); if (!value) continue;
            this.db.prepare('UPDATE lease_funds SET amount=amount-? WHERE reference=? AND source=?')
                .run(value, fund.reference, fund.source);
            if (fund.source === 'purchased') {
                this.db.prepare('UPDATE accounts SET balance=balance+? WHERE id=?').run(value, account); refunded += value;
            } else if (this.db.prepare('UPDATE allowances SET remaining=remaining+? WHERE invoice=? AND ends>? AND expired=0')
                .run(value, fund.source, this.store.now()).changes) refunded += value;
            left -= value; if (!left) break;
        }
        return refunded;
    }
    purchase(account, purpose, amount, currency) {
        return this.store.transaction(() => {
            const pending = this.db.prepare(`SELECT * FROM billing_purchases WHERE account=? AND purpose=? AND status='pending'
                ORDER BY created LIMIT 1`).get(account, purpose);
            if (pending) {
                requireValue(pending.amount === amount, 409, 'Finish or cancel the pending purchase before choosing another amount.');
                return pending;
            }
            const state = this.state(account);
            if (purpose !== 'subscription') {
                requireValue(state.active && state.extraEnabled, 403, 'Enable extra usage on an active subscription before adding credit.');
                requireValue(state.spent + amount <= state.spendCap, 409, 'Monthly extra-spend cap reached.');
            }
            const id = identifier(), starts = state.periodStart || 0, ends = state.periodEnd || 0;
            this.db.prepare(`INSERT INTO billing_purchases(id,account,purpose,amount,currency,starts,ends,created)
                VALUES (?,?,?,?,?,?,?,?)`).run(id, account, purpose, amount, currency, starts, ends, this.store.now());
            return this.db.prepare('SELECT * FROM billing_purchases WHERE id=?').get(id);
        });
    }
    fulfill(purchase, intent) {
        return this.store.transaction(() => {
            const p = this.db.prepare('SELECT * FROM billing_purchases WHERE id=?').get(purchase.id);
            if (p.status === 'succeeded') return false;
            const reversed = Boolean(this.db.prepare('SELECT 1 FROM reversed_intents WHERE intent=?').get(intent));
            const value = p.amount * units;
            requireValue(Number.isSafeInteger(this.store.account(p.account).balance + value), 409, 'Credit balance limit reached.');
            this.db.prepare('INSERT INTO payments(id,account,amount,currency,intent,credited,reversed) VALUES (?,?,?,?,?,?,?)')
                .run(p.id, p.account, p.amount, p.currency, intent, reversed ? 0 : 1, reversed ? 1 : 0);
            this.db.prepare("UPDATE billing_purchases SET status='succeeded' WHERE id=?").run(p.id);
            if (reversed) {
                this.db.prepare('UPDATE accounts SET disabled=1 WHERE id=?').run(p.account);
                this.store.incident(p.account, null, 'payment', 'Payment reversed before fulfillment; hosting suspended pending review.');
            } else {
                this.db.prepare('UPDATE accounts SET balance=balance+? WHERE id=?').run(value, p.account);
                this.db.prepare('INSERT INTO ledger(account,amount,reference,created) VALUES (?,?,?,?)')
                    .run(p.account, value, `payment/${p.id}`, this.store.now());
            }
            return !reversed;
        });
    }
}
