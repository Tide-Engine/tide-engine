import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHmac } from 'node:crypto';
import { Store } from './store.mjs';
import { Payments } from './payments.mjs';

const month = 30 * 86400_000;
function fixture(t) {
    let now = 1_800_000_000_000;
    const start = now;
    const store = new Store(':memory:', { now: () => now }); t.after(() => store.close());
    store.invite(123); const account = store.signIn({ id: 123, login: 'billing-test' });
    store.billing.required = true;
    store.billing.ensure(account.id);
    store.db.prepare('UPDATE billing_accounts SET customer=? WHERE account=?').run('cus_fixture', account.id);
    const config = { origin: 'https://console.example', stripeSecret: 'rk_test_fixture', stripeWebhookSecret: 'whsec_fixture',
        stripePrice: 'price_fixture', stripePortal: 'bpc_fixture', monthlyAmount: 2000, currency: 'eur', topups: [500,1000],
        region: 'fra', presets: { small: { rate: 1000 } }, projects: 3, accountMatches: 2, hostMatches: 2,
        accepting: true, leaseSeconds: 30, releases: 4, storageBytes: 10000 };
    const sub = { id: 'sub_fixture', customer: 'cus_fixture', metadata: { tide_account: account.id }, status: 'active',
        cancel_at_period_end: false, default_payment_method: 'pm_fixture', latest_invoice: 'in_fixture', items: { has_more: false,
            data: [{ quantity: 1, price: { id: 'price_fixture', currency: 'eur', unit_amount: 2000, recurring: { interval: 'month', interval_count: 1 } } }] } };
    const invoice = { id: 'in_fixture', customer: sub.customer, currency: 'eur', status: 'paid', amount_paid: 2000,
        billing_reason: 'subscription_create', parent: { subscription_details: { subscription: sub.id } }, lines: { has_more: false,
            data: [{ quantity: 1, amount: 2000, pricing: { price_details: { price: 'price_fixture' } }, period: { start: start / 1000, end: (start + month) / 1000 } }] } };
    const intents = new Map(), sessions = new Map(), requests = [];
    let intercept;
    const fetcher = async (url, options) => {
        const path = new URL(url).pathname.slice(4); requests.push({ path, ...options });
        assert.equal(options.headers['Stripe-Version'], '2026-09-30.endive');
        if (intercept) { const result = await intercept(path, options); if (result) return result; }
        if (path === `subscriptions/${sub.id}`) return Response.json(sub);
        if (path === `invoices/${invoice.id}`) return Response.json(invoice);
        if (path === 'customers/cus_fixture') return Response.json({ id: 'cus_fixture', invoice_settings: { default_payment_method: 'pm_fixture' } });
        if (path === 'checkout/sessions' && options.method === 'POST') {
            const id = `cs_${sessions.size}`, session = { id, url: `https://checkout.stripe.com/c/pay/${id}`, status: 'open',
                customer: 'cus_fixture', client_reference_id: account.id, mode: options.body.get('mode'),
                metadata: { tide_purchase: options.body.get('metadata[tide_purchase]') } };
            sessions.set(id, session); return Response.json(session);
        }
        if (path.startsWith('checkout/sessions/')) return Response.json(sessions.get(path.split('/')[2]));
        if (path === 'payment_intents') {
            const id = `pi_${intents.size}`, intent = { id, status: 'succeeded', customer: 'cus_fixture', currency: 'eur',
                amount: Number(options.body.get('amount')), amount_received: Number(options.body.get('amount')),
                metadata: { tide_purchase: options.body.get('metadata[tide_purchase]') } };
            intents.set(id, intent); return Response.json(intent);
        }
        if (path.startsWith('payment_intents/')) {
            const intent = intents.get(path.split('/')[1]);
            if (path.endsWith('/cancel')) intent.status = 'canceled';
            return Response.json(intent);
        }
        throw Error(`Unexpected provider request: ${path}`);
    };
    const payments = new Payments(store, { ...config, fetcher });
    const notify = async (type, id) => {
        const payload = Buffer.from(JSON.stringify({ id: 'evt_fixture', type, livemode: false, data: { object: { id } } }));
        const timestamp = String(Math.floor(now / 1000));
        const signature = createHmac('sha256', config.stripeWebhookSecret).update(`${timestamp}.`).update(payload).digest('hex');
        await payments.webhook(payload, `t=${timestamp},v1=${signature}`);
    };
    const project = () => {
        const p = store.createProject(account.id, { name: 'game', region: 'fra', preset: 'small', maximum: 2 }, config);
        store.registerRelease(account.id, p.id, { id: 'a'.repeat(64), simulation: 'b'.repeat(64), bytes: 1, manifest: {} }, config);
        return p;
    };
    return { store, account, config, payments, sub, invoice, sessions, intents, requests, notify, project,
        setNow: n => { now = n; }, start, intercept: fn => { intercept = fn; }, fetcher };
}

test('only verified paid invoices grant one monthly allowance; renewals expire old credit without rolling it forward', async t => {
    const f = fixture(t), { store, account, payments, sub, invoice } = f;
    await f.notify('customer.subscription.created', sub.id);
    assert.equal(store.billing.state(account.id).active, false);
    invoice.status = 'open'; await f.notify('invoice.paid', invoice.id);
    assert.equal(store.billing.state(account.id).balance, 0);
    invoice.status = 'paid'; await f.notify('invoice.paid', invoice.id);
    await f.notify('invoice.paid', invoice.id);
    assert.equal(store.billing.state(account.id).included, 2_000_000_000);
    const old = structuredClone(invoice);
    invoice.id = 'in_renewal'; invoice.billing_reason = 'subscription_cycle'; sub.latest_invoice = invoice.id;
    invoice.lines.data[0].period = { start: (f.start + month) / 1000, end: (f.start + 2 * month) / 1000 };
    f.setNow(f.start + month + 1); await payments.invoice(invoice.id); store.billing.expire();
    assert.equal(store.billing.state(account.id).included, 2_000_000_000);
    assert.equal(store.db.prepare('SELECT remaining FROM allowances WHERE invoice=?').get(old.id).remaining, 0);
    f.intercept(path => path === `invoices/${old.id}` ? Response.json(old) : null);
    await payments.invoice(old.id);
    assert.equal(store.billing.state(account.id).included, 2_000_000_000);
    sub.cancel_at_period_end = true; await payments.subscription(sub.id);
    assert.equal(store.billing.state(account.id).cancelAtEnd, true);
    assert.equal(store.billing.state(account.id).active, true);
    f.setNow(f.start + 2 * month); assert.equal(store.billing.state(account.id).active, false);
});

test('monthly allowance funds reservations first; unused funds return to their original bucket and expired grants stay expired', async t => {
    const f = fixture(t), { store, account, config } = f;
    await f.payments.invoice(f.invoice.id); const project = f.project();
    store.db.prepare('UPDATE allowances SET remaining=10000').run();
    store.db.prepare('UPDATE accounts SET balance=100000 WHERE id=?').run(account.id);
    assert.throws(() => store.reserveMatch(account.id, project.id, config), /Insufficient/);
    store.billing.settings(account.id, { enabled: true, cap: 1000, amount: 500 }, config.topups);
    const match = store.reserveMatch(account.id, project.id, config);
    assert.equal(store.billing.state(account.id).included, 0);
    assert.equal(store.account(account.id).balance, 80000);
    store.finishMatch(match.id, 'suspended', config);
    assert.equal(store.billing.state(account.id).included, 10000);
    assert.equal(store.account(account.id).balance, 100000);
    const next = store.resumeMatch(account.id, match.id, config);
    store.db.prepare('UPDATE allowances SET ends=?,remaining=0,expired=1').run(f.start - 1);
    store.finishMatch(next.id, 'suspended', config);
    assert.equal(store.account(account.id).balance, 100000);
    assert.equal(store.billing.state(account.id).included, 0);
    assert.equal(store.billing.state(account.id).active, false);
    assert.throws(() => store.resumeMatch(account.id, match.id, config), /subscription/);
});

test('a lease cannot cross the paid month boundary; a confirmed next invoice enables renewal without changing the match', async t => {
    const f = fixture(t), { store, account, config } = f;
    await f.payments.invoice(f.invoice.id); const project = f.project();
    f.setNow(f.start + month - 10000);
    const match = store.reserveMatch(account.id, project.id, config);
    assert.equal(match.paid_until, f.start + month);
    assert.equal(match.reserved, 10000);
    assert.throws(() => store.renewMatch(match.id, config), /subscription/);
    f.invoice.id = 'in_next'; f.invoice.lines.data[0].period = { start: (f.start + month) / 1000, end: (f.start + 2 * month) / 1000 };
    await f.payments.invoice(f.invoice.id);
    assert.equal(store.renewMatch(match.id, config).paid_until, f.start + month + 30000);
});

test('manual and automatic purchases reserve the same cap, use durable checkout IDs and credit only once after confirmed payment', async t => {
    const f = fixture(t), { store, account, config, payments } = f;
    await payments.invoice(f.invoice.id);
    await assert.rejects(payments.checkout(account, 500), /Enable extra/);
    store.billing.settings(account.id, { enabled: true, cap: 1000, amount: 500 }, config.topups);
    const url = await payments.checkout(account, 500);
    assert.equal(await payments.checkout(account, 500), url);
    const post = f.requests.filter(r => r.path === 'checkout/sessions'); assert.equal(post.length, 1);
    assert.match(post[0].headers['Idempotency-Key'], /^tide-checkout-/);
    assert.equal(post[0].body.get('payment_method_types[0]'), null);
    const session = [...f.sessions.values()][0]; session.status = 'complete'; session.payment_status = 'unpaid';
    await f.notify('checkout.session.completed', session.id); assert.equal(store.account(account.id).balance, 0);
    session.payment_status = 'paid'; session.amount_total = 500; session.currency = 'eur'; session.payment_intent = 'pi_manual';
    await f.notify('checkout.session.async_payment_succeeded', session.id);
    await f.notify('checkout.session.async_payment_succeeded', session.id);
    assert.equal(store.account(account.id).balance, 500_000_000);
    store.billing.purchase(account.id, 'auto', 500, 'eur');
    assert.equal(store.billing.state(account.id).spent, 1000);
    await assert.rejects(payments.checkout(account, 500), /cap reached/);
    f.setNow(f.start + month); assert.equal(store.account(account.id).balance, 500_000_000);
});

test('uncertain automatic charge retries keep one idempotency key and reserve the cap until payment succeeds', async t => {
    const f = fixture(t), { store, account, config, payments } = f;
    await payments.invoice(f.invoice.id); const project = f.project();
    store.reserveMatch(account.id, project.id, config); store.db.prepare('UPDATE allowances SET remaining=0').run();
    store.billing.settings(account.id, { enabled: true, cap: 500, amount: 500 }, config.topups);
    payments.presets = config.presets;
    let fail = true;
    f.intercept(path => { if (path === 'payment_intents' && fail) { fail = false; throw Error('Lost provider response'); } });
    await assert.rejects(payments.automatic(account), /Lost provider response/);
    assert.equal(store.billing.state(account.id).spent, 500);
    await assert.rejects(payments.checkout(account, 500), /cap reached/);
    const restarted = new Payments(store, { ...config, fetcher: f.fetcher }); restarted.presets = config.presets;
    await restarted.automatic(account);
    const posts = f.requests.filter(r => r.path === 'payment_intents'); assert.equal(posts.length, 2);
    assert.equal(posts[0].headers['Idempotency-Key'], posts[1].headers['Idempotency-Key']);
    assert.equal(store.account(account.id).balance, 500_000_000);
    await f.notify('payment_intent.succeeded', [...f.intents.keys()][0]);
    assert.equal(store.account(account.id).balance, 500_000_000);
    assert.equal(store.billing.state(account.id).spent, 500);
});

test('authentication-required automatic payments are canceled before releasing the cap and pause further automatic charges', async t => {
    const f = fixture(t), { store, account, config, payments } = f;
    await payments.invoice(f.invoice.id); const project = f.project(); store.reserveMatch(account.id, project.id, config);
    store.db.prepare('UPDATE allowances SET remaining=0').run();
    store.billing.settings(account.id, { enabled: true, cap: 500, amount: 500 }, config.topups); payments.presets = config.presets;
    f.intercept((path, options) => {
        if (path !== 'payment_intents') return;
        const intent = { id: 'pi_action', status: 'requires_action', customer: 'cus_fixture', currency: 'eur', amount: 500,
            metadata: { tide_purchase: options.body.get('metadata[tide_purchase]') } };
        f.intents.set(intent.id, intent);
        return Response.json({ error: { payment_intent: intent } }, { status: 402 });
    });
    await payments.automatic(account);
    assert.equal(f.intents.get('pi_action').status, 'canceled');
    assert.equal(store.billing.state(account.id).spent, 0);
    assert.match(store.billing.state(account.id).issue, /attention/);
    assert.equal(store.account(account.id).balance, 0);
    const calls = f.requests.length; await payments.automatic(account); assert.equal(f.requests.length, calls);
});

test('wrong-price and wrong-amount invoices are refused, unrelated customers are ignored, and restricted live keys verify live mode', async t => {
    const f = fixture(t);
    f.invoice.amount_paid = 1; await assert.rejects(f.payments.invoice(f.invoice.id), /paid monthly/);
    f.invoice.amount_paid = 2000; f.sub.items.data[0].price.id = 'price_other';
    await assert.rejects(f.payments.invoice(f.invoice.id), /hosting plan/);
    f.invoice.customer = 'cus_other'; await f.payments.invoice(f.invoice.id);
    const live = new Payments(f.store, { ...f.config, stripeSecret: 'rk_live_fixture' });
    const body = Buffer.from(JSON.stringify({ id: 'evt_live', type: 'ignored', livemode: true }));
    const timestamp = String(f.start / 1000), hash = createHmac('sha256', f.config.stripeWebhookSecret).update(`${timestamp}.`).update(body).digest('hex');
    assert.equal(live.verify(body, `t=${timestamp},v1=${hash}`).livemode, true);
    assert.equal(f.store.billing.state(f.account.id).active, false);
});
