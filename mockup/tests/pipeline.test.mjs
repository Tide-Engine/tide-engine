import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createPipeline, validateFiles, modelUsage } from '../runtime/pipeline.mjs';

test('usage separates cached input and keeps unknown prices unavailable', () => {
    const body = { id: 'resp_1', model: 'configured-model', usage: { input_tokens: 1000, input_tokens_details: { cached_tokens: 600 }, output_tokens: 200, total_tokens: 1200 } };
    const usage = modelUsage(body, { rates: { input: 10, cachedInput: 1, output: 50 } });
    assert.equal(usage.estimatedUsd, 0.0146);
    assert.equal(usage.totalTokens, 1200);
    assert.equal(modelUsage(body).estimatedUsd, null);
    assert.equal(modelUsage({}), null);
});

test('generated source cannot escape its folder or introduce native code', () => {
    for (const path of ['../game.tide', 'C:/game.tide', 'game.c', '.secret.tide', 'game.tide/../x.tide']) {
        assert.throws(() => validateFiles([{ path, content: '' }]));
    }
    assert.throws(() => validateFiles([{ path: 'game.tide', content: 'extern void bad();' }]));
    assert.throws(() => validateFiles([{ path: 'game.tide', content: 'asset Text a = "https://example.com";' }]));
    assert.throws(() => validateFiles([{ path: 'GAME.tide', content: '' }, { path: 'game.tide', content: '' }]));
});

test('compile repair produces a new source set and accounts every model/build attempt', async t => {
    const directory = await mkdtemp(join(tmpdir(), 'mockup-pipeline-'));
    t.after(() => rm(directory, { recursive: true, force: true }));
    let responses = 0, builds = 0;
    const usage = [], resources = [], requests = [];
    const pipeline = await createPipeline({ dataDir: directory, apiKey: 'test-only', model: 'configured-model',
        recordUsage: async (job, value) => usage.push({ job, ...value }), recordResource: async (job, value) => resources.push({ job, ...value }),
        request: async (url, init) => {
            requests.push(JSON.parse(init.body));
            return Response.json({ id: `response_${++responses}`, status: 'completed', usage: { input_tokens: 100, output_tokens: 20 },
                output: [{ content: [{ type: 'output_text', text: JSON.stringify({ summary: 'Changed movement', files: [{ path: 'game.tide', content: `// attempt ${responses}` }] }) }] }] });
        },
        run: async (command, args) => {
            if (args[0] === 'version') return 'tide-test';
            if (++builds === 1) throw new Error('Unknown identifier: fixed in next attempt');
            await writeFile(args.at(-1), '<html>TIDE_PROGRAM</html>');
            return '';
        },
    });
    const context = { jobId: 'job1', project: { title: 'Test', idea: 'A garden' }, prompt: 'Improve movement', polish: 'polished', messages: [], assets: [], feedback: [], baseRevision: null };
    const generated = await pipeline.generator(context);
    const result = await pipeline.builder(generated, context);
    assert.equal(result.sourceFiles[0].content, '// attempt 2');
    assert.equal(usage.length, 2);
    assert.equal(resources.length, 2);
    assert.equal(resources[0].failed, true);
    assert.equal(requests[0].store, false);
    assert.equal(requests[0].text.format.strict, true);
    assert.match(requests[1].input[0].content[0].text, /Unknown identifier/);
});

test('a charged but incomplete response is accounted before generation fails', async () => {
    const usage = [];
    const pipeline = await createPipeline({ dataDir: '.', apiKey: 'test-only', recordUsage: async (job, record) => usage.push(record),
        run: async () => 'tide-test', request: async () => Response.json({ id: 'incomplete1', status: 'incomplete', usage: { input_tokens: 50, output_tokens: 10 } }) });
    await assert.rejects(pipeline.generator({ jobId: 'job', project: {}, messages: [], assets: [], feedback: [] }), /did not finish/);
    assert.equal(usage[0].totalTokens, 60);
});

test('a generated asset traversal is rejected before the compiler or filesystem staging runs', async t => {
    const directory = await mkdtemp(join(tmpdir(), 'mockup-pipeline-confinement-'));
    t.after(() => rm(directory, { recursive: true, force: true }));
    const commands = [], usage = [], resources = [];
    const pipeline = await createPipeline({ dataDir: directory, apiKey: 'test-only',
        recordUsage: async (_jobId, record) => usage.push(record),
        recordResource: async (_jobId, record) => resources.push(record),
        run: async (_command, args) => { commands.push(args[0]); return 'tide-test'; },
        request: async () => Response.json({ id: 'response-traversal', status: 'completed', usage: { input_tokens: 20, output_tokens: 15 },
            output: [{ content: [{ type: 'output_text', text: JSON.stringify({ summary: 'Attempted unsafe import', files: [{ path: 'game.tide', content: 'asset List<byte> secret = "../../../../.env";' }] }) }] }] }),
    });
    const context = { jobId: 'unsafe-job', project: { title: 'Test', idea: 'An orchard' }, prompt: 'Make a game', polish: 'polished', messages: [], assets: [], feedback: [], baseRevision: null };
    const generated = await pipeline.generator(context);
    await assert.rejects(pipeline.builder(generated, context), /not an exact uploaded game asset/);
    assert.deepEqual(commands, ['version']);
    assert.equal(usage.length, 1); // The actual model response remains accounted.
    assert.equal(resources.length, 0); // No compiler was run or measured.
    const { readdir } = await import('node:fs/promises');
    assert.deepEqual(await readdir(directory), []);
});
