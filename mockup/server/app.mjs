import { createServer } from 'node:http';
import { createHash, randomBytes, randomUUID, scrypt as deriveKey, timingSafeEqual } from 'node:crypto';
import { promisify } from 'node:util';
import { lstat, mkdir, readFile, readdir, writeFile } from 'node:fs/promises';
import { isAbsolute, join, relative, resolve } from 'node:path';
import { openStore } from './store.mjs';
import { appendCookie, authError, cookie, createOAuth } from './oauth.mjs';
import * as Y from 'yjs';

const scrypt = promisify(deriveKey);
const SESSION_AGE = 30 * 24 * 60 * 60 * 1000;
const PREVIEW_AGE = 60 * 60 * 1000;
const ASSET_LIMIT = 10 * 1024 * 1024;
const BODY_LIMIT = 15 * 1024 * 1024;
const id = () => randomUUID();
const now = () => new Date().toISOString();
const hash = value => createHash('sha256').update(value).digest('hex');

function fail(status, message) {
    const error = new Error(message);
    error.status = status;
    throw error;
}

function text(value, label, max = 12000, min = 1) {
    if (typeof value !== 'string' || value.trim().length < min || value.length > max) fail(400, `${label} must contain ${min}–${max} characters.`);
    return value.trim();
}

function polish(value = 'polished') {
    if (value !== 'polished' && value !== 'quick') fail(400, 'Choose polished or quick.');
    return value;
}

function documentUpdate(idea, encoded, update) {
    const doc = new Y.Doc();
    const brief = doc.getText('brief');
    try {
        if (encoded) Y.applyUpdate(doc, Buffer.from(encoded, 'base64'));
        else brief.insert(0, idea);
        if (update) Y.applyUpdate(doc, update);
        if (doc.share.size !== 1 || !doc.share.has('brief') || brief.length > 12000) fail(400, 'The shared brief exceeds its limit.');
        const state = Y.encodeStateAsUpdate(doc);
        if (state.length > 256 * 1024) fail(400, 'The shared document exceeds its limit.');
        return { encoded: Buffer.from(state).toString('base64'), idea: brief.toString() };
    } catch (error) {
        if (error.status) throw error;
        fail(400, 'Invalid shared document update.');
    } finally { doc.destroy(); }
}

function sources(value) {
    if (!Array.isArray(value) || value.length < 1 || value.length > 128) fail(400, 'A generation must contain 1–128 source files.');
    const names = new Set();
    let size = 0;
    return value.map(file => {
        const path = file?.path;
        if (typeof path !== 'string' || path.length > 200 || !/^[a-zA-Z0-9_./-]+$/.test(path) || path.startsWith('/') || path.split('/').some(part => !part || part === '.' || part === '..') || names.has(path.toLowerCase())) fail(400, 'Invalid or repeated source path.');
        if (typeof file.content !== 'string') fail(400, 'Source content must be text.');
        size += Buffer.byteLength(file.content);
        if (size > 2 * 1024 * 1024) fail(400, 'Generated source exceeds 2 MB.');
        names.add(path.toLowerCase());
        return { path, content: file.content };
    });
}

function inputEvent(value) {
    if (!value || typeof value !== 'object' || Array.isArray(value)) fail(400, 'An input event is required.');
    const serialized = JSON.stringify(value);
    if (serialized.length > 4096 || !['keydown', 'keyup', 'mousedown', 'mouseup', 'mousemove', 'pointerdown', 'pointerup', 'pointermove', 'pointercancel', 'wheel', 'blur', 'clear'].includes(value.type)) fail(400, 'Unsupported input event.');
    for (const coordinate of ['x', 'y']) if (value[coordinate] !== undefined && (typeof value[coordinate] !== 'number' || !Number.isFinite(value[coordinate]) || value[coordinate] < 0 || value[coordinate] > 1)) fail(400, 'Input coordinates must be between 0 and 1.');
    return JSON.parse(serialized);
}

function capture(value) {
    if (value === undefined || value === null) return null;
    if (typeof value !== 'object' || Array.isArray(value) || JSON.stringify(value).length > 2 * 1024 * 1024) fail(400, 'Feedback capture exceeds 2 MB.');
    if (value.screenshot !== undefined && (typeof value.screenshot !== 'string' || !/^data:image\/(png|jpeg|webp);base64,[a-zA-Z0-9+/=]+$/.test(value.screenshot))) fail(400, 'The screenshot must be a PNG, JPEG or WebP image.');
    if (value.inputs !== undefined && (!Array.isArray(value.inputs) || value.inputs.length > 600)) fail(400, 'A capture may contain at most 600 recent inputs.');
    return structuredClone(value);
}

async function body(req, limit = BODY_LIMIT) {
    if (!/^application\/json(?:;|$)/i.test(req.headers['content-type'] || '')) fail(415, 'Send JSON with Content-Type: application/json.');
    const declared = Number(req.headers['content-length']);
    if (Number.isFinite(declared) && declared > limit) fail(413, 'Request body is too large.');
    const pieces = [];
    let size = 0;
    for await (const chunk of req) {
        size += chunk.length;
        if (size > limit) fail(413, 'Request body is too large.');
        pieces.push(chunk);
    }
    let result;
    try { result = JSON.parse(Buffer.concat(pieces).toString('utf8')); }
    catch { fail(400, 'The request is not valid JSON.'); }
    if (!result || typeof result !== 'object' || Array.isArray(result)) fail(400, 'Send a JSON object.');
    return result;
}

function json(res, status, value) {
    res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' });
    res.end(JSON.stringify(value));
}

export async function createApp({ dataDir, generator, builder, starter, secureCookies = false, adminEmails = [], oauth = {} } = {}) {
    if (!dataDir) throw new Error('A dataDir is required.');
    const store = await openStore(dataDir);
    const social = createOAuth(oauth, { secureCookies });
    const roomStates = new Map();
    const authAttempts = new Map();
    const running = new Set();
    const pendingJobs = [];
    let stopping = false;
    const admins = new Set(adminEmails.map(email => email.trim().toLowerCase()).filter(Boolean));
    const isAdmin = user => user?.admin === true && (!admins.size || admins.has(user.email));
    const hasPassword = user => typeof user?.salt === 'string' && user.salt.length > 0 && user.salt.length <= 256 && typeof user.passwordHash === 'string' && /^[a-f0-9]{128}$/i.test(user.passwordHash);
    const publicUser = user => user ? { id: user.id, name: user.name, email: user.email, isAdmin: isAdmin(user), hasPassword: hasPassword(user),
        authMethods: [...(hasPassword(user) ? ['password'] : []), ...Object.values(store.state.identities || {}).filter(identity => identity.userId === user.id).map(identity => identity.provider)] } : null;
    const configured = { generation: typeof generator === 'function', build: typeof builder === 'function', starter: Boolean(starter) };
    await store.change(state => {
        state.identities ||= {};
        for (const project of Object.values(state.projects)) {
            if (!project.document) project.document = documentUpdate(project.idea).encoded;
        }
        for (const job of Object.values(state.jobs)) {
            if (['queued', 'generating', 'building'].includes(job.status)) {
                job.status = 'failed';
                job.error = 'The service restarted before this job finished. Your previous version is unchanged; try again.';
                job.updatedAt = now();
                delete job.context;
            }
        }
        for (const [token, session] of Object.entries(state.sessions)) if (session.expires < Date.now()) delete state.sessions[token];
        for (const [token, ticket] of Object.entries(state.previews)) if (ticket.expires < Date.now()) delete state.previews[token];
    });

    function session(req, required = true) {
        const token = cookie(req, 'mockup_session');
        const record = token && store.state.sessions[hash(token)];
        const user = record && record.expires > Date.now() && store.state.users[record.userId];
        if (!user && required) fail(401, 'Sign in to continue.');
        return user || null;
    }

    function sessionHash(req) {
        const token = cookie(req, 'mockup_session');
        return token ? hash(token) : null;
    }

    function requireLinkSession(req, link, state = store.state) {
        const key = sessionHash(req);
        const current = key && state.sessions[key];
        if (!link || key !== link.sessionHash || !current || current.userId !== link.userId || current.expires <= Date.now() || !state.users[link.userId]) throw authError('link-expired');
        return state.users[link.userId];
    }

    async function socialUser(req, identity, link) {
        return store.change(state => {
            const identityKey = hash(`${identity.provider}\0${identity.subject}`);
            const existing = state.identities[identityKey];
            let user;
            if (link) {
                user = requireLinkSession(req, link, state);
                if ((existing && existing.userId !== user.id) || Object.values(state.identities).some(item => item.userId === user.id && item.provider === identity.provider && item.subject !== identity.subject)) throw authError('identity-linked');
            } else if (existing) {
                user = state.users[existing.userId];
                if (!user) throw authError('provider');
                // A changed provider email never changes who owns an identity.
                if (!hasPassword(user) && user.admin !== true && !admins.has(identity.email) && !Object.values(state.users).some(item => item.id !== user.id && item.email === identity.email)) user.email = identity.email;
            } else {
                if (admins.has(identity.email)) throw authError('reserved-email');
                if (Object.values(state.users).some(item => item.email === identity.email)) throw authError('account-exists');
                user = { id: id(), name: identity.name, email: identity.email, emailVerified: true, createdAt: now() };
                state.users[user.id] = user;
            }
            state.identities[identityKey] = { provider: identity.provider, subject: identity.subject, userId: user.id, email: identity.email,
                createdAt: existing?.createdAt || now(), updatedAt: now() };
            return user;
        });
    }

    function redirect(res, destination) {
        res.writeHead(302, { Location: destination, 'Cache-Control': 'no-store', 'Referrer-Policy': 'no-referrer' });
        res.end();
    }

    async function socialCallback(req, res, provider, url) {
        let transaction;
        try {
            transaction = social.consume({ req, res, provider, url });
            if (transaction.link) requireLinkSession(req, transaction.link);
            if (url.searchParams.has('error')) throw authError('cancelled');
            if (url.searchParams.getAll('code').length !== 1) throw authError('provider');
            const identity = await social.identity(provider, url.searchParams.get('code'), transaction);
            const user = await socialUser(req, identity, transaction.link);
            if (!transaction.link) await signIn(res, user);
            return redirect(res, transaction.link ? `${transaction.returnTo}?authLinked=${provider}` : transaction.returnTo);
        } catch (error) {
            return redirect(res, `${transaction?.returnTo || '/'}?authError=${error.authCode || 'provider'}`);
        }
    }

    function membership(user, projectId) {
        const project = store.state.projects[projectId];
        if (!project || !project.memberIds.includes(user.id)) fail(404, 'Project not found.');
        return project;
    }

    function room(projectId) {
        if (!roomStates.has(projectId)) roomStates.set(projectId, {
            hostClientId: null, hostUserId: null, controllerClientId: null, controllerUserId: null,
            revisionId: null, epoch: 0, clients: new Map(),
        });
        return roomStates.get(projectId);
    }

    function publicRoom(projectId) {
        const state = room(projectId);
        const { hostClientId, hostUserId, controllerClientId, controllerUserId, revisionId, epoch } = state;
        return { hostClientId, hostUserId, controllerClientId, controllerUserId, revisionId, epoch,
            presence: [...state.clients.values()].map(client => ({ clientId: client.id, userId: client.userId, name: client.name, cursor: client.cursor || null, activity: client.activity || null })),
        };
    }

    function revisionView(revision) {
        const { htmlPath, ...visible } = revision;
        return { ...visible, previewUrl: `/preview/${revision.id}/` };
    }

    function assetView(asset) {
        const { filePath, ...visible } = asset;
        return { ...visible, url: `/api/projects/${asset.projectId}/assets/${asset.id}` };
    }

    function jobView(job) {
        const { context, ...visible } = job;
        return visible;
    }

    function projectView(project) {
        const { memberIds, document, ...visible } = project;
        return visible;
    }

    function usageSummary(jobs, projectIds = []) {
        const usage = jobs.flatMap(job => job.usage || []);
        const resources = jobs.flatMap(job => job.resources || []);
        const add = field => usage.reduce((total, item) => total + (item[field] || 0), 0);
        const unknown = usage.filter(item => item.estimatedUsd === null || item.estimatedUsd === undefined).length;
        const projects = new Set(projectIds);
        const assets = Object.values(store.state.assets).filter(asset => projects.has(asset.projectId));
        const revisions = Object.values(store.state.revisions).filter(revision => projects.has(revision.projectId));
        return {
            tokens: { input: add('inputTokens'), cachedInput: add('cachedInputTokens'), output: add('outputTokens'), total: add('totalTokens') },
            estimatedCostUsd: unknown ? null : usage.reduce((total, item) => total + (item.estimatedUsd || 0), 0),
            knownCostUsd: usage.reduce((total, item) => total + (item.estimatedUsd || 0), 0),
            pricedRequests: usage.length - unknown, unpricedRequests: unknown,
            jobs: { running: jobs.filter(job => ['queued', 'generating', 'building'].includes(job.status)).length, failed: jobs.filter(job => job.status === 'failed').length, total: jobs.length },
            buildSeconds: resources.filter(item => item.kind === 'build').reduce((total, item) => total + item.durationMs, 0) / 1000,
            modelSeconds: add('durationMs') / 1000,
            assetBytes: assets.reduce((total, item) => total + item.size, 0),
            artifactBytes: revisions.reduce((total, item) => total + (item.artifactBytes || 0), 0),
            sourceBytes: revisions.reduce((total, item) => total + item.sourceFiles.reduce((size, file) => size + Buffer.byteLength(file.content), 0), 0),
        };
    }

    async function adminView() {
        const jobs = Object.values(store.state.jobs);
        const projects = Object.values(store.state.projects);
        const memory = process.memoryUsage();
        const measurements = new Map();
        const measure = path => {
            const absolute = resolve(path);
            const fromRoot = relative(store.root, absolute);
            if (fromRoot === '..' || fromRoot.startsWith(`..${process.platform === 'win32' ? '\\' : '/'}`) || isAbsolute(fromRoot)) throw new Error('A project resource is outside the data directory.');
            if (!measurements.has(absolute)) measurements.set(absolute, fileBytes(absolute));
            return measurements.get(absolute);
        };
        const storage = async (projectIds, projectJobs) => {
            const ids = new Set(projectIds);
            const assets = Object.values(store.state.assets).filter(asset => ids.has(asset.projectId));
            const revisions = Object.values(store.state.revisions).filter(revision => ids.has(revision.projectId));
            const feedback = Object.values(store.state.feedback).filter(report => ids.has(report.projectId));
            const sumFiles = async paths => (await Promise.all([...new Set(paths)].map(measure))).reduce((total, bytes) => total + bytes, 0);
            const [assetBytes, artifactBytes, workBytes] = await Promise.all([
                sumFiles(assets.map(asset => asset.filePath)), sumFiles(revisions.map(revision => revision.htmlPath)),
                sumFiles(projectJobs.map(job => join(store.root, 'work', job.id))),
            ]);
            const sourceBytes = revisions.reduce((total, revision) => total + Buffer.byteLength(JSON.stringify(revision.sourceFiles)), 0);
            const feedbackBytes = feedback.reduce((total, report) => total + (report.capture ? Buffer.byteLength(JSON.stringify(report.capture)) : 0), 0);
            return { assetBytes, artifactBytes, workBytes, sourceBytes, feedbackBytes, totalBytes: assetBytes + artifactBytes + workBytes + sourceBytes + feedbackBytes,
                storageExclusions: ['Shared account/project/job metadata, CRDT state, session records and JSON field overhead are excluded.'],
            };
        };
        const [totalStorage, projectSummaries] = await Promise.all([
            storage(projects.map(project => project.id), jobs),
            Promise.all(projects.map(async project => {
                const projectJobs = jobs.filter(job => job.projectId === project.id);
                return { ...projectView(project), members: project.memberIds.map(userId => publicUser(store.state.users[userId])),
                    summary: { ...usageSummary(projectJobs, [project.id]), ...await storage([project.id], projectJobs) } };
            })),
        ]);
        return {
            summary: { ...usageSummary(jobs, projects.map(project => project.id)), ...totalStorage },
            projects: projectSummaries,
            recentJobs: jobs.slice(-100).reverse().map(job => ({ ...jobView(job), projectTitle: store.state.projects[job.projectId]?.title, ...usageSummary([job]) })),
            service: { rssBytes: memory.rss, heapUsedBytes: memory.heapUsed, uptimeSeconds: process.uptime(), activeJobs: running.size, queuedJobs: pendingJobs.length, userCount: Object.keys(store.state.users).length, projectCount: projects.length },
        };
    }

    async function fileBytes(path, depth = 0) {
        if (depth > 64) throw new Error('A project resource tree exceeds the supported depth.');
        let info;
        try { info = await lstat(path); }
        catch (error) { if (error.code === 'ENOENT') return 0; throw error; }
        if (info.isSymbolicLink()) return 0;
        if (info.isFile()) return info.size;
        if (!info.isDirectory()) return 0;
        const entries = await readdir(path, { withFileTypes: true });
        return (await Promise.all(entries.filter(entry => !entry.isSymbolicLink()).map(entry => fileBytes(join(path, entry.name), depth + 1)))).reduce((total, size) => total + size, 0);
    }

    async function recordUsage(jobId, value) {
        const usage = { id: id(), recordedAt: now(), responseId: typeof value.responseId === 'string' ? value.responseId.slice(0, 200) : null,
            model: typeof value.model === 'string' ? value.model.slice(0, 100) : null, attempt: value.attempt ?? null,
            estimatedUsd: value.estimatedUsd ?? null, rates: value.rates || null,
        };
        for (const key of ['inputTokens', 'cachedInputTokens', 'outputTokens', 'totalTokens', 'durationMs']) {
            if (value[key] !== undefined && (typeof value[key] !== 'number' || !Number.isFinite(value[key]) || value[key] < 0)) throw new Error(`Invalid usage ${key}.`);
            usage[key] = value[key] || 0;
        }
        if (usage.estimatedUsd !== null && (typeof usage.estimatedUsd !== 'number' || !Number.isFinite(usage.estimatedUsd) || usage.estimatedUsd < 0)) throw new Error('Invalid estimated usage cost.');
        await store.change(state => {
            const job = state.jobs[jobId];
            if (!job) throw new Error('Usage must belong to an existing job.');
            job.usage ||= [];
            if (usage.responseId && job.usage.some(item => item.responseId === usage.responseId)) return;
            job.usage.push(usage);
        });
        const job = store.state.jobs[jobId];
        broadcast(job.projectId, 'job', jobView(job));
    }

    async function recordResource(jobId, value) {
        if (value.kind !== 'build' || typeof value.durationMs !== 'number' || !Number.isFinite(value.durationMs) || value.durationMs < 0) throw new Error('A measured build duration is required.');
        const resource = { id: id(), kind: 'build', durationMs: value.durationMs, attempt: value.attempt ?? null,
            status: typeof value.status === 'string' ? value.status.slice(0, 40) : value.failed ? 'failed' : 'succeeded', recordedAt: now() };
        await store.change(state => {
            const job = state.jobs[jobId];
            if (!job) throw new Error('Resources must belong to an existing job.');
            (job.resources ||= []).push(resource);
        });
        const job = store.state.jobs[jobId];
        broadcast(job.projectId, 'job', jobView(job));
    }

    function detail(projectId) {
        const project = store.state.projects[projectId];
        const belonging = records => Object.values(records).filter(item => item.projectId === projectId);
        return {
            project: projectView(project),
            members: project.memberIds.map(userId => ({ ...publicUser(store.state.users[userId]), role: userId === project.ownerId ? 'owner' : 'member' })),
            messages: belonging(store.state.messages), revisions: belonging(store.state.revisions).map(revisionView),
            feedback: belonging(store.state.feedback), assets: belonging(store.state.assets).map(assetView),
            room: publicRoom(projectId), jobs: belonging(store.state.jobs).map(jobView),
        };
    }

    function send(client, event, value) {
        if (!client.res.writableEnded && !client.res.destroyed) {
            // A feedback screenshot can exceed Node's stream high-water mark.
            // Keep healthy readers connected, but bound a stalled browser's queue.
            if (client.res.writableLength > 8 * 1024 * 1024) return client.res.destroy();
            client.res.write(`event: ${event}\ndata: ${JSON.stringify(value)}\n\n`);
        }
    }

    function broadcast(projectId, event, value) {
        for (const client of room(projectId).clients.values()) send(client, event, value);
    }

    function changed(projectId) { broadcast(projectId, 'project', detail(projectId)); }

    function clearInput(projectId) {
        const state = room(projectId);
        broadcast(projectId, 'input', { clientId: null, userId: null, epoch: state.epoch, event: { type: 'clear' } });
    }

    function connected(projectId, user, clientId) {
        const client = room(projectId).clients.get(clientId);
        if (!client || client.userId !== user.id) fail(409, 'Connect to the playtest room before taking control.');
        return client;
    }

    function clientRate(client, label, maximum) {
        client.rates ||= {};
        const current = client.rates[label];
        const time = Date.now();
        if (!current || current.until < time) client.rates[label] = { count: 1, until: time + 1000 };
        else if (++current.count > maximum) fail(429, 'Updates are arriving too quickly.');
    }

    function csrf(req) {
        const origin = req.headers.origin;
        if (origin) {
            let expected = `${secureCookies || req.socket.encrypted ? 'https' : 'http'}://${req.headers.host}`;
            if (oauth.publicOrigin) expected = social.origin();
            if (origin !== expected) fail(403, 'Cross-origin requests are not allowed.');
        } else if (req.headers['sec-fetch-site'] === 'cross-site') fail(403, 'Cross-site requests are not allowed.');
    }

    function authRate(req) {
        const key = req.socket.remoteAddress;
        const time = Date.now();
        const record = authAttempts.get(key);
        if (!record || record.until < time) authAttempts.set(key, { until: time + 60_000, count: 1 });
        else if (++record.count > 20) fail(429, 'Too many sign-in attempts. Try again in a minute.');
        if (authAttempts.size > 10000) for (const [address, attempt] of authAttempts) if (attempt.until < time) authAttempts.delete(address);
    }

    async function signIn(res, user) {
        const token = randomBytes(32).toString('base64url');
        await store.change(state => {
            state.sessions[hash(token)] = { userId: user.id, expires: Date.now() + SESSION_AGE };
        });
        sessionCookie(res, token, SESSION_AGE / 1000);
    }

    function sessionCookie(res, token, seconds) {
        appendCookie(res, `mockup_session=${token}; Path=/; HttpOnly; SameSite=Lax; Max-Age=${seconds}${social.secure() ? '; Secure' : ''}`);
    }

    async function artifact(revisionId, htmlPath) {
        if (typeof htmlPath !== 'string') throw new Error('The builder did not return a playable HTML artifact.');
        const bytes = await readFile(htmlPath);
        if (!bytes.length || bytes.length > 64 * 1024 * 1024) throw new Error('The HTML artifact is empty or exceeds 64 MB.');
        const directory = join(store.root, 'artifacts', revisionId);
        await mkdir(directory, { recursive: true });
        const output = join(directory, 'index.html');
        await writeFile(output, bytes, { flag: 'wx', mode: 0o600 });
        return { htmlPath: output, artifactBytes: bytes.length };
    }

    async function previewTicket(req, revisionId) {
        const user = session(req);
        const revision = store.state.revisions[revisionId];
        if (!revision) fail(404, 'Version not found.');
        membership(user, revision.projectId);
        const token = randomBytes(32).toString('base64url');
        await store.change(state => {
            for (const [key, ticket] of Object.entries(state.previews)) if (ticket.expires < Date.now()) delete state.previews[key];
            state.previews[hash(token)] = { revisionId, userId: user.id, expires: Date.now() + PREVIEW_AGE };
        });
        return { token, revisionId };
    }

    async function preview(revisionId, token) {
        if (typeof token !== 'string' || token.length > 128) return null;
        const ticket = store.state.previews[hash(token)];
        const revision = store.state.revisions[revisionId];
        if (!ticket || ticket.revisionId !== revisionId || ticket.expires < Date.now() || !revision) return null;
        const project = store.state.projects[revision.projectId];
        if (!project?.memberIds.includes(ticket.userId)) return null;
        return { htmlPath: revision.htmlPath, projectId: revision.projectId, revisionId };
    }

    async function runJob(jobId) {
        const job = store.state.jobs[jobId];
        const projectId = job.projectId;
        try {
            await store.change(state => { state.jobs[jobId].status = 'generating'; state.jobs[jobId].updatedAt = now(); });
            broadcast(projectId, 'job', jobView(store.state.jobs[jobId]));
            const context = structuredClone(job.context);
            const generated = await generator(context);
            const generatedFiles = sources(generated?.files);
            const revisionId = id();
            await store.change(state => { state.jobs[jobId].status = 'building'; state.jobs[jobId].updatedAt = now(); });
            broadcast(projectId, 'job', jobView(store.state.jobs[jobId]));
            const built = await builder({ ...generated, files: generatedFiles }, {
                project: context.project, jobId, revisionId, assets: context.assets, baseRevision: context.baseRevision,
            });
            const sourceFiles = sources(built.sourceFiles || generatedFiles);
            const builtArtifact = await artifact(revisionId, built.htmlPath);
            const summary = typeof built.summary === 'string' ? built.summary.slice(0, 12000) : typeof generated.summary === 'string' ? generated.summary.slice(0, 12000) : 'Playable prototype built.';
            await store.change(state => {
                const current = state.projects[projectId];
                const active = current.activeRevisionId === job.baseRevisionId;
                const previous = current.activeRevisionId && state.revisions[current.activeRevisionId];
                if (active && previous) previous.status = 'archived';
                state.revisions[revisionId] = {
                    id: revisionId, projectId, number: Object.values(state.revisions).filter(item => item.projectId === projectId).length + 1,
                    baseRevisionId: job.baseRevisionId, status: active ? 'active' : 'candidate', origin: 'generated', polish: job.polish,
                    summary, sourceFiles, ...builtArtifact, toolchain: built.toolchain || null, createdAt: now(), createdBy: job.userId,
                };
                if (active) { current.activeRevisionId = revisionId; current.polish = job.polish; }
                current.updatedAt = now();
                const messageId = id();
                state.messages[messageId] = { id: messageId, projectId, userId: null, name: 'Mockup', role: 'assistant',
                    text: active ? summary : `${summary}\n\nThis is a candidate because the shared version changed while it was being built.`, revisionId, createdAt: now() };
                Object.assign(state.jobs[jobId], { status: 'succeeded', revisionId, updatedAt: now() });
                delete state.jobs[jobId].context;
            });
            broadcast(projectId, 'job', jobView(store.state.jobs[jobId]));
            changed(projectId);
        } catch (error) {
            await store.change(state => {
                const job = state.jobs[jobId];
                job.status = 'failed';
                job.error = String(error.message || 'The prototype could not be built.').slice(0, 8000);
                job.updatedAt = now();
                delete job.context;
            });
            broadcast(projectId, 'job', jobView(store.state.jobs[jobId]));
            changed(projectId);
        }
    }

    function pump() {
        while (!stopping && running.size < 4 && pendingJobs.length) {
            const jobId = pendingJobs.shift();
            const work = runJob(jobId);
            running.add(work);
            work.finally(() => { running.delete(work); pump(); }).catch(() => {});
        }
    }

    async function route(req, res, url) {
        const path = url.pathname;
        const method = req.method;
        if (method !== 'GET' && method !== 'HEAD') csrf(req);
        if (method === 'GET' && path === '/api/session') return json(res, 200, { user: publicUser(session(req, false)), configured, authProviders: social.providers });
        if (method === 'GET' && path === '/api/auth/providers') return json(res, 200, { providers: social.providers });
        const socialRoute = path.match(/^\/api\/auth\/(google|discord)\/(start|callback|link)$/);
        if (socialRoute && method === 'GET' && socialRoute[2] === 'callback') return socialCallback(req, res, socialRoute[1], url);
        if (socialRoute && method === 'GET' && socialRoute[2] === 'start') {
            if (!social.configured(socialRoute[1])) fail(503, 'This sign-in provider is not configured.');
            authRate(req);
            try { return redirect(res, social.begin({ req, res, provider: socialRoute[1], returnTo: url.searchParams.get('returnTo') || '/' })); }
            catch (error) { fail(error.authCode === 'busy' ? 429 : 400, error.authCode === 'destination' ? 'Invalid sign-in destination.' : 'This sign-in provider is not configured correctly.'); }
        }
        if (method === 'POST' && (path === '/api/signup' || path === '/api/login')) {
            authRate(req);
            const value = await body(req, 8192);
            const email = text(value.email, 'Email', 254).toLowerCase();
            if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) fail(400, 'Enter a valid email address.');
            if (typeof value.password !== 'string' || value.password.length < 10 || value.password.length > 256) fail(400, 'Password must contain 10–256 characters.');
            const password = value.password;
            let user = Object.values(store.state.users).find(item => item.email === email);
            if (path === '/api/signup') {
                if (admins.has(email)) fail(403, 'This address is reserved for an owner-provisioned account.');
                const name = text(value.name || email.split('@')[0], 'Name', 80);
                const salt = randomBytes(16).toString('hex');
                const key = (await scrypt(password, salt, 64)).toString('hex');
                user = { id: id(), name, email, salt, passwordHash: key, createdAt: now() };
                await store.change(state => {
                    if (Object.values(state.users).some(item => item.email === email)) fail(409, 'An account already uses that email.');
                    state.users[user.id] = user;
                });
            } else {
                const key = await scrypt(password, hasPassword(user) ? user.salt : 'missing-account', 64);
                if (!hasPassword(user) || !timingSafeEqual(key, Buffer.from(user.passwordHash, 'hex'))) fail(401, 'Email or password is incorrect.');
            }
            await signIn(res, user);
            return json(res, path === '/api/signup' ? 201 : 200, { user: publicUser(user) });
        }
        const user = session(req);
        if (socialRoute && method === 'POST' && socialRoute[2] === 'link') {
            if (!social.configured(socialRoute[1])) fail(503, 'This sign-in provider is not configured.');
            authRate(req);
            const value = await body(req, 8192);
            try {
                const key = sessionHash(req);
                const target = social.begin({ req, res, provider: socialRoute[1], returnTo: value.returnTo || '/', link: { userId: user.id, sessionHash: key } });
                // Refresh the same token so pre-OAuth Strict cookies also survive the callback.
                sessionCookie(res, cookie(req, 'mockup_session'), Math.max(0, Math.ceil((store.state.sessions[key].expires - Date.now()) / 1000)));
                return json(res, 200, { url: target });
            }
            catch (error) { fail(error.authCode === 'busy' ? 429 : 400, error.authCode === 'destination' ? 'Invalid sign-in destination.' : 'This sign-in provider is not configured correctly.'); }
        }
        if (method === 'GET' && path.startsWith('/api/admin')) {
            if (!isAdmin(user)) fail(403, 'Only the service owner can view administration.');
            if (path === '/api/admin') return json(res, 200, await adminView());
            const project = path.match(/^\/api\/admin\/projects\/([a-zA-Z0-9-]+)$/);
            if (!project || !store.state.projects[project[1]]) fail(404, 'Project not found.');
            return json(res, 200, detail(project[1]));
        }
        if (method === 'POST' && path === '/api/logout') {
            const token = cookie(req, 'mockup_session');
            await store.change(state => { if (token) delete state.sessions[hash(token)]; });
            for (const state of roomStates.values()) for (const client of state.clients.values()) if (client.userId === user.id) client.res.end();
            appendCookie(res, `mockup_session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0${social.secure() ? '; Secure' : ''}`);
            return json(res, 200, { ok: true });
        }
        if (path === '/api/projects' && method === 'GET') {
            const projects = Object.values(store.state.projects).filter(project => project.memberIds.includes(user.id)).map(projectView);
            return json(res, 200, { projects });
        }
        if (path === '/api/projects' && method === 'POST') {
            const value = await body(req, 16384);
            const project = { id: id(), title: text(value.title, 'Title', 100), idea: text(value.idea, 'Idea', 12000), polish: polish(value.polish), ownerId: user.id, memberIds: [user.id], activeRevisionId: null, createdAt: now(), updatedAt: now() };
            project.document = documentUpdate(project.idea).encoded;
            let initial;
            if (starter) {
                const revisionId = id();
                initial = { id: revisionId, projectId: project.id, number: 1, baseRevisionId: null, status: 'active', origin: 'starter', polish: project.polish, summary: 'Starter playground',
                    sourceFiles: sources(starter.files), ...await artifact(revisionId, starter.htmlPath), toolchain: starter.toolchain || null, createdAt: now(), createdBy: user.id };
                project.activeRevisionId = revisionId;
            }
            await store.change(state => { state.projects[project.id] = project; if (initial) state.revisions[initial.id] = initial; });
            return json(res, 201, { project: projectView(project) });
        }
        const inviteMatch = path.match(/^\/api\/invites\/([a-zA-Z0-9_-]{32,128})\/join$/);
        if (method === 'POST' && inviteMatch) {
            let projectId;
            await store.change(state => {
                const invite = state.invites[hash(inviteMatch[1])];
                if (!invite || invite.expires < Date.now()) fail(404, 'This invitation has expired or does not exist.');
                projectId = invite.projectId;
                const project = state.projects[projectId];
                if (!project.memberIds.includes(user.id)) project.memberIds.push(user.id);
                project.updatedAt = now();
            });
            changed(projectId);
            return json(res, 200, { project: detail(projectId).project });
        }
        const jobMatch = path.match(/^\/api\/jobs\/([a-zA-Z0-9-]+)$/);
        if (method === 'GET' && jobMatch) {
            const job = store.state.jobs[jobMatch[1]];
            if (!job) fail(404, 'Job not found.');
            membership(user, job.projectId);
            return json(res, 200, { job: jobView(job) });
        }
        const projectMatch = path.match(/^\/api\/projects\/([a-zA-Z0-9-]+)(?:\/(.*))?$/);
        if (!projectMatch) fail(404, 'API endpoint not found.');
        const projectId = projectMatch[1];
        const action = projectMatch[2] || '';
        const project = membership(user, projectId);
        if (method === 'GET' && !action) return json(res, 200, detail(projectId));
        if (method === 'GET' && action === 'document') return json(res, 200, { update: project.document });
        if (method === 'GET' && action === 'events') {
            csrf(req);
            const clientId = text(url.searchParams.get('clientId'), 'Client id', 100);
            if (!/^[a-zA-Z0-9_-]+$/.test(clientId)) fail(400, 'Invalid client id.');
            const state = room(projectId);
            const previous = state.clients.get(clientId);
            if (previous && previous.userId !== user.id) fail(409, 'This client id is already connected.');
            if (state.clients.size >= 50 && !previous) fail(429, 'The playtest room is full.');
            res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache, no-transform', 'Connection': 'keep-alive', 'X-Accel-Buffering': 'no' });
            res.flushHeaders();
            const client = { id: clientId, userId: user.id, name: user.name, res };
            state.clients.set(clientId, client);
            previous?.res.end();
            send(client, 'snapshot', detail(projectId));
            broadcast(projectId, 'room', publicRoom(projectId));
            const heartbeat = setInterval(() => {
                if (!session(req, false)) return res.end();
                if (res.writableLength > 8 * 1024 * 1024) res.destroy();
                else res.write(': heartbeat\n\n');
            }, 15000);
            heartbeat.unref();
            res.on('close', () => {
                clearInterval(heartbeat);
                if (state.clients.get(clientId) !== client) return;
                state.clients.delete(clientId);
                if (state.controllerClientId === clientId) {
                    state.controllerClientId = state.controllerUserId = null;
                    ++state.epoch;
                    clearInput(projectId);
                }
                if (state.hostClientId === clientId) {
                    state.hostClientId = state.hostUserId = state.revisionId = null;
                    ++state.epoch;
                }
                broadcast(projectId, 'room', publicRoom(projectId));
            });
            return;
        }
        const assetMatch = action.match(/^assets\/([a-zA-Z0-9-]+)$/);
        if (method === 'GET' && assetMatch) {
            const asset = store.state.assets[assetMatch[1]];
            if (!asset || asset.projectId !== projectId) fail(404, 'Asset not found.');
            const bytes = await readFile(asset.filePath);
            res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': bytes.length, 'Content-Disposition': `attachment; filename*=UTF-8''${encodeURIComponent(asset.name)}`, 'Cache-Control': 'private, max-age=31536000, immutable', 'X-Content-Type-Options': 'nosniff' });
            return res.end(bytes);
        }
        const tokenMatch = action.match(/^revisions\/([a-zA-Z0-9-]+)\/preview-token$/);
        if (method === 'GET' && tokenMatch) {
            const revision = store.state.revisions[tokenMatch[1]];
            if (!revision || revision.projectId !== projectId) fail(404, 'Version not found.');
            return json(res, 200, await previewTicket(req, revision.id));
        }
        if (method !== 'POST') fail(404, 'API endpoint not found.');
        if (action === 'invites') {
            const token = randomBytes(32).toString('base64url');
            await store.change(state => { state.invites[hash(token)] = { projectId, createdBy: user.id, expires: Date.now() + 7 * 24 * 60 * 60 * 1000 }; });
            return json(res, 201, { token, url: `/invite/${token}` });
        }
        const value = await body(req);
        if (action === 'document') {
            const client = connected(projectId, user, value.clientId);
            clientRate(client, 'document', 30);
            if (typeof value.update !== 'string' || value.update.length > 90000 || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value.update)) fail(400, 'Invalid shared document update.');
            const update = Buffer.from(value.update, 'base64');
            if (!update.length || update.length > 65536) fail(400, 'Shared document update exceeds 64 KB.');
            let merged;
            await store.change(state => {
                const current = state.projects[projectId];
                merged = documentUpdate(current.idea, current.document, update);
                current.idea = merged.idea;
                current.document = merged.encoded;
                current.updatedAt = now();
            });
            broadcast(projectId, 'document', { clientId: value.clientId, update: value.update });
            return json(res, 200, { ok: true, idea: merged.idea });
        }
        if (action === 'presence') {
            const client = connected(projectId, user, value.clientId);
            clientRate(client, 'presence', 30);
            let cursor = client.cursor || null;
            if (Object.hasOwn(value, 'cursor') && value.cursor === null) cursor = null;
            else if (Object.hasOwn(value, 'cursor')) {
                if (!value.cursor || typeof value.cursor !== 'object' || Array.isArray(value.cursor)) fail(400, 'Invalid collaboration cursor.');
                const { x, y, surface } = value.cursor;
                if (![x, y].every(number => typeof number === 'number' && Number.isFinite(number) && number >= 0 && number <= 1) || !['game', 'brief'].includes(surface)) fail(400, 'Invalid collaboration cursor.');
                cursor = { x, y, surface };
            }
            const activity = !Object.hasOwn(value, 'activity') ? client.activity || null : value.activity === null ? null : text(value.activity, 'Activity', 80);
            client.cursor = cursor;
            client.activity = activity;
            broadcast(projectId, 'presence', { clientId: client.id, userId: client.userId, name: client.name, cursor, activity });
            return json(res, 200, { ok: true });
        }
        if (action === 'messages') {
            const message = { id: id(), projectId, userId: user.id, name: user.name, role: 'user', text: text(value.text, 'Message'), createdAt: now() };
            await store.change(state => { state.messages[message.id] = message; });
            changed(projectId);
            return json(res, 201, { message });
        }
        if (action === 'feedback') {
            const revision = store.state.revisions[value.revisionId];
            if (!revision || revision.projectId !== projectId) fail(400, 'Select a version for this feedback.');
            if (typeof value.x !== 'number' || typeof value.y !== 'number' || !Number.isFinite(value.x) || !Number.isFinite(value.y) || value.x < 0 || value.x > 1 || value.y < 0 || value.y > 1) fail(400, 'Feedback coordinates must be between 0 and 1.');
            const feedback = { id: id(), projectId, userId: user.id, name: user.name, text: text(value.text, 'Feedback', 4000), x: value.x, y: value.y,
                revisionId: revision.id, capture: capture(value.capture), createdAt: now() };
            await store.change(state => { state.feedback[feedback.id] = feedback; });
            changed(projectId);
            return json(res, 201, { feedback });
        }
        if (action === 'assets') {
            const name = text(value.name, 'Asset name', 180);
            if (/[\/\\\x00-\x1f]/.test(name) || name === '.' || name === '..') fail(400, 'Use a filename without directory separators.');
            if (value.usage !== 'game' && value.usage !== 'reference') fail(400, 'Choose game or reference for this asset.');
            const type = typeof value.type === 'string' && /^[a-zA-Z0-9.+-]+\/[a-zA-Z0-9.+-]+$/.test(value.type) ? value.type : 'application/octet-stream';
            if (typeof value.data !== 'string' || value.data.length > Math.ceil(ASSET_LIMIT / 3) * 4 || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value.data)) fail(400, 'Upload a base64 file no larger than 10 MB.');
            const bytes = Buffer.from(value.data, 'base64');
            if (!bytes.length || bytes.length > ASSET_LIMIT) fail(400, 'Upload a file between 1 byte and 10 MB.');
            const sha256 = hash(bytes);
            const directory = join(store.root, 'assets');
            await mkdir(directory, { recursive: true });
            const filePath = join(directory, sha256);
            await writeFile(filePath, bytes, { flag: 'wx', mode: 0o600 }).catch(error => { if (error.code !== 'EEXIST') throw error; });
            const asset = { id: id(), projectId, userId: user.id, name, type, usage: value.usage, size: bytes.length, sha256, filePath, createdAt: now() };
            await store.change(state => { state.assets[asset.id] = asset; });
            changed(projectId);
            return json(res, 201, { asset: assetView(asset) });
        }
        if (action === 'control') {
            connected(projectId, user, value.clientId);
            const state = room(projectId);
            state.controllerClientId = value.clientId;
            state.controllerUserId = user.id;
            ++state.epoch;
            clearInput(projectId);
            broadcast(projectId, 'room', publicRoom(projectId));
            return json(res, 200, { room: publicRoom(projectId) });
        }
        if (action === 'host') {
            connected(projectId, user, value.clientId);
            const revision = store.state.revisions[value.revisionId];
            if (!revision || revision.projectId !== projectId) fail(400, 'Select a version to host.');
            const state = room(projectId);
            if (state.hostClientId && state.hostClientId !== value.clientId) return json(res, 200, { room: publicRoom(projectId) });
            clearInput(projectId);
            state.hostClientId = value.clientId;
            state.hostUserId = user.id;
            state.revisionId = revision.id;
            if (!state.controllerClientId) {
                state.controllerClientId = value.clientId;
                state.controllerUserId = user.id;
            }
            ++state.epoch;
            clearInput(projectId);
            broadcast(projectId, 'room', publicRoom(projectId));
            return json(res, 200, { room: publicRoom(projectId) });
        }
        if (action === 'input') {
            const client = connected(projectId, user, value.clientId);
            clientRate(client, 'input', 120);
            const state = room(projectId);
            if (state.controllerClientId !== value.clientId || state.controllerUserId !== user.id || value.epoch !== state.epoch) fail(409, 'Control has changed. Refresh the room state.');
            const host = state.clients.get(state.hostClientId);
            if (!host) fail(409, 'No browser is hosting this playtest.');
            const event = inputEvent(value.event);
            broadcast(projectId, 'input', { clientId: value.clientId, userId: user.id, epoch: state.epoch, event });
            return json(res, 200, { ok: true });
        }
        if (action === 'signal') {
            const client = connected(projectId, user, value.from);
            clientRate(client, 'signal', 60);
            const state = room(projectId);
            const recipient = state.clients.get(value.to);
            if (!recipient || value.from === value.to) fail(404, 'The other browser is no longer in the room.');
            if (value.from !== state.hostClientId && value.to !== state.hostClientId) fail(403, 'Playtest signals must involve the hosting browser.');
            if (!value.signal || typeof value.signal !== 'object' || JSON.stringify(value.signal).length > 100000) fail(400, 'Invalid playtest signal.');
            send(recipient, 'signal', { from: value.from, to: value.to, signal: value.signal });
            return json(res, 200, { ok: true });
        }
        if (action === 'jobs') {
            if (!configured.generation || !configured.build) fail(503, 'Game generation is not configured. Set the generator and Tide builder before requesting an iteration.');
            const prompt = text(value.prompt, 'Prompt');
            const requestedPolish = polish(value.polish || project.polish);
            const baseRevisionId = value.baseRevisionId === undefined ? project.activeRevisionId : value.baseRevisionId;
            const baseRevision = baseRevisionId && store.state.revisions[baseRevisionId];
            if (baseRevisionId !== null && (!baseRevision || baseRevision.projectId !== projectId)) fail(400, 'The base version does not belong to this project.');
            const job = { id: id(), projectId, userId: user.id, prompt, polish: requestedPolish, baseRevisionId, status: 'queued', createdAt: now(), updatedAt: now() };
            await store.change(state => {
                if (Object.values(state.jobs).filter(item => item.projectId === projectId && ['queued', 'generating', 'building'].includes(item.status)).length >= 4) fail(429, 'This project already has four iterations in progress.');
                if (Object.values(state.jobs).filter(item => ['queued', 'generating', 'building'].includes(item.status)).length >= 100) fail(429, 'The build queue is full. Try again shortly.');
                const last = Object.values(state.messages).filter(item => item.projectId === projectId).at(-1);
                if (last?.userId !== user.id || last?.text !== prompt) {
                    const messageId = id();
                    state.messages[messageId] = { id: messageId, projectId, userId: user.id, name: user.name, role: 'user', text: prompt, createdAt: now() };
                }
                job.context = {
                    project: structuredClone(state.projects[projectId]), messages: Object.values(state.messages).filter(item => item.projectId === projectId),
                    assets: Object.values(state.assets).filter(item => item.projectId === projectId), feedback: Object.values(state.feedback).filter(item => item.projectId === projectId),
                    revisions: Object.values(state.revisions).filter(item => item.projectId === projectId).map(revisionView),
                    prompt, polish: requestedPolish, baseRevision: baseRevision ? structuredClone(baseRevision) : null, jobId: job.id,
                };
                state.jobs[job.id] = job;
            });
            pendingJobs.push(job.id);
            changed(projectId);
            pump();
            return json(res, 202, { job: jobView(job) });
        }
        const activateMatch = action.match(/^revisions\/([a-zA-Z0-9-]+)\/activate$/);
        if (activateMatch) {
            const revisionId = activateMatch[1];
            await store.change(state => {
                const revision = state.revisions[revisionId];
                const current = state.projects[projectId];
                if (!revision || revision.projectId !== projectId) fail(404, 'Version not found.');
                if (value.expectedRevisionId !== current.activeRevisionId) fail(409, 'The shared version changed. Review it before switching versions.');
                if (current.activeRevisionId && current.activeRevisionId !== revisionId) state.revisions[current.activeRevisionId].status = 'archived';
                revision.status = 'active';
                current.activeRevisionId = revisionId;
                current.polish = revision.polish || current.polish;
                current.updatedAt = now();
            });
            changed(projectId);
            return json(res, 200, { revision: revisionView(store.state.revisions[revisionId]), project: detail(projectId).project });
        }
        fail(404, 'API endpoint not found.');
    }

    async function handler(req, res) {
        let url;
        try { url = new URL(req.url, 'http://mockup.local'); }
        catch { json(res, 400, { error: 'Invalid request URL.' }); return true; }
        if (!url.pathname.startsWith('/api/')) return false;
        try { await route(req, res, url); }
        catch (error) {
            if (res.headersSent) res.end();
            else json(res, error.status || 500, { error: error.status ? error.message : 'An internal error occurred.' });
        }
        return true;
    }
    const server = createServer(async (req, res) => { if (!await handler(req, res)) json(res, 404, { error: 'Not found.' }); });
    server.requestTimeout = 30_000;
    server.headersTimeout = 15_000;
    return {
        server, handler, preview, previewTicket, recordUsage, recordResource, ready: configured,
        async listen(port = 0, host = '127.0.0.1') {
            await new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, host, () => { server.off('error', reject); resolve(); }); });
            return server.address();
        },
        async close() {
            stopping = true;
            social.close();
            for (const state of roomStates.values()) for (const client of state.clients.values()) client.res.end();
            if (server.listening) await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
            await Promise.allSettled([...running]);
            await store.flush();
        },
    };
}
