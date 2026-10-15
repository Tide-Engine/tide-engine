import { createHmac, timingSafeEqual } from 'node:crypto';
import { identifier, requireValue, ServiceError } from './store.mjs';
import { providerJson } from './http.mjs';

// Keep API reads/writes on the same version as the configured webhook endpoint.
const stripeVersion = '2026-09-30.endive';

export class Payments {
    constructor(store, { origin, stripeSecret, stripeWebhookSecret, stripePrice, stripePortal, monthlyAmount = 2000, currency = 'eur', topups = [], fetcher = fetch }) {
        Object.assign(this, { store, origin, stripeSecret, stripeWebhookSecret, stripePrice, stripePortal, monthlyAmount, currency, topups, fetcher });
        this.inflight = new Map(); this.nextPoll = 0; this.nextReconcile = 0; this.shutdown = new AbortController();
    }
    ready() { requireValue(this.stripeSecret && this.stripeWebhookSecret, 503, 'Payments are not configured.'); }
    async api(route, parameters, key) {
        this.ready();
        const response = await this.fetcher(`https://api.stripe.com/v1/${route}`, {
            method: parameters ? 'POST' : 'GET', redirect: 'error', signal: AbortSignal.any([AbortSignal.timeout(10000), this.shutdown.signal]),
            headers: { Authorization: `Bearer ${this.stripeSecret}`, 'Stripe-Version': stripeVersion,
                ...(parameters ? { 'Content-Type': 'application/x-www-form-urlencoded' } : {}),
                ...(key ? { 'Idempotency-Key': key } : {}) },
            ...(parameters ? { body: new URLSearchParams(parameters) } : {}) });
        const data = await response.json();
        if (!response.ok || data.error) {
            const error = new ServiceError(502, 'Stripe could not complete this request. Try again or update your payment method.');
            error.providerStatus = response.status;
            error.paymentIntent = typeof data.error?.payment_intent === 'string' ? data.error.payment_intent : data.error?.payment_intent?.id;
            throw error;
        }
        return data;
    }
    once(account, fn) {
        if (this.inflight.has(account)) return Promise.reject(new ServiceError(409, 'Another billing request is in progress. Try again shortly.'));
        const promise = Promise.resolve().then(fn).finally(() => this.inflight.delete(account));
        this.inflight.set(account, promise); return promise;
    }
    async customer(account) {
        const row = this.store.billing.ensure(account.id);
        if (row.customer) return row.customer;
        const customer = await this.api('customers', { 'metadata[tide_account]': account.id }, `tide-customer-${row.customer_key}`);
        requireValue(/^cus_[A-Za-z0-9]+$/.test(customer.id), 502, 'Stripe did not return a valid customer.');
        this.store.db.prepare('UPDATE billing_accounts SET customer=? WHERE account=?').run(customer.id, account.id);
        return customer.id;
    }
    async subscription(id) {
        requireValue(/^sub_[A-Za-z0-9_]+$/.test(id), 400, 'Invalid subscription.');
        const sub = await this.api(`subscriptions/${id}`);
        const row = this.store.db.prepare('SELECT * FROM billing_accounts WHERE customer=?').get(sub.customer);
        if (!row) return null; // Other sandbox/business customers are not Tide accounts.
        requireValue(row && sub.metadata?.tide_account === row.account,
        400, 'Subscription does not belong to this hosting account.');
        const items = sub.items?.data;
        requireValue(items?.length === 1 && !sub.items.has_more && items[0].quantity === 1
            && items[0].price.id === this.stripePrice && items[0].price.currency === this.currency
            && items[0].price.unit_amount === this.monthlyAmount && items[0].price.recurring?.interval === 'month'
            && items[0].price.recurring.interval_count === 1, 400, 'Subscription does not match the hosting plan.');
        if (!row.subscription || row.subscription === sub.id || ['canceled','incomplete_expired'].includes(row.status))
            this.store.db.prepare(`UPDATE billing_accounts SET subscription=?,status=?,cancel_at_end=?,payment_method=? WHERE account=?`)
                .run(sub.id, sub.status, sub.cancel_at_period_end ? 1 : 0, typeof sub.default_payment_method === 'string' ? sub.default_payment_method : sub.default_payment_method?.id || null, row.account);
        return { sub, account: row.account };
    }
    async invoice(id) {
        requireValue(/^in_[A-Za-z0-9_]+$/.test(id), 400, 'Invalid invoice.');
        const invoice = await this.api(`invoices/${id}`);
        const subscription = invoice.parent?.subscription_details?.subscription;
        if (!subscription) return;
        if (!this.store.db.prepare('SELECT 1 FROM billing_accounts WHERE customer=?').get(invoice.customer)) return;
        const { sub, account } = await this.subscription(subscription);
        requireValue(invoice.customer === sub.customer && invoice.currency === this.currency, 400, 'Invoice account or currency did not match.');
        if (invoice.status !== 'paid') return;
        requireValue(['subscription_create','subscription_cycle'].includes(invoice.billing_reason)
            && invoice.amount_paid >= this.monthlyAmount, 400, 'Invoice is not a paid monthly renewal.');
        const lines = invoice.lines?.data || [];
        requireValue(!invoice.lines?.has_more && lines.length === 1, 400, 'Unexpected subscription invoice lines.');
        const line = lines[0];
        requireValue(line.pricing?.price_details?.price === this.stripePrice && line.quantity === 1
            && line.amount >= this.monthlyAmount && !line.parent?.subscription_item_details?.proration,
        400, 'Invoice does not match the monthly hosting plan.');
        this.store.billing.grant(account, sub.id, invoice.id, line.period.start * 1000, line.period.end * 1000, this.monthlyAmount * 1_000_000);
    }
    async hosted(account, purpose, amount) {
        this.ready();
        const customer = await this.customer(account);
        const current = this.store.billing.ensure(account.id);
        if (purpose === 'subscription') requireValue(!current.subscription || ['canceled','incomplete_expired','none'].includes(current.status),
            409, 'A subscription already exists. Manage its billing instead.');
        const purchase = this.store.billing.purchase(account.id, purpose, amount, this.currency);
        if (purchase.provider) {
            const session = await this.api(`checkout/sessions/${purchase.provider}`);
            if (session.status === 'expired') {
                this.store.db.prepare("UPDATE billing_purchases SET status='failed' WHERE id=?").run(purchase.id);
                return this.hosted(account, purpose, amount);
            }
            if (session.status === 'complete') {
                await this.checkoutEvent(session);
                throw new ServiceError(409, 'This checkout is complete. Refresh the dashboard to see its payment status.');
            }
            return this.checkoutUrl(session.url);
        }
        requireValue(this.store.now() - purchase.created < 23 * 3600_000, 409,
            'This purchase needs reconciliation before retrying. Contact hosting support.');
        const parameters = { mode: purpose === 'subscription' ? 'subscription' : 'payment', customer,
            client_reference_id: account.id, 'metadata[tide_purchase]': purchase.id,
            integration_identifier: `tide_${purpose}_${purchase.id.slice(0,8).replace(/[0-9]/g, n => String.fromCharCode(97 + Number(n)))}`,
            success_url: `${this.origin}/dashboard?payment=complete`, cancel_url: `${this.origin}/dashboard`,
            'line_items[0][quantity]': '1' };
        if (purpose === 'subscription') {
            parameters['line_items[0][price]'] = this.stripePrice;
            parameters['subscription_data[metadata][tide_account]'] = account.id;
        } else {
            parameters['line_items[0][price_data][currency]'] = this.currency;
            parameters['line_items[0][price_data][unit_amount]'] = String(amount);
            parameters['line_items[0][price_data][product_data][name]'] = 'Tide additional hosting credit';
            parameters['payment_intent_data[metadata][tide_purchase]'] = purchase.id;
        }
        const saved = this.parameters(purchase, parameters);
        let session;
        try { session = await this.api('checkout/sessions', saved, `tide-checkout-${purchase.id}`); }
        catch (error) {
            if (error.providerStatus >= 400 && error.providerStatus < 500)
                this.store.db.prepare("UPDATE billing_purchases SET status='failed' WHERE id=?").run(purchase.id);
            throw error;
        }
        requireValue(/^cs_[A-Za-z0-9_]+$/.test(session.id), 502, 'Stripe did not return a valid checkout.');
        const url = this.checkoutUrl(session.url);
        this.store.db.prepare('UPDATE billing_purchases SET provider=?,url=? WHERE id=?').run(session.id, url, purchase.id);
        return url;
    }
    checkoutUrl(url) {
        requireValue(typeof url === 'string' && new URL(url).origin === 'https://checkout.stripe.com', 502, 'Stripe did not return a valid checkout URL.');
        return url;
    }
    parameters(purchase, values) {
        this.store.db.prepare('UPDATE billing_purchases SET parameters=? WHERE id=? AND parameters IS NULL')
            .run(JSON.stringify(values), purchase.id);
        return JSON.parse(this.store.db.prepare('SELECT parameters FROM billing_purchases WHERE id=?').get(purchase.id).parameters);
    }
    subscribe(account) {
        requireValue(this.stripePrice, 503, 'Subscriptions are not configured.');
        return this.once(account.id, () => this.hosted(account, 'subscription', this.monthlyAmount));
    }
    async portal(account) {
        this.ready();
        const row = this.store.billing.ensure(account.id);
        requireValue(row.customer && this.stripePortal, 503, 'Billing management is not configured.');
        const result = await this.api('billing_portal/sessions', { customer: row.customer, configuration: this.stripePortal, return_url: `${this.origin}/dashboard` });
        requireValue(typeof result.url === 'string' && new URL(result.url).origin === 'https://billing.stripe.com', 502, 'Invalid billing portal URL.');
        return result.url;
    }
    async cancellation(account, cancel) {
        requireValue(typeof cancel === 'boolean', 400, 'Choose whether to cancel renewal.');
        const row = this.store.billing.ensure(account.id);
        requireValue(row.subscription, 409, 'There is no subscription to change.');
        await this.api(`subscriptions/${row.subscription}`, { cancel_at_period_end: String(cancel) });
        await this.subscription(row.subscription);
    }
    async checkoutEvent(session) {
        const purchase = this.store.db.prepare('SELECT * FROM billing_purchases WHERE id=?').get(session.metadata?.tide_purchase || '');
        requireValue(purchase && session.client_reference_id === purchase.account
            && session.customer === this.store.billing.ensure(purchase.account).customer, 400, 'Checkout account did not match.');
        if (purchase.purpose === 'subscription') {
            requireValue(session.mode === 'subscription' && session.subscription, 400, 'Invalid subscription checkout.');
            const { sub } = await this.subscription(session.subscription);
            this.store.db.prepare("UPDATE billing_purchases SET status='succeeded',provider=? WHERE id=?").run(session.id, purchase.id);
            if (sub.latest_invoice) await this.invoice(typeof sub.latest_invoice === 'string' ? sub.latest_invoice : sub.latest_invoice.id);
        } else if (session.payment_status === 'paid') {
            requireValue(session.mode === 'payment' && session.currency === purchase.currency && session.amount_total === purchase.amount
                && typeof session.payment_intent === 'string', 400, 'Credit payment did not match.');
            this.store.billing.fulfill(purchase, session.payment_intent);
        }
    }
    async intentEvent(id) {
        requireValue(/^pi_[A-Za-z0-9_]+$/.test(id), 400, 'Invalid payment intent.');
        const intent = await this.api(`payment_intents/${id}`);
        const p = this.store.db.prepare('SELECT * FROM billing_purchases WHERE id=?').get(intent.metadata?.tide_purchase || '');
        if (!p || p.purpose !== 'auto') return;
        requireValue(intent.customer === this.store.billing.ensure(p.account).customer && intent.amount === p.amount
            && intent.currency === p.currency, 400, 'Automatic payment did not match.');
        if (intent.status === 'succeeded') {
            requireValue(intent.amount_received === p.amount, 400, 'Automatic payment amount did not match.');
            this.store.billing.fulfill(p, intent.id);
        } else if (['requires_action','requires_payment_method','canceled'].includes(intent.status)) {
            // Do not release the spend-cap reservation while the provider can
            // still complete this payment later.
            if (intent.status !== 'canceled') {
                const canceled = await this.api(`payment_intents/${intent.id}/cancel`, {});
                requireValue(canceled.status === 'canceled', 409, 'The pending top-up could not yet be canceled.');
            }
            this.store.db.prepare("UPDATE billing_purchases SET status='failed' WHERE id=? AND status='pending'").run(p.id);
            this.store.db.prepare('UPDATE billing_accounts SET issue=? WHERE account=?')
                .run('Automatic top-up needs attention. Update your payment method or add credit manually, then save extra-usage settings to retry.', p.account);
        }
    }
    async automatic(account) {
        const row = this.store.billing.ensure(account.id), state = this.store.billing.state(account.id);
        if (!state.active || !row.extra || row.issue || account.disabled || !this.topups.includes(row.topup)) return;
        const pending = this.store.db.prepare("SELECT * FROM billing_purchases WHERE account=? AND purpose='auto' AND status='pending'").get(account.id);
        const need = this.store.db.prepare(`SELECT p.preset FROM matches m JOIN projects p ON p.id=m.project
            WHERE p.account=? AND m.state IN ('starting','running','recovering')`).all(account.id)
            .reduce((sum, p) => sum + (this.presets?.[p.preset]?.rate || 0) * 60, 0);
        if (!pending && (!need || state.balance >= need || state.spent + row.topup > row.spend_cap)) return;
        const p = pending || this.store.billing.purchase(account.id, 'auto', row.topup, this.currency);
        if (p.provider) { await this.intentEvent(p.provider); return; }
        if (this.store.now() - p.created >= 23 * 3600_000) {
            this.store.db.prepare('UPDATE billing_accounts SET issue=? WHERE account=?').run('A pending top-up needs support reconciliation; automatic purchases are paused.', account.id); return;
        }
        let parameters = p.parameters ? JSON.parse(p.parameters) : null;
        if (!parameters) {
            const { sub } = await this.subscription(row.subscription);
            const customer = await this.api(`customers/${row.customer}`);
            const method = customer.invoice_settings?.default_payment_method || sub.default_payment_method;
            if (!method) {
                this.store.db.prepare("UPDATE billing_purchases SET status='failed' WHERE id=?").run(p.id);
                this.store.db.prepare('UPDATE billing_accounts SET issue=? WHERE account=?').run('Add a payment method in Manage billing, then save extra-usage settings to retry.', account.id); return;
            }
            parameters = this.parameters(p, { amount: String(p.amount), currency: p.currency, customer: row.customer,
                payment_method: typeof method === 'string' ? method : method.id, confirm: 'true', off_session: 'true',
                'automatic_payment_methods[enabled]': 'true', 'automatic_payment_methods[allow_redirects]': 'never',
                'metadata[tide_purchase]': p.id, description: 'Tide prepaid extra hosting usage' });
        }
        let intent;
        try {
            intent = await this.api('payment_intents', parameters, `tide-auto-${p.id}`);
        } catch (error) {
            if (error.paymentIntent && /^pi_[A-Za-z0-9_]+$/.test(error.paymentIntent)) {
                this.store.db.prepare('UPDATE billing_purchases SET provider=? WHERE id=?').run(error.paymentIntent, p.id);
                await this.intentEvent(error.paymentIntent); return;
            }
            if (error.providerStatus >= 400 && error.providerStatus < 500) {
                this.store.db.prepare("UPDATE billing_purchases SET status='failed' WHERE id=?").run(p.id);
                this.store.db.prepare('UPDATE billing_accounts SET issue=? WHERE account=?').run('Automatic top-up failed. Update billing or add credit manually; save extra-usage settings to retry.', account.id);
            }
            throw error;
        }
        requireValue(/^pi_[A-Za-z0-9_]+$/.test(intent.id), 502, 'Invalid automatic payment response.');
        this.store.db.prepare('UPDATE billing_purchases SET provider=? WHERE id=?').run(intent.id, p.id);
        await this.intentEvent(intent.id);
    }
    poll(config) {
        if (!this.stripePrice || this.store.now() < this.nextPoll) return;
        this.nextPoll = this.store.now() + 5000; this.presets = config.presets;
        const reconcile = this.store.now() >= this.nextReconcile;
        if (reconcile) this.nextReconcile = this.store.now() + 60_000;
        for (const row of this.store.db.prepare('SELECT account FROM billing_accounts').all()) {
            this.once(row.account, async () => {
                const account = this.store.account(row.account);
                if (reconcile) await this.reconcile(account);
                await this.automatic(account);
            }).catch(() => { /* Preserve pending purchases for an idempotent retry. */ });
        }
    }
    async close() { this.shutdown.abort(); await Promise.allSettled([...this.inflight.values()]); }
    async reconcile(account) {
        for (const p of this.store.db.prepare("SELECT * FROM billing_purchases WHERE account=? AND status='pending' AND purpose<>'auto'").all(account.id)) {
            if (!p.provider) continue; // Retry the original Checkout request with its durable idempotency key.
            const session = await this.api(`checkout/sessions/${p.provider}`);
            if (session.status === 'complete') await this.checkoutEvent(session);
            else if (session.status === 'expired') this.store.db.prepare("UPDATE billing_purchases SET status='failed' WHERE id=?").run(p.id);
        }
        for (const p of this.store.db.prepare("SELECT provider FROM billing_purchases WHERE account=? AND purpose='auto' AND status='pending' AND provider IS NOT NULL").all(account.id))
            await this.intentEvent(p.provider);
        const row = this.store.billing.ensure(account.id);
        if (!row.subscription) return;
        const { sub } = await this.subscription(row.subscription);
        if (sub.latest_invoice) await this.invoice(typeof sub.latest_invoice === 'string' ? sub.latest_invoice : sub.latest_invoice.id);
    }
    async checkout(account, amount) {
        if (this.stripePrice) {
            requireValue(this.topups.includes(amount), 400, 'Choose an available credit amount.');
            return this.once(account.id, () => this.hosted(account, 'manual', amount));
        }
        requireValue(this.stripeSecret && this.stripeWebhookSecret, 503, 'Payments are not configured.');
        requireValue(Number.isSafeInteger(amount) && this.topups.includes(amount), 400, 'Choose an available credit amount.');
        const session = await providerJson(this.fetcher, 'https://api.stripe.com/v1/checkout/sessions', {
            method: 'POST', headers: { Authorization: `Bearer ${this.stripeSecret}`,
                'Stripe-Version': stripeVersion,
                'Content-Type': 'application/x-www-form-urlencoded', 'Idempotency-Key': `tide-credit-${identifier()}` },
            body: new URLSearchParams({ mode: 'payment', client_reference_id: account.id,
                success_url: `${this.origin}/dashboard?payment=complete`, cancel_url: `${this.origin}/dashboard`,
                'line_items[0][price_data][currency]': this.currency,
                'line_items[0][price_data][unit_amount]': String(amount),
                'line_items[0][price_data][product_data][name]': 'Tide hosting prepaid credit',
                'line_items[0][quantity]': '1',
            }),
        });
        requireValue(typeof session.id === 'string' && session.id.startsWith('cs_')
            && typeof session.url === 'string' && new URL(session.url).origin === 'https://checkout.stripe.com',
        502, 'Stripe did not return a valid checkout.');
        this.store.createPayment(session.id, account.id, amount, this.currency);
        return session.url;
    }
    verify(payload, signature) {
        requireValue(this.stripeSecret && this.stripeWebhookSecret && typeof signature === 'string' && signature.length <= 2048,
            400, 'Invalid payment notification.');
        const fields = signature.split(',').map(part => part.split('='));
        const timestamp = fields.find(([key]) => key === 't')?.[1];
        requireValue(/^\d{1,12}$/.test(timestamp || '') && Math.abs(this.store.now() / 1000 - Number(timestamp)) <= 300,
            400, 'Expired payment notification.');
        const expected = createHmac('sha256', this.stripeWebhookSecret).update(`${timestamp}.`).update(payload).digest();
        requireValue(fields.some(([key, value]) => key === 'v1' && /^[a-f0-9]{64}$/.test(value || '')
            && timingSafeEqual(expected, Buffer.from(value, 'hex'))), 400, 'Invalid payment notification signature.');
        let event;
        try { event = JSON.parse(payload.toString('utf8')); } catch { requireValue(false, 400, 'Invalid payment notification.'); }
        requireValue(event && typeof event.id === 'string' && typeof event.type === 'string'
            && event.livemode === /^[sr]k_live_/.test(this.stripeSecret), 400, 'Payment notification mode did not match.');
        return event;
    }
    async webhook(payload, signature) {
        const event = this.verify(payload, signature);
        if (['checkout.session.completed', 'checkout.session.async_payment_succeeded'].includes(event.type)) {
            const id = event.data?.object?.id;
            requireValue(typeof id === 'string' && /^cs_[A-Za-z0-9_]{1,200}$/.test(id), 400, 'Invalid checkout ID.');
            // Fetch current provider state; a redirect or an old event is never
            // sufficient authority to credit the customer's balance.
            const session = await providerJson(this.fetcher, `https://api.stripe.com/v1/checkout/sessions/${id}`, {
                headers: { Authorization: `Bearer ${this.stripeSecret}`, 'Stripe-Version': stripeVersion },
            });
            if (session.metadata?.tide_purchase) await this.checkoutEvent(session);
            else if (session.payment_status === 'paid') this.store.creditPayment(session);
        } else if (event.type === 'checkout.session.expired') {
            const session = await this.api(`checkout/sessions/${event.data?.object?.id}`);
            if (session.status === 'expired') this.store.db.prepare("UPDATE billing_purchases SET status='failed' WHERE provider=? AND status='pending'").run(session.id);
        } else if (event.type === 'invoice.paid' || event.type === 'invoice.payment_failed') {
            await this.invoice(event.data?.object?.id);
        } else if (['customer.subscription.created','customer.subscription.updated','customer.subscription.deleted'].includes(event.type)) {
            await this.subscription(event.data?.object?.id);
        } else if (event.type === 'payment_intent.succeeded' || event.type === 'payment_intent.payment_failed') {
            await this.intentEvent(event.data?.object?.id);
        } else if (['charge.refunded', 'charge.dispute.created'].includes(event.type)) {
            const intent = event.data?.object?.payment_intent;
            if (typeof intent === 'string') this.store.reversePayment(intent);
            const charge = event.type === 'charge.dispute.created' ? await this.api(`charges/${event.data?.object?.charge}`) : event.data?.object;
            const row = this.store.db.prepare('SELECT account FROM billing_accounts WHERE customer=?').get(charge?.customer || '');
            if (row) {
                this.store.db.prepare('UPDATE accounts SET disabled=1 WHERE id=?').run(row.account);
                this.store.incident(row.account, null, 'payment', 'A hosting payment was refunded or disputed; hosting suspended pending review.');
            }
        }
    }
}
