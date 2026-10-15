import { createHmac, timingSafeEqual } from 'node:crypto';
import { identifier, requireValue } from './store.mjs';
import { providerJson } from './http.mjs';

export class Payments {
    constructor(store, { origin, stripeSecret, stripeWebhookSecret, currency = 'eur', topups = [], fetcher = fetch }) {
        Object.assign(this, { store, origin, stripeSecret, stripeWebhookSecret, currency, topups, fetcher });
    }
    async checkout(account, amount) {
        requireValue(this.stripeSecret && this.stripeWebhookSecret, 503, 'Payments are not configured.');
        requireValue(Number.isSafeInteger(amount) && this.topups.includes(amount), 400, 'Choose an available credit amount.');
        const session = await providerJson(this.fetcher, 'https://api.stripe.com/v1/checkout/sessions', {
            method: 'POST', headers: { Authorization: `Bearer ${this.stripeSecret}`,
                'Content-Type': 'application/x-www-form-urlencoded', 'Idempotency-Key': `tide-credit-${identifier()}` },
            body: new URLSearchParams({ mode: 'payment', client_reference_id: account.id,
                success_url: `${this.origin}/dashboard?payment=complete`, cancel_url: `${this.origin}/dashboard`,
                'line_items[0][price_data][currency]': this.currency,
                'line_items[0][price_data][unit_amount]': String(amount),
                'line_items[0][price_data][product_data][name]': 'Tide hosting prepaid credit',
                'line_items[0][quantity]': '1', 'payment_method_types[0]': 'card',
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
            && event.livemode === this.stripeSecret.startsWith('sk_live_'), 400, 'Payment notification mode did not match.');
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
                headers: { Authorization: `Bearer ${this.stripeSecret}` },
            });
            if (session.payment_status === 'paid') this.store.creditPayment(session);
        } else if (['charge.refunded', 'charge.dispute.created'].includes(event.type)) {
            const intent = event.data?.object?.payment_intent;
            if (typeof intent === 'string') this.store.reversePayment(intent);
        }
    }
}
