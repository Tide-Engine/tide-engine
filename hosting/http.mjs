import { ServiceError, requireValue } from './store.mjs';

export function json(response, status, value, headers = {}) {
    const body = JSON.stringify(value);
    response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8',
        'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', ...headers });
    response.end(body);
}
export async function bytes(request, maximum) {
    const declared = request.headers['content-length'];
    requireValue(!declared || (/^\d+$/.test(declared) && Number(declared) <= maximum), 413, 'Request is too large.');
    const chunks = [];
    let size = 0;
    for await (const chunk of request) {
        size += chunk.length;
        if (size > maximum) throw new ServiceError(413, 'Request is too large.');
        chunks.push(chunk);
    }
    return Buffer.concat(chunks, size);
}
export async function body(request, maximum = 16_384) {
    requireValue(request.headers['content-type']?.split(';')[0] === 'application/json', 415, 'Send JSON with Content-Type: application/json.');
    try { return JSON.parse((await bytes(request, maximum)).toString('utf8')); }
    catch (error) {
        if (error instanceof ServiceError) throw error;
        throw new ServiceError(400, 'Invalid JSON.');
    }
}
export function cookie(request, name) {
    const found = String(request.headers.cookie || '').split(';').map(part => part.trim()).find(part => part.startsWith(`${name}=`));
    return found?.slice(name.length + 1);
}
export function setCookie(name, value, age, secure) {
    return `${name}=${value}; Path=/; HttpOnly; SameSite=Lax; Max-Age=${age}${secure ? '; Secure' : ''}`;
}
export async function providerJson(fetcher, url, options = {}) {
    const response = await fetcher(url, { ...options, redirect: 'error', signal: AbortSignal.timeout(15_000) });
    requireValue(response.ok, 502, 'The authentication or payment provider is temporarily unavailable.');
    const data = await response.json();
    requireValue(data && typeof data === 'object' && !data.error, 502, 'The provider could not complete this request.');
    return data;
}
export class RateLimits {
    constructor(now = Date.now) { this.entries = new Map(); this.now = now; }
    take(key, maximum, window = 60_000) {
        const now = this.now();
        if (this.entries.size >= 10_000) {
            for (const [key, entry] of this.entries) if (entry.until <= now) this.entries.delete(key);
            requireValue(this.entries.size < 10_000, 429, 'Service is busy. Try again shortly.');
        }
        let entry = this.entries.get(key);
        if (!entry || entry.until <= now) this.entries.set(key, entry = { n: 0, until: now + window });
        requireValue(++entry.n <= maximum, 429, 'Too many requests. Try again shortly.');
    }
}
