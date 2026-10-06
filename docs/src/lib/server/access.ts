import { randomUUID } from 'node:crypto';
import { mkdir, open } from 'node:fs/promises';
import path from 'node:path';

const MAX_BODY_BYTES = 16 * 1024;
const WINDOW_MS = 15 * 60 * 1000;
const MAX_CLIENTS = 10_000;
const USERNAME = /^[a-z\d](?:[a-z\d]|-(?=[a-z\d])){0,38}$/i;

export interface AccessRequest {
    id: string;
    createdAt: string;
    github: string;
    project: string;
}

interface Result {
    status: number;
    success: boolean;
    message: string;
    github: string;
    project: string;
    errors: { github?: string; project?: string; form?: string };
}

/** A private append-only file on the site's persistent volume. */
export async function appendRequest(file: string, request: AccessRequest) {
    await mkdir(path.dirname(file), { recursive: true, mode: 0o700 });
    const handle = await open(file, 'a', 0o600);
    try {
        await handle.writeFile(`${JSON.stringify(request)}\n`, 'utf8');
        await handle.sync();
    } finally {
        await handle.close();
    }
}

/** Limit bytes while reading, including requests without Content-Length. */
async function readBody(request: Request): Promise<string | undefined> {
    const declared = Number(request.headers.get('content-length'));
    if (declared > MAX_BODY_BYTES) {
        await request.body?.cancel().catch(() => {});
        return undefined;
    }
    const reader = request.body?.getReader();
    if (!reader) return '';
    const chunks: Uint8Array[] = [];
    let bytes = 0;
    try {
        for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            bytes += value.byteLength;
            if (bytes > MAX_BODY_BYTES) {
                await reader.cancel().catch(() => {});
                return undefined;
            }
            chunks.push(value);
        }
        return Buffer.concat(chunks).toString('utf8');
    } finally {
        reader.releaseLock();
    }
}

export function createAccessService(options: {
    file: string;
    now?: () => number;
    save?: typeof appendRequest;
}) {
    const now = options.now ?? Date.now;
    const save = options.save ?? appendRequest;
    const clients = new Map<string, { count: number; until: number }>();
    // One process owns the volume. Serialize appends so records cannot mix.
    let writing = Promise.resolve();

    function allowed(client: string) {
        const at = now();
        for (const [key, entry] of clients) {
            if (entry.until <= at) clients.delete(key);
        }
        const entry = clients.get(client);
        if (entry) {
            if (entry.count >= 3) return false;
            entry.count++;
        } else {
            if (clients.size >= MAX_CLIENTS) return false;
            clients.set(client, { count: 1, until: at + WINDOW_MS });
        }
        return true;
    }

    return {
        async submit(request: Request, client: string): Promise<Result> {
            const result: Result = { status: 200, success: false, message: '', github: '', project: '', errors: {} };
            const reject = (status: number, message: string) => ({ ...result, status, errors: { ...result.errors, form: message } });
            if (request.headers.get('content-type')?.split(';')[0].trim() !== 'application/x-www-form-urlencoded') {
                return reject(415, 'Please submit this page’s form.');
            }
            let body: string | undefined;
            try {
                body = await readBody(request);
            } catch {
                return reject(400, 'The request could not be read. Please try again.');
            }
            if (body === undefined) return reject(413, 'The request is too large. Keep the project description under 1,000 characters.');
            const fields = new URLSearchParams(body);
            if ([...fields.keys()].some((key) => !['github', 'project', 'website'].includes(key) || fields.getAll(key).length !== 1)) {
                return reject(400, 'Please submit one value for each field.');
            }
            // Bots filling the hidden field receive the same confirmation.
            if (fields.get('website')) return { ...result, success: true, message: 'Request received.' };

            const github = (fields.get('github') ?? '').trim();
            const project = (fields.get('project') ?? '').trim();
            result.github = github.slice(0, 200);
            result.project = project.slice(0, 1000);
            if (!USERNAME.test(github)) result.errors.github = 'Enter your GitHub username, without @. Use letters, numbers and single hyphens.';
            if (project.length > 1000) result.errors.project = 'Keep the project description under 1,000 characters.';
            if (Object.keys(result.errors).length) return { ...result, status: 400 };
            if (!allowed(client)) return reject(429, 'Too many requests. Please try again in 15 minutes.');

            const record: AccessRequest = { id: randomUUID(), createdAt: new Date(now()).toISOString(), github, project };
            const pending = writing.then(() => save(options.file, record));
            writing = pending.catch(() => {});
            try {
                await pending;
            } catch {
                return reject(503, 'Your request could not be saved. Please try again later.');
            }
            return { ...result, success: true, message: 'Request received. We’ll review it manually. If access is approved, you’ll receive an invitation on GitHub.' };
        },
    };
}
