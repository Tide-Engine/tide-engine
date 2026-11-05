// The WebGPU a web test's browser draws with, shared by the runners that start
// one (run_web_served_test.mjs, platform/tests/web_rooms.mjs and
// web_threads.mjs, docs/tests/examples.mjs).
//
// By default it's the browser's software WebGPU (SwiftShader), so no GPU is
// needed. With TIDE_WEB_HARDWARE=1 it's the machine's own GPU (Dawn on D3D12
// on Windows, Metal on macOS, Vulkan on Linux), still headless and muted, and
// a page that gets a software adapter instead fails the test: the hardware
// lane (notes/testing.md, The hardware web lane). TIDE_WEB_HARDWARE=d3d11 is
// that on Dawn's Direct3D 11 adapter, which some Windows machines get instead of
// D3D12 (its shaders go through FXC): a page whose adapter says another backend
// fails. TIDE_WEBGPU_FLAGS replaces the flags in either mode.
//
// Which adapter the browser gives is asked before the test's page: the browser
// is sent to `probePath`, a page that asks for an adapter as tide.js does
// (`adapterExpression`), posts what it got back and goes on to the test's own
// page (`probeAddress`). A runner that drives its pages over DevTools
// (examples.mjs) evaluates `adapterExpression` in its first page instead.

export const hardware = process.env.TIDE_WEB_HARDWARE === '1' || process.env.TIDE_WEB_HARDWARE === 'd3d11';
// The backend the hardware lane asks for, where it asks for one: 'D3D11', as adapters say it
export const backend = process.env.TIDE_WEB_HARDWARE === 'd3d11' ? 'D3D11' : null;

export function gpuFlags() {
    let flags;
    if (hardware) {
        // --enable-unsafe-webgpu: the adapter's full description, and the features the software runs get
        // --ignore-gpu-blocklist: a machine kept for this lane is to draw on its GPU, whatever Chrome thinks of it
        // --use-webgpu-power-preference: a machine with two GPUs draws on its discrete one
        // --disable-gpu-watchdog: as in software, a long frame of a test doesn't get the GPU process killed
        // --enable-webgpu-developer-features: the adapter's full description (device, driver) for the log
        flags = ['--enable-unsafe-webgpu', '--enable-webgpu-developer-features', '--ignore-gpu-blocklist',
            '--use-webgpu-power-preference=default-high-performance', '--disable-gpu-watchdog'];
        if (backend === 'D3D11') flags.push('--use-webgpu-adapter=d3d11');
    } else {
        // WebGPU in software, and the browser's own drawing where it can take WebGPU's images: on Windows
        // through WARP (with SwiftShader's GL, Chrome has no WebGPU at all). On Linux, SwiftShader's GL draws
        // WebGPU but the page's canvas never shows it, so pixels read back are zeros: CI's Linux runners leave
        // the `pixels` tests out. SwiftShader's Vulkan shows them under WSL, but gives no WebGPU adapter at all
        // on GitHub's runners, so it's asked for with TIDE_WEB_LINUX_VULKAN=1.
        // --disable-gpu-watchdog: software WebGPU's slow frames on a small runner otherwise get the GPU
        // process killed ("A valid external Instance reference no longer exists")
        flags = ['--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader', '--disable-gpu-watchdog'];
        if (process.platform === 'win32') flags.push('--use-angle=d3d11-warp');
        if (process.platform === 'linux' && process.env.TIDE_WEB_LINUX_VULKAN === '1') {
            flags.push('--use-angle=vulkan', '--use-vulkan=swiftshader', '--enable-features=Vulkan');
        }
    }
    if (process.env.TIDE_WEBGPU_FLAGS) flags = process.env.TIDE_WEBGPU_FLAGS.split(' ').filter(Boolean);
    return flags;
}

export const probePath = '/tide-adapter';

// The address that asks for the adapter, then goes to `next` (a path on the same server, with its query)
export const probeAddress = next => `${probePath}?next=${encodeURIComponent(next)}`;

// What a page's adapter is, as an expression that gives it (a promise): navigator.gpu.requestAdapter(), as tide.js
// asks, and nothing more
export const adapterExpression = `(async () => {
    try {
        const adapter = navigator.gpu ? await navigator.gpu.requestAdapter() : null;
        if (!navigator.gpu) return { error: 'no navigator.gpu' };
        if (!adapter) return { error: 'the browser gave no GPU adapter' };
        const about = adapter.info || {};
        return { vendor: about.vendor || '', architecture: about.architecture || '', device: about.device || '',
            description: about.description || '', backend: about.backend || '',
            fallback: !!(about.isFallbackAdapter ?? adapter.isFallbackAdapter) };
    } catch (error) {
        return { error: String(error && error.message || error) };
    }
})()`;

const probePage = `<!doctype html>
<meta charset="utf-8">
<title>Tide: the browser's WebGPU adapter</title>
<script>
(async () => {
    const info = await ${adapterExpression};
    try {
        await fetch('${probePath}', { method: 'POST', body: JSON.stringify(info) }).then(answer => answer.arrayBuffer());
    } catch {}
    location.replace(new URLSearchParams(location.search).get('next') || '/');
})();
</script>`;

const body = request => new Promise(done => {
    let text = '';
    request.on('data', chunk => { text += chunk; });
    request.on('end', () => done(text));
});

// Answers the probe's two requests, calling `heard(info)` with what the page got; false for any other request
export function probeRoute(request, response, heard) {
    if (request.url.split('?')[0] !== probePath) return false;
    if (request.method === 'POST') {
        body(request).then(text => {
            let info;
            try {
                info = JSON.parse(text);
            } catch {
                info = { error: `the probe sent what can't be read: ${text.slice(0, 200)}` };
            }
            response.end();
            heard(info);
        });
    } else {
        response.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
        response.end(probePage);
    }
    return true;
}

export const describeAdapter = info => info.error ? info.error
    : `${[info.vendor, info.architecture, info.device, info.description].filter(Boolean).join(', ') || 'no description'}`
        + (info.backend ? ` (${info.backend})` : '')
        + (info.fallback ? ' (a fallback adapter)' : '');

// Why an adapter isn't the machine's GPU, or null when it is
export function notHardware(info) {
    if (info.error) return info.error;
    if (info.fallback) return 'the browser gave its fallback adapter';
    const said = [info.vendor, info.architecture, info.device, info.description].join(' ');
    if (/swiftshader|llvmpipe|lavapipe|softpipe|warp|basic render|software/i.test(said)) return `a software adapter (${said.trim()})`;
    // The adapter says its backend with --enable-webgpu-developer-features, which the hardware lane gives
    if (backend && info.backend !== backend) return `its backend is ${info.backend || 'unsaid'}, not ${backend}`;
    return null;
}

// What a runner says once the adapter is known, and whether the test goes on: in the hardware lane a software
// adapter fails it
export function adapterVerdict(info) {
    const line = `WebGPU adapter: ${describeAdapter(info)}`;
    if (!hardware) return { line, failure: null };
    const why = notHardware(info);
    return { line, failure: why ? `TIDE_WEB_HARDWARE=${process.env.TIDE_WEB_HARDWARE}, but the browser didn't give the machine's GPU`
        + `${backend ? ` on ${backend}` : ''}: ${why}` : null };
}
