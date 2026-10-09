import { readFile, writeFile, mkdir, copyFile } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { join, resolve, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';
import { assetBuildPath, validateAssetReferences } from './assets.mjs';

const repo = fileURLToPath(new URL('../../', import.meta.url));
const schema = {
    type: 'object', additionalProperties: false,
    required: ['summary', 'files'],
    properties: {
        summary: { type: 'string' },
        files: { type: 'array', items: {
            type: 'object', additionalProperties: false,
            required: ['path', 'content'],
            properties: { path: { type: 'string' }, content: { type: 'string' } },
        } },
    },
};

export function validateFiles(files) {
    if (!Array.isArray(files) || !files.length || files.length > 24) throw new Error('Return between 1 and 24 Tide source files.');
    let size = 0;
    const paths = new Set();
    for (const file of files) {
        if (typeof file.path !== 'string' || typeof file.content !== 'string'
            || !/^[a-zA-Z0-9_-]+\.tide$/.test(file.path)
            || isAbsolute(file.path) || paths.has(file.path.toLowerCase())) {
            throw new Error('Source files must have unique, relative .tide paths.');
        }
        paths.add(file.path.toLowerCase());
        size += Buffer.byteLength(file.content);
        if (/\bextern\b|https?:\/\//i.test(file.content)) throw new Error('This local builder accepts Tide-only code and uploaded local assets.');
    }
    if (size > 512 * 1024) throw new Error('Generated source exceeds 512 KiB.');
    return files;
}

export function runTool(command, args, { cwd, timeout = 120000, env = {} } = {}) {
    return new Promise((done, fail) => {
        const child = spawn(command, args, { cwd, env: { ...process.env, ...env, TIDE_HIDDEN: '1', TIDE_NO_UPDATE_CHECK: '1' }, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
        let output = '';
        let expired = false;
        const timer = setTimeout(() => { expired = true; child.kill(); }, timeout);
        const collect = data => { output = (output + data.toString()).slice(-64000); };
        child.stdout.on('data', collect);
        child.stderr.on('data', collect);
        child.once('error', error => { clearTimeout(timer); fail(error); });
        child.once('close', code => {
            clearTimeout(timer);
            if (code !== 0 || expired) fail(new Error(expired ? 'The build exceeded its time limit.' : `Tide could not build this revision:\n${output}`));
            else done(output);
        });
    });
}

export async function createPipeline({ dataDir, tideBin = process.env.TIDE_BIN || 'tide', apiKey = process.env.OPENAI_API_KEY,
    model = process.env.OPENAI_MODEL || 'gpt-6-astra', request = fetch, run = runTool, maxAttempts = 3,
    recordUsage = async () => {}, recordResource = async () => {}, rates = null } = {}) {
    let toolchain;
    try { toolchain = (await run(tideBin, ['version'], { timeout: 10000 })).split('\n').slice(0, 2).join('\n').trim(); }
    catch { toolchain = null; }
    let reference = '';
    for (const path of ['docs/guide/first-game.md', 'docs/game/scenes.md', 'docs/game/input.md', 'docs/game/views.md', 'docs/game/systems.md', 'docs/game/entities.md', 'docs/game/assets.md', 'docs/game/commands.md']) {
        try { reference += `\n## ${path}\n${await readFile(join(repo, path), 'utf8')}`; } catch { /* optional packaged reference */ }
    }
    const contexts = new Map();
    const instructions = `You implement small, complete playable games in the Tide language for mockup.games.
Use the included official project documentation and existing working source. Tide is its own language; do not guess Unity/C# APIs.
Return a complete replacement set of .tide files, preserving working features and visual direction unless the request changes them.
When sourceOrigin is starter, the source is an API example only: create the user's requested game rather than preserving the starter's seed-collecting game loop.
The default is a small POLISHED game: coherent palette, satisfying movement/feedback, readable UI, deliberate animation, and clear objective/restart.
Quick mode prioritizes the requested mechanic with simpler presentation. Scope to one convincing loop.
Infer single-player vs multiplayer from the idea. All previews already support a social spectating/control layer; do not add players just for spectators.
Only Tide source files; no C, extern, packages, URLs, shell commands, or native interop. Asset declarations can reference supplied game asset buildPath values only.
Keep originals intact. Reference assets communicate direction; use game assets directly where suitable. Never claim an asset was used if it wasn't.
Summary is brief user-facing explanation of the actual change.
${reference}`;
    async function generate(context, repair) {
        if (!apiKey) throw new Error('Generation needs OPENAI_API_KEY on the server.');
        const assets = context.assets.map(asset => ({ id: asset.id, name: asset.name, type: asset.type, usage: asset.usage,
            buildPath: asset.usage === 'game' ? assetBuildPath(asset) : null }));
        const detail = { idea: context.project.idea, title: context.project.title, polish: context.polish,
            request: context.prompt, sourceOrigin: context.baseRevision?.origin || null, source: repair?.files || context.baseRevision?.sourceFiles || [], assets,
            recentMessages: context.messages.slice(-12).map(m => ({ text: m.text, role: m.role })),
            feedback: context.feedback.slice(-12).map(f => ({ text: f.text, revisionId: f.revisionId, x: f.x, y: f.y })),
            ...(repair ? { buildError: repair.error, instruction: 'Repair the compile failure while preserving the requested behavior.' } : {}),
        };
        const content = [{ type: 'input_text', text: JSON.stringify(detail) }];
        // Visual references and captured bugs are actual model inputs, not merely filenames.
        for (const asset of context.assets.filter(a => /^image\/(png|jpeg|webp)$/.test(a.type)).slice(0, 4)) {
            const bytes = await readFile(asset.filePath);
            content.push({ type: 'input_image', image_url: `data:${asset.type};base64,${bytes.toString('base64')}` });
        }
        const report = context.feedback.at(-1);
        if (report?.capture?.screenshot?.startsWith('data:image/')) content.push({ type: 'input_image', image_url: report.capture.screenshot });
        const started = performance.now();
        const response = await request('https://api.openai.com/v1/responses', {
            method: 'POST', headers: { Authorization: `Bearer ${apiKey}`, 'Content-Type': 'application/json' },
            signal: AbortSignal.timeout(180000),
            body: JSON.stringify({ model, store: false, instructions, input: [{ role: 'user', content }], max_output_tokens: 16000,
                text: { format: { type: 'json_schema', name: 'tide_game_revision', strict: true, schema } } }),
        });
        if (!response.ok) {
            // Do not return provider bodies, which may include request data, to teammates.
            throw new Error(`The model provider returned HTTP ${response.status}. Check the server's API key, model, and allowance.`);
        }
        const body = await response.json();
        const usage = modelUsage(body, { model, rates, durationMs: Math.round(performance.now() - started) });
        if (usage) await recordUsage(context.jobId, usage);
        if (body.status === 'incomplete' || body.error) throw new Error('The model did not finish the revision. Try a smaller change.');
        const text = body.output?.flatMap(item => item.content || []).filter(item => item.type === 'output_text').map(item => item.text).join('');
        if (!text) throw new Error('The model returned no game source.');
        const result = JSON.parse(text);
        validateFiles(result.files);
        return result;
    }
    const generator = apiKey && toolchain ? async context => {
        contexts.set(context.jobId, context);
        try { return await generate(context); } catch (error) { contexts.delete(context.jobId); throw error; }
    } : null;
    const builder = toolchain ? async (generated, context) => {
        const initial = contexts.get(context.jobId);
        let result = generated;
        try {
            for (let attempt = 0; attempt < maxAttempts; attempt++) {
                const files = validateFiles(result.files);
                const usedAssets = await validateAssetReferences(files, context.assets);
                const folder = resolve(dataDir, 'work', context.jobId, String(attempt), 'game');
                await mkdir(folder, { recursive: true });
                for (const file of files) {
                    const path = join(folder, file.path);
                    await mkdir(resolve(path, '..'), { recursive: true });
                    await writeFile(path, file.content);
                }
                for (const asset of usedAssets) {
                    const path = join(folder, assetBuildPath(asset));
                    await mkdir(resolve(path, '..'), { recursive: true });
                    await copyFile(asset.filePath, path);
                }
                const htmlPath = join(folder, 'build', 'game.html');
                await mkdir(join(folder, 'build'), { recursive: true });
                const started = performance.now();
                try {
                    await run(tideBin, ['build', folder, '--web', '--release', '-o', htmlPath], { cwd: folder });
                    const html = await readFile(htmlPath);
                    if (!html.includes(Buffer.from('TIDE_PROGRAM'))) throw new Error('The build did not produce a Tide web page.');
                    await recordResource(context.jobId, { kind: 'build', durationMs: Math.round(performance.now() - started), attempt: attempt + 1 });
                    return { htmlPath, sourceFiles: files, summary: result.summary, toolchain,
                        artifactSha256: createHash('sha256').update(html).digest('hex') };
                } catch (error) {
                    await recordResource(context.jobId, { kind: 'build', durationMs: Math.round(performance.now() - started), attempt: attempt + 1, failed: true });
                    if (!initial || attempt + 1 === maxAttempts) throw error;
                    result = await generate(initial, { files, error: error.message.slice(-16000) });
                }
            }
        } finally { contexts.delete(context.jobId); }
    } : null;
    return { generator, builder, toolchain };
}

export function modelUsage(body, { model, rates, durationMs = 0 } = {}) {
    if (!body.usage) return null;
    const inputTokens = body.usage.input_tokens || 0;
    const cachedInputTokens = body.usage.input_tokens_details?.cached_tokens || 0;
    const outputTokens = body.usage.output_tokens || 0;
    const validRates = rates && ['input', 'cachedInput', 'output'].every(key => typeof rates[key] === 'number' && Number.isFinite(rates[key]) && rates[key] >= 0);
    return { responseId: body.id, model: body.model || model, inputTokens, cachedInputTokens, outputTokens,
        totalTokens: body.usage.total_tokens ?? inputTokens + outputTokens, durationMs,
        estimatedUsd: validRates ? ((inputTokens - cachedInputTokens) * rates.input + cachedInputTokens * rates.cachedInput + outputTokens * rates.output) / 1_000_000 : null,
        rates: validRates ? { ...rates } : null };
}
